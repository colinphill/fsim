// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

using frontend::AssignmentKind;
using frontend::DesignUnit;
using frontend::Expression;
using frontend::ExpressionKind;
using frontend::ProcessKind;
using frontend::Statement;
using frontend::StatementKind;
using runtime::Logic4;
using runtime::PackedLogic4;
using namespace runtime::simir;

using ConstantEnvironment =
    std::unordered_map<std::string, std::int64_t>;

std::optional<std::int64_t> evaluate_constant_expression(
    const Expression& expression,
    const ConstantEnvironment& environment,
    std::string& error);

struct LoweredLiteral {
    PackedLogic4 value;
    frontend::ValueDomain domain{frontend::ValueDomain::Bit2};
};

std::string simple_top_name(std::string_view top) {
    if (const auto colon = top.rfind(':'); colon != std::string_view::npos) {
        top.remove_prefix(colon + 1);
    }
    if (const auto dot = top.rfind('.'); dot != std::string_view::npos) {
        top.remove_prefix(dot + 1);
    }
    if (const auto architecture = top.find('('); architecture != std::string_view::npos) {
        top = top.substr(0, architecture);
    }
    return std::string{top};
}

std::optional<std::uint64_t> unsigned_decimal(std::string_view text) {
    std::string cleaned{text};
    cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '_'), cleaned.end());
    std::uint64_t value{};
    const auto result =
        std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
    if (result.ec != std::errc{} || result.ptr != cleaned.data() + cleaned.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> constant_index(
    const Expression& expression) {
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        std::string cleaned{expression.text};
        cleaned.erase(
            std::remove(cleaned.begin(), cleaned.end(), '_'),
            cleaned.end());
        std::int64_t value{};
        const auto result = std::from_chars(
            cleaned.data(), cleaned.data() + cleaned.size(), value);
        if (result.ec == std::errc{}
            && result.ptr == cleaned.data() + cleaned.size()) {
            return value;
        }
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1
        && (expression.text == "+" || expression.text == "-")) {
        const auto value = constant_index(expression.operands[0]);
        if (!value) {
            return std::nullopt;
        }
        if (expression.text == "+") {
            return value;
        }
        if (*value == std::numeric_limits<std::int64_t>::min()) {
            return std::nullopt;
        }
        return -*value;
    }
    if (expression.kind == ExpressionKind::Binary
        || expression.kind == ExpressionKind::Call) {
        std::string error;
        return evaluate_constant_expression(
            expression, {}, error);
    }
    return std::nullopt;
}

std::uint64_t index_distance(
    const std::int64_t lhs,
    const std::int64_t rhs) noexcept {
    return lhs >= rhs
        ? static_cast<std::uint64_t>(lhs)
              - static_cast<std::uint64_t>(rhs)
        : static_cast<std::uint64_t>(rhs)
              - static_cast<std::uint64_t>(lhs);
}

PackedLogic4 unsigned_value(const std::uint64_t value, const std::size_t width) {
    PackedLogic4 result(width, Logic4::zero);
    for (std::size_t bit = 0; bit < width && bit < 64; ++bit) {
        result.set(bit, ((value >> bit) & 1U) != 0 ? Logic4::one : Logic4::zero);
    }
    return result;
}

std::optional<LoweredLiteral> literal_value(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Language language) {
    auto text = expression.text;
    if (expression.kind == ExpressionKind::BooleanLiteral
        && language == frontend::Language::Vhdl2008
        && (text == "true" || text == "false")) {
        return LoweredLiteral{
            PackedLogic4(
                1,
                text == "true" ? Logic4::one : Logic4::zero),
            frontend::ValueDomain::Boolean};
    }
    if (expression.kind == ExpressionKind::LogicLiteral && text.size() == 3
        && text.front() == '\'' && text.back() == '\'') {
        if (language == frontend::Language::Vhdl2008) {
            const auto parsed = runtime::parse_logic9(text[1]);
            if (!parsed) {
                return std::nullopt;
            }
            const auto domain =
                *parsed == runtime::Logic9::zero
                        || *parsed == runtime::Logic9::one
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic9;
            return LoweredLiteral{
                PackedLogic4(1, runtime::to_logic4(*parsed)), domain};
        }
        const auto parsed = runtime::parse_logic4(text[1]);
        if (!parsed) {
            return std::nullopt;
        }
        const auto domain =
            *parsed == Logic4::zero || *parsed == Logic4::one
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
        return LoweredLiteral{PackedLogic4(1, *parsed), domain};
    }
    if (expression.kind == ExpressionKind::StringLiteral && text.size() >= 2
        && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
        try {
            if (language == frontend::Language::Vhdl2008) {
                const auto nine_state =
                    runtime::PackedLogic9::from_msb_string(text);
                auto domain = frontend::ValueDomain::Bit2;
                for (std::size_t bit = 0; bit < nine_state.width(); ++bit) {
                    const auto value = nine_state.get(bit);
                    if (value != runtime::Logic9::zero
                        && value != runtime::Logic9::one) {
                        domain = frontend::ValueDomain::Logic9;
                        break;
                    }
                }
                return LoweredLiteral{
                    runtime::collapse_to_logic4(nine_state), domain};
            }
            auto value = PackedLogic4::from_msb_string(text);
            auto domain = frontend::ValueDomain::Bit2;
            for (std::size_t bit = 0; bit < value.width(); ++bit) {
                if (value.get(bit) != Logic4::zero
                    && value.get(bit) != Logic4::one) {
                    domain = frontend::ValueDomain::Logic4;
                    break;
                }
            }
            return LoweredLiteral{std::move(value), domain};
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }

    const auto quote = text.find('\'');
    if (quote != std::string::npos) {
        const auto width_text = std::string_view{text}.substr(0, quote);
        std::size_t width = expected_width;
        if (!width_text.empty()) {
            const auto parsed_width = unsigned_decimal(width_text);
            if (!parsed_width || *parsed_width == 0
                || *parsed_width > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            width = static_cast<std::size_t>(*parsed_width);
        }
        auto digits = std::string_view{text}.substr(quote + 1);
        if (!digits.empty() && (digits.front() == 's' || digits.front() == 'S')) {
            digits.remove_prefix(1);
        }
        if (digits.empty()) {
            return std::nullopt;
        }
        const auto base = static_cast<char>(std::tolower(static_cast<unsigned char>(digits.front())));
        digits.remove_prefix(1);
        if (base == 'b') {
            std::string expanded;
            for (const char c : digits) {
                if (c != '_') {
                    expanded.push_back(
                        language != frontend::Language::Vhdl2008
                                && c == '?'
                            ? 'z'
                            : c);
                }
            }
            if (expanded.size() < width) {
                expanded.insert(expanded.begin(), width - expanded.size(), '0');
            } else if (expanded.size() > width) {
                expanded.erase(0, expanded.size() - width);
            }
            try {
                auto value = PackedLogic4::from_msb_string(expanded);
                auto domain = frontend::ValueDomain::Bit2;
                for (std::size_t bit = 0; bit < value.width(); ++bit) {
                    if (value.get(bit) != Logic4::zero
                        && value.get(bit) != Logic4::one) {
                        domain = frontend::ValueDomain::Logic4;
                        break;
                    }
                }
                return LoweredLiteral{std::move(value), domain};
            } catch (const std::invalid_argument&) {
                return std::nullopt;
            }
        }
        if (base == 'd') {
            const auto value = unsigned_decimal(digits);
            return value
                ? std::optional{LoweredLiteral{
                      unsigned_value(*value, width),
                      frontend::ValueDomain::Bit2}}
                : std::nullopt;
        }
        if (base == 'h') {
            std::uint64_t value{};
            std::string cleaned;
            for (const char c : digits) {
                if (c != '_') {
                    cleaned.push_back(c);
                }
            }
            const auto parsed =
                std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != cleaned.data() + cleaned.size()) {
                return std::nullopt;
            }
            return LoweredLiteral{
                unsigned_value(value, width),
                frontend::ValueDomain::Bit2};
        }
        return std::nullopt;
    }

    const auto value = unsigned_decimal(text);
    return value
        ? std::optional{LoweredLiteral{
              unsigned_value(*value, expected_width),
              frontend::ValueDomain::Bit2}}
        : std::nullopt;
}

using ConstantDomainEnvironment =
    std::unordered_map<std::string, frontend::ValueDomain>;

std::optional<std::int64_t> constant_literal_integer(
    const Expression& expression,
    std::string& error) {
    if (expression.kind == ExpressionKind::BooleanLiteral) {
        if (expression.text == "true") {
            return 1;
        }
        if (expression.text == "false") {
            return 0;
        }
        error = "Boolean literal is malformed";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::IntegerLiteral
        && expression.text.find('\'') == std::string::npos) {
        const auto value = unsigned_decimal(expression.text);
        if (!value
            || *value
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            error = "decimal literal is outside fsim's signed 64-bit "
                    "constant range";
            return std::nullopt;
        }
        return static_cast<std::int64_t>(*value);
    }
    if (expression.kind != ExpressionKind::LogicLiteral) {
        error = "constant expression contains a non-integral literal";
        return std::nullopt;
    }
    const auto lowered = literal_value(
        expression, 64, frontend::Language::SystemVerilog2017);
    if (!lowered || lowered->value.width() > 64) {
        error = "literal is malformed or wider than 64 bits";
        return std::nullopt;
    }
    std::uint64_t bits = 0;
    for (std::size_t bit = 0; bit < lowered->value.width(); ++bit) {
        const auto value = lowered->value.get(bit);
        if (value == Logic4::x || value == Logic4::z) {
            error = "X and Z digits are not valid in an elaboration "
                    "constant";
            return std::nullopt;
        }
        if (value == Logic4::one) {
            bits |= std::uint64_t{1} << bit;
        }
    }
    const auto quote = expression.text.find('\'');
    const bool is_signed =
        quote != std::string::npos
        && quote + 1 < expression.text.size()
        && (expression.text[quote + 1] == 's'
            || expression.text[quote + 1] == 'S');
    const auto width = lowered->value.width();
    if (is_signed && width != 0
        && lowered->value.get(width - 1) == Logic4::one) {
        if (width < 64) {
            bits |= ~std::uint64_t{0} << width;
        }
        const auto magnitude = (~bits) + 1U;
        if (magnitude
            == (std::uint64_t{1} << 63U)) {
            return std::numeric_limits<std::int64_t>::min();
        }
        if (magnitude
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            error = "signed literal is outside fsim's signed 64-bit "
                    "constant range";
            return std::nullopt;
        }
        return -static_cast<std::int64_t>(magnitude);
    }
    if (bits
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        error = "unsigned literal is outside fsim's signed 64-bit "
                "constant range";
        return std::nullopt;
    }
    return static_cast<std::int64_t>(bits);
}

bool checked_add(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) {
    if ((right > 0
         && left
             > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0
            && left
                < std::numeric_limits<std::int64_t>::min() - right)) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_subtract(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) {
    if ((right < 0
         && left
             > std::numeric_limits<std::int64_t>::max() + right)
        || (right > 0
            && left
                < std::numeric_limits<std::int64_t>::min() + right)) {
        return false;
    }
    result = left - right;
    return true;
}

bool checked_multiply(
    const std::int64_t left,
    const std::int64_t right,
    std::int64_t& result) {
    if (left == 0 || right == 0) {
        result = 0;
        return true;
    }
    if ((left == -1
         && right == std::numeric_limits<std::int64_t>::min())
        || (right == -1
            && left == std::numeric_limits<std::int64_t>::min())) {
        return false;
    }
    if (left > 0) {
        if ((right > 0
             && left
                 > std::numeric_limits<std::int64_t>::max() / right)
            || (right < 0
                && right
                    < std::numeric_limits<std::int64_t>::min() / left)) {
            return false;
        }
    } else if (
        (right > 0
         && left
             < std::numeric_limits<std::int64_t>::min() / right)
        || (right < 0
            && left
                < std::numeric_limits<std::int64_t>::max() / right)) {
        return false;
    }
    result = left * right;
    return true;
}

bool checked_power(
    const std::int64_t base,
    const std::int64_t exponent,
    std::int64_t& result) {
    if (exponent < 0) {
        if (base == 0) {
            return false;
        }
        if (base == 1) {
            result = 1;
        } else if (base == -1) {
            result = (exponent & 1) != 0 ? -1 : 1;
        } else {
            result = 0;
        }
        return true;
    }
    result = 1;
    auto factor = base;
    auto remaining = static_cast<std::uint64_t>(exponent);
    while (remaining != 0) {
        if ((remaining & 1U) != 0) {
            if (!checked_multiply(result, factor, result)) {
                return false;
            }
        }
        remaining >>= 1U;
        if (remaining != 0
            && !checked_multiply(factor, factor, factor)) {
            return false;
        }
    }
    return true;
}

std::optional<std::int64_t> evaluate_constant_expression(
    const Expression& expression,
    const ConstantEnvironment& environment,
    std::string& error) {
    if (expression.kind == ExpressionKind::IntegerLiteral
        || expression.kind == ExpressionKind::LogicLiteral
        || expression.kind == ExpressionKind::BooleanLiteral) {
        return constant_literal_integer(expression, error);
    }
    if (expression.kind == ExpressionKind::Identifier) {
        const auto found = environment.find(expression.text);
        if (found == environment.end()) {
            error =
                "unknown or forward parameter reference '"
                + expression.text + "'";
            return std::nullopt;
        }
        return found->second;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1) {
        const auto operand = evaluate_constant_expression(
            expression.operands.front(), environment, error);
        if (!operand) {
            return std::nullopt;
        }
        if (expression.text == "+") {
            return operand;
        }
        if (expression.text == "-") {
            if (*operand
                == std::numeric_limits<std::int64_t>::min()) {
                error = "constant unary negation overflows signed 64-bit "
                        "range";
                return std::nullopt;
            }
            return -*operand;
        }
        if (expression.text == "~") {
            return ~*operand;
        }
        if (expression.text == "!") {
            return *operand == 0 ? 1 : 0;
        }
        error =
            "unsupported unary constant operator '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$clog2") {
        if (expression.operands.size() != 1) {
            error = "$clog2 requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_constant_expression(
            expression.operands.front(), environment, error);
        if (!operand) {
            return std::nullopt;
        }
        if (*operand < 0) {
            error =
                "$clog2 requires a nonnegative integral argument in "
                "the current executable slice";
            return std::nullopt;
        }
        auto magnitude = static_cast<std::uint64_t>(*operand);
        std::int64_t result = 0;
        if (magnitude > 1) {
            --magnitude;
            while (magnitude != 0) {
                ++result;
                magnitude >>= 1U;
            }
        }
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$signed"
            || expression.text == "$unsigned")) {
        if (expression.operands.size() != 1) {
            error =
                expression.text + " requires exactly one argument";
            return std::nullopt;
        }
        return evaluate_constant_expression(
            expression.operands.front(), environment, error);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunknown") {
        if (expression.operands.size() != 1) {
            error = "$isunknown requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_constant_expression(
            expression.operands.front(), environment, error);
        if (!operand) {
            return std::nullopt;
        }
        return 0;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3) {
        const auto condition = evaluate_constant_expression(
            expression.operands[0], environment, error);
        if (!condition) {
            return std::nullopt;
        }
        return evaluate_constant_expression(
            expression.operands[*condition != 0 ? 1 : 2],
            environment,
            error);
    }
    if (expression.kind != ExpressionKind::Binary
        || expression.operands.size() != 2) {
        error = "expression form is not an integral constant expression";
        return std::nullopt;
    }
    const auto left = evaluate_constant_expression(
        expression.operands[0], environment, error);
    if (!left) {
        return std::nullopt;
    }
    if (expression.text == "&&" && *left == 0) {
        return 0;
    }
    if (expression.text == "||" && *left != 0) {
        return 1;
    }
    const auto right = evaluate_constant_expression(
        expression.operands[1], environment, error);
    if (!right) {
        return std::nullopt;
    }
    std::int64_t result = 0;
    if (expression.text == "+") {
        if (!checked_add(*left, *right, result)) {
            error = "constant addition overflows signed 64-bit range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "-") {
        if (!checked_subtract(*left, *right, result)) {
            error = "constant subtraction overflows signed 64-bit range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "*") {
        if (!checked_multiply(*left, *right, result)) {
            error = "constant multiplication overflows signed 64-bit range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "**") {
        if (!checked_power(*left, *right, result)) {
            error =
                *left == 0 && *right < 0
                    ? "constant zero to a negative power is undefined"
                    : "constant exponentiation overflows signed 64-bit "
                      "range";
            return std::nullopt;
        }
        return result;
    }
    if (expression.text == "/" || expression.text == "%") {
        if (*right == 0) {
            error = "constant division by zero";
            return std::nullopt;
        }
        if (*left == std::numeric_limits<std::int64_t>::min()
            && *right == -1) {
            error = "constant division overflows signed 64-bit range";
            return std::nullopt;
        }
        return expression.text == "/"
            ? *left / *right
            : *left % *right;
    }
    if (expression.text == "<<"
        || expression.text == ">>"
        || expression.text == "<<<"
        || expression.text == ">>>"
        || expression.text == "sll"
        || expression.text == "srl"
        || expression.text == "sra") {
        if (*left < 0 || *right < 0 || *right >= 63) {
            error = "constant shifts require a nonnegative value and an "
                    "amount from 0 through 62";
            return std::nullopt;
        }
        if (expression.text == "<<"
            || expression.text == "<<<"
            || expression.text == "sll") {
            if (*left
                > (std::numeric_limits<std::int64_t>::max()
                   >> static_cast<unsigned>(*right))) {
                error = "constant left shift overflows signed 64-bit range";
                return std::nullopt;
            }
            return *left << static_cast<unsigned>(*right);
        }
        return *left >> static_cast<unsigned>(*right);
    }
    if (expression.text == "&") {
        return *left & *right;
    }
    if (expression.text == "|") {
        return *left | *right;
    }
    if (expression.text == "^") {
        return *left ^ *right;
    }
    if (expression.text == "~^" || expression.text == "^~") {
        return ~(*left ^ *right);
    }
    if (expression.text == "&&") {
        return *right != 0 ? 1 : 0;
    }
    if (expression.text == "||") {
        return *right != 0 ? 1 : 0;
    }
    if (expression.text == "==" || expression.text == "==="
        || expression.text == "=") {
        return *left == *right ? 1 : 0;
    }
    if (expression.text == "!=" || expression.text == "!=="
        || expression.text == "/=") {
        return *left != *right ? 1 : 0;
    }
    if (expression.text == "<") {
        return *left < *right ? 1 : 0;
    }
    if (expression.text == "<=") {
        return *left <= *right ? 1 : 0;
    }
    if (expression.text == ">") {
        return *left > *right ? 1 : 0;
    }
    if (expression.text == ">=") {
        return *left >= *right ? 1 : 0;
    }
    error =
        "unsupported binary constant operator '" + expression.text + "'";
    return std::nullopt;
}

Expression constant_expression(
    const std::int64_t value,
    const frontend::SourceSpan& span,
    const frontend::ValueDomain domain,
    const frontend::Language language) {
    if (domain == frontend::ValueDomain::Boolean) {
        return {
            ExpressionKind::BooleanLiteral,
            value == 0 ? "false" : "true",
            {},
            span};
    }
    if (domain == frontend::ValueDomain::Bit2
        && language == frontend::Language::Vhdl2008) {
        return {
            ExpressionKind::LogicLiteral,
            value == 0 ? "'0'" : "'1'",
            {},
            span};
    }
    if (value >= 0) {
        return {
            ExpressionKind::IntegerLiteral,
            std::to_string(value),
            {},
            span};
    }
    const auto magnitude =
        value == std::numeric_limits<std::int64_t>::min()
            ? std::uint64_t{1} << 63U
            : static_cast<std::uint64_t>(-value);
    return {
        ExpressionKind::Unary,
        "-",
        {
            Expression{
                ExpressionKind::IntegerLiteral,
                std::to_string(magnitude),
                {},
                span}},
        span};
}

void substitute_parameters(
    Expression& expression,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language) {
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            const auto domain = domains.find(expression.text);
            expression = constant_expression(
                found->second,
                expression.span,
                domain == domains.end()
                    ? frontend::ValueDomain::Integer
                    : domain->second,
                language);
            return;
        }
    }
    for (auto& operand : expression.operands) {
        substitute_parameters(
            operand, environment, domains, language);
    }
}

void substitute_parameters(
    frontend::Type& type,
    const ConstantEnvironment& environment,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    if (!type.packed_members.empty()) {
        const bool is_union =
            type.packed_aggregate
            == frontend::PackedAggregateKind::Union;
        std::uint64_t total_width = 0;
        std::optional<std::uint64_t> union_width;
        bool valid = true;
        for (auto& member : type.packed_members) {
            if (member.packed_range_expression) {
                std::string error;
                const auto left = evaluate_constant_expression(
                    member.packed_range_expression->left,
                    environment,
                    error);
                const auto right = left
                    ? evaluate_constant_expression(
                          member.packed_range_expression->right,
                          environment,
                          error)
                    : std::nullopt;
                if (!left || !right) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVSTRUCT-001",
                        "cannot evaluate packed struct member range: "
                            + error,
                        member.packed_range_expression->span});
                    valid = false;
                    continue;
                }
                member.packed_range = frontend::PackedRange{
                    *left, *right, *left >= *right};
                member.packed_range_expression.reset();
            }
            const auto width = member.width();
            if (!width || *width == 0) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVSTRUCT-001",
                    "packed aggregate member '" + member.name
                        + "' has an invalid or overflowing width",
                    member.span});
                valid = false;
                continue;
            }
            if (is_union) {
                if (union_width && *union_width != *width) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVUNION-001",
                        "packed union member '" + member.name
                            + "' has width "
                            + std::to_string(*width)
                            + " but every member must have width "
                            + std::to_string(*union_width),
                        member.span});
                    valid = false;
                    continue;
                }
                union_width = *width;
                total_width = *width;
            } else {
                if (*width
                    > std::numeric_limits<std::uint64_t>::max()
                        - total_width) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVSTRUCT-001",
                        "packed struct member '" + member.name
                            + "' overflows the aggregate width",
                        member.span});
                    valid = false;
                    continue;
                }
                total_width += *width;
            }
        }
        if (!valid || total_width == 0
            || total_width - 1U
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            if (valid) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVSTRUCT-001",
                    "packed aggregate total width exceeds the supported range",
                    type.packed_members.front().span});
            }
            type.packed_range.reset();
            return;
        }
        if (!is_union) {
            auto offset = total_width;
            for (auto& member : type.packed_members) {
                offset -= *member.width();
                member.lsb_offset = offset;
            }
        } else {
            for (auto& member : type.packed_members) {
                member.lsb_offset = 0;
            }
        }
        type.packed_range = frontend::PackedRange{
            static_cast<std::int64_t>(total_width - 1U),
            0,
            true};
        type.packed_range_expression.reset();
        return;
    }
    if (!type.packed_range_expression) {
        return;
    }
    std::string error;
    const auto left = evaluate_constant_expression(
        type.packed_range_expression->left, environment, error);
    const auto right = left
        ? evaluate_constant_expression(
              type.packed_range_expression->right,
              environment,
              error)
        : std::nullopt;
    if (!left || !right) {
        diagnostics.push_back({
            language == frontend::Language::Vhdl2008
                ? "FSIM-ELAB-GENERIC-006"
                : "FSIM-ELAB-PARAM-006",
            "cannot evaluate packed range: " + error,
            type.packed_range_expression->span});
        return;
    }
    const frontend::PackedRange range{
        *left,
        *right,
        type.packed_range_expression->descending.value_or(
            *left >= *right)};
    if (range.width() == 0) {
        diagnostics.push_back({
            language == frontend::Language::Vhdl2008
                ? "FSIM-ELAB-GENERIC-007"
                : "FSIM-ELAB-PARAM-007",
            "packed range width overflows fsim's 64-bit range",
            type.packed_range_expression->span});
        return;
    }
    type.packed_range = range;
    type.packed_range_expression.reset();
}

void substitute_parameters(
    frontend::VariableDeclaration& declaration,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    substitute_parameters(
        declaration.type, environment, diagnostics, language);
    if (declaration.initializer) {
        substitute_parameters(
            *declaration.initializer,
            environment,
            domains,
            language);
    }
}

void substitute_parameters(
    std::vector<Statement>& statements,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    for (auto& statement : statements) {
        substitute_parameters(
            statement.target, environment, domains, language);
        substitute_parameters(
            statement.value, environment, domains, language);
        substitute_parameters(
            statement.condition, environment, domains, language);
        substitute_parameters(
            statement.loop_initial, environment, domains, language);
        substitute_parameters(
            statement.loop_limit, environment, domains, language);
        for (auto& declaration : statement.declarations) {
            substitute_parameters(
                declaration,
                environment,
                domains,
                diagnostics,
                language);
        }
        for (auto& alternative : statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_parameters(
                    choice, environment, domains, language);
            }
            substitute_parameters(
                alternative.statements,
                environment,
                domains,
                diagnostics,
                language);
        }
        auto statement_environment = environment;
        auto statement_domains = domains;
        if (statement.kind == StatementKind::Loop) {
            statement_environment.erase(statement.loop_variable);
            statement_domains.erase(statement.loop_variable);
        }
        substitute_parameters(
            statement.statements,
            statement_environment,
            statement_domains,
            diagnostics,
            language);
        substitute_parameters(
            statement.else_statements,
            environment,
            domains,
            diagnostics,
            language);
    }
}

void substitute_parameters(
    std::vector<frontend::Instance>& instances,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language) {
    for (auto& instance : instances) {
        for (auto& override : instance.parameter_overrides) {
            substitute_parameters(
                override.value, environment, domains, language);
        }
        for (auto& connection : instance.connections) {
            substitute_parameters(
                connection.value, environment, domains, language);
        }
    }
}

void substitute_parameters(
    std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    frontend::Language language,
    std::vector<Diagnostic>& diagnostics);

void substitute_parameters(
    frontend::GenerateBody& body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& constant : body.constants) {
        substitute_parameters(
            constant.type, environment, diagnostics, language);
        substitute_parameters(
            constant.default_value,
            environment,
            domains,
            language);
    }
    for (auto& signal : body.signals) {
        substitute_parameters(
            signal.type, environment, diagnostics, language);
    }
    substitute_parameters(
        body.concurrent_statements,
        environment,
        domains,
        diagnostics,
        language);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_parameters(
            process.statements,
            environment,
            domains,
            diagnostics,
            language);
    }
    substitute_parameters(
        body.instances, environment, domains, language);
    substitute_parameters(
        body.generate_regions,
        environment,
        domains,
        language,
        diagnostics);
}

void substitute_parameters(
    std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& generate : generates) {
        substitute_parameters(
            generate.initial, environment, domains, language);
        auto body_environment = environment;
        auto body_domains = domains;
        if (generate.kind == frontend::GenerateKind::Iterative) {
            body_environment.erase(generate.variable);
            body_domains.erase(generate.variable);
        }
        substitute_parameters(
            generate.condition,
            body_environment,
            body_domains,
            language);
        substitute_parameters(
            generate.iteration,
            body_environment,
            body_domains,
            language);
        substitute_parameters(
            generate.then_body,
            body_environment,
            body_domains,
            language,
            diagnostics);
        substitute_parameters(
            generate.else_body,
            body_environment,
            body_domains,
            language,
            diagnostics);
        for (auto& alternative : generate.alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_parameters(
                    choice.left,
                    body_environment,
                    body_domains,
                    language);
                if (choice.right) {
                    substitute_parameters(
                        *choice.right,
                        body_environment,
                        body_domains,
                        language);
                }
            }
            substitute_parameters(
                alternative.body,
                body_environment,
                body_domains,
                language,
                diagnostics);
        }
    }
}

using QualifiedIdentifierMap =
    std::unordered_map<std::string, frontend::SourceSpan>;

struct SystemVerilogTypeBinding {
    frontend::Type type;
    std::string owner;
};

using SystemVerilogTypeEnvironment =
    std::unordered_map<std::string, SystemVerilogTypeBinding>;

void collect_qualified_identifiers(
    const Expression& expression,
    QualifiedIdentifierMap& identifiers) {
    if (expression.kind == ExpressionKind::Identifier
        && (expression.text.find('.') != std::string::npos
            || expression.text.find("::")
                != std::string::npos)) {
        identifiers.try_emplace(
            expression.text, expression.span);
    }
    for (const auto& operand : expression.operands) {
        collect_qualified_identifiers(operand, identifiers);
    }
}

void collect_qualified_identifiers(
    const frontend::Type& type,
    QualifiedIdentifierMap& identifiers) {
    if (!type.named_type.empty()
        && type.named_type.find("::") != std::string::npos) {
        identifiers.try_emplace(
            type.named_type, type.named_type_span);
    }
    for (const auto& member : type.packed_members) {
        if (!member.packed_range_expression) {
            continue;
        }
        collect_qualified_identifiers(
            member.packed_range_expression->left,
            identifiers);
        collect_qualified_identifiers(
            member.packed_range_expression->right,
            identifiers);
    }
    if (!type.packed_range_expression) {
        return;
    }
    collect_qualified_identifiers(
        type.packed_range_expression->left, identifiers);
    collect_qualified_identifiers(
        type.packed_range_expression->right, identifiers);
}

void collect_qualified_identifiers(
    const std::vector<Statement>& statements,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& statement : statements) {
        collect_qualified_identifiers(
            statement.target, identifiers);
        collect_qualified_identifiers(
            statement.value, identifiers);
        collect_qualified_identifiers(
            statement.condition, identifiers);
        for (const auto& declaration : statement.declarations) {
            collect_qualified_identifiers(
                declaration.type, identifiers);
            if (declaration.initializer) {
                collect_qualified_identifiers(
                    *declaration.initializer, identifiers);
            }
        }
        for (const auto& alternative :
             statement.case_alternatives) {
            for (const auto& choice : alternative.choices) {
                collect_qualified_identifiers(
                    choice, identifiers);
            }
            collect_qualified_identifiers(
                alternative.statements, identifiers);
        }
        collect_qualified_identifiers(
            statement.statements, identifiers);
        collect_qualified_identifiers(
            statement.else_statements, identifiers);
    }
}

void collect_qualified_identifiers(
    const std::vector<frontend::GenerateRegion>& generates,
    QualifiedIdentifierMap& identifiers);

void collect_qualified_identifiers(
    const frontend::GenerateBody& body,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& constant : body.constants) {
        collect_qualified_identifiers(
            constant.type, identifiers);
        collect_qualified_identifiers(
            constant.default_value, identifiers);
    }
    for (const auto& signal : body.signals) {
        collect_qualified_identifiers(signal.type, identifiers);
    }
    collect_qualified_identifiers(
        body.concurrent_statements, identifiers);
    for (const auto& process : body.processes) {
        for (const auto& variable : process.variables) {
            collect_qualified_identifiers(
                variable.type, identifiers);
            if (variable.initializer) {
                collect_qualified_identifiers(
                    *variable.initializer, identifiers);
            }
        }
        collect_qualified_identifiers(
            process.statements, identifiers);
    }
    for (const auto& instance : body.instances) {
        for (const auto& override :
             instance.parameter_overrides) {
            collect_qualified_identifiers(
                override.value, identifiers);
        }
        for (const auto& connection : instance.connections) {
            collect_qualified_identifiers(
                connection.value, identifiers);
        }
    }
    collect_qualified_identifiers(
        body.generate_regions, identifiers);
}

