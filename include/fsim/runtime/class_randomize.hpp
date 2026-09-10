// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/constraint_solver.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fsim::runtime {

using SystemVerilogClassRandomizeVariables = SystemVerilogConstraintVariables;

using SystemVerilogClassConstraintConfigurator = std::function<void(
    SystemVerilogConstraintSolver&,
    const SystemVerilogClassRandomizeVariables&)>;

enum class SystemVerilogClassRandomizeSelection : std::uint8_t {
  EnabledProperties,
  NoProperties,
};

struct SystemVerilogClassRandomizeRequest {
  /// Empty selects every enabled rand/randc property. A nonempty list selects
  /// only the named enabled random properties; every other packed property is
  /// still visible to constraints as its current singleton value.
  std::vector<std::string> variable_list;
  /// The explicit no-properties mode implements randomize(null): all packed
  /// members are fixed state variables, constraints are checked, and neither
  /// object state nor its random stream is advanced.
  SystemVerilogClassRandomizeSelection selection{
      SystemVerilogClassRandomizeSelection::EnabledProperties};
  /// Optional exact finite domains keyed by canonical identity or unique
  /// property suffix. This carries enum/restricted profiles without folding
  /// them into host integers and invalidates an incompatible randc cycle.
  std::map<std::string, std::vector<PackedLogic4>, std::less<>>
      property_domains;
  SystemVerilogClassConstraintConfigurator class_constraints;
  SystemVerilogClassConstraintConfigurator inline_constraints;
  SystemVerilogConstraintSolverLimits limits;
  std::string call_identity;
};

struct SystemVerilogClassRandomizeResult {
  SystemVerilogConstraintSolveStatus status{
      SystemVerilogConstraintSolveStatus::Unsatisfiable};
  SystemVerilogConstraintResource exhausted_resource{
      SystemVerilogConstraintResource::None};
  std::uint64_t search_steps{};
  std::uint64_t clause_evaluations{};

  [[nodiscard]] std::uint32_t language_result() const noexcept {
    return status == SystemVerilogConstraintSolveStatus::Satisfied ? 1U : 0U;
  }
};

/// Execute one object randomize transaction. Solver construction, class and
/// inline constraint registration, and complete-assignment validation precede
/// the final property/revision commit. Unsatisfiable and resource-exhausted
/// attempts therefore publish no partial object state.
[[nodiscard]] SystemVerilogClassRandomizeResult
randomize_systemverilog_class_object(
    SystemVerilogClassHeap& heap,
    SystemVerilogClassHandle handle,
    const SystemVerilogClassRandomizeRequest& request);

}  // namespace fsim::runtime
