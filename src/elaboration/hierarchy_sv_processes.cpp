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

namespace {

/// A generated continuous assignment of a constant: it runs once at time 0
/// and writes its one driver region from constants alone.
[[nodiscard]] bool constant_continuous_driver(
    const runtime::simir::ProcessProgramView& view)
{
    using namespace runtime::simir;
    if (!view.static_sensitivity().empty() || !view.initialize()
        || view.final() || view.observed() || view.reactive()
        || view.postponed() || view.program_owner()
        || view.driver_regions().size() != 1U
        || view.string_register_count() != 0U
        || view.container_register_count() != 0U
        || !view.debug_locals().empty() || !view.debug_string_locals().empty()
        || !view.debug_container_locals().empty()
        || !view.static_trigger_regions().empty()
        || view.switch_source() || view.switch_target()
        || view.switch_control()
        || view.scheduling_domain() != ProcessSchedulingDomain::systemverilog) {
        return false;
    }
    const auto& operations = view.operations();
    if (operations.empty()) {
        return false;
    }
    const auto* const halt = operation_get_if<Halt>(
        &operations[operations.size() - 1U]);
    if (halt == nullptr || halt->program_exit) {
        return false;
    }
    std::size_t writes { };
    for (std::size_t index = 0U; index + 1U < operations.size(); ++index) {
        const bool supported = visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, WriteUpdate>
                || std::is_same_v<Type, WriteUpdateSlice>) {
                ++writes;
                return true;
            } else {
                return std::is_same_v<Type, DebugPoint>
                    || std::is_same_v<Type, LoadConstant>
                    || std::is_same_v<Type, Concatenate>
                    || std::is_same_v<Type, Extract>
                    || std::is_same_v<Type, Insert>
                    || std::is_same_v<Type, CopyRegister>;
            }
        }, operations[index]);
        if (!supported) {
            return false;
        }
    }
    return writes == 1U;
}

