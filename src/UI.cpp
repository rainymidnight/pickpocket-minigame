#include "UIInternal.h"
#include "MeshRenderingFrameworkAPI.h"
#include "PickpocketEventHandler.h"
#include "PickpocketInventory.h"
#include "PickpocketRevealRules.h"
#include "PickpocketSession.h"
#include "Config.h"
#include "SkyUIIcons.h"
#include "StringUtils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ImGuiMCP;
namespace ImGui = ImGuiMCP;

namespace logger = SKSE::log;

namespace UI::Internal {
#define PICKPOCKET_KEY_ICON(fileName, key) KeyIconMapping{ fileName, key, #key }
    constexpr auto kKeyIconMappings = std::to_array<KeyIconMapping>({
        PICKPOCKET_KEY_ICON("0.png", ImGuiKey_0),
        PICKPOCKET_KEY_ICON("1.png", ImGuiKey_1),
        PICKPOCKET_KEY_ICON("2.png", ImGuiKey_2),
        PICKPOCKET_KEY_ICON("3.png", ImGuiKey_3),
        PICKPOCKET_KEY_ICON("4.png", ImGuiKey_4),
        PICKPOCKET_KEY_ICON("5.png", ImGuiKey_5),
        PICKPOCKET_KEY_ICON("6.png", ImGuiKey_6),
        PICKPOCKET_KEY_ICON("7.png", ImGuiKey_7),
        PICKPOCKET_KEY_ICON("8.png", ImGuiKey_8),
        PICKPOCKET_KEY_ICON("9.png", ImGuiKey_9),
        PICKPOCKET_KEY_ICON("A.png", ImGuiKey_A),
        PICKPOCKET_KEY_ICON("B.png", ImGuiKey_B),
        PICKPOCKET_KEY_ICON("C.png", ImGuiKey_C),
        PICKPOCKET_KEY_ICON("D.png", ImGuiKey_D),
        PICKPOCKET_KEY_ICON("E.png", ImGuiKey_E),
        PICKPOCKET_KEY_ICON("F.png", ImGuiKey_F),
        PICKPOCKET_KEY_ICON("G.png", ImGuiKey_G),
        PICKPOCKET_KEY_ICON("H.png", ImGuiKey_H),
        PICKPOCKET_KEY_ICON("I.png", ImGuiKey_I),
        PICKPOCKET_KEY_ICON("J.png", ImGuiKey_J),
        PICKPOCKET_KEY_ICON("K.png", ImGuiKey_K),
        PICKPOCKET_KEY_ICON("L.png", ImGuiKey_L),
        PICKPOCKET_KEY_ICON("M.png", ImGuiKey_M),
        PICKPOCKET_KEY_ICON("N.png", ImGuiKey_N),
        PICKPOCKET_KEY_ICON("O.png", ImGuiKey_O),
        PICKPOCKET_KEY_ICON("P.png", ImGuiKey_P),
        PICKPOCKET_KEY_ICON("Q.png", ImGuiKey_Q),
        PICKPOCKET_KEY_ICON("R.png", ImGuiKey_R),
        PICKPOCKET_KEY_ICON("S.png", ImGuiKey_S),
        PICKPOCKET_KEY_ICON("T.png", ImGuiKey_T),
        PICKPOCKET_KEY_ICON("U.png", ImGuiKey_U),
        PICKPOCKET_KEY_ICON("V.png", ImGuiKey_V),
        PICKPOCKET_KEY_ICON("W.png", ImGuiKey_W),
        PICKPOCKET_KEY_ICON("X.png", ImGuiKey_X),
        PICKPOCKET_KEY_ICON("Y.png", ImGuiKey_Y),
        PICKPOCKET_KEY_ICON("Z.png", ImGuiKey_Z),
        PICKPOCKET_KEY_ICON("F1.png", ImGuiKey_F1),
        PICKPOCKET_KEY_ICON("F2.png", ImGuiKey_F2),
        PICKPOCKET_KEY_ICON("F3.png", ImGuiKey_F3),
        PICKPOCKET_KEY_ICON("F4.png", ImGuiKey_F4),
        PICKPOCKET_KEY_ICON("F5.png", ImGuiKey_F5),
        PICKPOCKET_KEY_ICON("F6.png", ImGuiKey_F6),
        PICKPOCKET_KEY_ICON("F7.png", ImGuiKey_F7),
        PICKPOCKET_KEY_ICON("F8.png", ImGuiKey_F8),
        PICKPOCKET_KEY_ICON("F9.png", ImGuiKey_F9),
        PICKPOCKET_KEY_ICON("F10.png", ImGuiKey_F10),
        PICKPOCKET_KEY_ICON("F11.png", ImGuiKey_F11),
        PICKPOCKET_KEY_ICON("F12.png", ImGuiKey_F12),
        PICKPOCKET_KEY_ICON("Backslash.png", ImGuiKey_Backslash),
        PICKPOCKET_KEY_ICON("Backspace.png", ImGuiKey_Backspace),
        PICKPOCKET_KEY_ICON("Bracketleft.png", ImGuiKey_LeftBracket),
        PICKPOCKET_KEY_ICON("Bracketright.png", ImGuiKey_RightBracket),
        PICKPOCKET_KEY_ICON("CapsLock.png", ImGuiKey_CapsLock),
        PICKPOCKET_KEY_ICON("Comma.png", ImGuiKey_Comma),
        PICKPOCKET_KEY_ICON("Delete.png", ImGuiKey_Delete),
        PICKPOCKET_KEY_ICON("Down.png", ImGuiKey_DownArrow),
        PICKPOCKET_KEY_ICON("End.png", ImGuiKey_End),
        PICKPOCKET_KEY_ICON("Enter.png", ImGuiKey_Enter),
        PICKPOCKET_KEY_ICON("Equal.png", ImGuiKey_Equal),
        PICKPOCKET_KEY_ICON("Esc.png", ImGuiKey_Escape),
        PICKPOCKET_KEY_ICON("Home.png", ImGuiKey_Home),
        PICKPOCKET_KEY_ICON("Hyphen.png", ImGuiKey_Minus),
        PICKPOCKET_KEY_ICON("Insert.png", ImGuiKey_Insert),
        PICKPOCKET_KEY_ICON("Keypad1.png", ImGuiKey_Keypad1),
        PICKPOCKET_KEY_ICON("Keypad2.png", ImGuiKey_Keypad2),
        PICKPOCKET_KEY_ICON("Keypad3.png", ImGuiKey_Keypad3),
        PICKPOCKET_KEY_ICON("Keypad4.png", ImGuiKey_Keypad4),
        PICKPOCKET_KEY_ICON("Keypad5.png", ImGuiKey_Keypad5),
        PICKPOCKET_KEY_ICON("Keypad6.png", ImGuiKey_Keypad6),
        PICKPOCKET_KEY_ICON("Keypad7.png", ImGuiKey_Keypad7),
        PICKPOCKET_KEY_ICON("Keypad8.png", ImGuiKey_Keypad8),
        PICKPOCKET_KEY_ICON("KeypadEnter.png", ImGuiKey_KeypadEnter),
        PICKPOCKET_KEY_ICON("L-Alt.png", ImGuiKey_LeftAlt),
        PICKPOCKET_KEY_ICON("L-Ctrl.png", ImGuiKey_LeftCtrl),
        PICKPOCKET_KEY_ICON("L-Shift.png", ImGuiKey_LeftShift),
        PICKPOCKET_KEY_ICON("Left.png", ImGuiKey_LeftArrow),
        PICKPOCKET_KEY_ICON("Mouse1.png", ImGuiKey_MouseLeft),
        PICKPOCKET_KEY_ICON("Mouse2.png", ImGuiKey_MouseRight),
        PICKPOCKET_KEY_ICON("Mouse3.png", ImGuiKey_MouseMiddle),
        PICKPOCKET_KEY_ICON("Mouse4.png", ImGuiKey_MouseX1),
        PICKPOCKET_KEY_ICON("Mouse5.png", ImGuiKey_MouseX2),
        PICKPOCKET_KEY_ICON("NumLock.png", ImGuiKey_NumLock),
        PICKPOCKET_KEY_ICON("NumPad0.png", ImGuiKey_Keypad0),
        PICKPOCKET_KEY_ICON("NumPad9.png", ImGuiKey_Keypad9),
        PICKPOCKET_KEY_ICON("NumPadDec.png", ImGuiKey_KeypadDecimal),
        PICKPOCKET_KEY_ICON("NumPadDivide.png", ImGuiKey_KeypadDivide),
        PICKPOCKET_KEY_ICON("NumPadMinus.png", ImGuiKey_KeypadSubtract),
        PICKPOCKET_KEY_ICON("NumPadMult.png", ImGuiKey_KeypadMultiply),
        PICKPOCKET_KEY_ICON("NumPadPlus.png", ImGuiKey_KeypadAdd),
        PICKPOCKET_KEY_ICON("Pause.png", ImGuiKey_Pause),
        PICKPOCKET_KEY_ICON("Period.png", ImGuiKey_Period),
        PICKPOCKET_KEY_ICON("PgDn.png", ImGuiKey_PageDown),
        PICKPOCKET_KEY_ICON("PgUp.png", ImGuiKey_PageUp),
        PICKPOCKET_KEY_ICON("PrintScreen.png", ImGuiKey_PrintScreen),
        PICKPOCKET_KEY_ICON("Quotesingle.png", ImGuiKey_Apostrophe),
        PICKPOCKET_KEY_ICON("R-Alt.png", ImGuiKey_RightAlt),
        PICKPOCKET_KEY_ICON("R-Ctrl.png", ImGuiKey_RightCtrl),
        PICKPOCKET_KEY_ICON("R-Shift.png", ImGuiKey_RightShift),
        PICKPOCKET_KEY_ICON("Right.png", ImGuiKey_RightArrow),
        PICKPOCKET_KEY_ICON("ScrollLock.png", ImGuiKey_ScrollLock),
        PICKPOCKET_KEY_ICON("Semicolon.png", ImGuiKey_Semicolon),
        PICKPOCKET_KEY_ICON("Slash.png", ImGuiKey_Slash),
        PICKPOCKET_KEY_ICON("Space.png", ImGuiKey_Space),
        PICKPOCKET_KEY_ICON("Tab.png", ImGuiKey_Tab),
        PICKPOCKET_KEY_ICON("Tilde.png", ImGuiKey_GraveAccent),
        PICKPOCKET_KEY_ICON("Up.png", ImGuiKey_UpArrow),
    });
#undef PICKPOCKET_KEY_ICON

