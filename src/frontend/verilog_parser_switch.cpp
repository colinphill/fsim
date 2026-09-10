// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {
namespace {

std::optional<VerilogStrength> strength_rank(
    const std::string_view spelling) {
  const auto base = spelling.size() > 1
      && (spelling.ends_with('0') || spelling.ends_with('1'))
      ? spelling.substr(0, spelling.size() - 1) : spelling;
  if (base == "highz") return VerilogStrength::HighZ;
  if (base == "weak") return VerilogStrength::Weak;
  if (base == "pull") return VerilogStrength::Pull;
  if (base == "strong") return VerilogStrength::Strong;
  if (base == "supply") return VerilogStrength::Supply;
  return std::nullopt;
}

Expression inverted(Expression value, const SourceSpan& source) {
  const auto span = cover(source, value.span);
  return Expression{
      ExpressionKind::Unary,
      "~",
      {std::move(value)},
      span};
}

Expression conditional(
    Expression control,
    Expression value,
    const SourceSpan& source) {
  return Expression{
      ExpressionKind::Call,
      "?:",
      {std::move(control), std::move(value),
       Expression{
           ExpressionKind::LogicLiteral, "1'bz", {}, source}},
      source};
}

}  // namespace

void VerilogParser::parse_switch_primitive(
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
  const bool pull = operation == "pullup" || operation == "pulldown";
  const bool bidirectional = operation == "tran" || operation == "rtran"
      || operation == "tranif0" || operation == "tranif1"
      || operation == "rtranif0" || operation == "rtranif1";
  const bool resistive = operation == "rtran" || operation == "rtranif0"
      || operation == "rtranif1" || operation == "rnmos"
      || operation == "rpmos" || operation == "rcmos";

  std::optional<VerilogDriveStrength> strength;
  if (pull && at(TokenKind::LeftParen)
      && at(TokenKind::Identifier, 1)
      && at(TokenKind::RightParen, 2)) {
    const auto strength_start = advance();
    const auto selected = advance();
    const auto rank = strength_rank(selected.text);
    const bool expected_polarity = operation == "pullup"
        ? selected.text.ends_with('1') : selected.text.ends_with('0');
    expect(
        TokenKind::RightParen,
        "')' after pull strength",
        "FSIM-SV-PARSE-251");
    if (!rank || !expected_polarity) {
      error(
          selected,
          "FSIM-SV-SEM-154",
          operation + " requires a matching single-output strength");
    } else {
      strength = VerilogDriveStrength{
          operation == "pulldown" ? *rank : VerilogStrength::Pull,
          operation == "pullup" ? *rank : VerilogStrength::Pull,
          cover(strength_start.span, previous().span)};
    }
  } else if (pull && verilog_drive_strength_start()) {
    strength = parse_verilog_drive_strength("pull primitive");
  } else if (!pull && verilog_drive_strength_start()) {
    error(
        current(),
        "FSIM-SV-SEM-152",
        "drive-strength syntax is not legal on a MOS or transmission "
        "primitive");
    (void)parse_verilog_drive_strength("switch primitive");
  }
  std::optional<Delay> delay;
  if (match(TokenKind::Hash)) {
    delay = parse_verilog_delay(previous(), bidirectional ? 2 : 3);
  }

  do {
    const auto instance_start = current();
    std::string instance_name;
    if (at(TokenKind::Identifier)
        && (at(TokenKind::LeftParen, 1)
            || at(TokenKind::LeftBracket, 1))) {
      instance_name = advance().text;
    }
    bool array_declared = false;
    std::vector<std::int64_t> instance_indices;
    if (match(TokenKind::LeftBracket)) {
      array_declared = true;
      const auto range = previous();
      const auto left = parse_expression();
      expect(
          TokenKind::Colon,
          "':' in switch-instance array range",
          "FSIM-SV-PARSE-252");
      const auto right = parse_expression();
      expect(
          TokenKind::RightBracket,
          "']' after switch-instance array range",
          "FSIM-SV-PARSE-253");
      const auto literal = [&](const auto& self,
                               const Expression& expression)
          -> std::optional<std::int64_t> {
        if (expression.kind != ExpressionKind::IntegerLiteral) {
          if (expression.kind == ExpressionKind::Unary
              && expression.operands.size() == 1
              && (expression.text == "+" || expression.text == "-")) {
            const auto operand = self(self, expression.operands.front());
            if (!operand) return std::nullopt;
            if (expression.text == "+") return operand;
            if (*operand == std::numeric_limits<std::int64_t>::min()) {
              return std::nullopt;
            }
            return -*operand;
          }
          return std::nullopt;
        }
        return detail::decimal_i64(expression.text);
      };
      const auto left_value = literal(literal, left);
      const auto right_value = literal(literal, right);
      if (instance_name.empty()) {
        error(
            range,
            "FSIM-SV-SEM-389",
            "a switch-instance array requires an instance name");
      } else if (!left_value || !right_value) {
        error(
            range,
            "FSIM-SV-SEM-156",
            "switch-instance array bounds must be decimal locally static "
            "integers");
      } else {
        const auto distance = *left_value >= *right_value
            ? static_cast<std::uint64_t>(*left_value)
                - static_cast<std::uint64_t>(*right_value)
            : static_cast<std::uint64_t>(*right_value)
                - static_cast<std::uint64_t>(*left_value);
        if (distance >= maximum_instance_array_elements) {
          error(
              range,
              "FSIM-SV-SEM-157",
              "materializing the switch-instance array would exceed the "
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
        "'(' after switch primitive name",
        "FSIM-SV-PARSE-248");
    std::vector<Expression> terminals;
    if (!at(TokenKind::RightParen)) {
      terminals.push_back(parse_lvalue());
      while (match(TokenKind::Comma)) {
        terminals.push_back(
            bidirectional && terminals.size() == 1
                ? parse_lvalue() : parse_expression());
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after switch primitive terminals",
        "FSIM-SV-PARSE-249");

    const auto required = pull ? std::size_t{1}
        : operation == "cmos" || operation == "rcmos"
            ? std::size_t{4}
        : operation == "tran" || operation == "rtran"
            ? std::size_t{2}
            : std::size_t{3};
    if (terminals.size() != required) {
      error(
          instance_start,
          "FSIM-SV-SEM-153",
          operation + " requires exactly " + std::to_string(required)
              + " terminals");
      continue;
    }
    if (array_declared && instance_indices.empty()) continue;

    const auto terminal_for =
        [&](Expression terminal,
            const std::size_t ordinal,
            const std::int64_t instance_index) {
      auto terminal_index = instance_index;
      const SignalDeclaration* declaration = nullptr;
      if (terminal.kind == ExpressionKind::Identifier) {
        const auto find = [&](const auto& declarations) {
          return std::ranges::find_if(
              declarations,
              [&](const SignalDeclaration& candidate) {
                return candidate.name == terminal.text;
              });
        };
        const auto signal = find(signals);
        if (signal != signals.end()) {
          declaration = &*signal;
        } else {
          const auto port = find(ports);
          if (port != ports.end()) declaration = &*port;
        }
      }
      if (declaration) {
        const auto width = declaration->type.width();
        if (width && *width == 1) return terminal;
        if (!width || *width != instance_indices.size()) {
          error(
              instance_start,
              "FSIM-SV-SEM-158",
              "a switch-array terminal must be scalar or match the "
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
      } else if (terminal.kind == ExpressionKind::LogicLiteral
                 && terminal.text.starts_with("1'")) {
        return terminal;
      }
      const auto terminal_span = terminal.span;
      return Expression{
          ExpressionKind::Index,
          "index",
          {std::move(terminal),
           Expression{ExpressionKind::IntegerLiteral,
                      std::to_string(terminal_index), {}, terminal_span}},
          terminal_span};
    };

    const auto default_strength = VerilogDriveStrength{
        resistive ? VerilogStrength::Pull
                  : pull ? VerilogStrength::Pull
                         : VerilogStrength::Strong,
        resistive ? VerilogStrength::Pull
                  : pull ? VerilogStrength::Pull
                         : VerilogStrength::Strong,
        start.span};
    const auto emit = [&](Expression target,
                          Expression value,
                          std::string suffix,
                          std::optional<Expression> switch_source =
                              std::nullopt,
                          std::optional<Expression> switch_control =
                              std::nullopt,
                          const bool switch_active_high = true) {
      Statement statement;
      statement.kind = StatementKind::Assignment;
      statement.assignment_kind = AssignmentKind::Continuous;
      statement.target = std::move(target);
      statement.value = std::move(value);
      statement.delay = delay;
      statement.verilog_drive_strength =
          strength.value_or(default_strength);
      statement.verilog_switch_driver = switch_source.has_value();
      statement.verilog_switch_bidirectional =
          switch_source.has_value() && bidirectional;
      statement.verilog_switch_resistive =
          switch_source.has_value() && resistive;
      if (switch_source) {
        statement.verilog_switch_source = std::move(*switch_source);
      }
      if (switch_control) {
        statement.verilog_switch_control = std::move(*switch_control);
        statement.verilog_switch_active_high = switch_active_high;
      }
      statement.label = instance_name.empty()
          ? std::string{} : instance_name + std::move(suffix);
      statement.span = cover(start.span, previous().span);
      statements.push_back(std::move(statement));
    };

    const bool switch_array = !instance_indices.empty();
    if (!switch_array) instance_indices.push_back(0);
    for (std::size_t ordinal = 0;
         ordinal < instance_indices.size(); ++ordinal) {
      std::vector<Expression> mapped;
      mapped.reserve(terminals.size());
      for (const auto& terminal : terminals) {
        mapped.push_back(
            !switch_array
                ? terminal
                : terminal_for(
                    terminal, ordinal, instance_indices[ordinal]));
      }
      const auto saved_name = instance_name;
      if (switch_array) {
        instance_name += "[" + std::to_string(instance_indices[ordinal])
            + "]";
      }
      if (pull) {
        emit(
            std::move(mapped[0]),
            Expression{
                ExpressionKind::LogicLiteral,
                operation == "pullup" ? "1'b1" : "1'b0",
                {}, start.span},
            {});
      } else if (bidirectional) {
        auto left_value = mapped[1];
        auto right_value = mapped[0];
        std::optional<Expression> left_switch_control;
        std::optional<Expression> right_switch_control;
        const bool active_high = operation == "tranif1"
            || operation == "rtranif1";
        if (required == 3) {
          auto left_control = mapped[2];
          auto right_control = mapped[2];
          left_switch_control = mapped[2];
          right_switch_control = mapped[2];
          if (operation == "tranif0" || operation == "rtranif0") {
            left_control = inverted(std::move(left_control), start.span);
            right_control = inverted(std::move(right_control), start.span);
          }
          left_value = conditional(
              std::move(left_control), std::move(left_value), start.span);
          right_value = conditional(
              std::move(right_control), std::move(right_value), start.span);
        }
        emit(
            mapped[0], std::move(left_value), "$left", mapped[1],
            std::move(left_switch_control), active_high);
        emit(
            mapped[1], std::move(right_value), "$right", mapped[0],
            std::move(right_switch_control), active_high);
      } else {
        auto control = mapped[2];
        if (operation == "pmos" || operation == "rpmos") {
          control = inverted(std::move(control), start.span);
        } else if (operation == "cmos" || operation == "rcmos") {
          auto pcontrol = inverted(mapped[3], start.span);
          control = Expression{
              ExpressionKind::Binary,
              "&",
              {std::move(control), std::move(pcontrol)},
              start.span};
        }
        auto switch_source = mapped[1];
        emit(
            std::move(mapped[0]),
            conditional(
                std::move(control), std::move(mapped[1]), start.span),
            {}, std::move(switch_source));
      }
      instance_name = saved_name;
    }
  } while (match(TokenKind::Comma));

  expect(
      TokenKind::Semicolon,
      "';' after switch primitive",
      "FSIM-SV-PARSE-250");
}

}  // namespace fsim::frontend
