#include "PickpocketInventory.h"
#include "Config.h"
#include "PickpocketHooks.h"
#include "StringUtils.h"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <mutex>
#include <string_view>

namespace PickpocketInventory {
    namespace {
        std::mutex stateMutex;
        Snapshot currentSnapshot;

        struct ItemAccess {
            bool minigameEligible = false;
            std::string blockedReason;
        };

        struct LiveInventoryEntry {
            RE::InventoryEntryData* entry = nullptr;
            RE::TESBoundObject* object = nullptr;
            std::int32_t count = 0;
        };

        bool IsMissingName(std::string_view name) {
            return name.empty() || name == "<Missing Name>";
        }

        std::string GetEntryName(RE::TESBoundObject& object, RE::InventoryEntryData* entry) {
            if (entry) {
                if (const auto name = entry->GetDisplayName(); name && !IsMissingName(name)) {
                    return name;
                }
            }

            if (const auto name = object.GetName(); name && !IsMissingName(name)) {
                return name;
            }

            return {};
        }

        using StringUtils::ToLowerAscii;

        bool NameEqualsAny(std::string_view name, const std::initializer_list<std::string_view> candidates) {
            const auto loweredName = ToLowerAscii(name);
            return std::ranges::any_of(
                candidates,
                [&loweredName](const auto candidate) {
                    return loweredName == candidate;
                });
        }

        bool NameContainsAny(std::string_view name, const std::initializer_list<std::string_view> needles) {
            const auto loweredName = ToLowerAscii(name);
            return std::ranges::any_of(
                needles,
                [&loweredName](const auto needle) {
                    return loweredName.find(needle) != std::string::npos;
                });
        }

        bool HasAnyKeyword(RE::TESBoundObject& object, const std::initializer_list<std::string_view> keywordEditorIDs) {
            std::vector<RE::BGSKeyword*> keywords;
            keywords.reserve(keywordEditorIDs.size());

            for (const auto editorID : keywordEditorIDs) {
                if (auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID)) {
                    keywords.push_back(keyword);
                }
            }

            return !keywords.empty() && object.HasKeywordInArray(keywords, false);
        }

        bool IsKeyObject(RE::TESBoundObject& object) {
            return object.Is(RE::FormType::KeyMaster);
        }

        bool IsGoldObject(RE::TESBoundObject& object, std::string_view name) {
            constexpr RE::FormID kGold001 = 0x0000000F;
            return object.GetFormID() == kGold001 || NameEqualsAny(name, { "gold", "septim", "septims" });
        }

        bool IsJewelryObject(RE::TESBoundObject& object) {
            return HasAnyKeyword(
                object,
                { "VendorItemJewelry", "ArmorJewelry", "ClothingRing", "ClothingNecklace", "ClothingCirclet" });
        }

        bool IsGemObject(RE::TESBoundObject& object, std::string_view name) {
            if (HasAnyKeyword(object, { "VendorItemGem" })) {
                return true;
            }

            const auto loweredName = ToLowerAscii(name);
            if (loweredName.find("soul gem") != std::string::npos) {
                return false;
            }

            return NameContainsAny(name, { "ruby", "sapphire", "emerald", "diamond", "garnet", "amethyst" });
        }

        RE::NiPointer<RE::Actor> ResolveTargetActor(RE::ObjectRefHandle targetHandle) {
            auto targetReference = targetHandle.get();
            if (!targetReference) {
                return {};
            }

            return RE::NiPointer<RE::Actor>(targetReference->As<RE::Actor>());
        }

        bool HasEquippedItemPickpocketEntry(RE::Actor& actor) {
            return actor.HasPerkEntries(RE::BGSEntryPoint::ENTRY_POINTS::kCanPickpocketEquippedItem);
        }

        bool CanPickpocketEquippedItem(RE::Actor& player, RE::Actor& target, RE::InventoryEntryData& entry) {
            if (!entry.object || !HasEquippedItemPickpocketEntry(player)) {
                return false;
            }

            float canPickpocketEquippedItem = 0.0f;
            RE::BGSEntryPoint::HandleEntryPoint(
                RE::BGSEntryPoint::ENTRY_POINTS::kCanPickpocketEquippedItem,
                &player,
                entry.object,
                &target,
                &canPickpocketEquippedItem);

            return canPickpocketEquippedItem != 0.0f;
        }

        RE::InventoryEntryData* FindInventoryEntryByFormID(RE::Actor& actor, RE::FormID itemFormID) {
            const auto itemCount = actor.GetInventoryItemCount(false, false);
            for (std::int32_t index = 0; index < itemCount; ++index) {
                auto* entry = actor.GetInventoryItemAt(index, false);
                if (!entry || !entry->object || entry->countDelta <= 0) {
                    continue;
                }

                if (entry->object->GetFormID() == itemFormID) {
                    return entry;
                }
            }

            return nullptr;
        }

