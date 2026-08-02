// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
namespace {

const frontend::Type* type_mark(
    const DesignUnit& unit,
    const std::string_view name) {
    const auto alias = std::ranges::find(
        unit.type_aliases, name,
        &frontend::TypeAliasDeclaration::name);
    if (alias != unit.type_aliases.end()) {
        return &alias->type;
    }
    const auto builtin_integer = [](const std::int64_t left,
                                    const std::int64_t right,
                                    const std::string_view spelling) {
      frontend::Type type;
      type.domain = frontend::ValueDomain::Integer;
      type.spelling = spelling;
      type.integer_range = frontend::IntegerRange{left, right, false};
      return type;
    };
    static const auto integer = builtin_integer(
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        "integer");
    static const auto natural = builtin_integer(
        0, std::numeric_limits<std::int32_t>::max(), "natural");
    static const auto positive = builtin_integer(
        1, std::numeric_limits<std::int32_t>::max(), "positive");
    const auto builtin_discrete = [](const frontend::ValueDomain domain,
                                     const std::string_view spelling) {
      frontend::Type type;
      type.domain = domain;
      type.spelling = spelling;
      return type;
    };
    static const auto boolean = builtin_discrete(
        frontend::ValueDomain::Boolean, "boolean");
    static const auto bit = builtin_discrete(
        frontend::ValueDomain::Bit2, "bit");
    if (name == "integer") {
        return &integer;
    }
    if (name == "natural") {
        return &natural;
    }
    if (name == "positive") {
        return &positive;
    }
    if (name == "boolean") {
        return &boolean;
    }
    return name == "bit" ? &bit : nullptr;
}

std::optional<std::pair<std::int64_t, std::int64_t>>
static_range(
    const frontend::Type& type,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error) {
    if (type.integer_range) {
        return std::pair{
            type.integer_range->left,
            type.integer_range->right};
    }
    if (type.packed_range) {
        return std::pair{
            type.packed_range->left,
            type.packed_range->right};
    }
    std::optional<frontend::Expression> left;
    std::optional<frontend::Expression> right;
    if (type.integer_range_expression) {
        left = type.integer_range_expression->left;
        right = type.integer_range_expression->right;
    } else if (type.packed_range_expression) {
        left = type.packed_range_expression->left;
        right = type.packed_range_expression->right;
    }
    if (!left || !right
        || !fold_vhdl_enumeration_attributes(
            *left, unit, environment, error, range_error)
        || !fold_vhdl_static_expressions(
            *left, unit, environment, error, range_error)
        || !fold_vhdl_enumeration_attributes(
            *right, unit, environment, error, range_error)
        || !fold_vhdl_static_expressions(
            *right, unit, environment, error, range_error)) {
        return std::nullopt;
    }
    const auto left_value = evaluate_constant_expression(
        *left, environment, error);
    const auto right_value = left_value
        ? evaluate_constant_expression(*right, environment, error)
        : std::nullopt;
    if (!left_value || !right_value) {
        return std::nullopt;
    }
    return std::pair{*left_value, *right_value};
}

bool fold_type_attribute(
    Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error) {
    if (expression.kind != ExpressionKind::Call
        || !expression.text.starts_with('\'')
        || expression.operands.empty()
        || expression.operands.front().kind
            != ExpressionKind::Identifier) {
        return true;
    }
    const auto* type = type_mark(unit, expression.operands.front().text);
    const bool type_prefix = type != nullptr;
    if (type == nullptr) {
        type = vhdl_object_type(
            unit, expression.operands.front().text);
    }
    if (type == nullptr || !type->enumeration_literals.empty()) {
        return true;
    }
    const auto simple_name = type->spelling.substr(
        type->spelling.find_last_of('.') == std::string::npos
            ? 0
            : type->spelling.find_last_of('.') + 1);
    const bool array = type->vhdl_array
        || simple_name == "bit_vector"
        || simple_name == "std_logic_vector"
        || simple_name == "std_ulogic_vector"
        || simple_name == "signed" || simple_name == "unsigned";
    std::int64_t left = 0;
    std::int64_t right = 0;
    bool descending = false;
    bool null_range = false;
    if (array) {
        if (expression.operands.size() > 2) {
            error = expression.text
                + " accepts at most one array dimension argument";
            return false;
        }
        std::int64_t dimension = 1;
        if (expression.operands.size() == 2) {
            const auto selected = evaluate_constant_expression(
                expression.operands[1], environment, error);
            if (!selected) {
                error = expression.text
                    + " requires a locally static array dimension";
                return false;
            }
            dimension = *selected;
        }
        const auto rank = type->vhdl_array
            ? type->vhdl_array->dimensions.size()
            : std::size_t{1};
        if (dimension < 1
            || static_cast<std::uint64_t>(dimension) > rank) {
            range_error = true;
            error = expression.text + " dimension "
                + std::to_string(dimension) + " is outside array rank "
                + std::to_string(rank);
            return false;
        }
        if (type->vhdl_array) {
            const auto& selected = type->vhdl_array->dimensions[
                static_cast<std::size_t>(dimension - 1)];
            if (selected.range) {
                left = selected.range->left;
                right = selected.range->right;
                descending = selected.range->descending;
                null_range = selected.null;
            } else if (selected.constraint) {
                auto left_expression = selected.constraint->left;
                auto right_expression = selected.constraint->right;
                if (!fold_vhdl_enumeration_attributes(
                        left_expression, unit, environment, error, range_error)
                    || !fold_vhdl_static_expressions(
                        left_expression, unit, environment, error, range_error)
                    || !fold_vhdl_enumeration_attributes(
                        right_expression, unit, environment, error, range_error)
                    || !fold_vhdl_static_expressions(
                        right_expression, unit, environment, error, range_error)) {
                    return false;
                }
                const auto left_value = evaluate_constant_expression(
                    left_expression, environment, error);
                const auto right_value = left_value
                    ? evaluate_constant_expression(
                          right_expression, environment, error)
                    : std::nullopt;
                if (!left_value || !right_value) {
                    error = "VHDL array attribute constraint is not locally static";
                    return false;
                }
                left = *left_value;
                right = *right_value;
                descending = selected.constraint->descending;
                null_range = descending ? left < right : left > right;
            } else {
                error = "VHDL array attribute prefix has no concrete constraint";
                return false;
            }
        } else if (type->packed_range) {
            left = type->packed_range->left;
            right = type->packed_range->right;
            descending = type->packed_range->descending;
        } else {
            error = "VHDL array attribute prefix has no concrete constraint";
            return false;
        }
    } else {
        if (!type_prefix) {
            error = "a scalar attribute prefix must be a type or subtype mark";
            return false;
        }
        if (type->domain == frontend::ValueDomain::Integer) {
            const auto range = static_range(
                *type, unit, environment, error, range_error);
            if (!range) {
                error = "VHDL scalar attribute prefix has no locally static range";
                return false;
            }
            left = range->first;
            right = range->second;
            descending = type->integer_range
                ? type->integer_range->descending
                : type->integer_range_expression
                    && type->integer_range_expression->descending;
        } else if (type->packed_members.empty()
                   && (type->domain == frontend::ValueDomain::Boolean
                       || type->domain == frontend::ValueDomain::Bit2)) {
            left = 0;
            right = 1;
        } else {
            return true;
        }
    }

    const auto low = std::min(left, right);
    const auto high = std::max(left, right);
    const bool zero_arguments = expression.text == "'left"
        || expression.text == "'right" || expression.text == "'low"
        || expression.text == "'high" || expression.text == "'ascending"
        || expression.text == "'length";
    const bool one_argument = expression.text == "'pos"
        || expression.text == "'val" || expression.text == "'succ"
        || expression.text == "'pred" || expression.text == "'leftof"
        || expression.text == "'rightof";
    if (!zero_arguments && !one_argument) {
        return true;
    }
    if (array && one_argument) {
        error = expression.text + " is not defined for an array prefix";
        return false;
    }
    if (!array
        && expression.operands.size() != (one_argument ? 2U : 1U)) {
        error = expression.text + " requires "
            + (one_argument ? std::string{"one argument"}
                            : std::string{"no argument"});
        return false;
    }

    std::int64_t value = 0;
    auto result_domain = frontend::ValueDomain::Integer;
    if (expression.text == "'left") {
        value = left;
        result_domain = array ? frontend::ValueDomain::Integer : type->domain;
    } else if (expression.text == "'right") {
        value = right;
        result_domain = array ? frontend::ValueDomain::Integer : type->domain;
    } else if (expression.text == "'low") {
        value = low;
        result_domain = array ? frontend::ValueDomain::Integer : type->domain;
    } else if (expression.text == "'high") {
        value = high;
        result_domain = array ? frontend::ValueDomain::Integer : type->domain;
    } else if (expression.text == "'ascending") {
        value = descending ? 0 : 1;
        result_domain = frontend::ValueDomain::Boolean;
    } else if (expression.text == "'length") {
        const auto distance = null_range ? std::uint64_t{0}
            : index_distance(left, right) + 1U;
        if (distance
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::max())) {
            range_error = true;
            error = "VHDL attribute length is outside the bounded integer range";
            return false;
        }
        value = static_cast<std::int64_t>(distance);
    } else {
        const auto argument = evaluate_constant_expression(
            expression.operands[1], environment, error);
        if (!argument) {
            return false;
        }
        if (*argument < low || *argument > high) {
            range_error = true;
            error = expression.text + " argument is outside the scalar range";
            return false;
        }
        if (expression.text == "'pos") {
            value = *argument;
        } else if (expression.text == "'val") {
            value = *argument;
            result_domain = type->domain;
        } else {
            const bool successor = expression.text == "'succ"
                || (expression.text == "'leftof" && descending)
                || (expression.text == "'rightof" && !descending);
            value = *argument + (successor ? 1 : -1);
            if (value < low || value > high) {
                range_error = true;
                error = expression.text
                    + " argument has no result inside the scalar range";
                return false;
            }
            result_domain = type->domain;
        }
    }
    expression = constant_expression(
        value,
        expression.span,
        result_domain,
        frontend::Language::Vhdl2008);
    return true;
}

