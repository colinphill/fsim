// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <functional>

namespace fsim::frontend {

[[nodiscard]] std::optional<StandardRevision>
verilog_system_service_standard(std::string_view name);

[[maybe_unused]] constexpr std::string_view
    kRetiredCaseMatchPatternDiagnostic = "FSIM-SV-UNSUPPORTED-042";
[[maybe_unused]] constexpr std::string_view
    kRetiredCaseMatchGuardDiagnostic = "FSIM-SV-UNSUPPORTED-043";

namespace {

    [[nodiscard]] OutputFormat default_output_format(
        const std::string_view task) noexcept
    {
        if (task.ends_with('b'))
            return OutputFormat::Binary;
        if (task.ends_with('h'))
            return OutputFormat::Hexadecimal;
        if (task.ends_with('o'))
            return OutputFormat::Octal;
        return OutputFormat::Decimal;
    }

} // namespace

Process VerilogParser::parse_initial()
{
    const auto start = expect_keyword("initial", false, "FSIM-SV-PARSE-013");
    current_procedural_names_.clear();
    current_procedural_types_.clear();
    Process process;
    process.kind = ProcessKind::Initial;
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
    process.span = span_from(start, previous());
    current_procedural_names_.clear();
    current_procedural_types_.clear();
    return process;
}

Process VerilogParser::parse_final()
{
    const auto start = keyword("final")
            || (language_ == Language::Verilog2005
                && at(TokenKind::Identifier)
                && current().text == "final")
        ? advance()
        : expect_keyword("final", false, "FSIM-SV-PARSE-111");
    (void)require_standard(
        "a final process",
        StandardRevision::SystemVerilog2005,
        start,
        "FSIM-SV-PARSE-347");
    current_procedural_names_.clear();
    current_procedural_types_.clear();
    Process process;
    process.kind = ProcessKind::Final;
    if (language_ == Language::Verilog2005) {
        error(
            start,
            "FSIM-VERILOG-SEM-006",
            "final procedures require SystemVerilog");
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

    const auto inspect =
        [&](const auto& self,
            const std::vector<Statement>& statements,
            bool& has_suspension,
            bool& has_nonblocking) -> void {
        for (const auto& statement : statements) {
            has_suspension = has_suspension
                || statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn
                || statement.kind == StatementKind::WaitUntil
                || statement.kind == StatementKind::WaitOrder
                || statement.kind == StatementKind::Pause
                || statement.kind == StatementKind::Finish
                || statement.kind == StatementKind::Exit
                || (statement.kind == StatementKind::Assignment
                    && statement.procedural_assignment_control
                        != ProceduralAssignmentControl::None);
            has_nonblocking = has_nonblocking
                || (statement.kind == StatementKind::Assignment
                    && statement.assignment_kind
                        == AssignmentKind::NonBlocking);
            self(
                self, statement.statements,
                has_suspension, has_nonblocking);
            self(
                self, statement.else_statements,
                has_suspension, has_nonblocking);
            for (const auto& alternative :
                statement.case_alternatives) {
                self(
                    self, alternative.statements,
                    has_suspension, has_nonblocking);
            }
        }
    };
    bool has_suspension = false;
    bool has_nonblocking = false;
    inspect(
        inspect, process.statements,
        has_suspension, has_nonblocking);
    if (has_suspension) {
        error(
            start,
            "FSIM-SV-SEM-032",
            "a final procedure cannot contain a timing control, wait, "
            "or a simulation/program termination task");
    }
    if (has_nonblocking) {
        error(
            start,
            "FSIM-SV-SEM-033",
            "a final procedure cannot contain a nonblocking assignment");
    }
    process.span = span_from(start, previous());
    current_procedural_names_.clear();
    current_procedural_types_.clear();
    return process;
}

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
            // Accept and ignore the standard numeric finish control while
            // retaining one bounded constant-string display message.
            (void)parse_expression();
            if (match(TokenKind::Comma)) {
                if (at(TokenKind::StringLiteral)) {
                    statement.output_text = decoded_string_literal_text(advance());
                } else {
                    statement.value = parse_expression();
                }
            }
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
            error(
                previous(),
                "FSIM-SV-SEM-043",
                task.text
                    + " currently accepts at most one literal message");
            (void)parse_expression();
        }
    }
    expect(
        TokenKind::RightParen,
        "')' after " + task.text + " arguments",
        "FSIM-SV-PARSE-129");
}

