#include "UIInternal.h"
#include "PickpocketEventHandler.h"
#include "PickpocketRevealRules.h"
#include "PickpocketSession.h"
#include "Config.h"
#include "SkyUIIcons.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace ImGuiMCP;
namespace ImGui = ImGuiMCP;
namespace logger = SKSE::log;

namespace UI::Internal {
    float GetDialRevealProgress(std::size_t slotIndex);
    MeshRenderingFrameworkAPI::Mesh* GetInventoryPreview(const PickpocketInventory::Item& item);

    struct DialSlotRevealState {
        RE::FormID formID = 0;
        float progress = 0.0f;
        bool forceRevealed = false;
    };

    struct DialLayout {
        std::array<const PickpocketInventory::Item*, kDialSlotCount> itemsBySlot{};
    };

    namespace {
        struct InventoryPreviewDescriptor {
            std::uint64_t snapshotRevision = 0;
            RE::RefHandle targetHandle = 0;
            std::vector<RE::FormID> formIDs;

            friend bool operator==(const InventoryPreviewDescriptor&, const InventoryPreviewDescriptor&) = default;
        };

        struct InventoryPreviewSource {
            RE::FormID formID = 0;
            RE::TESBoundObject* object = nullptr;
        };

        struct InventoryPreviewEntry {
            RE::TESBoundObject* object = nullptr;
            std::unique_ptr<MeshRenderingFrameworkAPI::Mesh> mesh;
        };

        using InventoryPreviewMap = std::unordered_map<RE::FormID, InventoryPreviewEntry>;

        InventoryPreviewMap inventoryPreviews;
        std::mutex inventoryPreviewMutex;
        InventoryPreviewDescriptor inventoryPreviewDescriptor;
        std::vector<InventoryPreviewSource> inventoryPreviewSources;
        InventoryPreviewPreparationStatus inventoryPreviewStatus = InventoryPreviewPreparationStatus::Idle;
        std::uint64_t inventoryPreviewGeneration = 0;
        std::size_t inventoryPreviewNextSource = 0;
        std::unordered_set<RE::FormID> markedInventoryItems;
        int targetDialSlot = kDialExitSlot;
        int queuedDialMove = 0;
        float cursorDialPosition = static_cast<float>(kDialExitSlot);
        std::array<DialSlotRevealState, kDialSlotCount> dialRevealStates;
        std::uint64_t dialRevealRevision = 0;
        RE::FormID dialTimerTargetFormID = 0;
        float dialTimerRemainingSeconds = kDialTimerDurationSeconds;
        float dialTimerPenaltyFlashSeconds = 0.0f;
        float dialTimerPenaltyFlashFromFraction = 0.0f;
        float dialTimerPenaltyFlashToFraction = 0.0f;

        std::atomic<float> controllerStickX{ 0.0f };
        std::atomic<float> controllerStickY{ 0.0f };
        std::atomic_bool controllerMoveLeftDown{ false };
        std::atomic_bool controllerMoveRightDown{ false };
        std::atomic_bool controllerConfirmDown{ false };
        std::atomic_uint32_t controllerConfirmButtonID{ 0 };
        std::atomic_uint32_t pendingControllerActions{ 0 };
        bool takeSelectedInputWasControllerConfirm = false;
        bool exitRequestedByControllerConfirm = false;

        constexpr float kControllerNavigationDeadzone = 0.5f;

        constexpr std::uint32_t GetControllerActionMask(Input::MenuAction action) {
            return 1u << static_cast<std::uint32_t>(action);
        }

        void QueueControllerAction(Input::MenuAction action) {
            pendingControllerActions.fetch_or(GetControllerActionMask(action), std::memory_order_release);
        }

        void ResetControllerInput() {
            controllerStickX.store(0.0f, std::memory_order_release);
            controllerStickY.store(0.0f, std::memory_order_release);
            controllerMoveLeftDown.store(false, std::memory_order_release);
            controllerMoveRightDown.store(false, std::memory_order_release);
            controllerConfirmDown.store(false, std::memory_order_release);
            controllerConfirmButtonID.store(0, std::memory_order_release);
            pendingControllerActions.store(0, std::memory_order_release);
            takeSelectedInputWasControllerConfirm = false;
            exitRequestedByControllerConfirm = false;
        }

    }

    bool ConsumeControllerAction(Input::MenuAction action) {
        const auto mask = GetControllerActionMask(action);
        return (pendingControllerActions.fetch_and(~mask, std::memory_order_acq_rel) & mask) != 0;
    }

    namespace {
        void ClosePickpocketPresentation() {
            markedInventoryItems.clear();
            ResetControllerInput();
            SkyUIIcons::EndSession();
            CancelInventoryPreviewPreparation();
            if (pickpocketInventoryWindow) {
                pickpocketInventoryWindow->IsOpen = false;
            }
        }
    }

    void ClosePickpocketInventoryWindow() {
        const auto session = PickpocketSession::GetSnapshot();
        if (!PickpocketSession::BeginSafeClose(session.targetHandle, "phase 2 closed without taking items")) {
            return;
        }

        ClosePickpocketPresentation();
        (void)PickpocketSession::FinishTerminal("phase 2 closed");
    }

    void FailPickpocketInventoryAndClose(const char* reason) {
        const auto session = PickpocketSession::GetSnapshot();
        if (!PickpocketSession::BeginFailure(session.targetHandle, reason)) {
            return;
        }

        const auto result = PickpocketInventory::TriggerPickpocketFailure();
        if (!result.succeeded) {
            logger::warn("Pickpocket minigame failed ({}) but could not trigger vanilla alarm: {}", reason, result.message);
        }

        ClosePickpocketPresentation();
        (void)PickpocketSession::FinishTerminal("phase 2 failure resolved");
    }

    bool OpenVanillaDepositMenuAndClose(const PickpocketInventory::Snapshot& snapshot) {
        const auto targetHandle = snapshot.targetHandle.native_handle();
        if (!PickpocketSession::BeginDeposit(targetHandle)) {
            return false;
        }

        // End icon probing before requesting ContainerMenu so its open event
        // cannot be claimed as another SkyUI descriptor probe.
        ClosePickpocketPresentation();

        const auto opened = PickpocketEvents::OpenVanillaPickpocketMenu(snapshot.targetHandle);
        if (!opened) {
            PickpocketEvents::CancelVanillaPickpocketMenuRequest();
            PickpocketSession::Abort("vanilla deposit menu could not be requested");
        }
        return opened;
    }

    bool IsMeshRenderingFrameworkLoaded() {
        static const bool loaded = GetModuleHandleW(L"MeshRenderingFramework") != nullptr;
        return loaded;
    }

    void __stdcall ProcessInventoryPreviewRenderTask() {
        std::uint64_t generation = 0;
        std::vector<std::unique_ptr<MeshRenderingFrameworkAPI::Mesh>> retiredMeshes;
        InventoryPreviewSource source;
        bool prepareSource = false;
        {
            std::scoped_lock lock(inventoryPreviewMutex);
            generation = inventoryPreviewGeneration;
            if (inventoryPreviewStatus == InventoryPreviewPreparationStatus::Preparing) {
                while (inventoryPreviewNextSource < inventoryPreviewSources.size()) {
                    const auto& candidate = inventoryPreviewSources[inventoryPreviewNextSource];
                    const auto cached = inventoryPreviews.find(candidate.formID);
                    if (cached == inventoryPreviews.end() ||
                        cached->second.object != candidate.object ||
                        !cached->second.mesh ||
                        !cached->second.mesh->GetResourceView()) {
                        source = candidate;
                        prepareSource = true;
                        break;
                    }
                    ++inventoryPreviewNextSource;
                }

                if (!prepareSource) {
                    inventoryPreviewStatus = InventoryPreviewPreparationStatus::Ready;
                    for (auto preview = inventoryPreviews.begin(); preview != inventoryPreviews.end();) {
                        const auto active = std::ranges::find(
                            inventoryPreviewSources,
                            preview->first,
                            &InventoryPreviewSource::formID);
                        if (active != inventoryPreviewSources.end() &&
                            active->object == preview->second.object) {
                            ++preview;
                            continue;
                        }

                        retiredMeshes.push_back(std::move(preview->second.mesh));
                        preview = inventoryPreviews.erase(preview);
                    }
                }
            }
        }

        if (!prepareSource) {
            return;
        }

        auto preview = std::make_unique<MeshRenderingFrameworkAPI::Mesh>(
            source.object,
            kInventoryPreviewRenderSize,
            kInventoryPreviewRenderSize);
        const auto prepared = preview->GetResourceView() != nullptr;
        bool preparationFailed = false;
        {
            std::scoped_lock lock(inventoryPreviewMutex);
            if (inventoryPreviewGeneration != generation ||
                inventoryPreviewStatus != InventoryPreviewPreparationStatus::Preparing) {
                return;
            }

            if (!prepared) {
                inventoryPreviewStatus = InventoryPreviewPreparationStatus::Failed;
                preparationFailed = true;
            } else {
                auto cached = inventoryPreviews.find(source.formID);
                if (cached != inventoryPreviews.end()) {
                    retiredMeshes.push_back(std::move(cached->second.mesh));
                    cached->second = { source.object, std::move(preview) };
                } else {
                    inventoryPreviews.emplace(
                        source.formID,
                        InventoryPreviewEntry{ source.object, std::move(preview) });
                }
                ++inventoryPreviewNextSource;
            }
        }

        if (preparationFailed) {
            logger::error("MRF failed to prepare inventory mesh {:08X}", source.formID);
        }
    }

