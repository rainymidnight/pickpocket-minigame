#pragma once

#include "PickpocketInventory.h"

#include <span>

namespace SkyUIIcons {
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        [[nodiscard]] float CenterX() const noexcept { return x + width * 0.5f; }
        [[nodiscard]] float CenterY() const noexcept { return y + height * 0.5f; }

        friend bool operator==(const Rect&, const Rect&) = default;
    };

    struct Slot {
        RE::FormID formID = 0;
        Rect bounds;
        float alpha = 0.0f;

        friend bool operator==(const Slot&, const Slot&) = default;
    };

    bool Register();
    void PrepareSession(
        const PickpocketInventory::Snapshot& snapshot,
        std::span<const RE::FormID> iconFormIDs);
    void BeginSession(
        const PickpocketInventory::Snapshot& snapshot,
        std::span<const RE::FormID> iconFormIDs);
    void EndSession();
    void SubmitLayout(
        float screenWidth,
        float screenHeight,
        const Rect& dialBounds,
        float dimmerAlpha,
        std::span<const Slot> slots);

    [[nodiscard]] bool IsReadyForPresentation();
    [[nodiscard]] bool ClaimContainerMenu(RE::ContainerMenu& menu, RE::RefHandle targetHandle);
}
