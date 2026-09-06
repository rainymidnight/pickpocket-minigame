#include "PickpocketEventHandler.h"
#include "PickpocketEventHandlerInternal.h"
#include "Config.h"
#include "PickpocketInventory.h"
#include "PickpocketRules.h"
#include "PickpocketOrdinatorCompat.h"
#include "PickpocketSession.h"
#include "SKSEMenuFramework.h"
#include "UI.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace logger = SKSE::log;

namespace PickpocketEvents {
    namespace {
        constexpr RE::FormID kBaselineChanceProbeFormID = 0x00012EB7;
        constexpr float kPhase1AccessChanceFloor = 5.0f;
        constexpr float kPhase1AccessChanceCeiling = 90.0f;
        constexpr float kPhase1MinBankableSeconds = 3.0f;
        constexpr float kPhase1ContactGraceSeconds = 0.3f;
        constexpr float kPhase1BankableSecondsExponent = 0.9f;
        constexpr float kPhase1MinFillRate = 0.55f;
        constexpr float kPhase1MaxFillRate = 3.5f;
        constexpr float kPhase1LowAccessRiskRate = 0.30f;
        constexpr float kPhase1HighAccessRiskRate = 0.08f;
        constexpr float kPhase1RiskRateExponent = 5.0f;

        struct Phase1Difficulty {
            float accessChance = kPhase1AccessChanceCeiling;
            float access = 1.0f;
            float maxBankableSeconds = PickpocketRules::kDialTimerDurationSeconds;
            float fillRate = kPhase1MaxFillRate;
            float riskRate = 0.0f;
            std::uint32_t itemCount = 0;
        };

        std::mutex phase1Mutex;
        std::uint64_t phase1Generation = 0;
        float phase1Seconds = 0.0f;
        float phase1ElapsedSeconds = 0.0f;
        float phase1Risk = 0.0f;
        float phase1ContactLostSeconds = 0.0f;
        Phase1Difficulty phase1Difficulty;
        Phase1DebugInfo phase1DebugInfo;
        std::atomic_bool phase1ActivateInputDown = false;
        std::atomic_bool suppressPhase1ActivateUntilRelease = false;

        RE::ObjectRefHandle GetActiveTargetActorHandle(const RE::CrosshairPickData& pickData) {
            const auto& firstTargetActor =
                REL::RelocateMember<RE::ObjectRefHandle>(std::addressof(pickData), 0x8, 0x10);
            if (REL::Module::IsVR()) {
                const auto* targetActors = std::addressof(firstTargetActor);
                if (targetActors[RE::VR_DEVICE::kLeftController]) {
                    return targetActors[RE::VR_DEVICE::kLeftController];
                }
                if (targetActors[RE::VR_DEVICE::kRightController]) {
                    return targetActors[RE::VR_DEVICE::kRightController];
                }
                if (targetActors[RE::VR_DEVICE::kHeadset]) {
                    return targetActors[RE::VR_DEVICE::kHeadset];
                }
                return {};
            }
            return firstTargetActor;
        }

        bool IsValidPhase1Target(RE::Actor* target, RE::PlayerCharacter* player) {
            return
                player && player->IsSneaking() && target && target != player &&
                !target->IsDead() && !target->IsDisabled() && !target->IsActivationBlocked() &&
                target->CanPickpocket();
        }

        RE::Actor* GetCurrentPhase1Target() {
            const auto* pickData = RE::CrosshairPickData::GetSingleton();
            if (!pickData) {
                return nullptr;
            }
            const auto targetRef = GetActiveTargetActorHandle(*pickData).get();
            auto* targetActor = targetRef ? targetRef->As<RE::Actor>() : nullptr;
            return IsValidPhase1Target(targetActor, RE::PlayerCharacter::GetSingleton()) ? targetActor : nullptr;
        }

        bool IsSamePhase1Target(RE::Actor& target, RE::RefHandle expectedHandle) {
            return target.GetHandle().native_handle() == expectedHandle;
        }

        float GetActorValue(RE::Actor& actor, RE::ActorValue actorValue) {
            if (auto* actorValueOwner = actor.AsActorValueOwner()) {
                return actorValueOwner->GetActorValue(actorValue);
            }

            return 0.0f;
        }

