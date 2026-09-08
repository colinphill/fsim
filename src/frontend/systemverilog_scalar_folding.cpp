// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/systemverilog_scalar_folding.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace fsim::frontend {
namespace {

static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);

bool real_kind(const SystemVerilogScalarKind kind) noexcept {
  return kind == SystemVerilogScalarKind::ShortReal
      || kind == SystemVerilogScalarKind::Real
      || kind == SystemVerilogScalarKind::Realtime;
}

SystemVerilogScalarKind promote(
    const SystemVerilogScalarKind left,
    const SystemVerilogScalarKind right) noexcept {
  if (left == SystemVerilogScalarKind::Chandle
      || right == SystemVerilogScalarKind::Chandle) {
    return SystemVerilogScalarKind::Chandle;
  }
  if (left == SystemVerilogScalarKind::Real
      || right == SystemVerilogScalarKind::Real) return SystemVerilogScalarKind::Real;
  if (left == SystemVerilogScalarKind::Realtime
      || right == SystemVerilogScalarKind::Realtime) return SystemVerilogScalarKind::Realtime;
  if (left == SystemVerilogScalarKind::ShortReal
      || right == SystemVerilogScalarKind::ShortReal) return SystemVerilogScalarKind::ShortReal;
  if (left == SystemVerilogScalarKind::Time
      || right == SystemVerilogScalarKind::Time) return SystemVerilogScalarKind::Time;
  return SystemVerilogScalarKind::None;
}

std::optional<std::uint64_t> time_factor(const std::string_view unit) noexcept {
  if (unit == "fs") return 1;
  if (unit == "ps") return 1'000;
  if (unit == "ns") return 1'000'000;
  if (unit == "us") return 1'000'000'000;
  if (unit == "ms") return 1'000'000'000'000;
  if (unit == "s") return 1'000'000'000'000'000;
  return std::nullopt;
}

SystemVerilogScalarConstant integer_constant(
    const std::int64_t value,
    const SystemVerilogScalarKind kind = SystemVerilogScalarKind::None) {
  return {kind, std::bit_cast<std::uint64_t>(value)};
}

SystemVerilogScalarConstant real_constant(
    const double value,
    const SystemVerilogScalarKind kind) {
  return kind == SystemVerilogScalarKind::ShortReal
      ? SystemVerilogScalarConstant{
            kind, std::bit_cast<std::uint32_t>(static_cast<float>(value))}
      : SystemVerilogScalarConstant{kind, std::bit_cast<std::uint64_t>(value)};
}

std::string real_spelling(const SystemVerilogScalarConstant& value) {
  std::array<char, 64> buffer{};
  std::to_chars_result converted;
  if (value.kind == SystemVerilogScalarKind::ShortReal) {
    converted = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(),
        std::bit_cast<float>(static_cast<std::uint32_t>(value.bits)),
        std::chars_format::general, std::numeric_limits<float>::max_digits10);
  } else {
    converted = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(),
        std::bit_cast<double>(value.bits), std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
  }
  return converted.ec == std::errc{}
      ? std::string{buffer.data(), converted.ptr}
      : std::string{"0.0"};
}

SystemVerilogDecimalLiteral decimal_payload(std::string spelling) {
  SystemVerilogDecimalLiteral result;
  const auto exponent_position = spelling.find_first_of("eE");
  std::int64_t exponent{};
  if (exponent_position != std::string::npos) {
    const auto exponent_text = std::string_view{spelling}.substr(
        exponent_position + 1);
    (void)std::from_chars(
        exponent_text.data(), exponent_text.data() + exponent_text.size(),
        exponent);
    spelling.resize(exponent_position);
  }
  const auto point = spelling.find('.');
  if (point != std::string::npos) {
    exponent -= static_cast<std::int64_t>(spelling.size() - point - 1);
    spelling.erase(point, 1);
  }
  const auto first = spelling.find_first_not_of('0');
  if (first == std::string::npos) return {result.kind, "0", 0, {}};
  spelling.erase(0, first);
  while (spelling.size() > 1 && spelling.back() == '0') {
    spelling.pop_back();
    ++exponent;
  }
  result.digits = std::move(spelling);
  result.decimal_exponent = exponent;
  return result;
}

