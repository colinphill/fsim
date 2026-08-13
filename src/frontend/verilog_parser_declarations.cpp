// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

void VerilogParser::parse_parameter_overrides(
  Instance& instance,
  const Token& hash) {
  expect(
      TokenKind::LeftParen,
      "'(' after instance parameter '#'",
      "FSIM-SV-PARSE-055");
  bool saw_named = false;
  bool saw_positional = false;
  const auto begins_unambiguous_type_actual = [&]() {
    return keyword("byte") || keyword("shortint")
        || keyword("longint") || keyword("time")
        || keyword("shortreal") || keyword("real")
        || keyword("realtime") || keyword("chandle")
        || keyword("process")
        || keyword("integer") || keyword("int")
        || keyword("logic") || keyword("reg") || keyword("bit")
        || keyword("signed") || keyword("unsigned")
        || keyword("string") || at(TokenKind::LeftBracket);
  };
  const auto parse_actual = [&](ParameterOverride& override) {
    if (begins_unambiguous_type_actual()) {
      override.type_value = parse_type_parameter_actual();
    } else {
      override.value = parse_expression();
    }
  };
  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto start = current();
    ParameterOverride override;
    if (match(TokenKind::Dot)) {
      saw_named = true;
      const auto name = expect_identifier("overridden parameter name");
      override.name = name.text;
      expect(
          TokenKind::LeftParen,
          "'(' after named parameter override",
          "FSIM-SV-PARSE-056");
      parse_actual(override);
      expect(
          TokenKind::RightParen,
          "')' after named parameter override",
          "FSIM-SV-PARSE-057");
      if (std::any_of(
              instance.parameter_overrides.begin(),
              instance.parameter_overrides.end(),
              [&](const ParameterOverride& existing) {
                return existing.name == override.name;
              })) {
        error(
            name,
            "FSIM-SV-SEM-018",
            "duplicate named parameter override '" + name.text + "'");
      }
    } else {
      saw_positional = true;
      parse_actual(override);
    }
    override.span = cover(start.span, previous().span);
    instance.parameter_overrides.push_back(std::move(override));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after instance parameter overrides",
      "FSIM-SV-PARSE-058");
  if (saw_named && saw_positional) {
    error(
        hash,
        "FSIM-SV-SEM-019",
        "named and positional parameter overrides cannot be mixed");
  }
}

void VerilogParser::parse_module_ports(DesignUnit& unit) {
  VerilogTypeSpec inherited;
  bool have_inherited_type = false;
  while (!at_end() && !at(TokenKind::RightParen)) {
      if (verilog_attribute_instance_start()) {
          parse_verilog_attribute_instances();
          continue;
      }
    if (at(TokenKind::Dot)) {
      const auto dot = advance();
      error(dot, "FSIM-SV-UNSUPPORTED-005",
            "named port connections are not valid in a module declaration");
      skip_to_port_delimiter();
    } else {
      const bool explicit_generic_interface = keyword("interface");
      const bool typed_interface =
          at(TokenKind::Identifier)
          && !keyword_reserved(keyword_set_, current().text)
          && ((at(TokenKind::Dot, 1)
               && at(TokenKind::Identifier, 2)
               && at(TokenKind::Identifier, 3))
              || at(TokenKind::Identifier, 1));
      if (explicit_generic_interface || typed_interface) {
        const auto interface_start = current();
        std::string interface_type;
        std::string modport;
        if (explicit_generic_interface) {
          advance();
        } else {
          interface_type = advance().text;
          if (match(TokenKind::Dot)) {
            modport = expect_identifier("interface modport name").text;
          }
        }
        const auto port_name = expect_identifier("interface port name");
        SignalDeclaration declaration{
            port_name.text,
            Type{ValueDomain::Unknown, "interface", std::nullopt, false},
            PortDirection::Unknown,
            true,
            cover(interface_start.span, port_name.span),
            std::nullopt,
            std::move(interface_type),
            std::move(modport)};
        const auto duplicate = std::ranges::find_if(
            unit.ports,
            [&](const SignalDeclaration& port) {
              return port.name == port_name.text;
            });
        if (duplicate != unit.ports.end()) {
          error(
              port_name,
              "FSIM-SV-SEM-003",
              "duplicate module port declaration '"
                  + port_name.text + "'");
        } else {
          unit.ports.push_back(std::move(declaration));
        }
        if (!match(TokenKind::Comma)) {
          break;
        }
        continue;
      }
      VerilogTypeSpec spec = inherited;
      bool declared_here = false;
      if (is_direction_keyword()) {
        spec.direction = parse_direction();
        spec.type = default_port_net_type();
        const bool explicit_variable = match_keyword("var");
        declared_here = true;
        const bool explicit_type =
            explicit_variable || is_net_type_keyword()
            || keyword("string") || keyword("chandle")
            || keyword("process")
            || is_named_type_reference_start();
        if (keyword("string") || keyword("chandle")
            || keyword("process")) {
          spec.type = parse_parameter_type();
        } else if (is_named_type_reference_start()) {
          spec.type = parse_named_type();
        } else {
          parse_optional_net_type(spec.type);
        }
        require_default_port_net_type(current(), explicit_type);
        if (spec.type.named_type.empty()) {
          parse_optional_signedness(spec.type);
          parse_optional_range(spec.type);
        }
        inherited = spec;
        have_inherited_type = true;
      } else if (!have_inherited_type) {
        spec.type = Type{ValueDomain::Unknown, {}, std::nullopt, false};
        spec.direction = PortDirection::Unknown;
      }

      const auto port_name = expect_identifier("port name");
      SignalDeclaration declaration{
          port_name.text, spec.type, spec.direction, true, port_name.span};
      if (spec.direction == PortDirection::Unknown) {
        non_ansi_ports_.insert(port_name.text);
      }
      if (match(TokenKind::Assign)) {
        const auto initializer = previous();
        (void)parse_expression();
        error(
            initializer,
            "FSIM-SV-UNSUPPORTED-010",
            "ANSI port default expressions are not executable in this "
            "frontend slice");
      }
      if (at(TokenKind::LeftBracket)) {
        (void)parse_optional_container_dimension(declaration.type);
      }
      const auto duplicate = std::find_if(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& port) {
            return port.name == port_name.text;
          });
      if (duplicate != unit.ports.end()) {
        error(
            port_name,
            "FSIM-SV-SEM-003",
            "duplicate module port declaration '" + port_name.text + "'");
      } else if (
          std::any_of(
              unit.parameters.begin(),
              unit.parameters.end(),
              [&](const ParameterDeclaration& parameter) {
                return parameter.name == port_name.text;
              })) {
        error(
            port_name,
            "FSIM-SV-SEM-020",
            "port '" + port_name.text
                + "' conflicts with a parameter declaration");
      } else {
        unit.ports.push_back(std::move(declaration));
      }
      (void)declared_here;
    }

    if (!match(TokenKind::Comma)) {
      break;
    }
  }
}