        std::uint16_t GetActorBaseLevel(RE::Actor& actor) {
            if (auto* actorBase = actor.GetActorBase()) {
                return actorBase->GetLevel();
            }

            return 0;
        }

        RE::TESForm* GetBaselineChanceProbeForm() {
            static auto* probeForm = RE::TESForm::LookupByID(kBaselineChanceProbeFormID);
            return probeForm;
        }

        float Lerp(float minValue, float maxValue, float t) {
            return minValue + (maxValue - minValue) * std::clamp(t, 0.0f, 1.0f);
        }

        float SmoothStep(float edge0, float edge1, float value) {
            if (edge0 == edge1) {
                return value >= edge1 ? 1.0f : 0.0f;
            }

            const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        std::uint32_t ClampToUInt32(std::int64_t value) {
            if (value <= 0) {
                return 0;
            }

            constexpr auto maxUInt32 = static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max());
            return static_cast<std::uint32_t>(std::min(value, maxUInt32));
        }

        std::uint32_t GetItemDifficultyValue(const PickpocketInventory::Item& item) {
            const auto count = static_cast<std::int64_t>(std::max(item.count, 1));
            const auto baseValue = static_cast<std::int64_t>(std::max(item.value, 0)) * count;
            return std::max(item.stealValue, ClampToUInt32(baseValue));
        }

        float GetItemDifficultyWeight(const PickpocketInventory::Item& item) {
            const auto value = static_cast<float>(std::max<std::uint32_t>(GetItemDifficultyValue(item), 10));
            return std::pow(value, 0.75f);
        }

        float GetItemTotalWeight(const PickpocketInventory::Item& item) {
            return std::max(item.weight, 0.0f) * static_cast<float>(std::max(item.count, 1));
        }

        std::vector<const PickpocketInventory::Item*> GetPhase1DifficultyItems(const PickpocketInventory::Snapshot& snapshot) {
            return PickpocketInventory::SelectTopItems(
                snapshot,
                PickpocketRules::kDialItemSlotCount,
                [](const PickpocketInventory::Item& item) { return item.minigameEligible && item.object; });
        }

        Phase1Difficulty BuildPhase1Difficulty(RE::Actor& target, const PickpocketInventory::Snapshot& snapshot) {
            Phase1Difficulty difficulty;

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return difficulty;
            }

            const auto candidates = GetPhase1DifficultyItems(snapshot);
            difficulty.itemCount = static_cast<std::uint32_t>(candidates.size());
            if (candidates.empty()) {
                return difficulty;
            }

            const auto playerPickpocket = GetActorValue(*player, RE::ActorValue::kPickpocket);
            const auto targetPickpocket = GetActorValue(target, RE::ActorValue::kPickpocket);
            float weightedChance = 0.0f;
            float totalWeight = 0.0f;

            for (const auto* item : candidates) {
                const auto itemValue = GetItemDifficultyValue(*item);
                const auto itemWeight = GetItemTotalWeight(*item);
                const auto chance = std::clamp(
                    static_cast<float>(RE::AIFormulas::ComputePickpocketSuccess(
                        playerPickpocket,
                        targetPickpocket,
                        itemValue,
                        itemWeight,
                        player,
                        &target,
                        false,
                        item->object)),
                    0.0f,
                    100.0f);
                const auto weight = GetItemDifficultyWeight(*item);
                weightedChance += chance * weight;
                totalWeight += weight;
            }

            if (totalWeight <= 0.0f) {
                return difficulty;
            }

            difficulty.accessChance = weightedChance / totalWeight;
            difficulty.access = SmoothStep(kPhase1AccessChanceFloor, kPhase1AccessChanceCeiling, difficulty.accessChance);
            difficulty.maxBankableSeconds =
                Lerp(kPhase1MinBankableSeconds, PickpocketRules::kDialTimerDurationSeconds, std::pow(difficulty.access, kPhase1BankableSecondsExponent));
            difficulty.fillRate = Lerp(kPhase1MinFillRate, kPhase1MaxFillRate, difficulty.access);
            difficulty.riskRate =
                Lerp(kPhase1LowAccessRiskRate, kPhase1HighAccessRiskRate, std::pow(difficulty.access, kPhase1RiskRateExponent)) *
                PickpocketConfig::GetPhase1RiskMultiplier();
            return difficulty;
        }

