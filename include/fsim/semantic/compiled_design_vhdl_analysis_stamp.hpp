// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <cstdint>
#include <span>
#include <string>

// Compile-time analysis results that links keep (gate BD, owner 11-19).
// A unit compile-time analysis resolved is stamped (vhdl::Unit::analyzed);
// its stored references are authoritative, and its references into other
// library units are kept as anchors that links rebind by position.
namespace fsim::semantic {

/// Stamps every VHDL unit of `design` among `source_units` (the units this
/// compilation analyzed) that is not a compiler-served standard package,
/// recording its references into other library units as anchors
/// (vhdl::DeclarationAnchor). Targets in compiler-served packages get no
/// anchor and are resolved at link as before.
void stamp_vhdl_analysis(CompiledDesign& design, std::span<const UnitId> source_units);

enum class AnchorRebind : std::uint8_t {
    /// While a library object is projected out of a design.
    projection,
    /// While linked designs are finalized.
    link,
};

/// Sets every anchored slot of the stamped units of `design` to the
/// declaration its anchor denotes in the design. Returns an empty string on
/// success. A link (or a projection keeping the target unit) whose target
/// unit is missing or differs from the one the unit was analyzed against
/// (digest) returns the error text: the unit is obsolete (IEEE 1076-2019
/// 13.5) and must be analyzed again; the linker reports it as
/// FSIM-SEM-0003. Only an inconsistency of the design itself (an ordinal or
/// item out of range for a matching digest) is a std::logic_error.
[[nodiscard]] std::string rebind_vhdl_declaration_anchors(
    CompiledDesign& design, AnchorRebind mode);

/// Test-only verifier (FSIM_VERIFY_VHDL_ANALYSIS=1; a difference fails the
/// link with FSIM-SEM-0004): resolves a copy of
/// `design` with every stamp cleared and compares the stored references of
/// each stamped unit with the recomputed ones. Empty when they agree;
/// otherwise one line per difference (unit, slot, stored, recomputed).
[[nodiscard]] std::string verify_stamped_vhdl_analysis(const CompiledDesign& design);

/// The first stamped unit of `design` whose analysis anchors a library unit
/// the design lacks (IEEE 1076-2019 13.5), described for a diagnostic; empty
/// when every anchor has its target. Links in parts leave such a reference
/// unbound until a later link brings its target; a design that is
/// elaborated must hold every target.
[[nodiscard]] std::string unbound_vhdl_anchor_error(const CompiledDesign& design);

} // namespace fsim::semantic