        LiveInventoryEntry GetLiveInventoryEntry(RE::Actor& actor, RE::FormID itemFormID) {
            auto* entry = FindInventoryEntryByFormID(actor, itemFormID);
            if (!entry || !entry->object) {
                return {};
            }

            return { entry, entry->object, entry->countDelta };
        }

        std::int32_t GetInventoryCountByFormID(RE::Actor& actor, RE::FormID itemFormID) {
            auto* entry = FindInventoryEntryByFormID(actor, itemFormID);
            return entry ? entry->countDelta : 0;
        }

        bool CanUseAsFailureAttemptProbe(RE::InventoryEntryData& entry) {
            return entry.object && !entry.IsWorn() && !entry.IsFavorited() && !entry.IsQuestObject();
        }

        LiveInventoryEntry GetPickpocketFailureAttemptEntry(RE::Actor& actor) {
            const auto itemCount = actor.GetInventoryItemCount(false, false);
            for (std::int32_t index = 0; index < itemCount; ++index) {
                auto* inventoryEntry = actor.GetInventoryItemAt(index, false);
                if (!inventoryEntry || !inventoryEntry->object || inventoryEntry->countDelta <= 0) {
                    continue;
                }

                if (!CanUseAsFailureAttemptProbe(*inventoryEntry)) {
                    continue;
                }

                return { inventoryEntry, inventoryEntry->object, inventoryEntry->countDelta };
            }

            return {};
        }

        ItemAccess EvaluateItemAccess(RE::InventoryEntryData& entry, RE::Actor* player, RE::Actor& target) {
            // Favorites never block; quest status is a worn-item exception.
            // Radiant pickpocket quests can assign jewelry that the target equips.
            // Keep those quests completable without requiring the Perfect Touch perk.
            ItemAccess access;
            access.minigameEligible =
                !entry.IsWorn() ||
                entry.IsQuestObject() ||
                (player && CanPickpocketEquippedItem(*player, target, entry));
            if (!access.minigameEligible) {
                access.blockedReason = "Worn item needs equipped-item pickpocket perk";
            }

            return access;
        }