bool fold_integer_conversion(
    Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error) {
    if (expression.kind != ExpressionKind::Call
        || expression.operands.size() != 1) {
        return true;
    }
    const auto* type = type_mark(unit, expression.text);
    if (type == nullptr
        || type->domain != frontend::ValueDomain::Integer) {
        return true;
    }
    const auto value = evaluate_constant_expression(
        expression.operands.front(), environment, error);
    if (!value) {
        return false;
    }
    const auto range = static_range(
        *type, unit, environment, error, range_error);
    if (range
        && (*value < std::min(range->first, range->second)
            || *value > std::max(range->first, range->second))) {
        range_error = true;
        error = "VHDL integer conversion result is outside subtype range";
        return false;
    }
    expression = constant_expression(
        *value,
        expression.span,
        frontend::ValueDomain::Integer,
        frontend::Language::Vhdl2008);
    return true;
}

}  // namespace

bool fold_vhdl_static_expressions(
    Expression& expression,
    const DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::string& error,
    bool& range_error) {
    for (auto& association : expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            if (!fold_vhdl_static_expressions(
                    choice, unit, environment, error, range_error)) {
                return false;
            }
        }
    }
    for (auto& operand : expression.operands) {
        if (!fold_vhdl_static_expressions(
                operand, unit, environment, error, range_error)) {
            return false;
        }
    }
    return fold_type_attribute(
               expression, unit, environment, error, range_error)
        && fold_integer_conversion(
               expression, unit, environment, error, range_error);
}

