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

/// While an instance is alive on the calling thread, the link-time VHDL
/// resolution keeps the declaration and type the compile-time typing stored
/// on an expression (vhdl::Expression::interpretation set; IEEE 1076-2019
/// 12.5) instead of recomputing them. The normalization that follows
/// analyze_vhdl_names, before the analyzed units are stamped, runs under it;
/// later links of unstamped units recompute (their typed results are not
/// anchored).
class VhdlTypingAuthority {
public:
    VhdlTypingAuthority() noexcept;
    ~VhdlTypingAuthority();
    VhdlTypingAuthority(const VhdlTypingAuthority&) = delete;
    VhdlTypingAuthority& operator=(const VhdlTypingAuthority&) = delete;
};

/// True while a VhdlTypingAuthority is alive on the calling thread.
[[nodiscard]] bool vhdl_typing_authority_active() noexcept;

/// True when the design declares a VHDL function with this operator
/// designator outside the STD and IEEE libraries, so the operator cannot be
/// evaluated as the predefined one.
[[nodiscard]] bool vhdl_operator_overloaded_by_design(
    const CompiledDesign& design, std::string_view operation);

} // namespace fsim::semantic
