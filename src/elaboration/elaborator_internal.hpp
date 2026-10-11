// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "elaboration_targets.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration::elaboration_detail {

using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;

// The units of STD.STANDARD.TIME and their values in femtoseconds
// (IEEE 1076-2008 16.3).
inline constexpr std::array<std::pair<std::string_view, std::uint64_t>, 8U>
    vhdl_time_units { {
        { "fs", 1U },
        { "ps", 1'000U },
        { "ns", 1'000'000U },
        { "us", 1'000'000'000U },
        { "ms", 1'000'000'000'000U },
        { "sec", 1'000'000'000'000'000U },
        { "min", 60'000'000'000'000'000U },
        { "hr", 3'600'000'000'000'000'000U },
    } };

// ASCII case folding of identifiers (VHDL basic identifiers are
// case-insensitive, IEEE 1076-2008 15.4.2).
inline void lowercase_ascii(std::string& text) noexcept
{
    for (auto& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
}

[[nodiscard]] inline std::string lowercase_ascii_copy(
    const std::string_view text)
{
    std::string result { text };
    lowercase_ascii(result);
    return result;
}

[[nodiscard]] inline bool equal_ignoring_ascii_case(
    const std::string_view left, const std::string_view right) noexcept
{
    const auto lower = [](const char value) {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value - 'A' + 'a')
            : value;
    };
    return left.size() == right.size()
        && std::ranges::equal(left, right, [&](const char lhs, const char rhs) {
               return lower(lhs) == lower(rhs);
           });
}

// The last segment of an expanded or scoped name: `lib.pkg.item` and
// `pkg::item` give `item`.
[[nodiscard]] constexpr std::string_view simple_name_of(
    const std::string_view spelling) noexcept
{
    const auto separator = spelling.find_last_of(".:");
    return separator == std::string_view::npos
        ? spelling
        : spelling.substr(separator + 1U);
}

[[nodiscard]] bool is_two_state_domain(
    frontend::ValueDomain domain) noexcept;
[[nodiscard]] ValueKind value_kind(
    frontend::ValueDomain domain) noexcept;

[[nodiscard]] std::string simple_top_name(std::string_view top);
[[nodiscard]] std::optional<std::uint64_t> unsigned_decimal(
    std::string_view text);
[[nodiscard]] std::uint64_t index_distance(
    std::int64_t lhs,
    std::int64_t rhs) noexcept;
[[nodiscard]] PackedLogic4 unsigned_value(
    std::uint64_t value,
    std::size_t width);
[[nodiscard]] PackedLogic4 integer_value(
    std::int64_t value,
    std::size_t width = 32U);

// The frontend span of a compiled source span, with its physical file and
// macro expansion stack.
[[nodiscard]] frontend::SourceSpan compiled_source_span(
    const semantic::CompiledDesign& compiled,
    semantic::SourceSpanId source);

[[nodiscard]] bool checked_add(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result);
[[nodiscard]] bool checked_subtract(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result);
[[nodiscard]] bool checked_multiply(
    std::int64_t left,
    std::int64_t right,
    std::int64_t& result);

} // namespace fsim::elaboration::elaboration_detail
