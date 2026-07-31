// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

DelayAlternative VerilogParser::parse_verilog_delay_alternative() {
  DelayAlternative alternative;
  const auto magnitude =
      expect(TokenKind::Number, "delay magnitude", "FSIM-SV-PARSE-023");
  if (const auto parsed = decimal_ratio(magnitude.text)) {
    alternative.magnitude = parsed->numerator;
    alternative.divisor = parsed->denominator;
    if (at(TokenKind::Identifier)
        && time_unit_femtoseconds(current().text)) {
      const auto explicit_unit = advance();
      alternative.unit = explicit_unit.text;
      if (language_ != Language::SystemVerilog2017) {
        error(
            explicit_unit,
            "FSIM-SV-SEM-051",
            "an explicitly unit-suffixed delay literal requires "
            "SystemVerilog-2017");
      }
    } else if (!module_time_unit_.empty()) {
      if (alternative.magnitude
          > std::numeric_limits<std::uint64_t>::max()
              / module_time_unit_magnitude_) {
        error(
            magnitude,
            "FSIM-SV-SEM-010",
            "delay magnitude overflows after applying "
            "SystemVerilog timeunit");
      } else {
        alternative.magnitude *= module_time_unit_magnitude_;
        alternative.unit = module_time_unit_;
      }
    } else if (alternative.divisor != 1) {
      error(
          magnitude,
          "FSIM-SV-SEM-050",
          "a fractional delay requires an explicit time unit or an "
          "active `timescale/timeunit");
    }
  } else {
    const bool nondecimal =
        magnitude.text.find('\'') != std::string::npos;
    error(
        magnitude,
        nondecimal ? "FSIM-SV-SEM-002" : "FSIM-SV-SEM-049",
        nondecimal
            ? "delay magnitude must be a decimal literal"
            : "delay magnitude must be a representable nonnegative "
              "decimal literal");
  }
  alternative.span = span_from(magnitude, previous());
  return alternative;
}

void VerilogParser::set_selected_delay(
  Delay& delay,
  const DelayAlternative& alternative) {
  delay.magnitude = alternative.magnitude;
  delay.divisor = alternative.divisor;
  delay.unit = alternative.unit;
}

Delay VerilogParser::parse_verilog_delay_value(const bool parenthesized) {
  Delay delay;
  auto first = parse_verilog_delay_alternative();
  set_selected_delay(delay, first);
  if (match(TokenKind::Colon)) {
    if (!parenthesized) {
      error(
          previous(),
          "FSIM-SV-SEM-052",
          "a min:typ:max delay triple must be parenthesized");
    }
    auto typical = parse_verilog_delay_alternative();
    expect(
        TokenKind::Colon,
        "second ':' in min:typ:max delay",
        "FSIM-SV-PARSE-134");
    auto maximum = parse_verilog_delay_alternative();
    delay.minimum = std::move(first);
    delay.typical = std::move(typical);
    delay.maximum = std::move(maximum);
    set_selected_delay(delay, *delay.typical);
  }
  delay.span = cover(first.span, previous().span);
  return delay;
}

Delay VerilogParser::parse_verilog_delay(
    const Token& start,
    const std::size_t maximum_values) {
  const bool parenthesized = match(TokenKind::LeftParen);
  auto delay = parse_verilog_delay_value(parenthesized);
  if (parenthesized) {
    while (match(TokenKind::Comma)) {
      const auto comma = previous();
      if (at(TokenKind::RightParen)) {
        error(
            comma,
            "FSIM-SV-PARSE-135",
            "expected a delay value after ','");
        break;
      }
      const auto value_start = current();
      delay.additional_values.push_back(
          parse_verilog_delay_value(true));
      if (delay.additional_values.size() + 1 > maximum_values) {
        error(
            value_start,
            "FSIM-SV-SEM-053",
            "this delay control accepts at most "
                + std::to_string(maximum_values)
                + " transition delay value"
                + (maximum_values == 1 ? "" : "s"));
      }
    }
    expect(TokenKind::RightParen, "')' after delay",
           "FSIM-SV-PARSE-024");
  }
  delay.span = span_from(start, previous());
  return delay;
}