        void ApplyPhase1DebugProgress(
            Phase1DebugInfo& info,
            const Phase1Difficulty& difficulty,
            float elapsedSeconds,
            float bankedSeconds,
            float risk) {
            info.elapsedSeconds = elapsedSeconds;
            info.seconds = bankedSeconds;
            info.accessChance = difficulty.accessChance;
            info.access = difficulty.access;
            info.maxBankableSeconds = difficulty.maxBankableSeconds;
            info.fillRate = difficulty.fillRate;
            info.riskRate = difficulty.riskRate;
            info.risk = risk;
            info.accessItemCount = difficulty.itemCount;
        }

        Phase1DebugInfo BuildPhase1DebugInfo(RE::Actor& target, std::int32_t targetDetectionLevel) {
            Phase1DebugInfo info;
            info.targetDetectionLevel = targetDetectionLevel;
            info.targetDetected = targetDetectionLevel > 0;
            info.targetFormID = target.GetFormID();
            info.targetLevel = target.GetLevel();
            info.targetBaseLevel = GetActorBaseLevel(target);
            info.targetCanPickpocket = target.CanPickpocket();
            info.targetIsGuard = target.IsGuard();
            info.targetIsInCombat = target.IsInCombat();
            info.targetIsTeammate = target.IsPlayerTeammate();
            info.targetPickpocket = GetActorValue(target, RE::ActorValue::kPickpocket);
            info.targetSneak = GetActorValue(target, RE::ActorValue::kSneak);
            info.targetAggression = GetActorValue(target, RE::ActorValue::kAggression);
            info.targetConfidence = GetActorValue(target, RE::ActorValue::kConfidence);
            info.targetMorality = GetActorValue(target, RE::ActorValue::kMorality);

            if (const auto* targetName = target.GetDisplayFullName(); targetName && targetName[0] != '\0') {
                info.targetName = targetName;
            } else {
                info.targetName = std::format("Actor {:08X}", info.targetFormID);
            }

            if (auto* crimeFaction = target.GetCrimeFaction()) {
                info.crimeFactionFormID = crimeFaction->GetFormID();
                info.pickpocketCrimeGold = crimeFaction->crimeData.crimevalues.pickpocketCrimeGold;
            }

            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                if (auto* processLists = RE::ProcessLists::GetSingleton()) {
                    info.highestDetectionLevel = processLists->RequestHighestDetectionLevelAgainstActor(player, info.losCount);
                    info.anyDetected = info.highestDetectionLevel > 0;
                }
                info.targetIsHostileToPlayer = target.IsHostileToActor(player);
                info.playerPickpocket = GetActorValue(*player, RE::ActorValue::kPickpocket);
                info.playerSneak = GetActorValue(*player, RE::ActorValue::kSneak);
                info.playerPickpocketModifier = GetActorValue(*player, RE::ActorValue::kPickpocketModifier);
                info.playerPickpocketPowerModifier = GetActorValue(*player, RE::ActorValue::kPickpocketPowerModifier);
                if (auto* probeForm = GetBaselineChanceProbeForm()) {
                    info.baselineChanceAvailable = true;
                    info.baselineHiddenChance = RE::AIFormulas::ComputePickpocketSuccess(
                        info.playerPickpocket,
                        info.targetPickpocket,
                        0,
                        0.0f,
                        player,
                        &target,
                        false,
                        probeForm);
                    info.baselineDetectedChance = RE::AIFormulas::ComputePickpocketSuccess(
                        info.playerPickpocket,
                        info.targetPickpocket,
                        0,
                        0.0f,
                        player,
                        &target,
                        true,
                        probeForm);
                }
            }

