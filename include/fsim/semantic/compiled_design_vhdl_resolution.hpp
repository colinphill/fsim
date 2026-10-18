// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

namespace fsim::semantic {

/// The context-typed resolution pass over a linked design's VHDL HIR (IEEE
/// 1076-2008 12.5): each complete context (an assignment, a condition, a
/// return value, an object initializer) types its expression tree once, and
/// the function overloads and enumeration literals it selects are recorded
/// in `vhdl::Expression::resolved_declaration`. An expression the pass
/// cannot decide is left unset. Returns false only when the design's lookup
/// indexes cannot be refreshed.
[[nodiscard]] bool resolve_vhdl_expression_types(CompiledDesign& design);

} // namespace fsim::semantic
