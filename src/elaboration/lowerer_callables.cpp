// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <unordered_set>

namespace fsim::elaboration {

bool Lowerer::static_reference_actual(
    const Expression& expression) const {
    const auto static_identifier = [&](const std::string_view name) {
        if (signals_.contains(std::string { name })
            || string_objects_.contains(std::string { name })
            || container_objects_.contains(std::string { name })) {
            return true;
        }
        const bool local = locals_.contains(std::string { name })
            || string_locals_.contains(std::string { name })
            || container_locals_.contains(std::string { name });
        if (!local || active_procedure_) {
            return false;
        }
        if (active_function_) {
            const auto* source = function_frames_[*active_function_].source;
            if (source == nullptr) {
                return false;
            }
            const auto formal = std::ranges::find(
                source->arguments, name,
                &frontend::FunctionArgument::name);
            return formal != source->arguments.end()
                ? formal->static_reference
                : !source->automatic;
        }
        if (active_task_) {
            const auto* source = task_frames_[*active_task_].source;
            if (source == nullptr) {
                return false;
            }
            const auto formal = std::ranges::find(
                source->arguments, name,
                &frontend::TaskArgument::name);
            return formal != source->arguments.end()
                ? formal->static_reference
                : !source->automatic;
        }
        // Variables declared by a module procedural thread have static
        // lifetime unless they are inside an automatic callable activation.
        return true;
    };
    const auto visit = [&](const auto& self,
                           const Expression& candidate) -> bool {
        if (candidate.kind == ExpressionKind::Identifier) {
            return static_identifier(candidate.text);
        }
        if ((candidate.kind != ExpressionKind::Index
                && candidate.kind != ExpressionKind::Slice)
            || candidate.operands.empty()) {
            return false;
        }
        const auto& base = candidate.operands.front();
        if (base.kind == ExpressionKind::Identifier
            && (string_locals_.contains(base.text)
                || string_objects_.contains(base.text)
                || container_locals_.contains(base.text)
                || container_objects_.contains(base.text))) {
            // Elements of strings and dynamic containers do not themselves
            // have static lifetime. Fixed unpacked storage is not represented
            // by these dynamic-object maps.
            return false;
        }
        return self(self, base);
    };
    return visit(visit, expression);
}
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<std::vector<const Expression*>>
Lowerer::bind_function_actuals(
    const Expression& expression,
    const frontend::FunctionDeclaration& function) {
    std::vector<const Expression*> bound(function.arguments.size());
    const bool have_names = !expression.call_argument_names.empty();
    if (have_names
        && expression.call_argument_names.size()
            != expression.operands.size()) {
        report(
            "FSIM-ELAB-SVFUNC-010",
            "function call association metadata is inconsistent",
            expression.span);
        return std::nullopt;
    }
    bool named_seen = false;
    std::size_t positional = 0;
    for (std::size_t index = 0;
         index < expression.operands.size(); ++index) {
        const auto& name = have_names
            ? expression.call_argument_names[index]
            : std::string{};
        std::size_t formal_index = 0;
        if (name.empty()) {
            if (named_seen || positional >= bound.size()) {
                report(
                    "FSIM-ELAB-SVFUNC-010",
                    named_seen
                        ? "positional function argument follows a named argument"
                        : "too many positional function arguments",
                    expression.span);
                return std::nullopt;
            }
            formal_index = positional++;
        } else {
            named_seen = true;
            const auto found = std::ranges::find(
                function.arguments,
                name,
                &frontend::FunctionArgument::name);
            if (found == function.arguments.end()) {
                report(
                    "FSIM-ELAB-SVFUNC-010",
                    "unknown named function argument '" + name + "'",
                    expression.span);
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(function.arguments.begin(), found));
        }
        if (bound[formal_index] != nullptr) {
            report(
                "FSIM-ELAB-SVFUNC-010",
                "function argument '"
                    + function.arguments[formal_index].name
                    + "' is associated more than once",
                expression.span);
            return std::nullopt;
        }
        if (expression.operands[index].valid()) {
            bound[formal_index] = &expression.operands[index];
        }
    }
    for (std::size_t index = 0; index < bound.size(); ++index) {
        if (bound[index] == nullptr
            && function.arguments[index].default_value) {
            bound[index] = &*function.arguments[index].default_value;
        }
        if (bound[index] == nullptr) {
            report(
                "FSIM-ELAB-SVFUNC-010",
                "function argument '" + function.arguments[index].name
                    + "' has no actual or default value",
                expression.span);
            return std::nullopt;
        }
    }
    return bound;
}

bool Lowerer::validate_function_reference_actuals(
    const frontend::FunctionDeclaration& function,
    const std::vector<const Expression*>& actuals) {
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        if (!function.arguments[index].reference) {
            continue;
        }
        const auto& actual = *actuals[index];
        const auto writable = [&](const auto& self,
                                  const Expression& candidate) -> bool {
            if (candidate.kind == ExpressionKind::Identifier) {
                return locals_.contains(candidate.text)
                    || string_locals_.contains(candidate.text)
                    || container_locals_.contains(candidate.text)
                    || signals_.contains(candidate.text)
                    || packed_member_reference(candidate.text).has_value();
            }
            return ((candidate.kind == ExpressionKind::Index
                     && candidate.operands.size() == 2)
                    || (candidate.kind == ExpressionKind::Slice
                        && candidate.operands.size() == 3))
                && self(self, candidate.operands.front());
        };
        if (!function.automatic || !writable(writable, actual)) {
            report(
                "FSIM-ELAB-SVFUNC-012",
                "ref function arguments require an automatic function and "
                "a writable variable actual",
                actual.span);
            return false;
        }
        if (function.arguments[index].static_reference
            && !static_reference_actual(actual)) {
            report(
                "FSIM-ELAB-SVFUNC-013",
                "ref static function arguments require an actual with "
                "static storage lifetime",
                actual.span);
            return false;
        }
    }
    return true;
}

