// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace fsim::runtime::simir {

[[nodiscard]] std::string format_output_value(
    const PackedLogic4& value,
    OutputFormat format,
    bool signed_decimal,
    bool suppress_leading_zero);

[[nodiscard]] std::string make_formatted_output(
    std::string_view prefix,
    std::string_view suffix,
    OutputFormat format,
    const PackedLogic4& value,
    bool signed_decimal,
    bool suppress_leading_zero,
    std::uint32_t minimum_width,
    bool left_justify,
    bool zero_pad,
    SystemVerilogScalarKind scalar_kind = SystemVerilogScalarKind::None,
    // Digits after the point for %e/%f/%g (`%.3f`); the maximum value
    // selects the conversion's default.
    std::uint32_t precision = std::numeric_limits<std::uint32_t>::max());

// A container value as an assignment pattern, as `%p` prints it
// (IEEE 1800-2017 21.2.1.7): `'{1,-2,3}`, `'{a:1,s:"x"}`, `'{"k":1}`.
[[nodiscard]] std::string format_container_assignment_pattern(
    const ContainerValue& value);

[[nodiscard]] std::string make_time_output(
    std::string_view prefix,
    std::string_view suffix,
    SimulationTick tick,
    const SystemVerilogTimeFormat& time_format,
    bool use_timeformat_width,
    std::uint32_t minimum_width,
    bool left_justify,
    bool zero_pad);

} // namespace fsim::runtime::simir