Expression VerilogParser::parse_lvalue() {
  const auto name = expect_identifier("assignment target");
  Expression expression{ExpressionKind::Identifier, name.text, {},
                        name.span};
  for (;;) {
    if (match(TokenKind::Dot)) {
      const auto selected = expect_identifier("selected name");
      expression.text += '.';
      expression.text += selected.text;
      expression.span = cover(expression.span, selected.span);
    } else if (match(TokenKind::LeftBracket)) {
      Expression first = parse_expression();
      if (at(TokenKind::PlusColon)
          || at(TokenKind::MinusColon)) {
        const auto direction = advance();
        Expression width = parse_expression();
        expect(TokenKind::RightBracket,
               "']' after indexed part-select",
               "FSIM-SV-PARSE-025");
        expression =
            Expression{ExpressionKind::Slice, direction.text,
                       {std::move(expression), std::move(first),
                        std::move(width)},
                       cover(expression.span, previous().span)};
      } else if (match(TokenKind::Colon)) {
        Expression second = parse_expression();
        expect(TokenKind::RightBracket, "']' after part-select",
               "FSIM-SV-PARSE-025");
        expression =
            Expression{ExpressionKind::Slice, ":",
                       {std::move(expression), std::move(first),
                        std::move(second)},
                       cover(expression.span, previous().span)};
      } else {
        expect(TokenKind::RightBracket, "']' after index",
               "FSIM-SV-PARSE-026");
        expression =
            Expression{ExpressionKind::Index, "index",
                       {std::move(expression), std::move(first)},
                       cover(expression.span, previous().span)};
      }
    } else {
      break;
    }
  }
  const Expression* root = &expression;
  while ((root->kind == ExpressionKind::Index
          || root->kind == ExpressionKind::Slice)
         && !root->operands.empty()) {
    root = &root->operands.front();
  }
  if (root->kind == ExpressionKind::Identifier
      && root->text == name.text) {
    note_implicit_net_reference(name);
  }
  return expression;
}

std::optional<VerilogParser::BinaryOperation> VerilogParser::binary_operation() const  {
  if (at(TokenKind::OrOr)) {
    return BinaryOperation{1, "||"};
  }
  if (at(TokenKind::AndAnd)) {
    return BinaryOperation{2, "&&"};
  }
  if (at(TokenKind::Pipe)) {
    return BinaryOperation{3, "|"};
  }
  if (at(TokenKind::Caret) || at(TokenKind::TildeCaret)
      || at(TokenKind::CaretTilde)) {
    return BinaryOperation{4, current().text};
  }
  if (at(TokenKind::Ampersand)) {
    return BinaryOperation{5, "&"};
  }
  if (at(TokenKind::EqualEqual) || at(TokenKind::CaseEqual)
      || at(TokenKind::WildcardEqual)
      || at(TokenKind::CaseNotEqual)
      || at(TokenKind::WildcardNotEqual)
      || (at(TokenKind::NotEqual) && current().text == "!=")) {
    return BinaryOperation{6, current().text};
  }
  if (at(TokenKind::Less) || at(TokenKind::LessEqual) ||
      at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
    return BinaryOperation{7, current().text};
  }
  if (at(TokenKind::ShiftLeft)
      || at(TokenKind::ShiftRight)
      || at(TokenKind::ArithmeticShiftLeft)
      || at(TokenKind::ArithmeticShiftRight)) {
    return BinaryOperation{8, current().text};
  }
  if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
    return BinaryOperation{9, current().text};
  }
  if (at(TokenKind::Star) || at(TokenKind::Slash) ||
      at(TokenKind::Percent)) {
    return BinaryOperation{10, current().text};
  }
  if (at(TokenKind::Power)) {
    return BinaryOperation{11, "**"};
  }
  return std::nullopt;
}

