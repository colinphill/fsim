// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "tcl_completion.hpp"

#include <cstddef>
#include <string_view>

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

struct TclContext;

/// Build a bounded read-only session snapshot and complete one UTF-8 Tcl
/// buffer. The buffer is parsed but never evaluated.
tcl_completion::CompletionResult complete_tcl(
    TclContext&,
    std::string_view buffer,
    std::size_t cursor_byte,
    tcl_completion::RequestContext context = tcl_completion::RequestContext::interactive_console);

/// Recompute the current data generations before a consumer applies a result.
tcl_completion::Generations tcl_completion_generations(TclContext&);

#endif

} // namespace fsim::app::tcl_detail
