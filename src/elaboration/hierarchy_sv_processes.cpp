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

void HierarchyBuilder::publish_systemverilog_concurrent_occurrence(
    SystemVerilogConcurrentProcessOccurrence& lowered,
    const semantic::sv::Unit& unit,
    SpecializationInfo& specialization,
    const std::optional<std::uint32_t> program_owner)
{
    if (lowered.instance) {
        auto& instance = *lowered.instance;
        instance.reactive = program_owner.has_value();
        instance.program_owner = program_owner;
        canonicalize_process_operations(lowered.common, instance);
        specialization.processes.push_back(instance.id);
        design_.append_process_instance_record(
            std::move(lowered.common), std::move(instance));
        return;
    }
    auto& process = *lowered.process;
    process.language_standard = unit.standard;
    process.compatibility_profile = unit.compatibility_profile;
    process.reactive = program_owner.has_value();
    process.program_owner = program_owner;
    canonicalize_process_operations(process);
    specialization.processes.push_back(process.id);
    design_.append_process_record(std::move(process));
}

const HierarchyBuilder::SystemVerilogDisableLabels&
HierarchyBuilder::systemverilog_disable_labels()
{
    if (systemverilog_disable_labels_) {
        return *systemverilog_disable_labels_;
    }
    auto& labels = systemverilog_disable_labels_.emplace();
    const auto& hir = compiled_->systemverilog_hir;
    for (const auto& statement : hir.statements()) {
        if (statement.kind == semantic::sv::StatementKind::disable) {
            const auto& spelling = statement.task.spelling;
            const auto dot = spelling.rfind('.');
            labels.targets.insert(dot == std::string::npos
                    ? spelling
                    : spelling.substr(dot + 1U));
        } else if (!statement.label.empty()) {
            labels.declared.insert(statement.label);
        }
    }
    for (const auto& declaration : hir.declarations()) {
        if (declaration.form == semantic::sv::DeclarationForm::task) {
            labels.declared.insert(declaration.name);
        }
    }
    return labels;
}

void HierarchyBuilder::publish_systemverilog_process(Process& lowered,
    const semantic::sv::Unit& unit,
    const bool concurrent_assertion,
    SpecializationInfo& specialization,
    const semantic::ProcessId process_id,
    const std::optional<std::uint32_t> program_owner,
    const bool canonicalize)
{
    lowered.language_standard = unit.standard;
    lowered.compatibility_profile = unit.compatibility_profile;
    lowered.observed = concurrent_assertion;
    lowered.reactive = !concurrent_assertion && program_owner.has_value();
    lowered.program_owner = program_owner;
    if (canonicalize) {
        canonicalize_process_operations(lowered);
    }
    specialization.processes.push_back(lowered.id);
    specialization.semantic_processes.emplace_back(lowered.id, process_id);
    design_.append_process_record(std::move(lowered));
}