void fold_vhdl_static_type_expressions(
    DesignUnit& unit,
    const ConstantEnvironment& environment,
    std::vector<Diagnostic>& diagnostics) {
    const auto fold_type = [&](const auto& self, frontend::Type& type) -> void {
      const auto fold_expression = [&](frontend::Expression& expression) {
        std::string error;
        bool range_error = false;
        if (!fold_vhdl_enumeration_attributes(
                expression, unit, environment, error, range_error)
            || !fold_vhdl_static_expressions(
                expression, unit, environment, error, range_error)) {
          diagnostics.push_back({
              "FSIM-ELAB-GENERIC-006",
              "cannot evaluate VHDL type expression: " + error,
              expression.span});
        }
      };
      const auto fold_range = [&](auto& range) {
        if (!range) {
          return;
        }
        std::string error;
        bool range_error = false;
        const bool left_enum = fold_vhdl_enumeration_attributes(
            range->left, unit, environment, error, range_error);
        const bool left = left_enum
            && fold_vhdl_static_expressions(
                range->left, unit, environment, error, range_error);
        const bool right = left
            && fold_vhdl_enumeration_attributes(
                range->right, unit, environment, error, range_error)
            && fold_vhdl_static_expressions(
                range->right, unit, environment, error, range_error);
        if (!right) {
          diagnostics.push_back({
              "FSIM-ELAB-GENERIC-006",
              "cannot evaluate VHDL type bound: " + error,
              range->span});
        }
      };
      fold_range(type.packed_range_expression);
      fold_range(type.integer_range_expression);
      fold_range(type.integer_base_range_expression);
      fold_range(type.enumeration_range_expression);
      fold_range(type.enumeration_base_range_expression);
      fold_range(type.discrete_range_expression);
      if (type.vhdl_array) {
        for (auto& dimension : type.vhdl_array->dimensions) {
          fold_range(dimension.constraint);
        }
        for (auto& element : type.vhdl_array->element_types) {
          self(self, element);
        }
      }
      if (type.vhdl_access) {
        for (auto& designated :
             type.vhdl_access->designated_types) {
          self(self, designated);
        }
      }
      if (type.vhdl_physical) {
        if (type.vhdl_physical->range) {
          fold_expression(type.vhdl_physical->range->left);
          fold_expression(type.vhdl_physical->range->right);
        }
        for (auto& physical_unit : type.vhdl_physical->units) {
          if (physical_unit.scale) {
            fold_expression(*physical_unit.scale);
          }
        }
      }
      if (type.vhdl_protected) {
        for (auto& variable : type.vhdl_protected->variables) {
          self(self, variable.type);
        }
        for (auto& function : type.vhdl_protected->functions) {
          self(self, function.return_type);
          for (auto& argument : function.arguments) {
            self(self, argument.type);
          }
        }
        for (auto& procedure : type.vhdl_protected->procedures) {
          for (auto& argument : procedure.arguments) {
            self(self, argument.type);
          }
        }
      }
      for (auto& member : type.packed_members) {
        for (auto& nested : member.nested_types) {
          self(self, nested);
        }
      }
      if (type.systemverilog_container
          && type.systemverilog_container->associative_index_type) {
        self(
            self,
            *type.systemverilog_container->associative_index_type);
      }
    };
    const auto fold_variable = [&](auto& declaration) {
      fold_type(fold_type, declaration.type);
    };
    const auto fold_statements = [&](const auto& self, auto& statements) -> void {
      for (auto& statement : statements) {
        for (auto& declaration : statement.declarations) {
          fold_variable(declaration);
        }
        self(self, statement.statements);
        self(self, statement.else_statements);
        for (auto& alternative : statement.case_alternatives) {
          self(self, alternative.statements);
        }
      }
    };
    const auto fold_local_region =
        [&](const auto& self, auto& region) -> void {
          for (auto& constant : region.constants) {
            fold_variable(constant);
          }
          for (auto& alias : region.type_aliases) {
            fold_type(fold_type, alias.type);
          }
          for (auto& alias : region.signal_aliases) {
            fold_type(fold_type, alias.type);
          }
          for (auto& variable : region.variables) {
            fold_variable(variable);
          }
          for (auto& function : region.functions) {
            fold_type(fold_type, function.return_type);
            for (auto& argument : function.arguments) {
              fold_type(fold_type, argument.type);
            }
            self(self, function);
          }
          for (auto& procedure : region.procedures) {
            for (auto& argument : procedure.arguments) {
              fold_type(fold_type, argument.type);
            }
            self(self, procedure);
          }
          for (auto& package : region.package_instances) {
            for (auto& actual : package.generic_map) {
              if (actual.type_value) {
                fold_type(fold_type, *actual.type_value);
              }
            }
          }
          fold_statements(fold_statements, region.statements);
        };
    for (auto& parameter : unit.parameters) {
      fold_type(fold_type, parameter.type);
    }
    for (auto& alias : unit.type_aliases) {
      fold_type(fold_type, alias.type);
    }
    for (auto& port : unit.ports) {
      fold_variable(port);
    }
    for (auto& signal : unit.signals) {
      fold_variable(signal);
    }
    for (auto& variable : unit.variables) {
      fold_variable(variable);
    }
    for (auto& function : unit.functions) {
      fold_type(fold_type, function.return_type);
      for (auto& argument : function.arguments) {
        fold_type(fold_type, argument.type);
      }
      fold_local_region(fold_local_region, function);
    }
    for (auto& procedure : unit.procedures) {
      for (auto& argument : procedure.arguments) {
        fold_type(fold_type, argument.type);
      }
      fold_local_region(fold_local_region, procedure);
    }
    for (auto& task : unit.tasks) {
      for (auto& argument : task.arguments) {
        fold_type(fold_type, argument.type);
      }
      for (auto& variable : task.variables) {
        fold_variable(variable);
      }
      fold_statements(fold_statements, task.statements);
    }
    for (auto& process : unit.processes) {
      fold_local_region(fold_local_region, process);
    }
    fold_statements(fold_statements, unit.concurrent_statements);
}

}  // namespace fsim::elaboration::elaboration_detail