        TransferResult TransferItemStack(
            RE::Actor& targetActor,
            RE::PlayerCharacter& player,
            RE::FormID itemFormID,
            bool ignoreMinigameEligibility) {
            const auto liveItem = GetLiveInventoryEntry(targetActor, itemFormID);
            if (!liveItem.entry || !liveItem.object) {
                return { false, "Item is no longer in the target inventory." };
            }

            if (liveItem.count <= 0) {
                return { false, "Item is no longer available." };
            }

            const auto access = EvaluateItemAccess(*liveItem.entry, &player, targetActor);
            if (!ignoreMinigameEligibility && !access.minigameEligible) {
                return { false, std::format("Item is blocked: {}", access.blockedReason) };
            }

            const auto itemName = GetEntryName(*liveItem.object, liveItem.entry);
            const auto transferCount = liveItem.count;
            bool vanillaAttemptSucceeded = false;
            {
                PickpocketHooks::ScopedForcePickpocketSuccess forceSuccess(player, targetActor);
                vanillaAttemptSucceeded = player.AttemptPickpocket(&targetActor, liveItem.entry, transferCount, true);
            }

            const auto remainingAfterVanillaAttempt = GetInventoryCountByFormID(targetActor, itemFormID);
            std::int32_t explicitlyTransferredCount = 0;
            if (remainingAfterVanillaAttempt > 0) {
                targetActor.RemoveItem(
                    liveItem.object,
                    remainingAfterVanillaAttempt,
                    RE::ITEM_REMOVE_REASON::kSteal,
                    nullptr,
                    &player);
                explicitlyTransferredCount = remainingAfterVanillaAttempt;
            }

            if (!vanillaAttemptSucceeded && explicitlyTransferredCount <= 0) {
                return { false, std::format("Vanilla pickpocket transfer failed for {}.", itemName) };
            }

            return { true, {} };
        }

    }

    bool HasRenderableModel(RE::TESBoundObject* base) {
        if (!base) {
            return false;
        }

        if (auto weapon = base->As<RE::TESObjectWEAP>()) {
            if (auto firstPersonModel = weapon->firstPersonModelObject) {
                const char* path = firstPersonModel->GetModel();
                if (path && path[0]) {
                    return true;
                }
            }
        }

        if (auto model = base->As<RE::TESModel>()) {
            const char* path = model->GetModel();
            if (path && path[0]) {
                return true;
            }
        }

        if (auto biped = base->As<RE::TESBipedModelForm>()) {
            auto player = RE::PlayerCharacter::GetSingleton();
            auto playerBase = player ? player->GetActorBase() : nullptr;
            if (playerBase) {
                const char* path = biped->worldModels[playerBase->GetSex()].GetModel();
                if (path && path[0]) {
                    return true;
                }
            }
        }

        if (auto spell = base->As<RE::SpellItem>()) {
            if (auto displayObject = spell->GetMenuDisplayObject()) {
                if (auto model = displayObject->As<RE::TESModel>()) {
                    const char* path = model->GetModel();
                    if (path && path[0]) {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    void CaptureFromActor(RE::Actor& actor) {
        Snapshot next;
        next.targetHandle = actor.GetHandle();
        next.targetFormID = actor.GetFormID();
        if (const auto targetName = actor.GetDisplayFullName(); targetName && targetName[0] != '\0') {
            next.targetName = targetName;
        }
        if (next.targetName.empty()) {
            next.targetName = std::format("Actor {:08X}", next.targetFormID);
        }

        auto* player = RE::PlayerCharacter::GetSingleton();

        const auto itemCount = actor.GetInventoryItemCount(false, false);
        next.items.reserve(static_cast<std::size_t>(std::max(itemCount, 0)));

        for (std::int32_t index = 0; index < itemCount; ++index) {
            auto* entry = actor.GetInventoryItemAt(index, false);
            if (!entry || !entry->object) {
                continue;
            }

            auto* object = entry->object;
            const auto count = entry->countDelta;
            if (count <= 0) {
                continue;
            }

            auto name = GetEntryName(*object, entry);
            if (name.empty()) {
                continue;
            }

            if (!PickpocketConfig::UsingSkyUIIcons() &&
                !HasRenderableModel(object)) {
                continue;
            }

            Item item;
            item.formID = object->GetFormID();
            item.object = object;
            item.name = std::move(name);
            item.count = count;
            item.value = entry->GetValue();
            item.weight = entry->GetWeight();
            item.stealValue = actor.GetStealValue(entry, count, true);
            item.isKey = IsKeyObject(*object);
            item.isGold = IsGoldObject(*object, item.name);
            item.isJewelry = IsJewelryObject(*object);
            item.isGem = IsGemObject(*object, item.name);
            item.worn = entry->IsWorn();
            item.favorited = entry->IsFavorited();
            item.questItem = entry->IsQuestObject();

            const auto access = EvaluateItemAccess(*entry, player, actor);
            item.minigameEligible = access.minigameEligible;
            item.blockedReason = access.blockedReason;

            next.items.push_back(std::move(item));
        }

        {
            std::lock_guard lock(stateMutex);
            next.revision = currentSnapshot.revision + 1;
            currentSnapshot = std::move(next);
        }
    }

    Snapshot GetSnapshot() {
        std::lock_guard lock(stateMutex);
        return currentSnapshot;
    }

    void TransferItemStacks(std::span<const RE::FormID> itemFormIDs, bool ignoreMinigameEligibility) {
        RE::ObjectRefHandle targetHandle;
        {
            std::lock_guard lock(stateMutex);
            targetHandle = currentSnapshot.targetHandle;
        }

        const auto targetActor = ResolveTargetActor(targetHandle);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!targetActor || !player) {
            SKSE::log::warn("Cannot transfer marked items: pickpocket target or player is unavailable.");
            return;
        }

        for (const auto formID : itemFormIDs) {
            const auto result = TransferItemStack(*targetActor, *player, formID, ignoreMinigameEligibility);
            if (!result.succeeded) {
                SKSE::log::warn("Failed to transfer marked item {:08X}: {}", formID, result.message);
            }
        }
        CaptureFromActor(*targetActor);
    }

    TransferResult TriggerPickpocketFailure() {
        RE::ObjectRefHandle targetHandle;
        {
            std::lock_guard lock(stateMutex);
            targetHandle = currentSnapshot.targetHandle;
        }

        auto targetActor = ResolveTargetActor(targetHandle);
        if (!targetActor) {
            return { false, "Pickpocket target is no longer available." };
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return { false, "Player character is not available." };
        }

        const auto attemptItem = GetPickpocketFailureAttemptEntry(*targetActor);
        if (!attemptItem.entry || !attemptItem.object) {
            return { true, "Pickpocket failed, but target inventory had no safe item for a vanilla failure probe." };
        }

        {
            PickpocketHooks::ScopedForcePickpocketFailure forceFailure(*player, *targetActor);
            player->AttemptPickpocket(targetActor.get(), attemptItem.entry, 1, true);
        }

        CaptureFromActor(*targetActor);
        return { true, "Pickpocket failed; triggered vanilla pickpocket attempt." };
    }

}
