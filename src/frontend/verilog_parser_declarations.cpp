// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"
#include "fsim/support/index_distance.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"

namespace fsim::frontend {
namespace {

bool same_port_bound_expression(
    const Expression& left, const Expression& right)
{
  if (left.kind != right.kind || left.text != right.text
      || left.nominal_type != right.nominal_type
      || left.operands.size() != right.operands.size()
      || left.aggregate_choices != right.aggregate_choices
      || left.aggregate_choice_expressions.size()
          != right.aggregate_choice_expressions.size()
      || left.decoded_string != right.decoded_string
      || left.call_argument_names != right.call_argument_names
      || left.call_argument_directions != right.call_argument_directions
      || left.call_result_width != right.call_result_width
      || left.call_result_domain != right.call_result_domain
      || left.call_result_signed != right.call_result_signed
      || left.systemverilog_scalar_kind
          != right.systemverilog_scalar_kind
      || left.generated_text != right.generated_text
      || left.systemverilog_decimal_literal.has_value()
          != right.systemverilog_decimal_literal.has_value()) {
    return false;
  }
  if (left.systemverilog_decimal_literal
      && (left.systemverilog_decimal_literal->kind
              != right.systemverilog_decimal_literal->kind
          || left.systemverilog_decimal_literal->digits
              != right.systemverilog_decimal_literal->digits
          || left.systemverilog_decimal_literal->decimal_exponent
              != right.systemverilog_decimal_literal->decimal_exponent
          || left.systemverilog_decimal_literal->time_unit
              != right.systemverilog_decimal_literal->time_unit)) {
    return false;
  }
  for (std::size_t index = 0; index < left.operands.size(); ++index) {
    if (!same_port_bound_expression(
            left.operands[index], right.operands[index])) {
      return false;
    }
  }
  for (std::size_t index = 0;
       index < left.aggregate_choice_expressions.size(); ++index) {
    const auto& left_choices = left.aggregate_choice_expressions[index];
    const auto& right_choices = right.aggregate_choice_expressions[index];
    if (left_choices.size() != right_choices.size()) {
      return false;
    }
    for (std::size_t choice = 0; choice < left_choices.size(); ++choice) {
      if (!same_port_bound_expression(
              left_choices[choice], right_choices[choice])) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

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
        || at(TokenKind::LeftBracket)
        // A class specialization (`C#(8)::t`) is never an expression.
        || (at(TokenKind::Identifier) && at(TokenKind::Hash, 1))
        // A packed array of a typedef name (`t[7:0]`, `p::t[3:0]`, 7.4.1).
        || (at(TokenKind::Identifier) && at(TokenKind::LeftBracket, 1)
            && packed_typedef_types_.contains(current().text))
        || (at(TokenKind::Identifier) && at(TokenKind::Scope, 1)
            && at(TokenKind::Identifier, 2) && at(TokenKind::LeftBracket, 3)
            && (packed_typedef_types_.contains(
                    current().text + "::" + current(2).text)
                || packed_typedef_types_.contains(current(2).text)));
  };
  const auto parse_actual = [&](ParameterOverride& override) {
    // `type(...)` is a type actual (6.23) unless it begins a constant
    // expression such as `type(a) == type(b)`.
    if (keyword("type") && at(TokenKind::LeftParen, 1)
        && language_ == Language::SystemVerilog2017) {
      const auto resume = index_;
      auto operator_type = parse_type_operator_type();
      if (operator_type
          && (at(TokenKind::Comma) || at(TokenKind::RightParen))) {
        override.type_value = std::move(*operator_type);
        return;
      }
      index_ = resume;
      override.value = parse_expression();
      return;
    }
    // `int'(x)` is a cast expression, not the type `int` (6.24.1).
    if (begins_unambiguous_type_actual()
        && !at(TokenKind::Apostrophe, 1)) {
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
      // `.p()` leaves the parameter at its default (IEEE 1800-2017
      // 23.10.2.2).
      if (match(TokenKind::RightParen)) {
        if (!match(TokenKind::Comma)) {
          break;
        }
        continue;
      }
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
  unit.systemverilog_port_slots.emplace();
  VerilogTypeSpec inherited;
  bool have_inherited_type = false;
  std::optional<bool> ansi_port_list;
  // The interface type and modport of the previous interface port.
  std::optional<std::pair<std::string, std::string>> inherited_interface;
  std::optional<Token> first_empty_slot;
  bool first_empty_ansi_error_reported = false;
  const auto note_port_style = [&](const bool ansi, const Token&) {
    if (!ansi_port_list) {
      ansi_port_list = ansi;
    }
    if (ansi && first_empty_slot && !first_empty_ansi_error_reported) {
      error(*first_empty_slot, "FSIM-SV-PARSE-001",
          "an empty port slot is only valid in a non-ANSI port list");
      first_empty_ansi_error_reported = true;
    }
  };
  // A port expression's names are the internal objects the body declares
  // (IEEE 1800-2017 23.2.2.1).
  const auto note_port_expression_names = [&](const Expression& expression) {
    const auto visit = [&](const auto& self, const Expression& node) -> void {
      if (node.kind == ExpressionKind::Identifier) {
        non_ansi_ports_.insert(node.text);
      }
      for (const auto& operand : node.operands) {
        self(self, operand);
      }
    };
    visit(visit, expression);
  };
  const auto add_port_expression = [&](std::string external,
                                       std::optional<Expression> expression,
                                       const SourceSpan span) {
    if (external.empty()) {
      external = "fsim__port_expression_"
          + std::to_string(non_ansi_port_expressions_.size());
    }
    if (expression) {
      note_port_expression_names(*expression);
    }
    non_ansi_port_expressions_.push_back(
        {external, std::move(expression), span});
    return external;
  };
  while (!at_end() && !at(TokenKind::RightParen)) {
      if (verilog_attribute_instance_start()) {
          parse_verilog_attribute_instances();
          continue;
      }
    if (at(TokenKind::Comma)) {
      const auto empty = current();
      if (have_inherited_type || ansi_port_list.value_or(false)) {
        error(empty, "FSIM-SV-PARSE-001",
            "an empty port slot is only valid in a non-ANSI port list");
        first_empty_ansi_error_reported = true;
      }
      if (!first_empty_slot) {
        first_empty_slot = empty;
      }
      unit.systemverilog_port_slots->push_back(std::nullopt);
      advance();
      if (at(TokenKind::RightParen)) {
        // The consumed comma separates this empty port from a trailing empty
        // port, preserving both positions in a list such as `(, )`.
        unit.systemverilog_port_slots->push_back(std::nullopt);
        break;
      }
      continue;
    } else if (at(TokenKind::Dot)
        && (have_inherited_type || ansi_port_list.value_or(false))) {
      const auto dot = advance();
      error(dot, "FSIM-SV-UNSUPPORTED-005",
            "named port connections are not valid in a module declaration");
      skip_to_port_delimiter();
    } else if (at(TokenKind::Dot)) {
      // `.name(expression)` names a port whose connection is the port
      // expression (IEEE 1800-2017 23.2.2.1).
      const auto dot = advance();
      const auto external = expect_identifier("port name");
      note_port_style(false, external);
      expect(TokenKind::LeftParen, "'(' after explicit port name",
          "FSIM-SV-PARSE-006");
      std::optional<Expression> internal;
      if (!at(TokenKind::RightParen)) {
        internal = parse_expression();
      }
      expect(TokenKind::RightParen, "')' after port expression",
          "FSIM-SV-PARSE-006");
      unit.systemverilog_port_slots->push_back(add_port_expression(
          external.text, std::move(internal),
          cover(dot.span, previous().span)));
    } else if (at(TokenKind::LeftBrace) && !have_inherited_type
        && !ansi_port_list.value_or(false)) {
      // An unnamed port whose port expression is a concatenation.
      const auto open = current();
      note_port_style(false, open);
      auto internal = parse_expression();
      const auto span = cover(open.span, previous().span);
      unit.systemverilog_port_slots->push_back(
          add_port_expression({}, std::move(internal), span));
    } else {
      const bool explicit_generic_interface = keyword("interface");
      // A type parameter of this unit names a data type, not an interface
      // (`parameter type T ... (T io)`, IEEE 1800-2017 23.2.2).
      const bool known_data_type = at(TokenKind::Identifier)
          && std::ranges::any_of(unit.parameters,
              [&](const ParameterDeclaration& parameter) {
                return parameter.kind == ParameterKind::Type
                    && parameter.name == current().text;
              });
      const bool typed_interface =
          at(TokenKind::Identifier) && !known_data_type
          && !keyword_reserved(keyword_set_, current().text)
          && ((at(TokenKind::Dot, 1)
               && at(TokenKind::Identifier, 2)
               && at(TokenKind::Identifier, 3))
              || at(TokenKind::Identifier, 1));
      // A bare name after an interface port is another port of the same
      // interface type and modport (IEEE 1800-2017 23.2.2.3).
      const bool inherited_interface_port = inherited_interface
          && at(TokenKind::Identifier)
          && !keyword_reserved(keyword_set_, current().text)
          && (at(TokenKind::Comma, 1) || at(TokenKind::RightParen, 1)
              || at(TokenKind::LeftBracket, 1));
      if (explicit_generic_interface || typed_interface
          || inherited_interface_port) {
        note_port_style(true, current());
        const auto interface_start = current();
        (void)require_standard(
            "an interface-typed port",
            StandardRevision::SystemVerilog2005,
            interface_start);
        std::string interface_type;
        std::string modport;
        if (inherited_interface_port) {
          interface_type = inherited_interface->first;
          modport = inherited_interface->second;
        } else if (explicit_generic_interface) {
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
        unit.systemverilog_port_slots->push_back(port_name.text);
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
            interface_type,
            modport};
        inherited_interface.emplace(
            std::move(interface_type), std::move(modport));
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
        if (at(TokenKind::RightParen)) {
          const auto empty = previous();
          error(empty, "FSIM-SV-PARSE-001",
              "an empty port slot is only valid in a non-ANSI port list");
          first_empty_ansi_error_reported = true;
          unit.systemverilog_port_slots->push_back(std::nullopt);
          break;
        }
        continue;
      }
      VerilogTypeSpec spec = inherited;
      bool declared_here = false;
      inherited_interface.reset();
      // `int b` after `input int a`, or a first port `logic [3:0] p`: an
      // ANSI port whose omitted direction is inherited, or inout for the
      // first port (IEEE 1800-2017 23.2.2.3).
      const bool typed_without_direction
          = language_ == Language::SystemVerilog2017
          && !is_direction_keyword()
          && (have_inherited_type ? inherited.direction
                                        != PortDirection::Unknown
                                  : unit.ports.empty())
          && (keyword("int") || keyword("integer") || keyword("logic")
              || keyword("bit") || keyword("byte") || keyword("shortint")
              || keyword("longint") || keyword("time") || keyword("real")
              || keyword("reg") || keyword("var")
              || (is_named_type_reference_start()
                  && at(TokenKind::Identifier, 1)));
      // A `ref` port passes a variable by reference (IEEE 1800-2017
      // 23.2.2.3).
      const bool ref_port = language_ == Language::SystemVerilog2017
          && keyword("ref");
      if (is_direction_keyword() || typed_without_direction || ref_port) {
        const auto direction_token = current();
        note_port_style(true, direction_token);
        (void)require_standard(
            "an ANSI module-port declaration",
            StandardRevision::Verilog2001,
            direction_token);
        spec.direction = ref_port && match_keyword("ref")
            ? PortDirection::Ref
            : typed_without_direction
            ? (have_inherited_type ? inherited.direction
                                   : PortDirection::Inout)
            : parse_direction();
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
        } else if (auto expanded = parse_named_packed_array_type()) {
          // `output nib_t [1:0] o`: a packed array of a typedef (IEEE
          // 1800-2017 7.4.1).
          spec.type = std::move(*expanded);
        } else if (is_named_type_reference_start()) {
          spec.type = parse_named_type();
        } else {
          const auto default_spelling = spec.type.spelling;
          parse_optional_net_type(spec.type);
          // `output var b` declares a variable of type logic (IEEE 1800-2017
          // 6.8), not the default net.
          if (explicit_variable && spec.type.spelling == default_spelling) {
            spec.type.spelling = "logic";
            spec.type.domain = ValueDomain::Logic4;
            spec.type.systemverilog_net_type.clear();
          }
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

      const auto name_position = position();
      const auto port_name = expect_identifier("port name");
      const bool ansi_port = spec.direction != PortDirection::Unknown;
      note_port_style(ansi_port, port_name);
      unit.systemverilog_port_slots->push_back(port_name.text);
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
        // A variable output port's initializer, or an input port's default
        // value when it is left unconnected (23.2.2.3, 23.2.2.4).
        declaration.default_value = parse_expression();
        // An output net port's initializer is a net declaration assignment,
        // a continuous assignment to the net (6.7.1, 23.2.2.3).
        const auto& net = spec.type.systemverilog_net_type.empty()
            ? spec.type.spelling
            : spec.type.systemverilog_net_type;
        if (spec.direction == PortDirection::Output
            && contains_word({"wire", "tri", "tri0", "tri1", "wand",
                                 "triand", "wor", "trior", "uwire"},
                net)) {
          Statement driver;
          driver.kind = StatementKind::Assignment;
          driver.assignment_kind = AssignmentKind::Continuous;
          driver.target = Expression{
              ExpressionKind::Identifier, port_name.text, {},
              port_name.span};
          driver.value = std::move(*declaration.default_value);
          driver.span = span_from(port_name, previous());
          unit.concurrent_statements.push_back(std::move(driver));
          declaration.default_value.reset();
        }
      }
      if (at(TokenKind::LeftBracket)) {
        if (ansi_port) {
          (void)require_standard(
              "an unpacked port dimension",
              StandardRevision::SystemVerilog2005,
              current());
          (void)parse_optional_container_dimension(declaration.type);
        } else {
          const auto open = advance();
          auto left = parse_expression();
          Expression right = left;
          if (match(TokenKind::Colon)) {
            right = parse_expression();
          }
          expect(TokenKind::RightBracket,
              "']' after non-ANSI port selection", "FSIM-SV-PARSE-006");
          // Reparse the whole selection as the port expression it is.
          const auto end = position();
          rewind(name_position);
          auto whole = parse_expression();
          if (position() != end) {
            rewind(end);
            whole = Expression{ExpressionKind::Identifier, port_name.text, {},
                port_name.span};
          }
          non_ansi_port_selections_.push_back({
              port_name.text, std::move(left), std::move(right),
              cover(open.span, previous().span), std::move(whole)});
        }
      }
      const bool repeated_non_ansi_port = !ansi_port
          && std::ranges::any_of(unit.ports,
              [&](const SignalDeclaration& port) {
                return port.name == port_name.text;
              });
      if (repeated_non_ansi_port) {
        // A name listed again is another port connected to the same
        // internal object (IEEE 1800-2017 23.2.2.1).
        const bool selected = !non_ansi_port_selections_.empty()
            && non_ansi_port_selections_.back().port_name == port_name.text
            && non_ansi_port_selections_.back().span.begin.offset
                > port_name.span.begin.offset;
        auto expression = selected
            ? std::move(non_ansi_port_selections_.back().expression)
            : Expression{ExpressionKind::Identifier, port_name.text, {},
                  port_name.span};
        if (selected) {
          non_ansi_port_selections_.pop_back();
        }
        unit.systemverilog_port_slots->back() = add_port_expression(
            {}, std::move(expression), span_from(port_name, previous()));
        (void)declared_here;
      } else {
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
      }
      (void)declared_here;
    }

    if (!match(TokenKind::Comma)) {
      break;
    }
    if (at(TokenKind::RightParen)) {
      const auto empty = previous();
      if (have_inherited_type || ansi_port_list.value_or(false)) {
        error(empty, "FSIM-SV-PARSE-001",
            "an empty port slot is only valid in a non-ANSI port list");
        first_empty_ansi_error_reported = true;
      }
      if (!first_empty_slot) {
        first_empty_slot = empty;
      }
      unit.systemverilog_port_slots->push_back(std::nullopt);
      break;
    }
  }
}

void VerilogParser::finalize_non_ansi_port_expressions(DesignUnit& unit)
{
  if (non_ansi_port_expressions_.empty() || !unit.systemverilog_port_slots) {
    return;
  }
  const auto identifier = [](const std::string& name, const SourceSpan& span) {
    return Expression{ExpressionKind::Identifier, name, {}, span};
  };
  const auto is_external = [&](const std::string& name) {
    return std::ranges::any_of(non_ansi_port_expressions_,
        [&](const NonAnsiPortExpression& port) {
          return port.external == name;
        });
  };
  // The internal objects the port expressions connect.
  std::vector<std::string> internals;
  const auto collect = [&](const auto& self, const Expression& node) -> void {
    if (node.kind == ExpressionKind::Identifier
        && std::ranges::find(internals, node.text) == internals.end()
        && std::ranges::any_of(unit.ports,
            [&](const SignalDeclaration& port) {
              return port.name == node.text && !is_external(node.text);
            })) {
      internals.push_back(node.text);
    }
    for (const auto& operand : node.operands) {
      self(self, operand);
    }
  };
  for (const auto& port : non_ansi_port_expressions_) {
    if (port.expression) {
      collect(collect, *port.expression);
    }
  }
  // A listed name or selection of an object another port expression also
  // connects is a port expression too (IEEE 1800-2017 23.2.2.1).
  for (auto& slot : *unit.systemverilog_port_slots) {
    if (!slot || is_external(*slot)
        || std::ranges::find(internals, *slot) == internals.end()) {
      continue;
    }
    const auto declared = std::ranges::find(
        unit.ports, *slot, &SignalDeclaration::name);
    const auto span = declared != unit.ports.end() ? declared->span
                                                   : SourceSpan{};
    const auto selection = std::ranges::find(
        non_ansi_port_selections_, *slot, &NonAnsiPortSelection::port_name);
    auto expression = identifier(*slot, span);
    if (selection != non_ansi_port_selections_.end()) {
      expression = std::move(selection->expression);
      non_ansi_port_selections_.erase(selection);
    }
    std::string external = "fsim__port_expression_"
        + std::to_string(non_ansi_port_expressions_.size());
    non_ansi_port_expressions_.push_back({external, std::move(expression),
        span});
    *slot = std::move(external);
  }
  // The internal objects are ordinary declarations of the body.
  std::unordered_map<std::string, SignalDeclaration> internal_declarations;
  for (const auto& name : internals) {
    const auto declared = std::ranges::find(
        unit.ports, name, &SignalDeclaration::name);
    if (declared == unit.ports.end()) {
      continue;
    }
    auto signal = std::move(*declared);
    unit.ports.erase(declared);
    internal_declarations.emplace(name, signal);
    signal.is_port = false;
    signal.direction = PortDirection::Unknown;
    unit.signals.push_back(std::move(signal));
  }
  std::vector<Statement> connections;
  for (const auto& port : non_ansi_port_expressions_) {
    // The port takes the direction of the objects it connects.
    std::optional<PortDirection> direction;
    const auto direction_of = [&](const auto& self, const Expression& node)
        -> void {
      if (node.kind == ExpressionKind::Identifier) {
        const auto found = internal_declarations.find(node.text);
        if (found != internal_declarations.end()) {
          if (!direction
              || found->second.direction == PortDirection::Inout) {
            direction = found->second.direction;
          }
        }
      }
      for (const auto& operand : node.operands) {
        self(self, operand);
      }
    };
    if (port.expression) {
      direction_of(direction_of, *port.expression);
    }
    const auto port_direction = direction.value_or(PortDirection::Input);
    Type type = default_port_net_type();
    if (port.expression && port.expression->kind == ExpressionKind::Identifier
        && internal_declarations.contains(port.expression->text)) {
      type = internal_declarations.at(port.expression->text).type;
    } else if (port.expression) {
      // The port is as wide as its port expression.
      const auto span = port.expression->span;
      type.packed_range_expression = PackedRangeExpression{
          Expression{ExpressionKind::Binary, "-",
              {Expression{ExpressionKind::Call, "$bits", {*port.expression},
                   span},
                  Expression{ExpressionKind::IntegerLiteral, "1", {}, span}},
              span},
          Expression{ExpressionKind::IntegerLiteral, "0", {}, span}, span,
          std::nullopt};
    }
    unit.ports.emplace_back(
        port.external, std::move(type), port_direction, true, port.span);
    body_port_declarations_.insert(port.external);
    if (!port.expression) {
      continue;
    }
    const auto external = identifier(port.external, port.span);
    if (port_direction == PortDirection::Inout) {
      unit.systemverilog_aliases.push_back(
          SystemVerilogAliasDeclaration{{external, *port.expression},
              port.span});
      continue;
    }
    Statement connection;
    connection.kind = StatementKind::Assignment;
    connection.assignment_kind = AssignmentKind::Continuous;
    // An input drives its internal objects; an output is driven by them.
    const bool output = port_direction == PortDirection::Output
        || port_direction == PortDirection::Buffer;
    connection.target = output ? external : *port.expression;
    connection.value = output ? *port.expression : external;
    connection.span = port.span;
    connections.push_back(std::move(connection));
  }
  // A port's connection is established before the body's processes start,
  // as for a port collapsed with its internal net (IEEE 1800-2017 23.3.3).
  unit.concurrent_statements.insert(unit.concurrent_statements.begin(),
      std::make_move_iterator(connections.begin()),
      std::make_move_iterator(connections.end()));
}

void VerilogParser::validate_non_ansi_port_selections(
    const DesignUnit& unit)
{
  for (const auto& selection : non_ansi_port_selections_) {
    const auto port = std::ranges::find(
        unit.ports, selection.port_name, &SignalDeclaration::name);
    if (port == unit.ports.end() || !port->type.packed_range_expression
        || !port->type.systemverilog_packed_dimensions.empty()) {
      diagnostics_.push_back({
          DiagnosticSeverity::Error,
          "FSIM-SV-UNSUPPORTED-051",
          "non-ANSI selected port '" + selection.port_name
              + "' is supported only when its selection exactly matches one "
                "declared packed range",
          selection.span,
          {}});
      continue;
    }
    const auto& declared = *port->type.packed_range_expression;
    if (!same_port_bound_expression(selection.left, declared.left)
        || !same_port_bound_expression(selection.right, declared.right)) {
      diagnostics_.push_back({
          DiagnosticSeverity::Error,
          "FSIM-SV-UNSUPPORTED-051",
          "non-ANSI selected port '" + selection.port_name
              + "' is supported only when its selection exactly matches the "
                "declared packed range; partial selections are not lowered",
          selection.span,
          {}});
    }
  }
}

namespace {

template <typename Declaration>
[[nodiscard]] bool declares_callable(
    const DesignUnit& unit, const Declaration& declaration) {
  return std::ranges::any_of(unit.functions,
             [&](const FunctionDeclaration& existing) {
               return existing.name == declaration.name;
             })
      || std::ranges::any_of(unit.tasks, [&](const TaskDeclaration& existing) {
           return existing.name == declaration.name;
         });
}

}  // namespace

void VerilogParser::declare_interface_prototype(
    DesignUnit& unit, const Token& start, FunctionDeclaration function) {
  if (replace_modport_prototype(unit, function)) return;
  if (declares_callable(unit, function)) {
    error(start, "FSIM-SV-SEM-066",
        "duplicate module function '" + function.name + "'");
    return;
  }
  unit.functions.push_back(std::move(function));
}

void VerilogParser::declare_interface_prototype(
    DesignUnit& unit, const Token& start, TaskDeclaration task) {
  if (replace_modport_prototype(unit, task)) return;
  if (declares_callable(unit, task)) {
    error(start, "FSIM-SV-SEM-073",
        "duplicate module task '" + task.name + "'");
    return;
  }
  unit.tasks.push_back(std::move(task));
}

bool VerilogParser::replace_modport_prototype(
    DesignUnit& unit, FunctionDeclaration& function) {
  const auto existing = std::ranges::find(
      unit.functions, function.name, &FunctionDeclaration::name);
  if (!modport_prototype_callables_.contains(function.name)
      || existing == unit.functions.end()) {
    return false;
  }
  modport_prototype_callables_.erase(function.name);
  *existing = std::move(function);
  return true;
}

bool VerilogParser::replace_modport_prototype(
    DesignUnit& unit, TaskDeclaration& task) {
  const auto existing =
      std::ranges::find(unit.tasks, task.name, &TaskDeclaration::name);
  if (!modport_prototype_callables_.contains(task.name)
      || existing == unit.tasks.end()) {
    return false;
  }
  modport_prototype_callables_.erase(task.name);
  *existing = std::move(task);
  return true;
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
        // A modport import or export may give the subroutine's prototype
        // (`export task send(input int v)`, IEEE 1800-2017 25.7.2); one the
        // interface does not declare declares it.
        std::optional<Token> prototype_name;
        if (callable_access != CallableAccess::None
            && (keyword("function") || keyword("task"))) {
          explicit_function = keyword("function");
          const auto keyword_token = advance();
          std::size_t open = index_;
          while (open < tokens_.size()
              && tokens_[open].kind != TokenKind::LeftParen
              && tokens_[open].kind != TokenKind::Comma
              && tokens_[open].kind != TokenKind::RightParen
              && tokens_[open].kind != TokenKind::EndOfFile) {
            ++open;
          }
          if (open < tokens_.size() && open > index_
              && tokens_[open].kind == TokenKind::LeftParen
              && tokens_[open - 1U].kind == TokenKind::Identifier) {
            std::size_t close = open;
            for (std::size_t depth{}; close < tokens_.size(); ++close) {
              if (tokens_[close].kind == TokenKind::LeftParen) {
                ++depth;
              } else if (tokens_[close].kind == TokenKind::RightParen
                  && --depth == 0U) {
                break;
              }
            }
            if (close < tokens_.size()) {
              prototype_name = tokens_[open - 1U];
              std::vector<Token> header(
                  tokens_.begin() + static_cast<std::ptrdiff_t>(index_),
                  tokens_.begin() + static_cast<std::ptrdiff_t>(close + 1U));
              auto terminator = synthetic_tokens(";", keyword_token.span);
              header.insert(header.end(), terminator.begin(), terminator.end());
              Token end;
              end.kind = TokenKind::EndOfFile;
              end.span = keyword_token.span;
              header.push_back(std::move(end));
              auto saved_tokens = std::move(tokens_);
              const auto saved_index = close + 1U;
              tokens_ = std::move(header);
              index_ = 0;
              std::optional<FunctionDeclaration> function;
              std::optional<TaskDeclaration> task;
              if (*explicit_function) {
                function = parse_function(keyword_token, true);
              } else {
                task = parse_task(keyword_token, true);
              }
              tokens_ = std::move(saved_tokens);
              index_ = saved_index;
              const bool declared = function
                  ? declares_callable(unit, *function)
                  : declares_callable(unit, *task);
              if (!declared) {
                modport_prototype_callables_.insert(prototype_name->text);
                if (function) {
                  unit.functions.push_back(std::move(*function));
                } else {
                  unit.tasks.push_back(std::move(*task));
                }
              }
            }
          }
        }
        // A modport expression `.alias(signal)` names an interface signal
        // by a port-visible alias (IEEE 1800-2017 25.5.4).
        std::string alias;
        std::optional<Token> alias_token;
        if (language_ == Language::SystemVerilog2017
            && match(TokenKind::Dot)) {
          alias_token = expect_identifier("modport expression name");
          alias = alias_token->text;
          expect(TokenKind::LeftParen, "'(' after modport expression name",
              "FSIM-SV-PARSE-213");
        }
        // An input modport expression may select part of an interface
        // signal (`.in(a[7:0])`, IEEE 1800-2017 25.5.4): the alias reads a
        // net as wide as the select, driven by it.
        std::optional<Expression> selected_signal;
        auto member = [&] {
          if (prototype_name) return *prototype_name;
          if (!alias_token || direction != PortDirection::Input
              || !at(TokenKind::Identifier)
              || !at(TokenKind::LeftBracket, 1U)
              || (at(TokenKind::Number, 2U)
                  && at(TokenKind::RightBracket, 3U)
                  && at(TokenKind::Dot, 4U))) {
            return expect_identifier("modport member name");
          }
          auto head = current();
          selected_signal = parse_expression();
          head.text = "__modport_" + declaration.name + "_" + alias;
          return head;
        }();
        if (selected_signal) {
          const auto span = selected_signal->span;
          SignalDeclaration net;
          net.name = member.text;
          net.type = Type{ValueDomain::Logic4, "wire", std::nullopt, false};
          net.type.packed_range_expression = PackedRangeExpression{
              Expression{ExpressionKind::Binary, "-",
                  {Expression{ExpressionKind::Call, "$bits",
                       {*selected_signal}, span},
                      Expression{ExpressionKind::IntegerLiteral, "1", {},
                          span}},
                  span},
              Expression{ExpressionKind::IntegerLiteral, "0", {}, span},
              span, std::nullopt};
          net.span = member.span;
          unit.signals.push_back(std::move(net));
          Statement driver;
          driver.kind = StatementKind::Assignment;
          driver.assignment_kind = AssignmentKind::Continuous;
          driver.target = Expression{
              ExpressionKind::Identifier, member.text, {}, member.span};
          driver.value = std::move(*selected_signal);
          driver.span = span;
          unit.concurrent_statements.push_back(std::move(driver));
        }
        // The expression may name a signal of a nested interface instance
        // (`.r(inner.val)`).
        bool hierarchical_member = false;
        // An element of an interface instance array (`.r(ch[0].val)`).
        const auto constant_index = [&] {
          while (alias_token && at(TokenKind::LeftBracket)
              && at(TokenKind::Number, 1U)
              && at(TokenKind::RightBracket, 2U)
              && at(TokenKind::Dot, 3U)) {
            advance();
            member.text += "[" + advance().text + "]";
            advance();
          }
        };
        constant_index();
        while (alias_token && at(TokenKind::Dot)) {
          advance();
          member.text += "." + expect_identifier("modport member name").text;
          hierarchical_member = true;
          constant_index();
        }
        if (alias_token) {
          expect(TokenKind::RightParen,
              "')' after modport expression", "FSIM-SV-PARSE-213");
        }
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
        const auto visible_name = alias.empty() ? member.text : alias;
        const bool duplicate = std::ranges::any_of(
            declaration.members,
            [&](const SystemVerilogModportMember& existing) {
              return (existing.alias.empty() ? existing.name
                                             : existing.alias)
                  == visible_name;
            });
        if (duplicate) {
          error(
              member,
              "FSIM-SV-SEM-117",
              "duplicate modport member '" + visible_name + "'");
        } else {
          const bool signal_declared = hierarchical_member
              || std::ranges::any_of(
              unit.signals,
              [&](const SignalDeclaration& signal) {
                return signal.name == member.text;
              }) || std::ranges::any_of(
                  unit.ports,
                  [&](const SignalDeclaration& port) {
                    return port.name == member.text;
                  })
              // An unpacked array variable (`logic w[1:N]`).
              || std::ranges::any_of(
                  unit.variables,
                  [&](const VariableDeclaration& variable) {
                    return variable.name == member.text;
                  });
          if (!callable && !signal_declared) {
            error(
                member,
                "FSIM-SV-SEM-118",
                "modport member '" + member.text
                    + "' is not declared by the interface");
          }
          const bool pending_callable = callable
              && function == unit.functions.end()
              && task == unit.tasks.end();
          if (pending_callable) {
            pending_modport_callables_.push_back({
                unit.systemverilog_modports.size(),
                declaration.members.size(),
                member,
                callable_access == CallableAccess::Import,
                explicit_function});
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
              member.text, direction, member.span, kind, alias});
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
  if (language_ == Language::SystemVerilog2017 && keyword("interconnect")) {
    return true;
  }
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
  const auto has_packed_declarator = [&](std::size_t cursor) {
    bool saw_dimension = false;
    while (at(TokenKind::LeftBracket, cursor)) {
      ++cursor;
      std::size_t nested_brackets{};
      std::size_t parentheses{};
      std::size_t braces{};
      std::size_t conditional_depth{};
      bool has_range_separator = false;
      bool closed = false;
      for (; !at(TokenKind::EndOfFile, cursor); ++cursor) {
        const auto kind = current(cursor).kind;
        if (kind == TokenKind::LeftBracket) {
          ++nested_brackets;
        } else if (kind == TokenKind::RightBracket) {
          if (parentheses == 0U && braces == 0U
              && nested_brackets == 0U) {
            closed = true;
            break;
          }
          if (nested_brackets != 0U) {
            --nested_brackets;
          }
        } else if (kind == TokenKind::LeftParen) {
          ++parentheses;
        } else if (kind == TokenKind::RightParen && parentheses != 0U) {
          --parentheses;
        } else if (kind == TokenKind::LeftBrace) {
          ++braces;
        } else if (kind == TokenKind::RightBrace && braces != 0U) {
          --braces;
        } else if (parentheses == 0U && braces == 0U
                   && nested_brackets == 0U) {
          if (kind == TokenKind::Question) {
            ++conditional_depth;
          } else if (kind == TokenKind::Colon) {
            if (conditional_depth != 0U) {
              --conditional_depth;
            } else {
              has_range_separator = true;
            }
          }
        }
      }
      if (!closed || !has_range_separator) {
        return false;
      }
      ++cursor;
      saw_dimension = true;
    }
    return saw_dimension && at(TokenKind::Identifier, cursor);
  };
  const auto has_type_declarator = [&](std::size_t cursor) {
    return at(TokenKind::Identifier, cursor)
        || has_packed_declarator(cursor);
  };
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
    return !selected || has_type_declarator(cursor);
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
      // `P::cfg#(C)::data_t`: a type declared in the specialization
      // (8.25.1).
      while (at(TokenKind::Scope, cursor)
             && at(TokenKind::Identifier, cursor + 1U)) {
        cursor += 2U;
      }
      return has_type_declarator(cursor);
    }
    return has_type_declarator(cursor + 1U);
  }
  return (at(TokenKind::Identifier, offset + 1)
          && !at(TokenKind::LeftParen, offset + 2)
          && !at(TokenKind::Hash, offset + 2))
      || has_packed_declarator(offset + 1U);
}

Type VerilogParser::parse_named_type() {
  const auto source_order = systemverilog_source_order();
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
  // `$unit::t` names a type of this file's compilation-unit package (IEEE
  // 1800-2017 3.12.1).
  if (name.starts_with("$unit::") && compilation_unit_package_) {
    name = compilation_unit_package_->name
        + name.substr(std::string_view { "$unit" }.size());
  }
  // A typedef of an enclosing procedural block (6.18).
  if (const auto local = block_typedef_types_.find(name);
      local != block_typedef_types_.end() && !at(TokenKind::Hash)
      && class_parse_depth_ == 0U) {
    auto type = local->second;
    type.systemverilog_source_order = source_order;
    return type;
  }
  if (name == first.text) {
    note_wildcard_import_type_reference(first);
  }
  Type type{
      ValueDomain::Unknown,
      name,
      std::nullopt,
      false};
  type.named_type = name;
  type.named_type_span = span;
  type.systemverilog_source_order = source_order;
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
    // `C #(...)::t`: a type declared in a class specialization (8.25.1);
    // the actuals stay with the selected name.
    while (at(TokenKind::Scope) && at(TokenKind::Identifier, 1)) {
      (void)advance();
      const auto member = advance();
      type.named_type += "::" + member.text;
      type.spelling = type.named_type;
      type.named_type_span = cover(type.named_type_span, member.span);
    }
  }
  // A typedef of string is a string (IEEE 1800-2017 6.18); the alias name
  // stays the type's identity.
  if (!type.systemverilog_container
      && type.systemverilog_class_parameter_actuals.empty()) {
    const auto known = packed_typedef_types_.find(type.named_type);
    if (known != packed_typedef_types_.end()
        && known->second.domain == ValueDomain::String
        && !known->second.systemverilog_container) {
      type.domain = ValueDomain::String;
      type.is_signed = false;
    }
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
    // An unpacked array of virtual interfaces (IEEE 1800-2017 25.9, 7.4).
    auto variable_type = common_type;
    (void)parse_optional_container_dimension(variable_type);
    if (variable_type.systemverilog_container) {
      unit_container_names_.insert(name.text);
    }
    std::optional<Expression> initializer;
    if (match(TokenKind::Assign)) {
      // An interface instance initializer is resolved, and an unknown one
      // reported, when the virtual interface is bound.
      const auto saved_context = std::exchange(implicit_net_context_, true);
      initializer = parse_expression();
      implicit_net_context_ = saved_context;
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
          std::move(variable_type),
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

[[nodiscard]] bool VerilogParser::is_block_declaration_start() const {
  // A block may declare a virtual interface variable (IEEE 1800-2017 25.9).
  return is_declaration_start()
      || (language_ == Language::SystemVerilog2017 && keyword("virtual")
          && !keyword("class", 1));
}

[[nodiscard]] bool VerilogParser::is_declaration_start() const  {
  if (language_ == Language::SystemVerilog2017
      && (keyword("var") || keyword("const"))) {
    return true;
  }
  return is_direction_keyword() || is_net_type_keyword()
      || (language_ == Language::SystemVerilog2017 && keyword("type")
          && at(TokenKind::LeftParen, 1))
      || keyword("static") || keyword("automatic")
      || keyword("string") || keyword("chandle") || keyword("event")
      || (keyword("process") && !at(TokenKind::Scope, 1))
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
    auto declaration_type = type;
    (void)parse_optional_container_dimension(declaration_type);
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
          declaration_type,
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
  const auto net_kind_spelling = [](const std::string& spelling) {
    return contains_word(
        {"wire", "tri", "tri0", "tri1", "wand", "triand", "wor", "trior",
            "trireg", "uwire", "supply0", "supply1"},
        spelling);
  };
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
        && !(keyword("type") && at(TokenKind::LeftParen, 1))
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
    } else if (auto expanded = parse_named_packed_array_type()) {
      spec.type = std::move(*expanded);
      port_type_explicit = true;
    } else if (is_named_type_reference_start()) {
      spec.type = parse_named_type();
    } else {
      const auto default_spelling = spec.type.spelling;
      parse_optional_net_type(spec.type);
      // A net port of an aggregate, enumeration or user-defined data type
      // (IEEE 1800-2017 6.7.1, 23.2.2.3): `input wire struct_t p;`.
      if (language_ == Language::SystemVerilog2017
          && net_kind_spelling(spec.type.spelling)) {
        const auto net_kind = spec.type.spelling;
        if (keyword("struct") || keyword("union") || keyword("enum")) {
          spec.type = parse_parameter_type();
          spec.type.systemverilog_net_type = net_kind;
        } else if (is_named_type_reference_start()) {
          if (auto named_array = parse_named_packed_array_type()) {
            spec.type = std::move(*named_array);
          } else {
            spec.type = parse_named_type();
          }
          spec.type.systemverilog_net_type = net_kind;
        }
      }
      // `output var b` declares a variable of type logic (IEEE 1800-2017
      // 6.8), not the default net.
      if (explicit_variable && spec.type.spelling == default_spelling
          && !spec.type.packed_range && !spec.type.packed_range_expression) {
        spec.type.spelling = "logic";
        spec.type.domain = ValueDomain::Logic4;
        spec.type.systemverilog_net_type.clear();
      }
    }
    require_default_port_net_type(current(), explicit_type);
  } else if (keyword("string") || keyword("chandle")
             || keyword("process")
             || keyword("struct") || keyword("union") || keyword("enum")) {
    spec.type = parse_parameter_type();
  } else if (auto operator_type = parse_type_operator_type()) {
    // `type(...)` as the data type of a declaration (IEEE 1800-2017 6.23).
    spec.type = std::move(*operator_type);
  } else if (auto expanded = parse_named_packed_array_type()) {
    spec.type = std::move(*expanded);
  } else if (is_named_type_reference_start()) {
    spec.type = parse_named_type();
  } else {
    parse_optional_net_type(spec.type);
    // A net of an aggregate or enumeration data type (IEEE 1800-2017
    // 6.7.1): `wire struct packed {...} w;`.
    if (language_ == Language::SystemVerilog2017
        && net_kind_spelling(spec.type.spelling)
        && (keyword("struct") || keyword("union") || keyword("enum"))) {
      const auto net_kind = spec.type.spelling;
      spec.type = parse_parameter_type();
      spec.type.systemverilog_net_type = net_kind;
    } else if (language_ == Language::SystemVerilog2017
        && net_kind_spelling(spec.type.spelling)
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
  declare_member_enumerations(unit);
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
    unit_foreach_types_.insert_or_assign(name.text, declaration_type);
    if (!declaration_type.systemverilog_container
        && (declaration_type.domain == ValueDomain::String
            || declaration_type.spelling == "string")) {
      unit_string_names_.insert(name.text);
    }
    if (declaration_type.systemverilog_container) {
      unit_container_names_.insert(name.text);
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
            && named != "process"
            // A scoped type may name a package covergroup; one that names a
            // class parsed in this file constructs it.
            && (named.find("::") == std::string::npos
                || known_class_names_.contains(
                    named.substr(named.rfind("::") + 2U)))
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
        // A container initializer that is not an elaboration constant runs
        // at time zero before any procedure (IEEE 1800-2017 6.8); a
        // constant one is applied at elaboration and stored again.
        std::optional<Expression> container_initializer;
        if (initializer && declaration_type.systemverilog_container
            && !package_variable && !systemverilog_const
            && (unit.kind == UnitKind::VerilogModule
                || unit.kind == UnitKind::SystemVerilogProgram
                || unit.kind == UnitKind::SystemVerilogInterface)
            && (initializer->kind == ExpressionKind::Call
                || initializer->kind == ExpressionKind::Identifier
                || initializer->kind == ExpressionKind::Index
                || initializer->kind == ExpressionKind::Slice
                // A pattern for a nested container with a dynamic or queue
                // dimension (`int q[$][$] = '{'{1}}`) is built at time zero
                // like an assignment (IEEE 1800-2017 10.9).
                || (initializer->kind == ExpressionKind::Aggregate
                    && [&] {
                         const auto& outer
                             = *declaration_type.systemverilog_container;
                         if (outer.element_types.size() != 1U
                             || !outer.element_types.front()
                                     .systemverilog_container) {
                           return false;
                         }
                         const auto& inner = *outer.element_types.front()
                                                  .systemverilog_container;
                         return outer.kind
                                 != SystemVerilogContainerKind::StaticArray
                             || inner.kind
                                 != SystemVerilogContainerKind::StaticArray;
                       }()))) {
          container_initializer = *initializer;
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
        if (container_initializer) {
          Statement assignment;
          assignment.kind = StatementKind::Assignment;
          assignment.assignment_kind = AssignmentKind::Blocking;
          assignment.target = Expression {
              ExpressionKind::Identifier, name.text, { }, name.span
          };
          assignment.value = std::move(*container_initializer);
          assignment.span = cover(name.span, assignment.value.span);
          Process process;
          process.kind = ProcessKind::Initial;
          process.name = "$declaration_initializer_" + name.text;
          process.statements.push_back(std::move(assignment));
          process.span = process.statements.front().span;
          unit.processes.push_back(std::move(process));
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
  // A parameter of a block, function or task is a local parameter (IEEE
  // 1800-2017 6.20.1); it is kept as a local initialized by its value. One
  // declared with `parameter` can still be redefined by a defparam (23.10.1).
  const bool overridable_parameter = match_keyword("parameter");
  const bool local_parameter = overridable_parameter || match_keyword("localparam");
  // block_item_declaration ::= [const] [var] [lifetime] data_type_or_implicit
  // (IEEE 1800-2017 A.2.8, 6.8, 6.20.6). A constant variable is never
  // written by legal code, so it is an ordinary initialized variable here.
  bool explicit_variable { };
  bool static_lifetime { };
  bool automatic { };
  for (bool qualified = true; qualified && !local_parameter;) {
    qualified = false;
    if (language_ == Language::SystemVerilog2017 && match_keyword("const")) {
      qualified = true;
    } else if (language_ == Language::SystemVerilog2017
        && match_keyword("var")) {
      explicit_variable = true;
      qualified = true;
    } else if (match_keyword("static")) {
      static_lifetime = true;
      qualified = true;
    } else if (match_keyword("automatic")) {
      automatic = true;
      qualified = true;
    }
  }
  Type type = default_verilog_type();
  if (local_parameter && at(TokenKind::Identifier) && at(TokenKind::Assign, 1)) {
    // An untyped parameter takes an integer value.
    type = Type{ValueDomain::Logic4, "integer", PackedRange{31, 0, true}, true};
  } else if (keyword("string") || keyword("chandle") || keyword("event")
      || keyword("process")
      || keyword("struct") || keyword("union") || keyword("enum")) {
    type = parse_parameter_type();
  } else if (auto expanded = parse_named_packed_array_type()) {
    type = std::move(*expanded);
  } else if (is_named_type_reference_start()) {
    type = parse_named_type();
  } else if (language_ == Language::SystemVerilog2017 && keyword("virtual")) {
    // A virtual interface variable (IEEE 1800-2017 25.9).
    const auto virtual_start = advance();
    type = parse_virtual_interface_type(virtual_start);
  } else {
    parse_optional_net_type(type);
  }
  // An anonymous enumeration declared in a block gets a hidden typedef,
  // so its literal names resolve there (IEEE 1800-2017 6.19).
  if (!type.enumeration_literals.empty() && type.named_type.empty()
      && type.enumeration_literals.size()
          == type.systemverilog_enumeration_values.size()) {
    std::vector<EnumLiteralDeclaration> literals;
    for (std::size_t index = 0; index < type.enumeration_literals.size();
         ++index) {
      literals.push_back({ type.enumeration_literals[index],
          type.systemverilog_enumeration_values[index],
          type.systemverilog_enumeration_values[index].span });
    }
    const auto name = anonymous_enumeration_type_name(literals.front().name);
    if (std::ranges::none_of(block.type_aliases,
            [&](const TypeAliasDeclaration& alias) {
              return alias.name == name;
            })) {
      block.type_aliases.push_back({ name, type, start.span,
          std::move(literals), TypeDeclarationKind::SystemVerilogTypedef,
          { }, { }, { }, false });
    }
  }
  // `var` with an implicit data type declares a logic variable (6.8).
  if (explicit_variable && type.named_type.empty() && type.spelling == "wire") {
    type.spelling = "logic";
  }
  if (local_parameter && type.named_type.empty() && type.spelling == "wire") {
    type.spelling = "logic";
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
  // The literals of an enumeration declared here are declared in this
  // block's scope (IEEE 1800-2017 6.19).
  for (const auto& literal : type.enumeration_literals) {
    current_procedural_names_.insert(literal);
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
    block.declarations.back().systemverilog_static = static_lifetime;
    // A parameter of a task, function or block is a constant of that scope
    // (IEEE 1800-2017 6.20.1), usable in its declarations' ranges.
    block.declarations.back().systemverilog_local_parameter
        = local_parameter;
    block.declarations.back().systemverilog_parameter
        = overridable_parameter;
    block.declarations.back().systemverilog_automatic = automatic
        || (!static_lifetime && unit_default_automatic_
            && !in_task_ && current_function_name_.empty());
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
  } else if (!ranged(port_type) && ranged(data_type)
      && keyword_set_rank(keyword_set_for_standard_revision(standard_revision_))
          < keyword_set_rank(keyword_set_for_standard_revision(
              StandardRevision::SystemVerilog2005))) {
    // Verilog requires identical ranges in both declarations (IEEE
    // 1364-2005 12.3.3).
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
      // The port is signed when either its port or its data declaration
      // says so (IEEE 1800-2017 23.2.2.1).
      const auto port_signed = existing.type.is_signed;
      existing.type = declaration.type;
      existing.type.is_signed = existing.type.is_signed || port_signed;
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
        // A continuous-assignment target declares an implicit net since
        // IEEE 1364-2001 (3.6); in Verilog-1995 only connections do.
        const auto saved_context = std::exchange(implicit_net_context_,
            standard_revision_ != StandardRevision::Verilog1995);
        Expression target = parse_lvalue();
        implicit_net_context_ = saved_context;
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

void VerilogParser::check_verilog_primitive_net_terminal(const Expression& terminal,
    const std::vector<SignalDeclaration>& signals, const Token& instance_start,
    const bool bidirectional, const bool continuous_assignment)
{
  // SystemVerilog lets a variable take a primitive's output, but a
  // bidirectional switch connects nets (IEEE 1800-2017 28.8).
  const bool systemverilog
      = keyword_set_rank(keyword_set_for_standard_revision(standard_revision_))
      >= keyword_set_rank(keyword_set_for_standard_revision(
          StandardRevision::SystemVerilog2005));
  if (systemverilog && !bidirectional) {
    return;
  }
  if (terminal.kind == ExpressionKind::Concatenation) {
    for (const auto& operand : terminal.operands) {
      check_verilog_primitive_net_terminal(operand, signals, instance_start,
          bidirectional, continuous_assignment);
    }
    return;
  }
  const Expression* root = &terminal;
  while ((root->kind == ExpressionKind::Index || root->kind == ExpressionKind::Slice)
      && !root->operands.empty()) {
    root = &root->operands.front();
  }
  if (root->kind != ExpressionKind::Identifier) {
    return;
  }
  const auto signal = std::ranges::find_if(signals, [&](const SignalDeclaration& candidate) {
    return candidate.name == root->text;
  });
  if (signal == signals.end()) {
    return;
  }
  const auto spelling = detail::ascii_lower(signal->type.spelling);
  if (spelling == "reg" || spelling == "integer" || spelling == "time"
      || spelling == "real" || spelling == "realtime"
      || (systemverilog && signal->type.systemverilog_net_type.empty()
          && (spelling == "logic" || spelling == "bit" || spelling == "int"
              || spelling == "byte" || spelling == "shortint"
              || spelling == "longint"))) {
    if (continuous_assignment) {
      // IEEE 1364-2005 6.1: a continuous assignment drives a net.
      error(instance_start, "FSIM-SV-SEM-412",
          "a Verilog continuous assignment target must be a net, not variable '"
              + root->text + "'");
      return;
    }
    error(instance_start, "FSIM-SV-SEM-412",
        "a Verilog primitive output or bidirectional terminal must be a net, "
        "not variable '" + root->text + "'");
  }
}

GenerateRegion VerilogParser::generate_gate_array(const Token& instance_start,
    const std::string& instance_name,
    const std::pair<std::size_t, std::size_t> header,
    const std::array<std::size_t, 4U> bounds,
    const std::vector<const Expression*>& terminals,
    const std::vector<std::pair<std::size_t, std::size_t>>& terminal_tokens,
    const std::size_t outputs,
    const std::vector<SignalDeclaration>& signals,
    const std::vector<SignalDeclaration>& ports) {
  // `bufif1 b[W-1:0] (y, a, e);` with bounds known only at elaboration is
  // the loop generate
  //   for (genvar g = L; L >= R ? g >= R : g <= R; g = L >= R ? g-1 : g+1)
  //   begin : b localparam int o = L >= R ? L - g : g - L; <gate> end
  // whose gate takes bit o from the left of each vector terminal or input
  // expression, operand o of each concatenation, and a scalar whole (IEEE
  // 1800-2017 28.3.6).
  std::vector<Token> out;
  const auto emit = [&](const std::string_view text) {
    auto tokens = synthetic_tokens(text, instance_start.span);
    out.insert(out.end(), tokens.begin(), tokens.end());
  };
  const auto emit_tokens = [&](const std::size_t begin, const std::size_t end) {
    out.insert(out.end(), tokens_.begin() + static_cast<std::ptrdiff_t>(begin),
        tokens_.begin() + static_cast<std::ptrdiff_t>(end));
  };
  const auto emit_bound = [&](const std::size_t which) {
    emit("(");
    emit_tokens(bounds[which * 2U], bounds[which * 2U + 1U]);
    emit(")");
  };
  const auto descending = [&] {
    emit("(");
    emit_bound(0U);
    emit(">=");
    emit_bound(1U);
    emit(")");
  };
  const auto suffix = std::to_string(generated_statement_counter_++);
  const auto variable = "__fsim_ga" + suffix;
  const auto ordinal = "__fsim_gao" + suffix;
  // A concatenation of scalar operands: its operands' token ranges.
  std::vector<std::vector<std::pair<std::size_t, std::size_t>>> operands(
      terminals.size());
  std::optional<std::size_t> count;
  for (std::size_t index = 0; index < terminals.size(); ++index) {
    const auto [begin, end] = terminal_tokens[index];
    if (terminals[index]->kind != ExpressionKind::Concatenation
        || end - begin < 2U || tokens_[begin].kind != TokenKind::LeftBrace
        || tokens_[end - 1U].kind != TokenKind::RightBrace) {
      continue;
    }
    std::size_t depth{};
    auto operand_begin = begin + 1U;
    for (auto position = begin + 1U; position < end - 1U; ++position) {
      const auto kind = tokens_[position].kind;
      if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket
          || kind == TokenKind::LeftBrace) {
        ++depth;
      } else if ((kind == TokenKind::RightParen
                     || kind == TokenKind::RightBracket
                     || kind == TokenKind::RightBrace)
          && depth != 0U) {
        --depth;
      } else if (kind == TokenKind::Comma && depth == 0U) {
        operands[index].emplace_back(operand_begin, position);
        operand_begin = position + 1U;
      }
    }
    operands[index].emplace_back(operand_begin, end - 1U);
    if (count && *count != operands[index].size()) {
      error(instance_start, "FSIM-SV-SEM-114",
          "a gate-array terminal must be scalar or match the instance count");
    }
    count = operands[index].size();
  }
  const auto declaration_of = [&](const Expression& terminal)
      -> const SignalDeclaration* {
    if (terminal.kind != ExpressionKind::Identifier) return nullptr;
    for (const auto* declarations : {&signals, &ports}) {
      const auto found = std::ranges::find(
          *declarations, terminal.text, &SignalDeclaration::name);
      if (found != declarations->end()) return &*found;
    }
    return nullptr;
  };
  const auto emit_gate = [&](const std::size_t arm) {
    emit_tokens(header.first, header.second);
    emit("(");
    for (std::size_t index = 0; index < terminals.size(); ++index) {
      if (index != 0U) emit(",");
      const auto [begin, end] = terminal_tokens[index];
      if (!operands[index].empty()) {
        const auto [operand_begin, operand_end] = operands[index][arm];
        emit_tokens(operand_begin, operand_end);
        continue;
      }
      const auto* declaration = declaration_of(*terminals[index]);
      const auto width = declaration ? declaration->type.width()
                                     : decltype(declaration->type.width()){};
      if (declaration != nullptr && width && *width == 1U) {
        emit_tokens(begin, end);
        continue;
      }
      if (declaration == nullptr && index < outputs) {
        // An output is a scalar net or bit-select.
        emit_tokens(begin, end);
        continue;
      }
      if (declaration == nullptr || (!width && index >= outputs)) {
        // An input expression as wide as the array gives each instance one
        // bit, its leftmost bit to the leftmost instance; one of width 1
        // is shared by every instance.
        emit("1'((");
        emit_tokens(begin, end);
        emit(") >> (($bits(");
        emit_tokens(begin, end);
        emit(") == 1) ? 0 : $bits(");
        emit_tokens(begin, end);
        emit(") - 1 - " + ordinal + "))");
        continue;
      }
      const auto& name = terminals[index]->text;
      emit(name + "[($left(" + name + ") >= $right(" + name + ")) ? $left("
          + name + ") - " + ordinal + " : $left(" + name + ") + " + ordinal
          + "]");
    }
    emit(");");
  };
  emit("( genvar " + variable + " = ");
  emit_bound(0U);
  emit(";");
  descending();
  emit("? " + variable + " >=");
  emit_bound(1U);
  emit(": " + variable + " <=");
  emit_bound(1U);
  emit("; " + variable + " = ");
  descending();
  emit("? " + variable + " - 1 : " + variable + " + 1 ) begin : "
      + instance_name + " localparam int " + ordinal + " = ");
  descending();
  emit("?");
  emit_bound(0U);
  emit("- " + variable + " : " + variable + " - ");
  emit_bound(0U);
  emit(";");
  if (count) {
    emit("case (" + ordinal + ")");
    for (std::size_t arm = 0; arm < *count; ++arm) {
      emit(std::to_string(arm) + ": begin");
      emit_gate(arm);
      emit("end");
    }
    emit("endcase");
  } else {
    emit_gate(0U);
  }
  emit("end");
  Token end;
  end.kind = TokenKind::EndOfFile;
  end.span = instance_start.span;
  out.push_back(std::move(end));
  auto saved_tokens = std::move(tokens_);
  const auto saved_index = index_;
  const auto saved_synthetic_stream = synthetic_token_stream_;
  tokens_ = std::move(out);
  index_ = 0;
  synthetic_token_stream_ = true;
  auto region = parse_iterative_generate(instance_start);
  tokens_ = std::move(saved_tokens);
  index_ = saved_index;
  synthetic_token_stream_ = saved_synthetic_stream;
  return region;
}

void VerilogParser::parse_gate_primitive(
    std::vector<Statement>& statements,
    const std::vector<SignalDeclaration>& signals,
    const std::vector<SignalDeclaration>& ports,
    std::vector<GenerateRegion>* const regions) {
  const auto gate_begin = index_;
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
  // The primitive, drive strength and delay, repeated for each element of
  // a generated gate array.
  const auto header_end = index_;

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
    // The token ranges of bounds that are not literals.
    std::optional<std::array<std::size_t, 4U>> generated_bounds;
    if (match(TokenKind::LeftBracket)) {
      const auto range_start = previous();
      const auto left_begin = index_;
      const auto left = parse_expression();
      const auto left_end = index_;
      expect(
          TokenKind::Colon,
          "':' in gate-instance array range",
          "FSIM-SV-PARSE-210");
      const auto right_begin = index_;
      const auto right = parse_expression();
      const auto right_end = index_;
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
      // A net, variable or port in a bound, other than as the argument of
      // a system function such as $left, is not constant (IEEE 1800-2017
      // 28.3.5).
      const auto names_object =
          [&](auto& self, const Expression& expression) -> bool {
            if (expression.kind == ExpressionKind::Call
                && expression.text.starts_with('$')) {
              return false;
            }
            if (expression.kind == ExpressionKind::Identifier) {
              return std::ranges::find(signals, expression.text,
                         &SignalDeclaration::name) != signals.end()
                  || std::ranges::find(ports, expression.text,
                         &SignalDeclaration::name) != ports.end();
            }
            return std::ranges::any_of(expression.operands,
                [&](const Expression& operand) {
                  return self(self, operand);
                });
          };
      if (instance_name.empty()) {
        error(
            range_start,
            "FSIM-SV-SEM-111",
            "a gate-instance array requires an instance name");
      } else if ((!left_value || !right_value)
          && (regions == nullptr || names_object(names_object, left)
              || names_object(names_object, right))) {
        error(
            range_start,
            "FSIM-SV-SEM-112",
            "gate-instance array bounds must be constant expressions");
      } else if (!left_value || !right_value) {
        generated_bounds = std::array<std::size_t, 4U> {
            left_begin, left_end, right_begin, right_end };
      } else {
        const auto distance
            = support::index_distance(*left_value, *right_value);
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
    const auto saved_context = std::exchange(implicit_net_context_, true);
    std::vector<std::pair<std::size_t, std::size_t>> terminal_tokens;
    const auto target_begin = index_;
    auto target = parse_lvalue();
    terminal_tokens.emplace_back(target_begin, index_);
    std::vector<Expression> inputs;
    while (match(TokenKind::Comma)) {
      const auto input_begin = index_;
      inputs.push_back(parse_expression());
      terminal_tokens.emplace_back(input_begin, index_);
    }
    implicit_net_context_ = saved_context;
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

    check_verilog_primitive_net_terminal(target, signals, instance_start);
    if (unary) {
      for (std::size_t index = 0; index + 1 < inputs.size(); ++index) {
        check_verilog_primitive_net_terminal(inputs[index], signals, instance_start);
      }
    }
    if (generated_bounds) {
      std::vector<const Expression*> terminals{&target};
      for (const auto& input : inputs) terminals.push_back(&input);
      regions->push_back(generate_gate_array(instance_start, instance_name,
          {gate_begin, header_end}, *generated_bounds, terminals,
          terminal_tokens, unary ? terminals.size() - 1U : 1U, signals,
          ports));
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
          // A concatenation of one operand per instance gives each
          // instance its operand, the leftmost to the leftmost instance
          // (IEEE 1800-2017 28.3.6).
          if (terminal.kind == ExpressionKind::Concatenation
              && instance_indices.size() > 1U
              && terminal.operands.size() == instance_indices.size()) {
            return std::move(terminal.operands[ordinal]);
          }
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
              (terminal.kind == ExpressionKind::LogicLiteral
                  && terminal.text.starts_with("1'"))
              || terminal.kind == ExpressionKind::Index) {
            // A bit-select is a scalar terminal shared by every instance.
            return terminal;
          } else {
            // A sized literal or a constant part-select has a known width,
            // which must be 1 or the instance count (IEEE 1364-2005 7.1.6).
            std::optional<std::uint64_t> width;
            if (terminal.kind == ExpressionKind::LogicLiteral) {
              if (const auto quote = terminal.text.find('\'');
                  quote != std::string::npos && quote != 0U) {
                if (const auto size = detail::decimal_i64(terminal.text.substr(0U, quote))) {
                  width = static_cast<std::uint64_t>(*size);
                }
              }
            } else if (terminal.kind == ExpressionKind::Slice
                && terminal.operands.size() == 3U
                && terminal.operands[1].kind == ExpressionKind::IntegerLiteral
                && terminal.operands[2].kind == ExpressionKind::IntegerLiteral) {
              const auto left = detail::decimal_i64(terminal.operands[1].text);
              const auto right = detail::decimal_i64(terminal.operands[2].text);
              if (left && right) {
                width = static_cast<std::uint64_t>(
                    (*left > *right ? *left - *right : *right - *left) + 1);
              }
            }
            if (width && *width != 1U && *width != instance_indices.size()) {
              error(
                  instance_start,
                  "FSIM-SV-SEM-114",
                  "a gate-array terminal must be scalar or match the "
                  "instance count");
              return terminal;
            }
            // The leftmost instance takes the leftmost bit of a constant
            // part-select (IEEE 1364-2005 7.1.6).
            if (width && terminal.kind == ExpressionKind::Slice
                && terminal.text != "+:" && terminal.text != "-:") {
              if (*width == 1U) {
                return terminal;
              }
              const auto left = *detail::decimal_i64(terminal.operands[1].text);
              const auto right = *detail::decimal_i64(terminal.operands[2].text);
              const auto offset = static_cast<std::int64_t>(ordinal);
              Expression bit{
                  ExpressionKind::IntegerLiteral,
                  std::to_string(left > right ? left - offset : left + offset),
                  {},
                  terminal.span};
              const auto span = terminal.span;
              return Expression{
                  ExpressionKind::Index,
                  "",
                  {std::move(terminal.operands[0]), std::move(bit)},
                  span};
            }
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
      std::optional<std::pair<Expression, Expression>> tristate_drivers;
      if (unary) {
        const auto combined = cover(start.span, value.span);
        if (operation == "not") {
          value = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(value)},
              combined};
        } else {
          // A buffer drives x for a z input (IEEE 1800-2017 28.5).
          value = Expression{
              ExpressionKind::Binary,
              "|",
              {std::move(value),
               Expression{ExpressionKind::LogicLiteral, "1'b0", {},
                   combined}},
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
        } else {
          // A buffer drives x for a z input (IEEE 1800-2017 28.6).
          const auto buffered_span = cover(start.span, value.span);
          value = Expression{
              ExpressionKind::Binary,
              "|",
              {std::move(value),
               Expression{ExpressionKind::LogicLiteral, "1'b0", {},
                   buffered_span}},
              buffered_span};
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
        const auto gate_span = cover(start.span, previous().span);
        auto data = value;
        value = Expression{
            ExpressionKind::Call,
            "?:",
            {std::move(control), std::move(value), high_impedance},
            gate_span};
        // Without a delay, the gate is two drivers so that an unknown
        // control drives L or H, a strength range on one side (IEEE
        // 1800-2017 28.6, Table 28-7): one pulls only to 0, the other only
        // to 1. A delayed gate keeps one driver.
        if (!delay) {
          const auto split = [&](const char* blocked) {
            Expression level{
                ExpressionKind::LogicLiteral, blocked, {}, gate_span};
            Expression test{ExpressionKind::Binary, "===",
                {data, std::move(level)}, gate_span};
            return Expression{ExpressionKind::Call, "?:",
                {std::move(test), high_impedance, value}, gate_span};
          };
          tristate_drivers = {split("1'b1"), split("1'b0")};
        }
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
        if (tristate_drivers) {
          const auto declared = strength.value_or(VerilogDriveStrength{
              VerilogStrength::Strong, VerilogStrength::Strong,
              statement.span});
          auto high = statement;
          statement.value = tristate_drivers->first;
          statement.verilog_drive_strength = VerilogDriveStrength{
              declared.zero, VerilogStrength::HighZ, declared.span};
          high.value = tristate_drivers->second;
          high.verilog_drive_strength = VerilogDriveStrength{
              VerilogStrength::HighZ, declared.one, declared.span};
          if (!high.label.empty()) {
            high.label += "$one";
          }
          statements.push_back(std::move(statement));
          statements.push_back(std::move(high));
          continue;
        }
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
  std::optional<Expression> process_event_guard;
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
    process_event_guard = std::exchange(event_guard_, std::nullopt);
  } else if (process.kind != ProcessKind::VerilogAlways) {
    error(current(), "FSIM-SV-PARSE-012",
          "always process requires an event control in this frontend "
          "slice");
  }
  auto body = parse_statement();
  if (body) {
    if (body->kind == StatementKind::Block
        && body->label.empty()
        && body->systemverilog_imports.empty()
        && body->type_aliases.empty()) {
      process.variables = std::move(body->declarations);
      process.statements = std::move(body->statements);
    } else {
      process.statements.push_back(std::move(*body));
    }
  }
  // `always @(e iff g) S` runs S at the occurrences of e at which g holds
  // (IEEE 1800-2017 9.4.2.3).
  if (process_event_guard) {
    Statement guarded;
    guarded.kind = StatementKind::If;
    guarded.condition = std::move(*process_event_guard);
    guarded.statements = std::move(process.statements);
    guarded.span = start.span;
    process.statements.clear();
    process.statements.push_back(std::move(guarded));
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
