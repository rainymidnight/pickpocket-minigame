#include "SkyUIIcons.h"

#include "DialGeometry.h"
#include "logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace SkyUIIcons {
    namespace {
        constexpr auto kMenuName = "PickpocketMinigameIconLayer"sv;
        constexpr auto kMovieName = "PickpocketMinigameIcons"sv;
        constexpr auto kDefaultIconSource = "skyui/icons_item_psychosteve.swf"sv;
        constexpr std::uint32_t kProbeTimeoutFrames = 120;
        constexpr std::uint32_t kProbeStableFrames = 3;
        constexpr std::uint32_t kCloseSettleFrames = 2;
        constexpr float kIconLibrarySize = 128.0f;
        constexpr float kIconFillRatio = 0.78f;
        constexpr float kDialAnnulusRadius = DialGeometry::kSlotRingRadius;
        constexpr float kDialAnnulusThickness = 360.0f / DialGeometry::kDesignSize;
        constexpr float kDialAnnulusAlpha = 0.80f;

        enum class ProbeStage {
            Idle,
            Requested,
            Opening,
            Closing
        };

        struct Descriptor {
            std::string source{ kDefaultIconSource };
            std::string label{ "default_misc" };
            std::uint32_t color = 0xFFFFFF;

            friend bool operator==(const Descriptor&, const Descriptor&) = default;
        };

        struct ItemSpec {
            RE::FormID formID = 0;
            Descriptor fallback;

            friend bool operator==(const ItemSpec&, const ItemSpec&) = default;
        };

        struct UnderlayLayout {
            float screenWidth = 0.0f;
            float screenHeight = 0.0f;
            Rect dialBounds;
            float dimmerAlpha = 0.0f;

            friend bool operator==(const UnderlayLayout&, const UnderlayLayout&) = default;
        };

        struct LayoutSnapshot {
            UnderlayLayout underlay;
            std::vector<Slot> slots;

            friend bool operator==(const LayoutSnapshot&, const LayoutSnapshot&) = default;
        };

        struct ExtractedDescriptors {
            std::unordered_map<RE::FormID, Descriptor> byForm;
            std::uint32_t entryCount = 0;

            friend bool operator==(const ExtractedDescriptors&, const ExtractedDescriptors&) = default;
        };

        struct SharedState {
            std::mutex mutex;
            bool registered = false;
            bool registrationFailed = false;
            bool active = false;
            bool presenting = false;
            bool hostReady = false;
            bool hostFailed = false;
            bool resolutionComplete = false;
            bool layoutSubmitted = false;
            ProbeStage probeStage = ProbeStage::Idle;
            bool probeTimedOut = false;
            bool cursorSuppressed = false;
            bool systemCursorSuppressed = false;
            bool systemCursorWasVisible = false;
            bool probeBlurNeutralized = false;
            std::uint32_t probeFrames = 0;
            std::uint32_t stableFrames = 0;
            std::uint32_t closeSettleFrames = 0;
            std::uint32_t probeBlurBaselineCount = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t descriptorRevision = 1;
            std::uint64_t underlayRevision = 1;
            std::uint64_t layoutRevision = 1;
            RE::ObjectRefHandle targetHandle;
            std::vector<ItemSpec> items;
            std::unordered_map<RE::FormID, Descriptor> descriptors;
            std::unordered_set<RE::FormID> loaded;
            std::unordered_set<RE::FormID> failed;
            ExtractedDescriptors lastExtracted;
            ExtractedDescriptors pendingExtracted;
            LayoutSnapshot layout;
        };

        struct MenuSnapshot {
            bool active = false;
            bool presenting = false;
            bool resolutionComplete = false;
            std::vector<RE::FormID> iconKeys;
            std::unordered_map<RE::FormID, Descriptor> descriptors;
            std::unordered_set<RE::FormID> loaded;
            LayoutSnapshot layout;
            std::uint64_t descriptorRevision = 0;
            std::uint64_t underlayRevision = 0;
            std::uint64_t layoutRevision = 0;
        };

        SharedState& State()
        {
            static SharedState state;
            return state;
        }

        void SuppressCursorForProbe()
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            if (!state.active || state.probeStage == ProbeStage::Idle) {
                return;
            }

            if (const auto systemCursor = RE::MenuCursor::GetSingleton()) {
                if (!state.systemCursorSuppressed) {
                    state.systemCursorWasVisible = systemCursor->GetRuntimeData().showCursorCount >= 0;
                    state.systemCursorSuppressed = true;
                }
                systemCursor->SetCursorVisibility(false);
            }

            const auto ui = RE::UI::GetSingleton();
            const auto cursor = ui ? ui->GetMenu<RE::CursorMenu>() : nullptr;
            if (cursor && cursor->uiMovie) {
                state.cursorSuppressed = true;
                cursor->uiMovie->SetVisible(false);
            }
        }

        [[nodiscard]] bool IsProbeLifecycleActive()
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            return
                state.active &&
                !state.resolutionComplete &&
                state.probeStage != ProbeStage::Idle;
        }

        void RestoreCursorAfterProbe()
        {
            bool cursorWasSuppressed = false;
            bool restoreSystemCursor = false;
            bool systemCursorWasVisible = false;
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                cursorWasSuppressed = state.cursorSuppressed;
                state.cursorSuppressed = false;
                restoreSystemCursor = state.systemCursorSuppressed;
                systemCursorWasVisible = state.systemCursorWasVisible;
                state.systemCursorSuppressed = false;
                state.systemCursorWasVisible = false;
            }
            if (restoreSystemCursor) {
                if (const auto systemCursor = RE::MenuCursor::GetSingleton()) {
                    systemCursor->SetCursorVisibility(systemCursorWasVisible);
                }
            }
            if (cursorWasSuppressed) {
                const auto ui = RE::UI::GetSingleton();
                const auto cursor = ui ? ui->GetMenu<RE::CursorMenu>() : nullptr;
                if (cursor && cursor->uiMovie) {
                    cursor->uiMovie->SetVisible(false);
                }
            }
        }

        void SuppressContainerForProbe(RE::ContainerMenu* menu)
        {
            if (!menu || !menu->uiMovie ||
                RE::ContainerMenu::GetContainerMode() != RE::ContainerMenu::ContainerMode::kPickpocket) {
                return;
            }

            const auto targetHandle = RE::ContainerMenu::GetTargetRefHandle();
            const auto blur = RE::UIBlurManager::GetSingleton();
            const auto blurCount = blur ? blur->blurCount : 0U;
            bool neutralizeBlur = false;
            auto& state = State();
            {
                std::scoped_lock lock(state.mutex);
                if (!state.active || state.resolutionComplete ||
                    state.probeStage == ProbeStage::Idle ||
                    state.targetHandle.native_handle() != targetHandle) {
                    return;
                }
                if (blur && !state.probeBlurNeutralized &&
                    blurCount > state.probeBlurBaselineCount) {
                    state.probeBlurNeutralized = true;
                    neutralizeBlur = true;
                }
            }
            // This ContainerMenu exists only long enough for SkyUI/I4 to
            // populate its item descriptors. Let it remain on the UI
            // stack so Scaleform advances, but keep it from putting the
            // game into item-menu mode, pausing it, hiding the HUD, or
            // activating custom/cursor rendering.
            menu->menuFlags.reset(
                RE::UI_MENU_FLAGS::kPausesGame,
                RE::UI_MENU_FLAGS::kUsesCursor,
                RE::UI_MENU_FLAGS::kUsesMenuContext,
                RE::UI_MENU_FLAGS::kDisablePauseMenu,
                RE::UI_MENU_FLAGS::kUpdateUsesCursor,
                RE::UI_MENU_FLAGS::kInventoryItemMenu,
                RE::UI_MENU_FLAGS::kCustomRendering,
                RE::UI_MENU_FLAGS::kUsesBlurredBackground);
            menu->uiMovie->SetVisible(false);
            if (neutralizeBlur) {
                // ContainerMenu raises the native blur counter in its own
                // lifecycle even when its movie is hidden. Remove only that
                // one contribution; put it back immediately before vanilla
                // closes the menu so its matching decrement stays balanced.
                blur->DecrementBlurCount();
            }
        }

        void RestoreProbeBlurBeforeContainerClose(RE::ContainerMenu* menu)
        {
            if (!menu ||
                RE::ContainerMenu::GetContainerMode() != RE::ContainerMenu::ContainerMode::kPickpocket) {
                return;
            }

            const auto targetHandle = RE::ContainerMenu::GetTargetRefHandle();
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                if (!state.probeBlurNeutralized ||
                    state.targetHandle.native_handle() != targetHandle) {
                    return;
                }
                state.probeBlurNeutralized = false;
            }
            if (const auto blur = RE::UIBlurManager::GetSingleton()) {
                blur->IncrementBlurCount();
            }
        }

        using CursorProcessMessage = RE::UI_MESSAGE_RESULTS (*)(RE::CursorMenu*, RE::UIMessage&);
        using CursorAdvanceMovie = void (*)(RE::CursorMenu*, float, std::uint32_t);
        using ContainerProcessMessage = RE::UI_MESSAGE_RESULTS (*)(RE::ContainerMenu*, RE::UIMessage&);
        using ContainerAdvanceMovie = void (*)(RE::ContainerMenu*, float, std::uint32_t);
        using HUDProcessMessage = RE::UI_MESSAGE_RESULTS (*)(RE::HUDMenu*, RE::UIMessage&);
        CursorProcessMessage originalCursorProcessMessage = nullptr;
        CursorAdvanceMovie originalCursorAdvanceMovie = nullptr;
        ContainerProcessMessage originalContainerProcessMessage = nullptr;
        ContainerAdvanceMovie originalContainerAdvanceMovie = nullptr;
        HUDProcessMessage originalHUDProcessMessage = nullptr;

        RE::UI_MESSAGE_RESULTS HUDProcessMessageHook(
            RE::HUDMenu* menu,
            RE::UIMessage& message)
        {
            if (message.type == RE::UI_MESSAGE_TYPE::kUpdate &&
                message.data && IsProbeLifecycleActive()) {
                const auto data = skyrim_cast<RE::HUDData*>(message.data);
                if (data && data->type == RE::GetHUDMessageType(RE::HUD_MESSAGE_TYPE::kSetMode)) {
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }
            }
            return originalHUDProcessMessage(menu, message);
        }

        RE::UI_MESSAGE_RESULTS CursorProcessMessageHook(
            RE::CursorMenu* menu,
            RE::UIMessage& message)
        {
            const auto result = originalCursorProcessMessage(menu, message);
            SuppressCursorForProbe();
            return result;
        }

        void CursorAdvanceMovieHook(
            RE::CursorMenu* menu,
            float interval,
            std::uint32_t currentTime)
        {
            originalCursorAdvanceMovie(menu, interval, currentTime);
            SuppressCursorForProbe();
        }

        RE::UI_MESSAGE_RESULTS ContainerProcessMessageHook(
            RE::ContainerMenu* menu,
            RE::UIMessage& message)
        {
            const bool closing =
                message.type == RE::UI_MESSAGE_TYPE::kHide ||
                message.type == RE::UI_MESSAGE_TYPE::kForceHide;
            if (closing) {
                RestoreProbeBlurBeforeContainerClose(menu);
            } else {
                // Clear presentation-affecting flags before vanilla handles
                // the message as well. ContainerMenu otherwise uses its
                // original item-menu state to fade individual HUD clips even
                // though the flags are cleared before it reaches the stack.
                SuppressContainerForProbe(menu);
            }
            const auto result = originalContainerProcessMessage(menu, message);
            if (!closing) {
                SuppressContainerForProbe(menu);
            }
            return result;
        }

        void ContainerAdvanceMovieHook(
            RE::ContainerMenu* menu,
            float interval,
            std::uint32_t currentTime)
        {
            originalContainerAdvanceMovie(menu, interval, currentTime);
            SuppressContainerForProbe(menu);
        }

        [[nodiscard]] bool InstallProbeSuppressionHooks()
        {
            if (originalCursorProcessMessage && originalCursorAdvanceMovie &&
                originalContainerProcessMessage && originalContainerAdvanceMovie &&
                originalHUDProcessMessage) {
                return true;
            }

            try {
                constexpr std::size_t kProcessMessageVtableIndex = 4;
                constexpr std::size_t kAdvanceMovieVtableIndex = 5;
                REL::Relocation<std::uintptr_t> cursorVtable{ RE::CursorMenu::VTABLE[0] };
                originalCursorProcessMessage = reinterpret_cast<CursorProcessMessage>(
                    cursorVtable.write_vfunc(kProcessMessageVtableIndex, &CursorProcessMessageHook));
                originalCursorAdvanceMovie = reinterpret_cast<CursorAdvanceMovie>(
                    cursorVtable.write_vfunc(kAdvanceMovieVtableIndex, &CursorAdvanceMovieHook));

                REL::Relocation<std::uintptr_t> containerVtable{ RE::ContainerMenu::VTABLE[0] };
                originalContainerProcessMessage = reinterpret_cast<ContainerProcessMessage>(
                    containerVtable.write_vfunc(kProcessMessageVtableIndex, &ContainerProcessMessageHook));
                originalContainerAdvanceMovie = reinterpret_cast<ContainerAdvanceMovie>(
                    containerVtable.write_vfunc(kAdvanceMovieVtableIndex, &ContainerAdvanceMovieHook));

                REL::Relocation<std::uintptr_t> hudVtable{ RE::HUDMenu::VTABLE[0] };
                originalHUDProcessMessage = reinterpret_cast<HUDProcessMessage>(
                    hudVtable.write_vfunc(kProcessMessageVtableIndex, &HUDProcessMessageHook));
            } catch (const std::exception& error) {
                logger::error("Could not install menu probe-suppression hooks: {}", error.what());
                return false;
            }

            if (!originalCursorProcessMessage || !originalCursorAdvanceMovie ||
                !originalContainerProcessMessage || !originalContainerAdvanceMovie ||
                !originalHUDProcessMessage) {
                logger::error("Could not install menu probe-suppression hooks: an original function was null");
                return false;
            }

            logger::info("Installed CursorMenu, ContainerMenu, and HUDMenu probe-suppression hooks");
            return true;
        }

        [[nodiscard]] Descriptor FallbackDescriptor(const PickpocketInventory::Item& item)
        {
            Descriptor descriptor;
            if (!item.object) {
                return descriptor;
            }

            switch (item.object->GetFormType()) {
            case RE::FormType::Scroll:
                descriptor.label = "default_scroll";
                break;
            case RE::FormType::Armor:
                descriptor.label = "default_armor";
                descriptor.color = 0xEDDA87;
                break;
            case RE::FormType::Book:
                descriptor.label = "default_book";
                break;
            case RE::FormType::Ingredient:
                descriptor.label = "default_ingredient";
                break;
            case RE::FormType::Light:
                descriptor.label = "misc_torch";
                break;
            case RE::FormType::Weapon:
                descriptor.label = "default_weapon";
                descriptor.color = 0xA4A5BF;
                break;
            case RE::FormType::Ammo:
                descriptor.label = "weapon_arrow";
                descriptor.color = 0xA89E8C;
                break;
            case RE::FormType::KeyMaster:
                descriptor.label = "default_key";
                break;
            case RE::FormType::AlchemyItem:
                descriptor.label = "default_potion";
                break;
            case RE::FormType::SoulGem:
                descriptor.label = "misc_soulgem";
                descriptor.color = 0xE3E0FF;
                break;
            case RE::FormType::Misc:
                if (item.isGold) {
                    descriptor.label = "misc_gold";
                    descriptor.color = 0xCCCC33;
                } else if (item.isGem) {
                    descriptor.label = "misc_gem";
                    descriptor.color = 0xFFB0D1;
                }
                break;
            default:
                break;
            }
            return descriptor;
        }

        [[nodiscard]] MenuSnapshot CaptureMenuSnapshot()
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            std::vector<RE::FormID> iconKeys;
            iconKeys.reserve(state.items.size());
            std::ranges::transform(state.items, std::back_inserter(iconKeys), &ItemSpec::formID);
            return {
                state.active,
                state.presenting,
                state.resolutionComplete,
                std::move(iconKeys),
                state.descriptors,
                state.loaded,
                state.layout,
                state.descriptorRevision,
                state.underlayRevision,
                state.layoutRevision
            };
        }

        void SetHostReady(bool ready, bool failed = false)
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            state.hostReady = ready;
            if (ready) {
                state.hostFailed = false;
            } else if (failed) {
                state.hostFailed = true;
            }
        }

        void ReportLoaded(RE::FormID formID)
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            state.loaded.insert(formID);
            state.failed.erase(formID);
            ++state.layoutRevision;
        }

        void ReportFailed(RE::FormID formID, std::string_view reason)
        {
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                state.loaded.erase(formID);
                state.failed.insert(formID);
                ++state.layoutRevision;
            }
            logger::warn("{} for {:08X}", reason, formID);
        }

        [[nodiscard]] std::optional<RE::GFxValue> GetContainerEntryList(RE::ContainerMenu& menu)
        {
            RE::GFxValue inventoryLists;
            auto root = menu.GetRuntimeData().root;
            if (root.IsObject()) {
                root.GetMember("inventoryLists", std::addressof(inventoryLists));
            }

            RE::GFxValue itemList;
            if (inventoryLists.IsObject()) {
                inventoryLists.GetMember("itemList", std::addressof(itemList));
                if (!itemList.IsObject()) {
                    RE::GFxValue panelContainer;
                    inventoryLists.GetMember("panelContainer", std::addressof(panelContainer));
                    if (panelContainer.IsObject()) {
                        panelContainer.GetMember("itemList", std::addressof(itemList));
                    }
                }
            }

            if (!itemList.IsObject() && menu.uiMovie) {
                constexpr std::array paths{
                    "_root.Menu_mc.inventoryLists.itemList",
                    "_root.Menu_mc.inventoryLists.panelContainer.itemList",
                    "_root.inventoryLists.itemList",
                    "_root.inventoryLists.panelContainer.itemList"
                };
                for (const auto path : paths) {
                    if (menu.uiMovie->GetVariable(std::addressof(itemList), path) && itemList.IsObject()) {
                        break;
                    }
                }
            }

            if (!itemList.IsObject()) {
                return std::nullopt;
            }

            RE::GFxValue entries;
            itemList.GetMember("_entryList", std::addressof(entries));
            if (!entries.IsArray()) {
                itemList.GetMember("entryList", std::addressof(entries));
            }
            return entries.IsArray() ? std::optional{ entries } : std::nullopt;
        }

        [[nodiscard]] ExtractedDescriptors ExtractDescriptors(
            RE::ContainerMenu& menu,
            const std::vector<ItemSpec>& expected)
        {
            ExtractedDescriptors result;
            const auto entries = GetContainerEntryList(menu);
            if (!entries) {
                return result;
            }

            result.entryCount = entries->GetArraySize();
            for (std::uint32_t index = 0; index < result.entryCount; ++index) {
                RE::GFxValue entry;
                entries->GetElement(index, std::addressof(entry));
                if (!entry.IsObject()) {
                    continue;
                }

                RE::GFxValue formIDValue;
                RE::GFxValue labelValue;
                entry.GetMember("formId", std::addressof(formIDValue));
                entry.GetMember("iconLabel", std::addressof(labelValue));
                if (!formIDValue.IsNumber() || !labelValue.IsString()) {
                    continue;
                }

                const auto formID = static_cast<RE::FormID>(formIDValue.GetNumber());
                const auto item = std::ranges::find(expected, formID, &ItemSpec::formID);
                if (item == expected.end()) {
                    continue;
                }

                auto descriptor = item->fallback;
                descriptor.label = labelValue.GetString();

                RE::GFxValue sourceValue;
                entry.GetMember("iconSource", std::addressof(sourceValue));
                if (sourceValue.IsString() && sourceValue.GetString()[0] != '\0') {
                    descriptor.source = sourceValue.GetString();
                }

                RE::GFxValue colorValue;
                entry.GetMember("iconColor", std::addressof(colorValue));
                if (colorValue.IsNumber()) {
                    descriptor.color = static_cast<std::uint32_t>(colorValue.GetNumber());
                }

                result.byForm.insert_or_assign(formID, std::move(descriptor));
            }
            return result;
        }

        [[nodiscard]] bool HasEveryExpectedForm(
            const std::vector<ItemSpec>& expected,
            const ExtractedDescriptors& extracted)
        {
            return std::ranges::all_of(expected, [&](const ItemSpec& item) {
                return extracted.byForm.contains(item.formID);
            });
        }

        void HideContainerMenu()
        {
            if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(RE::ContainerMenu::MENU_NAME.data(), RE::UI_MESSAGE_TYPE::kForceHide, nullptr);
            }
        }

        void BeginProbeClose(
            std::uint64_t sessionRevision,
            const ExtractedDescriptors& extracted,
            bool timedOut)
        {
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                if (!state.active || state.sessionRevision != sessionRevision || state.resolutionComplete) {
                    return;
                }
                state.pendingExtracted = extracted;
                state.probeTimedOut = timedOut;
                state.probeStage = ProbeStage::Closing;
                state.closeSettleFrames = 0;
            }
            HideContainerMenu();
        }

        void FinishProbe(std::uint64_t sessionRevision)
        {
            std::size_t fallbackCount = 0;
            ExtractedDescriptors extracted;
            bool timedOut = false;
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                if (!state.active || state.sessionRevision != sessionRevision || state.resolutionComplete) {
                    return;
                }

                extracted = std::move(state.pendingExtracted);
                timedOut = state.probeTimedOut;

                state.descriptors.clear();
                for (const auto& item : state.items) {
                    if (const auto found = extracted.byForm.find(item.formID); found != extracted.byForm.end()) {
                        state.descriptors.emplace(item.formID, found->second);
                    } else {
                        state.descriptors.emplace(item.formID, item.fallback);
                        ++fallbackCount;
                    }
                }
                state.loaded.clear();
                state.failed.clear();
                state.resolutionComplete = true;
                state.probeStage = ProbeStage::Idle;
                state.probeTimedOut = false;
                state.closeSettleFrames = 0;
                ++state.descriptorRevision;
            }

            if (timedOut) {
                logger::warn(
                    "SkyUI icon probe timed out ({} list entries, {} resolved, {} fallbacks)",
                    extracted.entryCount,
                    extracted.byForm.size(),
                    fallbackCount);
            } else {
                logger::info(
                    "Resolved {} SkyUI item icon(s); {} item(s) use stock fallbacks",
                    extracted.byForm.size(),
                    fallbackCount);
            }
            RestoreCursorAfterProbe();
        }

        void AdvanceProbe()
        {
            bool requestOpen = false;
            bool waitingForClose = false;
            std::uint32_t frames = 0;
            std::uint64_t sessionRevision = 0;
            RE::ObjectRefHandle targetHandle;
            std::vector<ItemSpec> expected;
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                if (!state.active || state.resolutionComplete) {
                    return;
                }

                if (state.probeStage == ProbeStage::Closing) {
                    waitingForClose = true;
                } else if (state.probeStage == ProbeStage::Requested) {
                    state.probeStage = ProbeStage::Opening;
                    requestOpen = true;
                }
                if (state.probeStage == ProbeStage::Opening) {
                    frames = ++state.probeFrames;
                }
                sessionRevision = state.sessionRevision;
                targetHandle = state.targetHandle;
                expected = state.items;
            }

            SuppressCursorForProbe();

            if (waitingForClose) {
                const auto ui = RE::UI::GetSingleton();
                const bool menuOpen = ui && ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME);
                bool settled = false;
                {
                    auto& state = State();
                    std::scoped_lock lock(state.mutex);
                    if (!state.active || state.sessionRevision != sessionRevision ||
                        state.probeStage != ProbeStage::Closing) {
                        return;
                    }
                    state.closeSettleFrames = menuOpen ? 0U : state.closeSettleFrames + 1U;
                    settled = state.closeSettleFrames >= kCloseSettleFrames;
                }
                if (settled) {
                    FinishProbe(sessionRevision);
                }
                return;
            }

            if (requestOpen) {
                const auto target = targetHandle.get();
                if (!target) {
                    BeginProbeClose(sessionRevision, {}, true);
                    return;
                }
                RE::ContainerMenu::OpenMenu(target.get(), RE::ContainerMenu::ContainerMode::kPickpocket);
                SuppressCursorForProbe();
            }

            ExtractedDescriptors extracted;
            bool matchingMenuOpen = false;
            if (const auto ui = RE::UI::GetSingleton()) {
                const auto menu = ui->GetMenu<RE::ContainerMenu>();
                matchingMenuOpen =
                    menu &&
                    RE::ContainerMenu::GetContainerMode() == RE::ContainerMenu::ContainerMode::kPickpocket &&
                    RE::ContainerMenu::GetTargetRefHandle() == targetHandle.native_handle();
                if (matchingMenuOpen) {
                    if (menu->uiMovie) {
                        menu->uiMovie->SetVisible(false);
                    }
                    extracted = ExtractDescriptors(*menu, expected);
                }
            }

            bool stable = false;
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                if (!state.active || state.sessionRevision != sessionRevision || state.resolutionComplete) {
                    return;
                }
                if (matchingMenuOpen && extracted == state.lastExtracted) {
                    ++state.stableFrames;
                } else {
                    state.lastExtracted = extracted;
                    state.stableFrames = matchingMenuOpen ? 1U : 0U;
                }
                stable = extracted.entryCount > 0 && state.stableFrames >= kProbeStableFrames;
            }

            const bool resolved =
                (matchingMenuOpen && HasEveryExpectedForm(expected, extracted)) || stable;
            if (resolved || frames >= kProbeTimeoutFrames) {
                BeginProbeClose(sessionRevision, extracted, !resolved);
            }
        }

        [[nodiscard]] RE::FormID FormIDFromListener(const RE::GFxValue& listener)
        {
            RE::GFxValue formID;
            listener.GetMember("formID", std::addressof(formID));
            return formID.IsNumber() ? static_cast<RE::FormID>(formID.GetNumber()) : RE::FormID{ 0 };
        }

        void ApplyColor(const RE::GFxValue& icon, std::uint32_t rgb)
        {
            icon.VisitMembers([rgb]([[maybe_unused]] const char* name, const RE::GFxValue& value) {
                if (!value.IsDisplayObject()) {
                    return;
                }

                RE::GRenderer::Cxform transform;
                transform.matrix[0][0] = 0.0f;
                transform.matrix[1][0] = 0.0f;
                transform.matrix[2][0] = 0.0f;
                transform.matrix[0][1] = static_cast<float>((rgb >> 16U) & 0xFFU);
                transform.matrix[1][1] = static_cast<float>((rgb >> 8U) & 0xFFU);
                transform.matrix[2][1] = static_cast<float>(rgb & 0xFFU);
                const_cast<RE::GFxValue&>(value).SetCxform(transform);
            });
        }

        class LoadInitHandler final : public RE::GFxFunctionHandler {
        public:
            void Call(Params& params) override
            {
                if (!params.thisPtr || params.argCount < 1 || !params.args[0].IsDisplayObject()) {
                    return;
                }

                auto& icon = params.args[0];
                RE::GFxValue label;
                params.thisPtr->GetMember("iconLabel", std::addressof(label));
                if (label.IsString()) {
                    icon.Invoke("gotoAndStop", nullptr, std::addressof(label), 1);
                }

                RE::GFxValue color;
                params.thisPtr->GetMember("iconColor", std::addressof(color));
                ApplyColor(icon, color.IsNumber() ? static_cast<std::uint32_t>(color.GetNumber()) : 0xFFFFFF);
                icon.SetMember("_visible", false);
                ReportLoaded(FormIDFromListener(*params.thisPtr));
            }
        };

        class LoadErrorHandler final : public RE::GFxFunctionHandler {
        public:
            void Call(Params& params) override
            {
                if (!params.thisPtr) {
                    return;
                }
                std::string reason = "Scaleform MovieClipLoader could not load an item icon source";
                if (params.argCount >= 2 && params.args[1].IsString()) {
                    reason = std::format("SkyUI item icon load failed: {}", params.args[1].GetString());
                }
                const auto formID = FormIDFromListener(*params.thisPtr);
                ReportFailed(formID, reason);
            }
        };

        void InvokePoint(RE::GFxValue& clip, const char* method, double x, double y)
        {
            std::array args{ RE::GFxValue(x), RE::GFxValue(y) };
            clip.Invoke(method, args);
        }

        void InvokeCurve(
            RE::GFxValue& clip,
            double controlX,
            double controlY,
            double anchorX,
            double anchorY)
        {
            std::array args{
                RE::GFxValue(controlX),
                RE::GFxValue(controlY),
                RE::GFxValue(anchorX),
                RE::GFxValue(anchorY)
            };
            clip.Invoke("curveTo", args);
        }

        void DrawFilledRect(RE::GFxValue& clip, const Rect& rect, std::uint32_t color, float alpha)
        {
            std::array fillArgs{
                RE::GFxValue(static_cast<double>(color)),
                RE::GFxValue(static_cast<double>(alpha))
            };
            clip.Invoke("beginFill", fillArgs);
            InvokePoint(clip, "moveTo", rect.x, rect.y);
            InvokePoint(clip, "lineTo", rect.x + rect.width, rect.y);
            InvokePoint(clip, "lineTo", rect.x + rect.width, rect.y + rect.height);
            InvokePoint(clip, "lineTo", rect.x, rect.y + rect.height);
            InvokePoint(clip, "lineTo", rect.x, rect.y);
            clip.Invoke("endFill");
        }

        void DrawCircleStroke(
            RE::GFxValue& clip,
            double centerX,
            double centerY,
            double radius,
            double thickness,
            std::uint32_t color,
            float alpha)
        {
            std::array lineArgs{
                RE::GFxValue(thickness),
                RE::GFxValue(static_cast<double>(color)),
                RE::GFxValue(static_cast<double>(alpha))
            };
            clip.Invoke("lineStyle", lineArgs);

            constexpr int segmentCount = 8;
            constexpr double fullCircle = 6.28318530717958647692;
            constexpr double segmentAngle = fullCircle / static_cast<double>(segmentCount);
            const auto controlRadius = radius / std::cos(segmentAngle * 0.5);

            InvokePoint(clip, "moveTo", centerX + radius, centerY);
            for (int segment = 0; segment < segmentCount; ++segment) {
                const auto startAngle = static_cast<double>(segment) * segmentAngle;
                const auto endAngle = startAngle + segmentAngle;
                const auto middleAngle = startAngle + segmentAngle * 0.5;
                InvokeCurve(
                    clip,
                    centerX + std::cos(middleAngle) * controlRadius,
                    centerY + std::sin(middleAngle) * controlRadius,
                    centerX + std::cos(endAngle) * radius,
                    centerY + std::sin(endAngle) * radius);
            }
        }

        class IconLayerMenu final : public RE::IMenu {
        public:
            IconLayerMenu()
            {
                depthPriority = 12;
                menuFlags.set(
                    RE::UI_MENU_FLAGS::kRequiresUpdate,
                    RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
                    RE::UI_MENU_FLAGS::kRendersUnderPauseMenu);

                const auto manager = RE::BSScaleformManager::GetSingleton();
                const bool loaded = manager && manager->LoadMovieEx(
                    this,
                    kMovieName,
                    RE::GFxMovieView::ScaleModeType::kShowAll,
                    0.0f,
                    [](RE::GFxMovieDef* definition) {
                        definition->SetState(RE::GFxState::StateType::kLog, RE::make_gptr<RE::GFxLog>().get());
                    });
                if (!loaded || !uiMovie) {
                    const auto error = std::format("Could not load Interface/{}.swf", kMovieName);
                    logger::error("{}", error);
                    SetHostReady(false, true);
                    return;
                }
                // The movie can stay registered and advance while hidden;
                // depthPriority is sufficient to place its phase-2 content.
                // Marking a hidden preload menu as topmost prevents Skyrim
                // from rendering lower surfaces such as HUD Menu in phase 1.
                uiMovie->SetVisible(false);
                uiMovie->SetBackgroundAlpha(0.0f);
                Initialize();
            }

            ~IconLayerMenu() override
            {
                SetHostReady(false);
            }

            void PostCreate() override
            {
                RE::IMenu::PostCreate();
                Initialize();
            }

            void AdvanceMovie(float interval, std::uint32_t currentTime) override
            {
                RE::IMenu::AdvanceMovie(interval, currentTime);
                AdvanceProbe();
                Synchronize();
            }

            static RE::IMenu* Create()
            {
                return new IconLayerMenu();
            }

        private:
            struct IconSlot {
                RE::FormID formID = 0;
                RE::GFxValue clip;
                RE::GFxValue loader;
                RE::GFxValue listener;
            };

            void Initialize()
            {
                if (initialized_ || !uiMovie ||
                    !uiMovie->GetVariable(std::addressof(root_), "_root") || !root_.IsObject()) {
                    return;
                }

                std::array underlayArgs{ RE::GFxValue("pickpocketUnderlay"), RE::GFxValue(1.0) };
                root_.Invoke("createEmptyMovieClip", std::addressof(underlay_), underlayArgs);
                if (!underlay_.IsDisplayObject()) {
                    logger::error("Could not create the Scaleform icon underlay");
                    SetHostReady(false, true);
                    return;
                }

                loadInitHandler_ = RE::make_gptr<LoadInitHandler>();
                loadErrorHandler_ = RE::make_gptr<LoadErrorHandler>();
                initialized_ = true;
                root_.SetMember("_visible", false);
                SetHostReady(true);
            }

            void ClearIcons()
            {
                for (auto& slot : slots_) {
                    if (slot.clip.IsDisplayObject()) {
                        slot.clip.Invoke("removeMovieClip");
                    }
                }
                slots_.clear();
                slotKeys_.clear();
            }

            void RebuildIcons(const MenuSnapshot& snapshot, std::vector<RE::FormID> keys)
            {
                ClearIcons();
                slotKeys_ = std::move(keys);
                std::uint32_t slotIndex = 0;
                for (const auto formID : slotKeys_) {
                    const auto descriptor = snapshot.descriptors.find(formID);
                    if (descriptor == snapshot.descriptors.end()) {
                        continue;
                    }

                    IconSlot slot;
                    slot.formID = formID;
                    const auto clipName = std::format("pickpocketIcon{}", slotIndex);
                    std::array clipArgs{
                        RE::GFxValue(clipName.c_str()),
                        RE::GFxValue(static_cast<double>(1000U + slotIndex))
                    };
                    root_.Invoke("createEmptyMovieClip", std::addressof(slot.clip), clipArgs);
                    if (!slot.clip.IsDisplayObject()) {
                        ReportFailed(formID, "Could not create an item icon MovieClip");
                        ++slotIndex;
                        continue;
                    }
                    slot.clip.SetMember("_visible", false);

                    uiMovie->CreateObject(std::addressof(slot.listener));
                    slot.listener.SetMember("formID", static_cast<double>(formID));
                    slot.listener.SetMember("iconLabel", descriptor->second.label.c_str());
                    slot.listener.SetMember("iconColor", static_cast<double>(descriptor->second.color));

                    RE::GFxValue initFunction;
                    uiMovie->CreateFunction(std::addressof(initFunction), loadInitHandler_.get());
                    slot.listener.SetMember("onLoadInit", initFunction);
                    RE::GFxValue errorFunction;
                    uiMovie->CreateFunction(std::addressof(errorFunction), loadErrorHandler_.get());
                    slot.listener.SetMember("onLoadError", errorFunction);

                    uiMovie->CreateObject(std::addressof(slot.loader), "MovieClipLoader");
                    if (!slot.loader.IsObject()) {
                        ReportFailed(formID, "Could not create an item icon MovieClipLoader");
                        slot.clip.Invoke("removeMovieClip");
                        ++slotIndex;
                        continue;
                    }

                    std::array listenerArgs{ slot.listener };
                    slot.loader.Invoke("addListener", listenerArgs);
                    std::array loadArgs{ RE::GFxValue(descriptor->second.source.c_str()), slot.clip };
                    slot.loader.Invoke("loadClip", loadArgs);
                    slots_.push_back(std::move(slot));
                    ++slotIndex;
                }
            }

            void DrawUnderlay(const UnderlayLayout& layout)
            {
                if (!uiMovie || !underlay_.IsDisplayObject()) {
                    return;
                }

                underlay_.Invoke("clear");
                if (layout.screenWidth <= 0.0f || layout.screenHeight <= 0.0f ||
                    layout.dialBounds.width <= 0.0f || layout.dialBounds.height <= 0.0f) {
                    return;
                }

                const auto visible = uiMovie->GetVisibleFrameRect();
                const auto scaleX =
                    (visible.right - visible.left) / std::max(1.0f, layout.screenWidth);
                const auto scaleY =
                    (visible.bottom - visible.top) / std::max(1.0f, layout.screenHeight);
                const Rect screen{
                    visible.left,
                    visible.top,
                    visible.right - visible.left,
                    visible.bottom - visible.top
                };
                DrawFilledRect(
                    underlay_,
                    screen,
                    0x000000,
                    std::clamp(layout.dimmerAlpha, 0.0f, 1.0f) * 100.0f);

                const auto centerX = visible.left + layout.dialBounds.CenterX() * scaleX;
                const auto centerY = visible.top + layout.dialBounds.CenterY() * scaleY;
                const auto stageDialSize = std::min(
                    layout.dialBounds.width * scaleX,
                    layout.dialBounds.height * scaleY);
                DrawCircleStroke(
                    underlay_,
                    centerX,
                    centerY,
                    stageDialSize * kDialAnnulusRadius,
                    stageDialSize * kDialAnnulusThickness,
                    0x000000,
                    kDialAnnulusAlpha * 100.0f);
            }

            void PositionIcons(const MenuSnapshot& snapshot)
            {
                if (!uiMovie || snapshot.layout.underlay.screenWidth <= 0.0f ||
                    snapshot.layout.underlay.screenHeight <= 0.0f) {
                    return;
                }

                const auto visible = uiMovie->GetVisibleFrameRect();
                const auto scaleX =
                    (visible.right - visible.left) / std::max(1.0f, snapshot.layout.underlay.screenWidth);
                const auto scaleY =
                    (visible.bottom - visible.top) / std::max(1.0f, snapshot.layout.underlay.screenHeight);
                const auto uniformScale = std::min(scaleX, scaleY);

                for (auto& icon : slots_) {
                    const auto found = std::ranges::find(snapshot.layout.slots, icon.formID, &Slot::formID);
                    const bool loaded = snapshot.loaded.contains(icon.formID);
                    if (found == snapshot.layout.slots.end() || !icon.clip.IsDisplayObject()) {
                        if (icon.clip.IsDisplayObject()) {
                            icon.clip.SetMember("_visible", false);
                        }
                        continue;
                    }

                    const auto alpha = std::clamp(found->alpha, 0.0f, 1.0f);
                    const auto available = std::max(1.0f, std::min(found->bounds.width, found->bounds.height));
                    const auto targetPixels = available * kIconFillRatio;
                    const auto targetStage = targetPixels * uniformScale;
                    const auto centerX = visible.left + found->bounds.CenterX() * scaleX;
                    const auto centerY = visible.top + found->bounds.CenterY() * scaleY;

                    RE::GFxValue::DisplayInfo display;
                    display.SetPosition(centerX - targetStage * 0.5f, centerY - targetStage * 0.5f);
                    const auto iconScale = targetStage / kIconLibrarySize * 100.0f;
                    display.SetScale(iconScale, iconScale);
                    display.SetAlpha(alpha * 100.0f);
                    display.SetVisible(loaded && alpha > 0.001f);
                    icon.clip.SetDisplayInfo(display);
                }
            }

            void Synchronize()
            {
                if (!initialized_) {
                    Initialize();
                }
                if (!initialized_) {
                    return;
                }

                const auto snapshot = CaptureMenuSnapshot();
                const bool presenting = snapshot.active && snapshot.presenting;
                uiMovie->SetVisible(presenting);
                root_.SetMember("_visible", presenting);
                if (!snapshot.active) {
                    return;
                }

                if (snapshot.underlayRevision != underlayRevision_) {
                    DrawUnderlay(snapshot.layout.underlay);
                    underlayRevision_ = snapshot.underlayRevision;
                }
                if (!snapshot.resolutionComplete) {
                    return;
                }

                if (snapshot.descriptorRevision != descriptorRevision_ || snapshot.iconKeys != slotKeys_) {
                    RebuildIcons(snapshot, snapshot.iconKeys);
                    descriptorRevision_ = snapshot.descriptorRevision;
                    iconLayoutRevision_ = 0;
                }
                if (snapshot.layoutRevision != iconLayoutRevision_) {
                    PositionIcons(snapshot);
                    iconLayoutRevision_ = snapshot.layoutRevision;
                }
            }

            bool initialized_ = false;
            std::uint64_t descriptorRevision_ = 0;
            std::uint64_t underlayRevision_ = 0;
            std::uint64_t iconLayoutRevision_ = 0;
            RE::GFxValue root_;
            RE::GFxValue underlay_;
            RE::GPtr<LoadInitHandler> loadInitHandler_;
            RE::GPtr<LoadErrorHandler> loadErrorHandler_;
            std::vector<RE::FormID> slotKeys_;
            std::vector<IconSlot> slots_;
        };
    }

    bool Register()
    {
        auto& state = State();
        {
            std::scoped_lock lock(state.mutex);
            if (state.registered) {
                return true;
            }
            if (state.registrationFailed) {
                return false;
            }
        }

        const auto ui = RE::UI::GetSingleton();
        if (!ui) {
            logger::error("Could not register the SkyUI icon layer: Skyrim UI is unavailable");
            std::scoped_lock lock(state.mutex);
            state.registrationFailed = true;
            return false;
        }

        if (!InstallProbeSuppressionHooks()) {
            logger::warn("SkyUI icon probing will use fallback menu suppression timing");
        }

        ui->Register(kMenuName, IconLayerMenu::Create);
        {
            std::scoped_lock lock(state.mutex);
            state.registered = true;
        }
        return true;
    }

    namespace {
        [[nodiscard]] std::vector<ItemSpec> BuildItemSpecs(
            const PickpocketInventory::Snapshot& snapshot,
            std::span<const RE::FormID> iconFormIDs)
        {
            std::vector<ItemSpec> items;
            items.reserve(iconFormIDs.size());
            for (const auto formID : iconFormIDs) {
                const auto item = std::ranges::find(snapshot.items, formID, &PickpocketInventory::Item::formID);
                if (item == snapshot.items.end() || formID == 0 ||
                    std::ranges::find(items, formID, &ItemSpec::formID) != items.end()) {
                    continue;
                }
                items.push_back({ formID, FallbackDescriptor(*item) });
            }
            std::ranges::sort(items, {}, &ItemSpec::formID);
            return items;
        }

        void StartOrPromoteSession(
            const PickpocketInventory::Snapshot& snapshot,
            std::span<const RE::FormID> iconFormIDs,
            bool presenting)
        {
            auto items = BuildItemSpecs(snapshot, iconFormIDs);
            const auto blur = RE::UIBlurManager::GetSingleton();
            const auto blurCount = blur ? blur->blurCount : 0U;
            bool hideProbe = false;
            bool showHost = false;
            {
                auto& state = State();
                std::scoped_lock lock(state.mutex);
                const bool compatible =
                    state.active &&
                    state.targetHandle.native_handle() == snapshot.targetHandle.native_handle() &&
                    state.items == items;
                if (compatible) {
                    state.presenting = state.presenting || presenting;
                } else {
                    hideProbe = state.active && !state.resolutionComplete &&
                        (state.probeStage == ProbeStage::Opening ||
                         state.probeStage == ProbeStage::Closing);
                    state.active = true;
                    state.presenting = presenting;
                    state.targetHandle = snapshot.targetHandle;
                    state.items = std::move(items);
                    state.descriptors.clear();
                    state.loaded.clear();
                    state.failed.clear();
                    state.layout = {};
                    state.layoutSubmitted = false;
                    state.resolutionComplete = state.items.empty() || !state.registered;
                    state.probeStage = state.resolutionComplete ? ProbeStage::Idle : ProbeStage::Requested;
                    state.probeTimedOut = false;
                    state.probeFrames = 0;
                    state.stableFrames = 0;
                    state.closeSettleFrames = 0;
                    state.probeBlurNeutralized = false;
                    state.probeBlurBaselineCount = blurCount;
                    state.lastExtracted = {};
                    state.pendingExtracted = {};
                    ++state.sessionRevision;
                    ++state.descriptorRevision;
                    ++state.underlayRevision;
                    ++state.layoutRevision;
                    showHost = state.registered;
                }
            }

            if (hideProbe) {
                HideContainerMenu();
                RestoreCursorAfterProbe();
            }
            SuppressCursorForProbe();
            if (showHost) {
                if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(kMenuName.data(), RE::UI_MESSAGE_TYPE::kShow, nullptr);
                }
            }

        }
    }

    void PrepareSession(
        const PickpocketInventory::Snapshot& snapshot,
        std::span<const RE::FormID> iconFormIDs)
    {
        StartOrPromoteSession(snapshot, iconFormIDs, false);
    }

    void BeginSession(
        const PickpocketInventory::Snapshot& snapshot,
        std::span<const RE::FormID> iconFormIDs)
    {
        StartOrPromoteSession(snapshot, iconFormIDs, true);
    }

    void EndSession()
    {
        bool hideProbe = false;
        bool hideHost = false;
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            hideProbe = state.active && !state.resolutionComplete &&
                (state.probeStage == ProbeStage::Opening ||
                 state.probeStage == ProbeStage::Closing);
            hideHost = state.registered;
            state.active = false;
            state.presenting = false;
            state.resolutionComplete = false;
            state.probeStage = ProbeStage::Idle;
            state.probeTimedOut = false;
            state.probeFrames = 0;
            state.stableFrames = 0;
            state.closeSettleFrames = 0;
            state.lastExtracted = {};
            state.pendingExtracted = {};
            state.items.clear();
            state.descriptors.clear();
            state.loaded.clear();
            state.failed.clear();
            state.layout = {};
            state.layoutSubmitted = false;
            ++state.sessionRevision;
            ++state.descriptorRevision;
            ++state.underlayRevision;
            ++state.layoutRevision;
        }

        if (hideProbe) {
            HideContainerMenu();
        }
        RestoreCursorAfterProbe();
        if (hideHost) {
            if (const auto queue = RE::UIMessageQueue::GetSingleton()) {
                queue->AddMessage(kMenuName.data(), RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }
        }
    }

    void SubmitLayout(
        float screenWidth,
        float screenHeight,
        const Rect& dialBounds,
        float dimmerAlpha,
        std::span<const Slot> slots)
    {
        LayoutSnapshot next;
        next.underlay = { screenWidth, screenHeight, dialBounds, dimmerAlpha };
        next.slots.assign(slots.begin(), slots.end());

        auto& state = State();
        std::scoped_lock lock(state.mutex);
        state.layoutSubmitted = true;
        const bool underlayChanged = state.layout.underlay != next.underlay;
        if (state.layout != next) {
            state.layout = std::move(next);
            ++state.layoutRevision;
        }
        if (underlayChanged) {
            ++state.underlayRevision;
        }
    }

    bool IsReadyForPresentation()
    {
        auto& state = State();
        std::scoped_lock lock(state.mutex);
        if (!state.active) {
            return true;
        }
        if (!state.presenting) {
            return false;
        }
        if (!state.resolutionComplete) {
            return false;
        }
        if (!state.layoutSubmitted) {
            return false;
        }
        if (!state.registered || state.hostFailed) {
            return true;
        }
        if (!state.hostReady) {
            return false;
        }

        return std::ranges::all_of(state.layout.slots, [&](const Slot& slot) {
            return
                state.descriptors.contains(slot.formID) &&
                (state.loaded.contains(slot.formID) || state.failed.contains(slot.formID));
        });
    }

    bool ClaimContainerMenu(RE::ContainerMenu& menu, RE::RefHandle targetHandle)
    {
        {
            auto& state = State();
            std::scoped_lock lock(state.mutex);
            if (!state.active || state.probeStage != ProbeStage::Opening ||
                state.targetHandle.native_handle() != targetHandle) {
                return false;
            }
        }

        if (menu.uiMovie) {
            menu.uiMovie->SetVisible(false);
        }
        SuppressCursorForProbe();
        return true;
    }

}