void HierarchyBuilder::append_generated_systemverilog_processes(
    Lowerer& source,
    const semantic::sv::Unit& unit,
    SpecializationInfo& specialization,
    const std::optional<std::uint32_t> program_owner)
{
    auto generated_processes = source.take_generated_processes();
    record_lowering_census(
        systemverilog_lowerer_generated_processes_,
        generated_processes.size());
    for (auto& generated : generated_processes) {
        generated.language_standard = unit.standard;
        generated.compatibility_profile = unit.compatibility_profile;
        generated.reactive = program_owner.has_value();
        generated.program_owner = program_owner;
        canonicalize_process_operations(generated);
        specialization.processes.push_back(generated.id);
        design_.append_process_record(std::move(generated));
    }
}

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
    std::vector<semantic::ProcessId>* const deferred_processes,
    std::vector<std::pair<semantic::StatementId, std::size_t>>* const
        deferred_statements,
    std::vector<DeferredGeneratedSystemVerilogItem>* const
        deferred_generated)
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
        append_generated_systemverilog_processes(
            source, unit, specialization, program_owner);
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
        const auto diagnostics_before = diagnostics_.size();
        lowerer.reset_hierarchical_reference_state();
        lowerer.set_hierarchical_reference_retry(
            deferred_statements != nullptr);
        auto lowered = lower_cached_systemverilog_concurrent_statement(
            unit, specialized, lowerer, statement_id, source_language,
            unit.declarations, path, order, program_owner);
        lowerer.set_hierarchical_reference_retry(false);
        if (!lowered && deferred_statements != nullptr
            && lowerer.hierarchical_reference_missed()) {
            // `assign intf.member = ...` names a member of an instance
            // created after this unit's statements; retry then.
            diagnostics_.resize(diagnostics_before);
            deferred_statements->emplace_back(statement_id, order);
            continue;
        }
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled concurrent statement could not be lowered "
                "from HIR"
                    + hir_lowering_rejection_detail(lowerer),
                compiled_source_span(*compiled_, statement_source));
            return false;
        }
        publish_systemverilog_concurrent_occurrence(
            *lowered, unit, specialization, program_owner);
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
            process_id, path);
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
            lowerer.set_hierarchical_reference_retry(
                deferred_processes != nullptr);
            lowered = lowerer.lower_hir_process(
                process_id,
                source_language,
                path);
            lowerer.set_hierarchical_reference_retry(false);
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
                "compiled process could not be lowered from HIR"
                    + hir_lowering_rejection_detail(lowerer),
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
        generated_lowerer.set_ancestor_systemverilog_specializations(
            &materialization.type_owner_contexts);
        generated_lowerer.set_hir_code_coverage_context(
            coverage_, unit.kind == semantic::sv::UnitKind::module
                && source_language == frontend::Language::SystemVerilog2017);
        generated_lowerer.set_systemverilog_interface_handles(
            &systemverilog_interface_handles_, &systemverilog_interface_types_,
            &systemverilog_instance_specializations_,
            &systemverilog_interface_exports_);
        {
            const auto& disable_labels = systemverilog_disable_labels();
            generated_lowerer.set_disable_labels(
                &disable_labels.targets, &disable_labels.declared);
        }
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
            const auto order = concurrent_order++;
            const auto diagnostics_before = diagnostics_.size();
            generated_lowerer.reset_hierarchical_reference_state();
            generated_lowerer.set_hierarchical_reference_retry(
                deferred_generated != nullptr);
            auto lowered = lower_cached_systemverilog_concurrent_statement(
                unit, *occurrence.specialization, generated_lowerer,
                statement_id, source_language,
                declaration_bindings->second, occurrence.path,
                order, program_owner);
            generated_lowerer.set_hierarchical_reference_retry(false);
            if (!lowered && deferred_generated != nullptr
                && generated_lowerer.hierarchical_reference_missed()) {
                // `assign intf.member = ...` names a member of an interface
                // instance of this block, created after the statements.
                diagnostics_.resize(diagnostics_before);
                deferred_generated->push_back({ index, statement_id,
                    std::nullopt, order, declaration_bindings->second });
                continue;
            }
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
            publish_systemverilog_concurrent_occurrence(
                *lowered, unit, specialization, program_owner);
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
                generated_lowerer.reset_hierarchical_reference_state();
                generated_lowerer.set_hierarchical_reference_retry(
                    deferred_generated != nullptr);
                lowered = generated_lowerer.lower_hir_process(
                    process_id, source_language, occurrence.path);
                generated_lowerer.set_hierarchical_reference_retry(false);
                generated_lowerer.set_hir_code_coverage_active(false);
                lowered_here = lowered.has_value();
                if (!lowered && deferred_generated != nullptr
                    && generated_lowerer.hierarchical_reference_missed()) {
                    diagnostics_.resize(diagnostics_before);
                    deferred_generated->push_back({ index, std::nullopt,
                        process_id, 0U,
                        declaration_bindings
                                != generated_signal_declarations.end()
                            ? declaration_bindings->second
                            : std::vector<semantic::DeclarationId> { } });
                    continue;
                }
            }
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated process could not be lowered "
                    "from HIR"
                        + hir_lowering_rejection_detail(generated_lowerer),
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
                && !generated_lowerer.has_generated_processes()
                && !generated_lowerer.hierarchical_reference_used()) {
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

bool HierarchyBuilder::lower_deferred_generated_systemverilog(
    const semantic::sv::Unit& unit,
    const frontend::Language source_language,
    const std::vector<hierarchy_sv_generate_detail::Occurrence>&
        generate_occurrences,
    std::vector<SystemVerilogHirMaterialization>&
        generated_materializations,
    const std::vector<DeferredGeneratedSystemVerilogItem>& deferred,
    const std::size_t specialization_index,
    const std::optional<std::uint32_t> program_owner)
{
    for (const auto& item : deferred) {
        if (item.occurrence >= generate_occurrences.size()
            || item.occurrence >= generated_materializations.size()) {
            return false;
        }
        const auto& occurrence = generate_occurrences[item.occurrence];
        auto& materialization = generated_materializations[item.occurrence];
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
        generated_lowerer.set_ancestor_systemverilog_specializations(
            &materialization.type_owner_contexts);
        generated_lowerer.set_systemverilog_interface_handles(
            &systemverilog_interface_handles_, &systemverilog_interface_types_,
            &systemverilog_instance_specializations_,
            &systemverilog_interface_exports_);
        {
            const auto& disable_labels = systemverilog_disable_labels();
            generated_lowerer.set_disable_labels(
                &disable_labels.targets, &disable_labels.declared);
        }
        generated_lowerer.set_systemverilog_program_owner(program_owner);
        auto& specialization
            = design_.specializations_[specialization_index];
        const auto append_generated = [&] {
            append_generated_systemverilog_processes(
                generated_lowerer, unit, specialization, program_owner);
        };
        if (item.statement) {
            const auto statement = occurrence.specialization
                                       ->find_statement(*item.statement);
            auto lowered = lower_cached_systemverilog_concurrent_statement(
                unit, *occurrence.specialization, generated_lowerer,
                *item.statement, source_language, item.declarations,
                occurrence.path, item.order, program_owner);
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated concurrent statement could not "
                    "be lowered from HIR",
                    compiled_source_span(*compiled_,
                        statement && statement->systemverilog != nullptr
                            ? statement->systemverilog->source
                            : unit.source));
                return false;
            }
            publish_systemverilog_concurrent_occurrence(
                *lowered, unit, specialization, program_owner);
            append_generated();
            continue;
        }
        const auto process = occurrence.specialization->find_process(
            *item.process);
        auto lowered = generated_lowerer.lower_hir_process(
            *item.process, source_language, occurrence.path);
        if (!lowered) {
            report("FSIM-ELAB-HIR-001",
                "compiled generated process could not be lowered from HIR"
                    + hir_lowering_rejection_detail(generated_lowerer),
                compiled_source_span(*compiled_,
                    process && process->systemverilog != nullptr
                        ? process->systemverilog->source
                        : unit.source));
            return false;
        }
        const bool concurrent_assertion = process
            && process->systemverilog != nullptr
            && process->systemverilog->concurrent_assertion;
        if (process && process->systemverilog != nullptr
            && process->systemverilog->name == "<process>") {
            lowered->name = occurrence.path;
        }
        publish_systemverilog_process(*lowered, unit, concurrent_assertion,
            specialization, *item.process, program_owner);
        append_generated();
    }
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
    const std::optional<std::uint32_t> program_owner,
    const std::vector<std::pair<semantic::StatementId, std::size_t>>&
        deferred_statements,
    ForwardDeferredSystemVerilogItems* const missed)
{
    for (const auto& [statement_id, order] : deferred_statements) {
        const auto statement = specialized.find_statement(statement_id);
        const auto statement_source = statement
                && statement->systemverilog != nullptr
            ? statement->systemverilog->source
            : unit.source;
        lowerer.reset_hierarchical_reference_state();
        const auto diagnostics_before = diagnostics_.size();
        lowerer.set_hierarchical_reference_retry(missed != nullptr);
        auto lowered = lower_cached_systemverilog_concurrent_statement(
            unit, specialized, lowerer, statement_id, source_language,
            unit.declarations, path, order, program_owner);
        lowerer.set_hierarchical_reference_retry(false);
        if (!lowered && missed != nullptr
            && lowerer.hierarchical_reference_missed()) {
            diagnostics_.resize(diagnostics_before);
            missed->statements.emplace_back(statement_id, order);
            continue;
        }
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled concurrent statement could not be lowered "
                "from HIR"
                    + hir_lowering_rejection_detail(lowerer),
                compiled_source_span(*compiled_, statement_source));
            return false;
        }
        auto& specialization
            = design_.specializations_[specialization_index];
        publish_systemverilog_concurrent_occurrence(
            *lowered, unit, specialization, program_owner);
        append_generated_systemverilog_processes(
            lowerer, unit, specialization, program_owner);
    }
    for (const auto process_id : deferred_processes) {
        const auto process = specialized.find_process(process_id);
        const auto process_source = process
                && process->systemverilog != nullptr
            ? process->systemverilog->source
            : unit.source;
        lowerer.reset_hierarchical_reference_state();
        const auto diagnostics_before = diagnostics_.size();
        lowerer.set_hierarchical_reference_retry(missed != nullptr);
        auto lowered = lowerer.lower_hir_process(
            process_id, source_language, path);
        lowerer.set_hierarchical_reference_retry(false);
        if (!lowered && missed != nullptr
            && lowerer.hierarchical_reference_missed()) {
            diagnostics_.resize(diagnostics_before);
            missed->processes.push_back(process_id);
            continue;
        }
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled process could not be lowered from HIR"
                    + hir_lowering_rejection_detail(lowerer),
                compiled_source_span(*compiled_, process_source));
            return false;
        }
        const bool concurrent_assertion = process
            && process->systemverilog != nullptr
            && process->systemverilog->concurrent_assertion;
        auto& specialization
            = design_.specializations_[specialization_index];
        publish_systemverilog_process(*lowered, unit, concurrent_assertion,
            specialization, process_id, program_owner);
        append_generated_systemverilog_processes(
            lowerer, unit, specialization, program_owner);
    }
    return true;
}

