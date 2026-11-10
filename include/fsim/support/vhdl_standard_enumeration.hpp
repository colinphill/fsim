// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

/// The enumeration types the compiler provides without a retained declaration
/// whose literals include character literals: BOOLEAN, BIT and CHARACTER of
/// STD.STANDARD (IEEE 1076-2019 16.3) and STD_ULOGIC of IEEE.STD_LOGIC_1164
/// (16.7). Each carries its literal list here, so that a fold resolves a
/// character literal by the type its context gives it (9.3.2, 12.5) to its
/// position in that type, whatever other types share the spelling.
enum class VhdlIntrinsicEnumeration : std::uint8_t {
    boolean,
    bit,
    character,
    std_ulogic,
};

namespace vhdl_standard_enumeration_detail {

// STD_ULOGIC's literals in declaration order: 'U' is position 0 and '-'
// position 8 (IEEE 1076-2019 16.7).
inline constexpr std::string_view std_ulogic_literals { "UX01ZWLH-" };
// The identifiers of CHARACTER's control positions 0 to 31; DEL is 127 and
// C128 to C159 are 128 to 159 (IEEE 1076-2019 16.3).
inline constexpr std::array<std::string_view, 32U> character_controls {
    "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
    "bs", "ht", "lf", "vt", "ff", "cr", "so", "si",
    "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb",
    "can", "em", "sub", "esc", "fsp", "gsp", "rsp", "usp",
};

} // namespace vhdl_standard_enumeration_detail

/// The intrinsic enumeration a predefined type identity denotes, as the
/// context-typing pass records it (an expression's resolved_builtin_type):
/// the key it derives from the declaration of BOOLEAN, BIT, CHARACTER,
/// STD_ULOGIC or its subtype STD_LOGIC, retained or provided by the compiler.
/// Never a source spelling, whose meaning only name resolution decides
/// (CompiledDesignResolver::resolve_vhdl_intrinsic_enumeration).
[[nodiscard]] constexpr std::optional<VhdlIntrinsicEnumeration>
find_vhdl_intrinsic_enumeration(const std::string_view identity) noexcept
{
    if (equal_ignoring_ascii_case(identity, "boolean")) {
        return VhdlIntrinsicEnumeration::boolean;
    }
    if (equal_ignoring_ascii_case(identity, "bit")) {
        return VhdlIntrinsicEnumeration::bit;
    }
    if (equal_ignoring_ascii_case(identity, "character")) {
        return VhdlIntrinsicEnumeration::character;
    }
    if (equal_ignoring_ascii_case(identity, "std_ulogic")
        || equal_ignoring_ascii_case(identity, "std_logic")) {
        return VhdlIntrinsicEnumeration::std_ulogic;
    }
    return std::nullopt;
}

/// The position of the type's last literal (its first is position 0).
[[nodiscard]] constexpr std::int64_t vhdl_intrinsic_enumeration_last(
    const VhdlIntrinsicEnumeration type) noexcept
{
    switch (type) {
    case VhdlIntrinsicEnumeration::boolean:
    case VhdlIntrinsicEnumeration::bit:
        return 1;
    case VhdlIntrinsicEnumeration::character:
        return 255;
    case VhdlIntrinsicEnumeration::std_ulogic:
        return static_cast<std::int64_t>(
                   vhdl_standard_enumeration_detail::std_ulogic_literals
                       .size())
            - 1;
    }
    return 0;
}

/// The position of a literal of the type, spelled as in a source text: a
/// character literal (`'Z'`, which is case sensitive) or an identifier (any
/// case). No value when the type has no such literal.
[[nodiscard]] constexpr std::optional<std::int64_t>
vhdl_intrinsic_enumeration_position(
    const VhdlIntrinsicEnumeration type,
    const std::string_view literal) noexcept
{
    const bool character_literal = literal.size() == 3U
        && literal.front() == '\'' && literal.back() == '\'';
    const auto character = character_literal ? literal[1] : '\0';
    switch (type) {
    case VhdlIntrinsicEnumeration::boolean:
        if (equal_ignoring_ascii_case(literal, "false")) {
            return 0;
        }
        if (equal_ignoring_ascii_case(literal, "true")) {
            return 1;
        }
        return std::nullopt;
    case VhdlIntrinsicEnumeration::bit:
        if (character_literal && (character == '0' || character == '1')) {
            return character == '1' ? 1 : 0;
        }
        return std::nullopt;
    case VhdlIntrinsicEnumeration::std_ulogic: {
        const auto position = character_literal
            ? vhdl_standard_enumeration_detail::std_ulogic_literals.find(
                  character)
            : std::string_view::npos;
        return position == std::string_view::npos
            ? std::nullopt
            : std::optional { static_cast<std::int64_t>(position) };
    }
    case VhdlIntrinsicEnumeration::character:
        break;
    }
    if (character_literal) {
        return static_cast<std::int64_t>(
            static_cast<unsigned char>(character));
    }
    const auto& controls = vhdl_standard_enumeration_detail::character_controls;
    for (std::size_t code { }; code < controls.size(); ++code) {
        if (equal_ignoring_ascii_case(literal, controls[code])) {
            return static_cast<std::int64_t>(code);
        }
    }
    if (equal_ignoring_ascii_case(literal, "del")) {
        return 127;
    }
    if (literal.size() == 4U
        && (literal.front() == 'c' || literal.front() == 'C')) {
        std::int64_t code { };
        for (const auto digit : literal.substr(1U)) {
            if (digit < '0' || digit > '9') {
                return std::nullopt;
            }
            code = code * 10 + (digit - '0');
        }
        if (code >= 128 && code <= 159) {
            return code;
        }
    }
    return std::nullopt;
}

/// The literal of the type at a position, as a source text spells it and
/// T'IMAGE writes it (IEEE 1076-2019 16.2.2): a character literal in
/// quotes, or an identifier in lower case.
[[nodiscard]] inline std::string vhdl_intrinsic_enumeration_literal(
    const VhdlIntrinsicEnumeration type, const std::int64_t position)
{
    if (position < 0 || position > vhdl_intrinsic_enumeration_last(type)) {
        return { };
    }
    const auto quoted = [](const char character) {
        return std::string { '\'', character, '\'' };
    };
    switch (type) {
    case VhdlIntrinsicEnumeration::boolean:
        return position == 0 ? "false" : "true";
    case VhdlIntrinsicEnumeration::bit:
        return quoted(position == 0 ? '0' : '1');
    case VhdlIntrinsicEnumeration::std_ulogic:
        return quoted(vhdl_standard_enumeration_detail::std_ulogic_literals
                [static_cast<std::size_t>(position)]);
    case VhdlIntrinsicEnumeration::character:
        break;
    }
    const auto& controls = vhdl_standard_enumeration_detail::character_controls;
    if (position < static_cast<std::int64_t>(controls.size())) {
        return std::string { controls[static_cast<std::size_t>(position)] };
    }
    if (position == 127) {
        return "del";
    }
    if (position >= 128 && position <= 159) {
        return "c" + std::to_string(position);
    }
    return quoted(static_cast<char>(static_cast<unsigned char>(position)));
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
