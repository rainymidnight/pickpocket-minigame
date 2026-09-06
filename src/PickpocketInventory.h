#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace PickpocketInventory {
    struct Item {
        RE::FormID formID = 0;
        RE::TESBoundObject* object = nullptr;
        std::string name;
        std::int32_t count = 0;
        std::int32_t value = 0;
        std::uint32_t stealValue = 0;
        float weight = 0.0f;
        bool isKey = false;
        bool isGold = false;
        bool isJewelry = false;
        bool isGem = false;
        bool worn = false;
        bool favorited = false;
        bool questItem = false;
        bool minigameEligible = false;
        std::string blockedReason;
    };

    struct Snapshot {
        std::uint64_t revision = 0;
        RE::ObjectRefHandle targetHandle;
        RE::FormID targetFormID = 0;
        std::string targetName;
        std::vector<Item> items;
    };

    struct TransferResult {
        bool succeeded = false;
        std::string message;
    };

    // Ranks the items a caller accepts by how attractive they are to steal and
    // keeps the best `limit` of them. Phase 1 grades difficulty from this list
    // and phase 2 fills its dial slots from it, so both must rank identically.
    template <class Filter>
    [[nodiscard]] std::vector<const Item*> SelectTopItems(
        const Snapshot& snapshot,
        std::size_t limit,
        Filter accepts) {
        std::vector<const Item*> selected;
        selected.reserve(snapshot.items.size());
        for (const auto& item : snapshot.items) {
            if (accepts(item)) {
                selected.push_back(&item);
            }
        }

        std::ranges::sort(selected, [](const Item* lhs, const Item* rhs) {
            if (lhs->stealValue != rhs->stealValue) {
                return lhs->stealValue > rhs->stealValue;
            }
            if (lhs->value != rhs->value) {
                return lhs->value > rhs->value;
            }
            if (lhs->count != rhs->count) {
                return lhs->count > rhs->count;
            }
            return lhs->formID < rhs->formID;
        });

        if (selected.size() > limit) {
            selected.resize(limit);
        }

        return selected;
    }

    [[nodiscard]] bool HasRenderableModel(RE::TESBoundObject* object);
    void CaptureFromActor(RE::Actor& actor);
    Snapshot GetSnapshot();
    void TransferItemStacks(std::span<const RE::FormID> itemFormIDs, bool ignoreMinigameEligibility = false);
    TransferResult TriggerPickpocketFailure();
}
