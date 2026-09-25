// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/semantic/compiled_design.hpp"

namespace fsim::app::application_detail {

/// Bind newly parsed class uses to an immutable compiled package environment.
/// Only source-owned syntax is annotated. Imported declarations and executable
/// bodies remain in their original compiled HIR throughout source analysis.
[[nodiscard]] bool prepare_compiled_systemverilog_environment(
    frontend::ParsedDesign&, const semantic::CompiledDesign&,
    diagnostic::Engine&);

/// Resolve package declaration and type identities after the source HIR and
/// the compiled environment have been linked into one identity domain.
[[nodiscard]] bool resolve_compiled_systemverilog_environment(
    semantic::CompiledDesign&, diagnostic::Engine&);

} // namespace fsim::app::application_detail
