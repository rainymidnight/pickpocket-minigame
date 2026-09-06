#include "PickpocketEventHandler.h"
#include "PickpocketEventHandlerInternal.h"
#include "Config.h"
#include "PickpocketInventory.h"
#include "PickpocketSession.h"
#include "SKSEMenuFramework.h"
#include "SkyUIIcons.h"
#include "UI.h"

namespace logger = SKSE::log;

namespace PickpocketEvents {
    namespace Internal {
        void ForceHideContainerMenu() {
            if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
                msgQueue->AddMessage("ContainerMenu", RE::UI_MESSAGE_TYPE::kForceHide, nullptr);
            }
        }
    }

    namespace {
        using Internal::ForceHideContainerMenu;

        bool CaptureTargetInventory(RE::RefHandle targetHandle) {
            RE::NiPointer<RE::TESObjectREFR> targetRef;
            if (!RE::TESObjectREFR::LookupByHandle(targetHandle, targetRef)) {
                return false;
            }

            auto* targetActor = targetRef->As<RE::Actor>();
            if (!targetActor) {
                return false;
            }

            PickpocketInventory::CaptureFromActor(*targetActor);
            return true;
        }

        class MenuEventHandler : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        public:
            static MenuEventHandler* GetSingleton() {
                static MenuEventHandler singleton;
                return &singleton;
            }

            virtual RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                                         RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
                if (!a_event || a_event->menuName != "ContainerMenu") {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (!a_event->opening) {
                    if (PickpocketSession::IsDepositOpen()) {
                        Internal::RestoreVanillaDepositLosses();
                        (void)PickpocketSession::FinishDeposit();
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* ui = RE::UI::GetSingleton();
                if (!ui) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto containerMenu = ui->GetMenu<RE::ContainerMenu>();
                if (!containerMenu || containerMenu->GetContainerMode() != RE::ContainerMenu::ContainerMode::kPickpocket) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto targetHandle = RE::ContainerMenu::GetTargetRefHandle();
                if (PickpocketSession::IsOpeningDepositFor(targetHandle)) {
                    if (!Internal::ConsumeVanillaPickpocketMenuBypass(targetHandle)) {
                        logger::error(
                            "Vanilla deposit menu opened for {:08X} without its bypass token",
                            targetHandle);
                        CancelVanillaPickpocketMenuRequest();
                        PickpocketSession::Abort("deposit bypass token was unavailable");
                        return RE::BSEventNotifyControl::kContinue;
                    }
                    if (!PickpocketSession::ConfirmDepositOpen(targetHandle)) {
                        CancelVanillaPickpocketMenuRequest();
                        PickpocketSession::Abort("deposit menu open event was rejected");
                        return RE::BSEventNotifyControl::kContinue;
                    }
                    Internal::ScheduleDepositMenuConfiguration();
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (Internal::ConsumeVanillaPickpocketMenuBypass(targetHandle)) {
                    logger::warn(
                        "Discarding stale vanilla deposit bypass token for {:08X}",
                        targetHandle);
                    CancelVanillaPickpocketMenuRequest();
                    PickpocketSession::Abort("stale deposit bypass token consumed");
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (PickpocketConfig::settings.disableMinigame) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (PickpocketSession::CanClaimIconProbe(targetHandle) &&
                    SkyUIIcons::ClaimContainerMenu(*containerMenu, targetHandle)) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (PickpocketSession::IsPhase1Target(targetHandle)) {
                    ForceHideContainerMenu();
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (PickpocketSession::GetSnapshot().state != PickpocketSession::State::Idle) {
                    ForceHideContainerMenu();
                    return RE::BSEventNotifyControl::kContinue;
                }

                if (!CaptureTargetInventory(targetHandle)) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Icon previews still need the vanilla menu alive to publish
                // their descriptors, so hide it instead of closing it.
                if (PickpocketConfig::UsingSkyUIIcons()) {
                    if (containerMenu->uiMovie) {
                        containerMenu->uiMovie->SetVisible(false);
                    }
                } else {
                    ForceHideContainerMenu();
                }

                if (!UI::OpenPickpocketInventoryWindow()) {
                    ForceHideContainerMenu();
                }

                return RE::BSEventNotifyControl::kContinue;
            }

        private:
            MenuEventHandler() = default;
        };
    }

    void Register() {
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuEventHandler::GetSingleton());
            logger::info("Pickpocket event handler registered");
        } else {
            logger::error("Failed to register pickpocket event handler - UI singleton not available");
        }

        if (SKSEMenuFramework::IsInstalled()) {
            SKSEMenuFramework::AddInputEvent(Internal::ProcessPhase1Input);
            logger::info("Pickpocket phase 1 input handler registered");
        } else {
            logger::warn("Could not register pickpocket phase 1 input handler: SKSE Menu Framework is unavailable");
        }
    }
}
