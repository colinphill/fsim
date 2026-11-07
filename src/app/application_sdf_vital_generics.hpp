// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_cell_resolution.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/project/project.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

/// SDF backannotation of VITAL models (IEEE 1076.4-2000 5.1.1, 5.2).
///
/// A VITAL model takes its timing only through its backannotation timing
/// generics, which are constants of elaboration. The annotator therefore
/// works in two steps: this module reads the SDF files, finds the VHDL cells
/// they name in the elaborated design, builds the generic names and delay
/// lists the VITAL SDF map prescribes (5.2.7), and the build elaborates the
/// design again with those annotations, so the model is lowered with the
/// annotated generics (see elaboration::TimingGenericAnnotation).

/// Copies the SDF files and delay selection of a command line into the
/// configuration the build reads (`elaborate --sdf`).
void add_sdf_vital_inputs(
    project::Config& config, const cli::Invocation& invocation);

/// The annotations the VITAL cells of the configured SDF files (and of the
/// design's `$sdf_annotate` requests) make. Reports the errors of 5.2 that
/// belong to VITAL: a generic name that cannot be a VHDL identifier, a
/// generic the entity does not declare is reported by the elaborator, a delay
/// list longer than the generic holds, and the multiple source mode that
/// cannot be set up. Errors of the SDF file or of the instance paths are left to
/// the SDF session, which reports them for every cell.
[[nodiscard]] bool plan_sdf_vital_timing_generics(
    const elaboration::ElaboratedDesign& design,
    const project::Config& config, diagnostic::Engine& diagnostics,
    std::vector<project::TimingGenericAnnotation>& annotations);

/// Removes the VHDL instances from the targets of resolved SDF cells and
/// returns how many it removed: the SDF session annotates Verilog and
/// SystemVerilog targets, while VHDL cells are annotated through their
/// generics at elaboration.
[[nodiscard]] std::size_t remove_sdf_vital_targets(
    std::vector<SdfResolvedCell>& cells);

/// The generic name condition suffix of IEEE 1076.4-2000 5.2.7.3.2 for the
/// text of an SDF condition expression; empty if the expression has no
/// translation to a legal VHDL identifier part.
[[nodiscard]] std::string sdf_vital_condition_suffix(std::string_view text);

} // namespace fsim::app
