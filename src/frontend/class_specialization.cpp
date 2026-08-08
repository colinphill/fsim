// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/class_specialization.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>

namespace fsim::frontend {

namespace {

using ValueEnvironment = std::map<std::string, std::int64_t>;
using StringEnvironment = std::map<std::string, std::string>;
using TypeEnvironment = std::map<std::string, Type>;

[[nodiscard]] std::optional<std::string> evaluate_string(
    const Expression& expression,
    const StringEnvironment& values) {
  if (expression.kind == ExpressionKind::StringLiteral) {
    return expression.decoded_string.value_or(expression.text);
  }
  if (expression.kind == ExpressionKind::Identifier) {
    if (const auto found = values.find(expression.text);
        found != values.end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> checked_add(
    const std::int64_t left,
    const std::int64_t right) {
  if ((right > 0
       && left > std::numeric_limits<std::int64_t>::max() - right)
      || (right < 0
          && left < std::numeric_limits<std::int64_t>::min() - right)) {
    return std::nullopt;
  }
  return left + right;
}

[[nodiscard]] std::optional<std::int64_t> checked_subtract(
    const std::int64_t left,
    const std::int64_t right) {
  if ((right < 0
       && left > std::numeric_limits<std::int64_t>::max() + right)
      || (right > 0
          && left < std::numeric_limits<std::int64_t>::min() + right)) {
    return std::nullopt;
  }
  return left - right;
}

[[nodiscard]] std::optional<std::int64_t> checked_multiply(
    const std::int64_t left,
    const std::int64_t right) {
  if (left == 0 || right == 0) return 0;
  if ((left == -1
       && right == std::numeric_limits<std::int64_t>::min())
      || (right == -1
          && left == std::numeric_limits<std::int64_t>::min())) {
    return std::nullopt;
  }
  if ((left > 0
       && ((right > 0
            && left > std::numeric_limits<std::int64_t>::max() / right)
           || (right < 0
               && right
                   < std::numeric_limits<std::int64_t>::min() / left)))
      || (left < 0
          && ((right > 0
               && left
                   < std::numeric_limits<std::int64_t>::min() / right)
              || (right < 0
                  && left
                      < std::numeric_limits<std::int64_t>::max() / right)))) {
    return std::nullopt;
  }
  return left * right;
}

void diagnose(
    SystemVerilogClassSpecializationResult& result,
    std::string code,
    std::string message,
    const SourceSpan& span) {
  result.diagnostics.push_back({
      DiagnosticSeverity::Error,
      std::move(code),
      std::move(message),
      span,
      span.expansion_stack});
}

[[nodiscard]] std::optional<std::int64_t> evaluate(
    const Expression& expression,
    const ValueEnvironment& environment) {
  if (expression.kind == ExpressionKind::Identifier) {
    const auto found = environment.find(expression.text);
    return found == environment.end()
        ? std::nullopt
        : std::optional{found->second};
  }
  if (expression.kind == ExpressionKind::IntegerLiteral) {
    std::int64_t value{};
    const auto* begin = expression.text.data();
    const auto* end = begin + expression.text.size();
    const auto converted = std::from_chars(begin, end, value, 10);
    return converted.ec == std::errc{} && converted.ptr == end
        ? std::optional{value}
        : std::nullopt;
  }
  if (expression.kind == ExpressionKind::Unary
      && expression.operands.size() == 1U) {
    const auto value = evaluate(expression.operands.front(), environment);
    if (!value) {
      return std::nullopt;
    }
    if (expression.text == "+") return value;
    if (expression.text == "-") {
      if (*value == std::numeric_limits<std::int64_t>::min()) {
        return std::nullopt;
      }
      return -*value;
    }
    if (expression.text == "~") return ~*value;
    if (expression.text == "!") return *value == 0 ? 1 : 0;
    return std::nullopt;
  }
  if (expression.kind != ExpressionKind::Binary
      || expression.operands.size() != 2U) {
    return std::nullopt;
  }
  const auto left = evaluate(expression.operands[0], environment);
  const auto right = evaluate(expression.operands[1], environment);
  if (!left || !right) {
    return std::nullopt;
  }
  if (expression.text == "+") return checked_add(*left, *right);
  if (expression.text == "-") return checked_subtract(*left, *right);
  if (expression.text == "*") return checked_multiply(*left, *right);
  if (expression.text == "/") {
    return *right == 0 ? std::nullopt
                       : std::optional{*left / *right};
  }
  if (expression.text == "%") {
    return *right == 0 ? std::nullopt
                       : std::optional{*left % *right};
  }
  if (expression.text == "&") return *left & *right;
  if (expression.text == "|") return *left | *right;
  if (expression.text == "^") return *left ^ *right;
  if (expression.text == "==") return *left == *right ? 1 : 0;
  if (expression.text == "!=") return *left != *right ? 1 : 0;
  if (expression.text == "<") return *left < *right ? 1 : 0;
  if (expression.text == "<=") return *left <= *right ? 1 : 0;
  if (expression.text == ">") return *left > *right ? 1 : 0;
  if (expression.text == ">=") return *left >= *right ? 1 : 0;
  return std::nullopt;
}

[[nodiscard]] std::string type_identity(const Type& type) {
  const auto expression_identity =
      [](const auto& self, const Expression& expression) -> std::string {
    if (expression.operands.empty()) {
      return expression.text;
    }
    if (expression.operands.size() == 1U) {
      return expression.text
          + self(self, expression.operands.front());
    }
    std::string retained{"("};
    for (std::size_t index = 0;
         index < expression.operands.size(); ++index) {
      if (index != 0U) retained += expression.text;
      retained += self(self, expression.operands[index]);
    }
    retained += ')';
    return retained;
  };
  std::ostringstream identity;
  if (!type.systemverilog_class_declaration.empty()) {
    identity << "class:" << type.systemverilog_class_declaration;
  } else if (!type.named_type.empty()) {
    identity << "named:" << type.named_type;
  } else {
    identity << "domain:" << static_cast<unsigned>(type.domain)
             << ':' << type.spelling;
  }
  identity << ':' << (type.is_signed ? 's' : 'u');
  if (!type.systemverilog_packed_dimensions.empty()) {
    for (const auto& dimension :
         type.systemverilog_packed_dimensions) {
      identity << '['
               << expression_identity(
                      expression_identity, dimension.left)
               << ':'
               << expression_identity(
                      expression_identity, dimension.right)
               << ']';
    }
  } else if (type.packed_range) {
    identity << '[' << type.packed_range->left
             << ':' << type.packed_range->right << ']';
  }
  return identity.str();
}

[[nodiscard]] Type specialize_type(
    Type type,
    const ValueEnvironment& values,
    const TypeEnvironment& types) {
  const auto outer_container = type.systemverilog_container;
  std::set<std::string> substituted;
  while (!type.named_type.empty()
         && substituted.insert(type.named_type).second) {
    const auto replacement = types.find(type.named_type);
    if (replacement == types.end()) {
      break;
    }
    type = replacement->second;
  }
  if (outer_container) {
    type.systemverilog_container = outer_container;
  }
  if (type.packed_range_expression) {
    const auto left = evaluate(
        type.packed_range_expression->left, values);
    const auto right = evaluate(
        type.packed_range_expression->right, values);
    if (left && right) {
      type.packed_range = PackedRange{*left, *right, *left >= *right};
      type.packed_range_expression.reset();
    }
  }
  if (!type.systemverilog_packed_dimensions.empty()) {
    std::uint64_t flattened_width = 1U;
    bool concrete = true;
    for (auto& dimension :
         type.systemverilog_packed_dimensions) {
      const auto left = evaluate(dimension.left, values);
      const auto right = evaluate(dimension.right, values);
      if (!left || !right) {
        concrete = false;
        continue;
      }
      dimension.left = Expression{
          ExpressionKind::IntegerLiteral,
          std::to_string(*left),
          {},
          dimension.left.span};
      dimension.right = Expression{
          ExpressionKind::IntegerLiteral,
          std::to_string(*right),
          {},
          dimension.right.span};
      const auto width =
          PackedRange{*left, *right, *left >= *right}.width();
      constexpr auto maximum_width =
          static_cast<std::uint64_t>(
              std::numeric_limits<std::int64_t>::max()) + 1U;
      if (width == 0U || flattened_width > maximum_width / width) {
        concrete = false;
      } else {
        flattened_width *= width;
      }
    }
    if (concrete) {
      type.packed_range = PackedRange{
          static_cast<std::int64_t>(flattened_width - 1U),
          0,
          true};
    } else {
      type.packed_range.reset();
    }
  }
  for (auto& member : type.packed_members) {
    if (member.packed_range_expression) {
      const auto left = evaluate(
          member.packed_range_expression->left, values);
      const auto right = evaluate(
          member.packed_range_expression->right, values);
      if (left && right) {
        member.packed_range = PackedRange{
            *left, *right, *left >= *right};
        member.packed_range_expression.reset();
      }
    }
    for (auto& nested : member.nested_types) {
      nested = specialize_type(std::move(nested), values, types);
    }
  }
  if (type.systemverilog_container) {
    for (auto& element :
         type.systemverilog_container->element_types) {
      element = specialize_type(std::move(element), values, types);
    }
  }
  for (auto& actual : type.systemverilog_class_parameter_actuals) {
    if (actual.type_actual) {
      actual.type_actual = std::make_shared<Type>(
          specialize_type(
              std::move(*actual.type_actual), values, types));
    } else if (const auto value = evaluate(actual.value, values)) {
      actual.value = Expression{
          ExpressionKind::IntegerLiteral,
          std::to_string(*value),
          {},
          actual.value.span};
    }
  }
  return type;
}

[[nodiscard]] std::optional<std::size_t> storage_width(const Type& type) {
  if (!type.systemverilog_class_declaration.empty()
      || type.domain == ValueDomain::String
      || type.systemverilog_container
      || type.packed_aggregate
          == PackedAggregateKind::UnpackedStruct
      || type.packed_aggregate
          == PackedAggregateKind::UnpackedUnion) {
    return 32U;
  }
  if (!type.packed_members.empty()) {
    std::size_t total = 0;
    for (const auto& member : type.packed_members) {
      std::optional<std::uint64_t> width;
      if (!member.nested_types.empty()) {
        width = member.nested_types.front().width();
      } else if (member.packed_range) {
        width = member.packed_range->width();
      } else if (
          member.domain == ValueDomain::Bit2
          || member.domain == ValueDomain::Logic4
          || member.domain == ValueDomain::Logic9
          || member.domain == ValueDomain::Boolean) {
        width = 1U;
      } else if (member.domain == ValueDomain::Integer) {
        width = 32U;
      }
      if (!width
          || *width > std::numeric_limits<std::size_t>::max() - total) {
        return std::nullopt;
      }
      total += static_cast<std::size_t>(*width);
    }
    return total;
  }
  const auto width = type.width();
  if (!width || *width > std::numeric_limits<std::size_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(*width);
}

void collect_declarations(
    const SystemVerilogClassDeclaration& declaration,
    std::map<std::string, const SystemVerilogClassDeclaration*>& index) {
  index.emplace(declaration.canonical_identity, &declaration);
  for (const auto& nested : declaration.nested_classes) {
    collect_declarations(nested, index);
  }
}

}  // namespace

bool SystemVerilogClassSpecializationResult::ok() const {
  return !has_errors(diagnostics);
}

SystemVerilogClassSpecializationResult
specialize_systemverilog_classes(const ParsedDesign& design) {
  SystemVerilogClassSpecializationResult result;
  std::map<std::string, const SystemVerilogClassDeclaration*> declarations;
  for (const auto& unit : design.units) {
    for (const auto& declaration : unit.systemverilog_classes) {
      collect_declarations(declaration, declarations);
    }
  }
  for (const auto& declaration : design.systemverilog_classes) {
    collect_declarations(declaration, declarations);
  }
  std::map<std::string, Type> package_aliases;
  std::set<std::string> ambiguous_aliases;
  for (const auto& unit : design.units) {
    for (const auto& alias : unit.type_aliases) {
      const auto retain = [&](const std::string& name) {
        if (ambiguous_aliases.contains(name)) {
          return;
        }
        if (!package_aliases.emplace(name, alias.type).second) {
          package_aliases.erase(name);
          ambiguous_aliases.insert(name);
        }
      };
      retain(alias.name);
      retain(unit.name + "::" + alias.name);
    }
  }
  std::map<std::string, Type> class_aliases;
  std::set<std::string> ambiguous_class_aliases;
  for (const auto& [identity, declaration] : declarations) {
    for (const auto& alias : declaration->type_aliases) {
      class_aliases.emplace(identity + "::" + alias.name, alias.type);
      const auto short_name =
          declaration->name + "::" + alias.name;
      if (ambiguous_class_aliases.contains(short_name)) {
        continue;
      }
      if (!class_aliases.emplace(short_name, alias.type).second) {
        class_aliases.erase(short_name);
        ambiguous_class_aliases.insert(short_name);
      }
    }
  }
  ValueEnvironment package_values;
  std::set<std::string> ambiguous_package_values;
  for (const auto& unit : design.units) {
    ValueEnvironment unit_values;
    for (const auto& parameter : unit.parameters) {
      if (parameter.kind == ParameterKind::Type
          || parameter.type.domain == ValueDomain::String) {
        continue;
      }
      const auto value = evaluate(parameter.default_value, unit_values);
      if (!value) {
        continue;
      }
      unit_values.insert_or_assign(parameter.name, *value);
      package_values.insert_or_assign(
          unit.name + "::" + parameter.name, *value);
      if (ambiguous_package_values.contains(parameter.name)) {
        continue;
      }
      if (!package_values.emplace(parameter.name, *value).second) {
        package_values.erase(parameter.name);
        ambiguous_package_values.insert(parameter.name);
      }
    }
  }

  const auto source_profile = [](const SystemVerilogClassMethod& method) {
    std::string profile = method.name + ':'
        + std::to_string(static_cast<unsigned>(method.kind)) + '(';
    for (const auto& argument : method.arguments) {
      profile += std::to_string(static_cast<unsigned>(argument.direction));
      profile += ':' + type_identity(argument.type) + ';';
    }
    profile += ')';
    return profile;
  };
  struct SlotTable {
    std::map<std::string, std::uint32_t> profiles;
    std::map<std::string, std::uint32_t> methods;
  };
  std::map<std::string, SlotTable> slot_tables;
  std::set<std::string> active_slot_tables;
  std::function<void(const SystemVerilogClassDeclaration&)> build_slots;
  build_slots = [&](const SystemVerilogClassDeclaration& declaration) {
    if (slot_tables.contains(declaration.canonical_identity)) return;
    if (!active_slot_tables.insert(declaration.canonical_identity).second) {
      return;
    }
    SlotTable table;
    if (declaration.base
        && !declaration.base->declaration_identity.empty()) {
      if (const auto base = declarations.find(
              declaration.base->declaration_identity);
          base != declarations.end()) {
        build_slots(*base->second);
        table = slot_tables[base->first];
      }
    }
    std::uint32_t next_slot{};
    for (const auto& [profile, slot] : table.profiles) {
      (void)profile;
      if (slot == std::numeric_limits<std::uint32_t>::max()) {
        next_slot = slot;
        break;
      }
      next_slot = std::max(next_slot, static_cast<std::uint32_t>(slot + 1U));
    }
    for (const auto& method : declaration.methods) {
      if (method.kind == SystemVerilogClassMethodKind::Constructor) continue;
      const auto profile = source_profile(method);
      const auto inherited = table.profiles.find(profile);
      if (inherited != table.profiles.end()) {
        table.methods[method.canonical_identity] = inherited->second;
      } else if (method.is_virtual || method.is_pure) {
        if (next_slot == std::numeric_limits<std::uint32_t>::max()) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-011",
              "class virtual-method slot domain is exhausted",
              method.span);
          continue;
        }
        table.profiles.emplace(profile, next_slot);
        table.methods.emplace(method.canonical_identity, next_slot);
        ++next_slot;
      }
    }
    active_slot_tables.erase(declaration.canonical_identity);
    slot_tables.emplace(declaration.canonical_identity, std::move(table));
  };
  for (const auto& [identity, declaration] : declarations) {
    (void)identity;
    build_slots(*declaration);
  }

  using Actual = SystemVerilogClassTypeActual;
  struct PendingSpecialization {
    const SystemVerilogClassDeclaration* declaration{};
    std::vector<Actual> actuals;
    SourceSpan span;
  };
  std::vector<PendingSpecialization> pending_specializations;
  std::map<std::string, std::size_t> materialized;
  std::set<std::string> active;
  std::function<std::optional<std::string>(
      const SystemVerilogClassDeclaration&,
      const std::vector<Actual>&,
      const SourceSpan&)> specialize;
  specialize = [&](const SystemVerilogClassDeclaration& declaration,
                   const std::vector<Actual>& actuals,
                   const SourceSpan& reference_span)
      -> std::optional<std::string> {
    ValueEnvironment values = package_values;
    std::set<std::string> bound_values;
    StringEnvironment string_values;
    TypeEnvironment types;
    std::vector<std::pair<std::string, std::string>> display_values;
    std::vector<std::pair<std::string, std::string>> identity_values;
    std::vector<bool> assigned(declaration.parameters.size(), false);
    bool saw_named = false;
    bool saw_positional = false;
    for (std::size_t actual_index = 0;
         actual_index < actuals.size(); ++actual_index) {
      const auto& actual = actuals[actual_index];
      std::size_t formal_index = actual_index;
      if (actual.name) {
        saw_named = true;
        const auto found = std::ranges::find(
            declaration.parameters,
            *actual.name,
            &ParameterDeclaration::name);
        if (found == declaration.parameters.end()) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-001",
              "class '" + declaration.canonical_identity
                  + "' has no parameter named '" + *actual.name + "'",
              actual.span);
          continue;
        }
        formal_index = static_cast<std::size_t>(
            std::distance(declaration.parameters.begin(), found));
      } else {
        saw_positional = true;
      }
      if (formal_index >= declaration.parameters.size()) {
        diagnose(
            result,
            "FSIM-SV-CLASS-SPEC-001",
            "too many class parameter actuals for '"
                + declaration.canonical_identity + "'",
            actual.span);
        continue;
      }
      if (assigned[formal_index]) {
        diagnose(
            result,
            "FSIM-SV-CLASS-SPEC-002",
            "duplicate class parameter actual '"
                + declaration.parameters[formal_index].name + "'",
            actual.span);
        continue;
      }
      assigned[formal_index] = true;
      const auto& formal = declaration.parameters[formal_index];
      if (formal.kind == ParameterKind::Type) {
        std::optional<Type> actual_type;
        if (actual.type_actual) {
          actual_type = *actual.type_actual;
        } else if (
            actual.value.kind == ExpressionKind::Identifier
            && actual.value.operands.empty()) {
          Type inferred;
          inferred.spelling = actual.value.text;
          inferred.named_type = actual.value.text;
          inferred.named_type_span = actual.value.span;
          if (const auto alias =
                  package_aliases.find(actual.value.text);
              alias != package_aliases.end()) {
            inferred = alias->second;
          }
          const SystemVerilogClassDeclaration* selected = nullptr;
          for (const auto& [identity, candidate] : declarations) {
            const bool matches =
                candidate->name == actual.value.text
                || identity == actual.value.text
                || (identity.size() > actual.value.text.size()
                    && identity.ends_with("::" + actual.value.text));
            if (!matches) {
              continue;
            }
            if (selected != nullptr && selected != candidate) {
              selected = nullptr;
              break;
            }
            selected = candidate;
          }
          if (selected != nullptr) {
            inferred.systemverilog_class_name = selected->name;
            inferred.systemverilog_class_declaration =
                selected->canonical_identity;
          }
          actual_type = std::move(inferred);
        }
        if (!actual_type) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-003",
              "type parameter '" + formal.name
                  + "' requires a data-type actual",
              actual.span);
        } else {
          if (const auto replacement =
                  types.find(actual_type->named_type);
              replacement != types.end()) {
            actual_type = replacement->second;
          }
          types.emplace(formal.name, std::move(*actual_type));
        }
      } else {
        const bool string_parameter =
            formal.type.domain == ValueDomain::String;
        const auto value = string_parameter
            ? std::optional<std::int64_t>{}
            : evaluate(actual.value, values);
        const auto string_value = string_parameter
            ? evaluate_string(actual.value, string_values)
            : std::optional<std::string>{};
        if (!value && !string_value) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-004",
              "value actual for class parameter '" + formal.name
                  + "' is not locally constant",
              actual.span);
        } else if (string_value) {
          string_values.emplace(formal.name, *string_value);
        } else {
          values.insert_or_assign(formal.name, *value);
          bound_values.insert(formal.name);
        }
      }
    }
    if (saw_named && saw_positional) {
      diagnose(
          result,
          "FSIM-SV-CLASS-SPEC-005",
          "named and positional class parameter actuals cannot be mixed",
          reference_span);
    }
    bool symbolic_type_actual = false;
    for (std::size_t index = 0;
         index < declaration.parameters.size(); ++index) {
      const auto& formal = declaration.parameters[index];
      if (formal.kind == ParameterKind::Type) {
        if (!types.contains(formal.name)) {
          if (formal.default_type) {
            types.emplace(formal.name, *formal.default_type);
          } else {
            diagnose(
                result,
                "FSIM-SV-CLASS-SPEC-006",
                "type parameter '" + formal.name + "' has no actual or default",
                formal.span);
            continue;
          }
        }
        auto normalized =
            specialize_type(types.at(formal.name), values, types);
        types.insert_or_assign(formal.name, normalized);
        if (normalized.domain == ValueDomain::Unknown
            && !normalized.packed_range
            && normalized.packed_members.empty()
            && !normalized.systemverilog_container
            && normalized.systemverilog_class_declaration.empty()
            && std::ranges::any_of(
                declaration.parameters,
                [&](const ParameterDeclaration& candidate) {
                  return candidate.kind == ParameterKind::Type
                      && candidate.name == normalized.named_type;
                })) {
          symbolic_type_actual = true;
        }
        const auto identity = type_identity(types.at(formal.name));
        display_values.emplace_back(formal.name, identity);
        identity_values.emplace_back(formal.name, identity);
      } else {
        const bool string_parameter =
            formal.type.domain == ValueDomain::String;
        if (!bound_values.contains(formal.name)
            && !string_values.contains(formal.name)) {
          const auto value = string_parameter
              ? std::optional<std::int64_t>{}
              : evaluate(formal.default_value, values);
          const auto string_value = string_parameter
              ? evaluate_string(
                    formal.default_value, string_values)
              : std::optional<std::string>{};
          if (!value && !string_value) {
            diagnose(
                result,
                "FSIM-SV-CLASS-SPEC-007",
                "default for class parameter '" + formal.name
                    + "' is not locally constant",
                formal.span);
            continue;
          } else if (string_value) {
            string_values.emplace(formal.name, *string_value);
          } else {
            values.insert_or_assign(formal.name, *value);
            bound_values.insert(formal.name);
          }
        }
        const auto string_value =
            string_values.find(formal.name);
        const auto spelling =
            string_value == string_values.end()
                ? std::to_string(values.at(formal.name))
                : string_value->second;
        display_values.emplace_back(formal.name, spelling);
        identity_values.emplace_back(
            formal.name,
            type_identity(formal.type) + "="
                + (string_value == string_values.end()
                       ? spelling
                       : "s" + std::to_string(spelling.size())
                             + ":" + spelling));
      }
    }
    if (symbolic_type_actual) {
      return std::nullopt;
    }

    std::ostringstream identity;
    identity << declaration.canonical_identity << '<';
    for (const auto& [name, value] : identity_values) {
      identity << name << '=' << value << ';';
    }
    identity << '>';
    const auto specialization_identity = identity.str();
    if (materialized.contains(specialization_identity)) {
      return specialization_identity;
    }
    if (!active.insert(specialization_identity).second) {
      diagnose(
          result,
          "FSIM-SV-CLASS-SPEC-008",
          "recursive class specialization '" + specialization_identity + "'",
          reference_span);
      return std::nullopt;
    }

    SystemVerilogClassSpecialization retained;
    retained.declaration_identity = declaration.canonical_identity;
    retained.specialization_identity = specialization_identity;
    retained.parameter_values = std::move(display_values);
    retained.parameter_identity_values = std::move(identity_values);
    const auto source = std::string{physical_source(declaration.span)};
    if (!source.empty()) {
      retained.source_dependencies.push_back(source);
    }

    if (declaration.base
        && !declaration.base->declaration_identity.empty()) {
      const auto base = declarations.find(
          declaration.base->declaration_identity);
      if (base != declarations.end()) {
        std::vector<Actual> base_actuals;
        for (const auto& actual : declaration.base->parameter_actuals) {
          Actual converted;
          if (!actual.name.empty()) converted.name = actual.name;
          converted.value = actual.value;
          if (const auto value = evaluate(actual.value, values)) {
            converted.value = Expression{
                ExpressionKind::IntegerLiteral,
                std::to_string(*value),
                {},
                actual.value.span};
          }
          if (actual.type_actual) {
            auto type = *actual.type_actual;
            if (const auto replacement = types.find(type.named_type);
                replacement != types.end()) {
              type = replacement->second;
            }
            converted.type_actual = std::make_shared<Type>(
                specialize_type(std::move(type), values, types));
          }
          converted.span = actual.span;
          base_actuals.push_back(std::move(converted));
        }
        if (const auto base_identity = specialize(
                *base->second, base_actuals, declaration.base->span)) {
          retained.base_specialization_identity = *base_identity;
          const auto existing = materialized.find(*base_identity);
          if (existing != materialized.end()) {
            const auto& base_specialization =
                result.specializations[existing->second];
            retained.instance_bit_width =
                base_specialization.instance_bit_width;
            for (const auto& property : base_specialization.properties) {
              if (!property.is_static) {
                retained.properties.push_back(property);
              }
            }
            retained.source_dependencies.insert(
                retained.source_dependencies.end(),
                base_specialization.source_dependencies.begin(),
                base_specialization.source_dependencies.end());
          }
        }
      }
    }

    std::map<std::string, Type> aliases;
    std::map<std::string, Type> scoped_aliases;
    for (const auto& alias : declaration.type_aliases) {
      aliases.emplace(
          alias.name,
          specialize_type(alias.type, values, types));
    }
    std::function<Type(Type, std::set<std::string>)>
        expand_type_aliases;
    expand_type_aliases =
        [&](Type type, std::set<std::string> expanded) -> Type {
      type = specialize_type(std::move(type), values, types);
      while (!type.named_type.empty()
             && expanded.insert(type.named_type).second) {
        const Type* replacement = nullptr;
        if (const auto scoped_alias =
                scoped_aliases.find(type.named_type);
            scoped_alias != scoped_aliases.end()) {
          replacement = &scoped_alias->second;
        } else if (const auto local_alias = aliases.find(type.named_type);
                   local_alias != aliases.end()) {
          replacement = &local_alias->second;
        } else if (const auto package_alias =
                       package_aliases.find(type.named_type);
                   package_alias != package_aliases.end()) {
          replacement = &package_alias->second;
        } else if (const auto class_alias =
                       class_aliases.find(type.named_type);
                   class_alias != class_aliases.end()) {
          replacement = &class_alias->second;
        }
        if (replacement == nullptr) {
          break;
        }
        const auto outer_container = type.systemverilog_container;
        type = specialize_type(*replacement, values, types);
        if (outer_container) {
          type.systemverilog_container = outer_container;
        }
      }
      for (auto& member : type.packed_members) {
        for (auto& nested : member.nested_types) {
          nested = expand_type_aliases(
              std::move(nested), expanded);
        }
      }
      if (type.systemverilog_container) {
        for (auto& element :
             type.systemverilog_container->element_types) {
          element = expand_type_aliases(
              std::move(element), expanded);
        }
      }
      for (auto& actual :
           type.systemverilog_class_parameter_actuals) {
        if (actual.type_actual) {
          actual.type_actual = std::make_shared<Type>(
              expand_type_aliases(
                  std::move(*actual.type_actual), expanded));
        }
      }
      return type;
    };
    const auto queue_type_specializations =
        [&](const auto& self,
            const Type& type,
            const SourceSpan& span) -> void {
      const auto concrete_actual = [](const Actual& actual) {
        if (actual.type_actual) {
          const auto& actual_type = *actual.type_actual;
          return actual_type.domain != ValueDomain::Unknown
              || actual_type.packed_range
              || !actual_type.packed_members.empty()
              || actual_type.systemverilog_container
              || !actual_type.systemverilog_class_declaration.empty();
        }
        return actual.value.kind == ExpressionKind::IntegerLiteral
            || actual.value.kind == ExpressionKind::StringLiteral;
      };
      if (!type.systemverilog_class_declaration.empty()
          && !type.systemverilog_class_parameter_actuals.empty()
          && (std::ranges::all_of(
                  type.systemverilog_class_parameter_actuals,
                  concrete_actual)
              || std::ranges::any_of(
                  type.systemverilog_class_parameter_actuals,
                  [](const Actual& actual) {
                    return !actual.type_actual
                        && (actual.value.kind
                                == ExpressionKind::IntegerLiteral
                            || actual.value.kind
                                == ExpressionKind::StringLiteral);
                  }))) {
        if (const auto found = declarations.find(
                type.systemverilog_class_declaration);
            found != declarations.end()) {
          pending_specializations.push_back(PendingSpecialization{
              found->second,
              type.systemverilog_class_parameter_actuals,
              span});
        }
      }
      for (const auto& actual :
           type.systemverilog_class_parameter_actuals) {
        if (actual.type_actual) {
          self(self, *actual.type_actual, actual.span);
        }
      }
      if (type.systemverilog_container) {
        for (const auto& element :
             type.systemverilog_container->element_types) {
          self(self, element, span);
        }
      }
      for (const auto& member : type.packed_members) {
        for (const auto& nested : member.nested_types) {
          self(self, nested, span);
        }
      }
    };
    for (const auto& property : declaration.properties) {
      auto type = expand_type_aliases(
          property.declaration.type, {});
      queue_type_specializations(
          queue_type_specializations, type, property.span);
      const auto width = storage_width(type);
      if (!width) {
        diagnose(
            result,
            "FSIM-SV-CLASS-SPEC-009",
            "class property '" + property.declaration.name
                + "' has no finite specialized layout for type '"
                + type_identity(type) + "' in specialization '"
                + specialization_identity + "'",
            property.span);
        continue;
      }
      SystemVerilogClassPropertyLayout layout;
      layout.name = property.declaration.name;
      layout.owner_identity = declaration.canonical_identity;
      layout.type = std::move(type);
      layout.bit_width = *width;
      layout.is_static = property.is_static;
      layout.is_rand = property.is_rand;
      layout.is_randc = property.is_randc;
      layout.initializer = property.declaration.initializer;
      if (layout.initializer) {
        if (const auto value = evaluate(*layout.initializer, values)) {
          layout.initializer = Expression{
              ExpressionKind::IntegerLiteral,
              std::to_string(*value),
              {},
              layout.initializer->span};
        }
      }
      if (property.is_static) {
        ++retained.static_property_count;
      } else {
        if (*width > std::numeric_limits<std::size_t>::max()
                         - retained.instance_bit_width) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-010",
              "class instance layout exceeds host addressable storage",
              property.span);
          continue;
        }
        layout.bit_offset = retained.instance_bit_width;
        retained.instance_bit_width += *width;
      }
      retained.properties.push_back(std::move(layout));
    }
    for (const auto& method : declaration.methods) {
      struct PriorAlias {
        std::string name;
        std::optional<Type> type;
      };
      std::vector<PriorAlias> prior_method_aliases;
      SystemVerilogClassMethodProfile profile;
      profile.name = method.name;
      profile.canonical_identity = method.canonical_identity;
      profile.kind = method.kind;
      profile.return_type = expand_type_aliases(
          method.return_type, {});
      profile.lifetime = method.lifetime;
      profile.is_static = method.is_static;
      profile.is_virtual = method.is_virtual;
      profile.is_pure = method.is_pure;
      profile.arguments = method.arguments;
      for (auto& argument : profile.arguments) {
        argument.type = expand_type_aliases(
            argument.type, {});
      }
      profile.type_aliases = method.type_aliases;
      for (auto& alias : profile.type_aliases) {
        alias.type = specialize_type(alias.type, values, types);
        const auto prior = scoped_aliases.find(alias.name);
        prior_method_aliases.push_back({
            alias.name,
            prior == scoped_aliases.end()
                ? std::optional<Type>{}
                : std::optional<Type>{prior->second}});
        scoped_aliases.insert_or_assign(alias.name, alias.type);
      }
      for (auto& alias : profile.type_aliases) {
        alias.type = expand_type_aliases(alias.type, {});
        scoped_aliases.insert_or_assign(alias.name, alias.type);
      }
      profile.variables = method.variables;
      for (auto& variable : profile.variables) {
        variable.type = expand_type_aliases(variable.type, {});
        queue_type_specializations(
            queue_type_specializations, variable.type, variable.span);
      }
      profile.statements = method.statements;
      const auto specialize_statement_types =
          [&](const auto& self,
              std::vector<Statement>& statements) -> void {
            for (auto& statement : statements) {
              std::vector<PriorAlias> prior_statement_aliases;
              for (auto& alias : statement.type_aliases) {
                alias.type = specialize_type(alias.type, values, types);
                const auto prior = scoped_aliases.find(alias.name);
                prior_statement_aliases.push_back({
                    alias.name,
                    prior == scoped_aliases.end()
                        ? std::optional<Type>{}
                        : std::optional<Type>{prior->second}});
                scoped_aliases.insert_or_assign(alias.name, alias.type);
              }
              for (auto& alias : statement.type_aliases) {
                alias.type = expand_type_aliases(alias.type, {});
                scoped_aliases.insert_or_assign(alias.name, alias.type);
              }
              for (auto& variable : statement.declarations) {
                variable.type = expand_type_aliases(variable.type, {});
                queue_type_specializations(
                    queue_type_specializations,
                    variable.type,
                    variable.span);
              }
              for (auto& argument : statement.class_method_arguments) {
                argument.type = expand_type_aliases(argument.type, {});
              }
              self(self, statement.loop_updates);
              self(self, statement.statements);
              self(self, statement.else_statements);
              for (auto& alternative : statement.case_alternatives) {
                self(self, alternative.statements);
              }
              for (auto prior = prior_statement_aliases.rbegin();
                   prior != prior_statement_aliases.rend(); ++prior) {
                if (prior->type) {
                  scoped_aliases.insert_or_assign(
                      prior->name, std::move(*prior->type));
                } else {
                  scoped_aliases.erase(prior->name);
                }
              }
            }
          };
      specialize_statement_types(
          specialize_statement_types, profile.statements);
      if (const auto table = slot_tables.find(
              declaration.canonical_identity);
          table != slot_tables.end()) {
        if (const auto slot = table->second.methods.find(
                method.canonical_identity);
            slot != table->second.methods.end()) {
          profile.virtual_slot = slot->second;
          profile.is_virtual = true;
        }
      }
      std::ostringstream profile_identity;
      profile_identity << static_cast<unsigned>(method.kind)
                       << ':' << method.name << '(';
      for (const auto& argument : method.arguments) {
        profile_identity << static_cast<unsigned>(argument.direction)
                         << ':'
                         << type_identity(expand_type_aliases(
                                argument.type, {}))
                         << ';';
      }
      profile_identity << ")->"
                       << type_identity(expand_type_aliases(
                              method.return_type, {}));
      profile.profile_identity = profile_identity.str();
      queue_type_specializations(
          queue_type_specializations,
          profile.return_type,
          method.span);
      for (const auto& argument : profile.arguments) {
        queue_type_specializations(
            queue_type_specializations,
            argument.type,
            argument.span);
      }
      retained.methods.push_back(std::move(profile));
      for (auto prior = prior_method_aliases.rbegin();
           prior != prior_method_aliases.rend(); ++prior) {
        if (prior->type) {
          scoped_aliases.insert_or_assign(
              prior->name, std::move(*prior->type));
        } else {
          scoped_aliases.erase(prior->name);
        }
      }
    }
    std::ranges::sort(retained.source_dependencies);
    retained.source_dependencies.erase(
        std::unique(
            retained.source_dependencies.begin(),
            retained.source_dependencies.end()),
        retained.source_dependencies.end());
    active.erase(specialization_identity);
    const auto index = result.specializations.size();
    materialized.emplace(specialization_identity, index);
    result.specializations.push_back(std::move(retained));
    return specialization_identity;
  };

  for (const auto& [identity, declaration] : declarations) {
    (void)identity;
    (void)specialize(*declaration, {}, declaration->span);
  }
  for (std::size_t index = 0;
       index < pending_specializations.size(); ++index) {
    const auto pending = pending_specializations[index];
    (void)specialize(
        *pending.declaration, pending.actuals, pending.span);
  }
  return result;
}

}  // namespace fsim::frontend