    SKSEMenuFramework::Model::WindowInterface* pickpocketInventoryWindow = nullptr;
    float pickpocketWindowDimAlpha = 0.55f;
    bool plantItemsKeyWasDown = false;
    bool selectKeyWasDown = false;
    KeyBindingSlot waitingForKeyBinding = KeyBindingSlot::None;

    namespace {
        // Sized SVGs are rasterized per draw size, so unlike the other caches
        // there is no per-texture slot to remember a failure in.
        std::unordered_set<std::string> failedSvgTextureWarnings;
        std::unordered_map<std::string, ImTextureID> keyIconTextures;
        std::array<bool, kKeyIconMappings.size()> keyPickerPreviousDownStates{};
    }

    std::string_view GetImGuiKeySuffix(std::string_view configName) {
        constexpr std::string_view prefix = "ImGuiKey_";
        return configName.starts_with(prefix) ? configName.substr(prefix.size()) : configName;
    }

    const KeyIconMapping* FindKeyIconMappingByConfigValue(std::string_view value) {
        const auto trimmedValue = StringUtils::TrimView(value);
        for (const auto& mapping : kKeyIconMappings) {
            if (StringUtils::EqualsIgnoreCaseAscii(mapping.configName, trimmedValue) ||
                StringUtils::EqualsIgnoreCaseAscii(GetImGuiKeySuffix(mapping.configName), trimmedValue)) {
                return &mapping;
            }
        }

        return nullptr;
    }

