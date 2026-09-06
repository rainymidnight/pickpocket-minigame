#pragma once

#include "PickpocketEventHandler.h"

#include <cstdint>

namespace PickpocketEvents::Internal {
    bool __stdcall ProcessPhase1Input(RE::InputEvent* inputEvent);

    void ForceHideContainerMenu();
    bool ConsumeVanillaPickpocketMenuBypass(RE::RefHandle targetHandle);
    void RestoreVanillaDepositLosses();
    void ScheduleDepositMenuConfiguration();
}
