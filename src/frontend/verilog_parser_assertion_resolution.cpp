// SPDX-License-Identifier: Apache-2.0

#include "verilog_parser_internal.hpp"
#include "verilog_parser_assertion_support.hpp"

#include <algorithm>
#include <map>
#include <unordered_set>

namespace fsim::frontend {

void VerilogParser::resolve_assertion_references(DesignUnit& unit)
{
    const auto design_names = assertion_design_unit_names(unit);
    resolve_assertion_declaration_references(unit);
    using namespace assertion_resolution_detail;

    for (std::size_t index = 0;
        index < unit.systemverilog_concurrent_assertions.size();
        ++index) {
        const auto& directive = unit.systemverilog_concurrent_assertions[index];
        if (directive.property_tokens.empty()) {
            continue;
        }
        std::span<const Token> predicate_tokens {
            directive.property_tokens
        };
        const SystemVerilogAssertionDeclaration* property { };
        std::vector<Token> substituted_predicate_tokens;
        std::vector<Token> substituted_clock_tokens;
        std::vector<Token> substituted_disable_tokens;
        std::vector<Token> substituted_abort_condition_tokens;
        std::vector<Token> substituted_abort_property_tokens;
        struct ExecutableAssertionLocal {
            const SystemVerilogAssertionLocalVariable* declaration { };
            std::vector<Token> initializer_tokens;
        };
        std::vector<ExecutableAssertionLocal> executable_locals;
        if (directive.form
                == SystemVerilogConcurrentAssertionForm::Property
            && !predicate_tokens.empty()
            && predicate_tokens.front().kind == TokenKind::Identifier) {
            const auto found = std::ranges::find_if(
                unit.systemverilog_assertion_declarations,
                [&](const SystemVerilogAssertionDeclaration& candidate) {
                    return candidate.kind
                        == SystemVerilogAssertionDeclarationKind::Property
                        && candidate.name == predicate_tokens.front().text;
                });
            if (found != unit.systemverilog_assertion_declarations.end()) {
                property = &*found;
                std::vector<std::vector<Token>> actuals;
                const bool empty_call = predicate_tokens.size() == 3U
                    && predicate_tokens[1].kind == TokenKind::LeftParen
                    && predicate_tokens[2].kind == TokenKind::RightParen;
                if (predicate_tokens.size() != 1U && !empty_call) {
                    if (predicate_tokens.size() < 4U
                        || predicate_tokens[1].kind != TokenKind::LeftParen
                        || predicate_tokens.back().kind != TokenKind::RightParen) {
                        error(
                            predicate_tokens.front(), "FSIM-SV-SEM-199",
                            "an executable property invocation must use one balanced "
                            "actual-argument list");
                        continue;
                    }
                    const auto arguments = predicate_tokens.subspan(
                        2U, predicate_tokens.size() - 3U);
                    std::size_t begin { };
                    int parentheses { };
                    int brackets { };
                    int braces { };
                    for (std::size_t position = 0U;
                        position <= arguments.size(); ++position) {
                        const bool separator = position == arguments.size()
                            || (arguments[position].kind == TokenKind::Comma
                                && parentheses == 0 && brackets == 0 && braces == 0);
                        if (separator) {
                            actuals.emplace_back(
                                arguments.begin() + static_cast<std::ptrdiff_t>(begin),
                                arguments.begin() + static_cast<std::ptrdiff_t>(position));
                            begin = position + 1U;
                            continue;
                        }
                        if (arguments[position].kind == TokenKind::LeftParen)
                            ++parentheses;
                        else if (arguments[position].kind == TokenKind::RightParen)
                            --parentheses;
                        else if (arguments[position].kind == TokenKind::LeftBracket)
                            ++brackets;
                        else if (arguments[position].kind == TokenKind::RightBracket)
                            --brackets;
                        else if (arguments[position].kind == TokenKind::LeftBrace)
                            ++braces;
                        else if (arguments[position].kind == TokenKind::RightBrace)
                            --braces;
                    }
                    if (parentheses != 0 || brackets != 0 || braces != 0) {
                        error(
                            predicate_tokens.front(), "FSIM-SV-SEM-199",
                            "an executable property actual-argument list is unbalanced");
                        continue;
                    }
                }
                std::map<std::string, std::vector<Token>, std::less<>> bindings;
                std::size_t positional { };
                bool malformed_actuals { };
                for (auto& actual : actuals) {
                    std::string name;
                    std::vector<Token> value;
                    if (actual.size() >= 5U
                        && actual[0].kind == TokenKind::Dot
                        && actual[1].kind == TokenKind::Identifier
                        && actual[2].kind == TokenKind::LeftParen
                        && actual.back().kind == TokenKind::RightParen) {
                        name = actual[1].text;
                        value.assign(actual.begin() + 3, actual.end() - 1);
                    } else if (positional < property->formals.size()) {
                        name = property->formals[positional++].name;
                        value = std::move(actual);
                    } else {
                        malformed_actuals = true;
                        break;
                    }
                    if (value.empty() || !bindings.emplace(name, std::move(value)).second)
                        malformed_actuals = true;
                }
                for (const auto& formal : property->formals) {
                    if (!bindings.contains(formal.name)) {
                        if (formal.default_tokens.empty()) {
                            malformed_actuals = true;
                        } else {
                            bindings.emplace(formal.name, formal.default_tokens);
                        }
                    }
                }
                if (bindings.size() != property->formals.size())
                    malformed_actuals = true;
                if (malformed_actuals) {
                    error(
                        predicate_tokens.front(), "FSIM-SV-SEM-199",
                        "property actuals do not match the executable formal profile");
                    continue;
                }
                const auto substitute = [&](const std::span<const Token> tokens) {
                    std::vector<Token> result;
                    for (const auto& token : tokens) {
                        const auto binding = token.kind == TokenKind::Identifier
                            ? bindings.find(token.text)
                            : bindings.end();
                        if (binding == bindings.end()) {
                            result.push_back(token);
                        } else {
                            result.insert(
                                result.end(), binding->second.begin(), binding->second.end());
                        }
                    }
                    return result;
                };
                substituted_predicate_tokens = substitute(property->expression_tokens);
                predicate_tokens = substituted_predicate_tokens;
                if (property->clock)
                    substituted_clock_tokens = substitute(property->clock->event_tokens);
                if (property->disable)
                    substituted_disable_tokens = substitute(
                        property->disable->condition_tokens);
                if (property->property_expression
                    && property->property_expression->aborts.size() == 1U) {
                    const auto& abort
                        = property->property_expression->aborts.front();
                    substituted_abort_condition_tokens = substitute(
                        abort.condition_tokens);
                    substituted_abort_property_tokens = substitute(
                        abort.property_tokens);
                }
                for (const auto& local : property->local_variables) {
                    executable_locals.push_back(
                        { &local, substitute(local.initializer_tokens) });
                }
            }
        }
        while (!predicate_tokens.empty()
            && predicate_tokens.back().kind == TokenKind::Semicolon) {
            predicate_tokens = predicate_tokens.first(
                predicate_tokens.size() - 1U);
        }
        if (directive.form == SystemVerilogConcurrentAssertionForm::Sequence
            && directive.inline_sequence) {
            predicate_tokens = directive.inline_sequence->expression_tokens;
            while (!predicate_tokens.empty()
                && predicate_tokens.back().kind == TokenKind::Semicolon) {
                predicate_tokens = predicate_tokens.first(
                    predicate_tokens.size() - 1U);
            }
        }
        if (predicate_tokens.size() == 1U
            && predicate_tokens.front().kind == TokenKind::Identifier) {
            const auto variable = std::ranges::find_if(
                unit.variables, [&](const VariableDeclaration& candidate) {
                    return candidate.name == predicate_tokens.front().text;
                });
            if (variable != unit.variables.end()
                && (variable->type.domain == ValueDomain::String
                    || variable->type.systemverilog_container
                    || !variable->type.systemverilog_class_name.empty()
                    || variable->type.systemverilog_virtual_interface)) {
                error(
                    predicate_tokens.front(),
                    "FSIM-SV-SEM-200",
                    "an executable concurrent-property predicate must have a "
                    "packed scalar or vector type");
                continue;
            }
        }
        const SystemVerilogAssertionDeclaration* sequence
            = directive.form == SystemVerilogConcurrentAssertionForm::Sequence
                && directive.inline_sequence
            ? &*directive.inline_sequence
            : nullptr;
        std::vector<Token> substituted_sequence_tokens;
        std::vector<Token> substituted_sequence_clock_tokens;
        std::vector<Token> substituted_sequence_disable_tokens;
        if ((property
                || directive.form
                    == SystemVerilogConcurrentAssertionForm::Sequence)
            && !predicate_tokens.empty()
            && predicate_tokens.front().kind == TokenKind::Identifier) {
            const auto found = std::ranges::find_if(
                unit.systemverilog_assertion_declarations,
                [&](const SystemVerilogAssertionDeclaration& candidate) {
                    return candidate.kind
                        == SystemVerilogAssertionDeclarationKind::Sequence
                        && candidate.name == predicate_tokens.front().text;
                });
            if (found != unit.systemverilog_assertion_declarations.end()) {
                sequence = &*found;
                std::vector<std::vector<Token>> actuals;
                const bool empty_call = predicate_tokens.size() == 3U
                    && predicate_tokens[1].kind == TokenKind::LeftParen
                    && predicate_tokens[2].kind == TokenKind::RightParen;
                bool malformed = predicate_tokens.size() != 1U && !empty_call
                    && (predicate_tokens.size() < 4U
                        || predicate_tokens[1].kind != TokenKind::LeftParen
                        || predicate_tokens.back().kind != TokenKind::RightParen);
                if (predicate_tokens.size() != 1U && !empty_call && !malformed) {
                    const auto arguments = predicate_tokens.subspan(
                        2U, predicate_tokens.size() - 3U);
                    std::size_t begin { };
                    int parentheses { };
                    int brackets { };
                    int braces { };
                    for (std::size_t position = 0U;
                        position <= arguments.size(); ++position) {
                        const bool separator = position == arguments.size()
                            || (arguments[position].kind == TokenKind::Comma
                                && parentheses == 0 && brackets == 0 && braces == 0);
                        if (separator) {
                            actuals.emplace_back(
                                arguments.begin()
                                    + static_cast<std::ptrdiff_t>(begin),
                                arguments.begin()
                                    + static_cast<std::ptrdiff_t>(position));
                            begin = position + 1U;
                            continue;
                        }
                        if (arguments[position].kind == TokenKind::LeftParen)
                            ++parentheses;
                        else if (arguments[position].kind == TokenKind::RightParen)
                            --parentheses;
                        else if (arguments[position].kind == TokenKind::LeftBracket)
                            ++brackets;
                        else if (arguments[position].kind == TokenKind::RightBracket)
                            --brackets;
                        else if (arguments[position].kind == TokenKind::LeftBrace)
                            ++braces;
                        else if (arguments[position].kind == TokenKind::RightBrace)
                            --braces;
                    }
                    malformed = parentheses != 0 || brackets != 0 || braces != 0;
                }
                std::map<std::string, std::vector<Token>, std::less<>> bindings;
                std::size_t positional { };
                for (auto& actual : actuals) {
                    std::string name;
                    std::vector<Token> value;
                    if (actual.size() >= 5U
                        && actual[0].kind == TokenKind::Dot
                        && actual[1].kind == TokenKind::Identifier
                        && actual[2].kind == TokenKind::LeftParen
                        && actual.back().kind == TokenKind::RightParen) {
                        name = actual[1].text;
                        value.assign(actual.begin() + 3, actual.end() - 1);
                    } else if (positional < sequence->formals.size()) {
                        name = sequence->formals[positional++].name;
                        value = std::move(actual);
                    } else {
                        malformed = true;
                    }
                    if (value.empty()
                        || !bindings.emplace(name, std::move(value)).second)
                        malformed = true;
                }
                for (const auto& formal : sequence->formals) {
                    if (formal.kind != SystemVerilogAssertionFormalKind::Value
                        && formal.kind
                            != SystemVerilogAssertionFormalKind::Untyped) {
                        malformed = true;
                    }
                    if (!bindings.contains(formal.name)) {
                        if (formal.default_tokens.empty()) {
                            malformed = true;
                        } else {
                            bindings.emplace(formal.name, formal.default_tokens);
                        }
                    }
                }
                if (bindings.size() != sequence->formals.size())
                    malformed = true;
                if (malformed) {
                    error(
                        predicate_tokens.front(),
                        "FSIM-SV-SEM-199",
                        "sequence actuals do not match the executable formal "
                        "profile");
                    continue;
                }
                const auto substitute_sequence
                    = [&](const std::span<const Token> tokens) {
                          std::vector<Token> result;
                          for (const auto& token : tokens) {
                              const auto binding
                                  = token.kind == TokenKind::Identifier
                                  ? bindings.find(token.text)
                                  : bindings.end();
                              if (binding == bindings.end()) {
                                  result.push_back(token);
                              } else {
                                  result.insert(
                                      result.end(),
                                      binding->second.begin(),
                                      binding->second.end());
                              }
                          }
                          return result;
                      };
                if (!bindings.empty()) {
                    substituted_sequence_tokens
                        = substitute_sequence(sequence->expression_tokens);
                    while (!substituted_sequence_tokens.empty()
                        && substituted_sequence_tokens.back().kind
                            == TokenKind::Semicolon) {
                        substituted_sequence_tokens.pop_back();
                    }
                }
                if (sequence->clock) {
                    substituted_sequence_clock_tokens = substitute_sequence(
                        sequence->clock->event_tokens);
                }
                if (sequence->disable) {
                    substituted_sequence_disable_tokens = substitute_sequence(
                        sequence->disable->condition_tokens);
                }
                for (const auto& local : sequence->local_variables) {
                    executable_locals.push_back(
                        { &local, substitute_sequence(local.initializer_tokens) });
                }
            }
        }
        const auto* directive_sequence = directive.form
                == SystemVerilogConcurrentAssertionForm::Sequence
            && directive.inline_sequence
            ? &*directive.inline_sequence
            : nullptr;
        const auto* assertion_clock = property && property->clock
            ? &*property->clock
            : directive_sequence && directive_sequence->clock
            ? &*directive_sequence->clock
            : sequence && sequence->clock
            ? &*sequence->clock
            : nullptr;
        std::span<const Token> assertion_clock_tokens;
        if (property && property->clock) {
            assertion_clock_tokens = substituted_clock_tokens.empty()
                ? std::span<const Token> { property->clock->event_tokens }
                : std::span<const Token> { substituted_clock_tokens };
        } else if (directive_sequence && directive_sequence->clock) {
            assertion_clock_tokens = directive_sequence->clock->event_tokens;
        } else if (sequence && sequence->clock) {
            assertion_clock_tokens = substituted_sequence_clock_tokens.empty()
                ? std::span<const Token> { sequence->clock->event_tokens }
                : std::span<const Token> {
                      substituted_sequence_clock_tokens };
        }
        if (assertion_clock) {
            std::vector<std::string_view> clock_objects;
            for (const auto& token : assertion_clock_tokens) {
                if (token.kind == TokenKind::Identifier
                    && token.text != "posedge"
                    && token.text != "negedge"
                    && token.text != "edge"
                    && token.text != "or"
                    && token.text != "iff") {
                    clock_objects.push_back(token.text);
                }
            }
            if (clock_objects.size() != 1U
                || !design_names.contains(
                    std::string { clock_objects.front() })) {
                error(
                    assertion_clock_tokens.front(),
                    "FSIM-SV-SEM-201",
                    "the executable concurrent-assertion slice requires one "
                    "direct design-unit clock object with an optional edge");
                continue;
            }
        }
        std::optional<Expression> sequence_antecedent;
        std::optional<Expression> sequence_cycle_count;
        std::optional<Expression> sequence_maximum_cycle_count;
        std::optional<bool> sequence_strong;
        std::optional<Expression> disable_condition;
        std::optional<Expression> implication_antecedent;
        std::optional<Expression> implication_cycle_count;
        std::optional<SystemVerilogPropertyImplicationKind> implication_kind;
        std::optional<Expression> until_left;
        std::optional<Expression> until_right;
        std::optional<SystemVerilogPropertyUntilKind> until_kind;
        std::optional<Expression> always_minimum;
        std::optional<Expression> always_maximum;
        std::optional<Expression> eventually_minimum;
        std::optional<Expression> eventually_maximum;
        std::vector<Statement> sequence_match_actions;
        bool ranged_always { };
        bool strong_always { };
        bool eventually { };
        bool strong_eventually { };
        bool implication_nexttime { };
        bool implication_strong_nexttime { };
        const SystemVerilogPropertyAbort* property_abort
            = property && property->property_expression
                && property->property_expression->aborts.size() == 1U
            ? &property->property_expression->aborts.front()
            : nullptr;
        const std::span<const Token> abort_property_tokens
            = property_abort && !substituted_abort_property_tokens.empty()
            ? std::span<const Token> { substituted_abort_property_tokens }
            : property_abort
            ? std::span<const Token> { property_abort->property_tokens }
            : std::span<const Token> { };
        const std::span<const Token> abort_condition_tokens
            = property_abort && !substituted_abort_condition_tokens.empty()
            ? std::span<const Token> { substituted_abort_condition_tokens }
            : property_abort
            ? std::span<const Token> { property_abort->condition_tokens }
            : std::span<const Token> { };
        auto abort_condition = property_abort
            ? scalar_expression(abort_condition_tokens)
            : std::optional<Expression> { };
        auto condition = sequence
            ? std::optional<Expression> { }
            : property_abort
            ? scalar_expression(abort_property_tokens)
            : scalar_expression(predicate_tokens);
        if (property_abort && (!abort_condition || !condition)) {
            error(
                property_abort->span.empty()
                    ? directive.property_tokens.front()
                    : property_abort->condition_tokens.front(),
                "FSIM-SV-SEM-200",
                "executable property abort operators currently require scalar "
                "condition and property operands");
            continue;
        }
        if (sequence) {
            const auto& expression = sequence->sequence_expression;
            if (expression && !expression->intersection_operands.empty()) {
                std::optional<Expression> intersection;
                for (const auto& operand : expression->intersection_operands) {
                    auto value = scalar_expression(operand.tokens);
                    if (!value) {
                        intersection.reset();
                        break;
                    }
                    if (!intersection) {
                        intersection = std::move(*value);
                    } else {
                        intersection = Expression {
                            ExpressionKind::Binary,
                            "&&",
                            { std::move(*intersection), std::move(*value) },
                            expression->span
                        };
                    }
                }
                if (intersection) {
                    condition = std::move(*intersection);
                }
            }
            if (expression && !condition
                && expression->binary_operations.size() == 1U) {
                const auto& operation = expression->binary_operations.front();
                auto left = scalar_expression(operation.left_tokens);
                auto right = scalar_expression(operation.right_tokens);
                if (left && right) {
                    condition = Expression {
                        ExpressionKind::Binary,
                        "&&",
                        { std::move(*left), std::move(*right) },
                        operation.span
                    };
                }
            }
            if (expression && !condition
                && expression->first_matches.size() == 1U) {
                const auto& first_match = expression->first_matches.front();
                condition = scalar_expression(first_match.sequence_tokens);
                if (!condition) {
                    const std::span<const Token> match_sequence {
                        first_match.sequence_tokens
                    };
                    const auto delay = std::ranges::find_if(
                        match_sequence,
                        [&](const Token& token) {
                            const auto offset = static_cast<std::size_t>(
                                &token - match_sequence.data());
                            return token.kind == TokenKind::Hash
                                && offset + 2U < match_sequence.size()
                                && match_sequence[offset + 1U].kind
                                == TokenKind::Hash;
                        });
                    if (delay != match_sequence.end()) {
                        const auto position = static_cast<std::size_t>(
                            delay - match_sequence.begin());
                        std::size_t right = position + 2U;
                        const Token* minimum { nullptr };
                        const Token* maximum { nullptr };
                        if (right < match_sequence.size()
                            && match_sequence[right].kind == TokenKind::Number) {
                            minimum = &match_sequence[right++];
                        } else if (right + 4U < match_sequence.size()
                            && match_sequence[right].kind
                                == TokenKind::LeftBracket
                            && match_sequence[right + 1U].kind
                                == TokenKind::Number
                            && match_sequence[right + 2U].kind
                                == TokenKind::Colon
                            && match_sequence[right + 3U].kind
                                == TokenKind::Number
                            && match_sequence[right + 4U].kind
                                == TokenKind::RightBracket) {
                            minimum = &match_sequence[right + 1U];
                            maximum = &match_sequence[right + 3U];
                            right += 5U;
                        }
                        auto first = position == 0U
                            ? std::optional<Expression> { }
                            : scalar_expression(match_sequence.first(position));
                        auto second = right >= match_sequence.size()
                            ? std::optional<Expression> { }
                            : scalar_expression(match_sequence.subspan(right));
                        if (first && second && minimum) {
                            sequence_antecedent = std::move(*first);
                            condition = std::move(*second);
                            sequence_cycle_count = Expression {
                                ExpressionKind::IntegerLiteral,
                                minimum->text,
                                { },
                                minimum->span
                            };
                            if (maximum) {
                                sequence_maximum_cycle_count = Expression {
                                    ExpressionKind::IntegerLiteral,
                                    maximum->text,
                                    { },
                                    maximum->span
                                };
                            }
                        }
                    }
                }
                const std::span<const Token> items { first_match.match_item_tokens };
                std::size_t begin { };
                int parentheses { };
                for (std::size_t position = 0U;
                    position <= items.size(); ++position) {
                    const bool separator = position == items.size()
                        || (items[position].kind == TokenKind::Comma
                            && parentheses == 0);
                    if (!separator) {
                        if (items[position].kind == TokenKind::LeftParen)
                            ++parentheses;
                        else if (items[position].kind == TokenKind::RightParen)
                            --parentheses;
                        continue;
                    }
                    const auto item = items.subspan(begin, position - begin);
                    begin = position + 1U;
                    if (item.empty())
                        continue;
                    const auto assignment = std::ranges::find_if(
                        item, [](const Token& token) {
                            return token.kind == TokenKind::Assign
                                || token.kind == TokenKind::PlusAssign
                                || token.kind == TokenKind::MinusAssign
                                || token.kind == TokenKind::StarAssign
                                || token.kind == TokenKind::SlashAssign
                                || token.kind == TokenKind::PercentAssign
                                || token.kind == TokenKind::AmpersandAssign
                                || token.kind == TokenKind::PipeAssign
                                || token.kind == TokenKind::CaretAssign
                                || token.kind == TokenKind::ShiftLeftAssign
                                || token.kind == TokenKind::ShiftRightAssign
                                || token.kind
                                == TokenKind::ArithmeticShiftLeftAssign
                                || token.kind
                                == TokenKind::ArithmeticShiftRightAssign;
                        });
                    const auto increment = item.size() == 2U
                        && ((item.front().kind == TokenKind::Identifier
                                && (item.back().kind == TokenKind::PlusPlus
                                    || item.back().kind
                                        == TokenKind::MinusMinus))
                            || (item.back().kind == TokenKind::Identifier
                                && (item.front().kind == TokenKind::PlusPlus
                                    || item.front().kind
                                        == TokenKind::MinusMinus)));
                    if (assignment != item.end() || increment) {
                        const auto& target = increment
                                && item.front().kind != TokenKind::Identifier
                            ? item.back()
                            : item.front();
                        if (target.kind != TokenKind::Identifier
                            || (assignment != item.end()
                                && (assignment == item.begin()
                                    || std::next(assignment) == item.end()))) {
                            condition.reset();
                            sequence_match_actions.clear();
                            break;
                        }
                        Statement statement;
                        statement.kind = StatementKind::Assignment;
                        statement.assignment_kind = AssignmentKind::Blocking;
                        statement.target = Expression {
                            ExpressionKind::Identifier,
                            target.text,
                            { },
                            target.span
                        };
                        if (increment) {
                            const auto increase = item.front().kind
                                    == TokenKind::PlusPlus
                                || item.back().kind == TokenKind::PlusPlus;
                            statement.value = Expression {
                                ExpressionKind::Binary,
                                increase ? "+" : "-",
                                { statement.target,
                                    Expression {
                                        ExpressionKind::IntegerLiteral,
                                        "1", { }, target.span } },
                                cover(item.front().span, item.back().span)
                            };
                        } else {
                            const auto assignment_index
                                = static_cast<std::size_t>(
                                    assignment - item.begin());
                            auto value = scalar_expression(
                                item.subspan(assignment_index + 1U));
                            if (!value) {
                                condition.reset();
                                sequence_match_actions.clear();
                                break;
                            }
                            const auto operation = [&]() -> std::string_view {
                                switch (assignment->kind) {
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
                                    return { };
                                }
                            }();
                            statement.value = operation.empty()
                                ? std::move(*value)
                                : Expression {
                                      ExpressionKind::Binary,
                                      std::string { operation },
                                      { statement.target, std::move(*value) },
                                      cover(item.front().span, item.back().span)
                                  };
                        }
                        statement.span = cover(
                            item.front().span, item.back().span);
                        sequence_match_actions.push_back(std::move(statement));
                        continue;
                    }
                    auto calls = action_statements(item);
                    if (!calls.empty()) {
                        sequence_match_actions.insert(
                            sequence_match_actions.end(),
                            std::make_move_iterator(calls.begin()),
                            std::make_move_iterator(calls.end()));
                        continue;
                    }
                    if (item.size() >= 3U
                        && item.front().kind == TokenKind::Identifier
                        && item[1].kind == TokenKind::LeftParen
                        && item.back().kind == TokenKind::RightParen) {
                        Statement call;
                        call.kind = StatementKind::TaskCall;
                        call.task_name = item.front().text;
                        call.span = cover(item.front().span, item.back().span);
                        const auto arguments = item.subspan(
                            2U, item.size() - 3U);
                        std::size_t argument_begin { };
                        int argument_depth { };
                        bool valid_call { true };
                        for (std::size_t argument = 0U;
                            argument <= arguments.size(); ++argument) {
                            const bool argument_end
                                = argument == arguments.size()
                                || (arguments[argument].kind == TokenKind::Comma
                                    && argument_depth == 0);
                            if (argument_end) {
                                const auto value_tokens = arguments.subspan(
                                    argument_begin, argument - argument_begin);
                                if (!value_tokens.empty()) {
                                    auto value = scalar_expression(value_tokens);
                                    if (!value) {
                                        valid_call = false;
                                        break;
                                    }
                                    call.task_arguments.push_back(
                                        std::move(*value));
                                    call.task_argument_names.emplace_back();
                                }
                                argument_begin = argument + 1U;
                            } else if (arguments[argument].kind
                                == TokenKind::LeftParen) {
                                ++argument_depth;
                            } else if (arguments[argument].kind
                                == TokenKind::RightParen) {
                                --argument_depth;
                            }
                        }
                        if (valid_call && argument_depth == 0) {
                            sequence_match_actions.push_back(std::move(call));
                            continue;
                        }
                    }
                    condition.reset();
                    sequence_match_actions.clear();
                    break;
                }
            }
            if (expression && !condition
                && expression->elements.size() == 1U
                && expression->delays.empty()
                && expression->intersection_operands.empty()
                && expression->binary_operations.empty()
                && expression->first_matches.empty()) {
                const auto& element_tokens = substituted_sequence_tokens.empty()
                    ? expression->elements.front().expression_tokens
                    : substituted_sequence_tokens;
                condition = scalar_expression(element_tokens);
            }
            const bool two_element_sequence = expression
                && expression->elements.size() == 2U
                && expression->delays.size() == 1U
                && expression->intersection_operands.empty()
                && expression->binary_operations.empty()
                && expression->first_matches.empty()
                && std::ranges::all_of(
                    expression->elements, [](const auto& element) {
                        return element.repetition
                            == SystemVerilogSequenceRepetitionKind::None;
                    })
                && expression->delays.front().range.minimum_tokens.size() == 1U && (expression->delays.front().range.maximum_tokens.empty() || expression->delays.front().range.maximum_tokens.size() == 1U);
            if (two_element_sequence) {
                const auto& first_tokens = substituted_sequence_tokens.empty()
                    ? expression->elements.front().expression_tokens
                    : substituted_sequence_tokens;
                std::span<const Token> first_span { first_tokens };
                std::span<const Token> second_span
                    = expression->elements.back().expression_tokens;
                const Token* count = expression->delays.front()
                                         .range.minimum_tokens.front()
                                         .kind
                        == TokenKind::Number
                    ? &expression->delays.front().range.minimum_tokens.front()
                    : nullptr;
                if (!substituted_sequence_tokens.empty()) {
                    const auto separator = std::ranges::find_if(
                        substituted_sequence_tokens,
                        [](const Token& token) {
                            return token.kind == TokenKind::Hash;
                        });
                    if (separator != substituted_sequence_tokens.end()) {
                        const auto position = static_cast<std::size_t>(
                            separator - substituted_sequence_tokens.begin());
                        if (position != 0U
                            && position + 3U < substituted_sequence_tokens.size()
                            && substituted_sequence_tokens[position + 2U].kind
                                == TokenKind::Number) {
                            first_span = std::span<const Token> {
                                substituted_sequence_tokens.data(), position
                            };
                            second_span = std::span<const Token> {
                                substituted_sequence_tokens.data() + position + 3U,
                                substituted_sequence_tokens.size() - position - 3U
                            };
                            count = &substituted_sequence_tokens[position + 2U];
                        }
                    }
                }
                auto first = scalar_expression(
                    first_span);
                auto second = scalar_expression(second_span);
                if (first && second && count) {
                    sequence_antecedent = std::move(*first);
                    condition = std::move(*second);
                    sequence_cycle_count = Expression {
                        ExpressionKind::IntegerLiteral,
                        count->text,
                        { },
                        count->span
                    };
                    if (expression->delays.front().range.maximum_tokens.size()
                            == 1U
                        && expression->delays.front().range.maximum_tokens.front().kind
                            == TokenKind::Number) {
                        const auto& maximum
                            = expression->delays.front().range.maximum_tokens.front();
                        sequence_maximum_cycle_count = Expression {
                            ExpressionKind::IntegerLiteral,
                            maximum.text,
                            { },
                            maximum.span
                        };
                    }
                }
            }
        }
        if (!condition && property && property->property_expression
            && property->property_expression->delays.size() == 1U
            && property->property_expression->implications.empty()
            && property->property_expression->until_operations.empty()
            && property->property_expression->nexttimes.empty()
            && property->property_expression->recurrences.empty()
            && property->property_expression->sequence_strengths.empty()
            && property->property_expression->aborts.empty()) {
            const auto& delay = property->property_expression->delays.front();
            const auto exact_delay = delay.range.minimum_tokens.size() == 1U
                && delay.range.minimum_tokens.front().kind == TokenKind::Number
                && delay.range.maximum_tokens.empty();
            const auto separator = std::ranges::find_if(
                predicate_tokens,
                [&](const Token& token) {
                    const auto offset = static_cast<std::size_t>(
                        &token - predicate_tokens.data());
                    return token.kind == TokenKind::Hash
                        && offset + 1U < predicate_tokens.size()
                        && predicate_tokens[offset + 1U].kind == TokenKind::Hash;
                });
            if (exact_delay && separator != predicate_tokens.end()) {
                const auto position = static_cast<std::size_t>(
                    separator - predicate_tokens.begin());
                const auto right = position + 3U;
                if (position != 0U && right < predicate_tokens.size()
                    && predicate_tokens[position + 2U].kind == TokenKind::Number
                    && predicate_tokens[position + 2U].text
                        == delay.range.minimum_tokens.front().text) {
                    auto first = scalar_expression(predicate_tokens.first(position));
                    auto second = scalar_expression(predicate_tokens.subspan(right));
                    if (first && second) {
                        sequence_antecedent = std::move(*first);
                        condition = std::move(*second);
                        const auto& count = delay.range.minimum_tokens.front();
                        sequence_cycle_count = Expression {
                            ExpressionKind::IntegerLiteral,
                            count.text,
                            { },
                            count.span
                        };
                    }
                }
            }
        }
        if (!condition && property && property->property_expression
            && property->property_expression->sequence_strengths.size() == 1U) {
            const auto& strength
                = property->property_expression->sequence_strengths.front();
            const std::span<const Token> tokens { strength.sequence_tokens };
            const auto separator = std::ranges::find_if(
                tokens, [&](const Token& token) {
                    const auto offset
                        = static_cast<std::size_t>(&token - tokens.data());
                    return token.kind == TokenKind::Hash
                        && offset + 2U < tokens.size()
                        && tokens[offset + 1U].kind == TokenKind::Hash
                        && tokens[offset + 2U].kind == TokenKind::Number;
                });
            if (separator != tokens.end()) {
                const auto position = static_cast<std::size_t>(
                    separator - tokens.begin());
                const auto right = position + 3U;
                if (position != 0U && right < tokens.size()) {
                    auto first = scalar_expression(tokens.first(position));
                    auto second = scalar_expression(tokens.subspan(right));
                    if (first && second) {
                        sequence_antecedent = std::move(*first);
                        condition = std::move(*second);
                        const auto& count = tokens[position + 2U];
                        sequence_cycle_count = Expression {
                            ExpressionKind::IntegerLiteral,
                            count.text,
                            { },
                            count.span
                        };
                        sequence_strong = strength.kind
                            == SystemVerilogPropertySequenceStrengthKind::Strong;
                    }
                }
            }
        }
        if (!condition && property && property->property_expression
            && property->property_expression->recurrences.size() == 1U) {
            const auto& recurrence = property->property_expression->recurrences.front();
            if ((recurrence.kind
                        == SystemVerilogPropertyRecurrenceKind::Always
                    || recurrence.kind
                        == SystemVerilogPropertyRecurrenceKind::StrongAlways)
                && !recurrence.range) {
                condition = scalar_expression(recurrence.operand_tokens);
            } else if ((recurrence.kind
                               == SystemVerilogPropertyRecurrenceKind::Always
                           || recurrence.kind
                               == SystemVerilogPropertyRecurrenceKind::StrongAlways)
                && recurrence.range) {
                auto minimum = scalar_expression(
                    recurrence.range->minimum_tokens);
                auto maximum = recurrence.range->maximum_tokens.empty()
                    ? minimum
                    : scalar_expression(recurrence.range->maximum_tokens);
                auto operand = scalar_expression(recurrence.operand_tokens);
                if (minimum && maximum && operand) {
                    always_minimum = std::move(*minimum);
                    always_maximum = std::move(*maximum);
                    condition = std::move(*operand);
                    ranged_always = true;
                    strong_always = recurrence.kind
                        == SystemVerilogPropertyRecurrenceKind::StrongAlways;
                }
            } else if ((recurrence.kind
                               == SystemVerilogPropertyRecurrenceKind::Eventually
                           || recurrence.kind
                               == SystemVerilogPropertyRecurrenceKind::StrongEventually)
                && !recurrence.range) {
                condition = scalar_expression(recurrence.operand_tokens);
                eventually = condition.has_value();
                strong_eventually = recurrence.kind
                    == SystemVerilogPropertyRecurrenceKind::StrongEventually;
            } else if ((recurrence.kind
                               == SystemVerilogPropertyRecurrenceKind::Eventually
                           || recurrence.kind
                               == SystemVerilogPropertyRecurrenceKind::StrongEventually)
                && recurrence.range) {
                auto minimum = scalar_expression(
                    recurrence.range->minimum_tokens);
                auto maximum = recurrence.range->maximum_tokens.empty()
                    ? minimum
                    : scalar_expression(recurrence.range->maximum_tokens);
                auto operand = scalar_expression(recurrence.operand_tokens);
                if (minimum && maximum && operand) {
                    eventually_minimum = std::move(*minimum);
                    eventually_maximum = std::move(*maximum);
                    condition = std::move(*operand);
                    eventually = true;
                    strong_eventually = recurrence.kind
                        == SystemVerilogPropertyRecurrenceKind::StrongEventually;
                }
            }
        }
        if (!condition && property && property->property_expression
            && property->property_expression->until_operations.size() == 1U) {
            const auto& until
                = property->property_expression->until_operations.front();
            auto left = scalar_expression(until.left_tokens);
            auto right = scalar_expression(until.right_tokens);
            if (left && right) {
                until_left = std::move(*left);
                until_right = std::move(*right);
                until_kind = until.kind;
                condition = *until_right;
            }
        }
        if (!condition && property && property->property_expression
            && property->property_expression->implications.size() == 1U) {
            const auto& implication = property->property_expression->implications.front();
            auto antecedent = scalar_expression(implication.antecedent_tokens);
            auto consequent = scalar_expression(implication.consequent_tokens);
            if (!consequent
                && implication.kind
                    == SystemVerilogPropertyImplicationKind::Overlapped
                && property->property_expression->nexttimes.size() == 1U) {
                const auto& nexttime = property->property_expression->nexttimes.front();
                const auto exact_nexttime = !implication.consequent_tokens.empty()
                    && (implication.consequent_tokens.front().text == "nexttime"
                        || implication.consequent_tokens.front().text
                            == "s_nexttime")
                    && (nexttime.kind
                            == SystemVerilogPropertyNexttimeKind::Nexttime
                        || nexttime.kind
                            == SystemVerilogPropertyNexttimeKind::StrongNexttime)
                    && (nexttime.count_tokens.empty()
                        || (nexttime.count_tokens.size() == 1U
                            && nexttime.count_tokens.front().kind
                                == TokenKind::Number));
                if (exact_nexttime) {
                    consequent = scalar_expression(nexttime.operand_tokens);
                    const auto& count = nexttime.count_tokens.empty()
                        ? implication.consequent_tokens.front()
                        : nexttime.count_tokens.front();
                    implication_cycle_count = Expression {
                        ExpressionKind::IntegerLiteral,
                        nexttime.count_tokens.empty() ? "1" : count.text,
                        { },
                        count.span
                    };
                    implication_strong_nexttime = nexttime.kind
                        == SystemVerilogPropertyNexttimeKind::StrongNexttime;
                    implication_nexttime = true;
                }
            }
            if (antecedent && consequent) {
                implication_antecedent = std::move(*antecedent);
                implication_kind = implication.kind;
                condition = std::move(*consequent);
            }
        }
        if ((implication_cycle_count
                || (implication_kind
                    && *implication_kind
                        == SystemVerilogPropertyImplicationKind::Nonoverlapped))
            && !assertion_clock) {
            error(
                directive.property_tokens.front(),
                "FSIM-SV-SEM-200",
                "an executable delayed implication requires a direct property "
                "clock");
            continue;
        }
        if (sequence_antecedent && !assertion_clock) {
            error(
                directive.property_tokens.front(),
                "FSIM-SV-SEM-200",
                "an executable sequence concatenation requires a direct clock");
            continue;
        }
        if (until_kind && !assertion_clock) {
            error(
                directive.property_tokens.front(),
                "FSIM-SV-SEM-200",
                "an executable until property requires a direct property clock");
            continue;
        }
        if (eventually && !assertion_clock) {
            error(
                directive.property_tokens.front(),
                "FSIM-SV-SEM-200",
                "an executable eventually property requires a direct property "
                "clock");
            continue;
        }
        const auto* assertion_disable = property && property->disable
            ? &*property->disable
            : directive_sequence && directive_sequence->disable
            ? &*directive_sequence->disable
            : sequence && sequence->disable
            ? &*sequence->disable
            : nullptr;
        if (condition && assertion_disable) {
            const std::span<const Token> disable_tokens
                = property && property->disable
                ? substituted_disable_tokens.empty()
                    ? std::span<const Token> {
                          property->disable->condition_tokens }
                    : std::span<const Token> { substituted_disable_tokens }
                : directive_sequence && directive_sequence->disable
                ? std::span<const Token> {
                      directive_sequence->disable->condition_tokens }
                : substituted_sequence_disable_tokens.empty()
                ? std::span<const Token> {
                      sequence->disable->condition_tokens }
                : std::span<const Token> {
                      substituted_sequence_disable_tokens };
            disable_condition = scalar_expression(disable_tokens);
            if (!disable_condition) {
                error(
                    disable_tokens.empty()
                        ? directive.property_tokens.front()
                        : disable_tokens.front(),
                    "FSIM-SV-SEM-200",
                    "executable disable iff requires a scalar condition");
                continue;
            }
        }
        if (!condition) {
            error(
                directive.property_tokens.front(),
                "FSIM-SV-SEM-200",
                "the executable concurrent-property slice requires a scalar "
                "predicate, unbounded scalar always property, scalar implication, "
                "scalar nexttime consequent, or two-element scalar sequence with "
                "one exact cycle delay");
            continue;
        }

        std::vector<VariableDeclaration> assertion_local_variables;
        std::vector<Statement> assertion_local_initializers;
        std::unordered_set<std::string> assertion_local_names;
        bool invalid_assertion_local { };
        for (const auto& local : executable_locals) {
            if (!local.declaration
                || !assertion_local_names.insert(local.declaration->name).second) {
                error(
                    directive.property_tokens.front(),
                    "FSIM-SV-SEM-199",
                    "executable property and sequence local-variable names must "
                    "be unique within one assertion attempt");
                invalid_assertion_local = true;
                break;
            }
            const auto type = assertion_local_type(local.declaration->type_tokens);
            if (!type) {
                error(
                    local.declaration->name_span.empty()
                        ? directive.property_tokens.front()
                        : Token {
                              TokenKind::Identifier,
                              local.declaration->name,
                              local.declaration->name_span,
                              { } },
                    "FSIM-SV-SEM-199", "an executable assertion local variable requires a concrete "
                                       "integral or named packed type");
                invalid_assertion_local = true;
                break;
            }
            assertion_local_variables.emplace_back(
                local.declaration->name,
                *type,
                std::nullopt,
                local.declaration->span);
            if (local.initializer_tokens.empty())
                continue;
            const auto initializer = scalar_expression(local.initializer_tokens);
            if (!initializer) {
                error(
                    local.initializer_tokens.front(),
                    "FSIM-SV-SEM-199",
                    "an executable assertion local initializer requires a scalar "
                    "integral expression");
                invalid_assertion_local = true;
                break;
            }
            Statement assignment;
            assignment.kind = StatementKind::Assignment;
            assignment.assignment_kind = AssignmentKind::Blocking;
            assignment.target = Expression {
                ExpressionKind::Identifier,
                local.declaration->name,
                { },
                local.declaration->name_span
            };
            assignment.value = std::move(*initializer);
            assignment.span = local.declaration->span;
            assertion_local_initializers.push_back(std::move(assignment));
        }
        for (const auto& action : sequence_match_actions) {
            if (action.kind == StatementKind::Assignment
                && !assertion_local_names.contains(action.target.text)) {
                error(
                    directive.property_tokens.front(),
                    "FSIM-SV-SEM-199",
                    "an executable sequence match assignment must target a "
                    "property or sequence local variable");
                invalid_assertion_local = true;
                break;
            }
        }
        if (invalid_assertion_local)
            continue;

        Process process;
        process.kind = assertion_clock
            ? ProcessKind::VerilogAlways
            : ProcessKind::Initial;
        process.systemverilog_concurrent_assertion = true;
        process.name = directive.label.empty()
            ? "$assertion$" + std::to_string(index + 1U)
            : directive.label;
        process.span = directive.span;
        process.variables = std::move(assertion_local_variables);
        if (assertion_clock) {
            const auto event = assertion_clock_tokens;
            const auto signal = std::ranges::find_if(
                event.rbegin(), event.rend(), [](const Token& token) {
                    return token.kind == TokenKind::Identifier
                        && token.text != "posedge"
                        && token.text != "negedge"
                        && token.text != "edge";
                });
            if (signal != event.rend()) {
                Sensitivity sensitivity;
                sensitivity.signal = signal->text;
                sensitivity.span = assertion_clock->span;
                sensitivity.edge = std::ranges::any_of(event, [](const Token& token) {
                    return token.text == "posedge";
                })
                    ? EdgeKind::Positive
                    : std::ranges::any_of(event, [](const Token& token) {
                          return token.text == "negedge";
                      })
                    ? EdgeKind::Negative
                    : EdgeKind::Any;
                process.sensitivities.push_back(std::move(sensitivity));
            }
        }

        Statement assertion;
        assertion.kind = StatementKind::Assert;
        assertion.condition = std::move(*condition);
        assertion.span = directive.span;
        assertion.assertion_message = "concurrent assertion '" + process.name + "' failed";
        const auto kind_name = directive.kind == SystemVerilogConcurrentAssertionKind::Assume
            ? "assumption"
            : directive.kind == SystemVerilogConcurrentAssertionKind::Cover
            ? "cover"
            : directive.kind == SystemVerilogConcurrentAssertionKind::Restrict
            ? "restriction"
            : "assertion";
        const auto coverage_marker =
            [&](const std::string_view outcome) {
                Statement marker;
                marker.kind = StatementKind::Display;
                marker.output_newline = false;
                marker.output_text = std::string { "\x1f"
                                                   "fsim.concurrent-assertion|" }
                    + std::to_string(index)
                    + "|" + kind_name
                    + "|" + std::string { outcome }
                    + "|" + process.name;
                marker.span = directive.span;
                return marker;
            };
        const auto action_region_marker = [&] {
            Statement marker;
            marker.kind = StatementKind::Display;
            marker.output_newline = false;
            marker.output_text = std::string { "\x1f"
                                               "fsim.concurrent-assertion-action-region|" }
                + process.name;
            marker.span = directive.span;
            return marker;
        };
        const auto append_actions = [&](std::vector<Statement>& destination,
                                        const std::span<const Statement> actions) {
            if (actions.empty()) {
                return;
            }
            destination.push_back(action_region_marker());
            destination.insert(
                destination.end(), actions.begin(), actions.end());
        };
        const auto serialized_actions = [](const std::vector<Statement>& statements) {
            auto begin = std::size_t { 0 };
            constexpr std::string_view internal_marker {
                "\x1f"
                "fsim.concurrent-assertion"
            };
            while (begin < statements.size()
                && statements[begin].kind == StatementKind::Display
                && statements[begin].output_text.starts_with(
                    internal_marker)) {
                ++begin;
            }
            return std::span<const Statement> { statements }.subspan(begin);
        };
        const auto pending_marker = [&](const std::string_view outcome,
                                        const std::span<const Statement> actions) {
            constexpr char digits[] = "0123456789abcdef";
            const auto encode = [&](const std::string_view text) {
                std::string result;
                result.reserve(text.size() * 2U);
                for (const auto character : text) {
                    const auto byte = static_cast<unsigned char>(character);
                    result.push_back(digits[byte >> 4U]);
                    result.push_back(digits[byte & 0x0fU]);
                }
                return result;
            };
            Statement marker;
            marker.kind = StatementKind::Display;
            marker.output_newline = false;
            marker.output_text
                = std::string { "\x1f"
                                "fsim.concurrent-assertion-pending|" }
                + std::to_string(index) + "|" + kind_name + "|"
                + std::string { outcome } + "|" + process.name;
            for (const auto& action : actions) {
                marker.output_text += "|";
                marker.output_text += action.kind == StatementKind::Report ? "R" : "D";
                marker.output_text += "," + std::to_string(static_cast<std::underlying_type_t<AssertionSeverity>>(action.assertion_severity));
                marker.output_text += action.output_newline ? ",1," : ",0,";
                marker.output_text += encode(action.output_text) + ",";
                marker.output_text += encode(action.span.source_name) + ",";
                marker.output_text += std::to_string(action.span.begin.line) + ",";
                marker.output_text += std::to_string(action.span.begin.column);
            }
            marker.span = directive.span;
            return marker;
        };
        assertion.assertion_has_pass_action = true;
        assertion.statements.push_back(coverage_marker("pass"));
        const auto pass_actions = action_statements(directive.pass_action_tokens);
        if (!sequence_match_actions.empty() || !pass_actions.empty()) {
            assertion.statements.push_back(action_region_marker());
            assertion.statements.insert(
                assertion.statements.end(),
                sequence_match_actions.begin(), sequence_match_actions.end());
            assertion.statements.insert(
                assertion.statements.end(), pass_actions.begin(), pass_actions.end());
        }
        assertion.assertion_has_failure_action = true;
        assertion.else_statements.push_back(coverage_marker("failure"));
        const auto failure_actions = action_statements(directive.failure_action_tokens);
        append_actions(assertion.else_statements, failure_actions);
        if (failure_actions.empty() && !directive.has_failure_action && (directive.kind == SystemVerilogConcurrentAssertionKind::Assert || directive.kind == SystemVerilogConcurrentAssertionKind::Assume)) {
            Statement report;
            report.kind = StatementKind::Report;
            report.output_text = assertion.assertion_message;
            report.assertion_severity = AssertionSeverity::Error;
            report.span = directive.span;
            assertion.else_statements.push_back(action_region_marker());
            assertion.else_statements.push_back(std::move(report));
        }
        const auto append_evaluation =
            [&](Statement evaluation) {
                Statement attempt;
                attempt.kind = StatementKind::Fork;
                attempt.span = directive.span;
                attempt.fork_join_kind = ForkJoinKind::None;
                attempt.statements.insert(
                    attempt.statements.end(),
                    assertion_local_initializers.begin(),
                    assertion_local_initializers.end());
                attempt.statements.push_back(std::move(evaluation));
                if (!disable_condition) {
                    process.statements.push_back(std::move(attempt));
                    return;
                }
                Statement disable;
                disable.kind = StatementKind::If;
                disable.condition = std::move(*disable_condition);
                disable.span = directive.span;
                disable.statements.push_back(coverage_marker("abort"));
                disable.else_statements.push_back(std::move(attempt));
                process.statements.push_back(std::move(disable));
            };
        if (property_abort) {
            Statement aborted;
            aborted.kind = StatementKind::If;
            aborted.condition = std::move(*abort_condition);
            aborted.span = property_abort->span;
            if (property_abort->outcome
                == SystemVerilogPropertyAbortOutcome::VacuousSuccess) {
                aborted.statements.push_back(coverage_marker("vacuous"));
                append_actions(aborted.statements, pass_actions);
            } else {
                auto failure = assertion;
                failure.condition = Expression {
                    ExpressionKind::IntegerLiteral,
                    "0",
                    { },
                    property_abort->span
                };
                aborted.statements.push_back(std::move(failure));
            }
            aborted.else_statements.push_back(std::move(assertion));
            append_evaluation(std::move(aborted));
        } else if (eventually) {
            const auto ranged = eventually_minimum && eventually_maximum;
            Statement success = assertion;
            Statement stop;
            stop.kind = StatementKind::Break;
            stop.span = directive.span;

            Statement wait;
            wait.kind = StatementKind::WaitOn;
            wait.span = directive.span;
            wait.sensitivities = process.sensitivities;

            Statement match;
            match.kind = StatementKind::If;
            match.condition = assertion.condition;
            match.span = directive.span;
            match.statements.push_back(std::move(success));
            match.statements.push_back(std::move(stop));
            if (ranged) {
                const auto counter_name
                    = "@assertion_eventually_counter_" + std::to_string(index);
                const Expression counter {
                    ExpressionKind::Identifier,
                    counter_name,
                    { },
                    directive.span
                };
                const Expression one {
                    ExpressionKind::IntegerLiteral,
                    "1",
                    { },
                    directive.span
                };
                const Expression count {
                    ExpressionKind::Binary,
                    "+",
                    { Expression {
                          ExpressionKind::Binary,
                          "-",
                          { *eventually_maximum, *eventually_minimum },
                          directive.span },
                        one },
                    directive.span
                };
                const Expression next {
                    ExpressionKind::Binary,
                    "+",
                    { counter, one },
                    directive.span
                };

                Statement exhausted;
                exhausted.kind = StatementKind::If;
                exhausted.condition = Expression {
                    ExpressionKind::Binary,
                    ">=",
                    { next, count },
                    directive.span
                };
                exhausted.span = directive.span;
                exhausted.statements.push_back(assertion);
                exhausted.statements.push_back(stop);
                exhausted.else_statements.push_back(std::move(wait));
                match.else_statements.push_back(std::move(exhausted));

                Statement loop;
                loop.kind = StatementKind::Loop;
                loop.loop_runtime = true;
                loop.loop_variable_declared = true;
                loop.loop_variable = counter_name;
                loop.target = counter;
                loop.loop_initial = Expression {
                    ExpressionKind::IntegerLiteral,
                    "0",
                    { },
                    directive.span
                };
                loop.condition = Expression {
                    ExpressionKind::Binary,
                    "<",
                    { counter, count },
                    directive.span
                };
                loop.loop_update_target = counter;
                loop.value = next;
                loop.span = directive.span;
                loop.statements.push_back(std::move(match));

                Statement initial_wait;
                initial_wait.kind = StatementKind::WaitOn;
                initial_wait.span = directive.span;
                initial_wait.sensitivities = process.sensitivities;
                initial_wait.procedural_assignment_repeat = true;
                initial_wait.loop_limit = *eventually_minimum;

                const auto pending_failure_actions
                    = serialized_actions(assertion.else_statements);
                Statement evaluation;
                evaluation.kind = StatementKind::Block;
                evaluation.span = directive.span;
                evaluation.statements.push_back(pending_marker(
                    strong_eventually ? "failure" : "vacuous",
                    strong_eventually
                        ? pending_failure_actions
                        : std::span<const Statement> { pass_actions }));
                evaluation.statements.push_back(std::move(initial_wait));
                evaluation.statements.push_back(std::move(loop));
                append_evaluation(std::move(evaluation));
            } else {
                match.else_statements.push_back(std::move(wait));

                Statement loop;
                loop.kind = StatementKind::Loop;
                loop.loop_runtime = true;
                loop.condition = Expression {
                    ExpressionKind::IntegerLiteral,
                    "1",
                    { },
                    directive.span
                };
                loop.span = directive.span;
                loop.statements.push_back(std::move(match));

                const auto pending_failure_actions
                    = serialized_actions(assertion.else_statements);
                Statement evaluation;
                evaluation.kind = StatementKind::Block;
                evaluation.span = directive.span;
                evaluation.statements.push_back(pending_marker(
                    strong_eventually ? "failure" : "vacuous",
                    strong_eventually
                        ? pending_failure_actions
                        : std::span<const Statement> { pass_actions }));
                evaluation.statements.push_back(std::move(loop));
                append_evaluation(std::move(evaluation));
            }
        } else if (ranged_always) {
            const auto counter_name
                = "@assertion_always_counter_" + std::to_string(index);
            const Expression counter {
                ExpressionKind::Identifier,
                counter_name,
                { },
                directive.span
            };
            const Expression one {
                ExpressionKind::IntegerLiteral,
                "1",
                { },
                directive.span
            };
            const Expression count {
                ExpressionKind::Binary,
                "+",
                { Expression {
                      ExpressionKind::Binary,
                      "-",
                      { *always_maximum, *always_minimum },
                      directive.span },
                    one },
                directive.span
            };
            const Expression next {
                ExpressionKind::Binary,
                "+",
                { counter, one },
                directive.span
            };

            Statement stop;
            stop.kind = StatementKind::Break;
            stop.span = directive.span;

            Statement wait;
            wait.kind = StatementKind::WaitOn;
            wait.span = directive.span;
            wait.sensitivities = process.sensitivities;

            Statement exhausted;
            exhausted.kind = StatementKind::If;
            exhausted.condition = Expression {
                ExpressionKind::Binary,
                ">=",
                { next, count },
                directive.span
            };
            exhausted.span = directive.span;
            exhausted.statements.push_back(assertion);
            exhausted.statements.push_back(stop);
            exhausted.else_statements.push_back(std::move(wait));

            Statement match;
            match.kind = StatementKind::If;
            match.condition = assertion.condition;
            match.span = directive.span;
            match.statements.push_back(std::move(exhausted));
            match.else_statements.push_back(assertion);
            match.else_statements.push_back(stop);

            Statement loop;
            loop.kind = StatementKind::Loop;
            loop.loop_runtime = true;
            loop.loop_variable_declared = true;
            loop.loop_variable = counter_name;
            loop.target = counter;
            loop.loop_initial = Expression {
                ExpressionKind::IntegerLiteral,
                "0",
                { },
                directive.span
            };
            loop.condition = Expression {
                ExpressionKind::Binary,
                "<",
                { counter, count },
                directive.span
            };
            loop.loop_update_target = counter;
            loop.value = next;
            loop.span = directive.span;
            loop.statements.push_back(std::move(match));

            Statement initial_wait;
            initial_wait.kind = StatementKind::WaitOn;
            initial_wait.span = directive.span;
            initial_wait.sensitivities = process.sensitivities;
            initial_wait.procedural_assignment_repeat = true;
            initial_wait.loop_limit = *always_minimum;

            const auto pending_failure_actions
                = serialized_actions(assertion.else_statements);
            Statement evaluation;
            evaluation.kind = StatementKind::Block;
            evaluation.span = directive.span;
            evaluation.statements.push_back(pending_marker(
                strong_always ? "failure" : "vacuous",
                strong_always
                    ? pending_failure_actions
                    : std::span<const Statement> { pass_actions }));
            evaluation.statements.push_back(std::move(initial_wait));
            evaluation.statements.push_back(std::move(loop));
            append_evaluation(std::move(evaluation));
        } else if (until_kind) {
            const bool inclusive
                = *until_kind == SystemVerilogPropertyUntilKind::UntilWith
                || *until_kind
                    == SystemVerilogPropertyUntilKind::StrongUntilWith;
            const bool strong
                = *until_kind == SystemVerilogPropertyUntilKind::StrongUntil
                || *until_kind
                    == SystemVerilogPropertyUntilKind::StrongUntilWith;

            Statement success = assertion;
            success.condition = inclusive
                ? *until_left
                : Expression {
                      ExpressionKind::IntegerLiteral,
                      "1",
                      { },
                      directive.span
                  };
            Statement stop_success;
            stop_success.kind = StatementKind::Break;
            stop_success.span = directive.span;

            Statement failure = assertion;
            failure.condition = *until_left;
            Statement stop_failure = stop_success;

            Statement wait;
            wait.kind = StatementKind::WaitOn;
            wait.span = directive.span;
            wait.sensitivities = process.sensitivities;

            Statement left;
            left.kind = StatementKind::If;
            left.condition = *until_left;
            left.span = directive.span;
            left.statements.push_back(std::move(wait));
            left.else_statements.push_back(std::move(failure));
            left.else_statements.push_back(std::move(stop_failure));

            Statement right;
            right.kind = StatementKind::If;
            right.condition = *until_right;
            right.span = directive.span;
            right.statements.push_back(std::move(success));
            right.statements.push_back(std::move(stop_success));
            right.else_statements.push_back(std::move(left));

            Statement loop;
            loop.kind = StatementKind::Loop;
            loop.loop_runtime = true;
            loop.condition = Expression {
                ExpressionKind::IntegerLiteral,
                "1",
                { },
                directive.span
            };
            loop.span = directive.span;
            loop.statements.push_back(std::move(right));

            const auto pending_failure_actions
                = serialized_actions(assertion.else_statements);
            Statement evaluation;
            evaluation.kind = StatementKind::Block;
            evaluation.span = directive.span;
            evaluation.statements.push_back(pending_marker(
                strong ? "failure" : "vacuous",
                strong
                    ? pending_failure_actions
                    : std::span<const Statement> { pass_actions }));
            evaluation.statements.push_back(std::move(loop));
            append_evaluation(std::move(evaluation));
        } else if (sequence_antecedent) {
            auto failure_statements = assertion.else_statements;
            if (sequence_maximum_cycle_count) {
                const auto counter_name
                    = "@assertion_sequence_counter_" + std::to_string(index);
                const Expression counter {
                    ExpressionKind::Identifier,
                    counter_name,
                    { },
                    directive.span
                };
                const Expression one {
                    ExpressionKind::IntegerLiteral,
                    "1",
                    { },
                    directive.span
                };
                const Expression next {
                    ExpressionKind::Binary,
                    "+",
                    { counter, one },
                    directive.span
                };

                Statement stop;
                stop.kind = StatementKind::Break;
                stop.span = directive.span;

                Statement wait;
                wait.kind = StatementKind::WaitOn;
                wait.span = directive.span;
                wait.sensitivities = process.sensitivities;

                Statement exhausted;
                exhausted.kind = StatementKind::If;
                exhausted.condition = Expression {
                    ExpressionKind::Binary,
                    ">=",
                    { counter, *sequence_maximum_cycle_count },
                    directive.span
                };
                exhausted.span = directive.span;
                exhausted.statements.push_back(assertion);
                exhausted.statements.push_back(stop);
                exhausted.else_statements.push_back(std::move(wait));

                Statement match;
                match.kind = StatementKind::If;
                match.condition = assertion.condition;
                match.span = directive.span;
                match.statements.push_back(assertion);
                match.statements.push_back(stop);
                match.else_statements.push_back(std::move(exhausted));

                Statement loop;
                loop.kind = StatementKind::Loop;
                loop.loop_runtime = true;
                loop.loop_variable_declared = true;
                loop.loop_variable = counter_name;
                loop.target = counter;
                loop.loop_initial = *sequence_cycle_count;
                loop.condition = Expression {
                    ExpressionKind::Binary,
                    "<=",
                    { counter, *sequence_maximum_cycle_count },
                    directive.span
                };
                loop.loop_update_target = counter;
                loop.value = next;
                loop.span = directive.span;
                loop.statements.push_back(std::move(match));

                Statement initial_wait;
                initial_wait.kind = StatementKind::WaitOn;
                initial_wait.span = directive.span;
                initial_wait.sensitivities = process.sensitivities;
                initial_wait.procedural_assignment_repeat = true;
                initial_wait.loop_limit = *sequence_cycle_count;

                Statement evaluation;
                evaluation.kind = StatementKind::Block;
                evaluation.span = directive.span;
                evaluation.statements.push_back(std::move(initial_wait));
                evaluation.statements.push_back(std::move(loop));

                const auto pending_failure_actions
                    = serialized_actions(failure_statements);
                Statement attempt;
                attempt.kind = StatementKind::Fork;
                attempt.span = directive.span;
                attempt.fork_join_kind = ForkJoinKind::None;
                attempt.statements.push_back(pending_marker(
                    sequence_strong && !*sequence_strong
                        ? "vacuous"
                        : "failure",
                    sequence_strong && !*sequence_strong
                        ? std::span<const Statement> { pass_actions }
                        : pending_failure_actions));
                attempt.statements.push_back(std::move(evaluation));

                Statement first;
                first.kind = StatementKind::Assert;
                first.condition = std::move(*sequence_antecedent);
                first.span = directive.span;
                first.assertion_has_pass_action = true;
                first.statements.push_back(std::move(attempt));
                first.assertion_has_failure_action = true;
                first.else_statements = std::move(failure_statements);
                append_evaluation(std::move(first));
            } else {
                Statement wait;
                wait.kind = StatementKind::WaitOn;
                wait.span = directive.span;
                wait.sensitivities = process.sensitivities;
                wait.procedural_assignment_repeat = true;
                wait.loop_limit = std::move(*sequence_cycle_count);
                wait.statements.push_back(std::move(assertion));

                Statement attempt;
                attempt.kind = StatementKind::Fork;
                attempt.span = directive.span;
                attempt.fork_join_kind = ForkJoinKind::None;
                if (sequence_strong) {
                    const auto pending_failure_actions
                        = serialized_actions(failure_statements);
                    attempt.statements.push_back(pending_marker(
                        *sequence_strong ? "failure" : "vacuous",
                        *sequence_strong
                            ? pending_failure_actions
                            : std::span<const Statement> { pass_actions }));
                }
                attempt.statements.push_back(std::move(wait));

                Statement first;
                first.kind = StatementKind::Assert;
                first.condition = std::move(*sequence_antecedent);
                first.span = directive.span;
                first.assertion_has_pass_action = true;
                first.statements.push_back(std::move(attempt));
                first.assertion_has_failure_action = true;
                first.else_statements = std::move(failure_statements);
                append_evaluation(std::move(first));
            }
        } else if (implication_antecedent) {
            Statement implication;
            implication.kind = StatementKind::If;
            implication.condition = std::move(*implication_antecedent);
            implication.span = directive.span;
            if (implication_cycle_count
                || (implication_kind
                    && *implication_kind
                        == SystemVerilogPropertyImplicationKind::Nonoverlapped)) {
                Statement wait;
                wait.kind = StatementKind::WaitOn;
                wait.span = directive.span;
                wait.sensitivities = process.sensitivities;
                if (implication_cycle_count) {
                    wait.procedural_assignment_repeat = true;
                    wait.loop_limit = std::move(*implication_cycle_count);
                }
                const auto pending_failure_actions
                    = serialized_actions(assertion.else_statements);
                auto pending = implication_nexttime
                    ? std::optional<Statement> { pending_marker(
                          implication_strong_nexttime ? "failure" : "vacuous",
                          implication_strong_nexttime
                              ? pending_failure_actions
                              : std::span<const Statement> { pass_actions }) }
                    : std::nullopt;
                wait.statements.push_back(std::move(assertion));

                Statement attempt;
                attempt.kind = StatementKind::Fork;
                attempt.span = directive.span;
                attempt.fork_join_kind = ForkJoinKind::None;
                if (pending) {
                    attempt.statements.push_back(std::move(*pending));
                }
                attempt.statements.push_back(std::move(wait));
                implication.statements.push_back(std::move(attempt));
            } else {
                implication.statements.push_back(std::move(assertion));
            }
            implication.else_statements.push_back(coverage_marker("vacuous"));
            append_actions(implication.else_statements, pass_actions);
            append_evaluation(std::move(implication));
        } else {
            append_evaluation(std::move(assertion));
        }
        process.statements.insert(
            process.statements.begin(), coverage_marker("register"));
        unit.processes.push_back(std::move(process));
    }
}

} // namespace fsim::frontend