std::optional<double> exact_decimal(
    const SystemVerilogDecimalLiteral& literal,
    std::string& error) {
  const auto spelling = literal.digits + "e"
      + std::to_string(literal.decimal_exponent);
  double value{};
  const auto parsed = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value,
      std::chars_format::scientific);
  if (parsed.ec != std::errc{}
      || parsed.ptr != spelling.data() + spelling.size()
      || !std::isfinite(value)) {
    error = "real/time literal is outside deterministic IEEE-754 range";
    return std::nullopt;
  }
  return value;
}

std::optional<std::int64_t> integer_literal(const std::string_view text) {
  std::string spelling;
  for (const char character : text) if (character != '_') spelling += character;
  std::int64_t value{};
  const auto parsed = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), value);
  return parsed.ec == std::errc{}
          && parsed.ptr == spelling.data() + spelling.size()
      ? std::optional<std::int64_t>{value} : std::nullopt;
}

std::optional<double> numeric(
    const SystemVerilogScalarConstant& value) noexcept {
  if (const auto real = value.real()) return real;
  if (const auto integer = value.integral()) return static_cast<double>(*integer);
  return std::nullopt;
}

std::optional<std::int64_t> checked_integral_arithmetic(
    const std::int64_t left,
    const std::int64_t right,
    const std::string_view operation) noexcept {
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  if (operation == "+") {
    if ((right > 0 && left > maximum - right)
        || (right < 0 && left < minimum - right)) return std::nullopt;
    return left + right;
  }
  if (operation == "-") {
    if ((right < 0 && left > maximum + right)
        || (right > 0 && left < minimum + right)) return std::nullopt;
    return left - right;
  }
  if (operation == "*") {
    if (left == 0 || right == 0) return 0;
    if ((left == -1 && right == minimum)
        || (right == -1 && left == minimum)) return std::nullopt;
    if (left > 0) {
      if ((right > 0 && left > maximum / right)
          || (right < 0 && right < minimum / left)) return std::nullopt;
    } else if ((right > 0 && left < minimum / right)
               || (right < 0 && left < maximum / right)) {
      return std::nullopt;
    }
    return left * right;
  }
  return std::nullopt;
}