    const char* GetDefaultKeyConfigName(KeyBindingSlot slot) {
        switch (slot) {
        case KeyBindingSlot::PlaceItems:
            return PickpocketConfig::kDefaultPlaceItemsKey;
        case KeyBindingSlot::MoveLeft:
            return PickpocketConfig::kDefaultMoveLeftKey;
        case KeyBindingSlot::MoveRight:
            return PickpocketConfig::kDefaultMoveRightKey;
        case KeyBindingSlot::Select:
            return PickpocketConfig::kDefaultSelectKey;
        default:
            assert(false);
            return PickpocketConfig::kDefaultPlaceItemsKey;
        }
    }

    const char* GetKeyBindingLabel(KeyBindingSlot slot) {
        switch (slot) {
        case KeyBindingSlot::PlaceItems:
            return "Place items key";
        case KeyBindingSlot::MoveLeft:
            return "Move left key";
        case KeyBindingSlot::MoveRight:
            return "Move right key";
        case KeyBindingSlot::Select:
            return "Select key";
        default:
            assert(false);
            return "Key";
        }
    }

    std::string& GetConfiguredKeyConfigName(KeyBindingSlot slot) {
        auto& settings = PickpocketConfig::settings;
        switch (slot) {
        case KeyBindingSlot::PlaceItems:
            return settings.placeItemsKey;
        case KeyBindingSlot::MoveLeft:
            return settings.moveLeftKey;
        case KeyBindingSlot::MoveRight:
            return settings.moveRightKey;
        case KeyBindingSlot::Select:
            return settings.selectKey;
        default:
            assert(false);
            return settings.placeItemsKey;
        }
    }

    const KeyIconMapping& GetDefaultKeyBindingMapping(KeyBindingSlot slot) {
        const auto* mapping = FindKeyIconMappingByConfigValue(GetDefaultKeyConfigName(slot));
        assert(mapping);
        return *mapping;
    }

