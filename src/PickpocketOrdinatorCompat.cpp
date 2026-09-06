#include "PickpocketOrdinatorCompat.h"

namespace PickpocketOrdinatorCompat {
    namespace {
        constexpr std::string_view kOrdinatorPlugin = "Ordinator - Perks of Skyrim.esp";
        constexpr RE::FormID kThiefsEyeQuestFormID = 0x03279B;

        RE::TESQuest* LookupThiefsEyeQuest() {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                return nullptr;
            }

            return dataHandler->LookupForm<RE::TESQuest>(kThiefsEyeQuestFormID, kOrdinatorPlugin);
        }

        bool IsThiefsEyeTarget(const RE::Actor& target, const RE::TESQuest& thiefsEyeQuest) {
            if (!thiefsEyeQuest.IsRunning()) {
                return false;
            }

            for (auto* alias : thiefsEyeQuest.aliases) {
                auto* refAlias = skyrim_cast<RE::BGSRefAlias*>(alias);
                if (!refAlias) {
                    continue;
                }

                if (refAlias->GetActorReference() == &target) {
                    return true;
                }
            }

            return false;
        }
    }

    void NotifyActivation(RE::ObjectRefHandle targetHandle) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto targetRef = targetHandle.get();
        auto* target = targetRef ? targetRef->As<RE::Actor>() : nullptr;
        auto* thiefsEyeQuest = LookupThiefsEyeQuest();
        if (!player || !target || !thiefsEyeQuest || !IsThiefsEyeTarget(*target, *thiefsEyeQuest)) {
            return;
        }

        RE::NiPointer<RE::TESObjectREFR> actionRef(player);
        auto* eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!eventSourceHolder || !actionRef) {
            return;
        }

        eventSourceHolder->SendActivateEvent(targetRef, actionRef);
    }
}
