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
    if (name == "integer") {
        return &integer;
    }
    if (name == "natural") {
        return &natural;
    }
    return name == "positive" ? &positive : nullptr;
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
    const auto* type = type_mark(
        unit, expression.operands.front().text);
    if (type == nullptr || !type->enumeration_literals.empty()) {
        return true;
    }
    if (expression.operands.size() != 1) {
        error = expression.text
            + " on a scalar or array type takes no argument";
        return false;
    }
    const auto range = static_range(
        *type, unit, environment, error, range_error);
    if (!range) {
        error = "VHDL type attribute prefix has no locally static range";
        return false;
    }
    const auto [left, right] = *range;
    std::optional<std::int64_t> value;
    if (expression.text == "'left") {
        value = left;
    } else if (expression.text == "'right") {
        value = right;
    } else if (expression.text == "'low") {
        value = std::min(left, right);
    } else if (expression.text == "'high") {
        value = std::max(left, right);
    } else if (expression.text == "'ascending") {
        value = left <= right ? 1 : 0;
    } else if (expression.text == "'length") {
        const auto distance = left >= right
            ? static_cast<std::uint64_t>(left - right)
            : static_cast<std::uint64_t>(right - left);
        if (distance
            >= static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            range_error = true;
            error = "VHDL type attribute length is not representable";
            return false;
        }
        value = static_cast<std::int64_t>(distance + 1U);
    } else {
        return true;
    }
    expression = constant_expression(
        *value,
        expression.span,
        expression.text == "'ascending"
            ? frontend::ValueDomain::Boolean
            : frontend::ValueDomain::Integer,
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
