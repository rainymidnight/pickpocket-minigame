#pragma once

#include <cstdint>
#include <optional>

namespace RE {
    class ButtonEvent;
    class InputEvent;
}

namespace Input {
    enum class MenuAction : std::uint8_t {
        Confirm,
        Cancel,
        Place,
        MoveLeft,
        MoveRight,
    };

    void ObserveInputEvent(const RE::InputEvent& inputEvent);
    [[nodiscard]] bool IsControllerActive();
    [[nodiscard]] std::optional<MenuAction> ResolveMenuAction(const RE::ButtonEvent& button);
}
