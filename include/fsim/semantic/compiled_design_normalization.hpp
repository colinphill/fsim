// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

namespace fsim::semantic {

/// Canonicalizes dependency-independent HIR expressions and annotates every
/// residual expression and generate with the semantic actuals required during
/// specialization. Returns false when an HIR edge names an invalid identity.
[[nodiscard]] bool normalize_compiled_design(
    CompiledDesign& design) noexcept;

} // namespace fsim::semantic
