// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

[[nodiscard]] bool is_process_type(
    const frontend::Type* type) {
  return type != nullptr && type->spelling == "process";
}

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_process_expression(
    const Expression& expression) {
  if (language_ != frontend::Language::SystemVerilog2017
      || expression.kind != ExpressionKind::Call) {
    return {};
  }
  if (expression.text == "process::self") {
    if (!expression.operands.empty()) {
      report(
          "FSIM-ELAB-SVPROCESS-001",
          "process::self() does not accept arguments",
          expression.span);
      return std::nullopt;
    }
    const auto destination = allocate_register(
        64, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(ProcessSelf{destination});
    return destination;
  }
  if (expression.text != ".status"
      && expression.text != ".completed") {
    return {};
  }
  if (expression.operands.size() != 1U
      || expression.operands.front().kind
          != ExpressionKind::Identifier
      || !is_process_type(
          object_type(expression.operands.front().text))) {
    report(
        "FSIM-ELAB-SVPROCESS-001",
        "process status queries require a direct process receiver "
        "and no arguments",
        expression.span);
    return std::nullopt;
  }
  const auto& receiver = expression.operands.front();
  const auto source = lower_expression(
      receiver, 64, object_type(receiver.text));
  if (!source || register_width(*source) != 64U) {
    report(
        "FSIM-ELAB-SVPROCESS-001",
        "process status receiver has no 64-bit handle value",
        receiver.span);
    return std::nullopt;
  }
  if (expression.text == ".status") {
    const auto destination = allocate_register(
        32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        ProcessStatusQuery{destination, *source});
    return destination;
  }
  const auto destination = allocate_register(
      1, frontend::ValueDomain::Bit2);
  process_.operations.emplace_back(
      ProcessCompleted{destination, *source});
  return destination;
}

bool Lowerer::lower_process_method_statement(
    const Statement& statement) {
  if (language_ == frontend::Language::SystemVerilog2017
      && statement.kind == StatementKind::TaskCall) {
    const auto separator = statement.task_name.rfind('.');
    const auto method = separator == std::string::npos
        ? std::string_view{}
        : std::string_view{statement.task_name}.substr(separator);
    if (method != ".await" && method != ".kill") {
      return false;
    }
    const auto receiver_name = statement.task_name.substr(0, separator);
    if (!statement.task_arguments.empty()
        || receiver_name.find('.') != std::string::npos
        || receiver_name.find("::") != std::string::npos
        || !is_process_type(object_type(receiver_name))) {
      report(
          "FSIM-ELAB-SVPROCESS-002",
          "process await/kill methods require a direct process receiver "
          "and no arguments",
          statement.span);
      return true;
    }
    const Expression receiver{
        ExpressionKind::Identifier,
        receiver_name,
        {},
        statement.span};
    const auto source = lower_expression(
        receiver, 64, object_type(receiver_name));
    if (!source || register_width(*source) != 64U) {
      report(
          "FSIM-ELAB-SVPROCESS-002",
          "process method receiver has no 64-bit handle value",
          statement.span);
      return true;
    }
    if (method == ".await") {
      process_.operations.emplace_back(ProcessAwait{*source});
    } else {
      process_.operations.emplace_back(ProcessKill{*source});
    }
    return true;
  }
  const auto& call = statement.value;
  if (language_ != frontend::Language::SystemVerilog2017
      || call.kind != ExpressionKind::Call
      || (call.text != ".await" && call.text != ".kill")) {
    return false;
  }
  if (call.operands.size() != 1U
      || call.operands.front().kind != ExpressionKind::Identifier
      || !is_process_type(
          object_type(call.operands.front().text))) {
    report(
        "FSIM-ELAB-SVPROCESS-002",
        "process await/kill methods require a direct process receiver "
        "and no arguments",
        call.span);
    return true;
  }
  const auto& receiver = call.operands.front();
  const auto source = lower_expression(
      receiver, 64, object_type(receiver.text));
  if (!source || register_width(*source) != 64U) {
    report(
        "FSIM-ELAB-SVPROCESS-002",
        "process method receiver has no 64-bit handle value",
        receiver.span);
    return true;
  }
  if (call.text == ".await") {
    process_.operations.emplace_back(ProcessAwait{*source});
  } else {
    process_.operations.emplace_back(ProcessKill{*source});
  }
  return true;
}

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
