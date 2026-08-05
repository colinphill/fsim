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
using TypeEnvironment = std::map<std::string, Type>;

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
  if (type.packed_range) {
    identity << '[' << type.packed_range->left
             << ':' << type.packed_range->right << ']';
  }
  return identity.str();
}

[[nodiscard]] Type specialize_type(
    Type type,
    const ValueEnvironment& values,
    const TypeEnvironment& types) {
  if (const auto replacement = types.find(type.named_type);
      replacement != types.end()) {
    type = replacement->second;
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
  return type;
}

[[nodiscard]] std::optional<std::size_t> storage_width(const Type& type) {
  if (!type.systemverilog_class_declaration.empty()
      || type.domain == ValueDomain::String
      || type.systemverilog_container) {
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
    ValueEnvironment values;
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
        if (!actual.type_actual) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-003",
              "type parameter '" + formal.name
                  + "' requires a data-type actual",
              actual.span);
        } else {
          types.emplace(formal.name, *actual.type_actual);
        }
      } else {
        const auto value = evaluate(actual.value, values);
        if (!value) {
          diagnose(
              result,
              "FSIM-SV-CLASS-SPEC-004",
              "value actual for class parameter '" + formal.name
                  + "' is not locally constant",
              actual.span);
        } else {
          values.emplace(formal.name, *value);
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
        const auto identity = type_identity(types.at(formal.name));
        display_values.emplace_back(formal.name, identity);
        identity_values.emplace_back(formal.name, identity);
      } else {
        if (!values.contains(formal.name)) {
          const auto value = evaluate(formal.default_value, values);
          if (!value) {
            diagnose(
                result,
                "FSIM-SV-CLASS-SPEC-007",
                "default for class parameter '" + formal.name
                    + "' is not locally constant",
                formal.span);
            continue;
          }
          values.emplace(formal.name, *value);
        }
        const auto spelling = std::to_string(values.at(formal.name));
        display_values.emplace_back(formal.name, spelling);
        identity_values.emplace_back(
            formal.name,
            type_identity(formal.type) + "=" + spelling);
      }
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
            converted.type_actual = std::make_shared<Type>(std::move(type));
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
    for (const auto& alias : declaration.type_aliases) {
      aliases.emplace(
          alias.name,
          specialize_type(alias.type, values, types));
    }
    for (const auto& property : declaration.properties) {
      auto type = specialize_type(property.declaration.type, values, types);
      if (const auto alias = aliases.find(type.named_type);
          alias != aliases.end()) {
        type = alias->second;
      }
      const auto width = storage_width(type);
      if (!width) {
        diagnose(
            result,
            "FSIM-SV-CLASS-SPEC-009",
            "class property '" + property.declaration.name
                + "' has no finite specialized layout",
            property.span);
        continue;
      }
      SystemVerilogClassPropertyLayout layout;
      layout.name = property.declaration.name;
      layout.owner_identity = declaration.canonical_identity;
      layout.type = std::move(type);
      layout.bit_width = *width;
      layout.is_static = property.is_static;
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
      SystemVerilogClassMethodProfile profile;
      profile.name = method.name;
      profile.canonical_identity = method.canonical_identity;
      profile.kind = method.kind;
      profile.return_type = specialize_type(method.return_type, values, types);
      profile.lifetime = method.lifetime;
      profile.is_static = method.is_static;
      profile.is_virtual = method.is_virtual;
      profile.is_pure = method.is_pure;
      profile.arguments = method.arguments;
      profile.variables = method.variables;
      profile.statements = method.statements;
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
                         << type_identity(specialize_type(
                                argument.type, values, types))
                         << ';';
      }
      profile_identity << ")->"
                       << type_identity(specialize_type(
                              method.return_type, values, types));
      profile.profile_identity = profile_identity.str();
      retained.methods.push_back(std::move(profile));
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

  std::function<void(const Type&, const SourceSpan&)> scan_type;
  scan_type = [&](const Type& type, const SourceSpan& span) {
    if (!type.systemverilog_class_declaration.empty()
        && !type.systemverilog_class_parameter_actuals.empty()) {
      if (const auto found = declarations.find(
              type.systemverilog_class_declaration);
          found != declarations.end()) {
        (void)specialize(
            *found->second,
            type.systemverilog_class_parameter_actuals,
            span);
      }
    }
    for (const auto& actual : type.systemverilog_class_parameter_actuals) {
      if (actual.type_actual) scan_type(*actual.type_actual, actual.span);
    }
  };
  for (const auto& [identity, declaration] : declarations) {
    (void)identity;
    for (const auto& property : declaration->properties) {
      scan_type(property.declaration.type, property.span);
    }
    for (const auto& method : declaration->methods) {
      scan_type(method.return_type, method.span);
      for (const auto& argument : method.arguments) {
        scan_type(argument.type, argument.span);
      }
    }
  }
  return result;
}

}  // namespace fsim::frontend
