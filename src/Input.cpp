#include "Input.h"

#include <algorithm>
#include <array>
#include <atomic>

namespace Input {
    namespace {
        enum class ActiveDevice : std::uint8_t {
            Unknown,
            KeyboardMouse,
            Controller,
        };

        std::atomic activeDevice{ ActiveDevice::Unknown };

        constexpr std::array kMenuActionContexts{
            RE::UserEvents::INPUT_CONTEXT_ID::kGameplay,
            RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode,
            RE::UserEvents::INPUT_CONTEXT_ID::kItemMenu,
            RE::UserEvents::INPUT_CONTEXT_ID::kInventory,
        };

        bool MatchesMappedAction(const RE::ButtonEvent& button, const RE::BSFixedString& eventID) {
            if (button.QUserEvent() == eventID) {
                return true;
            }

            const auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) {
                return false;
            }

            return std::ranges::any_of(kMenuActionContexts, [&](const auto context) {
                const auto mappedKey = controlMap->GetMappedKey(eventID.c_str(), button.GetDevice(), context);
                return mappedKey != RE::ControlMap::kInvalid && button.GetIDCode() == mappedKey;
            });
        }

        std::optional<MenuAction> ResolveDirectXAction(std::uint32_t idCode) {
            switch (static_cast<RE::BSWin32GamepadDevice::Key>(idCode)) {
            case RE::BSWin32GamepadDevice::Key::kA:
                return MenuAction::Confirm;
            case RE::BSWin32GamepadDevice::Key::kB:
                return MenuAction::Cancel;
            case RE::BSWin32GamepadDevice::Key::kBack:
                return MenuAction::Place;
            case RE::BSWin32GamepadDevice::Key::kLeft:
                return MenuAction::MoveLeft;
            case RE::BSWin32GamepadDevice::Key::kRight:
                return MenuAction::MoveRight;
            default:
                return std::nullopt;
            }
        }

        std::optional<MenuAction> ResolveOrbisAction(std::uint32_t idCode) {
            switch (static_cast<RE::BSPCOrbisGamepadDevice::Key>(idCode)) {
            case RE::BSPCOrbisGamepadDevice::Key::kPS3_A:
                return MenuAction::Confirm;
            case RE::BSPCOrbisGamepadDevice::Key::kPS3_B:
                return MenuAction::Cancel;
            case RE::BSPCOrbisGamepadDevice::Key::kPS3_Back:
                return MenuAction::Place;
            case RE::BSPCOrbisGamepadDevice::Key::kLeft:
                return MenuAction::MoveLeft;
            case RE::BSPCOrbisGamepadDevice::Key::kRight:
                return MenuAction::MoveRight;
            default:
                return std::nullopt;
            }
        }

        std::optional<MenuAction> ResolvePhysicalAction(const RE::ButtonEvent& button) {
            const auto* controlMap = RE::ControlMap::GetSingleton();
            if (controlMap && controlMap->GetGamePadType() == RE::PC_GAMEPAD_TYPE::kOrbis) {
                return ResolveOrbisAction(button.GetIDCode());
            }
            return ResolveDirectXAction(button.GetIDCode());
        }
    }

    void ObserveInputEvent(const RE::InputEvent& inputEvent) {
        if (const auto* button = inputEvent.AsButtonEvent(); button && !button->IsDown()) {
            return;
        }

        switch (inputEvent.GetDevice()) {
        case RE::INPUT_DEVICE::kKeyboard:
        case RE::INPUT_DEVICE::kMouse:
            activeDevice.store(ActiveDevice::KeyboardMouse, std::memory_order_release);
            break;
        case RE::INPUT_DEVICE::kGamepad:
            activeDevice.store(ActiveDevice::Controller, std::memory_order_release);
            break;
        default:
            break;
        }
    }

    bool IsControllerActive() {
        switch (activeDevice.load(std::memory_order_acquire)) {
        case ActiveDevice::KeyboardMouse:
            return false;
        case ActiveDevice::Controller:
            return true;
        case ActiveDevice::Unknown:
        default:
            auto* inputManager = RE::BSInputDeviceManager::GetSingleton();
            return inputManager && inputManager->IsGamepadEnabled();
        }
    }

    std::optional<MenuAction> ResolveMenuAction(const RE::ButtonEvent& button) {
        if (button.GetDevice() != RE::INPUT_DEVICE::kGamepad) {
            return std::nullopt;
        }

        if (const auto* userEvents = RE::UserEvents::GetSingleton()) {
            if (MatchesMappedAction(button, userEvents->accept) ||
                MatchesMappedAction(button, userEvents->activate)) {
                return MenuAction::Confirm;
            }
            if (MatchesMappedAction(button, userEvents->cancel) ||
                MatchesMappedAction(button, userEvents->tweenMenu) ||
                MatchesMappedAction(button, userEvents->pause)) {
                return MenuAction::Cancel;
            }
        }

        return ResolvePhysicalAction(button);
    }
}
