// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/vhdl_native_identity.hpp"

namespace fsim::semantic {
namespace {

using Name = VhdlNativeName;
using Package = VhdlNativePackage;

[[nodiscard]] bool is_type(const VhdlNativeIntrinsic& entry) noexcept
{
    return entry.kind == VhdlNativeKind::type;
}

} // namespace

const VhdlNativeIntrinsic* vhdl_native_of(
    const vhdl::Declaration& declaration) noexcept
{
    return vhdl_native_intrinsic(declaration.native_intrinsic);
}

bool vhdl_native_is(const vhdl::Declaration& declaration,
    const VhdlNativePackage package, const VhdlNativeName name) noexcept
{
    const auto* entry = vhdl_native_of(declaration);
    return entry != nullptr && entry->package_id == package && entry->name == name;
}

std::string_view vhdl_native_designator(const VhdlNativeIntrinsic& entry) noexcept
{
    auto rest = entry.key.substr(entry.key.find('|') + 1U);
    if (!rest.empty() && rest.front() == '"') {
        return rest.substr(0U, rest.find('"', 1U) + 1U);
    }
    return rest.substr(0U, rest.find_first_of("(=#"));
}

vhdl::ProvidedType vhdl_native_provided_type(
    const VhdlNativeIntrinsic& entry) noexcept
{
    using vhdl::ProvidedType;
    if (!is_type(entry)) {
        return ProvidedType::none;
    }
    switch (entry.package_id) {
    case Package::ieee_std_logic_1164:
        switch (entry.name) {
        case Name::std_ulogic: return ProvidedType::std_ulogic;
        case Name::std_logic: return ProvidedType::std_logic;
        case Name::std_ulogic_vector: return ProvidedType::std_ulogic_vector;
        case Name::std_logic_vector: return ProvidedType::std_logic_vector;
        default: return ProvidedType::none;
        }
    case Package::ieee_numeric_std:
        switch (entry.name) {
        case Name::unsigned_:
        case Name::unresolved_unsigned:
            return ProvidedType::numeric_std_unsigned;
        case Name::signed_:
        case Name::unresolved_signed:
            return ProvidedType::numeric_std_signed;
        default: return ProvidedType::none;
        }
    case Package::ieee_numeric_bit:
        switch (entry.name) {
        case Name::unsigned_:
        case Name::unresolved_unsigned:
            return ProvidedType::numeric_bit_unsigned;
        case Name::signed_:
        case Name::unresolved_signed:
            return ProvidedType::numeric_bit_signed;
        default: return ProvidedType::none;
        }
    case Package::std_textio:
        switch (entry.name) {
        case Name::line: return ProvidedType::textio_line;
        case Name::text: return ProvidedType::textio_text;
        case Name::side: return ProvidedType::textio_side;
        case Name::width: return ProvidedType::textio_width;
        case Name::line_vector: return ProvidedType::textio_line_vector;
        default: return ProvidedType::none;
        }
    default:
        return ProvidedType::none;
    }
}

vhdl::BuiltinTypeIdentity vhdl_native_builtin_type(
    const VhdlNativeIntrinsic& entry) noexcept
{
    using vhdl::BuiltinTypeIdentity;
    if (!is_type(entry) || entry.package_id != Package::ieee_std_logic_1164) {
        return BuiltinTypeIdentity::none;
    }
    switch (entry.name) {
    case Name::std_ulogic_vector:
        return BuiltinTypeIdentity::ieee_std_logic_1164_std_ulogic_vector;
    case Name::std_logic_vector:
        return BuiltinTypeIdentity::ieee_std_logic_1164_std_logic_vector;
    default:
        return BuiltinTypeIdentity::none;
    }
}

vhdl::BuiltinOperatorIdentity vhdl_native_operator(
    const VhdlNativeIntrinsic& entry) noexcept
{
    using vhdl::BuiltinOperatorIdentity;
    if (entry.kind != VhdlNativeKind::function
        || (entry.package_id != Package::ieee_std_logic_1164
            && entry.package_id != Package::ieee_numeric_std)) {
        return BuiltinOperatorIdentity::none;
    }
    switch (entry.name) {
    case Name::op_not: return BuiltinOperatorIdentity::ieee_std_logic_1164_not;
    case Name::op_and: return BuiltinOperatorIdentity::ieee_std_logic_1164_and;
    case Name::op_or: return BuiltinOperatorIdentity::ieee_std_logic_1164_or;
    case Name::op_nand: return BuiltinOperatorIdentity::ieee_std_logic_1164_nand;
    case Name::op_nor: return BuiltinOperatorIdentity::ieee_std_logic_1164_nor;
    case Name::op_xor: return BuiltinOperatorIdentity::ieee_std_logic_1164_xor;
    case Name::op_xnor: return BuiltinOperatorIdentity::ieee_std_logic_1164_xnor;
    default: return BuiltinOperatorIdentity::none;
    }
}

template const VhdlNativeIntrinsic* vhdl_native_type_of<CompiledDesign>(
    const CompiledDesign&, TypeId);
template const VhdlNativeIntrinsic* vhdl_native_base_type_of<CompiledDesign>(
    const CompiledDesign&, TypeId);
template bool vhdl_native_logic9<CompiledDesign>(const CompiledDesign&, TypeId);

VhdlNativeDomain vhdl_native_domain(const VhdlNativeIntrinsic& entry) noexcept
{
    if (vhdl_native_provided_type(entry) == vhdl::ProvidedType::none) {
        return VhdlNativeDomain::none;
    }
    switch (entry.package_id) {
    case Package::ieee_std_logic_1164:
    case Package::ieee_numeric_std:
        return VhdlNativeDomain::logic9;
    case Package::ieee_numeric_bit:
        return VhdlNativeDomain::bit2;
    default:
        return VhdlNativeDomain::none;
    }
}

} // namespace fsim::semantic
