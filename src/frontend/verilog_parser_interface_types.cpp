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
#include <string>
#include <utility>
#include <vector>

namespace fsim::frontend {

namespace {

using NameMap = std::map<std::string, std::string, std::less<>>;
// The hidden name an interface typedef is imported as, if it is one.
using TypeNames
    = std::function<std::optional<std::string>(const std::string&)>;

void rename_expression(Expression& expression, const NameMap& names,
                       const TypeNames* types = nullptr) {
  if (expression.kind == ExpressionKind::Identifier) {
    const auto dot = expression.text.find('.');
    const auto head = std::string_view{expression.text}.substr(
        0, dot == std::string::npos ? expression.text.size() : dot);
    if (const auto found = names.find(head); found != names.end()) {
      expression.text = found->second
          + (dot == std::string::npos ? std::string{}
                                      : expression.text.substr(dot));
    } else if (types != nullptr && dot == std::string::npos) {
      // An interface typedef named in an expression (`$bits(row_t)`,
      // IEEE 1800-2017 20.6.2) is imported with the type that names it.
      if (auto hidden = (*types)(expression.text)) {
        expression.text = std::move(*hidden);
      }
    }
  }
  for (auto& operand : expression.operands) {
    rename_expression(operand, names, types);
  }
}

void rename_range(PackedRangeExpression& range, const NameMap& names,
                  const TypeNames* types = nullptr) {
  rename_expression(range.left, names, types);
  rename_expression(range.right, names, types);
}

}  // namespace

// The interface instance or interface port `first` of `unit`, followed
// through the nested interface instances `path[0, count)`: the interface
// it names, or null. `failed` is the first segment that is not a nested
// interface instance.
const DesignUnit* VerilogParser::interface_path_unit(
    const DesignUnit& unit, const std::string_view first,
    const std::vector<std::string>& path, const std::size_t count,
    std::size_t* failed) const {
  if (parsed_design_ == nullptr) return nullptr;
  const auto find_interface = [&](const std::string_view name)
      -> const DesignUnit* {
    const auto found = std::ranges::find_if(
        parsed_design_->units, [&](const DesignUnit& candidate) {
          return candidate.kind == UnitKind::SystemVerilogInterface
              && candidate.name == name;
        });
    return found == parsed_design_->units.end() ? nullptr : &*found;
  };
  const DesignUnit* current = nullptr;
  if (const auto instance = std::ranges::find(
          unit.instances, first, &Instance::name);
      instance != unit.instances.end()) {
    current = find_interface(instance->unit_name);
  } else if (const auto declared = std::ranges::find_if(
                 unit.ports, [&](const SignalDeclaration& candidate) {
                   return candidate.name == first
                       && !candidate.interface_type.empty();
                 });
             declared != unit.ports.end()) {
    current = find_interface(declared->interface_type);
  }
  for (std::size_t segment = 0; current != nullptr && segment < count;
       ++segment) {
    const auto nested = std::ranges::find(
        current->instances, path[segment], &Instance::name);
    const auto* nested_unit = nested == current->instances.end()
        ? nullptr
        : find_interface(nested->unit_name);
    if (nested_unit == nullptr && failed != nullptr) *failed = segment;
    current = nested_unit;
  }
  return current;
}

std::optional<std::string> VerilogParser::import_interface_member(
    DesignUnit& unit, const std::string& first,
    const std::vector<std::string>& path, const SourceSpan& anchor,
    const InterfaceMemberKind kind) {
  if (path.empty()) return std::nullopt;
  // Nothing is imported unless the member exists.
  const auto* target = interface_path_unit(
      unit, first, path, path.size() - 1U, nullptr);
  if (target == nullptr) return std::nullopt;
  const auto& member = path.back();
  const auto declared_object = [&](const DesignUnit& owner)
      -> const Type* {
    for (const auto* declarations : {&owner.variables}) {
      const auto found = std::ranges::find(
          *declarations, member, &VariableDeclaration::name);
      if (found != declarations->end()) return &found->type;
    }
    for (const auto* declarations : {&owner.signals, &owner.ports}) {
      const auto found = std::ranges::find(
          *declarations, member, &SignalDeclaration::name);
      if (found != declarations->end() && found->interface_type.empty()) {
        return &found->type;
      }
    }
    return nullptr;
  };
  switch (kind) {
  case InterfaceMemberKind::type:
    if (!std::ranges::any_of(target->type_aliases,
            [&](const TypeAliasDeclaration& alias) {
              return alias.name == member;
            })) {
      return std::nullopt;
    }
    break;
  case InterfaceMemberKind::parameter:
    if (!std::ranges::any_of(target->parameters,
            [&](const ParameterDeclaration& parameter) {
              return parameter.name == member;
            })) {
      return std::nullopt;
    }
    break;
  case InterfaceMemberKind::object:
    if (declared_object(*target) == nullptr) return std::nullopt;
    break;
  }

  const DesignUnit* interface_unit
      = interface_path_unit(unit, first, path, 0U, nullptr);
  const auto instance = std::ranges::find(unit.instances, first,
      &Instance::name);
  const bool port = instance == unit.instances.end();
  const auto* overrides = port ? nullptr : &instance->parameter_overrides;

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
  const auto make_context = [&](const DesignUnit& target_unit,
                                std::string prefix,
                                const std::vector<ParameterOverride>* actuals,
                                const NameMap* outer, const bool bound) {
    Context context{&target_unit, std::move(prefix), {}};
    for (const auto& parameter : target_unit.parameters) {
      context.names.insert_or_assign(
          parameter.name, context.prefix + "__" + parameter.name);
    }
    std::size_t positional{};
    for (const auto& parameter : target_unit.parameters) {
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
      declaration.span = anchor;
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
      overrides, nullptr, port);
  for (std::size_t segment = 0; segment + 1U < path.size(); ++segment) {
    const auto nested = std::ranges::find(
        context.unit->instances, path[segment], &Instance::name);
    const auto* nested_unit = interface_path_unit(
        unit, first, path, segment + 1U, nullptr);
    context = make_context(*nested_unit,
        context.prefix + "_" + path[segment], &nested->parameter_overrides,
        &context.names, false);
  }
  if (kind == InterfaceMemberKind::parameter) {
    return context.names.at(member);
  }

  // Imports an interface typedef, and those it names, as hidden typedefs.
  std::function<std::optional<std::string>(const std::string&)> import_alias;
  const TypeNames type_names = [&](const std::string& name) {
    return import_alias(name);
  };
  std::function<void(Type&)> rename_type = [&](Type& type) {
    if (type.packed_range_expression) {
      rename_range(*type.packed_range_expression, context.names, &type_names);
    }
    for (auto& dimension : type.systemverilog_packed_dimensions) {
      rename_range(dimension, context.names, &type_names);
    }
    for (auto& value : type.systemverilog_enumeration_values) {
      rename_expression(value, context.names, &type_names);
    }
    for (auto& packed_member : type.packed_members) {
      if (packed_member.packed_range_expression) {
        rename_range(*packed_member.packed_range_expression, context.names,
            &type_names);
      }
      for (auto& nested : packed_member.nested_types) {
        rename_type(nested);
      }
      if (packed_member.initializer) {
        rename_expression(*packed_member.initializer, context.names,
            &type_names);
      }
    }
    if (type.systemverilog_container) {
      auto& container = *type.systemverilog_container;
      for (auto& range : container.static_range_expressions) {
        rename_range(range, context.names, &type_names);
      }
      if (container.queue_maximum) {
        rename_expression(*container.queue_maximum, context.names, &type_names);
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
      } else if (const auto parameter = context.names.find(type.named_type);
                 parameter != context.names.end()) {
        // A type parameter of the interface (`the_type foo;`).
        if (type.spelling == type.named_type) {
          type.spelling = parameter->second;
        }
        type.named_type = parameter->second;
      }
    }
  };
  const auto add_alias = [&](const std::string& hidden, Type type) {
    TypeAliasDeclaration copy;
    copy.name = hidden;
    copy.type = std::move(type);
    copy.span = anchor;
    rename_type(copy.type);
    unit.type_aliases.push_back(std::move(copy));
    packed_typedef_types_.insert_or_assign(hidden, unit.type_aliases.back().type);
  };
  const auto imported = [&](const std::string& hidden) {
    return std::ranges::any_of(unit.type_aliases,
        [&](const TypeAliasDeclaration& existing) {
          return existing.name == hidden;
        });
  };
  import_alias = [&](const std::string& name) -> std::optional<std::string> {
    const auto alias = std::ranges::find(
        context.unit->type_aliases, name, &TypeAliasDeclaration::name);
    if (alias == context.unit->type_aliases.end()) {
      return std::nullopt;
    }
    const auto hidden = context.prefix + "__" + name;
    if (imported(hidden)) {
      return hidden;
    }
    auto copy = *alias;
    copy.name = hidden;
    copy.span = anchor;
    rename_type(copy.type);
    unit.type_aliases.push_back(std::move(copy));
    packed_typedef_types_.insert_or_assign(hidden, unit.type_aliases.back().type);
    return hidden;
  };
  if (kind == InterfaceMemberKind::type) {
    return import_alias(member);
  }
  // The declared type of an interface variable or net, as `$bits(i.v)`
  // reads it (IEEE 1800-2017 20.6.2).
  const auto hidden = context.prefix + "__object__" + member;
  if (!imported(hidden)) {
    add_alias(hidden, *declared_object(*context.unit));
  }
  return hidden;
}

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
  const auto first = current().text;
  // A generic interface port names no interface of its own: the connection
  // chooses it (IEEE 1800-2017 25.5), so its types are imported once the
  // instantiating units are parsed.
  const bool generic = std::ranges::any_of(
      unit.ports, [&](const SignalDeclaration& port) {
        return port.name == first && port.interface_type.empty()
            && port.type.spelling == "interface";
      });
  if (!generic && interface_path_unit(unit, first, {}, 0U, nullptr) == nullptr) {
    return std::nullopt;
  }
  std::vector<std::string> path;
  const auto anchor = current();
  index_ += cursor;
  while (match(TokenKind::Dot)) {
    path.push_back(expect_identifier("interface member name").text);
    // The elements of an interface instance array share one type, so the
    // index of a nested instance array (`x_if_a[0]`) selects nothing.
    // A bracket after the last segment is a packed dimension of the type.
    if (at(TokenKind::LeftBracket)) {
      std::size_t depth{};
      std::size_t closing{};
      for (std::size_t offset = 0; !at(TokenKind::EndOfFile, offset);
           ++offset) {
        if (at(TokenKind::LeftBracket, offset)) {
          ++depth;
        } else if (at(TokenKind::RightBracket, offset) && --depth == 0U) {
          closing = offset;
          break;
        }
      }
      if (closing != 0U && at(TokenKind::Dot, closing + 1U)) {
        for (std::size_t skipped = 0; skipped <= closing; ++skipped) {
          advance();
        }
      }
    }
  }
  if (path.empty()) {
    return std::nullopt;
  }
  if (generic) {
    // The typedef import_interface_member declares later is named
    // `<prefix>__<member>`, the prefix being the port and the nested
    // instances (see make_context there).
    auto hidden = "__fsim_ifp_" + first;
    for (std::size_t segment = 0; segment + 1U < path.size(); ++segment) {
      hidden += "_" + path[segment];
    }
    hidden += "__" + path.back();
    pending_generic_interface_types_.push_back(
        {unit.name, first, path, anchor, unit.type_aliases.size()});
    Type type{ValueDomain::Unknown, hidden, std::nullopt, false};
    type.named_type = hidden;
    type.named_type_span = anchor.span;
    return type;
  }
  std::size_t failed = path.size();
  const auto* target = interface_path_unit(
      unit, first, path, path.size() - 1U, &failed);
  if (target == nullptr) {
    const auto* owner = interface_path_unit(unit, first, path, failed, nullptr);
    error(anchor, "FSIM-SV-SEM-405",
        "'" + path[failed] + "' is not an interface instance in '"
            + (owner != nullptr ? owner->name : first) + "'");
    return Type{};
  }
  const auto hidden = import_interface_member(unit, first, path, anchor.span,
      InterfaceMemberKind::type);
  if (!hidden) {
    error(anchor, "FSIM-SV-SEM-405",
        "'" + path.back() + "' is not a type declared in interface '"
            + target->name + "'");
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

// Imports the types that units named through a generic interface port
// (`interface.mp p; typedef p.t t;`, IEEE 1800-2017 25.5, 25.10, 6.18). The
// port's interface is the one its instances connect, found among the
// instantiating units of the design; the typedef is then imported as
// import_interface_type imports it for a typed port. The port keeps no
// interface type: only the import sees one.
void VerilogParser::resolve_generic_interface_types(ParsedDesign& design) {
  if (pending_generic_interface_types_.empty()) return;
  auto pending = std::move(pending_generic_interface_types_);
  pending_generic_interface_types_.clear();
  const auto find_interface = [&](const std::string_view name)
      -> const DesignUnit* {
    const auto found = std::ranges::find_if(
        design.units, [&](const DesignUnit& candidate) {
          return candidate.kind == UnitKind::SystemVerilogInterface
              && candidate.name == name;
        });
    return found == design.units.end() ? nullptr : &*found;
  };
  // The name an actual connection starts from: `ifc`, `ifc.mp`, `ifc[1]`,
  // or `ifc[i].mp`.
  const auto actual_head = [](const Expression& value) -> std::string {
    const Expression* node = &value;
    while (!node->operands.empty()
        && (node->kind == ExpressionKind::Index
            || node->kind == ExpressionKind::Slice
            || (node->kind == ExpressionKind::Call
                && node->text.starts_with("@sv-select:")))) {
      node = &node->operands.front();
    }
    if (node->kind != ExpressionKind::Identifier) return {};
    return node->text.substr(0, node->text.find_first_of(".["));
  };
  // The interfaces the instances of `unit_name` connect to `port`.
  std::function<void(const std::string&, const std::string&, int,
                     std::vector<const DesignUnit*>&)> connected;
  connected = [&](const std::string& unit_name, const std::string& port,
                  const int depth, std::vector<const DesignUnit*>& found) {
    const auto owner = std::ranges::find(design.units, unit_name,
        &DesignUnit::name);
    if (owner == design.units.end()) return;
    const auto port_index = static_cast<std::size_t>(std::ranges::find(
        owner->ports, port, &SignalDeclaration::name) - owner->ports.begin());
    for (const auto& scope : design.units) {
      for (const auto& instance : scope.instances) {
        if (instance.unit_name != unit_name) continue;
        std::string head;
        std::size_t position{};
        for (const auto& connection : instance.connections) {
          const bool positional = !connection.port;
          const auto slot = position;
          if (positional) ++position;
          if (connection.kind != PortActualKind::Expression) continue;
          if (positional ? slot == port_index : *connection.port == port) {
            head = actual_head(connection.value);
          } else if (connection.port && *connection.port == "*"
              && head.empty()) {
            // `.*` connects the interface instance of the same name.
            head = port;
          }
        }
        if (head.empty()) continue;
        if (const auto nested = std::ranges::find(
                scope.instances, head, &Instance::name);
            nested != scope.instances.end()) {
          if (const auto* interface = find_interface(nested->unit_name)) {
            found.push_back(interface);
          }
        } else if (const auto declared = std::ranges::find(
                       scope.ports, head, &SignalDeclaration::name);
                   declared != scope.ports.end()
                   && declared->type.spelling == "interface") {
          if (!declared->interface_type.empty()) {
            if (const auto* interface =
                    find_interface(declared->interface_type)) {
              found.push_back(interface);
            }
          } else if (depth > 0) {
            // A generic port forwarded from the instantiating unit.
            connected(scope.name, head, depth - 1, found);
          }
        }
      }
    }
  };
  std::ranges::stable_sort(pending, {},
      &PendingGenericInterfaceType::position);
  // Aliases inserted so far, per unit, shift the later positions.
  std::map<std::string, std::size_t, std::less<>> inserted;
  for (const auto& request : pending) {
    const auto owner = std::ranges::find(design.units, request.unit,
        &DesignUnit::name);
    if (owner == design.units.end()) continue;
    std::vector<const DesignUnit*> interfaces;
    connected(request.unit, request.port, 8, interfaces);
    std::ranges::sort(interfaces, {}, &DesignUnit::name);
    interfaces.erase(std::ranges::unique(interfaces, {},
                         &DesignUnit::name).begin(),
        interfaces.end());
    if (interfaces.size() != 1U) {
      error(request.anchor, "FSIM-SV-UNSUPPORTED-024",
          interfaces.empty()
              ? "generic interface port '" + request.port + "' of '"
                  + request.unit + "' is not connected to an interface "
                  "instance of this design, so its type '"
                  + request.path.back() + "' is unknown"
              : "generic interface port '" + request.port + "' of '"
                  + request.unit + "' is connected to different "
                  "interfaces, so its type '" + request.path.back()
                  + "' is ambiguous");
      continue;
    }
    auto port = std::ranges::find(owner->ports, request.port,
        &SignalDeclaration::name);
    if (port == owner->ports.end()) continue;
    const auto saved = std::exchange(port->interface_type,
        interfaces.front()->name);
    const auto before = owner->type_aliases.size();
    const auto hidden = import_interface_member(*owner, request.port,
        request.path, request.anchor.span, InterfaceMemberKind::type);
    port->interface_type = saved;
    if (!hidden) {
      error(request.anchor, "FSIM-SV-SEM-405",
          "'" + request.path.back() + "' is not a type declared in interface '"
              + interfaces.front()->name + "'");
      continue;
    }
    // The imported typedefs precede the typedef that names them.
    auto& shift = inserted[request.unit];
    const auto count = owner->type_aliases.size() - before;
    const auto target = std::min(request.position + shift, before);
    std::rotate(owner->type_aliases.begin()
            + static_cast<std::ptrdiff_t>(target),
        owner->type_aliases.begin() + static_cast<std::ptrdiff_t>(before),
        owner->type_aliases.end());
    shift += count;
  }
}

// Names of interface parameters and objects in elaboration-time constants
// of a unit (`$bits(i.v)`, `i.N`, `q.cfg.w`): each becomes a hidden
// parameter or typedef of the unit, bound as import_interface_type binds
// them (IEEE 1800-2017 25.10, 23.6, 20.6.2).
void VerilogParser::import_interface_member_references(DesignUnit& unit) {
  if (language_ != Language::SystemVerilog2017 || parsed_design_ == nullptr) {
    return;
  }
  const auto split = [](const std::string& text) {
    std::vector<std::string> segments;
    std::size_t begin{};
    while (true) {
      const auto dot = text.find('.', begin);
      segments.push_back(text.substr(begin,
          dot == std::string::npos ? std::string::npos : dot - begin));
      if (dot == std::string::npos) break;
      begin = dot + 1U;
    }
    return segments;
  };
  const auto plain = [](const std::vector<std::string>& segments) {
    return segments.size() >= 2U
        && std::ranges::all_of(segments, [](const std::string& segment) {
             return !segment.empty() && segment.find_first_of("[]\\:#")
                 == std::string::npos;
           });
  };
  std::function<void(Expression&)> rewrite = [&](Expression& expression) {
    // `$bits({i.a, i.b})` is the sum of its operands' widths (IEEE
    // 1800-2017 11.4.12, 20.6.2).
    if (expression.kind == ExpressionKind::Call && expression.text == "$bits"
        && expression.operands.size() == 1U
        && expression.operands.front().kind == ExpressionKind::Concatenation
        && !expression.operands.front().operands.empty()
        && std::ranges::all_of(expression.operands.front().operands,
            [&](const Expression& operand) {
              if (operand.kind != ExpressionKind::Identifier) return false;
              const auto segments = split(operand.text);
              return plain(segments) && interface_path_unit(unit,
                  segments.front(), {}, 0U, nullptr) != nullptr;
            })) {
      const auto span = expression.span;
      auto operands = std::move(expression.operands.front().operands);
      std::optional<Expression> sum;
      for (auto& operand : operands) {
        Expression query{ExpressionKind::Call, "$bits", {}, operand.span};
        query.operands.push_back(std::move(operand));
        sum = sum ? Expression{ExpressionKind::Binary, "+",
                        {std::move(*sum), std::move(query)}, span}
                  : std::move(query);
      }
      expression = std::move(*sum);
      rewrite(expression);
      return;
    }
    const bool query = expression.kind == ExpressionKind::Call
        && expression.text == "$bits" && expression.operands.size() == 1U
        && expression.operands.front().kind == ExpressionKind::Identifier
        && !expression.systemverilog_type_operand;
    if (query) {
      auto& operand = expression.operands.front();
      auto segments = split(operand.text);
      if (plain(segments)
          && interface_path_unit(unit, segments.front(), {}, 0U, nullptr)
              != nullptr) {
        const std::vector<std::string> path(
            segments.begin() + 1, segments.end());
        if (const auto hidden = import_interface_member(unit,
                segments.front(), path, operand.span,
                InterfaceMemberKind::object)) {
          operand.text = *hidden;
          return;
        }
      }
    }
    if (expression.kind == ExpressionKind::Identifier) {
      auto segments = split(expression.text);
      if (plain(segments)
          && interface_path_unit(unit, segments.front(), {}, 0U, nullptr)
              != nullptr) {
        for (std::size_t count = 1U; count < segments.size(); ++count) {
          const std::vector<std::string> path(segments.begin() + 1,
              segments.begin() + static_cast<std::ptrdiff_t>(count + 1U));
          if (const auto hidden = import_interface_member(unit,
                  segments.front(), path, expression.span,
                  InterfaceMemberKind::parameter)) {
            auto text = *hidden;
            for (std::size_t rest = count + 1U; rest < segments.size();
                 ++rest) {
              text += "." + segments[rest];
            }
            expression.text = std::move(text);
            return;
          }
        }
      }
    }
    for (auto& operand : expression.operands) rewrite(operand);
  };
  const auto rewrite_type = [&](Type& type) {
    if (type.packed_range_expression) {
      rewrite(type.packed_range_expression->left);
      rewrite(type.packed_range_expression->right);
    }
    for (auto& dimension : type.systemverilog_packed_dimensions) {
      rewrite(dimension.left);
      rewrite(dimension.right);
    }
  };
  // Parameter values, instance overrides, declared ranges and generate
  // conditions are evaluated at elaboration.
  const auto rewrite_overrides = [&](std::vector<Instance>& instances) {
    for (auto& instance : instances) {
      for (auto& override_value : instance.parameter_overrides) {
        if (!override_value.type_value) rewrite(override_value.value);
      }
    }
  };
  const auto rewrite_declarations = [&](auto& declarations) {
    for (auto& declaration : declarations) rewrite_type(declaration.type);
  };
  // Importing appends hidden parameters, so each value is rewritten as a
  // copy.
  const auto parameter_count = unit.parameters.size();
  for (std::size_t index = 0; index < parameter_count; ++index) {
    if (unit.parameters[index].name.starts_with("__fsim_ifp_")) continue;
    if (unit.parameters[index].kind == ParameterKind::Value) {
      auto value = unit.parameters[index].default_value;
      rewrite(value);
      unit.parameters[index].default_value = std::move(value);
      continue;
    }
    // `localparam type t = type(i.v)` (IEEE 1800-2017 6.23).
    const auto& default_type = unit.parameters[index].default_type;
    const auto operand = default_type
        ? default_type->systemverilog_type_of
        : std::shared_ptr<const Expression>{};
    if (operand == nullptr || operand->kind != ExpressionKind::Identifier) {
      continue;
    }
    const auto segments = split(operand->text);
    if (!plain(segments)
        || interface_path_unit(unit, segments.front(), {}, 0U, nullptr)
            == nullptr) {
      continue;
    }
    const std::vector<std::string> path(segments.begin() + 1, segments.end());
    const auto span = operand->span;
    if (const auto hidden = import_interface_member(unit, segments.front(),
            path, span, InterfaceMemberKind::object)) {
      Type type{ValueDomain::Unknown, *hidden, std::nullopt, false};
      type.named_type = *hidden;
      type.named_type_span = span;
      unit.parameters[index].default_type = std::move(type);
    }
  }
  rewrite_overrides(unit.instances);
  rewrite_declarations(unit.signals);
  rewrite_declarations(unit.variables);
  std::function<void(GenerateBody&)> rewrite_body;
  const auto rewrite_region = [&](GenerateRegion& region) {
    rewrite(region.initial);
    rewrite(region.condition);
    rewrite(region.iteration);
    rewrite_body(region.then_body);
    rewrite_body(region.else_body);
    for (auto& alternative : region.alternatives) {
      for (auto& choice : alternative.choices) {
        rewrite(choice.left);
        if (choice.right) rewrite(*choice.right);
      }
      rewrite_body(alternative.body);
    }
  };
  rewrite_body = [&](GenerateBody& body) {
    for (auto& constant : body.constants) {
      if (constant.kind == ParameterKind::Value) rewrite(constant.default_value);
    }
    rewrite_overrides(body.instances);
    rewrite_declarations(body.signals);
    rewrite_declarations(body.variables);
    for (auto& region : body.generate_regions) rewrite_region(region);
  };
  for (auto& region : unit.generate_regions) rewrite_region(region);
}

}  // namespace fsim::frontend
