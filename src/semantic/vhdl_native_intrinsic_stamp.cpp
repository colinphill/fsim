// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_native_intrinsics.hpp"

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/design_library.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// Native intrinsic stamp (gate BD build D2). The declarations of a covered
// package are the items of its declarative part (IEEE 1076-2019 4.7), which
// vhdl::Unit::declarations lists in source order: the HIR builder merges the
// parser's per-category vectors by source offset
// (VhdlHirBuilder::add_unit_declarations), and lists only the declarations
// the package itself makes (enumeration literals, record elements and
// formal parameters are children, not entries). The generated table lists
// the same items in the same source order (scripts/
// generate_vhdl_native_intrinsics.py), so the i-th declaration takes
// the i-th id of the package's list (0: an item that stays ordinary VHDL);
// the count and the designators are checked.
namespace fsim::semantic {
namespace {

[[nodiscard]] std::string_view unquoted(const std::string_view text) noexcept
{
    if (text.size() >= 2U && text.front() == '"' && text.back() == '"') {
        return text.substr(1U, text.size() - 2U);
    }
    return text;
}

// The designator of a table key: the text after the kind up to the first
// '(' (subprogram), '=' (alias) or '#' (duplicate suffix); an operator
// symbol is read to its closing quotation mark.
[[nodiscard]] std::string_view key_designator(const std::string_view key)
{
    const auto bar = key.find('|');
    if (bar == std::string_view::npos) {
        throw std::logic_error { "VHDL native intrinsics: malformed key "
            + std::string { key } };
    }
    const auto rest = key.substr(bar + 1U);
    if (!rest.empty() && rest.front() == '"') {
        const auto close = rest.find('"', 1U);
        if (close == std::string_view::npos) {
            throw std::logic_error { "VHDL native intrinsics: malformed key "
                + std::string { key } };
        }
        return rest.substr(0U, close + 1U);
    }
    return rest.substr(0U, rest.find_first_of("(=#"));
}

[[nodiscard]] bool same_designator(
    const std::string_view declared, const std::string_view keyed)
{
    return support::ascii_lower(unquoted(declared))
        == support::ascii_lower(unquoted(keyed));
}

} // namespace

void stamp_vhdl_native_intrinsics(
    CompiledDesign& design, const frontend::VhdlStandard standard)
{
    const auto table = vhdl_native_intrinsics();
    bool changed = false;
    for (const auto& unit : design.vhdl_hir.units()) {
        // A package declaration (not a package body) of library IEEE or STD.
        if (unit.kind != vhdl::UnitKind::package
            || !unit.primary_name.empty() || unit.extended_name) {
            continue;
        }
        const auto library = support::ascii_lower(
            support::normalized_library(unit.library));
        const auto package = support::ascii_lower(unit.name);
        const auto items
            = vhdl_native_package_items(standard, library, package);
        if (items.empty()) {
            continue;
        }
        const std::string package_name = library + "." + package;
        std::size_t next { };
        for (const auto id : unit.declarations) {
            const auto* declaration = design.vhdl_hir.find_declaration(id);
            if (declaration == nullptr) {
                throw std::logic_error { "VHDL native intrinsics: package "
                    + package_name + " lists a missing declaration" };
            }
            if (next >= items.size()) {
                throw std::logic_error { "VHDL native intrinsics: package "
                    + package_name + " declares more items than the table ("
                    + std::to_string(items.size()) + "); item "
                    + declaration->name };
            }
            const auto intrinsic = items[next];
            if (intrinsic == 0U) {
                // An item that stays ordinary VHDL (X01, CopyRightNotice).
                ++next;
                continue;
            }
            if (intrinsic > table.size()) {
                throw std::logic_error { "VHDL native intrinsics: package "
                    + package_name + " item " + declaration->name
                    + " has an invalid table id" };
            }
            const auto& row = table[intrinsic - 1U];
            if (row.library != library || row.package != package
                || !same_designator(
                    declaration->name, key_designator(row.key))) {
                throw std::logic_error { "VHDL native intrinsics: package "
                    + package_name + " item " + std::to_string(next + 1U)
                    + " is " + declaration->name + " but the table has "
                    + std::string { row.key } };
            }
            design.vhdl_hir.mutable_declarations()[id.value()]
                .native_intrinsic
                = intrinsic;
            changed = true;
            ++next;
        }
        if (next != items.size()) {
            throw std::logic_error { "VHDL native intrinsics: package "
                + package_name + " declares " + std::to_string(next)
                + " items but the table has " + std::to_string(items.size()) };
        }
    }
    if (changed) {
        // The stamp changes no field the lookup indexes read.
        design.refresh_lookup_indexes();
    }
}

} // namespace fsim::semantic
