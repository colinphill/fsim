// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

void Lowerer::lower_fork(const Statement& statement) {
  if (active_function_ || active_task_ || active_procedure_) {
    report(
        "FSIM-ELAB-107",
        "bounded fork branches cannot escape a callable frame",
        statement.span);
    return;
  }

  auto outer_locals = locals_;
  auto outer_string_locals = string_locals_;
  auto outer_container_locals = container_locals_;
  auto outer_signed = local_signed_;
  auto outer_ranges = local_ranges_;
  auto outer_integer_ranges = local_integer_ranges_;
  auto outer_members = local_members_;
  auto outer_types = local_types_;
  local_scope_.push_back(block_scope_name(statement));
  initialize_variables(statement.declarations);

  const auto fork_instruction =
      static_cast<InstructionIndex>(process_.operations.size());
  process_.operations.emplace_back(Fork{});
  const auto continuation_jump =
      static_cast<InstructionIndex>(process_.operations.size());
  process_.operations.emplace_back(Jump{});

  std::vector<InstructionIndex> branches;
  branches.reserve(statement.statements.size());
  auto outer_loop_controls = std::move(loop_controls_);
  for (const auto& branch : statement.statements) {
    branches.push_back(
        static_cast<InstructionIndex>(process_.operations.size()));
    loop_controls_.clear();
    lower_statement(branch);
    process_.operations.emplace_back(ForkEnd{});
  }
  loop_controls_ = std::move(outer_loop_controls);

  const auto continuation =
      static_cast<InstructionIndex>(process_.operations.size());
  process_.operations[continuation_jump] = Jump{continuation};
  auto join = runtime::simir::ForkJoinKind::all;
  switch (statement.fork_join_kind) {
  case frontend::ForkJoinKind::All:
    join = runtime::simir::ForkJoinKind::all;
    break;
  case frontend::ForkJoinKind::Any:
    join = runtime::simir::ForkJoinKind::any;
    break;
  case frontend::ForkJoinKind::None:
    join = runtime::simir::ForkJoinKind::none;
    break;
  }
  process_.operations[fork_instruction] =
      Fork{std::move(branches), join};

  local_scope_.pop_back();
  locals_ = std::move(outer_locals);
  string_locals_ = std::move(outer_string_locals);
  container_locals_ = std::move(outer_container_locals);
  local_signed_ = std::move(outer_signed);
  local_ranges_ = std::move(outer_ranges);
  local_integer_ranges_ = std::move(outer_integer_ranges);
  local_members_ = std::move(outer_members);
  local_types_ = std::move(outer_types);
}

}  // namespace fsim::elaboration
