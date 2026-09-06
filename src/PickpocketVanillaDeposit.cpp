#include "PickpocketEventHandler.h"
#include "PickpocketEventHandlerInternal.h"
#include "PickpocketInventory.h"
#include "PickpocketSession.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace logger = SKSE::log;

namespace PickpocketEvents {
    namespace {
        constexpr std::int32_t kDepositMenuConfigureAttempts = 30;
        constexpr std::int32_t kDepositMenuOpenWatchdogAttempts = 30;
        constexpr double kGiveTabIndex = 1.0;
        constexpr double kDisabledSwitchKey = -1.0;

        std::mutex vanillaPickpocketMenuMutex;
        bool vanillaPickpocketMenuBypassArmed = false;
        RE::RefHandle vanillaPickpocketMenuBypassTarget = 0;
        bool vanillaDepositGuardActive = false;
        RE::ObjectRefHandle vanillaDepositGuardTarget;
        RE::TESObjectREFR::InventoryCountMap vanillaDepositBaseline;

        void ClearVanillaDepositState() {
            std::lock_guard lock(vanillaPickpocketMenuMutex);
            vanillaPickpocketMenuBypassArmed = false;
            vanillaPickpocketMenuBypassTarget = 0;
            vanillaDepositGuardActive = false;
            vanillaDepositGuardTarget.reset();
            vanillaDepositBaseline.clear();
        }

        void ArmVanillaPickpocketMenuBypass(RE::ObjectRefHandle targetHandle) {
            std::lock_guard lock(vanillaPickpocketMenuMutex);
            vanillaPickpocketMenuBypassTarget = targetHandle.native_handle();
            vanillaPickpocketMenuBypassArmed = true;
        }

        void ArmVanillaDepositGuard(RE::ObjectRefHandle targetHandle, RE::TESObjectREFR& target) {
            auto baseline = target.GetInventoryCounts();

            std::lock_guard lock(vanillaPickpocketMenuMutex);
            vanillaDepositGuardTarget = targetHandle;
            vanillaDepositBaseline = std::move(baseline);
            vanillaDepositGuardActive = true;
        }

        bool IsVanillaDepositGuardActive() {
            std::lock_guard lock(vanillaPickpocketMenuMutex);
            return vanillaDepositGuardActive;
        }

        bool IsScaleformObject(const RE::GFxValue& value) {
            return value.IsObject() || value.IsDisplayObject();
        }

        std::optional<double> GetScaleformNumberMember(RE::GFxValue& value, const char* memberName) {
            RE::GFxValue member;
            if (!value.GetMember(memberName, &member) || !member.IsNumber()) {
                return std::nullopt;
            }

            return member.GetNumber();
        }

        void ConfigureTabButtonPath(RE::GFxMovieView& movie, const std::string& buttonPath) {
            const auto permanent = RE::GFxMovie::SetVarType::kPermanent;
            movie.SetVariable((buttonPath + "._visible").c_str(), RE::GFxValue(false), permanent);
            movie.SetVariable((buttonPath + ".enabled").c_str(), RE::GFxValue(false), permanent);
        }

        void ConfigureTabBarPath(RE::GFxMovieView& movie, const std::string& tabBarPath) {
            const auto permanent = RE::GFxMovie::SetVarType::kPermanent;
            movie.SetVariable((tabBarPath + ".activeTab").c_str(), RE::GFxValue(kGiveTabIndex), permanent);
            movie.SetVariable((tabBarPath + "._visible").c_str(), RE::GFxValue(false), permanent);
            movie.SetVariable((tabBarPath + ".disabled").c_str(), RE::GFxValue(true), permanent);
            ConfigureTabButtonPath(movie, tabBarPath + ".leftButton");
            ConfigureTabButtonPath(movie, tabBarPath + ".rightButton");
        }

        void ConfigureTabButtonObject(RE::GFxValue& button) {
            button.SetMember("_visible", RE::GFxValue(false));
            button.SetMember("enabled", RE::GFxValue(false));
        }

        void ConfigureTabBarObject(RE::GFxValue& tabBar) {
            tabBar.SetMember("activeTab", RE::GFxValue(kGiveTabIndex));
            tabBar.SetMember("_visible", RE::GFxValue(false));
            tabBar.SetMember("disabled", RE::GFxValue(true));

            RE::GFxValue leftButton;
            if (tabBar.GetMember("leftButton", &leftButton) && IsScaleformObject(leftButton)) {
                ConfigureTabButtonObject(leftButton);
            }

            RE::GFxValue rightButton;
            if (tabBar.GetMember("rightButton", &rightButton) && IsScaleformObject(rightButton)) {
                ConfigureTabButtonObject(rightButton);
            }
        }