void VerilogParser::parse_modport(
    DesignUnit& unit,
    const Token& start) {
  for (;;) {
    const auto name = expect_identifier("modport name");
    SystemVerilogModport declaration;
    declaration.name = name.text;
    expect(
        TokenKind::LeftParen,
        "'(' after modport name",
        "FSIM-SV-PARSE-212");
    PortDirection direction = PortDirection::Unknown;
    enum class CallableAccess { None, Import, Export };
    CallableAccess callable_access{CallableAccess::None};
    while (!at_end() && !at(TokenKind::RightParen)) {
      if (is_direction_keyword()) {
        direction = parse_direction();
        callable_access = CallableAccess::None;
      } else if (match_keyword("ref")) {
        direction = PortDirection::Ref;
        callable_access = CallableAccess::None;
      } else if (match_keyword("import")) {
        callable_access = CallableAccess::Import;
        direction = PortDirection::Unknown;
      } else if (match_keyword("export")) {
        callable_access = CallableAccess::Export;
        direction = PortDirection::Unknown;
      }
      if (match_keyword("clocking")) {
        const auto member =
            expect_identifier("modport clocking-block name");
        const bool duplicate = std::ranges::any_of(
            declaration.members,
            [&](const SystemVerilogModportMember& existing) {
              return existing.name == member.text;
            });
        if (duplicate) {
          error(
              member,
              "FSIM-SV-SEM-117",
              "duplicate modport member '" + member.text + "'");
        } else {
          const auto block = std::ranges::find(
              unit.systemverilog_clocking_blocks,
              member.text,
              &SystemVerilogClockingBlock::name);
          if (block == unit.systemverilog_clocking_blocks.end()) {
            error(
                member,
                "FSIM-SV-SEM-190",
                "modport clocking member '" + member.text
                    + "' is not declared by the interface");
          }
          declaration.members.push_back({
              member.text, PortDirection::Unknown, member.span,
              SystemVerilogModportMemberKind::Clocking});
        }
        direction = PortDirection::Unknown;
        callable_access = CallableAccess::None;
      } else if (!at(TokenKind::RightParen)) {
        std::optional<bool> explicit_function;
        if (callable_access != CallableAccess::None
            && (keyword("function") || keyword("task"))) {
          explicit_function = keyword("function");
          advance();
        }
        const auto member = expect_identifier("modport member name");
        const auto function = std::ranges::find_if(
            unit.functions,
            [&](const FunctionDeclaration& candidate) {
              return candidate.name == member.text;
            });
        const auto task = std::ranges::find_if(
            unit.tasks,
            [&](const TaskDeclaration& candidate) {
              return candidate.name == member.text;
            });
        const bool callable = callable_access != CallableAccess::None;
        if (!callable && direction == PortDirection::Unknown) {
          error(
              member,
              "FSIM-SV-SEM-116",
              "a modport member requires an explicit direction");
        }
        const bool duplicate = std::ranges::any_of(
            declaration.members,
            [&](const SystemVerilogModportMember& existing) {
              return existing.name == member.text;
            });
        if (duplicate) {
          error(
              member,
              "FSIM-SV-SEM-117",
              "duplicate modport member '" + member.text + "'");
        } else {
          const bool signal_declared = std::ranges::any_of(
              unit.signals,
              [&](const SignalDeclaration& signal) {
                return signal.name == member.text;
              }) || std::ranges::any_of(
                  unit.ports,
                  [&](const SignalDeclaration& port) {
                    return port.name == member.text;
                  });
          if (!callable && !signal_declared) {
            error(
                member,
                "FSIM-SV-SEM-118",
                "modport member '" + member.text
                    + "' is not declared by the interface");
          }
          if (callable && function == unit.functions.end()
              && task == unit.tasks.end()) {
            error(
                member,
                "FSIM-SV-SEM-122",
                "modport callable '" + member.text
                    + "' is not declared by the interface");
          }
          if (callable && explicit_function
              && ((*explicit_function
                       && function == unit.functions.end())
                  || (!*explicit_function
                      && task == unit.tasks.end()))) {
            error(
                member,
                "FSIM-SV-SEM-123",
                "modport callable kind does not match '"
                    + member.text + "'");
          }
          auto kind = SystemVerilogModportMemberKind::Signal;
          if (callable && function != unit.functions.end()) {
            kind = callable_access == CallableAccess::Import
                ? SystemVerilogModportMemberKind::FunctionImport
                : SystemVerilogModportMemberKind::FunctionExport;
          } else if (callable && task != unit.tasks.end()) {
            kind = callable_access == CallableAccess::Import
                ? SystemVerilogModportMemberKind::TaskImport
                : SystemVerilogModportMemberKind::TaskExport;
          }
          declaration.members.push_back({
              member.text, direction, member.span, kind});
        }
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after modport members",
        "FSIM-SV-PARSE-213");
    declaration.span = cover(name.span, previous().span);
    if (std::ranges::any_of(
            unit.systemverilog_modports,
            [&](const SystemVerilogModport& existing) {
              return existing.name == declaration.name;
            })) {
      error(
          name,
          "FSIM-SV-SEM-119",
          "duplicate modport declaration '" + name.text + "'");
    } else {
      unit.systemverilog_modports.push_back(std::move(declaration));
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after modport declaration",
      "FSIM-SV-PARSE-214");
  (void)start;
}

void VerilogParser::skip_to_port_delimiter() {
  std::size_t depth = 0;
  while (!at_end()) {
    if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket) ||
        at(TokenKind::LeftBrace)) {
      ++depth;
    } else if (at(TokenKind::RightParen) ||
               at(TokenKind::RightBracket) ||
               at(TokenKind::RightBrace)) {
      if (depth == 0) {
        return;
      }
      --depth;
    } else if (at(TokenKind::Comma) && depth == 0) {
      return;
    }
    advance();
  }
}

