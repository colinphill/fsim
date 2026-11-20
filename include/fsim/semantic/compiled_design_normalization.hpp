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

/// While an instance is alive on the calling thread, normalization leaves
/// the expressions of VHDL units that compile-time name analysis has not
/// resolved yet (vhdl::Unit::analyzed false) as written: their names are
/// resolved by visibility (IEEE 1076-2019 12.3, 12.4; analyze_vhdl_names)
/// before any of them is evaluated, so a folded name cannot hide a name that
/// is not visible. Links and environment installs between HIR build and name
/// analysis run under it; the normalization after name analysis folds.
class VhdlFoldingDeferral {
public:
    VhdlFoldingDeferral() noexcept;
    ~VhdlFoldingDeferral();
    VhdlFoldingDeferral(const VhdlFoldingDeferral&) = delete;
    VhdlFoldingDeferral& operator=(const VhdlFoldingDeferral&) = delete;
};

/// True when the design declares a VHDL function with this operator
/// designator outside the STD and IEEE libraries, so the operator cannot be
/// evaluated as the predefined one.
[[nodiscard]] bool vhdl_operator_overloaded_by_design(
    const CompiledDesign& design, std::string_view operation);

} // namespace fsim::semantic
