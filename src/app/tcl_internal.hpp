// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"
#include "fsim/app/sdf_control.hpp"
#include "tcl.hpp"
#include "tcl_references.hpp"

#include <array>
#include <sstream>
#include <string_view>

#if defined(FSIM_HAS_TCL)
#include <tcl.h>
#endif

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

class TclTranscript;

// The configuration that fsim::config, fsim::coverage and fsim::bind
// change. `layer` is that of the script being sourced, else session.
struct TclConfigurationState {
    std::shared_ptr<project::Configuration> configuration;
    project::ConfigLayer layer { project::ConfigLayer::session };
    std::filesystem::path script;
};

struct TclContext {
    cli::Invocation invocation;
    project::Config config;
    project::Config initial_config;
    diagnostic::Engine& diagnostics;
    Tcl_Interp* interpreter;
    std::ostream& output;
    std::ostream& error;
    std::optional<BuiltProject> built;
    std::unique_ptr<Simulation> simulation;
    std::optional<SimulationEngine> simulation_engine;
    // The --vpi, --vhpi and --sv-lib libraries; destroyed before the
    // simulation they are loaded into.
    std::unique_ptr<application_detail::ForeignLibraries> foreign;
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
    std::unique_ptr<TclReferenceTable> references;
    TclTranscript* transcript { };
    std::vector<std::filesystem::path> sdf_resolved_paths { };
    std::optional<elaboration::ElaboratedDesign> sdf_baseline_design { };
    std::string sdf_base_cache_key { };
    std::vector<std::string> sdf_base_specialization_keys { };
    TclConfigurationState configuration { };
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
int transcript_command(
    TclContext&, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[]);

int command_error(Tcl_Interp*, std::string_view message);
int command_diagnostic_error(
    TclContext&, Tcl_Interp*, std::string_view fallback);
bool ensure_built(TclContext&, Tcl_Interp*);
bool ensure_simulation(TclContext&, Tcl_Interp*,
    std::optional<SimulationEngine> requested_engine = std::nullopt);
const semantic::design::DesignIr* current_design_ir(const TclContext&);

// Native workspace commands call this only after successful publication.
void catalog_changed(
    TclContext&, std::string_view library, TclCatalogMutation mutation);

// The configuration commands, on a state of their own: the Tcl session
// reaches them through its catalog, the configuration loader of the other
// commands registers them alone.
int configuration_settings_command(
    TclConfigurationState&, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[]);
int coverage_settings_command(
    TclConfigurationState&, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[]);
int bind_settings_command(
    TclConfigurationState&, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[]);

// Creates ::fsim::config, ::fsim::coverage and ::fsim::bind on `state`
// alone, for an interpreter that runs only configuration scripts.
bool register_configuration_commands(Tcl_Interp*, TclConfigurationState&);

// Sources the configuration scripts of `workspace`, each in its layer.
// FSIM-TCL-CFG-001 reports the first that fails.
bool source_configuration_scripts(TclConfigurationState&, Tcl_Interp*,
    const std::filesystem::path& workspace, diagnostic::Engine&);

// Loads a snapshot transactionally, preserving the previous session on error.
int reload_snapshot(
    TclContext&, Tcl_Interp*, std::string_view snapshot);

#endif

} // namespace fsim::app::tcl_detail