    void PrepareInventoryPreviews(
        const PickpocketInventory::Snapshot& snapshot,
        std::span<const RE::FormID> previewFormIDs) {
        InventoryPreviewDescriptor descriptor;
        descriptor.snapshotRevision = snapshot.revision;
        descriptor.targetHandle = snapshot.targetHandle.native_handle();

        std::vector<InventoryPreviewSource> sources;
        sources.reserve(previewFormIDs.size());
        descriptor.formIDs.reserve(previewFormIDs.size());
        std::unordered_set<RE::FormID> uniqueFormIDs;
        uniqueFormIDs.reserve(previewFormIDs.size());
        for (const auto formID : previewFormIDs) {
            if (formID == 0 || !uniqueFormIDs.insert(formID).second) {
                continue;
            }

            const auto item = std::ranges::find(snapshot.items, formID, &PickpocketInventory::Item::formID);
            if (item == snapshot.items.end() || !item->object) {
                continue;
            }

            descriptor.formIDs.push_back(formID);
            sources.push_back({ formID, item->object });
        }

        const auto mrfLoaded = IsMeshRenderingFrameworkLoaded();
        {
            std::scoped_lock lock(inventoryPreviewMutex);
            if (inventoryPreviewDescriptor == descriptor &&
                (inventoryPreviewStatus == InventoryPreviewPreparationStatus::Preparing ||
                 inventoryPreviewStatus == InventoryPreviewPreparationStatus::Ready)) {
                return;
            }

            ++inventoryPreviewGeneration;
            inventoryPreviewDescriptor = descriptor;
            inventoryPreviewSources = std::move(sources);
            inventoryPreviewNextSource = 0;
            inventoryPreviewStatus = mrfLoaded ?
                InventoryPreviewPreparationStatus::Preparing :
                InventoryPreviewPreparationStatus::Failed;
        }

        if (!mrfLoaded) {
            logger::error("Cannot prepare inventory meshes: Mesh Rendering Framework is not loaded");
        }

    }

    // End the active request without destroying completed previews. A later
    // snapshot reuses them only when both the form and bound object still match.
    void CancelInventoryPreviewPreparation() {
        std::scoped_lock lock(inventoryPreviewMutex);
        ++inventoryPreviewGeneration;
        inventoryPreviewDescriptor = {};
        inventoryPreviewSources.clear();
        inventoryPreviewNextSource = 0;
        inventoryPreviewStatus = InventoryPreviewPreparationStatus::Idle;
    }

    InventoryPreviewPreparationStatus GetInventoryPreviewPreparationStatus(
        const PickpocketInventory::Snapshot& snapshot) {
        std::scoped_lock lock(inventoryPreviewMutex);
        if (inventoryPreviewDescriptor.snapshotRevision != snapshot.revision ||
            inventoryPreviewDescriptor.targetHandle != snapshot.targetHandle.native_handle()) {
            return InventoryPreviewPreparationStatus::Idle;
        }
        return inventoryPreviewStatus;
    }

    void ResetDialTimer(RE::FormID targetFormID, float timerSeconds = kDialTimerDurationSeconds) {
        dialTimerTargetFormID = targetFormID;
        dialTimerRemainingSeconds = std::clamp(timerSeconds, 0.0f, kDialTimerDurationSeconds);
        dialTimerPenaltyFlashSeconds = 0.0f;
        dialTimerPenaltyFlashFromFraction = 0.0f;
        dialTimerPenaltyFlashToFraction = 0.0f;
    }

    void ResetDialInteractionForNextSession() {
        markedInventoryItems.clear();
        ResetControllerInput();
        plantItemsKeyWasDown = false;
        selectKeyWasDown = false;
        targetDialSlot = kDialExitSlot;
        queuedDialMove = 0;
        cursorDialPosition = static_cast<float>(kDialExitSlot);
        dialRevealStates = {};
        dialRevealRevision = 0;
    }

    bool __stdcall ProcessInput(RE::InputEvent* inputEvent) {
        if (!inputEvent) {
            return false;
        }

        Input::ObserveInputEvent(*inputEvent);
        if (
            !pickpocketInventoryWindow ||
            !pickpocketInventoryWindow->IsOpen ||
            inputEvent->GetDevice() != RE::INPUT_DEVICE::kGamepad) {
            return false;
        }

        if (const auto* thumbstick = inputEvent->AsThumbstickEvent(); thumbstick && thumbstick->IsLeft()) {
            controllerStickX.store(thumbstick->xValue, std::memory_order_release);
            controllerStickY.store(thumbstick->yValue, std::memory_order_release);
            return true;
        }

        const auto* button = inputEvent->AsButtonEvent();
        if (!button) {
            return false;
        }

        const auto action = Input::ResolveMenuAction(*button);
        if (!action) {
            return false;
        }

        switch (*action) {
        case Input::MenuAction::MoveLeft:
            controllerMoveLeftDown.store(button->IsPressed(), std::memory_order_release);
            break;
        case Input::MenuAction::MoveRight:
            controllerMoveRightDown.store(button->IsPressed(), std::memory_order_release);
            break;
        case Input::MenuAction::Confirm:
            if (button->IsDown()) {
                controllerConfirmButtonID.store(button->GetIDCode(), std::memory_order_release);
                controllerConfirmDown.store(true, std::memory_order_release);
                QueueControllerAction(*action);
            } else if (
                controllerConfirmButtonID.load(std::memory_order_acquire) == button->GetIDCode()) {
                controllerConfirmDown.store(button->IsPressed(), std::memory_order_release);
            }
            break;
        case Input::MenuAction::Cancel:
        case Input::MenuAction::Place:
            if (button->IsDown()) {
                QueueControllerAction(*action);
            }
            break;
        }
        return true;
    }

    void SyncDialTimer(const PickpocketInventory::Snapshot& snapshot) {
        if (dialTimerTargetFormID != snapshot.targetFormID) {
            ResetDialTimer(snapshot.targetFormID);
        }
    }

    bool IsDialTimerExpired() {
        return dialTimerRemainingSeconds <= 0.0f;
    }

    void ApplyDialTakeTimePenalty() {
        const auto previousRemaining = dialTimerRemainingSeconds;
        dialTimerRemainingSeconds = std::clamp(dialTimerRemainingSeconds - kDialTakeTimePenaltySeconds, 0.0f, kDialTimerDurationSeconds);

        const auto deductedSeconds = previousRemaining - dialTimerRemainingSeconds;
        if (deductedSeconds <= 0.0f) {
            return;
        }

        dialTimerPenaltyFlashFromFraction = dialTimerRemainingSeconds / kDialTimerDurationSeconds;
        dialTimerPenaltyFlashToFraction = previousRemaining / kDialTimerDurationSeconds;
        dialTimerPenaltyFlashSeconds = kDialTakePenaltyFlashSeconds;
    }

    bool IsInventoryItemMarked(RE::FormID formID) {
        return formID != 0 && markedInventoryItems.contains(formID);
    }

