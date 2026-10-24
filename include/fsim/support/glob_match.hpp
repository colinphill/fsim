// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string_view>

namespace fsim::support {

/// Whether `value` matches `pattern`, where `*` matches any run of
/// characters and `?` any one character. Greedy with single-star
/// backtracking, so the work is at most quadratic; `step` is called once per
/// step (for callers that bound the work).
template <typename Step>
[[nodiscard]] bool glob_match(const std::string_view pattern,
    const std::string_view value, Step&& step)
{
    std::size_t pattern_index { };
    std::size_t value_index { };
    std::size_t star { std::string_view::npos };
    std::size_t retry { };
    while (value_index < value.size()) {
        step();
        if (pattern_index < pattern.size()
            && (pattern[pattern_index] == '?'
                || pattern[pattern_index] == value[value_index])) {
            ++pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size()
            && pattern[pattern_index] == '*') {
            star = pattern_index++;
            retry = value_index;
        } else if (star != std::string_view::npos) {
            pattern_index = star + 1U;
            value_index = ++retry;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        step();
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

[[nodiscard]] inline bool glob_match(
    const std::string_view pattern, const std::string_view value) noexcept
{
    return glob_match(pattern, value, [] { });
}

}  // namespace fsim::support
