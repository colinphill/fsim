// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <functional>

namespace fsim::frontend {

void VerilogParser::skip_case_statement()
{
    std::size_t depth = 1;
    while (!at_end() && depth != 0) {
        if (keyword("case") || keyword("casez") || keyword("casex")) {
            ++depth;
            advance();
        } else if (match_keyword("endcase")) {
            --depth;
        } else {
            advance();
        }
    }
}

Statement VerilogParser::parse_case_statement(
    const Token& start,
    const CaseMatchKind match_kind,
    const CaseQualifier qualifier)
{
    Statement statement;
    statement.kind = StatementKind::Case;
    statement.case_match_kind = match_kind;
    statement.case_qualifier = qualifier;
    expect(TokenKind::LeftParen, "'(' after case", "FSIM-SV-PARSE-046");
    statement.condition = parse_expression();
    expect(TokenKind::RightParen, "')' after case expression",
        "FSIM-SV-PARSE-047");
    if (at(TokenKind::Identifier) && current().text == "inside") {
        advance();
        if (language_ != Language::SystemVerilog2017) {
            error(previous(), "FSIM-SV-PARSE-180",
                "case inside matching requires SystemVerilog");
        }
        if (match_kind != CaseMatchKind::Exact) {
            error(previous(), "FSIM-SV-PARSE-184",
                "case inside cannot be combined with casez or casex");
        }
        statement.case_match_kind = CaseMatchKind::Inside;
    } else if (at(TokenKind::Identifier)
        && current().text == "matches") {
        advance();
        if (language_ != Language::SystemVerilog2017) {
            error(previous(), "FSIM-SV-PARSE-187",
                "case matches pattern matching requires SystemVerilog");
        }
        if (match_kind != CaseMatchKind::Exact) {
            error(previous(), "FSIM-SV-PARSE-188",
                "bounded case matches cannot be combined with casez or casex");
        }
        statement.case_match_kind = CaseMatchKind::Matches;
    }

    bool saw_default = false;
    const auto skip_pattern_tail = [&]() {
        std::size_t parentheses = 0;
        std::size_t brackets = 0;
        std::size_t braces = 0;
        while (!at_end() && !keyword("endcase")) {
            if (at(TokenKind::Colon) && parentheses == 0
                && brackets == 0 && braces == 0) {
                return;
            }
            if (at(TokenKind::LeftParen)) {
                ++parentheses;
            } else if (at(TokenKind::RightParen) && parentheses != 0) {
                --parentheses;
            } else if (at(TokenKind::LeftBracket)) {
                ++brackets;
            } else if (at(TokenKind::RightBracket) && brackets != 0) {
                --brackets;
            } else if (at(TokenKind::LeftBrace)) {
                ++braces;
            } else if (at(TokenKind::RightBrace) && braces != 0) {
                --braces;
            }
            advance();
        }
    };
    std::function<Expression()> parse_match_pattern;
    parse_match_pattern = [&]() -> Expression {
        if (match(TokenKind::Dot)) {
            const auto pattern_start = previous();
            if (match(TokenKind::Star)) {
                return Expression {
                    ExpressionKind::Call,
                    "@match-wildcard",
                    { },
                    cover(pattern_start.span, previous().span)
                };
            }
            if (at(TokenKind::Identifier)) {
                const auto variable = advance();
                return Expression {
                    ExpressionKind::Call,
                    "@match-bind:" + variable.text,
                    { },
                    cover(pattern_start.span, variable.span)
                };
            }
            error(pattern_start, "FSIM-SV-PARSE-190",
                "expected '*' or a pattern variable after '.'");
            return Expression {
                ExpressionKind::Call,
                "@match-unsupported",
                { },
                pattern_start.span
            };
        }
        if (match_keyword("tagged")) {
            const auto tagged = previous();
            const auto member = expect_identifier(
                "a tagged-union member in a case pattern");
            std::vector<Expression> nested;
            if (!at(TokenKind::Colon)
                && !at(TokenKind::AndAndAnd)) {
                nested.push_back(parse_match_pattern());
            }
            return Expression {
                ExpressionKind::Call,
                "@match-tagged:" + member.text,
                std::move(nested),
                cover(tagged.span, previous().span)
            };
        }
        if (match(TokenKind::Apostrophe)) {
            const auto apostrophe = previous();
            expect(TokenKind::LeftBrace,
                "'{' to begin a structured case pattern",
                "FSIM-SV-PARSE-191");
            Expression result {
                ExpressionKind::Aggregate,
                "@match-structure",
                { },
                apostrophe.span
            };
            if (!at(TokenKind::RightBrace)) {
                for (;;) {
                    std::string choice;
                    std::vector<Expression> choice_expressions;
                    if (at(TokenKind::Identifier)
                        && at(TokenKind::Colon, 1)) {
                        const auto member = advance();
                        advance();
                        choice = "@key";
                        choice_expressions.push_back(Expression {
                            ExpressionKind::Identifier,
                            member.text,
                            { },
                            member.span });
                    }
                    result.operands.push_back(parse_match_pattern());
                    result.aggregate_choices.push_back(std::move(choice));
                    result.aggregate_choice_expressions.push_back(
                        std::move(choice_expressions));
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            expect(TokenKind::RightBrace,
                "'}' after a structured case pattern",
                "FSIM-SV-PARSE-192");
            result.span = cover(apostrophe.span, previous().span);
            return result;
        }
        return parse_expression();
    };
    while (!at_end() && !keyword("endcase")) {
        const auto item_start = current();
        CaseAlternative alternative;
        if (match_keyword("default")) {
            alternative.is_default = true;
            if (saw_default) {
                error(item_start, "FSIM-SV-SEM-014",
                    "a case statement may contain only one default item");
            }
            saw_default = true;
        } else {
            const auto parse_choice = [&]() {
                if (statement.case_match_kind == CaseMatchKind::Matches) {
                    alternative.choices.push_back(parse_match_pattern());
                    return;
                }
                if (statement.case_match_kind == CaseMatchKind::Inside
                    && match(TokenKind::LeftBracket)) {
                    const auto range_start = previous();
                    auto low = parse_expression();
                    expect(TokenKind::Colon, "':' in case inside range",
                        "FSIM-SV-PARSE-181");
                    auto high = parse_expression();
                    expect(TokenKind::RightBracket, "']' after case inside range",
                        "FSIM-SV-PARSE-182");
                    alternative.choices.push_back(Expression {
                        ExpressionKind::Call,
                        "@inside-range",
                        { std::move(low), std::move(high) },
                        cover(range_start.span, previous().span) });
                    return;
                }
                alternative.choices.push_back(parse_expression());
            };
            if (statement.case_match_kind == CaseMatchKind::Inside
                && at(TokenKind::Colon)) {
                error(current(), "FSIM-SV-PARSE-183",
                    "a case inside item requires at least one choice");
            } else {
                parse_choice();
            }
            if (statement.case_match_kind == CaseMatchKind::Matches
                && match(TokenKind::AndAndAnd)) {
                const auto guard_start = previous();
                auto guard = parse_expression();
                if (alternative.choices.size() == 1U) {
                    auto pattern = std::move(alternative.choices.front());
                    alternative.choices.front() = Expression {
                        ExpressionKind::Call,
                        "@match-guard",
                        { std::move(pattern), std::move(guard) },
                        cover(item_start.span, previous().span)
                    };
                } else {
                    error(guard_start, "FSIM-SV-PARSE-189",
                        "a guarded case matches item requires exactly one "
                        "pattern");
                }
            }
            if (statement.case_match_kind == CaseMatchKind::Matches
                && match(TokenKind::Comma)) {
                error(previous(), "FSIM-SV-PARSE-189",
                    "a case matches item accepts exactly one pattern");
                skip_pattern_tail();
            }
            while (match(TokenKind::Comma)) {
                if (statement.case_match_kind == CaseMatchKind::Inside
                    && at(TokenKind::Colon)) {
                    error(current(), "FSIM-SV-PARSE-183",
                        "a case inside item cannot end with an empty choice");
                    break;
                }
                parse_choice();
            }
        }
        expect(TokenKind::Colon, "':' after case item",
            "FSIM-SV-PARSE-048");
        if (auto body = parse_statement()) {
            alternative.statements.push_back(std::move(*body));
        }
        alternative.span = cover(item_start.span, previous().span);
        statement.case_alternatives.push_back(std::move(alternative));
    }
    expect_keyword("endcase", false, "FSIM-SV-PARSE-049");
    statement.span = span_from(start, previous());
    return statement;
}

void VerilogParser::parse_procedural_loop_body(
    const Token& start, Statement& statement)
{
    (void)start;
    ++current_loop_depth_;
    auto body = parse_statement();
    --current_loop_depth_;
    if (!body) {
        return;
    }
    if (body->kind == StatementKind::Block
        && body->label.empty()
        && body->declarations.empty()) {
        statement.statements = std::move(body->statements);
        return;
    }
    statement.statements.push_back(std::move(*body));
}

Statement VerilogParser::parse_procedural_for_statement(const Token& start)
{
    Statement statement;
    statement.kind = StatementKind::Loop;
    expect(
        TokenKind::LeftParen,
        "'(' after procedural for",
        "FSIM-SV-PARSE-095");
    const bool built_in_loop_type = keyword("byte") || keyword("shortint")
        || keyword("int") || keyword("longint")
        || keyword("integer") || keyword("time")
        || keyword("bit") || keyword("logic")
        || keyword("reg") || keyword("signed")
        || keyword("unsigned") || at(TokenKind::LeftBracket);
    const bool named_loop_type = at(TokenKind::Identifier)
        && at(TokenKind::Identifier, 1);
    const bool inline_variable = built_in_loop_type || named_loop_type;
    if (inline_variable) {
        (void)require_standard(
            "an inline procedural loop declaration",
            StandardRevision::SystemVerilog2005,
            current(),
            "FSIM-SV-PARSE-347");
        if (language_ == Language::Verilog2005) {
            error(
                current(), "FSIM-VERILOG-SEM-013",
                "a procedural for-loop declaration requires SystemVerilog");
        }
        (void)parse_parameter_type();
    }
    const auto variable = expect_identifier("procedural loop variable");
    statement.loop_variable = variable.text;
    statement.loop_variable_declared = inline_variable;
    statement.target = Expression {
        ExpressionKind::Identifier,
        variable.text,
        { },
        variable.span
    };
    expect(
        TokenKind::Assign,
        "'=' after procedural loop variable",
        "FSIM-SV-PARSE-096");
    statement.loop_initial = parse_expression();
    ++current_loop_names_[statement.loop_variable];
    expect(
        TokenKind::Semicolon,
        "';' after procedural loop initializer",
        "FSIM-SV-PARSE-097");

    auto condition = parse_expression();
    statement.condition = condition;
    expect(
        TokenKind::Semicolon,
        "';' after procedural loop condition",
        "FSIM-SV-PARSE-098");
    const bool canonical_condition = condition.kind == ExpressionKind::Binary
        && condition.operands.size() == 2
        && condition.operands.front().kind
            == ExpressionKind::Identifier
        && condition.operands.front().text
            == statement.loop_variable
        && (condition.text == "<" || condition.text == "<="
            || condition.text == ">" || condition.text == ">=");
    if (!canonical_condition) {
        statement.loop_runtime = true;
    } else {
        statement.loop_descending = condition.text == ">" || condition.text == ">=";
        statement.loop_limit_exclusive = condition.text == "<" || condition.text == ">";
        statement.loop_limit = condition.operands[1];
    }
    if (!inline_variable) {
        statement.loop_runtime = true;
    }

    std::optional<Token> prefix_update;
    if (match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
        prefix_update = previous();
        (void)require_standard(
            "an increment or decrement loop update",
            StandardRevision::SystemVerilog2005,
            *prefix_update,
            "FSIM-SV-PARSE-347");
    }
    const auto iteration_variable = expect_identifier("procedural loop iteration variable");
    statement.loop_update_target = Expression {
        ExpressionKind::Identifier,
        iteration_variable.text,
        { },
        iteration_variable.span
    };
    std::string update_operation;
    std::optional<Expression> update_operand;
    if (prefix_update) {
        update_operation = prefix_update->kind == TokenKind::PlusPlus ? "+" : "-";
        update_operand = Expression {
            ExpressionKind::IntegerLiteral,
            "1",
            { },
            prefix_update->span
        };
    } else if (
        match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
        (void)require_standard(
            "an increment or decrement loop update",
            StandardRevision::SystemVerilog2005,
            previous(),
            "FSIM-SV-PARSE-347");
        update_operation = previous().kind == TokenKind::PlusPlus ? "+" : "-";
        update_operand = Expression {
            ExpressionKind::IntegerLiteral,
            "1",
            { },
            previous().span
        };
    } else if (
        match(TokenKind::PlusAssign)
        || match(TokenKind::MinusAssign)) {
        (void)require_standard(
            "a compound loop update",
            StandardRevision::SystemVerilog2005,
            previous(),
            "FSIM-SV-PARSE-347");
        update_operation = previous().kind == TokenKind::PlusAssign ? "+" : "-";
        update_operand = parse_expression();
    } else if (match(TokenKind::Assign)) {
        statement.value = parse_expression();
    }
    if (update_operand) {
        statement.value = Expression {
            ExpressionKind::Binary,
            update_operation,
            { statement.loop_update_target, std::move(*update_operand) },
            cover(iteration_variable.span, previous().span)
        };
    }
    if (!statement.value.valid()) {
        error(
            iteration_variable,
            "FSIM-SV-SEM-103",
            "procedural loop update is not an assignment or increment of '"
                + statement.loop_variable + "'");
        statement.loop_runtime = true;
    }
    while (match(TokenKind::Comma)) {
        const auto update_start = current();
        Statement update;
        update.kind = StatementKind::Assignment;
        update.assignment_kind = AssignmentKind::Blocking;
        std::optional<Token> update_prefix;
        if (match(TokenKind::PlusPlus)
            || match(TokenKind::MinusMinus)) {
            update_prefix = previous();
        }
        update.target = parse_lvalue();
        std::optional<std::string> operation;
        bool unit = update_prefix.has_value();
        if (update_prefix) {
            operation = update_prefix->kind == TokenKind::PlusPlus ? "+" : "-";
            update.procedural_update_kind = ProceduralUpdateKind::Prefix;
        } else if (match(TokenKind::PlusPlus)
            || match(TokenKind::MinusMinus)) {
            operation = previous().kind == TokenKind::PlusPlus ? "+" : "-";
            unit = true;
            update.procedural_update_kind = ProceduralUpdateKind::Postfix;
        } else if (match(TokenKind::PlusAssign)
            || match(TokenKind::MinusAssign)) {
            operation = previous().kind == TokenKind::PlusAssign ? "+" : "-";
            update.procedural_update_kind = ProceduralUpdateKind::Compound;
        } else if (match(TokenKind::Assign)) {
            update.value = parse_expression();
        } else {
            error(
                current(),
                "FSIM-SV-SEM-103",
                "procedural loop update is not an assignment or increment");
        }
        if (operation) {
            Expression operand = unit
                ? Expression {
                      ExpressionKind::IntegerLiteral,
                      "1",
                      { },
                      previous().span
                  }
                : parse_expression();
            update.value = Expression {
                ExpressionKind::Binary,
                *operation,
                { update.target, std::move(operand) },
                cover(update.target.span, previous().span)
            };
            update.procedural_update_operator = *operation;
        }
        update.span = cover(update_start.span, previous().span);
        statement.loop_updates.push_back(std::move(update));
        statement.loop_runtime = true;
    }

    const auto update_amount = statement.value.kind == ExpressionKind::Binary
            && statement.value.operands.size() == 2
            && (statement.value.text == "+"
                || statement.value.text == "-")
        ? simple_verilog_integer_constant(statement.value.operands[1])
        : std::nullopt;
    const bool supported_update = update_amount && *update_amount > 0
        && iteration_variable.text == statement.loop_variable
        && (statement.loop_descending
                ? statement.value.text == "-"
                : statement.value.text == "+");
    if (!canonical_condition || !supported_update
        || !inline_variable || *update_amount != 1) {
        statement.loop_runtime = true;
    }
    expect(
        TokenKind::RightParen,
        "')' after procedural loop header",
        "FSIM-SV-PARSE-099");
    parse_procedural_loop_body(start, statement);
    const auto loop_name = current_loop_names_.find(statement.loop_variable);
    if (loop_name != current_loop_names_.end()
        && --loop_name->second == 0) {
        current_loop_names_.erase(loop_name);
    }
    statement.span = span_from(start, previous());
    return statement;
}

Statement VerilogParser::parse_procedural_foreach_statement(
    const Token& start)
{
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_runtime = true;
    statement.loop_variable_declared = true;
    expect(
        TokenKind::LeftParen,
        "'(' after procedural foreach",
        "FSIM-SV-PARSE-330");
    const auto collection = keyword("this") || keyword("super")
        ? advance()
        : expect_identifier("foreach collection");
    Expression collection_expression {
        ExpressionKind::Identifier,
        collection.text,
        { },
        collection.span
    };
    for (;;) {
        if (at(TokenKind::Dot) || at(TokenKind::Scope)) {
            const auto separator = advance();
            const auto member = expect_identifier("selected foreach collection");
            collection_expression.text += separator.kind == TokenKind::Scope ? "::" : ".";
            collection_expression.text += member.text;
            collection_expression.span = cover(collection_expression.span, member.span);
            continue;
        }
        if (!at(TokenKind::LeftBracket)) {
            break;
        }
        std::size_t cursor = 1U;
        std::size_t depth = 1U;
        while (depth != 0U
            && !at(TokenKind::EndOfFile, cursor)) {
            if (at(TokenKind::LeftBracket, cursor)) {
                ++depth;
            } else if (at(TokenKind::RightBracket, cursor)) {
                --depth;
            }
            ++cursor;
        }
        if (depth != 0U
            || (!at(TokenKind::Dot, cursor)
                && !at(TokenKind::Scope, cursor))) {
            break;
        }
        const auto selection_start = advance();
        auto index = parse_expression();
        expect(
            TokenKind::RightBracket,
            "']' after selected foreach collection index",
            "FSIM-SV-PARSE-332");
        collection_expression = Expression {
            ExpressionKind::Index,
            "index",
            { std::move(collection_expression),
                std::move(index) },
            cover(selection_start.span, previous().span)
        };
    }
    statement.target = std::move(collection_expression);
    expect(
        TokenKind::LeftBracket,
        "'[' after foreach collection",
        "FSIM-SV-PARSE-331");
    std::vector<Expression> indices;
    std::vector<std::string> index_names;
    do {
        if (at(TokenKind::Comma) || at(TokenKind::RightBracket)) {
            indices.emplace_back();
            continue;
        }
        const auto variable = expect_identifier("foreach index variable");
        if (statement.loop_variable.empty()) {
            statement.loop_variable = variable.text;
        }
        index_names.push_back(variable.text);
        indices.push_back(Expression {
            ExpressionKind::Identifier,
            variable.text,
            { },
            variable.span });
    } while (match(TokenKind::Comma));
    expect(
        TokenKind::RightBracket,
        "']' after foreach index variable",
        "FSIM-SV-PARSE-332");
    expect(
        TokenKind::RightParen,
        "')' after procedural foreach header",
        "FSIM-SV-PARSE-333");
    indices.insert(indices.begin(), statement.target);
    statement.condition = Expression {
        ExpressionKind::Call,
        "@sv-foreach",
        std::move(indices),
        span_from(start, previous())
    };
    for (const auto& index_name : index_names) {
        ++current_loop_names_[index_name];
    }
    parse_procedural_loop_body(start, statement);
    for (const auto& index_name : index_names) {
        const auto loop_name = current_loop_names_.find(index_name);
        if (loop_name != current_loop_names_.end()
            && --loop_name->second == 0) {
            current_loop_names_.erase(loop_name);
        }
    }
    statement.span = span_from(start, previous());
    return statement;
}

Statement VerilogParser::parse_repeat_statement(const Token& start)
{
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_repeat = true;
    statement.loop_limit_exclusive = true;
    statement.loop_initial = Expression {
        ExpressionKind::IntegerLiteral,
        "0",
        { },
        start.span
    };
    expect(
        TokenKind::LeftParen,
        "'(' after repeat",
        "FSIM-SV-PARSE-100");
    statement.loop_limit = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after repeat count",
        "FSIM-SV-PARSE-101");
    parse_procedural_loop_body(start, statement);
    statement.span = span_from(start, previous());
    return statement;
}

Statement VerilogParser::parse_while_statement(const Token& start)
{
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_runtime = true;
    expect(
        TokenKind::LeftParen,
        "'(' after while",
        "FSIM-SV-PARSE-102");
    statement.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after while condition",
        "FSIM-SV-PARSE-103");
    parse_procedural_loop_body(start, statement);
    statement.span = span_from(start, previous());
    return statement;
}

Statement VerilogParser::parse_do_while_statement(const Token& start)
{
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_runtime = true;
    statement.loop_post_test = true;
    parse_procedural_loop_body(start, statement);
    expect_keyword(
        "while", false, "FSIM-SV-PARSE-105");
    expect(
        TokenKind::LeftParen,
        "'(' after do-while body",
        "FSIM-SV-PARSE-106");
    statement.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after do-while condition",
        "FSIM-SV-PARSE-107");
    expect(
        TokenKind::Semicolon,
        "';' after do-while statement",
        "FSIM-SV-PARSE-108");
    statement.span = span_from(start, previous());
    return statement;
}