    bool MatchesRevealCategories(
        const PickpocketInventory::Item& item,
        const PickpocketRevealRules::RevealCategories& categories) {
        return
            categories.allItems ||
            (categories.keys && item.isKey) ||
            (categories.gold && item.isGold) ||
            (categories.jewelry && item.isJewelry) ||
            (categories.gems && item.isGem);
    }

    bool ShouldForceRevealItem(
        const PickpocketInventory::Item& item,
        const PickpocketRevealRules::RevealCategories& perkCategories) {
        return PickpocketConfig::settings.alwaysRevealAllItems || MatchesRevealCategories(item, perkCategories);
    }

    bool CanPickpocketItemInDial(const PickpocketInventory::Item& item) {
        return item.minigameEligible || PickpocketConfig::settings.allowPickpocketingAllInventoryItems;
    }

    void MarkInventoryItemForTransfer(const PickpocketInventory::Item& item) {
        if (markedInventoryItems.insert(item.formID).second) {
            ApplyDialTakeTimePenalty();
        }
    }

    std::vector<RE::FormID> GetMarkedItemFormIDs(const PickpocketInventory::Snapshot& snapshot) {
        std::vector<RE::FormID> formIDs;
        formIDs.reserve(markedInventoryItems.size());
        for (const auto& item : snapshot.items) {
            if (IsInventoryItemMarked(item.formID)) {
                formIDs.push_back(item.formID);
            }
        }

        return formIDs;
    }

    bool HasMarkedItemsInSnapshot(const PickpocketInventory::Snapshot& snapshot) {
        return std::ranges::any_of(snapshot.items, [](const auto& item) {
            return IsInventoryItemMarked(item.formID);
        });
    }

    void CommitMarkedInventoryItemsAndClose(const PickpocketInventory::Snapshot& snapshot) {
        const auto markedFormIDs = GetMarkedItemFormIDs(snapshot);
        if (markedFormIDs.empty()) {
            ClosePickpocketInventoryWindow();
            return;
        }
        if (!PickpocketSession::BeginTransfer(snapshot.targetHandle.native_handle())) {
            return;
        }

        PickpocketInventory::TransferItemStacks(
            markedFormIDs, PickpocketConfig::settings.allowPickpocketingAllInventoryItems);

        ClosePickpocketPresentation();
        (void)PickpocketSession::FinishTerminal("marked items resolved");
    }

    bool ShouldRenderInventoryItem(const PickpocketInventory::Item& item) {
        if (!item.object || item.name.empty()) {
            return false;
        }
        return PickpocketInventory::HasRenderableModel(item.object);
    }

    MeshRenderingFrameworkAPI::Mesh* GetInventoryPreview(const PickpocketInventory::Item& item) {
        if (auto preview = inventoryPreviews.find(item.formID); preview != inventoryPreviews.end()) {
            return preview->second.object == item.object ? preview->second.mesh.get() : nullptr;
        }
        return nullptr;
    }

    bool RenderInventoryItem(const PickpocketInventory::Item& item) {
        if (!ShouldRenderInventoryItem(item)) {
            return false;
        }

        auto* preview = GetInventoryPreview(item);
        if (!PickpocketConfig::UsingSkyUIIcons() && !preview) {
            return false;
        }

        ImGui::PushID(static_cast<int>(item.formID));
        const auto previewSize = static_cast<float>(kInventoryPreviewRenderSize);
        ImGui::InvisibleButton("##InventoryItemPreview", { previewSize, previewSize });
        if (preview) {
            const auto imageMin = ImGui::GetItemRectMin();
            const auto imageMax = ImGui::GetItemRectMax();
            const auto inset = previewSize * (1.0f - kInventoryPreviewContentScale) * 0.5f;
            ImGui::ImDrawListManager::AddImage(
                ImGui::GetWindowDrawList(),
                reinterpret_cast<ImTextureID>(preview->GetResourceView()),
                { imageMin.x + inset, imageMin.y + inset },
                { imageMax.x - inset, imageMax.y - inset },
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32_WHITE);
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextWrapped("%s", item.name.c_str());
        ImGui::Text("Form: %08X", item.formID);
        ImGui::Text("Count: %d", item.count);
        ImGui::Text("Value: %d", item.value);
        ImGui::Text("Steal value: %u", item.stealValue);
        ImGui::Text("Weight: %.2f", std::max(item.weight, 0.0f));
        std::string categories;
        const auto appendCategory = [&categories](const char* category) {
            if (!categories.empty()) {
                categories += ", ";
            }
            categories += category;
        };
        if (item.isKey) {
            appendCategory("key");
        }
        if (item.isGold) {
            appendCategory("gold");
        }
        if (item.isJewelry) {
            appendCategory("jewelry");
        }
        if (item.isGem) {
            appendCategory("gem");
        }
        ImGui::Text("Categories: %s", categories.empty() ? "none" : categories.c_str());
        if (item.worn) {
            ImGui::Text("Worn");
        }
        if (item.favorited) {
            ImGui::Text("Favorited");
        }
        if (item.questItem) {
            ImGui::Text("Quest item");
        }
        ImGui::Text("Without equipped-item perk: %s", item.worn ? "blocked" : "allowed");
        ImGui::Text("With equipped-item perk: allowed");
        ImGui::Text("Minigame take: %s", item.minigameEligible ? "allowed" : "blocked");
        if (!item.minigameEligible && !item.blockedReason.empty()) {
            ImGui::TextWrapped("Block: %s", item.blockedReason.c_str());
        }

        ImGui::EndGroup();

        ImGui::PopID();
        ImGui::Separator();
        return true;
    }


    void UpdateDialTimer() {
        const auto deltaTime = std::max(ImGui::GetIO()->DeltaTime, 0.0f);
        dialTimerRemainingSeconds = std::clamp(dialTimerRemainingSeconds - deltaTime, 0.0f, kDialTimerDurationSeconds);
        dialTimerPenaltyFlashSeconds = std::clamp(dialTimerPenaltyFlashSeconds - deltaTime, 0.0f, kDialTakePenaltyFlashSeconds);
    }

    void DrawTimerArcSegment(
        ImDrawList* drawList,
        ImVec2 center,
        float dialSize,
        float startFraction,
        float endFraction,
        ImU32 color,
        float thicknessScale = 1.0f) {
        const auto clampedStart = std::clamp(startFraction, 0.0f, 1.0f);
        const auto clampedEnd = std::clamp(endFraction, 0.0f, 1.0f);
        if (clampedEnd <= clampedStart) {
            return;
        }

        const auto startAngle = -kPi * 0.5f;
        const auto radius = dialSize * kDialTimerRadius;
        const auto thickness = dialSize * kDialTimerThickness * thicknessScale;
        const auto arcStartAngle = startAngle + clampedStart * kPi * 2.0f;
        const auto arcEndAngle = startAngle + clampedEnd * kPi * 2.0f;
        const auto segmentSteps = std::max(8, static_cast<int>(96.0f * (clampedEnd - clampedStart)));

        ImGui::ImDrawListManager::PathClear(drawList);
        ImGui::ImDrawListManager::PathArcTo(drawList, center, radius, arcStartAngle, arcEndAngle, segmentSteps);
        ImGui::ImDrawListManager::PathStroke(drawList, color, 0, thickness);
    }


    void DrawTimerArc(ImDrawList* drawList, ImTextureID timerFillTexture, ImVec2 center, float dialSize, bool showTakePenaltyPreview) {
        const auto timeRemaining = dialTimerRemainingSeconds / kDialTimerDurationSeconds;
        if (timerFillTexture) {
            DrawTexturedTimerArcSegment(
                drawList,
                timerFillTexture,
                center,
                dialSize * kDialTimerRadius,
                dialSize * kDialTimerFillThickness,
                0.0f,
                timeRemaining,
                GetTimerArcColor(timeRemaining));
        }

        if (showTakePenaltyPreview && dialTimerRemainingSeconds > 0.0f) {
            const auto penaltyStart = std::max(0.0f, (dialTimerRemainingSeconds - kDialTakeTimePenaltySeconds) / kDialTimerDurationSeconds);
            DrawTimerArcSegment(drawList, center, dialSize, penaltyStart, timeRemaining, IM_COL32(255, 112, 58, 190), 1.10f);
        }

        if (dialTimerPenaltyFlashSeconds > 0.0f && dialTimerPenaltyFlashToFraction > dialTimerPenaltyFlashFromFraction) {
            const auto flashProgress = dialTimerPenaltyFlashSeconds / kDialTakePenaltyFlashSeconds;
            const auto flashAlpha = std::clamp(static_cast<int>(230.0f * flashProgress), 0, 230);
            DrawTimerArcSegment(
                drawList,
                center,
                dialSize,
                dialTimerPenaltyFlashFromFraction,
                dialTimerPenaltyFlashToFraction,
                IM_COL32(255, 58, 58, flashAlpha),
                1.18f);
        }
    }

    void RenderDialSlotPreview(
        ImDrawList* drawList,
        const PickpocketInventory::Item& item,
        ImVec2 slotCenter,
        float previewSize,
        ImTextureID unknownTexture,
        float revealProgress) {
        const ImVec2 previewMin{
            slotCenter.x - previewSize * 0.5f,
            slotCenter.y - previewSize * 0.5f
        };
        const ImVec2 previewMax{
            slotCenter.x + previewSize * 0.5f,
            slotCenter.y + previewSize * 0.5f
        };

        const auto clampedReveal = std::clamp(revealProgress, 0.0f, 1.0f);
        const auto meshReveal = std::pow(clampedReveal, kDialMeshRevealPower);
        const auto coverReveal = std::pow(clampedReveal, kDialCoverRevealPower);
        const auto usingSkyUIIcons = PickpocketConfig::UsingSkyUIIcons();
        auto* preview = usingSkyUIIcons ? nullptr : GetInventoryPreview(item);
        const auto hasPreview = usingSkyUIIcons || (preview && preview->GetResourceView());
        if (!usingSkyUIIcons && hasPreview && meshReveal > 0.0f) {
            const auto inset = previewSize * (1.0f - kInventoryPreviewContentScale) * 0.5f;
            ImGui::ImDrawListManager::AddImage(
                drawList,
                reinterpret_cast<ImTextureID>(preview->GetResourceView()),
                { previewMin.x + inset, previewMin.y + inset },
                { previewMax.x - inset, previewMax.y - inset },
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32(255, 255, 255, std::clamp(static_cast<int>(meshReveal * 255.0f), 0, 255)));
        }

        if (unknownTexture) {
            const auto coverProgress = hasPreview ? 1.0f - coverReveal : std::max(1.0f - clampedReveal, 0.75f);
            const auto coverAlpha = std::clamp(static_cast<int>(coverProgress * 220.0f), 0, 220);
            if (coverAlpha <= 0) {
                return;
            }

            ImGui::ImDrawListManager::AddImage(
                drawList,
                unknownTexture,
                previewMin,
                previewMax,
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32(255, 255, 255, coverAlpha));
        }
    }

    void SubmitSkyUIIconLayout(
        const DialLayout& layout,
        const ImVec2& viewport,
        ImVec2 dialCenter,
        float dialSize,
        float previewSize,
        bool revealIcons) {
        std::vector<SkyUIIcons::Slot> slots;
        slots.reserve(kDialItemSlotCount);
        for (int slotIndex = kDialFirstItemSlot; slotIndex < kDialSlotCount; ++slotIndex) {
            const auto* item = layout.itemsBySlot[static_cast<std::size_t>(slotIndex)];
            if (!item) {
                continue;
            }

            const auto center = GetDialSlotCenter(dialCenter, dialSize, slotIndex);
            const auto revealProgress = revealIcons ?
                GetDialRevealProgress(static_cast<std::size_t>(slotIndex)) :
                0.0f;
            slots.push_back({
                item->formID,
                {
                    center.x - previewSize * 0.5f,
                    center.y - previewSize * 0.5f,
                    previewSize,
                    previewSize
                },
                std::pow(std::clamp(revealProgress, 0.0f, 1.0f), kDialMeshRevealPower)
            });
        }

        SkyUIIcons::SubmitLayout(
            viewport.x,
            viewport.y,
            revealIcons ? SkyUIIcons::Rect{
                dialCenter.x - dialSize * 0.5f,
                dialCenter.y - dialSize * 0.5f,
                dialSize,
                dialSize
            } : SkyUIIcons::Rect{},
            revealIcons ? pickpocketWindowDimAlpha : 0.0f,
            slots);
    }

    int WrapDialSlot(int slot) {
        auto wrapped = slot % kDialSlotCount;
        if (wrapped < 0) {
            wrapped += kDialSlotCount;
        }
        return wrapped;
    }

    bool IsDialExitSlot(int slotIndex) {
        return WrapDialSlot(slotIndex) == kDialExitSlot;
    }

    int GetDialSlotDistanceFromExit(int slotIndex) {
        const auto wrappedSlot = WrapDialSlot(slotIndex);
        return std::min(wrappedSlot, kDialSlotCount - wrappedSlot);
    }

    std::uint64_t MixDialLayoutSeed(std::uint64_t value) {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    void HashDialLayoutSeed(std::uint64_t& seed, std::uint64_t value) {
        seed = MixDialLayoutSeed(seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2)));
    }

