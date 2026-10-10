// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <string_view>

namespace fsim::semantic {

/// Canonicalizes dependency-independent HIR expressions and annotates every
/// residual expression and generate with the semantic actuals required during
/// specialization. Returns false when an HIR edge names an invalid identity.
[[nodiscard]] bool normalize_compiled_design(
    CompiledDesign& design) noexcept;

/// True when the design declares a VHDL function with this operator
/// designator outside the STD and IEEE libraries, so the operator cannot be
/// evaluated as the predefined one.
[[nodiscard]] bool vhdl_operator_overloaded_by_design(
    const CompiledDesign& design, std::string_view operation);

} // namespace fsim::semantic
