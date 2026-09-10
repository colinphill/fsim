// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"
#include "tcl.hpp"
#include "fsim/app/sdf_control.hpp"

#include <array>
#include <sstream>

#if defined(FSIM_HAS_TCL)
#include <tcl.h>
#endif

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

struct TclContext {
    const cli::Invocation& invocation;
    project::Config config;
    diagnostic::Engine& diagnostics;
    Tcl_Interp* interpreter;
    std::ostream& output;
    std::ostream& error;
    std::optional<BuiltProject> built;
    std::unique_ptr<Simulation> simulation;
    std::optional<SimulationEngine> simulation_engine;
    std::unique_ptr<std::ostringstream> debug_output;
    std::unique_ptr<std::ostringstream> debug_error;
    std::unique_ptr<DebuggerControl> debugger;
    std::array<std::vector<std::string>, 4> callbacks;
    std::uint64_t signal_callback_token { };
    std::uint64_t safe_point_callback_token { };
    std::size_t callback_depth { };
    bool callbacks_attached { };
    bool lifecycle_started { };
    std::optional<std::string> callback_error;
    bool exit_requested { };
    int exit_code { };
    SdfControlRequest sdf_request { };
    std::shared_ptr<const SdfControlApplication> sdf_control;
};

Tcl_Size tcl_size(std::size_t value);
Tcl_WideInt tcl_wide_size(std::size_t value);
std::string_view assertion_severity_name(
    runtime::simir::AssertionSeverity severity);
diagnostic::Severity assertion_diagnostic_severity(
    runtime::simir::AssertionSeverity severity);
int version_command(
    void* client_data,
    Tcl_Interp* interpreter,
    Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) noexcept;
int fsim_command(
    void* client_data,
    Tcl_Interp* interpreter,
    Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) noexcept;

#endif

} // namespace fsim::app::tcl_detail
