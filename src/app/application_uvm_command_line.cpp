// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include <algorithm>
#include <limits>

namespace fsim::app::application_detail {

bool apply_uvm_command_line(
    Simulation& simulation,
    const std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics) {
  try {
    simulation.uvm_command_line().apply(plusargs);
    simulation.uvm_reports().set_default_verbosity(
        simulation.uvm_command_line().settings().initial_verbosity.value_or(
            200));
    return true;
  } catch (const runtime::SystemVerilogUvmCommandLineError& error) {
    const auto column = static_cast<std::uint32_t>(
        std::min<std::size_t>(
            error.argument_index() + 1,
            std::numeric_limits<std::uint32_t>::max()));
    diagnostics.error(
        "FSIM-UVM-CLI-001", error.what(),
        {"<command-line>", {1, column, 0}, {1, column, 0}});
  } catch (const std::exception& error) {
    diagnostics.error(
        "FSIM-UVM-CLI-001",
        "cannot apply UVM command-line settings: "
            + std::string{error.what()},
        {"<command-line>", {1, 1, 0}, {1, 1, 0}});
  }
  return false;
}

}  // namespace fsim::app::application_detail
