#pragma once

#include <cstdint>
#include <optional>

namespace RE {
    class Actor;
}

namespace PickpocketHooks {
    [[nodiscard]] bool Install();

    struct ForcedChanceContext {
        std::int32_t chance = 0;
        RE::Actor* thief = nullptr;
        RE::Actor* target = nullptr;
        bool applied = false;
    };

    class ScopedForcePickpocketChance {
    public:
        ScopedForcePickpocketChance(const ScopedForcePickpocketChance&) = delete;
        ScopedForcePickpocketChance& operator=(const ScopedForcePickpocketChance&) = delete;

    protected:
        ScopedForcePickpocketChance(std::int32_t forcedChance, RE::Actor& thief, RE::Actor& target);
        ~ScopedForcePickpocketChance();

    private:
        std::optional<ForcedChanceContext> previousContext;
    };

    class ScopedForcePickpocketFailure final : private ScopedForcePickpocketChance {
    public:
        ScopedForcePickpocketFailure(RE::Actor& thief, RE::Actor& target);
        ~ScopedForcePickpocketFailure() = default;

        ScopedForcePickpocketFailure(const ScopedForcePickpocketFailure&) = delete;
        ScopedForcePickpocketFailure& operator=(const ScopedForcePickpocketFailure&) = delete;
    };

    class ScopedForcePickpocketSuccess final : private ScopedForcePickpocketChance {
    public:
        ScopedForcePickpocketSuccess(RE::Actor& thief, RE::Actor& target);
        ~ScopedForcePickpocketSuccess() = default;

        ScopedForcePickpocketSuccess(const ScopedForcePickpocketSuccess&) = delete;
        ScopedForcePickpocketSuccess& operator=(const ScopedForcePickpocketSuccess&) = delete;
    };
}