void collect_qualified_identifiers(
    const std::vector<frontend::GenerateRegion>& generates,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& generate : generates) {
        collect_qualified_identifiers(
            generate.initial, identifiers);
        collect_qualified_identifiers(
            generate.condition, identifiers);
        collect_qualified_identifiers(
            generate.iteration, identifiers);
        collect_qualified_identifiers(
            generate.then_body, identifiers);
        collect_qualified_identifiers(
            generate.else_body, identifiers);
        for (const auto& alternative :
             generate.alternatives) {
            for (const auto& choice : alternative.choices) {
                collect_qualified_identifiers(
                    choice.left, identifiers);
                if (choice.right) {
                    collect_qualified_identifiers(
                        *choice.right, identifiers);
                }
            }
            collect_qualified_identifiers(
                alternative.body, identifiers);
        }
    }
}

QualifiedIdentifierMap qualified_identifiers(
    const DesignUnit& unit) {
    QualifiedIdentifierMap result;
    for (const auto& alias : unit.type_aliases) {
        collect_qualified_identifiers(alias.type, result);
    }
    for (const auto& parameter : unit.parameters) {
        collect_qualified_identifiers(parameter.type, result);
        collect_qualified_identifiers(
            parameter.default_value, result);
    }
    for (const auto& port : unit.ports) {
        collect_qualified_identifiers(port.type, result);
    }
    for (const auto& signal : unit.signals) {
        collect_qualified_identifiers(signal.type, result);
    }
    collect_qualified_identifiers(
        unit.concurrent_statements, result);
    for (const auto& process : unit.processes) {
        for (const auto& variable : process.variables) {
            collect_qualified_identifiers(variable.type, result);
            if (variable.initializer) {
                collect_qualified_identifiers(
                    *variable.initializer, result);
            }
        }
        collect_qualified_identifiers(process.statements, result);
    }
    for (const auto& instance : unit.instances) {
        for (const auto& override :
             instance.parameter_overrides) {
            collect_qualified_identifiers(
                override.value, result);
        }
        for (const auto& connection : instance.connections) {
            collect_qualified_identifiers(
                connection.value, result);
        }
    }
    collect_qualified_identifiers(unit.generate_regions, result);
    return result;
}

std::string generated_scope(
    const std::string_view parent_scope,
    const std::string_view local_scope) {
    return parent_scope.empty()
        ? std::string{local_scope}
        : std::string{parent_scope} + "." + std::string{local_scope};
}

using GeneratedNameEnvironment =
    std::unordered_map<std::string, std::string>;

void qualify_generated_expression(
    Expression& expression,
    const GeneratedNameEnvironment& names) {
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = names.find(expression.text);
            found != names.end()) {
            expression.text = found->second;
        }
    }
    for (auto& operand : expression.operands) {
        qualify_generated_expression(operand, names);
    }
}

void qualify_generated_statements(
    std::vector<Statement>& statements,
    const GeneratedNameEnvironment& names);

void qualify_generated_statement(
    Statement& statement,
    const GeneratedNameEnvironment& names) {
    auto body_names = names;
    for (auto& declaration : statement.declarations) {
        if (declaration.initializer) {
            qualify_generated_expression(
                *declaration.initializer, body_names);
        }
        body_names.erase(declaration.name);
    }
    qualify_generated_expression(statement.target, body_names);
    qualify_generated_expression(statement.value, body_names);
    qualify_generated_expression(statement.condition, body_names);
    for (auto& sensitivity : statement.sensitivities) {
        if (const auto found = body_names.find(sensitivity.signal);
            found != body_names.end()) {
            sensitivity.signal = found->second;
        }
    }
    for (auto& alternative : statement.case_alternatives) {
        for (auto& choice : alternative.choices) {
            qualify_generated_expression(choice, body_names);
        }
        qualify_generated_statements(
            alternative.statements, body_names);
    }
    qualify_generated_statements(statement.statements, body_names);
    qualify_generated_statements(
        statement.else_statements, body_names);
}

void qualify_generated_statements(
    std::vector<Statement>& statements,
    const GeneratedNameEnvironment& names) {
    for (auto& statement : statements) {
        qualify_generated_statement(statement, names);
    }
}

void qualify_generated_process(
    frontend::Process& process,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    process.name = process.name.empty()
        ? std::string{scope}
        : generated_scope(scope, process.name);
    auto process_names = names;
    for (auto& variable : process.variables) {
        if (variable.initializer) {
            qualify_generated_expression(
                *variable.initializer, process_names);
        }
        process_names.erase(variable.name);
    }
    for (auto& sensitivity : process.sensitivities) {
        if (const auto found = process_names.find(sensitivity.signal);
            found != process_names.end()) {
            sensitivity.signal = found->second;
        }
    }
    qualify_generated_statements(process.statements, process_names);
}

void qualify_generated_instance(
    frontend::Instance& instance,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    instance.name = generated_scope(scope, instance.name);
    for (auto& override : instance.parameter_overrides) {
        qualify_generated_expression(override.value, names);
    }
    for (auto& connection : instance.connections) {
        qualify_generated_expression(connection.value, names);
    }
}

void expand_generate_regions(
    const std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    frontend::Language language,
    std::string_view parent_scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics);

void evaluate_generated_constants(
    frontend::GenerateBody& body,
    ConstantEnvironment& environment,
    ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& constant : body.constants) {
        substitute_parameters(
            constant.type, environment, diagnostics, language);
        substitute_parameters(
            constant.default_value,
            environment,
            domains,
            language);
        std::string error;
        auto value = evaluate_constant_expression(
            constant.default_value, environment, error);
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-011",
                "cannot evaluate generated "
                    + std::string{
                        language == frontend::Language::Vhdl2008
                            ? "constant"
                            : "parameter"}
                    + " '" + constant.name + "': " + error,
                constant.span});
            value = 0;
        }
        const bool exceeds_word =
            constant.type.packed_range
            && constant.type.packed_range->width() > 64;
        if (exceeds_word) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-012",
                "generated "
                    + std::string{
                        language == frontend::Language::Vhdl2008
                            ? "constant"
                            : "parameter"}
                    + " '" + constant.name
                    + "' exceeds the bounded 64-bit integral width",
                constant.span});
        } else if (language == frontend::Language::Vhdl2008) {
            const auto& spelling = constant.type.spelling;
            const bool violates_natural =
                spelling == "natural" && *value < 0;
            const bool violates_positive =
                spelling == "positive" && *value <= 0;
            const bool violates_boolean =
                constant.type.domain == frontend::ValueDomain::Boolean
                && *value != 0 && *value != 1;
            const bool violates_bit =
                constant.type.domain == frontend::ValueDomain::Bit2
                && *value != 0 && *value != 1;
            if (violates_natural || violates_positive
                || violates_boolean || violates_bit) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-012",
                    "generated constant '" + constant.name
                        + "' value is outside subtype '"
                        + constant.type.spelling + "'",
                    constant.span});
            }
        }
        environment[constant.name] = *value;
        domains[constant.name] = constant.type.domain;
    }
}

void append_generated_body(
    frontend::GenerateBody body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::string_view scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics) {
    auto body_environment = environment;
    auto body_domains = domains;
    evaluate_generated_constants(
        body,
        body_environment,
        body_domains,
        language,
        diagnostics);
    substitute_parameters(
        body,
        body_environment,
        body_domains,
        language,
        diagnostics);
    auto body_names = visible_names;
    for (auto& signal : body.signals) {
        const auto local_name = signal.name;
        signal.name = generated_scope(scope, local_name);
        body_names[local_name] = signal.name;
        unit.signals.push_back(std::move(signal));
    }
    for (auto& statement : body.concurrent_statements) {
        qualify_generated_statement(statement, body_names);
        unit.concurrent_statements.push_back(std::move(statement));
    }
    for (auto& process : body.processes) {
        qualify_generated_process(process, body_names, scope);
        unit.processes.push_back(std::move(process));
    }
    for (auto& instance : body.instances) {
        qualify_generated_instance(instance, body_names, scope);
        unit.instances.push_back(std::move(instance));
    }
    expand_generate_regions(
        body.generate_regions,
        body_environment,
        body_domains,
        language,
        scope,
        body_names,
        unit,
        diagnostics);
}

void expand_generate_regions(
    const std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::string_view parent_scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics) {
    for (const auto& generate : generates) {
        if (generate.kind == frontend::GenerateKind::StaticBlock) {
            append_generated_body(
                generate.then_body,
                environment,
                domains,
                language,
                generated_scope(
                    parent_scope, generate.then_scope),
                visible_names,
                unit,
                diagnostics);
            continue;
        }
        if (generate.kind == frontend::GenerateKind::Selection) {
            std::string error;
            const auto selector = evaluate_constant_expression(
                generate.condition, environment, error);
            if (!selector) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-008",
                    "cannot evaluate selection-generate expression: "
                        + error,
                    generate.condition.span});
                continue;
            }
            const frontend::GenerateAlternative* selected = nullptr;
            const frontend::GenerateAlternative* default_alternative =
                nullptr;
            bool invalid = false;
            struct ChoiceInterval {
                std::int64_t lower;
                std::int64_t upper;
            };
            std::vector<ChoiceInterval> choice_intervals;
            for (const auto& alternative : generate.alternatives) {
                if (alternative.is_default) {
                    if (default_alternative != nullptr) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-010",
                            "selection generate has more than one "
                            "default alternative",
                            alternative.span});
                        invalid = true;
                    } else {
                        default_alternative = &alternative;
                    }
                    continue;
                }
                bool alternative_matches = false;
                for (const auto& choice : alternative.choices) {
                    error.clear();
                    const auto left = evaluate_constant_expression(
                        choice.left, environment, error);
                    if (!left) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-009",
                            "cannot evaluate selection-generate choice: "
                                + error,
                            choice.span});
                        invalid = true;
                        continue;
                    }
                    auto right = left;
                    if (choice.right) {
                        error.clear();
                        right = evaluate_constant_expression(
                            *choice.right, environment, error);
                        if (!right) {
                            diagnostics.push_back({
                                "FSIM-ELAB-GEN-009",
                                "cannot evaluate selection-generate range "
                                "bound: " + error,
                                choice.span});
                            invalid = true;
                            continue;
                        }
                    }
                    const bool empty_range =
                        choice.right
                        && (choice.descending
                                ? *left < *right
                                : *left > *right);
                    if (empty_range) {
                        continue;
                    }
                    const ChoiceInterval interval{
                        std::min(*left, *right),
                        std::max(*left, *right)};
                    const bool overlaps =
                        std::any_of(
                            choice_intervals.begin(),
                            choice_intervals.end(),
                            [&](const ChoiceInterval& existing) {
                                return interval.lower
                                           <= existing.upper
                                    && existing.lower
                                           <= interval.upper;
                            });
                    if (overlaps) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-010",
                            "selection generate has overlapping "
                            "constant choices or ranges",
                            choice.span});
                        invalid = true;
                    } else {
                        choice_intervals.push_back(interval);
                    }
                    alternative_matches =
                        alternative_matches
                        || (interval.lower <= *selector
                            && *selector <= interval.upper);
                }
                if (alternative_matches) {
                    if (selected != nullptr) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-010",
                            "selection generate has overlapping matching "
                            "alternatives",
                            alternative.span});
                        invalid = true;
                    } else {
                        selected = &alternative;
                    }
                }
            }
            if (invalid) {
                continue;
            }
            if (selected == nullptr) {
                selected = default_alternative;
            }
            if (selected != nullptr) {
                append_generated_body(
                    selected->body,
                    environment,
                    domains,
                    language,
                    generated_scope(
                        parent_scope, selected->scope),
                    visible_names,
                    unit,
                    diagnostics);
            }
            continue;
        }
        if (generate.kind == frontend::GenerateKind::Iterative) {
            if (environment.contains(generate.variable)) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-007",
                    "nested loop-generate variable '"
                        + generate.variable
                        + "' shadows an enclosing constant; this "
                          "bounded slice requires a distinct name",
                    generate.span});
                continue;
            }
            std::string error;
            const auto initial = evaluate_constant_expression(
                generate.initial, environment, error);
            if (!initial) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-002",
                    "cannot evaluate loop-generate initial value: "
                        + error,
                    generate.initial.span});
                continue;
            }
            auto iteration_environment = environment;
            auto iteration_domains = domains;
            iteration_domains[generate.variable] =
                frontend::ValueDomain::Integer;
            std::int64_t value = *initial;
            constexpr std::size_t maximum_iterations = 1'000'000;
            std::size_t count = 0;
            while (true) {
                iteration_environment[generate.variable] = value;
                error.clear();
                const auto condition = evaluate_constant_expression(
                    generate.condition,
                    iteration_environment,
                    error);
                if (!condition) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-003",
                        "cannot evaluate loop-generate condition: "
                            + error,
                        generate.condition.span});
                    break;
                }
                if (*condition == 0) {
                    break;
                }
                if (count++ == maximum_iterations) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-004",
                        "loop generate exceeds the bounded "
                        "1,000,000-iteration elaboration limit",
                        generate.span});
                    break;
                }
                auto body = generate.then_body;
                substitute_parameters(
                    body,
                    iteration_environment,
                    iteration_domains,
                    language,
                    diagnostics);
                const auto indexed_scope =
                    generate.then_scope + "["
                    + std::to_string(value) + "]";
                append_generated_body(
                    std::move(body),
                    iteration_environment,
                    iteration_domains,
                    language,
                    generated_scope(parent_scope, indexed_scope),
                    visible_names,
                    unit,
                    diagnostics);
                error.clear();
                const auto next = evaluate_constant_expression(
                    generate.iteration,
                    iteration_environment,
                    error);
                if (!next) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-005",
                        "cannot evaluate loop-generate iteration: "
                            + error,
                        generate.iteration.span});
                    break;
                }
                if (*next == value) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-006",
                        "loop-generate iteration does not advance",
                        generate.iteration.span});
                    break;
                }
                value = *next;
            }
            continue;
        }
        std::string error;
        const auto condition = evaluate_constant_expression(
            generate.condition, environment, error);
        if (!condition) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-001",
                "cannot evaluate conditional generate expression: "
                    + error,
                generate.condition.span});
            continue;
        }
        const bool selected_then = *condition != 0;
        const auto& selected_body =
            selected_then
            ? generate.then_body
            : generate.else_body;
        const auto& local_scope =
            selected_then
            ? generate.then_scope
            : generate.else_scope;
        append_generated_body(
            selected_body,
            environment,
            domains,
            language,
            generated_scope(parent_scope, local_scope),
            visible_names,
            unit,
            diagnostics);
    }
}

struct SpecializedUnit {
    DesignUnit unit;
    ConstantEnvironment environment;
    std::vector<std::pair<std::string, std::string>> values;
};

enum class SpecializationDiagnostic {
    invalid_actual,
    duplicate_actual,
    association_order,
    actual_evaluation,
    default_evaluation,
    subtype_constraint,
    ambiguous_name,
};

const char* specialization_diagnostic_code(
    const bool is_vhdl,
    const bool is_package,
    const bool is_systemverilog_package,
    const SpecializationDiagnostic diagnostic) {
    switch (diagnostic) {
    case SpecializationDiagnostic::invalid_actual:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-001"
            : "FSIM-ELAB-PARAM-001";
    case SpecializationDiagnostic::duplicate_actual:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-002"
            : "FSIM-ELAB-PARAM-002";
    case SpecializationDiagnostic::association_order:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-003"
            : "FSIM-ELAB-PARAM-003";
    case SpecializationDiagnostic::actual_evaluation:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-004"
            : "FSIM-ELAB-PARAM-004";
    case SpecializationDiagnostic::default_evaluation:
        if (is_package) {
            return "FSIM-ELAB-PKG-005";
        }
        if (is_systemverilog_package) {
            return "FSIM-ELAB-SVPKG-006";
        }
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-005"
            : "FSIM-ELAB-PARAM-005";
    case SpecializationDiagnostic::subtype_constraint:
        return is_package
            ? "FSIM-ELAB-PKG-006"
            : "FSIM-ELAB-GENERIC-008";
    case SpecializationDiagnostic::ambiguous_name:
        return "FSIM-ELAB-PARAM-009";
    }
    return is_vhdl
        ? "FSIM-ELAB-GENERIC-001"
        : "FSIM-ELAB-PARAM-001";
}

bool parameter_name_matches(
    const std::string_view formal,
    const std::string_view actual,
    const frontend::Language target_language,
    const frontend::Language association_language) {
    if (target_language != frontend::Language::Vhdl2008
        && association_language
            != frontend::Language::Vhdl2008) {
        return formal == actual;
    }
    if (formal.size() != actual.size()) {
        return false;
    }
    for (std::size_t index = 0; index < formal.size(); ++index) {
        const auto formal_character =
            static_cast<unsigned char>(formal[index]);
        const auto actual_character =
            static_cast<unsigned char>(actual[index]);
        if (std::tolower(formal_character)
            != std::tolower(actual_character)) {
            return false;
        }
    }
    return true;
}

SpecializedUnit specialize_unit(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
    SpecializedUnit result;
    result.unit = source;
    const bool is_vhdl =
        source.language == frontend::Language::Vhdl2008;
    const bool is_package =
        source.kind == frontend::UnitKind::VhdlPackage;
    const bool is_systemverilog_package =
        source.kind
        == frontend::UnitKind::SystemVerilogPackage;
    const bool is_verilog =
        source.language == frontend::Language::SystemVerilog2017
        || source.language == frontend::Language::Verilog2005;
    const bool association_is_vhdl =
        association_language == frontend::Language::Vhdl2008;
    const auto code = [&](const SpecializationDiagnostic diagnostic) {
        return specialization_diagnostic_code(
            is_vhdl,
            is_package,
            is_systemverilog_package,
            diagnostic);
    };
    const auto object_kind =
        (is_package || is_systemverilog_package)
            ? std::string_view{"package constant"}
        : is_vhdl ? std::string_view{"generic"}
                : std::string_view{"parameter"};
    if (!is_vhdl && !is_verilog) {
        if (!overrides.empty()) {
            diagnostics.push_back({
                code(SpecializationDiagnostic::invalid_actual),
                "generic or parameter actuals cannot target this design "
                "unit",
                overrides.front().span});
        }
        return result;
    }

    std::vector<const frontend::ParameterDeclaration*> overridable;
    for (const auto& parameter : source.parameters) {
        if (!parameter.local) {
            overridable.push_back(&parameter);
        }
    }
    std::vector<std::optional<std::int64_t>> actuals(
        overridable.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    bool saw_positional = false;
    for (const auto& override : overrides) {
        std::string error;
        const auto value = evaluate_constant_expression(
            override.value, parent_environment, error);
        if (!value) {
            diagnostics.push_back({
                code(SpecializationDiagnostic::actual_evaluation),
                "cannot evaluate " + std::string{object_kind}
                    + " actual: " + error,
                override.span});
            continue;
        }
        std::optional<std::size_t> actual_index;
        if (override.name) {
            saw_named = true;
            std::vector<
                const frontend::ParameterDeclaration*> matches;
            std::copy_if(
                overridable.begin(),
                overridable.end(),
                std::back_inserter(matches),
                [&](const frontend::ParameterDeclaration* parameter) {
                    return parameter_name_matches(
                        parameter->name,
                        *override.name,
                        source.language,
                        association_language);
                });
            if (matches.size() > 1) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::ambiguous_name),
                    "VHDL generic name '" + *override.name
                        + "' ambiguously matches multiple "
                        "case-sensitive target parameters",
                    override.span});
                continue;
            }
            if (matches.empty()) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    "unknown or local " + std::string{object_kind}
                        + " actual '" + *override.name + "'",
                    override.span});
                continue;
            }
            actual_index = static_cast<std::size_t>(
                std::distance(
                    overridable.begin(),
                    std::find(
                        overridable.begin(),
                        overridable.end(),
                        matches.front())));
        } else {
            saw_positional = true;
            if (association_is_vhdl && saw_named) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::association_order),
                    "a positional generic actual cannot follow a named "
                    "actual",
                    override.span});
            }
            if (next_positional >= overridable.size()) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    "too many positional "
                        + std::string{object_kind} + " actuals",
                    override.span});
                continue;
            }
            actual_index = next_positional++;
        }
        if (actual_index) {
            if (actuals[*actual_index]) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::duplicate_actual),
                    "duplicate " + std::string{object_kind}
                        + " actual for '"
                        + overridable[*actual_index]->name + "'",
                    override.span});
            } else {
                actuals[*actual_index] = *value;
            }
        }
    }
    if (!association_is_vhdl && saw_named && saw_positional) {
        diagnostics.push_back({
            code(SpecializationDiagnostic::association_order),
            "named and positional parameter overrides cannot be mixed",
            overrides.front().span});
    }

    std::size_t overridable_index = 0;
    ConstantDomainEnvironment domains;
    for (const auto& parameter : source.parameters) {
        std::optional<std::int64_t> value;
        if (!parameter.local) {
            value = actuals.at(overridable_index++);
        }
        if (!value) {
            if (parameter.default_value.kind
                == ExpressionKind::Invalid) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    std::string{object_kind} + " '"
                        + parameter.name
                        + "' requires an actual because it has no default",
                    parameter.span});
                value = 0;
            } else {
                std::string error;
                value = evaluate_constant_expression(
                    parameter.default_value,
                    result.environment,
                    error);
                if (!value) {
                    diagnostics.push_back({
                        code(
                            SpecializationDiagnostic::
                                default_evaluation),
                        "cannot evaluate default for "
                            + std::string{object_kind} + " '"
                            + parameter.name + "': " + error,
                        parameter.span});
                    value = 0;
                }
            }
        }
        if (is_vhdl) {
            const auto spelling = parameter.type.spelling;
            const bool violates_natural =
                spelling == "natural" && *value < 0;
            const bool violates_positive =
                spelling == "positive" && *value <= 0;
            const bool violates_boolean =
                parameter.type.domain == frontend::ValueDomain::Boolean
                && *value != 0 && *value != 1;
            const bool violates_bit =
                parameter.type.domain == frontend::ValueDomain::Bit2
                && *value != 0 && *value != 1;
            if (violates_natural || violates_positive
                || violates_boolean || violates_bit) {
                diagnostics.push_back({
                    code(
                        SpecializationDiagnostic::
                            subtype_constraint),
                    std::string{object_kind} + " '"
                        + parameter.name
                        + "' value is outside subtype '"
                        + parameter.type.spelling + "'",
                    parameter.span});
            }
        }
        result.environment[parameter.name] = *value;
        domains[parameter.name] = parameter.type.domain;
        result.values.emplace_back(
            parameter.name,
            is_vhdl
                    && parameter.type.domain
                        == frontend::ValueDomain::Boolean
                ? (*value == 0 ? "false" : "true")
                : std::to_string(*value));
    }

    for (auto& parameter : result.unit.parameters) {
        substitute_parameters(
            parameter.type,
            result.environment,
            diagnostics,
            source.language);
        substitute_parameters(
            parameter.default_value,
            result.environment,
            domains,
            source.language);
    }
    for (auto& alias : result.unit.type_aliases) {
        substitute_parameters(
            alias.type,
            result.environment,
            diagnostics,
            source.language);
        if (alias.enum_literals.empty()) {
            continue;
        }
        const auto width = alias.type.width();
        if (!width || *width == 0 || *width > 64) {
            diagnostics.push_back({
                "FSIM-ELAB-SVENUM-001",
                "enum '" + alias.name
                    + "' has an unsupported base width",
                alias.span});
            continue;
        }
        std::unordered_map<std::int64_t, std::string>
            enum_values;
        for (const auto& literal : alias.enum_literals) {
            const auto value =
                result.environment.find(literal.name);
            if (value == result.environment.end()) {
                continue;
            }
            bool in_range = false;
            if (alias.type.is_signed) {
                if (*width == 64) {
                    in_range = true;
                } else {
                    const auto limit =
                        std::int64_t{1} << (*width - 1U);
                    in_range =
                        value->second >= -limit
                        && value->second < limit;
                }
            } else if (value->second >= 0) {
                in_range =
                    *width == 64
                    || static_cast<std::uint64_t>(
                           value->second)
                        < (std::uint64_t{1} << *width);
            }
            if (!in_range) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVENUM-001",
                    "enum literal '" + literal.name
                        + "' does not fit the base type of '"
                        + alias.name + "'",
                    literal.span});
            }
            const auto [duplicate, inserted] =
                enum_values.emplace(
                    value->second, literal.name);
            if (!inserted) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVENUM-002",
                    "enum literals '" + duplicate->second
                        + "' and '" + literal.name
                        + "' have the same value",
                    literal.span});
            }
        }
    }
    for (auto& port : result.unit.ports) {
        substitute_parameters(
            port.type,
            result.environment,
            diagnostics,
            source.language);
    }
    for (auto& signal : result.unit.signals) {
        substitute_parameters(
            signal.type,
            result.environment,
            diagnostics,
            source.language);
    }
    substitute_parameters(
        result.unit.concurrent_statements,
        result.environment,
        domains,
        diagnostics,
        source.language);
    for (auto& process : result.unit.processes) {
        for (auto& variable : process.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            process.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    substitute_parameters(
        result.unit.instances,
        result.environment,
        domains,
        source.language);
    substitute_parameters(
        result.unit.generate_regions,
        result.environment,
        domains,
        source.language,
        diagnostics);
    expand_generate_regions(
        result.unit.generate_regions,
        result.environment,
        domains,
        source.language,
        {},
        {},
        result.unit,
        diagnostics);
    result.unit.generate_regions.clear();
    return result;
}

bool valid_systemc_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value) {
    switch (type) {
    case FSIM_SC_CONSTRUCTION_INTEGER:
        return true;
    case FSIM_SC_CONSTRUCTION_NATURAL:
        return value >= 0;
    case FSIM_SC_CONSTRUCTION_POSITIVE:
        return value > 0;
    case FSIM_SC_CONSTRUCTION_BOOLEAN:
    case FSIM_SC_CONSTRUCTION_BIT:
        return value == 0 || value == 1;
    }
    return false;
}

std::optional<std::vector<std::pair<std::string, std::int64_t>>>
specialize_systemc_construction(
    const std::vector<SystemCConstructionParameter>& schema,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
    const auto initial_diagnostic_count = diagnostics.size();
    const bool vhdl_association =
        association_language == frontend::Language::Vhdl2008;
    std::vector<std::optional<std::int64_t>> actuals(schema.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    bool saw_positional = false;
    for (const auto& override : overrides) {
        std::string evaluation_error;
        const auto value = evaluate_constant_expression(
            override.value, parent_environment, evaluation_error);
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-004",
                "cannot evaluate SystemC construction actual: "
                    + evaluation_error,
                override.span});
            continue;
        }
        std::optional<std::size_t> index;
        if (override.name) {
            saw_named = true;
            std::vector<std::size_t> matches;
            for (std::size_t candidate = 0;
                 candidate < schema.size();
                 ++candidate) {
                const auto matches_name =
                    vhdl_association
                    ? parameter_name_matches(
                          schema[candidate].name,
                          *override.name,
                          frontend::Language::SystemVerilog2017,
                          association_language)
                    : schema[candidate].name == *override.name;
                if (matches_name) {
                    matches.push_back(candidate);
                }
            }
            if (matches.size() > 1) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-006",
                    "VHDL generic name '" + *override.name
                        + "' ambiguously matches multiple case-sensitive "
                          "SystemC construction parameters",
                    override.span});
                continue;
            }
            if (matches.empty()) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-001",
                    "unknown SystemC construction parameter '"
                        + *override.name + "'",
                    override.span});
                continue;
            }
            index = matches.front();
        } else {
            saw_positional = true;
            if (vhdl_association && saw_named) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-003",
                    "a positional SystemC construction actual cannot "
                    "follow a named VHDL actual",
                    override.span});
            }
            if (next_positional >= schema.size()) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-001",
                    "too many positional SystemC construction actuals",
                    override.span});
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-002",
                "duplicate SystemC construction actual for '"
                    + schema[*index].name + "'",
                override.span});
        } else {
            actuals[*index] = *value;
        }
    }
    if (!vhdl_association && saw_named && saw_positional) {
        diagnostics.push_back({
            "FSIM-ELAB-SC-PARAM-003",
            "named and positional SystemC construction actuals cannot "
            "be mixed",
            overrides.empty() ? frontend::SourceSpan{}
                              : overrides.front().span});
    }

    std::vector<std::pair<std::string, std::int64_t>> values;
    values.reserve(schema.size());
    for (std::size_t index = 0; index < schema.size(); ++index) {
        const auto value =
            actuals[index].has_value()
            ? actuals[index]
            : schema[index].default_value;
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-001",
                "SystemC construction parameter '"
                    + schema[index].name + "' requires an actual",
                {}});
            continue;
        }
        if (!valid_systemc_construction_value(
                schema[index].type, *value)) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-005",
                "SystemC construction parameter '"
                    + schema[index].name
                    + "' violates its declared scalar subtype",
                {}});
            continue;
        }
        values.emplace_back(schema[index].name, *value);
    }
    if (diagnostics.size() != initial_diagnostic_count) {
        return std::nullopt;
    }
    return values;
}

} // namespace

class Lowerer final {
public:
    Lowerer(
        ElaboratedDesign& design,
        const std::unordered_map<std::string, SignalId>& signals,
        std::vector<Diagnostic>& diagnostics)
        : design_(design), signals_(signals), diagnostics_(diagnostics) {}