    double NextDialLayoutRandomUnit(std::uint64_t& seed) {
        seed = MixDialLayoutSeed(seed);
        return static_cast<double>(seed >> 11) * (1.0 / 9007199254740992.0);
    }

    std::uint64_t GetDialLayoutSeed(
        const PickpocketInventory::Snapshot& snapshot,
        const std::vector<const PickpocketInventory::Item*>& candidates) {
        auto seed = 0xD1A1F00D5EED1234ull;
        HashDialLayoutSeed(seed, snapshot.targetFormID);
        HashDialLayoutSeed(seed, snapshot.revision);
        HashDialLayoutSeed(seed, PickpocketConfig::settings.allowPickpocketingAllInventoryItems ? 1ull : 0ull);
        HashDialLayoutSeed(seed, static_cast<std::uint64_t>(std::max(PickpocketConfig::settings.dialValueDistanceBias, PickpocketConfig::kDialValueDistanceBiasMin) * 1000.0f));

        for (const auto* item : candidates) {
            HashDialLayoutSeed(seed, item->formID);
            HashDialLayoutSeed(seed, item->stealValue);
            HashDialLayoutSeed(seed, static_cast<std::uint64_t>(std::max(item->count, 0)));
        }

        return seed;
    }

    std::vector<const PickpocketInventory::Item*> BuildDialCandidates(
        const PickpocketInventory::Snapshot& snapshot) {
        return PickpocketInventory::SelectTopItems(
            snapshot,
            static_cast<std::size_t>(kDialItemSlotCount),
            [](const PickpocketInventory::Item& item) { return CanPickpocketItemInDial(item); });
    }

    std::vector<RE::FormID> GetDialCandidateFormIDs(const PickpocketInventory::Snapshot& snapshot) {
        const auto candidates = BuildDialCandidates(snapshot);
        std::vector<RE::FormID> formIDs;
        formIDs.reserve(candidates.size());
        std::ranges::transform(candidates, std::back_inserter(formIDs), &PickpocketInventory::Item::formID);
        return formIDs;
    }

