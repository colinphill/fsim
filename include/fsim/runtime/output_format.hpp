// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design_core.hpp"
#include "fsim/runtime/simir.hpp"

#include <cstdint>
#include <functional>
#include <optional>
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

// The simulation tick of a known nonnegative integral `%t` argument given
// in `unit_femtoseconds` (IEEE 1800-2017 21.2.1.3), or nullopt.
[[nodiscard]] std::optional<SimulationTick> integral_time_ticks(
    const PackedLogic4& value,
    bool signed_value,
    std::uint64_t unit_femtoseconds,
    const SystemVerilogTimeFormat& time_format,
    SystemVerilogScalarKind scalar_kind = SystemVerilogScalarKind::None);

[[nodiscard]] std::string make_time_output(
    std::string_view prefix,
    std::string_view suffix,
    SimulationTick tick,
    const SystemVerilogTimeFormat& time_format,
    bool use_timeformat_width,
    std::uint32_t minimum_width,
    bool left_justify,
    bool zero_pad);

/// The SimIR format of a conversion that consumes a value: %p and %v show
/// decimal digits; %m, %l and %t do not consume a value and are rejected.
[[nodiscard]] OutputFormat runtime_output_format(frontend::OutputFormat format);

/// The digits `%d` shows of a `width`-bit value without an explicit field
/// width (IEEE 1800-2017 21.2.1.3): those of its largest magnitude, plus a
/// sign for a signed value.
[[nodiscard]] std::uint32_t automatic_decimal_width(
    std::size_t width, bool signed_value) noexcept;

/// The text of a run-time format applied to its arguments (IEEE 1800-2017
/// 21.3.3), with values read through `packed` and `text`. Throws for a
/// malformed format or one that needs more arguments than it has.
[[nodiscard]] std::string format_runtime_string(
    const RuntimeStringFormat& operation, std::string_view format,
    const std::function<PackedLogic4(const RuntimeFormatArgument&)>& packed,
    const std::function<std::string(const RuntimeFormatArgument&)>& text,
    SimulationTick tick, const SystemVerilogTimeFormat& time_format);

} // namespace fsim::runtime::simir
