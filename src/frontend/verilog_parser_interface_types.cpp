// SPDX-License-Identifier: Apache-2.0
// Types named through an interface instance or interface port, as
// `typedef ifc.data_t data_t;` or `localparam type t = ifc.sub.t;`
// (IEEE 1800-2017 25.10 and 6.18). The interface typedef is copied into the
// referencing unit. The interface parameters it depends on become hidden
// parameters of the unit: a local instance supplies its overrides or the
// defaults; an interface port's are bound at elaboration from the connected
// instance's parameter values.
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <functional>
#include <map>

namespace fsim::frontend {

namespace {

using NameMap = std::map<std::string, std::string, std::less<>>;

void rename_expression(Expression& expression, const NameMap& names) {
  if (expression.kind == ExpressionKind::Identifier) {
    const auto dot = expression.text.find('.');
    const auto head = std::string_view{expression.text}.substr(
        0, dot == std::string::npos ? expression.text.size() : dot);
    if (const auto found = names.find(head); found != names.end()) {
      expression.text = found->second
          + (dot == std::string::npos ? std::string{}
                                      : expression.text.substr(dot));
    }
  }
  for (auto& operand : expression.operands) {
    rename_expression(operand, names);
  }
}

void rename_range(PackedRangeExpression& range, const NameMap& names) {
  rename_expression(range.left, names);
  rename_expression(range.right, names);
}

}  // namespace

std::optional<Type> VerilogParser::import_interface_type(DesignUnit& unit) {
  if (language_ != Language::SystemVerilog2017 || parsed_design_ == nullptr
      || !at(TokenKind::Identifier)) {
    return std::nullopt;
  }
  // The path: an instance or port, optionally indexed, then names.
  std::size_t cursor = 1U;
  if (at(TokenKind::LeftBracket, cursor)) {
    std::size_t depth{};
    do {
      if (at(TokenKind::LeftBracket, cursor)) {
        ++depth;
      } else if (at(TokenKind::RightBracket, cursor)) {
        --depth;
      }
      ++cursor;
    } while (depth != 0U && !at(TokenKind::EndOfFile, cursor));
  }
  if (!at(TokenKind::Dot, cursor) || !at(TokenKind::Identifier, cursor + 1U)) {
    return std::nullopt;
  }
  const auto find_interface = [&](const std::string_view name)
      -> const DesignUnit* {
    const auto found = std::ranges::find_if(
        parsed_design_->units, [&](const DesignUnit& candidate) {
          return candidate.kind == UnitKind::SystemVerilogInterface
              && candidate.name == name;
        });
    return found == parsed_design_->units.end() ? nullptr : &*found;
  };
  const auto first = current().text;
  const DesignUnit* interface_unit = nullptr;
  const std::vector<ParameterOverride>* overrides = nullptr;
  bool port = false;
  if (const auto instance = std::ranges::find(
          unit.instances, first, &Instance::name);
      instance != unit.instances.end()) {
    interface_unit = find_interface(instance->unit_name);
    overrides = &instance->parameter_overrides;
  } else if (const auto declared = std::ranges::find_if(
                 unit.ports, [&](const SignalDeclaration& candidate) {
                   return candidate.name == first
                       && !candidate.interface_type.empty();
                 });
             declared != unit.ports.end()) {
    interface_unit = find_interface(declared->interface_type);
    port = true;
  }
  if (interface_unit == nullptr) {
    return std::nullopt;
  }
  std::vector<std::string> path;
  const auto anchor = current();
  index_ += cursor;
  while (match(TokenKind::Dot)) {
    path.push_back(expect_identifier("interface member name").text);
  }
  if (path.empty()) {
    return std::nullopt;
  }

  struct Context {
    const DesignUnit* unit{};
    std::string prefix;
    NameMap names;
  };
  const auto declared_parameter = [&](const std::string_view name) {
    return std::ranges::any_of(unit.parameters,
        [&](const ParameterDeclaration& parameter) {
          return parameter.name == name;
        });
  };
  // Each interface parameter becomes `<prefix>__<name>`.
  const auto make_context = [&](const DesignUnit& target, std::string prefix,
                                const std::vector<ParameterOverride>* actuals,
                                const NameMap* outer, const bool bound) {
    Context context{&target, std::move(prefix), {}};
    for (const auto& parameter : target.parameters) {
      context.names.insert_or_assign(
          parameter.name, context.prefix + "__" + parameter.name);
    }
    std::size_t positional{};
    for (const auto& parameter : target.parameters) {
      const auto hidden = context.names.at(parameter.name);
      const ParameterOverride* actual = nullptr;
      if (actuals != nullptr && !parameter.local) {
        const auto named = std::ranges::find_if(
            *actuals, [&](const ParameterOverride& candidate) {
              return candidate.name && *candidate.name == parameter.name;
            });
        if (named != actuals->end()) {
          actual = &*named;
        } else if (positional < actuals->size()
            && !actuals->at(positional).name) {
          actual = &actuals->at(positional);
        }
        ++positional;
      }
      if (declared_parameter(hidden)) {
        continue;
      }
      auto declaration = parameter;
      declaration.name = hidden;
      declaration.span = anchor.span;
      if (actual != nullptr) {
        declaration.local = true;
        if (actual->type_value) {
          declaration.default_type = *actual->type_value;
        } else {
          declaration.default_value = actual->value;
          if (outer != nullptr) {
            rename_expression(declaration.default_value, *outer);
          }
        }
      } else {
        declaration.local = !bound || parameter.local;
        rename_expression(declaration.default_value, context.names);
      }
      unit.parameters.push_back(std::move(declaration));
    }
    return context;
  };

  auto context = make_context(*interface_unit, "__fsim_ifp_" + first,
      port ? nullptr : overrides, nullptr, port);
  for (std::size_t segment = 0; segment + 1U < path.size(); ++segment) {
    const auto nested = std::ranges::find(
        context.unit->instances, path[segment], &Instance::name);
    const auto* nested_unit = nested == context.unit->instances.end()
        ? nullptr
        : find_interface(nested->unit_name);
    if (nested_unit == nullptr) {
      error(anchor, "FSIM-SV-SEM-405",
          "'" + path[segment] + "' is not an interface instance in '"
              + context.unit->name + "'");
      return Type{};
    }
    context = make_context(*nested_unit,
        context.prefix + "_" + path[segment], &nested->parameter_overrides,
        &context.names, false);
  }

  // Imports an interface typedef, and those it names, as hidden typedefs.
  std::function<std::optional<std::string>(const std::string&)> import_alias;
  std::function<void(Type&)> rename_type = [&](Type& type) {
    if (type.packed_range_expression) {
      rename_range(*type.packed_range_expression, context.names);
    }
    for (auto& dimension : type.systemverilog_packed_dimensions) {
      rename_range(dimension, context.names);
    }
    for (auto& value : type.systemverilog_enumeration_values) {
      rename_expression(value, context.names);
    }
    for (auto& member : type.packed_members) {
      if (member.packed_range_expression) {
        rename_range(*member.packed_range_expression, context.names);
      }
      for (auto& nested : member.nested_types) {
        rename_type(nested);
      }
      if (member.initializer) {
        rename_expression(*member.initializer, context.names);
      }
    }
    if (type.systemverilog_container) {
      auto& container = *type.systemverilog_container;
      for (auto& range : container.static_range_expressions) {
        rename_range(range, context.names);
      }
      if (container.queue_maximum) {
        rename_expression(*container.queue_maximum, context.names);
      }
      for (auto& element : container.element_types) {
        rename_type(element);
      }
      if (container.associative_index_type) {
        auto index = std::make_shared<Type>(*container.associative_index_type);
        rename_type(*index);
        container.associative_index_type = std::move(index);
      }
    }
    if (!type.named_type.empty()) {
      if (const auto hidden = import_alias(type.named_type)) {
        if (type.spelling == type.named_type) {
          type.spelling = *hidden;
        }
        type.named_type = *hidden;
      }
    }
  };
  import_alias = [&](const std::string& name) -> std::optional<std::string> {
    const auto alias = std::ranges::find(
        context.unit->type_aliases, name, &TypeAliasDeclaration::name);
    if (alias == context.unit->type_aliases.end()) {
      return std::nullopt;
    }
    const auto hidden = context.prefix + "__" + name;
    if (std::ranges::any_of(unit.type_aliases,
            [&](const TypeAliasDeclaration& existing) {
              return existing.name == hidden;
            })) {
      return hidden;
    }
    auto copy = *alias;
    copy.name = hidden;
    copy.span = anchor.span;
    rename_type(copy.type);
    unit.type_aliases.push_back(std::move(copy));
    packed_typedef_types_.insert_or_assign(hidden, unit.type_aliases.back().type);
    return hidden;
  };
  const auto hidden = import_alias(path.back());
  if (!hidden) {
    error(anchor, "FSIM-SV-SEM-405",
        "'" + path.back() + "' is not a type declared in interface '"
            + context.unit->name + "'");
    return Type{};
  }
  const auto imported = std::ranges::find(
      unit.type_aliases, *hidden, &TypeAliasDeclaration::name);
  if (imported != unit.type_aliases.end()
      && !imported->type.packed_members.empty()) {
    // An aggregate is used by its description, as a local typedef is.
    return imported->type;
  }
  Type type{ValueDomain::Unknown, *hidden, std::nullopt, false};
  type.named_type = *hidden;
  type.named_type_span = anchor.span;
  return type;
}

}  // namespace fsim::frontend