std::optional<SystemVerilogScalarConstant> evaluate(
    const Expression& expression,
    const SystemVerilogScalarConstantEnvironment& environment,
    const SystemVerilogScalarEvaluationContext& context,
    std::string& error) {
  if (expression.systemverilog_decimal_literal) {
    const auto& literal = *expression.systemverilog_decimal_literal;
    auto value = exact_decimal(literal, error);
    if (!value) return std::nullopt;
    auto kind = SystemVerilogScalarKind::Real;
    if (literal.kind == SystemVerilogDecimalLiteralKind::Time) {
      const auto factor = time_factor(literal.time_unit);
      if (!factor || context.time_unit_femtoseconds == 0
          || context.time_precision_femtoseconds == 0) {
        error = "time literal has an invalid unit or evaluation context";
        return std::nullopt;
      }
      *value *= static_cast<double>(*factor)
          / static_cast<double>(context.time_unit_femtoseconds);
      kind = SystemVerilogScalarKind::Realtime;
    }
    return real_constant(*value, kind);
  }
  if (expression.kind == ExpressionKind::IntegerLiteral) {
    const auto value = integer_literal(expression.text);
    if (!value) {
      error = "integer literal is outside the signed 64-bit scalar range";
      return std::nullopt;
    }
    return integer_constant(*value);
  }
  if (expression.kind == ExpressionKind::Identifier) {
    if (const auto found = environment.find(expression.text);
        found != environment.end()) return found->second;
    error = "unknown scalar constant '" + expression.text + "'";
    return std::nullopt;
  }
  if (expression.kind == ExpressionKind::Call
      && expression.text == "@sv-null") {
    return SystemVerilogScalarConstant{
        SystemVerilogScalarKind::Chandle, 0};
  }
  if (expression.kind == ExpressionKind::Unary
      && expression.operands.size() == 1) {
    auto operand = evaluate(expression.operands[0], environment, context, error);
    if (!operand) return std::nullopt;
    if (expression.text == "+") return operand;
    if (expression.text == "!") return integer_constant(!operand->truth());
    if (expression.text == "-") {
      if (const auto value = operand->real()) return real_constant(-*value, operand->kind);
      if (const auto value = operand->integral(); value
          && *value != std::numeric_limits<std::int64_t>::min()) {
        return integer_constant(-*value, operand->kind);
      }
      error = "unary scalar negation overflows";
      return std::nullopt;
    }
  }
  if (expression.kind == ExpressionKind::Call && expression.text == "?:"
      && expression.operands.size() == 3) {
    auto condition = evaluate(expression.operands[0], environment, context, error);
    if (!condition) return std::nullopt;
    auto selected = evaluate(
        expression.operands[condition->truth() ? 1U : 2U],
        environment, context, error);
    if (!selected) return std::nullopt;
    return convert_systemverilog_scalar_constant(
        *selected,
        promote(expression.operands[1].systemverilog_scalar_kind,
                expression.operands[2].systemverilog_scalar_kind), error);
  }
  if (expression.kind == ExpressionKind::Call
      && expression.operands.size() == 1
      && expression.text.starts_with("@sv-cast:")) {
    auto operand = evaluate(expression.operands[0], environment, context, error);
    if (!operand) return std::nullopt;
    const auto name = std::string_view{expression.text}.substr(9);
    const auto target = name == "shortreal" ? SystemVerilogScalarKind::ShortReal
        : name == "real" ? SystemVerilogScalarKind::Real
        : name == "realtime" ? SystemVerilogScalarKind::Realtime
        : name == "time" ? SystemVerilogScalarKind::Time
        : name == "chandle" ? SystemVerilogScalarKind::Chandle
        : SystemVerilogScalarKind::None;
    return convert_systemverilog_scalar_constant(*operand, target, error);
  }
  if (expression.kind != ExpressionKind::Binary
      || expression.operands.size() != 2) {
    error = "expression is not a supported scalar constant form";
    return std::nullopt;
  }
  auto left = evaluate(expression.operands[0], environment, context, error);
  if (!left) return std::nullopt;
  if (expression.text == "&&" && !left->truth()) return integer_constant(0);
  if (expression.text == "||" && left->truth()) return integer_constant(1);
  auto right = evaluate(expression.operands[1], environment, context, error);
  if (!right) return std::nullopt;
  if (left->kind == SystemVerilogScalarKind::Chandle
      || right->kind == SystemVerilogScalarKind::Chandle) {
    if (left->kind != SystemVerilogScalarKind::Chandle
        || right->kind != SystemVerilogScalarKind::Chandle
        || (expression.text != "==" && expression.text != "!="
            && expression.text != "===" && expression.text != "!==")) {
      error = "chandle constants only support equality and inequality with "
              "chandle or null";
      return std::nullopt;
    }
    const bool equal = left->bits == right->bits;
    return integer_constant(
        expression.text == "==" || expression.text == "==="
            ? equal : !equal);
  }
  if (expression.text == "&&" || expression.text == "||") {
    return integer_constant(right->truth());
  }
  const auto left_integer = left->integral();
  const auto right_integer = right->integral();
  const auto lhs = numeric(*left);
  const auto rhs = numeric(*right);
  if (!lhs || !rhs) {
    error = "scalar constant operand is not numeric";
    return std::nullopt;
  }
  if (left_integer && right_integer) {
    if (expression.text == "==" || expression.text == "===") return integer_constant(*left_integer == *right_integer);
    if (expression.text == "!=" || expression.text == "!==") return integer_constant(*left_integer != *right_integer);
    if (expression.text == "<") return integer_constant(*left_integer < *right_integer);
    if (expression.text == "<=") return integer_constant(*left_integer <= *right_integer);
    if (expression.text == ">") return integer_constant(*left_integer > *right_integer);
    if (expression.text == ">=") return integer_constant(*left_integer >= *right_integer);
  } else {
    if (expression.text == "==" || expression.text == "===") return integer_constant(*lhs == *rhs);
    if (expression.text == "!=" || expression.text == "!==") return integer_constant(*lhs != *rhs);
    if (expression.text == "<") return integer_constant(*lhs < *rhs);
    if (expression.text == "<=") return integer_constant(*lhs <= *rhs);
    if (expression.text == ">") return integer_constant(*lhs > *rhs);
    if (expression.text == ">=") return integer_constant(*lhs >= *rhs);
  }
  const auto kind = promote(left->kind, right->kind);
  if (left_integer && right_integer
      && !real_kind(kind)) {
    if ((expression.text == "/" || expression.text == "%")
        && *right_integer == 0) {
      error = "scalar constant division by zero";
      return std::nullopt;
    }
    if (expression.text == "/") {
      if (*left_integer == std::numeric_limits<std::int64_t>::min()
          && *right_integer == -1) {
        error = "integral scalar constant operation overflows";
        return std::nullopt;
      }
      return integer_constant(*left_integer / *right_integer, kind);
    }
    if (expression.text == "%") {
      if (*left_integer == std::numeric_limits<std::int64_t>::min()
          && *right_integer == -1) return integer_constant(0, kind);
      return integer_constant(*left_integer % *right_integer, kind);
    }
    if (const auto result = checked_integral_arithmetic(
            *left_integer, *right_integer, expression.text)) {
      return integer_constant(*result, kind);
    }
    error = "integral scalar constant operation overflows";
    return std::nullopt;
  }
  double result{};
  if (expression.text == "+") result = *lhs + *rhs;
  else if (expression.text == "-") result = *lhs - *rhs;
  else if (expression.text == "*") result = *lhs * *rhs;
  else if (expression.text == "/" && *rhs != 0.0) result = *lhs / *rhs;
  else {
    error = *rhs == 0.0 && expression.text == "/"
        ? "scalar constant division by zero"
        : "unsupported scalar constant operator '" + expression.text + "'";
    return std::nullopt;
  }
  if (!std::isfinite(result)) {
    error = "scalar constant operation produced a nonfinite result";
    return std::nullopt;
  }
  if (real_kind(kind)) return real_constant(result, kind);
  if (result > static_cast<double>(std::numeric_limits<std::int64_t>::max())
      || result < static_cast<double>(std::numeric_limits<std::int64_t>::min())
      || std::trunc(result) != result) {
    error = "integral scalar constant operation overflows";
    return std::nullopt;
  }
  return integer_constant(static_cast<std::int64_t>(result), kind);
}

}  // namespace

