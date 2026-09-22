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