            return info;
        }

        void FailPhase1WithVanillaPickpocketAlarm(
            RE::Actor& target,
            std::string_view reason,
            std::int32_t targetDetectionLevel);

        void ClearPhase1State(bool preservePreparation = false) {
            {
                std::lock_guard lock(phase1Mutex);
                phase1Generation = 0;
                phase1Seconds = 0.0f;
                phase1ElapsedSeconds = 0.0f;
                phase1Risk = 0.0f;
                phase1ContactLostSeconds = 0.0f;
                phase1Difficulty = {};
                phase1DebugInfo = {};
            }
            if (!preservePreparation) {
                UI::CancelPickpocketInventoryPreparation();
            }
        }

        bool StartPhase1(RE::Actor& target) {
            const auto targetHandle = target.GetHandle().native_handle();
            if (PickpocketSession::IsPhase1Target(targetHandle)) {
                return true;
            }
            if (!PickpocketSession::BeginPhase1(targetHandle)) {
                return false;
            }

            PickpocketOrdinatorCompat::NotifyActivation(target.GetHandle());
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                const auto detectionLevel = target.RequestDetectionLevel(player, RE::DETECTION_PRIORITY::kNormal);
                if (detectionLevel > 0) {
                    FailPhase1WithVanillaPickpocketAlarm(target, "target detected player", detectionLevel);
                    return true;
                }
            }
            PickpocketInventory::CaptureFromActor(target);
            const auto difficulty = BuildPhase1Difficulty(target, PickpocketInventory::GetSnapshot());
            const auto session = PickpocketSession::GetSnapshot();

            {
                std::lock_guard lock(phase1Mutex);
                phase1Generation = session.generation;
                phase1Seconds = 0.0f;
                phase1ElapsedSeconds = 0.0f;
                phase1Risk = 0.0f;
                phase1ContactLostSeconds = 0.0f;
                phase1Difficulty = difficulty;
                phase1DebugInfo = {};
            }
            UI::PreparePickpocketInventoryWindow();
            return true;
        }

        bool IsActivateButton(const RE::ButtonEvent& button) {
            const auto* userEvents = RE::UserEvents::GetSingleton();
            if (!userEvents) {
                return false;
            }
            if (button.QUserEvent() == userEvents->activate) {
                return true;
            }

            const auto* controlMap = RE::ControlMap::GetSingleton();
            if (!controlMap) {
                return false;
            }

            const auto mappedKey = controlMap->GetMappedKey(userEvents->activate.c_str(), button.GetDevice());
            return mappedKey != RE::ControlMap::kInvalid && button.GetIDCode() == mappedKey;
        }

        bool IsMappedActivateInputPressed() {
            const auto* userEvents = RE::UserEvents::GetSingleton();
            const auto* controlMap = RE::ControlMap::GetSingleton();
            auto* inputDeviceManager = RE::BSInputDeviceManager::GetSingleton();
            if (!userEvents || !controlMap || !inputDeviceManager) {
                return false;
            }

            const auto isPressed = [&](RE::INPUT_DEVICE device, const RE::BSInputDevice* inputDevice) {
                if (!inputDevice || !inputDevice->IsEnabled()) {
                    return false;
                }

                const auto mappedKey = controlMap->GetMappedKey(userEvents->activate.c_str(), device);
                return mappedKey != RE::ControlMap::kInvalid && inputDevice->IsPressed(mappedKey);
            };

            return
                isPressed(RE::INPUT_DEVICE::kKeyboard, inputDeviceManager->GetKeyboard()) ||
                isPressed(RE::INPUT_DEVICE::kMouse, inputDeviceManager->GetMouse()) ||
                isPressed(RE::INPUT_DEVICE::kGamepad, inputDeviceManager->GetGamepad());
        }

        bool CanStartPhase1FromGameplay() {
            const auto* controlMap = RE::ControlMap::GetSingleton();
            return controlMap &&
                !IsPhase1Suspended() &&
                controlMap->IsActivateControlsEnabled();
        }

        bool IsPhase1Active() {
            const auto session = PickpocketSession::GetSnapshot();
            std::lock_guard lock(phase1Mutex);
            return
                session.state == PickpocketSession::State::Phase1 &&
                phase1Generation == session.generation;
        }

        void FailPhase1WithVanillaPickpocketAlarm(RE::Actor& target, std::string_view reason, std::int32_t targetDetectionLevel) {
            const auto targetName = target.GetDisplayFullName();
            const auto targetFormID = target.GetFormID();
            const auto targetHandle = target.GetHandle().native_handle();
            if (!PickpocketSession::BeginFailure(targetHandle, reason)) {
                return;
            }
            ClearPhase1State();

            PickpocketInventory::CaptureFromActor(target);
            const auto result = PickpocketInventory::TriggerPickpocketFailure();
            if (!result.succeeded) {
                logger::warn(
                    "Pickpocket phase 1 failed because {}, but vanilla alarm could not be triggered: {} ({:08X}), detection level {}; {}",
                    reason,
                    targetName,
                    targetFormID,
                    targetDetectionLevel,
                    result.message);
            }
            (void)PickpocketSession::FinishTerminal("phase 1 failure resolved");
        }

        bool CompletePhase1AndOpenPickpocketMenu() {
            const auto session = PickpocketSession::GetSnapshot();
            if (session.state != PickpocketSession::State::Phase1) {
                return false;
            }

            float elapsed = 0.0f;
            {
                std::lock_guard lock(phase1Mutex);
                if (phase1Generation != session.generation) {
                    return false;
                }
                elapsed = phase1Seconds;
            }

            auto* target = GetCurrentPhase1Target();
            if (!target || !IsSamePhase1Target(*target, session.targetHandle)) {
                ClearPhase1State();
                PickpocketSession::Abort("phase 1 target became invalid before release");
                return true;
            }

            const auto timerSeconds = std::clamp(elapsed, 0.0f, PickpocketRules::kDialTimerDurationSeconds);
            if (!UI::OpenPickpocketInventoryWindow(timerSeconds)) {
                ClearPhase1State();
            }
            return true;
        }

    }

    bool IsPhase1Suspended() {
        auto* ui = RE::UI::GetSingleton();
        return
            !ui ||
            ui->GameIsPaused() ||
            ui->IsApplicationMenuOpen() ||
            SKSEMenuFramework::IsAnyBlockingWindowOpened();
    }

    namespace Internal {
        bool __stdcall ProcessPhase1Input(RE::InputEvent* inputEvent) {
            const auto* button = inputEvent ? inputEvent->AsButtonEvent() : nullptr;
            if (PickpocketConfig::settings.disableMinigame) {
                return false;
            }

            if (!button || !IsActivateButton(*button)) {
                return false;
            }

            phase1ActivateInputDown.store(button->IsPressed());

            if (suppressPhase1ActivateUntilRelease.load()) {
                if (!button->IsPressed()) {
                    suppressPhase1ActivateUntilRelease.store(false);
                }
                return true;
            }

            if (button->IsDown()) {
                if (IsPhase1Active()) {
                    return true;
                }
                if (!CanStartPhase1FromGameplay()) {
                    return false;
                }
                if (auto* target = GetCurrentPhase1Target()) {
                    return StartPhase1(*target);
                }
                return false;
            }

            if (button->IsUp()) {
                if (IsPhase1Active() && IsPhase1Suspended()) {
                    ClearPhase1State();
                    PickpocketSession::Abort("activate released while phase 1 was suspended by a menu");
                    return true;
                }
                return CompletePhase1AndOpenPickpocketMenu();
            }

            if (button->IsHeld()) {
                return IsPhase1Active();
            }

            return false;
        }
    }

    void SuppressPhase1ActivateUntilRelease() {
        const auto eventStatePressed = phase1ActivateInputDown.load();
        const auto deviceStatePressed = IsMappedActivateInputPressed();
        if (eventStatePressed || deviceStatePressed) {
            suppressPhase1ActivateUntilRelease.store(true);
        }
        logger::debug(
            "Phase 1 Activate release gate {} (event state {}, device state {})",
            eventStatePressed || deviceStatePressed ? "armed" : "not armed",
            eventStatePressed,
            deviceStatePressed);
    }

    void EndPhase1TransitionPresentation() {
        ClearPhase1State(true);
    }

    void UpdatePhase1(float deltaSeconds) {
        const auto session = PickpocketSession::GetSnapshot();
        if (session.state != PickpocketSession::State::Phase1) {
            return;
        }
        if (IsPhase1Suspended()) {
            return;
        }

        float currentSeconds = 0.0f;
        float currentElapsedSeconds = 0.0f;
        float currentRisk = 0.0f;
        float currentContactLostSeconds = 0.0f;
        Phase1Difficulty difficulty;
        {
            std::lock_guard lock(phase1Mutex);
            if (phase1Generation != session.generation) {
                return;
            }
            currentSeconds = phase1Seconds;
            currentElapsedSeconds = phase1ElapsedSeconds;
            currentRisk = phase1Risk;
            currentContactLostSeconds = phase1ContactLostSeconds;
            difficulty = phase1Difficulty;
        }

        const auto delta = std::max(deltaSeconds, 0.0f);
        RE::NiPointer<RE::TESObjectREFR> targetRef;
        RE::TESObjectREFR::LookupByHandle(session.targetHandle, targetRef);
        auto* target = targetRef ? targetRef->As<RE::Actor>() : nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!IsValidPhase1Target(target, player)) {
            ClearPhase1State();
            PickpocketSession::Abort("phase 1 target became invalid");
            return;
        }

        const auto detectionLevel = target->RequestDetectionLevel(player, RE::DETECTION_PRIORITY::kNormal);
        if (detectionLevel > 0) {
            FailPhase1WithVanillaPickpocketAlarm(*target, "target detected player", detectionLevel);
            return;
        }

        auto* currentTarget = GetCurrentPhase1Target();
        if (!currentTarget || !IsSamePhase1Target(*currentTarget, session.targetHandle)) {
            const auto nextContactLostSeconds = currentContactLostSeconds + delta;
            if (nextContactLostSeconds >= kPhase1ContactGraceSeconds) {
                ClearPhase1State();
                PickpocketSession::Abort("phase 1 target contact lost");
            } else {
                std::lock_guard lock(phase1Mutex);
                if (phase1Generation == session.generation) {
                    phase1ContactLostSeconds = nextContactLostSeconds;
                }
            }
            return;
        }

        const auto maxBankableSeconds = std::max(difficulty.maxBankableSeconds, 0.1f);
        const auto bankedRatio = std::clamp(currentSeconds / maxBankableSeconds, 0.0f, 1.0f);
        const auto riskPressure = 1.0f + std::pow(bankedRatio, 1.8f);
        const auto nextElapsedSeconds = currentElapsedSeconds + delta;
        const auto nextSeconds = std::clamp(currentSeconds + delta * difficulty.fillRate, 0.0f, maxBankableSeconds);
        const auto nextRisk = std::clamp(currentRisk + delta * difficulty.riskRate * riskPressure, 0.0f, 1.0f);

        if (nextRisk >= 1.0f) {
            FailPhase1WithVanillaPickpocketAlarm(*target, "phase-1 risk reached the limit", detectionLevel);
            return;
        }

        if (!PickpocketSession::IsPhase1Target(session.targetHandle)) {
            return;
        }

        auto debugInfo = PickpocketConfig::settings.showPhase1DebugOverlay ?
            BuildPhase1DebugInfo(*target, detectionLevel) :
            Phase1DebugInfo{};

        std::lock_guard lock(phase1Mutex);
        if (phase1Generation == session.generation) {
            phase1ElapsedSeconds = nextElapsedSeconds;
            phase1Seconds = nextSeconds;
            phase1Risk = nextRisk;
            phase1ContactLostSeconds = 0.0f;
            phase1DebugInfo = std::move(debugInfo);
        }
    }

    bool GetPhase1DebugInfo(Phase1DebugInfo& infoOut) {
        const auto session = PickpocketSession::GetSnapshot();
        std::lock_guard lock(phase1Mutex);
        infoOut = phase1DebugInfo;
        infoOut.transitioning = session.state == PickpocketSession::State::Phase2Preparing;
        infoOut.active =
            (session.state == PickpocketSession::State::Phase1 || infoOut.transitioning) &&
            phase1Generation == session.generation;
        ApplyPhase1DebugProgress(infoOut, phase1Difficulty, phase1ElapsedSeconds, phase1Seconds, phase1Risk);
        return infoOut.active;
    }
}
