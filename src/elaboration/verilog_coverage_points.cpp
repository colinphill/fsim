// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/verilog_coverage_points.hpp"

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

    using PointKey = std::pair<std::uint64_t, std::uint64_t>;

    std::optional<frontend::CodeCoverageLanguage> coverage_language(
        const frontend::Language language) noexcept
    {
        switch (language) {
        case frontend::Language::Verilog2005:
            return frontend::CodeCoverageLanguage::Verilog;
        case frontend::Language::SystemVerilog2017:
            return frontend::CodeCoverageLanguage::SystemVerilog;
        case frontend::Language::Vhdl2008:
            return std::nullopt;
        }
        return std::nullopt;
    }

} // namespace

bool is_executable_verilog_statement_kind(
    const frontend::StatementKind kind) noexcept
{
    switch (kind) {
    case frontend::StatementKind::Block:
    case frontend::StatementKind::Null:
        return false;
    case frontend::StatementKind::Assignment:
    case frontend::StatementKind::Force:
    case frontend::StatementKind::Release:
    case frontend::StatementKind::If:
    case frontend::StatementKind::Case:
    case frontend::StatementKind::Loop:
    case frontend::StatementKind::Break:
    case frontend::StatementKind::Continue:
    case frontend::StatementKind::Return:
    case frontend::StatementKind::TaskCall:
    case frontend::StatementKind::ProcedureCall:
    case frontend::StatementKind::Assert:
    case frontend::StatementKind::Delay:
    case frontend::StatementKind::WaitOn:
    case frontend::StatementKind::WaitUntil:
    case frontend::StatementKind::EventTrigger:
    case frontend::StatementKind::Fork:
    case frontend::StatementKind::WaitFork:
    case frontend::StatementKind::DisableFork:
    case frontend::StatementKind::Disable:
    case frontend::StatementKind::Display:
    case frontend::StatementKind::FileClose:
    case frontend::StatementKind::FileFlush:
    case frontend::StatementKind::FileDisplay:
    case frontend::StatementKind::MemoryLoad:
    case frontend::StatementKind::ContainerMethod:
    case frontend::StatementKind::MonitorControl:
    case frontend::StatementKind::Report:
    case frontend::StatementKind::Pause:
    case frontend::StatementKind::Finish:
    case frontend::StatementKind::Exit:
    case frontend::StatementKind::ProceduralAssign:
    case frontend::StatementKind::Deassign:
    case frontend::StatementKind::WaitOrder:
        return true;
    }
    return false;
}

VerilogCoveragePointResult discover_verilog_statement_points(
    const std::span<const frontend::Statement> statements,
    const frontend::Language language,
    const std::span<const VerilogCoverageSource> sources,
    const VerilogCoveragePointLimits limits) noexcept
{
    VerilogCoveragePointResult result;
    const auto reject = [&](const VerilogCoveragePointError error,
                            const std::size_t statement_index = 0U) {
        result.points.clear();
        result.exclusions.clear();
        result.error = error;
        result.statement_index = statement_index;
        return result;
    };

    try {
        const auto point_language = coverage_language(language);
        if (!point_language) {
            return reject(VerilogCoveragePointError::InvalidLanguage);
        }
        if (sources.size() > limits.maximum_sources
            || statements.size() > limits.maximum_statements) {
            return reject(VerilogCoveragePointError::ResourceLimit);
        }

        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        std::vector<frontend::CoverageSourceControlResult> source_controls;
        source_controls.reserve(sources.size());
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (source.source_name.empty()) {
                return reject(VerilogCoveragePointError::EmptySourceName);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    source.identity)) {
                return reject(
                    VerilogCoveragePointError::InvalidSourceIdentity);
            }
            if (!source.source_text.empty()
                && (source.source_text.size()
                        != source.identity.content_bytes
                    || support::Sha256::digest(source.source_text)
                        != source.identity.content_digest)) {
                return reject(
                    VerilogCoveragePointError::InvalidSourceIdentity, index);
            }
            if (!source_by_name.emplace(source.source_name, index).second) {
                return reject(
                    VerilogCoveragePointError::DuplicateSourceName);
            }
            source_controls.push_back(
                source.source_text.empty()
                    ? frontend::CoverageSourceControlResult { }
                    : frontend::parse_coverage_source_controls(
                          source.source_text, language));
            if (!source_controls.back().ok()) {
                return reject(
                    VerilogCoveragePointError::InvalidSourceControl, index);
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

        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            const auto current_index = visited++;
            if (current.depth > limits.maximum_nesting) {
                return reject(
                    VerilogCoveragePointError::ResourceLimit, current_index);
            }

            // Push in reverse traversal order: ordinary body, else body, then
            // case alternatives in source order precede the next sibling.
            for (std::size_t alternative_index
                = current.statement->case_alternatives.size();
                alternative_index > 0U; --alternative_index) {
                const auto& alternative
                    = current.statement->case_alternatives[alternative_index - 1U];
                if (!enqueue(
                        alternative.statements, current.depth + 1U)) {
                    return reject(VerilogCoveragePointError::ResourceLimit,
                        current_index);
                }
            }
            if (!enqueue(current.statement->else_statements,
                    current.depth + 1U)
                || !enqueue(current.statement->statements,
                    current.depth + 1U)) {
                return reject(
                    VerilogCoveragePointError::ResourceLimit, current_index);
            }

            if (!is_executable_verilog_statement_kind(
                    current.statement->kind)) {
                continue;
            }
            const auto physical_name = frontend::physical_source(
                current.statement->span);
            const auto source = source_by_name.find(physical_name);
            if (source == source_by_name.end()) {
                return reject(
                    VerilogCoveragePointError::UnknownStatementSource,
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
                    sources[source->second].identity, *point_language,
                    frontend::CodeCoverageConstructKind::Statement, span);
            if (!identity.ok()) {
                return reject(
                    VerilogCoveragePointError::InvalidStatementSpan,
                    current_index);
            }
            const auto key = PointKey {
                identity.identity->high, identity.identity->low
            };
            if (!point_ids.insert(key).second) {
                return reject(
                    VerilogCoveragePointError::DuplicatePoint, current_index);
            }
            VerilogStatementCoveragePoint point {
                *identity.identity,
                *point_language,
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
        return reject(VerilogCoveragePointError::ResourceLimit);
    }
}

} // namespace fsim::elaboration
