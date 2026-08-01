// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

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
    if ((generic.type.packed_range && !supported_packed)
        || !generic.type.packed_members.empty()
        || (generic.type.named_type.empty()
            && generic.type.domain != frontend::ValueDomain::Integer
            && generic.type.domain != frontend::ValueDomain::Boolean
            && generic.type.domain != frontend::ValueDomain::Bit2)) {
        report(
            "FSIM-ELAB-GENERIC-010",
            "a bounded VHDL generic subtype must resolve to a supported "
            "scalar or up-to-64-bit packed type",
            generic.span);
    }
}

}  // namespace fsim::elaboration