    // Resolving a binding scans every known key icon, and the dial asks for its
    // bindings several times a frame. Remember the last answer per slot and only
    // rescan when the configured name actually changes.
    const KeyIconMapping& GetKeyBindingMapping(KeyBindingSlot slot) {
        struct ResolvedBinding {
            std::string configName;
            const KeyIconMapping* mapping = nullptr;
        };
        static std::array<ResolvedBinding, 5> resolvedBindings;

        auto& configured = GetConfiguredKeyConfigName(slot);
        auto& resolved = resolvedBindings[static_cast<std::size_t>(slot)];
        if (resolved.mapping && resolved.configName == configured) {
            return *resolved.mapping;
        }

        const auto* mapping = FindKeyIconMappingByConfigValue(configured);
        if (!mapping) {
            mapping = &GetDefaultKeyBindingMapping(slot);
        }

        configured = mapping->configName;
        resolved = { configured, mapping };
        return *mapping;
    }

    std::string GetKeyIconLabel(std::string_view fileName) {
        return std::string(fileName.substr(0, fileName.rfind('.')));
    }

    ImTextureID GetKeyIconTexture(std::string_view fileName) {
        const auto path = std::string(kImGuiIconDirectory).append(fileName);
        if (const auto cached = keyIconTextures.find(path); cached != keyIconTextures.end()) {
            return cached->second;
        }

        const auto texture = SKSEMenuFramework::LoadTexture(path);
        keyIconTextures.emplace(path, texture);
        if (!texture) {
            logger::warn("Failed to load key icon {}", path);
        }

        return texture;
    }

    bool IsConfiguredKeyDown(ImGuiKey key) {
        const auto& io = *ImGui::GetIO();
        if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt || key == ImGuiKey_ReservedForModAlt) {
            return
                io.KeyAlt ||
                ImGui::IsKeyDown(ImGuiKey_LeftAlt) ||
                ImGui::IsKeyDown(ImGuiKey_RightAlt) ||
                ImGui::IsKeyDown(ImGuiKey_ReservedForModAlt);
        }

