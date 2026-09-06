#pragma once

#include <cstdint>
#include <string>

namespace PickpocketConfig {
    inline constexpr float kPhase1RiskMultiplierMin = 0.25f;
    inline constexpr float kPhase1RiskMultiplierMax = 4.0f;
    inline constexpr float kDialValueDistanceBiasMin = 0.0f;
    inline constexpr float kDialValueDistanceBiasMax = 6.0f;
    inline constexpr float kUIScaleMin = 0.5f;
    inline constexpr float kUIScaleMax = 2.0f;
    inline constexpr float kUIXOffsetPercentMin = -50.0f;
    inline constexpr float kUIXOffsetPercentMax = 50.0f;
    inline constexpr float kUIYOffsetPercentMin = -50.0f;
    inline constexpr float kUIYOffsetPercentMax = 50.0f;
    inline constexpr const char* kDefaultPlaceItemsKey = "ImGuiKey_LeftAlt";
    inline constexpr const char* kDefaultMoveLeftKey = "ImGuiKey_A";
    inline constexpr const char* kDefaultMoveRightKey = "ImGuiKey_D";
    inline constexpr const char* kDefaultSelectKey = "ImGuiKey_E";

    enum class PreviewMode : std::uint8_t {
        Meshes,
        SkyUIIcons,
    };

    struct Settings {
        bool disableMinigame = false;
        float phase1RiskMultiplier = 1.0f;
        float dialValueDistanceBias = 3.0f;
        float uiScale = 1.0f;
        float uiXOffsetPercent = 0.0f;
        float uiYOffsetPercent = 0.0f;
        bool showDebugPickpocketListDuringPhase2 = false;
        bool showPhase1DebugOverlay = false;
        bool allowPickpocketingAllInventoryItems = false;
        bool alwaysRevealAllItems = false;
        PreviewMode previewMode = PreviewMode::SkyUIIcons;
        std::string placeItemsKey = kDefaultPlaceItemsKey;
        std::string moveLeftKey = kDefaultMoveLeftKey;
        std::string moveRightKey = kDefaultMoveRightKey;
        std::string selectKey = kDefaultSelectKey;
    };

    extern Settings settings;

    [[nodiscard]] inline bool UsingSkyUIIcons() {
        return settings.previewMode == PreviewMode::SkyUIIcons;
    }

    [[nodiscard]] float GetPhase1RiskMultiplier();
    void LoadSettings();
    void SaveSettings();
    void EnsureSettingsFile();
    void ClampSettings();
}
