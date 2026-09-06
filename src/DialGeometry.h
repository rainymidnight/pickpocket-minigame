#pragma once

#include "PickpocketRules.h"

#include <numbers>

// Geometry shared by the ImGui dial and the Scaleform icon layer that draws
// underneath it. Both renderers must agree on where the slot ring sits.
namespace DialGeometry {
    inline constexpr float kSlotAngleRadians = 2.0f * std::numbers::pi_v<float> / PickpocketRules::kDialSlotCount;
    inline constexpr float kDesignSize = 2048.0f;
    inline constexpr float kSlotRingRadius = 740.0f / kDesignSize;
}