std::optional<std::vector<const Expression*>>
Lowerer::bind_task_actuals(
    const Statement& statement,
    const frontend::TaskDeclaration& task) {
    std::vector<const Expression*> bound(task.arguments.size());
    const bool have_names = !statement.task_argument_names.empty();
    if (have_names
        && statement.task_argument_names.size()
            != statement.task_arguments.size()) {
        report(
            "FSIM-ELAB-SVTASK-012",
            "task call association metadata is inconsistent",
            statement.span);
        return std::nullopt;
    }
    bool named_seen = false;
    std::size_t positional = 0;
    for (std::size_t index = 0;
         index < statement.task_arguments.size(); ++index) {
        const auto& name = have_names
            ? statement.task_argument_names[index]
            : std::string{};
        std::size_t formal_index = 0;
        if (name.empty()) {
            if (named_seen || positional >= bound.size()) {
                report(
                    "FSIM-ELAB-SVTASK-012",
                    named_seen
                        ? "positional task argument follows a named argument"
                        : "too many positional task arguments",
                    statement.span);
                return std::nullopt;
            }
            formal_index = positional++;
        } else {
            named_seen = true;
            const auto found = std::ranges::find(
                task.arguments,
                name,
                &frontend::TaskArgument::name);
            if (found == task.arguments.end()) {
                report(
                    "FSIM-ELAB-SVTASK-012",
                    "unknown named task argument '" + name + "'",
                    statement.span);
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(task.arguments.begin(), found));
        }
        if (bound[formal_index] != nullptr) {
            report(
                "FSIM-ELAB-SVTASK-012",
                "task argument '" + task.arguments[formal_index].name
                    + "' is associated more than once",
                statement.span);
            return std::nullopt;
        }
        if (statement.task_arguments[index].valid()) {
            bound[formal_index] = &statement.task_arguments[index];
        }
    }
    for (std::size_t index = 0; index < bound.size(); ++index) {
        if (bound[index] == nullptr
            && task.arguments[index].default_value) {
            bound[index] = &*task.arguments[index].default_value;
        }
        if (bound[index] == nullptr) {
            report(
                "FSIM-ELAB-SVTASK-012",
                "task argument '" + task.arguments[index].name
                    + "' has no actual or default value",
                statement.span);
            return std::nullopt;
        }
    }
    return bound;
}