Expression VerilogParser::parse_expression(int minimum_precedence) {
  Expression left = parse_unary();
  for (;;) {
    const auto operation = binary_operation();
    if (!operation || operation->precedence < minimum_precedence) {
      break;
    }
    if (language_ != Language::SystemVerilog2017
        && (at(TokenKind::WildcardEqual)
            || at(TokenKind::WildcardNotEqual))) {
      error(
          current(),
          "FSIM-VERILOG-SEM-004",
          "wildcard equality operators require SystemVerilog");
    }
    advance();
    Expression right = parse_expression(operation->precedence + 1);
    const auto combined = cover(left.span, right.span);
    left = Expression{ExpressionKind::Binary, operation->name,
                      {std::move(left), std::move(right)}, combined};
  }
  if (minimum_precedence == 0 && match(TokenKind::Question)) {
    Expression when_true = parse_expression();
    expect(TokenKind::Colon, "':' in conditional expression",
           "FSIM-SV-PARSE-027");
    Expression when_false = parse_expression();
    const auto combined = cover(left.span, when_false.span);
    left = Expression{ExpressionKind::Call, "?:",
                      {std::move(left), std::move(when_true),
                       std::move(when_false)},
                      combined};
  }
  return left;
}

Expression VerilogParser::parse_unary() {
  if (at(TokenKind::Plus) || at(TokenKind::Minus) ||
      at(TokenKind::Bang) || at(TokenKind::Tilde) ||
      at(TokenKind::Ampersand) || at(TokenKind::Pipe) ||
      at(TokenKind::Caret) || at(TokenKind::TildeAmpersand) ||
      at(TokenKind::TildePipe) || at(TokenKind::TildeCaret) ||
      at(TokenKind::CaretTilde)) {
    const auto operation = advance();
    Expression operand = parse_unary();
    return Expression{ExpressionKind::Unary, operation.text,
                      {std::move(operand)},
                      cover(operation.span, operand.span)};
  }
  return parse_primary();
}

