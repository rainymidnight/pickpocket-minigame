#include "PickpocketHooks.h"

#include <Windows.h>

#include <array>
#include <cstring>

namespace logger = SKSE::log;

namespace {
    using ComputePickpocketSuccess = decltype(&RE::AIFormulas::ComputePickpocketSuccess);

    constexpr std::size_t kRelativeCallSize = 5;
    constexpr std::size_t kAttemptPickpocketScanBytes = 0x400;
    constexpr std::uint8_t kRelativeCallOpcode = 0xE8;

    thread_local std::optional<PickpocketHooks::ForcedChanceContext> forcedChanceContext;
    ComputePickpocketSuccess originalComputePickpocketSuccess = nullptr;

    std::uintptr_t ResolveRelativeCallTarget(std::uintptr_t callAddress) {
        std::int32_t displacement = 0;
        std::memcpy(
            &displacement,
            reinterpret_cast<const void*>(callAddress + 1),
            sizeof(displacement));
        return callAddress + kRelativeCallSize + displacement;
    }

    std::uintptr_t FindPickpocketChanceCallsite(
        std::uintptr_t attemptPickpocketAddress,
        std::uintptr_t computePickpocketSuccessAddress) {
        std::uintptr_t match = 0;
        std::size_t matchCount = 0;

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(attemptPickpocketAddress);
        for (std::size_t offset = 0; offset + kRelativeCallSize <= kAttemptPickpocketScanBytes; ++offset) {
            if (bytes[offset] != kRelativeCallOpcode) {
                continue;
            }

            const auto candidate = attemptPickpocketAddress + offset;
            if (ResolveRelativeCallTarget(candidate) != computePickpocketSuccessAddress) {
                continue;
            }

            match = candidate;
            ++matchCount;
        }

        if (matchCount != 1) {
            logger::critical(
                "Expected exactly one ComputePickpocketSuccess call in PlayerCharacter::AttemptPickpocket, found {}",
                matchCount);
            return 0;
        }

        return match;
    }

    std::int32_t ComputePickpocketSuccessHook(
        float thiefSkill,
        float targetSkill,
        std::uint32_t valueStolen,
        float weightStolen,
        RE::Actor* thief,
        RE::Actor* target,
        bool isDetected,
        RE::TESForm* itemPickpocketing) {
        if (forcedChanceContext &&
            forcedChanceContext->thief == thief &&
            forcedChanceContext->target == target) {
            forcedChanceContext->applied = true;
            logger::debug(
                "Forcing pickpocket chance to {} for target {:08X}",
                forcedChanceContext->chance,
                target ? target->GetFormID() : 0);
            return forcedChanceContext->chance;
        }

        return originalComputePickpocketSuccess(
            thiefSkill,
            targetSkill,
            valueStolen,
            weightStolen,
            thief,
            target,
            isDetected,
            itemPickpocketing);
    }
}

namespace PickpocketHooks {
    bool Install() {
        if (originalComputePickpocketSuccess) {
            return true;
        }

        const REL::Relocation<std::uintptr_t> attemptPickpocket{ RELOCATION_ID(39568, 40654) };
        const REL::Relocation<std::uintptr_t> computePickpocketSuccess{ RELOCATION_ID(25822, 26379) };
        const auto callsite = FindPickpocketChanceCallsite(
            attemptPickpocket.address(),
            computePickpocketSuccess.address());
        if (!callsite || *reinterpret_cast<const std::uint8_t*>(callsite) != kRelativeCallOpcode) {
            logger::critical("Could not validate the PlayerCharacter::AttemptPickpocket chance callsite");
            return false;
        }

        auto& trampoline = SKSE::GetTrampoline();
        std::array<std::uint8_t, kRelativeCallSize> originalCallBytes{};
        std::memcpy(
            originalCallBytes.data(),
            reinterpret_cast<const void*>(callsite),
            originalCallBytes.size());
        originalComputePickpocketSuccess =
            reinterpret_cast<ComputePickpocketSuccess>(computePickpocketSuccess.address());
        const auto originalAddress =
            trampoline.write_call<kRelativeCallSize>(callsite, ComputePickpocketSuccessHook);
        if (originalAddress != computePickpocketSuccess.address()) {
            REL::safe_write(callsite, originalCallBytes.data(), originalCallBytes.size());
            ::FlushInstructionCache(
                ::GetCurrentProcess(),
                reinterpret_cast<const void*>(callsite),
                originalCallBytes.size());
            originalComputePickpocketSuccess = nullptr;
            logger::critical(
                "AttemptPickpocket chance call target changed during hook installation: expected {:X}, found {:X}",
                computePickpocketSuccess.address(),
                originalAddress);
            return false;
        }
        ::FlushInstructionCache(
            ::GetCurrentProcess(),
            reinterpret_cast<const void*>(callsite),
            kRelativeCallSize);

        logger::info(
            "Installed scoped pickpocket chance hook at AttemptPickpocket+0x{:X}",
            callsite - attemptPickpocket.address());
        return true;
    }

    ScopedForcePickpocketChance::ScopedForcePickpocketChance(
        std::int32_t forcedChance,
        RE::Actor& thief,
        RE::Actor& target) :
        previousContext(forcedChanceContext) {
        forcedChanceContext = ForcedChanceContext{
            .chance = forcedChance,
            .thief = &thief,
            .target = &target
        };
    }

    ScopedForcePickpocketChance::~ScopedForcePickpocketChance() {
        if (forcedChanceContext && !forcedChanceContext->applied) {
            logger::error(
                "Scoped pickpocket chance {} was not consumed for target {:08X}",
                forcedChanceContext->chance,
                forcedChanceContext->target ? forcedChanceContext->target->GetFormID() : 0);
        }
        forcedChanceContext = previousContext;
    }

    ScopedForcePickpocketFailure::ScopedForcePickpocketFailure(
        RE::Actor& thief,
        RE::Actor& target) :
        ScopedForcePickpocketChance(0, thief, target) {}

    ScopedForcePickpocketSuccess::ScopedForcePickpocketSuccess(
        RE::Actor& thief,
        RE::Actor& target) :
        ScopedForcePickpocketChance(100, thief, target) {}
}