std::optional<Expression> Lowerer::capture_callable_copy_out_target(
    const Expression& target,
    std::string temporary_prefix) {
    auto captured = target;
    std::size_t selector_index = 0;
    const auto capture = [&](const auto& self,
                             Expression& candidate) -> bool {
        if ((candidate.kind != ExpressionKind::Index
             && candidate.kind != ExpressionKind::Slice)
            || candidate.operands.empty()) {
            return true;
        }
        if (!self(self, candidate.operands.front())) {
            return false;
        }
        for (std::size_t index = 1;
             index < candidate.operands.size(); ++index) {
            auto& selector = candidate.operands[index];
            if (static_integer_value(selector)) {
                continue;
            }
            auto value = lower_expression(selector, 32);
            if (!value) {
                return false;
            }
            if (register_width(*value) != 32) {
                *value = resize_register(
                    *value, 32, is_signed_expression(selector));
            }
            const auto name = temporary_prefix + "_selector_"
                + std::to_string(selector_index++);
            locals_.insert_or_assign(name, *value);
            local_signed_.insert_or_assign(name, true);
            selector = Expression{
                ExpressionKind::Identifier, name, {}, selector.span};
        }
        return true;
    };
    if (!capture(capture, captured)) {
        return std::nullopt;
    }
    return captured;
}

void Lowerer::lower_callable_copy_out(
    const Expression& target,
    const frontend::Type& type,
    const RegisterId value,
    const StringRegisterId string_value,
    const ContainerRegisterId container_value,
    const bool is_string,
    const bool is_container,
    std::string temporary) {
    if (is_container) {
        container_locals_.insert_or_assign(temporary, container_value);
    } else if (is_string) {
        string_locals_.insert_or_assign(temporary, string_value);
    } else {
        locals_.insert_or_assign(temporary, value);
        local_signed_.insert_or_assign(temporary, type.is_signed);
        local_ranges_.insert_or_assign(temporary, type.packed_range);
        local_integer_ranges_.insert_or_assign(
            temporary, type.integer_range);
        local_members_.insert_or_assign(temporary, type.packed_members);
    }
    local_types_.insert_or_assign(temporary, &type);
    Statement copy_out;
    copy_out.kind = StatementKind::Assignment;
    copy_out.assignment_kind = AssignmentKind::Blocking;
    copy_out.target = target;
    copy_out.value = Expression{
        ExpressionKind::Identifier,
        std::move(temporary),
        {},
        target.span};
    copy_out.span = target.span;
    lower_assignment(copy_out);
}

