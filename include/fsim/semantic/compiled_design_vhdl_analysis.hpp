// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/semantic/compiled_design.hpp"

#include "fsim/support/symbol.hpp"

#include <span>

// Compile-time VHDL name analysis (gate BD build D3, IEEE 1076-2019 12.1-12.4
// and 8.3): every name of a newly analyzed unit that visibility resolves is
// resolved once, through declarative-region symbol tables keyed by interned
// symbols, and the result is stored in the unit's HIR.
namespace fsim::semantic {

/// A primary unit of a library that the design holds no HIR of (a unit
/// compiled in another VHDL edition, known by its workspace metadata): an
/// expanded name L.U may denote it (13.1, 8.3); nothing it declares is
/// visible.
struct VhdlAvailablePrimary {
    support::SymbolId library;
    support::SymbolId name;
};

/// Resolves every visibility-resolved vhdl::Name of the VHDL units `units` of
/// `design` (the units this compilation analyzed; every other unit of
/// `design` — standard libraries, mapped libraries, primary units of these
/// secondary units — is visible to them as already analyzed):
///  - a name denoting one non-overloadable declaration gets `selected`;
///  - a name denoting overloadable declarations (subprograms, enumeration
///    literals) gets every visible one in `overloads` (`selected` too when
///    exactly one is visible); choosing among them by type is 12.5's
///    business (VhdlResolution), not this pass's.
/// Names resolved by context rather than visibility (record element and
/// formal designators, attribute designators, choices of record aggregates,
/// a physical unit after an abstract literal, labels at their statements,
/// architecture names of binding indications) are left unchanged.
/// Reports FSIM-VHDL-NAME-001 (no visible declaration), FSIM-VHDL-NAME-002
/// (ambiguous: potentially visible through two use clauses, 12.4) and
/// FSIM-VHDL-NAME-003 (an expanded name's prefix has no such member, 8.3).
/// Returns false when it reported an error.
/// `bound_labels`: the instance labels (last path part) that project
/// bindings rebind at elaboration (project::Binding): a direct entity
/// instantiation with such a label names its target there, so the pass
/// leaves it unresolved.
[[nodiscard]] bool analyze_vhdl_names(CompiledDesign& design,
    std::span<const UnitId> units, diagnostic::Engine& diagnostics,
    std::span<const VhdlAvailablePrimary> available = { },
    std::span<const support::SymbolId> bound_labels = { });

} // namespace fsim::semantic
