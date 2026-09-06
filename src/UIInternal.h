#pragma once

#include "DialGeometry.h"
#include "Input.h"
#include "UI.h"
#include "MeshRenderingFrameworkAPI.h"
#include "PickpocketInventory.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace ImGuiMCP;

namespace UI::Internal {
    inline constexpr std::uint32_t kInventoryPreviewRenderSize = 256;
    inline constexpr float kInventoryPreviewContentScale = 0.8f;
    using namespace PickpocketRules;
    inline constexpr float kPi = std::numbers::pi_v<float>;
    inline constexpr float kDialDesignSize = DialGeometry::kDesignSize;
    inline constexpr float kDialSlotRadius = DialGeometry::kSlotRingRadius;
    inline constexpr float kDialSlotRecessRadius = 132.0f / kDialDesignSize;
    inline constexpr float kDialSlotRingRadius = 125.4f / kDialDesignSize;
    inline constexpr float kDialPreviewSize = 256.0f / kDialDesignSize;
    inline constexpr float kDialTimerRadius = 972.0f / kDialDesignSize;
    inline constexpr float kDialTimerThickness = 40.0f / kDialDesignSize;
    inline constexpr float kDialTimerFillThickness = 44.0f / kDialDesignSize;
    inline constexpr float kDialTakeTimePenaltySeconds = 1.0f;
    inline constexpr float kDialTakePenaltyFlashSeconds = 0.45f;
    inline constexpr float kDialRevealSeconds = 1.25f;
    inline constexpr float kDialObscureSeconds = 1.25f;
    inline constexpr float kDialRevealCompleteThreshold = 0.995f;
    inline constexpr float kDialMeshRevealPower = 2.5f;
    inline constexpr float kDialCoverRevealPower = 2.0f;
    inline constexpr float kDialCenterTextFadeStart = 0.90f;
    inline constexpr float kDialCursorSlotsPerSecond = 4.0f;
    inline constexpr float kDialCursorArriveEpsilon = 0.01f;
    inline constexpr float kDialInputBufferGraceSeconds = 0.12f;
    inline constexpr const char* kDialBaseSvgTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_base.svg";
    inline constexpr const char* kDialIconForegroundSvgTexturePath =
        "Data\\interface\\PickpocketMinigame\\pickpocket_base_icon_foreground.svg";
    inline constexpr const char* kDialTimerBorderSvgTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_timer_border.svg";
    inline constexpr const char* kDialTimerFillTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_timer_fill.dds";
    inline constexpr const char* kDialSelectionTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_selection.dds";
    inline constexpr const char* kDialUnknownSvgTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_slot_unknown.svg";
    inline constexpr const char* kDialExitIconSvgTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_slot_exit.svg";
    inline constexpr float kDialSvgTextureSizeSnap = 8.0f;
    inline constexpr const char* kPhase1NumberAtlasTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_number_atlas.dds";
    inline constexpr int kPhase1NumberAtlasColumns = 8;
    inline constexpr int kPhase1NumberAtlasRows = 4;
    inline constexpr int kPhase1NumberAtlasCellSize = 256;
    inline constexpr int kPhase1NumberAtlasMax = 30;
    inline constexpr const char* kPhase1TrackSvgTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_phase1_track.svg";
    inline constexpr const char* kPhase1BorderSvgTexturePath = "Data\\interface\\PickpocketMinigame\\pickpocket_phase1_border.svg";
    inline constexpr float kPhase1DesignSize = 512.0f;
    inline constexpr float kPhase1RingOuterRadius = 242.0f / kPhase1DesignSize;
    inline constexpr float kPhase1FillRadius = 225.0f / kPhase1DesignSize;
    inline constexpr float kPhase1FillThickness = 28.0f / kPhase1DesignSize;
    inline constexpr const char* kImGuiIconDirectory = "Data\\Interface\\ImGuiIcons\\Icons\\";
    inline constexpr const char* kPlantItemsHintLabel = "Place Items";
    inline constexpr const char* kMoveLeftHintLabel = "Move Left";
    inline constexpr const char* kMoveRightHintLabel = "Move Right";
    inline constexpr const char* kSelectHintLabel = "Select";
    inline constexpr float kControlHintReferenceHeight = 1080.0f;
    inline constexpr float kPlantItemsHintIconSourceSize = 64.0f;
    inline constexpr float kPlantItemsHintIconScale = 0.60f;
    inline constexpr float kPlantItemsHintFontSize = 40.0f;
    inline constexpr float kPlantItemsHintRightMargin = 32.0f;
    inline constexpr float kControlHintLeftMargin = 32.0f;
    inline constexpr float kPlantItemsHintBottomMargin = 26.0f;
    inline constexpr float kControlHintGap = 34.0f;
    inline constexpr float kSettingsKeyIconSize = 48.0f;

