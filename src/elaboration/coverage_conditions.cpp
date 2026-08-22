// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_conditions.hpp"

#include "fsim/frontend/source.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    struct PendingStatement {
        const frontend::Statement* statement { };
        std::size_t depth { };
    };

    struct PendingExpression {
        const frontend::Expression* expression { };
        std::size_t depth { };
        std::vector<CoverageConditionPathStep> path;
    };

    using PointKey = std::pair<std::uint64_t, std::uint64_t>;

    bool valid_language(const frontend::CodeCoverageLanguage language) noexcept
    {
        switch (language) {
        case frontend::CodeCoverageLanguage::Verilog:
        case frontend::CodeCoverageLanguage::SystemVerilog:
        case frontend::CodeCoverageLanguage::Vhdl:
            return true;
        }
        return false;
    }

    bool statically_true(const frontend::Expression& expression) noexcept
    {
        return (expression.kind == frontend::ExpressionKind::BooleanLiteral
                   && expression.text == "true")
            || (expression.kind == frontend::ExpressionKind::LogicLiteral
                && (expression.text == "1'b1" || expression.text == "'1'"));
    }

    std::optional<CoverageConditionDecisionKind> decision_kind(
        const frontend::Statement& statement,
        const frontend::CodeCoverageLanguage language) noexcept
    {
        switch (statement.kind) {
        case frontend::StatementKind::If:
            return CoverageConditionDecisionKind::If;
        case frontend::StatementKind::Loop:
            if (statement.condition.valid()
                && !statically_true(statement.condition)) {
                return CoverageConditionDecisionKind::Loop;
            }
            return std::nullopt;
        case frontend::StatementKind::Assert:
            return CoverageConditionDecisionKind::ImmediateAssertion;
        case frontend::StatementKind::WaitUntil:
            if (language == frontend::CodeCoverageLanguage::Vhdl
                && statement.condition.valid()
                && !statically_true(statement.condition)) {
                return CoverageConditionDecisionKind::WaitUntil;
            }
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    std::optional<CoverageConditionLogicalOperator> logical_operator(
        const frontend::Expression& expression,
        const frontend::CodeCoverageLanguage language) noexcept
    {
        if (language == frontend::CodeCoverageLanguage::Vhdl) {
            if (expression.kind == frontend::ExpressionKind::Unary
                && expression.text == "not") {
                return CoverageConditionLogicalOperator::Not;
            }
            if (expression.kind != frontend::ExpressionKind::Binary) {
                return std::nullopt;
            }
            if (expression.text == "and") {
                return CoverageConditionLogicalOperator::And;
            }
            if (expression.text == "or") {
                return CoverageConditionLogicalOperator::Or;
            }
            if (expression.text == "nand") {
                return CoverageConditionLogicalOperator::Nand;
            }
            if (expression.text == "nor") {
                return CoverageConditionLogicalOperator::Nor;
            }
            if (expression.text == "xor") {
                return CoverageConditionLogicalOperator::Xor;
            }
            if (expression.text == "xnor") {
                return CoverageConditionLogicalOperator::Xnor;
            }
            return std::nullopt;
        }

        if (expression.kind == frontend::ExpressionKind::Unary
            && expression.text == "!") {
            return CoverageConditionLogicalOperator::Not;
        }
        if (expression.kind == frontend::ExpressionKind::Binary
            && expression.text == "&&") {
            return CoverageConditionLogicalOperator::And;
        }
        if (expression.kind == frontend::ExpressionKind::Binary
            && expression.text == "||") {
            return CoverageConditionLogicalOperator::Or;
        }
        return std::nullopt;
    }

} // namespace

CoverageConditionResult discover_coverage_conditions(
    const std::span<const frontend::Statement> statements,
    const frontend::CodeCoverageLanguage language,
    const std::span<const CoverageConditionSource> sources,
    const CoverageConditionLimits limits) noexcept
{
    CoverageConditionResult result;
    const auto reject = [&](const CoverageConditionError error,
                            const std::size_t statement_index = 0U,
                            const std::size_t expression_index = 0U) {
        result.points.clear();
        result.error = error;
        result.statement_index = statement_index;
        result.expression_index = expression_index;
        return result;
    };

    try {
        if (!valid_language(language)) {
            return reject(CoverageConditionError::InvalidLanguage);
        }
        if (sources.size() > limits.maximum_sources
            || statements.size() > limits.maximum_statements) {
            return reject(CoverageConditionError::ResourceLimit);
        }

        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(CoverageConditionError::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(CoverageConditionError::InvalidSourceIdentity);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(CoverageConditionError::DuplicateSourceName);
            }
        }

        std::vector<PendingStatement> pending_statements;
        pending_statements.reserve(
            std::min(statements.size(), limits.maximum_statements));
        for (auto statement = statements.rbegin();
            statement != statements.rend(); ++statement) {
            pending_statements.push_back(PendingStatement { &*statement, 1U });
        }
        std::size_t scheduled_statements = statements.size();
        std::size_t visited_statements { };
        std::size_t visited_expressions { };
        std::size_t retained_path_steps { };
        std::size_t decision_index { };
        std::set<PointKey> point_ids;

        const auto enqueue_statements
            = [&](const std::span<const frontend::Statement> children,
                  const std::size_t depth) {
                  if (children.size()
                      > limits.maximum_statements - scheduled_statements) {
                      return false;
                  }
                  scheduled_statements += children.size();
                  for (auto child = children.rbegin();
                      child != children.rend(); ++child) {
                      pending_statements.push_back(
                          PendingStatement { &*child, depth });
                  }
                  return true;
              };

        while (!pending_statements.empty()) {
            const auto current = pending_statements.back();
            pending_statements.pop_back();
            const auto current_statement_index = visited_statements++;
            if (current.depth > limits.maximum_statement_nesting) {
                return reject(CoverageConditionError::ResourceLimit,
                    current_statement_index);
            }

            for (std::size_t alternative_index
                = current.statement->case_alternatives.size();
                alternative_index > 0U; --alternative_index) {
                const auto& alternative = current.statement->case_alternatives[alternative_index - 1U];
                if (!enqueue_statements(
                        alternative.statements, current.depth + 1U)) {
                    return reject(CoverageConditionError::ResourceLimit,
                        current_statement_index);
                }
            }
            if (!enqueue_statements(current.statement->else_statements,
                    current.depth + 1U)
                || !enqueue_statements(current.statement->statements,
                    current.depth + 1U)) {
                return reject(CoverageConditionError::ResourceLimit,
                    current_statement_index);
            }

            const auto kind = decision_kind(*current.statement, language);
            if (!kind) {
                continue;
            }
            if (!current.statement->condition.valid()) {
                return reject(CoverageConditionError::MissingDecisionCondition,
                    current_statement_index);
            }

            std::vector<PendingExpression> pending_expressions;
            pending_expressions.push_back(PendingExpression {
                &current.statement->condition, 1U, { } });
            std::size_t condition_index { };
            while (!pending_expressions.empty()) {
                auto expression = std::move(pending_expressions.back());
                pending_expressions.pop_back();
                const auto current_expression_index = visited_expressions++;
                if (visited_expressions > limits.maximum_expression_nodes
                    || expression.depth
                        > limits.maximum_expression_nesting) {
                    return reject(CoverageConditionError::ResourceLimit,
                        current_statement_index, current_expression_index);
                }

                const auto operation
                    = logical_operator(*expression.expression, language);
                if (operation) {
                    if (*operation == CoverageConditionLogicalOperator::Not) {
                        if (expression.expression->operands.size() != 1U) {
                            return reject(
                                CoverageConditionError::MalformedLogicalExpression,
                                current_statement_index,
                                current_expression_index);
                        }
                        if (expression.path.size()
                            >= limits.maximum_expression_nesting) {
                            return reject(CoverageConditionError::ResourceLimit,
                                current_statement_index,
                                current_expression_index);
                        }
                        expression.path.push_back(
                            CoverageConditionPathStep {
                                *operation, CoverageConditionOperand::Only });
                        pending_expressions.push_back(PendingExpression {
                            &expression.expression->operands.front(),
                            expression.depth + 1U,
                            std::move(expression.path),
                        });
                        continue;
                    }
                    if (expression.expression->operands.size() != 2U) {
                        return reject(
                            CoverageConditionError::MalformedLogicalExpression,
                            current_statement_index,
                            current_expression_index);
                    }
                    if (expression.path.size()
                        >= limits.maximum_expression_nesting) {
                        return reject(CoverageConditionError::ResourceLimit,
                            current_statement_index,
                            current_expression_index);
                    }
                    auto right_path = expression.path;
                    right_path.push_back(CoverageConditionPathStep {
                        *operation, CoverageConditionOperand::Right });
                    expression.path.push_back(CoverageConditionPathStep {
                        *operation, CoverageConditionOperand::Left });
                    pending_expressions.push_back(PendingExpression {
                        &expression.expression->operands[1],
                        expression.depth + 1U,
                        std::move(right_path),
                    });
                    pending_expressions.push_back(PendingExpression {
                        &expression.expression->operands[0],
                        expression.depth + 1U,
                        std::move(expression.path),
                    });
                    continue;
                }

                if (!expression.expression->valid()) {
                    return reject(
                        CoverageConditionError::MalformedLogicalExpression,
                        current_statement_index, current_expression_index);
                }
                if (result.points.size() >= limits.maximum_conditions
                    || expression.path.size()
                        > limits.maximum_path_steps - retained_path_steps) {
                    return reject(CoverageConditionError::ResourceLimit,
                        current_statement_index, current_expression_index);
                }
                retained_path_steps += expression.path.size();
                const auto physical_name
                    = frontend::physical_source(expression.expression->span);
                const auto source = source_by_name.find(physical_name);
                if (source == source_by_name.end()) {
                    return reject(CoverageConditionError::UnknownConditionSource,
                        current_statement_index, current_expression_index);
                }
                const frontend::CodeCoverageSourceSpan span {
                    static_cast<std::uint64_t>(
                        expression.expression->span.begin.offset),
                    static_cast<std::uint64_t>(
                        expression.expression->span.end.offset),
                };
                const auto identity
                    = frontend::make_code_coverage_point_identity(
                        sources[source->second].identity, language,
                        frontend::CodeCoverageConstructKind::AtomicCondition,
                        span);
                if (!identity.ok()) {
                    return reject(CoverageConditionError::InvalidConditionSpan,
                        current_statement_index, current_expression_index);
                }
                const auto key = PointKey {
                    identity.identity->high, identity.identity->low
                };
                if (!point_ids.insert(key).second) {
                    return reject(CoverageConditionError::DuplicatePoint,
                        current_statement_index, current_expression_index);
                }
                result.points.push_back(CoverageConditionPoint {
                    *identity.identity,
                    language,
                    *kind,
                    expression.expression->kind,
                    span,
                    source->second,
                    decision_index,
                    condition_index++,
                    std::move(expression.path),
                });
            }
            ++decision_index;
        }
        result.statement_index = 0U;
        result.expression_index = 0U;
        return result;
    } catch (...) {
        return reject(CoverageConditionError::ResourceLimit);
    }
}

} // namespace fsim::elaboration
