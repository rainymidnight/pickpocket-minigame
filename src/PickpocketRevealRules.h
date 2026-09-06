#pragma once

namespace PickpocketRevealRules {
    struct RevealCategories {
        bool allItems = false;
        bool keys = false;
        bool gold = false;
        bool jewelry = false;
        bool gems = false;
    };

    [[nodiscard]] RevealCategories GetPlayerRevealCategories();
}