    struct KeyIconMapping {
        const char* fileName;
        ImGuiKey key;
        const char* configName;
    };

    enum class KeyBindingSlot {
        None,
        PlaceItems,
        MoveLeft,
        MoveRight,
        Select,
    };

    enum class InventoryPreviewPreparationStatus {
        Idle,
        Preparing,
        Ready,
        Failed,
    };

    extern SKSEMenuFramework::Model::WindowInterface* pickpocketInventoryWindow;
    extern float pickpocketWindowDimAlpha;
    extern bool plantItemsKeyWasDown;
    extern bool selectKeyWasDown;
    extern KeyBindingSlot waitingForKeyBinding;

    const char* GetKeyBindingLabel(KeyBindingSlot slot);
    const KeyIconMapping& GetDefaultKeyBindingMapping(KeyBindingSlot slot);
    const KeyIconMapping& GetKeyBindingMapping(KeyBindingSlot slot);
    std::string GetKeyIconLabel(std::string_view fileName);
    ImTextureID GetKeyIconTexture(std::string_view fileName);
    bool IsConfiguredKeyDown(ImGuiKey key);
    bool IsConfiguredKeyPressed(ImGuiKey key, bool& keyWasDown);
    void ResetKeyPickerCaptureState();
    const KeyIconMapping* GetPressedKeyIconMapping();
    void SetKeyBinding(KeyBindingSlot slot, const KeyIconMapping& mapping);
    bool __stdcall ProcessInput(RE::InputEvent* inputEvent);
    bool ConsumeControllerAction(Input::MenuAction action);
    bool IsClosePickpocketWindowInputPressed();
    bool IsOpenVanillaDepositInputPressed();

    void SaveSettingsIfChanged(bool changed);
    void SaveSettingsAfterSliderEdit();
    void RenderSettings();

    void PrepareInventoryPreviews(
        const PickpocketInventory::Snapshot& snapshot,
        std::span<const RE::FormID> previewFormIDs);
    void CancelInventoryPreviewPreparation();
    void __stdcall ProcessInventoryPreviewRenderTask();
    InventoryPreviewPreparationStatus GetInventoryPreviewPreparationStatus(
        const PickpocketInventory::Snapshot& snapshot);
    void ResetDialTimer(RE::FormID targetFormID, float timerSeconds);
    void ResetDialInteractionForNextSession();
    std::vector<RE::FormID> GetDialCandidateFormIDs(const PickpocketInventory::Snapshot& snapshot);
    void RenderCapturedInventoryMinigame();

    ImTextureID LoadSizedSvgTexture(const char* texturePath, float size);
    ImTextureID GetTimerFillTexture();
    ImTextureID GetSelectionTexture();
    float GetUIScale();
    float GetUIXOffset(const ImVec2& screenSize);
    float GetUIYOffset(const ImVec2& screenSize);
    float GetControlHintScale(const ImVec2& screenSize);
    void DrawRotatedTexture(
        ImDrawList* drawList,
        ImTextureID texture,
        ImVec2 topLeft,
        ImVec2 bottomRight,
        ImVec2 center,
        float angle,
        ImU32 color = IM_COL32_WHITE);
    ImVec2 GetDialSlotCenter(ImVec2 center, float dialSize, int slotIndex);
    ImU32 GetTimerArcColor(float timeRemaining);
    ImU32 GetPhase1RiskColor(float risk, int alpha);
    void DrawTexturedTimerArcSegment(
        ImDrawList* drawList,
        ImTextureID timerFillTexture,
        ImVec2 center,
        float radius,
        float thickness,
        float startFraction,
        float endFraction,
        ImU32 tint);
    ImVec2 CalcFontTextSize(ImFont* font, float fontSize, const std::string& text);
    float FitFontSize(ImFont* font, float desiredSize, float minSize, float maxWidth, const std::string& text);
    int ScaleAlpha(int alpha, float opacity);
    void DrawTextWithShadow(
        ImDrawList* drawList,
        ImFont* font,
        float fontSize,
        ImVec2 pos,
        ImU32 color,
        const std::string& text,
        float opacity = 1.0f);
    void DrawCenteredText(
        ImDrawList* drawList,
        ImFont* font,
        float fontSize,
        ImVec2 centerTop,
        ImU32 color,
        const std::string& text,
        float opacity = 1.0f);
    void DrawPhase1NumberFromAtlas(ImDrawList* drawList, ImVec2 center, float scale, int seconds, ImU32 color);
}
