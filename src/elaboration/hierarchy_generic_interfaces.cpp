// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {

void HierarchyBuilder::validate_vhdl_generic_type(
    const frontend::ParameterDeclaration& generic) {
    const bool supported_packed =
        generic.type.packed_range
        && generic.type.packed_members.empty()
        && generic.type.width().value_or(0) <= 64
        && (generic.type.domain == frontend::ValueDomain::Bit2
            || generic.type.domain == frontend::ValueDomain::Logic4
            || generic.type.domain == frontend::ValueDomain::Logic9);
    const bool supported_time =
        generic.type.nominal_type == "@builtin:time"
        && generic.type.domain == frontend::ValueDomain::Integer
        && generic.type.width().value_or(0) == 64;
    const bool supported_logic_scalar =
        !generic.type.packed_range
        && generic.type.width().value_or(0) == 1
        && (generic.type.domain == frontend::ValueDomain::Logic4
            || generic.type.domain == frontend::ValueDomain::Logic9);
    const bool supported_unconstrained_builtin_array =
        generic.type.spelling == "bit_vector"
        || generic.type.spelling == "std_logic_vector"
        || generic.type.spelling == "std_ulogic_vector"
        || generic.type.spelling == "signed"
        || generic.type.spelling == "unsigned";
    const bool supported_composite =
        (generic.type.vhdl_array
         || !generic.type.packed_members.empty())
        && generic.type.width().value_or(0) != 0;
    if ((generic.type.packed_range
         && !supported_packed && !supported_time
         && !supported_composite)
        || (!generic.type.packed_range
            && (generic.type.vhdl_array
                || !generic.type.packed_members.empty())
            && !supported_composite)
        || (generic.type.named_type.empty()
            && !supported_packed
            && !supported_time
            && !supported_composite
            && !supported_logic_scalar
            && !supported_unconstrained_builtin_array
            && generic.type.domain != frontend::ValueDomain::Integer
            && generic.type.domain != frontend::ValueDomain::Boolean
            && generic.type.domain != frontend::ValueDomain::Bit2)) {
        report(
            "FSIM-ELAB-GENERIC-010",
            "VHDL generic '" + generic.name
                + "' must resolve to a supported "
            "scalar, physical-time, packed, or statically constrained "
            "composite type (resolved type '" + generic.type.spelling
                + "', width "
                + std::to_string(generic.type.width().value_or(0))
                + ", domain "
                + std::to_string(static_cast<unsigned>(generic.type.domain))
                + ")",
            generic.span);
    }
}

}  // namespace fsim::elaboration
