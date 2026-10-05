// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_workspace.hpp"
#include "application_workspace_systemc.hpp"

namespace fsim::app {
using namespace application_detail;

cli::Services make_cli_services(std::istream& input) {
  cli::Handler tcl =
      [&input](
          const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine& diagnostics,
          std::ostream& output,
          std::ostream& error) {
        return handle_tcl(
            invocation,
            config,
            diagnostics,
            input,
            output,
            error);
      };
  return {
      handle_check,
      handle_build,
      handle_run,
      tcl,
      std::move(tcl),
      handle_workspace_compile,
      handle_workspace_elaborate,
      handle_workspace_simulate,
      handle_workspace_systemc_compile,
      handle_workspace_systemc_link,
      handle_coverage_merge,
      handle_coverage_report,
      handle_workspace_library,
      handle_workspace_library,
      handle_workspace_library,
      handle_workspace_library,
      handle_workspace_library,
      handle_workspace_library};
}

cli::Services make_cli_services() {
  return make_cli_services(std::cin);
}

cli::Services make_stdio_cli_services() {
  auto services = make_cli_services();
  services.stdio_run = handle_run_stdio;
  services.stdio_simulate = handle_workspace_simulate_stdio;
  return services;
}


} // namespace fsim::app