void VerilogParser::parse_fatal_arguments(Statement& statement)
{
    statement.output_text = "$fatal";
    statement.assertion_severity = AssertionSeverity::Failure;
    if (!match(TokenKind::LeftParen)) {
        return;
    }
    if (!at(TokenKind::RightParen)) {
        if (at(TokenKind::StringLiteral)) {
            statement.output_text = decoded_string_literal_text(advance());
        } else {
            (void)parse_expression();
            if (match(TokenKind::Comma)) {
                if (at(TokenKind::StringLiteral)) {
                    statement.output_text = decoded_string_literal_text(advance());
                } else {
                    statement.value = parse_expression();
                }
            }
        }
        while (match(TokenKind::Comma)) {
            statement.task_arguments.push_back(parse_expression());
        }
    }
    expect(
        TokenKind::RightParen,
        "')' after $fatal arguments",
        "FSIM-SV-PARSE-115");
}
void VerilogParser::parse_nonfatal_report_arguments(
    Statement& statement,
    const Token& task)
{
    statement.output_text = task.text;
    if (!match(TokenKind::LeftParen)) {
        return;
    }
    if (!at(TokenKind::RightParen)) {
        if (at(TokenKind::StringLiteral)) {
            statement.output_text = decoded_string_literal_text(advance());
        } else {
            statement.value = parse_expression();
        }
        while (match(TokenKind::Comma)) {
            statement.task_arguments.push_back(parse_expression());
        }
    }
    expect(
        TokenKind::RightParen,
        "')' after " + task.text + " arguments",
        "FSIM-SV-PARSE-129");
}

} // namespace fsim::frontend