[[nodiscard]] bool VerilogParser::is_direction_keyword() const  {
  return keyword("input") || keyword("output") || keyword("inout");
}

PortDirection VerilogParser::parse_direction() {
  if (match_keyword("input")) {
    return PortDirection::Input;
  }
  if (match_keyword("output")) {
    return PortDirection::Output;
  }
  if (match_keyword("inout")) {
    return PortDirection::Inout;
  }
  return PortDirection::Unknown;
}

Type VerilogParser::default_verilog_type() {
  return Type{ValueDomain::Logic4, "wire", std::nullopt, false};
}

Type VerilogParser::default_port_net_type() const  {
  if (current_default_nettype_ == "none") {
    return default_verilog_type();
  }
  return Type{
      ValueDomain::Logic4,
      current_default_nettype_,
      std::nullopt,
      false};
}

void VerilogParser::require_default_port_net_type(
  const Token& location,
  const bool explicit_type) {
  if (current_default_nettype_ == "none" && !explicit_type) {
    error(
        location,
        "FSIM-SV-SEM-016",
        "a port without an explicit net or variable type is forbidden by "
        "`default_nettype none");
  }
}

[[nodiscard]] bool VerilogParser::is_net_type_keyword() const  {
  return any_keyword({
      "wire", "tri", "tri0", "tri1", "wand", "triand", "wor",
      "trior", "trireg", "uwire", "supply0", "supply1",
      "reg", "logic", "bit", "byte",
      "shortint", "int", "longint", "integer", "time",
      "shortreal", "real", "realtime"});
}

[[nodiscard]] bool VerilogParser::is_named_type_reference_start(
  const std::size_t offset) const  {
  if (!at(TokenKind::Identifier, offset)
      || keyword_reserved(keyword_set_, current(offset).text)) {
    return false;
  }
  if (contains_word(
          {"always_ff", "always_comb", "always_latch"},
          current(offset).text)) {
    return false;
  }
  if (at(TokenKind::Hash, offset + 1)) {
    std::size_t cursor = offset + 2U;
    if (!at(TokenKind::LeftParen, cursor)) {
      return true;
    }
    std::size_t depth{};
    do {
      if (at(TokenKind::LeftParen, cursor)) {
        ++depth;
      } else if (at(TokenKind::RightParen, cursor)) {
        if (--depth == 0) {
          ++cursor;
          break;
        }
      }
      ++cursor;
    } while (!at(TokenKind::EndOfFile, cursor));
    bool selected = false;
    while (at(TokenKind::Scope, cursor)
           && at(TokenKind::Identifier, cursor + 1U)) {
      selected = true;
      cursor += 2U;
    }
    return !selected || at(TokenKind::Identifier, cursor);
  }
  if (at(TokenKind::Scope, offset + 1)) {
    return at(TokenKind::Identifier, offset + 2)
        && at(TokenKind::Identifier, offset + 3);
  }
  return at(TokenKind::Identifier, offset + 1)
      && !at(TokenKind::LeftParen, offset + 2)
      && !at(TokenKind::Hash, offset + 2);
}

Type VerilogParser::parse_named_type() {
  const auto first =
      expect_identifier("SystemVerilog type name");
  std::string name = first.text;
  auto span = first.span;
  while (match(TokenKind::Scope)) {
    const auto selected =
        expect_identifier("package type name");
    name += "::";
    name += selected.text;
    span = cover(span, selected.span);
  }
  Type type{
      ValueDomain::Unknown,
      name,
      std::nullopt,
      false};
  type.named_type = name;
  type.named_type_span = span;
  if (name == "mailbox" || name == "semaphore") {
    type.domain = ValueDomain::Bit2;
    type.systemverilog_scalar = SystemVerilogScalarKind::Chandle;
  }
  if (match(TokenKind::Hash)) {
    const auto hash = previous();
    Instance actual_owner;
    parse_parameter_overrides(actual_owner, hash);
    for (auto& actual : actual_owner.parameter_overrides) {
      SystemVerilogClassTypeActual retained;
      retained.name = std::move(actual.name);
      retained.value = std::move(actual.value);
      if (actual.type_value) {
        retained.type_actual = std::make_shared<Type>(
            std::move(*actual.type_value));
      }
      retained.span = std::move(actual.span);
      type.systemverilog_class_parameter_actuals.push_back(
          std::move(retained));
    }
    type.named_type_span = cover(type.named_type_span, previous().span);
  }
  return type;
}

Type VerilogParser::parse_virtual_interface_type(
    const Token& start) {
  (void)match_keyword("interface");
  auto type = parse_named_type();
  type.domain = ValueDomain::Bit2;
  type.systemverilog_scalar =
      SystemVerilogScalarKind::Chandle;
  type.systemverilog_virtual_interface = true;
  type.systemverilog_interface_type = type.named_type;
  if (match(TokenKind::Dot)) {
    const auto modport =
        expect_identifier("virtual-interface modport name");
    type.systemverilog_interface_modport = modport.text;
    type.named_type_span =
        cover(type.named_type_span, modport.span);
  }
  type.spelling = "virtual interface "
      + type.systemverilog_interface_type;
  if (!type.systemverilog_interface_modport.empty()) {
    type.spelling += "."
        + type.systemverilog_interface_modport;
  }
  if (type.systemverilog_interface_type.empty()) {
    error(
        start,
        "FSIM-SV-SEM-188",
        "a virtual interface requires an interface type name");
  }
  return type;
}

