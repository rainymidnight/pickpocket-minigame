#pragma once

#include "RE/B/BSPointerHandle.h"
#include "RE/F/FormTypes.h"

#include <cstdint>
#include <string>

namespace PickpocketEvents {
    struct Phase1DebugInfo {
        bool active = false;
        bool transitioning = false;
        float seconds = 0.0f;
        std::int32_t targetDetectionLevel = 0;
        std::int16_t highestDetectionLevel = 0;
        std::uint32_t losCount = 0;
        bool targetDetected = false;
        bool anyDetected = false;
        std::string targetName;
        RE::FormID targetFormID = 0;
        std::uint16_t targetLevel = 0;
        std::uint16_t targetBaseLevel = 0;
        RE::FormID crimeFactionFormID = 0;
        std::uint16_t pickpocketCrimeGold = 0;
        bool targetCanPickpocket = false;
        bool targetIsGuard = false;
        bool targetIsInCombat = false;
        bool targetIsTeammate = false;
        bool targetIsHostileToPlayer = false;
        float playerPickpocket = 0.0f;
        float playerSneak = 0.0f;
        float playerPickpocketModifier = 0.0f;
        float playerPickpocketPowerModifier = 0.0f;
        float targetPickpocket = 0.0f;
        float targetSneak = 0.0f;
        float targetAggression = 0.0f;
        float targetConfidence = 0.0f;
        float targetMorality = 0.0f;
        bool baselineChanceAvailable = false;
        std::int32_t baselineHiddenChance = 0;
        std::int32_t baselineDetectedChance = 0;
        float elapsedSeconds = 0.0f;
        float accessChance = 0.0f;
        float access = 0.0f;
        float maxBankableSeconds = 0.0f;
        float fillRate = 0.0f;
        float riskRate = 0.0f;
        float risk = 0.0f;
        std::uint32_t accessItemCount = 0;
    };

    void Register();
    bool OpenVanillaPickpocketMenu(RE::ObjectRefHandle targetHandle);
    void CancelVanillaPickpocketMenuRequest();
    void SuppressPhase1ActivateUntilRelease();
    void EndPhase1TransitionPresentation();
    bool IsPhase1Suspended();
    void UpdatePhase1(float deltaSeconds);
    bool GetPhase1DebugInfo(Phase1DebugInfo& infoOut);
}
