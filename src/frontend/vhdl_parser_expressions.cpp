// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

Expression VhdlParser::parse_expression(int minimum_precedence) {
  Expression left = parse_unary();
  for (;;) {
    const auto operation = binary_operation();
    if (!operation || operation->precedence < minimum_precedence) {
      break;
    }
    const auto operator_token = advance();
    for (std::size_t index = 1; index < operation->token_count; ++index) {
      advance();
    }
    if (operation->name == "**"
        && (at(TokenKind::Plus) || at(TokenKind::Minus))) {
      error(
          current(),
          "FSIM-VHDL-PARSE-114",
          "a signed VHDL exponent must be parenthesized");
    }
    Expression right = parse_expression(operation->precedence + 1);
    const auto combined_span = cover(left.span, right.span);
    left = Expression{ExpressionKind::Binary, operation->name,
                      {std::move(left), std::move(right)}, combined_span};
    (void)operator_token;
    if (operation->name == "**" && at(TokenKind::Power)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-114",
          "chained VHDL exponentiation requires parentheses");
    }
  }
  return left;
}

std::optional<VhdlParser::BinaryOperation> VhdlParser::binary_operation() const  {
  if (keyword("or", 0, true) || keyword("nor", 0, true) ||
      keyword("xor", 0, true) || keyword("xnor", 0, true)) {
    return BinaryOperation{1, detail::ascii_lower(current().text)};
  }
  if (keyword("and", 0, true) || keyword("nand", 0, true)) {
    return BinaryOperation{2, detail::ascii_lower(current().text)};
  }
  if (at(TokenKind::Assign)
      || (at(TokenKind::NotEqual) && current().text == "/=")
      || at(TokenKind::Less) || at(TokenKind::LessEqual) ||
      at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
    return BinaryOperation{3, current().text};
  }
  if (at(TokenKind::Question)
      && (at(TokenKind::Assign, 1)
          || (at(TokenKind::NotEqual, 1)
              && current(1).text == "/="))) {
    return BinaryOperation{
        3, at(TokenKind::Assign, 1) ? "?=" : "?/=", 2};
  }
  if (keyword("sll", 0, true) || keyword("srl", 0, true)
      || keyword("sla", 0, true) || keyword("sra", 0, true)
      || keyword("rol", 0, true) || keyword("ror", 0, true)) {
    return BinaryOperation{
        4, detail::ascii_lower(current().text)};
  }
  if (at(TokenKind::Ampersand) || at(TokenKind::Plus) ||
      at(TokenKind::Minus)) {
    return BinaryOperation{5, current().text};
  }
  if (at(TokenKind::Star) || at(TokenKind::Slash) ||
      keyword("mod", 0, true) || keyword("rem", 0, true)) {
    return BinaryOperation{6, detail::ascii_lower(current().text)};
  }
  if (at(TokenKind::Power)) {
    return BinaryOperation{7, "**"};
  }
  return std::nullopt;
}

Expression VhdlParser::parse_unary() {
  if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
    const auto operation = advance();
    Expression operand = parse_expression(6);
    return Expression{ExpressionKind::Unary,
                      detail::ascii_lower(operation.text),
                      {std::move(operand)},
                      cover(operation.span, operand.span)};
  }
  if (keyword("not", 0, true) || keyword("abs", 0, true)) {
    const auto operation = advance();
    Expression operand = parse_unary();
    return Expression{ExpressionKind::Unary,
                      detail::ascii_lower(operation.text),
                      {std::move(operand)},
                      cover(operation.span, operand.span)};
  }
  return parse_primary();
}

