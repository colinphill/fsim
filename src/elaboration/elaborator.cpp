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
                    expanded.push_back(c);
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

using ConstantEnvironment =
    std::unordered_map<std::string, std::int64_t>;
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
    if (expression.text == "<<" || expression.text == ">>") {
        if (*left < 0 || *right < 0 || *right >= 63) {
            error = "constant shifts require a nonnegative value and an "
                    "amount from 0 through 62";
            return std::nullopt;
        }
        if (expression.text == "<<") {
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
    if (expression.text == "&&") {
        return *right != 0 ? 1 : 0;
    }
    if (expression.text == "||") {
        return *right != 0 ? 1 : 0;
    }
    if (expression.text == "==" || expression.text == "=") {
        return *left == *right ? 1 : 0;
    }
    if (expression.text == "!=" || expression.text == "/=") {
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
        substitute_parameters(
            statement.statements,
            environment,
            domains,
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
    const bool is_verilog =
        source.language == frontend::Language::SystemVerilog2017
        || source.language == frontend::Language::Verilog2005;
    const bool association_is_vhdl =
        association_language == frontend::Language::Vhdl2008;
    const auto code = [&](const SpecializationDiagnostic diagnostic) {
        return specialization_diagnostic_code(
            is_vhdl, is_package, diagnostic);
    };
    const auto object_kind =
        is_package ? std::string_view{"package constant"}
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
        next_register_ = 0;
        register_widths_.clear();
        register_domains_.clear();
        locals_.clear();
        local_signed_.clear();
        local_ranges_.clear();
        process_.id = static_cast<ProcessId>(design_.processes_.size());
        process_.name = std::string(hierarchy) + "."
            + (source.name.empty()
                   ? "process_" + std::to_string(process_.id)
                   : source.name);
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
        if (source.kind == ProcessKind::Initial) {
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
        process_.id = static_cast<ProcessId>(design_.processes_.size());
        process_.name = name + ".concurrent_" + std::to_string(order);
        emit_debug_point(DebugPointKind::process_entry, statement.span);

        std::set<std::string> dependencies;
        collect_identifiers(statement.value, dependencies);
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
            if (locals_.contains(variable.name)
                || signals_.contains(variable.name)) {
                report(
                    "FSIM-ELAB-053",
                    "duplicate or shadowing local variable '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            const auto register_id =
                allocate_register(*width, variable.type.domain);
            locals_.emplace(variable.name, register_id);
            local_signed_.emplace(
                variable.name, variable.type.is_signed);
            local_ranges_.emplace(
                variable.name, variable.type.packed_range);
            process_.debug_locals.push_back(DebugLocal{
                variable.name,
                variable.type.spelling,
                register_id,
                *width,
                SourceLocation{
                    variable.span.source_name,
                    static_cast<std::uint32_t>(
                        variable.span.begin.line),
                    static_cast<std::uint32_t>(
                        variable.span.begin.column)}});
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
        if (!statement.declarations.empty()) {
            report(
                "FSIM-ELAB-055",
                "nested procedural block variables are not executable in "
                "this slice",
                statement.span);
        }
        if (statement.kind != StatementKind::Block) {
            auto kind = DebugPointKind::statement;
            if (statement.kind == StatementKind::Assert) {
                kind = DebugPointKind::assertion;
            } else if (
                statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn) {
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
            if (!signals.empty()) {
                process_.operations.emplace_back(
                    WaitOn{std::move(signals), std::move(edges)});
            }
            lower_statements(statement.statements);
            break;
        }
        case StatementKind::Finish:
            process_.operations.emplace_back(Stop{});
            break;
        case StatementKind::Block:
            lower_statements(statement.statements);
            break;
        case StatementKind::Null:
            break;
        }
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

    void lower_assignment(const Statement& statement) {
        const Expression* base = &statement.target;
        std::optional<std::uint32_t> selected_offset;
        std::optional<std::size_t> selected_width;
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

        const auto target_name = base->text;
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

        if (statement.target.kind == ExpressionKind::Index) {
            const auto index =
                constant_index(statement.target.operands[1]);
            const auto offset =
                index
                    ? select_offset(*base, *index, whole_width)
                    : std::nullopt;
            if (!index || !offset
                || *offset
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "an assignment bit-select requires a constant index "
                    "inside the target's declared packed range",
                    statement.target.span);
                return;
            }
            selected_offset =
                static_cast<std::uint32_t>(*offset);
            selected_width = 1;
        } else if (statement.target.kind == ExpressionKind::Slice) {
            const auto left =
                constant_index(statement.target.operands[1]);
            const auto right =
                constant_index(statement.target.operands[2]);
            if (!left || !right) {
                report(
                    "FSIM-ELAB-068",
                    "an assignment part-select requires constant integer "
                    "bounds",
                    statement.target.span);
                return;
            }
            const auto range = expression_range(*base, whole_width);
            const auto offset =
                select_offset(*base, *right, whole_width);
            const auto left_offset =
                select_offset(*base, *left, whole_width);
            const auto width = index_distance(*left, *right) + 1;
            const bool selected_descending = *left >= *right;
            const bool direction_matches =
                *left == *right
                || (range
                    && selected_descending
                        == (range->left >= range->right));
            if (!offset || !left_offset || !direction_matches
                || *offset
                    > std::numeric_limits<std::uint32_t>::max()
                || width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "assignment part-select bounds "
                        + std::to_string(*left) + ":"
                        + std::to_string(*right)
                        + " are outside or reverse the target's declared "
                          "packed range",
                    statement.target.span);
                return;
            }
            selected_offset =
                static_cast<std::uint32_t>(*offset);
            selected_width = static_cast<std::size_t>(width);
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
                register_domain(local->second);
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
            design_.signal_info_[signal->second].source_domain;
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
                    BinaryOperator::case_equal,
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

    std::optional<RegisterId> lower_expression(
        const Expression& expression, const std::size_t expected_width) {
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                return local->second;
            }
            const auto found = signals_.find(expression.text);
            if (found == signals_.end()) {
                report(
                    "FSIM-ELAB-040",
                    "unknown identifier '" + expression.text + "'",
                    expression.span);
                return std::nullopt;
            }
            const auto& signal = design_.signal_info_[found->second];
            const auto destination =
                allocate_register(signal.width, signal.source_domain);
            process_.operations.emplace_back(ReadSignal{destination, found->second});
            return destination;
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
            const auto left =
                constant_index(expression.operands[1]);
            const auto right =
                constant_index(expression.operands[2]);
            if (!source_width || !left || !right) {
                report(
                    "FSIM-ELAB-068",
                    "a part-select requires an inferable packed source and "
                    "constant integer bounds",
                    expression.span);
                return std::nullopt;
            }
            const auto range =
                expression_range(
                    expression.operands[0], *source_width);
            const auto offset = select_offset(
                expression.operands[0], *right, *source_width);
            const auto left_offset = select_offset(
                expression.operands[0], *left, *source_width);
            const auto width = index_distance(*left, *right) + 1;
            const bool selected_descending = *left >= *right;
            const bool direction_matches =
                *left == *right
                || (range
                    && selected_descending
                        == (range->left >= range->right));
            if (!offset || !left_offset || !direction_matches
                || width
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-068",
                    "part-select bounds "
                        + std::to_string(*left) + ":"
                        + std::to_string(*right)
                        + " are outside or reverse the source's declared "
                          "packed range",
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
                static_cast<std::size_t>(width),
                register_domain(*source));
            process_.operations.emplace_back(Extract{
                destination,
                *source,
                static_cast<std::uint32_t>(*offset),
                static_cast<std::uint32_t>(width)});
            return destination;
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
                || expression.text == "^")) {
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
            if (expression.text == "&") {
                operation = ReductionOperator::bit_and;
            } else if (expression.text == "|") {
                operation = ReductionOperator::bit_or;
            }
            const auto destination =
                allocate_register(1, result_domain);
            process_.operations.emplace_back(
                Reduction{operation, destination, *source});
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
                || expression.text == ">>")) {
            const auto value_width =
                infer_width(expression.operands[0])
                    .value_or(expected_width);
            const auto amount_width =
                infer_width(expression.operands[1])
                    .value_or(expected_width);
            const auto value =
                lower_expression(
                    expression.operands[0], value_width);
            const auto amount =
                lower_expression(
                    expression.operands[1], amount_width);
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
                expression.text == "<<"
                    ? ShiftOperator::logical_left
                    : ShiftOperator::logical_right,
                destination,
                *value,
                *amount});
            return destination;
        }
        if (expression.kind == ExpressionKind::Binary && expression.operands.size() == 2) {
            const auto width = infer_width(expression).value_or(expected_width);
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
                       || expression.text == "xnor") {
                operation = BinaryOperator::bit_xor;
                invert_result = expression.text == "xnor";
            } else if (expression.text == "+") {
                operation = BinaryOperator::add_unsigned;
            } else if (expression.text == "-") {
                operation = BinaryOperator::subtract_unsigned;
            } else if (expression.text == "*") {
                operation = BinaryOperator::multiply_unsigned;
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
        if (expression.kind == ExpressionKind::Identifier) {
            if (const auto local = locals_.find(expression.text);
                local != locals_.end()) {
                return register_width(local->second);
            }
            if (const auto found = signals_.find(expression.text); found != signals_.end()) {
                return design_.signal_info_[found->second].width;
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
                || expression.text == "^") {
                return false;
            }
            return is_signed_expression(expression.operands[0]);
        case ExpressionKind::Call:
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
                || expression.text == ">>") {
                return is_signed_expression(expression.operands[0]);
            }
            if (expression.text == "=="
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
            output.insert(expression.text);
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
            case StatementKind::Delay:
            case StatementKind::WaitOn:
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
    frontend::Language language_{frontend::Language::Vhdl2008};
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

    void import_vhdl_package_constants(
        DesignUnit& unit,
        const std::span<const frontend::VhdlContextItem>
            context) {
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
                auto specialized = specialize_unit(
                    *package,
                    {},
                    {},
                    frontend::Language::Vhdl2008,
                    diagnostics_);
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
                if (dependencies.insert(
                        package->span.source_name).second) {
                    unit.source_dependencies.push_back(
                        package->span.source_name);
                }
            }
        }
        imports.insert(
            imports.end(),
            std::make_move_iterator(unit.parameters.begin()),
            std::make_move_iterator(unit.parameters.end()));
        unit.parameters = std::move(imports);
    }

    DesignUnit effective_unit(const DesignUnit& selected) {
        auto result = selected;
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
        import_vhdl_package_constants(result, context);
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
            declaration.type.domain,
            declaration.type.is_signed,
            declaration.type.packed_range,
            declaration.is_port,
            declaration.direction});
        auto initial = Logic4::x;
        if (declaration.type.spelling == "tri0") {
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
        const frontend::SourceSpan& source) {
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
            validate_boundary_type(port, actual_info, path, connection.span);
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
                placeholder, actual_info, path, {});
            validate_boundary_type(
                *formal, actual_info, path, {});
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
        specialization.source = unit.span.source_name;
        if (unit.kind == frontend::UnitKind::VhdlArchitecture) {
            if (const auto* entity = find_vhdl_entity(parsed_, unit);
                entity != nullptr
                && entity->span.source_name
                    != specialization.source) {
                specialization.source_dependencies.push_back(
                    entity->span.source_name);
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
    const runtime::SchedulerOptions options) const {
    auto interpreter = std::make_unique<runtime::simir::Interpreter>(options);
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