    DialLayout BuildDialLayout(const PickpocketInventory::Snapshot& snapshot) {
        DialLayout layout;
        const auto candidates = BuildDialCandidates(snapshot);
        std::vector<int> availableSlots;
        availableSlots.reserve(kDialItemSlotCount);
        for (int slotIndex = kDialFirstItemSlot; slotIndex < kDialSlotCount; ++slotIndex) {
            availableSlots.push_back(slotIndex);
        }

        auto seed = GetDialLayoutSeed(snapshot, candidates);
        for (std::size_t itemIndex = 0; itemIndex < candidates.size() && !availableSlots.empty(); ++itemIndex) {
            const auto valueRank = candidates.size() <= 1 ?
                1.0 :
                1.0 - (static_cast<double>(itemIndex) / static_cast<double>(candidates.size() - 1));
            const auto biasStrength = static_cast<double>(std::max(PickpocketConfig::settings.dialValueDistanceBias, PickpocketConfig::kDialValueDistanceBiasMin));

            std::vector<double> slotWeights;
            slotWeights.reserve(availableSlots.size());
            double totalWeight = 0.0;
            for (const auto slotIndex : availableSlots) {
                const auto distanceNorm = static_cast<double>(GetDialSlotDistanceFromExit(slotIndex)) / static_cast<double>(kDialSlotCount / 2);
                const auto slotWeight = std::exp(biasStrength * valueRank * distanceNorm);
                slotWeights.push_back(slotWeight);
                totalWeight += slotWeight;
            }

            auto pick = NextDialLayoutRandomUnit(seed) * totalWeight;
            std::size_t chosenIndex = availableSlots.size() - 1;
            for (std::size_t slotIndex = 0; slotIndex < slotWeights.size(); ++slotIndex) {
                pick -= slotWeights[slotIndex];
                if (pick <= 0.0) {
                    chosenIndex = slotIndex;
                    break;
                }
            }

            const auto chosenSlot = availableSlots[chosenIndex];
            layout.itemsBySlot[static_cast<std::size_t>(chosenSlot)] = candidates[itemIndex];
            availableSlots.erase(availableSlots.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
        }

        return layout;
    }

    const PickpocketInventory::Item* GetDialItemForSlot(const DialLayout& layout, int slotIndex) {
        const auto wrappedSlot = WrapDialSlot(slotIndex);
        if (wrappedSlot < kDialFirstItemSlot) {
            return nullptr;
        }

        return layout.itemsBySlot[static_cast<std::size_t>(wrappedSlot)];
    }

    float WrapDialPosition(float position) {
        auto wrapped = std::fmod(position, static_cast<float>(kDialSlotCount));
        if (wrapped < 0.0f) {
            wrapped += static_cast<float>(kDialSlotCount);
        }
        return wrapped;
    }

    float GetShortestDialDelta(float fromPosition, float toPosition) {
        auto delta = WrapDialPosition(toPosition) - WrapDialPosition(fromPosition);
        const auto halfSlotCount = static_cast<float>(kDialSlotCount) * 0.5f;
        if (delta > halfSlotCount) {
            delta -= static_cast<float>(kDialSlotCount);
        } else if (delta < -halfSlotCount) {
            delta += static_cast<float>(kDialSlotCount);
        }
        return delta;
    }

    int GetCursorDialSlot() {
        return WrapDialSlot(static_cast<int>(std::round(WrapDialPosition(cursorDialPosition))));
    }

    bool IsCursorAtTargetDialSlot() {
        return std::abs(GetShortestDialDelta(cursorDialPosition, static_cast<float>(targetDialSlot))) <= kDialCursorArriveEpsilon;
    }

    bool CanClosePickpocketInventoryWindowWithoutFailure(const PickpocketInventory::Snapshot& snapshot) {
        if (PickpocketConfig::settings.showDebugPickpocketListDuringPhase2) {
            return false;
        }

        if (HasMarkedItemsInSnapshot(snapshot)) {
            return false;
        }

        const auto cursorSlot = GetCursorDialSlot();
        return IsCursorAtTargetDialSlot() && cursorSlot == targetDialSlot && IsDialExitSlot(cursorSlot);
    }

    bool IsDialInputInBufferGraceWindow() {
        const auto remainingSlots = std::abs(GetShortestDialDelta(cursorDialPosition, static_cast<float>(targetDialSlot)));
        const auto remainingSeconds = remainingSlots / kDialCursorSlotsPerSecond;
        return remainingSeconds <= kDialInputBufferGraceSeconds;
    }

    float GetControllerStickX() {
        const auto moveLeft =
            controllerMoveLeftDown.load(std::memory_order_acquire) ||
            ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft);
        const auto moveRight =
            controllerMoveRightDown.load(std::memory_order_acquire) ||
            ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight);
        if (moveLeft != moveRight) {
            return moveLeft ? -1.0f : 1.0f;
        }

        const auto rawX = controllerStickX.load(std::memory_order_acquire);
        const auto rawY = controllerStickY.load(std::memory_order_acquire);
        if (rawX != 0.0f || rawY != 0.0f) {
            return rawX;
        }