Expression VerilogParser::parse_primary() {
  if (match(TokenKind::Apostrophe)) {
    const auto apostrophe = previous();
    if (language_ != Language::SystemVerilog2017) {
      error(
          apostrophe,
          "FSIM-SV-SEM-084",
          "assignment patterns require SystemVerilog-2017");
    }
    expect(
        TokenKind::LeftBrace,
        "'{' after assignment-pattern apostrophe",
        "FSIM-SV-PARSE-163");
    Expression pattern{
        ExpressionKind::Aggregate,
        "sv-pattern",
        {},
        apostrophe.span};
    while (!at(TokenKind::RightBrace)
           && !at(TokenKind::EndOfFile)) {
      const bool default_choice =
          current().text == "default";
      auto first =
          default_choice
              ? Expression{
                    ExpressionKind::Identifier,
                    advance().text,
                    {},
                    previous().span}
              : parse_expression();
      if (match(TokenKind::Colon)) {
        pattern.aggregate_choices.push_back(
            default_choice ? "default" : "@key");
        pattern.aggregate_choice_expressions.push_back(
            default_choice
                ? std::vector<Expression>{}
                : std::vector<Expression>{std::move(first)});
        pattern.operands.push_back(parse_expression());
      } else {
        pattern.aggregate_choices.emplace_back();
        pattern.aggregate_choice_expressions.emplace_back();
        pattern.operands.push_back(std::move(first));
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::RightBrace,
        "'}' after assignment pattern",
        "FSIM-SV-PARSE-164");
    pattern.span = span_from(apostrophe, previous());
    return pattern;
  }
  if (at(TokenKind::Number)) {
    const auto token = advance();
    const auto kind = token.text.find('\'') == std::string::npos
                          ? ExpressionKind::IntegerLiteral
                          : ExpressionKind::LogicLiteral;
    return Expression{kind, token.text, {}, token.span};
  }
  if (at(TokenKind::StringLiteral)) {
    const auto token = advance();
    auto expression = Expression{
        ExpressionKind::StringLiteral, token.text, {}, token.span};
    expression.decoded_string =
        decoded_string_literal_text(token);
    return expression;
  }
  if (at(TokenKind::Identifier)) {
    const auto name = advance();
    std::string canonical = name.text;
    while (match(TokenKind::Scope)) {
      canonical += "::";
      canonical +=
          expect_identifier("package-scoped name").text;
    }
    Expression expression{
        ExpressionKind::Identifier,
        canonical,
        {},
        cover(name.span, previous().span)};
    if (match(TokenKind::LeftParen)) {
      std::vector<Expression> arguments;
      if (!at(TokenKind::RightParen)) {
        do {
          arguments.push_back(parse_expression());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RightParen, "')' after arguments",
             "FSIM-SV-PARSE-028");
      expression = Expression{ExpressionKind::Call, canonical,
                              std::move(arguments),
                              cover(name.span, previous().span)};
      const auto require_file_call =
          [&](const std::size_t arity,
              const std::string_view description) {
            if (language_ != Language::SystemVerilog2017) {
              error(
                  name,
                  "FSIM-SV-SEM-074",
                  canonical + " requires SystemVerilog-2017");
            }
            if (expression.operands.size() != arity) {
              error(
                  name,
                  "FSIM-SV-SEM-075",
                  canonical + " requires " + std::string{description});
            }
          };
      if (canonical == "$fopen") {
        require_file_call(2, "a filename and text mode");
      } else if (canonical == "$fgets") {
        require_file_call(2, "a string target and file handle");
      } else if (canonical == "$feof") {
        require_file_call(1, "one file handle");
      } else if (canonical == "$ferror") {
        require_file_call(2, "a file handle and string target");
      }
      return parse_postfix(std::move(expression));
    }
    if (canonical == "$urandom" || canonical == "$random") {
      expression.kind = ExpressionKind::Call;
      return parse_postfix(std::move(expression));
    }
    expression = parse_postfix(std::move(expression));
    const Expression* root = &expression;
    while ((root->kind == ExpressionKind::Index
            || root->kind == ExpressionKind::Slice)
           && !root->operands.empty()) {
      root = &root->operands.front();
    }
    if (root->kind == ExpressionKind::Identifier
        && root->text == name.text) {
      note_implicit_net_reference(name);
    }
    return expression;
  }
  if (match(TokenKind::LeftParen)) {
    const auto open = previous();
    Expression expression = parse_expression();
    expect(TokenKind::RightParen, "')' after expression",
           "FSIM-SV-PARSE-029");
    expression.span = span_from(open, previous());
    return expression;
  }
  if (match(TokenKind::LeftBrace)) {
    const auto open = previous();
    std::vector<Expression> elements;
    if (!at(TokenKind::RightBrace)) {
      Expression first = parse_expression();
      if (match(TokenKind::LeftBrace)) {
        elements.push_back(std::move(first));
        if (at(TokenKind::RightBrace)) {
          error(
              current(),
              "FSIM-SV-PARSE-090",
              "replication concatenations require at least one operand");
        } else {
          do {
            elements.push_back(parse_expression());
          } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightBrace,
               "'}' after replication operands",
               "FSIM-SV-PARSE-030");
        expect(TokenKind::RightBrace,
               "'}' after replication concatenation",
               "FSIM-SV-PARSE-030");
        return Expression{
            ExpressionKind::Replication,
            "replicate",
            std::move(elements),
            span_from(open, previous())};
      }
      elements.push_back(std::move(first));
      while (match(TokenKind::Comma)) {
        elements.push_back(parse_expression());
      }
    }
    expect(TokenKind::RightBrace, "'}' after concatenation",
           "FSIM-SV-PARSE-030");
    return Expression{ExpressionKind::Concatenation, "concat",
                      std::move(elements), span_from(open, previous())};
  }
  const auto invalid = advance();
  error(invalid, "FSIM-SV-PARSE-031", "expected expression");
  return Expression{ExpressionKind::Invalid, invalid.text, {},
                    invalid.span};
}

Expression VerilogParser::parse_postfix(Expression expression) {
  for (;;) {
    if (match(TokenKind::Dot)) {
      auto member = current();
      if (member.kind == TokenKind::Identifier
          && (member.text == "and"
              || member.text == "or"
              || member.text == "xor"
              || member.text == "unique")) {
        advance();
      } else {
        member = expect_identifier("member name");
      }
      if (match(TokenKind::LeftParen)) {
        const auto receiver_span = expression.span;
        const bool predicate_locator_method =
            member.text == "find"
            || member.text == "find_index"
            || member.text == "find_first"
            || member.text == "find_first_index"
            || member.text == "find_last"
            || member.text == "find_last_index";
        const auto implicit_reference_count =
            implicit_net_references_.size();
        std::vector<Expression> operands;
        operands.push_back(std::move(expression));
        if (!at(TokenKind::RightParen)) {
          do {
            operands.push_back(parse_expression());
          } while (match(TokenKind::Comma));
        }
        expect(
            TokenKind::RightParen,
            "')' after method arguments",
            "FSIM-SV-PARSE-028");
        const auto argument_count = operands.size() - 1U;
        const bool reduction_method =
            member.text == "sum"
            || member.text == "product"
            || member.text == "and"
            || member.text == "or"
            || member.text == "xor";
        const bool ordering_method =
            member.text == "reverse"
            || member.text == "sort"
            || member.text == "rsort";
        const bool unsupported_shuffle =
            member.text == "shuffle";
        const bool locator_method =
            member.text == "min"
            || member.text == "max"
            || member.text == "unique"
            || member.text == "unique_index";
        const bool valid_iterator_argument =
            predicate_locator_method
            && argument_count == 1
            && operands[1].kind == ExpressionKind::Identifier
            && operands[1].text.find('.') == std::string::npos;
        if (valid_iterator_argument) {
          implicit_net_references_.resize(
              implicit_reference_count);
          locator_iterator_names_.insert(
              operands[1].text);
        }
        const auto expected_arguments =
            member.text == "push_front"
                    || member.text == "push_back"
                    || member.text == "exists"
                    || member.text == "first"
                    || member.text == "last"
                    || member.text == "next"
                    || member.text == "prev"
                ? std::optional<std::size_t>{1}
            : member.text == "size"
                    || member.text == "pop_front"
                    || member.text == "pop_back"
                    || reduction_method
                    || ordering_method
                    || unsupported_shuffle
                    || locator_method
                ? std::optional<std::size_t>{0}
            : member.text == "delete"
                ? (argument_count <= 1
                       ? std::optional<std::size_t>{argument_count}
                       : std::optional<std::size_t>{1})
                : std::nullopt;
        if (expected_arguments
            && argument_count != *expected_arguments) {
          error(
              member,
              "FSIM-SV-SEM-081",
              "container method '" + member.text + "' requires "
                  + std::to_string(*expected_arguments)
                  + " argument(s)");
        }
        if (predicate_locator_method
            && (argument_count > 1
                || (argument_count == 1
                    && operands[1].kind
                        != ExpressionKind::Identifier)
                || (argument_count == 1
                    && operands[1].text.find('.')
                        != std::string::npos))) {
          error(
              member,
              "FSIM-SV-SEM-090",
              "predicate container locator method '" + member.text
                  + "' accepts at most one iterator identifier");
        }
        if (reduction_method
            && language_ != Language::SystemVerilog2017) {
          error(
              member,
              "FSIM-SV-SEM-085",
              "container reduction methods require SystemVerilog 2017");
        }
        if (reduction_method
            && current().text == "with") {
          error(
              current(),
              "FSIM-SV-UNSUPPORTED-039",
              "container reduction with-clauses are not supported");
        }
        if ((ordering_method || unsupported_shuffle)
            && language_ != Language::SystemVerilog2017) {
          error(
              member,
              "FSIM-SV-SEM-086",
              "container ordering methods require SystemVerilog 2017");
        }
        if ((ordering_method || unsupported_shuffle)
            && current().text == "with") {
          error(
              current(),
              "FSIM-SV-UNSUPPORTED-041",
              "container ordering with-clauses are not supported");
        }
        if (locator_method
            && language_ != Language::SystemVerilog2017) {
          error(
              member,
              "FSIM-SV-SEM-087",
              "container locator methods require SystemVerilog 2017");
        }
        if (locator_method && current().text == "with") {
          error(
              current(),
              "FSIM-SV-UNSUPPORTED-042",
              "container locator with-clauses are not supported");
        }
        if (predicate_locator_method
            && language_ != Language::SystemVerilog2017) {
          error(
              member,
              "FSIM-SV-SEM-088",
              "predicate container locator methods require "
              "SystemVerilog 2017");
        }
        if (predicate_locator_method) {
          bool iterator_scope_inserted = false;
          if (valid_iterator_argument) {
            iterator_scope_inserted =
                current_procedural_names_.insert(
                    operands[1].text).second;
          }
          if (current().text != "with") {
            error(
                current(),
                "FSIM-SV-SEM-089",
                "predicate container locator method '" + member.text
                    + "' requires a with-clause");
          } else {
            advance();
            expect(
                TokenKind::LeftParen,
                "'(' after container locator with",
                "FSIM-SV-PARSE-165");
            if (at(TokenKind::RightParen)) {
              error(
                  current(),
                  "FSIM-SV-SEM-089",
                  "a container locator with-clause requires a predicate");
            } else {
              operands.push_back(parse_expression());
            }
            expect(
                TokenKind::RightParen,
                "')' after container locator predicate",
                "FSIM-SV-PARSE-166");
          }
          if (iterator_scope_inserted) {
            current_procedural_names_.erase(
                operands[1].text);
          }
        }
        expression = Expression{
            ExpressionKind::Call,
            "." + member.text,
            std::move(operands),
            cover(receiver_span, previous().span)};
        continue;
      }
      expression.text += '.';
      expression.text += member.text;
      expression.span = cover(expression.span, member.span);
    } else if (match(TokenKind::LeftBracket)) {
      Expression first = parse_expression();
      if (at(TokenKind::PlusColon)
          || at(TokenKind::MinusColon)) {
        const auto direction = advance();
        Expression width = parse_expression();
        expect(TokenKind::RightBracket,
               "']' after indexed part-select",
               "FSIM-SV-PARSE-032");
        expression =
            Expression{ExpressionKind::Slice, direction.text,
                       {std::move(expression), std::move(first),
                        std::move(width)},
                       cover(expression.span, previous().span)};
      } else if (match(TokenKind::Colon)) {
        Expression second = parse_expression();
        expect(TokenKind::RightBracket, "']' after part-select",
               "FSIM-SV-PARSE-032");
        expression =
            Expression{ExpressionKind::Slice, ":",
                       {std::move(expression), std::move(first),
                        std::move(second)},
                       cover(expression.span, previous().span)};
      } else {
        expect(TokenKind::RightBracket, "']' after index",
               "FSIM-SV-PARSE-033");
        expression =
            Expression{ExpressionKind::Index, "index",
                       {std::move(expression), std::move(first)},
                       cover(expression.span, previous().span)};
      }
    } else {
      break;
    }
  }
  return expression;
}

}  // namespace fsim::frontend
