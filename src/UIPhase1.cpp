#include "UIInternal.h"
#include "PickpocketEventHandler.h"
#include "Config.h"
#include "SkyUIIcons.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

using namespace ImGuiMCP;
namespace ImGui = ImGuiMCP;

namespace UI::HudOverlay {
    using namespace Internal;
    void __stdcall RenderPickpocketPhase1Counter() {
        UpdatePickpocketInventoryPresentation();

        const auto& io = *ImGui::GetIO();
        PickpocketEvents::UpdatePhase1(io.DeltaTime);

        PickpocketEvents::Phase1DebugInfo debugInfo;
        if (!PickpocketEvents::GetPhase1DebugInfo(debugInfo)) {
            return;
        }
        if (debugInfo.transitioning &&
            PickpocketConfig::UsingSkyUIIcons() &&
            SkyUIIcons::IsReadyForPresentation()) {
            return;
        }
        if (PickpocketEvents::IsPhase1Suspended() && !debugInfo.transitioning) {
            return;
        }

        auto* font = ImGui::GetFont();
        if (!font) {
            return;
        }

        const auto screenSize = io.DisplaySize;
        const auto scale = std::clamp(screenSize.y / 1080.0f, 0.75f, 1.6f) * GetUIScale();
        const auto debugFontSize = 22.0f * scale;
        const auto seconds = std::clamp(
            static_cast<int>(std::floor(debugInfo.seconds)),
            0,
            static_cast<int>(kDialTimerDurationSeconds));
        const ImVec2 center{
            screenSize.x * 0.5f + GetUIXOffset(screenSize),
            screenSize.y * 0.5f + GetUIYOffset(screenSize)
        };
        const auto risk = std::clamp(debugInfo.risk, 0.0f, 1.0f);
        const auto riskColor = GetPhase1RiskColor(risk, 255);
        const auto ringRadius = 96.0f * scale;
        const auto overlaySize = ringRadius / kPhase1RingOuterRadius;
        const ImVec2 overlayMin{ center.x - overlaySize * 0.5f, center.y - overlaySize * 0.5f };
        const ImVec2 overlayMax{ overlayMin.x + overlaySize, overlayMin.y + overlaySize };

        auto* drawList = ImGui::GetForegroundDrawList();
        const auto trackTexture = LoadSizedSvgTexture(kPhase1TrackSvgTexturePath, overlaySize);
        const auto borderTexture = LoadSizedSvgTexture(kPhase1BorderSvgTexturePath, overlaySize);
        if (trackTexture) {
            ImGui::ImDrawListManager::AddImage(
                drawList,
                trackTexture,
                overlayMin,
                overlayMax,
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32_WHITE);
        }

        // The arc banked here uses the same time scale as the phase 2 timer.
        const auto bankedFraction = std::clamp(debugInfo.seconds / kDialTimerDurationSeconds, 0.0f, 1.0f);
        if (bankedFraction > 0.0f) {
            DrawTexturedTimerArcSegment(
                drawList,
                GetTimerFillTexture(),
                center,
                overlaySize * kPhase1FillRadius,
                overlaySize * kPhase1FillThickness,
                0.0f,
                bankedFraction,
                riskColor);
        }

        if (borderTexture) {
            ImGui::ImDrawListManager::AddImage(
                drawList,
                borderTexture,
                overlayMin,
                overlayMax,
                { 0.0f, 0.0f },
                { 1.0f, 1.0f },
                IM_COL32_WHITE);
        }
        DrawPhase1NumberFromAtlas(drawList, center, scale, seconds, riskColor);

        if (!PickpocketConfig::settings.showPhase1DebugOverlay) {
            return;
        }

        const auto debugColor =
            debugInfo.targetDetected || debugInfo.anyDetected || risk >= 0.75f ?
                IM_COL32(255, 150, 120, 255) :
                risk >= 0.45f ?
                    IM_COL32(245, 220, 150, 255) :
                    IM_COL32(210, 230, 220, 255);
        const auto BoolText = [](bool value) { return value ? "yes" : "no"; };
        std::vector<std::string> debugLines;
        debugLines.reserve(9);
        debugLines.push_back(std::format(
            "{} [{:08X}]  level {}/{}  can pickpocket {}",
            debugInfo.targetName,
            debugInfo.targetFormID,
            debugInfo.targetLevel,
            debugInfo.targetBaseLevel,
            BoolText(debugInfo.targetCanPickpocket)));
        debugLines.push_back(std::format(
            "phase 1: banked {:.1f}/{:.1f}s  held {:.1f}s  risk {:.0f}%",
            debugInfo.seconds,
            debugInfo.maxBankableSeconds,
            debugInfo.elapsedSeconds,
            risk * 100.0f));
        debugLines.push_back(std::format(
            "access: chance {:.0f}%  score {:.2f}  top items {}  fill {:.2f}s/s  risk {:.2f}/s",
            debugInfo.accessChance,
            debugInfo.access,
            debugInfo.accessItemCount,
            debugInfo.fillRate,
            debugInfo.riskRate));
        debugLines.push_back(std::format(
            "target AV: pick {:.0f}  sneak {:.0f}  aggression {:.0f}  confidence {:.0f}  morality {:.0f}",
            debugInfo.targetPickpocket,
            debugInfo.targetSneak,
            debugInfo.targetAggression,
            debugInfo.targetConfidence,
            debugInfo.targetMorality));
        debugLines.push_back(std::format(
            "target state: guard {}  combat {}  teammate {}  hostile {}",
            BoolText(debugInfo.targetIsGuard),
            BoolText(debugInfo.targetIsInCombat),
            BoolText(debugInfo.targetIsTeammate),
            BoolText(debugInfo.targetIsHostileToPlayer)));
        debugLines.push_back(std::format(
            "crime faction {:08X}  pickpocket bounty {}",
            debugInfo.crimeFactionFormID,
            debugInfo.pickpocketCrimeGold));
        debugLines.push_back(std::format(
            "player AV: pick {:.0f}  sneak {:.0f}  pick mod {:.0f}  pick power {:.0f}",
            debugInfo.playerPickpocket,
            debugInfo.playerSneak,
            debugInfo.playerPickpocketModifier,
            debugInfo.playerPickpocketPowerModifier));
        if (debugInfo.baselineChanceAvailable) {
            debugLines.push_back(std::format(
                "vanilla baseline chance: hidden {}%  detected {}%  (iron sword 0 value/weight probe)",
                debugInfo.baselineHiddenChance,
                debugInfo.baselineDetectedChance));
        }
        debugLines.push_back(std::format(
            "detection: target {} ({})  highest {}  los {} ({})",
            debugInfo.targetDetectionLevel,
            debugInfo.targetDetected ? "detected" : "hidden",
            static_cast<int>(debugInfo.highestDetectionLevel),
            debugInfo.losCount,
            debugInfo.anyDetected ? "detected" : "hidden"));

        const auto debugLineHeight = debugFontSize * 1.15f;
        const auto debugBottom = center.y - ringRadius - 20.0f * scale;
        const auto debugTop = debugBottom - static_cast<float>(debugLines.size() - 1) * debugLineHeight;
        for (std::size_t i = 0; i < debugLines.size(); ++i) {
            DrawCenteredText(
                drawList,
                font,
                debugFontSize,
                { center.x, debugTop + static_cast<float>(i) * debugLineHeight },
                debugColor,
                debugLines[i]);
        }
    }
}