void VerilogParser::parse_virtual_interface_declaration(
    DesignUnit& unit,
    const Token& start) {
  const auto common_type =
      parse_virtual_interface_type(start);
  for (;;) {
    const auto name =
        expect_identifier("virtual-interface variable name");
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      initializer = parse_expression();
    }
    const bool duplicate = std::ranges::any_of(
        unit.variables,
        [&](const VariableDeclaration& variable) {
          return variable.name == name.text;
        })
        || std::ranges::any_of(
            unit.signals,
            [&](const SignalDeclaration& signal) {
              return signal.name == name.text;
            })
        || std::ranges::any_of(
            unit.ports,
            [&](const SignalDeclaration& port) {
              return port.name == name.text;
            });
    if (duplicate) {
      error(
          name,
          "FSIM-SV-SEM-189",
          "duplicate virtual-interface variable '"
              + name.text + "'");
    } else {
      unit.variables.push_back(VariableDeclaration{
          name.text,
          common_type,
          std::move(initializer),
          span_from(start, previous())});
    }
    if (!match(TokenKind::Comma)) break;
  }
  expect(
      TokenKind::Semicolon,
      "';' after a virtual-interface declaration",
      "FSIM-SV-PARSE-289");
}

[[nodiscard]] bool VerilogParser::is_declaration_start() const  {
  return is_direction_keyword() || is_net_type_keyword()
      || keyword("static") || keyword("automatic")
      || keyword("string") || keyword("chandle") || keyword("event")
      || keyword("process")
      || keyword("struct") || keyword("union") || keyword("enum")
      || is_named_type_reference_start();
}

void VerilogParser::parse_event_declaration(
  DesignUnit& unit, const Token& start) {
  Type type = default_verilog_type();
  type.spelling = "event";
  type.domain = ValueDomain::Logic4;
  for (;;) {
    const auto name = expect_identifier("named event");
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
        if (language_ != Language::SystemVerilog2017) {
            error(
                previous(),
                "FSIM-VERILOG-SEM-014",
                "named-event declaration assignment requires SystemVerilog");
        }
        initializer = parse_expression();
    }
    const auto duplicate_signal = std::find_if(
        unit.signals.begin(),
        unit.signals.end(),
        [&](const SignalDeclaration& signal) {
          return signal.name == name.text;
        });
    const auto duplicate_port = std::find_if(
        unit.ports.begin(),
        unit.ports.end(),
        [&](const SignalDeclaration& port) {
          return port.name == name.text;
        });
    if (duplicate_signal != unit.signals.end()
        || duplicate_port != unit.ports.end()) {
      error(
          name,
          "FSIM-SV-SEM-035",
          "duplicate named event or object declaration '"
              + name.text + "'");
    } else {
      unit.signals.push_back(SignalDeclaration{
          name.text,
          type,
          PortDirection::Unknown,
          false,
          name.span});
      if (initializer) {
          Statement alias;
          alias.kind = StatementKind::Assignment;
          alias.assignment_kind = AssignmentKind::Blocking;
          alias.target = Expression {
              ExpressionKind::Identifier, name.text, { }, name.span
          };
          alias.value = std::move(*initializer);
          alias.span = cover(name.span, alias.value.span);
          Process initializer_process;
          initializer_process.kind = ProcessKind::Initial;
          initializer_process.name = "$event_initializer_" + name.text;
          initializer_process.statements.push_back(std::move(alias));
          initializer_process.span = initializer_process.statements.front().span;
          unit.processes.push_back(std::move(initializer_process));
      }
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after named event declaration",
      "FSIM-SV-PARSE-117");
  if (!unit.signals.empty()) {
    unit.signals.back().span = span_from(start, previous());
  }
}