        return ImGui::GetKeyMagnitude2d(
            ImGuiKey_GamepadLStickLeft,
            ImGuiKey_GamepadLStickRight,
            ImGuiKey_GamepadLStickUp,
            ImGuiKey_GamepadLStickDown).x;
    }

    int GetDialMoveInput() {
        const auto stickX = GetControllerStickX();
        const auto moveLeft =
            IsConfiguredKeyDown(GetKeyBindingMapping(KeyBindingSlot::MoveLeft).key) ||
            stickX <= -kControllerNavigationDeadzone;
        const auto moveRight =
            IsConfiguredKeyDown(GetKeyBindingMapping(KeyBindingSlot::MoveRight).key) ||
            stickX >= kControllerNavigationDeadzone;
        if (moveLeft == moveRight) {
            return 0;
        }

        return moveLeft ? -1 : 1;
    }

    bool IsTakeSelectedInputPressed() {
        const auto rawControllerPressed = ConsumeControllerAction(Input::MenuAction::Confirm);
        const auto imguiControllerPressed = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false);
        takeSelectedInputWasControllerConfirm = rawControllerPressed || imguiControllerPressed;
        const auto controllerPressed = takeSelectedInputWasControllerConfirm;
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            selectKeyWasDown = false;
            return controllerPressed;
        }

        return IsConfiguredKeyPressed(GetKeyBindingMapping(KeyBindingSlot::Select).key, selectKeyWasDown) ||
            controllerPressed;
    }

    void MoveTargetDialSlot(int delta) {
        targetDialSlot = WrapDialSlot(targetDialSlot + delta);
    }

    void UpdateDialCursorPosition() {
        targetDialSlot = WrapDialSlot(targetDialSlot);
        cursorDialPosition = WrapDialPosition(cursorDialPosition);

        const auto delta = GetShortestDialDelta(cursorDialPosition, static_cast<float>(targetDialSlot));
        const auto maxStep = kDialCursorSlotsPerSecond * ImGui::GetIO()->DeltaTime;
        if (std::abs(delta) <= maxStep) {
            cursorDialPosition = static_cast<float>(targetDialSlot);
            if (queuedDialMove != 0 && queuedDialMove == GetDialMoveInput()) {
                MoveTargetDialSlot(queuedDialMove);
            }
            queuedDialMove = 0;
            return;
        }

        cursorDialPosition = WrapDialPosition(cursorDialPosition + std::copysign(maxStep, delta));
    }

    void SyncDialRevealStates(const PickpocketInventory::Snapshot& snapshot, const DialLayout& layout) {
        if (dialRevealRevision != snapshot.revision) {
            dialRevealStates = {};
            dialRevealRevision = snapshot.revision;
            queuedDialMove = 0;
        }

        const auto perkRevealCategories = PickpocketRevealRules::GetPlayerRevealCategories();
        for (std::size_t i = 0; i < dialRevealStates.size(); ++i) {
            const auto* item = GetDialItemForSlot(layout, static_cast<int>(i));
            const auto formID = item ? item->formID : 0;
            const auto forceRevealed = item && ShouldForceRevealItem(*item, perkRevealCategories);
            if (dialRevealStates[i].formID != formID) {
                dialRevealStates[i] = { formID };
            }

            dialRevealStates[i].forceRevealed = forceRevealed;
            if (forceRevealed || IsInventoryItemMarked(formID)) {
                dialRevealStates[i].progress = 1.0f;
            }
        }
    }

    void UpdateDialRevealProgress() {
        const auto activeSlot = static_cast<std::size_t>(GetCursorDialSlot());
        const auto deltaTime = ImGui::GetIO()->DeltaTime;

        for (std::size_t i = 0; i < dialRevealStates.size(); ++i) {
            auto& state = dialRevealStates[i];
            if (state.formID == 0) {
                state.progress = 0.0f;
                continue;
            }

            if (state.forceRevealed || IsInventoryItemMarked(state.formID)) {
                state.progress = 1.0f;
                continue;
            }

            if (i == activeSlot) {
                state.progress = std::clamp(state.progress + deltaTime / kDialRevealSeconds, 0.0f, 1.0f);
            } else {
                state.progress = std::clamp(state.progress - deltaTime / kDialObscureSeconds, 0.0f, 1.0f);
            }
        }
    }

    float GetDialRevealProgress(std::size_t slotIndex) {
        if (slotIndex >= dialRevealStates.size()) {
            return 0.0f;
        }

        return dialRevealStates[slotIndex].progress;
    }

    bool IsDialSlotRevealed(std::size_t slotIndex) {
        return GetDialRevealProgress(slotIndex) >= kDialRevealCompleteThreshold;
    }

    namespace {
        struct ControlPrompt {
            std::string_view iconFileName;
            const char* label = "";
        };

        struct ControllerIconSet {
            const char* move;
            const char* select;
            const char* placeItems;
        };

        constexpr ControllerIconSet kDirectXControllerIcons{
            "360_LS.png",
            "360_A.png",
            "360_Back.png",
        };
        constexpr ControllerIconSet kOrbisControllerIcons{
            "PS3_LS.png",
            "PS3_A.png",
            "PS3_Back.png",
        };

        const ControllerIconSet& GetControllerIconSet() {
            const auto* controlMap = RE::ControlMap::GetSingleton();
            return controlMap && controlMap->GetGamePadType() == RE::PC_GAMEPAD_TYPE::kOrbis ?
                kOrbisControllerIcons :
                kDirectXControllerIcons;
        }
    }

    struct ControlHintLayout {
        ImTextureID icon = nullptr;
        std::string fallbackKeyText;
        ImVec2 fallbackKeySize{};
        ImVec2 labelSize{};
        ImVec2 size{};
        float keyWidth = 0.0f;
    };

    ControlHintLayout MeasureControlHint(
        ImFont* font,
        float fontSize,
        float iconSize,
        float spacing,
        std::string_view iconFileName,
        const char* label) {
        ControlHintLayout layout;
        layout.icon = GetKeyIconTexture(iconFileName);
        layout.fallbackKeyText = GetKeyIconLabel(iconFileName);
        layout.fallbackKeySize = CalcFontTextSize(font, fontSize, layout.fallbackKeyText);
        layout.labelSize = CalcFontTextSize(font, fontSize, label);
        layout.keyWidth = layout.icon ? iconSize : layout.fallbackKeySize.x;
        layout.size = {
            layout.keyWidth + spacing + layout.labelSize.x,
            std::max(layout.icon ? iconSize : layout.fallbackKeySize.y, layout.labelSize.y)
        };
        return layout;
    }

    void DrawControlHint(
        ImDrawList* drawList,
        ImFont* font,
        float fontSize,
        float iconSize,
        float spacing,
        ImVec2 position,
        const ControlHintLayout& layout,
        const char* label) {
        const auto textColor = IM_COL32(251, 251, 251, 255);

        if (layout.icon) {
            const ImVec2 iconMin{ position.x, position.y + (layout.size.y - iconSize) * 0.5f };
            const ImVec2 iconMax{ iconMin.x + iconSize, iconMin.y + iconSize };
            ImGui::ImDrawListManager::AddImage(
                drawList,
                layout.icon,
                iconMin,
                iconMax,
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32_WHITE);
        } else {
            DrawTextWithShadow(
                drawList,
                font,
                fontSize,
                { position.x, position.y + (layout.size.y - layout.fallbackKeySize.y) * 0.5f },
                textColor,
                layout.fallbackKeyText);
        }

        DrawTextWithShadow(
            drawList,
            font,
            fontSize,
            {
                position.x + layout.keyWidth + spacing,
                position.y + (layout.size.y - layout.labelSize.y) * 0.5f
            },
            textColor,
            label);
    }

    template <std::size_t PromptCount>
    void RenderLeftControlHints(const std::array<ControlPrompt, PromptCount>& prompts) {
        const auto screenSize = ImGui::GetIO()->DisplaySize;
        const auto scale = GetControlHintScale(screenSize);
        auto* font = ImGui::GetFont();
        if (!font) {
            return;
        }

        const auto iconSize = kPlantItemsHintIconSourceSize * kPlantItemsHintIconScale * scale;
        const auto fontSize = kPlantItemsHintFontSize * scale;
        const auto spacing = 10.0f * scale;
        const auto gap = kControlHintGap * scale;

        float maxHeight = 0.0f;
        std::array<ControlHintLayout, PromptCount> layouts{};
        for (std::size_t index = 0; index < prompts.size(); ++index) {
            layouts[index] = MeasureControlHint(
                font,
                fontSize,
                iconSize,
                spacing,
                prompts[index].iconFileName,
                prompts[index].label);
            maxHeight = std::max(maxHeight, layouts[index].size.y);
        }

        auto position = ImVec2{
            kControlHintLeftMargin * scale,
            screenSize.y - maxHeight - (kPlantItemsHintBottomMargin * scale)
        };

        auto* drawList = ImGui::GetWindowDrawList();
        for (std::size_t index = 0; index < prompts.size(); ++index) {
            DrawControlHint(
                drawList,
                font,
                fontSize,
                iconSize,
                spacing,
                position,
                layouts[index],
                prompts[index].label);
            position.x += layouts[index].size.x + gap;
        }
    }

    void RenderMovementControlHints() {
        if (Input::IsControllerActive()) {
            const auto& icons = GetControllerIconSet();
            const std::array<ControlPrompt, 2> prompts{
                ControlPrompt{ icons.move, "Move" },
                ControlPrompt{ icons.select, kSelectHintLabel },
            };
            RenderLeftControlHints(prompts);
            return;
        }

        const std::array<ControlPrompt, 3> prompts{
            ControlPrompt{ GetKeyBindingMapping(KeyBindingSlot::MoveLeft).fileName, kMoveLeftHintLabel },
            ControlPrompt{ GetKeyBindingMapping(KeyBindingSlot::MoveRight).fileName, kMoveRightHintLabel },
            ControlPrompt{ GetKeyBindingMapping(KeyBindingSlot::Select).fileName, kSelectHintLabel },
        };
        RenderLeftControlHints(prompts);
    }

    void RenderPlantItemsHintButton(const PickpocketInventory::Snapshot& snapshot) {
        const auto screenSize = ImGui::GetIO()->DisplaySize;
        const auto scale = GetControlHintScale(screenSize);
        auto* font = ImGui::GetFont();
        if (!font) {
            return;
        }

        const auto iconFileName = Input::IsControllerActive() ?
            std::string_view(GetControllerIconSet().placeItems) :
            std::string_view(GetKeyBindingMapping(KeyBindingSlot::PlaceItems).fileName);
        const auto iconSize = kPlantItemsHintIconSourceSize * kPlantItemsHintIconScale * scale;
        const auto fontSize = kPlantItemsHintFontSize * scale;
        const auto spacing = 10.0f * scale;
        const auto layout = MeasureControlHint(font, fontSize, iconSize, spacing, iconFileName, kPlantItemsHintLabel);
        const ImVec2 buttonPos{
            screenSize.x - layout.size.x - (kPlantItemsHintRightMargin * scale),
            screenSize.y - layout.size.y - (kPlantItemsHintBottomMargin * scale)
        };

        ImGui::SetCursorScreenPos(buttonPos);
        ImGui::InvisibleButton("##PlantItemsHintButton", layout.size);
        const auto clicked = ImGui::IsItemClicked();

        DrawControlHint(ImGui::GetWindowDrawList(), font, fontSize, iconSize, spacing, buttonPos, layout, kPlantItemsHintLabel);

        if (clicked) {
            OpenVanillaDepositMenuAndClose(snapshot);
        }
    }

    void DrawMarkedDialSlotRing(ImDrawList* drawList, ImVec2 slotCenter, float dialSize) {
        const auto radius = dialSize * kDialSlotRingRadius;
        const auto thickness = std::clamp(dialSize * 0.006f, 4.0f, 10.0f);

        ImGui::ImDrawListManager::AddCircle(drawList, slotCenter, radius, IM_COL32(0, 0, 0, 210), 0, thickness + 2.0f);
        ImGui::ImDrawListManager::AddCircle(drawList, slotCenter, radius, IM_COL32(255, 255, 255, 245), 0, thickness);
    }

    float GetDialExitIconSize(float dialSize) {
        return dialSize * kDialSlotRecessRadius * 0.95f * 1.72f;
    }

    void DrawDialExitIcon(ImDrawList* drawList, ImTextureID iconTexture, ImVec2 slotCenter, float dialSize, bool active) {
        if (!iconTexture) {
            return;
        }

        const auto iconSize = GetDialExitIconSize(dialSize);
        const auto opacity = active ? 1.0f : 0.72f;
        const ImVec2 min{
            std::round(slotCenter.x - iconSize * 0.5f),
            std::round(slotCenter.y - iconSize * 0.5f)
        };
        const ImVec2 max{ min.x + iconSize, min.y + iconSize };
        const auto shadowOffset = std::clamp(dialSize * 0.0020f, 1.0f, 3.0f);

        ImGui::ImDrawListManager::AddImage(
            drawList,
            iconTexture,
            { min.x + shadowOffset, min.y + shadowOffset },
            { max.x + shadowOffset, max.y + shadowOffset },
            { 0.0f, 0.0f },
            { 1.0f, 1.0f },
            IM_COL32(0, 0, 0, ScaleAlpha(170, opacity)));
        ImGui::ImDrawListManager::AddImage(
            drawList,
            iconTexture,
            min,
            max,
            { 0.0f, 0.0f },
            { 1.0f, 1.0f },
            IM_COL32(255, 255, 255, ScaleAlpha(255, opacity)));
    }

    void RenderDialExitCenterInfo(ImDrawList* drawList, ImVec2 dialCenter, float dialSize) {
        auto* font = ImGui::GetFont();
        if (!font) {
            return;
        }

        const auto titleSize = std::clamp(dialSize * 0.052f, 26.0f, 56.0f);
        DrawCenteredText(
            drawList,
            font,
            titleSize,
            { dialCenter.x, dialCenter.y - dialSize * 0.065f },
            IM_COL32(238, 238, 238, 255),
            "Leave");
    }

    std::string FormatWeight(float weight) {
        weight = std::max(weight, 0.0f);
        const auto precision = std::abs(weight - std::round(weight)) < 0.01f ? 0 : 1;
        return std::format("{:.{}f}", weight, precision);
    }

    struct DialStat {
        const char* label = "";
        std::string value;
        float labelWidth = 0.0f;
        float width = 0.0f;
    };

    DialStat MeasureDialStat(ImFont* font, float labelSize, float valueSize, const char* label, std::string value) {
        DialStat stat{ label, std::move(value) };
        stat.labelWidth = CalcFontTextSize(font, labelSize, label).x;
        stat.width =
            stat.labelWidth +
            valueSize * 0.22f +
            CalcFontTextSize(font, valueSize, stat.value).x;
        return stat;
    }

    void DrawDialStat(ImDrawList* drawList, ImFont* font, float labelSize, float valueSize, float x, float y, const DialStat& stat, float opacity) {
        const auto labelColor = IM_COL32(178, 178, 178, ScaleAlpha(230, opacity));
        const auto valueColor = IM_COL32(238, 238, 238, ScaleAlpha(255, opacity));

        DrawTextWithShadow(drawList, font, labelSize, { x, y + valueSize * 0.34f }, labelColor, stat.label, opacity);
        DrawTextWithShadow(
            drawList,
            font,
            valueSize,
            { x + stat.labelWidth + valueSize * 0.22f, y },
            valueColor,
            stat.value,
            opacity);
    }

    void RenderDialCenterInfo(
        ImDrawList* drawList,
        ImVec2 dialCenter,
        float dialSize,
        const PickpocketInventory::Item* item,
        float revealProgress) {
        auto* font = ImGui::GetFont();
        if (!font || !item) {
            return;
        }

        const auto textOpacity = std::clamp(
            (std::clamp(revealProgress, 0.0f, 1.0f) - kDialCenterTextFadeStart) / (1.0f - kDialCenterTextFadeStart),
            0.0f,
            1.0f);
        if (textOpacity <= 0.0f) {
            return;
        }

        const auto titleWidth = dialSize * 0.48f;
        const auto titleSize = FitFontSize(font, std::clamp(dialSize * 0.048f, 24.0f, 52.0f), 18.0f, titleWidth, item->name);
        const auto labelSize = std::clamp(dialSize * 0.023f, 14.0f, 25.0f);
        const auto valueSize = std::clamp(dialSize * 0.043f, 22.0f, 44.0f);
        const auto titleY = dialCenter.y - dialSize * 0.070f;
        const auto statY = dialCenter.y - dialSize * 0.002f;

        DrawCenteredText(drawList, font, titleSize, { dialCenter.x, titleY }, IM_COL32(238, 238, 238, ScaleAlpha(255, textOpacity)), item->name, textOpacity);

        const std::array stats{
            MeasureDialStat(font, labelSize, valueSize, "COUNT", std::to_string(item->count)),
            MeasureDialStat(font, labelSize, valueSize, "VALUE", std::to_string(item->value)),
            MeasureDialStat(font, labelSize, valueSize, "WEIGHT", FormatWeight(item->weight)),
        };

        const auto gap = dialSize * 0.034f;
        auto totalWidth = gap * static_cast<float>(stats.size() - 1);
        for (const auto& stat : stats) {
            totalWidth += stat.width;
        }

        auto x = dialCenter.x - totalWidth * 0.5f;
        for (const auto& stat : stats) {
            DrawDialStat(drawList, font, labelSize, valueSize, x, statY, stat, textOpacity);
            x += stat.width + gap;
        }
    }

    void HandleDialKeyboardInput() {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            return;
        }

        const auto moveInput = GetDialMoveInput();
        if (moveInput == 0) {
            queuedDialMove = 0;
            return;
        }

        if (IsCursorAtTargetDialSlot()) {
            queuedDialMove = 0;
            MoveTargetDialSlot(moveInput);
            return;
        }

        if (IsDialInputInBufferGraceWindow()) {
            queuedDialMove = moveInput;
        }
    }

    void RenderPickpocketDial(const PickpocketInventory::Snapshot& snapshot) {
        const auto available = ImGui::GetContentRegionAvail();
        const auto viewport = ImGui::GetIO()->DisplaySize;
        auto maxDialSize = std::min(available.x, viewport.y * 0.78f);
        if (available.y > 520.0f) {
            maxDialSize = std::min(maxDialSize, available.y - 120.0f);
        }
        const auto baseDialSize = std::clamp(maxDialSize, 360.0f, 1400.0f);
        const auto dialSize = std::clamp(baseDialSize * GetUIScale(), 180.0f, 2800.0f);
        const auto previewSize = std::clamp(dialSize * kDialPreviewSize, 64.0f, 196.0f);

        auto dialTopLeft = ImGui::GetCursorScreenPos();
        dialTopLeft.x += (available.x - dialSize) * 0.5f + GetUIXOffset(viewport);
        dialTopLeft.y += (available.y - dialSize) * 0.5f;
        dialTopLeft.y += GetUIYOffset(viewport);

        const ImVec2 dialBottomRight{ dialTopLeft.x + dialSize, dialTopLeft.y + dialSize };
        const ImVec2 dialCenter{ dialTopLeft.x + dialSize * 0.5f, dialTopLeft.y + dialSize * 0.5f };
        auto* drawList = ImGui::GetWindowDrawList();
        const auto usingSkyUIIcons = PickpocketConfig::UsingSkyUIIcons();
        const auto baseTexture = LoadSizedSvgTexture(
            usingSkyUIIcons ? kDialIconForegroundSvgTexturePath : kDialBaseSvgTexturePath,
            dialSize);
        const auto timerBorderTexture = LoadSizedSvgTexture(kDialTimerBorderSvgTexturePath, dialSize);
        if (!baseTexture) {
            return;
        }

        const auto layout = BuildDialLayout(snapshot);
        SyncDialTimer(snapshot);
        SyncDialRevealStates(snapshot, layout);
        if (usingSkyUIIcons && !SkyUIIcons::IsReadyForPresentation()) {
            SubmitSkyUIIconLayout(layout, viewport, dialCenter, dialSize, previewSize, false);
            if (!SkyUIIcons::IsReadyForPresentation()) {
                return;
            }
        }
        if (!PickpocketSession::ActivatePhase2(snapshot.targetHandle.native_handle())) {
            return;
        }
        PickpocketEvents::EndPhase1TransitionPresentation();
        if (!exitRequestedByControllerConfirm) {
            HandleDialKeyboardInput();
            UpdateDialCursorPosition();
            UpdateDialRevealProgress();
            UpdateDialTimer();
            if (IsDialTimerExpired()) {
                FailPickpocketInventoryAndClose("timer expired");
                return;
            }
        }
        if (usingSkyUIIcons) {
            SubmitSkyUIIconLayout(layout, viewport, dialCenter, dialSize, previewSize, true);
        }

        const auto cursorSlot = GetCursorDialSlot();
        const auto cursorArrived = IsCursorAtTargetDialSlot() && cursorSlot == targetDialSlot;
        const auto cursorOnExit = cursorArrived && IsDialExitSlot(cursorSlot);
        const auto* selectedItem = GetDialItemForSlot(layout, cursorSlot);
        const auto cursorSlotIndex = static_cast<std::size_t>(cursorSlot);
        const auto selectedRevealProgress = selectedItem ? GetDialRevealProgress(cursorSlotIndex) : 0.0f;
        const auto selectedRevealed = selectedItem && IsDialSlotRevealed(cursorSlotIndex);
        const auto selectedCanMark =
            selectedItem &&
            cursorArrived &&
            selectedRevealed &&
            CanPickpocketItemInDial(*selectedItem) &&
            !IsInventoryItemMarked(selectedItem->formID) &&
            !IsDialTimerExpired();

        ImGui::Dummy({ dialSize, dialSize });

        ImGui::ImDrawListManager::AddImage(
            drawList,
            baseTexture,
            dialTopLeft,
            dialBottomRight,
            { 0.0f, 0.0f },
            { 1.0f, 1.0f },
            IM_COL32_WHITE);
        DrawTimerArc(drawList, GetTimerFillTexture(), dialCenter, dialSize, selectedCanMark);
        if (timerBorderTexture) {
            ImGui::ImDrawListManager::AddImage(
                drawList,
                timerBorderTexture,
                dialTopLeft,
                dialBottomRight,
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32_WHITE);
        }

        const auto cursorAngle = WrapDialPosition(cursorDialPosition) * DialGeometry::kSlotAngleRadians;
        DrawRotatedTexture(drawList, GetSelectionTexture(), dialTopLeft, dialBottomRight, dialCenter, cursorAngle);

        const auto unknownTexture = LoadSizedSvgTexture(kDialUnknownSvgTexturePath, previewSize);
        const auto exitIconTexture = LoadSizedSvgTexture(kDialExitIconSvgTexturePath, GetDialExitIconSize(dialSize));
        for (int slotIndex = kDialFirstItemSlot; slotIndex < kDialSlotCount; ++slotIndex) {
            const auto* item = GetDialItemForSlot(layout, slotIndex);
            if (!item) {
                continue;
            }

            const auto slotCenter = GetDialSlotCenter(dialCenter, dialSize, slotIndex);
            RenderDialSlotPreview(drawList, *item, slotCenter, previewSize, unknownTexture, GetDialRevealProgress(static_cast<std::size_t>(slotIndex)));
            if (IsInventoryItemMarked(item->formID)) {
                DrawMarkedDialSlotRing(drawList, slotCenter, dialSize);
            }
        }
        DrawDialExitIcon(
            drawList,
            exitIconTexture,
            GetDialSlotCenter(dialCenter, dialSize, kDialExitSlot),
            dialSize,
            cursorOnExit);

        ImGui::SetCursorScreenPos({ dialTopLeft.x, dialBottomRight.y + 8.0f });

        if (exitRequestedByControllerConfirm) {
            RenderDialExitCenterInfo(drawList, dialCenter, dialSize);
            const auto controllerConfirmStillDown =
                controllerConfirmDown.load(std::memory_order_acquire) ||
                ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
            if (!controllerConfirmStillDown) {
                exitRequestedByControllerConfirm = false;
                CommitMarkedInventoryItemsAndClose(snapshot);
            }
            return;
        }

        const auto activateInputPressed = IsTakeSelectedInputPressed();
        if (cursorOnExit && activateInputPressed) {
            if (takeSelectedInputWasControllerConfirm) {
                exitRequestedByControllerConfirm = true;
                RenderDialExitCenterInfo(drawList, dialCenter, dialSize);
            } else {
                PickpocketEvents::SuppressPhase1ActivateUntilRelease();
                CommitMarkedInventoryItemsAndClose(snapshot);
            }
            return;
        }

        if (cursorOnExit) {
            RenderDialExitCenterInfo(drawList, dialCenter, dialSize);
        } else if (selectedItem) {
            RenderDialCenterInfo(drawList, dialCenter, dialSize, selectedItem, selectedRevealProgress);

            if (selectedCanMark && activateInputPressed) {
                MarkInventoryItemForTransfer(*selectedItem);
            }
        }
    }

    void RenderCapturedInventoryDebugList(const PickpocketInventory::Snapshot& snapshot) {
        if (snapshot.items.empty()) {
            ImGui::TextWrapped("The target inventory is empty.");
            return;
        }

        const auto childHeight = std::max(ImGui::GetContentRegionAvail().y, 300.0f);
        std::size_t renderedItems = 0;
        if (ImGui::BeginChild("##PickpocketInventoryItems", ImGui::ImVec2(0.0f, childHeight), 0, 0)) {
            for (const auto& item : snapshot.items) {
                if (RenderInventoryItem(item)) {
                    ++renderedItems;
                }
            }

            if (renderedItems == 0) {
                ImGui::TextWrapped("No named inventory items with renderable models were found.");
            }
        }
        ImGui::EndChild();
    }

    void RenderCapturedInventoryDebug() {
        const auto snapshot = PickpocketInventory::GetSnapshot();
        if (snapshot.targetName.empty()) {
            ImGui::TextWrapped("No pickpocket inventory captured yet.");
            return;
        }

        SyncDialTimer(snapshot);

        RenderCapturedInventoryDebugList(snapshot);
    }

    void RenderTriggeredInventoryDebugPanel() {
        const auto screenSize = ImGui::GetIO()->DisplaySize;
        const auto margin = 32.0f;
        const auto panelWidth = std::max(std::min(screenSize.x - (margin * 2.0f), 980.0f), 320.0f);
        const auto panelHeight = std::max(screenSize.y - (margin * 2.0f), 320.0f);

        ImGui::SetCursorPos({ margin, margin });
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ImVec4(0.02f, 0.02f, 0.02f, 0.88f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ImVec4(0.72f, 0.72f, 0.72f, 0.55f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 16.0f, 14.0f });
        if (ImGui::BeginChild(
                "##PickpocketTriggeredDebugPanel",
                { panelWidth, panelHeight },
                ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding,
                0)) {
            RenderCapturedInventoryDebug();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }


    void RenderCapturedInventoryMinigame() {
        const auto snapshot = PickpocketInventory::GetSnapshot();
        const auto session = PickpocketSession::GetSnapshot();
        const auto targetHandle = snapshot.targetHandle.native_handle();
        const auto isPhase2 =
            session.state == PickpocketSession::State::Phase2Preparing ||
            session.state == PickpocketSession::State::Phase2Active;
        if (!isPhase2) {
            ClosePickpocketPresentation();
            return;
        }
        if (session.targetHandle != targetHandle || snapshot.targetName.empty()) {
            ClosePickpocketPresentation();
            PickpocketSession::Abort("phase 2 inventory snapshot became invalid");
            return;
        }

        if (exitRequestedByControllerConfirm) {
            RenderPickpocketDial(snapshot);
            return;
        }

        const auto closeInputPressed = IsClosePickpocketWindowInputPressed();
        if (closeInputPressed) {
            if (session.state == PickpocketSession::State::Phase2Preparing ||
                CanClosePickpocketInventoryWindowWithoutFailure(snapshot)) {
                ClosePickpocketInventoryWindow();
            } else {
                FailPickpocketInventoryAndClose("phase 2 cancel input pressed");
            }
            return;
        }

        // Both remaining paths sync the dial timer themselves.
        if (session.state == PickpocketSession::State::Phase2Preparing) {
            (void)ConsumeControllerAction(Input::MenuAction::Place);
            RenderPickpocketDial(snapshot);
            return;
        }

        if (IsOpenVanillaDepositInputPressed()) {
            (void)OpenVanillaDepositMenuAndClose(snapshot);
            return;
        }

        if (PickpocketConfig::settings.showDebugPickpocketListDuringPhase2) {
            RenderTriggeredInventoryDebugPanel();
            return;
        }

        RenderPickpocketDial(snapshot);
        if (pickpocketInventoryWindow && pickpocketInventoryWindow->IsOpen) {
            RenderMovementControlHints();
            RenderPlantItemsHintButton(snapshot);
        }
    }

}