/// One process running the constant continuous drivers of a module instance
/// in order. Each wrote its own driver region once at time 0, which the
/// merged process does too: the values and the time-0 events are the same,
/// and only the order among independent time-0 updates (a race) can differ.
/// Overlapping targets (several drivers of one bit) or differing drive
/// strengths are not merged.
[[nodiscard]] std::optional<runtime::simir::Process> merge_constant_drivers(
    const std::vector<runtime::simir::Process>& drivers)
{
    using namespace runtime::simir;
    if (drivers.size() < 2U) {
        return std::nullopt;
    }
    std::vector<std::tuple<SignalId, std::uint64_t, std::uint64_t>> targets;
    for (const auto& driver : drivers) {
        if (!(driver.drive_strength == drivers.front().drive_strength)
            || driver.language_standard != drivers.front().language_standard
            || driver.compatibility_profile
                != drivers.front().compatibility_profile) {
            return std::nullopt;
        }
        const auto& region = driver.driver_regions.front();
        targets.emplace_back(region.signal,
            region.whole ? 0U : region.offset,
            region.whole ? std::numeric_limits<std::uint64_t>::max()
                         : static_cast<std::uint64_t>(region.offset)
                    + region.width);
    }
    std::ranges::sort(targets);
    for (std::size_t index = 1U; index < targets.size(); ++index) {
        if (std::get<0>(targets[index - 1U]) == std::get<0>(targets[index])
            && std::get<2>(targets[index - 1U])
                > std::get<1>(targets[index])) {
            return std::nullopt;
        }
    }
    Process merged;
    merged.name = drivers.front().name + "_constants";
    merged.language_standard = drivers.front().language_standard;
    merged.compatibility_profile = drivers.front().compatibility_profile;
    merged.drive_strength = drivers.front().drive_strength;
    merged.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    merged.initialize = true;
    std::vector<Operation> operations;
    std::vector<ValueKind> kinds;
    bool any_kinds { };
    RegisterId base { };
    for (const auto& driver : drivers) {
        any_kinds = any_kinds || !driver.register_value_kinds.empty();
    }
    for (const auto& driver : drivers) {
        const auto renumber = [&](RegisterId& reg) { reg += base; };
        for (std::size_t index = 0U; index + 1U < driver.operations.size();
             ++index) {
            auto operation = driver.operations.expanded(index);
            visit_operation([&](auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, LoadConstant>) {
                    renumber(value.destination);
                } else if constexpr (std::is_same_v<Type, Concatenate>) {
                    renumber(value.destination);
                    for (auto& operand : value.operands) {
                        renumber(operand);
                    }
                } else if constexpr (std::is_same_v<Type, Extract>
                    || std::is_same_v<Type, CopyRegister>) {
                    renumber(value.destination);
                    renumber(value.source);
                } else if constexpr (std::is_same_v<Type, Insert>) {
                    renumber(value.destination);
                    renumber(value.target);
                    renumber(value.source);
                } else if constexpr (std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteUpdateSlice>) {
                    renumber(value.source);
                }
            }, operation);
            operations.push_back(std::move(operation));
        }
        if (any_kinds) {
            for (std::size_t reg = 0U; reg < driver.register_count; ++reg) {
                kinds.push_back(reg < driver.register_value_kinds.size()
                        ? driver.register_value_kinds[reg]
                        : ValueKind::logic4);
            }
        }
        base += static_cast<RegisterId>(driver.register_count);
        merged.driver_regions.push_back(driver.driver_regions.front());
        // Expression sizing metadata, in program order.
        for (const auto& profile : driver.expression_profiles) {
            merged.expression_profiles.push_back(profile);
        }
    }
    operations.push_back(Halt { });
    merged.register_count = base;
    merged.operations = OperationList { std::move(operations) };
    if (any_kinds) {
        merged.register_value_kinds = CopyOnWriteVector<ValueKind> {
            std::move(kinds) };
    }
    return merged;
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
    std::size_t& concurrent_order,
    std::vector<semantic::ProcessId>* const deferred_processes)
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
            lowerer.reset_hierarchical_reference_state();
            lowered = lowerer.lower_hir_process(
                process_id,
                source_language,
                path);
            lowerer.set_hir_code_coverage_active(false);
            lowered_here = lowered.has_value();
        }
        if (!lowered && deferred_processes != nullptr
            && lowerer.hierarchical_reference_missed()) {
            // A hierarchical reference may name a child instance that is
            // instantiated after this unit's processes; retry then.
            diagnostics_.resize(diagnostics_before);
            deferred_processes->push_back(process_id);
            continue;
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
            && !lowerer.has_generated_processes()
            && !lowerer.hierarchical_reference_used()) {
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
    // Constant continuous drivers of this instance's generate blocks run as
    // one process (merge_constant_drivers);
    // FSIM_MERGE_CONSTANT_DRIVERS=0 keeps one process each.
    std::vector<runtime::simir::Process> constant_drivers;
    const auto flush_constant_drivers = [&] {
        auto merged = merge_constant_drivers(constant_drivers);
        if (merged) {
            constant_drivers.clear();
            constant_drivers.push_back(std::move(*merged));
        }
        for (auto& process : constant_drivers) {
            process.id = static_cast<ProcessId>(design_.process_count());
            canonicalize_process_operations(process);
            specialization.processes.push_back(process.id);
            design_.append_process_record(std::move(process));
        }
        constant_drivers.clear();
    };
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
            // Code coverage counts each statement: keep their processes.
            if (!program_owner && merge_constant_drivers_
                && coverage_ == nullptr) {
                const auto view = lowered->instance
                    ? ProcessProgramView { *lowered->common, *lowered->instance }
                    : ProcessProgramView { *lowered->process };
                if (constant_continuous_driver(view)) {
                    auto process = view.materialize();
                    process.language_standard = unit.standard;
                    process.compatibility_profile = unit.compatibility_profile;
                    constant_drivers.push_back(std::move(process));
                    append_generated_processes(generated_lowerer);
                    continue;
                }
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
    flush_constant_drivers();
    return true;
}

bool HierarchyBuilder::lower_deferred_systemverilog_processes(
    const semantic::sv::Unit& unit,
    const semantic::SpecializedHirUnit& specialized,
    const std::string& path,
    const frontend::Language source_language,
    const std::vector<semantic::ProcessId>& deferred_processes,
    Lowerer& lowerer,
    const std::size_t specialization_index,
    const std::optional<std::uint32_t> program_owner)
{
    for (const auto process_id : deferred_processes) {
        const auto process = specialized.find_process(process_id);
        const auto process_source = process
                && process->systemverilog != nullptr
            ? process->systemverilog->source
            : unit.source;
        lowerer.reset_hierarchical_reference_state();
        auto lowered = lowerer.lower_hir_process(
            process_id, source_language, path);
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled process could not be lowered from HIR",
                compiled_source_span(*compiled_, process_source));
            return false;
        }
        const bool concurrent_assertion = process
            && process->systemverilog != nullptr
            && process->systemverilog->concurrent_assertion;
        lowered->language_standard = unit.standard;
        lowered->compatibility_profile = unit.compatibility_profile;
        lowered->observed = concurrent_assertion;
        lowered->reactive = !concurrent_assertion
            && program_owner.has_value();
        lowered->program_owner = program_owner;
        canonicalize_process_operations(*lowered);
        auto& specialization
            = design_.specializations_[specialization_index];
        specialization.processes.push_back(lowered->id);
        specialization.semantic_processes.emplace_back(
            lowered->id, process_id);
        design_.append_process_record(std::move(*lowered));
        for (auto& generated : lowerer.take_generated_processes()) {
            generated.language_standard = unit.standard;
            generated.compatibility_profile = unit.compatibility_profile;
            canonicalize_process_operations(generated);
            design_.specializations_[specialization_index]
                .processes.push_back(generated.id);
            design_.append_process_record(std::move(generated));
        }
    }
    return true;
}

} // namespace fsim::elaboration