        return ImGui::IsKeyDown(key);
    }

    bool IsConfiguredKeyPressed(ImGuiKey key, bool& keyWasDown) {
        const auto keyDown = IsConfiguredKeyDown(key);
        const auto pressed = keyDown && !keyWasDown;
        keyWasDown = keyDown;
        return pressed;
    }

    void ResetKeyPickerCaptureState() {
        for (std::size_t index = 0; index < kKeyIconMappings.size(); ++index) {
            keyPickerPreviousDownStates[index] = IsConfiguredKeyDown(kKeyIconMappings[index].key);
        }
    }

    const KeyIconMapping* GetPressedKeyIconMapping() {
        const KeyIconMapping* pressedMapping = nullptr;
        for (std::size_t index = 0; index < kKeyIconMappings.size(); ++index) {
            const auto keyDown = IsConfiguredKeyDown(kKeyIconMappings[index].key);
            if (!pressedMapping && keyDown && !keyPickerPreviousDownStates[index]) {
                pressedMapping = &kKeyIconMappings[index];
            }

            keyPickerPreviousDownStates[index] = keyDown;
        }

        return pressedMapping;
    }

    void SetKeyBinding(KeyBindingSlot slot, const KeyIconMapping& mapping) {
        GetConfiguredKeyConfigName(slot) = mapping.configName;
        PickpocketConfig::SaveSettings();
        if (slot == KeyBindingSlot::PlaceItems) {
            plantItemsKeyWasDown = IsConfiguredKeyDown(mapping.key);
        } else if (slot == KeyBindingSlot::Select) {
            selectKeyWasDown = IsConfiguredKeyDown(mapping.key);
        }
    }

    bool IsClosePickpocketWindowInputPressed() {
        return ConsumeControllerAction(Input::MenuAction::Cancel) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
    }

    bool IsOpenVanillaDepositInputPressed() {
        const auto controllerPressed =
            ConsumeControllerAction(Input::MenuAction::Place) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false);
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            plantItemsKeyWasDown = false;
            return controllerPressed;
        }

        return IsConfiguredKeyPressed(GetKeyBindingMapping(KeyBindingSlot::PlaceItems).key, plantItemsKeyWasDown) ||
            controllerPressed;
    }

    void SaveSettingsIfChanged(bool changed) {
        if (changed) {
            PickpocketConfig::SaveSettings();
        }
    }

    void SaveSettingsAfterSliderEdit() {
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            PickpocketConfig::SaveSettings();
        }
    }

    // A fixed-size texture shipped with the mod: loaded and reported at most once.
    struct PackagedTexture {
        ImTextureID texture = nullptr;
        bool loadAttempted = false;
    };

    ImTextureID LoadPackagedTexture(PackagedTexture& slot, const char* texturePath) {
        if (!slot.loadAttempted) {
            slot.loadAttempted = true;
            slot.texture = SKSEMenuFramework::LoadTexture(texturePath);
            if (!slot.texture) {
                logger::warn("Failed to load pickpocket texture {}", texturePath);
            }
        }

        return slot.texture;
    }

    float SnapSvgTextureSize(float size) {
        const auto safeSize = std::max(size, kDialSvgTextureSizeSnap);
        return std::ceil(safeSize / kDialSvgTextureSizeSnap) * kDialSvgTextureSizeSnap;
    }

    ImTextureID LoadSizedSvgTexture(const char* texturePath, float size) {
        const auto rasterSize = SnapSvgTextureSize(size);
        const auto texture = SKSEMenuFramework::LoadTexture(texturePath, { rasterSize, rasterSize });
        if (texture) {
            return texture;
        }

        if (failedSvgTextureWarnings.insert(texturePath).second) {
            logger::warn("Failed to load pickpocket SVG texture {}", texturePath);
        }

        return nullptr;
    }

    ImTextureID GetTimerFillTexture() {
        static PackagedTexture timerFill;
        return LoadPackagedTexture(timerFill, kDialTimerFillTexturePath);
    }

    ImTextureID GetSelectionTexture() {
        static PackagedTexture selection;
        return LoadPackagedTexture(selection, kDialSelectionTexturePath);
    }

    ImTextureID GetPhase1NumberAtlasTexture() {
        static PackagedTexture numberAtlas;
        return LoadPackagedTexture(numberAtlas, kPhase1NumberAtlasTexturePath);
    }

    float GetUIScale() {
        return std::clamp(PickpocketConfig::settings.uiScale, PickpocketConfig::kUIScaleMin, PickpocketConfig::kUIScaleMax);
    }

    float GetUIXOffset(const ImVec2& screenSize) {
        return screenSize.x *
            std::clamp(
                PickpocketConfig::settings.uiXOffsetPercent,
                PickpocketConfig::kUIXOffsetPercentMin,
                PickpocketConfig::kUIXOffsetPercentMax) *
            0.01f;
    }

    float GetUIYOffset(const ImVec2& screenSize) {
        return screenSize.y *
            std::clamp(
                PickpocketConfig::settings.uiYOffsetPercent,
                PickpocketConfig::kUIYOffsetPercentMin,
                PickpocketConfig::kUIYOffsetPercentMax) *
            0.01f;
    }

    float GetControlHintScale(const ImVec2& screenSize) {
        const auto baseScale = screenSize.y > 0.0f ? screenSize.y / kControlHintReferenceHeight : 1.0f;
        return baseScale * GetUIScale();
    }

    ImVec2 RotateAround(ImVec2 point, ImVec2 center, float angle) {
        const auto s = std::sin(angle);
        const auto c = std::cos(angle);
        point.x -= center.x;
        point.y -= center.y;

        return {
            center.x + point.x * c - point.y * s,
            center.y + point.x * s + point.y * c
        };
    }

    void DrawRotatedTexture(
        ImDrawList* drawList,
        ImTextureID texture,
        ImVec2 topLeft,
        ImVec2 bottomRight,
        ImVec2 center,
        float angle,
        ImU32 color) {
        if (!texture) {
            return;
        }

        const ImVec2 topRight{ bottomRight.x, topLeft.y };
        const ImVec2 bottomLeft{ topLeft.x, bottomRight.y };

        ImGui::ImDrawListManager::AddImageQuad(
            drawList,
            texture,
            RotateAround(topLeft, center, angle),
            RotateAround(topRight, center, angle),
            RotateAround(bottomRight, center, angle),
            RotateAround(bottomLeft, center, angle),
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 1.0f },
            color);
    }

    ImVec2 GetDialSlotCenter(ImVec2 center, float dialSize, int slotIndex) {
        const auto angle = -kPi * 0.5f + static_cast<float>(slotIndex) * DialGeometry::kSlotAngleRadians;
        const auto radius = dialSize * kDialSlotRadius;

        return {
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        };
    }

    int LerpColorChannel(int from, int to, float progress) {
        return static_cast<int>(std::round(static_cast<float>(from) + static_cast<float>(to - from) * progress));
    }

    // White -> gold -> red
    ImU32 GetWarningRampColor(float progress, int alpha) {
        const auto clampedProgress = std::clamp(progress, 0.0f, 1.0f);

        if (clampedProgress <= 0.5f) {
            const auto yellowProgress = clampedProgress / 0.5f;
            return IM_COL32(
                255,
                LerpColorChannel(255, 214, yellowProgress),
                LerpColorChannel(255, 0, yellowProgress),
                alpha);
        }

        const auto redProgress = (clampedProgress - 0.5f) / 0.5f;
        return IM_COL32(
            255,
            LerpColorChannel(214, 54, redProgress),
            LerpColorChannel(0, 46, redProgress),
            alpha);
    }

    ImU32 GetTimerArcColor(float timeRemaining) {
        const auto danger = std::clamp((0.25f - timeRemaining) / 0.25f, 0.0f, 1.0f);
        return GetWarningRampColor(danger, 255);
    }

    ImU32 GetPhase1RiskColor(float risk, int alpha) {
        const auto clampedRisk = std::clamp(risk, 0.0f, 1.0f);
        if (clampedRisk <= 0.40f) {
            return IM_COL32(255, 255, 255, alpha);
        }

        return GetWarningRampColor((clampedRisk - 0.40f) / 0.60f, alpha);
    }

    ImVec2 PointOnTimerArc(ImVec2 center, float radius, float angle) {
        return {
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        };
    }

    void DrawTexturedTimerArcSegment(
        ImDrawList* drawList,
        ImTextureID timerFillTexture,
        ImVec2 center,
        float radius,
        float thickness,
        float startFraction,
        float endFraction,
        ImU32 tint) {
        const auto clampedStart = std::clamp(startFraction, 0.0f, 1.0f);
        const auto clampedEnd = std::clamp(endFraction, 0.0f, 1.0f);
        if (!timerFillTexture || clampedEnd <= clampedStart) {
            return;
        }

        const auto startAngle = -kPi * 0.5f;
        const auto halfThickness = thickness * 0.5f;
        const auto innerRadius = radius - halfThickness;
        const auto outerRadius = radius + halfThickness;
        const auto arcStartAngle = startAngle + clampedStart * kPi * 2.0f;
        const auto arcEndAngle = startAngle + clampedEnd * kPi * 2.0f;
        const auto arcFraction = clampedEnd - clampedStart;
        const auto segmentCount = std::max(24, static_cast<int>(std::ceil(360.0f * arcFraction)));

        for (int i = 0; i < segmentCount; ++i) {
            const auto segmentStart = static_cast<float>(i) / static_cast<float>(segmentCount);
            const auto segmentEnd = static_cast<float>(i + 1) / static_cast<float>(segmentCount);
            const auto angle0 = arcStartAngle + (arcEndAngle - arcStartAngle) * segmentStart;
            const auto angle1 = arcStartAngle + (arcEndAngle - arcStartAngle) * segmentEnd;
            const auto uv0 = clampedStart + (clampedEnd - clampedStart) * segmentStart;
            const auto uv1 = clampedStart + (clampedEnd - clampedStart) * segmentEnd;

            const auto inner0 = PointOnTimerArc(center, innerRadius, angle0);
            const auto outer0 = PointOnTimerArc(center, outerRadius, angle0);
            const auto outer1 = PointOnTimerArc(center, outerRadius, angle1);
            const auto inner1 = PointOnTimerArc(center, innerRadius, angle1);

            ImGui::ImDrawListManager::AddImageQuad(
                drawList,
                timerFillTexture,
                inner0,
                outer0,
                outer1,
                inner1,
                { uv0, 1.0f },
                { uv0, 0.0f },
                { uv1, 0.0f },
                { uv1, 1.0f },
                tint);
        }
    }

    ImVec2 CalcFontTextSize(ImFont* font, float fontSize, const std::string& text) {
        return ImGui::ImFontManger::CalcTextSizeA(font, fontSize, 100000.0f, 0.0f, text.c_str(), nullptr, nullptr);
    }

    float FitFontSize(ImFont* font, float desiredSize, float minSize, float maxWidth, const std::string& text) {
        auto fontSize = desiredSize;
        while (fontSize > minSize && CalcFontTextSize(font, fontSize, text).x > maxWidth) {
            fontSize *= 0.92f;
        }

        return std::max(fontSize, minSize);
    }

    int ScaleAlpha(int alpha, float opacity) {
        return std::clamp(static_cast<int>(static_cast<float>(alpha) * std::clamp(opacity, 0.0f, 1.0f)), 0, 255);
    }

    void DrawTextWithShadow(
        ImDrawList* drawList,
        ImFont* font,
        float fontSize,
        ImVec2 pos,
        ImU32 color,
        const std::string& text,
        float opacity) {
        constexpr float shadowOffset = 1.5f;
        const ImVec2 shadowPos{ pos.x + shadowOffset, pos.y + shadowOffset };
        ImGui::ImDrawListManager::AddText(
            drawList,
            font,
            fontSize,
            shadowPos,
            IM_COL32(0, 0, 0, ScaleAlpha(190, opacity)),
            text.c_str());
        ImGui::ImDrawListManager::AddText(drawList, font, fontSize, pos, color, text.c_str());
    }

    void DrawCenteredText(ImDrawList* drawList, ImFont* font, float fontSize, ImVec2 centerTop, ImU32 color, const std::string& text, float opacity) {
        const auto textSize = CalcFontTextSize(font, fontSize, text);
        DrawTextWithShadow(
            drawList,
            font,
            fontSize,
            { centerTop.x - textSize.x * 0.5f, centerTop.y },
            color,
            text,
            opacity);
    }

    void DrawPhase1NumberFromAtlas(ImDrawList* drawList, ImVec2 center, float scale, int seconds, ImU32 color) {
        auto texture = GetPhase1NumberAtlasTexture();
        if (!texture) {
            return;
        }

        const auto clampedSeconds = std::clamp(seconds, 0, kPhase1NumberAtlasMax);
        const auto column = clampedSeconds % kPhase1NumberAtlasColumns;
        const auto row = clampedSeconds / kPhase1NumberAtlasColumns;
        const auto atlasWidth = static_cast<float>(kPhase1NumberAtlasColumns * kPhase1NumberAtlasCellSize);
        const auto atlasHeight = static_cast<float>(kPhase1NumberAtlasRows * kPhase1NumberAtlasCellSize);
        const auto cell = static_cast<float>(kPhase1NumberAtlasCellSize);
        const auto u0 = (static_cast<float>(column) * cell + 0.5f) / atlasWidth;
        const auto v0 = (static_cast<float>(row) * cell + 0.5f) / atlasHeight;
        const auto u1 = (static_cast<float>(column + 1) * cell - 0.5f) / atlasWidth;
        const auto v1 = (static_cast<float>(row + 1) * cell - 0.5f) / atlasHeight;

        const auto drawSize = std::round(132.0f * scale);
        const ImVec2 min{ std::round(center.x - drawSize * 0.5f), std::round(center.y - drawSize * 0.5f) };
        const ImVec2 max{ min.x + drawSize, min.y + drawSize };
        constexpr float shadowOffset = 1.0f;
        ImGui::ImDrawListManager::AddImage(
            drawList,
            texture,
            { min.x + shadowOffset, min.y + shadowOffset },
            { max.x + shadowOffset, max.y + shadowOffset },
            { u0, v0 },
            { u1, v1 },
            IM_COL32(0, 0, 0, 170));
        ImGui::ImDrawListManager::AddImage(drawList, texture, min, max, { u0, v0 }, { u1, v1 }, color);
    }

}

