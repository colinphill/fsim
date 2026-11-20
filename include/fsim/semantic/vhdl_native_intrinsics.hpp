// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/token.hpp"
#include "fsim/semantic/vhdl_native_names.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace fsim::semantic {

class CompiledDesign;

/// A declaration of a compiler-served standard package (IEEE 1076-2019 16:
/// STD.TEXTIO, STD.ENV, IEEE.STD_LOGIC_1164, IEEE.NUMERIC_BIT,
/// IEEE.NUMERIC_STD, their UNSIGNED companions, IEEE.MATH_REAL) whose
/// implementation is native, generated at build time from the package
/// declarations of every supported edition
/// (scripts/generate_vhdl_native_intrinsics.py). `key` names the
/// declaration as the derivation of the editions does
/// (scripts/derive_ieee_packages.py item keys, e.g.
/// "function|to_x01(bit_vector)->std_ulogic_vector"), so overloads with one
/// designator have distinct keys. `editions` has bit `int(VhdlStandard)` set
/// for each edition whose package declares it. `package_id`, `kind` and
/// `name` (generated enumerators, vhdl_native_names.hpp) are how compiler
/// code identifies a native declaration: never by its spelling.
struct VhdlNativeIntrinsic {
    std::string_view library;
    std::string_view package;
    std::string_view key;
    std::uint32_t editions;
    VhdlNativePackage package_id;
    VhdlNativeKind kind;
    VhdlNativeName name;
};

/// The table; intrinsic id N is element N - 1 (0 is no intrinsic).
[[nodiscard]] std::span<const VhdlNativeIntrinsic> vhdl_native_intrinsics() noexcept;

/// The entry of intrinsic id `id` (vhdl::Declaration::native_intrinsic);
/// null for 0 or an id outside the table.
[[nodiscard]] const VhdlNativeIntrinsic* vhdl_native_intrinsic(
    std::uint32_t id) noexcept;

/// The intrinsic id of every declarative item of `library`.`package` in
/// `standard`, in declaration order; 0 for an item that stays ordinary VHDL
/// (the subtypes X01, X01Z, UX01, UX01Z and the constant CopyRightNotice).
/// Empty for a package the table does not cover.
[[nodiscard]] std::span<const std::uint32_t> vhdl_native_package_items(
    frontend::VhdlStandard standard, std::string_view library,
    std::string_view package) noexcept;

/// The editions (bit int(VhdlStandard) per edition) whose `package`
/// declares an item, aliases included, with the identifier designator
/// `designator` (case-folded, IEEE 1076-2019 15.4.2); 0 for none. It tells
/// a name that is not visible in one edition because only another
/// edition's package declares it (e.g. 2008 STD.TEXTIO HEX_READ in 2002).
[[nodiscard]] std::uint32_t vhdl_native_designator_editions(
    VhdlNativePackage package, std::string_view designator) noexcept;

/// A digest of the whole table: a standard library artifact records it, and
/// loading one built against another table is an error.
[[nodiscard]] std::string_view vhdl_native_intrinsics_digest() noexcept;

/// Stamps vhdl::Declaration::native_intrinsic on the declarations of the
/// covered packages of `design`, which hold the units analyzed from
/// `standard`'s sources (the standard library artifact build). Throws
/// std::logic_error naming the package and item when the analyzed
/// declarations and the table disagree in count, order or designator.
void stamp_vhdl_native_intrinsics(
    CompiledDesign& design, frontend::VhdlStandard standard);

} // namespace fsim::semantic
