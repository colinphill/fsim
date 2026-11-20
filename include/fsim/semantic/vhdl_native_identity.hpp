// SPDX-License-Identifier: Apache-2.0
#pragma once

// Native identity of the declarations of the precompiled standard packages
// (gate BD build D2): the only way compiler code recognizes STD.TEXTIO,
// STD.ENV, IEEE.STD_LOGIC_1164, IEEE.NUMERIC_BIT, IEEE.NUMERIC_STD, their
// UNSIGNED companions and IEEE.MATH_REAL declarations. Never by spelling:
// a user declaration of the same name is an ordinary declaration.
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/semantic/vhdl_native_intrinsics.hpp"

#include "fsim/support/vhdl_standard_declaration.hpp"
#include "fsim/support/vhdl_standard_enumeration.hpp"

#include <optional>
#include <unordered_set>

namespace fsim::semantic {

/// The native entry of `declaration`; null for an ordinary declaration.
/// Enumeration literals, record elements, formals and implicit operations
/// carry none: identify them through their type or their subprogram.
[[nodiscard]] const VhdlNativeIntrinsic* vhdl_native_of(
    const vhdl::Declaration& declaration) noexcept;

/// Whether `declaration` is the native declaration `name` of `package`
/// (any kind).
[[nodiscard]] bool vhdl_native_is(const vhdl::Declaration& declaration,
    VhdlNativePackage package, VhdlNativeName name) noexcept;

/// The designator of a native declaration as its table key spells it,
/// lowercase ("to_unsigned", "\"and\""): for diagnostics and generated
/// names only, never to identify a declaration.
[[nodiscard]] std::string_view vhdl_native_designator(
    const VhdlNativeIntrinsic& entry) noexcept;

/// The provided type a native type declaration denotes (STD_ULOGIC,
/// STD_LOGIC, their vectors, NUMERIC_STD and NUMERIC_BIT UNSIGNED/SIGNED and
/// their unresolved forms, the TEXTIO types); none for any other entry.
[[nodiscard]] vhdl::ProvidedType vhdl_native_provided_type(
    const VhdlNativeIntrinsic& entry) noexcept;

/// The predefined array identity of STD_ULOGIC_VECTOR and STD_LOGIC_VECTOR;
/// none for any other entry.
[[nodiscard]] vhdl::BuiltinTypeIdentity vhdl_native_builtin_type(
    const VhdlNativeIntrinsic& entry) noexcept;

/// The logical-operator family of a native "not", "and", "or", "nand",
/// "nor", "xor" or "xnor" of IEEE.STD_LOGIC_1164 or IEEE.NUMERIC_STD
/// (one family per operator: the kernel selects by operand shape); none for
/// any other entry.
[[nodiscard]] vhdl::BuiltinOperatorIdentity vhdl_native_operator(
    const VhdlNativeIntrinsic& entry) noexcept;

/// The value domain of a native type's scalar elements.
enum class VhdlNativeDomain : std::uint8_t {
    none,
    logic9, // STD_ULOGIC and every array of it (1164, NUMERIC_STD)
    bit2,   // NUMERIC_BIT's UNSIGNED and SIGNED
};
[[nodiscard]] VhdlNativeDomain vhdl_native_domain(
    const VhdlNativeIntrinsic& entry) noexcept;

/// The native entry of the nearest native type or subtype declaration on
/// `type`'s subtype/alias chain: STD_LOGIC for STD_LOGIC and for a user
/// `subtype S is STD_LOGIC`, STD_LOGIC_VECTOR for an anonymous
/// `STD_LOGIC_VECTOR(7 downto 0)`; null when the chain reaches an ordinary
/// base type (or a missing link or a cycle). `Hir` is CompiledDesign or
/// SpecializedHirUnit.
template <typename Hir>
[[nodiscard]] const VhdlNativeIntrinsic* vhdl_native_type_of(
    const Hir& hir, TypeId type)
{
    std::unordered_set<std::uint32_t> visited;
    while (type.valid() && visited.insert(type.value()).second) {
        const auto record = hir.find_type(type);
        if (!record || record->vhdl == nullptr) {
            return nullptr;
        }
        if (record->vhdl->declaration.valid()) {
            if (const auto declaration
                = hir.find_declaration(record->vhdl->declaration);
                declaration && declaration->vhdl != nullptr) {
                if (const auto* entry = vhdl_native_of(*declaration->vhdl);
                    entry != nullptr && entry->kind == VhdlNativeKind::type) {
                    return entry;
                }
            }
        }
        if (record->vhdl->form != vhdl::TypeForm::subtype
            && record->vhdl->form != vhdl::TypeForm::alias) {
            return nullptr;
        }
        type = record->vhdl->base.type_mark.target;
    }
    return nullptr;
}

/// The native entry of `type`'s base type (IEEE 1076-2019 5.1): STD_ULOGIC
/// for STD_LOGIC, UNRESOLVED_UNSIGNED for UNSIGNED (2008/2019); null for an
/// ordinary base type.
template <typename Hir>
[[nodiscard]] const VhdlNativeIntrinsic* vhdl_native_base_type_of(
    const Hir& hir, const TypeId type)
{
    const auto* base = vhdl_base_type_definition(hir, type);
    if (base == nullptr || !base->declaration.valid()) {
        return nullptr;
    }
    const auto declaration = hir.find_declaration(base->declaration);
    if (!declaration || declaration->vhdl == nullptr) {
        return nullptr;
    }
    const auto* entry = vhdl_native_of(*declaration->vhdl);
    return entry != nullptr && entry->kind == VhdlNativeKind::type ? entry : nullptr;
}

/// Whether `type`'s base type is IEEE.STD_LOGIC_1164's STD_ULOGIC: its
/// literals are the nine values in runtime::Logic9 order (16.7).
template <typename Hir>
[[nodiscard]] bool vhdl_native_logic9(const Hir& hir, const TypeId type)
{
    const auto* entry = vhdl_native_base_type_of(hir, type);
    return entry != nullptr && entry->package_id == VhdlNativePackage::ieee_std_logic_1164
        && entry->name == VhdlNativeName::std_ulogic;
}

/// Whether a stored expression type (Expression::type) is the STANDARD type
/// `row` (16.3).
[[nodiscard]] inline bool vhdl_standard_type_is(
    const vhdl::TypeIdentity& type, const support::StandardDeclaration row) noexcept
{
    return type.kind == vhdl::TypeIdentity::Kind::standard && type.standard == row;
}

/// The intrinsic enumeration a stored expression type is: STANDARD BOOLEAN,
/// BIT or CHARACTER (16.3), or a base type of IEEE.STD_LOGIC_1164's
/// STD_ULOGIC (16.7); none for any other type.
template <typename Hir>
[[nodiscard]] std::optional<support::VhdlIntrinsicEnumeration>
vhdl_intrinsic_enumeration_of(const Hir& hir, const vhdl::TypeIdentity& type)
{
    using Kind = vhdl::TypeIdentity::Kind;
    using SD = support::StandardDeclaration;
    if (type.kind == Kind::standard) {
        switch (type.standard) {
        case SD::boolean: return support::VhdlIntrinsicEnumeration::boolean;
        case SD::bit: return support::VhdlIntrinsicEnumeration::bit;
        case SD::character: return support::VhdlIntrinsicEnumeration::character;
        default: return std::nullopt;
        }
    }
    if (type.kind == Kind::declared && vhdl_native_logic9(hir, type.declared)) {
        return support::VhdlIntrinsicEnumeration::std_ulogic;
    }
    return std::nullopt;
}

} // namespace fsim::semantic