void VerilogParser::parse_declaration(DesignUnit& unit) {
  const auto start = current();
  const bool package_variable =
      unit.kind == UnitKind::SystemVerilogPackage;
  const bool systemverilog_const =
      package_variable && match_keyword("const");
  VerilogTypeSpec spec;
  spec.type = default_verilog_type();
  if (is_direction_keyword()) {
    spec.direction = parse_direction();
    spec.type = default_port_net_type();
    const bool explicit_variable = match_keyword("var");
    const bool explicit_type =
        explicit_variable || is_net_type_keyword()
        || keyword("string") || keyword("chandle")
        || keyword("process")
        || keyword("struct") || keyword("union") || keyword("enum")
        || is_named_type_reference_start();
    if (keyword("string") || keyword("chandle")
        || keyword("process")
        || keyword("struct") || keyword("union") || keyword("enum")) {
      spec.type = parse_parameter_type();
    } else if (is_named_type_reference_start()) {
      spec.type = parse_named_type();
    } else {
      parse_optional_net_type(spec.type);
    }
    require_default_port_net_type(current(), explicit_type);
  } else if (keyword("string") || keyword("chandle")
             || keyword("process")
             || keyword("struct") || keyword("union") || keyword("enum")) {
    spec.type = parse_parameter_type();
  } else if (is_named_type_reference_start()) {
    spec.type = parse_named_type();
  } else {
    parse_optional_net_type(spec.type);
  }
  auto drive_strength = parse_verilog_drive_strength("net declaration");
  auto charge_strength = parse_verilog_charge_strength("net declaration");
  if (drive_strength
      && contains_word(
      {"reg", "logic", "bit", "byte", "shortint", "int",
           "longint", "integer", "time", "shortreal", "real",
           "realtime"},
          spec.type.spelling)) {
    error(
        start,
        "FSIM-SV-SEM-150",
        "a variable declaration cannot carry a net drive strength");
    drive_strength.reset();
  }
  if (charge_strength && spec.type.spelling != "trireg") {
    error(
        start,
        "FSIM-SV-SEM-151",
        "a charge strength is legal only on a trireg declaration");
    charge_strength.reset();
  }
  if (spec.type.named_type.empty()) {
    parse_optional_signedness(spec.type);
    parse_optional_range(spec.type);
  }
  if (spec.type.named_type.empty()
      && !spec.type.systemverilog_enumeration_values.empty()) {
    for (std::size_t index = 0;
         index < spec.type.enumeration_literals.size(); ++index) {
      const auto& literal_name = spec.type.enumeration_literals[index];
      const auto& literal_value =
          spec.type.systemverilog_enumeration_values[index];
      add_parameter(
          unit,
          ParameterDeclaration{
              literal_name,
              spec.type,
              literal_value,
              true,
              literal_value.span,
              ParameterKind::Value,
              std::nullopt},
          Token{
              TokenKind::Identifier,
              literal_name,
              literal_value.span,
              {}});
    }
  }
  std::optional<Delay> net_delay;
  std::optional<Delay> charge_decay;
  if (match(TokenKind::Hash)) {
    auto parsed_delay = parse_verilog_delay(previous(), 3);
    if (spec.type.spelling == "trireg") {
      charge_decay = std::move(parsed_delay);
    } else {
      net_delay = std::move(parsed_delay);
    }
    if (spec.type.spelling != "wire"
        && spec.type.systemverilog_net_type != "wire"
        && spec.type.spelling != "trireg") {
      error(
          start,
          "FSIM-SV-SEM-110",
          "a net-declaration delay requires a wire or trireg net type");
    }
  }

  for (;;) {
    const auto name = expect_identifier("declared name");
    auto declaration_type = spec.type;
    (void)parse_optional_container_dimension(declaration_type);
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      initializer = parse_expression();
    }
    const bool named_construction =
        !declaration_type.named_type.empty()
        && initializer
        && initializer->kind == ExpressionKind::Call
        && initializer->text == "@sv-new";
    if (initializer
        && !package_variable
        && declaration_type.domain != ValueDomain::String
        && !declaration_type.systemverilog_container
        && !named_construction
        && spec.type.spelling != "wire"
        && spec.type.systemverilog_net_type != "wire") {
      error(
          name,
          "FSIM-SV-UNSUPPORTED-011",
          "declaration initializers are not executable in this frontend "
          "slice");
      initializer.reset();
    }

    if (package_variable
        || declaration_type.domain == ValueDomain::String
        || declaration_type.systemverilog_container
        || named_construction) {
      if (spec.direction != PortDirection::Unknown) {
        SignalDeclaration declaration{
            name.text,
            std::move(declaration_type),
            spec.direction,
            true,
            span_from(start, previous())};
        if (!non_ansi_ports_.contains(declaration.name)
            || !body_port_declarations_.insert(
                    declaration.name).second) {
          error(
              name,
              "FSIM-SV-SEM-004",
              "duplicate port declaration '" + declaration.name + "'");
        }
        update_or_add_port(unit, std::move(declaration));
        if (!match(TokenKind::Comma)) {
          break;
        }
        continue;
      }
      const auto duplicate_variable = std::ranges::any_of(
          unit.variables,
          [&](const VariableDeclaration& variable) {
            return variable.name == name.text;
          });
      const auto object_conflict = std::ranges::any_of(
          unit.signals,
          [&](const SignalDeclaration& signal) {
            return signal.name == name.text;
          });
      const auto port_conflict = std::ranges::any_of(
          unit.ports,
          [&](const SignalDeclaration& port) {
            return port.name == name.text;
          });
      const auto parameter_conflict = std::ranges::any_of(
          unit.parameters,
          [&](const ParameterDeclaration& parameter) {
            return parameter.name == name.text;
          });
      if (duplicate_variable || object_conflict || port_conflict
          || parameter_conflict) {
        error(
            name,
            "FSIM-SV-SEM-006",
            "duplicate variable declaration '" + name.text + "'");
      } else {
        VariableDeclaration variable{
            name.text,
            std::move(declaration_type),
            std::move(initializer),
            span_from(start, previous())};
        variable.systemverilog_const = systemverilog_const;
        unit.variables.push_back(std::move(variable));
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
      continue;
    }

    SignalDeclaration declaration{
        name.text, std::move(declaration_type), spec.direction,
        spec.direction != PortDirection::Unknown,
        span_from(start, previous()), net_delay};
    declaration.drive_strength = drive_strength;
    declaration.charge_strength = charge_strength;
    declaration.charge_decay = charge_decay;
    const auto parameter_conflict = std::any_of(
        unit.parameters.begin(),
        unit.parameters.end(),
        [&](const ParameterDeclaration& parameter) {
          return parameter.name == declaration.name;
        });
    if (declared_genvars_.contains(declaration.name)) {
      error(
          name,
          "FSIM-SV-SEM-022",
          "object '" + declaration.name
              + "' conflicts with a genvar declaration");
      if (!match(TokenKind::Comma)) {
        break;
      }
      continue;
    }
    if (parameter_conflict) {
      error(
          name,
          "FSIM-SV-SEM-020",
          "object '" + declaration.name
              + "' conflicts with a parameter declaration");
      if (!match(TokenKind::Comma)) {
        break;
      }
      continue;
    }
    const auto existing_port = std::find_if(
        unit.ports.begin(),
        unit.ports.end(),
        [&](const SignalDeclaration& port) {
          return port.name == declaration.name;
        });
    if (declaration.is_port) {
      if (!non_ansi_ports_.contains(declaration.name)
          || !body_port_declarations_.insert(declaration.name).second) {
        error(
            name,
            "FSIM-SV-SEM-004",
            "duplicate port declaration '" + declaration.name + "'");
      }
      update_or_add_port(unit, std::move(declaration));
    } else if (existing_port != unit.ports.end()) {
      // In a non-ANSI declaration, `output q; reg q;` describes one port,
      // not a distinct internal signal.
      if (!non_ansi_ports_.contains(declaration.name)
          || !port_type_refinements_.insert(declaration.name).second) {
        error(
            name,
            "FSIM-SV-SEM-005",
            "duplicate declaration of port '" + declaration.name + "'");
      } else {
        (void)update_existing_port_type(unit, declaration);
      }
    } else {
      const auto duplicate = std::find_if(
          unit.signals.begin(),
          unit.signals.end(),
          [&](const SignalDeclaration& signal) {
            return signal.name == declaration.name;
          });
      if (duplicate != unit.signals.end()) {
        error(
            name,
            "FSIM-SV-SEM-006",
            "duplicate signal declaration '" + declaration.name + "'");
      } else {
        unit.signals.push_back(std::move(declaration));
      }
    }
    if (initializer
        && (spec.type.spelling == "wire"
            || spec.type.systemverilog_net_type == "wire")
        && spec.direction == PortDirection::Unknown) {
      Statement driver;
      driver.kind = StatementKind::Assignment;
      driver.assignment_kind = AssignmentKind::Continuous;
      driver.target = Expression{
          ExpressionKind::Identifier, name.text, {}, name.span};
      driver.value = std::move(*initializer);
      driver.verilog_drive_strength = drive_strength;
      driver.span = span_from(name, previous());
      unit.concurrent_statements.push_back(std::move(driver));
    }
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(TokenKind::Semicolon, "';' after declaration",
         "FSIM-SV-PARSE-008");
}

void VerilogParser::parse_procedural_declaration(Statement& block) {
  const auto start = current();
  (void)match_keyword("static");
  (void)match_keyword("automatic");
  Type type = default_verilog_type();
  if (keyword("string") || keyword("chandle") || keyword("event")
      || keyword("process")
      || keyword("struct") || keyword("union") || keyword("enum")) {
    type = parse_parameter_type();
  } else if (is_named_type_reference_start()) {
    type = parse_named_type();
  } else {
    parse_optional_net_type(type);
  }
  if (type.named_type.empty() && type.spelling == "wire") {
    error(
        start,
        "FSIM-SV-UNSUPPORTED-014",
        "procedural wire declarations are not supported; use a variable "
        "type");
  }
  if (type.named_type.empty()) {
    parse_optional_signedness(type);
    parse_optional_range(type);
  }

  for (;;) {
    const auto name = expect_identifier("local variable name");
    auto declaration_type = type;
    (void)parse_optional_container_dimension(declaration_type);
    current_procedural_names_.insert(name.text);
    current_procedural_types_.insert_or_assign(
        name.text, declaration_type);
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      initializer = parse_expression();
    }
    block.declarations.push_back(VariableDeclaration{
        name.text,
        std::move(declaration_type),
        std::move(initializer),
        span_from(name, previous())});
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after local variable declaration",
      "FSIM-SV-PARSE-045");
}

