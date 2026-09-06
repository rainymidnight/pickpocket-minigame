#pragma once

namespace PickpocketRules {
    // Shared time budget and slot capacity for both minigame phases.
    inline constexpr float kDialTimerDurationSeconds = 30.0f;
    inline constexpr int kDialSlotCount = 12;
    inline constexpr int kDialExitSlot = 0;
    inline constexpr int kDialFirstItemSlot = kDialExitSlot + 1;
    inline constexpr int kDialItemSlotCount = kDialSlotCount - kDialFirstItemSlot;
}
