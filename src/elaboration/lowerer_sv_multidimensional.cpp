// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <algorithm>
#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;

std::optional<RegisterId>
Lowerer::lower_multidimensional_index(
    const Expression& expression,
    const frontend::Type& type) {
  if (!type.systemverilog_container) {
    return std::nullopt;
  }
  const auto& ranges = type.systemverilog_container
      ->static_range_expressions;
  std::vector<const Expression*> indices;
  const Expression* base = &expression;
  while (base->kind == ExpressionKind::Index
         && base->operands.size() == 2) {
    indices.push_back(&base->operands[1]);
    base = &base->operands[0];
  }
  std::ranges::reverse(indices);
  if (base->kind != ExpressionKind::Identifier
      || ranges.size() <= 1 || indices.size() != ranges.size()) {
    report(
        "FSIM-ELAB-SVMDARRAY-001",
        "a multidimensional static-array access must supply exactly one "
        "index per declared dimension",
        expression.span);
    return std::nullopt;
  }
  const auto runtime_type = container_type(type, expression.span);
  if (!runtime_type
      || runtime_type->dimensions.size() != ranges.size()) {
    return std::nullopt;
  }
  std::optional<RegisterId> linear;
  for (std::size_t dimension = 0;
       dimension < ranges.size(); ++dimension) {
    const auto& bounds = runtime_type->dimensions[dimension];
    const auto low = std::min(bounds.first, bounds.second);
    const auto high = std::max(bounds.first, bounds.second);
    std::optional<RegisterId> ordinal;
    if (const auto constant = static_integer_value(*indices[dimension])) {
      if (*constant < low || *constant > high) {
        report(
            "FSIM-ELAB-SVMDARRAY-003",
            "multidimensional static-array index is outside its declared "
            "range",
            indices[dimension]->span);
        return std::nullopt;
      }
      const auto value = static_cast<std::uint64_t>(
          bounds.first >= bounds.second
              ? static_cast<std::int64_t>(bounds.first) - *constant
              : *constant - bounds.first);
      ordinal = allocate_register(32, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(
          LoadConstant{*ordinal, unsigned_value(value, 32)});
    } else {
      const auto index = lower_expression(*indices[dimension], 32);
      if (!index || register_width(*index) != 32) {
        report(
            "FSIM-ELAB-SVMDARRAY-002",
            "a runtime multidimensional index must lower to a signed "
            "32-bit integral value",
            indices[dimension]->span);
        return std::nullopt;
      }
      process_.operations.emplace_back(IntegerCheck{
          *index, low, high});
      const auto declared_left =
          allocate_register(32, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(LoadConstant{
          declared_left, integer_value(bounds.first)});
      ordinal = allocate_register(32, frontend::ValueDomain::Integer);
      process_.operations.emplace_back(IntegerBinary{
          IntegerBinaryOperator::subtract,
          *ordinal,
          bounds.first >= bounds.second ? declared_left : *index,
          bounds.first >= bounds.second ? *index : declared_left});
    }
    if (!linear) {
      linear = *ordinal;
      continue;
    }
    const auto count = static_cast<std::int64_t>(
        static_cast<std::int64_t>(high) - low + 1);
    const auto count_register =
        allocate_register(32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        LoadConstant{count_register, integer_value(count)});
    const auto scaled =
        allocate_register(32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary{
        IntegerBinaryOperator::multiply,
        scaled, *linear, count_register});
    const auto combined =
        allocate_register(32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary{
        IntegerBinaryOperator::add,
        combined, scaled, *ordinal});
    linear = combined;
  }
  return linear;
}

Lowerer::ExpressionAttempt
Lowerer::lower_multidimensional_container_read(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Index) {
    return {};
  }
  const Expression* base = &expression;
  while (base->kind == ExpressionKind::Index
         && base->operands.size() == 2) {
    base = &base->operands.front();
  }
  if (base->kind != ExpressionKind::Identifier) {
    return {};
  }
  const auto* type = object_type(base->text);
  if (type == nullptr || !type->systemverilog_container
      || type->systemverilog_container
             ->static_range_expressions.size()
          <= 1) {
    return {};
  }
  const auto linear = lower_multidimensional_index(expression, *type);
  const auto width = type->width();
  if (!linear || !width) {
    return std::nullopt;
  }
  const auto source = lower_container_expression(*base);
  if (!source) {
    return std::nullopt;
  }
  const auto destination = allocate_register(*width, type->domain);
  process_.operations.emplace_back(ContainerRead{
      destination, *source, *linear, true, true});
  return destination;
}

bool Lowerer::lower_multidimensional_container_assignment(
    const Statement& statement,
    const frontend::Type& type,
    const ContainerRegisterId target,
    const std::optional<ContainerObjectId> object) {
  if (!type.systemverilog_container
      || type.systemverilog_container
             ->static_range_expressions.size()
          <= 1
      || statement.target.kind != ExpressionKind::Index) {
    return false;
  }
  const auto linear = lower_multidimensional_index(
      statement.target, type);
  const auto element_width = type.width();
  if (!linear || !element_width) {
    return true;
  }
  auto scalar_element_type = type;
  scalar_element_type.systemverilog_container.reset();
  auto value = lower_expression(
      statement.value, *element_width, &scalar_element_type);
  if (!value) {
    return true;
  }
  if (register_width(*value) != *element_width) {
    *value = resize_register(
        *value, *element_width,
        is_signed_expression(statement.value));
  }
  process_.operations.emplace_back(ContainerWrite{
      target, *linear, *value, true, true});
  if (object) {
    process_.operations.emplace_back(
        WriteContainerObject{*object, target});
  }
  return true;
}

std::optional<ContainerRegisterId>
Lowerer::lower_multidimensional_container_pattern(
    const Expression& expression,
    const frontend::Type& source_type,
    const ContainerType& runtime_type) {
  const auto destination = allocate_container_register(runtime_type);
  auto element_type = source_type;
  element_type.systemverilog_container.reset();
  std::function<bool(
      const Expression&, std::size_t, std::uint64_t)> lower_dimension;
  lower_dimension =
      [&](const Expression& pattern,
          const std::size_t dimension,
          const std::uint64_t base) {
        if (pattern.kind != ExpressionKind::Aggregate
            || pattern.text != "sv-pattern"
            || pattern.operands.size()
                != pattern.aggregate_choices.size()
            || pattern.operands.size()
                != pattern.aggregate_choice_expressions.size()) {
          report(
              "FSIM-ELAB-SVPATTERN-001",
              "each multidimensional static-array dimension requires "
              "consistent nested assignment-pattern metadata",
              pattern.span);
          return false;
        }
        const auto& bounds = runtime_type.dimensions[dimension];
        const auto count = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(
                std::max(bounds.first, bounds.second))
            - std::min(bounds.first, bounds.second) + 1);
        bool positional{};
        bool keyed{};
        std::optional<const Expression*> default_value;
        std::map<std::int32_t, const Expression*> explicit_values;
        for (std::size_t member = 0;
             member < pattern.operands.size(); ++member) {
          const auto& choice = pattern.aggregate_choices[member];
          const auto& choices =
              pattern.aggregate_choice_expressions[member];
          positional |= choice.empty();
          keyed |= !choice.empty();
          if (choice.empty() && choices.empty()) {
            continue;
          }
          if (choice == "default" && choices.size() == 1
              && choices.front().kind
                  == ExpressionKind::DefaultChoice) {
            if (default_value) {
              report(
                  "FSIM-ELAB-SVPATTERN-005",
                  "a multidimensional assignment-pattern dimension "
                  "cannot contain more than one default",
                  pattern.operands[member].span);
              return false;
            }
            default_value = &pattern.operands[member];
            continue;
          }
          if (choice != "@key" || choices.size() != 1) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "a multidimensional assignment-pattern association has "
                "inconsistent key metadata",
                pattern.operands[member].span);
            return false;
          }
          const auto key = static_integer_value(choices.front());
          if (!key
              || *key < std::min(bounds.first, bounds.second)
              || *key > std::max(bounds.first, bounds.second)) {
            report(
                key ? "FSIM-ELAB-SVPATTERN-007"
                    : "FSIM-ELAB-SVPATTERN-006",
                key
                    ? "a multidimensional assignment-pattern key is "
                      "outside the declared dimension"
                    : "multidimensional assignment-pattern keys must be "
                      "locally constant known integral values",
                choices.front().span);
            return false;
          }
          if (!explicit_values.emplace(
                   static_cast<std::int32_t>(*key),
                   &pattern.operands[member]).second) {
            report(
                "FSIM-ELAB-SVPATTERN-007",
                "multidimensional assignment-pattern keys must be unique",
                choices.front().span);
            return false;
          }
        }
        if (positional && keyed) {
          report(
              "FSIM-ELAB-SVPATTERN-004",
              "one multidimensional assignment-pattern dimension cannot "
              "mix positional and keyed/default members",
              pattern.span);
          return false;
        }
        if ((positional && pattern.operands.size() != count)
            || (keyed && !default_value)) {
          report(
              keyed ? "FSIM-ELAB-SVPATTERN-005"
                    : "FSIM-ELAB-SVPATTERN-002",
              keyed
                  ? "a keyed multidimensional assignment-pattern "
                    "dimension requires exactly one default"
                  : "a positional multidimensional assignment-pattern "
                    "dimension must match its declared element count",
              pattern.span);
          return false;
        }
        for (std::uint64_t ordinal = 0; ordinal < count; ++ordinal) {
          const auto declared = static_cast<std::int32_t>(
              static_cast<std::int64_t>(bounds.first)
              + (bounds.first >= bounds.second
                     ? -static_cast<std::int64_t>(ordinal)
                     : static_cast<std::int64_t>(ordinal)));
          const Expression* value = nullptr;
          if (positional) {
            value = &pattern.operands[ordinal];
          } else if (const auto found = explicit_values.find(declared);
                     found != explicit_values.end()) {
            value = found->second;
          } else if (default_value) {
            value = *default_value;
          }
          if (value == nullptr) {
            report(
                "FSIM-ELAB-SVPATTERN-002",
                "a multidimensional assignment pattern leaves a declared "
                "element uncovered",
                pattern.span);
            return false;
          }
          const auto linear = base * count + ordinal;
          if (dimension + 1U < runtime_type.dimensions.size()) {
            if (!lower_dimension(*value, dimension + 1U, linear)) {
              return false;
            }
            continue;
          }
          auto lowered = lower_expression(
              *value, runtime_type.element_width, &element_type);
          if (!lowered) {
            return false;
          }
          if (register_width(*lowered) != runtime_type.element_width) {
            *lowered = resize_register(
                *lowered,
                runtime_type.element_width,
                is_signed_expression(*value));
          }
          const auto index =
              allocate_register(32, frontend::ValueDomain::Bit2);
          process_.operations.emplace_back(LoadConstant{
              index, unsigned_value(linear, 32)});
          process_.operations.emplace_back(ContainerWrite{
              destination, index, *lowered, true, true});
        }
        return true;
      };
  return lower_dimension(expression, 0, 0)
      ? std::optional<ContainerRegisterId>{destination}
      : std::nullopt;
}

} // namespace fsim::elaboration