std::optional<std::int64_t>
SystemVerilogScalarConstant::integral() const noexcept {
  return kind == SystemVerilogScalarKind::None
          || kind == SystemVerilogScalarKind::Time
      ? std::optional<std::int64_t>{std::bit_cast<std::int64_t>(bits)}
      : std::nullopt;
}

std::optional<double> SystemVerilogScalarConstant::real() const noexcept {
  if (kind == SystemVerilogScalarKind::ShortReal) {
    return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
  }
  return kind == SystemVerilogScalarKind::Real
          || kind == SystemVerilogScalarKind::Realtime
      ? std::optional<double>{std::bit_cast<double>(bits)} : std::nullopt;
}

bool SystemVerilogScalarConstant::truth() const noexcept {
  if (const auto value = real()) return *value != 0.0;
  return integral().value_or(0) != 0;
}

std::string SystemVerilogScalarConstant::display() const {
  if (kind == SystemVerilogScalarKind::Chandle) {
    return bits == 0 ? "null" : "<opaque-chandle>";
  }
  if (const auto value = integral()) return std::to_string(*value);
  return real_spelling(*this);
}

std::string SystemVerilogScalarConstant::canonical() const {
  std::ostringstream output;
  output << "svscalar-v1:k=" << static_cast<unsigned>(kind)
         << ":b=" << std::hex << std::setw(16) << std::setfill('0') << bits;
  return output.str();
}

