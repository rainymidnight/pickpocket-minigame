#include "PickpocketSession.h"

#include <algorithm>
#include <initializer_list>
#include <mutex>

namespace logger = SKSE::log;

namespace PickpocketSession {
    namespace {
        std::mutex sessionMutex;
        Snapshot session;

        bool IsOneOf(State state, std::initializer_list<State> allowedStates) {
            return std::ranges::find(allowedStates, state) != allowedStates.end();
        }

        bool TargetMatches(RE::RefHandle targetHandle) {
            return targetHandle != 0 && session.targetHandle == targetHandle;
        }

        // Callers must hold sessionMutex.
        void EnterState(State next, std::string_view reason) {
            logger::info(
                "Pickpocket session {} -> {} for target {:08X}: {}",
                StateName(session.state),
                StateName(next),
                session.targetHandle,
                reason);
            session.state = next;
        }

        void EnterIdle(std::string_view reason) {
            EnterState(State::Idle, reason);
            session.targetHandle = 0;
            ++session.generation;
        }

        bool Transition(
            std::initializer_list<State> allowedStates,
            State next,
            RE::RefHandle targetHandle,
            std::string_view reason) {
            std::lock_guard lock(sessionMutex);
            if (!IsOneOf(session.state, allowedStates) || !TargetMatches(targetHandle)) {
                logger::warn(
                    "Rejected pickpocket session transition {} -> {} for target {:08X}: {}",
                    StateName(session.state),
                    StateName(next),
                    targetHandle,
                    reason);
                return false;
            }

            EnterState(next, reason);
            return true;
        }

        bool Finish(std::initializer_list<State> allowedStates, std::string_view reason) {
            std::lock_guard lock(sessionMutex);
            if (!IsOneOf(session.state, allowedStates)) {
                logger::warn(
                    "Rejected completion of pickpocket session in state {}: {}",
                    StateName(session.state),
                    reason);
                return false;
            }

            EnterIdle(reason);
            return true;
        }
    }

    const char* StateName(State state) {
        switch (state) {
        case State::Idle:
            return "Idle";
        case State::Phase1:
            return "Phase1";
        case State::Phase2Preparing:
            return "Phase2Preparing";
        case State::Phase2Active:
            return "Phase2Active";
        case State::ResolvingTransfer:
            return "ResolvingTransfer";
        case State::ResolvingFailure:
            return "ResolvingFailure";
        case State::OpeningDeposit:
            return "OpeningDeposit";
        case State::DepositOpen:
            return "DepositOpen";
        case State::Closing:
            return "Closing";
        }
        return "Unknown";
    }

    Snapshot GetSnapshot() {
        std::lock_guard lock(sessionMutex);
        return session;
    }

    bool IsPhase1Target(RE::RefHandle targetHandle) {
        std::lock_guard lock(sessionMutex);
        return session.state == State::Phase1 && TargetMatches(targetHandle);
    }

    bool CanClaimIconProbe(RE::RefHandle targetHandle) {
        std::lock_guard lock(sessionMutex);
        return
            (session.state == State::Phase1 || session.state == State::Phase2Preparing) &&
            TargetMatches(targetHandle);
    }

    bool IsOpeningDepositFor(RE::RefHandle targetHandle) {
        std::lock_guard lock(sessionMutex);
        return session.state == State::OpeningDeposit && TargetMatches(targetHandle);
    }

    bool IsDepositOpen() {
        std::lock_guard lock(sessionMutex);
        return session.state == State::DepositOpen;
    }

    bool BeginPhase1(RE::RefHandle targetHandle) {
        if (targetHandle == 0) {
            return false;
        }

        std::lock_guard lock(sessionMutex);
        if (session.state != State::Idle) {
            logger::warn(
                "Cannot start pickpocket phase 1 while session is {}",
                StateName(session.state));
            return false;
        }

        session.targetHandle = targetHandle;
        ++session.generation;
        EnterState(State::Phase1, "activate pressed");
        return true;
    }

    bool BeginPhase2(RE::RefHandle targetHandle, bool requiresPreparation) {
        if (targetHandle == 0) {
            return false;
        }

        std::lock_guard lock(sessionMutex);
        if (session.state == State::Idle) {
            session.targetHandle = targetHandle;
            ++session.generation;
        } else if (session.state != State::Phase1 || !TargetMatches(targetHandle)) {
            logger::warn(
                "Cannot start pickpocket phase 2 from {} for target {:08X}",
                StateName(session.state),
                targetHandle);
            return false;
        }

        EnterState(
            requiresPreparation ? State::Phase2Preparing : State::Phase2Active,
            requiresPreparation ? "waiting for presentation assets" : "presentation ready");
        return true;
    }

    bool ActivatePhase2(RE::RefHandle targetHandle) {
        std::lock_guard lock(sessionMutex);
        if (session.state == State::Phase2Active && TargetMatches(targetHandle)) {
            return true;
        }
        if (session.state != State::Phase2Preparing || !TargetMatches(targetHandle)) {
            logger::warn(
                "Cannot activate pickpocket phase 2 from {} for target {:08X}",
                StateName(session.state),
                targetHandle);
            return false;
        }

        EnterState(State::Phase2Active, "presentation assets ready");
        return true;
    }

    bool BeginSafeClose(RE::RefHandle targetHandle, std::string_view reason) {
        return Transition(
            { State::Phase2Preparing, State::Phase2Active },
            State::Closing,
            targetHandle,
            reason);
    }

    bool BeginTransfer(RE::RefHandle targetHandle) {
        return Transition(
            { State::Phase2Active },
            State::ResolvingTransfer,
            targetHandle,
            "committing marked items");
    }

    bool BeginFailure(RE::RefHandle targetHandle, std::string_view reason) {
        return Transition(
            { State::Phase1, State::Phase2Active },
            State::ResolvingFailure,
            targetHandle,
            reason);
    }

    bool BeginDeposit(RE::RefHandle targetHandle) {
        return Transition(
            { State::Phase2Active },
            State::OpeningDeposit,
            targetHandle,
            "opening vanilla deposit menu");
    }

    bool ConfirmDepositOpen(RE::RefHandle targetHandle) {
        return Transition(
            { State::OpeningDeposit },
            State::DepositOpen,
            targetHandle,
            "vanilla deposit menu opened");
    }

    bool FinishTerminal(std::string_view reason) {
        return Finish(
            { State::ResolvingTransfer, State::ResolvingFailure, State::Closing },
            reason);
    }

    bool FinishDeposit() {
        return Finish({ State::DepositOpen }, "vanilla deposit menu closed");
    }

    void Abort(std::string_view reason) {
        std::lock_guard lock(sessionMutex);
        if (session.state != State::Idle) {
            EnterIdle(reason);
        }
    }
}