        bool ConfigureMenuPathForDeposit(RE::GFxMovieView& movie, const char* menuPath) {
            const auto inventoryListsPath = std::string(menuPath) + ".inventoryLists";
            if (!movie.IsAvailable(inventoryListsPath.c_str())) {
                return false;
            }

            const auto permanent = RE::GFxMovie::SetVarType::kPermanent;
            movie.SetVariable((std::string(menuPath) + ".bNPCMode").c_str(), RE::GFxValue(true), permanent);
            movie.SetVariable((inventoryListsPath + "._switchTabKey").c_str(), RE::GFxValue(kDisabledSwitchKey), permanent);
            movie.SetVariable((inventoryListsPath + ".categoryList.activeSegment").c_str(), RE::GFxValue(kGiveTabIndex), permanent);

            const auto categoryListPath = inventoryListsPath + ".categoryList";
            RE::GFxValue dividerIndexValue;
            if (movie.GetVariable(&dividerIndexValue, (categoryListPath + ".dividerIndex").c_str()) && dividerIndexValue.IsNumber()) {
                const auto giveAllIndex = dividerIndexValue.GetNumber() + 1.0;
                movie.SetVariable((categoryListPath + ".selectedIndex").c_str(), RE::GFxValue(giveAllIndex), permanent);

                RE::GFxValue categoryPressArgs[2];
                categoryPressArgs[0] = giveAllIndex;
                categoryPressArgs[1] = 0.0;
                movie.Invoke((categoryListPath + ".onItemPress").c_str(), nullptr, categoryPressArgs, 2);
            }

            RE::GFxValue tabEvent;
            movie.CreateObject(&tabEvent);
            tabEvent.SetMember("index", RE::GFxValue(kGiveTabIndex));
            movie.Invoke((inventoryListsPath + ".onTabPress").c_str(), nullptr, &tabEvent, 1);
            movie.Invoke((inventoryListsPath + ".showItemsList").c_str(), nullptr, nullptr, 0);

            ConfigureTabBarPath(movie, inventoryListsPath + ".tabBar");
            ConfigureTabBarPath(movie, inventoryListsPath + ".panelContainer.tabBar");

            return true;
        }

        bool ConfigureMenuRootForDeposit(RE::ContainerMenu& containerMenu, RE::GFxValue& root) {
            if (!IsScaleformObject(root)) {
                return false;
            }

            root.SetMember("bNPCMode", RE::GFxValue(true));
            RE::GFxValue inventoryLists;
            if (!root.GetMember("inventoryLists", &inventoryLists) || !IsScaleformObject(inventoryLists)) {
                return false;
            }

            inventoryLists.SetMember("_switchTabKey", RE::GFxValue(kDisabledSwitchKey));

            RE::GFxValue categoryList;
            if (inventoryLists.GetMember("categoryList", &categoryList) && IsScaleformObject(categoryList)) {
                categoryList.SetMember("activeSegment", RE::GFxValue(kGiveTabIndex));
                if (const auto dividerIndex = GetScaleformNumberMember(categoryList, "dividerIndex")) {
                    const auto giveAllIndex = *dividerIndex + 1.0;
                    categoryList.SetMember("selectedIndex", RE::GFxValue(giveAllIndex));

                    RE::GFxValue categoryPressArgs[2];
                    categoryPressArgs[0] = giveAllIndex;
                    categoryPressArgs[1] = 0.0;
                    categoryList.Invoke("onItemPress", nullptr, categoryPressArgs, 2);
                }
            }

            RE::GFxValue tabBar;
            if (inventoryLists.GetMember("tabBar", &tabBar) && IsScaleformObject(tabBar)) {
                ConfigureTabBarObject(tabBar);
            }

            RE::GFxValue panelContainer;
            if (inventoryLists.GetMember("panelContainer", &panelContainer) && IsScaleformObject(panelContainer)) {
                RE::GFxValue panelTabBar;
                if (panelContainer.GetMember("tabBar", &panelTabBar) && IsScaleformObject(panelTabBar)) {
                    ConfigureTabBarObject(panelTabBar);
                }
            }

            RE::GFxValue tabEvent;
            if (containerMenu.uiMovie) {
                containerMenu.uiMovie->CreateObject(&tabEvent);
                tabEvent.SetMember("index", RE::GFxValue(kGiveTabIndex));
                inventoryLists.Invoke("onTabPress", nullptr, &tabEvent, 1);
            }
            inventoryLists.Invoke("showItemsList");
            return true;
        }

        bool ForceContainerMenuToGiveTab(RE::ContainerMenu& containerMenu) {
            bool configured = false;
            if (containerMenu.uiMovie) {
                configured |= ConfigureMenuPathForDeposit(*containerMenu.uiMovie, "_root.Menu_mc");
                configured |= ConfigureMenuPathForDeposit(*containerMenu.uiMovie, "_root");
            }

            auto root = containerMenu.GetRuntimeData().root;
            configured |= ConfigureMenuRootForDeposit(containerMenu, root);
            return configured;
        }

        bool ConfigureCurrentContainerMenuForDeposit() {
            const auto ui = RE::UI::GetSingleton();
            if (!ui) {
                return false;
            }

            const auto containerMenu = ui->GetMenu<RE::ContainerMenu>();
            if (!containerMenu) {
                return false;
            }

            return ForceContainerMenuToGiveTab(*containerMenu);
        }