void VerilogParser::update_or_add_port(DesignUnit& unit,
                             SignalDeclaration declaration) {
  for (auto& existing : unit.ports) {
    if (existing.name == declaration.name) {
      existing.type = std::move(declaration.type);
      existing.direction = declaration.direction;
      existing.net_delay = std::move(declaration.net_delay);
      existing.drive_strength = std::move(declaration.drive_strength);
      existing.charge_strength = std::move(declaration.charge_strength);
      existing.charge_decay = std::move(declaration.charge_decay);
      existing.span = std::move(declaration.span);
      return;
    }
  }
  unit.ports.push_back(std::move(declaration));
}

bool VerilogParser::update_existing_port_type(
  DesignUnit& unit, const SignalDeclaration& declaration) {
  for (auto& existing : unit.ports) {
    if (existing.name == declaration.name) {
      existing.type = declaration.type;
      existing.net_delay = declaration.net_delay;
      existing.drive_strength = declaration.drive_strength;
      existing.charge_strength = declaration.charge_strength;
      existing.charge_decay = declaration.charge_decay;
      existing.span = cover(existing.span, declaration.span);
      return true;
    }
  }
  return false;
}

std::vector<Statement> VerilogParser::parse_continuous_assignments(
    const Token& start)
{
    auto strength = parse_verilog_drive_strength("continuous assignment");
    std::optional<Delay> delay;
    if (match(TokenKind::Hash)) {
        delay = parse_verilog_delay(previous(), 3);
    }
    std::vector<Statement> statements;
    do {
        if (!at(TokenKind::Identifier)) {
            error(
                current(), "FSIM-SV-PARSE-009",
                "expected continuous assignment target");
            skip_to_semicolon();
            return statements;
        }
        Expression target = parse_lvalue();
        expect(
            TokenKind::Assign, "'=' in continuous assignment",
            "FSIM-SV-PARSE-010");
        Expression value = parse_expression();
        Statement statement;
        statement.kind = StatementKind::Assignment;
        statement.assignment_kind = AssignmentKind::Continuous;
        statement.target = std::move(target);
        statement.value = std::move(value);
        statement.delay = delay;
        statement.verilog_drive_strength = strength;
        statement.span = cover(start.span, previous().span);
        statements.push_back(std::move(statement));
    } while (match(TokenKind::Comma));
    expect(TokenKind::Semicolon, "';' after continuous assignment",
        "FSIM-SV-PARSE-011");
    return statements;
}

[[nodiscard]] bool VerilogParser::is_gate_primitive() const {
  return keyword("buf") || keyword("not")
      || keyword("and") || keyword("nand")
      || keyword("or") || keyword("nor")
      || keyword("xor") || keyword("xnor")
      || keyword("bufif0") || keyword("bufif1")
      || keyword("notif0") || keyword("notif1");
}