    Process lower_process(
        const frontend::Process& source,
        const frontend::Language language,
        const std::string_view hierarchy) {
        process_ = Process{};
        language_ = language;
        hierarchy_ = std::string{hierarchy};
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
        local_signed_.clear();
        local_ranges_.clear();
        local_members_.clear();
        declaration_registers_.clear();
        debug_local_names_.clear();
        local_scope_.clear();
        loop_controls_.clear();
        process_.id = static_cast<ProcessId>(design_.processes_.size());
        process_.name = std::string(hierarchy) + "."
            + (source.name.empty()
                   ? "process_" + std::to_string(process_.id)
                   : source.name);
        process_.final = source.kind == ProcessKind::Final;
        if (process_.final) {
            process_.initialize = false;
        }
        initialize_variables(source.variables);
        bool wildcard_sensitivity = false;
        for (const auto& sensitivity : source.sensitivities) {
            if (sensitivity.signal == "*") {
                wildcard_sensitivity = true;
                continue;
            }
            const auto found = signals_.find(sensitivity.signal);
            if (found == signals_.end()) {
                report(
                    "FSIM-ELAB-020",
                    "unknown sensitivity signal '" + sensitivity.signal + "'",
                    sensitivity.span);
                continue;
            }
            runtime::simir::EdgeKind edge = runtime::simir::EdgeKind::any;
            if (sensitivity.edge == frontend::EdgeKind::Positive) {
                edge = runtime::simir::EdgeKind::posedge;
            } else if (sensitivity.edge == frontend::EdgeKind::Negative) {
                edge = runtime::simir::EdgeKind::negedge;
            }
            process_.static_sensitivity.push_back({found->second, edge});
        }
        if (wildcard_sensitivity) {
            std::set<std::string> dependencies;
            collect_statement_identifiers(
                source.statements, dependencies);
            for (const auto& dependency : dependencies) {
                if (locals_.contains(dependency)) {
                    continue;
                }
                if (const auto found = signals_.find(dependency);
                    found != signals_.end()) {
                    process_.static_sensitivity.push_back(
                        {found->second,
                         runtime::simir::EdgeKind::any});
                }
            }
            if (process_.static_sensitivity.empty()) {
                report(
                    "FSIM-ELAB-061",
                    "wildcard process sensitivity has no readable signal "
                    "dependencies",
                    source.span);
            }
        }

        const bool verilog_event_process =
            language != frontend::Language::Vhdl2008
            && source.kind != ProcessKind::Initial
            && source.kind
                != ProcessKind::SystemVerilogAlwaysComb
            && source.kind
                != ProcessKind::SystemVerilogAlwaysLatch
            && !process_.static_sensitivity.empty();
        const Statement* vhdl_edge_guard =
            language == frontend::Language::Vhdl2008
                ? recognized_vhdl_edge_guard(source)
                : nullptr;
        const bool waits_before_first_execution =
            verilog_event_process || vhdl_edge_guard != nullptr;
        const bool vhdl_explicit_wait =
            language == frontend::Language::Vhdl2008
            && contains_explicit_wait(source.statements);
        const auto resume_entry =
            static_cast<InstructionIndex>(process_.operations.size());
        if (waits_before_first_execution) {
            process_.operations.emplace_back(WaitSensitivity{});
        }
        emit_debug_point(DebugPointKind::process_entry, source.span);
        if (vhdl_edge_guard != nullptr) {
            // The frontend refines the sensitivity edge from this canonical
            // idiom. The scheduler now enforces the predicate, so lower only
            // the taken body and retain any following statements.
            lower_statements(vhdl_edge_guard->statements);
            for (std::size_t index = 1; index < source.statements.size(); ++index) {
                lower_statement(source.statements[index]);
            }
        } else {
            lower_statements(source.statements);
        }
        if (source.kind == ProcessKind::Initial
            || source.kind == ProcessKind::Final) {
            process_.operations.emplace_back(Halt{});
        } else if (waits_before_first_execution) {
            // Event-controlled processes wait before their first execution.
            // Returning directly to operation zero preserves one body
            // execution per matching event.
            process_.operations.emplace_back(Jump{resume_entry});
        } else if (
            language == frontend::Language::Vhdl2008
            && source.sensitivities.empty()
            && vhdl_explicit_wait) {
            // A VHDL process implicitly repeats. Explicit waits inside the
            // body provide the required suspension boundary.
            process_.operations.emplace_back(Jump{resume_entry});
        } else {
            // A VHDL process with a sensitivity list executes once at time
            // zero, then waits at the implicit trailing sensitivity point.
            process_.operations.emplace_back(WaitSensitivity{});
            process_.operations.emplace_back(Jump{resume_entry});
        }
        process_.register_count = next_register_;
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
        local_signed_.clear();
        local_ranges_.clear();
        local_members_.clear();
        declaration_registers_.clear();
        debug_local_names_.clear();
        local_scope_.clear();
        loop_controls_.clear();
        return std::move(process_);
    }

    Process lower_concurrent(
        const Statement& statement,
        const frontend::Language language,
        const std::string& name,
        const std::size_t order) {
        process_ = Process{};
        language_ = language;
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
        local_signed_.clear();
        local_ranges_.clear();
        local_members_.clear();
        declaration_registers_.clear();
        debug_local_names_.clear();
        local_scope_.clear();
        loop_controls_.clear();
        process_.id = static_cast<ProcessId>(design_.processes_.size());
        process_.name = name + "."
            + (statement.label.empty()
                   ? "concurrent_" + std::to_string(order)
                   : statement.label);
        emit_debug_point(DebugPointKind::process_entry, statement.span);

        std::set<std::string> dependencies;
        if (statement.kind == StatementKind::Assert) {
            collect_identifiers(statement.condition, dependencies);
        } else if (statement.kind == StatementKind::Assignment) {
            collect_identifiers(statement.value, dependencies);
        } else if (statement.kind == StatementKind::Case) {
            collect_identifiers(statement.condition, dependencies);
            for (const auto& alternative :
                 statement.case_alternatives) {
                for (const auto& choice : alternative.choices) {
                    collect_identifiers(choice, dependencies);
                }
                collect_statement_identifiers(
                    alternative.statements, dependencies);
            }
        }
        for (const auto& dependency : dependencies) {
            if (const auto found = signals_.find(dependency); found != signals_.end()) {
                process_.static_sensitivity.push_back({found->second, runtime::simir::EdgeKind::any});
            }
        }
        lower_statement(statement);
        if (!process_.static_sensitivity.empty()) {
            process_.operations.emplace_back(WaitSensitivity{});
            process_.operations.emplace_back(Jump{0});
        } else {
            process_.operations.emplace_back(Halt{});
        }
        process_.register_count = next_register_;
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
        local_signed_.clear();
        local_ranges_.clear();
        local_members_.clear();
        declaration_registers_.clear();
        debug_local_names_.clear();
        local_scope_.clear();
        loop_controls_.clear();
        return std::move(process_);
    }

private:
    static bool contains_explicit_wait(
        const std::vector<Statement>& statements) {
        return std::any_of(
            statements.begin(), statements.end(),
            [](const Statement& statement) {
                const auto case_wait =
                    std::any_of(
                        statement.case_alternatives.begin(),
                        statement.case_alternatives.end(),
                        [](const frontend::CaseAlternative& alternative) {
                            return contains_explicit_wait(
                                alternative.statements);
                        });
                return statement.kind == StatementKind::Delay
                    || statement.kind == StatementKind::WaitOn
                    || statement.kind == StatementKind::WaitUntil
                    || contains_explicit_wait(statement.statements)
                    || contains_explicit_wait(statement.else_statements)
                    || case_wait;
            });
    }

    void initialize_variables(
        const std::vector<frontend::VariableDeclaration>& variables) {
        struct Pending {
            const frontend::VariableDeclaration* declaration{};
            RegisterId register_id{};
            std::size_t width{};
        };
        std::vector<Pending> pending;
        pending.reserve(variables.size());
        std::unordered_set<std::string> declared_here;
        for (const auto& variable : variables) {
            const auto width = variable.type.width();
            if (!width || *width == 0) {
                report(
                    "FSIM-ELAB-052",
                    "local variable '" + variable.name
                        + "' has no executable packed width",
                    variable.span);
                continue;
            }
            if (!declared_here.emplace(variable.name).second) {
                report(
                    "FSIM-ELAB-053",
                    "duplicate local variable in the same scope '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            const auto key = declaration_key(variable);
            const auto register_found = declaration_registers_.find(key);
            RegisterId register_id{};
            if (register_found == declaration_registers_.end()) {
                register_id =
                    allocate_register(*width, variable.type.domain);
                declaration_registers_.emplace(key, register_id);
                auto debug_name = scoped_local_name(variable.name);
                if (!debug_local_names_.emplace(debug_name).second) {
                    debug_name += "@"
                        + std::to_string(variable.span.begin.line)
                        + ":" + std::to_string(
                            variable.span.begin.column);
                    debug_local_names_.emplace(debug_name);
                }
                process_.debug_locals.push_back(DebugLocal{
                    std::move(debug_name),
                    variable.type.spelling,
                    register_id,
                    *width,
                    SourceLocation{
                        variable.span.source_name,
                        static_cast<std::uint32_t>(
                            variable.span.begin.line),
                        static_cast<std::uint32_t>(
                            variable.span.begin.column)}});
            } else {
                register_id = register_found->second;
            }
            locals_.insert_or_assign(variable.name, register_id);
            local_signed_.insert_or_assign(
                variable.name, variable.type.is_signed);
            local_ranges_.insert_or_assign(
                variable.name, variable.type.packed_range);
            local_members_.insert_or_assign(
                variable.name, variable.type.packed_members);
            pending.push_back(Pending{&variable, register_id, *width});
        }
        for (const auto& local : pending) {
            const auto& variable = *local.declaration;
            if (variable.initializer) {
                const auto value =
                    lower_expression(*variable.initializer, local.width);
                if (!value) {
                    continue;
                }
                if (register_width(*value) != local.width) {
                    report(
                        "FSIM-ELAB-054",
                        "local variable initializer width mismatch for '"
                            + variable.name + "'",
                        variable.span);
                    continue;
                }
                if ((variable.type.domain
                         == frontend::ValueDomain::Bit2
                     || variable.type.domain
                         == frontend::ValueDomain::Boolean)
                    && register_domain(*value)
                        != frontend::ValueDomain::Bit2
                    && register_domain(*value)
                        != frontend::ValueDomain::Boolean) {
                    report(
                        "FSIM-ELAB-058",
                        "two-state local variable initializer for '"
                            + variable.name
                            + "' requires an explicit conversion",
                        variable.span);
                    continue;
                }
                process_.operations.emplace_back(
                    CopyRegister{local.register_id, *value});
                continue;
            }
            const auto initial =
                variable.type.domain == frontend::ValueDomain::Bit2
                    || variable.type.domain
                        == frontend::ValueDomain::Boolean
                    ? Logic4::zero
                    : Logic4::x;
            process_.operations.emplace_back(
                LoadConstant{
                    local.register_id,
                    PackedLogic4{local.width, initial}});
        }
    }

    [[nodiscard]] static std::string declaration_key(
        const frontend::VariableDeclaration& variable) {
        return variable.span.source_name + ":"
            + std::to_string(variable.span.begin.offset) + ":"
            + variable.name;
    }

    [[nodiscard]] std::string scoped_local_name(
        const std::string_view name) const {
        std::string result;
        for (const auto& scope : local_scope_) {
            if (!result.empty()) {
                result += ".";
            }
            result += scope;
        }
        if (!result.empty()) {
            result += ".";
        }
        result += name;
        return result;
    }

    [[nodiscard]] static std::string block_scope_name(
        const Statement& statement) {
        if (!statement.label.empty()) {
            return statement.label;
        }
        return "$block_"
            + std::to_string(statement.span.begin.line) + "_"
            + std::to_string(statement.span.begin.column) + "_"
            + std::to_string(statement.span.begin.offset);
    }

    void lower_block(const Statement& statement) {
        auto outer_locals = locals_;
        auto outer_signed = local_signed_;
        auto outer_ranges = local_ranges_;
        auto outer_members = local_members_;
        local_scope_.push_back(block_scope_name(statement));
        initialize_variables(statement.declarations);
        lower_statements(statement.statements);
        local_scope_.pop_back();
        locals_ = std::move(outer_locals);
        local_signed_ = std::move(outer_signed);
        local_ranges_ = std::move(outer_ranges);
        local_members_ = std::move(outer_members);
    }

    void lower_event_trigger(const Statement& statement) {
        if (statement.target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-099",
                "named-event trigger requires a simple event name",
                statement.span);
            return;
        }
        const auto found = signals_.find(statement.target.text);
        if (found == signals_.end()) {
            report(
                "FSIM-ELAB-100",
                "unknown named event '" + statement.target.text + "'",
                statement.target.span);
            return;
        }
        const auto& info = design_.signal_info_.at(found->second);
        if (info.type_name != "event") {
            report(
                "FSIM-ELAB-101",
                "event trigger target '" + statement.target.text
                    + "' is not declared as an event",
                statement.target.span);
            return;
        }
        const auto current =
            allocate_register(1, frontend::ValueDomain::Logic4);
        const auto toggled =
            allocate_register(1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(
            ReadSignal{current, found->second});
        process_.operations.emplace_back(
            UnaryNot{toggled, current});
        if (statement.assignment_kind
            == AssignmentKind::NonBlocking) {
            if (statement.delay) {
                process_.operations.emplace_back(
                    WriteAfter{
                        found->second,
                        toggled,
                        statement.delay->magnitude});
            } else {
                process_.operations.emplace_back(
                    WriteUpdate{found->second, toggled});
            }
        } else {
            process_.operations.emplace_back(
                WriteBlocking{found->second, toggled});
        }
    }

    static const Statement* recognized_vhdl_edge_guard(
        const frontend::Process& source) {
        if (source.statements.empty()) {
            return nullptr;
        }
        const auto& statement = source.statements.front();
        if (statement.kind != StatementKind::If
            || statement.condition.kind != ExpressionKind::Call
            || statement.condition.operands.size() != 1
            || statement.condition.operands.front().kind
                != ExpressionKind::Identifier) {
            return nullptr;
        }
        const auto& callee = statement.condition.text;
        const auto expected_edge =
            callee == "rising_edge"
                ? frontend::EdgeKind::Positive
                : callee == "falling_edge"
                    ? frontend::EdgeKind::Negative
                    : frontend::EdgeKind::Any;
        if (expected_edge == frontend::EdgeKind::Any) {
            return nullptr;
        }
        const auto& signal = statement.condition.operands.front().text;
        const auto matching = std::find_if(
            source.sensitivities.begin(),
            source.sensitivities.end(),
            [&](const frontend::Sensitivity& sensitivity) {
                return sensitivity.signal == signal
                    && sensitivity.edge == expected_edge;
            });
        return matching == source.sensitivities.end() ? nullptr : &statement;
    }

    void lower_statements(const std::vector<Statement>& statements) {
        for (const auto& statement : statements) {
            lower_statement(statement);
        }
    }

    void lower_statement(const Statement& statement) {
        if (statement.kind != StatementKind::Block
            && !(statement.kind == StatementKind::Loop
                 && statement.loop_runtime)) {
            auto kind = DebugPointKind::statement;
            if (statement.kind == StatementKind::Assert) {
                kind = DebugPointKind::assertion;
            } else if (
                statement.kind == StatementKind::Display
                || statement.kind == StatementKind::Report) {
                kind = DebugPointKind::call;
            } else if (
                statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn
                || statement.kind == StatementKind::WaitUntil) {
                kind = DebugPointKind::wait;
            }
            emit_debug_point(kind, statement.span);
        }
        switch (statement.kind) {
        case StatementKind::Assignment:
            lower_assignment(statement);
            break;
        case StatementKind::If:
            lower_if(statement);
            break;
        case StatementKind::Case:
            lower_case(statement);
            break;
        case StatementKind::Loop:
            lower_loop(statement);
            break;
        case StatementKind::Break:
            lower_loop_control(statement, true);
            break;
        case StatementKind::Continue:
            lower_loop_control(statement, false);
            break;
        case StatementKind::Assert:
            lower_assert(statement);
            break;
        case StatementKind::Delay:
            if (!statement.delay) {
                report("FSIM-ELAB-030", "delay statement has no delay", statement.span);
                break;
            }
            process_.operations.emplace_back(WaitFor{statement.delay->magnitude});
            lower_statements(statement.statements);
            break;
        case StatementKind::WaitOn: {
            auto [signals, edges] =
                resolve_wait_sensitivities(statement);
            if (!signals.empty() || statement.delay) {
                WaitOn wait{
                    std::move(signals), std::move(edges)};
                if (statement.delay) {
                    wait.timeout = statement.delay->magnitude;
                }
                process_.operations.emplace_back(std::move(wait));
            }
            lower_statements(statement.statements);
            break;
        }
        case StatementKind::WaitUntil:
            lower_wait_until(statement);
            break;
        case StatementKind::EventTrigger:
            lower_event_trigger(statement);
            break;
        case StatementKind::MonitorControl:
            process_.operations.emplace_back(
                MonitorControl{statement.monitor_enabled});
            break;
        case StatementKind::Display: {
            const auto runtime_output_format =
                [](const frontend::OutputFormat source) {
                    switch (source) {
                    case frontend::OutputFormat::Binary:
                        return runtime::simir::OutputFormat::binary;
                    case frontend::OutputFormat::Hexadecimal:
                        return runtime::simir::OutputFormat::hexadecimal;
                    case frontend::OutputFormat::Octal:
                        return runtime::simir::OutputFormat::octal;
                    case frontend::OutputFormat::Decimal:
                        return runtime::simir::OutputFormat::decimal;
                    case frontend::OutputFormat::Character:
                        return runtime::simir::OutputFormat::character;
                    case frontend::OutputFormat::String:
                        return runtime::simir::OutputFormat::string;
                    case frontend::OutputFormat::Hierarchy:
                    case frontend::OutputFormat::Time:
                        break;
                    }
                    throw std::logic_error{
                        "invalid frontend output format"};
                };
            if (statement.output_monitor) {
                if (statement.output_values.empty()
                    && !statement.output_format) {
                    process_.operations.emplace_back(
                        MonitorInstall{
                            {},
                            statement.output_text,
                            statement.output_newline});
                    break;
                }
                MonitorInstall monitor;
                monitor.newline = statement.output_newline;
                std::string pending_prefix;
                const auto append_value =
                    [&](const frontend::OutputValue& output) {
                        auto prefix =
                            std::move(pending_prefix) + output.prefix;
                        if (output.format
                            == frontend::OutputFormat::Hierarchy) {
                            pending_prefix =
                                std::move(prefix) + hierarchy_;
                            return;
                        }
                        MonitorValue value;
                        value.prefix = std::move(prefix);
                        value.minimum_width = output.minimum_width;
                        value.left_justify = output.left_justify;
                        value.zero_pad = output.zero_pad;
                        value.suppress_leading_zero =
                            output.suppress_leading_zero;
                        if (output.format
                            == frontend::OutputFormat::Time) {
                            value.kind = MonitorValueKind::time;
                        } else {
                            if (output.value.kind
                                != frontend::ExpressionKind::Identifier) {
                                report(
                                    "FSIM-ELAB-103",
                                    "$monitor currently requires direct "
                                    "packed-signal value expressions",
                                    output.value.span);
                                return;
                            }
                            const auto signal =
                                signals_.find(output.value.text);
                            if (signal == signals_.end()) {
                                report(
                                    "FSIM-ELAB-020",
                                    "unknown monitor signal '"
                                        + output.value.text + "'",
                                    output.value.span);
                                return;
                            }
                            value.kind = MonitorValueKind::signal;
                            value.signal = signal->second;
                            value.format =
                                runtime_output_format(output.format);
                            value.signed_decimal =
                                value.format
                                        == runtime::simir::OutputFormat::
                                            decimal
                                    && is_signed_expression(output.value);
                        }
                        monitor.values.push_back(std::move(value));
                    };
                if (!statement.output_values.empty()) {
                    for (const auto& output :
                         statement.output_values) {
                        append_value(output);
                    }
                    monitor.trailing_text =
                        std::move(pending_prefix)
                        + statement.output_trailing_text;
                } else {
                    append_value(
                        frontend::OutputValue{
                            statement.value,
                            *statement.output_format,
                            statement.output_prefix,
                            statement.output_suppress_leading_zero,
                            statement.output_minimum_width,
                            statement.output_left_justify,
                            statement.output_zero_pad});
                    monitor.trailing_text =
                        std::move(pending_prefix)
                        + statement.output_suffix;
                }
                if (!monitor.values.empty()) {
                    process_.operations.emplace_back(
                        std::move(monitor));
                }
                break;
            }
            if (!statement.output_values.empty()) {
                for (std::size_t index = 0;
                     index < statement.output_values.size();
                     ++index) {
                    const auto& output = statement.output_values[index];
                    const bool last =
                        index + 1 == statement.output_values.size();
                    if (output.format
                        == frontend::OutputFormat::Hierarchy) {
                        process_.operations.emplace_back(
                            Display{
                                output.prefix + hierarchy_
                                    + (last
                                           ? statement.output_trailing_text
                                           : std::string{}),
                                last && statement.output_newline,
                                statement.output_postponed});
                        continue;
                    }
                    if (output.format == frontend::OutputFormat::Time) {
                        process_.operations.emplace_back(
                            TimeDisplay{
                                output.prefix,
                                last
                                    ? statement.output_trailing_text
                                    : std::string{},
                                last && statement.output_newline,
                                statement.output_postponed,
                                output.minimum_width,
                                output.left_justify,
                                output.zero_pad});
                        continue;
                    }
                    const auto width =
                        infer_width(output.value)
                            .value_or(std::size_t{32});
                    const auto source =
                        lower_expression(output.value, width);
                    if (!source) {
                        report(
                            "FSIM-ELAB-102",
                            "formatted output value cannot be lowered",
                            output.value.span);
                        continue;
                    }
                    const auto format =
                        runtime_output_format(output.format);
                    process_.operations.emplace_back(
                        FormatDisplay{
                            *source,
                            format,
                            output.prefix,
                            last
                                ? statement.output_trailing_text
                                : std::string{},
                            last && statement.output_newline,
                            statement.output_postponed,
                            format
                                    == runtime::simir::OutputFormat::decimal
                                && is_signed_expression(output.value),
                            output.suppress_leading_zero,
                            output.minimum_width,
                            output.left_justify,
                            output.zero_pad});
                }
                break;
            }
            if (statement.output_format) {
                const auto width =
                    infer_width(statement.value)
                        .value_or(std::size_t{32});
                const auto source =
                    lower_expression(statement.value, width);
                if (!source) {
                    report(
                        "FSIM-ELAB-102",
                        "formatted output value cannot be lowered",
                        statement.span);
                    break;
                }
                const auto format =
                    runtime_output_format(*statement.output_format);
                process_.operations.emplace_back(
                    FormatDisplay{
                        *source,
                        format,
                        statement.output_prefix,
                        statement.output_suffix,
                        statement.output_newline,
                        statement.output_postponed,
                        format == runtime::simir::OutputFormat::decimal
                            && is_signed_expression(statement.value),
                        statement.output_suppress_leading_zero,
                        statement.output_minimum_width,
                        statement.output_left_justify,
                        statement.output_zero_pad});
            } else {
                process_.operations.emplace_back(
                    Display{
                        statement.output_text,
                        statement.output_newline,
                        statement.output_postponed});
            }
            break;
        }
        case StatementKind::Report: {
            AssertionSeverity severity = AssertionSeverity::note;
            switch (statement.assertion_severity) {
            case frontend::AssertionSeverity::Note:
                severity = AssertionSeverity::note;
                break;
            case frontend::AssertionSeverity::Warning:
                severity = AssertionSeverity::warning;
                break;
            case frontend::AssertionSeverity::Error:
                severity = AssertionSeverity::error;
                break;
            case frontend::AssertionSeverity::Failure:
                severity = AssertionSeverity::failure;
                break;
            }
            process_.operations.emplace_back(
                runtime::simir::Report{
                    statement.output_text,
                    severity,
                    SourceLocation{
                        statement.span.source_name,
                        static_cast<std::uint32_t>(
                            statement.span.begin.line),
                        static_cast<std::uint32_t>(
                            statement.span.begin.column)}});
            break;
        }
        case StatementKind::Pause:
            process_.operations.emplace_back(Pause{});
            break;
        case StatementKind::Finish:
            process_.operations.emplace_back(Stop{});
            break;
        case StatementKind::Block:
            lower_block(statement);
            break;
        case StatementKind::Null:
            break;
        }
    }

    [[nodiscard]] std::pair<
        std::vector<SignalId>,
        std::vector<runtime::simir::EdgeKind>>
    resolve_wait_sensitivities(
        const Statement& statement) {
        std::vector<SignalId> signals;
        std::vector<runtime::simir::EdgeKind> edges;
        signals.reserve(statement.sensitivities.size());
        edges.reserve(statement.sensitivities.size());
        for (const auto& sensitivity : statement.sensitivities) {
            if (sensitivity.signal == "*") {
                std::set<std::string> dependencies;
                collect_statement_identifiers(
                    statement.statements, dependencies);
                for (const auto& dependency : dependencies) {
                    if (locals_.contains(dependency)) {
                        continue;
                    }
                    if (const auto found =
                            signals_.find(dependency);
                        found != signals_.end()) {
                        signals.push_back(found->second);
                        edges.push_back(
                            runtime::simir::EdgeKind::any);
                    }
                }
                if (dependencies.empty() || signals.empty()) {
                    report(
                        "FSIM-ELAB-062",
                        "dynamic wildcard event control has no readable "
                        "signal dependencies",
                        sensitivity.span);
                }
                continue;
            }
            const auto found = signals_.find(sensitivity.signal);
            if (found == signals_.end()) {
                report(
                    "FSIM-ELAB-059",
                    "unknown wait signal '" + sensitivity.signal + "'",
                    sensitivity.span);
                continue;
            }
            if (sensitivity.edge != frontend::EdgeKind::Any
                && design_.signal_info_[found->second].width != 1) {
                report(
                    "FSIM-ELAB-060",
                    "dynamic edge-qualified wait signal '"
                        + sensitivity.signal
                        + "' must be scalar",
                    sensitivity.span);
                continue;
            }
            signals.push_back(found->second);
            auto edge = runtime::simir::EdgeKind::any;
            if (sensitivity.edge
                == frontend::EdgeKind::Positive) {
                edge = runtime::simir::EdgeKind::posedge;
            } else if (
                sensitivity.edge
                == frontend::EdgeKind::Negative) {
                edge = runtime::simir::EdgeKind::negedge;
            }
            edges.push_back(edge);
        }
        return {std::move(signals), std::move(edges)};
    }

    void emit_debug_point(
        const DebugPointKind kind,
        const frontend::SourceSpan& span) {
        process_.operations.emplace_back(DebugPoint{
            kind,
            SourceLocation{
                span.source_name,
                static_cast<std::uint32_t>(span.begin.line),
                static_cast<std::uint32_t>(span.begin.column)}});
    }

    void lower_wait_until(const Statement& statement) {
        if (language_ != frontend::Language::Vhdl2008) {
            lower_immediate_condition_wait(statement);
            return;
        }

        const auto condition_operations_start =
            process_.operations.size();
        const auto condition = lower_condition(
            statement.condition,
            "FSIM-ELAB-079",
            "wait-until");
        if (!condition) {
            return;
        }
        std::vector<runtime::simir::Operation> condition_operations(
            std::make_move_iterator(
                process_.operations.begin()
                + static_cast<std::ptrdiff_t>(
                    condition_operations_start)),
            std::make_move_iterator(
                process_.operations.end()));
        process_.operations.resize(condition_operations_start);

        std::vector<SignalId> waited_signals;
        std::vector<runtime::simir::EdgeKind> waited_edges;
        if (!statement.sensitivities.empty()) {
            auto resolved =
                resolve_wait_sensitivities(statement);
            waited_signals = std::move(resolved.first);
            waited_edges = std::move(resolved.second);
        } else {
            std::set<std::string> dependencies;
            collect_identifiers(
                statement.condition, dependencies);
            for (const auto& dependency : dependencies) {
                if (locals_.contains(dependency)) {
                    continue;
                }
                if (const auto found = signals_.find(dependency);
                    found != signals_.end()) {
                    waited_signals.push_back(found->second);
                }
            }
            std::ranges::sort(waited_signals);
            waited_signals.erase(
                std::unique(
                    waited_signals.begin(),
                    waited_signals.end()),
                waited_signals.end());
        }

        if (waited_signals.empty() && !statement.delay) {
            process_.operations.emplace_back(WaitForever{});
            lower_statements(statement.statements);
            return;
        }

        std::optional<RegisterId> timed_out;
        if (statement.delay) {
            timed_out = allocate_register(
                1, frontend::ValueDomain::Boolean);
        }

        const auto initial_wait =
            static_cast<InstructionIndex>(
                process_.operations.size());
        WaitOn first_wait{
            waited_signals, waited_edges};
        if (statement.delay) {
            first_wait.timeout =
                statement.delay->magnitude;
            first_wait.timeout_result = timed_out;
        }
        process_.operations.emplace_back(
            std::move(first_wait));

        std::optional<InstructionIndex> timeout_branch;
        if (timed_out) {
            timeout_branch =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations.emplace_back(
                Branch{
                    *timed_out,
                    0,
                    0,
                    UnknownBranchPolicy::error});
        }

        const auto condition_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        process_.operations.insert(
            process_.operations.end(),
            std::make_move_iterator(
                condition_operations.begin()),
            std::make_move_iterator(
                condition_operations.end()));
        const auto condition_branch =
            static_cast<InstructionIndex>(
                process_.operations.size());
        const auto unknown_policy =
            language_ == frontend::Language::Vhdl2008
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch{*condition, 0, 0, unknown_policy});

        const auto rewait_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        WaitOn rewait{
            std::move(waited_signals),
            std::move(waited_edges)};
        if (statement.delay) {
            rewait.timeout =
                statement.delay->magnitude;
            rewait.timeout_result = timed_out;
            rewait.timeout_origin = initial_wait;
        }
        process_.operations.emplace_back(std::move(rewait));
        process_.operations.emplace_back(
            Jump{
                timeout_branch.value_or(
                    condition_start)});

        const auto satisfied_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        lower_statements(statement.statements);
        process_.operations[condition_branch] = Branch{
            *condition,
            satisfied_start,
            rewait_start,
            unknown_policy};
        if (timeout_branch) {
            process_.operations[*timeout_branch] = Branch{
                *timed_out,
                satisfied_start,
                condition_start,
                UnknownBranchPolicy::error};
        }
    }

    void lower_immediate_condition_wait(
        const Statement& statement) {
        const auto condition_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        const auto condition = lower_condition(
            statement.condition,
            "FSIM-ELAB-079",
            "wait");
        if (!condition) {
            return;
        }
        const auto branch_index =
            static_cast<InstructionIndex>(
                process_.operations.size());
        process_.operations.emplace_back(
            Branch{
                *condition,
                0,
                0,
                UnknownBranchPolicy::when_false});

        const auto wait_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        std::set<std::string> dependencies;
        collect_identifiers(
            statement.condition, dependencies);
        std::vector<SignalId> waited_signals;
        for (const auto& dependency : dependencies) {
            if (locals_.contains(dependency)) {
                continue;
            }
            if (const auto found = signals_.find(dependency);
                found != signals_.end()) {
                waited_signals.push_back(found->second);
            }
        }
        std::ranges::sort(waited_signals);
        waited_signals.erase(
            std::unique(
                waited_signals.begin(),
                waited_signals.end()),
            waited_signals.end());
        if (waited_signals.empty()) {
            process_.operations.emplace_back(WaitForever{});
        } else {
            process_.operations.emplace_back(
                WaitOn{std::move(waited_signals)});
            process_.operations.emplace_back(
                Jump{condition_start});
        }

        const auto satisfied_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        lower_statements(statement.statements);
        process_.operations[branch_index] = Branch{
            *condition,
            satisfied_start,
            wait_start,
            UnknownBranchPolicy::when_false};
    }

    std::optional<RegisterId> lower_condition(
        const Expression& expression,
        std::string diagnostic_code,
        std::string_view construct) {
        const auto expression_width =
            infer_width(expression).value_or(std::size_t{1});
        const auto source =
            lower_expression(expression, expression_width);
        if (!source) {
            return std::nullopt;
        }
        if (language_ == frontend::Language::Vhdl2008) {
            if (register_width(*source) != 1
                || register_domain(*source)
                    != frontend::ValueDomain::Boolean) {
                report(
                    std::move(diagnostic_code),
                    "a VHDL " + std::string{construct}
                        + " condition must have type boolean",
                    expression.span);
                return std::nullopt;
            }
            return source;
        }

        // SystemVerilog conditionals apply logical truth conversion to the
        // complete expression. Reusing logical negation twice preserves 0,
        // 1, and unknown truth while normalizing any packed width to a
        // scalar. Branching subsequently treats X/Z as false, as required
        // for procedural conditions.
        const auto inverted =
            allocate_register(1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(LogicalNot{inverted, *source});
        const auto normalized =
            allocate_register(1, frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(
            LogicalNot{normalized, inverted});
        return normalized;
    }

    void lower_assert(const Statement& statement) {
        const auto condition = lower_condition(
            statement.condition, "FSIM-ELAB-051", "assertion");
        if (!condition) {
            return;
        }
        if (language_
            == frontend::Language::SystemVerilog2017) {
            const auto branch_index =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations.emplace_back(
                Branch{
                    *condition,
                    0,
                    0,
                    UnknownBranchPolicy::when_false});
            const auto pass_start =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            lower_statements(statement.statements);
            const auto jump_index =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations.emplace_back(Jump{0});
            const auto failure_start =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            if (statement.assertion_has_failure_action) {
                lower_statements(statement.else_statements);
            } else {
                process_.operations.emplace_back(
                    runtime::simir::Report{
                        statement.assertion_message.empty()
                            ? "assertion failed"
                            : statement.assertion_message,
                        AssertionSeverity::error,
                        SourceLocation{
                            statement.span.source_name,
                            static_cast<std::uint32_t>(
                                statement.span.begin.line),
                            static_cast<std::uint32_t>(
                                statement.span.begin.column)}});
            }
            const auto end =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            process_.operations[branch_index] =
                Branch{
                    *condition,
                    pass_start,
                    failure_start,
                    UnknownBranchPolicy::when_false};
            process_.operations[jump_index] = Jump{end};
            return;
        }
        AssertionSeverity severity = AssertionSeverity::error;
        switch (statement.assertion_severity) {
        case frontend::AssertionSeverity::Note:
            severity = AssertionSeverity::note;
            break;
        case frontend::AssertionSeverity::Warning:
            severity = AssertionSeverity::warning;
            break;
        case frontend::AssertionSeverity::Error:
            severity = AssertionSeverity::error;
            break;
        case frontend::AssertionSeverity::Failure:
            severity = AssertionSeverity::failure;
            break;
        }
        process_.operations.emplace_back(Assert{
            *condition,
            statement.assertion_message,
            severity,
            SourceLocation{
                statement.span.source_name,
                static_cast<std::uint32_t>(statement.span.begin.line),
                static_cast<std::uint32_t>(statement.span.begin.column)}});
    }

    struct ConstantSliceSelection {
        std::size_t offset{};
        std::size_t width{};
    };

    std::optional<ConstantSliceSelection>
    constant_slice_selection(
        const Expression& expression,
        const std::size_t source_width) const {
        if (expression.kind != ExpressionKind::Slice
            || expression.operands.size() != 3) {
            return std::nullopt;
        }
        const auto& source = expression.operands[0];
        const auto range =
            expression_range(source, source_width);
        if (!range) {
            return std::nullopt;
        }

        std::int64_t left = 0;
        std::int64_t right = 0;
        std::uint64_t width = 0;
        if (expression.text == "+:"
            || expression.text == "-:") {
            const auto base =
                constant_index(expression.operands[1]);
            const auto selected_width =
                constant_index(expression.operands[2]);
            if (!base || !selected_width
                || *selected_width <= 0) {
                return std::nullopt;
            }
            const auto distance = *selected_width - 1;
            std::int64_t lower = 0;
            std::int64_t upper = 0;
            if (expression.text == "+:") {
                if (*base
                    > std::numeric_limits<std::int64_t>::max()
                          - distance) {
                    return std::nullopt;
                }
                lower = *base;
                upper = *base + distance;
            } else {
                if (*base
                    < std::numeric_limits<std::int64_t>::min()
                          + distance) {
                    return std::nullopt;
                }
                lower = *base - distance;
                upper = *base;
            }
            if (range->left >= range->right) {
                left = upper;
                right = lower;
            } else {
                left = lower;
                right = upper;
            }
            width = static_cast<std::uint64_t>(*selected_width);
        } else {
            const auto parsed_left =
                constant_index(expression.operands[1]);
            const auto parsed_right =
                constant_index(expression.operands[2]);
            if (!parsed_left || !parsed_right) {
                return std::nullopt;
            }
            left = *parsed_left;
            right = *parsed_right;
            const bool selected_descending = left >= right;
            if (left != right
                && selected_descending
                    != (range->left >= range->right)) {
                return std::nullopt;
            }
            width = index_distance(left, right) + 1;
        }
        const auto offset =
            select_offset(source, right, source_width);
        const auto left_offset =
            select_offset(source, left, source_width);
        if (!offset || !left_offset
            || width == 0
            || width
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return ConstantSliceSelection{
            *offset, static_cast<std::size_t>(width)};
    }

    void lower_assignment(const Statement& statement) {
        const Expression* base = &statement.target;
        std::optional<std::uint32_t> selected_offset;
        std::optional<std::size_t> selected_width;
        std::optional<frontend::ValueDomain> selected_domain;
        if (statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2) {
            base = &statement.target.operands[0];
        } else if (
            statement.target.kind == ExpressionKind::Slice
            && statement.target.operands.size() == 3) {
            base = &statement.target.operands[0];
        } else if (statement.target.kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-031",
                "an assignment target must be a packed object, bit-select, "
                "or constant part-select",
                statement.target.span);
            return;
        }
        if (base->kind != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-031",
                "nested or aggregate selected assignment targets are not "
                "executable yet",
                statement.target.span);
            return;
        }

        auto target_name = base->text;
        if (!locals_.contains(target_name)
            && !signals_.contains(target_name)) {
            if (const auto selected =
                    packed_member_reference(target_name)) {
                const auto width = selected->member->width();
                if (!width || *width == 0
                    || *width
                        > std::numeric_limits<std::uint32_t>::max()
                    || selected->member->lsb_offset
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVSTRUCT-002",
                        "packed aggregate member '" + target_name
                            + "' has no executable layout",
                        statement.target.span);
                    return;
                }
                target_name = selected->base;
                selected_offset = static_cast<std::uint32_t>(
                    selected->member->lsb_offset);
                selected_width = static_cast<std::size_t>(*width);
                selected_domain = selected->member->domain;
            }
        }
        const auto local = locals_.find(target_name);
        const auto signal = signals_.find(target_name);
        if (local == locals_.end() && signal == signals_.end()) {
            report(
                "FSIM-ELAB-032",
                "unknown assignment target '" + target_name + "'",
                statement.target.span);
            return;
        }
        const auto whole_width =
            local != locals_.end()
                ? register_width(local->second)
                : design_.signal_info_[signal->second].width;
        const auto selection_source_width =
            selected_width.value_or(whole_width);

        if (statement.target.kind == ExpressionKind::Index) {
            const auto index =
                constant_index(statement.target.operands[1]);
            const auto offset =
                index
                    ? select_offset(
                          *base, *index, selection_source_width)
                    : std::nullopt;
            const auto base_offset =
                static_cast<std::uint64_t>(
                    selected_offset.value_or(0));
            if (!index || !offset
                || base_offset + *offset
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "an assignment bit-select requires a constant index "
                    "inside the target's declared packed range",
                    statement.target.span);
                return;
            }
            selected_offset =
                static_cast<std::uint32_t>(base_offset + *offset);
            selected_width = 1;
        } else if (statement.target.kind == ExpressionKind::Slice) {
            const auto selection =
                constant_slice_selection(
                    statement.target, selection_source_width);
            const auto base_offset =
                static_cast<std::uint64_t>(
                    selected_offset.value_or(0));
            if (!selection
                || base_offset + selection->offset
                    > std::numeric_limits<std::uint32_t>::max()
                || selection->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "an assignment part-select requires constant in-range "
                    "bounds, a positive indexed width, and a direction "
                    "compatible with the target's declared packed range",
                    statement.target.span);
                return;
            }
            selected_offset =
                static_cast<std::uint32_t>(
                    base_offset + selection->offset);
            selected_width = selection->width;
        }

