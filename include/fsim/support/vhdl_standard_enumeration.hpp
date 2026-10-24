// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "fsim/support/vhdl_identifier.hpp"

// The enumeration types of STD.STANDARD the compiler provides natively rather
// than from a package declaration (IEEE 1076-2019 16.3): one table that the
// frontend, the semantic resolver and elaboration all consult for a type's
// literals, positions and storage width.
namespace fsim::support {

struct VhdlStandardEnumeration {
    std::string_view name;
    std::span<const std::string_view> literals;
    // The edition of IEEE 1076 that predefines the type.
    int since { };
};

namespace vhdl_standard_enumeration_detail {

inline constexpr std::array<std::string_view, 4U> severity_level {
    "note", "warning", "error", "failure"
};
inline constexpr std::array<std::string_view, 3U> file_open_kind {
    "read_mode", "write_mode", "append_mode"
};
inline constexpr std::array<std::string_view, 4U> file_open_status {
    "open_ok", "status_error", "name_error", "mode_error"
};
// The direction element of a range record (IEEE 1076-2019 5.3.3.2).
inline constexpr std::array<std::string_view, 2U> range_direction {
    "ascending", "descending"
};

inline constexpr std::array<VhdlStandardEnumeration, 4U> types { {
    { "severity_level", severity_level, 1987 },
    { "file_open_kind", file_open_kind, 1993 },
    { "file_open_status", file_open_status, 1993 },
    { "range_direction", range_direction, 2019 },
} };

} // namespace vhdl_standard_enumeration_detail

[[nodiscard]] constexpr std::span<const VhdlStandardEnumeration>
vhdl_standard_enumerations() noexcept
{
    return vhdl_standard_enumeration_detail::types;
}

/// The natively provided STD.STANDARD enumeration a simple or
/// STD.STANDARD-selected type name denotes, or null.
[[nodiscard]] constexpr const VhdlStandardEnumeration*
find_vhdl_standard_enumeration(std::string_view name) noexcept
{
    constexpr std::string_view prefix { "std.standard." };
    if (name.size() > prefix.size()
        && equal_ignoring_ascii_case(name.substr(0U, prefix.size()), prefix)) {
        name.remove_prefix(prefix.size());
    }
    for (const auto& type : vhdl_standard_enumeration_detail::types) {
        if (equal_ignoring_ascii_case(name, type.name)) {
            return &type;
        }
    }
    return nullptr;
}

/// The position of a literal within its type, when the literal belongs to a
/// natively provided STD.STANDARD enumeration.
[[nodiscard]] constexpr std::optional<std::int64_t>
vhdl_standard_enumeration_position(
    const VhdlStandardEnumeration& type, const std::string_view literal) noexcept
{
    for (std::size_t index { }; index < type.literals.size(); ++index) {
        if (equal_ignoring_ascii_case(literal, type.literals[index])) {
            return static_cast<std::int64_t>(index);
        }
    }
    return std::nullopt;
}

/// The bits that hold any position of the type.
[[nodiscard]] constexpr std::size_t vhdl_standard_enumeration_width(
    const VhdlStandardEnumeration& type) noexcept
{
    std::size_t width { 1U };
    while ((std::size_t { 1U } << width) < type.literals.size()) {
        ++width;
    }
    return width;
}

} // namespace fsim::support