void VerilogParser::parse_gate_primitive(
    std::vector<Statement>& statements,
    const std::vector<SignalDeclaration>& signals,
    const std::vector<SignalDeclaration>& ports) {
  const auto start = advance();
  const auto operation = detail::ascii_lower(start.text);
  auto strength = parse_verilog_drive_strength("gate primitive");
  std::optional<Delay> delay;
  if (match(TokenKind::Hash)) {
    const bool tristate = operation == "bufif0"
        || operation == "bufif1" || operation == "notif0"
        || operation == "notif1";
    delay = parse_verilog_delay(previous(), tristate ? 3 : 2);
  }

  const bool unary = operation == "buf" || operation == "not";
  const bool tristate = operation == "bufif0"
      || operation == "bufif1" || operation == "notif0"
      || operation == "notif1";
  do {
    const auto instance_start = current();
    std::string instance_name;
    if (at(TokenKind::Identifier)
        && (at(TokenKind::LeftParen, 1)
            || at(TokenKind::LeftBracket, 1))) {
      instance_name = advance().text;
    }
    std::vector<std::int64_t> instance_indices;
    if (match(TokenKind::LeftBracket)) {
      const auto range_start = previous();
      const auto left = parse_expression();
      expect(
          TokenKind::Colon,
          "':' in gate-instance array range",
          "FSIM-SV-PARSE-210");
      const auto right = parse_expression();
      expect(
          TokenKind::RightBracket,
          "']' after gate-instance array range",
          "FSIM-SV-PARSE-211");
      const auto literal_value =
          [&](auto& self,
              const Expression& expression)
              -> std::optional<std::int64_t> {
            if (expression.kind == ExpressionKind::IntegerLiteral) {
              return detail::decimal_i64(expression.text);
            }
            if (expression.kind == ExpressionKind::Unary
                && expression.operands.size() == 1
                && (expression.text == "+"
                    || expression.text == "-")) {
              const auto operand = self(
                  self, expression.operands.front());
              if (!operand) {
                return std::nullopt;
              }
              if (expression.text == "+") {
                return operand;
              }
              if (*operand
                  == std::numeric_limits<std::int64_t>::min()) {
                return std::nullopt;
              }
              return -*operand;
            }
            return std::nullopt;
          };
      const auto left_value = literal_value(literal_value, left);
      const auto right_value = literal_value(literal_value, right);
      if (instance_name.empty()) {
        error(
            range_start,
            "FSIM-SV-SEM-111",
            "a gate-instance array requires an instance name");
      } else if (!left_value || !right_value) {
        error(
            range_start,
            "FSIM-SV-SEM-112",
            "gate-instance array bounds must be decimal locally static "
            "integers in this bounded slice");
      } else {
        const auto distance = *left_value >= *right_value
            ? static_cast<std::uint64_t>(*left_value)
                - static_cast<std::uint64_t>(*right_value)
            : static_cast<std::uint64_t>(*right_value)
                - static_cast<std::uint64_t>(*left_value);
        if (distance >= maximum_instance_array_elements) {
          error(
              range_start,
              "FSIM-SV-SEM-113",
              "materializing the gate-instance array would exceed the "
              "256 MiB frontend owning-storage budget");
        } else {
          const auto count = distance + 1;
          for (std::uint64_t ordinal = 0; ordinal < count; ++ordinal) {
            instance_indices.push_back(
                *left_value >= *right_value
                    ? *left_value - static_cast<std::int64_t>(ordinal)
                    : *left_value + static_cast<std::int64_t>(ordinal));
          }
        }
      }
    }
    expect(
        TokenKind::LeftParen,
        "'(' after gate primitive name",
        "FSIM-SV-PARSE-091");
    if (!at(TokenKind::Identifier)) {
      error(
          current(),
          "FSIM-SV-PARSE-092",
          "a gate primitive requires an output lvalue first");
      skip_to_semicolon();
      return;
    }
    auto target = parse_lvalue();
    std::vector<Expression> inputs;
    while (match(TokenKind::Comma)) {
      inputs.push_back(parse_expression());
    }
    expect(
        TokenKind::RightParen,
        "')' after gate primitive terminals",
        "FSIM-SV-PARSE-093");

    if ((unary && inputs.size() != 1)
        || (tristate && inputs.size() != 2)
        || (!unary && !tristate && inputs.size() < 2)) {
      error(
          instance_start,
          "FSIM-SV-SEM-026",
          unary
              ? "buf/not primitives require exactly one input terminal"
          : tristate
              ? "bufif/notif primitives require one data and one control "
                "input terminal"
              : "logic gate primitives require at least two input terminals");
      continue;
    }

    const auto terminal_for =
        [&](Expression terminal,
            const std::size_t ordinal,
            const std::int64_t instance_index) {
          auto terminal_index = instance_index;
          const SignalDeclaration* declaration = nullptr;
          if (terminal.kind == ExpressionKind::Identifier) {
            const auto find = [&](const auto& declarations) {
              return std::find_if(
                  declarations.begin(),
                  declarations.end(),
                  [&](const SignalDeclaration& candidate) {
                    return candidate.name == terminal.text;
                  });
            };
            const auto signal = find(signals);
            if (signal != signals.end()) {
              declaration = &*signal;
            } else {
              const auto port = find(ports);
              if (port != ports.end()) {
                declaration = &*port;
              }
            }
          }
          if (declaration) {
            const auto width = declaration->type.width();
            if (width && *width == 1) {
              return terminal;
            }
            if (!width || *width != instance_indices.size()) {
              error(
                  instance_start,
                  "FSIM-SV-SEM-114",
                  "a gate-array terminal must be scalar or match the "
                  "instance count");
              return terminal;
            }
            if (declaration->type.packed_range) {
              const auto& range = *declaration->type.packed_range;
              terminal_index = range.left
                  + (range.descending
                         ? -static_cast<std::int64_t>(ordinal)
                         : static_cast<std::int64_t>(ordinal));
            }
          } else if (
              terminal.kind == ExpressionKind::LogicLiteral
              && terminal.text.starts_with("1'")) {
            return terminal;
          }
          Expression index{
              ExpressionKind::IntegerLiteral,
              std::to_string(terminal_index),
              {},
              terminal.span};
          return Expression{
              ExpressionKind::Index,
              "",
              {std::move(terminal), std::move(index)},
              terminal.span};
        };
    const bool gate_array = !instance_indices.empty();
    if (!gate_array) {
      instance_indices.push_back(0);
    }
    for (std::size_t ordinal = 0;
         ordinal < instance_indices.size(); ++ordinal) {
      auto mapped_target = !gate_array
          ? target
          : terminal_for(target, ordinal, instance_indices[ordinal]);
      std::vector<Expression> mapped_inputs;
      mapped_inputs.reserve(inputs.size());
      for (const auto& input : inputs) {
        mapped_inputs.push_back(
            !gate_array
                ? input
                : terminal_for(input, ordinal, instance_indices[ordinal]));
      }

      Expression value = std::move(mapped_inputs.front());
      if (unary) {
        if (operation == "not") {
          const auto combined = cover(start.span, value.span);
          value = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(value)},
              combined};
        }
      } else if (tristate) {
        if (operation == "notif0" || operation == "notif1") {
          const auto inverted_span = cover(start.span, value.span);
          value = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(value)},
              inverted_span};
        }
        auto control = std::move(mapped_inputs[1]);
        if (operation == "bufif0" || operation == "notif0") {
          const auto inverted_span = cover(start.span, control.span);
          control = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(control)},
              inverted_span};
        }
        Expression high_impedance{
            ExpressionKind::LogicLiteral,
            "1'bz",
            {},
            start.span};
        value = Expression{
            ExpressionKind::Call,
            "?:",
            {std::move(control), std::move(value),
             std::move(high_impedance)},
            cover(start.span, previous().span)};
      } else {
        const auto binary_operation =
            operation == "and" || operation == "nand"
                ? "&"
                : operation == "or" || operation == "nor"
                    ? "|"
                    : "^";
        for (std::size_t index = 1;
             index < mapped_inputs.size(); ++index) {
          const auto combined =
              cover(value.span, mapped_inputs[index].span);
          value = Expression{
              ExpressionKind::Binary,
              binary_operation,
              {std::move(value), std::move(mapped_inputs[index])},
              combined};
        }
        if (operation == "nand" || operation == "nor"
            || operation == "xnor") {
          const auto combined = cover(start.span, value.span);
          value = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(value)},
              combined};
        }
      }

      Statement statement;
      statement.kind = StatementKind::Assignment;
      statement.assignment_kind = AssignmentKind::Continuous;
      statement.target = std::move(mapped_target);
      statement.value = std::move(value);
      statement.delay = delay;
      statement.verilog_drive_strength = strength;
      statement.label = instance_name.empty()
          ? std::string{}
          : instance_name
              + (instance_indices.size() == 1
                     && !gate_array
                     ? std::string{}
                     : "[" + std::to_string(instance_indices[ordinal])
                         + "]");
      statement.span = cover(start.span, previous().span);
      statements.push_back(std::move(statement));
    }
  } while (match(TokenKind::Comma));
  expect(
      TokenKind::Semicolon,
      "';' after gate primitive",
      "FSIM-SV-PARSE-094");
}

