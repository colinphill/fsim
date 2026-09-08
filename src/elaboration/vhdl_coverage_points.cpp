// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vhdl_coverage_points.hpp"

#include "fsim/frontend/source.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <variant>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    struct PendingStatement {
        const frontend::Statement* statement { };
        std::size_t depth { };
    };

    struct PendingCallable {
        std::variant<const frontend::FunctionDeclaration*,
            const frontend::ProcedureDeclaration*>
            declaration;
        std::size_t depth { };
    };

    using PointKey = std::pair<std::uint64_t, std::uint64_t>;

    constexpr bool is_vhdl_standard_valid(
        const frontend::VhdlStandard standard) noexcept
    {
        switch (standard) {
        case frontend::VhdlStandard::Vhdl1987:
        case frontend::VhdlStandard::Vhdl1993:
        case frontend::VhdlStandard::Vhdl2000:
        case frontend::VhdlStandard::Vhdl2002:
        case frontend::VhdlStandard::Vhdl2008:
        case frontend::VhdlStandard::Vhdl2019:
            return true;
        }
        return false;
    }

} // namespace

VhdlCoveragePointResult discover_vhdl_statement_points(
    const std::span<const frontend::Statement> statements,
    const frontend::Language language,
    const frontend::VhdlStandard standard,
    const std::span<const VhdlCoverageSource> sources,
    const VhdlCoveragePointLimits limits) noexcept
{
    VhdlCoveragePointResult result;
    const auto reject = [&](const VhdlCoveragePointError error,
                            const std::size_t statement_index = 0U) {
        result.points.clear();
        result.exclusions.clear();
        result.error = error;
        result.statement_index = statement_index;
        return result;
    };

    try {
        if (language != frontend::Language::Vhdl2008) {
            return reject(VhdlCoveragePointError::InvalidLanguage);
        }
        if (!is_vhdl_standard_valid(standard)) {
            return reject(VhdlCoveragePointError::InvalidStandard);
        }
        if (sources.size() > limits.maximum_sources
            || statements.size() > limits.maximum_statements) {
            return reject(VhdlCoveragePointError::ResourceLimit);
        }

        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        std::vector<frontend::CoverageSourceControlResult> source_controls;
        source_controls.reserve(sources.size());
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(VhdlCoveragePointError::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(VhdlCoveragePointError::InvalidSourceIdentity);
            }
            if (!source.source_text.empty()
                && (source.source_text.size()
                        != source.identity.content_bytes
                    || support::Sha256::digest(source.source_text)
                        != source.identity.content_digest)) {
                return reject(
                    VhdlCoveragePointError::InvalidSourceIdentity, index);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(VhdlCoveragePointError::DuplicateSourceName);
            }
            source_controls.push_back(
                source.source_text.empty()
                    ? frontend::CoverageSourceControlResult { }
                    : frontend::parse_coverage_source_controls(
                          source.source_text, language));
            if (!source_controls.back().ok()) {
                return reject(
                    VhdlCoveragePointError::InvalidSourceControl, index);
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

        const auto enqueue_block_callables
            = [&](const frontend::Statement& block,
                  const std::size_t depth) {
                  std::vector<PendingCallable> callables;
                  callables.reserve(
                      block.functions.size() + block.procedures.size());
                  for (const auto& function : block.functions) {
                      callables.push_back(PendingCallable { &function, depth });
                  }
                  for (const auto& procedure : block.procedures) {
                      callables.push_back(PendingCallable { &procedure, depth });
                  }
                  std::vector<PendingStatement> bodies;
                  std::size_t visited_callables { };
                  while (!callables.empty()) {
                      if (++visited_callables > limits.maximum_statements) {
                          return false;
                      }
                      const auto callable = callables.back();
                      callables.pop_back();
                      if (callable.depth > limits.maximum_nesting) {
                          return false;
                      }
                      const auto append = [&](const auto* declaration) {
                          if (declaration->statements.size()
                              > limits.maximum_statements - scheduled) {
                              return false;
                          }
                          scheduled += declaration->statements.size();
                          for (const auto& statement : declaration->statements) {
                              bodies.push_back(PendingStatement {
                                  &statement, callable.depth });
                          }
                          if (declaration->functions.size()
                                  > limits.maximum_statements
                                      - callables.size()
                              || declaration->procedures.size()
                                  > limits.maximum_statements
                                      - callables.size()
                                      - declaration->functions.size()) {
                              return false;
                          }
                          for (const auto& function : declaration->functions) {
                              callables.push_back(PendingCallable {
                                  &function, callable.depth + 1U });
                          }
                          for (const auto& procedure : declaration->procedures) {
                              callables.push_back(PendingCallable {
                                  &procedure, callable.depth + 1U });
                          }
                          return true;
                      };
                      if (!std::visit(append, callable.declaration)) {
                          return false;
                      }
                  }
                  std::ranges::sort(bodies, [](const auto& left,
                                                const auto& right) {
                      const auto left_source
                          = frontend::physical_source(left.statement->span);
                      const auto right_source
                          = frontend::physical_source(right.statement->span);
                      return left_source != right_source
                          ? left_source < right_source
                          : left.statement->span.begin.offset
                              < right.statement->span.begin.offset;
                  });
                  for (auto body = bodies.rbegin(); body != bodies.rend();
                      ++body) {
                      pending.push_back(*body);
                  }
                  return true;
              };

        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            const auto current_index = visited++;
            if (current.depth > limits.maximum_nesting) {
                return reject(
                    VhdlCoveragePointError::ResourceLimit, current_index);
            }
            for (std::size_t alternative_index
                = current.statement->case_alternatives.size();
                alternative_index > 0U; --alternative_index) {
                const auto& alternative
                    = current.statement->case_alternatives[alternative_index - 1U];
                if (!enqueue(
                        alternative.statements, current.depth + 1U)) {
                    return reject(VhdlCoveragePointError::ResourceLimit,
                        current_index);
                }
            }
            if (!enqueue(current.statement->else_statements,
                    current.depth + 1U)
                || !enqueue(current.statement->statements,
                    current.depth + 1U)
                || (current.statement->kind == frontend::StatementKind::Block
                    && !enqueue_block_callables(
                        *current.statement, current.depth + 1U))) {
                return reject(
                    VhdlCoveragePointError::ResourceLimit, current_index);
            }

            if (!is_executable_vhdl_statement_kind(
                    current.statement->kind)) {
                continue;
            }
            const auto physical_name = frontend::physical_source(
                current.statement->span);
            const auto source = source_by_name.find(physical_name);
            if (source == source_by_name.end()) {
                return reject(
                    VhdlCoveragePointError::UnknownStatementSource,
                    current_index);
            }
            const frontend::CodeCoverageSourceSpan span {
                static_cast<std::uint64_t>(
                    current.statement->span.begin.offset),
                static_cast<std::uint64_t>(
                    current.statement->span.end.offset),
            };
            const auto* source_exclusion
                = frontend::coverage_source_exclusion_at(
                    source_controls[source->second].exclusions,
                    frontend::CoverageSourceMetric::Statement,
                    static_cast<std::size_t>(span.begin_offset));
            const auto identity
                = frontend::make_code_coverage_point_identity(
                    sources[source->second].identity,
                    frontend::CodeCoverageLanguage::Vhdl,
                    frontend::CodeCoverageConstructKind::Statement, span);
            if (!identity.ok()) {
                return reject(VhdlCoveragePointError::InvalidStatementSpan,
                    current_index);
            }
            const auto key = PointKey {
                identity.identity->high, identity.identity->low
            };
            if (!point_ids.insert(key).second) {
                return reject(
                    VhdlCoveragePointError::DuplicatePoint, current_index);
            }
            VhdlStatementCoveragePoint point {
                *identity.identity,
                current.statement->kind,
                span,
                source->second,
                static_cast<std::uint64_t>(
                    current.statement->span.begin.line),
            };
            if (source_exclusion) {
                result.exclusions.push_back(
                    { std::move(point), source_exclusion->reason });
            } else {
                result.points.push_back(std::move(point));
            }
        }
        return result;
    } catch (...) {
        return reject(VhdlCoveragePointError::ResourceLimit);
    }
}

} // namespace fsim::elaboration
