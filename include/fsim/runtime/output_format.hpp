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

} // namespace fsim::runtime::simir
