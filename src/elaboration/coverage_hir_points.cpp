// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_hir_points.hpp"

#include "fsim/elaboration/elaborator.hpp"

#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    bool executable(const semantic::sv::StatementKind kind) noexcept
    {
        using Kind = semantic::sv::StatementKind;
        switch (kind) {
        case Kind::block:
        case Kind::null_statement:
            return false;
        case Kind::assignment:
        case Kind::force:
        case Kind::release:
        case Kind::conditional:
        case Kind::selection:
        case Kind::loop:
        case Kind::break_loop:
        case Kind::continue_loop:
        case Kind::return_statement:
        case Kind::task_call:
        case Kind::assertion:
        case Kind::delay_control:
        case Kind::event_control:
        case Kind::wait_statement:
        case Kind::event_trigger:
        case Kind::fork:
        case Kind::wait_fork:
        case Kind::disable_fork:
        case Kind::disable:
        case Kind::display:
        case Kind::file_close:
        case Kind::file_flush:
        case Kind::file_display:
        case Kind::memory_transfer:
        case Kind::container_method:
        case Kind::monitor_control:
        case Kind::pause:
        case Kind::finish:
        case Kind::exit_program:
        case Kind::procedural_assign:
        case Kind::deassign:
        case Kind::wait_order:
        case Kind::report:
            return true;
        }
        return false;
    }

} // namespace