std::optional<Statement> VerilogParser::parse_statement()
{
    if (const auto required = verilog_system_service_standard(current().text)) {
        (void)require_standard(
            "the system service '" + current().text + "'", *required,
            current(), "FSIM-SV-PARSE-350");
    }
    const auto require_sv2005 = [&](const std::string_view feature,
                                    const Token& token) {
        (void)require_standard(feature, StandardRevision::SystemVerilog2005,
            token, "FSIM-SV-PARSE-347");
    };
    if (match(TokenKind::ThinArrow)) {
        const auto start = previous();
        Statement statement;
        statement.kind = StatementKind::EventTrigger;
        if (match(TokenKind::Greater)) {
            statement.assignment_kind = AssignmentKind::NonBlocking;
            require_sv2005("a nonblocking named-event trigger", previous());
            if (language_ == Language::Verilog2005) {
                error(
                    previous(),
                    "FSIM-VERILOG-SEM-008",
                    "nonblocking named-event trigger '->>' requires "
                    "SystemVerilog");
            }
        }
        if (match(TokenKind::Hash)) {
            const auto delay_start = previous();
            statement.delay = parse_verilog_delay(delay_start);
            if (statement.assignment_kind
                != AssignmentKind::NonBlocking) {
                error(
                    delay_start,
                    "FSIM-SV-SEM-036",
                    "a delayed named-event trigger requires nonblocking "
                    "'->>' syntax");
            }
        }
        const auto implicit_reference_count = implicit_net_references_.size();
        statement.target = parse_lvalue();
        implicit_net_references_.resize(implicit_reference_count);
        expect(
            TokenKind::Semicolon,
            "';' after named-event trigger",
            "FSIM-SV-PARSE-118");
        statement.span = span_from(start, previous());
        return statement;
    }
    const auto is_case_qualifier = [&]() {
        return at(TokenKind::Identifier)
            && (current().text == "unique"
                || current().text == "unique0"
                || current().text == "priority");
    };
    if (is_case_qualifier()) {
        const auto qualifier_start = advance();
        (void)require_standard(
            "case qualifier '" + qualifier_start.text + "'",
            qualifier_start.text == "unique0"
                ? StandardRevision::SystemVerilog2009
                : StandardRevision::SystemVerilog2005,
            qualifier_start,
            "FSIM-SV-PARSE-347");
        CaseQualifier qualifier = CaseQualifier::Unique;
        if (qualifier_start.text == "unique0") {
            qualifier = CaseQualifier::Unique0;
        } else if (qualifier_start.text == "priority") {
            qualifier = CaseQualifier::Priority;
        }
        if (language_ != Language::SystemVerilog2017) {
            error(qualifier_start, "FSIM-SV-PARSE-185",
                "case qualifiers require SystemVerilog");
        }
        while (is_case_qualifier()) {
            error(current(), "FSIM-SV-PARSE-186",
                "a case statement accepts only one qualifier");
            advance();
        }
        if (match_keyword("case")) {
            return parse_case_statement(
                qualifier_start, CaseMatchKind::Exact, qualifier);
        }
        if (match_keyword("casez")) {
            return parse_case_statement(
                qualifier_start, CaseMatchKind::WildcardZ, qualifier);
        }
        if (match_keyword("casex")) {
            return parse_case_statement(
                qualifier_start, CaseMatchKind::WildcardXZ, qualifier);
        }
        error(qualifier_start, "FSIM-SV-UNSUPPORTED-017",
            "bounded unique and priority qualifiers require a case statement");
        return parse_statement();
    }
    if (match_keyword("case")) {
        return parse_case_statement(previous(), CaseMatchKind::Exact);
    }
    if (match_keyword("casez")) {
        return parse_case_statement(previous(), CaseMatchKind::WildcardZ);
    }
    if (match_keyword("casex")) {
        return parse_case_statement(previous(), CaseMatchKind::WildcardXZ);
    }
    if (match_keyword("for")) {
        return parse_procedural_for_statement(previous());
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("foreach")) {
        require_sv2005("a foreach loop", previous());
        return parse_procedural_foreach_statement(previous());
    }
    if (match_keyword("repeat")) {
        return parse_repeat_statement(previous());
    }
    if (match_keyword("while")) {
        return parse_while_statement(previous());
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("do")) {
        require_sv2005("a do-while loop", previous());
        return parse_do_while_statement(previous());
    }
    if (match_keyword("forever")) {
        return parse_forever_statement(previous());
    }
    if (language_ == Language::SystemVerilog2017
        && (keyword("break") || keyword("continue"))) {
        const auto start = advance();
        require_sv2005("a " + start.text + " statement", start);
        const auto is_break = start.text == "break";
        if (current_loop_depth_ == 0) {
            error(
                start,
                "FSIM-SV-SEM-031",
                std::string { "a SystemVerilog " }
                    + (is_break ? "break" : "continue")
                    + " statement must be nested in a procedural loop");
        }
        expect(
            TokenKind::Semicolon,
            "';' after SystemVerilog break or continue statement",
            "FSIM-SV-PARSE-104");
        Statement statement;
        statement.kind = is_break ? StatementKind::Break : StatementKind::Continue;
        statement.span = span_from(start, previous());
        return statement;
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("return")) {
        const auto start = previous();
        require_sv2005("a return statement", start);
        Statement statement;
        statement.kind = StatementKind::Return;
        if (!in_function_ && !in_task_) {
            error(start,
                "FSIM-SV-SEM-056",
                "a return statement must be nested in a function or task");
        }
        if (in_task_ && !at(TokenKind::Semicolon)) {
            error(start, "FSIM-SV-SEM-071",
                "a task return statement cannot return a value");
            statement.value = parse_expression();
        } else if (!at(TokenKind::Semicolon)) {
            statement.value = parse_expression();
        }
        expect(
            TokenKind::Semicolon,
            "';' after return statement",
            "FSIM-SV-PARSE-139");
        statement.span = span_from(start, previous());
        return statement;
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("wait_order")) {
        const auto start = previous();
        require_sv2005("a wait_order statement", start);
        Statement statement;
        statement.kind = StatementKind::WaitOrder;
        expect(
            TokenKind::LeftParen,
            "'(' after wait_order",
            "FSIM-SV-PARSE-364");
        if (at(TokenKind::RightParen)) {
            error(
                current(),
                "FSIM-SV-PARSE-365",
                "wait_order requires at least one named event");
        } else {
            do {
                auto event = parse_expression();
                Sensitivity sensitivity;
                sensitivity.span = event.span;
                if (event.kind == ExpressionKind::Identifier) {
                    sensitivity.signal = std::move(event.text);
                } else {
                    sensitivity.expression = std::move(event);
                    error(
                        current(),
                        "FSIM-SV-SEM-243",
                        "wait_order operands must be named-event identifiers");
                }
                statement.sensitivities.push_back(
                    std::move(sensitivity));
            } while (match(TokenKind::Comma));
        }
        expect(
            TokenKind::RightParen,
            "')' after wait_order event list",
            "FSIM-SV-PARSE-366");
        if (auto success = parse_statement()) {
            statement.statements.push_back(std::move(*success));
        } else {
            error(
                current(),
                "FSIM-SV-PARSE-367",
                "wait_order requires a success action statement");
        }
        if (match_keyword("else")) {
            if (auto failure = parse_statement()) {
                statement.else_statements.push_back(
                    std::move(*failure));
            } else {
                error(
                    current(),
                    "FSIM-SV-PARSE-367",
                    "wait_order else requires an action statement");
            }
        }
        statement.span = span_from(start, previous());
        return statement;
    }
    if (match_keyword("wait")) {
        const auto start = previous();
        Statement statement;
        if (match_keyword("fork")) {
            statement.kind = StatementKind::WaitFork;
            require_sv2005("wait fork", start);
            if (language_ != Language::SystemVerilog2017) {
                error(
                    start, "FSIM-SV-SEM-107",
                    "wait fork requires SystemVerilog");
            }
            expect(
                TokenKind::Semicolon,
                "';' after wait fork",
                "FSIM-SV-PARSE-207");
            statement.span = span_from(start, previous());
            return statement;
        }
        statement.kind = StatementKind::WaitUntil;
        expect(
            TokenKind::LeftParen,
            "'(' after wait",
            "FSIM-SV-PARSE-109");
        statement.condition = parse_expression();
        expect(
            TokenKind::RightParen,
            "')' after wait condition",
            "FSIM-SV-PARSE-110");
        if (auto controlled = parse_statement()) {
            statement.statements.push_back(
                std::move(*controlled));
        }
        statement.span = span_from(start, previous());
        return statement;
    }
    if (match_keyword("fork")) {
        return parse_fork_statement(previous());
    }
    if (match_keyword("disable")) {
        const auto start = previous();
        Statement statement;
        if (match_keyword("fork")) {
            statement.kind = StatementKind::DisableFork;
            require_sv2005("disable fork", start);
            if (language_ != Language::SystemVerilog2017) {
                error(
                    start, "FSIM-SV-SEM-107",
                    "disable fork requires SystemVerilog");
            }
        } else {
            statement.kind = StatementKind::Disable;
            const auto target = expect_identifier("disable target");
            statement.task_name = target.text;
            while (match(TokenKind::Dot)) {
                statement.task_name += ".";
                statement.task_name += expect_identifier("disable target").text;
            }
        }
        expect(
            TokenKind::Semicolon,
            "';' after disable statement",
            "FSIM-SV-PARSE-209");
        statement.span = span_from(start, previous());
        return statement;
    }
    if (keyword("void") && at(TokenKind::Apostrophe, 1)) {
        const auto start = current();
        Statement statement;
        statement.kind = StatementKind::ContainerMethod;
        statement.value = parse_expression();
        expect(
            TokenKind::Semicolon,
            "';' after void cast statement",
            "FSIM-SV-PARSE-329");
        statement.span = span_from(start, previous());
        return statement;
    }
    if ((language_ == Language::SystemVerilog2017
            && match_keyword("assert"))
        || (at(TokenKind::Identifier)
            && current().text == "assert" && (advance(), true))) {
        const auto start = previous();
        (void)require_standard(
            "an immediate assertion",
            StandardRevision::SystemVerilog2005,
            start,
            "FSIM-SV-PARSE-348");
        Statement statement;
        statement.kind = StatementKind::Assert;
        expect(TokenKind::LeftParen, "'(' after assert",
            "FSIM-SV-PARSE-137");
        statement.condition = parse_expression();
        expect(TokenKind::RightParen, "')' after assertion condition",
            "FSIM-SV-PARSE-138");
        if (match_keyword("else")) {
            statement.assertion_has_failure_action = true;
            if (auto action = parse_statement()) {
                statement.else_statements.push_back(
                    std::move(*action));
            } else {
                error(
                    current(),
                    "FSIM-SV-PARSE-044",
                    "expected an immediate-assertion failure action");
            }
        } else {
            statement.assertion_has_pass_action = true;
            if (auto action = parse_statement()) {
                statement.statements.push_back(std::move(*action));
            } else {
                error(
                    current(),
                    "FSIM-SV-PARSE-044",
                    "expected an immediate-assertion pass action");
            }
            if (match_keyword("else")) {
                statement.assertion_has_failure_action = true;
                if (auto action = parse_statement()) {
                    statement.else_statements.push_back(
                        std::move(*action));
                } else {
                    error(
                        current(),
                        "FSIM-SV-PARSE-044",
                        "expected an immediate-assertion failure action");
                }
            }
        }
        statement.span = span_from(start, previous());
        return statement;
    }
    if (keyword("$fatal") || keyword("$error")
        || keyword("$warning") || keyword("$info")) {
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::Report;
        if (language_ == Language::Verilog2005) {
            error(
                start,
                start.text == "$fatal"
                    ? "FSIM-VERILOG-SEM-007"
                    : "FSIM-VERILOG-SEM-009",
                start.text + " requires SystemVerilog");
        }
        if (start.text == "$fatal") {
            parse_fatal_arguments(statement);
        } else {
            if (start.text == "$info") {
                statement.assertion_severity = AssertionSeverity::Note;
            } else if (start.text == "$warning") {
                statement.assertion_severity = AssertionSeverity::Warning;
            } else {
                statement.assertion_severity = AssertionSeverity::Error;
            }
            parse_nonfatal_report_arguments(statement, start);
        }
        expect(
            TokenKind::Semicolon,
            "';' after " + start.text,
            start.text == "$fatal"
                ? "FSIM-SV-PARSE-116"
                : "FSIM-SV-PARSE-130");
        statement.span = span_from(start, previous());
        return statement;
    }
    if (match_keyword("begin")) {
        const auto start = previous();
        std::string opening_label;
        if (match(TokenKind::Colon)) {
            opening_label = expect_identifier("block name").text;
        }
        Statement block;
        block.kind = StatementKind::Block;
        block.label = opening_label;
        DesignUnit local_declarations;
        struct PriorBlockType {
            std::string name;
            std::optional<Type> type;
        };
        std::vector<PriorBlockType> prior_block_types;
        while (!at_end() && !keyword("end")) {
            const auto before = position();
            if (match_keyword("typedef")) {
                const auto alias_count = local_declarations.type_aliases.size();
                parse_typedef(local_declarations, previous());
                if (local_declarations.type_aliases.size() > alias_count) {
                    const auto& alias = local_declarations.type_aliases.back();
                    const auto prior = current_procedural_types_.find(alias.name);
                    prior_block_types.push_back({ alias.name,
                        prior == current_procedural_types_.end()
                            ? std::optional<Type> { }
                            : std::optional<Type> { prior->second } });
                    current_procedural_types_.insert_or_assign(alias.name, alias.type);
                }
            } else if (is_declaration_start()) {
                parse_procedural_declaration(block);
            } else if (auto child = parse_statement()) {
                block.statements.push_back(std::move(*child));
            }
            if (position() == before) {
                advance();
            }
        }
        block.type_aliases = std::move(local_declarations.type_aliases);
        expect_keyword("end", false, "FSIM-SV-PARSE-017");
        if (match(TokenKind::Colon)) {
            const auto closing_label = expect_identifier("block name");
            if (opening_label.empty()) {
                error(
                    closing_label,
                    "FSIM-SV-SEM-034",
                    "an end block label requires a matching opening label");
            } else if (closing_label.text != opening_label) {
                error(
                    closing_label,
                    "FSIM-SV-SEM-034",
                    "end block label '" + closing_label.text
                        + "' does not match opening label '"
                        + opening_label + "'");
            }
        }
        for (auto prior = prior_block_types.rbegin();
            prior != prior_block_types.rend(); ++prior) {
            if (prior->type) {
                current_procedural_types_.insert_or_assign(
                    prior->name, std::move(*prior->type));
            } else {
                current_procedural_types_.erase(prior->name);
            }
        }
        block.span = span_from(start, previous());
        return block;
    }

    if (match_keyword("if")) {
        const auto start = previous();
        Statement statement;
        statement.kind = StatementKind::If;
        expect(TokenKind::LeftParen, "'(' after if", "FSIM-SV-PARSE-018");
        statement.condition = parse_expression();
        expect(TokenKind::RightParen, "')' after if condition",
            "FSIM-SV-PARSE-019");
        if (auto true_branch = parse_statement()) {
            if (true_branch->kind == StatementKind::Block
                && true_branch->label.empty()
                && true_branch->declarations.empty()) {
                statement.statements = std::move(true_branch->statements);
            } else {
                statement.statements.push_back(std::move(*true_branch));
            }
        }
        if (match_keyword("else")) {
            if (auto false_branch = parse_statement()) {
                if (false_branch->kind == StatementKind::Block
                    && false_branch->label.empty()
                    && false_branch->declarations.empty()) {
                    statement.else_statements = std::move(false_branch->statements);
                } else {
                    statement.else_statements.push_back(
                        std::move(*false_branch));
                }
            }
        }
        statement.span = span_from(start, previous());
        return statement;
    }

    if (match(TokenKind::At)) {
        const auto start = previous();
        Statement statement;
        statement.kind = StatementKind::WaitOn;
        statement.sensitivities = parse_sensitivity();
        if (!match(TokenKind::Semicolon)) {
            if (auto controlled = parse_statement()) {
                statement.statements.push_back(std::move(*controlled));
            }
        }
        statement.span = span_from(start, previous());
        return statement;
    }

    if (at(TokenKind::Hash) && at(TokenKind::Hash, 1)) {
        const auto start = advance();
        advance();
        Statement statement;
        statement.kind = StatementKind::WaitOn;
        statement.clocking_cycle_delay = true;
        statement.clocking_cycle_count = parse_expression();
        if (language_ != Language::SystemVerilog2017) {
            error(
                start,
                "FSIM-SV-SEM-184",
                "a ## cycle delay requires SystemVerilog-2017");
        }
        if (!match(TokenKind::Semicolon)) {
            if (auto controlled = parse_statement()) {
                statement.statements.push_back(std::move(*controlled));
            }
        }
        statement.span = span_from(start, previous());
        return statement;
    }

    if (match(TokenKind::Hash)) {
        const auto start = previous();
        Statement statement;
        statement.kind = StatementKind::Delay;
        statement.delay = parse_verilog_delay(start);
        if (match(TokenKind::Semicolon)) {
            statement.span = span_from(start, previous());
            return statement;
        }
        if (auto delayed = parse_statement()) {
            statement.statements.push_back(std::move(*delayed));
        }
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$readmemb") || keyword("$readmemh")
        || keyword("$writememb") || keyword("$writememh")) {
        const bool hexadecimal = keyword("$readmemh") || keyword("$writememh");
        const bool write = keyword("$writememb") || keyword("$writememh");
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::MemoryLoad;
        statement.memory_hex = hexadecimal;
        statement.memory_write = write;
        expect(
            TokenKind::LeftParen,
            "'(' after " + start.text,
            "FSIM-SV-PARSE-159");
        statement.value = parse_expression();
        expect(
            TokenKind::Comma,
            "',' after memory-file name",
            "FSIM-SV-PARSE-160");
        statement.target = parse_expression();
        if (match(TokenKind::Comma)) {
            statement.task_arguments.push_back(parse_expression());
            if (match(TokenKind::Comma)) {
                statement.task_arguments.push_back(parse_expression());
            }
        }
        expect(
            TokenKind::RightParen,
            "')' after read-memory arguments",
            "FSIM-SV-PARSE-161");
        expect(
            TokenKind::Semicolon,
            "';' after read-memory task",
            "FSIM-SV-PARSE-162");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$fflush")) {
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::FileFlush;
        if (match(TokenKind::LeftParen)) {
            if (!at(TokenKind::RightParen)) {
                statement.file_handle = parse_expression();
                while (match(TokenKind::Comma)) {
                    error(
                        previous(),
                        "FSIM-SV-SEM-075",
                        "$fflush accepts at most one file handle");
                    (void)parse_expression();
                }
            }
            expect(
                TokenKind::RightParen,
                "')' after $fflush arguments",
                "FSIM-SV-PARSE-201");
        }
        expect(
            TokenKind::Semicolon,
            "';' after $fflush",
            "FSIM-SV-PARSE-202");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$fclose")) {
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::FileClose;
        expect(
            TokenKind::LeftParen,
            "'(' after $fclose",
            "FSIM-SV-PARSE-148");
        statement.file_handle = parse_expression();
        expect(
            TokenKind::RightParen,
            "')' after $fclose handle",
            "FSIM-SV-PARSE-149");
        expect(
            TokenKind::Semicolon,
            "';' after $fclose",
            "FSIM-SV-PARSE-150");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$fdisplay") || keyword("$fdisplayb")
        || keyword("$fdisplayh") || keyword("$fdisplayo")
        || keyword("$fwrite") || keyword("$fwriteb")
        || keyword("$fwriteh") || keyword("$fwriteo")
        || keyword("$fstrobe") || keyword("$fstrobeb")
        || keyword("$fstrobeh") || keyword("$fstrobeo")
        || keyword("$fmonitor") || keyword("$fmonitorb")
        || keyword("$fmonitorh") || keyword("$fmonitoro")) {
        const bool monitor = current().text.starts_with("$fmonitor");
        const bool postponed = current().text.starts_with("$fstrobe")
            || monitor;
        const bool newline = current().text.starts_with("$fdisplay")
            || postponed;
        const auto start = advance();
        const std::string task_name = start.text;
        const auto default_format = default_output_format(task_name);
        Statement statement;
        statement.kind = StatementKind::FileDisplay;
        statement.output_newline = newline;
        statement.output_postponed = postponed;
        statement.output_monitor = monitor;
        expect(
            TokenKind::LeftParen,
            "'(' after " + task_name,
            "FSIM-SV-PARSE-151");
        statement.file_handle = parse_expression();
        expect(
            TokenKind::Comma,
            "',' after " + task_name + " file handle",
            "FSIM-SV-PARSE-152");
        if (!at(TokenKind::StringLiteral)) {
            do {
                statement.output_values.push_back(
                    OutputValue {
                        parse_expression(), default_format, { } });
            } while (match(TokenKind::Comma));
        } else {
            const auto format_token = advance();
            auto parsed_format = parse_output_format(
                decoded_string_literal_text(format_token));
            std::vector<Expression> values;
            while (match(TokenKind::Comma)) {
                values.push_back(parse_expression());
            }
            const auto consumes_value =
                [](const OutputFormat format) {
                    return format != OutputFormat::Hierarchy;
                };
            const auto required_values = static_cast<std::size_t>(std::ranges::count_if(
                parsed_format.conversions,
                [&consumes_value](const auto& conversion) {
                    return consumes_value(conversion.format);
                }));
            if (!parsed_format.valid) {
                error(
                    format_token,
                    "FSIM-SV-SEM-076",
                    task_name
                        + " format contains an unsupported conversion");
            } else if (values.size() < required_values) {
                error(
                    format_token,
                    "FSIM-SV-SEM-076",
                    task_name
                        + " format conversions require matching value arguments");
            } else if (values.empty()
                && parsed_format.conversions.empty()) {
                statement.output_text = std::move(parsed_format.trailing_text);
            } else if (parsed_format.conversions.size() == 1
                && values.size() == 1
                && consumes_value(
                    parsed_format.conversions.front().format)) {
                auto& conversion = parsed_format.conversions.front();
                statement.output_format = conversion.format;
                statement.output_prefix = std::move(conversion.prefix);
                statement.output_suffix = std::move(parsed_format.trailing_text);
                statement.output_suppress_leading_zero = conversion.suppress_leading_zero;
                statement.output_minimum_width = conversion.minimum_width;
                statement.output_left_justify = conversion.left_justify;
                statement.output_zero_pad = conversion.zero_pad;
                if (!values.empty()) {
                    statement.value = std::move(values.front());
                }
            } else {
                statement.output_values.reserve(
                    parsed_format.conversions.size()
                    + values.size() - required_values);
                std::size_t value_index { };
                for (auto& conversion : parsed_format.conversions) {
                    Expression value;
                    if (consumes_value(conversion.format)) {
                        value = std::move(values[value_index++]);
                    }
                    statement.output_values.push_back(
                        OutputValue {
                            std::move(value),
                            conversion.format,
                            std::move(conversion.prefix),
                            conversion.suppress_leading_zero,
                            conversion.minimum_width,
                            conversion.left_justify,
                            conversion.zero_pad });
                }
                for (std::size_t index = value_index;
                    index < values.size(); ++index) {
                    statement.output_values.push_back(
                        OutputValue {
                            std::move(values[index]),
                            default_format,
                            index == value_index
                                ? std::move(parsed_format.trailing_text)
                                : std::string { } });
                }
                if (value_index == values.size()) {
                    statement.output_trailing_text = std::move(parsed_format.trailing_text);
                }
            }
        }
        expect(
            TokenKind::RightParen,
            "')' after " + task_name + " arguments",
            "FSIM-SV-PARSE-153");
        expect(
            TokenKind::Semicolon,
            "';' after " + task_name,
            "FSIM-SV-PARSE-154");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$monitoron") || keyword("$monitoroff")) {
        const auto start = advance();
        const bool enabled = start.text == "$monitoron";
        if (match(TokenKind::LeftParen)) {
            expect(
                TokenKind::RightParen,
                "')' after " + start.text,
                "FSIM-SV-PARSE-127");
        }
        expect(
            TokenKind::Semicolon,
            "';' after " + start.text,
            "FSIM-SV-PARSE-128");
        Statement statement;
        statement.kind = StatementKind::MonitorControl;
        statement.monitor_enabled = enabled;
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$display") || keyword("$displayb")
        || keyword("$displayh") || keyword("$displayo")
        || keyword("$write") || keyword("$writeb")
        || keyword("$writeh") || keyword("$writeo")
        || keyword("$strobe") || keyword("$strobeb")
        || keyword("$strobeh") || keyword("$strobeo")
        || keyword("$monitor") || keyword("$monitorb")
        || keyword("$monitorh") || keyword("$monitoro")) {
        const auto task_name = std::string_view { current().text };
        const bool monitor = task_name.starts_with("$monitor");
        const bool postponed = task_name.starts_with("$strobe") || monitor;
        const bool newline = !task_name.starts_with("$write");
        const auto default_format = default_output_format(task_name);
        const std::string semantic_code = monitor ? "FSIM-SV-SEM-041"
            : postponed                           ? "FSIM-SV-SEM-039"
            : newline                             ? "FSIM-SV-SEM-037"
                                                  : "FSIM-SV-SEM-038";
        const std::string close_code = monitor ? "FSIM-SV-PARSE-125"
            : postponed                        ? "FSIM-SV-PARSE-123"
            : newline                          ? "FSIM-SV-PARSE-119"
                                               : "FSIM-SV-PARSE-121";
        const std::string semicolon_code = monitor ? "FSIM-SV-PARSE-126"
            : postponed                            ? "FSIM-SV-PARSE-124"
            : newline                              ? "FSIM-SV-PARSE-120"
                                                   : "FSIM-SV-PARSE-122";
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::Display;
        statement.output_newline = newline;
        statement.output_postponed = postponed;
        statement.output_monitor = monitor;
        if (match(TokenKind::LeftParen)) {
            if (!at(TokenKind::RightParen)) {
                if (at(TokenKind::StringLiteral)) {
                    const auto format_token = advance();
                    auto parsed_format = parse_output_format(
                        decoded_string_literal_text(format_token));
                    std::vector<Expression> values;
                    while (match(TokenKind::Comma)) {
                        values.push_back(parse_expression());
                    }
                    const auto consumes_value =
                        [](const OutputFormat format) {
                            return format != OutputFormat::Hierarchy
                                && format != OutputFormat::Time;
                        };
                    const auto required_values = static_cast<std::size_t>(std::ranges::count_if(
                        parsed_format.conversions,
                        [&consumes_value](const auto& conversion) {
                            return consumes_value(conversion.format);
                        }));
                    if (!parsed_format.valid) {
                        error(
                            format_token,
                            "FSIM-SV-SEM-042",
                            "the current formatted-output slice supports "
                            "%b, %h/%x, %o, %d, %c, %s, %e/%f/%g, %m, or %t "
                            "conversion, field width, left/zero padding, "
                            "and %%");
                    } else if (values.size() < required_values) {
                        error(
                            format_token,
                            semantic_code,
                            std::string { task_name }
                                + " format conversions require matching value "
                                  "arguments");
                    } else if (values.empty()
                        && parsed_format.conversions.empty()) {
                        statement.output_text = std::move(parsed_format.trailing_text);
                    } else if (parsed_format.conversions.size() == 1
                        && values.size() == 1
                        && consumes_value(
                            parsed_format.conversions.front().format)) {
                        auto& conversion = parsed_format.conversions.front();
                        statement.output_format = conversion.format;
                        statement.output_prefix = std::move(conversion.prefix);
                        statement.output_suffix = std::move(parsed_format.trailing_text);
                        statement.output_suppress_leading_zero = conversion.suppress_leading_zero;
                        statement.output_minimum_width = conversion.minimum_width;
                        statement.output_left_justify = conversion.left_justify;
                        statement.output_zero_pad = conversion.zero_pad;
                        statement.value = std::move(values.front());
                    } else {
                        statement.output_values.reserve(
                            parsed_format.conversions.size()
                            + values.size() - required_values);
                        std::size_t value_index { };
                        for (std::size_t index = 0;
                            index < parsed_format.conversions.size();
                            ++index) {
                            auto& conversion = parsed_format.conversions[index];
                            Expression value;
                            if (consumes_value(conversion.format)) {
                                value = std::move(values[value_index++]);
                            }
                            statement.output_values.push_back(
                                OutputValue {
                                    std::move(value),
                                    conversion.format,
                                    std::move(conversion.prefix),
                                    conversion.suppress_leading_zero,
                                    conversion.minimum_width,
                                    conversion.left_justify,
                                    conversion.zero_pad });
                        }
                        for (std::size_t index = value_index;
                            index < values.size(); ++index) {
                            statement.output_values.push_back(
                                OutputValue {
                                    std::move(values[index]),
                                    default_format,
                                    index == value_index
                                        ? std::move(parsed_format.trailing_text)
                                        : std::string { } });
                        }
                        if (value_index == values.size()) {
                            statement.output_trailing_text = std::move(parsed_format.trailing_text);
                        }
                    }
                } else {
                    std::vector<Expression> values;
                    do {
                        values.push_back(parse_expression());
                    } while (match(TokenKind::Comma));
                    if (default_format == OutputFormat::Decimal
                        && values.size() == 1
                        && (values.front().kind
                                == ExpressionKind::IntegerLiteral
                            || values.front().kind
                                == ExpressionKind::LogicLiteral)) {
                        const auto value = constant_output_number(values.front().text);
                        if (!value) {
                            error(
                                start,
                                semantic_code,
                                "the current " + std::string { task_name }
                                    + " slice requires a known numeric literal");
                        } else {
                            statement.output_text = *value;
                        }
                    } else {
                        statement.output_values.reserve(values.size());
                        for (auto& value : values) {
                            statement.output_values.push_back(
                                OutputValue {
                                    std::move(value),
                                    default_format,
                                    std::string { } });
                        }
                    }
                }
            }
            expect(
                TokenKind::RightParen,
                "')' after " + std::string { task_name } + " arguments",
                close_code);
        }
        expect(
            TokenKind::Semicolon,
            "';' after " + std::string { task_name },
            semicolon_code);
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("$exit")) {
        const auto start = advance();
        if (match(TokenKind::LeftParen)) {
            if (!at(TokenKind::RightParen)) {
                (void)parse_expression();
                error(
                    start,
                    "FSIM-SV-SEM-075",
                    "$exit does not accept arguments");
            }
            expect(
                TokenKind::RightParen,
                "')' after $exit",
                "FSIM-SV-PARSE-112");
        }
        expect(
            TokenKind::Semicolon,
            "';' after $exit",
            "FSIM-SV-PARSE-113");
        Statement statement;
        statement.kind = StatementKind::Exit;
        statement.span = span_from(start, previous());
        return statement;
    }
    if (keyword("$finish")) {
        const auto start = advance();
        if (match(TokenKind::LeftParen)) {
            if (!at(TokenKind::RightParen)) {
                (void)parse_expression();
            }
            expect(TokenKind::RightParen, "')' after $finish",
                "FSIM-SV-PARSE-020");
        }
        expect(TokenKind::Semicolon, "';' after $finish",
            "FSIM-SV-PARSE-021");
        Statement statement;
        statement.kind = StatementKind::Finish;
        statement.span = span_from(start, previous());
        return statement;
    }
    if (keyword("$stop")) {
        const auto start = advance();
        if (match(TokenKind::LeftParen)) {
            if (!at(TokenKind::RightParen)) {
                (void)parse_expression();
            }
            expect(
                TokenKind::RightParen,
                "')' after $stop",
                "FSIM-SV-PARSE-112");
        }
        expect(
            TokenKind::Semicolon,
            "';' after $stop",
            "FSIM-SV-PARSE-113");
        Statement statement;
        statement.kind = StatementKind::Pause;
        statement.span = span_from(start, previous());
        return statement;
    }

    if (match(TokenKind::Semicolon)) {
        Statement statement;
        statement.kind = StatementKind::Null;
        statement.span = previous().span;
        return statement;
    }

    if (keyword("assign")) {
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::ProceduralAssign;
        statement.target = parse_lvalue();
        expect(
            TokenKind::Assign,
            "'=' in procedural continuous assignment",
            "FSIM-SV-PARSE-342");
        statement.value = parse_expression();
        expect(
            TokenKind::Semicolon,
            "';' after procedural continuous assignment",
            "FSIM-SV-PARSE-343");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("deassign")) {
        const auto start = advance();
        Statement statement;
        statement.kind = StatementKind::Deassign;
        statement.target = parse_lvalue();
        expect(
            TokenKind::Semicolon,
            "';' after procedural deassign",
            "FSIM-SV-PARSE-344");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (keyword("force") || keyword("release")) {
        const auto start = advance();
        const bool force = start.text == "force";
        Statement statement;
        statement.kind = force
            ? StatementKind::Force
            : StatementKind::Release;
        statement.target = parse_lvalue();
        if (force) {
            expect(
                TokenKind::Assign,
                "'=' in procedural force statement",
                "FSIM-SV-PARSE-195");
            statement.value = parse_expression();
        }
        expect(
            TokenKind::Semicolon,
            "';' after procedural force/release",
            "FSIM-SV-PARSE-196");
        statement.span = span_from(start, previous());
        return statement;
    }

    bool container_method_statement = false;
    if (at(TokenKind::Identifier)) {
        std::size_t square_depth = 0;
        for (std::size_t lookahead = 1;
            lookahead < 4096;
            ++lookahead) {
            const auto kind = current(lookahead).kind;
            if (kind == TokenKind::EndOfFile
                || (kind == TokenKind::Semicolon
                    && square_depth == 0)
                || ((kind == TokenKind::Assign
                        || kind == TokenKind::LessEqual
                        || kind == TokenKind::PlusAssign
                        || kind == TokenKind::MinusAssign
                        || kind == TokenKind::StarAssign
                        || kind == TokenKind::SlashAssign
                        || kind == TokenKind::PercentAssign
                        || kind == TokenKind::AmpersandAssign
                        || kind == TokenKind::PipeAssign
                        || kind == TokenKind::CaretAssign
                        || kind == TokenKind::ShiftLeftAssign
                        || kind == TokenKind::ShiftRightAssign
                        || kind == TokenKind::ArithmeticShiftLeftAssign
                        || kind == TokenKind::ArithmeticShiftRightAssign)
                    && square_depth == 0)
                || (kind == TokenKind::LeftParen
                    && square_depth == 0)) {
                break;
            }
            if (kind == TokenKind::LeftBracket) {
                ++square_depth;
            } else if (
                kind == TokenKind::RightBracket
                && square_depth != 0) {
                --square_depth;
            } else if (
                square_depth == 0
                && kind == TokenKind::Dot
                && current(lookahead + 1U).kind
                    == TokenKind::Identifier
                && current(lookahead + 2U).kind
                    == TokenKind::LeftParen
                && contains_word(
                    { "delete", "insert", "push_front", "push_back",
                        "pop_front", "pop_back", "exists",
                        "first", "last", "next", "prev",
                        "sum", "product", "and", "or", "xor",
                        "reverse", "sort", "rsort", "shuffle",
                        "min", "max", "unique", "unique_index",
                        "find", "find_index", "find_first",
                        "find_first_index", "find_last",
                        "find_last_index", "putc", "itoa",
                        "hextoa", "octtoa", "bintoa", "realtoa" },
                    current(lookahead + 1U).text)) {
                container_method_statement = true;
                break;
            }
        }
    }
    if (container_method_statement) {
        const auto start = current();
        Statement statement;
        statement.kind = StatementKind::ContainerMethod;
        statement.value = parse_expression();
        expect(
            TokenKind::Semicolon,
            "';' after container method call",
            "FSIM-SV-PARSE-156");
        statement.span = span_from(start, previous());
        return statement;
    }

    if (at(TokenKind::Identifier)) {
        std::size_t lookahead = 1;
        if (at(TokenKind::Hash, lookahead)
            && at(TokenKind::LeftParen, lookahead + 1U)) {
            ++lookahead;
            std::size_t depth { };
            do {
                if (at(TokenKind::LeftParen, lookahead)) {
                    ++depth;
                } else if (at(TokenKind::RightParen, lookahead)) {
                    --depth;
                }
                ++lookahead;
            } while (depth != 0U
                && !at(TokenKind::EndOfFile, lookahead));
        }
        while ((at(TokenKind::Scope, lookahead)
                   || at(TokenKind::Dot, lookahead))
            && at(TokenKind::Identifier, lookahead + 1)) {
            lookahead += 2;
        }
        if (at(TokenKind::LeftParen, lookahead) || at(TokenKind::Semicolon, lookahead)) {
            const auto start = advance();
            std::string name = start.text;
            if (match(TokenKind::Hash)) {
                Instance actual_owner;
                parse_parameter_overrides(actual_owner, previous());
                name += "#(";
                for (std::size_t index = 0;
                    index < actual_owner.parameter_overrides.size(); ++index) {
                    if (index != 0U)
                        name += ',';
                    const auto& actual = actual_owner.parameter_overrides[index];
                    if (actual.name)
                        name += "." + *actual.name + "(";
                    if (actual.type_value) {
                        name += actual.type_value->named_type.empty()
                            ? actual.type_value->spelling
                            : actual.type_value->named_type;
                    } else {
                        name += actual.value.text;
                    }
                    if (actual.name)
                        name += ')';
                }
                name += ')';
            }
            while (at(TokenKind::Scope) || at(TokenKind::Dot)) {
                const auto separator = advance();
                name += separator.kind == TokenKind::Scope ? "::" : ".";
                name += keyword("new")
                    ? advance().text
                    : expect_identifier("selected task name").text;
            }
            Statement statement;
            statement.kind = StatementKind::TaskCall;
            if (name == "$assertcontrol") {
                statement.assertion_control = SystemVerilogAssertionControlKind::Control;
            } else if (name == "$asserton") {
                statement.assertion_control = SystemVerilogAssertionControlKind::On;
            } else if (name == "$assertoff") {
                statement.assertion_control = SystemVerilogAssertionControlKind::Off;
            } else if (name == "$assertkill") {
                statement.assertion_control = SystemVerilogAssertionControlKind::Kill;
            } else if (name == "$assertpasson") {
                statement.assertion_control = SystemVerilogAssertionControlKind::PassOn;
            } else if (name == "$assertpassoff") {
                statement.assertion_control = SystemVerilogAssertionControlKind::PassOff;
            } else if (name == "$assertfailon") {
                statement.assertion_control = SystemVerilogAssertionControlKind::FailOn;
            } else if (name == "$assertfailoff") {
                statement.assertion_control = SystemVerilogAssertionControlKind::FailOff;
            } else if (name == "$assertnonvacuouson") {
                statement.assertion_control = SystemVerilogAssertionControlKind::NonvacuousOn;
            } else if (name == "$assertvacuousoff") {
                statement.assertion_control = SystemVerilogAssertionControlKind::VacuousOff;
            }
            statement.task_name = std::move(name);
            if (match(TokenKind::LeftParen)) {
                if (!at(TokenKind::RightParen)) {
                    do {
                        if (at(TokenKind::Comma)
                            || at(TokenKind::RightParen)) {
                            statement.task_argument_names.emplace_back();
                            statement.task_arguments.emplace_back();
                        } else if (match(TokenKind::Dot)) {
                            const auto formal = expect_identifier("named task argument");
                            expect(
                                TokenKind::LeftParen,
                                "'(' after named task argument",
                                "FSIM-SV-PARSE-201");
                            statement.task_argument_names.push_back(formal.text);
                            if (at(TokenKind::RightParen)) {
                                statement.task_arguments.emplace_back();
                            } else {
                                statement.task_arguments.push_back(parse_expression());
                            }
                            expect(
                                TokenKind::RightParen,
                                "')' after named task argument",
                                "FSIM-SV-PARSE-202");
                        } else {
                            statement.task_argument_names.emplace_back();
                            statement.task_arguments.push_back(parse_expression());
                        }
                    } while (match(TokenKind::Comma));
                }
                expect(TokenKind::RightParen, "')' after task call arguments",
                    "FSIM-SV-PARSE-146");
            }
            expect(TokenKind::Semicolon, "';' after task call", "FSIM-SV-PARSE-147");
            statement.span = span_from(start, previous());
            return statement;
        }
    }

    const auto potential_complex_call = [&]() {
        for (std::size_t lookahead = 1U;
            !at(TokenKind::Semicolon, lookahead)
            && !at(TokenKind::EndOfFile, lookahead);
            ++lookahead) {
            if (at(TokenKind::Assign, lookahead)
                || at(TokenKind::LessEqual, lookahead)
                || at(TokenKind::PlusAssign, lookahead)
                || at(TokenKind::MinusAssign, lookahead)
                || at(TokenKind::StarAssign, lookahead)
                || at(TokenKind::SlashAssign, lookahead)
                || at(TokenKind::PercentAssign, lookahead)
                || at(TokenKind::AmpersandAssign, lookahead)
                || at(TokenKind::PipeAssign, lookahead)
                || at(TokenKind::CaretAssign, lookahead)
                || at(TokenKind::ShiftLeftAssign, lookahead)
                || at(TokenKind::ShiftRightAssign, lookahead)
                || at(TokenKind::ArithmeticShiftLeftAssign, lookahead)
                || at(TokenKind::ArithmeticShiftRightAssign, lookahead)) {
                return false;
            }
            if (at(TokenKind::LeftParen, lookahead)) {
                return true;
            }
        }
        return false;
    }();
    if ((at(TokenKind::Identifier)
            || keyword("this") || keyword("super"))
        && potential_complex_call) {
        const auto before = position();
        const auto start = current();
        auto call = parse_expression();
        if (call.kind == ExpressionKind::Call
            && match(TokenKind::Semicolon)) {
            Statement statement;
            statement.kind = StatementKind::ContainerMethod;
            statement.value = std::move(call);
            statement.span = span_from(start, previous());
            return statement;
        }
        rewind(before);
    }

    if (at(TokenKind::Identifier)
        || at(TokenKind::PlusPlus)
        || at(TokenKind::MinusMinus)
        || at(TokenKind::LeftBrace)) {
        const auto before = position();
        const auto start = current();
        std::optional<Token> prefix_update;
        if (match(TokenKind::PlusPlus)
            || match(TokenKind::MinusMinus)) {
            prefix_update = previous();
            require_sv2005(
                "an increment or decrement statement", *prefix_update);
        }
        Expression target = at(TokenKind::LeftBrace)
            ? parse_expression()
            : parse_lvalue();
        AssignmentKind assignment_kind { AssignmentKind::Blocking };
        std::optional<std::string> update_operation;
        bool unit_update = prefix_update.has_value();
        ProceduralUpdateKind update_kind {
            ProceduralUpdateKind::None
        };
        if (prefix_update) {
            update_operation = prefix_update->kind == TokenKind::PlusPlus ? "+" : "-";
            update_kind = ProceduralUpdateKind::Prefix;
        } else if (match(TokenKind::PlusPlus)
            || match(TokenKind::MinusMinus)) {
            require_sv2005(
                "an increment or decrement statement", previous());
            update_operation = previous().kind == TokenKind::PlusPlus ? "+" : "-";
            unit_update = true;
            update_kind = ProceduralUpdateKind::Postfix;
        } else if (match(TokenKind::LessEqual)) {
            assignment_kind = AssignmentKind::NonBlocking;
        } else if (match(TokenKind::Assign)) {
            assignment_kind = AssignmentKind::Blocking;
        } else {
            const auto compound_operation =
                [](const TokenKind kind)
                -> std::optional<std::string_view> {
                switch (kind) {
                case TokenKind::PlusAssign:
                    return "+";
                case TokenKind::MinusAssign:
                    return "-";
                case TokenKind::StarAssign:
                    return "*";
                case TokenKind::SlashAssign:
                    return "/";
                case TokenKind::PercentAssign:
                    return "%";
                case TokenKind::AmpersandAssign:
                    return "&";
                case TokenKind::PipeAssign:
                    return "|";
                case TokenKind::CaretAssign:
                    return "^";
                case TokenKind::ShiftLeftAssign:
                    return "<<";
                case TokenKind::ShiftRightAssign:
                    return ">>";
                case TokenKind::ArithmeticShiftLeftAssign:
                    return "<<<";
                case TokenKind::ArithmeticShiftRightAssign:
                    return ">>>";
                default:
                    return std::nullopt;
                }
            }(current().kind);
            if (compound_operation) {
                require_sv2005("a compound assignment", current());
                update_operation = *compound_operation;
                update_kind = ProceduralUpdateKind::Compound;
                advance();
            } else {
                rewind(before);
                const auto unsupported = advance();
                error(unsupported, "FSIM-SV-UNSUPPORTED-008",
                    "unsupported procedural statement starting with '" + unsupported.text + "'");
                skip_to_semicolon();
                return std::nullopt;
            }
        }
        if (update_operation
            && language_ == Language::Verilog2005) {
            error(
                start,
                "FSIM-VERILOG-SEM-005",
                "compound assignments and standalone increment/decrement "
                "require SystemVerilog");
        }

        std::optional<Delay> delay;
        std::vector<Sensitivity> assignment_sensitivities;
        Expression assignment_repeat_count;
        bool assignment_repeat = false;
        ProceduralAssignmentControl assignment_control {
            ProceduralAssignmentControl::None
        };
        if (!unit_update) {
            if (match_keyword("repeat")) {
                assignment_repeat = true;
                expect(
                    TokenKind::LeftParen,
                    "'(' after repeated assignment control",
                    "FSIM-SV-PARSE-203");
                assignment_repeat_count = parse_expression();
                expect(
                    TokenKind::RightParen,
                    "')' after repeated assignment count",
                    "FSIM-SV-PARSE-204");
                expect(
                    TokenKind::At,
                    "'@' after repeated assignment count",
                    "FSIM-SV-PARSE-205");
                assignment_control = ProceduralAssignmentControl::Event;
                assignment_sensitivities = parse_sensitivity();
            } else if (match(TokenKind::Hash)) {
                assignment_control = ProceduralAssignmentControl::Delay;
                delay = parse_verilog_delay(previous());
            } else if (match(TokenKind::At)) {
                assignment_control = ProceduralAssignmentControl::Event;
                assignment_sensitivities = parse_sensitivity();
            }
            while (at(TokenKind::Hash) || at(TokenKind::At)) {
                const auto duplicate = advance();
                error(
                    duplicate,
                    "FSIM-SV-SEM-054",
                    "a procedural assignment accepts only one delay or event "
                    "control");
                if (duplicate.kind == TokenKind::Hash) {
                    (void)parse_verilog_delay(duplicate);
                } else {
                    (void)parse_sensitivity();
                }
            }
        }
        Expression value;
        bool recovered_through_semicolon = false;
        if (unit_update) {
            value = Expression {
                ExpressionKind::Binary,
                *update_operation,
                { target,
                    Expression {
                        ExpressionKind::IntegerLiteral,
                        "1",
                        { },
                        previous().span } },
                cover(target.span, previous().span)
            };
        } else {
            value = parse_expression();
            if (update_operation) {
                const auto value_span = value.span;
                value = Expression {
                    ExpressionKind::Binary,
                    *update_operation,
                    { target, std::move(value) },
                    cover(target.span, value_span)
                };
            }
        }
        if (!recovered_through_semicolon) {
            expect(TokenKind::Semicolon, "';' after assignment",
                "FSIM-SV-PARSE-022");
        }
        Statement statement;
        statement.kind = StatementKind::Assignment;
        statement.assignment_kind = assignment_kind;
        statement.target = std::move(target);
        statement.value = std::move(value);
        statement.delay = std::move(delay);
        statement.sensitivities = std::move(assignment_sensitivities);
        statement.procedural_assignment_control = assignment_control;
        statement.procedural_assignment_repeat = assignment_repeat;
        statement.loop_limit = std::move(assignment_repeat_count);
        statement.procedural_update_kind = update_kind;
        statement.procedural_update_operator = update_operation.value_or(std::string { });
        statement.span = span_from(start, previous());
        return statement;
    }

    const auto unexpected = advance();
    error(unexpected, "FSIM-SV-UNSUPPORTED-009",
        "unsupported statement starting with '" + unexpected.text + "'");
    skip_to_semicolon();
    return std::nullopt;
}

std::optional<VerilogParser::DecimalRatio> VerilogParser::decimal_ratio(
    const std::string_view spelling)
{
    std::string compact;
    compact.reserve(spelling.size());
    for (const char character : spelling) {
        if (character != '_') {
            compact.push_back(character);
        }
    }
    const auto exponent_position = compact.find_first_of("eE");
    if (exponent_position != std::string::npos
        && compact.find_first_of("eE", exponent_position + 1)
            != std::string::npos) {
        return std::nullopt;
    }
    const auto mantissa = std::string_view { compact }.substr(0, exponent_position);
    std::int64_t exponent { };
    if (exponent_position != std::string::npos) {
        auto exponent_text = std::string_view { compact }.substr(exponent_position + 1);
        bool negative = false;
        if (!exponent_text.empty()
            && (exponent_text.front() == '+'
                || exponent_text.front() == '-')) {
            negative = exponent_text.front() == '-';
            exponent_text.remove_prefix(1);
        }
        const auto parsed_exponent = decimal_i64(exponent_text, negative);
        if (!parsed_exponent) {
            return std::nullopt;
        }
        exponent = *parsed_exponent;
    }

    std::string digits;
    digits.reserve(mantissa.size());
    bool saw_decimal = false;
    std::size_t fractional_digits { };
    for (const char character : mantissa) {
        if (character == '.') {
            if (saw_decimal) {
                return std::nullopt;
            }
            saw_decimal = true;
            continue;
        }
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        digits.push_back(character);
        if (saw_decimal) {
            ++fractional_digits;
        }
    }
    if (digits.empty()) {
        return std::nullopt;
    }
    auto numerator = decimal_u64(digits);
    if (!numerator) {
        return std::nullopt;
    }
    if (*numerator == 0) {
        return DecimalRatio { };
    }
    if (fractional_digits
        > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    auto scale = static_cast<std::int64_t>(fractional_digits);
    if ((exponent > 0
            && scale < std::numeric_limits<std::int64_t>::min() + exponent)
        || (exponent < 0
            && scale > std::numeric_limits<std::int64_t>::max() + exponent)) {
        return std::nullopt;
    }
    scale -= exponent;
    while (scale > 0 && *numerator % 10 == 0) {
        *numerator /= 10;
        --scale;
    }
    std::uint64_t denominator = 1;
    while (scale > 0) {
        if (denominator
            > std::numeric_limits<std::uint64_t>::max() / 10) {
            return std::nullopt;
        }
        denominator *= 10;
        --scale;
    }
    while (scale < 0) {
        if (*numerator
            > std::numeric_limits<std::uint64_t>::max() / 10) {
            return std::nullopt;
        }
        *numerator *= 10;
        ++scale;
    }
    const auto divisor = std::gcd(*numerator, denominator);
    return DecimalRatio {
        *numerator / divisor,
        denominator / divisor
    };
}

} // namespace fsim::frontend
