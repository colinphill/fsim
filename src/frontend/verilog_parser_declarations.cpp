// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"

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
        || keyword("string") || keyword("virtual")
        || keyword("struct") || keyword("union") || keyword("enum")
        || at(TokenKind::LeftBracket);
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
        (void)require_standard(
            "an interface-typed port",
            StandardRevision::SystemVerilog2005,
            interface_start);
        std::string interface_type;
        std::string modport;
        if (explicit_generic_interface) {
          advance();
          // `interface.mp a`: a generic interface port restricted to a
          // modport of whatever interface is connected (25.4).
          if (match(TokenKind::Dot)) {
            modport = expect_identifier("interface modport name").text;
          }
        } else {
          interface_type = advance().text;
          if (match(TokenKind::Dot)) {
            modport = expect_identifier("interface modport name").text;
          }
        }
        const auto port_name = expect_identifier("interface port name");
        Type port_type{ValueDomain::Unknown, "interface", std::nullopt, false};
        // An interface port array (IEEE 1800-2017 25.5): `[N]` is
        // `[0:N-1]`.
        while (match(TokenKind::LeftBracket)) {
          const auto dimension_start = previous();
          auto left = parse_expression();
          Expression right;
          if (at(TokenKind::RightBracket)) {
            const auto size_span = left.span;
            right = Expression{ExpressionKind::Binary, "-",
                {std::move(left),
                 Expression{ExpressionKind::IntegerLiteral, "1", {},
                     size_span}},
                size_span};
            left = Expression{ExpressionKind::IntegerLiteral, "0", {},
                size_span};
          } else {
            expect(TokenKind::Colon, "':' in an interface port array range",
                "FSIM-SV-PARSE-221");
            right = parse_expression();
          }
          expect(TokenKind::RightBracket,
              "']' after an interface port array range", "FSIM-SV-PARSE-222");
          port_type.systemverilog_interface_array_dimensions.push_back(
              PackedRangeExpression{std::move(left), std::move(right),
                  cover(dimension_start.span, previous().span),
                  std::nullopt});
        }
        SignalDeclaration declaration{
            port_name.text,
            std::move(port_type),
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
        const auto direction_token = current();
        (void)require_standard(
            "an ANSI module-port declaration",
            StandardRevision::Verilog2001,
            direction_token);
        spec.direction = parse_direction();
        spec.type = default_port_net_type();
        if (at(TokenKind::Identifier)
            && declaration_word_standard(current().text)
            && !keyword_reserved(keyword_set_, current().text)) {
          const auto later_type = advance();
          (void)require_standard(
              "port type '" + later_type.text + "'",
              *declaration_word_standard(later_type.text),
              later_type);
        }
        const bool explicit_variable = match_keyword("var");
        declared_here = true;
        // In Verilog an input or inout port is a net (IEEE 1364-2005
        // 12.3.4).
        if ((spec.direction == PortDirection::Input
                || spec.direction == PortDirection::Inout)
            && (keyword("reg") || keyword("integer") || keyword("time")
                || keyword("real") || keyword("realtime"))
            && keyword_set_rank(keyword_set_for_standard_revision(standard_revision_))
                < keyword_set_rank(keyword_set_for_standard_revision(
                    StandardRevision::SystemVerilog2005))) {
          error(current(), "FSIM-SV-SEM-410",
              "a Verilog input or inout port must be a net, not a variable");
        }
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
          // `input wire T x`: a net port whose data type is a type name
          // (IEEE 1800-2017 23.2.2.3).
          if (is_named_type_reference_start()
              && !at(TokenKind::Comma, 1) && !at(TokenKind::RightParen, 1)
              && !at(TokenKind::LeftBracket, 1)) {
            spec.type = parse_named_type();
          }
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
        (void)require_standard(
            "an ANSI port default expression",
            StandardRevision::SystemVerilog2005,
            initializer);
        (void)parse_expression();
        error(
            initializer,
            "FSIM-SV-UNSUPPORTED-010",
            "ANSI port default expressions are not executable in this "
            "frontend slice");
      }
      if (at(TokenKind::LeftBracket)) {
        (void)require_standard(
            "an unpacked port dimension",
            StandardRevision::SystemVerilog2005,
            current());
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
  if (language_ != Language::SystemVerilog2017) {
    return false;
  }
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
    if (!at(TokenKind::Identifier, offset + 2)) {
      return false;
    }
    std::size_t cursor = offset + 2U;
    while (at(TokenKind::Scope, cursor + 1U)
           && at(TokenKind::Identifier, cursor + 2U)) {
      cursor += 2U;
    }
    if (at(TokenKind::Hash, cursor + 1U)
        && at(TokenKind::LeftParen, cursor + 2U)) {
      cursor += 2U;
      std::size_t depth{};
      do {
        if (at(TokenKind::LeftParen, cursor)) {
          ++depth;
        } else if (at(TokenKind::RightParen, cursor)
                   && --depth == 0U) {
          ++cursor;
          break;
        }
        ++cursor;
      } while (!at(TokenKind::EndOfFile, cursor));
      return at(TokenKind::Identifier, cursor);
    }
    return at(TokenKind::Identifier, cursor + 1U);
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
    const bool standard_member = name == "std"
        && at(TokenKind::Identifier)
        && find_systemverilog_standard_package_declaration(
               standard_revision_, current().text) != nullptr;
    const auto selected = standard_member
        ? advance()
        : expect_identifier("package type name");
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
  const auto standard_name = name.starts_with("std::")
      && name.find("::", 5U) == std::string::npos
      ? std::string_view { name }.substr(5U)
      : std::string_view { name };
  const auto* standard_declaration
      = find_systemverilog_standard_package_declaration(
          standard_revision_, standard_name);
  if (standard_declaration != nullptr
      && standard_declaration->kind
          == SystemVerilogStandardPackageMemberKind::class_type) {
    if (require_standard(
            "predefined type '" + std::string { standard_name } + "'",
            standard_declaration->minimum_standard,
            first,
            "FSIM-SV-PARSE-349")) {
      type.spelling = std::string { standard_name };
      type.named_type = type.spelling;
      type.domain = ValueDomain::Bit2;
      type.systemverilog_scalar = SystemVerilogScalarKind::Chandle;
    }
  } else if (standard_name == "weak_reference") {
    (void)require_standard(
        "predefined type 'weak_reference'",
        StandardRevision::SystemVerilog2023,
        first,
        "FSIM-SV-PARSE-349");
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
  if (language_ == Language::SystemVerilog2017
      && (keyword("var") || keyword("const"))) {
    return true;
  }
  return is_direction_keyword() || is_net_type_keyword()
      || keyword("static") || keyword("automatic")
      || keyword("string") || keyword("chandle") || keyword("event")
      || keyword("process")
      || keyword("struct") || keyword("union") || keyword("enum")
      || is_named_type_reference_start()
      || (at(TokenKind::Identifier) && at(TokenKind::LeftBracket, 1)
          && packed_typedef_types_.contains(current().text));
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
  if (keyword("automatic") || keyword("static")) {
    const auto lifetime = advance();
    (void)require_standard(
        "an explicit variable lifetime",
        StandardRevision::SystemVerilog2005,
        lifetime);
  }
  const bool package_variable =
      unit.kind == UnitKind::SystemVerilogPackage;
  const bool systemverilog_const =
      package_variable && match_keyword("const");
  if (!package_variable && language_ == Language::SystemVerilog2017
      && keyword("const")) {
    // A module-level constant variable (IEEE 1800-2017 6.20.6): legal code
    // never writes it, so it is an ordinary initialized variable here.
    (void)advance();
  }
  VerilogTypeSpec spec;
  spec.type = default_verilog_type();
  if (language_ == Language::SystemVerilog2017 && !is_direction_keyword()
      && match_keyword("var")) {
    // `var` declares a variable; with an implicit data type it is logic
    // (IEEE 1800-2017 6.8).
    if (!is_net_type_keyword() && !keyword("string")
        && !keyword("chandle") && !keyword("event")
        && !keyword("struct") && !keyword("union") && !keyword("enum")
        && !is_named_type_reference_start()) {
      spec.type.spelling = "logic";
    }
  }
  bool port_type_explicit = false;
  if (is_direction_keyword()) {
    spec.direction = parse_direction();
    spec.type = default_port_net_type();
    const bool explicit_variable = match_keyword("var");
    port_type_explicit = explicit_variable || is_net_type_keyword()
        || keyword("string") || keyword("chandle") || keyword("process")
        || keyword("struct") || keyword("union") || keyword("enum")
        || is_named_type_reference_start();
    const bool explicit_type =
        explicit_variable || is_net_type_keyword()
        || keyword("string") || keyword("chandle")
        || keyword("process")
        || keyword("struct") || keyword("union") || keyword("enum")
        || is_named_type_reference_start();
    // In Verilog an input or inout port is a net (IEEE 1364-2005 12.3.4).
    if ((spec.direction == PortDirection::Input
            || spec.direction == PortDirection::Inout)
        && (keyword("reg") || keyword("integer") || keyword("time")
            || keyword("real") || keyword("realtime"))
        && keyword_set_rank(keyword_set_for_standard_revision(standard_revision_))
            < keyword_set_rank(keyword_set_for_standard_revision(
                StandardRevision::SystemVerilog2005))) {
      error(current(), "FSIM-SV-SEM-410",
          "a Verilog input or inout port must be a net, not a variable");
    }
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
  } else if (auto expanded = parse_named_packed_array_type()) {
    spec.type = std::move(*expanded);
  } else if (is_named_type_reference_start()) {
    spec.type = parse_named_type();
  } else {
    parse_optional_net_type(spec.type);
    // A net of an aggregate or enumeration data type (IEEE 1800-2017
    // 6.7.1): `wire struct packed {...} w;`.
    if (language_ == Language::SystemVerilog2017
        && contains_word(
            {"wire", "tri", "tri0", "tri1", "wand", "triand", "wor",
                "trior", "trireg", "uwire", "supply0", "supply1"},
            spec.type.spelling)
        && (keyword("struct") || keyword("union") || keyword("enum"))) {
      const auto net_kind = spec.type.spelling;
      spec.type = parse_parameter_type();
      spec.type.systemverilog_net_type = net_kind;
    } else if (language_ == Language::SystemVerilog2017
        && contains_word(
            {"wire", "tri", "tri0", "tri1", "wand", "triand", "wor",
                "trior", "trireg", "uwire", "supply0", "supply1"},
            spec.type.spelling)
        && is_named_type_reference_start()) {
      // A net of a user-defined data type: `wire byte_t [1:0] w;`.
      const auto net_kind = spec.type.spelling;
      if (auto named_array = parse_named_packed_array_type()) {
        spec.type = std::move(*named_array);
      } else {
        spec.type = parse_named_type();
      }
      spec.type.systemverilog_net_type = net_kind;
      // A net's data type is integral or an aggregate of integral types;
      // a class handle is not (IEEE 1800-2017 6.7.1).
      if (std::ranges::any_of(unit.systemverilog_classes,
              [&](const SystemVerilogClassDeclaration& declared) {
                return declared.name == spec.type.named_type;
              })) {
        error(start, "FSIM-SV-SEM-407",
            "a net cannot have the class type '" + spec.type.named_type
                + "'");
      }
    }
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
    // A hidden typedef gives the anonymous enumeration a type definition,
    // so its methods and literal names resolve as for a typedef
    // (IEEE 1800-2017 6.19.5). Its first literal's name, unique in this
    // scope, keys it.
    std::vector<EnumLiteralDeclaration> literals;
    for (std::size_t index = 0;
         index < spec.type.enumeration_literals.size(); ++index) {
      literals.push_back({ spec.type.enumeration_literals[index],
          spec.type.systemverilog_enumeration_values[index],
          spec.type.systemverilog_enumeration_values[index].span });
    }
    if (!literals.empty()) {
      unit.type_aliases.push_back({
          anonymous_enumeration_type_name(literals.front().name),
          spec.type,
          span_from(start, previous()),
          std::move(literals),
          TypeDeclarationKind::SystemVerilogTypedef,
          { },
          { },
          { },
          false });
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
    if (declaration_type.systemverilog_container) {
      unit_container_names_.insert(name.text);
      unit_container_types_.insert_or_assign(name.text, declaration_type);
      if (declaration_type.systemverilog_container->associative_index_type) {
        unit_associative_index_types_.insert_or_assign(name.text,
            *declaration_type.systemverilog_container->associative_index_type);
      }
    }
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      const auto assignment = previous();
      if (!contains_word(
              {"wire", "tri", "tri0", "tri1", "wand", "triand",
               "wor", "trior", "trireg", "supply0", "supply1"},
              spec.type.spelling)) {
        (void)require_standard(
            "a variable declaration initializer",
            StandardRevision::Verilog2001,
            assignment);
      }
      initializer = parse_expression();
    }
    const bool named_construction =
        !declaration_type.named_type.empty()
        && initializer
        && initializer->kind == ExpressionKind::Call
        && initializer->text == "@sv-new";
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
        // A class handle initialized by `new` is constructed before any
        // initial or always procedure starts (IEEE 1800-2017 6.21, 8.7);
        // the construction runs in a leading initializer process.
        const auto& named = declaration_type.named_type;
        const bool class_construction = named_construction
            && !package_variable && !systemverilog_const
            && (unit.kind == UnitKind::VerilogModule
                || unit.kind == UnitKind::SystemVerilogProgram
                || unit.kind == UnitKind::SystemVerilogInterface)
            && named != "mailbox" && named != "semaphore"
            && named != "process"
            // A scoped type may name a package covergroup.
            && named.find("::") == std::string::npos
            && std::ranges::none_of(
                unit.systemverilog_covergroups,
                [&](const SystemVerilogCovergroupDeclaration& covergroup) {
                  return covergroup.name == named;
                });
        std::optional<Expression> construction;
        if (class_construction) {
          construction = std::move(initializer);
          initializer.reset();
        }
        VariableDeclaration variable{
            name.text,
            std::move(declaration_type),
            std::move(initializer),
            span_from(start, previous())};
        variable.systemverilog_const = systemverilog_const;
        unit.variables.push_back(std::move(variable));
        if (construction) {
          Statement assignment;
          assignment.kind = StatementKind::Assignment;
          assignment.assignment_kind = AssignmentKind::Blocking;
          assignment.target = Expression {
              ExpressionKind::Identifier, name.text, { }, name.span
          };
          assignment.value = std::move(*construction);
          assignment.span = cover(name.span, assignment.value.span);
          Process initializer_process;
          initializer_process.kind = ProcessKind::Initial;
          initializer_process.name = "$class_initializer_" + name.text;
          initializer_process.statements.push_back(std::move(assignment));
          initializer_process.span
              = initializer_process.statements.front().span;
          const auto position = std::ranges::find_if(
              unit.processes, [](const Process& process) {
                return !process.name.starts_with("$class_initializer_");
              });
          unit.processes.insert(position, std::move(initializer_process));
        }
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
      if (port_type_explicit) {
        explicit_port_types_.insert(declaration.name);
      }
      // `int i; output i;` declares one port whose data type is the
      // earlier declaration's (IEEE 1800-2017 23.2.2.1).
      if (existing_port != unit.ports.end()
          && port_type_refinements_.contains(declaration.name)) {
        check_port_redeclaration(declaration.type, port_type_explicit,
            existing_port->type, name, "port");
        const auto port_signed = declaration.type.is_signed;
        declaration.type = existing_port->type;
        declaration.type.is_signed = declaration.type.is_signed
            || port_signed;
        declaration.net_delay = existing_port->net_delay;
        declaration.drive_strength = existing_port->drive_strength;
        declaration.charge_strength = existing_port->charge_strength;
        declaration.charge_decay = existing_port->charge_decay;
      } else if (const auto prior = std::ranges::find(
                     unit.signals, declaration.name,
                     &SignalDeclaration::name);
          prior != unit.signals.end()
          && non_ansi_ports_.contains(declaration.name)
          && port_type_refinements_.insert(declaration.name).second) {
        // The data declaration preceded every port declaration, so it was
        // recorded as a signal; it becomes the port's data type.
        const auto port_signed = declaration.type.is_signed;
        declaration.type = prior->type;
        declaration.type.is_signed = declaration.type.is_signed
            || port_signed;
        declaration.net_delay = prior->net_delay;
        declaration.drive_strength = prior->drive_strength;
        declaration.charge_strength = prior->charge_strength;
        declaration.charge_decay = prior->charge_decay;
        unit.signals.erase(prior);
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
        if (existing_port->direction != PortDirection::Unknown) {
          check_port_redeclaration(existing_port->type,
              explicit_port_types_.contains(declaration.name),
              declaration.type, name, "port");
        }
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
    // A net declaration assignment is a continuous assignment to the net
    // (IEEE 1800-2017 6.7.1, 10.3.1).
    const auto net_spelling = [](const std::string_view spelling) {
      return spelling == "wire" || spelling == "tri" || spelling == "tri0"
          || spelling == "tri1" || spelling == "wand" || spelling == "triand"
          || spelling == "wor" || spelling == "trior" || spelling == "uwire"
          || spelling == "supply0" || spelling == "supply1";
    };
    if (initializer
        && (net_spelling(spec.type.spelling)
            || net_spelling(spec.type.systemverilog_net_type))
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
    } else if (initializer) {
      Statement assignment;
      assignment.kind = StatementKind::Assignment;
      assignment.assignment_kind = AssignmentKind::Blocking;
      assignment.target = Expression{
          ExpressionKind::Identifier, name.text, {}, name.span};
      assignment.value = std::move(*initializer);
      assignment.span = span_from(name, previous());
      Process process;
      process.kind = ProcessKind::Initial;
      process.name = "$declaration_initializer_" + name.text;
      process.statements.push_back(std::move(assignment));
      process.span = process.statements.front().span;
      unit.processes.push_back(std::move(process));
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
  const bool automatic = match_keyword("automatic");
  Type type = default_verilog_type();
  if (keyword("string") || keyword("chandle") || keyword("event")
      || keyword("process")
      || keyword("struct") || keyword("union") || keyword("enum")) {
    type = parse_parameter_type();
  } else if (auto expanded = parse_named_packed_array_type()) {
    type = std::move(*expanded);
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
    // An automatic variable is initialized each time its scope is entered,
    // also inside a static callable or block (IEEE 1800-2017 6.21).
    if (automatic && initializer && !automatic_callable_) {
      Statement initialization;
      initialization.kind = StatementKind::Assignment;
      initialization.assignment_kind = AssignmentKind::Blocking;
      initialization.target = Expression{
          ExpressionKind::Identifier, name.text, {}, name.span};
      initialization.value = *initializer;
      initialization.span = span_from(name, previous());
      block.statements.push_back(std::move(initialization));
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

void VerilogParser::check_port_redeclaration(
  const Type& port_type,
  const bool port_explicit,
  const Type& data_type,
  const Token& name,
  const std::string_view what) {
  if (port_explicit) {
    error(name, "FSIM-SV-SEM-396",
        std::string { what } + " '" + name.text
            + "' declared with a net or data type cannot be declared again");
    return;
  }
  // The data declaration supplies the type; an implicit port declaration
  // may only repeat its packed range (IEEE 1800-2017 23.2.2.1).
  const auto ranged = [](const Type& type) {
    return type.packed_range || type.packed_range_expression
        || !type.systemverilog_packed_dimensions.empty();
  };
  const bool atom = !data_type.named_type.empty()
      || !data_type.packed_members.empty()
      || !data_type.enumeration_literals.empty()
      || contains_word({ "integer", "int", "shortint", "longint", "byte",
                         "time", "real", "realtime", "shortreal", "string",
                         "event", "chandle" },
          data_type.spelling);
  bool mismatch = false;
  if (atom) {
    mismatch = ranged(port_type);
  } else if (ranged(port_type) && !ranged(data_type)) {
    // An unranged port declaration takes the data declaration's range, as
    // xsim and Verilator accept.
    mismatch = true;
  } else if (port_type.packed_range && data_type.packed_range) {
    mismatch = port_type.packed_range->left != data_type.packed_range->left
        || port_type.packed_range->right != data_type.packed_range->right;
  }
  if (mismatch) {
    error(name, "FSIM-SV-SEM-396",
        "the declarations of " + std::string { what } + " '" + name.text
            + "' have different packed ranges or types");
  }
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
        if (!at(TokenKind::Identifier) && !at(TokenKind::LeftBrace)) {
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
  if (program_generate_context_) {
    error(
        start,
        "FSIM-SV-SEM-390",
        "a program block cannot contain a built-in primitive instance");
  }
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
    if (at(TokenKind::RightParen)) {
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

    if ((unary && inputs.empty())
        || (tristate && inputs.size() != 2)
        || (!unary && !tristate && inputs.empty())) {
      error(
          instance_start,
          "FSIM-SV-SEM-026",
          unary
              ? "buf/not primitives require one or more output terminals "
                "followed by exactly one input terminal"
          : tristate
              ? "bufif/notif primitives require one data and one control "
                "input terminal"
              : "logic gate primitives require at least one input terminal");
      continue;
    }

    std::vector<Expression> targets;
    targets.push_back(std::move(target));
    if (unary) {
      const auto output_terminal = [&](const auto& self,
                                       const Expression& expression)
          -> bool {
        if (expression.kind == ExpressionKind::Identifier) return true;
        if ((expression.kind == ExpressionKind::Index
             || expression.kind == ExpressionKind::Slice)
            && !expression.operands.empty()) {
          return self(self, expression.operands.front());
        }
        return expression.kind == ExpressionKind::Concatenation
            && !expression.operands.empty()
            && std::ranges::all_of(
                expression.operands,
                [&](const Expression& operand) {
                  return self(self, operand);
                });
      };
      for (std::size_t index = 0; index + 1 < inputs.size(); ++index) {
        if (!output_terminal(output_terminal, inputs[index])) {
          error(
              instance_start,
              "FSIM-SV-SEM-388",
              "every buf/not terminal except the final input must be a "
              "net lvalue");
        }
        targets.push_back(std::move(inputs[index]));
      }
      auto input = std::move(inputs.back());
      inputs.clear();
      inputs.push_back(std::move(input));
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
      std::vector<Expression> mapped_targets;
      mapped_targets.reserve(targets.size());
      for (const auto& output : targets) {
        mapped_targets.push_back(
            !gate_array
                ? output
                : terminal_for(
                    output, ordinal, instance_indices[ordinal]));
      }
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

      const auto base_label = instance_name.empty()
          ? std::string{}
          : instance_name
              + (instance_indices.size() == 1
                     && !gate_array
                     ? std::string{}
                     : "[" + std::to_string(instance_indices[ordinal])
                         + "]");
      for (std::size_t output = 0; output < mapped_targets.size(); ++output) {
        Statement statement;
        statement.kind = StatementKind::Assignment;
        statement.assignment_kind = AssignmentKind::Continuous;
        statement.target = std::move(mapped_targets[output]);
        statement.value = value;
        statement.delay = delay;
        statement.verilog_drive_strength = strength;
        statement.label = base_label.empty() || mapped_targets.size() == 1
            ? base_label
            : base_label + "$output" + std::to_string(output);
        statement.span = cover(start.span, previous().span);
        statements.push_back(std::move(statement));
      }
    }
  } while (match(TokenKind::Comma));
  expect(
      TokenKind::Semicolon,
      "';' after gate primitive",
      "FSIM-SV-PARSE-094");
}

Process VerilogParser::parse_always() {
  const auto start = advance();
  if (start.text == "always_ff" || start.text == "always_comb"
      || start.text == "always_latch") {
    (void)require_standard(
        "process form '" + start.text + "'",
        StandardRevision::SystemVerilog2005,
        start,
        "FSIM-SV-PARSE-347");
  }
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
    // always_ff begins with one event control (IEEE 1800-2017 9.2.2.4),
    // which may list several events (`@(posedge clk or negedge rst)`).
    if (sequential_always && process.sensitivities.empty()) {
      error(
          start,
          "FSIM-SV-SEM-101",
          "always_ff requires an event control");
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