        const auto target_width =
            selected_width.value_or(whole_width);
        if (local != locals_.end()) {
            if (statement.assignment_kind != AssignmentKind::Blocking
                || statement.delay) {
                report(
                    "FSIM-ELAB-056",
                    "local variable assignments require an undelayed "
                    "blocking/variable assignment",
                    statement.span);
                return;
            }
            const auto value =
                lower_expression(statement.value, target_width);
            if (!value) {
                return;
            }
            if (register_width(*value) != target_width) {
                report(
                    "FSIM-ELAB-057",
                    "local variable assignment width mismatch for '"
                        + target_name + "'",
                    statement.span);
                return;
            }
            const auto target_domain =
                selected_domain.value_or(
                    register_domain(local->second));
            if ((target_domain == frontend::ValueDomain::Bit2
                 || target_domain == frontend::ValueDomain::Boolean)
                && register_domain(*value)
                    != frontend::ValueDomain::Bit2
                && register_domain(*value)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-058",
                    "assignment to two-state local variable '"
                        + target_name
                        + "' requires an explicit conversion",
                    statement.span);
                return;
            }
            if (selected_offset) {
                process_.operations.emplace_back(Insert{
                    local->second,
                    local->second,
                    *value,
                    *selected_offset});
            } else {
                process_.operations.emplace_back(
                    CopyRegister{local->second, *value});
            }
            return;
        }
        const auto value = lower_expression(statement.value, target_width);
        if (!value) {
            return;
        }
        if (register_width(*value) != target_width) {
            report(
                "FSIM-ELAB-047",
                "assignment width mismatch: target '"
                    + target_name + "' is "
                    + std::to_string(target_width)
                    + " bits but the expression is "
                    + std::to_string(register_width(*value)) + " bits",
                statement.span);
            return;
        }
        const auto target_domain =
            selected_domain.value_or(
                design_.signal_info_[signal->second].source_domain);
        if ((target_domain == frontend::ValueDomain::Bit2
             || target_domain == frontend::ValueDomain::Boolean)
            && register_domain(*value) != frontend::ValueDomain::Bit2
            && register_domain(*value) != frontend::ValueDomain::Boolean) {
            report(
                "FSIM-ELAB-050",
                "assignment to two-state target '"
                    + target_name
                    + "' requires an explicit conversion from a "
                      "four-/nine-state expression",
                statement.span);
            return;
        }
        if (statement.delay
            && statement.assignment_kind == AssignmentKind::Blocking) {
            report(
                "FSIM-ELAB-046",
                "a procedural blocking intra-assignment delay is parsed but "
                "not executable until SimIR can suspend between RHS "
                "evaluation and the write",
                statement.span);
            return;
        }
        if (statement.delay) {
            if (selected_offset) {
                process_.operations.emplace_back(WriteAfterSlice{
                    signal->second,
                    *value,
                    *selected_offset,
                    statement.delay->magnitude});
            } else {
                process_.operations.emplace_back(WriteAfter{
                    signal->second,
                    *value,
                    statement.delay->magnitude});
            }
        } else if (
            statement.assignment_kind == AssignmentKind::Blocking) {
            if (selected_offset) {
                process_.operations.emplace_back(WriteBlockingSlice{
                    signal->second, *value, *selected_offset});
            } else {
                process_.operations.emplace_back(
                    WriteBlocking{signal->second, *value});
            }
        } else {
            if (selected_offset) {
                process_.operations.emplace_back(WriteUpdateSlice{
                    signal->second, *value, *selected_offset});
            } else {
                process_.operations.emplace_back(
                    WriteUpdate{signal->second, *value});
            }
        }
    }

    void lower_if(const Statement& statement) {
        const auto condition = lower_condition(
            statement.condition, "FSIM-ELAB-048", "if");
        if (!condition) {
            return;
        }
        const auto branch_index =
            static_cast<InstructionIndex>(process_.operations.size());
        const auto unknown_policy =
            language_ == frontend::Language::Vhdl2008
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch{*condition, 0, 0, unknown_policy});
        const auto true_start =
            static_cast<InstructionIndex>(process_.operations.size());
        lower_statements(statement.statements);
        const auto jump_index =
            static_cast<InstructionIndex>(process_.operations.size());
        process_.operations.emplace_back(Jump{0});
        const auto false_start =
            static_cast<InstructionIndex>(process_.operations.size());
        lower_statements(statement.else_statements);
        const auto end = static_cast<InstructionIndex>(process_.operations.size());
        process_.operations[branch_index] = Branch{
            *condition, true_start, false_start, unknown_policy};
        process_.operations[jump_index] = Jump{end};
    }

    void lower_case(const Statement& statement) {
        BinaryOperator match_operation = BinaryOperator::case_equal;
        switch (statement.case_match_kind) {
        case frontend::CaseMatchKind::Exact:
            break;
        case frontend::CaseMatchKind::WildcardZ:
            match_operation = BinaryOperator::casez_equal;
            break;
        case frontend::CaseMatchKind::WildcardXZ:
            match_operation = BinaryOperator::casex_equal;
            break;
        default:
            report(
                "FSIM-ELAB-081",
                "case statement has an invalid matching mode",
                statement.span);
            return;
        }
        const auto selector_width =
            infer_width(statement.condition).value_or(std::size_t{1});
        const auto selector =
            lower_expression(statement.condition, selector_width);
        if (!selector) {
            return;
        }

        std::vector<InstructionIndex> exit_jumps;
        const frontend::CaseAlternative* default_alternative = nullptr;
        for (const auto& alternative : statement.case_alternatives) {
            if (alternative.is_default) {
                default_alternative = &alternative;
                continue;
            }

            std::vector<InstructionIndex> branches;
            for (const auto& choice : alternative.choices) {
                const auto choice_register =
                    lower_expression(choice, register_width(*selector));
                if (!choice_register) {
                    continue;
                }
                if (register_width(*choice_register)
                    != register_width(*selector)) {
                    report(
                        "FSIM-ELAB-063",
                        "case item width "
                            + std::to_string(
                                register_width(*choice_register))
                            + " does not match selector width "
                            + std::to_string(register_width(*selector)),
                        choice.span);
                    continue;
                }
                const auto condition =
                    allocate_register(1, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(Binary{
                    match_operation,
                    condition,
                    *selector,
                    *choice_register});
                branches.push_back(
                    static_cast<InstructionIndex>(
                        process_.operations.size()));
                process_.operations.emplace_back(Branch{
                    condition,
                    0,
                    0,
                    UnknownBranchPolicy::when_false});
            }

            const auto skip_body =
                static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Jump{0});
            const auto body_start =
                static_cast<InstructionIndex>(process_.operations.size());
            lower_statements(alternative.statements);
            exit_jumps.push_back(
                static_cast<InstructionIndex>(
                    process_.operations.size()));
            process_.operations.emplace_back(Jump{0});
            const auto next_alternative =
                static_cast<InstructionIndex>(process_.operations.size());

            for (std::size_t index = 0; index < branches.size(); ++index) {
                const auto false_target =
                    index + 1 < branches.size()
                        ? static_cast<InstructionIndex>(
                              branches[index] + 1)
                        : skip_body;
                const auto& operation =
                    std::get<Branch>(process_.operations[branches[index]]);
                process_.operations[branches[index]] = Branch{
                    operation.condition,
                    body_start,
                    false_target,
                    UnknownBranchPolicy::when_false};
            }
            process_.operations[skip_body] = Jump{next_alternative};
        }

        if (default_alternative != nullptr) {
            lower_statements(default_alternative->statements);
        }
        const auto end =
            static_cast<InstructionIndex>(process_.operations.size());
        for (const auto jump : exit_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }

    void lower_loop(const Statement& statement) {
        if (statement.loop_runtime) {
            lower_runtime_loop(statement);
            return;
        }
        const auto assigns_loop_parameter =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> bool {
            for (const auto& child : statements) {
                if (child.kind == StatementKind::Assignment) {
                    const Expression* target = &child.target;
                    while ((target->kind == ExpressionKind::Index
                            || target->kind
                                == ExpressionKind::Slice)
                           && !target->operands.empty()) {
                        target = &target->operands.front();
                    }
                    if (target->kind == ExpressionKind::Identifier
                        && target->text
                            == statement.loop_variable) {
                        return true;
                    }
                }
                if (self(self, child.statements)
                    || self(self, child.else_statements)) {
                    return true;
                }
                for (const auto& alternative :
                     child.case_alternatives) {
                    if (self(self, alternative.statements)) {
                        return true;
                    }
                }
            }
            return false;
        };
        if (assigns_loop_parameter(
                assigns_loop_parameter,
                statement.statements)) {
            report(
                "FSIM-ELAB-074",
                "sequential for-loop index '"
                    + statement.loop_variable
                    + (language_ == frontend::Language::Vhdl2008
                           ? "' is an implicit constant and cannot be "
                             "assigned"
                           : "' is statically substituted by this bounded "
                             "slice and cannot be assigned in the body"),
                statement.span);
            return;
        }

        std::string error;
        const auto initial = evaluate_constant_expression(
            statement.loop_initial, {}, error);
        if (!initial) {
            report(
                "FSIM-ELAB-071",
                "cannot evaluate sequential for-loop initial bound: "
                    + error,
                statement.loop_initial.span);
            return;
        }
        error.clear();
        const auto limit = evaluate_constant_expression(
            statement.loop_limit, {}, error);
        if (!limit) {
            report(
                statement.loop_repeat
                    ? "FSIM-ELAB-075"
                    : "FSIM-ELAB-072",
                (statement.loop_repeat
                     ? "cannot evaluate repeat count: "
                     : "cannot evaluate sequential for-loop final bound: ")
                    + error,
                statement.loop_limit.span);
            return;
        }
        if (statement.loop_repeat && *limit < 0) {
            report(
                "FSIM-ELAB-076",
                "repeat count must be nonnegative",
                statement.loop_limit.span);
            return;
        }

        const bool null_range =
            statement.loop_descending
                ? (statement.loop_limit_exclusive
                       ? *initial <= *limit
                       : *initial < *limit)
                : (statement.loop_limit_exclusive
                       ? *initial >= *limit
                       : *initial > *limit);
        if (null_range) {
            return;
        }
        const auto distance =
            index_distance(*initial, *limit);
        constexpr std::uint64_t maximum_iterations = 1'000'000;
        const bool too_many_iterations =
            statement.loop_limit_exclusive
                ? distance > maximum_iterations
                : distance >= maximum_iterations;
        if (too_many_iterations) {
            report(
                "FSIM-ELAB-073",
                "sequential for loop exceeds the bounded "
                "1,000,000-iteration elaboration limit",
                statement.span);
            return;
        }

        ConstantDomainEnvironment domains;
        if (!statement.loop_variable.empty()) {
            domains.emplace(
                statement.loop_variable,
                frontend::ValueDomain::Integer);
        }
        auto value = *initial;
        std::size_t count = 0;
        const auto in_range = [&]() {
            if (statement.loop_descending) {
                return statement.loop_limit_exclusive
                    ? value > *limit
                    : value >= *limit;
            }
            return statement.loop_limit_exclusive
                ? value < *limit
                : value <= *limit;
        };
        loop_controls_.push_back({});
        loop_controls_.back().label =
            statement.loop_label;
        while (in_range()) {
            if (count++ == maximum_iterations) {
                report(
                    "FSIM-ELAB-073",
                    "sequential for loop exceeds the bounded "
                    "1,000,000-iteration elaboration limit",
                    statement.span);
                loop_controls_.pop_back();
                return;
            }
            auto body = statement.statements;
            ConstantEnvironment environment;
            if (!statement.loop_variable.empty()) {
                environment.emplace(
                    statement.loop_variable, value);
            }
            substitute_parameters(
                body,
                environment,
                domains,
                diagnostics_,
                language_);
            lower_statements(body);
            const auto next_iteration =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            for (const auto jump :
                 loop_controls_.back().continue_jumps) {
                process_.operations[jump] =
                    Jump{next_iteration};
            }
            loop_controls_.back().continue_jumps.clear();
            if (!statement.loop_limit_exclusive
                && value == *limit) {
                break;
            }
            value += statement.loop_descending ? -1 : 1;
        }
        auto loop_control = std::move(loop_controls_.back());
        loop_controls_.pop_back();
        const auto end =
            static_cast<InstructionIndex>(
                process_.operations.size());
        for (const auto jump : loop_control.break_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }

    void lower_runtime_loop(const Statement& statement) {
        const auto loop_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        emit_debug_point(
            DebugPointKind::statement, statement.span);
        if (statement.loop_post_test) {
            loop_controls_.push_back({});
            loop_controls_.back().label =
                statement.loop_label;
            lower_statements(statement.statements);
            auto loop_control =
                std::move(loop_controls_.back());
            loop_controls_.pop_back();
            const auto condition_start =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            for (const auto jump :
                 loop_control.continue_jumps) {
                process_.operations[jump] =
                    Jump{condition_start};
            }
            const auto condition = lower_condition(
                statement.condition,
                "FSIM-ELAB-048",
                "do-while");
            if (!condition) {
                return;
            }
            const auto branch_index =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            const auto unknown_policy =
                language_ == frontend::Language::Vhdl2008
                    ? UnknownBranchPolicy::error
                    : UnknownBranchPolicy::when_false;
            process_.operations.emplace_back(
                Branch{
                    *condition,
                    loop_start,
                    branch_index + 1,
                    unknown_policy});
            const auto end =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            for (const auto jump :
                 loop_control.break_jumps) {
                process_.operations[jump] = Jump{end};
            }
            return;
        }
        const auto condition = lower_condition(
            statement.condition, "FSIM-ELAB-077", "while");
        if (!condition) {
            return;
        }
        const auto branch_index =
            static_cast<InstructionIndex>(
                process_.operations.size());
        const auto unknown_policy =
            language_ == frontend::Language::Vhdl2008
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch{*condition, 0, 0, unknown_policy});
        const auto body_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        loop_controls_.push_back(
            LoopControlContext{
                loop_start,
                {},
                {},
                statement.loop_label});
        lower_statements(statement.statements);
        auto loop_control = std::move(loop_controls_.back());
        loop_controls_.pop_back();
        process_.operations.emplace_back(Jump{loop_start});
        const auto end =
            static_cast<InstructionIndex>(
                process_.operations.size());
        process_.operations[branch_index] = Branch{
            *condition, body_start, end, unknown_policy};
        for (const auto jump : loop_control.break_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }

    void lower_loop_control(
        const Statement& statement, const bool is_break) {
        if (loop_controls_.empty()) {
            report(
                "FSIM-ELAB-078",
                std::string{"a "}
                    + (is_break ? "break/exit" : "continue/next")
                    + " statement has no enclosing loop",
                statement.span);
            return;
        }
        auto context = std::prev(loop_controls_.end());
        if (!statement.loop_control_label.empty()) {
            const auto found = std::find_if(
                loop_controls_.rbegin(),
                loop_controls_.rend(),
                [&](const LoopControlContext& candidate) {
                    return candidate.label
                        == statement.loop_control_label;
                });
            if (found == loop_controls_.rend()) {
                report(
                    "FSIM-ELAB-080",
                    "loop-control target '"
                        + statement.loop_control_label
                        + "' has no enclosing loop",
                    statement.span);
                return;
            }
            context = std::prev(found.base());
        }
        const auto jump =
            static_cast<InstructionIndex>(
                process_.operations.size());
        if (!is_break
            && context->continue_target) {
            process_.operations.emplace_back(
                Jump{*context->continue_target});
            return;
        }
        process_.operations.emplace_back(Jump{0});
        auto& targets =
            is_break
                ? context->break_jumps
                : context->continue_jumps;
        targets.push_back(jump);
    }

    struct PackedMemberReference {
        std::string base;
        const frontend::PackedMember* member{};
    };

    std::optional<PackedMemberReference> packed_member_reference(
        const std::string_view name) const {
        const auto separator = name.rfind('.');
        if (separator == std::string_view::npos
            || separator == 0
            || separator + 1 >= name.size()) {
            return std::nullopt;
        }
        const auto base = std::string{name.substr(0, separator)};
        const auto member_name = name.substr(separator + 1);
        const std::vector<frontend::PackedMember>* members = nullptr;
        if (const auto local = local_members_.find(base);
            local != local_members_.end()) {
            members = &local->second;
        } else if (const auto signal = signals_.find(base);
                   signal != signals_.end()) {
            members =
                &design_.signal_info_[signal->second].packed_members;
        }
        if (members == nullptr) {
            return std::nullopt;
        }
        const auto member = std::find_if(
            members->begin(),
            members->end(),
            [&](const frontend::PackedMember& candidate) {
                return candidate.name == member_name;
            });
        if (member == members->end()) {
            return std::nullopt;
        }
        return PackedMemberReference{base, &*member};
    }

    std::optional<RegisterId> lower_expression(
        const Expression& expression, const std::size_t expected_width) {
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                return local->second;
            }
            const auto found = signals_.find(expression.text);
            if (found != signals_.end()) {
                const auto& signal =
                    design_.signal_info_[found->second];
                const auto destination = allocate_register(
                    signal.width, signal.source_domain);
                process_.operations.emplace_back(
                    ReadSignal{destination, found->second});
                return destination;
            }
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                const auto base_width =
                    infer_width(Expression{
                        ExpressionKind::Identifier,
                        selected->base,
                        {},
                        expression.span});
                const auto member_width =
                    selected->member->width();
                if (!base_width || !member_width
                    || *member_width == 0
                    || selected->member->lsb_offset
                        > std::numeric_limits<std::uint32_t>::max()
                    || *member_width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVSTRUCT-002",
                        "packed aggregate member '" + expression.text
                            + "' has no executable layout",
                        expression.span);
                    return std::nullopt;
                }
                const auto source = lower_expression(
                    Expression{
                        ExpressionKind::Identifier,
                        selected->base,
                        {},
                        expression.span},
                    *base_width);
                if (!source) {
                    return std::nullopt;
                }
                const auto destination = allocate_register(
                    *member_width,
                    selected->member->domain);
                process_.operations.emplace_back(Extract{
                    destination,
                    *source,
                    static_cast<std::uint32_t>(
                        selected->member->lsb_offset),
                    static_cast<std::uint32_t>(*member_width)});
                return destination;
            }
            {
                report(
                    "FSIM-ELAB-040",
                    "unknown identifier '" + expression.text + "'",
                    expression.span);
                return std::nullopt;
            }
        }
        if (expression.kind == ExpressionKind::IntegerLiteral
            || expression.kind == ExpressionKind::BooleanLiteral
            || expression.kind == ExpressionKind::LogicLiteral
            || expression.kind == ExpressionKind::StringLiteral) {
            const auto literal =
                literal_value(expression, expected_width, language_);
            if (!literal) {
                report(
                    "FSIM-ELAB-041",
                    "unsupported or malformed literal '" + expression.text + "'",
                    expression.span);
                return std::nullopt;
            }
            const auto destination =
                allocate_register(literal->value.width(), literal->domain);
            process_.operations.emplace_back(
                LoadConstant{destination, std::move(literal->value)});
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1
            && (locals_.contains(expression.text)
                || signals_.contains(expression.text))) {
            return lower_expression(
                Expression{
                    ExpressionKind::Index,
                    "index",
                    {
                        Expression{
                            ExpressionKind::Identifier,
                            expression.text,
                            {},
                            expression.span},
                        expression.operands[0]},
                    expression.span},
                expected_width);
        }
        if (expression.kind == ExpressionKind::Call) {
            emit_debug_point(
                DebugPointKind::call, expression.span);
        }
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2) {
            const auto source_width =
                infer_width(expression.operands[0]);
            const auto index =
                constant_index(expression.operands[1]);
            if (!source_width || !index) {
                report(
                    "FSIM-ELAB-068",
                    "a bit-select requires an inferable packed source and "
                    "a constant integer index",
                    expression.span);
                return std::nullopt;
            }
            const auto offset = select_offset(
                expression.operands[0], *index, *source_width);
            if (!offset
                || *offset
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "bit-select index "
                        + std::to_string(*index)
                        + " is outside the source's declared packed range",
                    expression.span);
                return std::nullopt;
            }
            const auto source =
                lower_expression(
                    expression.operands[0], *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination =
                allocate_register(1, register_domain(*source));
            process_.operations.emplace_back(Extract{
                destination,
                *source,
                static_cast<std::uint32_t>(*offset),
                1});
            return destination;
        }
        if (expression.kind == ExpressionKind::Slice
            && expression.operands.size() == 3) {
            const auto source_width =
                infer_width(expression.operands[0]);
            const auto selection =
                source_width
                    ? constant_slice_selection(
                          expression, *source_width)
                    : std::nullopt;
            if (!source_width || !selection
                || selection->offset
                    > std::numeric_limits<std::uint32_t>::max()
                || selection->width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "a part-select requires an inferable packed source, "
                    "constant in-range bounds, a positive indexed width, "
                    "and compatible direction",
                    expression.span);
                return std::nullopt;
            }
            const auto source =
                lower_expression(
                    expression.operands[0], *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                selection->width,
                register_domain(*source));
            process_.operations.emplace_back(Extract{
                destination,
                *source,
                static_cast<std::uint32_t>(selection->offset),
                static_cast<std::uint32_t>(selection->width)});
            return destination;
        }
        if (language_ != frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Replication) {
            std::string count_error;
            const auto count =
                expression.operands.empty()
                    ? std::nullopt
                    : evaluate_constant_expression(
                          expression.operands[0],
                          {},
                          count_error);
            if (!count || *count <= 0
                || expression.operands.size() < 2) {
                report(
                    "FSIM-ELAB-SVREPL-001",
                    "a replication concatenation requires a positive "
                    "constant count and at least one packed operand",
                    expression.span);
                return std::nullopt;
            }
            std::vector<RegisterId> group_operands;
            group_operands.reserve(expression.operands.size() - 1);
            std::size_t group_width = 0;
            auto result_domain = frontend::ValueDomain::Bit2;
            for (std::size_t index = 1;
                 index < expression.operands.size();
                 ++index) {
                const auto& operand_expression =
                    expression.operands[index];
                const auto operand_width =
                    infer_width(operand_expression);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - group_width) {
                    report(
                        "FSIM-ELAB-SVREPL-001",
                        "a replication operand width is not statically "
                        "inferable or the group width overflows",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto operand = lower_expression(
                    operand_expression, *operand_width);
                if (!operand) {
                    return std::nullopt;
                }
                group_operands.push_back(*operand);
                group_width += register_width(*operand);
                const auto domain = register_domain(*operand);
                if (domain == frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic9;
                } else if (
                    domain != frontend::ValueDomain::Bit2
                    && domain != frontend::ValueDomain::Boolean
                    && result_domain
                        != frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic4;
                }
            }
            const auto repetition_count =
                static_cast<std::uint64_t>(*count);
            if (group_width == 0
                || repetition_count
                    > std::numeric_limits<std::uint32_t>::max()
                          / group_width) {
                report(
                    "FSIM-ELAB-SVREPL-001",
                    "replication result width is outside the supported "
                    "range",
                    expression.span);
                return std::nullopt;
            }
            RegisterId group = group_operands.front();
            if (group_operands.size() > 1) {
                group = allocate_register(
                    group_width, result_domain);
                process_.operations.emplace_back(Concatenate{
                    group,
                    std::move(group_operands),
                    static_cast<std::uint32_t>(group_width)});
            }

            std::optional<RegisterId> result;
            std::size_t result_width = 0;
            auto block = group;
            auto block_width = group_width;
            auto remaining = repetition_count;
            while (remaining != 0) {
                if ((remaining & 1U) != 0) {
                    if (!result) {
                        result = block;
                        result_width = block_width;
                    } else {
                        const auto combined_width =
                            result_width + block_width;
                        const auto combined = allocate_register(
                            combined_width, result_domain);
                        process_.operations.emplace_back(
                            Concatenate{
                                combined,
                                {*result, block},
                                static_cast<std::uint32_t>(
                                    combined_width)});
                        result = combined;
                        result_width = combined_width;
                    }
                }
                remaining >>= 1U;
                if (remaining != 0) {
                    const auto doubled_width =
                        block_width * 2U;
                    const auto doubled = allocate_register(
                        doubled_width, result_domain);
                    process_.operations.emplace_back(
                        Concatenate{
                            doubled,
                            {block, block},
                            static_cast<std::uint32_t>(
                                doubled_width)});
                    block = doubled;
                    block_width = doubled_width;
                }
            }
            return result;
        }
        if (language_ != frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Concatenation) {
            if (expression.operands.empty()) {
                report(
                    "FSIM-ELAB-069",
                    "a concatenation requires at least one packed operand",
                    expression.span);
                return std::nullopt;
            }
            std::vector<RegisterId> operands;
            operands.reserve(expression.operands.size());
            std::size_t width = 0;
            auto result_domain = frontend::ValueDomain::Bit2;
            for (const auto& operand_expression : expression.operands) {
                const auto operand_width =
                    infer_width(operand_expression);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - width) {
                    report(
                        "FSIM-ELAB-069",
                        "concatenation operand width is not statically "
                        "inferable or the total width overflows",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto operand =
                    lower_expression(
                        operand_expression, *operand_width);
                if (!operand) {
                    return std::nullopt;
                }
                operands.push_back(*operand);
                width += register_width(*operand);
                const auto domain = register_domain(*operand);
                if (domain == frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic9;
                } else if (
                    domain != frontend::ValueDomain::Bit2
                    && domain != frontend::ValueDomain::Boolean
                    && result_domain
                        != frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic4;
                }
            }
            if (width == 0
                || width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-069",
                    "concatenation result width is outside the supported "
                    "range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination =
                allocate_register(width, result_domain);
            process_.operations.emplace_back(Concatenate{
                destination,
                std::move(operands),
                static_cast<std::uint32_t>(width)});
            return destination;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Binary
            && expression.text == "&"
            && expression.operands.size() == 2) {
            std::vector<RegisterId> operands;
            operands.reserve(2);
            std::size_t width = 0;
            auto result_domain = frontend::ValueDomain::Bit2;
            for (const auto& operand_expression : expression.operands) {
                const auto operand_width =
                    infer_width(operand_expression);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::uint32_t>::max()
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - width) {
                    report(
                        "FSIM-ELAB-069",
                        "VHDL concatenation operand width is not "
                        "statically inferable or the total width "
                        "overflows",
                        operand_expression.span);
                    return std::nullopt;
                }
                const auto operand = lower_expression(
                    operand_expression, *operand_width);
                if (!operand) {
                    return std::nullopt;
                }
                operands.push_back(*operand);
                width += register_width(*operand);
                const auto domain = register_domain(*operand);
                if (domain == frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic9;
                } else if (
                    domain != frontend::ValueDomain::Bit2
                    && domain != frontend::ValueDomain::Boolean
                    && result_domain
                        != frontend::ValueDomain::Logic9) {
                    result_domain = frontend::ValueDomain::Logic4;
                }
            }
            if (width == 0
                || width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-069",
                    "VHDL concatenation result width is outside the "
                    "supported range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination =
                allocate_register(width, result_domain);
            process_.operations.emplace_back(Concatenate{
                destination,
                std::move(operands),
                static_cast<std::uint32_t>(width)});
            return destination;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && expression.text == "!") {
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source =
                lower_expression(
                    expression.operands[0], source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto source_domain = register_domain(*source);
            const auto result_domain =
                source_domain == frontend::ValueDomain::Bit2
                        || source_domain
                            == frontend::ValueDomain::Boolean
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            const auto destination =
                allocate_register(1, result_domain);
            process_.operations.emplace_back(
                LogicalNot{destination, *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && (expression.text == "&"
                || expression.text == "|"
                || expression.text == "^"
                || expression.text == "~&"
                || expression.text == "~|"
                || expression.text == "~^"
                || expression.text == "^~")) {
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source =
                lower_expression(
                    expression.operands[0], source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto source_domain = register_domain(*source);
            const auto result_domain =
                source_domain == frontend::ValueDomain::Bit2
                        || source_domain
                            == frontend::ValueDomain::Boolean
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            auto operation = ReductionOperator::bit_xor;
            if (expression.text == "&"
                || expression.text == "~&") {
                operation = ReductionOperator::bit_and;
            } else if (expression.text == "|"
                       || expression.text == "~|") {
                operation = ReductionOperator::bit_or;
            }
            const auto destination =
                allocate_register(1, result_domain);
            process_.operations.emplace_back(
                Reduction{operation, destination, *source});
            if (expression.text.starts_with("~")
                || expression.text == "^~") {
                const auto inverted =
                    allocate_register(1, result_domain);
                process_.operations.emplace_back(
                    UnaryNot{inverted, destination});
                return inverted;
            }
            return destination;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && (expression.text == "+"
                || expression.text == "-")) {
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source =
                lower_expression(
                    expression.operands[0], source_width);
            if (!source || expression.text == "+") {
                return source;
            }
            const auto zero = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(LoadConstant{
                zero,
                PackedLogic4(
                    register_width(*source), Logic4::zero)});
            const auto destination = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(Binary{
                is_signed_expression(expression.operands[0])
                    ? BinaryOperator::subtract_signed
                    : BinaryOperator::subtract_unsigned,
                destination,
                zero,
                *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && expression.text == "abs") {
            if (language_ != frontend::Language::Vhdl2008
                || !is_signed_expression(expression.operands[0])) {
                report(
                    "FSIM-ELAB-082",
                    "the bounded packed abs operator requires a signed "
                    "VHDL operand",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto source = lower_expression(
                expression.operands[0], source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto zero = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(LoadConstant{
                zero,
                PackedLogic4(
                    register_width(*source), Logic4::zero)});
            const auto negated = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(Binary{
                BinaryOperator::subtract_signed,
                negated,
                zero,
                *source});
            const auto sign = allocate_register(
                1, register_domain(*source));
            process_.operations.emplace_back(Extract{
                sign,
                *source,
                static_cast<std::uint32_t>(
                    register_width(*source) - 1U),
                1});
            const auto destination = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(ConditionalSelect{
                destination,
                sign,
                negated,
                *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Unary
            && expression.operands.size() == 1
            && (expression.text == "not" || expression.text == "~")) {
            const auto source = lower_expression(expression.operands[0], expected_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                register_width(*source), register_domain(*source));
            process_.operations.emplace_back(UnaryNot{destination, *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'event") {
            if (expression.operands.size() != 1
                || expression.operands.front().kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-094",
                    "'event requires one signal name and no dimension",
                    expression.span);
                return std::nullopt;
            }
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-094",
                    "'event object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                SignalEvent{destination, signal->second});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'last_value") {
            if (expression.operands.size() != 1
                || expression.operands.front().kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-095",
                    "'last_value requires one signal name and no dimension",
                    expression.span);
                return std::nullopt;
            }
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-095",
                    "'last_value object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto& info = design_.signal_info_[signal->second];
            const auto destination =
                allocate_register(info.width, info.source_domain);
            process_.operations.emplace_back(
                SignalLastValue{destination, signal->second});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'last_event") {
            if (expression.operands.size() != 1
                || expression.operands.front().kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-096",
                    "'last_event requires one signal name and no duration",
                    expression.span);
                return std::nullopt;
            }
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-096",
                    "'last_event object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                64, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                SignalLastEvent{destination, signal->second});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'stable") {
            if (expression.operands.size() != 1
                || expression.operands.front().kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-097",
                    "bounded 'stable supports one signal name and its "
                    "default zero duration",
                    expression.span);
                return std::nullopt;
            }
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-097",
                    "'stable object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto event = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                SignalEvent{event, signal->second});
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                UnaryNot{destination, event});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'active") {
            if (expression.operands.size() != 1
                || expression.operands.front().kind
                    != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-098",
                    "'active requires one signal name and no duration",
                    expression.span);
                return std::nullopt;
            }
            const auto signal =
                signals_.find(expression.operands.front().text);
            if (signal == signals_.end()) {
                report(
                    "FSIM-ELAB-098",
                    "'active object is not a visible signal",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(
                SignalActive{destination, signal->second});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && (expression.text == "'left"
                || expression.text == "'right"
                || expression.text == "'low"
                || expression.text == "'high"
                || expression.text == "'length"
                || expression.text == "'ascending")) {
            if (expression.operands.empty()
                || expression.operands.size() > 2) {
                report(
                    "FSIM-ELAB-093",
                    expression.text
                        + " requires one bounded array object and at "
                          "most one dimension",
                    expression.span);
                return std::nullopt;
            }
            if (expression.operands.size() == 2) {
                const auto dimension =
                    constant_index(expression.operands[1]);
                if (!dimension || *dimension != 1) {
                    report(
                        "FSIM-ELAB-093",
                        expression.text
                            + " supports only the constant dimension 1",
                        expression.operands[1].span);
                    return std::nullopt;
                }
            }
            const auto operand_width =
                infer_width(expression.operands.front());
            const auto range =
                operand_width
                    ? expression_range(
                          expression.operands.front(),
                          *operand_width)
                    : std::nullopt;
            if (!operand_width || !range || *operand_width == 0
                || *operand_width
                    > std::numeric_limits<std::int32_t>::max()) {
                report(
                    "FSIM-ELAB-093",
                    expression.text
                        + " cannot infer a representable static packed "
                          "range for its object",
                    expression.operands.front().span);
                return std::nullopt;
            }
            if (expression.text == "'ascending") {
                const auto destination = allocate_register(
                    1, frontend::ValueDomain::Boolean);
                process_.operations.emplace_back(LoadConstant{
                    destination,
                    PackedLogic4(
                        1,
                        range->descending
                            ? Logic4::zero
                            : Logic4::one)});
                return destination;
            }
            std::int64_t result = 0;
            if (expression.text == "'left") {
                result = range->left;
            } else if (expression.text == "'right") {
                result = range->right;
            } else if (expression.text == "'low") {
                result = std::min(range->left, range->right);
            } else if (expression.text == "'high") {
                result = std::max(range->left, range->right);
            } else {
                result = static_cast<std::int64_t>(*operand_width);
            }
            if (result < std::numeric_limits<std::int32_t>::min()
                || result
                    > std::numeric_limits<std::int32_t>::max()) {
                report(
                    "FSIM-ELAB-093",
                    expression.text
                        + " result is outside the bounded 32-bit "
                          "integer range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(
                    static_cast<std::uint32_t>(result), 32)});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && language_ != frontend::Language::Vhdl2008
            && (expression.text == "$signed"
                || expression.text == "$unsigned")) {
            if (expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-083",
                    expression.text
                        + " requires exactly one packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front())
                    .value_or(expected_width);
            return lower_expression(
                expression.operands.front(), source_width);
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$urandom"
                || expression.text == "$random")) {
            const bool urandom = expression.text == "$urandom";
            if ((urandom
                 && language_
                     != frontend::Language::SystemVerilog2017)
                || (!urandom
                    && language_
                        == frontend::Language::Vhdl2008)
                || !expression.operands.empty()) {
                report(
                    "FSIM-ELAB-104",
                    expression.text
                        + " requires no arguments"
                          + (urandom ? " in SystemVerilog" : ""),
                    expression.span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                RandomValue{
                    destination,
                    urandom
                        ? RandomKind::urandom
                        : RandomKind::random,
                    std::nullopt,
                    std::nullopt});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$urandom_range") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.empty()
                || expression.operands.size() > 2) {
                report(
                    "FSIM-ELAB-104",
                    "$urandom_range requires one maximum and an "
                    "optional minimum argument in SystemVerilog",
                    expression.span);
                return std::nullopt;
            }
            const auto maximum =
                lower_expression(expression.operands[0], 32);
            if (!maximum) {
                return std::nullopt;
            }
            std::optional<RegisterId> minimum;
            if (expression.operands.size() == 2) {
                minimum =
                    lower_expression(expression.operands[1], 32);
                if (!minimum) {
                    return std::nullopt;
                }
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                RandomValue{
                    destination,
                    RandomKind::urandom_range,
                    *maximum,
                    minimum});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$isunknown") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-084",
                    "$isunknown requires SystemVerilog and exactly one "
                    "packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front())
                    .value_or(expected_width);
            const auto source = lower_expression(
                expression.operands.front(), source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto self_equal = allocate_register(
                1, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(Binary{
                BinaryOperator::equal,
                self_equal,
                *source,
                *source});
            const auto unknown = allocate_register(
                1, frontend::ValueDomain::Logic4);
            process_.operations.emplace_back(LoadConstant{
                unknown, PackedLogic4(1, Logic4::x)});
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary{
                BinaryOperator::case_equal,
                destination,
                self_equal,
                unknown});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$bits") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-085",
                    "$bits requires SystemVerilog and exactly one "
                    "statically sized packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto operand_width =
                infer_width(expression.operands.front());
            if (!operand_width || *operand_width == 0
                || *operand_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-085",
                    "$bits cannot infer a representable static packed "
                    "width for its argument",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(*operand_width, 32)});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$left"
                || expression.text == "$right"
                || expression.text == "$low"
                || expression.text == "$high"
                || expression.text == "$size"
                || expression.text == "$increment")) {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.empty()
                || expression.operands.size() > 2) {
                report(
                    "FSIM-ELAB-086",
                    expression.text
                        + " requires SystemVerilog, one "
                          "one-dimensional packed argument, and at "
                          "most one dimension argument",
                    expression.span);
                return std::nullopt;
            }
            if (expression.operands.size() == 2) {
                const auto dimension =
                    constant_index(expression.operands[1]);
                if (!dimension || *dimension != 1) {
                    report(
                        "FSIM-ELAB-086",
                        expression.text
                            + " supports only the constant packed "
                              "dimension 1",
                        expression.operands[1].span);
                    return std::nullopt;
                }
            }
            const auto operand_width =
                infer_width(expression.operands.front());
            const auto range =
                operand_width
                    ? expression_range(
                          expression.operands.front(),
                          *operand_width)
                    : std::nullopt;
            if (!operand_width || !range
                || *operand_width == 0
                || *operand_width
                    > std::numeric_limits<std::int32_t>::max()) {
                report(
                    "FSIM-ELAB-086",
                    expression.text
                        + " cannot infer a representable static packed "
                          "range for its argument",
                    expression.operands.front().span);
                return std::nullopt;
            }
            std::int64_t result = 0;
            if (expression.text == "$left") {
                result = range->left;
            } else if (expression.text == "$right") {
                result = range->right;
            } else if (expression.text == "$low") {
                result = std::min(range->left, range->right);
            } else if (expression.text == "$high") {
                result = std::max(range->left, range->right);
            } else if (expression.text == "$increment") {
                result =
                    range->left >= range->right ? 1 : -1;
            } else {
                result = static_cast<std::int64_t>(*operand_width);
            }
            if (result < std::numeric_limits<std::int32_t>::min()
                || result
                    > std::numeric_limits<std::int32_t>::max()) {
                report(
                    "FSIM-ELAB-086",
                    expression.text
                        + " result is outside the bounded 32-bit "
                          "integer range",
                    expression.span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(
                    static_cast<std::uint32_t>(result), 32)});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$dimensions"
                || expression.text == "$unpacked_dimensions")) {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-090",
                    expression.text
                        + " requires SystemVerilog and exactly one "
                          "statically sized packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto operand_width =
                infer_width(expression.operands.front());
            const auto range =
                operand_width
                    ? expression_range(
                          expression.operands.front(),
                          *operand_width)
                    : std::nullopt;
            if (!operand_width || !range || *operand_width == 0) {
                report(
                    "FSIM-ELAB-090",
                    expression.text
                        + " cannot infer a static packed dimension "
                          "for its argument",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(
                    expression.text == "$dimensions" ? 1 : 0,
                    32)});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$onehot"
                || expression.text == "$onehot0")) {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-087",
                    expression.text
                        + " requires SystemVerilog and exactly one "
                          "packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front())
                    .value_or(expected_width);
            const auto source = lower_expression(
                expression.operands.front(), source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Reduction{
                expression.text == "$onehot"
                    ? ReductionOperator::one_hot
                    : ReductionOperator::one_hot_or_zero,
                destination,
                *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countones") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 1) {
                report(
                    "FSIM-ELAB-088",
                    "$countones requires SystemVerilog and exactly one "
                    "packed argument",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front());
            if (!source_width || *source_width == 0
                || *source_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-088",
                    "$countones cannot infer a representable static "
                    "packed width for its argument",
                    expression.operands.front().span);
                return std::nullopt;
            }
            const auto source = lower_expression(
                expression.operands.front(), *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                CountOnes{destination, *source});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countbits") {
            if (language_
                    != frontend::Language::SystemVerilog2017
                || expression.operands.size() < 2) {
                report(
                    "FSIM-ELAB-089",
                    "$countbits requires SystemVerilog, one packed "
                    "expression, and at least one constant one-bit "
                    "control",
                    expression.span);
                return std::nullopt;
            }
            const auto source_width =
                infer_width(expression.operands.front());
            if (!source_width || *source_width == 0
                || *source_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-089",
                    "$countbits cannot infer a representable static "
                    "packed width for its expression",
                    expression.operands.front().span);
                return std::nullopt;
            }
            std::uint8_t state_mask = 0;
            for (std::size_t index = 1;
                 index < expression.operands.size();
                 ++index) {
                const auto control = literal_value(
                    expression.operands[index],
                    1,
                    frontend::Language::SystemVerilog2017);
                if (!control || control->value.width() != 1) {
                    report(
                        "FSIM-ELAB-089",
                        "$countbits controls must be constant one-bit "
                        "0, 1, X, or Z values",
                        expression.operands[index].span);
                    return std::nullopt;
                }
                const auto state = static_cast<std::uint8_t>(
                    control->value.get(0));
                state_mask |=
                    static_cast<std::uint8_t>(
                        std::uint8_t{1} << state);
            }
            const auto source = lower_expression(
                expression.operands.front(), *source_width);
            if (!source) {
                return std::nullopt;
            }
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                CountBits{
                    destination, *source, state_mask});
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "rising_edge"
                || expression.text == "falling_edge")) {
            report(
                "FSIM-ELAB-045",
                "a VHDL edge predicate is executable only as the sole, "
                "else-free outer statement of a sensitive process",
                expression.span);
            return std::nullopt;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "?:"
            && expression.operands.size() == 3) {
            const auto condition =
                lower_expression(expression.operands[0], 1);
            if (!condition) {
                return std::nullopt;
            }
            if (register_width(*condition) != 1) {
                report(
                    "FSIM-ELAB-064",
                    "a conditional-expression condition must produce one "
                    "bit in this executable slice",
                    expression.operands[0].span);
                return std::nullopt;
            }
            if (language_ == frontend::Language::Vhdl2008
                && register_domain(*condition)
                    != frontend::ValueDomain::Boolean) {
                report(
                    "FSIM-ELAB-092",
                    "a VHDL conditional-assignment condition must have "
                    "type boolean",
                    expression.operands[0].span);
                return std::nullopt;
            }
            const auto value_width =
                infer_width(expression.operands[1])
                    .value_or(
                        infer_width(expression.operands[2])
                            .value_or(expected_width));
            const auto when_true =
                lower_expression(expression.operands[1], value_width);
            const auto when_false =
                lower_expression(expression.operands[2], value_width);
            if (!when_true || !when_false) {
                return std::nullopt;
            }
            if (register_width(*when_true)
                != register_width(*when_false)) {
                report(
                    "FSIM-ELAB-065",
                    "conditional-expression alternatives have different "
                    "widths ("
                        + std::to_string(register_width(*when_true))
                        + " and "
                        + std::to_string(register_width(*when_false))
                        + ")",
                    expression.span);
                return std::nullopt;
            }
            const auto is_two_state =
                [](const frontend::ValueDomain domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Boolean;
                };
            const auto result_domain =
                is_two_state(register_domain(*when_true))
                        && is_two_state(register_domain(*when_false))
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            const auto destination =
                allocate_register(
                    register_width(*when_true), result_domain);
            process_.operations.emplace_back(ConditionalSelect{
                destination,
                *condition,
                *when_true,
                *when_false});
            return destination;
        }
        if (expression.kind == ExpressionKind::Binary
            && expression.operands.size() == 2
            && (expression.text == "&&"
                || expression.text == "||")) {
            const auto lhs_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto rhs_width =
                infer_width(expression.operands[1])
                    .value_or(expected_width);
            const auto lhs =
                lower_expression(expression.operands[0], lhs_width);
            const auto rhs =
                lower_expression(expression.operands[1], rhs_width);
            if (!lhs || !rhs) {
                return std::nullopt;
            }
            const auto is_two_state =
                [](const frontend::ValueDomain domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Boolean;
                };
            const auto result_domain =
                is_two_state(register_domain(*lhs))
                        && is_two_state(register_domain(*rhs))
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            const auto destination =
                allocate_register(1, result_domain);
            process_.operations.emplace_back(LogicalBinary{
                expression.text == "&&"
                    ? LogicalBinaryOperator::logical_and
                    : LogicalBinaryOperator::logical_or,
                destination,
                *lhs,
                *rhs});
            return destination;
        }
        if (expression.kind == ExpressionKind::Binary
            && expression.operands.size() == 2
            && (expression.text == "<<"
                || expression.text == ">>"
                || expression.text == "<<<"
                || expression.text == ">>>"
                || expression.text == "sll"
                || expression.text == "srl"
                || expression.text == "sla"
                || expression.text == "sra"
                || expression.text == "rol"
                || expression.text == "ror")) {
            auto effective_operator = expression.text;
            std::optional<std::uint64_t> static_amount;
            if (language_ == frontend::Language::Vhdl2008) {
                const auto count =
                    constant_index(expression.operands[1]);
                if (!count) {
                    report(
                        "FSIM-ELAB-070",
                        "VHDL packed shifts and rotates currently require "
                        "a locally static integer count",
                        expression.operands[1].span);
                    return std::nullopt;
                }
                static_amount = index_distance(*count, 0);
                if (*count < 0) {
                    if (effective_operator == "sll") {
                        effective_operator = "srl";
                    } else if (effective_operator == "srl") {
                        effective_operator = "sll";
                    } else if (effective_operator == "sla") {
                        effective_operator = "sra";
                    } else if (effective_operator == "sra") {
                        effective_operator = "sla";
                    } else if (effective_operator == "rol") {
                        effective_operator = "ror";
                    } else if (effective_operator == "ror") {
                        effective_operator = "rol";
                    }
                }
            }
            const auto value_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            auto amount_width =
                infer_width(expression.operands[1])
                    .value_or(expected_width);
            if (static_amount) {
                auto magnitude = *static_amount;
                std::size_t required_width = 1;
                while (magnitude > 1) {
                    ++required_width;
                    magnitude >>= 1U;
                }
                amount_width =
                    std::max(amount_width, required_width);
            }
            const auto value =
                lower_expression(
                    expression.operands[0], value_width);
            std::optional<RegisterId> amount;
            if (static_amount) {
                amount = allocate_register(
                    amount_width, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant{
                    *amount,
                    unsigned_value(*static_amount, amount_width)});
            } else {
                amount = lower_expression(
                    expression.operands[1], amount_width);
            }
            if (!value || !amount) {
                return std::nullopt;
            }
            const auto is_two_state =
                [](const frontend::ValueDomain domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Boolean;
                };
            const auto result_domain =
                is_two_state(register_domain(*value))
                        && is_two_state(register_domain(*amount))
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            const auto destination =
                allocate_register(
                    register_width(*value), result_domain);
            process_.operations.emplace_back(Shift{
                effective_operator == ">>"
                    || effective_operator == "srl"
                    || (effective_operator == ">>>"
                        && !is_signed_expression(
                            expression.operands[0]))
                    ? ShiftOperator::logical_right
                    : effective_operator == ">>>"
                            || effective_operator == "sra"
                        ? ShiftOperator::arithmetic_right
                        : effective_operator == "sla"
                            ? ShiftOperator::arithmetic_left
                        : effective_operator == "rol"
                            ? ShiftOperator::rotate_left
                        : effective_operator == "ror"
                            ? ShiftOperator::rotate_right
                        : ShiftOperator::logical_left,
                destination,
                *value,
                *amount});
            return destination;
        }
        if (expression.kind == ExpressionKind::Binary && expression.operands.size() == 2) {
            const auto width = infer_width(expression).value_or(expected_width);
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "**") {
                const auto exponent =
                    constant_index(expression.operands[1]);
                if (!exponent || *exponent < 0) {
                    report(
                        "FSIM-ELAB-091",
                        "bounded VHDL integer exponentiation requires "
                        "a locally static nonnegative exponent",
                        expression.operands[1].span);
                    return std::nullopt;
                }
            }
            const auto lhs = lower_expression(expression.operands[0], width);
            const auto rhs = lower_expression(expression.operands[1], width);
            if (!lhs || !rhs) {
                return std::nullopt;
            }
            if (register_width(*lhs) != register_width(*rhs)) {
                report(
                    "FSIM-ELAB-049",
                    "binary operator operands have different widths ("
                        + std::to_string(register_width(*lhs)) + " and "
                        + std::to_string(register_width(*rhs))
                        + "); implicit sizing is not executable in this slice",
                    expression.span);
                return std::nullopt;
            }
            std::optional<BinaryOperator> operation;
            bool invert_result = false;
            if (expression.text == "&" || expression.text == "and"
                || expression.text == "nand") {
                operation = BinaryOperator::bit_and;
                invert_result = expression.text == "nand";
            } else if (expression.text == "|" || expression.text == "or"
                       || expression.text == "nor") {
                operation = BinaryOperator::bit_or;
                invert_result = expression.text == "nor";
            } else if (expression.text == "^" || expression.text == "xor"
                       || expression.text == "xnor"
                       || expression.text == "~^"
                       || expression.text == "^~") {
                operation = BinaryOperator::bit_xor;
                invert_result =
                    expression.text == "xnor"
                    || expression.text == "~^"
                    || expression.text == "^~";
            } else if (expression.text == "+") {
                operation = BinaryOperator::add_unsigned;
            } else if (expression.text == "-") {
                operation = BinaryOperator::subtract_unsigned;
            } else if (expression.text == "*") {
                operation = BinaryOperator::multiply_unsigned;
            } else if (expression.text == "**") {
                operation = BinaryOperator::power_unsigned;
            } else if (expression.text == "/") {
                operation = BinaryOperator::divide_unsigned;
            } else if (
                language_ != frontend::Language::Vhdl2008
                && expression.text == "%") {
                operation = BinaryOperator::modulo_unsigned;
            } else if (
                language_ == frontend::Language::Vhdl2008
                && (expression.text == "mod"
                    || expression.text == "rem")) {
                operation = BinaryOperator::modulo_unsigned;
            } else if (
                expression.text == "=" || expression.text == "==") {
                operation = BinaryOperator::equal;
            } else if (
                language_ != frontend::Language::Vhdl2008
                && (expression.text == "==="
                    || expression.text == "!==")) {
                operation = BinaryOperator::case_equal;
                invert_result = expression.text == "!==";
            } else if (
                language_ == frontend::Language::SystemVerilog2017
                && (expression.text == "==?"
                    || expression.text == "!=?")) {
                operation = BinaryOperator::wildcard_equal;
                invert_result = expression.text == "!=?";
            } else if (
                language_ != frontend::Language::Vhdl2008
                && expression.text == "!=") {
                operation = BinaryOperator::not_equal;
            } else if (
                language_ == frontend::Language::Vhdl2008
                && expression.text == "/=") {
                operation = BinaryOperator::not_equal;
            } else if (expression.text == "<") {
                operation = BinaryOperator::less_unsigned;
            } else if (expression.text == "<=") {
                operation = BinaryOperator::less_equal_unsigned;
            } else if (expression.text == ">") {
                operation = BinaryOperator::greater_unsigned;
            } else if (expression.text == ">=") {
                operation = BinaryOperator::greater_equal_unsigned;
            }
            if (!operation) {
                report(
                    "FSIM-ELAB-042",
                    "operator '" + expression.text + "' is parsed but not executable yet",
                    expression.span);
                return std::nullopt;
            }
            const auto relational =
                *operation == BinaryOperator::less_unsigned
                || *operation
                    == BinaryOperator::less_equal_unsigned
                || *operation == BinaryOperator::greater_unsigned
                || *operation
                    == BinaryOperator::greater_equal_unsigned;
            const auto arithmetic =
                *operation == BinaryOperator::add_unsigned
                || *operation == BinaryOperator::subtract_unsigned
                || *operation == BinaryOperator::multiply_unsigned
                || *operation == BinaryOperator::power_unsigned
                || *operation == BinaryOperator::divide_unsigned
                || *operation == BinaryOperator::modulo_unsigned;
            const bool lhs_signed =
                is_signed_expression(expression.operands[0]);
            const bool rhs_signed =
                is_signed_expression(expression.operands[1]);
            const bool signed_operation =
                lhs_signed && rhs_signed;
            const bool contextual_integer =
                expression.operands[0].kind
                    == ExpressionKind::IntegerLiteral
                || expression.operands[1].kind
                    == ExpressionKind::IntegerLiteral;
            if (language_ == frontend::Language::Vhdl2008
                && (arithmetic || relational)
                && lhs_signed != rhs_signed
                && !contextual_integer) {
                report(
                    relational ? "FSIM-ELAB-066"
                               : "FSIM-ELAB-067",
                    "mixed signed/unsigned VHDL operands require an "
                    "explicit conversion",
                    expression.span);
                return std::nullopt;
            }
            if (signed_operation) {
                if (*operation == BinaryOperator::add_unsigned) {
                    operation = BinaryOperator::add_signed;
                } else if (
                    *operation
                    == BinaryOperator::subtract_unsigned) {
                    operation = BinaryOperator::subtract_signed;
                } else if (
                    *operation
                    == BinaryOperator::multiply_unsigned) {
                    operation = BinaryOperator::multiply_signed;
                } else if (
                    *operation
                    == BinaryOperator::power_unsigned) {
                    operation = BinaryOperator::power_signed;
                } else if (
                    *operation
                    == BinaryOperator::divide_unsigned) {
                    operation = BinaryOperator::divide_signed;
                } else if (
                    *operation
                    == BinaryOperator::modulo_unsigned) {
                    operation =
                        language_
                                    == frontend::Language::Vhdl2008
                                && expression.text == "mod"
                            ? BinaryOperator::modulo_signed
                            : BinaryOperator::remainder_signed;
                } else if (
                    *operation
                    == BinaryOperator::less_unsigned) {
                    operation = BinaryOperator::less_signed;
                } else if (
                    *operation
                    == BinaryOperator::less_equal_unsigned) {
                    operation =
                        BinaryOperator::less_equal_signed;
                } else if (
                    *operation
                    == BinaryOperator::greater_unsigned) {
                    operation = BinaryOperator::greater_signed;
                } else if (
                    *operation
                    == BinaryOperator::greater_equal_unsigned) {
                    operation =
                        BinaryOperator::greater_equal_signed;
                }
            }
            const auto scalar_result =
                *operation == BinaryOperator::equal
                || *operation == BinaryOperator::case_equal
                || *operation == BinaryOperator::wildcard_equal
                || *operation == BinaryOperator::not_equal
                || *operation == BinaryOperator::less_unsigned
                || *operation
                    == BinaryOperator::less_equal_unsigned
                || *operation == BinaryOperator::greater_unsigned
                || *operation
                    == BinaryOperator::greater_equal_unsigned
                || *operation == BinaryOperator::less_signed
                || *operation
                    == BinaryOperator::less_equal_signed
                || *operation == BinaryOperator::greater_signed
                || *operation
                    == BinaryOperator::greater_equal_signed;
            const auto result_width =
                scalar_result ? std::size_t{1}
                              : register_width(*lhs);
            const auto is_two_state =
                [](const frontend::ValueDomain domain) {
                    return domain == frontend::ValueDomain::Bit2
                        || domain == frontend::ValueDomain::Boolean;
                };
            auto result_domain =
                scalar_result
                    && language_
                        == frontend::Language::Vhdl2008
                ? frontend::ValueDomain::Boolean
                : language_
                            == frontend::Language::Vhdl2008
                        && register_domain(*lhs)
                            == frontend::ValueDomain::Boolean
                        && register_domain(*rhs)
                            == frontend::ValueDomain::Boolean
                    ? frontend::ValueDomain::Boolean
                : is_two_state(register_domain(*lhs))
                        && is_two_state(register_domain(*rhs))
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4;
            if (!scalar_result
                && (register_domain(*lhs)
                        == frontend::ValueDomain::Logic9
                    || register_domain(*rhs)
                        == frontend::ValueDomain::Logic9)) {
                result_domain = frontend::ValueDomain::Logic9;
            }
            const auto destination =
                allocate_register(result_width, result_domain);
            process_.operations.emplace_back(Binary{*operation, destination, *lhs, *rhs});
            if (invert_result) {
                const auto inverted =
                    allocate_register(result_width, result_domain);
                process_.operations.emplace_back(
                    UnaryNot{inverted, destination});
                return inverted;
            }
            return destination;
        }
        report(
            "FSIM-ELAB-043",
            "expression form is parsed but not executable yet",
            expression.span);
        return std::nullopt;
    }

    std::optional<std::size_t> infer_width(const Expression& expression) const {
        if (expression.kind == ExpressionKind::BooleanLiteral) {
            return std::size_t{1};
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1
            && (locals_.contains(expression.text)
                || signals_.contains(expression.text))) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Slice
            && expression.operands.size() == 3) {
            if (expression.text == "+:"
                || expression.text == "-:") {
                const auto width =
                    constant_index(expression.operands[2]);
                if (width && *width > 0
                    && static_cast<std::uint64_t>(*width)
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
                return std::nullopt;
            }
            const auto left = constant_index(expression.operands[1]);
            const auto right = constant_index(expression.operands[2]);
            if (left && right) {
                const auto width = index_distance(*left, *right) + 1;
                if (width
                    <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(width);
                }
            }
            return std::nullopt;
        }
        if (expression.kind == ExpressionKind::Concatenation) {
            std::size_t width = 0;
            for (const auto& operand : expression.operands) {
                const auto operand_width = infer_width(operand);
                if (!operand_width
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - width) {
                    return std::nullopt;
                }
                width += *operand_width;
            }
            return width == 0
                ? std::nullopt
                : std::optional<std::size_t>{width};
        }
        if (expression.kind == ExpressionKind::Replication) {
            if (expression.operands.size() < 2) {
                return std::nullopt;
            }
            std::string count_error;
            const auto count = evaluate_constant_expression(
                expression.operands[0], {}, count_error);
            if (!count || *count <= 0) {
                return std::nullopt;
            }
            std::size_t group_width = 0;
            for (std::size_t index = 1;
                 index < expression.operands.size();
                 ++index) {
                const auto operand_width =
                    infer_width(expression.operands[index]);
                if (!operand_width || *operand_width == 0
                    || *operand_width
                        > std::numeric_limits<std::size_t>::max()
                            - group_width) {
                    return std::nullopt;
                }
                group_width += *operand_width;
            }
            const auto repetitions =
                static_cast<std::uint64_t>(*count);
            if (group_width == 0
                || repetitions
                    > std::numeric_limits<std::size_t>::max()
                          / group_width) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(
                repetitions * group_width);
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Binary
            && expression.text == "&"
            && expression.operands.size() == 2) {
            const auto lhs = infer_width(expression.operands[0]);
            const auto rhs = infer_width(expression.operands[1]);
            if (!lhs || !rhs
                || *rhs
                    > std::numeric_limits<std::size_t>::max()
                        - *lhs) {
                return std::nullopt;
            }
            return *lhs + *rhs;
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::LogicLiteral) {
            return std::size_t{1};
        }
        if (language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::StringLiteral
            && expression.text.size() >= 2) {
            return expression.text.size() - 2;
        }
        if (expression.kind == ExpressionKind::LogicLiteral) {
            const auto quote = expression.text.find('\'');
            if (quote != std::string::npos && quote != 0) {
                const auto width = unsigned_decimal(
                    std::string_view{expression.text}.substr(0, quote));
                if (width && *width != 0
                    && *width
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
            }
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "?:"
            && expression.operands.size() == 3) {
            if (const auto width = infer_width(expression.operands[1])) {
                return width;
            }
            return infer_width(expression.operands[2]);
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$isunknown") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$bits") {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && (expression.text == "'left"
                || expression.text == "'right"
                || expression.text == "'low"
                || expression.text == "'high"
                || expression.text == "'length")) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'ascending") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'event") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'last_value"
            && expression.operands.size() == 1) {
            return infer_width(expression.operands.front());
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'last_event") {
            return std::size_t{64};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'stable") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && language_ == frontend::Language::Vhdl2008
            && expression.text == "'active") {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$left"
                || expression.text == "$right"
                || expression.text == "$low"
                || expression.text == "$high"
                || expression.text == "$size"
                || expression.text == "$increment"
                || expression.text == "$dimensions"
                || expression.text == "$unpacked_dimensions")) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$onehot"
                || expression.text == "$onehot0")) {
            return std::size_t{1};
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countones") {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "$countbits") {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "$urandom"
                || expression.text == "$random"
                || expression.text == "$urandom_range")) {
            return std::size_t{32};
        }
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                return register_width(local->second);
            }
            if (const auto found = signals_.find(expression.text); found != signals_.end()) {
                return design_.signal_info_[found->second].width;
            }
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                const auto width = selected->member->width();
                if (width
                    && *width
                        <= std::numeric_limits<std::size_t>::max()) {
                    return static_cast<std::size_t>(*width);
                }
            }
        }
        for (const auto& operand : expression.operands) {
            if (const auto width = infer_width(operand)) {
                return width;
            }
        }
        return std::nullopt;
    }

    std::optional<frontend::PackedRange> expression_range(
        const Expression& expression,
        const std::size_t width) const {
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local =
                    local_ranges_.find(expression.text);
                local != local_ranges_.end()
                && local->second) {
                return *local->second;
            }
            if (const auto signal =
                    signals_.find(expression.text);
                signal != signals_.end()
                && design_.signal_info_[signal->second].packed_range) {
                return *design_
                            .signal_info_[signal->second]
                            .packed_range;
            }
            if (const auto selected =
                    packed_member_reference(expression.text);
                selected
                && selected->member->packed_range) {
                return *selected->member->packed_range;
            }
        }
        if (width == 0
            || width - 1
                > static_cast<std::size_t>(
                    std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return frontend::PackedRange{
            static_cast<std::int64_t>(width - 1), 0, true};
    }

    std::optional<std::size_t> select_offset(
        const Expression& expression,
        const std::int64_t index,
        const std::size_t width) const {
        const auto range = expression_range(expression, width);
        if (!range) {
            return std::nullopt;
        }
        const auto lower = std::min(range->left, range->right);
        const auto upper = std::max(range->left, range->right);
        if (index < lower || index > upper) {
            return std::nullopt;
        }
        const auto offset = index_distance(index, range->right);
        if (offset >= width
            || offset
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(offset);
    }

    [[nodiscard]] bool is_signed_expression(
        const Expression& expression) const {
        switch (expression.kind) {
        case ExpressionKind::Identifier:
            if (const auto local =
                    local_signed_.find(expression.text);
                local != local_signed_.end()) {
                return local->second;
            }
            if (const auto signal = signals_.find(expression.text);
                    signal != signals_.end()) {
                return design_.signal_info_[signal->second].is_signed;
            }
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                return selected->member->is_signed;
            }
            return false;
        case ExpressionKind::IntegerLiteral:
            return true;
        case ExpressionKind::BooleanLiteral:
            return false;
        case ExpressionKind::LogicLiteral:
            return expression.text.find("'s") != std::string::npos
                || expression.text.find("'S") != std::string::npos;
        case ExpressionKind::StringLiteral:
        case ExpressionKind::Concatenation:
        case ExpressionKind::Replication:
        case ExpressionKind::Invalid:
            return false;
        case ExpressionKind::Index:
            return false;
        case ExpressionKind::Slice:
            return language_ == frontend::Language::Vhdl2008
                && !expression.operands.empty()
                && is_signed_expression(expression.operands[0]);
        case ExpressionKind::Unary:
            if (expression.operands.size() != 1
                || expression.text == "!"
                || expression.text == "&"
                || expression.text == "|"
                || expression.text == "^"
                || expression.text == "~&"
                || expression.text == "~|"
                || expression.text == "~^"
                || expression.text == "^~") {
                return false;
            }
            return is_signed_expression(expression.operands[0]);
        case ExpressionKind::Call:
            if (language_ != frontend::Language::Vhdl2008
                && expression.text == "$random") {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && (expression.text == "'left"
                    || expression.text == "'right"
                    || expression.text == "'low"
                    || expression.text == "'high"
                    || expression.text == "'length")) {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && (expression.text == "'ascending"
                    || expression.text == "'event")) {
                return false;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'last_value"
                && expression.operands.size() == 1) {
                return is_signed_expression(expression.operands.front());
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'last_event") {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'stable") {
                return false;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.text == "'active") {
                return false;
            }
            if (language_ != frontend::Language::Vhdl2008
                && expression.operands.size() == 1
                && expression.text == "$signed") {
                return true;
            }
            if (language_ != frontend::Language::Vhdl2008
                && expression.operands.size() == 1
                && expression.text == "$unsigned") {
                return false;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() == 1
                && (expression.text == "$left"
                    || expression.text == "$right"
                    || expression.text == "$low"
                    || expression.text == "$high"
                    || expression.text == "$size"
                    || expression.text == "$increment"
                    || expression.text == "$dimensions"
                    || expression.text == "$unpacked_dimensions")) {
                return true;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() == 2
                && (expression.text == "$left"
                    || expression.text == "$right"
                    || expression.text == "$low"
                    || expression.text == "$high"
                    || expression.text == "$size"
                    || expression.text == "$increment")) {
                return true;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() == 1
                && expression.text == "$countones") {
                return true;
            }
            if (language_
                    == frontend::Language::SystemVerilog2017
                && expression.operands.size() >= 2
                && expression.text == "$countbits") {
                return true;
            }
            if (language_ == frontend::Language::Vhdl2008
                && expression.operands.size() == 1
                && (locals_.contains(expression.text)
                    || signals_.contains(expression.text))) {
                return false;
            }
            if (expression.text == "?:"
                && expression.operands.size() == 3) {
                return is_signed_expression(expression.operands[1])
                    && is_signed_expression(expression.operands[2]);
            }
            return false;
        case ExpressionKind::Binary:
            if (expression.operands.size() != 2) {
                return false;
            }
            if (expression.text == "<<"
                || expression.text == ">>"
                || expression.text == "<<<"
                || expression.text == ">>>"
                || expression.text == "sll"
                || expression.text == "srl"
                || expression.text == "sla"
                || expression.text == "sra"
                || expression.text == "rol"
                || expression.text == "ror") {
                return is_signed_expression(expression.operands[0]);
            }
            if (expression.text == "=="
                || expression.text == "==="
                || expression.text == "!=="
                || expression.text == "!="
                || expression.text == "="
                || expression.text == "<"
                || expression.text == "<="
                || expression.text == ">"
                || expression.text == ">="
                || expression.text == "&&"
                || expression.text == "||"
                || (language_ == frontend::Language::Vhdl2008
                    && expression.text == "&")) {
                return false;
            }
            return is_signed_expression(expression.operands[0])
                && is_signed_expression(expression.operands[1]);
        }
        return false;
    }

    void collect_identifiers(
        const Expression& expression,
        std::set<std::string>& output) const {
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto selected =
                    packed_member_reference(expression.text)) {
                output.insert(selected->base);
            } else {
                output.insert(expression.text);
            }
        } else if (
            language_ == frontend::Language::Vhdl2008
            && expression.kind == ExpressionKind::Call
            && expression.operands.size() == 1
            && (signals_.contains(expression.text)
                || locals_.contains(expression.text))) {
            output.insert(expression.text);
        }
        for (const auto& operand : expression.operands) {
            collect_identifiers(operand, output);
        }
    }

    void collect_statement_identifiers(
        const std::vector<Statement>& statements,
        std::set<std::string>& output) const {
        for (const auto& statement : statements) {
            switch (statement.kind) {
            case StatementKind::Assignment:
                collect_identifiers(statement.value, output);
                break;
            case StatementKind::If:
            case StatementKind::Assert:
            case StatementKind::WaitUntil:
                collect_identifiers(statement.condition, output);
                break;
            case StatementKind::Case:
                collect_identifiers(statement.condition, output);
                for (const auto& alternative :
                     statement.case_alternatives) {
                    for (const auto& choice : alternative.choices) {
                        collect_identifiers(choice, output);
                    }
                }
                break;
            case StatementKind::Loop:
                if (statement.loop_runtime) {
                    collect_identifiers(
                        statement.condition, output);
                } else {
                    collect_identifiers(
                        statement.loop_initial, output);
                    collect_identifiers(
                        statement.loop_limit, output);
                }
                break;
            case StatementKind::Break:
            case StatementKind::Continue:
            case StatementKind::Delay:
            case StatementKind::WaitOn:
            case StatementKind::Display:
                if (statement.output_format) {
                    collect_identifiers(statement.value, output);
                }
                for (const auto& value : statement.output_values) {
                    collect_identifiers(value.value, output);
                }
                break;
            case StatementKind::MonitorControl:
                break;
            case StatementKind::EventTrigger:
            case StatementKind::Report:
            case StatementKind::Pause:
            case StatementKind::Finish:
            case StatementKind::Block:
            case StatementKind::Null:
                break;
            }
            collect_statement_identifiers(
                statement.statements, output);
            collect_statement_identifiers(
                statement.else_statements, output);
            for (const auto& alternative :
                 statement.case_alternatives) {
                collect_statement_identifiers(
                    alternative.statements, output);
            }
        }
    }

    RegisterId allocate_register(
        const std::size_t width,
        const frontend::ValueDomain domain) {
        const auto id = next_register_++;
        register_widths_.push_back(width);
        register_domains_.push_back(domain);
        return id;
    }

    [[nodiscard]] std::size_t register_width(const RegisterId id) const {
        return register_widths_.at(static_cast<std::size_t>(id));
    }

    [[nodiscard]] frontend::ValueDomain register_domain(
        const RegisterId id) const {
        return register_domains_.at(static_cast<std::size_t>(id));
    }

    void report(std::string code, std::string message, frontend::SourceSpan span) {
        diagnostics_.push_back({std::move(code), std::move(message), std::move(span)});
    }

    ElaboratedDesign& design_;
    const std::unordered_map<std::string, SignalId>& signals_;
    std::vector<Diagnostic>& diagnostics_;
    Process process_;
    RegisterId next_register_{};
    std::vector<std::size_t> register_widths_;
    std::vector<frontend::ValueDomain> register_domains_;
    std::unordered_map<std::string, RegisterId> locals_;
    std::unordered_map<std::string, bool> local_signed_;
    std::unordered_map<
        std::string, std::optional<frontend::PackedRange>>
        local_ranges_;
    std::unordered_map<
        std::string, std::vector<frontend::PackedMember>>
        local_members_;
    std::unordered_map<std::string, RegisterId>
        declaration_registers_;
    std::unordered_set<std::string> debug_local_names_;
    std::vector<std::string> local_scope_;
    struct LoopControlContext {
        std::optional<InstructionIndex> continue_target;
        std::vector<InstructionIndex> continue_jumps;
        std::vector<InstructionIndex> break_jumps;
        std::string label;
    };
    std::vector<LoopControlContext> loop_controls_;
    frontend::Language language_{frontend::Language::Vhdl2008};
    std::string hierarchy_;
};