Expression SystemVerilogScalarConstant::expression(
    const SourceSpan& use_span) const {
  if (kind == SystemVerilogScalarKind::Chandle) {
    Expression value{
        ExpressionKind::Call, "@sv-null", {}, use_span};
    value.systemverilog_scalar_kind = SystemVerilogScalarKind::Chandle;
    return value;
  }
  if (const auto value = integral()) {
    Expression literal{
        ExpressionKind::IntegerLiteral, std::to_string(*value), {}, use_span};
    if (kind == SystemVerilogScalarKind::None) return literal;
    Expression result{
        ExpressionKind::Call, "@sv-cast:time", {std::move(literal)}, use_span};
    result.systemverilog_scalar_kind = kind;
    return result;
  }
  auto spelling = real_spelling(*this);
  const bool negative = !spelling.empty() && spelling.front() == '-';
  if (negative) spelling.erase(0, 1);
  Expression literal{
      ExpressionKind::IntegerLiteral, spelling, {}, use_span};
  literal.systemverilog_decimal_literal = decimal_payload(spelling);
  literal.systemverilog_scalar_kind = SystemVerilogScalarKind::Real;
  Expression result{
      ExpressionKind::Call,
      kind == SystemVerilogScalarKind::ShortReal
          ? "@sv-cast:shortreal"
          : kind == SystemVerilogScalarKind::Realtime
              ? "@sv-cast:realtime" : "@sv-cast:real",
      {std::move(literal)}, use_span};
  result.systemverilog_scalar_kind = kind;
  if (!negative) return result;
  Expression negated{
      ExpressionKind::Unary, "-", {std::move(result)}, use_span};
  negated.systemverilog_scalar_kind = kind;
  return negated;
}

std::optional<SystemVerilogScalarConstant>
convert_systemverilog_scalar_constant(
    const SystemVerilogScalarConstant& value,
    const SystemVerilogScalarKind target,
    std::string& error) {
  error.clear();
  if (target == value.kind) return value;
  if (target == SystemVerilogScalarKind::Chandle) {
    const auto integral = value.integral();
    if (integral && *integral == 0) {
      return SystemVerilogScalarConstant {
          SystemVerilogScalarKind::Chandle, 0U};
    }
    error = "chandle casts require a chandle or null operand";
    return std::nullopt;
  }
  if (value.kind == SystemVerilogScalarKind::Chandle) {
    error = "chandle casts require a chandle or null operand";
    return std::nullopt;
  }
  const auto number = numeric(value);
  if (!number || !std::isfinite(*number)) {
    error = "scalar value is not finite and convertible";
    return std::nullopt;
  }
  if (real_kind(target)) return real_constant(*number, target);
  if (*number > static_cast<double>(std::numeric_limits<std::int64_t>::max())
      || *number < static_cast<double>(std::numeric_limits<std::int64_t>::min())) {
    error = "real-family conversion exceeds the signed 64-bit range";
    return std::nullopt;
  }
  return integer_constant(static_cast<std::int64_t>(std::round(*number)), target);
}