void HierarchyBuilder::lower_forward_deferred_systemverilog(
    const std::string_view ancestor, const bool final)
{
    auto pending = std::move(forward_deferred_systemverilog_);
    forward_deferred_systemverilog_.clear();
    for (auto& entry : pending) {
        if (!ancestor.empty()
            && !(entry.path.size() > ancestor.size()
                && entry.path.starts_with(ancestor)
                && entry.path[ancestor.size()] == '.')) {
            forward_deferred_systemverilog_.push_back(std::move(entry));
            continue;
        }
        auto& materialization = *entry.materialization;
        Lowerer lowerer {
            design_,
            materialization.signals,
            materialization.read_only_signals,
            materialization.string_objects,
            materialization.read_only_strings,
            materialization.container_objects,
            materialization.read_only_containers,
            diagnostics_,
        };
        lowerer.set_specialized_hir_unit(materialization.specialization);
        lowerer.set_ancestor_systemverilog_specializations(
            &materialization.type_owner_contexts);
        lowerer.set_systemverilog_interface_handles(
            &systemverilog_interface_handles_, &systemverilog_interface_types_,
            &systemverilog_instance_specializations_,
            &systemverilog_interface_exports_);
        lowerer.set_hir_container_declaration_bindings(
            &materialization.container_declaration_bindings);
        {
            const auto& disable_labels = systemverilog_disable_labels();
            lowerer.set_disable_labels(
                &disable_labels.targets, &disable_labels.declared);
        }
        lowerer.set_systemverilog_program_owner(entry.program_owner);
        lowerer.set_package_constant_signal(entry.package_signal);
        lowerer.set_package_container_object(entry.package_container);
        lowerer.set_package_string_object(entry.package_string);
        ForwardDeferredSystemVerilogItems missed;
        static_cast<void>(lower_deferred_systemverilog_processes(
            *entry.unit, *materialization.specialization, entry.path,
            entry.source_language, entry.items.processes, lowerer,
            entry.specialization_index, entry.program_owner,
            entry.items.statements, final ? nullptr : &missed));
        if (!missed.processes.empty() || !missed.statements.empty()) {
            entry.items = std::move(missed);
            forward_deferred_systemverilog_.push_back(std::move(entry));
        }
    }
}

} // namespace fsim::elaboration