namespace {

const DesignUnit* choose_unit(
    const frontend::ParsedDesign& parsed, const std::string& requested) {
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VerilogModule && unit.name == requested) {
            return &unit;
        }
    }
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VhdlArchitecture
            && unit.primary_name == requested) {
            return &unit;
        }
    }
    return nullptr;
}

struct TargetSpec {
    std::string language;
    std::string library;
    std::string unit;
    std::optional<std::string> architecture;
};

std::optional<TargetSpec> parse_target(const std::string_view spelling) {
    const auto colon = spelling.find(':');
    if (colon == std::string_view::npos || colon == 0
        || colon + 1 == spelling.size()) {
        return std::nullopt;
    }
    TargetSpec result;
    result.language = std::string{spelling.substr(0, colon)};
    auto remainder = spelling.substr(colon + 1);
    if (const auto dot = remainder.rfind('.'); dot != std::string_view::npos) {
        result.library = std::string{remainder.substr(0, dot)};
        remainder.remove_prefix(dot + 1);
    }
    if (const auto open = remainder.find('('); open != std::string_view::npos) {
        if (remainder.back() != ')' || open + 1 == remainder.size() - 1) {
            return std::nullopt;
        }
        result.architecture =
            std::string{remainder.substr(open + 1, remainder.size() - open - 2)};
        remainder = remainder.substr(0, open);
    }
    if (remainder.empty()) {
        return std::nullopt;
    }
    result.unit = std::string{remainder};
    return result;
}