std::unordered_map<std::string, Lowerer::CallableVariableRegister>
Lowerer::allocate_static_callable_variables(
    const std::vector<frontend::VariableDeclaration>& variables,
    const std::vector<frontend::Statement>& statements,
    const std::string_view callable_name) {
    std::unordered_map<std::string, CallableVariableRegister> registers;

    auto saved_locals = std::move(locals_);
    auto saved_string_locals = std::move(string_locals_);
    auto saved_container_locals = std::move(container_locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = {std::string{callable_name}};

    const auto allocate_scope =
        [&](const std::vector<frontend::VariableDeclaration>& declarations) {
          initialize_variables(declarations);
          for (const auto& variable : declarations) {
              CallableVariableRegister storage;
              if (variable.type.systemverilog_container) {
                  const auto found = container_locals_.find(variable.name);
                  if (found == container_locals_.end()) {
                      continue;
                  }
                  storage.is_container = true;
                  storage.container = found->second;
              } else if (
                  variable.type.domain == frontend::ValueDomain::String) {
                  const auto found = string_locals_.find(variable.name);
                  if (found == string_locals_.end()) {
                      continue;
                  }
                  storage.is_string = true;
                  storage.string = found->second;
              } else {
                  const auto found = locals_.find(variable.name);
                  if (found == locals_.end()) {
                      continue;
                  }
                  storage.packed = found->second;
              }
              registers.emplace(declaration_key(variable), storage);
          }
        };
    const auto allocate_statements =
        [&](const auto& self,
            const std::vector<frontend::Statement>& nested) -> void {
          for (const auto& statement : nested) {
              const bool scoped =
                  statement.kind == frontend::StatementKind::Block
                  || statement.kind == frontend::StatementKind::Fork;
              std::unordered_map<std::string, RegisterId> outer_locals;
              std::unordered_map<std::string, StringRegisterId>
                  outer_string_locals;
              std::unordered_map<std::string, ContainerRegisterId>
                  outer_container_locals;
              std::unordered_map<std::string, bool> outer_signed;
              std::unordered_map<
                  std::string,
                  std::optional<frontend::PackedRange>> outer_ranges;
              std::unordered_map<
                  std::string,
                  std::optional<frontend::IntegerRange>>
                  outer_integer_ranges;
              std::unordered_map<
                  std::string,
                  std::vector<frontend::PackedMember>> outer_members;
              std::unordered_map<std::string, const frontend::Type*>
                  outer_types;
              if (scoped) {
                  outer_locals = locals_;
                  outer_string_locals = string_locals_;
                  outer_container_locals = container_locals_;
                  outer_signed = local_signed_;
                  outer_ranges = local_ranges_;
                  outer_integer_ranges = local_integer_ranges_;
                  outer_members = local_members_;
                  outer_types = local_types_;
                  local_scope_.push_back(block_scope_name(statement));
              }
              allocate_scope(statement.declarations);
              self(self, statement.statements);
              self(self, statement.else_statements);
              for (const auto& alternative :
                   statement.case_alternatives) {
                  self(self, alternative.statements);
              }
              if (scoped) {
                  local_scope_.pop_back();
                  locals_ = std::move(outer_locals);
                  string_locals_ = std::move(outer_string_locals);
                  container_locals_ =
                      std::move(outer_container_locals);
                  local_signed_ = std::move(outer_signed);
                  local_ranges_ = std::move(outer_ranges);
                  local_integer_ranges_ =
                      std::move(outer_integer_ranges);
                  local_members_ = std::move(outer_members);
                  local_types_ = std::move(outer_types);
              }
          }
        };
    allocate_scope(variables);
    allocate_statements(allocate_statements, statements);

    local_scope_ = std::move(saved_scope);
    local_types_ = std::move(saved_types);
    local_members_ = std::move(saved_members);
    local_integer_ranges_ = std::move(saved_integer_ranges);
    local_ranges_ = std::move(saved_ranges);
    local_signed_ = std::move(saved_signed);
    container_locals_ = std::move(saved_container_locals);
    string_locals_ = std::move(saved_string_locals);
    locals_ = std::move(saved_locals);
    return registers;
}

void Lowerer::bind_static_callable_variables(
    const std::vector<frontend::VariableDeclaration>& variables,
    const std::unordered_map<
        std::string, CallableVariableRegister>& registers) {
    for (const auto& variable : variables) {
        const auto found = registers.find(declaration_key(variable));
        if (found == registers.end()) {
            continue;
        }
        const auto& storage = found->second;
        if (storage.is_container) {
            container_locals_.insert_or_assign(
                variable.name, storage.container);
            local_types_.insert_or_assign(variable.name, &variable.type);
            continue;
        }
        if (storage.is_string) {
            string_locals_.insert_or_assign(variable.name, storage.string);
            local_types_.insert_or_assign(variable.name, &variable.type);
            continue;
        }
        const auto register_id = storage.packed;
        locals_.insert_or_assign(variable.name, register_id);
        local_signed_.insert_or_assign(
            variable.name, variable.type.is_signed);
        local_ranges_.insert_or_assign(
            variable.name, variable.type.packed_range);
        local_integer_ranges_.insert_or_assign(
            variable.name, variable.type.integer_range);
        local_members_.insert_or_assign(
            variable.name, variable.type.packed_members);
        local_types_.insert_or_assign(variable.name, &variable.type);
    }
}

}  // namespace fsim::elaboration
