// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    frontend::SourceSpan compiled_source_span(
        const semantic::CompiledDesign& compiled,
        const semantic::SourceSpanId source)
    {
        frontend::SourceSpan result;
        const auto& spans = compiled.semantics.source_spans();
        if (!source.valid() || source.value() >= spans.size()) {
            return result;
        }
        const auto& span = spans[source.value()];
        result.source_name = span.logical_name;
        result.begin = {
            static_cast<std::size_t>(span.begin.offset),
            span.begin.line,
            span.begin.column,
        };
        result.end = {
            static_cast<std::size_t>(span.end.offset),
            span.end.line,
            span.end.column,
        };
        const auto& files = compiled.semantics.source_files();
        if (span.file.valid() && span.file.value() < files.size()) {
            result.physical_source_name = files[span.file.value()].physical_name;
        }
        return result;
    }

} // namespace

bool HierarchyBuilder::lower_compiled_systemverilog_processes(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    const std::string& path,
    const frontend::Language source_language,
    const std::vector<semantic::StatementId>&
        active_concurrent_statements,
    const std::vector<semantic::ProcessId>& active_processes,
    const std::vector<hierarchy_sv_generate_detail::Occurrence>&
        generate_occurrences,
    std::vector<SystemVerilogHirMaterialization>&
        generated_materializations,
    Lowerer& lowerer,
    std::vector<Process>& clocking_processes,
    SpecializationInfo& specialization,
    const std::optional<std::uint32_t> program_owner,
    std::size_t& concurrent_order)
{
    std::unordered_map<const semantic::sv::GenerateRegion*,
        std::vector<semantic::DeclarationId>> generated_signal_declarations;
    const auto collect_generate_declarations = [&]
        (const auto& self, const semantic::sv::GenerateRegion& generate,
            const std::vector<semantic::DeclarationId>& inherited)
        -> void {
        auto declarations = inherited;
        for (const auto declaration : generate.declarations) {
            if (std::ranges::find(declarations, declaration)
                == declarations.end()) {
                declarations.push_back(declaration);
            }
        }
        generated_signal_declarations.emplace(&generate, declarations);
        for (const auto& nested : generate.nested) {
            self(self, nested, declarations);
        }
    };
    const std::vector<semantic::DeclarationId> unit_declarations
        = unit.declarations;
    for (const auto& generate : unit.generates) {
        collect_generate_declarations(
            collect_generate_declarations, generate, unit_declarations);
    }

    const auto append_generated_processes = [&](Lowerer& source) {
        auto generated_processes = source.take_generated_processes();
        record_lowering_census(
            systemverilog_lowerer_generated_processes_,
            generated_processes.size());
        for (auto& generated : generated_processes) {
            generated.language_standard = unit.standard;
            generated.compatibility_profile
                = unit.compatibility_profile;
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            canonicalize_process_operations(generated);
            specialization.processes.push_back(generated.id);
            design_.append_process_record(std::move(generated));
        }
    };

    record_lowering_census(
        systemverilog_clocking_process_occurrences_,
        clocking_processes.size());
    for (auto& process : clocking_processes) {
        process.id = static_cast<ProcessId>(design_.process_count());
        process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        process.language_standard = unit.standard;
        process.compatibility_profile = unit.compatibility_profile;
        process.program_owner = program_owner;
        canonicalize_process_operations(process);
        specialization.processes.push_back(process.id);
        design_.append_process_record(std::move(process));
    }

    for (const auto statement_id : active_concurrent_statements) {
        const auto statement = specialized.find_statement(statement_id);
        const auto statement_source = statement
                && statement->systemverilog != nullptr
                ? statement->systemverilog->source
            : unit.source;
        const auto order = concurrent_order++;
        auto lowered = lower_cached_systemverilog_concurrent_statement(
            unit, specialized, lowerer, statement_id, source_language,
            unit.declarations, path, order, program_owner);
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled concurrent statement could not be lowered "
                "from HIR",
                compiled_source_span(*compiled_, statement_source));
            return false;
        }
        if (lowered->instance) {
            auto& instance = *lowered->instance;
            instance.reactive = program_owner.has_value();
            instance.program_owner = program_owner;
            canonicalize_process_operations(lowered->common, instance);
            specialization.processes.push_back(instance.id);
            design_.append_process_instance_record(
                std::move(lowered->common), std::move(instance));
        } else {
            auto& process = *lowered->process;
            process.language_standard = unit.standard;
            process.compatibility_profile = unit.compatibility_profile;
            process.reactive = program_owner.has_value();
            process.program_owner = program_owner;
            canonicalize_process_operations(process);
            specialization.processes.push_back(process.id);
            design_.append_process_record(std::move(process));
        }
        append_generated_processes(lowerer);
    }
    // Preserve the established elaboration order: concurrent drivers are
    // initialized before lexical initial/always processes.  Besides
    // keeping process identities deterministic across the HIR cutover,
    // this lets a zero-delay continuous assignment observe its source's
    // initial value before a time-zero procedural assignment updates it.
    for (const auto process_id : active_processes) {
        const auto process = specialized.find_process(process_id);
        const auto process_source = process
                && process->systemverilog != nullptr
            ? process->systemverilog->source
            : unit.source;
        const bool concurrent_assertion = process
            && process->systemverilog != nullptr
            && process->systemverilog->concurrent_assertion;
        lowerer.diagnose_hir_systemverilog_file_process(
            process_id);
        const auto& origins = compiled_->semantics.origins();
        const bool parsed_process = process
            && process->systemverilog != nullptr
            && process->systemverilog->origin.valid()
            && process->systemverilog->origin.value() < origins.size()
                && origins[process->systemverilog->origin.value()].kind
                    == semantic::OriginKind::parsed;
        record_lowering_census(
            systemverilog_ordinary_process_template_occurrences_);
        std::optional<SystemVerilogProcessReplay> replay;
        if (process && process->systemverilog != nullptr) {
            replay = replay_systemverilog_process_template(
                unit,
                specialized,
                *process->systemverilog,
                lowerer,
                unit.declarations,
                source_language,
                false,
                path,
                path,
                program_owner);
        }
        if (replay) {
            const auto runtime_process_id = replay->instance.id;
            specialization.processes.push_back(runtime_process_id);
            specialization.semantic_processes.emplace_back(
                runtime_process_id, process_id);
            design_.append_process_instance_record(
                std::move(replay->common), std::move(replay->instance));
            append_generated_processes(lowerer);
            continue;
        }
        std::optional<Process> lowered;
        bool lowered_here { };
        std::size_t diagnostics_before { };
        std::uint32_t invocation_before { };
        if (!lowered) {
            lowerer.set_hir_code_coverage_active(parsed_process
                && !concurrent_assertion
                && source_language
                    == frontend::Language::SystemVerilog2017);
            record_lowering_census(
                systemverilog_ordinary_process_template_lower_requests_);
            record_lowering_census(
                systemverilog_process_lower_requests_);
            diagnostics_before = diagnostics_.size();
            invocation_before
                = lowerer.next_hir_callable_invocation_identity();
            lowered = lowerer.lower_hir_process(
                process_id,
                source_language,
                path);
            lowerer.set_hir_code_coverage_active(false);
            lowered_here = lowered.has_value();
        }
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled process could not be lowered from HIR",
                compiled_source_span(*compiled_, process_source));
            return false;
        }
        lowered->language_standard = unit.standard;
        lowered->compatibility_profile = unit.compatibility_profile;
        lowered->observed = concurrent_assertion;
        lowered->reactive = !concurrent_assertion
            && program_owner.has_value();
        lowered->program_owner = program_owner;
        bool remembered_template { };
        const auto invocation_after = lowered_here
            ? lowerer.next_hir_callable_invocation_identity()
            : invocation_before;
        if (lowered_here && process
            && process->systemverilog != nullptr
            && diagnostics_.size() == diagnostics_before
            && invocation_after >= invocation_before
            && !lowerer.has_generated_processes()) {
            remembered_template
                = remember_systemverilog_process_template(
                    unit,
                    specialized,
                    *process->systemverilog,
                    lowerer,
                    unit.declarations,
                    source_language,
                    false,
                    path,
                    path,
                    invocation_before,
                    invocation_after,
                    *lowered);
        }
        // The first cached occurrence must keep the same immutable body as
        // the prototype copied by remember_systemverilog_process_template.
        // The generic canonicalizer uses a mutable OperationList iterator,
        // which would detach that first occurrence from the cached body.
        if (!remembered_template) {
            canonicalize_process_operations(*lowered);
        }
        specialization.processes.push_back(lowered->id);
        specialization.semantic_processes.emplace_back(
            lowered->id, process_id);
        design_.append_process_record(std::move(*lowered));
        append_generated_processes(lowerer);
    }
    for (std::size_t index = 0U;
        index < generate_occurrences.size(); ++index) {
        const auto& occurrence = generate_occurrences[index];
        auto& materialization = generated_materializations[index];
        Lowerer generated_lowerer {
            design_,
            materialization.signals,
            materialization.read_only_signals,
            materialization.string_objects,
            materialization.read_only_strings,
            materialization.container_objects,
            materialization.read_only_containers,
            diagnostics_,
        };
        generated_lowerer.set_hir_container_declaration_bindings(
            &materialization.container_declaration_bindings);
        generated_lowerer.set_specialized_hir_unit(
            occurrence.specialization.get());
        generated_lowerer.set_hir_code_coverage_context(
            coverage_, unit.kind == semantic::sv::UnitKind::module
                && source_language == frontend::Language::SystemVerilog2017);
        generated_lowerer.set_systemverilog_interface_handles(
            &systemverilog_interface_handles_);
        generated_lowerer.set_systemverilog_program_owner(
            program_owner);
        auto generated_concurrent_statements
            = std::span<const semantic::StatementId> { };
        if (unit.kind != semantic::sv::UnitKind::program) {
            generated_concurrent_statements
                = occurrence.region->concurrent_statements;
        }
        for (const auto statement_id : generated_concurrent_statements) {
            const auto statement = occurrence.specialization
                                       ->find_statement(statement_id);
            const auto statement_source = statement
                    && statement->systemverilog != nullptr
                ? statement->systemverilog->source
                : unit.source;
            const auto declaration_bindings
                = generated_signal_declarations.find(occurrence.region);
            if (declaration_bindings
                == generated_signal_declarations.end()) {
                return false;
            }
            record_lowering_census(
                systemverilog_generated_concurrent_occurrences_);
            const auto replayed_before = lowering_census_enabled_
                ? systemverilog_concurrent_templates_replayed_ : 0U;
            auto lowered = lower_cached_systemverilog_concurrent_statement(
                unit, *occurrence.specialization, generated_lowerer,
                statement_id, source_language,
                declaration_bindings->second, occurrence.path,
                concurrent_order++, program_owner);
            if (lowering_census_enabled_
                && systemverilog_concurrent_templates_replayed_
                    != replayed_before) {
                record_lowering_census(
                    systemverilog_generated_concurrent_replays_);
            }
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated concurrent statement could not "
                    "be lowered from HIR",
                    compiled_source_span(
                        *compiled_, statement_source));
                return false;
            }
            if (lowered->instance) {
                auto& instance = *lowered->instance;
                instance.reactive = program_owner.has_value();
                instance.program_owner = program_owner;
                canonicalize_process_operations(
                    lowered->common, instance);
                specialization.processes.push_back(instance.id);
                design_.append_process_instance_record(
                    std::move(lowered->common), std::move(instance));
            } else {
                auto& process = *lowered->process;
                process.language_standard = unit.standard;
                process.compatibility_profile
                    = unit.compatibility_profile;
                process.reactive = program_owner.has_value();
                process.program_owner = program_owner;
                canonicalize_process_operations(process);
                specialization.processes.push_back(process.id);
                design_.append_process_record(std::move(process));
            }
            append_generated_processes(generated_lowerer);
        }
        for (const auto process_id : occurrence.region->processes) {
            const auto process
                = occurrence.specialization->find_process(process_id);
            const auto process_source = process
                    && process->systemverilog != nullptr
                ? process->systemverilog->source
                : unit.source;
            const bool concurrent_assertion = process
                && process->systemverilog != nullptr
                && process->systemverilog->concurrent_assertion;
            const auto& origins = compiled_->semantics.origins();
            const bool parsed_process = process
                && process->systemverilog != nullptr
                && process->systemverilog->origin.valid()
                && process->systemverilog->origin.value() < origins.size()
                && origins[process->systemverilog->origin.value()].kind
                    == semantic::OriginKind::parsed;
            record_lowering_census(
                systemverilog_generated_process_occurrences_);
            const auto declaration_bindings
                = generated_signal_declarations.find(occurrence.region);
            std::optional<SystemVerilogProcessReplay> replay;
            if (process && process->systemverilog != nullptr
                && declaration_bindings
                    != generated_signal_declarations.end()) {
                replay = replay_systemverilog_process_template(
                    unit,
                    *occurrence.specialization,
                    *process->systemverilog,
                    generated_lowerer,
                    declaration_bindings->second,
                    source_language,
                    true,
                    path,
                    occurrence.path,
                    program_owner);
            }
            if (replay) {
                if (process->systemverilog->name == "<process>") {
                    replay->instance.name = occurrence.path;
                }
                const auto runtime_process_id = replay->instance.id;
                specialization.processes.push_back(runtime_process_id);
                specialization.semantic_processes.emplace_back(
                    runtime_process_id, process_id);
                design_.append_process_instance_record(
                    std::move(replay->common), std::move(replay->instance));
                append_generated_processes(generated_lowerer);
                continue;
            }
            std::optional<Process> lowered;
            bool lowered_here { };
            std::size_t diagnostics_before { };
            std::uint32_t invocation_before { };
            if (!lowered) {
                generated_lowerer.set_hir_code_coverage_active(
                    parsed_process
                    && !concurrent_assertion
                    && source_language
                        == frontend::Language::SystemVerilog2017);
                record_lowering_census(
                    systemverilog_generated_process_lower_requests_);
                diagnostics_before = diagnostics_.size();
                invocation_before
                    = generated_lowerer
                        .next_hir_callable_invocation_identity();
                lowered = generated_lowerer.lower_hir_process(
                    process_id, source_language, occurrence.path);
                generated_lowerer.set_hir_code_coverage_active(false);
                lowered_here = lowered.has_value();
            }
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated process could not be lowered "
                    "from HIR",
                    compiled_source_span(
                        *compiled_, process_source));
                return false;
            }
            if (process && process->systemverilog != nullptr
                && process->systemverilog->name == "<process>") {
                lowered->name = occurrence.path;
            }
            lowered->language_standard = unit.standard;
            lowered->compatibility_profile
                = unit.compatibility_profile;
            lowered->observed = concurrent_assertion;
            lowered->reactive = !concurrent_assertion
                && program_owner.has_value();
            lowered->program_owner = program_owner;
            bool remembered_template { };
            const auto invocation_after = lowered_here
                ? generated_lowerer.next_hir_callable_invocation_identity()
                : invocation_before;
            if (lowered_here && process
                && process->systemverilog != nullptr
                && declaration_bindings
                    != generated_signal_declarations.end()
                && diagnostics_.size() == diagnostics_before
                && invocation_after >= invocation_before
                && !generated_lowerer.has_generated_processes()) {
                remembered_template
                    = remember_systemverilog_process_template(
                        unit,
                        *occurrence.specialization,
                        *process->systemverilog,
                        generated_lowerer,
                        declaration_bindings->second,
                        source_language,
                        true,
                        path,
                        occurrence.path,
                        invocation_before,
                        invocation_after,
                        *lowered);
            }
            // Both a successful replay and its first remembered occurrence
            // must retain the prototype's immutable operation body. The
            // generic canonicalizer traverses mutable OperationList
            // iterators and would detach the original from that body too.
            if (!remembered_template) {
                canonicalize_process_operations(*lowered);
            }
            specialization.processes.push_back(lowered->id);
            specialization.semantic_processes.emplace_back(
                lowered->id, process_id);
            design_.append_process_record(std::move(*lowered));
            append_generated_processes(generated_lowerer);
        }
    }
    return true;
}

} // namespace fsim::elaboration