Process VerilogParser::parse_always() {
  const auto start = advance();
  current_procedural_names_.clear();
  current_procedural_types_.clear();
  Process process;
  if (start.text == "always_ff") {
    process.kind = ProcessKind::SystemVerilogAlwaysFF;
  } else if (start.text == "always_comb") {
    process.kind = ProcessKind::SystemVerilogAlwaysComb;
  } else if (start.text == "always_latch") {
    process.kind = ProcessKind::SystemVerilogAlwaysLatch;
  } else {
    process.kind = ProcessKind::VerilogAlways;
  }
  if (process.kind == ProcessKind::SystemVerilogAlwaysFF
      && language_ == Language::Verilog2005) {
    error(start, "FSIM-VERILOG-SEM-001",
          "always_ff requires SystemVerilog");
  }
  if (process.kind == ProcessKind::SystemVerilogAlwaysComb
      && language_ == Language::Verilog2005) {
    error(start, "FSIM-VERILOG-SEM-002",
          "always_comb requires SystemVerilog");
  }
  if (process.kind == ProcessKind::SystemVerilogAlwaysLatch
      && language_ == Language::Verilog2005) {
    error(start, "FSIM-VERILOG-SEM-003",
          "always_latch requires SystemVerilog");
  }
  const bool implicit_sensitivity =
      process.kind == ProcessKind::SystemVerilogAlwaysComb
      || process.kind == ProcessKind::SystemVerilogAlwaysLatch;
  const bool sequential_always =
      process.kind == ProcessKind::SystemVerilogAlwaysFF;
  if (implicit_sensitivity) {
    if (match(TokenKind::At)) {
      error(
          previous(),
          "FSIM-SV-SEM-011",
          "always_comb/always_latch supplies its own implicit sensitivity "
          "and cannot have an explicit event control");
      (void)parse_sensitivity();
    }
    process.sensitivities.push_back(
        Sensitivity{EdgeKind::Any, "*", start.span, {}});
  } else if (match(TokenKind::At)) {
    process.sensitivities = parse_sensitivity();
  } else if (process.kind != ProcessKind::VerilogAlways) {
    error(current(), "FSIM-SV-PARSE-012",
          "always process requires an event control in this frontend "
          "slice");
  }
  auto body = parse_statement();
  if (body) {
    if (body->kind == StatementKind::Block
        && body->label.empty()) {
      process.variables = std::move(body->declarations);
      process.statements = std::move(body->statements);
    } else {
      process.statements.push_back(std::move(*body));
    }
  }
  if (process.kind == ProcessKind::VerilogAlways
      && process.sensitivities.empty()
      && !cycle_paths_are_safe(process.statements)) {
    error(
        start,
        "FSIM-SV-SEM-106",
        "every reachable body-timed always path must suspend or "
        "terminate before process re-entry");
  }
  if (implicit_sensitivity || sequential_always) {
    const auto inspect =
        [&](const auto& self,
            const std::vector<Statement>& statements,
            bool& has_timing,
            bool& has_nonblocking) -> void {
      for (const auto& statement : statements) {
        has_timing =
            has_timing
            || statement.kind == StatementKind::Delay
            || statement.kind == StatementKind::WaitOn
            || (statement.kind == StatementKind::Assignment
                && statement.procedural_assignment_control
                    != ProceduralAssignmentControl::None);
        has_nonblocking =
            has_nonblocking
            || (statement.kind == StatementKind::Assignment
                && statement.assignment_kind
                    == AssignmentKind::NonBlocking);
        self(
            self, statement.statements, has_timing,
            has_nonblocking);
        self(
            self, statement.else_statements, has_timing,
            has_nonblocking);
        for (const auto& alternative : statement.case_alternatives) {
          self(
              self, alternative.statements, has_timing,
              has_nonblocking);
        }
      }
    };
    bool has_timing = false;
    bool has_nonblocking = false;
    inspect(
        inspect, process.statements, has_timing,
        has_nonblocking);
    if (implicit_sensitivity && has_timing) {
      error(
          start,
          "FSIM-SV-SEM-012",
          "always_comb/always_latch cannot contain timing controls");
    }
    if (implicit_sensitivity && has_nonblocking) {
      error(
          start,
          "FSIM-SV-SEM-013",
          "always_comb/always_latch assignments must be blocking in this "
          "executable slice");
    }
    if (sequential_always
        && (process.sensitivities.size() != 1
            || process.sensitivities.front().edge == EdgeKind::Any)) {
      error(
          start,
          "FSIM-SV-SEM-101",
          "bounded always_ff requires exactly one edge-qualified event");
    }
    if (sequential_always && has_timing) {
      error(
          start,
          "FSIM-SV-SEM-102",
          "bounded always_ff cannot contain a nested timing control");
    }
  }
  process.span = span_from(start, previous());
  current_procedural_names_.clear();
  current_procedural_types_.clear();
  return process;
}
}  // namespace fsim::frontend
