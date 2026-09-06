#include "Config.h"

#include "logger.h"
#include "StringUtils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {
    using StringUtils::ToLowerAscii;
    using StringUtils::Trim;

    constexpr std::string_view kSettingsSection = "[settings]";
    constexpr const char* kSettingsPath = "Data/SKSE/Plugins/PickpocketMinigame.ini";

    bool ParseBool(std::string_view rawValue, bool fallback) {
        const auto value = ToLowerAscii(StringUtils::TrimView(rawValue));
        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no" || value == "off") {
            return false;
        }
        return fallback;
    }

    float ParseFloat(std::string_view rawValue, float fallback) {
        try {
            std::size_t processed = 0;
            const auto parsed = std::stof(Trim(rawValue), &processed);
            return processed > 0 ? parsed : fallback;
        } catch (...) {
            return fallback;
        }
    }

    PickpocketConfig::PreviewMode ParsePreviewMode(
        std::string_view rawValue,
        PickpocketConfig::PreviewMode fallback) {
        const auto value = ToLowerAscii(StringUtils::TrimView(rawValue));
        if (value == "meshes" || value == "3d") {
            return PickpocketConfig::PreviewMode::Meshes;
        }
        if (value == "icons" || value == "skyuiicons" || value == "2d") {
            return PickpocketConfig::PreviewMode::SkyUIIcons;
        }
        return fallback;
    }

    void ApplySetting(std::string_view rawKey, std::string_view value) {
        const auto key = ToLowerAscii(StringUtils::TrimView(rawKey));
        auto& settings = PickpocketConfig::settings;

        if (key == "disableminigame") {
            settings.disableMinigame = ParseBool(value, settings.disableMinigame);
        } else if (key == "difficulty") {
            settings.phase1RiskMultiplier = ParseFloat(value, settings.phase1RiskMultiplier);
        } else if (key == "valuedistancebias") {
            settings.dialValueDistanceBias = ParseFloat(value, settings.dialValueDistanceBias);
        } else if (key == "uiscale") {
            settings.uiScale = ParseFloat(value, settings.uiScale);
        } else if (key == "uixoffset") {
            settings.uiXOffsetPercent = ParseFloat(value, settings.uiXOffsetPercent);
        } else if (key == "uiyoffset") {
            settings.uiYOffsetPercent = ParseFloat(value, settings.uiYOffsetPercent);
        } else if (key == "showdebugpickpocketlistduringphase2") {
            settings.showDebugPickpocketListDuringPhase2 = ParseBool(value, settings.showDebugPickpocketListDuringPhase2);
        } else if (key == "showphase1debugoverlay") {
            settings.showPhase1DebugOverlay = ParseBool(value, settings.showPhase1DebugOverlay);
        } else if (key == "allowpickpocketingallitemsinnpcinventory") {
            settings.allowPickpocketingAllInventoryItems = ParseBool(value, settings.allowPickpocketingAllInventoryItems);
        } else if (key == "alwaysrevealallitems") {
            settings.alwaysRevealAllItems = ParseBool(value, settings.alwaysRevealAllItems);
        } else if (key == "previewmode") {
            settings.previewMode = ParsePreviewMode(value, settings.previewMode);
        } else if (key == "placeitemskey") {
            settings.placeItemsKey = Trim(value);
        } else if (key == "moveleftkey") {
            settings.moveLeftKey = Trim(value);
        } else if (key == "moverightkey") {
            settings.moveRightKey = Trim(value);
        } else if (key == "selectkey") {
            settings.selectKey = Trim(value);
        }
    }
}

namespace PickpocketConfig {
    Settings settings;

    float GetPhase1RiskMultiplier() {
        return std::clamp(
            PickpocketConfig::settings.phase1RiskMultiplier,
            PickpocketConfig::kPhase1RiskMultiplierMin,
            PickpocketConfig::kPhase1RiskMultiplierMax);
    }

