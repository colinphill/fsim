// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::runtime {

/// The lifetime boundary at which a UVM service is unique. A simulation scope
/// is one independently constructed simulation context, not the host process.
enum class SystemVerilogUvmStateScope : std::uint8_t {
  Simulation,
  Root,
};

/// Public ownership contract for the UVM state exposed by one simulation.
/// Registry, factory, resource, configuration, callback, plusarg, report, and
/// phase-graph, and sequence-identity state is deliberately shared by every
/// UVM root in that simulation. Equal component paths, phase participation,
/// and sequence identities remain qualified by explicit root handles.
/// Destroying the simulation destroys all of these services; no process-global
/// UVM state is retained for a subsequent simulation.
struct SystemVerilogUvmOwnershipContract {
  SystemVerilogUvmStateScope type_registry;
  SystemVerilogUvmStateScope factory;
  SystemVerilogUvmStateScope resources;
  SystemVerilogUvmStateScope configuration;
  SystemVerilogUvmStateScope callbacks;
  SystemVerilogUvmStateScope command_line;
  SystemVerilogUvmStateScope reporting;
  SystemVerilogUvmStateScope phases;
  SystemVerilogUvmStateScope objections;
  SystemVerilogUvmStateScope tlm1;
  SystemVerilogUvmStateScope tlm2;
  SystemVerilogUvmStateScope sequences;
  SystemVerilogUvmStateScope component_paths;
};

inline constexpr SystemVerilogUvmOwnershipContract
    kSystemVerilogUvmOwnershipContract{
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Simulation,
        SystemVerilogUvmStateScope::Root};

}  // namespace fsim::runtime
