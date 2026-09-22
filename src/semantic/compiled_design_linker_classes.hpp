// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

namespace fsim::semantic {

[[nodiscard]] std::optional<UnitId> compiled_class_owner(
    const CompiledDesign& design,
    const sv::ClassDeclaration& declaration);

void append_compiled_class_references(
    const CompiledDesign& design,
    std::vector<CompiledReference>& references);

} // namespace fsim::semantic
