#pragma once

#include "RE/B/BSPointerHandle.h"

#include <cstdint>
#include <string_view>

namespace PickpocketSession {
    enum class State : std::uint8_t {
        Idle,
        Phase1,
        Phase2Preparing,
        Phase2Active,
        ResolvingTransfer,
        ResolvingFailure,
        OpeningDeposit,
        DepositOpen,
        Closing
    };

    struct Snapshot {
        State state = State::Idle;
        RE::RefHandle targetHandle = 0;
        std::uint64_t generation = 0;
    };

    [[nodiscard]] Snapshot GetSnapshot();
    [[nodiscard]] bool IsPhase1Target(RE::RefHandle targetHandle);
    [[nodiscard]] bool CanClaimIconProbe(RE::RefHandle targetHandle);
    [[nodiscard]] bool IsOpeningDepositFor(RE::RefHandle targetHandle);
    [[nodiscard]] bool IsDepositOpen();

    [[nodiscard]] bool BeginPhase1(RE::RefHandle targetHandle);
    [[nodiscard]] bool BeginPhase2(RE::RefHandle targetHandle, bool requiresPreparation);
    [[nodiscard]] bool ActivatePhase2(RE::RefHandle targetHandle);
    [[nodiscard]] bool BeginSafeClose(RE::RefHandle targetHandle, std::string_view reason);
    [[nodiscard]] bool BeginTransfer(RE::RefHandle targetHandle);
    [[nodiscard]] bool BeginFailure(RE::RefHandle targetHandle, std::string_view reason);
    [[nodiscard]] bool BeginDeposit(RE::RefHandle targetHandle);
    [[nodiscard]] bool ConfirmDepositOpen(RE::RefHandle targetHandle);

    [[nodiscard]] bool FinishTerminal(std::string_view reason);
    [[nodiscard]] bool FinishDeposit();
    void Abort(std::string_view reason);

    [[nodiscard]] const char* StateName(State state);
}