bool propagate_systemverilog_scalar_types(
    Expression& expression,
    const SystemVerilogScalarTypeEnvironment& environment,
    std::string& error) {
  error.clear();
  for (auto& operand : expression.operands) {
    if (!propagate_systemverilog_scalar_types(operand, environment, error)) return false;
  }
  if (expression.systemverilog_decimal_literal) {
    expression.systemverilog_scalar_kind =
        expression.systemverilog_decimal_literal->kind
                == SystemVerilogDecimalLiteralKind::Time
            ? SystemVerilogScalarKind::Realtime : SystemVerilogScalarKind::Real;
  } else if (expression.kind == ExpressionKind::Identifier) {
    if (const auto found = environment.find(expression.text); found != environment.end()) {
      expression.systemverilog_scalar_kind = found->second;
    }
  } else if (expression.kind == ExpressionKind::Call
             && expression.text == "@sv-null") {
    expression.systemverilog_scalar_kind = SystemVerilogScalarKind::Chandle;
  } else if (expression.kind == ExpressionKind::Unary
             && expression.operands.size() == 1) {
    if (expression.operands[0].systemverilog_scalar_kind
        == SystemVerilogScalarKind::Chandle) {
      error = "chandle does not support unary operators";
      return false;
    }
    expression.systemverilog_scalar_kind = expression.text == "!"
        ? SystemVerilogScalarKind::None
        : expression.operands[0].systemverilog_scalar_kind;
  } else if (expression.kind == ExpressionKind::Binary
             && expression.operands.size() == 2) {
    const auto left = expression.operands[0].systemverilog_scalar_kind;
    const auto right = expression.operands[1].systemverilog_scalar_kind;
    if (left == SystemVerilogScalarKind::Chandle
        || right == SystemVerilogScalarKind::Chandle) {
      if (left != SystemVerilogScalarKind::Chandle
          || right != SystemVerilogScalarKind::Chandle
          || (expression.text != "==" && expression.text != "!="
              && expression.text != "===" && expression.text != "!==")) {
        error = "chandle only supports equality and inequality with chandle "
                "or null";
        return false;
      }
      expression.systemverilog_scalar_kind = SystemVerilogScalarKind::None;
      return true;
    }
    const bool predicate = expression.text == "==" || expression.text == "!="
        || expression.text == "===" || expression.text == "!=="
        || expression.text == "<" || expression.text == "<="
        || expression.text == ">" || expression.text == ">="
        || expression.text == "&&" || expression.text == "||";
    expression.systemverilog_scalar_kind = predicate
        ? SystemVerilogScalarKind::None
        : promote(expression.operands[0].systemverilog_scalar_kind,
                  expression.operands[1].systemverilog_scalar_kind);
  } else if (expression.kind == ExpressionKind::Call
             && expression.text == "?:"
             && expression.operands.size() == 3) {
    if (expression.operands[0].systemverilog_scalar_kind
        == SystemVerilogScalarKind::Chandle) {
      error = "chandle does not provide conditional truth";
      return false;
    }
    const auto left = expression.operands[1].systemverilog_scalar_kind;
    const auto right = expression.operands[2].systemverilog_scalar_kind;
    if (left == SystemVerilogScalarKind::Chandle
        || right == SystemVerilogScalarKind::Chandle) {
      if (left != SystemVerilogScalarKind::Chandle
          || right != SystemVerilogScalarKind::Chandle) {
        error = "conditional chandle branches must both be chandle or null";
        return false;
      }
      expression.systemverilog_scalar_kind = SystemVerilogScalarKind::Chandle;
      return true;
    }
    expression.systemverilog_scalar_kind = promote(
        expression.operands[1].systemverilog_scalar_kind,
        expression.operands[2].systemverilog_scalar_kind);
  } else if (expression.kind == ExpressionKind::Call
             && expression.text.starts_with("@sv-cast:")) {
    const auto name = std::string_view{expression.text}.substr(9);
    expression.systemverilog_scalar_kind = name == "shortreal"
        ? SystemVerilogScalarKind::ShortReal
        : name == "real" ? SystemVerilogScalarKind::Real
        : name == "realtime" ? SystemVerilogScalarKind::Realtime
        : name == "time" ? SystemVerilogScalarKind::Time
        : name == "chandle" ? SystemVerilogScalarKind::Chandle
        : SystemVerilogScalarKind::None;
  }
  return true;
}

std::optional<SystemVerilogScalarConstant>
evaluate_systemverilog_scalar_constant(
    const Expression& expression,
    const SystemVerilogScalarConstantEnvironment& environment,
    const SystemVerilogScalarEvaluationContext& context,
    std::string& error) {
  error.clear();
  return evaluate(expression, environment, context, error);
}

}  // namespace fsim::frontend