Expression VhdlParser::parse_primary() {
  if (keyword("true", 0, true) || keyword("false", 0, true)) {
    const auto token = advance();
    return Expression{
        ExpressionKind::BooleanLiteral,
        vhdl_name(token.text),
        {},
        token.span};
  }
  if (at(TokenKind::Number)) {
    const auto token = advance();
    return Expression{ExpressionKind::IntegerLiteral, token.text, {},
                      token.span};
  }
  if (at(TokenKind::CharacterLiteral)) {
    const auto token = advance();
    return Expression{ExpressionKind::LogicLiteral, token.text, {},
                      token.span};
  }
  if (at(TokenKind::StringLiteral)) {
    const auto token = advance();
    return Expression{ExpressionKind::StringLiteral, token.text, {},
                      token.span};
  }
  if (at(TokenKind::Identifier)) {
    const auto name = advance();
    std::string canonical = vhdl_name(name.text);
    while (match(TokenKind::Dot)) {
      canonical += '.';
      canonical += vhdl_name(expect_identifier("selected name").text);
    }
    if (match(TokenKind::Apostrophe)) {
      if (at(TokenKind::LeftParen)) {
        auto value = parse_primary();
        return Expression{
            ExpressionKind::Call,
            "@vhdl-qualified:" + canonical,
            {std::move(value)},
            cover(name.span, previous().span)};
      }
      const auto attribute =
          expect_identifier("attribute designator");
      const auto designator = vhdl_name(attribute.text);
      static constexpr std::array<std::string_view, 19>
          supported_attributes{
              "left", "right", "low", "high", "length",
              "ascending", "event", "last_value", "last_event",
              "stable", "active", "pos", "val", "succ", "pred",
              "leftof", "rightof", "range", "reverse_range"};
      if (std::ranges::find(
              supported_attributes, designator)
          == supported_attributes.end()) {
        error(
            attribute,
            "FSIM-VHDL-SEM-030",
            "unsupported bounded VHDL attribute '"
                + attribute.text + "'");
      }
      std::vector<Expression> operands{
          Expression{
              ExpressionKind::Identifier,
              canonical,
              {},
              name.span}};
      if (match(TokenKind::LeftParen)) {
        operands.push_back(parse_expression());
        expect(
            TokenKind::RightParen,
            "')' after attribute argument",
            "FSIM-VHDL-PARSE-120");
      }
      const bool requires_argument =
          designator == "pos" || designator == "val"
          || designator == "succ" || designator == "pred"
          || designator == "leftof"
          || designator == "rightof";
      if (requires_argument && operands.size() != 2) {
        error(
            attribute,
            "FSIM-VHDL-PARSE-142",
            "enumeration attribute '" + attribute.text
                + "' requires one parenthesized argument");
      }
      return Expression{
          ExpressionKind::Call,
          "'" + designator,
          std::move(operands),
          cover(name.span, previous().span)};
    }
    if (match(TokenKind::LeftParen)) {
      const auto base =
          Expression{
              ExpressionKind::Identifier,
              canonical,
              {},
              name.span};
      std::vector<Expression> arguments;
      std::vector<std::string> argument_names;
      bool saw_named = false;
      if (!at(TokenKind::RightParen)) {
        do {
          std::string argument_name;
          if (at(TokenKind::Identifier)
              && at(TokenKind::Arrow, 1)) {
            saw_named = true;
            argument_name = vhdl_name(advance().text);
            advance();
          } else if (saw_named) {
            error(
                current(),
                "FSIM-VHDL-SEM-073",
                "a positional function-call actual cannot follow a "
                "named actual");
          }
          auto argument = parse_expression();
          if (argument_name.empty()
              && (keyword("downto", 0, true)
                  || keyword("to", 0, true))) {
            const auto first_span = argument.span;
            advance();
            const auto direction = previous();
            auto second = parse_expression();
            if (arguments.empty()
                && at(TokenKind::RightParen)) {
              expect(TokenKind::RightParen, "')' after slice",
                     "FSIM-VHDL-PARSE-033");
              return Expression{
                  ExpressionKind::Slice,
                  detail::ascii_lower(direction.text),
                  {base, std::move(argument), std::move(second)},
                  cover(name.span, previous().span)};
            }
            argument = Expression{
                ExpressionKind::Binary,
                detail::ascii_lower(direction.text),
                {std::move(argument), std::move(second)},
                cover(first_span, previous().span)};
          }
          arguments.push_back(std::move(argument));
          argument_names.push_back(std::move(argument_name));
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RightParen, "')' after arguments",
             "FSIM-VHDL-PARSE-033");
      Expression call{
          ExpressionKind::Call,
          std::move(canonical),
          std::move(arguments),
          cover(name.span, previous().span)};
      if (saw_named) {
        call.call_argument_names = std::move(argument_names);
      }
      for (;;) {
        if (match(TokenKind::LeftParen)) {
          auto first = parse_expression();
          if (match_keyword("downto", true)
              || match_keyword("to", true)) {
            const auto direction = previous();
            auto second = parse_expression();
            expect(TokenKind::RightParen, "')' after chained slice",
                   "FSIM-VHDL-PARSE-033");
            call = Expression{
                ExpressionKind::Slice,
                detail::ascii_lower(direction.text),
                {std::move(call), std::move(first), std::move(second)},
                cover(name.span, previous().span)};
          } else {
            expect(TokenKind::RightParen, "')' after chained index",
                   "FSIM-VHDL-PARSE-033");
            call = Expression{
                ExpressionKind::Index, "index",
                {std::move(call), std::move(first)},
                cover(name.span, previous().span)};
          }
          continue;
        }
        if (!match(TokenKind::Dot)) {
          break;
        }
        const auto member = expect_identifier("selected record element");
        call = Expression{
            ExpressionKind::Call,
            "@vhdl-member:" + vhdl_name(member.text),
            {std::move(call)},
            cover(name.span, member.span)};
      }
      return call;
    }
    return Expression{ExpressionKind::Identifier, std::move(canonical), {},
                      name.span};
  }
  if (match(TokenKind::LeftParen)) {
    const auto open = previous();
    auto first = parse_expression();
    if (!at(TokenKind::Arrow)
        && !at(TokenKind::Comma)
        && !at(TokenKind::Pipe)
        && !keyword("to", 0, true)
        && !keyword("downto", 0, true)) {
      expect(TokenKind::RightParen, "')' after expression",
             "FSIM-VHDL-PARSE-034");
      first.span = span_from(open, previous());
      return first;
    }

    Expression aggregate;
    aggregate.kind = ExpressionKind::Aggregate;
    bool named_association = false;
    bool saw_others = false;
    const auto append_association =
        [&](Expression head) {
          const auto choice_with_optional_range =
              [&]() {
                const auto left_span = head.span;
                if (!match_keyword("to", true)
                    && !match_keyword("downto", true)) {
                  return head;
                }
                const auto direction = previous();
                Expression right;
                if (at(TokenKind::Arrow)
                    || at(TokenKind::Pipe)
                    || at(TokenKind::Comma)
                    || at(TokenKind::RightParen)
                    || at_end()) {
                  error(
                      current(),
                      "FSIM-VHDL-PARSE-149",
                      "expected a right bound in aggregate range "
                      "choice");
                  right = Expression{
                      ExpressionKind::Invalid,
                      {},
                      {},
                      current().span};
                } else {
                  right = parse_expression();
                }
                return Expression{
                    ExpressionKind::Binary,
                    detail::ascii_lower(direction.text),
                    {std::move(head), std::move(right)},
                    cover(
                        left_span,
                        previous().span)};
              };
          std::vector<Expression> choices;
          choices.push_back(choice_with_optional_range());
          while (match(TokenKind::Pipe)) {
            if (at(TokenKind::Arrow)
                || at(TokenKind::Comma)
                || at(TokenKind::RightParen)
                || at_end()) {
              error(
                  current(),
                  "FSIM-VHDL-PARSE-150",
                  "expected an aggregate choice after '|'");
              break;
            }
            head = parse_expression();
            choices.push_back(choice_with_optional_range());
          }
          if (match(TokenKind::Arrow)) {
            named_association = true;
            std::size_t others_count = 0;
            for (auto& choice_expression : choices) {
              if (choice_expression.kind
                      == ExpressionKind::BooleanLiteral
                  || choice_expression.kind
                      == ExpressionKind::LogicLiteral
                  || choice_expression.kind
                      == ExpressionKind::StringLiteral
                  || choice_expression.kind
                      == ExpressionKind::Aggregate) {
                error(
                    previous(),
                    "FSIM-VHDL-PARSE-132",
                    "an aggregate choice must be a record element, "
                    "others, or a locally static integer expression or "
                    "range");
              }
              if (choice_expression.kind
                      == ExpressionKind::Identifier) {
                choice_expression.text =
                    vhdl_name(choice_expression.text);
                if (choice_expression.text == "others") {
                  ++others_count;
                }
              }
            }
            if (others_count != 0) {
              if (choices.size() != 1) {
                error(
                    previous(),
                    "FSIM-VHDL-SEM-041",
                    "others must be the only choice in its aggregate "
                    "association");
              }
              if (saw_others) {
                error(
                    previous(),
                    "FSIM-VHDL-SEM-039",
                    "an aggregate has more than one others "
                    "association");
              }
              saw_others = true;
            }
            std::string choice = "@array";
            if (choices.size() == 1
                && choices.front().kind
                    == ExpressionKind::Identifier) {
              choice = choices.front().text;
            }
            Expression value;
            if (at(TokenKind::Comma)
                || at(TokenKind::RightParen)
                || at_end()) {
              error(
                  current(),
                  "FSIM-VHDL-PARSE-133",
                  "expected a value after aggregate =>");
              value = Expression{
                  ExpressionKind::Invalid,
                  {},
                  {},
                  current().span};
            } else {
              value = parse_expression();
            }
            aggregate.aggregate_choices.push_back(
                std::move(choice));
            aggregate.aggregate_choice_expressions.push_back(
                std::move(choices));
            aggregate.operands.push_back(
                std::move(value));
            return;
          }
          if (choices.size() != 1
              || (choices.front().kind
                      == ExpressionKind::Binary
                  && (choices.front().text == "to"
                      || choices.front().text == "downto"))) {
            error(
                current(),
                "FSIM-VHDL-PARSE-151",
                "an aggregate choice list or range must be followed "
                "by =>");
          }
          if (named_association) {
            error(
                current(),
                "FSIM-VHDL-SEM-038",
                "a positional aggregate association cannot "
                "follow a named association");
          }
          aggregate.aggregate_choices.emplace_back();
          aggregate.aggregate_choice_expressions.emplace_back();
          aggregate.operands.push_back(
              std::move(choices.front()));
        };
    append_association(std::move(first));
    while (match(TokenKind::Comma)) {
      if (saw_others) {
        error(
            previous(),
            "FSIM-VHDL-SEM-039",
            "the others aggregate association must be last");
      }
      if (at(TokenKind::RightParen) || at_end()) {
        error(
            current(),
            "FSIM-VHDL-PARSE-133",
            "expected an aggregate association after ','");
        break;
      }
      append_association(parse_expression());
    }
    expect(TokenKind::RightParen, "')' after expression",
           "FSIM-VHDL-PARSE-034");
    aggregate.span = span_from(open, previous());
    return aggregate;
  }

  const auto invalid = advance();
  error(invalid, "FSIM-VHDL-PARSE-035", "expected expression");
  return Expression{ExpressionKind::Invalid, invalid.text, {},
                    invalid.span};
}

void VhdlParser::infer_process_edge(Process& process) {
  if (process.statements.size() != 1
      || process.statements.front().kind != StatementKind::If
      || !process.statements.front().else_statements.empty()) {
    return;
  }
  const auto& condition = process.statements.front().condition;
  if (condition.kind != ExpressionKind::Call ||
      condition.operands.size() != 1 ||
      condition.operands.front().kind != ExpressionKind::Identifier) {
    return;
  }
  EdgeKind edge = EdgeKind::Any;
  if (condition.text == "rising_edge") {
    edge = EdgeKind::Positive;
  } else if (condition.text == "falling_edge") {
    edge = EdgeKind::Negative;
  } else {
    return;
  }
  const auto& signal = condition.operands.front().text;
  for (auto& sensitivity : process.sensitivities) {
    if (sensitivity.signal == signal) {
      sensitivity.edge = edge;
    }
  }
}

}  // namespace fsim::frontend