        void ScheduleDepositMenuConfigurationAttempt(std::int32_t attemptsRemaining) {
            if (attemptsRemaining <= 0 || !IsVanillaDepositGuardActive()) {
                return;
            }
            const auto taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                return;
            }

            taskInterface->AddUITask([attemptsRemaining] {
                if (!IsVanillaDepositGuardActive()) {
                    return;
                }

                ConfigureCurrentContainerMenuForDeposit();
                ScheduleDepositMenuConfigurationAttempt(attemptsRemaining - 1);
            });
        }

        void ScheduleDepositMenuOpenWatchdog(
            RE::RefHandle targetHandle,
            std::int32_t attemptsRemaining) {
            const auto taskInterface = SKSE::GetTaskInterface();
            if (!taskInterface) {
                logger::warn("Cannot monitor the vanilla deposit menu opening: task interface is unavailable");
                return;
            }

            taskInterface->AddUITask([targetHandle, attemptsRemaining] {
                if (!PickpocketSession::IsOpeningDepositFor(targetHandle)) {
                    return;
                }
                if (attemptsRemaining > 0) {
                    ScheduleDepositMenuOpenWatchdog(targetHandle, attemptsRemaining - 1);
                    return;
                }

                logger::error(
                    "Vanilla deposit menu did not open for target {:08X}",
                    targetHandle);
                Internal::ForceHideContainerMenu();
                ClearVanillaDepositState();
                PickpocketSession::Abort("vanilla deposit menu open timed out");
            });
        }

    }

    namespace Internal {
        bool ConsumeVanillaPickpocketMenuBypass(RE::RefHandle targetHandle) {
            std::lock_guard lock(vanillaPickpocketMenuMutex);
            if (!vanillaPickpocketMenuBypassArmed || vanillaPickpocketMenuBypassTarget != targetHandle) {
                return false;
            }

            vanillaPickpocketMenuBypassArmed = false;
            vanillaPickpocketMenuBypassTarget = 0;
            return true;
        }

        // Vanilla lets the player hand items back through the deposit menu, but
        // taking anything out of the target belongs to the minigame. Put back
        // whatever left the target inventory while that menu was open.
        void RestoreVanillaDepositLosses() {
            RE::ObjectRefHandle targetHandle;
            RE::TESObjectREFR::InventoryCountMap baseline;
            {
                std::lock_guard lock(vanillaPickpocketMenuMutex);
                if (!vanillaDepositGuardActive) {
                    return;
                }

                targetHandle = vanillaDepositGuardTarget;
                baseline = std::move(vanillaDepositBaseline);
                vanillaDepositGuardTarget.reset();
                vanillaDepositGuardActive = false;
            }

            auto targetRef = targetHandle.get();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!targetRef || !player) {
                logger::warn("Could not enforce vanilla deposit guard: target or player is unavailable");
                return;
            }

            const auto currentCounts = targetRef->GetInventoryCounts();
            for (const auto& [object, originalCount] : baseline) {
                if (!object || originalCount <= 0) {
                    continue;
                }

                const auto current = currentCounts.find(object);
                const auto currentCount = current != currentCounts.end() ? current->second : 0;
                const auto missingCount = originalCount - currentCount;
                if (missingCount <= 0) {
                    continue;
                }

                const auto playerCount = player->GetItemCount(object);
                const auto restoreCount = std::min(missingCount, playerCount);
                if (restoreCount <= 0) {
                    logger::warn(
                        "Vanilla deposit guard could not restore {} missing item(s) for {:08X}: player no longer has any",
                        missingCount,
                        object->GetFormID());
                    continue;
                }

                player->RemoveItem(
                    object,
                    restoreCount,
                    RE::ITEM_REMOVE_REASON::kStoreInContainer,
                    nullptr,
                    targetRef.get());

                if (restoreCount < missingCount) {
                    logger::warn(
                        "Vanilla deposit guard only restored {} of {} missing item(s) for {:08X}",
                        restoreCount,
                        missingCount,
                        object->GetFormID());
                }
            }

            if (auto* targetActor = targetRef->As<RE::Actor>()) {
                PickpocketInventory::CaptureFromActor(*targetActor);
            }
        }

        void ScheduleDepositMenuConfiguration() {
            ScheduleDepositMenuConfigurationAttempt(kDepositMenuConfigureAttempts);
        }
    }

    bool OpenVanillaPickpocketMenu(RE::ObjectRefHandle targetHandle) {
        auto targetRef = targetHandle.get();
        if (!targetRef) {
            logger::warn("Cannot open vanilla pickpocket menu: target handle is no longer valid");
            return false;
        }

        ArmVanillaDepositGuard(targetHandle, *targetRef);
        ArmVanillaPickpocketMenuBypass(targetHandle);
        RE::ContainerMenu::OpenMenu(targetRef.get(), RE::ContainerMenu::ContainerMode::kPickpocket);
        ScheduleDepositMenuOpenWatchdog(
            targetHandle.native_handle(),
            kDepositMenuOpenWatchdogAttempts);
        return true;
    }

    void CancelVanillaPickpocketMenuRequest() {
        ClearVanillaDepositState();
    }
}