const DesignUnit* find_vhdl_entity(
    const frontend::ParsedDesign& parsed, const DesignUnit& architecture) {
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VhdlEntity
            && unit.name == architecture.primary_name
            && (unit.library.empty() || architecture.library.empty()
                || unit.library == architecture.library)) {
            return &unit;
        }
    }
    return nullptr;
}

const std::vector<frontend::SignalDeclaration>* unit_ports(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& unit) {
    if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        if (!unit.ports.empty()) {
            return &unit.ports;
        }
        const auto* entity = find_vhdl_entity(parsed, unit);
        return entity == nullptr ? nullptr : &entity->ports;
    }
    return &unit.ports;
}

const DesignUnit* choose_bound_unit(
    const frontend::ParsedDesign& parsed,
    const TargetSpec& target) {
    if (target.language == "sv" || target.language == "verilog") {
        for (const auto& unit : parsed.units) {
            if (unit.kind == frontend::UnitKind::VerilogModule
                && unit.name == target.unit
                && (target.library.empty() || unit.library.empty()
                    || unit.library == target.library)) {
                return &unit;
            }
        }
        return nullptr;
    }
    if (target.language == "vhdl") {
        for (const auto& unit : parsed.units) {
            if (unit.kind == frontend::UnitKind::VhdlArchitecture
                && unit.primary_name == target.unit
                && (target.library.empty() || unit.library.empty()
                    || unit.library == target.library)
                && (!target.architecture
                    || unit.name == *target.architecture)) {
                return &unit;
            }
        }
    }
    return nullptr;
}

const DesignUnit* choose_top_unit(
    const frontend::ParsedDesign& parsed,
    const std::string_view spelling) {
    if (spelling.find(':') != std::string_view::npos) {
        const auto target = parse_target(spelling);
        return target ? choose_bound_unit(parsed, *target) : nullptr;
    }
    return choose_unit(parsed, simple_top_name(spelling));
}

const DesignUnit* choose_same_language_instance(
    const frontend::ParsedDesign& parsed,
    const DesignUnit& parent,
    const std::string_view name) {
    if (parent.language == frontend::Language::Vhdl2008) {
        auto primary = name;
        std::optional<std::string_view> architecture;
        if (const auto open = primary.find('(');
            open != std::string_view::npos && primary.back() == ')') {
            architecture = primary.substr(
                open + 1, primary.size() - open - 2);
            primary = primary.substr(0, open);
        }
        std::optional<std::string_view> selected_library;
        if (const auto dot = primary.rfind('.');
            dot != std::string_view::npos) {
            selected_library = primary.substr(0, dot);
            primary.remove_prefix(dot + 1);
        }
        const auto desired_library =
            selected_library.value_or(parent.library);
        for (const auto& unit : parsed.units) {
            if (unit.kind == frontend::UnitKind::VhdlArchitecture
                && unit.primary_name == primary
                && (desired_library.empty() || unit.library.empty()
                    || unit.library == desired_library)
                && (!architecture || unit.name == *architecture)) {
                return &unit;
            }
        }
        return nullptr;
    }
    for (const auto& unit : parsed.units) {
        if (unit.kind == frontend::UnitKind::VerilogModule
            && unit.language == parent.language && unit.name == name) {
            if (!unit.library.empty() && !parent.library.empty()
                && unit.library != parent.library) {
                continue;
            }
            return &unit;
        }
    }
    return nullptr;
}

std::string unit_identity(const DesignUnit& unit) {
    const auto library =
        unit.library.empty()
            ? std::string_view{"work"}
            : std::string_view{unit.library};
    if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
        return "vhdl:" + std::string{library} + "." + unit.primary_name
            + "(" + unit.name + ")";
    }
    return "sv:" + std::string{library} + "." + unit.name;
}

} // namespace

class HierarchyBuilder final {
public:
    HierarchyBuilder(
        const frontend::ParsedDesign& parsed,
        ElaboratedDesign& design,
        std::vector<Diagnostic>& diagnostics,
        const std::span<const Binding> bindings,
        const std::span<const SystemCInstanceDescription>
            systemc_instances,
        SystemCFactoryProvider* systemc_provider)
        : parsed_(parsed),
          design_(design),
          diagnostics_(diagnostics),
          systemc_provider_(systemc_provider) {
        for (const auto& binding : bindings) {
            if (!bindings_.emplace(binding.instance, &binding).second) {
                report(
                    "FSIM-ELAB-BIND-010",
                    "duplicate binding for instance '" + binding.instance + "'",
                    {});
            }
        }
        for (const auto& instance : systemc_instances) {
            if (!systemc_instances_
                     .emplace(instance.path, &instance)
                     .second) {
                report(
                    "FSIM-ELAB-BIND-032",
                    "duplicate constructed SystemC instance path '"
                        + instance.path + "'",
                    {});
            }
        }
    }

    void build(const DesignUnit& root) {
        auto specialized = specialize_selected_unit(
            root, {}, {}, root.language);
        instantiate(
            specialized.unit,
            design_.top_,
            {},
            std::move(specialized.environment),
            std::move(specialized.values));
        finish();
    }

    void build(const SystemCInstanceDescription& root) {
        instantiate_systemc(root, root.path, {}, {});
        finish();
    }

private:
    using SignalMap = std::unordered_map<std::string, SignalId>;
    using ObjectMap = std::unordered_map<std::uint64_t, SignalId>;

    static std::vector<std::string> selected_name_parts(
        const std::string_view name) {
        std::vector<std::string> result;
        std::size_t begin = 0;
        while (begin <= name.size()) {
            const auto separator = name.find('.', begin);
            result.emplace_back(
                name.substr(
                    begin,
                    separator == std::string_view::npos
                        ? name.size() - begin
                        : separator - begin));
            if (separator == std::string_view::npos) {
                break;
            }
            begin = separator + 1;
        }
        return result;
    }

    void expand_vhdl_context_references(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem> context,
        std::vector<frontend::VhdlContextItem>& expanded,
        std::vector<const DesignUnit*>& context_stack,
        const std::string_view visibility_library) {
        for (const auto& item : context) {
            if (item.kind
                != frontend::VhdlContextItemKind::
                    ContextReference) {
                auto visible_item = item;
                if (visible_item.kind
                    == frontend::VhdlContextItemKind::UseClause) {
                    for (auto& selected_name :
                         visible_item.selected_names) {
                        const auto parts =
                            selected_name_parts(selected_name);
                        if (!parts.empty()
                            && parts.front() == "work") {
                            selected_name =
                                std::string{visibility_library}
                                + selected_name.substr(4);
                        }
                    }
                }
                expanded.push_back(std::move(visible_item));
                continue;
            }
            for (const auto& selected_name : item.selected_names) {
                const auto parts =
                    selected_name_parts(selected_name);
                if (parts.size() != 2) {
                    report(
                        "FSIM-ELAB-CTX-001",
                        "bounded context references require "
                        "library.context",
                        item.span);
                    continue;
                }
                const auto requested_library =
                    parts[0] == "work"
                        ? std::string{visibility_library}
                        : parts[0];
                const auto context_unit = std::find_if(
                    parsed_.units.begin(),
                    parsed_.units.end(),
                    [&](const DesignUnit& candidate) {
                        const auto candidate_library =
                            candidate.library.empty()
                                ? std::string_view{"work"}
                                : std::string_view{
                                      candidate.library};
                        return candidate.kind
                                == frontend::UnitKind::VhdlContext
                            && candidate.name == parts[1]
                            && candidate_library
                                == requested_library;
                    });
                if (context_unit == parsed_.units.end()) {
                    if (parts[0] == "ieee"
                        || parts[0] == "std") {
                        continue;
                    }
                    report(
                        "FSIM-ELAB-CTX-002",
                        "VHDL context '" + parts[0] + "."
                            + parts[1] + "' was not found",
                        item.span);
                    continue;
                }
                const auto context_owner =
                    (context_unit->library.empty()
                         ? std::string{"work"}
                         : context_unit->library)
                    + "." + context_unit->name;
                if (std::find(
                        context_stack.begin(),
                        context_stack.end(),
                        &*context_unit)
                    != context_stack.end()) {
                    std::string cycle;
                    for (const auto* referenced : context_stack) {
                        if (!cycle.empty()) {
                            cycle += " -> ";
                        }
                        cycle +=
                            (referenced->library.empty()
                                 ? std::string{"work"}
                                 : referenced->library)
                            + "." + referenced->name;
                    }
                    cycle += " -> " + context_owner;
                    report(
                        "FSIM-ELAB-CTX-003",
                        "cyclic VHDL context visibility: " + cycle,
                        item.span);
                    continue;
                }
                const auto context_source = std::string{
                    frontend::physical_source(context_unit->span)};
                if (std::find(
                        unit.source_dependencies.begin(),
                        unit.source_dependencies.end(),
                        context_source)
                    == unit.source_dependencies.end()) {
                    unit.source_dependencies.push_back(
                        context_source);
                }
                context_stack.push_back(&*context_unit);
                const auto nested_library =
                    context_unit->library.empty()
                        ? std::string{"work"}
                        : context_unit->library;
                expand_vhdl_context_references(
                    unit,
                    context_unit->vhdl_context,
                    expanded,
                    context_stack,
                    nested_library);
                context_stack.pop_back();
            }
        }
    }

