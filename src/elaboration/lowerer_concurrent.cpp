// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "lowerer_driver_regions.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Process Lowerer::lower_concurrent(
    const Statement& statement,
    const frontend::Language language,
    const std::string& name,
    const std::size_t order) {
  process_ = Process{};
  language_ = language;
  hierarchy_ = name;
  process_kind_ = ProcessKind::VhdlProcess;
  next_register_ = 0;
  next_string_register_ = 0;
  next_container_register_ = 0;
  register_widths_.clear();
  register_domains_.clear();
  locals_.clear();
  string_locals_.clear();
  container_locals_.clear();
  local_signed_.clear();
  local_ranges_.clear();
  local_integer_ranges_.clear();
  local_members_.clear();
  local_types_.clear();
  declaration_registers_.clear();
  debug_local_names_.clear();
  local_scope_.clear();
  loop_controls_.clear();
  process_.id = static_cast<ProcessId>(design_.processes_.size());
  process_.name = name + "."
      + (statement.label.empty()
             ? "concurrent_" + std::to_string(order)
             : statement.label);
  initialize_function_support();
  initialize_task_support();
  initialize_procedure_support();
  emit_debug_point(DebugPointKind::process_entry, statement.span);

  std::set<std::string> dependencies;
  collect_wildcard_identifiers(
      std::vector<Statement>{statement}, dependencies);
  for (const auto& dependency : dependencies) {
    if (const auto found = signals_.find(dependency);
        found != signals_.end()) {
      process_.static_sensitivity.push_back(
          {found->second, runtime::simir::EdgeKind::any});
    }
  }
  if (!statement.vhdl_guarded_assignment) {
    lower_statement(statement);
  } else if (!statement.vhdl_guard.valid()) {
    report(
        "FSIM-ELAB-VHDLGUARD-001",
        "a guarded concurrent assignment requires an enclosing "
        "Boolean-guarded block",
        statement.span);
  } else {
    const auto guard = lower_condition(
        statement.vhdl_guard,
        "FSIM-ELAB-VHDLGUARD-001",
        "guarded concurrent assignment");
    if (guard) {
      const auto branch = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Branch{
          *guard, 0, 0, UnknownBranchPolicy::error});
      const auto active_start = static_cast<InstructionIndex>(
          process_.operations.size());
      auto active = statement;
      active.vhdl_guarded_assignment = false;
      lower_statement(active);
      const auto exit = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Jump{0});
      const auto inactive_start = static_cast<InstructionIndex>(
          process_.operations.size());
      const auto find_assignment = [&](const auto& self,
                                       const Statement& node)
          -> const Statement* {
        if (node.kind == StatementKind::Assignment) return &node;
        for (const auto& child : node.statements) {
          if (const auto* found = self(self, child)) return found;
        }
        for (const auto& child : node.else_statements) {
          if (const auto* found = self(self, child)) return found;
        }
        for (const auto& alternative : node.case_alternatives) {
          for (const auto& child : alternative.statements) {
            if (const auto* found = self(self, child)) return found;
          }
        }
        return nullptr;
      };
      if (const auto* source =
              find_assignment(find_assignment, statement)) {
        auto disconnect = *source;
        disconnect.vhdl_guarded_assignment = false;
        disconnect.vhdl_unaffected = false;
        disconnect.value = {};
        disconnect.vhdl_waveform.clear();
        disconnect.vhdl_waveform.push_back(
            frontend::VhdlWaveformElement{
                {}, source->delay, true, source->span});
        lower_assignment(disconnect);
      } else {
        report(
            "FSIM-ELAB-VHDLGUARD-001",
            "guarded concurrent-assignment HIR has no assignment leaf",
            statement.span);
      }
      const auto end = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations[branch] = Branch{
          *guard,
          active_start,
          inactive_start,
          UnknownBranchPolicy::error};
      process_.operations[exit] = Jump{end};
    }
  }
  if (!process_.static_sensitivity.empty()) {
    process_.operations.emplace_back(WaitSensitivity{});
    process_.operations.emplace_back(Jump{0});
  } else {
    process_.operations.emplace_back(Halt{});
  }
  lower_pending_tasks();
  lower_pending_procedures();
  lower_pending_functions();
  validate_read_only_signal_writes(statement.span);
  process_.register_count = next_register_;
  process_.string_register_count = next_string_register_;
  process_.container_register_count = next_container_register_;
  process_.register_value_kinds.reserve(register_domains_.size());
  for (const auto domain : register_domains_) {
    process_.register_value_kinds.push_back(value_kind(domain));
  }
  process_.driver_regions = collect_driver_regions(
      process_, register_widths_);
  next_register_ = 0;
  next_string_register_ = 0;
  next_container_register_ = 0;
  register_widths_.clear();
  register_domains_.clear();
  locals_.clear();
  string_locals_.clear();
  container_locals_.clear();
  local_signed_.clear();
  local_ranges_.clear();
  local_integer_ranges_.clear();
  local_members_.clear();
  local_types_.clear();
  declaration_registers_.clear();
  debug_local_names_.clear();
  local_scope_.clear();
  loop_controls_.clear();
  return std::move(process_);
}

}  // namespace fsim::elaboration