CoverageHirPointResult discover_hir_module_coverage_points(
    const semantic::CompiledDesign& compiled,
    const SpecializationInfo& specialization,
    const std::span<const CoverageHirSource> sources,
    const std::size_t maximum_statements) noexcept
{
    CoverageHirPointResult result;
    result.draft.specialization = specialization.id;
    const auto reject = [&](const CoverageHirPointError error) {
        result.draft.points.clear();
        result.error = error;
        return result;
    };
    if (specialization.language != frontend::Language::SystemVerilog2017
        || !specialization.source_unit) {
        return result;
    }
    const auto unit = compiled.find_unit(*specialization.source_unit);
    if (!unit || unit->systemverilog == nullptr
        || unit->systemverilog->kind != semantic::sv::UnitKind::module) {
        return result;
    }

    try {
        const auto& model = compiled.semantics;
        const auto& spans = model.source_spans();
        const auto& files = model.source_files();
        const auto& origins = model.origins();
        std::map<std::string_view, std::size_t, std::less<>> source_by_name;
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            source_by_name.emplace(sources[index].inventory.source_name, index);
        }
        std::set<std::pair<std::uint64_t, std::uint64_t>> point_ids;
        const auto source_span = [&](const semantic::SourceSpanId id)
            -> const semantic::SourceSpan* {
            return id.valid() && id.value() < spans.size()
                ? &spans[id.value()] : nullptr;
        };
        const auto append = [&](const semantic::SourceSpan& first,
                                const semantic::SourceSpan& last,
                                const runtime::CodeCoverageMetric metric,
                                const frontend::CodeCoverageConstructKind kind) {
            if (!first.file.valid() || first.file != last.file
                || first.file.value() >= files.size()
                || first.begin.offset >= last.end.offset) {
                return CoverageHirPointError::InvalidSpan;
            }
            if (result.draft.points.size() >= maximum_statements) {
                return CoverageHirPointError::ResourceLimit;
            }
            const auto source = source_by_name.find(
                files[first.file.value()].physical_name);
            if (source == source_by_name.end()) {
                return CoverageHirPointError::InvalidSource;
            }
            const auto source_metric = metric == runtime::CodeCoverageMetric::Branch
                ? frontend::CoverageSourceMetric::Branch
                : frontend::CoverageSourceMetric::Statement;
            if (frontend::coverage_source_exclusion_at(
                    sources[source->second].exclusions,
                    source_metric, first.begin.offset) != nullptr) {
                return CoverageHirPointError::None;
            }
            const frontend::CodeCoverageSourceSpan span {
                first.begin.offset, last.end.offset };
            const auto identity = frontend::make_code_coverage_point_identity(
                sources[source->second].inventory.identity,
                frontend::CodeCoverageLanguage::SystemVerilog,
                kind, span);
            if (!identity.ok()) {
                return CoverageHirPointError::InvalidSpan;
            }
            if (!point_ids.emplace(identity.identity->high,
                    identity.identity->low).second) {
                return CoverageHirPointError::DuplicatePoint;
            }
            result.draft.points.push_back({ *identity.identity, metric,
                source->second, span, first.begin.line });
            return CoverageHirPointError::None;
        };

        std::vector<semantic::StatementId> pending;
        std::size_t scheduled { };
        const auto enqueue = [&](const std::span<const semantic::StatementId> ids) {
            if (ids.size() > maximum_statements - scheduled) {
                return false;
            }
            scheduled += ids.size();
            for (auto item = ids.rbegin(); item != ids.rend(); ++item) {
                pending.push_back(*item);
            }
            return true;
        };
        std::set<std::uint32_t> visited_processes;
        for (const auto& [runtime_process, semantic_process] :
            specialization.semantic_processes) {
            (void)runtime_process;
            if (!visited_processes.insert(semantic_process.value()).second) {
                continue;
            }
            const auto process = compiled.find_process(semantic_process);
            if (!process || process->systemverilog == nullptr) {
                return reject(CoverageHirPointError::InvalidProcess);
            }
            const auto& owner = *process->systemverilog;
            // Concurrent assertion monitor processes are synthesized from
            // source actions and are not module procedural source processes.
            if (owner.concurrent_assertion) {
                continue;
            }
            if (!owner.origin.valid() || owner.origin.value() >= origins.size()) {
                return reject(CoverageHirPointError::InvalidProcess);
            }
            if (origins[owner.origin.value()].kind
                != semantic::OriginKind::parsed) {
                continue;
            }
            if (!enqueue(owner.statements)) {
                return reject(CoverageHirPointError::ResourceLimit);
            }
            while (!pending.empty()) {
                const auto id = pending.back();
                pending.pop_back();
                const auto record = compiled.find_statement(id);
                if (!record || record->systemverilog == nullptr) {
                    return reject(CoverageHirPointError::InvalidStatement);
                }
                const auto& statement = *record->systemverilog;
                if (!statement.origin.valid()
                    || statement.origin.value() >= origins.size()) {
                    return reject(CoverageHirPointError::InvalidStatement);
                }
                const bool parsed = origins[statement.origin.value()].kind
                    == semantic::OriginKind::parsed;
                const auto* own_span = source_span(statement.source);
                if (parsed && own_span == nullptr) {
                    return reject(CoverageHirPointError::InvalidSpan);
                }
                if (parsed && executable(statement.kind)) {
                    const auto error = append(*own_span, *own_span,
                        runtime::CodeCoverageMetric::Statement,
                        frontend::CodeCoverageConstructKind::Statement);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                }
                if (parsed && statement.kind == semantic::sv::StatementKind::conditional) {
                    const auto append_arm = [&](const std::span<const semantic::StatementId> body,
                                                const frontend::CodeCoverageConstructKind kind) {
                        if (body.empty()) {
                            return CoverageHirPointError::MissingBranchArm;
                        }
                        const auto first = compiled.find_statement(body.front());
                        const auto last = compiled.find_statement(body.back());
                        if (!first || !last || first->systemverilog == nullptr
                            || last->systemverilog == nullptr) {
                            return CoverageHirPointError::InvalidStatement;
                        }
                        const auto* begin = source_span(first->systemverilog->source);
                        const auto* end = source_span(last->systemverilog->source);
                        if (begin == nullptr || end == nullptr) {
                            return CoverageHirPointError::InvalidSpan;
                        }
                        return append(*begin, *end,
                            runtime::CodeCoverageMetric::Branch, kind);
                    };
                    auto error = append_arm(statement.statements,
                        frontend::CodeCoverageConstructKind::BranchTrueArm);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                    error = statement.else_statements.empty()
                        ? append(*own_span, *own_span,
                            runtime::CodeCoverageMetric::Branch,
                            frontend::CodeCoverageConstructKind::BranchImplicitArm)
                        : append_arm(statement.else_statements,
                            frontend::CodeCoverageConstructKind::BranchFalseArm);
                    if (error != CoverageHirPointError::None) {
                        return reject(error);
                    }
                }
                for (auto alternative = statement.case_alternatives.rbegin();
                    alternative != statement.case_alternatives.rend();
                    ++alternative) {
                    if (!enqueue(alternative->statements)) {
                        return reject(CoverageHirPointError::ResourceLimit);
                    }
                }
                if (!enqueue(statement.loop_updates)
                    || !enqueue(statement.else_statements)
                    || !enqueue(statement.statements)) {
                    return reject(CoverageHirPointError::ResourceLimit);
                }
            }
        }
        return result;
    } catch (const std::bad_alloc&) {
        return reject(CoverageHirPointError::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(CoverageHirPointError::ResourceLimit);
    }
}

} // namespace fsim::elaboration
