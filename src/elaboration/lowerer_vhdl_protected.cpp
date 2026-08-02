// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

struct ProtectedName {
  std::string object;
  std::string method;
};

std::optional<ProtectedName> protected_name(
    const std::string_view selected) {
  const auto separator = selected.find_last_of('.');
  if (separator == std::string_view::npos
      || separator == 0 || separator + 1 >= selected.size()) {
    return std::nullopt;
  }
  return ProtectedName{
      std::string{selected.substr(0, separator)},
      std::string{selected.substr(separator + 1)}};
}

bool contains_call(
    const std::vector<frontend::Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement.kind == frontend::StatementKind::ProcedureCall
        || contains_call(statement.statements)
        || contains_call(statement.else_statements)) {
      return true;
    }
    for (const auto& alternative : statement.case_alternatives) {
      if (contains_call(alternative.statements)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_protected_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  if ((expression.kind != ExpressionKind::Call
       && expression.kind != ExpressionKind::Identifier)
      || expression.text.starts_with("@")) {
    return ExpressionAttempt{};
  }
  const auto selected = protected_name(expression.text);
  if (!selected) {
    return ExpressionAttempt{};
  }
  const auto type_found = visible_types_.find(selected->object);
  if (type_found == visible_types_.end()
      || type_found->second == nullptr
      || !type_found->second->vhdl_protected) {
    return ExpressionAttempt{};
  }
  const auto& info = *type_found->second->vhdl_protected;
  std::vector<const frontend::FunctionDeclaration*> matches;
  for (const auto& function : info.functions) {
    if (function.name != selected->method
        || function.arguments.size()
            != expression.operands.size()) {
      continue;
    }
    bool compatible = true;
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
      if (!vhdl_expression_matches_type(
              expression.operands[index],
              function.arguments[index].type)) {
        compatible = false;
        break;
      }
    }
    if (compatible) {
      matches.push_back(&function);
    }
  }
  if (matches.size() != 1) {
    report(
        matches.empty()
            ? "FSIM-ELAB-VHPROTECTED-012"
            : "FSIM-ELAB-VHPROTECTED-013",
        matches.empty()
            ? "protected function call '" + expression.text
                + "' matches no public profile"
            : "protected function call '" + expression.text
                + "' is ambiguous",
        expression.span);
    return ExpressionAttempt{std::nullopt};
  }
  const auto& function = *matches.front();
  if (active_vhdl_protected_method_) {
    report(
        "FSIM-ELAB-VHPROTECTED-014",
        "re-entry into a protected method is outside the bounded "
        "execution policy",
        expression.span);
    return ExpressionAttempt{std::nullopt};
  }
  if (function.statements.size() != 1
      || function.statements.front().kind != StatementKind::Return
      || !function.statements.front().value.valid()) {
    report(
        "FSIM-ELAB-VHPROTECTED-015",
        "a bounded protected function body must contain one direct "
        "value-return statement",
        function.span);
    return ExpressionAttempt{std::nullopt};
  }
  const auto result_width = function.return_type.width();
  if (!result_width || *result_width == 0 || *result_width > 64
      || *result_width != expected_width
      || (expected_type != nullptr
          && !vhdl_callable_type_matches(
              *expected_type, function.return_type))) {
    report(
        "FSIM-ELAB-VHPROTECTED-016",
        "protected function result is incompatible with its context",
        expression.span);
    return ExpressionAttempt{std::nullopt};
  }

  std::vector<RegisterId> arguments;
  for (std::size_t index = 0;
       index < function.arguments.size(); ++index) {
    const auto& formal = function.arguments[index];
    const auto width = formal.type.width();
    if (!width || *width == 0 || *width > 64) {
      return ExpressionAttempt{std::nullopt};
    }
    const auto actual = lower_expression(
        expression.operands[index], *width, &formal.type);
    if (!actual) {
      return ExpressionAttempt{std::nullopt};
    }
    arguments.push_back(*actual);
  }

  auto saved_locals = std::move(locals_);
  auto saved_signed = std::move(local_signed_);
  auto saved_ranges = std::move(local_ranges_);
  auto saved_integer_ranges = std::move(local_integer_ranges_);
  auto saved_members = std::move(local_members_);
  auto saved_types = std::move(local_types_);
  auto saved_scope = std::move(local_scope_);
  locals_.clear();
  local_signed_.clear();
  local_ranges_.clear();
  local_integer_ranges_.clear();
  local_members_.clear();
  local_types_.clear();
  local_scope_ = {selected->object, selected->method};
  active_vhdl_protected_method_ = true;
  const auto bind = [&](const std::string& name,
                        const frontend::Type& member_type,
                        const RegisterId value) {
    locals_.insert_or_assign(name, value);
    local_signed_.insert_or_assign(name, member_type.is_signed);
    local_ranges_.insert_or_assign(name, member_type.packed_range);
    local_integer_ranges_.insert_or_assign(
        name, member_type.integer_range);
    local_members_.insert_or_assign(
        name, member_type.packed_members);
    local_types_.insert_or_assign(name, &member_type);
  };
  const auto zero = allocate_register(
      32, frontend::ValueDomain::Integer);
  process_.operations.emplace_back(
      LoadConstant{zero, unsigned_value(0, 32)});
  bool storage_ok = true;
  for (const auto& member : info.variables) {
    const auto storage = container_objects_.find(
        selected->object + "." + member.name);
    const auto width = member.type.width();
    if (storage == container_objects_.end()
        || !width || *width == 0 || *width > 64) {
      report(
          "FSIM-ELAB-VHPROTECTED-017",
          "protected private storage for '" + selected->object
              + "." + member.name + "' is unavailable",
          expression.span);
      storage_ok = false;
      break;
    }
    const auto container = allocate_container_register(
        design_.container_object_info_.at(storage->second).type);
    const auto value = allocate_register(*width, member.type.domain);
    process_.operations.emplace_back(
        ReadContainerObject{container, storage->second});
    process_.operations.emplace_back(
        ContainerRead{value, container, zero, true, false});
    bind(member.name, member.type, value);
  }
  for (std::size_t index = 0;
       storage_ok && index < function.arguments.size(); ++index) {
    bind(
        function.arguments[index].name,
        function.arguments[index].type,
        arguments[index]);
  }
  std::optional<RegisterId> result;
  if (storage_ok) {
    result = lower_expression(
        function.statements.front().value,
        *result_width,
        &function.return_type);
  }
  active_vhdl_protected_method_ = false;
  locals_ = std::move(saved_locals);
  local_signed_ = std::move(saved_signed);
  local_ranges_ = std::move(saved_ranges);
  local_integer_ranges_ = std::move(saved_integer_ranges);
  local_members_ = std::move(saved_members);
  local_types_ = std::move(saved_types);
  local_scope_ = std::move(saved_scope);
  return ExpressionAttempt{result};
}

