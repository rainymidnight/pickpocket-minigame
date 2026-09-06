#include "PickpocketRevealRules.h"

#include <array>
#include <string_view>

namespace PickpocketRevealRules {
    namespace {
        struct RevealRule {
            std::string_view pluginName;
            RE::FormID perkFormID;
            std::string_view perkEditorID;
            RevealCategories categories;
        };

        constexpr std::array kRevealRules{
            RevealRule{
                "",
                0,
                "Cutpurse",
                { false, false, true, false, false },
            },
            RevealRule{
                "",
                0,
                "KeyMaster",
                { false, true, false, false, false },
            },
            RevealRule{
                "Vokrii - Minimalistic Perks of Skyrim.esp",
                0x00058204,
                "",
                { false, true, true, true, true },
            },
            RevealRule{
                "Vokrii - Minimalistic Perks of Skyrim.esp",
                0x000D79A0,
                "",
                { true },
            },
            RevealRule{
                "Ordinator - Perks of Skyrim.esp",
                0x00058202,
                "",
                { false, true, true, true, false },
            },
            RevealRule{
                "Ordinator - Perks of Skyrim.esp",
                0x00058204,
                "",
                { false, false, true, false, false },
            },
            RevealRule{
                "Adamant.esp",
                0x00058202,
                "",
                { false, true, true, false, false },
            },
            RevealRule{
                "Adamant.esp",
                0x00058204,
                "",
                { false, false, false, true, true },
            },
        };

        bool IsPluginConditionMet(std::string_view pluginName) {
            if (pluginName.empty()) {
                return true;
            }

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler && dataHandler->LookupModByName(pluginName) != nullptr;
        }

        RE::BGSPerk* ResolvePerk(const RevealRule& rule) {
            if (!rule.perkEditorID.empty()) {
                return RE::TESForm::LookupByEditorID<RE::BGSPerk>(rule.perkEditorID);
            }

            if (rule.perkFormID != 0) {
                return RE::TESForm::LookupByID<RE::BGSPerk>(rule.perkFormID);
            }

            return nullptr;
        }

        void MergeCategories(RevealCategories& target, const RevealCategories& source) {
            target.allItems = target.allItems || source.allItems;
            target.keys = target.keys || source.keys;
            target.gold = target.gold || source.gold;
            target.jewelry = target.jewelry || source.jewelry;
            target.gems = target.gems || source.gems;
        }

    }

    RevealCategories GetPlayerRevealCategories() {
        const auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return {};
        }

        RevealCategories categories;
        for (const auto& rule : kRevealRules) {
            if (!IsPluginConditionMet(rule.pluginName)) {
                continue;
            }

            auto* perk = ResolvePerk(rule);
            if (perk && player->HasPerk(perk)) {
                MergeCategories(categories, rule.categories);
            }
        }

        return categories;
    }
}
