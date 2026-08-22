// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_branches.hpp"

#include "fsim/frontend/source.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
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

using PointKey = std::pair<std::uint64_t, std::uint64_t>;

constexpr bool is_language_valid(
    const frontend::CodeCoverageLanguage language) noexcept
{
    switch (language) {
    case frontend::CodeCoverageLanguage::Verilog:
    case frontend::CodeCoverageLanguage::SystemVerilog:
    case frontend::CodeCoverageLanguage::Vhdl:
        return true;
    }
    return false;
}

bool is_statically_true_loop_condition(
    const frontend::Expression& condition) noexcept
{
    return (condition.kind == frontend::ExpressionKind::BooleanLiteral
               && condition.text == "true")
        || (condition.kind == frontend::ExpressionKind::LogicLiteral
            && condition.text == "1'b1");
}

bool is_conditional_loop(const frontend::Statement& statement) noexcept
{
    if (statement.loop_repeat || statement.loop_initial.valid()
        || statement.loop_limit.valid()) {
        return true;
    }
    return statement.condition.valid()
        && !is_statically_true_loop_condition(statement.condition);
}

} // namespace

CoverageBranchResult discover_coverage_branch_points(
    const std::span<const frontend::Statement> statements,
    const frontend::CodeCoverageLanguage language,
    const std::span<const CoverageBranchSource> sources,
    const CoverageBranchLimits limits) noexcept
{
    CoverageBranchResult result;
    const auto reject = [&](const CoverageBranchError error,
                            const std::size_t statement_index = 0U) {
        result.points.clear();
        result.error = error;
        result.statement_index = statement_index;
        return result;
    };

    try {
        if (!is_language_valid(language)) {
            return reject(CoverageBranchError::InvalidLanguage);
        }
        if (sources.size() > limits.maximum_sources
            || statements.size() > limits.maximum_statements) {
            return reject(CoverageBranchError::ResourceLimit);
        }

        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(CoverageBranchError::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(CoverageBranchError::InvalidSourceIdentity);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(CoverageBranchError::DuplicateSourceName);
            }
        }

        std::vector<PendingStatement> pending;
        pending.reserve(
            std::min(statements.size(), limits.maximum_statements));
        for (auto statement = statements.rbegin();
            statement != statements.rend(); ++statement) {
            pending.push_back(PendingStatement { &*statement, 1U });
        }
        std::size_t scheduled = statements.size();
        std::size_t visited { };
        std::set<PointKey> point_ids;

        const auto enqueue
            = [&](const std::span<const frontend::Statement> children,
                  const std::size_t depth) {
                  if (children.size()
                      > limits.maximum_statements - scheduled) {
                      return false;
                  }
                  scheduled += children.size();
                  for (auto child = children.rbegin();
                      child != children.rend(); ++child) {
                      pending.push_back(
                          PendingStatement { &*child, depth });
                  }
                  return true;
              };

        const auto append_arm
            = [&](const frontend::SourceSpan& source_span,
                  const CoverageDecisionKind decision,
                  const frontend::CodeCoverageConstructKind arm,
                  const std::size_t arm_index,
                  const std::size_t statement_index) {
                  if (result.points.size() >= limits.maximum_arms) {
                      return CoverageBranchError::ResourceLimit;
                  }
                  const auto source_name
                      = frontend::physical_source(source_span);
                  const auto source = source_by_name.find(source_name);
                  if (source == source_by_name.end()) {
                      return CoverageBranchError::UnknownArmSource;
                  }
                  const frontend::CodeCoverageSourceSpan span {
                      static_cast<std::uint64_t>(source_span.begin.offset),
                      static_cast<std::uint64_t>(source_span.end.offset),
                  };
                  const auto identity
                      = frontend::make_code_coverage_point_identity(
                          sources[source->second].identity, language, arm,
                          span);
                  if (!identity.ok()) {
                      return CoverageBranchError::InvalidArmSpan;
                  }
                  const auto key = PointKey {
                      identity.identity->high, identity.identity->low
                  };
                  if (!point_ids.insert(key).second) {
                      return CoverageBranchError::DuplicatePoint;
                  }
                  result.points.push_back(CoverageBranchPoint {
                      *identity.identity,
                      language,
                      decision,
                      arm,
                      span,
                      source->second,
                      arm_index,
                  });
                  result.statement_index = statement_index;
                  return CoverageBranchError::None;
              };

        const auto body_span
            = [](const std::vector<frontend::Statement>& body,
                  frontend::SourceSpan& span) {
                  if (body.empty()) {
                      return false;
                  }
                  const auto first = frontend::physical_source(
                      body.front().span);
                  const auto last = frontend::physical_source(
                      body.back().span);
                  if (first != last) {
                      return false;
                  }
                  span = body.front().span;
                  span.end = body.back().span.end;
                  return true;
              };

        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            const auto current_index = visited++;
            if (current.depth > limits.maximum_nesting) {
                return reject(
                    CoverageBranchError::ResourceLimit, current_index);
            }

            const auto append_checked
                = [&](const frontend::SourceSpan& span,
                      const CoverageDecisionKind decision,
                      const frontend::CodeCoverageConstructKind arm,
                      const std::size_t arm_index) {
                      const auto error = append_arm(span, decision, arm,
                          arm_index, current_index);
                      return error == CoverageBranchError::None
                          ? true
                          : (result.error = error, false);
                  };

            if (current.statement->kind == frontend::StatementKind::If) {
                frontend::SourceSpan true_span;
                if (!body_span(current.statement->statements, true_span)) {
                    return reject(CoverageBranchError::MissingExplicitArm,
                        current_index);
                }
                if (!append_checked(true_span, CoverageDecisionKind::If,
                        frontend::CodeCoverageConstructKind::BranchTrueArm,
                        0U)) {
                    return reject(result.error, current_index);
                }
                if (current.statement->else_statements.empty()) {
                    if (!append_checked(current.statement->span,
                            CoverageDecisionKind::If,
                            frontend::CodeCoverageConstructKind::BranchImplicitArm,
                            1U)) {
                        return reject(result.error, current_index);
                    }
                } else {
                    frontend::SourceSpan false_span;
                    if (!body_span(
                            current.statement->else_statements, false_span)) {
                        return reject(CoverageBranchError::MissingExplicitArm,
                            current_index);
                    }
                    if (!append_checked(false_span, CoverageDecisionKind::If,
                            frontend::CodeCoverageConstructKind::BranchFalseArm,
                            1U)) {
                        return reject(result.error, current_index);
                    }
                }
            } else if (current.statement->kind
                == frontend::StatementKind::Case) {
                if (current.statement->case_alternatives.empty()) {
                    return reject(CoverageBranchError::MissingExplicitArm,
                        current_index);
                }
                bool has_default = false;
                for (std::size_t index = 0U;
                    index < current.statement->case_alternatives.size();
                    ++index) {
                    const auto& alternative
                        = current.statement->case_alternatives[index];
                    has_default = has_default || alternative.is_default;
                    const auto kind = alternative.is_default
                        ? frontend::CodeCoverageConstructKind::BranchDefaultArm
                        : frontend::CodeCoverageConstructKind::BranchCaseArm;
                    if (!append_checked(alternative.span,
                            CoverageDecisionKind::Case, kind, index)) {
                        return reject(result.error, current_index);
                    }
                }
                if (!has_default
                    && !append_checked(current.statement->span,
                        CoverageDecisionKind::Case,
                        frontend::CodeCoverageConstructKind::BranchImplicitArm,
                        current.statement->case_alternatives.size())) {
                    return reject(result.error, current_index);
                }
            } else if (current.statement->kind
                    == frontend::StatementKind::Loop
                && is_conditional_loop(*current.statement)) {
                frontend::SourceSpan true_span;
                if (!body_span(current.statement->statements, true_span)) {
                    return reject(CoverageBranchError::MissingExplicitArm,
                        current_index);
                }
                if (!append_checked(true_span, CoverageDecisionKind::Loop,
                        frontend::CodeCoverageConstructKind::BranchTrueArm,
                        0U)
                    || !append_checked(current.statement->span,
                        CoverageDecisionKind::Loop,
                        frontend::CodeCoverageConstructKind::BranchImplicitArm,
                        1U)) {
                    return reject(result.error, current_index);
                }
            }

            for (std::size_t alternative_index
                    = current.statement->case_alternatives.size();
                alternative_index > 0U; --alternative_index) {
                const auto& alternative
                    = current.statement->case_alternatives[
                        alternative_index - 1U];
                if (!enqueue(
                        alternative.statements, current.depth + 1U)) {
                    return reject(CoverageBranchError::ResourceLimit,
                        current_index);
                }
            }
            if (!enqueue(current.statement->else_statements,
                    current.depth + 1U)
                || !enqueue(current.statement->statements,
                    current.depth + 1U)) {
                return reject(
                    CoverageBranchError::ResourceLimit, current_index);
            }
        }
        result.statement_index = 0U;
        return result;
    } catch (...) {
        return reject(CoverageBranchError::ResourceLimit);
    }
}

} // namespace fsim::elaboration
