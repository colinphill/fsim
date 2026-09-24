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
    const auto append_generated_processes = [&](Lowerer& source) {
        for (auto& generated : source.take_generated_processes()) {
            generated.language_standard = unit.standard;
            generated.compatibility_profile
                = unit.compatibility_profile;
            generated.reactive = program_owner.has_value();
            generated.program_owner = program_owner;
            canonicalize_process_operations(generated);
            specialization.processes.push_back(generated.id);
            design_.processes_.push_back(std::move(generated));
        }
    };

    struct StaticGeneratedSliceAssignment {
        SignalId signal { };
        std::uint32_t offset { };
        PackedLogic4 value;
        ValueKind value_kind { ValueKind::logic4 };
        std::vector<Operation> debug_points;
    };
    const auto static_generated_slice_assignment = [](
                                                       const Process& process)
        -> std::optional<StaticGeneratedSliceAssignment> {
        if (!process.static_sensitivity.empty()
            || process.string_register_count != 0U
            || process.container_register_count != 0U
            || !process.debug_locals.empty()
            || !process.debug_string_locals.empty()
            || !process.debug_container_locals.empty()
            || process.switch_source || process.switch_target
            || process.switch_control) {
            return std::nullopt;
        }
        const WriteUpdateSlice* write = nullptr;
        std::unordered_map<RegisterId, PackedLogic4> values;
        std::vector<Operation> debug_points;
        for (const auto& operation : process.operations) {
            if (const auto* load_candidate
                = operation_get_if<LoadConstant>(&operation)) {
                if (!values.emplace(
                               load_candidate->destination,
                               load_candidate->value)
                        .second) {
                    return std::nullopt;
                }
            } else if (const auto* binary
                = operation_get_if<Binary>(&operation)) {
                const auto lhs = values.find(binary->lhs);
                const auto rhs = values.find(binary->rhs);
                if (lhs == values.end() || rhs == values.end()) {
                    return std::nullopt;
                }
                values.insert_or_assign(
                    binary->destination,
                    binary_value(
                        binary->operation,
                        lhs->second,
                        rhs->second));
            } else if (const auto* write_candidate
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                if (write != nullptr) {
                    return std::nullopt;
                }
                write = write_candidate;
            } else if (operation_holds<DebugPoint>(operation)) {
                debug_points.push_back(operation);
            } else if (!operation_holds<Halt>(operation)) {
                return std::nullopt;
            }
        }
        if (write == nullptr
            || write->source >= process.register_value_kinds.size()) {
            return std::nullopt;
        }
        const auto value = values.find(write->source);
        if (value == values.end()) {
            return std::nullopt;
        }
        return StaticGeneratedSliceAssignment {
            write->signal,
            write->offset,
            value->second,
            process.register_value_kinds[write->source],
            std::move(debug_points),
        };
    };
    std::vector<Process> generated_slice_processes;
    const auto append_generated_slice_processes = [&] {
        if (generated_slice_processes.empty()) {
            return;
        }
        std::vector<StaticGeneratedSliceAssignment> assignments;
        assignments.reserve(generated_slice_processes.size());
        for (const auto& process : generated_slice_processes) {
            const auto assignment
                = static_generated_slice_assignment(process);
            if (!assignment) {
                for (auto pending : generated_slice_processes) {
                    pending.id = static_cast<ProcessId>(
                        design_.processes_.size());
                    specialization.processes.push_back(pending.id);
                    design_.processes_.push_back(std::move(pending));
                }
                generated_slice_processes.clear();
                return;
            }
            assignments.push_back(*assignment);
        }
        if (assignments.size() < 2U) {
            auto process = std::move(generated_slice_processes.front());
            process.id = static_cast<ProcessId>(
                design_.processes_.size());
            specialization.processes.push_back(process.id);
            design_.processes_.push_back(std::move(process));
            generated_slice_processes.clear();
            return;
        }

        Process fused;
        fused.id = static_cast<ProcessId>(design_.processes_.size());
        fused.name = path + ".continuous_fused_"
            + std::to_string(concurrent_order++);
        fused.language_standard
            = generated_slice_processes.front().language_standard;
        fused.compatibility_profile
            = generated_slice_processes.front().compatibility_profile;
        fused.reactive = generated_slice_processes.front().reactive;
        fused.program_owner
            = generated_slice_processes.front().program_owner;
        for (const auto& process : generated_slice_processes) {
            for (const auto& profile : process.expression_profiles) {
                fused.expression_profiles.push_back(profile);
            }
        }

        const auto common_signal = assignments.front().signal;
        const auto one_signal = std::ranges::all_of(
            assignments,
            [&](const StaticGeneratedSliceAssignment& assignment) {
                return assignment.signal == common_signal;
            });
        auto ordered = assignments;
        std::ranges::sort(
            ordered,
            { },
            &StaticGeneratedSliceAssignment::offset);
        std::uint64_t covered { };
        bool contiguous = one_signal;
        for (const auto& assignment : ordered) {
            if (assignment.value.empty()
                || assignment.offset != covered) {
                contiguous = false;
                break;
            }
            covered += assignment.value.width();
        }
        const auto fully_covered = contiguous
            && common_signal < design_.signal_info_.size()
            && covered == design_.signal_info_[common_signal].width
            && assignments.size()
                <= 512U / std::max<std::size_t>(1U, static_cast<std::size_t>((covered + 63U) / 64U));
        if (fully_covered) {
            PackedLogic4 value(
                design_.signal_info_[common_signal].width,
                Logic4::x);
            for (const auto& assignment : ordered) {
                value.insert_bits(
                    assignment.value, assignment.offset);
                fused.operations.insert(
                    fused.operations.end(),
                    assignment.debug_points.begin(),
                    assignment.debug_points.end());
            }
            fused.register_count = 1U;
            fused.register_value_kinds.push_back(
                value_kind(
                    design_.signal_info_[common_signal].source_domain));
            fused.operations.emplace_back(LoadConstant {
                0U, std::move(value) });
            fused.operations.emplace_back(
                WriteUpdate { common_signal, 0U });
            fused.driver_regions.push_back(Process::DriverRegion {
                common_signal,
                0U,
                static_cast<std::uint32_t>(covered),
                true,
            });
        } else {
            for (auto& assignment : assignments) {
                fused.operations.insert(
                    fused.operations.end(),
                    assignment.debug_points.begin(),
                    assignment.debug_points.end());
                const auto source = static_cast<RegisterId>(
                    fused.register_count++);
                fused.register_value_kinds.push_back(
                    assignment.value_kind);
                const auto width = assignment.value.width();
                fused.operations.emplace_back(LoadConstant {
                    source, std::move(assignment.value) });
                fused.operations.emplace_back(WriteUpdateSlice {
                    assignment.signal,
                    source,
                    assignment.offset,
                });
                fused.driver_regions.push_back(Process::DriverRegion {
                    assignment.signal,
                    assignment.offset,
                    static_cast<std::uint32_t>(width),
                    false,
                });
            }
        }
        fused.operations.emplace_back(Halt { });
        specialization.processes.push_back(fused.id);
        design_.processes_.push_back(std::move(fused));
        generated_slice_processes.clear();
    };

    for (auto& process : clocking_processes) {
        process.id = static_cast<ProcessId>(design_.processes_.size());
        process.language_standard = unit.standard;
        process.compatibility_profile = unit.compatibility_profile;
        process.program_owner = program_owner;
        canonicalize_process_operations(process);
        specialization.processes.push_back(process.id);
        design_.processes_.push_back(std::move(process));
    }

    for (const auto statement_id : active_concurrent_statements) {
        const auto statement = specialized.find_statement(statement_id);
        const auto statement_source = statement
                && statement->systemverilog != nullptr
            ? statement->systemverilog->source
            : unit.source;
        auto lowered = lowerer.lower_hir_concurrent_statement(
            statement_id,
            source_language,
            path,
            concurrent_order++);
        if (!lowered) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled concurrent statement could not be lowered "
                "from HIR",
                compiled_source_span(*compiled_, statement_source));
            return false;
        }
        lowered->language_standard = unit.standard;
        lowered->compatibility_profile = unit.compatibility_profile;
        lowered->reactive = program_owner.has_value();
        lowered->program_owner = program_owner;
        canonicalize_process_operations(*lowered);
        specialization.processes.push_back(lowered->id);
        design_.processes_.push_back(std::move(*lowered));
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
        auto lowered = lowerer.lower_hir_process(
            process_id,
            source_language,
            path);
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
        canonicalize_process_operations(*lowered);
        specialization.processes.push_back(lowered->id);
        specialization.semantic_processes.emplace_back(
            lowered->id, process_id);
        design_.processes_.push_back(std::move(*lowered));
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
        generated_lowerer.set_specialized_hir_unit(
            &occurrence.specialization);
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
                                       .find_statement(statement_id);
            const auto statement_source = statement
                    && statement->systemverilog != nullptr
                ? statement->systemverilog->source
                : unit.source;
            auto lowered = generated_lowerer
                               .lower_hir_concurrent_statement(
                                   statement_id, source_language,
                                   occurrence.path,
                                   concurrent_order++);
            if (!lowered) {
                report("FSIM-ELAB-HIR-001",
                    "compiled generated concurrent statement could not "
                    "be lowered from HIR",
                    compiled_source_span(
                        *compiled_, statement_source));
                return false;
            }
            lowered->language_standard = unit.standard;
            lowered->compatibility_profile
                = unit.compatibility_profile;
            lowered->reactive = program_owner.has_value();
            lowered->program_owner = program_owner;
            const auto static_assignment
                = static_generated_slice_assignment(*lowered);
            if (static_assignment) {
                generated_slice_processes.push_back(
                    std::move(*lowered));
            } else {
                canonicalize_process_operations(*lowered);
                specialization.processes.push_back(lowered->id);
                design_.processes_.push_back(std::move(*lowered));
            }
            append_generated_processes(generated_lowerer);
        }
        for (const auto process_id : occurrence.region->processes) {
            const auto process
                = occurrence.specialization.find_process(process_id);
            const auto process_source = process
                    && process->systemverilog != nullptr
                ? process->systemverilog->source
                : unit.source;
            const bool concurrent_assertion = process
                && process->systemverilog != nullptr
                && process->systemverilog->concurrent_assertion;
            auto lowered = generated_lowerer.lower_hir_process(
                process_id, source_language, occurrence.path);
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
            canonicalize_process_operations(*lowered);
            specialization.processes.push_back(lowered->id);
            specialization.semantic_processes.emplace_back(
                lowered->id, process_id);
            design_.processes_.push_back(std::move(*lowered));
            append_generated_processes(generated_lowerer);
        }
    }
    append_generated_slice_processes();
    return true;
}

} // namespace fsim::elaboration
