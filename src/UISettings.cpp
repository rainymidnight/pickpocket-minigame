#include "UIInternal.h"
#include "Config.h"
#include "PickpocketEventHandler.h"
#include "PickpocketSession.h"

using namespace ImGuiMCP;
namespace ImGui = ImGuiMCP;

namespace UI::Internal {
    namespace {
        constexpr auto kMeshPreviewWarningPopup = "Enable 3D mesh previews?";
        constexpr auto kMeshPreviewWarningText =
            "3D mesh previews can currently cause game freezes during opening the menu and visual bugs in some load orders. Continue using 3D mesh previews?";
    }

    void DisableActiveMinigameSession() {
        const auto session = PickpocketSession::GetSnapshot();
        if (session.state != PickpocketSession::State::Phase1 &&
            session.state != PickpocketSession::State::Phase2Preparing &&
            session.state != PickpocketSession::State::Phase2Active) {
            return;
        }

        UI::CancelPickpocketInventoryPreparation();
        if (pickpocketInventoryWindow) {
            pickpocketInventoryWindow->IsOpen = false;
        }
        PickpocketSession::Abort("minigame disabled in settings");
        PickpocketEvents::EndPhase1TransitionPresentation();
    }

    void RenderKeyBindingSetting(KeyBindingSlot slot) {
        const auto& mapping = GetKeyBindingMapping(slot);
        const auto iconTexture = GetKeyIconTexture(mapping.fileName);
        const auto isWaitingForThisKey = waitingForKeyBinding == slot;

        ImGui::PushID(static_cast<int>(slot));
        ImGui::Text("%s", GetKeyBindingLabel(slot));
        ImGui::SameLine(180.0f);
        if (iconTexture) {
            ImGui::Image(iconTexture, { kSettingsKeyIconSize, kSettingsKeyIconSize });
        } else {
            const auto keyLabel = GetKeyIconLabel(mapping.fileName);
            ImGui::Text("%s", keyLabel.c_str());
        }

        ImGui::SameLine();

        if (isWaitingForThisKey) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ImVec4(0.25f, 0.7f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
            ImGui::Button("Press any key...");
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                waitingForKeyBinding = KeyBindingSlot::None;
                ImGui::PopID();
                return;
            }

            if (const auto* pressedKey = GetPressedKeyIconMapping()) {
                SetKeyBinding(slot, *pressedKey);
                waitingForKeyBinding = KeyBindingSlot::None;
            }
        } else {
            if (ImGui::Button("Change")) {
                waitingForKeyBinding = slot;
                ResetKeyPickerCaptureState();
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset")) {
                SetKeyBinding(slot, GetDefaultKeyBindingMapping(slot));
            }
        }
        ImGui::PopID();
    }

    void RenderSettings() {
        auto& settings = PickpocketConfig::settings;

        auto previewMode = PickpocketConfig::UsingSkyUIIcons() ? 0 : 1;
        if (ImGui::Combo("Item previews", &previewMode, "Flat SkyUI icons\0" "3D meshes (Experimental)\0\0")) {
            if (previewMode == 0) {
                settings.previewMode = PickpocketConfig::PreviewMode::SkyUIIcons;
                PickpocketConfig::SaveSettings();
            } else {
                ImGui::OpenPopup(kMeshPreviewWarningPopup);
            }
        }
        ImGui::SetNextWindowSize({ 520.0f, 0.0f }, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(
                kMeshPreviewWarningPopup,
                nullptr,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextWrapped("%s", kMeshPreviewWarningText);
            ImGui::Spacing();

            if (ImGui::Button("Accept", { 120.0f, 0.0f })) {
                settings.previewMode = PickpocketConfig::PreviewMode::Meshes;
                PickpocketConfig::SaveSettings();
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", { 120.0f, 0.0f })) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SliderFloat(
            "Difficulty",
            &settings.phase1RiskMultiplier,
            PickpocketConfig::kPhase1RiskMultiplierMin,
            PickpocketConfig::kPhase1RiskMultiplierMax,
            "%.2fx");
        SaveSettingsAfterSliderEdit();
        ImGui::SliderFloat(
            "Value distance bias",
            &settings.dialValueDistanceBias,
            PickpocketConfig::kDialValueDistanceBiasMin,
            PickpocketConfig::kDialValueDistanceBiasMax,
            "%.2f");
        SaveSettingsAfterSliderEdit();
        ImGui::SetItemTooltip("Controls how strongly valuable items are placed farther from the exit.");
        ImGui::SliderFloat("UI scale", &settings.uiScale, PickpocketConfig::kUIScaleMin, PickpocketConfig::kUIScaleMax, "%.2fx");
        SaveSettingsAfterSliderEdit();
        ImGui::SliderFloat(
            "UI X offset",
            &settings.uiXOffsetPercent,
            PickpocketConfig::kUIXOffsetPercentMin,
            PickpocketConfig::kUIXOffsetPercentMax,
            "%.0f%%");
        SaveSettingsAfterSliderEdit();
        ImGui::SliderFloat(
            "UI Y offset",
            &settings.uiYOffsetPercent,
            PickpocketConfig::kUIYOffsetPercentMin,
            PickpocketConfig::kUIYOffsetPercentMax,
            "%.0f%%");
        SaveSettingsAfterSliderEdit();
        RenderKeyBindingSetting(KeyBindingSlot::PlaceItems);
        RenderKeyBindingSetting(KeyBindingSlot::MoveLeft);
        RenderKeyBindingSetting(KeyBindingSlot::MoveRight);
        RenderKeyBindingSetting(KeyBindingSlot::Select);
        SaveSettingsIfChanged(ImGui::Checkbox("Show debug pickpocket list during phase 2", &settings.showDebugPickpocketListDuringPhase2));
        SaveSettingsIfChanged(ImGui::Checkbox("Show phase 1 debug overlay", &settings.showPhase1DebugOverlay));
        SaveSettingsIfChanged(ImGui::Checkbox("Allow pickpocketing all items in NPC inventory", &settings.allowPickpocketingAllInventoryItems));
        SaveSettingsIfChanged(ImGui::Checkbox("Always reveal all items", &settings.alwaysRevealAllItems));

        if (ImGui::Checkbox("Disable pickpocket minigame", &settings.disableMinigame)) {
            PickpocketConfig::SaveSettings();
            if (settings.disableMinigame) {
                DisableActiveMinigameSession();
            }
        }
    }

}