namespace fsim::elaboration {

/// A net or variable whose only driver is a constant continuous assignment
/// starts at that constant. The assignment's time-0 update then changes nothing, and initial
/// procedures read the constant at time zero, as other simulators' time-0
/// ordering lets them (the order is a race; IEEE 1800-2017 4.7). Forced or
/// released nets keep their resolution, which release re-evaluates, and
/// nets a process waits on keep the time-0 change that wakes it.
void HierarchyBuilder::initialize_constant_driven_nets()
{
    using namespace runtime::simir;
    std::vector<std::uint32_t> drivers(design_.signals_.size());
    // Forced or released signals, and nets a process waits on, whose
    // time-0 change from X must remain an event.
    std::vector<bool> forced(design_.signals_.size());
    std::vector<bool> waited(design_.signals_.size());
    for (std::size_t index = 0U; index < design_.process_count(); ++index) {
        const auto view = design_.process_view(index);
        for (const auto& region : view.driver_regions()) {
            if (region.signal < drivers.size()) {
                ++drivers[region.signal];
            }
        }
        // Only a wait for any change needs the time-0 change; an edge wait
        // on a constant net is not woken by its initialization.
        for (const auto& sensitivity : view.static_sensitivity()) {
            if (sensitivity.signal < waited.size()
                && sensitivity.edge == EdgeKind::any) {
                waited[sensitivity.signal] = true;
            }
        }
        const auto& operations = view.operations();
        for (std::size_t operation = 0U; operation < operations.size();
            ++operation) {
            const auto expanded = operations.expanded(operation);
            if (const auto* wait = operation_get_if<WaitOn>(&expanded)) {
                for (std::size_t position = 0U;
                    position < wait->signals.size(); ++position) {
                    const auto signal = wait->signals[position];
                    const bool any_change = position >= wait->edges.size()
                        || wait->edges[position] == EdgeKind::any;
                    if (signal < waited.size() && any_change) {
                        waited[signal] = true;
                    }
                }
                continue;
            }
            if (const auto* force
                = operation_get_if<ForceSignalSlice>(&expanded);
                force != nullptr && force->signal < forced.size()) {
                forced[force->signal] = true;
            } else if (const auto* release
                = operation_get_if<ReleaseSignalSlice>(&expanded);
                release != nullptr && release->signal < forced.size()) {
                forced[release->signal] = true;
            }
        }
    }
    // A SystemVerilog variable declaration's constant initializer takes
    // effect before any process starts, without a time-0 event (IEEE
    // 1800-2017 6.8). The parser keeps it as an initial process, which then
    // writes the value the variable already holds.
    for (std::size_t index = 0U; index < design_.process_count(); ++index) {
        const auto view = design_.process_view(index);
        if (view.startup_phase()
                == ProcessStartupPhase::package_static_initializer
            || view.name().find("$declaration_initializer_") == std::string::npos
            || !(view.language_standard() == "2009"
                || view.language_standard() == "2012"
                || view.language_standard() == "2017"
                || view.language_standard() == "2023")
            || !view.static_sensitivity().empty()) {
            continue;
        }
        const auto& operations = view.operations();
        std::unordered_map<RegisterId, PackedLogic4> constants;
        for (std::size_t operation = 0U; operation < operations.size();
            ++operation) {
            const auto expanded = operations.expanded(operation);
            if (const auto* load = operation_get_if<LoadConstant>(&expanded)) {
                constants.insert_or_assign(load->destination, load->value);
            } else if (const auto* copy
                = operation_get_if<CopyRegister>(&expanded)) {
                const auto source = constants.find(copy->source);
                if (source != constants.end()) {
                    auto value = source->second;
                    constants.insert_or_assign(
                        copy->destination, std::move(value));
                }
            } else if (const auto* write
                = operation_get_if<WriteBlocking>(&expanded)) {
                const auto constant = constants.find(write->source);
                if (constant == constants.end()
                    || write->signal >= design_.signals_.size()
                    || forced[write->signal]) {
                    break;
                }
                auto& signal = design_.signals_[write->signal];
                if (constant->second.width()
                        == signal.initial_value.width()
                    && signal.resolution == ResolutionKind::none
                    && !signal.event_variable
                    && signal.systemverilog_scalar
                        == frontend::SystemVerilogScalarKind::None) {
                    signal.initial_value = constant->second;
                }
                break;
            } else if (!operation_get_if<DebugPoint>(&expanded)) {
                break;
            }
        }
    }
    for (std::size_t index = 0U; index < design_.process_count(); ++index) {
        const auto view = design_.process_view(index);
        if (!constant_continuous_driver(view)
            || !(view.drive_strength() == DriveStrength { })) {
            continue;
        }
        const auto& operations = view.operations();
        std::unordered_map<RegisterId, PackedLogic4> constants;
        for (std::size_t operation = 0U;
            operation + 1U < operations.size(); ++operation) {
            const auto expanded = operations.expanded(operation);
            if (const auto* load = operation_get_if<LoadConstant>(&expanded)) {
                constants.insert_or_assign(load->destination, load->value);
                continue;
            }
            if (const auto* concatenate
                = operation_get_if<Concatenate>(&expanded)) {
                // The first operand is the most significant.
                PackedLogic4 value { concatenate->width, Logic4::x };
                std::size_t position = concatenate->width;
                bool complete = true;
                for (const auto operand : concatenate->operands) {
                    const auto part = constants.find(operand);
                    if (part == constants.end()
                        || part->second.width() > position) {
                        complete = false;
                        break;
                    }
                    position -= part->second.width();
                    for (std::size_t bit = 0U; bit < part->second.width();
                        ++bit) {
                        value.set(position + bit, part->second.get(bit));
                    }
                }
                if (complete && position == 0U) {
                    constants.insert_or_assign(
                        concatenate->destination, std::move(value));
                } else {
                    constants.erase(concatenate->destination);
                }
                continue;
            }
            if (const auto* extract = operation_get_if<Extract>(&expanded)) {
                const auto source = constants.find(extract->source);
                if (source == constants.end()) {
                    constants.erase(extract->destination);
                    continue;
                }
                PackedLogic4 value { extract->width, Logic4::x };
                for (std::size_t bit = 0U; bit < extract->width; ++bit) {
                    if (extract->offset + bit < source->second.width()) {
                        value.set(bit,
                            source->second.get(extract->offset + bit));
                    }
                }
                constants.insert_or_assign(
                    extract->destination, std::move(value));
                continue;
            }
            if (const auto* insert = operation_get_if<Insert>(&expanded)) {
                const auto target = constants.find(insert->target);
                const auto source = constants.find(insert->source);
                if (target == constants.end() || source == constants.end()) {
                    constants.erase(insert->destination);
                    continue;
                }
                auto value = target->second;
                for (std::size_t bit = 0U; bit < source->second.width()
                    && insert->offset + bit < value.width(); ++bit) {
                    value.set(insert->offset + bit, source->second.get(bit));
                }
                constants.insert_or_assign(
                    insert->destination, std::move(value));
                continue;
            }
            if (const auto* copy = operation_get_if<CopyRegister>(&expanded)) {
                const auto source = constants.find(copy->source);
                if (source != constants.end()) {
                    auto value = source->second;
                    constants.insert_or_assign(
                        copy->destination, std::move(value));
                } else {
                    constants.erase(copy->destination);
                }
                continue;
            }
            const auto* write = operation_get_if<WriteUpdate>(&expanded);
            if (write == nullptr) {
                continue;
            }
            const auto constant = constants.find(write->source);
            if (constant == constants.end()
                || write->signal >= design_.signals_.size()
                || drivers[write->signal] != 1U || forced[write->signal]
                || waited[write->signal]) {
                continue;
            }
            auto& signal = design_.signals_[write->signal];
            const auto& value = constant->second;
            if (value.width() != signal.initial_value.width()
                || signal.event_variable || signal.charge_strength
                || signal.implicit_driver
                || (signal.resolution != ResolutionKind::none
                    && signal.resolution != ResolutionKind::sv_wire)) {
                continue;
            }
            bool known = true;
            for (std::size_t bit = 0U; bit < value.width(); ++bit) {
                if (value.get(bit) == Logic4::z) {
                    known = false;
                    break;
                }
            }
            // A lone strong constant driver resolves to its own value, so
            // the net needs no resolution and starts at the constant.
            if (known) {
                signal.initial_value = value;
                signal.resolution = ResolutionKind::none;
                if (write->signal < design_.signal_info_.size()) {
                    design_.signal_info_[write->signal].resolution
                        = ResolutionKind::none;
                }
            }
        }
    }
}

} // namespace fsim::elaboration
