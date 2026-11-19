// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

// The declarations of package STD.STANDARD (IEEE 1076-2019 16.3) as a typed
// identity: the type, subtype, attribute and predefined operation each has a
// value of its own, with the standards that have it. Generated with the
// catalog (scripts/gen_vhdl_standard_catalog.py), so each value is the row of
// fsim::semantic::vhdl_standard_catalog() plus one; the frontend, the
// elaboration and the application layer name a declaration through the value,
// never through its spelling.
namespace fsim::support {

enum class StandardDeclaration : std::uint16_t {
    none = 0,
#define X(name, row, since, until) name = (row) + 1,
#include "fsim/support/vhdl_standard_declaration.inc"
#undef X
};

namespace standard_declaration_detail {

struct Row {
    std::string_view name;
    std::uint16_t catalog_row;
    std::uint16_t since;
    std::uint16_t until;
};

inline constexpr Row rows[] = {
#define X(name, row, since, until) { #name, row, since, until },
#include "fsim/support/vhdl_standard_declaration.inc"
#undef X
};

inline constexpr std::size_t row_count = sizeof(rows) / sizeof(rows[0]);

[[nodiscard]] constexpr const Row& row_of(const StandardDeclaration declaration)
{
    const auto value = static_cast<std::size_t>(declaration);
    if (value == 0U || value > row_count) {
        throw std::out_of_range("StandardDeclaration has no catalog row");
    }
    return rows[value - 1U];
}

// The revision of IEEE 1076 whose STANDARD a standard year uses: 1076-2000 and
// 1076-2002 added nothing to STANDARD (16.3), so they use the 1993 rows.
[[nodiscard]] constexpr std::uint16_t revision_of(const std::uint16_t year)
{
    switch (year) {
    case 1987U:
        return 1987U;
    case 1993U:
    case 2000U:
    case 2002U:
        return 1993U;
    case 2008U:
        return 2008U;
    case 2019U:
        return 2019U;
    default:
        throw std::invalid_argument("unknown VHDL standard year");
    }
}

} // namespace standard_declaration_detail

/// The number of declarations (the largest StandardDeclaration value).
[[nodiscard]] constexpr std::size_t standard_declaration_count() noexcept
{
    return standard_declaration_detail::row_count;
}

/// The first revision (1987, 1993, 2008 or 2019) whose STANDARD has it.
[[nodiscard]] constexpr std::uint16_t since(const StandardDeclaration declaration)
{
    return standard_declaration_detail::row_of(declaration).since;
}

/// The first revision whose STANDARD no longer has it because a later
/// declaration replaces it, or 0 when no later revision removes it.
[[nodiscard]] constexpr std::uint16_t until(const StandardDeclaration declaration)
{
    return standard_declaration_detail::row_of(declaration).until;
}

/// Whether the STANDARD of the IEEE 1076 edition `year` (1987, 1993, 2000,
/// 2002, 2008 or 2019) has the declaration; another year throws.
[[nodiscard]] constexpr bool available(const StandardDeclaration declaration,
    const std::uint16_t year)
{
    const auto& row = standard_declaration_detail::row_of(declaration);
    const auto revision = standard_declaration_detail::revision_of(year);
    return row.since <= revision && (row.until == 0U || revision < row.until);
}

/// The generated enumerator name, for diagnostics and tests; never a key.
[[nodiscard]] constexpr std::string_view name(const StandardDeclaration declaration)
{
    return standard_declaration_detail::row_of(declaration).name;
}

/// The index of the declaration's row in fsim::semantic::vhdl_standard_catalog().
[[nodiscard]] constexpr std::size_t catalog_row(const StandardDeclaration declaration)
{
    return standard_declaration_detail::row_of(declaration).catalog_row;
}

} // namespace fsim::support
