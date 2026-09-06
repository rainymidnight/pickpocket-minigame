#pragma once

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace StringUtils {
    inline constexpr std::string_view kWhitespace = " \t\r\n";

    [[nodiscard]] inline char ToLowerAscii(char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
    }

    [[nodiscard]] inline std::string ToLowerAscii(std::string_view text) {
        std::string lowered;
        lowered.reserve(text.size());
        std::ranges::transform(text, std::back_inserter(lowered), [](char ch) { return ToLowerAscii(ch); });
        return lowered;
    }

    [[nodiscard]] inline std::string_view TrimView(std::string_view text) {
        const auto first = text.find_first_not_of(kWhitespace);
        if (first == std::string_view::npos) {
            return {};
        }

        return text.substr(first, text.find_last_not_of(kWhitespace) - first + 1);
    }

    [[nodiscard]] inline std::string Trim(std::string_view text) {
        return std::string(TrimView(text));
    }

    [[nodiscard]] inline bool EqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs) {
        return std::ranges::equal(lhs, rhs, [](char left, char right) {
            return ToLowerAscii(left) == ToLowerAscii(right);
        });
    }
}