    void import_vhdl_package_constants(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem>
            context,
        std::vector<const DesignUnit*>& import_stack) {
        std::vector<frontend::ParameterDeclaration> imports;
        std::unordered_map<std::string, std::string> bare_owners;
        std::unordered_set<std::string> dependencies;
        const auto owner_library =
            unit.library.empty()
                ? std::string{"work"}
                : unit.library;
        for (const auto& item : context) {
            if (item.kind
                != frontend::VhdlContextItemKind::UseClause) {
                continue;
            }
            for (const auto& selected_name : item.selected_names) {
                const auto parts =
                    selected_name_parts(selected_name);
                if (parts.size() != 3) {
                    report(
                        "FSIM-ELAB-PKG-001",
                        "bounded package imports require "
                        "library.package.all or "
                        "library.package.constant",
                        item.span);
                    continue;
                }
                const auto requested_library =
                    parts[0] == "work"
                        ? owner_library
                        : parts[0];
                const auto package = std::find_if(
                    parsed_.units.begin(),
                    parsed_.units.end(),
                    [&](const DesignUnit& candidate) {
                        const auto candidate_library =
                            candidate.library.empty()
                                ? std::string_view{"work"}
                                : std::string_view{
                                      candidate.library};
                        return candidate.kind
                                == frontend::UnitKind::VhdlPackage
                            && candidate.name == parts[1]
                            && candidate_library
                                == requested_library;
                    });
                if (package == parsed_.units.end()) {
                    if (parts[0] == "ieee"
                        || parts[0] == "std") {
                        continue;
                    }
                    report(
                        "FSIM-ELAB-PKG-002",
                        "VHDL package '" + parts[0] + "."
                            + parts[1] + "' was not found",
                        item.span);
                    continue;
                }
                const auto package_owner =
                    (package->library.empty()
                         ? std::string{"work"}
                         : package->library)
                    + "." + package->name;
                auto specialized_package =
                    specialize_vhdl_package(
                        *package, import_stack, item.span);
                if (!specialized_package) {
                    continue;
                }
                auto& specialized = *specialized_package;
                const bool import_all = parts[2] == "all";
                bool found_selected = import_all;
                for (const auto& declaration :
                     package->parameters) {
                    if (!import_all
                        && declaration.name != parts[2]) {
                        continue;
                    }
                    found_selected = true;
                    const auto value =
                        specialized.environment.find(
                            declaration.name);
                    if (value
                        == specialized.environment.end()) {
                        continue;
                    }
                    const auto add_alias =
                        [&](std::string alias) {
                          const auto [owner, inserted] =
                              bare_owners.emplace(
                                  alias, package_owner);
                          if (!inserted
                              && owner->second != package_owner) {
                              report(
                                  "FSIM-ELAB-PKG-004",
                                  "VHDL package constant '"
                                      + alias
                                      + "' is directly visible "
                                      "from multiple packages",
                                  item.span);
                              return;
                          }
                          if (std::any_of(
                                  imports.begin(),
                                  imports.end(),
                                  [&](const auto& existing) {
                                      return existing.name
                                          == alias;
                                  })) {
                              return;
                          }
                          imports.push_back({
                              std::move(alias),
                              declaration.type,
                              constant_expression(
                                  value->second,
                                  declaration.span,
                                  declaration.type.domain,
                                  frontend::Language::Vhdl2008),
                              true,
                              declaration.span});
                        };
                    add_alias(declaration.name);
                }
                if (!found_selected) {
                    report(
                        "FSIM-ELAB-PKG-003",
                        "VHDL package '" + parts[0] + "."
                            + parts[1]
                            + "' has no constant '" + parts[2]
                            + "'",
                        item.span);
                }
                const auto package_source = std::string{
                    frontend::physical_source(package->span)};
                if (dependencies.insert(package_source).second) {
                    unit.source_dependencies.push_back(
                        package_source);
                }
                for (const auto& dependency :
                     specialized.unit.source_dependencies) {
                    if (dependencies.insert(dependency).second) {
                        unit.source_dependencies.push_back(
                            dependency);
                    }
                }
            }
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
    }

    std::optional<SpecializedUnit> specialize_vhdl_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span) {
        if (std::find(
                import_stack.begin(),
                import_stack.end(),
                &package)
            != import_stack.end()) {
            std::string cycle;
            for (const auto* imported : import_stack) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle +=
                    (imported->library.empty()
                         ? std::string{"work"}
                         : imported->library)
                    + "." + imported->name;
            }
            cycle += " -> "
                + (package.library.empty()
                       ? std::string{"work"}
                       : package.library)
                + "." + package.name;
            report(
                "FSIM-ELAB-PKG-007",
                "cyclic VHDL package visibility: " + cycle,
                reference_span);
            return std::nullopt;
        }
        import_stack.push_back(&package);
        auto effective_package = package;
        std::vector<frontend::VhdlContextItem>
            expanded_package_context;
        std::vector<const DesignUnit*> context_stack;
        const auto package_library =
            effective_package.library.empty()
                ? std::string{"work"}
                : effective_package.library;
        expand_vhdl_context_references(
            effective_package,
            package.vhdl_context,
            expanded_package_context,
            context_stack,
            package_library);
        import_vhdl_package_constants(
            effective_package,
            expanded_package_context,
            import_stack);
        import_qualified_vhdl_package_constants(
            effective_package, import_stack);
        auto specialized = specialize_unit(
            effective_package,
            {},
            {},
            frontend::Language::Vhdl2008,
            diagnostics_);
        import_stack.pop_back();
        return specialized;
    }

    void import_qualified_vhdl_package_constants(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack) {
        auto identifiers = qualified_identifiers(unit);
        std::vector<std::string> ordered;
        ordered.reserve(identifiers.size());
        for (const auto& [identifier, span] : identifiers) {
            (void)span;
            ordered.push_back(identifier);
        }
        std::sort(ordered.begin(), ordered.end());
        const auto owner_library =
            unit.library.empty()
                ? std::string{"work"}
                : unit.library;
        std::vector<frontend::ParameterDeclaration> imports;
        std::unordered_set<std::string> dependencies;
        for (const auto& identifier : ordered) {
            const auto& reference_span =
                identifiers.at(identifier);
            const auto parts = selected_name_parts(identifier);
            if (parts.size() != 2 && parts.size() != 3) {
                report(
                    "FSIM-ELAB-PKG-008",
                    "a selected package constant must be "
                    "package.constant or library.package.constant",
                    reference_span);
                continue;
            }
            const auto package_name =
                parts[parts.size() - 2];
            const auto constant_name = parts.back();
            const auto requested_library =
                parts.size() == 2
                    ? owner_library
                    : parts.front() == "work"
                        ? owner_library
                        : parts.front();
            const auto package = std::find_if(
                parsed_.units.begin(),
                parsed_.units.end(),
                [&](const DesignUnit& candidate) {
                    const auto candidate_library =
                        candidate.library.empty()
                            ? std::string_view{"work"}
                            : std::string_view{
                                  candidate.library};
                    return candidate.kind
                            == frontend::UnitKind::VhdlPackage
                        && candidate.name == package_name
                        && candidate_library
                            == requested_library;
                });
            if (package == parsed_.units.end()) {
                report(
                    "FSIM-ELAB-PKG-009",
                    "VHDL package '"
                        + requested_library + "."
                        + package_name + "' was not found",
                    reference_span);
                continue;
            }
            auto specialized_package =
                specialize_vhdl_package(
                    *package, import_stack, reference_span);
            if (!specialized_package) {
                continue;
            }
            const auto declaration = std::find_if(
                package->parameters.begin(),
                package->parameters.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            if (declaration == package->parameters.end()) {
                report(
                    "FSIM-ELAB-PKG-010",
                    "VHDL package '" + requested_library
                        + "." + package_name
                        + "' has no constant '"
                        + constant_name + "'",
                    reference_span);
                continue;
            }
            const auto value =
                specialized_package->environment.find(
                    constant_name);
            if (value
                == specialized_package->environment.end()) {
                continue;
            }
            imports.push_back({
                identifier,
                declaration->type,
                constant_expression(
                    value->second,
                    declaration->span,
                    declaration->type.domain,
                    frontend::Language::Vhdl2008),
                true,
                declaration->span});
            const auto package_source = std::string{
                frontend::physical_source(package->span)};
            if (dependencies.insert(package_source).second) {
                unit.source_dependencies.push_back(
                    package_source);
            }
            for (const auto& dependency :
                 specialized_package->unit.source_dependencies) {
                if (dependencies.insert(dependency).second) {
                    unit.source_dependencies.push_back(
                        dependency);
                }
            }
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
    }

    std::optional<SpecializedUnit>
    specialize_systemverilog_package(
        const DesignUnit& package,
        std::vector<const DesignUnit*>& import_stack,
        const frontend::SourceSpan& reference_span) {
        if (std::find(
                import_stack.begin(),
                import_stack.end(),
                &package)
            != import_stack.end()) {
            std::string cycle;
            for (const auto* imported : import_stack) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += imported->name;
            }
            cycle += " -> " + package.name;
            report(
                "FSIM-ELAB-SVPKG-004",
                "cyclic SystemVerilog package visibility: "
                    + cycle,
                reference_span);
            return std::nullopt;
        }
        import_stack.push_back(&package);
        auto effective_package = package;
        SystemVerilogTypeEnvironment type_environment;
        import_systemverilog_package_items(
            effective_package, import_stack, type_environment);
        import_qualified_systemverilog_package_items(
            effective_package, import_stack, type_environment);
        resolve_systemverilog_named_types(
            effective_package, type_environment);
        auto specialized = specialize_unit(
            effective_package,
            {},
            {},
            frontend::Language::SystemVerilog2017,
            diagnostics_);
        import_stack.pop_back();
        return specialized;
    }

    const DesignUnit* find_systemverilog_package(
        const DesignUnit& owner,
        const std::string_view name) const {
        const auto owner_library =
            owner.library.empty()
                ? std::string_view{"work"}
                : std::string_view{owner.library};
        const auto found = std::find_if(
            parsed_.units.begin(),
            parsed_.units.end(),
            [&](const DesignUnit& candidate) {
                const auto candidate_library =
                    candidate.library.empty()
                        ? std::string_view{"work"}
                        : std::string_view{candidate.library};
                return candidate.kind
                        == frontend::UnitKind::
                            SystemVerilogPackage
                    && candidate.name == name
                    && candidate_library == owner_library;
            });
        return found == parsed_.units.end()
            ? nullptr
            : &*found;
    }

    void append_package_dependencies(
        DesignUnit& unit,
        const DesignUnit& package,
        const SpecializedUnit& specialized) {
        const auto append = [&](const std::string& dependency) {
            if (std::find(
                    unit.source_dependencies.begin(),
                    unit.source_dependencies.end(),
                    dependency)
                == unit.source_dependencies.end()) {
                unit.source_dependencies.push_back(dependency);
            }
        };
        append(std::string{frontend::physical_source(package.span)});
        for (const auto& dependency :
             specialized.unit.source_dependencies) {
            append(dependency);
        }
    }

    void import_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        SystemVerilogTypeEnvironment& type_environment) {
        std::vector<frontend::ParameterDeclaration> imports;
        std::unordered_map<std::string, std::string> owners;
        for (const auto& import_item :
             unit.systemverilog_imports) {
            const auto* package =
                find_systemverilog_package(
                    unit, import_item.package);
            if (package == nullptr) {
                report(
                    "FSIM-ELAB-SVPKG-001",
                    "SystemVerilog package '"
                        + import_item.package + "' was not found",
                    import_item.span);
                continue;
            }
            auto specialized_package =
                specialize_systemverilog_package(
                    *package, import_stack, import_item.span);
            if (!specialized_package) {
                continue;
            }
            const bool wildcard = import_item.name.empty();
            bool found_selected = wildcard;
            for (const auto& declaration :
                 package->parameters) {
                if (!wildcard
                    && declaration.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto value =
                    specialized_package->environment.find(
                        declaration.name);
                if (value
                    == specialized_package->environment.end()) {
                    continue;
                }
                const auto specialized_declaration =
                    std::find_if(
                        specialized_package->unit.parameters.begin(),
                        specialized_package->unit.parameters.end(),
                        [&](const auto& candidate) {
                            return candidate.name
                                    == declaration.name
                                && candidate.span.source_name
                                    == declaration.span.source_name
                                && candidate.span.begin.offset
                                    == declaration.span.begin.offset;
                        });
                const auto& imported_type =
                    specialized_declaration
                            == specialized_package->unit.parameters.end()
                        ? declaration.type
                        : specialized_declaration->type;
                const auto [owner, inserted] =
                    owners.emplace(
                        declaration.name, package->name);
                if (!inserted
                    && owner->second != package->name) {
                    report(
                        "FSIM-ELAB-SVPKG-003",
                        "SystemVerilog package constant '"
                            + declaration.name
                            + "' is imported from multiple "
                              "packages",
                        import_item.span);
                    continue;
                }
                if (std::any_of(
                        imports.begin(),
                        imports.end(),
                        [&](const auto& existing) {
                            return existing.name
                                == declaration.name;
                        })) {
                    continue;
                }
                imports.push_back({
                    declaration.name,
                    imported_type,
                    constant_expression(
                        value->second,
                        declaration.span,
                        imported_type.domain,
                        frontend::Language::
                            SystemVerilog2017),
                    true,
                    declaration.span});
            }
            for (const auto& alias :
                 specialized_package->unit.type_aliases) {
                if (!wildcard
                    && alias.name != import_item.name) {
                    continue;
                }
                found_selected = true;
                const auto [existing, inserted] =
                    type_environment.emplace(
                        alias.name,
                        SystemVerilogTypeBinding{
                            alias.type,
                            package->name});
                if (!inserted
                    && existing->second.owner
                        != package->name) {
                    report(
                        "FSIM-ELAB-SVTYPE-002",
                        "SystemVerilog type '" + alias.name
                            + "' is imported from multiple "
                              "packages",
                        import_item.span);
                }
            }
            if (!found_selected) {
                report(
                    "FSIM-ELAB-SVPKG-002",
                    "SystemVerilog package '"
                        + package->name
                        + "' has no exported item '"
                        + import_item.name + "'",
                    import_item.span);
            }
            append_package_dependencies(
                unit, *package, *specialized_package);
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
    }

    void import_qualified_systemverilog_package_items(
        DesignUnit& unit,
        std::vector<const DesignUnit*>& import_stack,
        SystemVerilogTypeEnvironment& type_environment) {
        auto identifiers = qualified_identifiers(unit);
        std::vector<std::string> ordered;
        for (const auto& [identifier, span] : identifiers) {
            (void)span;
            if (identifier.find("::")
                != std::string::npos) {
                ordered.push_back(identifier);
            }
        }
        std::sort(ordered.begin(), ordered.end());
        std::vector<frontend::ParameterDeclaration> imports;
        for (const auto& identifier : ordered) {
            const auto& reference_span =
                identifiers.at(identifier);
            const auto separator = identifier.find("::");
            if (separator == std::string::npos
                || separator == 0
                || identifier.find("::", separator + 2)
                    != std::string::npos
                || separator + 2 >= identifier.size()) {
                report(
                    "FSIM-ELAB-SVPKG-005",
                    "a package-scoped item must be "
                    "package::name",
                    reference_span);
                continue;
            }
            const auto package_name =
                identifier.substr(0, separator);
            const auto constant_name =
                identifier.substr(separator + 2);
            const auto* package =
                find_systemverilog_package(
                    unit, package_name);
            if (package == nullptr) {
                report(
                    "FSIM-ELAB-SVPKG-001",
                    "SystemVerilog package '"
                        + package_name + "' was not found",
                    reference_span);
                continue;
            }
            auto specialized_package =
                specialize_systemverilog_package(
                    *package, import_stack, reference_span);
            if (!specialized_package) {
                continue;
            }
            const auto declaration = std::find_if(
                package->parameters.begin(),
                package->parameters.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            const auto alias = std::find_if(
                specialized_package->unit.type_aliases.begin(),
                specialized_package->unit.type_aliases.end(),
                [&](const auto& candidate) {
                    return candidate.name == constant_name;
                });
            if (declaration == package->parameters.end()
                && alias
                    == specialized_package->unit.type_aliases.end()) {
                report(
                    "FSIM-ELAB-SVPKG-002",
                    "SystemVerilog package '"
                        + package_name
                        + "' has no exported item '"
                        + constant_name + "'",
                    reference_span);
                continue;
            }
            if (alias
                != specialized_package->unit.type_aliases.end()) {
                type_environment.insert_or_assign(
                    identifier,
                    SystemVerilogTypeBinding{
                        alias->type,
                        package->name});
                append_package_dependencies(
                    unit, *package, *specialized_package);
                continue;
            }
            const auto value =
                specialized_package->environment.find(
                    constant_name);
            if (value
                == specialized_package->environment.end()) {
                continue;
            }
            const auto specialized_declaration =
                std::find_if(
                    specialized_package->unit.parameters.begin(),
                    specialized_package->unit.parameters.end(),
                    [&](const auto& candidate) {
                        return candidate.name
                                == declaration->name
                            && candidate.span.source_name
                                == declaration->span.source_name
                            && candidate.span.begin.offset
                                == declaration->span.begin.offset;
                    });
            const auto& imported_type =
                specialized_declaration
                        == specialized_package->unit.parameters.end()
                    ? declaration->type
                    : specialized_declaration->type;
            imports.push_back({
                identifier,
                imported_type,
                constant_expression(
                    value->second,
                    declaration->span,
                    imported_type.domain,
                    frontend::Language::
                        SystemVerilog2017),
                true,
                declaration->span});
            append_package_dependencies(
                unit, *package, *specialized_package);
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
    }

    void resolve_systemverilog_named_types(
        DesignUnit& unit,
        const SystemVerilogTypeEnvironment& imported_types) {
        std::unordered_map<std::string, std::size_t> local_types;
        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            local_types.emplace(
                unit.type_aliases[index].name, index);
        }
        std::vector<unsigned char> states(
            unit.type_aliases.size(), 0);
        std::function<bool(frontend::Type&)> resolve_type;
        std::function<bool(std::size_t)> resolve_alias;
        resolve_alias = [&](const std::size_t index) {
            if (states[index] == 2) {
                return true;
            }
            if (states[index] == 3) {
                return false;
            }
            if (states[index] == 1) {
                report(
                    "FSIM-ELAB-SVTYPE-003",
                    "cyclic SystemVerilog typedef involving '"
                        + unit.type_aliases[index].name + "'",
                    unit.type_aliases[index].span);
                return false;
            }
            states[index] = 1;
            const bool resolved =
                resolve_type(unit.type_aliases[index].type);
            states[index] = resolved ? 2 : 3;
            return resolved;
        };
        resolve_type = [&](frontend::Type& type) {
            if (type.named_type.empty()) {
                return true;
            }
            const auto name = type.named_type;
            const auto use_span = type.named_type_span;
            if (name.find("::") == std::string::npos) {
                if (const auto local = local_types.find(name);
                    local != local_types.end()) {
                    if (!resolve_alias(local->second)) {
                        return false;
                    }
                    type =
                        unit.type_aliases[local->second].type;
                    return true;
                }
            }
            const auto imported = imported_types.find(name);
            if (imported == imported_types.end()) {
                report(
                    "FSIM-ELAB-SVTYPE-001",
                    "SystemVerilog type alias '" + name
                        + "' is not visible in this unit",
                    use_span);
                return false;
            }
            type = imported->second.type;
            return true;
        };

        for (std::size_t index = 0;
             index < unit.type_aliases.size(); ++index) {
            (void)resolve_alias(index);
        }

        const auto resolve_declaration =
            [&](auto& declaration) {
                (void)resolve_type(declaration.type);
            };
        std::function<void(std::vector<Statement>&)>
            resolve_statements;
        resolve_statements =
            [&](std::vector<Statement>& statements) {
                for (auto& statement : statements) {
                    for (auto& declaration :
                         statement.declarations) {
                        resolve_declaration(declaration);
                    }
                    resolve_statements(statement.statements);
                    resolve_statements(
                        statement.else_statements);
                    for (auto& alternative :
                         statement.case_alternatives) {
                        resolve_statements(
                            alternative.statements);
                    }
                }
            };
        std::function<void(frontend::GenerateBody&)>
            resolve_generate_body;
        std::function<void(
            std::vector<frontend::GenerateRegion>&)>
            resolve_generate_regions;
        resolve_generate_body =
            [&](frontend::GenerateBody& body) {
                for (auto& constant : body.constants) {
                    resolve_declaration(constant);
                }
                for (auto& signal : body.signals) {
                    resolve_declaration(signal);
                }
                for (auto& process : body.processes) {
                    for (auto& variable : process.variables) {
                        resolve_declaration(variable);
                    }
                    resolve_statements(process.statements);
                }
                resolve_generate_regions(
                    body.generate_regions);
            };
        resolve_generate_regions =
            [&](std::vector<frontend::GenerateRegion>&
                    regions) {
                for (auto& region : regions) {
                    resolve_generate_body(region.then_body);
                    resolve_generate_body(region.else_body);
                    for (auto& alternative :
                         region.alternatives) {
                        resolve_generate_body(
                            alternative.body);
                    }
                }
            };

        for (auto& parameter : unit.parameters) {
            resolve_declaration(parameter);
        }
        for (auto& port : unit.ports) {
            resolve_declaration(port);
        }
        for (auto& signal : unit.signals) {
            resolve_declaration(signal);
        }
        for (auto& process : unit.processes) {
            for (auto& variable : process.variables) {
                resolve_declaration(variable);
            }
            resolve_statements(process.statements);
        }
        resolve_generate_regions(unit.generate_regions);
    }

    DesignUnit effective_unit(const DesignUnit& selected) {
        auto result = selected;
        if (selected.kind
            == frontend::UnitKind::VerilogModule) {
            std::vector<const DesignUnit*> import_stack;
            SystemVerilogTypeEnvironment type_environment;
            import_systemverilog_package_items(
                result, import_stack, type_environment);
            import_qualified_systemverilog_package_items(
                result, import_stack, type_environment);
            resolve_systemverilog_named_types(
                result, type_environment);
            return result;
        }
        if (selected.kind
            != frontend::UnitKind::VhdlArchitecture) {
            return result;
        }
        const auto* entity = find_vhdl_entity(parsed_, selected);
        if (entity == nullptr) {
            return result;
        }
        result.parameters = entity->parameters;
        result.ports = entity->ports;
        for (const auto& generic : result.parameters) {
            if (std::any_of(
                    result.signals.begin(),
                    result.signals.end(),
                    [&](const frontend::SignalDeclaration& signal) {
                        return signal.name == generic.name;
                    })) {
                report(
                    "FSIM-ELAB-GENERIC-009",
                    "architecture object '" + generic.name
                        + "' conflicts with an entity generic",
                    generic.span);
            }
        }
        std::vector<frontend::VhdlContextItem> context =
            entity->vhdl_context;
        context.insert(
            context.end(),
            selected.vhdl_context.begin(),
            selected.vhdl_context.end());
        std::vector<frontend::VhdlContextItem> expanded_context;
        std::vector<const DesignUnit*> context_stack;
        const auto unit_library =
            result.library.empty()
                ? std::string{"work"}
                : result.library;
        expand_vhdl_context_references(
            result,
            context,
            expanded_context,
            context_stack,
            unit_library);
        std::vector<const DesignUnit*> import_stack;
        import_vhdl_package_constants(
            result, expanded_context, import_stack);
        import_qualified_vhdl_package_constants(
            result, import_stack);
        return result;
    }

    SpecializedUnit specialize_selected_unit(
        const DesignUnit& selected,
        const std::vector<frontend::ParameterOverride>& overrides,
        const ConstantEnvironment& parent_environment,
        const frontend::Language association_language) {
        return specialize_unit(
            effective_unit(selected),
            overrides,
            parent_environment,
            association_language,
            diagnostics_);
    }

    void finish() {
        validate_process_drivers();
        for (const auto& [path, binding] : bindings_) {
            (void)binding;
            if (!used_bindings_.contains(path)) {
                report(
                    "FSIM-ELAB-BIND-011",
                    "binding instance path '" + path
                        + "' was not found in the elaborated hierarchy",
                    {});
            }
        }
        for (const auto& [path, instance] : systemc_instances_) {
            (void)instance;
            if (!used_systemc_instances_.contains(path)) {
                report(
                    "FSIM-ELAB-BIND-033",
                    "constructed SystemC instance path '" + path
                        + "' was not reached from the elaborated hierarchy",
                    {});
            }
        }
    }

    void validate_process_drivers() {
        std::unordered_map<SignalId, std::vector<ProcessId>> drivers;
        for (const auto& process : design_.processes_) {
            std::set<SignalId> process_outputs;
            for (const auto& operation : process.operations) {
                if (const auto* blocking =
                        std::get_if<WriteBlocking>(&operation)) {
                    process_outputs.insert(blocking->signal);
                } else if (const auto* update =
                               std::get_if<WriteUpdate>(&operation)) {
                    process_outputs.insert(update->signal);
                } else if (const auto* delayed =
                               std::get_if<WriteAfter>(&operation)) {
                    process_outputs.insert(delayed->signal);
                } else if (const auto* blocking_slice =
                               std::get_if<WriteBlockingSlice>(
                                   &operation)) {
                    process_outputs.insert(blocking_slice->signal);
                } else if (const auto* update_slice =
                               std::get_if<WriteUpdateSlice>(
                                   &operation)) {
                    process_outputs.insert(update_slice->signal);
                } else if (const auto* delayed_slice =
                               std::get_if<WriteAfterSlice>(
                                   &operation)) {
                    process_outputs.insert(delayed_slice->signal);
                }
            }
            for (const auto signal : process_outputs) {
                drivers[signal].push_back(process.id);
            }
        }
        for (const auto& [signal, processes] : drivers) {
            if (processes.size() <= 1) {
                continue;
            }
            report(
                "FSIM-ELAB-DRV-001",
                "signal '" + design_.signal_info_.at(signal).name
                    + "' has multiple process drivers; driver slots and "
                      "language-specific resolution are not executable in "
                      "this slice",
                {});
        }
    }

    std::optional<SignalId> add_owned_signal(
        const frontend::SignalDeclaration& declaration,
        const std::string_view path,
        SignalMap& local) {
        if (const auto existing = local.find(declaration.name);
            existing != local.end()) {
            return existing->second;
        }
        if (declaration.type.domain == frontend::ValueDomain::Unknown
            || declaration.type.domain == frontend::ValueDomain::Integer) {
            report(
                "FSIM-ELAB-TYPE-001",
                "signal '" + declaration.name
                    + "' has a type that the packed simulation runtime "
                      "cannot represent",
                declaration.span);
            return std::nullopt;
        }
        const auto width = declaration.type.width().value_or(1);
        if (width == 0 || width > std::numeric_limits<std::size_t>::max()) {
            report(
                "FSIM-ELAB-010",
                "signal '" + declaration.name + "' has an invalid width",
                declaration.span);
            return std::nullopt;
        }
        if (design_.signals_.size()
            > std::numeric_limits<SignalId>::max()) {
            report(
                "FSIM-ELAB-011",
                "the design has too many signals for dense 32-bit IDs",
                declaration.span);
            return std::nullopt;
        }
        const auto id = static_cast<SignalId>(design_.signals_.size());
        const auto full_name =
            std::string(path) + "." + declaration.name;
        local.emplace(declaration.name, id);
        local.emplace(full_name, id);
        design_.signal_by_name_.emplace(full_name, id);
        if (path == design_.top_) {
            design_.signal_by_name_.emplace(declaration.name, id);
        }
        design_.signal_info_.push_back({
            id,
            full_name,
            static_cast<std::size_t>(width),
            declaration.type.spelling,
            declaration.type.domain,
            declaration.type.is_signed,
            declaration.type.packed_range,
            declaration.type.packed_members,
            declaration.is_port,
            declaration.direction,
            declaration.span});
        auto initial = Logic4::x;
        if (declaration.type.spelling == "event") {
            initial = Logic4::zero;
        } else if (declaration.type.spelling == "tri0") {
            initial = Logic4::zero;
        } else if (declaration.type.spelling == "tri1") {
            initial = Logic4::one;
        } else if (
            declaration.type.domain == frontend::ValueDomain::Bit2
            || declaration.type.domain == frontend::ValueDomain::Boolean) {
            initial = Logic4::zero;
        } else if (
            declaration.type.domain == frontend::ValueDomain::Logic4
            && (declaration.type.spelling == "wire"
                || declaration.type.spelling == "tri"
                || declaration.type.spelling == "wand"
                || declaration.type.spelling == "triand"
                || declaration.type.spelling == "wor"
                || declaration.type.spelling == "trior"
                || declaration.type.spelling == "trireg"
                || declaration.type.spelling == "uwire")) {
            initial = Logic4::z;
        }
        design_.signals_.push_back({
            full_name,
            PackedLogic4(static_cast<std::size_t>(width), initial)});
        return id;
    }

    const Binding* binding_for(const std::string& path) {
        const auto found = bindings_.find(path);
        if (found == bindings_.end()) {
            return nullptr;
        }
        used_bindings_.insert(path);
        return found->second;
    }

    const DesignUnit* bound_target(
        const frontend::Instance& instance,
        const DesignUnit& parent,
        const std::string& path,
        const Binding* binding) {
        if (binding == nullptr) {
            const auto* target = choose_same_language_instance(
                parsed_, parent, instance.unit_name);
            if (target == nullptr) {
                report(
                    "FSIM-ELAB-BIND-012",
                    "instance '" + path + "' names unit '"
                        + instance.unit_name
                        + "', which was not found in the same language; "
                          "an explicit cross-language binding is required",
                    instance.span);
            }
            return target;
        }
        const auto target = parse_target(binding->target);
        if (!target) {
            report(
                "FSIM-ELAB-BIND-013",
                "malformed binding target '" + binding->target + "'",
                instance.span);
            return nullptr;
        }
        if (target->language == "systemc") {
            report(
                "FSIM-ELAB-BIND-014",
                "SystemC factory hierarchy is not executable in this slice",
                instance.span);
            return nullptr;
        }
        if (target->language == "vhdl" && !target->architecture) {
            report(
                "FSIM-ELAB-BIND-016",
                "an explicit VHDL binding target must name an architecture, "
                "for example vhdl:work.entity(rtl)",
                instance.span);
            return nullptr;
        }
        const auto* selected = choose_bound_unit(parsed_, *target);
        if (selected == nullptr) {
            report(
                "FSIM-ELAB-BIND-015",
                "binding target '" + binding->target + "' was not found",
                instance.span);
        }
        return selected;
    }

    void validate_boundary_type(
        const frontend::SignalDeclaration& port,
        const SignalInfo& actual,
        const std::string& path,
        const frontend::SourceSpan& source,
        const bool cross_language) {
        const auto unsupported_domain =
            [](const frontend::ValueDomain domain) {
                return domain == frontend::ValueDomain::Unknown
                    || domain == frontend::ValueDomain::Integer;
            };
        if (unsupported_domain(port.type.domain)
            || unsupported_domain(actual.source_domain)) {
            report(
                "FSIM-ELAB-BIND-019",
                "unsupported value domain on boundary '"
                    + path + "." + port.name + "'",
                source);
            return;
        }
        if (cross_language
            && (!port.type.packed_members.empty()
                || !actual.packed_members.empty())) {
            report(
                "FSIM-ELAB-BIND-049",
                "packed struct boundary '" + path + "."
                    + port.name
                    + "' requires a same-language scalar/vector wrapper",
                source);
            return;
        }
        const auto width = port.type.width().value_or(1);
        if (width != actual.width) {
            report(
                "FSIM-ELAB-BIND-020",
                "width mismatch on '" + path + "." + port.name + "': "
                    + std::to_string(width) + " versus "
                    + std::to_string(actual.width),
                source);
        }
        if (port.type.is_signed != actual.is_signed && width > 1) {
            report(
                "FSIM-ELAB-BIND-021",
                "signedness mismatch on '" + path + "." + port.name + "'",
                source);
        }
        const auto lossy_into_two_state =
            [](const frontend::ValueDomain destination,
               const frontend::ValueDomain source_domain) {
                return (destination == frontend::ValueDomain::Bit2
                        || destination == frontend::ValueDomain::Boolean)
                    && source_domain != frontend::ValueDomain::Bit2
                    && source_domain != frontend::ValueDomain::Boolean;
            };
        const bool lossy =
            port.direction == frontend::PortDirection::Output
                ? lossy_into_two_state(
                      actual.source_domain, port.type.domain)
                : lossy_into_two_state(
                      port.type.domain, actual.source_domain);
        if (lossy) {
            report(
                "FSIM-ELAB-BIND-022",
                "implicit lossy conversion into a 2-state boundary at '"
                    + path + "." + port.name + "' is forbidden",
                source);
        }
    }

    void note_boundary_driver(
        const SignalId signal,
        const Binding* binding,
        const std::string& path,
        const frontend::SourceSpan& source) {
        auto& count = boundary_driver_count_[signal];
        ++count;
        if (binding != nullptr && binding->resolver) {
            const auto [found, inserted] =
                resolver_by_signal_.emplace(signal, *binding->resolver);
            if (!inserted && found->second != *binding->resolver) {
                report(
                    "FSIM-ELAB-BIND-023",
                    "conflicting resolvers for boundary net '" + path + "'",
                    source);
            }
        }
        if (count > 1) {
            if (!resolver_by_signal_.contains(signal)) {
                report(
                    "FSIM-ELAB-BIND-024",
                    "multiple boundary drivers on '" + path
                        + "' require resolver = \"std_logic\" or \"sv_wire\"",
                    source);
            } else {
                report(
                    "FSIM-ELAB-BIND-029",
                    "multiple boundary drivers on '" + path
                        + "' cannot execute until driver-slot resolution is "
                          "implemented",
                    source);
            }
        }
    }

    SignalMap connect_ports(
        const frontend::Instance& instance,
        const std::vector<frontend::SignalDeclaration>& ports,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding,
        const bool cross_language) {
        SignalMap aliases;
        std::vector<bool> connected(ports.size());
        std::size_t positional = 0;
        for (const auto& connection : instance.connections) {
            std::size_t port_index = ports.size();
            if (connection.port) {
                const auto found = std::find_if(
                    ports.begin(), ports.end(),
                    [&](const frontend::SignalDeclaration& port) {
                        return port.name == *connection.port;
                    });
                if (found != ports.end()) {
                    port_index = static_cast<std::size_t>(
                        std::distance(ports.begin(), found));
                }
            } else {
                while (positional < ports.size() && connected[positional]) {
                    ++positional;
                }
                port_index = positional++;
            }
            if (port_index >= ports.size()) {
                report(
                    "FSIM-ELAB-BIND-025",
                    connection.port
                        ? "unknown port '" + *connection.port
                            + "' on instance '" + path + "'"
                        : "too many positional connections on instance '"
                            + path + "'",
                    connection.span);
                continue;
            }
            if (connected[port_index]) {
                report(
                    "FSIM-ELAB-BIND-026",
                    "port '" + ports[port_index].name
                        + "' is connected more than once on instance '"
                        + path + "'",
                    connection.span);
                continue;
            }
            connected[port_index] = true;
            if (connection.value.kind != frontend::ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-BIND-027",
                    "boundary connection actuals must be whole signals",
                    connection.value.span);
                continue;
            }
            const auto actual = parent_signals.find(connection.value.text);
            if (actual == parent_signals.end()) {
                report(
                    "FSIM-ELAB-BIND-028",
                    "unknown connection signal '" + connection.value.text
                        + "' on instance '" + path + "'",
                    connection.value.span);
                continue;
            }
            const auto& port = ports[port_index];
            const auto& actual_info = design_.signal_info_.at(actual->second);
            validate_boundary_type(
                port,
                actual_info,
                path,
                connection.span,
                cross_language);
            if (cross_language
                && port.direction == frontend::PortDirection::Inout) {
                if (binding == nullptr || !binding->resolver) {
                    report(
                        "FSIM-ELAB-BIND-030",
                        "cross-language inout '" + path + "." + port.name
                            + "' requires resolver = \"std_logic\" or "
                              "\"sv_wire\"",
                        connection.span);
                } else {
                    report(
                        "FSIM-ELAB-BIND-031",
                        "cross-language inout '" + path + "." + port.name
                            + "' cannot execute until driver-slot resolution "
                              "is implemented",
                        connection.span);
                }
            }
            aliases.emplace(port.name, actual->second);
            aliases.emplace(path + "." + port.name, actual->second);
            design_.signal_by_name_.emplace(
                path + "." + port.name, actual->second);
            if (port.direction == frontend::PortDirection::Output
                || port.direction == frontend::PortDirection::Inout
                || port.direction == frontend::PortDirection::Buffer) {
                note_boundary_driver(
                    actual->second, binding, path, connection.span);
            }
        }
        if (instance.unconnected_drive
            != frontend::VerilogUnconnectedDrive::None) {
            for (std::size_t port_index = 0;
                 port_index < ports.size(); ++port_index) {
                const auto& port = ports[port_index];
                if (connected[port_index]
                    || port.direction
                        != frontend::PortDirection::Input) {
                    continue;
                }
                auto pulled = port;
                pulled.type.spelling =
                    instance.unconnected_drive
                            == frontend::VerilogUnconnectedDrive::Pull0
                        ? "tri0"
                        : "tri1";
                (void)add_owned_signal(pulled, path, aliases);
            }
        }
        return aliases;
    }

    SignalMap connect_instance(
        const frontend::Instance& instance,
        const DesignUnit& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding,
        const bool cross_language) {
        const auto* ports = unit_ports(parsed_, target);
        if (ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + target.name
                    + "' has no matching entity",
                target.span);
            return {};
        }
        return connect_ports(
            instance,
            *ports,
            path,
            parent_signals,
            binding,
            cross_language);
    }

    static frontend::SignalDeclaration external_port_declaration(
        const ExternalPort& port) {
        return {
            port.name,
            port.type,
            port.direction,
            true,
            {}};
    }

    static frontend::SignalDeclaration foreign_port_declaration(
        const ForeignPort& port) {
        return {
            port.name,
            port.type,
            port.direction,
            true,
            {}};
    }

    std::pair<SignalMap, ObjectMap> connect_systemc_instance(
        const frontend::Instance& instance,
        const SystemCInstanceDescription& target,
        const std::string& path,
        const SignalMap& parent_signals,
        const Binding* binding) {
        std::vector<frontend::SignalDeclaration> ports;
        ports.reserve(target.ports.size());
        for (const auto& port : target.ports) {
            ports.push_back(external_port_declaration(port));
        }
        auto aliases = connect_ports(
            instance,
            ports,
            path,
            parent_signals,
            binding,
            true);
        ObjectMap objects;
        for (const auto& port : target.ports) {
            if (const auto signal = aliases.find(port.name);
                signal != aliases.end()) {
                objects.emplace(port.handle, signal->second);
            }
        }
        return {std::move(aliases), std::move(objects)};
    }

    SignalMap connect_foreign_child(
        const ForeignChild& child,
        const DesignUnit& target,
        const std::string& path,
        const ObjectMap& objects) {
        SignalMap aliases;
        const auto* target_ports = unit_ports(parsed_, target);
        if (target_ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + target.name
                    + "' has no matching entity",
                target.span);
            return aliases;
        }
        std::unordered_set<std::string> connected;
        for (const auto& foreign_port : child.ports) {
            const auto formal = std::find_if(
                target_ports->begin(),
                target_ports->end(),
                [&](const frontend::SignalDeclaration& port) {
                    return port.name == foreign_port.name;
                });
            if (formal == target_ports->end()) {
                report(
                    "FSIM-ELAB-BIND-034",
                    "foreign child '" + path
                        + "' declares unknown target port '"
                        + foreign_port.name + "'",
                    {});
                continue;
            }
            if (!connected.insert(foreign_port.name).second) {
                report(
                    "FSIM-ELAB-BIND-035",
                    "foreign child port '" + path + "."
                        + foreign_port.name
                        + "' is connected more than once",
                    {});
                continue;
            }
            const auto actual = objects.find(foreign_port.object);
            if (actual == objects.end()) {
                report(
                    "FSIM-ELAB-BIND-036",
                    "foreign child port '" + path + "."
                        + foreign_port.name
                        + "' references an unknown SystemC object",
                    {});
                continue;
            }
            const auto placeholder =
                foreign_port_declaration(foreign_port);
            const auto& actual_info =
                design_.signal_info_.at(actual->second);
            validate_boundary_type(
                placeholder, actual_info, path, {}, true);
            validate_boundary_type(
                *formal, actual_info, path, {}, true);
            if (placeholder.direction != formal->direction) {
                report(
                    "FSIM-ELAB-BIND-037",
                    "foreign child port direction mismatch on '"
                        + path + "." + foreign_port.name + "'",
                    {});
            }
            aliases.emplace(formal->name, actual->second);
            aliases.emplace(
                path + "." + formal->name, actual->second);
            design_.signal_by_name_.emplace(
                path + "." + formal->name, actual->second);
            // The foreign child is an implementation detail of the enclosing
            // SystemC module. Its output reaches the parent through that
            // module's already-recorded boundary driver, so recording a
            // second boundary driver here would turn one hierarchical drive
            // path into a false multi-driver conflict.
        }
        return aliases;
    }

    const SystemCInstanceDescription* systemc_description(
        const std::string& path,
        const std::string_view target,
        const frontend::SourceSpan& source) {
        const auto found = systemc_instances_.find(path);
        if (found == systemc_instances_.end()) {
            report(
                "FSIM-ELAB-BIND-038",
                "SystemC target '" + std::string{target}
                    + "' at '" + path
                    + "' was not constructed before HDL elaboration",
                source);
            return nullptr;
        }
        if (found->second->target != target) {
            report(
                "FSIM-ELAB-BIND-039",
                "constructed SystemC target '"
                    + found->second->target + "' at '" + path
                    + "' does not match binding target '"
                    + std::string{target} + "'",
                source);
            return nullptr;
        }
        used_systemc_instances_.insert(path);
        return found->second;
    }

    const SystemCInstanceDescription* construct_systemc_description(
        const frontend::Instance& instance,
        const std::string& path,
        const std::string_view target,
        const ConstantEnvironment& parent_environment,
        const frontend::Language association_language) {
        if (systemc_provider_ == nullptr) {
            if (!instance.parameter_overrides.empty()) {
                report(
                    association_language
                            == frontend::Language::Vhdl2008
                        ? "FSIM-ELAB-GENERIC-001"
                        : "FSIM-ELAB-PARAM-001",
                    "HDL generic or parameter actuals require a live "
                    "SystemC factory schema provider",
                    instance.parameter_overrides.front().span);
                return nullptr;
            }
            return systemc_description(path, target, instance.span);
        }

        std::string error;
        auto schema = systemc_provider_->schema(target, error);
        if (!schema) {
            report(
                "FSIM-ELAB-SC-PARAM-007",
                "cannot inspect SystemC construction schema for '"
                    + std::string{target} + "': " + error,
                instance.span);
            return nullptr;
        }
        auto values = specialize_systemc_construction(
            *schema,
            instance.parameter_overrides,
            parent_environment,
            association_language,
            diagnostics_);
        if (!values) {
            return nullptr;
        }
        auto constructed = systemc_provider_->instantiate(
            path, target, *values, error);
        if (!constructed) {
            report(
                "FSIM-ELAB-SC-PARAM-008",
                "cannot construct SystemC instance '" + path
                    + "': " + error,
                instance.span);
            return nullptr;
        }
        if (constructed->path != path
            || constructed->target != target) {
            report(
                "FSIM-ELAB-SC-PARAM-008",
                "SystemC factory provider returned inconsistent "
                "instance identity for '" + path + "'",
                instance.span);
            return nullptr;
        }
        owned_systemc_instances_.push_back(
            std::move(*constructed));
        const auto* description =
            &owned_systemc_instances_.back();
        if (!systemc_instances_
                 .emplace(path, description)
                 .second) {
            report(
                "FSIM-ELAB-BIND-032",
                "duplicate constructed SystemC instance path '"
                    + path + "'",
                instance.span);
            return nullptr;
        }
        used_systemc_instances_.insert(path);
        return description;
    }

    void instantiate_systemc(
        const SystemCInstanceDescription& instance,
        const std::string& path,
        SignalMap aliases,
        ObjectMap objects,
        const bool native_child = false) {
        if (!instance_paths_.insert(path).second) {
            report(
                "FSIM-ELAB-HIER-001",
                "duplicate instance path '" + path + "'",
                {});
            return;
        }
        const auto stack_identity =
            native_child
                ? instance.target + "#native:"
                    + std::to_string(instance.handle)
                : instance.target;
        if (std::find(
                stack_.begin(), stack_.end(), stack_identity)
            != stack_.end()) {
            report(
                "FSIM-ELAB-HIER-002",
                "recursive instantiation of '" + instance.target
                    + "' at '" + path + "'",
                {});
            return;
        }
        stack_.push_back(stack_identity);
        used_systemc_instances_.insert(path);

        std::unordered_set<std::uint64_t> connected_ports;
        for (const auto& port : instance.ports) {
            if (objects.contains(port.handle)) {
                connected_ports.insert(port.handle);
            }
            if (port.bound_object != 0
                && !objects.contains(port.bound_object)
                && std::none_of(
                    instance.internal_signals.begin(),
                    instance.internal_signals.end(),
                    [&](const ExternalInternalSignal& signal) {
                        return signal.handle == port.bound_object;
                    })) {
                report(
                    "FSIM-ELAB-BIND-046",
                    "SystemC port '" + path + "." + port.name
                        + "' binds an unknown signal handle",
                    {});
            }
        }

        for (const auto& port : instance.ports) {
            if (!aliases.contains(port.name)) {
                const auto declaration =
                    external_port_declaration(port);
                const auto signal =
                    add_owned_signal(declaration, path, aliases);
                if (signal) {
                    objects.emplace(port.handle, *signal);
                }
            } else if (!objects.contains(port.handle)) {
                objects.emplace(port.handle, aliases.at(port.name));
            }
        }
        for (const auto& event : instance.events) {
            frontend::Type type;
            type.domain = frontend::ValueDomain::Bit2;
            type.spelling = "systemc.event";
            const frontend::SignalDeclaration declaration{
                event.name,
                std::move(type),
                frontend::PortDirection::Unknown,
                false,
                {}};
            const auto signal =
                add_owned_signal(declaration, path, aliases);
            if (signal) {
                objects.emplace(event.handle, *signal);
            }
        }
        for (const auto& signal : instance.internal_signals) {
            std::optional<SignalId> bound_signal;
            bool use_internal_initial = false;
            bool conflicting_aliases = false;
            for (const auto& port : instance.ports) {
                if (port.bound_object != signal.handle) {
                    continue;
                }
                const auto runtime_port = objects.find(port.handle);
                if (runtime_port == objects.end()) {
                    continue;
                }
                if (bound_signal
                    && *bound_signal != runtime_port->second) {
                    report(
                        "FSIM-ELAB-BIND-046",
                        "SystemC ports bound to internal signal '"
                            + path + "." + signal.name
                            + "' connect to different parent signals",
                        {});
                    conflicting_aliases = true;
                    break;
                }
                bound_signal = runtime_port->second;
                use_internal_initial =
                    use_internal_initial
                    || !connected_ports.contains(port.handle)
                    || port.direction
                        != frontend::PortDirection::Input;
            }
            if (conflicting_aliases) {
                continue;
            }
            if (bound_signal) {
                const auto full_name = path + "." + signal.name;
                const auto local =
                    aliases.emplace(signal.name, *bound_signal);
                const auto full =
                    aliases.emplace(full_name, *bound_signal);
                if ((!local.second
                     && local.first->second != *bound_signal)
                    || (!full.second
                        && full.first->second != *bound_signal)) {
                    report(
                        "FSIM-ELAB-BIND-046",
                        "SystemC internal signal alias '" + full_name
                            + "' conflicts with another object",
                        {});
                    continue;
                }
                design_.signal_by_name_.emplace(
                    full_name, *bound_signal);
                if (path == design_.top_) {
                    design_.signal_by_name_.emplace(
                        signal.name, *bound_signal);
                }
                objects.emplace(signal.handle, *bound_signal);
                if (use_internal_initial) {
                    design_.signals_.at(*bound_signal).initial_value =
                        signal.initial_value;
                }
                continue;
            }
            const frontend::SignalDeclaration declaration{
                signal.name,
                signal.type,
                frontend::PortDirection::Unknown,
                false,
                {}};
            const auto runtime_signal =
                add_owned_signal(declaration, path, aliases);
            if (runtime_signal) {
                design_.signals_.at(*runtime_signal).initial_value =
                    signal.initial_value;
                objects.emplace(signal.handle, *runtime_signal);
            }
        }

        std::unordered_map<
            std::uint64_t, const ExternalExport*> exports_by_handle;
        for (const auto& export_object : instance.exports) {
            exports_by_handle.emplace(
                export_object.handle, &export_object);
        }
        std::unordered_set<std::uint64_t> resolving_exports;
        std::unordered_set<std::uint64_t> invalid_exports;
        const auto resolve_export =
            [&](const auto& self,
                const ExternalExport& export_object)
                -> std::optional<SignalId> {
              if (const auto resolved =
                      objects.find(export_object.handle);
                  resolved != objects.end()) {
                  return resolved->second;
              }
              if (export_object.bound_object == 0) {
                  if (invalid_exports.insert(
                          export_object.handle).second) {
                      report(
                          "FSIM-ELAB-BIND-048",
                          "SystemC export '" + path + "."
                              + export_object.name
                              + "' is unbound",
                          {});
                  }
                  return std::nullopt;
              }
              if (!resolving_exports.insert(
                      export_object.handle).second) {
                  if (invalid_exports.insert(
                          export_object.handle).second) {
                      report(
                          "FSIM-ELAB-BIND-048",
                          "SystemC export chain at '" + path + "."
                              + export_object.name
                              + "' is cyclic",
                          {});
                  }
                  return std::nullopt;
              }
              std::optional<SignalId> signal;
              if (const auto direct =
                      objects.find(export_object.bound_object);
                  direct != objects.end()) {
                  signal = direct->second;
              } else if (const auto nested =
                             exports_by_handle.find(
                                 export_object.bound_object);
                         nested != exports_by_handle.end()) {
                  signal = self(self, *nested->second);
              } else if (invalid_exports.insert(
                             export_object.handle).second) {
                  report(
                      "FSIM-ELAB-BIND-048",
                      "SystemC export '" + path + "."
                          + export_object.name
                          + "' binds an unknown object handle",
                      {});
              }
              resolving_exports.erase(export_object.handle);
              if (signal) {
                  objects.emplace(export_object.handle, *signal);
              }
              return signal;
            };
        for (const auto& export_object : instance.exports) {
            const auto signal =
                resolve_export(resolve_export, export_object);
            if (!signal) {
                continue;
            }
            const auto full_name = path + "." + export_object.name;
            const auto local =
                aliases.emplace(export_object.name, *signal);
            const auto full = aliases.emplace(full_name, *signal);
            if ((!local.second && local.first->second != *signal)
                || (!full.second && full.first->second != *signal)) {
                report(
                    "FSIM-ELAB-BIND-048",
                    "SystemC export alias '" + full_name
                        + "' conflicts with another object",
                    {});
                continue;
            }
            design_.signal_by_name_.emplace(full_name, *signal);
            if (path == design_.top_) {
                design_.signal_by_name_.emplace(
                    export_object.name, *signal);
            }
        }

        SystemCInstanceInfo info;
        info.id = static_cast<std::uint32_t>(
            design_.systemc_instances_.size());
        info.target = instance.target;
        info.instance = path;
        info.native_handle = instance.handle;
        info.construction_values =
            instance.construction_values;
        for (const auto& port : instance.ports) {
            if (const auto signal = aliases.find(port.name);
                signal != aliases.end()) {
                info.ports.push_back(
                    {port.name, port.handle, signal->second});
            }
        }
        for (const auto& event : instance.events) {
            if (const auto signal = objects.find(event.handle);
                signal != objects.end()) {
                info.events.push_back(
                    {event.name, event.handle, signal->second});
            }
        }
        for (const auto& channel : instance.primitive_channels) {
            info.primitive_channels.push_back(
                {channel.name, channel.handle});
        }
        for (const auto& signal : instance.internal_signals) {
            if (const auto runtime_signal =
                    objects.find(signal.handle);
                runtime_signal != objects.end()) {
                info.internal_signals.push_back(
                    {signal.name,
                     signal.handle,
                     runtime_signal->second});
            }
        }
        for (const auto& export_object : instance.exports) {
            if (const auto runtime_signal =
                    objects.find(export_object.handle);
                runtime_signal != objects.end()) {
                info.exports.push_back(
                    {export_object.name,
                     export_object.handle,
                     runtime_signal->second});
            }
        }
        design_.systemc_instances_.push_back(std::move(info));

        for (const auto& external : instance.processes) {
#if !defined(FSIM_HAS_BOOST_CONTEXT)
            if (external.kind != FSIM_SC_METHOD) {
                report(
                    "FSIM-ELAB-BIND-042",
                    "SystemC process '" + path + "." + external.name
                        + "' requires SC_THREAD/SC_CTHREAD suspension, "
                          "which is not executable yet",
                    {});
                continue;
            }
#endif
            runtime::simir::Process process;
            process.id = static_cast<ProcessId>(
                design_.processes_.size());
            if (static_cast<std::size_t>(process.id)
                != design_.processes_.size()) {
                report(
                    "FSIM-ELAB-008",
                    "the design has too many processes",
                    {});
                continue;
            }
            process.name = path + "." + external.name;
            process.initialize = external.initialize;
            for (const auto& sensitivity : external.sensitivity) {
                const auto signal = objects.find(sensitivity.object);
                if (signal == objects.end()) {
                    report(
                        "FSIM-ELAB-BIND-043",
                        "SystemC process sensitivity '" + process.name
                            + "' references an unknown registered object",
                        {});
                    continue;
                }
                auto edge = runtime::simir::EdgeKind::any;
                switch (sensitivity.edge) {
                case FSIM_SC_ANY_EDGE:
                    break;
                case FSIM_SC_POSEDGE:
                    edge = runtime::simir::EdgeKind::posedge;
                    break;
                case FSIM_SC_NEGEDGE:
                    edge = runtime::simir::EdgeKind::negedge;
                    break;
                default:
                    report(
                        "FSIM-ELAB-BIND-044",
                        "SystemC process '" + process.name
                            + "' has an invalid sensitivity edge",
                        {});
                    continue;
                }
                const auto& signal_info =
                    design_.signal_info_.at(signal->second);
                if (edge != runtime::simir::EdgeKind::any
                    && signal_info.width != 1) {
                    report(
                        "FSIM-ELAB-BIND-045",
                        "SystemC process edge sensitivity '"
                            + process.name
                            + "' requires a scalar object",
                        {});
                    continue;
                }
                process.static_sensitivity.push_back(
                    {signal->second, edge});
            }
            std::sort(
                process.static_sensitivity.begin(),
                process.static_sensitivity.end(),
                [](const runtime::simir::Sensitivity& left,
                   const runtime::simir::Sensitivity& right) {
                    return left.signal < right.signal
                        || (left.signal == right.signal
                            && left.edge < right.edge);
                });
            process.static_sensitivity.erase(
                std::unique(
                    process.static_sensitivity.begin(),
                    process.static_sensitivity.end()),
                process.static_sensitivity.end());
            process.operations.emplace_back(
                process.static_sensitivity.empty()
                    ? runtime::simir::Operation{
                          runtime::simir::Halt{}}
                    : runtime::simir::Operation{
                          runtime::simir::WaitSensitivity{}});
            design_.systemc_processes_.push_back(
                {process.id, external.handle});
            design_.processes_.push_back(std::move(process));
        }

        for (const auto& child : instance.native_children) {
            const auto prefix = path + ".";
            if (child.parent != instance.handle
                || child.path.size() <= prefix.size()
                || child.path.rfind(prefix, 0) != 0
                || child.path.find('.', prefix.size())
                    != std::string::npos) {
                report(
                    "FSIM-ELAB-BIND-047",
                    "native SystemC child under '" + path
                        + "' has inconsistent hierarchy metadata",
                    {});
                continue;
            }
            SignalMap child_aliases;
            ObjectMap child_objects = objects;
            for (const auto& port : child.ports) {
                if (port.bound_object == 0) {
                    continue;
                }
                const auto signal = objects.find(port.bound_object);
                if (signal == objects.end()) {
                    continue;
                }
                child_aliases.emplace(port.name, signal->second);
                child_objects.emplace(port.handle, signal->second);
            }
            instantiate_systemc(
                child,
                child.path,
                std::move(child_aliases),
                std::move(child_objects),
                true);
        }

        for (const auto& child : instance.foreign_children) {
            const auto child_path = path + "." + child.name;
            const auto* binding = binding_for(child_path);
            if (binding == nullptr) {
                report(
                    "FSIM-ELAB-BIND-040",
                    "SystemC foreign child '" + child_path
                        + "' requires an explicit VHDL or "
                          "Verilog/SystemVerilog binding",
                    {});
                continue;
            }
            const auto target = parse_target(binding->target);
            if (!target || target->language == "systemc") {
                report(
                    "FSIM-ELAB-BIND-041",
                    "SystemC foreign child '" + child_path
                        + "' must bind to an HDL target",
                    {});
                continue;
            }
            if (target->language == "vhdl"
                && !target->architecture) {
                report(
                    "FSIM-ELAB-BIND-016",
                    "an explicit VHDL binding target must name an "
                    "architecture, for example "
                    "vhdl:work.entity(rtl)",
                    {});
                continue;
            }
            const auto* selected =
                choose_bound_unit(parsed_, *target);
            if (selected == nullptr) {
                report(
                    "FSIM-ELAB-BIND-015",
                    "binding target '" + binding->target
                        + "' was not found",
                    {});
                continue;
            }
            std::vector<frontend::ParameterOverride> actuals;
            actuals.reserve(child.construction_actuals.size());
            for (const auto& [name, value] :
                 child.construction_actuals) {
                frontend::Expression expression;
                expression.kind =
                    frontend::ExpressionKind::IntegerLiteral;
                expression.text = std::to_string(value);
                actuals.push_back({
                    name,
                    std::move(expression),
                    {},
                });
            }
            auto specialized =
                specialize_selected_unit(
                    *selected,
                    actuals,
                    {},
                    selected->language);
            auto child_aliases = connect_foreign_child(
                child, specialized.unit, child_path, objects);
            instantiate(
                specialized.unit,
                child_path,
                std::move(child_aliases),
                std::move(specialized.environment),
                std::move(specialized.values));
        }
        stack_.pop_back();
    }

    void instantiate(
        const DesignUnit& unit,
        const std::string& path,
        SignalMap aliases,
        ConstantEnvironment parameter_environment,
        std::vector<std::pair<std::string, std::string>>
            parameter_values) {
        if (!instance_paths_.insert(path).second) {
            report(
                "FSIM-ELAB-HIER-001",
                "duplicate instance path '" + path + "'",
                unit.span);
            return;
        }
        const auto identity = unit_identity(unit);
        if (std::find(stack_.begin(), stack_.end(), identity) != stack_.end()) {
            report(
                "FSIM-ELAB-HIER-002",
                "recursive instantiation of '" + identity + "' at '" + path
                    + "'",
                unit.span);
            return;
        }
        stack_.push_back(identity);

        SignalMap local = std::move(aliases);
        const auto* ports = unit_ports(parsed_, unit);
        if (ports == nullptr) {
            report(
                "FSIM-ELAB-002",
                "architecture '" + unit.name + "' has no matching entity",
                unit.span);
            stack_.pop_back();
            return;
        }
        for (const auto& port : *ports) {
            if (!local.contains(port.name)) {
                (void)add_owned_signal(port, path, local);
            }
        }
        for (const auto& signal : unit.signals) {
            (void)add_owned_signal(signal, path, local);
        }

        const auto specialization_index = design_.specializations_.size();
        const auto specialization_id =
            static_cast<SpecializationId>(specialization_index);
        if (static_cast<std::size_t>(specialization_id)
            != specialization_index) {
            throw std::length_error(
                "too many elaborated design-unit specializations");
        }
        SpecializationInfo specialization;
        specialization.id = specialization_id;
        specialization.unit = identity;
        specialization.instance = path;
        specialization.source =
            std::string{frontend::physical_source(unit.span)};
        if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
            if (const auto* entity = find_vhdl_entity(parsed_, unit);
                entity != nullptr
                && frontend::physical_source(entity->span)
                    != specialization.source) {
                specialization.source_dependencies.push_back(
                    std::string{
                        frontend::physical_source(entity->span)});
            }
        }
        for (const auto& dependency : unit.source_dependencies) {
            if (dependency != specialization.source
                && std::find(
                       specialization.source_dependencies.begin(),
                       specialization.source_dependencies.end(),
                       dependency)
                    == specialization.source_dependencies.end()) {
                specialization.source_dependencies.push_back(
                    dependency);
            }
        }
        specialization.language = unit.language;
        specialization.library =
            unit.library.empty() ? "work" : unit.library;
        specialization.is_cell = unit.is_cell;
        specialization.parameter_values = std::move(parameter_values);

        Lowerer lowerer{design_, local, diagnostics_};
        for (std::size_t index = 0;
             index < unit.concurrent_statements.size(); ++index) {
            auto process = lowerer.lower_concurrent(
                unit.concurrent_statements[index],
                unit.language,
                path,
                index);
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
        }
        for (const auto& process : unit.processes) {
            auto lowered =
                lowerer.lower_process(process, unit.language, path);
            specialization.processes.push_back(lowered.id);
            design_.processes_.push_back(std::move(lowered));
        }
        design_.specializations_.push_back(std::move(specialization));

        for (const auto& instance : unit.instances) {
            const auto child_path = path + "." + instance.name;
            const auto* binding = binding_for(child_path);
            if (binding != nullptr) {
                const auto target = parse_target(binding->target);
                if (target
                    && target->language == "systemc") {
                    const auto* description =
                        construct_systemc_description(
                            instance,
                            child_path,
                            binding->target,
                            parameter_environment,
                            unit.language);
                    if (description == nullptr) {
                        continue;
                    }
                    auto [child_aliases, child_objects] =
                        connect_systemc_instance(
                            instance,
                            *description,
                            child_path,
                            local,
                            binding);
                    instantiate_systemc(
                        *description,
                        child_path,
                        std::move(child_aliases),
                        std::move(child_objects));
                    continue;
                }
            }
            const auto* target =
                bound_target(instance, unit, child_path, binding);
            if (target == nullptr) {
                continue;
            }
            auto child_specialized = specialize_selected_unit(
                *target,
                instance.parameter_overrides,
                parameter_environment,
                unit.language);
            auto child_aliases = connect_instance(
                instance,
                child_specialized.unit,
                child_path,
                local,
                binding,
                target->language != unit.language);
            instantiate(
                child_specialized.unit,
                child_path,
                std::move(child_aliases),
                std::move(child_specialized.environment),
                std::move(child_specialized.values));
        }
        stack_.pop_back();
    }

    void report(
        std::string code,
        std::string message,
        frontend::SourceSpan source) {
        diagnostics_.push_back(
            {std::move(code), std::move(message), std::move(source)});
    }

    const frontend::ParsedDesign& parsed_;
    ElaboratedDesign& design_;
    std::vector<Diagnostic>& diagnostics_;
    std::unordered_map<std::string, const Binding*> bindings_;
    std::unordered_set<std::string> used_bindings_;
    std::unordered_map<
        std::string, const SystemCInstanceDescription*>
        systemc_instances_;
    std::deque<SystemCInstanceDescription>
        owned_systemc_instances_;
    SystemCFactoryProvider* systemc_provider_{};
    std::unordered_set<std::string> used_systemc_instances_;
    std::unordered_set<std::string> instance_paths_;
    std::vector<std::string> stack_;
    std::unordered_map<SignalId, std::size_t> boundary_driver_count_;
    std::unordered_map<SignalId, std::string> resolver_by_signal_;
};

