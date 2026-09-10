// SPDX-License-Identifier: Apache-2.0

#include "verilog_parser_internal.hpp"
#include "verilog_parser_assertion_support.hpp"

#include <charconv>

#include <algorithm>
#include <map>
#include <unordered_set>

namespace fsim::frontend {

void VerilogParser::resolve_assertion_declaration_references(DesignUnit& unit)
{
    const auto design_names = assertion_design_unit_names(unit);
    std::unordered_set<std::string> assertion_names;
    for (const auto& declaration :
        unit.systemverilog_assertion_declarations) {
        assertion_names.insert(declaration.name);
    }

    for (auto& declaration :
        unit.systemverilog_assertion_declarations) {
        std::unordered_set<std::string> formal_names;
        std::unordered_set<std::string> local_names;
        for (const auto& formal : declaration.formals) {
            formal_names.insert(formal.name);
        }
        for (const auto& local : declaration.local_variables) {
            local_names.insert(local.name);
        }
        std::unordered_set<std::string> sequence_formal_names;
        for (const auto& formal : declaration.formals) {
            if (formal.kind == SystemVerilogAssertionFormalKind::Sequence) {
                sequence_formal_names.insert(formal.name);
            }
        }
        for (const auto& endpoint : declaration.sequence_endpoints) {
            const auto qualified = std::ranges::any_of(
                endpoint.receiver_tokens,
                [](const Token& token) {
                    return token.kind == TokenKind::Dot
                        || token.kind == TokenKind::Scope;
                });
            const auto base = std::ranges::find_if(
                endpoint.receiver_tokens,
                [](const Token& token) {
                    return token.kind == TokenKind::Identifier;
                });
            const auto sequence_declaration = base != endpoint.receiver_tokens.end()
                && std::ranges::any_of(
                    unit.systemverilog_assertion_declarations,
                    [&](const SystemVerilogAssertionDeclaration& candidate) {
                        return candidate.kind
                            == SystemVerilogAssertionDeclarationKind::Sequence
                            && candidate.name == base->text;
                    });
            if (!qualified
                && (base == endpoint.receiver_tokens.end()
                    || (!sequence_declaration
                        && !sequence_formal_names.contains(base->text)))) {
                error(
                    endpoint.receiver_tokens.front(),
                    "FSIM-SV-SEM-197",
                    "sequence endpoint receiver '" + endpoint.receiver_name
                        + "' does not name a sequence declaration or formal");
            }
        }

        const auto collect = [&](const std::span<const Token> tokens) {
            std::size_t position { };
            while (position < tokens.size()) {
                if (tokens[position].kind != TokenKind::Identifier
                    || assertion_reference_keyword(tokens[position].text)
                    || (position != 0U
                        && tokens[position - 1U].kind == TokenKind::Dot
                        && position + 1U < tokens.size()
                        && tokens[position + 1U].kind == TokenKind::LeftParen)
                    || (!tokens[position].text.empty()
                        && tokens[position].text.front() == '$')) {
                    ++position;
                    continue;
                }
                const auto start = position;
                std::vector<std::string> path { tokens[position].text };
                std::string canonical = tokens[position].text;
                bool hierarchical { };
                bool package { };
                while (position + 2U < tokens.size()
                    && (tokens[position + 1U].kind == TokenKind::Dot
                        || tokens[position + 1U].kind == TokenKind::Scope)
                    && tokens[position + 2U].kind == TokenKind::Identifier) {
                    const auto separator = tokens[position + 1U].kind;
                    hierarchical = hierarchical || separator == TokenKind::Dot;
                    package = package || separator == TokenKind::Scope;
                    canonical += separator == TokenKind::Dot ? "." : "::";
                    canonical += tokens[position + 2U].text;
                    path.push_back(tokens[position + 2U].text);
                    position += 2U;
                }

                SystemVerilogAssertionReference reference;
                reference.canonical_name = std::move(canonical);
                reference.path = std::move(path);
                reference.tokens.assign(
                    tokens.begin() + static_cast<std::ptrdiff_t>(start),
                    tokens.begin() + static_cast<std::ptrdiff_t>(position + 1U));
                reference.span = span_from(tokens[start], tokens[position]);
                const auto& root = reference.path.front();
                if (package) {
                    reference.kind = SystemVerilogAssertionReferenceKind::Package;
                } else if (hierarchical) {
                    reference.kind = SystemVerilogAssertionReferenceKind::Hierarchical;
                } else if (formal_names.contains(root)) {
                    reference.kind = SystemVerilogAssertionReferenceKind::Formal;
                } else if (local_names.contains(root)) {
                    reference.kind = SystemVerilogAssertionReferenceKind::LocalVariable;
                } else if (assertion_names.contains(root)) {
                    reference.kind = SystemVerilogAssertionReferenceKind::AssertionDeclaration;
                } else if (design_names.contains(root)) {
                    reference.kind = SystemVerilogAssertionReferenceKind::DesignUnitObject;
                } else {
                    error(
                        tokens[start],
                        "FSIM-SV-SEM-196",
                        "assertion reference '" + root
                            + "' is not declared in an assertion or design-unit scope");
                    ++position;
                    continue;
                }
                declaration.references.push_back(std::move(reference));
                ++position;
            }
        };

        for (const auto& formal : declaration.formals) {
            collect(formal.default_tokens);
        }
        for (const auto& local : declaration.local_variables) {
            collect(local.initializer_tokens);
        }
        if (declaration.clock) {
            collect(declaration.clock->event_tokens);
        }
        if (declaration.disable) {
            collect(declaration.disable->condition_tokens);
        }
        if (declaration.kind
            != SystemVerilogAssertionDeclarationKind::Checker) {
            collect(declaration.expression_tokens);
        }
    }
}

namespace assertion_resolution_detail {

std::optional<Expression> scalar_atom(const Token& token)
{
        if (token.kind == TokenKind::Identifier) {
            return Expression {
                ExpressionKind::Identifier,
                token.text,
                { },
                token.span
            };
        }
        if (token.kind == TokenKind::Number) {
            return Expression {
                token.text.find('\'') == std::string::npos
                    ? ExpressionKind::IntegerLiteral
                    : ExpressionKind::LogicLiteral,
                token.text,
                { },
                token.span
            };
        }
        return std::nullopt;
}

std::optional<Expression> scalar_expression(
    const std::span<const Token> tokens)
{
        auto expression_tokens = tokens;
        while (expression_tokens.size() >= 2U
            && expression_tokens.front().kind == TokenKind::LeftParen
            && expression_tokens.back().kind == TokenKind::RightParen) {
            int depth { };
            bool encloses_expression { true };
            for (std::size_t index = 0; index < expression_tokens.size(); ++index) {
                if (expression_tokens[index].kind == TokenKind::LeftParen) {
                    ++depth;
                } else if (expression_tokens[index].kind
                    == TokenKind::RightParen) {
                    --depth;
                    if (depth == 0U && index + 1U != expression_tokens.size()) {
                        encloses_expression = false;
                        break;
                    }
                }
            }
            if (!encloses_expression || depth != 0) {
                break;
            }
            expression_tokens = expression_tokens.subspan(
                1U, expression_tokens.size() - 2U);
        }
        if (expression_tokens.size() == 1U) {
            return scalar_atom(expression_tokens.front());
        }
        if (expression_tokens.size() == 2U
            && (expression_tokens.front().kind == TokenKind::Bang
                || expression_tokens.front().text == "not")
            && expression_tokens.back().kind == TokenKind::Identifier) {
            return Expression {
                ExpressionKind::Unary,
                "!",
                { Expression {
                    ExpressionKind::Identifier,
                    expression_tokens.back().text,
                    { },
                    expression_tokens.back().span } },
                cover(
                    expression_tokens.front().span,
                    expression_tokens.back().span)
            };
        }
        const auto binary = expression_tokens.size() == 3U
            && (expression_tokens[1].kind == TokenKind::EqualEqual
                || expression_tokens[1].kind == TokenKind::CaseEqual
                || expression_tokens[1].kind == TokenKind::WildcardEqual
                || expression_tokens[1].kind == TokenKind::NotEqual
                || expression_tokens[1].kind == TokenKind::CaseNotEqual
                || expression_tokens[1].kind == TokenKind::WildcardNotEqual
                || expression_tokens[1].kind == TokenKind::Less
                || expression_tokens[1].kind == TokenKind::LessEqual
                || expression_tokens[1].kind == TokenKind::Greater
                || expression_tokens[1].kind == TokenKind::GreaterEqual
                || expression_tokens[1].kind == TokenKind::AndAnd
                || expression_tokens[1].kind == TokenKind::OrOr
                || expression_tokens[1].kind == TokenKind::Plus
                || expression_tokens[1].kind == TokenKind::Minus
                || expression_tokens[1].kind == TokenKind::Star
                || expression_tokens[1].kind == TokenKind::Slash
                || expression_tokens[1].kind == TokenKind::Percent
                || expression_tokens[1].kind == TokenKind::Ampersand
                || expression_tokens[1].kind == TokenKind::Pipe
                || expression_tokens[1].kind == TokenKind::Caret
                || expression_tokens[1].kind == TokenKind::ShiftLeft
                || expression_tokens[1].kind == TokenKind::ShiftRight
                || expression_tokens[1].kind
                    == TokenKind::ArithmeticShiftLeft
                || expression_tokens[1].kind
                    == TokenKind::ArithmeticShiftRight
                || expression_tokens[1].text == "and"
                || expression_tokens[1].text == "or"
                || expression_tokens[1].text == "iff");
        if (binary) {
            auto left = scalar_atom(expression_tokens.front());
            auto right = scalar_atom(expression_tokens.back());
            if (left && right) {
                const auto operation = expression_tokens[1].text == "and"
                    ? "&&"
                    : expression_tokens[1].text == "or"
                    ? "||"
                    : expression_tokens[1].text == "iff"
                    ? "=="
                    : expression_tokens[1].text;
                return Expression {
                    ExpressionKind::Binary,
                    operation,
                    { std::move(*left), std::move(*right) },
                    cover(
                        expression_tokens.front().span,
                        expression_tokens.back().span)
                };
            }
        }
        return std::nullopt;
}

std::optional<Type> assertion_local_type(
    const std::span<const Token> tokens)
{
        if (tokens.empty())
            return std::nullopt;
        Type type;
        const auto spelling = tokens.front().text;
        if (spelling == "byte" || spelling == "shortint"
            || spelling == "int" || spelling == "longint"
            || spelling == "integer" || spelling == "time") {
            type.spelling = spelling;
            type.domain = spelling == "integer" || spelling == "time"
                ? ValueDomain::Logic4
                : ValueDomain::Bit2;
            type.is_signed = spelling != "time";
            const std::int64_t width = spelling == "byte"     ? 8
                : spelling == "shortint"                      ? 16
                : spelling == "longint" || spelling == "time" ? 64
                                                              : 32;
            type.packed_range = PackedRange { width - 1, 0, true };
            if (spelling == "time")
                type.systemverilog_scalar = SystemVerilogScalarKind::Time;
            return type;
        }
        if (spelling == "logic" || spelling == "reg" || spelling == "bit"
            || spelling == "signed" || spelling == "unsigned"
            || tokens.front().kind == TokenKind::LeftBracket) {
            type.spelling = spelling == "bit" ? "bit" : "logic";
            type.domain = spelling == "bit" ? ValueDomain::Bit2
                                            : ValueDomain::Logic4;
            type.is_signed = spelling == "signed";
            const auto signed_token = std::ranges::find_if(
                tokens,
                [](const Token& token) {
                    return token.text == "signed" || token.text == "unsigned";
                });
            if (signed_token != tokens.end())
                type.is_signed = signed_token->text == "signed";
            const auto left = std::ranges::find(
                tokens, TokenKind::LeftBracket, &Token::kind);
            if (left != tokens.end()) {
                const auto offset = static_cast<std::size_t>(left - tokens.begin());
                if (offset + 4U >= tokens.size()
                    || tokens[offset + 1U].kind != TokenKind::Number
                    || tokens[offset + 2U].kind != TokenKind::Colon
                    || tokens[offset + 3U].kind != TokenKind::Number
                    || tokens[offset + 4U].kind != TokenKind::RightBracket) {
                    return std::nullopt;
                }
                std::int64_t left_bound { };
                std::int64_t right_bound { };
                const auto parse_bound = [](const std::string& text,
                                             std::int64_t& value) {
                    const auto parsed = std::from_chars(
                        text.data(), text.data() + text.size(), value);
                    return parsed.ec == std::errc { }
                    && parsed.ptr == text.data() + text.size();
                };
                if (!parse_bound(tokens[offset + 1U].text, left_bound)
                    || !parse_bound(tokens[offset + 3U].text, right_bound)) {
                    return std::nullopt;
                }
                type.packed_range = PackedRange {
                    left_bound, right_bound, left_bound >= right_bound
                };
            }
            return type;
        }
        if (tokens.front().kind == TokenKind::Identifier) {
            type.spelling = spelling;
            type.named_type = spelling;
            type.named_type_span = tokens.front().span;
            return type;
        }
        return std::nullopt;
}

std::vector<Statement> action_statements(
    const std::span<const Token> tokens)
{
            std::vector<Statement> result;
            auto position = tokens.begin();
            while (position != tokens.end()) {
                const auto task = std::ranges::find_if(
                    position, tokens.end(), [](const Token& token) {
                        return token.text == "$display"
                            || token.text == "$write"
                            || token.text == "$info"
                            || token.text == "$warning"
                            || token.text == "$error"
                            || token.text == "$fatal";
                    });
                if (task == tokens.end()) {
                    break;
                }
                const auto statement_end = std::ranges::find_if(
                    task, tokens.end(), [](const Token& token) {
                        return token.kind == TokenKind::Semicolon;
                    });
                Statement statement;
                const bool display = task->text == "$display" || task->text == "$write";
                statement.kind = display ? StatementKind::Display : StatementKind::Report;
                statement.output_newline = task->text != "$write";
                if (task->text == "$info") {
                    statement.assertion_severity = AssertionSeverity::Note;
                } else if (task->text == "$warning") {
                    statement.assertion_severity = AssertionSeverity::Warning;
                } else if (task->text == "$fatal") {
                    statement.assertion_severity = AssertionSeverity::Failure;
                } else {
                    statement.assertion_severity = AssertionSeverity::Error;
                }
                const auto message = std::ranges::find_if(
                    task, statement_end, [](const Token& token) {
                        return token.kind == TokenKind::StringLiteral;
                    });
                if (message != statement_end) {
                    statement.output_text = message->text.size() >= 2U
                        ? message->text.substr(1U, message->text.size() - 2U)
                        : message->text;
                } else {
                    statement.output_text = task->text;
                }
                statement.span = statement_end == tokens.end()
                    ? task->span
                    : cover(task->span, statement_end->span);
                result.push_back(std::move(statement));
                position = statement_end == tokens.end()
                    ? tokens.end()
                    : std::next(statement_end);
            }
            return result;
}

} // namespace assertion_resolution_detail

} // namespace fsim::frontend