namespace UI {
    using namespace Internal;
    void Register() {

        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("SKSEMenuFramework not found");
            return;
        }

        SKSEMenuFramework::SetSection("Pickpocket Minigame");
        SKSEMenuFramework::AddSectionItem("Settings", Main::Render);
        pickpocketInventoryWindow = SKSEMenuFramework::AddWindow(PickpocketInventoryWindow::Render, true);
        SKSEMenuFramework::AddInputEvent(Internal::ProcessInput);
        if (!SkyUIIcons::Register()) {
            logger::warn("SkyUI icon previews could not be registered; 3D mesh previews remain available");
        }

        SKSEMenuFramework::AddHudElement(Internal::ProcessInventoryPreviewRenderTask);
        SKSEMenuFramework::AddHudElement(HudOverlay::RenderMenuDimmer);
        SKSEMenuFramework::AddHudElement(HudOverlay::RenderPickpocketPhase1Counter);

        logger::info("UI menus registered successfully");
    }

    namespace {
        std::atomic_bool meshPresentationPending{ false };

        std::vector<RE::FormID> GetPreviewFormIDs(const PickpocketInventory::Snapshot& snapshot) {
            if (PickpocketConfig::UsingSkyUIIcons() ||
                !PickpocketConfig::settings.showDebugPickpocketListDuringPhase2) {
                return GetDialCandidateFormIDs(snapshot);
            }

            std::vector<RE::FormID> formIDs;
            formIDs.reserve(snapshot.items.size());
            std::ranges::transform(
                snapshot.items,
                std::back_inserter(formIDs),
                &PickpocketInventory::Item::formID);
            return formIDs;
        }

        // Only one preview backend may hold resources at a time. `presenting`
        // tells the icon backend whether the dial is about to be shown or is
        // merely being warmed up during phase 1.
        void StartPreviewPreparation(const PickpocketInventory::Snapshot& snapshot, bool presenting) {
            const auto previewFormIDs = GetPreviewFormIDs(snapshot);
            if (!PickpocketConfig::UsingSkyUIIcons()) {
                SkyUIIcons::EndSession();
                PrepareInventoryPreviews(snapshot, previewFormIDs);
                return;
            }

            CancelInventoryPreviewPreparation();
            if (presenting) {
                SkyUIIcons::BeginSession(snapshot, previewFormIDs);
            } else {
                SkyUIIcons::PrepareSession(snapshot, previewFormIDs);
            }
        }
    }

    void PreparePickpocketInventoryWindow() {
        StartPreviewPreparation(PickpocketInventory::GetSnapshot(), false);
    }

    void CancelPickpocketInventoryPreparation() {
        meshPresentationPending.store(false, std::memory_order_release);
        SkyUIIcons::EndSession();
        CancelInventoryPreviewPreparation();
    }

    void UpdatePickpocketInventoryPresentation() {
        if (PickpocketConfig::UsingSkyUIIcons() ||
            !meshPresentationPending.load(std::memory_order_acquire)) {
            return;
        }

        const auto session = PickpocketSession::GetSnapshot();
        if (session.state != PickpocketSession::State::Phase2Preparing) {
            meshPresentationPending.store(false, std::memory_order_release);
            return;
        }

        const auto snapshot = PickpocketInventory::GetSnapshot();
        if (session.targetHandle != snapshot.targetHandle.native_handle()) {
            CancelPickpocketInventoryPreparation();
            PickpocketSession::Abort("mesh preparation target changed");
            PickpocketEvents::EndPhase1TransitionPresentation();
            return;
        }

        switch (GetInventoryPreviewPreparationStatus(snapshot)) {
        case InventoryPreviewPreparationStatus::Ready:
            meshPresentationPending.store(false, std::memory_order_release);
            pickpocketInventoryWindow->IsOpen = true;
            return;
        case InventoryPreviewPreparationStatus::Failed:
            logger::error("Pickpocket phase 2 aborted because its inventory meshes could not be prepared");
            CancelPickpocketInventoryPreparation();
            PickpocketSession::Abort("inventory mesh preparation failed");
            PickpocketEvents::EndPhase1TransitionPresentation();
            return;
        case InventoryPreviewPreparationStatus::Idle:
            logger::error("Pickpocket phase 2 aborted because its prepared inventory snapshot became stale");
            CancelPickpocketInventoryPreparation();
            PickpocketSession::Abort("inventory mesh preparation became stale");
            PickpocketEvents::EndPhase1TransitionPresentation();
            return;
        case InventoryPreviewPreparationStatus::Preparing:
            return;
        }
    }

    bool OpenPickpocketInventoryWindow(float timerSeconds) {
        if (PickpocketConfig::settings.disableMinigame) {
            return false;
        }

        if (!pickpocketInventoryWindow) {
            logger::warn("Pickpocket inventory window was not registered");
            SkyUIIcons::EndSession();
            CancelInventoryPreviewPreparation();
            PickpocketSession::Abort("phase 2 window is unavailable");
            return false;
        }

        const auto snapshot = PickpocketInventory::GetSnapshot();
        const auto usingSkyUIIcons = PickpocketConfig::UsingSkyUIIcons();
        const auto requiresPreparation =
            !usingSkyUIIcons ||
            !PickpocketConfig::settings.showDebugPickpocketListDuringPhase2;
        if (!PickpocketSession::BeginPhase2(
                snapshot.targetHandle.native_handle(),
                requiresPreparation)) {
            return false;
        }

        ResetDialTimer(snapshot.targetFormID, timerSeconds);
        ResetDialInteractionForNextSession();
        StartPreviewPreparation(snapshot, true);
        meshPresentationPending.store(!usingSkyUIIcons, std::memory_order_release);
        pickpocketInventoryWindow->IsOpen = usingSkyUIIcons;
        UpdatePickpocketInventoryPresentation();
        return PickpocketSession::GetSnapshot().state != PickpocketSession::State::Idle;
    }

    namespace PickpocketInventoryWindow {
        void __stdcall Render() {
            const auto screenSize = ImGui::GetIO()->DisplaySize;
            ImGui::SetNextWindowPos({ 0.0f, 0.0f }, ImGuiCond_Always);
            ImGui::SetNextWindowSize(screenSize, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::SetNextWindowFocus();

            constexpr auto windowFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::Begin("##PickpocketMinigameHost", nullptr, windowFlags)) {
                RenderCapturedInventoryMinigame();
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
        }
    }

    namespace Main {
        void __stdcall Render() {
            RenderSettings();
        }
    }

    namespace HudOverlay {
        void __stdcall RenderMenuDimmer() {
            if (!pickpocketInventoryWindow ||
                !pickpocketInventoryWindow->IsOpen ||
                PickpocketConfig::UsingSkyUIIcons()) {
                return;
            }

            const auto alpha = std::clamp(static_cast<int>(pickpocketWindowDimAlpha * 255.0f), 0, 255);
            if (alpha <= 0) {
                return;
            }

            const auto screenSize = ImGui::GetIO()->DisplaySize;
            ImGui::ImDrawListManager::AddRectFilled(
                ImGui::GetBackgroundDrawList(),
                { 0.0f, 0.0f },
                { screenSize.x, screenSize.y },
                IM_COL32(0, 0, 0, alpha),
                0.0f,
                0);
        }

    }
}