const std::string& ElaboratedDesign::top() const noexcept {
    return top_;
}

const std::vector<SignalInfo>& ElaboratedDesign::signals() const noexcept {
    return signal_info_;
}

const std::vector<runtime::simir::Process>& ElaboratedDesign::processes() const noexcept {
    return processes_;
}

const std::vector<SpecializationInfo>&
ElaboratedDesign::specializations() const noexcept {
    return specializations_;
}

const std::vector<SystemCInstanceInfo>&
ElaboratedDesign::systemc_instances() const noexcept {
    return systemc_instances_;
}

const std::vector<SystemCProcessInfo>&
ElaboratedDesign::systemc_processes() const noexcept {
    return systemc_processes_;
}

std::optional<runtime::simir::SignalId> ElaboratedDesign::find_signal(
    const std::string_view name) const noexcept {
    if (const auto found = signal_by_name_.find(std::string{name});
        found != signal_by_name_.end()) {
        return found->second;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, runtime::simir::SignalId>>
ElaboratedDesign::signal_paths() const {
    std::vector<std::pair<std::string, runtime::simir::SignalId>> result;
    result.reserve(signal_by_name_.size());
    for (const auto& [path, signal] : signal_by_name_) {
        result.emplace_back(path, signal);
    }
    std::sort(
        result.begin(), result.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second < right.second;
        });
    return result;
}

std::unique_ptr<runtime::simir::Interpreter> ElaboratedDesign::create_interpreter(
    const runtime::SchedulerOptions options,
    const std::uint64_t seed) const {
    auto interpreter =
        std::make_unique<runtime::simir::Interpreter>(
            options, seed);
    for (const auto& signal : signals_) {
        (void)interpreter->add_signal(signal);
    }
    for (const auto& process : processes_) {
        (void)interpreter->add_process(process);
    }
    return interpreter;
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed, const std::string_view top) {
    return elaborate(
        parsed,
        top,
        std::span<const Binding>{},
        std::span<const SystemCInstanceDescription>{});
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings) {
    return elaborate(
        parsed,
        top,
        bindings,
        std::span<const SystemCInstanceDescription>{});
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances) {
    return elaborate(
        parsed,
        top,
        bindings,
        systemc_instances,
        nullptr);
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances,
    SystemCFactoryProvider* systemc_provider) {
    ElaborationResult result;
    const auto requested = simple_top_name(top);
    bool systemc_top = false;
    if (top.find(':') != std::string_view::npos) {
        const auto target = parse_target(top);
        if (!target) {
            result.diagnostics.push_back({
                "FSIM-ELAB-003",
                "malformed qualified top-level target '"
                    + std::string{top} + "'",
                {}});
            return result;
        }
        systemc_top = target->language == "systemc";
        if (target->language == "vhdl" && !target->architecture) {
            result.diagnostics.push_back({
                "FSIM-ELAB-004",
                "a qualified VHDL top must name an architecture, for "
                "example vhdl:work.entity(rtl)",
                {}});
            return result;
        }
    }

    ElaboratedDesign design;
    design.top_ = requested;
    HierarchyBuilder builder{
        parsed,
        design,
        result.diagnostics,
        bindings,
        systemc_instances,
        systemc_provider};
    if (systemc_top) {
        const auto root = std::find_if(
            systemc_instances.begin(),
            systemc_instances.end(),
            [&](const SystemCInstanceDescription& instance) {
                return instance.path == requested
                    && instance.target == top;
            });
        if (root == systemc_instances.end()) {
            result.diagnostics.push_back({
                "FSIM-ELAB-001",
                "top-level SystemC factory '" + std::string{top}
                    + "' was not constructed",
                {}});
            return result;
        }
        builder.build(*root);
    } else {
        const auto* unit = choose_top_unit(parsed, top);
        if (unit == nullptr) {
            result.diagnostics.push_back({
                "FSIM-ELAB-001",
                "top-level design unit '" + requested
                    + "' was not found",
                {}});
            return result;
        }
        builder.build(*unit);
    }

    if (!result.diagnostics.empty()) {
        return result;
    }
    result.design = std::move(design);
    return result;
}

} // namespace fsim::elaboration
