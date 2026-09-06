#pragma once
#include "SKSEMenuFramework.h"
#include "PickpocketRules.h"

namespace UI {
    void Register();
    void PreparePickpocketInventoryWindow();
    void CancelPickpocketInventoryPreparation();
    void UpdatePickpocketInventoryPresentation();
    [[nodiscard]] bool OpenPickpocketInventoryWindow(float timerSeconds = PickpocketRules::kDialTimerDurationSeconds);

    namespace Main {
        void __stdcall Render();
    }

    namespace PickpocketInventoryWindow {
        void __stdcall Render();
    }

    namespace HudOverlay {
        void __stdcall RenderMenuDimmer();
        void __stdcall RenderPickpocketPhase1Counter();
    }
}