bool Lowerer::lower_vhdl_protected_procedure_call(
    const Statement& statement) {
  const auto selected = protected_name(statement.procedure_name);
  if (!selected) {
    return false;
  }
  const auto type_found = visible_types_.find(selected->object);
  if (type_found == visible_types_.end()
      || type_found->second == nullptr
      || !type_found->second->vhdl_protected) {
    return false;
  }
  const auto& info = *type_found->second->vhdl_protected;
  std::vector<const frontend::ProcedureDeclaration*> matches;
  for (const auto& procedure : info.procedures) {
    if (procedure.name != selected->method
        || procedure.arguments.size()
            != statement.procedure_arguments.size()) {
      continue;
    }
    bool compatible = true;
    for (std::size_t index = 0;
         index < procedure.arguments.size(); ++index) {
      if (statement.procedure_arguments[index].formal
          || procedure.arguments[index].direction
              != frontend::PortDirection::Input
          || !vhdl_expression_matches_type(
              statement.procedure_arguments[index].value,
              procedure.arguments[index].type)) {
        compatible = false;
        break;
      }
    }
    if (compatible) {
      matches.push_back(&procedure);
    }
  }
  if (matches.size() != 1) {
    report(
        matches.empty()
            ? "FSIM-ELAB-VHPROTECTED-018"
            : "FSIM-ELAB-VHPROTECTED-019",
        matches.empty()
            ? "protected procedure call '" + statement.procedure_name
                + "' matches no supported public input profile"
            : "protected procedure call '" + statement.procedure_name
                + "' is ambiguous",
        statement.span);
    return true;
  }
  const auto& procedure = *matches.front();
  if (active_vhdl_protected_method_) {
    report(
        "FSIM-ELAB-VHPROTECTED-014",
        "re-entry into a protected method is outside the bounded "
        "execution policy",
        statement.span);
    return true;
  }
  if (contains_explicit_wait(procedure.statements)) {
    report(
        "FSIM-ELAB-VHPROTECTED-020",
        "a protected method cannot suspend",
        procedure.span);
    return true;
  }
  if (contains_call(procedure.statements)) {
    report(
        "FSIM-ELAB-VHPROTECTED-021",
        "nested protected procedure calls are outside the bounded "
        "non-reentrant policy",
        procedure.span);
    return true;
  }

  std::vector<RegisterId> arguments;
  for (std::size_t index = 0;
       index < procedure.arguments.size(); ++index) {
    const auto& formal = procedure.arguments[index];
    const auto width = formal.type.width();
    if (!width || *width == 0 || *width > 64) {
      return true;
    }
    const auto actual = lower_expression(
        statement.procedure_arguments[index].value,
        *width,
        &formal.type);
    if (!actual) {
      return true;
    }
    arguments.push_back(*actual);
  }

  auto saved_locals = std::move(locals_);
  auto saved_signed = std::move(local_signed_);
  auto saved_ranges = std::move(local_ranges_);
  auto saved_integer_ranges = std::move(local_integer_ranges_);
  auto saved_members = std::move(local_members_);
  auto saved_types = std::move(local_types_);
  auto saved_scope = std::move(local_scope_);
  locals_.clear();
  local_signed_.clear();
  local_ranges_.clear();
  local_integer_ranges_.clear();
  local_members_.clear();
  local_types_.clear();
  local_scope_ = {selected->object, selected->method};
  active_vhdl_protected_method_ = true;
  const auto bind = [&](const std::string& name,
                        const frontend::Type& member_type,
                        const RegisterId value) {
    locals_.insert_or_assign(name, value);
    local_signed_.insert_or_assign(name, member_type.is_signed);
    local_ranges_.insert_or_assign(name, member_type.packed_range);
    local_integer_ranges_.insert_or_assign(
        name, member_type.integer_range);
    local_members_.insert_or_assign(
        name, member_type.packed_members);
    local_types_.insert_or_assign(name, &member_type);
  };
  const auto zero = allocate_register(
      32, frontend::ValueDomain::Integer);
  process_.operations.emplace_back(
      LoadConstant{zero, unsigned_value(0, 32)});
  struct MemberStorage {
    ContainerObjectId object{};
    ContainerRegisterId container{};
    RegisterId value{};
  };
  std::vector<MemberStorage> storage;
  bool storage_ok = true;
  for (const auto& member : info.variables) {
    const auto found = container_objects_.find(
        selected->object + "." + member.name);
    const auto width = member.type.width();
    if (found == container_objects_.end()
        || !width || *width == 0 || *width > 64) {
      report(
          "FSIM-ELAB-VHPROTECTED-017",
          "protected private storage for '" + selected->object
              + "." + member.name + "' is unavailable",
          statement.span);
      storage_ok = false;
      break;
    }
    const auto container = allocate_container_register(
        design_.container_object_info_.at(found->second).type);
    const auto value = allocate_register(*width, member.type.domain);
    process_.operations.emplace_back(
        ReadContainerObject{container, found->second});
    process_.operations.emplace_back(
        ContainerRead{value, container, zero, true, false});
    bind(member.name, member.type, value);
    storage.push_back({found->second, container, value});
  }
  for (std::size_t index = 0;
       storage_ok && index < procedure.arguments.size(); ++index) {
    bind(
        procedure.arguments[index].name,
        procedure.arguments[index].type,
        arguments[index]);
  }
  if (storage_ok) {
    lower_statements(procedure.statements);
    for (const auto& member : storage) {
      process_.operations.emplace_back(
          ContainerWrite{
              member.container, zero, member.value, true, false});
      process_.operations.emplace_back(
          WriteContainerObject{member.object, member.container});
    }
  }
  active_vhdl_protected_method_ = false;
  locals_ = std::move(saved_locals);
  local_signed_ = std::move(saved_signed);
  local_ranges_ = std::move(saved_ranges);
  local_integer_ranges_ = std::move(saved_integer_ranges);
  local_members_ = std::move(saved_members);
  local_types_ = std::move(saved_types);
  local_scope_ = std::move(saved_scope);
  return true;
}

}  // namespace fsim::elaboration