    void ClampSettings() {
        settings.phase1RiskMultiplier = std::clamp(settings.phase1RiskMultiplier, kPhase1RiskMultiplierMin, kPhase1RiskMultiplierMax);
        settings.dialValueDistanceBias = std::clamp(settings.dialValueDistanceBias, kDialValueDistanceBiasMin, kDialValueDistanceBiasMax);
        settings.uiScale = std::clamp(settings.uiScale, kUIScaleMin, kUIScaleMax);
        settings.uiXOffsetPercent = std::clamp(settings.uiXOffsetPercent, kUIXOffsetPercentMin, kUIXOffsetPercentMax);
        settings.uiYOffsetPercent = std::clamp(settings.uiYOffsetPercent, kUIYOffsetPercentMin, kUIYOffsetPercentMax);
        if (settings.placeItemsKey.empty()) {
            settings.placeItemsKey = kDefaultPlaceItemsKey;
        }
        if (settings.moveLeftKey.empty()) {
            settings.moveLeftKey = kDefaultMoveLeftKey;
        }
        if (settings.moveRightKey.empty()) {
            settings.moveRightKey = kDefaultMoveRightKey;
        }
        if (settings.selectKey.empty()) {
            settings.selectKey = kDefaultSelectKey;
        }
    }

    void LoadSettings() {
        const std::filesystem::path iniPath = kSettingsPath;
        if (!std::filesystem::exists(iniPath)) {
            return;
        }

        std::ifstream file(iniPath);
        if (!file) {
            logger::warn("Failed to open settings file for reading: {}", kSettingsPath);
            return;
        }

        std::string line;
        bool inSettingsSection = false;

        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line == kSettingsSection) {
                inSettingsSection = true;
                continue;
            }

            if (line[0] == '[') {
                inSettingsSection = false;
                continue;
            }

            if (!inSettingsSection) {
                continue;
            }

            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                continue;
            }

            ApplySetting(
                std::string_view(line).substr(0, separator),
                std::string_view(line).substr(separator + 1));
        }

        ClampSettings();
    }

    void SaveSettings() {
        ClampSettings();

        const std::filesystem::path iniPath = kSettingsPath;
        std::filesystem::create_directories(iniPath.parent_path());

        std::ofstream file(iniPath);
        if (!file) {
            logger::warn("Failed to open settings file for writing: {}", kSettingsPath);
            return;
        }

        file << "[settings]\n";
        file << "; When disabled, pickpocketing uses Skyrim's vanilla menu\n";
        file << "disableMinigame=" << (settings.disableMinigame ? "true" : "false") << "\n";
        file << "; Phase 1 detection risk multiplier Higher values make pickpocketing harder\n";
        file << "difficulty=" << settings.phase1RiskMultiplier << "\n";
        file << "; Higher values place valuable items farther from the exit in phase 2\n";
        file << "valueDistanceBias=" << settings.dialValueDistanceBias << "\n";
        file << "; Multiplies phase 1, phase 2, and control-hint UI size\n";
        file << "uiScale=" << settings.uiScale << "\n";
        file << "; Item preview renderer: Meshes or Icons\n";
        file << "previewMode=" << (settings.previewMode == PreviewMode::SkyUIIcons ? "Icons" : "Meshes") << "\n";
        file << "; Horizontal main UI offset as a percentage of screen width\n";
        file << "uiXOffset=" << settings.uiXOffsetPercent << "\n";
        file << "; Vertical main UI offset as a percentage of screen height\n";
        file << "uiYOffset=" << settings.uiYOffsetPercent << "\n";
        file << "; ImGui keys\n";
        file << "placeItemsKey=" << settings.placeItemsKey << "\n";
        file << "moveLeftKey=" << settings.moveLeftKey << "\n";
        file << "moveRightKey=" << settings.moveRightKey << "\n";
        file << "selectKey=" << settings.selectKey << "\n";
        file << "\n";
        file << "; Debug options\n";
        file << "showDebugPickpocketListDuringPhase2=" << (settings.showDebugPickpocketListDuringPhase2 ? "true" : "false") << "\n";
        file << "showPhase1DebugOverlay=" << (settings.showPhase1DebugOverlay ? "true" : "false") << "\n";
        file << "allowPickpocketingAllItemsInNPCInventory=" << (settings.allowPickpocketingAllInventoryItems ? "true" : "false") << "\n";
        file << "alwaysRevealAllItems=" << (settings.alwaysRevealAllItems ? "true" : "false") << "\n";
    }

    void EnsureSettingsFile() {
        if (!std::filesystem::exists(kSettingsPath)) {
            SaveSettings();
        }
    }
}
