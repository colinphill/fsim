// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/vhdl_identifier.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace fsim::support {

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

/// The femtoseconds in one TIME unit spelled exactly (lower case).
[[nodiscard]] constexpr std::optional<std::uint64_t>
vhdl_time_unit_femtoseconds(const std::string_view unit) noexcept
{
    for (const auto& [name, femtoseconds] : vhdl_time_units) {
        if (name == unit) {
            return femtoseconds;
        }
    }
    return std::nullopt;
}

/// Whether a type name is STD.STANDARD.TIME or its DELAY_LENGTH subtype.
[[nodiscard]] constexpr bool is_vhdl_time_type_name(
    const std::string_view name) noexcept
{
    return equal_ignoring_ascii_case(name, "time")
        || equal_ignoring_ascii_case(name, "delay_length");
}

} // namespace fsim::support
