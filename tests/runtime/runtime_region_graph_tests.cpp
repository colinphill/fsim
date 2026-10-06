// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_graph.hpp"
#include "fsim/runtime/simir_region_activation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_scheduler_batch_frontier()
{
    struct CapturedFrontier {
        std::uint64_t generation { };
        SimulationTick time { };
        std::uint64_t delta { };
        SchedulerPhase phase { SchedulerPhase::active };
        std::uint64_t systemverilog_round { };
        std::size_t cursor { };
        std::size_t end { };
        std::vector<SchedulerBatchFrontierEntry> tasks;
    };

    struct ProbeBatch final : SchedulerBatchTask {
        std::vector<CapturedFrontier> frontiers;

        SchedulerBatchResult execute(Scheduler& scheduler,
            const std::span<const std::uint64_t> payloads) override
        {
            const auto frontier = scheduler.current_batch_frontier();
            require(frontier.has_value(),
                "SV batch callback receives scheduler-owned frontier metadata");
            require(frontier->tasks.size() == payloads.size(),
                "frontier entries describe the exact callback payload offer");
            CapturedFrontier captured {
                frontier->generation,
                frontier->time,
                frontier->delta,
                frontier->phase,
                frontier->systemverilog_round,
                frontier->cursor,
                frontier->end,
                { frontier->tasks.begin(), frontier->tasks.end() },
            };
            frontiers.push_back(std::move(captured));
            const auto consumed = frontiers.size() == 1U
                ? std::size_t { 2U } : payloads.size();
            return { consumed, { } };
        }
    } batch;

    Scheduler scheduler;
    const auto fallback = [](Scheduler&) { };
    scheduler.schedule_systemverilog_batchable(
        SchedulerPhase::active, 10U, batch, 0x11U, fallback);
    scheduler.schedule_systemverilog_batchable(
        SchedulerPhase::active, 20U, batch, 0x22U, fallback);
    scheduler.schedule_systemverilog_batchable(
        SchedulerPhase::active, 30U, batch, 0x33U, fallback);
    const auto result = scheduler.run();
    require(result.status == RunStatus::completed
            && batch.frontiers.size() == 2U,
        "partial SV batch resume takes a fresh scheduler frontier snapshot");
    const auto& first = batch.frontiers[0U];
    const auto& resumed = batch.frontiers[1U];
    require(first.generation != 0U
            && resumed.generation > first.generation
            && first.time == resumed.time
            && first.delta == resumed.delta
            && first.phase == SchedulerPhase::active
            && resumed.phase == SchedulerPhase::active
            && first.systemverilog_round == resumed.systemverilog_round
            && first.cursor == 0U && first.end == 3U
            && resumed.cursor == 0U && resumed.end == 1U
            && first.tasks.size() == 3U && resumed.tasks.size() == 1U
            && first.tasks[0U].payload == 0x11U
            && first.tasks[1U].payload == 0x22U
            && first.tasks[2U].payload == 0x33U
            && resumed.tasks[0U].payload == 0x33U
            && first.tasks[0U].stable_order < first.tasks[1U].stable_order
            && first.tasks[1U].stable_order < first.tasks[2U].stable_order
            && first.tasks[2U].sequence == resumed.tasks[0U].sequence
            && !scheduler.current_batch_frontier().has_value(),
        "frontier records bind order, round, fresh generation, and live suffix");
}


Process transfer(ProcessId id, SignalId input, SignalId output)
{
    Process process;
    process.id = id;
    process.name = "graph_process_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { output, 0U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

RegionGraph build(const std::vector<Process>& processes,
    const std::vector<RegionSignalDescriptor>& signals,
    const bool collect_opaque_operation_counts = false)
{
    std::vector<const Process*> bindings;
    for (const auto& process : processes) {
        bindings.push_back(&process);
    }
    return RegionGraph::build(bindings, signals,
        collect_opaque_operation_counts);
}

using TestBitRange = std::pair<std::uint32_t, std::uint32_t>;

struct IntervalProcessSpec {
    std::vector<TestBitRange> reads;
    std::vector<TestBitRange> writes;
};

Process interval_process(ProcessId id, SignalId signal, SignalId trigger,
    const std::uint32_t signal_width, const IntervalProcessSpec& spec)
{
    Process process;
    process.id = id;
    process.name = "range_graph_process_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = std::max<std::size_t>(1U, spec.writes.size());
    if (spec.reads.empty()) {
        process.static_sensitivity = { { trigger, EdgeKind::any } };
    } else {
        for (const auto& [offset, width] : spec.reads) {
            process.static_sensitivity.push_back({
                signal, EdgeKind::any, offset, width });
        }
    }

    for (std::size_t index = 0U; index < spec.writes.size(); ++index) {
        const auto [offset, width] = spec.writes[index];
        const auto source_width = width == 0U ? signal_width : width;
        process.operations.push_back(LoadConstant {
            static_cast<RegisterId>(index),
            PackedLogic4(source_width, Logic4::zero) });
        if (width == 0U) {
            process.driver_regions.push_back({ signal, 0U, 0U, true });
            process.operations.push_back(WriteUpdate {
                signal, static_cast<RegisterId>(index),
                SignalUpdateDomain::systemverilog_active });
        } else {
            process.driver_regions.push_back({ signal, offset, width, false });
            process.operations.push_back(WriteUpdateSlice {
                signal, static_cast<RegisterId>(index), offset,
                SignalUpdateDomain::systemverilog_active });
        }
    }
    if (spec.writes.empty()) {
        process.operations.push_back(LoadConstant {
            0U, PackedLogic4(1U, Logic4::zero) });
    }
    process.operations.push_back(WaitSensitivity { });
    process.operations.push_back(Jump { 0U });
    return process;
}

RegionGraph build_interval_graph(
    const std::vector<IntervalProcessSpec>& specifications,
    const std::uint32_t signal_width,
    const ResolutionKind resolution = ResolutionKind::sv_wire,
    const RegionObservation observations = RegionObservation::none)
{
    RegionSignalDescriptor output { signal_width, resolution };
    output.observations = observations;
    std::vector<RegionSignalDescriptor> descriptors(
        specifications.size() + 1U, { 1U });
    descriptors[0U] = output;

    std::vector<Process> processes;
    processes.reserve(specifications.size());
    for (std::size_t index = 0U; index < specifications.size(); ++index) {
        processes.push_back(interval_process(
            static_cast<ProcessId>(index), 0U,
            static_cast<SignalId>(index + 1U), signal_width,
            specifications[index]));
    }
    return build(processes, descriptors);
}

std::vector<std::vector<ProcessId>> certificate_members(
    const RegionGraph& graph)
{
    std::vector<std::vector<ProcessId>> result;
    for (const auto& component : graph.certificate_inventory().components) {
        result.push_back(component.members);
    }
    std::ranges::sort(result);
    return result;
}

std::vector<std::vector<ProcessId>> pairwise_interval_components(
    const RegionGraph& graph, const SignalId signal_id)
{
    const auto& signal = graph.signals()[signal_id];
    const auto process_count = graph.processes().size();
    std::vector<std::vector<ProcessId>> adjacent(process_count);
    const auto overlaps = [&](const RegionAccess& left,
                              const RegionAccess& right) {
        const auto left_begin = left.width == 0U ? 0U : left.offset;
        const auto left_end = left.width == 0U
            ? signal.descriptor.width
            : left.offset + left.width;
        const auto right_begin = right.width == 0U ? 0U : right.offset;
        const auto right_end = right.width == 0U
            ? signal.descriptor.width
            : right.offset + right.width;
        return left_begin < right_end && right_begin < left_end;
    };
    const auto accesses_overlap = [&](const std::vector<RegionAccess>& left,
                                      const ProcessId left_process,
                                      const std::vector<RegionAccess>& right,
                                      const ProcessId right_process) {
        return std::ranges::any_of(left, [&](const RegionAccess& left_access) {
            return left_access.process == left_process
                && std::ranges::any_of(right,
                    [&](const RegionAccess& right_access) {
                        return right_access.process == right_process
                            && overlaps(left_access, right_access);
                    });
        });
    };
    for (ProcessId left = 0U; left < process_count; ++left) {
        for (ProcessId right = left + 1U; right < process_count; ++right) {
            const bool connected
                = accesses_overlap(signal.writers, left,
                      signal.writers, right)
                || accesses_overlap(signal.writers, left,
                    signal.readers, right)
                || accesses_overlap(signal.writers, right,
                    signal.readers, left);
            if (connected) {
                adjacent[left].push_back(right);
                adjacent[right].push_back(left);
            }
        }
    }

    std::vector<std::uint8_t> visited(process_count, 0U);
    std::vector<std::vector<ProcessId>> result;
    for (ProcessId seed = 0U; seed < process_count; ++seed) {
        if (visited[seed] != 0U) {
            continue;
        }
        std::vector<ProcessId> members { seed };
        visited[seed] = 1U;
        for (std::size_t cursor = 0U; cursor < members.size(); ++cursor) {
            for (const auto neighbor : adjacent[members[cursor]]) {
                if (visited[neighbor] == 0U) {
                    visited[neighbor] = 1U;
                    members.push_back(neighbor);
                }
            }
        }
        std::ranges::sort(members);
        result.push_back(std::move(members));
    }
    std::ranges::sort(result);
    return result;
}

void require_pairwise_interval_agreement(
    const std::vector<IntervalProcessSpec>& specifications,
    const std::uint32_t width,
    const char* message)
{
    const auto graph = build_interval_graph(specifications, width);
    require(std::ranges::all_of(graph.processes(), &RegionProcessNode::pure)
            && graph.certificate_inventory().access_inventory_complete
            && certificate_members(graph)
                == pairwise_interval_components(graph, 0U),
        message);
}

void check_range_aware_writer_components()
{
    constexpr std::uint32_t width = 16U;
    auto selective = build_interval_graph({
        { { }, { { 0U, 4U } } },
        { { }, { { 8U, 4U } } },
        { { { 1U, 1U } }, { } },
        { { { 9U, 1U } }, { } },
        { { { 4U, 4U } }, { } },
    }, width);
    require(certificate_members(selective)
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 2U }, { 1U, 3U }, { 4U } }
            && certificate_members(selective)
                == pairwise_interval_components(selective, 0U),
        "partial readers connect only to overlapping writers, with touching endpoints separate");
    const auto invalidated = selective.observe_signal(
        0U, RegionObservation::current);
    bool every_split_component_is_stale = true;
    for (std::size_t component = 0U;
         component < selective.certificate_inventory().components.size();
         ++component) {
        every_split_component_is_stale = every_split_component_is_stale
            && !selective.component_epochs_current(component);
    }
    require(std::ranges::equal(invalidated,
                std::array<ProcessId, 5U> { 0U, 1U, 2U, 3U, 4U })
            && every_split_component_is_stale,
        "a later signal observation invalidates every split component using that signal");

    require_pairwise_interval_agreement({
        { { }, { { 0U, 4U } } },
        { { }, { { 8U, 4U } } },
        { { { 2U, 8U } }, { } },
        { { { 4U, 4U } }, { } },
    }, width,
        "the pairwise reference preserves a bridge read and an unmatched reader");
    require_pairwise_interval_agreement({
        { { }, { { 0U, 3U } } },
        { { }, { { 6U, 3U } } },
        { { }, { { 12U, 3U } } },
        { { { 2U, 5U } }, { } },
        { { { 8U, 5U } }, { } },
        { { { 3U, 3U } }, { } },
    }, width,
        "the pairwise reference preserves transitive reader bridges and endpoint-only separation");
    require_pairwise_interval_agreement({
        { { }, { { 0U, 2U }, { 8U, 2U } } },
        { { { 0U, 1U } }, { } },
        { { { 8U, 1U } }, { } },
        { { { 4U, 2U } }, { } },
    }, width,
        "one process identity joins readers of its disjoint write intervals");

    const auto whole_reader = build_interval_graph({
        { { }, { { 0U, 4U } } },
        { { }, { { 8U, 4U } } },
        { { { 0U, 0U } }, { } },
    }, width);
    require(certificate_members(whole_reader)
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U, 2U } }
            && certificate_members(whole_reader)
                == pairwise_interval_components(whole_reader, 0U),
        "a whole-signal reader joins every disjoint writer cluster");

    const auto whole_writer = build_interval_graph({
        { { }, { { 0U, 0U } } },
        { { { 14U, 1U } }, { } },
    }, width);
    require(certificate_members(whole_writer)
                == std::vector<std::vector<ProcessId>> { { 0U, 1U } }
            && certificate_members(whole_writer)
                == pairwise_interval_components(whole_writer, 0U),
        "a width-zero writer interval covers every finite reader range");

    const auto overlapping_writer_fallback = build_interval_graph({
        { { }, { { 0U, 4U } } },
        { { }, { { 3U, 3U } } },
        { { }, { { 5U, 3U } } },
        { { }, { { 8U, 2U } } },
        { { { 12U, 2U } }, { } },
    }, width);
    require(certificate_members(overlapping_writer_fallback)
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U, 2U, 3U, 4U } },
        "resolved overlapping writers keep the conservative writer-star grouping");

    const auto observed_fallback = build_interval_graph({
        { { }, { { 0U, 2U } } },
        { { }, { { 8U, 2U } } },
        { { { 4U, 2U } }, { } },
    }, width, ResolutionKind::sv_wire, RegionObservation::current);
    require(certificate_members(observed_fallback)
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U, 2U } },
        "observed signals retain the conservative writer-star grouping");

    const auto unsupported_fallback = build_interval_graph({
        { { }, { { 0U, 2U } } },
        { { }, { { 8U, 2U } } },
        { { { 4U, 2U } }, { } },
    }, width, ResolutionKind::sv_wand);
    require(certificate_members(unsupported_fallback)
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U, 2U } },
        "unsupported resolution retains the conservative writer-star grouping");

    auto opaque_process = transfer(2U, 4U, 1U);
    ClassStaticMethodCall opaque_effect;
    opaque_effect.method_identity = "@dpi:range_component_fallback";
    opaque_process.operations.insert(opaque_process.operations.end() - 2,
        std::move(opaque_effect));
    auto incomplete_access = interval_process(
        0U, 0U, 2U, width, { { }, { { 0U, 2U } } });
    auto second_incomplete_writer = interval_process(
        1U, 0U, 3U, width, { { }, { { 8U, 2U } } });
    const std::vector<Process> incomplete_processes {
        incomplete_access, second_incomplete_writer, opaque_process
    };
    std::vector<const Process*> incomplete_bindings;
    for (const auto& process : incomplete_processes) {
        incomplete_bindings.push_back(&process);
    }
    std::vector<RegionSignalDescriptor> incomplete_signals {
        { width, ResolutionKind::sv_wire }, { 1U }, { 1U }, { 1U }, { 1U }
    };
    const auto incomplete_inventory_graph
        = RegionGraph::build(incomplete_bindings, incomplete_signals);
    require(!incomplete_inventory_graph.certificate_inventory()
                .access_inventory_complete
            && certificate_members(incomplete_inventory_graph)
                == std::vector<std::vector<ProcessId>> { { 0U, 1U } },
        "an incomplete global access inventory retains conservative writer-star grouping");

    const std::vector<RegionSignalDescriptor> alias_signals {
        { 4U, ResolutionKind::sv_wire },
        { 4U, ResolutionKind::sv_wire },
        { 1U }, { 1U },
    };
    const std::vector<RegionContainerDescriptor> alias_containers {
        { 0U, {
            { 0U, true, true, true },
            { 1U, true, true, false },
        }, true },
    };
    const std::vector<RegionSignalAliasFamilyDescriptor> alias_families {
        { 0U, 0U, 4U, { { 1U, 0U, 0U, 4U } },
            true, true, true },
    };
    const std::vector<IntervalProcessSpec> alias_writers {
        { { }, { { 0U, 1U } } },
        { { }, { { 2U, 1U } } },
    };
    std::vector<Process> alias_processes;
    for (std::size_t index = 0U; index < alias_writers.size(); ++index) {
        alias_processes.push_back(interval_process(
            static_cast<ProcessId>(index), 1U,
            static_cast<SignalId>(index + 2U), 4U,
            alias_writers[index]));
    }
    std::vector<const Process*> alias_bindings;
    for (const auto& process : alias_processes) {
        alias_bindings.push_back(&process);
    }
    auto alias_graph = RegionGraph::build(alias_bindings, alias_signals,
        false, alias_containers, { }, alias_families);
    require(certificate_members(alias_graph)
                == std::vector<std::vector<ProcessId>> { { 0U, 1U } }
            && alias_graph.component_epochs_current(0U),
        "aliased leaf signals retain conservative writer-star grouping");
    static_cast<void>(alias_graph.observe_signal(
        0U, RegionObservation::current));
    require(!alias_graph.component_epochs_current(0U),
        "an alias-family observation invalidates the retained component");

    constexpr std::uint32_t generated_width = 64U;
    for (std::size_t case_index = 0U; case_index < 32U; ++case_index) {
        std::vector<IntervalProcessSpec> specifications;
        const auto writer_count = 1U + case_index % 4U;
        for (std::size_t writer = 0U; writer < writer_count; ++writer) {
            const auto offset = static_cast<std::uint32_t>(
                writer * 12U + (case_index * 3U + writer * 5U) % 5U);
            const auto write_width = static_cast<std::uint32_t>(
                1U + (case_index + writer * 3U) % 4U);
            specifications.push_back({ { }, { { offset, write_width } } });
        }
        for (std::size_t reader = 0U; reader < 5U; ++reader) {
            IntervalProcessSpec read_specification;
            if ((case_index + reader) % 11U == 0U) {
                read_specification.reads.push_back({ 0U, 0U });
            } else {
                const auto read_width = static_cast<std::uint32_t>(
                    1U + (case_index * 5U + reader * 3U) % 12U);
                auto offset = static_cast<std::uint32_t>(
                    (case_index * 7U + reader * 11U) % generated_width);
                if (read_width > generated_width - offset) {
                    offset = generated_width - read_width;
                }
                read_specification.reads.push_back({ offset, read_width });
            }
            if (reader == 0U && case_index % 2U != 0U) {
                const auto offset = static_cast<std::uint32_t>(
                    (case_index * 13U) % (generated_width - 3U));
                read_specification.reads.push_back({ offset, 2U });
            }
            specifications.push_back(std::move(read_specification));
        }
        require_pairwise_interval_agreement(specifications,
            generated_width,
            "generated disjoint-writer interval sets match the pairwise reference");
    }
}

void check_read_only_systemverilog_member()
{
    auto reader = transfer(1U, 1U, 0U);
    reader.driver_regions.clear();
    reader.operations = {
        ReadSignal { 0U, 1U },
        WaitSensitivity { },
        Jump { 0U },
    };
    std::vector<Process> processes { transfer(0U, 0U, 1U), reader };
    const std::vector<RegionSignalDescriptor> signals { { 1U }, { 1U } };
    const auto graph = build(processes, signals);
    const auto& components = graph.certificate_inventory().components;
    require(graph.processes()[1U].pure
            && graph.processes()[1U].writes.empty()
            && components.size() == 1U
            && components[0U].members == std::vector<ProcessId> { 0U, 1U }
            && components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U },
        "a pure read-only SV member retains its producer's internal signal");
    const std::array<const Process*, 2U> bindings {
        &processes[0U], &processes[1U]
    };
    const auto program = graph.build_compute_program(0U, bindings);
    require(program.has_value()
            && program->activation_kernel.members.size() == 2U
            && program->activation_kernel.outputs.size() == 1U
            && program->activation_kernel.outputs.front().owner == 0U,
        "read-only members compile without inventing output drivers");

    processes[1U].scheduling_domain = ProcessSchedulingDomain::generic;
    require(!build(processes, signals).processes()[1U].pure,
        "a generic reader without update provenance stays on the ordinary route");
    processes[1U] = reader;
    processes[1U].operations.replace(1U, WaitFor { 1U });
    require(!build(processes, signals).processes()[1U].pure,
        "a delayed reader cannot enter the pure static region");
    processes[1U] = reader;
    processes[1U].driver_regions = { { 1U, 0U, 0U, true } };
    require(!build(processes, signals).processes()[1U].pure,
        "a declared driver without a matching update is not a read-only member");
}

std::string cone_logic9_pattern()
{
    constexpr char states[] = "UX01ZWLH-";
    constexpr std::size_t width = 129U;
    std::string result;
    result.reserve(width);
    for (std::size_t index = 0U; index < width; ++index) {
        result.push_back(states[index % (sizeof(states) - 1U)]);
    }
    return result;
}

std::vector<Signal> cone_fixture_signals()
{
    const auto wide_input = cone_logic9_pattern();
    return {
        Signal { "cone.input", PackedLogic4::from_msb_string("0Z") },
        Signal { "cone.internal", PackedLogic4::from_msb_string("XX") },
        Signal { "cone.output_a", PackedLogic4::from_msb_string("XX") },
        Signal { "cone.output_b", PackedLogic4::from_msb_string("XX") },
        Signal { "cone.wide_input",
            PackedLogic4::from_logic9_msb_string(wide_input),
            ResolutionKind::none, ValueKind::logic9 },
        Signal { "cone.wide_output",
            PackedLogic4 { 129U, Logic4::x },
            ResolutionKind::none, ValueKind::logic9 },
    };
}

std::vector<RegionSignalDescriptor> cone_fixture_descriptors()
{
    std::vector<RegionSignalDescriptor> descriptors {
        { 2U }, { 2U }, { 2U }, { 2U },
        { 129U, ResolutionKind::none, ValueKind::logic9 },
        { 129U, ResolutionKind::none, ValueKind::logic9 },
    };
    descriptors[2U].observations = RegionObservation::current;
    descriptors[3U].observations = RegionObservation::current;
    descriptors[5U].observations = RegionObservation::current;
    return descriptors;
}

std::vector<Process> cone_fixture_processes()
{
    Process output_a;
    output_a.id = 0U;
    output_a.name = "cone_output_a";
    output_a.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    output_a.register_count = 1U;
    output_a.static_sensitivity = { { 1U, EdgeKind::any } };
    output_a.driver_regions = { { 2U, 0U, 0U, true } };
    output_a.operations = {
        ReadSignal { 0U, 1U },
        WriteUpdate { 2U, 0U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 0U, PackedLogic4::from_msb_string("00") },
        WriteUpdate { 2U, 0U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 0U, PackedLogic4::from_msb_string("11") },
        WaitSensitivity { }, Jump { 0U },
    };

    Process output_b;
    output_b.id = 1U;
    output_b.name = "cone_output_b";
    output_b.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    output_b.register_count = 2U;
    output_b.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4
    };
    output_b.static_sensitivity = { { 1U, EdgeKind::any } };
    output_b.driver_regions = { { 3U, 0U, 0U, true } };
    output_b.operations = {
        ReadSignal { 0U, 1U },
        UnaryNot { 1U, 0U },
        WriteUpdate { 3U, 1U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 1U, PackedLogic4::from_msb_string("00") },
        WaitSensitivity { }, Jump { 0U },
    };

    Process producer;
    producer.id = 2U;
    producer.name = "cone_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 3U;
    producer.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic9
    };
    producer.static_sensitivity = {
        { 0U, EdgeKind::any }, { 4U, EdgeKind::any }
    };
    producer.driver_regions = {
        { 1U, 0U, 0U, true }, { 5U, 0U, 0U, true }
    };
    std::string overwritten_wide(129U, 'L');
    producer.operations = {
        ReadSignal { 0U, 0U },
        UnaryNot { 1U, 0U },
        WriteUpdate { 1U, 1U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 1U, PackedLogic4::from_msb_string("00") },
        ReadSignal { 2U, 4U },
        WriteUpdate { 5U, 2U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 2U,
            PackedLogic4::from_logic9_msb_string(overwritten_wide) },
        WaitSensitivity { }, Jump { 0U },
    };
    return { std::move(output_a), std::move(output_b), std::move(producer) };
}

std::vector<const Process*> bind_processes(
    const std::vector<Process>& processes)
{
    std::vector<const Process*> result;
    result.reserve(processes.size());
    for (const auto& process : processes) {
        result.push_back(&process);
    }
    return result;
}

std::vector<std::string> cone_operation_signature(const Process& process)
{
    std::vector<std::string> result;
    result.reserve(process.operations.size());
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        result.push_back(visit_operation([&](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            auto text = std::string { typeid(Type).name() };
            const auto append = [&](const auto field) {
                text += ':';
                text += std::to_string(field);
            };
            if constexpr (std::is_same_v<Type, LoadConstant>) {
                append(value.destination);
                text += ':';
                text += value.value.to_msb_string();
            } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                append(value.destination);
                append(value.signal);
                append(static_cast<unsigned>(value.kind));
                append(value.ticks);
                append(value.clock.value_or(
                    std::numeric_limits<SignalId>::max()));
                append(value.gate.value_or(
                    std::numeric_limits<SignalId>::max()));
            } else if constexpr (std::is_same_v<Type, CopyRegister>) {
                append(value.destination);
                append(value.source);
            } else if constexpr (std::is_same_v<Type, UnaryNot>) {
                append(value.destination);
                append(value.source);
            } else if constexpr (std::is_same_v<Type, Binary>) {
                append(static_cast<unsigned>(value.operation));
                append(value.destination);
                append(value.lhs);
                append(value.rhs);
            } else if constexpr (std::is_same_v<Type, Reduction>) {
                append(static_cast<unsigned>(value.operation));
                append(value.destination);
                append(value.source);
            } else if constexpr (std::is_same_v<Type, Extract>) {
                append(value.destination);
                append(value.source);
                append(value.offset);
                append(value.width);
            } else if constexpr (std::is_same_v<Type, Concatenate>) {
                append(value.destination);
                append(value.width);
                for (const auto operand : value.operands) {
                    append(operand);
                }
            } else if constexpr (std::is_same_v<Type, Halt>) {
                append(value.program_exit);
            } else if constexpr (std::is_same_v<Type, Branch>) {
                append(value.condition);
                append(value.when_true);
                append(value.when_false);
                append(static_cast<unsigned>(value.unknown_policy));
            }
            return text;
        }, operation));
    }
    return result;
}

std::vector<PackedLogic4> execute_region_activation_kernel(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image,
    const std::vector<Signal>& signals)
{
    auto process = kernel.program;
    process.operations = OperationList { };
    const auto prefix_size = image.register_inputs.size();
    if (prefix_size > std::numeric_limits<InstructionIndex>::max()) {
        throw std::runtime_error {
            "region kernel test input prefix exceeds instruction range"
        };
    }
    for (const auto& input : image.register_inputs) {
        process.operations.push_back(LoadConstant {
            input.register_id, input.value });
    }
    for (std::size_t index = 0U; index < kernel.program.operations.size(); ++index) {
        auto operation = kernel.program.operations.expanded(index);
        if (auto* branch = operation_get_if<Branch>(&operation)) {
            branch->when_true += static_cast<InstructionIndex>(prefix_size);
            branch->when_false += static_cast<InstructionIndex>(prefix_size);
        }
        process.operations.push_back(std::move(operation));
    }

    std::map<std::pair<ProcessId, InstructionIndex>, std::size_t>
        debug_by_output;
    for (const auto& output : kernel.outputs) {
        DebugLocal local;
        local.name = "activation_output_" + std::to_string(output.owner)
            + "_" + std::to_string(output.source_instruction);
        local.type_name = "logic";
        local.register_id = output.value_register;
        local.width = output.width;
        local.value_kind = output.value_kind;
        const auto index = process.debug_locals.size();
        process.debug_locals.push_back(std::move(local));
        debug_by_output.emplace(
            std::pair { output.owner, output.source_instruction }, index);
    }

    Interpreter interpreter;
    for (const auto& signal : signals) {
        static_cast<void>(interpreter.add_signal(signal));
    }
    const auto process_id = interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(result.status == RunStatus::completed,
        "readiness-masked activation kernel must complete as a one-shot body");

    std::vector<PackedLogic4> registers(kernel.program.register_count,
        PackedLogic4 { 1U, Logic4::x });
    for (const auto& output : kernel.outputs) {
        if (std::ranges::find(image.ready_processes, output.owner)
            == image.ready_processes.end()) {
            continue;
        }
        const auto index = debug_by_output.at(
            { output.owner, output.source_instruction });
        registers[output.value_register]
            = interpreter.read_debug_local(process_id, index);
    }
    return registers;
}

void check_compute_cone_program()
{
    const auto signals = cone_fixture_signals();
    const auto descriptors = cone_fixture_descriptors();
    const auto processes = cone_fixture_processes();
    const auto bindings = bind_processes(processes);
    const auto graph = RegionGraph::build(bindings, descriptors);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components[0U].members
                == std::vector<ProcessId> { 0U, 1U, 2U }
            && components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U },
        "fanout fixture must have one certified internal net and boundary outputs");

    const auto cone = graph.build_compute_program(0U, bindings);
    require(cone.has_value()
            && cone->members == std::vector<ProcessId> { 2U, 0U, 1U }
            && cone->member_spans.size() == cone->members.size()
            && cone->boundary_sensitivities
                == std::vector<Sensitivity> {
                    { 0U, EdgeKind::any }, { 4U, EdgeKind::any }
                }
            && cone->internal_materializations.size() == 1U
            && cone->internal_materializations[0U].owner == 2U
            && cone->internal_materializations[0U].signal == 1U
            && cone->internal_materializations[0U].width == 2U
            && cone->internal_materializations[0U].value_kind
                == ValueKind::logic4
            && cone->boundary_outputs.size() == 4U,
        "cone builder retains dependency order, internal identity, and original boundaries");

    const auto& kernel = cone->activation_kernel;
    struct ExpectedOutputBinding {
        ProcessId owner;
        InstructionIndex source_instruction;
        SignalId signal;
    };
    constexpr std::array expected_output_bindings {
        ExpectedOutputBinding { 0U, 1U, 2U },
        ExpectedOutputBinding { 0U, 3U, 2U },
        ExpectedOutputBinding { 1U, 2U, 3U },
        ExpectedOutputBinding { 2U, 2U, 1U },
        ExpectedOutputBinding { 2U, 5U, 5U },
    };
    require(kernel.outputs.size() == expected_output_bindings.size()
            && std::equal(kernel.outputs.begin(), kernel.outputs.end(),
                expected_output_bindings.begin(),
                [](const RegionConeOutputBinding& output,
                    const ExpectedOutputBinding& expected) {
                    return output.owner == expected.owner
                        && output.source_instruction
                            == expected.source_instruction
                        && output.signal == expected.signal;
                }),
        "output bindings retain owner/source order across a topological reorder, "
        "including internal and boundary writes around op gaps");

    std::vector<RegionConeOutputBinding> outputs = cone->boundary_outputs;
    const auto output_a = std::ranges::find(outputs, 2U,
        &RegionConeOutputBinding::signal);
    const auto output_b = std::ranges::find(outputs, 3U,
        &RegionConeOutputBinding::signal);
    const auto wide_output = std::ranges::find(outputs, 5U,
        &RegionConeOutputBinding::signal);
    require(output_a != outputs.end() && output_a->owner == 0U,
        "first repeated boundary write retains its original owner");
    const auto second_output_a = std::ranges::find_if(outputs,
        [&](const RegionConeOutputBinding& output) {
            return output.signal == 2U && output.owner == 0U
                && output.source_instruction != output_a->source_instruction;
        });
    require(second_output_a != outputs.end()
            && second_output_a->owner == output_a->owner
            && second_output_a->value_register != output_a->value_register
            && output_b != outputs.end() && output_b->owner == 1U
            && wide_output != outputs.end() && wide_output->owner == 2U
            && wide_output->value_kind == ValueKind::logic9
            && std::ranges::all_of(outputs, [](const auto& output) {
                return output.domain
                    == SignalUpdateDomain::systemverilog_active
                    && output.offset == 0U;
            }),
        "boundary values must retain each original writer and publication domain");

    std::vector<RegisterId> snapshot_registers {
        cone->internal_materializations[0U].value_register
    };
    for (const auto& output : cone->boundary_outputs) {
        snapshot_registers.push_back(output.value_register);
    }
    std::ranges::sort(snapshot_registers);
    require(std::ranges::all_of(snapshot_registers,
                [](const RegisterId value_register) {
                    return value_register >= 6U;
                })
            && std::ranges::adjacent_find(snapshot_registers)
                == snapshot_registers.end()
            && std::ranges::all_of(cone->boundary_outputs,
                [&](const RegionConeOutputBinding& output) {
                    return kernel.program.register_value_kinds.at(
                        output.value_register) == output.value_kind;
                })
            && kernel.program.register_value_kinds.at(
                cone->internal_materializations[0U].value_register)
                == cone->internal_materializations[0U].value_kind,
        "every materialization and output write gets a dedicated typed snapshot");

    for (const auto& span : cone->member_spans) {
        const auto& source = processes.at(span.process);
        require(span.begin <= span.end
                && span.end - span.begin == source.operations.size() - 2U
                && span.initialize == source.initialize
                && !span.sensitivities.empty(),
            "member spans retain source initialization and activation metadata");
    }
    for (const auto& output : cone->boundary_outputs) {
        const auto owner_span = std::ranges::find(cone->member_spans,
            output.owner, &RegionConeMemberSpan::process);
        require(owner_span != cone->member_spans.end()
                && output.compute_instruction >= owner_span->begin
                && output.compute_instruction < owner_span->end,
            "each output snapshot remains associated with its source member span");
    }
    require(std::ranges::none_of(kernel.program.operations,
                [](const Operation& operation) {
                    return operation_holds<WriteUpdate>(operation);
                })
            && operation_holds<Halt>(kernel.program.operations.expanded(
                kernel.program.operations.size() - 1U)),
        "compute body captures output snapshots and stops without publishing them");

    const auto input_pattern = cone_logic9_pattern();
    constexpr char logic9_states[] = "UX01ZWLH-";
    require(input_pattern.size() == 129U,
        "wide Logic9 fixture must span more than two packed words");
    for (std::size_t index = 0U;
         index + 1U < sizeof(logic9_states); ++index) {
        require(input_pattern.find(logic9_states[index]) != std::string::npos,
            "wide Logic9 fixture must cover all nine valid states");
    }
    const auto expected_wide
        = PackedLogic4::from_logic9_msb_string(input_pattern);
    Interpreter reference;
    for (const auto& signal : signals) {
        static_cast<void>(reference.add_signal(signal));
    }
    for (const auto& process : processes) {
        static_cast<void>(reference.add_process(process));
    }
    const auto reference_result = reference.run();
    require(reference_result.status == RunStatus::completed
            && reference.signal_value(1U).to_msb_string() == "1X"
            && reference.signal_value(2U).to_msb_string() == "00"
            && reference.signal_value(3U).to_msb_string() == "0X"
            && reference.signal_value(5U) == expected_wide,
        "ordinary interpreter fixture must settle narrow and wide four-state outputs");

    const auto repeated_graph = RegionGraph::build(bindings, descriptors);
    const auto repeated = repeated_graph.build_compute_program(0U, bindings);
    require(repeated.has_value()
            && repeated->members == cone->members
            && repeated->member_spans == cone->member_spans
            && repeated->boundary_sensitivities
                == cone->boundary_sensitivities
            && repeated->boundary_outputs == cone->boundary_outputs
            && repeated->internal_materializations
                == cone->internal_materializations
            && repeated->activation_kernel.program.name
                == kernel.program.name
            && repeated->activation_kernel.program.register_count
                == kernel.program.register_count
            && repeated->activation_kernel.program.register_value_kinds
                == kernel.program.register_value_kinds
            && repeated->activation_kernel.members
                == cone->activation_kernel.members
            && repeated->activation_kernel.inputs
                == cone->activation_kernel.inputs
            && repeated->activation_kernel.outputs
                == cone->activation_kernel.outputs
            && cone_operation_signature(repeated->activation_kernel.program)
                == cone_operation_signature(kernel.program),
        "repeated cone construction must be deterministic down to register rewrites");

    require(kernel.members.size() == 3U
            && kernel.members[0U].process == 0U
            && kernel.members[1U].process == 1U
            && kernel.members[2U].process == 2U
            && kernel.inputs.size() == 3U
            && std::ranges::all_of(kernel.members,
                [&](const RegionConeKernelMember& member) {
                    const auto operation = kernel.program.operations.expanded(
                        member.branch_instruction);
                    const auto* branch = operation_get_if<Branch>(&operation);
                    return branch != nullptr
                        && branch->condition == member.readiness_register
                        && branch->when_true == member.begin
                        && branch->when_false == member.end;
                }),
        "one stable activation kernel must retain per-member readiness blocks");
    for (const auto signal : kernel.internal_signals) {
        const auto input = std::ranges::find(kernel.inputs, signal,
            &RegionConeKernelInput::signal);
        const auto output = std::ranges::find(kernel.outputs, signal,
            &RegionConeOutputBinding::signal);
        require(input != kernel.inputs.end() && input->internal
                && output != kernel.outputs.end()
                && input->value_register != output->value_register,
            "internal current and pending-write slots must be distinct");
    }
    require(std::ranges::none_of(kernel.program.operations,
                [](const Operation& operation) {
                    return operation_holds<ReadSignal>(operation)
                        || operation_holds<WriteUpdate>(operation);
                })
            && std::ranges::any_of(kernel.program.operations,
                [&](const Operation& operation) {
                    const auto* copy = operation_get_if<CopyRegister>(
                        &operation);
                    const auto input = std::ranges::find(kernel.inputs,
                        1U, &RegionConeKernelInput::signal);
                    return copy != nullptr && input != kernel.inputs.end()
                        && copy->source == input->value_register;
                }),
        "runtime kernel reads captured planes and stages writes without publishing");

    std::vector<PackedLogic4> current_values;
    current_values.reserve(signals.size());
    for (const auto& signal : signals) {
        current_values.push_back(signal.initial_value);
    }
    const auto snapshot_boundary_inputs = [&] {
        std::vector<PackedLogic4> result;
        result.reserve(kernel.inputs.size());
        for (const auto& input : kernel.inputs) {
            if (!input.internal) {
                result.push_back(current_values[input.signal]);
            }
        }
        return result;
    };
    auto current_boundary_inputs = snapshot_boundary_inputs();
    const std::array internal_seed {
        RegionKernelInternalSeed { 1U, 2U,
            PackedLogic4::from_msb_string("XX"),
            PackedLogic4::from_msb_string("XZ"),
            PackedLogic4::from_msb_string("XX") },
    };
    RegionKernelActivationState activation { kernel, internal_seed };
    const auto origin = [](const StableOrder order,
                            const std::uint64_t sequence,
                            const std::uint64_t delta,
                            const std::uint64_t round) {
        return RegionKernelActivationOrigin {
            ProcessSchedulingDomain::systemverilog, SchedulerPhase::active,
            0U, delta, order, sequence, round };
    };
    const auto make_scheduler_prefix = [](
        const std::uint64_t frontier_generation,
        const std::size_t frontier_cursor,
        const std::size_t frontier_end,
        const std::span<const RegionKernelReadyMember> members) {
        if (members.empty()) {
            throw std::runtime_error {
                "activation test prefix needs at least one scheduler task"
            };
        }
        std::vector<RegionKernelReadyMember> ordered_members {
            members.begin(), members.end() };
        std::ranges::sort(ordered_members, [](const auto& left,
                                                const auto& right) {
            return std::pair {
                left.origin.stable_order, left.origin.sequence }
                < std::pair {
                    right.origin.stable_order, right.origin.sequence };
        });
        const auto& first = ordered_members.front().origin;
        RegionKernelSchedulerPrefix prefix;
        prefix.frontier_generation = frontier_generation;
        prefix.frontier_cursor = frontier_cursor;
        prefix.frontier_end = frontier_end;
        prefix.time = first.time;
        prefix.delta = first.delta;
        prefix.phase = first.phase;
        prefix.systemverilog_round = first.systemverilog_round;
        prefix.tasks.reserve(ordered_members.size());
        for (std::size_t index = 0U; index < ordered_members.size(); ++index) {
            prefix.tasks.push_back({ frontier_cursor + index,
                ordered_members[index] });
        }
        return prefix;
    };
    const std::array first_ready {
        RegionKernelReadyMember { 2U, Process::full_static_trigger_mask,
            origin(10U, 3U, 0U, 7U) },
        RegionKernelReadyMember { 0U, Process::full_static_trigger_mask,
            origin(20U, 4U, 0U, 7U) },
        RegionKernelReadyMember { 1U, Process::full_static_trigger_mask,
            origin(5U, 2U, 0U, 7U) },
    };
    const auto first_prefix = make_scheduler_prefix(
        7U, 0U, 4U, first_ready);
    auto gapped_prefix = first_prefix;
    ++gapped_prefix.tasks[1U].task_ordinal;
    bool rejected_gapped_prefix { };
    try {
        static_cast<void>(activation.begin_wave(
            gapped_prefix, current_boundary_inputs));
    } catch (const std::invalid_argument&) {
        rejected_gapped_prefix = true;
    }
    require(rejected_gapped_prefix,
        "activation recovers after rejecting a scheduler prefix with a hole");

    const auto first_image = activation.begin_wave(
        first_prefix, current_boundary_inputs);
    require(first_image.ready_processes
                == std::vector<ProcessId> { 1U, 2U, 0U }
            && first_image.active_member_indices
                == std::vector<std::size_t> { 0U, 1U, 2U }
            && first_image.requests[0U].origin.stable_order == 5U
            && first_image.scheduler_prefix.frontier_generation == 7U
            && first_image.scheduler_prefix.frontier_cursor == 0U
            && first_image.scheduler_prefix.frontier_end == 4U
            && first_image.scheduler_prefix.tasks[0U].task_ordinal == 0U
            && first_image.scheduler_prefix.tasks[2U].task_ordinal == 2U,
        "activation retains the exact contiguous scheduler span and ready mask");
    const auto internal_input = std::ranges::find(kernel.inputs, 1U,
        &RegionConeKernelInput::signal);
    const auto first_internal_input = internal_input == kernel.inputs.end()
        ? first_image.register_inputs.end()
        : std::ranges::find(first_image.register_inputs,
              internal_input->value_register,
              &RegionKernelRegisterInput::register_id);
    require(internal_input != kernel.inputs.end()
            && first_internal_input != first_image.register_inputs.end()
            && first_internal_input->value
                == PackedLogic4::from_msb_string("XX"),
        "all simultaneous members read committed internal current at wave entry");

    const auto first_registers = execute_region_activation_kernel(
        kernel, first_image, signals);
    activation.stage_kernel_outputs(first_image, first_registers);
    const auto first_publications = activation.pending_publications();
    require(first_publications.size() == 5U
            && first_publications[0U].binding.owner == 1U
            && first_publications[1U].binding.owner == 2U
            && first_publications[1U].binding.signal == 1U
            && first_publications[1U].value.to_msb_string() == "1X"
            && first_publications[2U].binding.owner == 2U
            && first_publications[2U].binding.signal == 5U
            && first_publications[2U].value == expected_wide
            && first_publications[3U].binding.owner == 0U
            && first_publications[3U].binding.source_instruction == 1U
            && first_publications[4U].binding.owner == 0U
            && first_publications[4U].binding.source_instruction == 3U
            && first_publications[3U].value.to_msb_string() == "XX"
            && first_publications[4U].value.to_msb_string() == "00"
            && first_publications[3U].origin == first_ready[1U].origin
            && first_publications[4U].origin == first_ready[1U].origin,
        "pending outputs retain original owners, operation order, and captured origins");

    for (std::size_t index = 0U; index < first_publications.size(); ++index) {
        activation.begin_raw_publication(index);
        if (index == 1U) {
            const auto& before_current = activation.internal_state(1U);
            require(before_current.raw_driver.to_msb_string() == "1X"
                    && before_current.current.to_msb_string() == "XX"
                    && before_current.previous.to_msb_string() == "XZ",
                "raw owner state advances before current/previous publication");
        }
        const auto& publication = first_publications[index];
        activation.publish_current(index, publication.value);
        if (publication.binding.signal != 1U) {
            current_values[publication.binding.signal] = publication.value;
        }
    }
    activation.complete_wave();
    require(activation.internal_state(1U).current.to_msb_string() == "1X"
            && activation.internal_state(1U).previous.to_msb_string() == "XX"
            && activation.internal_state(1U).raw_driver.to_msb_string() == "1X",
        "committed, previous, and original-owner raw planes remain distinct");
    bool rejected_replayed_prefix { };
    try {
        current_boundary_inputs = snapshot_boundary_inputs();
        static_cast<void>(activation.begin_wave(
            first_prefix, current_boundary_inputs));
    } catch (const std::invalid_argument&) {
        rejected_replayed_prefix = true;
    }
    require(rejected_replayed_prefix,
        "activation rejects a consumed scheduler prefix after publication");

    const std::array second_ready {
        RegionKernelReadyMember { 0U, Process::full_static_trigger_mask,
            origin(20U, 6U, 1U, 8U) },
        RegionKernelReadyMember { 1U, Process::full_static_trigger_mask,
            origin(5U, 5U, 1U, 8U) },
        RegionKernelReadyMember { 2U, Process::full_static_trigger_mask,
            origin(50U, 9U, 1U, 8U) },
    };
    const auto second_prefix = make_scheduler_prefix(
        8U, 0U, 3U, second_ready);
    current_boundary_inputs = snapshot_boundary_inputs();
    const auto second_image = activation.begin_wave(
        second_prefix, current_boundary_inputs);
    const auto second_input = std::ranges::find(second_image.register_inputs,
        internal_input->value_register,
        &RegionKernelRegisterInput::register_id);
    require(second_input != second_image.register_inputs.end()
            && second_input->value.to_msb_string() == "1X"
            && second_image.ready_processes
                == std::vector<ProcessId> { 1U, 0U, 2U },
        "next delta reads the committed value using only the current readiness mask");
    const auto second_registers = execute_region_activation_kernel(
        kernel, second_image, signals);
    activation.stage_kernel_outputs(second_image, second_registers);
    const auto second_publications = activation.pending_publications();
    const auto output_a_first = std::ranges::find_if(second_publications,
        [](const RegionKernelPendingPublication& publication) {
            return publication.binding.owner == 0U
                && publication.binding.signal == 2U
                && publication.binding.source_instruction == 1U;
        });
    const auto second_output_b = std::ranges::find_if(second_publications,
        [](const RegionKernelPendingPublication& publication) {
            return publication.binding.owner == 1U
                && publication.binding.signal == 3U;
        });
    require(output_a_first != second_publications.end()
            && output_a_first->value.to_msb_string() == "1X"
            && second_output_b != second_publications.end()
            && second_output_b->value.to_msb_string() == "0X",
        "consumer activations observe internal state only after its owner publication");
    for (std::size_t index = 0U; index < second_publications.size(); ++index) {
        activation.begin_raw_publication(index);
        const auto& publication = second_publications[index];
        activation.publish_current(index, publication.value);
        if (publication.binding.signal != 1U) {
            current_values[publication.binding.signal] = publication.value;
        }
    }
    activation.complete_wave();
    require(activation.internal_state(1U).current == reference.signal_value(1U)
            && current_values[2U] == reference.signal_value(2U)
            && current_values[3U] == reference.signal_value(3U)
            && current_values[5U] == reference.signal_value(5U)
            && current_values[5U] == expected_wide,
        "two exact activation waves match the ordinary interpreter, including wide Logic9");
    require(activation.internal_state(1U).current.to_msb_string() == "1X"
            && activation.internal_state(1U).previous.to_msb_string() == "XX",
        "same-value publication acknowledges its transaction without advancing LAST");

    const std::array third_ready {
        RegionKernelReadyMember { 1U, Process::full_static_trigger_mask,
            origin(5U, 10U, 2U, 9U) },
    };
    const auto third_prefix = make_scheduler_prefix(
        9U, 0U, 2U, third_ready);
    current_boundary_inputs = snapshot_boundary_inputs();
    const auto third_image = activation.begin_wave(
        third_prefix, current_boundary_inputs);
    require(third_image.ready_processes == std::vector<ProcessId> { 1U },
        "a partial readiness mask selects only its original member");
    const auto third_registers = execute_region_activation_kernel(
        kernel, third_image, signals);
    activation.stage_kernel_outputs(third_image, third_registers);
    const auto third_publications = activation.pending_publications();
    require(third_publications.size() == 1U
            && third_publications[0U].binding.owner == 1U
            && third_publications[0U].binding.signal == 3U
            && third_publications[0U].value == reference.signal_value(3U),
        "inactive members produce no publications or initialized output reads");
    activation.begin_raw_publication(0U);
    activation.publish_current(0U, third_publications[0U].value);
    activation.complete_wave();
    require(activation.internal_state(1U).current.to_msb_string() == "1X"
            && activation.internal_state(1U).previous.to_msb_string() == "XX",
        "a consumer-only activation leaves internal current and LAST untouched");
}

void check_compute_cone_debug_markers()
{
    const auto signals = cone_fixture_signals();
    const auto descriptors = cone_fixture_descriptors();
    auto processes = cone_fixture_processes();
    const auto add_markers = [](Process& process,
                                const std::string_view prefix) {
        const auto wait_index = process.operations.size() - 2U;
        process.operations.insert(
            process.operations.cbegin()
                + static_cast<std::ptrdiff_t>(wait_index),
            DebugPoint { DebugPointKind::statement,
                SourceLocation { std::string { prefix } + ".sv", 10U, 2U },
                std::string { prefix } + ".early" });
        process.operations.insert(process.operations.cbegin(),
            DebugPoint { DebugPointKind::process_entry,
                SourceLocation { std::string { prefix } + ".sv", 1U, 1U },
                std::string { prefix } + ".entry" });
    };
    add_markers(processes[0U], "cone.consumer");
    add_markers(processes[2U], "cone.producer");
    const auto share_debug_overlay = [&](Process& instance,
                                         const std::string_view source_path,
                                         const std::string_view scope) {
        const Process representative = instance;
        std::optional<InstructionIndex> statement_marker;
        for (std::size_t index = 0U;
             index < instance.operations.size(); ++index) {
            auto operation = instance.operations.expanded(index);
            auto* const point = operation_get_if<DebugPoint>(&operation);
            if (point == nullptr
                || point->kind != DebugPointKind::statement) {
                continue;
            }
            point->source.path = source_path;
            point->scope = scope;
            instance.operations.replace(index, std::move(operation));
            statement_marker = static_cast<InstructionIndex>(index);
            break;
        }
        require(statement_marker.has_value(),
            "each graph member has a source statement marker to remap");
        require(share_process_operations(
                    representative, instance, signals),
            "debug source and scope differences use per-instance overlays");
        require(instance.operations.shares_body_with(
                    representative.operations),
            "per-instance debug markers retain their shared immutable body");
    };
    share_debug_overlay(processes[0U], "cone.consumer.instance.sv",
        "cone.consumer.instance.early");
    share_debug_overlay(processes[2U], "cone.producer.instance.sv",
        "cone.producer.instance.early");
    const auto bindings = bind_processes(processes);
    const auto graph = RegionGraph::build(bindings, descriptors);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components[0U].status
                == RegionComponentCertificateStatus::structural_candidate,
        "source-marked lowered processes retain their graph certificate");

    const auto cone = graph.build_compute_program(0U, bindings);
    require(cone.has_value(),
        "inert source markers do not block region-kernel construction");
    const auto find_member = [&](const ProcessId process) {
        return std::ranges::find(cone->activation_kernel.members, process,
            &RegionConeKernelMember::process);
    };
    const auto consumer = find_member(0U);
    const auto producer = find_member(2U);
    require(consumer != cone->activation_kernel.members.end()
            && producer != cone->activation_kernel.members.end()
            && consumer->final_debug_state
            && producer->final_debug_state
            && consumer->final_debug_state->source.path
                == "cone.consumer.instance.sv"
            && consumer->final_debug_state->source.line == 10U
            && consumer->final_debug_state->source.column == 2U
            && consumer->final_debug_state->scope
                == "cone.consumer.instance.early"
            && producer->final_debug_state->source.path
                == "cone.producer.instance.sv"
            && producer->final_debug_state->source.line == 10U
            && producer->final_debug_state->scope
                == "cone.producer.instance.early",
        "each replacement member retains its last source and lexical scope");
    const auto has_member_debug_marker = [&](
        const RegionConeKernelMember& member,
        const std::string_view source_path,
        const std::string_view scope) {
        for (auto index = member.begin; index < member.end; ++index) {
            const auto operation
                = cone->activation_kernel.program.operations.expanded(index);
            const auto* const point = operation_get_if<DebugPoint>(&operation);
            if (point != nullptr && point->source.path == source_path
                && point->scope == scope) {
                return true;
            }
        }
        return false;
    };
    require(has_member_debug_marker(*consumer,
                "cone.consumer.instance.sv",
                "cone.consumer.instance.early")
            && has_member_debug_marker(*producer,
                "cone.producer.instance.sv",
                "cone.producer.instance.early"),
        "the built component retains each effective per-instance DebugPoint");
    require(std::ranges::count_if(cone->activation_kernel.program.operations,
                [](const Operation& operation) {
                    return operation_holds<DebugPoint>(operation);
                }) == 4,
        "the shared kernel retains all original source marker metadata");
}

void check_compute_cone_sensitivity_ranges()
{
    const auto signals = cone_fixture_signals();
    const auto descriptors = cone_fixture_descriptors();
    auto full_leaf_processes = cone_fixture_processes();
    full_leaf_processes[2U].static_sensitivity[1U].offset = 0U;
    full_leaf_processes[2U].static_sensitivity[1U].width
        = descriptors[4U].width;
    const auto full_leaf_bindings = bind_processes(full_leaf_processes);
    const auto full_leaf_graph = RegionGraph::build(
        full_leaf_bindings, descriptors);
    require(full_leaf_graph.build_compute_program(
                0U, full_leaf_bindings).has_value(),
        "an exact full-width leaf sensitivity can use the compute kernel");

    auto partial_leaf_processes = cone_fixture_processes();
    partial_leaf_processes[2U].static_sensitivity[1U].offset = 0U;
    partial_leaf_processes[2U].static_sensitivity[1U].width
        = descriptors[4U].width - 1U;
    const auto partial_leaf_bindings = bind_processes(partial_leaf_processes);
    const auto partial_leaf_graph = RegionGraph::build(
        partial_leaf_bindings, descriptors);
    const auto partial_leaf_program
        = partial_leaf_graph.build_compute_program(
            0U, partial_leaf_bindings);
    require(partial_leaf_program.has_value()
            && !partial_leaf_program->forwarding_kernel.has_value(),
        "wide Logic9 ranges remain ordinary activations, not narrow forwarding");

    const auto make_range_fixture = [](const std::uint32_t offset,
                                       const std::uint32_t width) {
        std::vector<Process> processes {
            transfer(0U, 1U, 2U),
            transfer(1U, 0U, 1U),
        };
        processes[0U].static_sensitivity = {
            { 1U, EdgeKind::any, offset, width },
        };
        const std::vector<RegionSignalDescriptor> descriptors {
            { 8U },
            { 8U, ResolutionKind::sv_wire },
            { 8U, ResolutionKind::sv_wire },
        };
        return std::pair { std::move(processes), descriptors };
    };
    for (const auto offset : { 0U, 3U, 7U }) {
        auto [range_processes, range_descriptors]
            = make_range_fixture(offset, 1U);
        const auto range_bindings = bind_processes(range_processes);
        const auto range_graph = RegionGraph::build(
            range_bindings, range_descriptors);
        const auto range_program
            = range_graph.build_compute_program(0U, range_bindings);
        require(range_program.has_value()
                && range_program->forwarding_kernel.has_value(),
            "low, middle, and high single-bit sensitivities retain the certified forwarding path");
        const auto& forwarding = *range_program->forwarding_kernel;
        const auto child_member = std::ranges::find(forwarding.members,
            ProcessId { 0U }, &RegionConeForwardingMember::process);
        require(child_member != forwarding.members.end()
                && child_member->dependency_count == 1U
                && child_member->dependency_begin
                    < forwarding.dependencies.size(),
            "the ranged child has one forwarding dependency record");
        const auto& dependency
            = forwarding.dependencies[child_member->dependency_begin];
        require(dependency.signal == 1U && dependency.edge == EdgeKind::any
                && dependency.offset == offset && dependency.width == 1U,
            "forwarding keeps the exact low, middle, or high sensitivity range");
    }

    auto [valid_processes, valid_descriptors]
        = make_range_fixture(0U, 0U);
    const auto valid_bindings = bind_processes(valid_processes);
    const auto valid_graph = RegionGraph::build(
        valid_bindings, valid_descriptors);
    const auto malformed_declines = [&](const Sensitivity sensitivity) {
        auto malformed = valid_processes;
        malformed[0U].static_sensitivity = { sensitivity };
        const auto malformed_bindings = bind_processes(malformed);
        return !valid_graph.build_compute_program(0U, malformed_bindings);
    };
    require(malformed_declines({ 1U, EdgeKind::any, 1U, 0U })
            && malformed_declines({ 1U, EdgeKind::any, 7U, 2U })
            && malformed_declines({ 1U, EdgeKind::any,
                std::numeric_limits<std::uint32_t>::max() - 1U, 4U })
            && malformed_declines({ 1U, EdgeKind::posedge, 0U, 1U }),
        "malformed bounds, overflowing offsets, and edge-sensitive ranges decline safely");
}

std::pair<std::vector<Process>, std::vector<RegionSignalDescriptor>>
make_multiparent_join_fixture(const std::uint32_t width = 8U)
{
    auto middle = transfer(0U, 2U, 3U);
    Process join;
    join.id = 1U;
    join.name = "join_process";
    join.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    join.register_count = 3U;
    join.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4
    };
    // The wider body read of signal 3 is deliberately paired with a narrow
    // sensitivity interval. Dependency provenance describes both facts.
    const auto sensitivity_offset = width > 64U ? 63U : 3U;
    const auto sensitivity_width = width > 64U ? 2U : 1U;
    join.static_sensitivity = {
        { 3U, EdgeKind::any, sensitivity_offset, sensitivity_width },
        { 4U, EdgeKind::any },
    };
    join.driver_regions = { { 5U, 0U, 0U, true } };
    join.operations = {
        ReadSignal { 0U, 3U },
        ReadSignal { 1U, 4U },
        Binary { BinaryOperator::bit_xor, 2U, 0U, 1U },
        WriteUpdate { 5U, 2U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    auto shallow_root = transfer(2U, 1U, 4U);
    auto deep_root = transfer(3U, 0U, 2U);
    std::vector<Process> processes {
        std::move(middle), std::move(join),
        std::move(shallow_root), std::move(deep_root),
    };
    std::vector<RegionSignalDescriptor> descriptors {
        { width }, { width },
        { width, ResolutionKind::sv_wire },
        { width, ResolutionKind::sv_wire },
        { width, ResolutionKind::sv_wire },
        { width },
    };
    descriptors[5U].observations = RegionObservation::current;
    return { std::move(processes), std::move(descriptors) };
}

void check_compute_cone_multi_parent_join(const std::uint32_t width = 8U)
{
    auto [processes, descriptors] = make_multiparent_join_fixture(width);
    const auto bindings = bind_processes(processes);
    const auto graph = RegionGraph::build(bindings, descriptors);
    const auto& components = graph.certificate_inventory().components;
    require(components.size() == 1U
            && components[0U].members
                == std::vector<ProcessId> { 0U, 1U, 2U, 3U }
            && components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 2U, 3U, 4U },
        "two roots and a deeper predecessor form one SV cone");

    const auto program = graph.build_compute_program(0U, bindings);
    require(program.has_value()
            && program->forwarding_kernel.has_value(),
        "a pure two-parent Logic4 join admits private forwarding");
    const auto& forwarding = *program->forwarding_kernel;
    require(forwarding.internal_signals
                == std::vector<SignalId> { 2U, 3U, 4U }
            && forwarding.dependencies.size() == 3U
            && forwarding.internal_reads.size() == 3U,
        "the full multi-parent DAG retains all internal signals and edges");
    const auto member_for = [&](const ProcessId process) {
        return std::ranges::find(forwarding.members, process,
            &RegionConeForwardingMember::process);
    };
    const auto middle = member_for(0U);
    const auto join = member_for(1U);
    const auto shallow_root = member_for(2U);
    const auto deep_root = member_for(3U);
    require(middle != forwarding.members.end()
            && join != forwarding.members.end()
            && shallow_root != forwarding.members.end()
            && deep_root != forwarding.members.end()
            && shallow_root->depth == 0U && deep_root->depth == 0U
            && middle->depth == 1U && join->depth == 2U
            && join->depth
                == std::max(middle->depth, shallow_root->depth) + 1U,
        "join depth is one past its deepest predecessor, independent of process IDs");

    const auto topological_position = [&](const ProcessId process) {
        for (std::size_t position = 0U;
             position < forwarding.topological_member_indices.size();
             ++position) {
            const auto member_index
                = forwarding.topological_member_indices[position];
            if (member_index < forwarding.members.size()
                && forwarding.members[member_index].process == process) {
                return position;
            }
        }
        return forwarding.topological_member_indices.size();
    };
    require(topological_position(3U) < topological_position(0U)
            && topological_position(0U) < topological_position(1U)
            && topological_position(2U) < topological_position(1U),
        "forwarding order follows producer edges rather than ProcessId order");

    require(middle->dependency_count == 1U
            && middle->dependency_begin <= forwarding.dependencies.size()
            && middle->dependency_count
                <= forwarding.dependencies.size() - middle->dependency_begin,
        "middle member retains its own dependency span");
    const auto& middle_dependency
        = forwarding.dependencies[middle->dependency_begin];
    require(middle_dependency.signal == 2U
            && middle_dependency.writer_member_index
                == static_cast<std::size_t>(deep_root
                    - forwarding.members.begin()),
        "middle dependency names the deep root's output");

    std::map<SignalId, ProcessId> dependency_writers;
    require(join->dependency_count == 2U
            && join->dependency_begin <= forwarding.dependencies.size()
            && join->dependency_count
                <= forwarding.dependencies.size() - join->dependency_begin,
        "join keeps both independent sensitivity dependencies");
    for (std::size_t index = join->dependency_begin;
         index < join->dependency_begin + join->dependency_count; ++index) {
        const auto& dependency = forwarding.dependencies[index];
        require(dependency.writer_member_index < forwarding.members.size()
                && dependency.edge == EdgeKind::any,
            "each join dependency identifies an earlier unique writer");
        dependency_writers.emplace(dependency.signal,
            forwarding.members[dependency.writer_member_index].process);
        if (dependency.signal == 3U) {
            const auto expected_offset = width > 64U ? 63U : 3U;
            const auto expected_width = width > 64U ? 2U : 1U;
            require(dependency.offset == expected_offset
                    && dependency.width == expected_width,
                "finite sensitivity remains distinct from the full body read");
        } else if (dependency.signal == 4U) {
            require(dependency.offset == 0U && dependency.width == 0U,
                "whole-signal sensitivity retains its whole-range encoding");
        }
    }
    require(dependency_writers
            == std::map<SignalId, ProcessId> { { 3U, 0U }, { 4U, 2U } },
        "each sensitivity dependency points to its own producer member");

    std::map<SignalId, ProcessId> read_writers;
    require(join->read_count == 2U
            && join->read_begin <= forwarding.internal_reads.size()
            && join->read_count
                <= forwarding.internal_reads.size() - join->read_begin,
        "join records every distinct internal body input");
    for (std::size_t index = join->read_begin;
         index < join->read_begin + join->read_count; ++index) {
        const auto& read = forwarding.internal_reads[index];
        require(read.writer_member_index < forwarding.members.size(),
            "each internal read identifies a unique producer member");
        read_writers.emplace(read.signal,
            forwarding.members[read.writer_member_index].process);
    }
    require(read_writers
            == std::map<SignalId, ProcessId> { { 3U, 0U }, { 4U, 2U } },
        "full-signal reads retain their individual producer provenance");
    require(middle->read_count == 1U
            && middle->read_begin < forwarding.internal_reads.size()
            && forwarding.internal_reads[middle->read_begin].signal == 2U
            && forwarding.internal_reads[middle->read_begin]
                    .writer_member_index
                == static_cast<std::size_t>(deep_root
                    - forwarding.members.begin()),
        "the deeper branch retains its own internal read provenance");

    const auto& execution = forwarding.execution_kernel;
    require(execution.inputs.size() == 2U
            && std::ranges::all_of(execution.inputs,
                [width](const RegionConeKernelInput& input) {
                    return input.width == width
                        && input.value_kind == ValueKind::logic4
                        && !input.internal;
                })
            && execution.outputs.size() == 4U
            && std::ranges::all_of(execution.outputs,
                [width](const RegionConeOutputBinding& output) {
                    return output.width == width
                        && output.value_kind == ValueKind::logic4;
                }),
        "forwarding carries full-width typed boundary and output planes");
    std::map<SignalId, ProcessId> output_owners;
    for (const auto& output : execution.outputs) {
        output_owners.emplace(output.signal, output.owner);
    }
    require(output_owners
            == std::map<SignalId, ProcessId> {
                { 2U, 3U }, { 3U, 0U }, { 4U, 2U }, { 5U, 1U } },
        "wide output snapshots retain their source owners");
    const auto execution_member_for = [&](const ProcessId process) {
        return std::ranges::find(execution.members, process,
            &RegionConeKernelMember::process);
    };
    for (const auto& member : execution.members) {
        for (std::size_t instruction = member.begin;
             instruction < member.end; ++instruction) {
            const auto operation
                = execution.program.operations.expanded(instruction);
            require(operation_get_if<ReadSignal>(&operation) == nullptr,
                "forwarding member bodies replace source reads before execution");
        }
    }
    const auto internal_read_uses_writer_register = [&](
        const ProcessId consumer_process,
        const SignalId signal,
        const ProcessId writer_process) {
        const auto consumer = execution_member_for(consumer_process);
        const auto writer_output = std::ranges::find_if(execution.outputs,
            [=](const RegionConeOutputBinding& output) {
                return output.owner == writer_process
                    && output.signal == signal;
            });
        if (consumer == execution.members.end()
            || writer_output == execution.outputs.end()) {
            return false;
        }
        std::optional<RegisterId> source_register;
        for (std::size_t index = 0U;
             index + 2U < processes[consumer_process].operations.size();
             ++index) {
            const auto operation
                = processes[consumer_process].operations.expanded(index);
            if (const auto* const read
                = operation_get_if<ReadSignal>(&operation);
                read != nullptr && read->signal == signal) {
                if (source_register.has_value()) {
                    return false;
                }
                source_register = read->destination;
            }
        }
        if (!source_register.has_value()) {
            return false;
        }
        const auto register_binding = std::ranges::find(
            consumer->register_bindings, *source_register,
            &RegionConeKernelRegisterBinding::source_register);
        if (register_binding == consumer->register_bindings.end()) {
            return false;
        }
        std::size_t matching_copies { };
        for (std::size_t instruction = consumer->begin;
             instruction < consumer->end; ++instruction) {
            const auto operation
                = execution.program.operations.expanded(instruction);
            if (const auto* const copy
                = operation_get_if<CopyRegister>(&operation);
                copy != nullptr
                && copy->destination == register_binding->activation_register
                && copy->source == writer_output->value_register) {
                ++matching_copies;
            }
        }
        return matching_copies == 1U;
    };
    require(internal_read_uses_writer_register(0U, 2U, 3U)
            && internal_read_uses_writer_register(1U, 3U, 0U)
            && internal_read_uses_writer_register(1U, 4U, 2U),
        "internal wide traffic uses each certified producer's output register");

    auto mixed_boundary_processes = processes;
    mixed_boundary_processes[1U].static_sensitivity.push_back(
        { 6U, EdgeKind::any, 0U, 1U });
    auto mixed_boundary_descriptors = descriptors;
    mixed_boundary_descriptors.push_back({ 1U, ResolutionKind::sv_wire });
    const auto mixed_boundary_bindings
        = bind_processes(mixed_boundary_processes);
    const auto mixed_boundary_graph = RegionGraph::build(
        mixed_boundary_bindings, mixed_boundary_descriptors);
    const auto& mixed_components
        = mixed_boundary_graph.certificate_inventory().components;
    const auto mixed_component = std::ranges::find_if(mixed_components,
        [](const RegionComponentCertificate& candidate) {
            return std::ranges::find(candidate.members, ProcessId { 1U })
                != candidate.members.end();
        });
    require(mixed_component != mixed_components.end(),
        "the mixed internal and external child remains in its component");
    const auto mixed_program
        = mixed_boundary_graph.build_compute_program(
            static_cast<std::size_t>(mixed_component - mixed_components.begin()),
            mixed_boundary_bindings);
    require(mixed_program && mixed_program->forwarding_kernel,
        "a child with internal and external sensitivities retains forwarding");
    const auto& mixed_forwarding = *mixed_program->forwarding_kernel;
    const auto mixed_child = std::ranges::find(mixed_forwarding.members,
        ProcessId { 1U }, &RegionConeForwardingMember::process);
    require(mixed_child != mixed_forwarding.members.end()
            && mixed_child->dependency_count == 2U
            && mixed_program->forwarding_kernel->execution_kernel.inputs.size()
                == 3U,
        "the mixed child retains two internal dependencies and three boundary inputs");
    const auto external_sensitivity
        = std::ranges::find(mixed_program->forwarding_kernel
                                ->execution_kernel.inputs,
            SignalId { 6U }, &RegionConeKernelInput::signal);
    require(external_sensitivity
                != mixed_program->forwarding_kernel
                    ->execution_kernel.inputs.end()
            && !external_sensitivity->internal
            && external_sensitivity->width == 1U
            && external_sensitivity->value_kind == ValueKind::logic4,
        "a sensitivity-only external signal joins the captured boundary inputs");
    const auto child_kernel_member = std::ranges::find(
        mixed_program->forwarding_kernel->execution_kernel.members,
        ProcessId { 1U }, &RegionConeKernelMember::process);
    require(child_kernel_member
                != mixed_program->forwarding_kernel
                    ->execution_kernel.members.end()
            && std::ranges::find(child_kernel_member->sensitivities,
                Sensitivity { 6U, EdgeKind::any, 0U, 1U })
                != child_kernel_member->sensitivities.end(),
        "the forwarding execution member preserves external trigger provenance");

    const auto forwarding_for = [](
        const std::vector<Process>& candidate_processes,
        const std::vector<RegionSignalDescriptor>& candidate_descriptors,
        const ProcessId target) {
        const auto candidate_bindings = bind_processes(candidate_processes);
        const auto candidate_graph = RegionGraph::build(
            candidate_bindings, candidate_descriptors);
        const auto& candidate_components
            = candidate_graph.certificate_inventory().components;
        for (std::size_t index = 0U;
             index < candidate_components.size(); ++index) {
            const auto& members = candidate_components[index].members;
            if (std::ranges::find(members, target) == members.end()) {
                continue;
            }
            const auto candidate_program
                = candidate_graph.build_compute_program(
                    index, candidate_bindings);
            return candidate_program.has_value()
                && candidate_program->forwarding_kernel.has_value();
        }
        return false;
    };
    const auto forwards_internal_signal = [](
        const std::vector<Process>& candidate_processes,
        const std::vector<RegionSignalDescriptor>& candidate_descriptors,
        const ProcessId target,
        const SignalId signal) {
        const auto candidate_bindings = bind_processes(candidate_processes);
        const auto candidate_graph = RegionGraph::build(
            candidate_bindings, candidate_descriptors);
        const auto& candidate_components
            = candidate_graph.certificate_inventory().components;
        for (std::size_t index = 0U;
             index < candidate_components.size(); ++index) {
            const auto& members = candidate_components[index].members;
            if (std::ranges::find(members, target) == members.end()) {
                continue;
            }
            const auto candidate_program
                = candidate_graph.build_compute_program(
                    index, candidate_bindings);
            if (!candidate_program
                || !candidate_program->forwarding_kernel) {
                return false;
            }
            const auto& internal_signals
                = candidate_program->forwarding_kernel->internal_signals;
            return std::ranges::find(internal_signals, signal)
                != internal_signals.end();
        }
        return false;
    };

    auto unsupported_edge = processes;
    unsupported_edge[1U].static_sensitivity[0U].edge = EdgeKind::posedge;
    require(!forwarding_for(unsupported_edge, descriptors, 1U),
        "edge-sensitive joins stay on the checked path");
    auto unsupported_external_edge = mixed_boundary_processes;
    unsupported_external_edge[1U].static_sensitivity.back().edge
        = EdgeKind::posedge;
    require(!forwarding_for(unsupported_external_edge,
                mixed_boundary_descriptors, 1U),
        "edge-sensitive external-only wakes stay on the checked path");

    auto cyclic = processes;
    cyclic[0U].register_count = 2U;
    cyclic[0U].register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4
    };
    cyclic[0U].static_sensitivity = {
        { 2U, EdgeKind::any }, { 5U, EdgeKind::any },
    };
    cyclic[0U].operations = {
        ReadSignal { 0U, 2U },
        ReadSignal { 1U, 5U },
        Binary { BinaryOperator::bit_xor, 0U, 0U, 1U },
        WriteUpdate { 3U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    auto cyclic_descriptors = descriptors;
    cyclic_descriptors[5U].observations = RegionObservation::none;
    const auto cyclic_bindings = bind_processes(cyclic);
    const auto cyclic_graph = RegionGraph::build(
        cyclic_bindings, cyclic_descriptors);
    require(cyclic_graph.processes()[0U].cyclic_or_dependent_on_cycle
            && cyclic_graph.processes()[1U].cyclic_or_dependent_on_cycle,
        "the negative fixture is classified as the middle-join feedback cycle");
    require(!forwarding_for(cyclic, cyclic_descriptors, 1U),
        "cyclic producer joins are rejected conservatively");

    auto ambiguous_writer = processes;
    ambiguous_writer.push_back(transfer(4U, 1U, 3U));
    require(ambiguous_writer.back().driver_regions.front().signal == 3U
            && ambiguous_writer[1U].static_sensitivity[0U].signal == 3U,
        "the second writer duplicates a direct join dependency");
    require(forwarding_for(ambiguous_writer, descriptors, 1U)
            && !forwards_internal_signal(
                ambiguous_writer, descriptors, 1U, 3U)
            && forwards_internal_signal(
                ambiguous_writer, descriptors, 1U, 4U),
        "an ambiguous writer remains a boundary while the unique sibling can forward");
    const auto ambiguous_bindings = bind_processes(ambiguous_writer);
    const auto ambiguous_graph = RegionGraph::build(
        ambiguous_bindings, descriptors);
    const auto& ambiguous_components
        = ambiguous_graph.certificate_inventory().components;
    const auto ambiguous_component = std::ranges::find_if(
        ambiguous_components, [](const auto& component) {
            return std::ranges::find(component.members, ProcessId { 1U })
                != component.members.end();
        });
    require(ambiguous_component != ambiguous_components.end(),
        "the join remains in a graph component with the ambiguous boundary");
    const auto ambiguous_program = ambiguous_graph.build_compute_program(
        static_cast<std::size_t>(
            ambiguous_component - ambiguous_components.begin()),
        ambiguous_bindings);
    require(ambiguous_program && ambiguous_program->forwarding_kernel,
        "the uniquely owned branch remains eligible for mixed-boundary forwarding");
    const auto& ambiguous_forwarding
        = *ambiguous_program->forwarding_kernel;
    const auto boundary_input = std::ranges::find_if(
        ambiguous_forwarding.execution_kernel.inputs,
        [](const RegionConeKernelInput& input) {
            return input.signal == 3U && !input.internal;
        });
    require(boundary_input
                != ambiguous_forwarding.execution_kernel.inputs.end()
            && std::ranges::none_of(
                ambiguous_forwarding.internal_reads,
                [](const RegionConeForwardingRead& read) {
                    return read.signal == 3U;
                })
            && std::ranges::none_of(
                ambiguous_forwarding.dependencies,
                [](const RegionConeForwardingDependency& dependency) {
                    return dependency.signal == 3U;
                }),
        "the multiply written signal is a full boundary input, never a forwarded internal read");

    auto unsensitized_read = processes;
    unsensitized_read.push_back(transfer(4U, 1U, 6U));
    unsensitized_read[1U].register_count = 4U;
    unsensitized_read[1U].register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4,
    };
    unsensitized_read[1U].operations = {
        ReadSignal { 0U, 4U },
        ReadSignal { 1U, 3U },
        ReadSignal { 2U, 6U },
        Binary { BinaryOperator::bit_xor, 3U, 0U, 1U },
        Binary { BinaryOperator::bit_xor, 3U, 3U, 2U },
        WriteUpdate { 5U, 3U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    auto unsensitized_descriptors = descriptors;
    unsensitized_descriptors.push_back({
        width, ResolutionKind::sv_wire
    });
    require(!forwarding_for(unsensitized_read,
                unsensitized_descriptors, 1U),
        "an internal body read without matching sensitivity provenance is rejected");

    auto invalid_kind_descriptors = descriptors;
    invalid_kind_descriptors[3U].value_kind
        = static_cast<ValueKind>(255U);
    bool invalid_kind_rejected { };
    try {
        invalid_kind_rejected = !forwards_internal_signal(
            processes, invalid_kind_descriptors, 1U, 3U);
    } catch (const std::invalid_argument&) {
        invalid_kind_rejected = true;
    }
    require(invalid_kind_rejected,
        "forwarding rejects an invalid internal value kind");

    auto out_of_bounds = processes;
    out_of_bounds[1U].static_sensitivity[0U].offset = width - 1U;
    out_of_bounds[1U].static_sensitivity[0U].width = 2U;
    bool range_rejected { };
    try {
        static_cast<void>(forwarding_for(out_of_bounds, descriptors, 1U));
    } catch (const std::invalid_argument&) {
        range_rejected = true;
    }
    require(range_rejected,
        "a join rejects a sensitivity range beyond the signal width");

    auto partial_processes = processes;
    auto partial_descriptors = descriptors;
    partial_descriptors[0U].width = width - 1U;
    partial_descriptors[2U].width = width - 1U;
    partial_processes[0U].driver_regions[0U]
        = { 3U, 0U, width - 1U, false };
    partial_processes[0U].operations[1U] = WriteUpdateSlice {
        3U, 0U, 0U, SignalUpdateDomain::systemverilog_active
    };
    const auto partial_bindings = bind_processes(partial_processes);
    const auto partial_graph = RegionGraph::build(
        partial_bindings, partial_descriptors);
    const auto& partial_components
        = partial_graph.certificate_inventory().components;
    const auto partial_component = std::ranges::find_if(
        partial_components, [](const auto& component) {
            return std::ranges::find(component.members, ProcessId { 0U })
                != component.members.end();
        });
    std::optional<RegionConeProgram> partial_program;
    if (partial_component != partial_components.end()) {
        partial_program = partial_graph.build_compute_program(
            static_cast<std::size_t>(
                partial_component - partial_components.begin()),
            partial_bindings);
    }
    const bool partial_boundary_slice_captured = partial_program
        && std::ranges::any_of(partial_program->boundary_outputs,
            [&](const RegionConeOutputBinding& output) {
                return output.owner == ProcessId { 0U }
                    && output.signal == SignalId { 3U }
                    && output.offset == 0U
                    && output.width == width - 1U
                    && output.signal_width == width
                    && output.source_instruction
                        == InstructionIndex { 1U }
                    && output.domain
                        == SignalUpdateDomain::systemverilog_active;
            });
    const bool partial_signal_is_not_internal = partial_program
        && partial_program->forwarding_kernel
        && std::ranges::find(
            partial_program->forwarding_kernel->internal_signals,
            SignalId { 3U })
            == partial_program->forwarding_kernel->internal_signals.end();
    require(partial_graph.signals()[3U].drivers
                == RegionDriverClass::single_partial
            && partial_program && partial_program->forwarding_kernel
            && partial_boundary_slice_captured
            && partial_signal_is_not_internal
            && !forwards_internal_signal(
                partial_processes, partial_descriptors, 1U, 3U),
        "a partial output remains an exact boundary slice, "
        "not a private whole signal");

    if (width == 65U) {
        auto [logic9_processes, logic9_descriptors]
            = make_multiparent_join_fixture(width);
        for (auto& process : logic9_processes) {
            process.register_value_kinds.assign(
                process.register_count, ValueKind::logic9);
        }
        for (auto& descriptor : logic9_descriptors) {
            descriptor.value_kind = ValueKind::logic9;
        }
        bool logic9_refused { };
        try {
            logic9_refused = !forwards_internal_signal(
                logic9_processes, logic9_descriptors, 1U, 2U)
                && !forwards_internal_signal(
                    logic9_processes, logic9_descriptors, 1U, 3U)
                && !forwards_internal_signal(
                    logic9_processes, logic9_descriptors, 1U, 4U);
        } catch (const std::invalid_argument&) {
            logic9_refused = true;
        }
        require(logic9_refused,
            "wide Logic9 joins remain outside the Logic4 forwarding certificate");
    }
}

void check_compute_cone_wide_multi_parent_join()
{
    constexpr std::array<std::uint32_t, 4U> widths {
        65U, 129U, 256U, 1024U
    };
    for (const auto width : widths) {
        check_compute_cone_multi_parent_join(width);
    }
}

void check_coverage_operations_remain_unknown()
{
    const auto descriptors = cone_fixture_descriptors();
    const auto original = cone_fixture_processes();
    const std::array<Operation, 6U> coverage_operations {
        CoverageSample { },
        CoverageQuery { },
        CoverageControl { },
        CoverageAccess { },
        CoverageDatabaseControl { },
        CodeCoverageHit { },
    };
    const std::array<const char*, 6U> operation_names {
        "coverage sample", "coverage query", "coverage control",
        "coverage access", "coverage database control", "code coverage hit"
    };

    for (std::size_t index = 0U; index < coverage_operations.size(); ++index) {
        auto processes = original;
        processes[2U].operations.insert(
            processes[2U].operations.cbegin(), coverage_operations[index]);
        const auto bindings = bind_processes(processes);
        const auto graph = RegionGraph::build(bindings, descriptors);
        require(!graph.certificate_inventory().access_inventory_complete
                && !graph.build_compute_program(0U, bindings),
            operation_names[index]);
    }
}

void check_compute_cone_rejections()
{
    const auto descriptors = cone_fixture_descriptors();
    const auto original = cone_fixture_processes();
    const auto original_bindings = bind_processes(original);
    const auto graph = RegionGraph::build(original_bindings, descriptors);

    auto mismatched = original;
    mismatched[2U].operations[0U] = ReadSignal { 0U, 2U };
    const auto mismatched_bindings = bind_processes(mismatched);
    require(!graph.build_compute_program(0U, mismatched_bindings),
        "a same-ID process with changed read dependencies cannot use an old graph");

    auto observed_graph = RegionGraph::build(original_bindings, descriptors);
    static_cast<void>(observed_graph.observe_signal(1U, RegionObservation::current));
    require(!observed_graph.build_compute_program(0U, original_bindings),
        "late observation invalidation makes cone construction stale");

    auto generic = original;
    generic[2U].scheduling_domain = ProcessSchedulingDomain::generic;
    const auto generic_bindings = bind_processes(generic);
    const auto generic_graph = RegionGraph::build(generic_bindings, descriptors);
    require(std::ranges::none_of(
                generic_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                        == RegionComponentCertificateStatus::structural_candidate;
                })
            && !generic_graph.build_compute_program(0U, generic_bindings),
        "generic scheduling domains do not produce SV cone certificates");

    auto vhdl = original;
    for (auto& process : vhdl) {
        process.scheduling_domain = ProcessSchedulingDomain::generic;
        process.language_standard = "2008";
    }
    const auto vhdl_bindings = bind_processes(vhdl);
    const auto vhdl_graph = RegionGraph::build(vhdl_bindings, descriptors);
    require(std::ranges::none_of(
                vhdl_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                        == RegionComponentCertificateStatus::structural_candidate;
                })
            && !vhdl_graph.build_compute_program(0U, vhdl_bindings),
        "VHDL processes using SystemVerilog Active updates remain unsupported");

    auto mixed = original;
    mixed[0U].scheduling_domain = ProcessSchedulingDomain::generic;
    const auto mixed_bindings = bind_processes(mixed);
    const auto mixed_graph = RegionGraph::build(mixed_bindings, descriptors);
    require(std::ranges::none_of(
                mixed_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                        == RegionComponentCertificateStatus::structural_candidate;
                })
            && !mixed_graph.build_compute_program(0U, mixed_bindings),
        "mixed scheduling domains cannot enter a compute cone");

    auto uninitialized = original;
    uninitialized[1U].initialize = false;
    const auto uninitialized_bindings = bind_processes(uninitialized);
    const auto uninitialized_graph
        = RegionGraph::build(uninitialized_bindings, descriptors);
    const auto uninitialized_cone
        = uninitialized_graph.build_compute_program(0U, uninitialized_bindings);
    require(uninitialized_cone.has_value()
            && std::ranges::any_of(uninitialized_cone->member_spans,
                [](const RegionConeMemberSpan& span) {
                    return span.process == 1U && !span.initialize;
                }),
        "compute construction retains member initialization state for later activation proofs");

    // A region activation starts its private process-register banks as
    // unknown. A local that is only initialized by an earlier loop iteration
    // must therefore stay on the ordinary executor route; the kernel builder
    // cannot treat a retained executor value as an implicit live-in.
    auto retained_register = original;
    retained_register[0U].register_count = 1U;
    retained_register[0U].operations = {
        UnaryNot { 0U, 0U },
        WriteUpdate { 2U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    const auto retained_register_bindings
        = bind_processes(retained_register);
    const auto retained_register_graph
        = RegionGraph::build(retained_register_bindings, descriptors);
    require(std::ranges::none_of(
                retained_register_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                            == RegionComponentCertificateStatus::structural_candidate
                        && std::ranges::find(component.members, 0U)
                            != component.members.end();
                }),
        "a register read that depends on a prior activation cannot enter a region kernel");

    // read_debug_local is the public process-register observation API. Kernel
    // members with any readable debug local are excluded so a later debugger
    // read can never observe a stale skipped-executor register.
    auto debug_observed = original;
    DebugLocal debug_local;
    debug_local.name = "temporary";
    debug_local.type_name = "logic";
    debug_local.register_id = 0U;
    debug_local.width = 2U;
    debug_observed[0U].debug_locals.push_back(std::move(debug_local));
    const auto debug_observed_bindings = bind_processes(debug_observed);
    const auto debug_observed_graph
        = RegionGraph::build(debug_observed_bindings, descriptors);
    require(std::ranges::none_of(
                debug_observed_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                            == RegionComponentCertificateStatus::structural_candidate
                        && std::ranges::find(component.members, 0U)
                            != component.members.end();
                }),
        "a process with public debug-register reads cannot be replaced by a region kernel");

    auto delayed = original;
    delayed[2U].operations[2U] = WriteAfter {
        1U, 1U, 1U, SignalUpdateDomain::systemverilog_active
    };
    const auto delayed_bindings = bind_processes(delayed);
    const auto delayed_graph = RegionGraph::build(delayed_bindings, descriptors);
    require(std::ranges::none_of(
                delayed_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                        == RegionComponentCertificateStatus::structural_candidate;
                }),
        "delayed writes remain outside straight-line compute certificates");

    auto observed_descriptors = descriptors;
    observed_descriptors[1U].observations = RegionObservation::current;
    const auto preobserved_graph
        = RegionGraph::build(original_bindings, observed_descriptors);
    require(std::ranges::none_of(
                preobserved_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                        == RegionComponentCertificateStatus::structural_candidate;
                })
            && !preobserved_graph.build_compute_program(0U, original_bindings),
        "preobserved internal signals cannot enter a structural compute cone");

    auto ranged_sensitivity = original;
    ranged_sensitivity[2U].static_sensitivity[0U].width = 1U;
    const auto ranged_sensitivity_bindings
        = bind_processes(ranged_sensitivity);
    const auto ranged_sensitivity_graph
        = RegionGraph::build(ranged_sensitivity_bindings, descriptors);
    const auto ranged_sensitivity_program
        = ranged_sensitivity_graph.build_compute_program(
            0U, ranged_sensitivity_bindings);
    require(ranged_sensitivity_program.has_value(),
        "a valid finite sensitivity range can use the activation kernel");
    const auto ranged_producer = std::ranges::find(
        ranged_sensitivity_program->activation_kernel.members, ProcessId { 2U },
        &RegionConeKernelMember::process);
    const std::array<Sensitivity, 2U> expected_ranged_sensitivities {{
        { 0U, EdgeKind::any, 0U, 1U },
        { 4U, EdgeKind::any, 0U, 0U },
    }};
    require(ranged_producer
                != ranged_sensitivity_program->activation_kernel.members.end()
            && std::ranges::equal(ranged_producer->sensitivities,
                expected_ranged_sensitivities),
        "the admitted activation member retains normalized finite and whole-signal sensitivity metadata");

    auto out_of_bounds_sensitivity = ranged_sensitivity;
    out_of_bounds_sensitivity[2U].static_sensitivity[0U].offset
        = descriptors[0U].width;
    const auto out_of_bounds_bindings
        = bind_processes(out_of_bounds_sensitivity);
    bool out_of_bounds_rejected { };
    try {
        static_cast<void>(RegionGraph::build(
            out_of_bounds_bindings, descriptors));
    } catch (const std::invalid_argument&) {
        out_of_bounds_rejected = true;
    }
    require(out_of_bounds_rejected,
        "a finite sensitivity beginning at the signal width is rejected");

    auto opaque = original;
    opaque[2U].operations.insert(opaque[2U].operations.cbegin(),
        Display { "opaque output", true, false });
    const auto opaque_bindings = bind_processes(opaque);
    const auto opaque_graph = RegionGraph::build(opaque_bindings, descriptors);
    require(!opaque_graph.certificate_inventory().components.empty()
            && !opaque_graph.build_compute_program(0U, opaque_bindings),
        "unsupported operation effects fail closed during program rewriting");

    auto wrong_writer = original;
    wrong_writer[2U].operations[2U] = WriteUpdate {
        2U, 1U, SignalUpdateDomain::systemverilog_active
    };
    const auto wrong_writer_bindings = bind_processes(wrong_writer);
    require(!graph.build_compute_program(0U, wrong_writer_bindings),
        "same-ID source programs must still match certified write owners");

    auto wrong_owner = original;
    wrong_owner[2U].driver_regions[0U].signal = 3U;
    const auto wrong_owner_bindings = bind_processes(wrong_owner);
    require(!graph.build_compute_program(0U, wrong_owner_bindings),
        "source owner regions must match the graph snapshot before rewriting");

    auto invalid_internal = descriptors;
    invalid_internal[1U].value_kind = static_cast<ValueKind>(255U);
    const auto invalid_kind_graph
        = RegionGraph::build(original_bindings, invalid_internal);
    require(!invalid_kind_graph.build_compute_program(0U, original_bindings),
        "unknown internal value kinds remain unsupported");

    auto self_dependent = original;
    self_dependent[2U].operations.insert(self_dependent[2U].operations.cbegin(),
        ReadSignal { 0U, 1U });
    const auto self_bindings = bind_processes(self_dependent);
    const auto self_graph = RegionGraph::build(self_bindings, descriptors);
    require(std::ranges::none_of(
                self_graph.certificate_inventory().components,
                [](const RegionComponentCertificate& component) {
                    return component.status
                        == RegionComponentCertificateStatus::structural_candidate;
                })
            && !self_graph.build_compute_program(0U, self_bindings),
        "a member that reads its own written net is a cycle, even if ordered in its body");

    auto cyclic = std::vector<Process> {
        transfer(0U, 1U, 0U), transfer(1U, 0U, 1U)
    };
    for (auto& process : cyclic) {
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.operations[1U] = WriteUpdate {
            process.driver_regions.front().signal, 0U,
            SignalUpdateDomain::systemverilog_active
        };
    }
    const std::vector<RegionSignalDescriptor> cyclic_descriptors {
        { 2U }, { 2U }
    };
    const auto cyclic_bindings = bind_processes(cyclic);
    const auto cyclic_graph
        = RegionGraph::build(cyclic_bindings, cyclic_descriptors);
    require(cyclic_graph.certificate_inventory().components.empty()
            && !cyclic_graph.build_compute_program(0U, cyclic_bindings),
        "feedback components have no compute program");

    auto ranged = original;
    ranged[2U].driver_regions[0U] = { 1U, 0U, 1U, false };
    ranged[2U].operations[1U] = Extract { 1U, 0U, 0U, 1U };
    ranged[2U].operations[2U] = WriteUpdateSlice {
        1U, 1U, 0U, SignalUpdateDomain::systemverilog_active
    };
    const auto ranged_bindings = bind_processes(ranged);
    const auto ranged_graph = RegionGraph::build(ranged_bindings, descriptors);
    require(ranged_graph.signals()[1U].drivers
            == RegionDriverClass::single_partial,
        "a one-bit range write remains a partial driver");
    for (std::size_t index = 0U;
         index < ranged_graph.certificate_inventory().components.size();
         ++index) {
        const auto candidate = ranged_graph.build_compute_program(
            index, ranged_bindings);
        require(!candidate
                || std::ranges::find(candidate->members, 2U)
                    == candidate->members.end(),
            "partial range writers are never rewritten as whole-signal stores");
    }
}

void check_vhdl_projected_activation_program()
{
    std::vector<Process> processes(2U);
    processes[0U].id = 0U;
    processes[0U].name = "vhdl_projected_source";
    processes[0U].language_standard = "2008";
    processes[0U].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[0U].register_count = 1U;
    processes[0U].static_sensitivity = { { 0U, EdgeKind::any } };
    processes[0U].driver_regions = { { 1U, 0U, 0U, true } };
    processes[0U].operations = {
        ReadSignal { 0U, 0U },
        WriteProjected { 1U, 0U, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };

    processes[1U].id = 1U;
    processes[1U].name = "vhdl_projected_sink";
    processes[1U].language_standard = "2008";
    processes[1U].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[1U].register_count = 1U;
    processes[1U].static_sensitivity = { { 1U, EdgeKind::any } };
    processes[1U].driver_regions = { { 2U, 0U, 0U, true } };
    processes[1U].operations = {
        ReadSignal { 0U, 1U },
        WriteProjected { 2U, 0U, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };

    const auto bindings = bind_processes(processes);
    std::vector<RegionSignalDescriptor> descriptors {
        { 1U }, { 1U }, { 1U }
    };
    descriptors[2U].observations = RegionObservation::current;
    const auto graph = RegionGraph::build(bindings, descriptors);
    require(graph.certificate_inventory().components.size() == 1U
            && graph.certificate_inventory().components.front().members
                == std::vector<ProcessId> { 0U, 1U },
        "generic VHDL projected producers and consumers share only their own domain component");
    const auto program = graph.build_compute_program(0U, bindings);
    require(program.has_value()
            && program->activation_kernel.program.scheduling_domain
                == ProcessSchedulingDomain::generic,
        "zero-delay VHDL projected loop lowers to a generic activation program");
    const auto& kernel = program->activation_kernel;
    const auto projected_output = std::ranges::find(kernel.outputs, 2U,
        &RegionConeOutputBinding::signal);
    require(projected_output != kernel.outputs.end()
            && projected_output->owner == 1U
            && projected_output->source_instruction == 1U
            && projected_output->update_kind
                == RegionUpdateKind::vhdl_projected
            && projected_output->domain == SignalUpdateDomain::generic
            && projected_output->projected_mode
                == ProjectedDelayMode::inertial
            && projected_output->projected_delay == 0U
            && projected_output->projected_rejection == 0U,
        "the activation output retains its original owner and exact projected contract");
    const auto kernel_write = kernel.program.operations.expanded(
        projected_output->kernel_instruction);
    const auto* snapshot = operation_get_if<CopyRegister>(&kernel_write);
    require(snapshot != nullptr
            && snapshot->destination == projected_output->value_register,
        "projected outputs are captured without replacing their publication metadata");

    const auto projected_internal_output = std::ranges::find(kernel.outputs,
        1U, &RegionConeOutputBinding::signal);
    require(projected_internal_output != kernel.outputs.end()
            && projected_internal_output->update_kind
                == RegionUpdateKind::vhdl_projected
            && projected_internal_output->domain
                == SignalUpdateDomain::generic
            && projected_internal_output->projected_mode
                == ProjectedDelayMode::inertial
            && projected_internal_output->projected_delay == 0U
            && projected_internal_output->projected_rejection == 0U,
        "internal and boundary captures both retain projected publication metadata");

    auto invalid_kernel = kernel;
    invalid_kernel.outputs.front().projected_delay = 1U;
    bool rejected_invalid_contract { };
    try {
        RegionKernelActivationState invalid_activation { invalid_kernel };
        static_cast<void>(invalid_activation);
    } catch (const std::invalid_argument&) {
        rejected_invalid_contract = true;
    }
    require(rejected_invalid_contract,
        "activation rejects projected outputs whose exact contract is unsupported");

    const std::array internal_seed {
        RegionKernelInternalSeed { 1U, 0U,
            PackedLogic4::from_msb_string("0"),
            PackedLogic4::from_msb_string("0"),
            PackedLogic4::from_msb_string("0") },
    };
    RegionKernelActivationState activation { kernel, internal_seed };
    const auto make_origin = [](const ProcessId process,
                                 const StableOrder order,
                                 const std::uint64_t sequence,
                                 const SimulationTick time,
                                 const std::uint64_t delta) {
        return RegionKernelReadyMember { process,
            Process::full_static_trigger_mask,
            RegionKernelActivationOrigin {
                ProcessSchedulingDomain::generic,
                SchedulerPhase::active, time, delta, order, sequence, 0U } };
    };
    const auto input_binding = std::ranges::find(kernel.inputs, 1U,
        &RegionConeKernelInput::signal);
    require(input_binding != kernel.inputs.end() && input_binding->internal,
        "VHDL consumer input is backed by committed internal state");
    const auto boundary_input = std::ranges::find(kernel.inputs, 0U,
        &RegionConeKernelInput::signal);
    require(boundary_input != kernel.inputs.end() && !boundary_input->internal,
        "VHDL source reads a boundary signal through the generic activation");
    const std::array boundary_inputs { PackedLogic4::from_msb_string("1") };
    RegionKernelSchedulerPrefix simultaneous_prefix;
    simultaneous_prefix.frontier_generation = 1U;
    simultaneous_prefix.frontier_cursor = 0U;
    simultaneous_prefix.frontier_end = 2U;
    simultaneous_prefix.time = 1U;
    simultaneous_prefix.phase = SchedulerPhase::active;
    simultaneous_prefix.process_domain = ProcessSchedulingDomain::generic;
    simultaneous_prefix.tasks = {
        { 0U, make_origin(0U, 1U, 1U, 1U, 0U) },
        { 1U, make_origin(1U, 2U, 2U, 1U, 0U) },
    };
    auto wrong_domain_prefix = simultaneous_prefix;
    wrong_domain_prefix.process_domain
        = ProcessSchedulingDomain::systemverilog;
    bool rejected_wrong_domain { };
    try {
        static_cast<void>(activation.begin_wave_reusable(
            wrong_domain_prefix, boundary_inputs));
    } catch (const std::invalid_argument&) {
        rejected_wrong_domain = true;
    }
    require(rejected_wrong_domain,
        "generic activation requires an explicitly generic scheduler frontier");

    const auto& simultaneous_image = activation.begin_wave_reusable(
        simultaneous_prefix, boundary_inputs);
    const auto captured_old_input = std::ranges::find(
        simultaneous_image.register_inputs, input_binding->value_register,
        &RegionKernelRegisterInput::register_id);
    require(captured_old_input != simultaneous_image.register_inputs.end()
            && captured_old_input->value == PackedLogic4::from_msb_string("0"),
        "generic activation captures internal inputs at the committed pre-cycle cut");
    const std::vector<Signal> runtime_signals {
        Signal { "vhdl.input", PackedLogic4::from_msb_string("1") },
        Signal { "vhdl.internal", PackedLogic4::from_msb_string("0") },
        Signal { "vhdl.output", PackedLogic4::from_msb_string("0") },
    };
    const auto first_registers = execute_region_activation_kernel(
        kernel, simultaneous_image, runtime_signals);
    activation.stage_kernel_outputs(simultaneous_image, first_registers);
    const auto first_publications = activation.pending_publications();
    const auto source_publication = std::ranges::find_if(first_publications,
        [](const RegionKernelPendingPublication& publication) {
            return publication.binding.signal == 1U;
        });
    const auto sink_publication = std::ranges::find_if(first_publications,
        [](const RegionKernelPendingPublication& publication) {
            return publication.binding.signal == 2U;
        });
    require(source_publication != first_publications.end()
            && source_publication->value == PackedLogic4::from_msb_string("1")
            && sink_publication != first_publications.end()
            && sink_publication->value == PackedLogic4::from_msb_string("0"),
        "same-cycle VHDL consumers do not forward an unpublished producer value");
    for (std::size_t index = 0U; index < first_publications.size(); ++index) {
        activation.begin_raw_publication(index);
        activation.publish_current(index, first_publications[index].value);
    }
    activation.complete_wave();

    RegionKernelSchedulerPrefix next_cycle_prefix;
    next_cycle_prefix.frontier_generation = 2U;
    next_cycle_prefix.frontier_cursor = 0U;
    next_cycle_prefix.frontier_end = 1U;
    next_cycle_prefix.time = 1U;
    next_cycle_prefix.delta = 1U;
    next_cycle_prefix.phase = SchedulerPhase::active;
    next_cycle_prefix.process_domain = ProcessSchedulingDomain::generic;
    next_cycle_prefix.tasks = { { 0U,
        make_origin(1U, 1U, 3U, 1U, 1U) } };
    const auto& next_cycle_image = activation.begin_wave_reusable(
        next_cycle_prefix, boundary_inputs);
    const auto committed_input = std::ranges::find(next_cycle_image.register_inputs,
        input_binding->value_register, &RegionKernelRegisterInput::register_id);
    require(committed_input != next_cycle_image.register_inputs.end()
            && committed_input->value == PackedLogic4::from_msb_string("1"),
        "the next generic cycle reads producer data after committed publication");
    const auto next_registers = execute_region_activation_kernel(
        kernel, next_cycle_image, runtime_signals);
    activation.stage_kernel_outputs(next_cycle_image, next_registers);
    const auto next_publications = activation.pending_publications();
    const auto next_sink = std::ranges::find_if(next_publications,
        [](const RegionKernelPendingPublication& publication) {
            return publication.binding.signal == 2U;
        });
    require(next_sink != next_publications.end()
            && next_sink->value == PackedLogic4::from_msb_string("1"),
        "a later generic frontier consumes the prior cycle's projected publication");
    activation.discard_wave();

    auto wide_descriptors = descriptors;
    for (auto& descriptor : wide_descriptors) {
        descriptor.width = 129U;
    }
    const auto wide_graph = RegionGraph::build(bindings, wide_descriptors);
    const auto wide_program = wide_graph.build_compute_program(0U, bindings);
    require(wide_program.has_value()
            && std::ranges::all_of(wide_program->activation_kernel.inputs,
                [](const RegionConeKernelInput& input) {
                    return input.width == 129U
                        && input.value_kind == ValueKind::logic4;
                })
            && std::ranges::all_of(wide_program->activation_kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.width == 129U
                        && output.value_kind == ValueKind::logic4;
                }),
        "wide unresolved Logic4 projected cones retain their exact input and output widths");

    auto logic9_processes = processes;
    for (auto& process : logic9_processes) {
        process.register_value_kinds = { ValueKind::logic9 };
    }
    const auto logic9_bindings = bind_processes(logic9_processes);
    auto logic9_descriptors = wide_descriptors;
    for (auto& descriptor : logic9_descriptors) {
        descriptor.value_kind = ValueKind::logic9;
    }
    const auto logic9_graph
        = RegionGraph::build(logic9_bindings, logic9_descriptors);
    const auto logic9_program
        = logic9_graph.build_compute_program(0U, logic9_bindings);
    require(logic9_program.has_value()
            && std::ranges::all_of(
                logic9_program->activation_kernel.inputs,
                [](const RegionConeKernelInput& input) {
                    return input.width == 129U
                        && input.value_kind == ValueKind::logic9;
                })
            && std::ranges::all_of(
                logic9_program->activation_kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.width == 129U
                        && output.value_kind == ValueKind::logic9;
                }),
        "wide unresolved Logic9 projected cones retain all four planes and exact widths");

    auto resolved_logic9_descriptors = logic9_descriptors;
    resolved_logic9_descriptors[2U].resolution = ResolutionKind::std_logic;
    const auto resolved_logic9_graph = RegionGraph::build(
        logic9_bindings, resolved_logic9_descriptors);
    require(!resolved_logic9_graph.build_compute_program(
                0U, logic9_bindings),
        "resolved Logic9 projected outputs stay on the checked route");

    auto delayed_logic9_processes = logic9_processes;
    delayed_logic9_processes[0U].operations.replace(1U,
        WriteProjected { 1U, 0U, 1U, 0U,
            ProjectedDelayMode::inertial });
    const auto delayed_logic9_bindings
        = bind_processes(delayed_logic9_processes);
    const auto delayed_logic9_graph = RegionGraph::build(
        delayed_logic9_bindings, logic9_descriptors);
    require(!delayed_logic9_graph.build_compute_program(
                0U, delayed_logic9_bindings),
        "delayed Logic9 projected outputs stay on the checked route");

    const auto rejects_projected = [&](const WriteProjected write) {
        auto invalid = processes;
        invalid[0U].operations.replace(1U, write);
        const auto invalid_bindings = bind_processes(invalid);
        const auto invalid_graph = RegionGraph::build(
            invalid_bindings, descriptors);
        return !invalid_graph.build_compute_program(0U, invalid_bindings);
    };
    require(rejects_projected(WriteProjected {
                1U, 0U, 1U, 0U, ProjectedDelayMode::inertial })
            && rejects_projected(WriteProjected {
                1U, 0U, 0U, 1U, ProjectedDelayMode::inertial })
            && rejects_projected(WriteProjected {
                1U, 0U, 0U, 0U, ProjectedDelayMode::transport }),
        "delayed, rejecting, and transport assignments stay on the ordinary projected route");
}

void check_chain_and_observation()
{
    // Reverse dependency order proves that canonical process IDs do not stand
    // in for the elaborated dataflow order.
    std::vector<Process> processes {
        transfer(0U, 1U, 2U), transfer(1U, 0U, 1U)
    };
    std::vector<RegionSignalDescriptor> signals(3U, { 129U });
    signals[1].observations = RegionObservation::previous | RegionObservation::drivers;
    processes[0].static_sensitivity = { { 1U, EdgeKind::any, 63U, 65U } };
    const auto graph = build(processes, signals);
    require(graph.topological_order().size() == 2U
            && graph.topological_order()[0] == 1U
            && graph.topological_order()[1] == 0U,
        "graph must follow dependencies independently of instance order");
    require(graph.processes()[0].sensitivities == processes[0].static_sensitivity,
        "graph must retain normalized sensitivity ranges");
    require(graph.signals()[1].drivers == RegionDriverClass::single_whole,
        "whole net driver must have one original owner");
    require(graph.signals()[1].invalidation_dependencies
            == std::vector<ProcessId> { 0U, 1U },
        "observation invalidation must include readers and original owners");
    require(graph.signals()[1].observations == signals[1].observations,
        "observation capabilities must survive graph construction");
    require(graph.processes()[0].scheduling_domain == ProcessSchedulingDomain::systemverilog
            && graph.processes()[0].update_kind == RegionUpdateKind::systemverilog_active,
        "process scheduling and publication provenance must remain explicit");
    const auto repeated = build(processes, signals);
    require(std::vector<ProcessId>(repeated.topological_order().begin(),
                repeated.topological_order().end())
            == std::vector<ProcessId>(graph.topological_order().begin(),
                graph.topological_order().end()),
        "graph construction must be deterministic");
}

void check_selective_observation_epochs()
{
    const std::vector<RegionSignalDescriptor> signals(5U, { 65U });
    const std::vector<Process> processes {
        transfer(0U, 0U, 1U), transfer(1U, 1U, 2U), transfer(2U, 3U, 4U)
    };
    auto graph = build(processes, signals);
    const auto observed = graph.observe_signal(1U,
        RegionObservation::previous | RegionObservation::drivers);
    require(std::vector<ProcessId>(observed.begin(), observed.end())
                == std::vector<ProcessId> { 0U, 1U }
            && graph.capability_epoch(0U) == 2U
            && graph.capability_epoch(1U) == 2U
            && graph.capability_epoch(2U) == 1U,
        "late observation invalidates only the original owner and dependent reader");
    require(graph.signals()[1].observations
            == (RegionObservation::previous | RegionObservation::drivers),
        "materialization requirements persist for later recertification");
    static_cast<void>(graph.observe_signal(1U, RegionObservation::current));
    require(graph.capability_epoch(0U) == 3U
            && graph.capability_epoch(1U) == 3U
            && graph.capability_epoch(2U) == 1U,
        "successive capability changes cannot revive an old certificate");
    bool rejected { };
    try {
        static_cast<void>(graph.observe_signal(5U, RegionObservation::unknown));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    require(rejected && graph.capability_epoch(0U) == 3U,
        "invalid observation handles cannot partially change graph capabilities");
}

bool captured_epochs_are_current(
    const RegionGraph& graph, const std::size_t component)
{
    const auto& certificate
        = graph.certificate_inventory().components.at(component);
    return std::ranges::all_of(certificate.captured_epochs,
        [&](const auto& captured) {
            return captured.epoch != 0U
                && graph.capability_epoch(captured.process) == captured.epoch;
        });
}

void check_component_epoch_fast_state()
{
    const std::vector<RegionSignalDescriptor> signals(6U, { 1U });
    const std::vector<Process> processes {
        transfer(0U, 0U, 1U), transfer(1U, 1U, 2U),
        transfer(2U, 3U, 4U)
    };
    auto graph = build(processes, signals);
    auto copied_graph = graph;
    const auto component_count
        = graph.certificate_inventory().components.size();
    require(component_count == 2U,
        "epoch fast state fixture has two independent certificate components");
    for (std::size_t component = 0U;
         component < component_count;
         ++component) {
        require(graph.component_epochs_current(component)
                == captured_epochs_are_current(graph, component),
            "component epoch fast state matches captured epoch scan at build");
    }

    static_cast<void>(graph.observe_signal(5U, RegionObservation::current));
    require(graph.component_epochs_current(0U)
            && graph.component_epochs_current(1U),
        "observing an isolated signal leaves all components current");

    static_cast<void>(graph.observe_signal(1U, RegionObservation::current));
    static_cast<void>(graph.observe_signal(1U, RegionObservation::current));
    require(graph.capability_epoch(0U) == 3U
            && graph.capability_epoch(1U) == 3U
            && !graph.component_epochs_current(0U)
            && graph.component_epochs_current(1U)
            && graph.component_epochs_current(0U)
                == captured_epochs_are_current(graph, 0U)
            && graph.component_epochs_current(1U)
                == captured_epochs_are_current(graph, 1U),
        "repeated observations preserve epoch bumps and stale only the affected component");
    require(copied_graph.component_epochs_current(0U)
            && copied_graph.component_epochs_current(1U),
        "a copied graph retains independent component invalidation state");

    static_cast<void>(copied_graph.observe_signal(
        3U, RegionObservation::current));
    require(copied_graph.component_epochs_current(0U)
            && !copied_graph.component_epochs_current(1U)
            && copied_graph.component_epochs_current(0U)
                == captured_epochs_are_current(copied_graph, 0U)
            && copied_graph.component_epochs_current(1U)
                == captured_epochs_are_current(copied_graph, 1U)
            && graph.component_epochs_current(1U),
        "copied graph observations invalidate only its own matching component");

    bool rejected_invalid_component { };
    try {
        static_cast<void>(graph.component_epochs_current(component_count));
    } catch (const std::out_of_range&) {
        rejected_invalid_component = true;
    }
    require(rejected_invalid_component,
        "epoch fast state preserves invalid component bounds behavior");
}

void check_systemverilog_partial_boundary_output_slices()
{
    constexpr std::uint32_t signal_width = 129U;
    constexpr SignalId input_signal = 0U;
    constexpr SignalId internal_signal = 1U;
    constexpr SignalId boundary_signal = 2U;
    constexpr ProcessId producer_id = 0U;
    constexpr ProcessId low_owner_id = 1U;
    constexpr ProcessId high_owner_id = 2U;

    std::vector<RegionSignalDescriptor> descriptors {
        { signal_width }, { signal_width },
        { signal_width, ResolutionKind::sv_wire },
    };
    descriptors[boundary_signal].observations = RegionObservation::current;

    auto producer = transfer(producer_id, input_signal, internal_signal);
    auto low_owner = transfer(low_owner_id, internal_signal, boundary_signal);
    low_owner.register_count = 3U;
    low_owner.register_value_kinds.assign(3U, ValueKind::logic4);
    low_owner.static_sensitivity = { { internal_signal, EdgeKind::any } };
    low_owner.driver_regions = {
        { boundary_signal, 0U, 31U, false },
        { boundary_signal, 32U, 33U, false },
    };
    low_owner.operations = {
        ReadSignal { 0U, internal_signal },
        Extract { 1U, 0U, 0U, 31U },
        WriteUpdateSlice { boundary_signal, 1U, 0U,
            SignalUpdateDomain::systemverilog_active },
        Extract { 2U, 0U, 32U, 33U },
        WriteUpdateSlice { boundary_signal, 2U, 32U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    auto high_owner = transfer(
        high_owner_id, internal_signal, boundary_signal);
    high_owner.register_count = 2U;
    high_owner.register_value_kinds.assign(2U, ValueKind::logic4);
    high_owner.static_sensitivity = { { internal_signal, EdgeKind::any } };
    high_owner.driver_regions = {
        { boundary_signal, 66U, 63U, false },
    };
    high_owner.operations = {
        ReadSignal { 0U, internal_signal },
        Extract { 1U, 0U, 66U, 63U },
        WriteUpdateSlice { boundary_signal, 1U, 66U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    std::vector<Process> processes {
        std::move(producer), std::move(low_owner), std::move(high_owner)
    };
    const auto bindings = bind_processes(processes);
    const auto graph = RegionGraph::build(bindings, descriptors);
    const auto& output_node = graph.signals()[boundary_signal];
    require(output_node.drivers == RegionDriverClass::disjoint_partial
            && output_node.writers.size() == 3U,
        "SV boundary slices preserve two disjoint owner ranges and one owner's two write sites");
    const auto& certificate = graph.certificate_inventory().components;
    require(certificate.size() == 1U
            && certificate.front().members
                == std::vector<ProcessId> {
                    producer_id, low_owner_id, high_owner_id }
            && certificate.front().structural_internal_signal_candidates
                == std::vector<SignalId> { internal_signal },
        "the private full-width producer remains the only structural internal state");
    const auto cone = graph.build_compute_program(0U, bindings);
    require(cone.has_value()
            && cone->activation_kernel.members.size() == 3U
            && cone->internal_materializations.size() == 1U
            && cone->boundary_outputs.size() == 3U
            && cone->activation_kernel.internal_signals
                == std::vector<SignalId> { internal_signal },
        "partial SV writes are boundary outputs while the private producer stays materialized whole");

    const auto internal = cone->internal_materializations.front();
    require(internal.owner == producer_id
            && internal.signal == internal_signal
            && internal.offset == 0U && internal.width == signal_width
            && internal.signal_width == signal_width,
        "private internal materialization remains a full-signal single-owner write");

    struct ExpectedOutput final {
        ProcessId owner;
        InstructionIndex instruction;
        std::uint32_t offset;
        std::uint32_t width;
    };
    const std::array<ExpectedOutput, 3U> expected {
        ExpectedOutput { low_owner_id, 2U, 0U, 31U },
        ExpectedOutput { low_owner_id, 4U, 32U, 33U },
        ExpectedOutput { high_owner_id, 2U, 66U, 63U },
    };
    for (const auto& source : expected) {
        const auto binding = std::ranges::find_if(cone->boundary_outputs,
            [&](const RegionConeOutputBinding& candidate) {
                return candidate.owner == source.owner
                    && candidate.source_instruction == source.instruction;
            });
        require(binding != cone->boundary_outputs.end()
                && binding->signal == boundary_signal
                && binding->offset == source.offset
                && binding->width == source.width
                && binding->signal_width == signal_width
                && binding->domain
                    == SignalUpdateDomain::systemverilog_active
                && binding->update_kind
                    == RegionUpdateKind::systemverilog_active
                && binding->publication_kind
                    == RegionOutputPublicationKind::update,
            "each captured boundary slice retains source owner, range, and full plane width");
        const auto operation
            = processes[source.owner].operations.expanded(source.instruction);
        const auto* const write = operation_get_if<WriteUpdateSlice>(&operation);
        require(write != nullptr && write->signal == boundary_signal
                && write->offset == source.offset
                && write->domain
                    == SignalUpdateDomain::systemverilog_active,
            "the binding authenticates its original direct-signal slice instruction");
    }
    require(cone->boundary_outputs[0U].source_instruction
                < cone->boundary_outputs[1U].source_instruction,
        "two disjoint writes by one owner retain distinct source order");

    auto bad_driver = processes;
    bad_driver[low_owner_id].driver_regions[1U].width = 32U;
    const auto bad_driver_bindings = bind_processes(bad_driver);
    const auto bad_driver_graph
        = RegionGraph::build(bad_driver_bindings, descriptors);
    require(!bad_driver_graph.build_compute_program(0U, bad_driver_bindings),
        "a mismatched partial boundary driver declaration declines compilation");
}

void check_driver_classes()
{
    std::vector<RegionSignalDescriptor> signals(3U, { 256U });
    signals[0].width = 65U;
    signals[1].width = 129U;
    auto left = transfer(0U, 0U, 2U);
    auto right = transfer(1U, 1U, 2U);
    left.driver_regions = { { 2U, 0U, 65U, false } };
    right.driver_regions = { { 2U, 65U, 129U, false } };
    left.operations[1] = WriteUpdateSlice {
        2U, 0U, 0U, SignalUpdateDomain::systemverilog_active
    };
    right.operations[1] = WriteUpdateSlice {
        2U, 0U, 65U, SignalUpdateDomain::systemverilog_active
    };
    std::vector<Process> processes { left, right };
    require(build(processes, signals).signals()[2].drivers
            == RegionDriverClass::disjoint_partial,
        "disjoint word-crossing ownership must not be classified as overlap");
    processes[1].driver_regions.front().offset = 64U;
    processes[1].operations[1] = WriteUpdateSlice {
        2U, 0U, 64U, SignalUpdateDomain::systemverilog_active
    };
    require(build(processes, signals).signals()[2].drivers == RegionDriverClass::resolved,
        "one overlapping bit requires ordinary resolution");
    processes.pop_back();
    require(build(processes, signals).signals()[2].drivers == RegionDriverClass::single_partial,
        "partial ownership must not silently claim the remainder");
    signals[2].external_driver = true;
    require(build(processes, signals).signals()[2].drivers == RegionDriverClass::resolved,
        "external drivers prevent exclusive ownership");
}

void check_cycles_and_domains()
{
    const std::vector<RegionSignalDescriptor> signals(6U, { 65U });
    std::vector<Process> processes {
        transfer(0U, 1U, 0U), transfer(1U, 0U, 1U),
        transfer(2U, 0U, 2U), transfer(3U, 3U, 4U)
    };
    const auto graph = build(processes, signals);
    require(graph.topological_order().size() == 1U
            && graph.topological_order().front() == 3U,
        "feedback must retain ordinary execution without rejecting an independent DAG");
    require(graph.processes()[0].cyclic_or_dependent_on_cycle
            && graph.processes()[1].cyclic_or_dependent_on_cycle
            && graph.processes()[2].cyclic_or_dependent_on_cycle
            && !graph.processes()[3].cyclic_or_dependent_on_cycle,
        "cycle-dependent state must remain conservatively classified");

    processes = { transfer(0U, 0U, 1U), transfer(1U, 1U, 2U) };
    processes[0].operations[1] = WriteUpdate {
        1U, 0U, SignalUpdateDomain::systemverilog_nba
    };
    processes[1].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[1].operations[1] = WriteProjected { 2U, 0U };
    const auto mixed = build(processes, signals);
    require(!mixed.processes()[0].pure
            && mixed.processes()[0].update_kind == RegionUpdateKind::systemverilog_nba,
        "NBA must not enter an Active combinational cone");
    require(mixed.processes()[1].pure
            && mixed.processes()[1].update_kind == RegionUpdateKind::vhdl_projected,
        "VHDL pure regions must retain their projected cycle contract");
    processes[1].operations[1] = WriteProjectedSlice { 2U, 0U, 0U };
    require(build(processes, signals).signals()[2].partial_projected_transactions,
        "partial projected writes retain their separate publication requirement");
    processes[1].operations[1] = WriteProjected { 2U, 0U, 1U };
    require(!build(processes, signals).processes()[1].pure,
        "delayed projected transactions cannot be pure immediate region effects");
}

void check_projected_operation_inventory()
{
    const std::vector<RegionSignalDescriptor> signals(3U, { 65U });
    const auto check = [&](const Operation& projected,
                           const bool expects_partial,
                           const bool expects_generic_kernel) {
        auto process = transfer(0U, 0U, 2U);
        process.scheduling_domain = ProcessSchedulingDomain::generic;
        process.language_standard = "2008";
        process.operations[1U] = projected;
        const std::vector<Process> processes { process };
        const auto graph = build(processes, signals);
        require(graph.processes()[0U].scheduling_domain
                    == ProcessSchedulingDomain::generic
                && graph.processes()[0U].update_kind
                    == RegionUpdateKind::vhdl_projected,
            "VHDL projected writes keep their generic scheduling and publication domains");
        require(!graph.processes()[0U].dependencies_unknown
                && graph.signals()[2U].writers.size() == 1U
                && graph.signals()[2U].writers[0U].process == 0U,
            "projected forms keep their known signal ownership in the graph");
        require(graph.signals()[2U].partial_projected_transactions
                == expects_partial,
            "partial and dynamically selected projected writes stay boundaries");
        const auto bindings = bind_processes(processes);
        const auto program = graph.build_compute_program(0U, bindings);
        if (!expects_generic_kernel) {
            require(!program,
                "waveform, partial, and dynamic projected writes stay on the checked route");
            return;
        }
        require(program.has_value()
                && program->activation_kernel.program.scheduling_domain
                    == ProcessSchedulingDomain::generic
                && !program->forwarding_kernel,
            "the VHDL compute body is a generic activation kernel, not an SV forwarding program");
        const auto& outputs = program->activation_kernel.outputs;
        require(outputs.size() == 1U
                && outputs.front().owner == 0U
                && outputs.front().signal == 2U
                && outputs.front().source_instruction == 1U
                && outputs.front().update_kind
                    == RegionUpdateKind::vhdl_projected
                && outputs.front().domain == SignalUpdateDomain::generic
                && outputs.front().projected_mode
                    == ProjectedDelayMode::inertial
                && outputs.front().projected_delay == 0U
                && outputs.front().projected_rejection == 0U,
            "generic activation capture retains the exact projected owner and timing contract");
    };

    check(WriteProjected { 2U, 0U, 0U, 0U,
              ProjectedDelayMode::inertial }, false, true);
    check(WriteProjectedWaveform { 2U, { { 0U, 0U } }, 0U,
              ProjectedDelayMode::inertial }, false, false);
    check(WriteProjectedSlice { 2U, 0U, 0U, 0U, 0U,
              ProjectedDelayMode::inertial }, true, false);
    check(WriteProjectedWaveformSlice { 2U, { { 0U, 0U } }, 0U, 0U,
              ProjectedDelayMode::inertial }, true, false);
    check(WriteProjectedDynamicSlice { 2U, 0U, { 0U, 0, 63, 0U, true },
              0U, 0U, ProjectedDelayMode::inertial }, true, false);
    check(WriteProjectedWaveformDynamicSlice { 2U, { { 0U, 0U } },
              { 0U, 0, 63, 0U, true }, 0U,
              ProjectedDelayMode::inertial }, true, false);
}

void check_sequential_and_language_boundaries()
{
    const std::vector<RegionSignalDescriptor> signals(4U, { 65U });
    // q -> combinational -> d -> clocked NBA -> q. The clocked owner is a
    // region boundary; its original ownership and read edges must survive.
    std::vector<Process> processes {
        transfer(0U, 1U, 2U), transfer(1U, 0U, 1U), transfer(2U, 2U, 0U)
    };
    processes[2].static_sensitivity = { { 3U, EdgeKind::posedge } };
    processes[2].operations[1] = WriteUpdate {
        0U, 0U, SignalUpdateDomain::systemverilog_nba
    };
    const auto sequential = build(processes, signals);
    require(std::vector<ProcessId>(sequential.topological_order().begin(),
                sequential.topological_order().end()) == std::vector<ProcessId> { 1U, 0U },
        "register feedback must not poison acyclic combinational ordering");
    require(!sequential.processes()[2].pure
            && !sequential.processes()[0].cyclic_or_dependent_on_cycle
            && !sequential.processes()[1].cyclic_or_dependent_on_cycle,
        "sequential dependencies must be cut only in pure-region cycle analysis");
    require(sequential.signals()[0].writers.front().process == 2U
            && sequential.signals()[2].readers.front().process == 2U
            && sequential.signals()[0].invalidation_dependencies
                == std::vector<ProcessId> { 1U, 2U },
        "boundary ownership, reads and invalidation edges must remain available");

    // A feedback path crossing SV Active and VHDL projected transactions is
    // two domain-local DAGs. Each must still yield at its own cycle boundary.
    processes = { transfer(0U, 1U, 0U), transfer(1U, 0U, 1U) };
    processes[1].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[1].operations[1] = WriteProjected { 1U, 0U };
    const auto mixed = build(processes, signals);
    require(mixed.topological_order().size() == 2U
            && mixed.processes()[0].pure && mixed.processes()[1].pure
            && !mixed.processes()[0].cyclic_or_dependent_on_cycle
            && !mixed.processes()[1].cyclic_or_dependent_on_cycle,
        "mixed-language boundaries must not become same-wave cycle edges");
    require(mixed.signals()[0].invalidation_dependencies
                == std::vector<ProcessId> { 0U, 1U }
            && mixed.signals()[1].invalidation_dependencies
                == std::vector<ProcessId> { 0U, 1U },
        "cross-domain observation dependencies must not be discarded");

    // Publication contracts also form a boundary when both process domains
    // use the generic scheduler (for example a VHDL/SystemC crossing).
    processes[0].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[0].operations[1] = WriteUpdate { 0U, 0U };
    require(build(processes, signals).topological_order().size() == 2U,
        "distinct publication contracts must remain separate in a shared scheduler");
    processes[0].operations[1] = WriteProjected { 0U, 0U };
    const auto projected_loop = build(processes, signals);
    require(projected_loop.topological_order().empty()
            && projected_loop.processes()[0].cyclic_or_dependent_on_cycle
            && projected_loop.processes()[1].cyclic_or_dependent_on_cycle,
        "a genuine same-domain projected feedback loop must remain ordinary");
}

void check_invalid_ranges_and_effects()
{
    const std::vector<RegionSignalDescriptor> signals(2U, { 1024U });
    auto process = transfer(0U, 0U, 1U);
    process.static_sensitivity = { { 0U, EdgeKind::any, 1023U, 2U } };
    bool rejected { };
    try {
        static_cast<void>(build({ process }, signals));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "out-of-bounds dependency metadata must be rejected");
    process.static_sensitivity = { { 0U, EdgeKind::any } };
    process.operations[0] = ReadSignal { 0U, 0U, SignalReadKind::past };
    const auto graph = build({ process }, signals);
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && graph.certificate_inventory().access_inventory_complete,
        "history-sensitive fixed-ID reads require ordinary execution "
        "while retaining a complete inventory");
    require(graph.signals()[0].observations
            == (RegionObservation::previous | RegionObservation::events),
        "history observation requirements must survive lowering analysis");

    process = transfer(0U, 0U, 1U);
    process.driver_regions = { { 1U, 64U, 65U, false } };
    const auto understated = build({ process }, signals);
    require(!understated.processes()[0].pure
            && understated.signals()[1].drivers == RegionDriverClass::unknown,
        "a whole write must not be admitted under narrower ownership metadata");
    process.operations = {
        LoadConstant { 0U, PackedLogic4 { 1024U, Logic4::zero } },
        WriteUpdate { 1U, 0U, SignalUpdateDomain::systemverilog_active },
        Halt { },
    };
    const auto initial_understated = build({ process }, signals);
    require(initial_understated.signals()[1].drivers == RegionDriverClass::unknown,
        "an initial writer cannot claim narrower ownership than its actual update");
    process.driver_regions = { { 1U, 0U, 0U, true } };
    require(build({ process }, signals).signals()[1].drivers
            == RegionDriverClass::single_whole,
        "a checked constant initial writer retains exact original ownership");
    process.driver_regions.clear();
    const auto missing = build({ process }, signals);
    require(!missing.processes()[0].pure
            && missing.signals()[1].drivers == RegionDriverClass::unknown
            && missing.signals()[1].invalidation_dependencies
                == std::vector<ProcessId> { 0U },
        "undeclared writers must retain original-owner invalidation dependencies");
}

void check_empty_array_boundary()
{
    // A VHDL null-range array is a real signal with an empty packed value.
    // Preserve its graph identity without certifying empty-value execution.
    const std::vector<RegionSignalDescriptor> signals {
        { 0U }, { 1U }, { 1U }, { 1U }, { 1U }
    };
    const auto graph = build({ transfer(0U, 0U, 1U), transfer(1U, 2U, 0U),
                                 transfer(2U, 3U, 4U) }, signals);
    require(graph.signals()[0].descriptor.width == 0U
            && graph.signals()[0].observations == RegionObservation::unknown
            && graph.signals()[0].drivers == RegionDriverClass::unknown,
        "empty arrays must retain an opaque graph node");
    require(graph.signals()[0].invalidation_dependencies
            == std::vector<ProcessId> { 0U, 1U },
        "empty array readers and original owners must remain identifiable");
    require(!graph.processes()[0].pure && !graph.processes()[1].pure
            && !graph.processes()[0].dependencies_unknown
            && !graph.processes()[1].dependencies_unknown
            && graph.certificate_inventory().access_inventory_complete
            && graph.topological_order().size() == 1U
            && graph.topological_order().front() == 2U,
        "known empty-array identities remain boundaries without poisoning "
        "an independent packed region");

    Interpreter interpreter;
    const auto empty = interpreter.add_signal({ "empty", PackedLogic4 { } });
    interpreter.prepare_signal_observation(empty);
    bool invalid_rejected { };
    try {
        interpreter.prepare_signal_observation(empty + 1U);
    } catch (const std::out_of_range&) {
        invalid_rejected = true;
    }
    require(invalid_rejected, "pre-start observation must validate the signal ID");
    require(interpreter.run().status == RunStatus::completed
            && interpreter.signal_value(empty).width() == 0U,
        "graph construction must preserve interpreter empty-array startup");
}

void check_fork_ownership()
{
    const std::vector<RegionSignalDescriptor> signals(1U, { 65U });
    Process process;
    process.id = 0U;
    process.register_count = 1U;
    process.driver_regions = { { 0U, 64U, 1U, false } };
    process.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
        WriteUpdateSlice { 0U, 0U, 64U },
        WaitFor { 1U }, Fork { { 5U }, ForkJoinKind::all }, Halt { },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteUpdateSlice { 0U, 0U, 64U }, ForkEnd { },
    };
    const auto checked = build({ process }, signals);
    require(!checked.processes()[0].pure
            && !checked.processes()[0].dependencies_unknown
            && !checked.signals()[0].writers_unknown
            && checked.signals()[0].dynamic_fork_writers
            && checked.signals()[0].drivers == RegionDriverClass::unknown,
        "fork accesses remain known, but dynamic child driver identities make ownership uncertain");

    process.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
        WriteUpdateSlice { 0U, 0U, 64U }, WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteUpdateSlice { 0U, 0U, 64U }, Halt { },
    };
    require(build({ process }, signals).signals()[0].drivers
            == RegionDriverClass::single_partial,
        "local definitions after suspension prove declared partial ownership");
    process.operations[3] = LoadConstant { 0U, PackedLogic4(2U, Logic4::one) };
    require(build({ process }, signals).signals()[0].writers_unknown,
        "a post-suspension write beyond declared ownership cannot be certified");
    process.operations[3] = DebugPoint { };
    require(build({ process }, signals).signals()[0].writers_unknown,
        "a suspended process cannot inherit an unproven register width");
    process.operations[3] = LoadConstant { 0U, PackedLogic4(1U, Logic4::one) };
    process.operations.insert(process.operations.begin() + 4U, WaitFor { 1U });
    require(build({ process }, signals).signals()[0].writers_unknown,
        "shared register widths must be re-established after suspension");
}

std::size_t process_exclusion_count(const RegionCertificateInventory& inventory,
    RegionProcessExclusionReason reason)
{
    return inventory.process_exclusion_counts[static_cast<std::size_t>(reason)];
}

std::size_t boundary_reason_count(const RegionCertificateInventory& inventory,
    RegionBoundaryReason reason)
{
    return inventory.boundary_reason_counts[static_cast<std::size_t>(reason)];
}

bool has_reader(const RegionSignalNode& signal, const ProcessId process)
{
    return std::ranges::any_of(signal.readers, [process](const auto& access) {
        return access.process == process;
    });
}

bool has_writer(const RegionSignalNode& signal, const ProcessId process)
{
    return std::ranges::any_of(signal.writers, [process](const auto& access) {
        return access.process == process;
    });
}

bool has_observation(
    const RegionSignalNode& signal, const RegionObservation capability)
{
    return (static_cast<std::uint32_t>(signal.observations)
               & static_cast<std::uint32_t>(capability)) != 0U;
}

void check_known_nonpure_accesses()
{
    const std::vector<RegionSignalDescriptor> signals(7U, { 1U });
    auto arithmetic = transfer(0U, 0U, 1U);
    arithmetic.register_count = 2U;
    arithmetic.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
        LoadConstant { 1U, PackedLogic4(1U, Logic4::one) },
        Binary { BinaryOperator::add_unsigned, 0U, 0U, 1U },
        SystemVerilogScalarBinary { },
        SystemVerilogMath { },
        WriteUpdate { 1U, 0U, SignalUpdateDomain::systemverilog_active },
        Call { 4U, 5U, { 0U, 1U, 1U } },
        WaitFor { 1U },
        WaitSensitivity { },
        Jump { 0U },
    };
    const std::vector<Process> processes {
        arithmetic, transfer(1U, 3U, 4U)
    };
    const auto graph = build(processes, signals);
    const auto& inventory = graph.certificate_inventory();
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && inventory.access_inventory_complete
            && inventory.components.size() == 1U
            && inventory.components[0].members
                == std::vector<ProcessId> { 1U }
            && inventory.components[0].structural_internal_signal_candidates
                == std::vector<SignalId> { 4U }
            && inventory.opaque_operation_counts.empty(),
        "known arithmetic, bounded-call and wait effects stay nonpure "
        "without poisoning an unrelated pure DAG");
}

void check_process_local_register_effects()
{
    auto register_work = transfer(0U, 0U, 1U);
    register_work.register_count = 3U;
    register_work.string_register_count = 2U;
    register_work.container_register_count = 2U;
    register_work.container_register_types = {
        ContainerType { }, ContainerType { }
    };
    register_work.static_sensitivity = { { 0U, EdgeKind::any } };
    CallableFramePush frame_push;
    frame_push.identity = 17U;
    frame_push.packed = { 0U };
    frame_push.strings = { 0U };
    frame_push.containers = { 0U };
    CallableFramePop frame_pop;
    frame_pop.identity = 17U;
    PlusArgSelect plusarg;
    plusarg.destination = 2U;
    plusarg.query = 0U;
    plusarg.selected = 1U;
    register_work.operations = {
        ReadSignal { 0U, 0U },
        Call { 6U, 2U, { } },
        WriteUpdate { 1U, 0U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
        Halt { },
        frame_push,
        ReadSignal { 1U, 2U },
        LoadStringConstant { 0U, "callee-local" },
        plusarg,
        CopyContainerRegister { 0U, 1U },
        ContainerSize { 2U, 0U },
        frame_pop,
        Return { { } },
    };

    const std::vector<RegionSignalDescriptor> signals(6U, { 1U });
    const auto graph = build(
        { register_work, transfer(1U, 3U, 4U) }, signals);
    const auto& inventory = graph.certificate_inventory();
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && inventory.access_inventory_complete
            && inventory.components.size() == 1U
            && inventory.components[0].members
                == std::vector<ProcessId> { 1U }
            && inventory.components[0].structural_internal_signal_candidates
                == std::vector<SignalId> { 4U }
            && inventory.opaque_operation_counts.empty()
            && has_reader(graph.signals()[0U], 0U)
            && has_reader(graph.signals()[2U], 0U)
            && has_writer(graph.signals()[1U], 0U)
            && graph.signals()[5].readers.empty()
            && graph.signals()[5].writers.empty(),
        "callee signal accesses stay inventoried across local string/container "
        "register work, while unrelated pure work keeps its candidate");
}

void check_output_callbacks_keep_access_inventory_known()
{
    auto output = transfer(0U, 0U, 1U);
    output.register_count = 1U;
    output.string_register_count = 1U;
    Assert assertion;
    assertion.condition = 0U;
    assertion.severity = AssertionSeverity::warning;
    output.operations = {
        ReadSignal { 0U, 0U },
        WriteUpdate { 1U, 0U, SignalUpdateDomain::systemverilog_active },
        Display { "literal output", true, false },
        FormatDisplay { },
        StringDisplay { },
        TimeDisplay { },
        Report { "literal report", AssertionSeverity::warning, { } },
        assertion,
        WaitSensitivity { },
        Jump { 0U },
    };
    const std::vector<RegionSignalDescriptor> signals(6U, { 1U });
    const auto graph = build(
        { output, transfer(1U, 4U, 5U) }, signals, true);
    const auto& inventory = graph.certificate_inventory();
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && inventory.access_inventory_complete
            && inventory.components.size() == 1U
            && inventory.components[0]
                .structural_internal_signal_candidates
                == std::vector<SignalId> { 5U }
            && inventory.opaque_operation_counts.empty()
            && has_reader(graph.signals()[0U], 0U)
            && has_writer(graph.signals()[1U], 0U),
        "output and assertion effects stay non-pure while fixed signal access "
        "is inventoried and unrelated pure work keeps its candidate");
}

void check_fork_access_inventory_is_signal_local()
{
    auto forked = transfer(0U, 0U, 1U);
    forked.register_count = 3U;
    forked.driver_regions.push_back({ 3U, 0U, 0U, true });
    forked.driver_regions.push_back({ 5U, 0U, 0U, true });
    forked.operations = {
        ReadSignal { 0U, 0U },
        Fork { { 7U, 10U }, ForkJoinKind::none },
        WriteUpdate { 1U, 0U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
        Halt { },
        DebugPoint { },
        ReadSignal { 1U, 2U },
        WriteUpdate { 3U, 1U, SignalUpdateDomain::systemverilog_active },
        ForkEnd { },
        ReadSignal { 2U, 4U },
        WriteUpdate { 5U, 2U, SignalUpdateDomain::systemverilog_active },
        ForkEnd { },
    };
    const std::vector<Process> processes {
        forked, transfer(1U, 6U, 7U), transfer(2U, 7U, 8U)
    };
    const std::vector<RegionSignalDescriptor> signals(9U, { 1U });
    const auto graph = build(processes, signals, true);
    const auto& inventory = graph.certificate_inventory();
    require(!graph.processes()[0].dependencies_unknown
            && !graph.processes()[0].pure
            && has_reader(graph.signals()[2U], 0U)
            && has_writer(graph.signals()[3U], 0U)
            && has_reader(graph.signals()[4U], 0U)
            && has_writer(graph.signals()[5U], 0U)
            && graph.signals()[1U].drivers == RegionDriverClass::unknown
            && graph.signals()[3U].drivers == RegionDriverClass::unknown
            && graph.signals()[5U].drivers == RegionDriverClass::unknown
            && graph.signals()[1U].dynamic_fork_writers
            && graph.signals()[3U].dynamic_fork_writers
            && graph.signals()[5U].dynamic_fork_writers
            && !graph.signals()[7U].dynamic_fork_writers
            && !graph.signals()[8U].dynamic_fork_writers
            && inventory.access_inventory_complete
            && inventory.components.size() == 1U
            && inventory.components[0].members
                == std::vector<ProcessId> { 1U, 2U }
            && inventory.components[0].structural_internal_signal_candidates
                == std::vector<SignalId> { 7U, 8U }
            && inventory.opaque_operation_counts.empty(),
        "fork control keeps fixed accesses known and only its written signals "
        "have dynamic ownership; unrelated pure state remains a candidate");
}

void check_invalid_fork_targets_remain_opaque()
{
    const std::vector<RegionSignalDescriptor> signals;
    const auto graph_for = [&](std::vector<Operation> operations) {
        Process malformed;
        malformed.id = 0U;
        malformed.name = "fork_access_completeness";
        malformed.operations = std::move(operations);
        return build({ malformed }, signals);
    };
    const auto incomplete = [&](std::vector<Operation> operations) {
        const auto graph = graph_for(std::move(operations));
        return graph.processes()[0U].dependencies_unknown
            && !graph.certificate_inventory().access_inventory_complete;
    };
    const auto valid_empty_fork
        = graph_for({ Fork { { }, ForkJoinKind::none }, Halt { } });
    require(!valid_empty_fork.processes()[0U].pure
            && !valid_empty_fork.processes()[0U].dependencies_unknown
            && valid_empty_fork.certificate_inventory().access_inventory_complete
            && valid_empty_fork.certificate_inventory().components.empty(),
        "a valid empty Fork with a continuation remains known but non-pure");
    require(incomplete({ Fork { { }, ForkJoinKind::none } }),
        "an empty Fork at the final instruction has no continuation");
    require(incomplete({ Fork { { 3U }, ForkJoinKind::none }, Halt { },
                ForkEnd { } }),
        "an out-of-range Fork branch is opaque to graph inventory");
    require(incomplete({ Halt { }, Fork { { 0U }, ForkJoinKind::none },
                Halt { }, ForkEnd { } }),
        "a non-forward Fork branch is opaque to graph inventory");
    require(incomplete({ Fork { { 1U }, ForkJoinKind::none }, Halt { },
                ForkEnd { } }),
        "a Fork branch equal to its continuation is opaque to graph inventory");
    require(incomplete({ Fork { { 2U, 2U }, ForkJoinKind::none }, Halt { },
                ForkEnd { } }),
        "duplicate Fork branches are opaque to graph inventory");
    require(incomplete({ Fork { { 2U }, static_cast<ForkJoinKind>(255U) },
                Halt { }, ForkEnd { } }),
        "an invalid Fork join kind is opaque to graph inventory");
}

void check_fixed_signal_effects()
{
    constexpr SignalId signal_count = 18U;
    constexpr SignalId output = 12U;
    constexpr SignalId dynamic_output = 13U;
    constexpr SignalId vital_trigger = 14U;
    constexpr SignalId vital_output = 15U;
    constexpr SignalId forced = 16U;
    std::vector<RegionSignalDescriptor> signals(signal_count, { 8U });
    Process process;
    process.id = 0U;
    process.name = "known_fixed_signal_effects";
    process.register_count = 2U;
    process.driver_regions = {
        { output, 0U, 0U, true },
        { dynamic_output, 0U, 0U, true },
        { vital_trigger, 0U, 0U, true },
        { vital_output, 0U, 0U, true },
    };
    VitalTimingCheck timing_check;
    timing_check.test_signal = 9U;
    timing_check.reference_signal = 10U;
    timing_check.trigger_signal = vital_trigger;
    VitalDelay vital_delay;
    vital_delay.output = vital_output;
    process.operations = {
        ReadSignal { 0U, 0U, SignalReadKind::past, 1U,
            std::optional<SignalId> { 1U }, SampledClockEdge::any,
            std::optional<SignalId> { 2U } },
        SignalEvent { 0U, 3U },
        SignalLastValue { 0U, 4U },
        SignalLastEvent { 0U, 5U },
        SignalActive { 0U, 6U },
        SignalLastActive { 0U, 7U },
        SignalDriving { 0U, output },
        SignalDrivingValue { 0U, output },
        WaitOn { std::vector<SignalId> { 8U } },
        WaitOrder { { 11U }, 0U },
        EventAlias { 17U, 8U, true },
        EventTriggered { 0U, 11U },
        WriteBlocking { output, 0U },
        WriteUpdateDynamicSlice { dynamic_output, 0U,
            DynamicIndex { 1U, 7, 0, 0U, false },
            SignalUpdateDomain::systemverilog_active },
        timing_check,
        vital_delay,
        ForceSignalSlice { forced, 0U, 0U, std::nullopt, false },
        Halt { },
    };

    const auto graph = build({ process }, signals);
    const auto& inventory = graph.certificate_inventory();
    require(inventory.access_inventory_complete
            && !graph.processes()[0].dependencies_unknown
            && inventory.opaque_operation_counts.empty(),
        "fixed signal IDs remain complete even when their operation effects are nonpure");
    for (const auto signal : { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
             8U, 9U, 10U, 11U, 17U }) {
        require(has_reader(graph.signals()[signal], 0U),
            "fixed signal attributes, waits, event aliases and VITAL inputs retain reader IDs");
    }
    require(has_writer(graph.signals()[output], 0U)
            && has_writer(graph.signals()[dynamic_output], 0U)
            && has_writer(graph.signals()[vital_trigger], 0U)
            && has_writer(graph.signals()[vital_output], 0U)
            && graph.signals()[dynamic_output].writers.front().width == 0U
            && graph.signals()[dynamic_output].writers_unknown,
        "fixed and dynamic-slice outputs keep their owner, with dynamic range conservative");
    require(has_observation(graph.signals()[0U], RegionObservation::previous)
            && has_observation(graph.signals()[0U], RegionObservation::events)
            && has_observation(graph.signals()[1U], RegionObservation::events)
            && has_observation(graph.signals()[2U], RegionObservation::current)
            && has_observation(graph.signals()[4U], RegionObservation::previous)
            && has_observation(graph.signals()[output], RegionObservation::drivers)
            && has_observation(graph.signals()[forced], RegionObservation::mutation)
            && has_reader(graph.signals()[forced], 0U),
        "known signal effects preserve conservative per-signal observation capabilities");
    require(!has_reader(graph.signals()[dynamic_output], 0U),
        "a fixed-target write is not invented as a signal read");
}

void check_closed_component_certificates()
{
    const std::vector<RegionSignalDescriptor> signals {
        { 1U }, { 1U }, { 1U, ResolutionKind::none, ValueKind::logic4,
            false, false, false, RegionObservation::current },
        { 1U }, { 1U }
    };
    const std::vector<Process> processes {
        transfer(0U, 0U, 1U), transfer(1U, 1U, 2U),
        transfer(2U, 3U, 4U)
    };
    auto graph = build(processes, signals);
    const auto& inventory = graph.certificate_inventory();
    require(inventory.access_inventory_complete
            && inventory.components.size() == 2U,
        "known active DAG processes form deterministic certificate components");
    require(inventory.components[0].members == std::vector<ProcessId> { 0U, 1U }
            && inventory.components[0].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U }
            && inventory.components[0].boundary_signals
                == std::vector<SignalId> { 0U, 2U }
            && inventory.components[0].requires_runtime_execution_proof
            && !inventory.components[0].requires_runtime_single_driver_proof
            && inventory.components[0].status
                == RegionComponentCertificateStatus::structural_candidate
            && boundary_reason_count(inventory,
                RegionBoundaryReason::observed_current) == 1U,
        "an unobserved single-whole-driver signal closed over all readers "
        "is an internal candidate");
    require(inventory.components[0].captured_epochs
                == std::vector<RegionCertificateEpoch> { { 0U, 1U }, { 1U, 1U } }
            && graph.component_epochs_current(0U),
        "structural candidates capture each member capability epoch");
    require(inventory.components[1].members == std::vector<ProcessId> { 2U }
            && inventory.components[1].structural_internal_signal_candidates
                == std::vector<SignalId> { 4U }
            && graph.component_epochs_current(1U),
        "independent components retain separate epoch snapshots");
    require(build(processes, signals).certificate_inventory() == inventory,
        "certificate members, boundaries, epochs and counts are deterministic");

    static_cast<void>(graph.observe_signal(1U, RegionObservation::current));
    require(!graph.component_epochs_current(0U)
            && graph.component_epochs_current(1U),
        "late observation stales only the certificate depending on that signal");
}

void check_certificate_boundaries()
{
    auto external_reader = transfer(2U, 2U, 3U);
    external_reader.scheduling_domain = ProcessSchedulingDomain::generic;
    external_reader.operations[1] = WriteUpdate { 3U, 0U };
    const std::vector<Process> processes {
        transfer(0U, 0U, 1U), transfer(1U, 1U, 2U), external_reader
    };
    const std::vector<RegionSignalDescriptor> signals(4U, { 1U });
    const auto inventory = build(processes, signals).certificate_inventory();
    require(inventory.components.size() == 2U
            && inventory.components[0].members
                == std::vector<ProcessId> { 0U, 1U }
            && inventory.components[0].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U }
            && inventory.components[0].boundary_signals
                == std::vector<SignalId> { 0U, 2U }
            && inventory.components[0].status
                == RegionComponentCertificateStatus::structural_candidate
            && inventory.components[1].members
                == std::vector<ProcessId> { 2U }
            && inventory.components[1].structural_internal_signal_candidates
                == std::vector<SignalId> { 3U }
            && inventory.components[1].boundary_signals
                == std::vector<SignalId> { 2U }
            && inventory.components[1].status
                == RegionComponentCertificateStatus::structural_candidate,
        "an outside generic reader stays separate while both components "
        "retain the correct boundary and closed internal signal");
    require(process_exclusion_count(inventory,
                RegionProcessExclusionReason::wrong_scheduling_domain) == 0U
            && process_exclusion_count(inventory,
                RegionProcessExclusionReason::wrong_update_kind) == 0U
            && boundary_reason_count(inventory,
                RegionBoundaryReason::reader_outside_component) == 1U
            && boundary_reason_count(inventory,
                RegionBoundaryReason::writer_outside_component) == 1U,
        "generic updates are admitted separately and cross-component accesses "
        "remain counted as signal boundaries");
}

void check_read_only_sibling_certificate_partition()
{
    const std::vector<RegionSignalDescriptor> signals(4U, { 1U });
    const std::vector<Process> readers {
        transfer(0U, 0U, 1U), transfer(1U, 0U, 2U)
    };
    auto graph = build(readers, signals);
    const auto& inventory = graph.certificate_inventory();
    require(inventory.components.size() == 2U
            && inventory.components[0U].members == std::vector<ProcessId> { 0U }
            && inventory.components[1U].members == std::vector<ProcessId> { 1U }
            && inventory.components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U }
            && inventory.components[1U].structural_internal_signal_candidates
                == std::vector<SignalId> { 2U }
            && inventory.components[0U].boundary_signals
                == std::vector<SignalId> { 0U }
            && inventory.components[1U].boundary_signals
                == std::vector<SignalId> { 0U }
            && inventory.components[0U].status
                == RegionComponentCertificateStatus::structural_candidate
            && inventory.components[1U].status
                == RegionComponentCertificateStatus::structural_candidate,
        "read-only same-domain siblings stay separate around their shared input");
    require(boundary_reason_count(inventory,
                RegionBoundaryReason::no_internal_writer) == 2U
            && boundary_reason_count(inventory,
                RegionBoundaryReason::reader_outside_component) == 2U
            && build(readers, signals).certificate_inventory() == inventory,
        "split reader certificates retain exact boundary reasons and deterministic order");

    const auto output_observers = graph.observe_signal(1U,
        RegionObservation::current);
    require(std::ranges::equal(output_observers,
                std::array<ProcessId, 1U> { 0U })
            && !graph.component_epochs_current(0U)
            && graph.component_epochs_current(1U),
        "observing one private output stales only its reader component");
    const auto shared_input_observers = graph.observe_signal(0U,
        RegionObservation::current);
    require(std::ranges::equal(shared_input_observers,
                std::array<ProcessId, 2U> { 0U, 1U })
            && !graph.component_epochs_current(0U)
            && !graph.component_epochs_current(1U),
        "a late observation of the shared input stales both reader components");

    auto externally_driven_signals = signals;
    externally_driven_signals[0U].external_driver = true;
    const auto external_inventory
        = build(readers, externally_driven_signals).certificate_inventory();
    require(external_inventory.components.size() == 2U
            && external_inventory.components[0U].members
                == std::vector<ProcessId> { 0U }
            && external_inventory.components[1U].members
                == std::vector<ProcessId> { 1U }
            && external_inventory.components[0U].boundary_signals
                == std::vector<SignalId> { 0U }
            && external_inventory.components[1U].boundary_signals
                == std::vector<SignalId> { 0U }
            && boundary_reason_count(external_inventory,
                RegionBoundaryReason::external_driver) == 2U,
        "an external driver does not reunite read-only sibling components");

    auto same_domain_writer = transfer(2U, 3U, 0U);
    const std::vector<Process> closed_processes {
        readers[0U], readers[1U], same_domain_writer
    };
    const auto closed_inventory = build(closed_processes, signals)
        .certificate_inventory();
    require(closed_inventory.components.size() == 1U
            && closed_inventory.components[0U].members
                == std::vector<ProcessId> { 0U, 1U, 2U }
            && closed_inventory.components[0U]
                .structural_internal_signal_candidates
                    == std::vector<SignalId> { 0U, 1U, 2U }
            && closed_inventory.components[0U].boundary_signals
                == std::vector<SignalId> { 3U },
        "a same-domain candidate writer still closes over all of its readers");

    auto other_domain_writer = transfer(2U, 3U, 0U);
    other_domain_writer.scheduling_domain = ProcessSchedulingDomain::generic;
    other_domain_writer.operations[1U] = WriteUpdate {
        0U, 0U, SignalUpdateDomain::generic };
    auto noncandidate_writer = transfer(3U, 3U, 0U);
    noncandidate_writer.operations[1U] = WriteUpdate {
        0U, 0U, SignalUpdateDomain::systemverilog_nba };
    const std::vector<Process> mixed_writers {
        readers[0U], readers[1U], other_domain_writer, noncandidate_writer
    };
    const auto mixed_inventory = build(mixed_writers, signals)
        .certificate_inventory();
    require(mixed_inventory.components.size() == 3U
            && mixed_inventory.components[0U].members
                == std::vector<ProcessId> { 0U }
            && mixed_inventory.components[1U].members
                == std::vector<ProcessId> { 1U }
            && mixed_inventory.components[2U].members
                == std::vector<ProcessId> { 2U }
            && mixed_inventory.components[0U].boundary_signals
                == std::vector<SignalId> { 0U }
            && mixed_inventory.components[1U].boundary_signals
                == std::vector<SignalId> { 0U }
            && mixed_inventory.components[2U].boundary_signals
                == std::vector<SignalId> { 0U, 3U }
            && boundary_reason_count(mixed_inventory,
                RegionBoundaryReason::reader_outside_component) >= 2U
            && process_exclusion_count(mixed_inventory,
                RegionProcessExclusionReason::not_pure) != 0U
            && build(mixed_writers, signals).certificate_inventory()
                == mixed_inventory,
        "other-domain writers stay separate, noncandidate writers stay outside, "
        "and membership is stable");
}

void check_certificate_signal_exclusions()
{
    auto partial = transfer(0U, 0U, 1U);
    partial.driver_regions = { { 1U, 0U, 1U, false } };
    partial.operations[1] = WriteUpdateSlice {
        1U, 0U, 0U, SignalUpdateDomain::systemverilog_active
    };
    const std::vector<RegionSignalDescriptor> partial_signals {
        { 1U }, { 2U }
    };
    const auto partial_inventory
        = build({ partial }, partial_signals).certificate_inventory();
    require(partial_inventory.components.size() == 1U
            && partial_inventory.components[0]
                .structural_internal_signal_candidates.empty()
            && partial_inventory.components[0].boundary_signals
                == std::vector<SignalId> { 0U, 1U }
            && partial_inventory.components[0].status
                == RegionComponentCertificateStatus::no_internal_state
            && boundary_reason_count(partial_inventory,
                RegionBoundaryReason::partial_driver) == 1U,
        "partial owners remain public boundaries, not internal-state candidates");

    const std::vector<RegionSignalDescriptor> resolved_signals(3U, { 1U });
    const std::vector<Process> resolved_processes {
        transfer(0U, 0U, 2U), transfer(1U, 1U, 2U)
    };
    const auto resolved_inventory
        = build(resolved_processes, resolved_signals).certificate_inventory();
    require(resolved_inventory.components.size() == 1U
            && resolved_inventory.components[0].members
                == std::vector<ProcessId> { 0U, 1U }
            && resolved_inventory.components[0]
                .structural_internal_signal_candidates.empty()
            && boundary_reason_count(resolved_inventory,
                RegionBoundaryReason::resolved_driver_class) == 1U,
        "multiple whole writers remain ordinary resolved boundary state");

    auto wire = transfer(0U, 0U, 1U);
    auto wire_signals = std::vector<RegionSignalDescriptor> {
        { 1U }, { 1U, ResolutionKind::sv_wire }
    };
    const auto wire_graph = build({ wire }, wire_signals);
    require(wire_graph.signals()[1].drivers == RegionDriverClass::single_whole
            && wire_graph.certificate_inventory().components[0]
                .structural_internal_signal_candidates
                    == std::vector<SignalId> { 1U }
            && wire_graph.certificate_inventory().components[0]
                .runtime_single_driver_proof_signals
                == std::vector<SignalId> { 1U }
            && wire_graph.certificate_inventory().components[0]
                .requires_runtime_single_driver_proof
            && wire_graph.certificate_inventory().components[0]
                .requires_runtime_execution_proof
            && wire_graph.certificate_inventory().components[0].status
                == RegionComponentCertificateStatus::structural_candidate,
        "single-driver sv_wire is a structural candidate with an explicit runtime route proof");
    wire_signals[1].resolution = ResolutionKind::sv_wand;
    const auto wand_inventory = build({ wire }, wire_signals).certificate_inventory();
    require(wand_inventory.components[0]
                .structural_internal_signal_candidates.empty()
            && boundary_reason_count(wand_inventory,
                RegionBoundaryReason::unsupported_resolution_mode) == 1U,
        "wired-AND resolution stays public until its own route is certified");

    std::vector<RegionSignalDescriptor> descriptors(24U, { 1U });
    descriptors[3].value_kind = ValueKind::logic9;
    descriptors[5].implicit_driver = true;
    descriptors[7].external_driver = true;
    descriptors[9].event_variable = true;
    descriptors[11].observations = RegionObservation::coverage;
    descriptors[13].observations = RegionObservation::unknown;
    descriptors[15].observations = RegionObservation::previous;
    descriptors[17].observations = RegionObservation::drivers;
    descriptors[19].observations = RegionObservation::pending;
    descriptors[21].observations = RegionObservation::events;
    descriptors[23].observations = RegionObservation::mutation;
    std::vector<Process> isolated;
    for (ProcessId id = 0U; id < 12U; ++id) {
        const SignalId input = id * 2U;
        const SignalId output = input + 1U;
        isolated.push_back(transfer(id, input, output));
    }
    // Wide Logic9 storage is supported by exact activation kernels. The
    // other signals independently retain every observation exclusion.
    isolated[1] = transfer(1U, 2U, 3U);
    isolated[1].register_count = 1U;
    isolated[1].operations[0] = LoadConstant {
        0U, PackedLogic4(65U, Logic4::zero)
    };
    descriptors[3].width = 65U;
    const auto observed_inventory = build(isolated, descriptors).certificate_inventory();
    require(observed_inventory.components.size() == 12U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::unsupported_width) == 0U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::unsupported_value_kind) == 0U
            && observed_inventory.components[1U]
                .structural_internal_signal_candidates
                    == std::vector<SignalId> { 3U }
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::implicit_driver) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::external_driver) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::event_signal) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_coverage) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_unknown) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_previous) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_drivers) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_pending) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_events) == 1U
            && boundary_reason_count(observed_inventory,
                RegionBoundaryReason::observed_mutation) == 1U,
        "width, value kind, drive, event and observation flags count as boundaries");
}

void check_disconnected_certificate_inventory()
{
    constexpr ProcessId component_count = 64U;
    std::vector<Process> processes;
    processes.reserve(component_count);
    const std::vector<RegionSignalDescriptor> signals(
        component_count * 2U, { 1U });
    for (ProcessId id = 0U; id < component_count; ++id) {
        processes.push_back(transfer(id, id * 2U, id * 2U + 1U));
    }

    const auto graph = build(processes, signals);
    const auto& inventory = graph.certificate_inventory();
    require(inventory.components.size() == component_count
            && inventory.access_inventory_complete,
        "disconnected eligible processes retain separate certificate components");
    for (ProcessId id = 0U; id < component_count; ++id) {
        const auto& component = inventory.components[id];
        require(component.members == std::vector<ProcessId> { id }
                && component.structural_internal_signal_candidates
                    == std::vector<SignalId> { id * 2U + 1U }
                && component.boundary_signals
                    == std::vector<SignalId> { id * 2U }
                && component.captured_epochs
                    == std::vector<RegionCertificateEpoch> { { id, 1U } }
                && component.status
                    == RegionComponentCertificateStatus::structural_candidate
                && graph.component_epochs_current(id),
            "disconnected component membership, signal order and epochs are deterministic");
    }
    require(boundary_reason_count(inventory,
                RegionBoundaryReason::no_internal_writer) == component_count,
        "each disconnected boundary input is counted exactly once");
}

void check_blocking_write_graph_admission()
{
    constexpr std::uint32_t width = 65U;
    const std::vector<RegionSignalDescriptor> signals {
        { width }, { width }
    };

    auto producer = transfer(0U, 0U, 1U);
    producer.operations[1U] = WriteBlocking { 1U, 0U };
    auto reader = transfer(1U, 1U, 0U);
    reader.driver_regions.clear();
    reader.operations = {
        ReadSignal { 0U, 1U }, WaitSensitivity { }, Jump { 0U }
    };
    const std::vector<Process> processes { producer, reader };
    const auto bindings = bind_processes(processes);
    const auto graph = RegionGraph::build(bindings, signals);
    const auto& inventory = graph.certificate_inventory();
    require(graph.processes()[0U].pure
            && graph.processes()[0U].update_kind
                == RegionUpdateKind::systemverilog_active
            && graph.signals()[1U].drivers == RegionDriverClass::single_whole
            && !graph.signals()[1U].writers_unknown
            && inventory.components.size() == 1U
            && inventory.components[0U].members
                == std::vector<ProcessId> { 0U, 1U }
            && inventory.components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U },
        "a full-width SV blocking writer with exact whole ownership is certified");
    const auto program = graph.build_compute_program(0U, bindings);
    require(program.has_value()
            && program->internal_materializations.size() == 1U
            && program->internal_materializations[0U].signal == 1U
            && program->internal_materializations[0U].owner == 0U
            && program->internal_materializations[0U].width == width
            && program->internal_materializations[0U].publication_kind
                == RegionOutputPublicationKind::blocking_immediate,
        "the accepted blocking write retains its immediate-publication identity");

    auto generic = producer;
    generic.scheduling_domain = ProcessSchedulingDomain::generic;
    const auto generic_graph = build({ generic }, signals);
    require(!generic_graph.processes()[0U].pure
            && generic_graph.certificate_inventory().components.empty()
            && process_exclusion_count(
                generic_graph.certificate_inventory(),
                RegionProcessExclusionReason::wrong_scheduling_domain) == 1U,
        "a generic-scheduled blocking write remains outside SV graph admission");

    auto sliced = producer;
    sliced.operations[1U] = WriteBlockingSlice { 1U, 0U, 0U };
    const auto sliced_graph = build({ sliced }, signals);
    require(!sliced_graph.processes()[0U].pure
            && sliced_graph.certificate_inventory().components.empty(),
        "a blocking slice does not inherit whole-write admission");

    auto partial_owner = producer;
    partial_owner.driver_regions = { { 1U, 0U, width - 1U, false } };
    const auto partial_graph = build({ partial_owner }, signals);
    require(!partial_graph.processes()[0U].pure
            && partial_graph.signals()[1U].writers_unknown
            && partial_graph.certificate_inventory().components.empty(),
        "a whole blocking write cannot exceed its declared partial driver range");

    auto missing_owner = producer;
    missing_owner.driver_regions.clear();
    const auto missing_graph = build({ missing_owner }, signals);
    require(!missing_graph.processes()[0U].pure
            && missing_graph.signals()[1U].writers_unknown
            && missing_graph.signals()[1U].invalidation_dependencies
                == std::vector<ProcessId> { 0U }
            && missing_graph.certificate_inventory().components.empty(),
        "an undeclared blocking owner remains an unknown writer boundary");
}

void check_certificate_process_exclusions()
{
    auto unknown = transfer(1U, 2U, 3U);
    unknown.operations = {
        ReadSignal { 0U, 2U },
        WriteUpdate { 3U, 0U, SignalUpdateDomain::systemverilog_active },
        ClassMethodCall { },
        ReadContainerObject { },
        WriteContainerObject { },
        WriteContainerObjectElement { },
        ReadStringObject { },
        WriteStringObject { },
    };
    ClassStaticMethodCall foreign_call;
    foreign_call.method_identity = "@dpi:opaque_fixture";
    unknown.operations.push_back(std::move(foreign_call));
    unknown.operations.push_back(WaitSensitivity { });
    unknown.operations.push_back(Jump { 0U });
    const std::vector<RegionSignalDescriptor> signals(4U, { 1U });
    const std::vector<Process> with_unknown {
        transfer(0U, 0U, 1U), unknown
    };
    const auto default_inventory
        = build(with_unknown, signals).certificate_inventory();
    require(!default_inventory.access_inventory_complete
            && default_inventory.components.size() == 1U
            && default_inventory.components[0]
                .structural_internal_signal_candidates.empty()
            && default_inventory.opaque_operation_counts.empty(),
        "unknown completeness remains active when the optional census is off");

    const auto unknown_inventory
        = build(with_unknown, signals, true).certificate_inventory();
    require(!unknown_inventory.access_inventory_complete
            && unknown_inventory.components.size() == 1U
            && unknown_inventory.components[0]
                .structural_internal_signal_candidates.empty()
            && unknown_inventory.components[0].status
                == RegionComponentCertificateStatus::incomplete_access_inventory
            && process_exclusion_count(unknown_inventory,
                RegionProcessExclusionReason::unknown_dependencies) == 1U
            && boundary_reason_count(unknown_inventory,
                RegionBoundaryReason::access_inventory_incomplete) == 2U
            && unknown_inventory.opaque_operation_counts.size() == 7U
            && std::ranges::all_of(
                unknown_inventory.opaque_operation_counts,
                [](const auto& operation) {
                    return operation.incidences == 1U;
                })
            && std::ranges::is_sorted(
                unknown_inventory.opaque_operation_counts,
                {}, &RegionOpaqueOperationCount::type_name),
        "an opaque process blocks hidden-state certificates graph-wide");

    auto edge = transfer(0U, 0U, 1U);
    edge.static_sensitivity = { { 0U, EdgeKind::posedge } };
    const auto edge_inventory = build({ edge }, signals).certificate_inventory();
    require(edge_inventory.components.empty()
            && process_exclusion_count(edge_inventory,
                RegionProcessExclusionReason::edge_sensitivity) == 1U,
        "edge-triggered processes are excluded even if other graph predicates pass");

    auto mixed = transfer(0U, 0U, 1U);
    mixed.scheduling_domain = ProcessSchedulingDomain::generic;
    mixed.operations[1] = WriteUpdate {
        1U, 0U, SignalUpdateDomain::systemverilog_active};
    const auto mixed_inventory = build({ mixed }, signals).certificate_inventory();
    require(mixed_inventory.components.empty()
            && process_exclusion_count(mixed_inventory,
                RegionProcessExclusionReason::wrong_scheduling_domain) == 1U
            && process_exclusion_count(mixed_inventory,
                RegionProcessExclusionReason::wrong_update_kind) == 1U,
        "a generic-scheduling process with an SV Active update is excluded");

    const std::vector<Process> cycle {
        transfer(0U, 1U, 0U), transfer(1U, 0U, 1U)
    };
    const std::vector<RegionSignalDescriptor> cycle_signals(2U, { 1U });
    const auto cycle_inventory = build(cycle, cycle_signals).certificate_inventory();
    require(cycle_inventory.components.empty()
            && process_exclusion_count(cycle_inventory,
                RegionProcessExclusionReason::cyclic_or_dependent_on_cycle) == 2U,
        "cycles remain scheduler boundaries and are counted per excluded process");
}

void check_bound_container_accesses()
{
    std::vector<RegionSignalDescriptor> signals(9U, { 8U });
    signals[3U].width = 16U;
    signals[7U].width = 1U;
    signals[8U].width = 1U;
    const std::vector<RegionContainerDescriptor> containers {
        { 0U, { { 1U, true, true } }, true },
        // A sparse element map is complete: absent ordinals remain in the
        // owning object and cannot introduce an unlisted signal access.
        { 1U, { { 2U, true, true } }, true },
        // Aggregate reads may use either the proxy or physical elements as
        // authority, so retain dependencies on every possible signal route.
        { 2U, {
            { 3U, true, true, true },
            { 4U, true, true }, { 5U, true, true }
        }, true },
        { 3U, { }, true },
    };
    ContainerType one_element;
    one_element.element_width = 8U;
    one_element.fixed = true;
    one_element.index_left = 0;
    one_element.index_right = 0;
    one_element.dimensions = { { 0, 0 } };
    ContainerType two_elements = one_element;
    two_elements.index_left = 1;
    two_elements.index_right = 0;
    two_elements.dimensions = { { 1, 0 } };
    ContainerType local_storage;
    local_storage.element_width = 8U;
    Process reader;
    reader.id = 0U;
    reader.name = "bound_container_reader";
    reader.container_register_count = 4U;
    reader.container_register_types = {
        one_element, two_elements, two_elements, local_storage
    };
    reader.operations = {
        ReadContainerObject { 0U, 0U },
        ReadContainerObject { 1U, 1U },
        ReadContainerObject { 2U, 2U },
        ReadContainerObject { 3U, 3U },
        Halt { },
    };
    Process writer;
    writer.id = 1U;
    writer.name = "bound_container_writer";
    writer.register_count = 3U;
    writer.container_register_count = 1U;
    writer.container_register_types = { one_element };
    writer.driver_regions = {
        { 1U, 0U, 0U, true }, { 2U, 0U, 0U, true },
        { 4U, 0U, 0U, true }, { 5U, 0U, 0U, true },
        { 7U, 0U, 0U, true },
    };
    writer.operations = {
        LoadConstant { 0U, PackedLogic4 { 32U, Logic4::zero } },
        LoadConstant { 2U, PackedLogic4 { 8U, Logic4::zero } },
        ReadContainerObject { 0U, 0U },
        WriteContainerObject { 0U, 0U, std::nullopt },
        WriteContainerObjectElement {
            1U, 0U, 2U, true, false, false, 7U, std::nullopt
        },
        WriteContainerObjectElement {
            2U, 0U, 2U, true, false, false, std::nullopt, std::nullopt
        },
        Halt { },
    };
    const std::vector<Process> processes { reader, writer };
    std::vector<const Process*> bindings;
    for (const auto& process : processes) {
        bindings.push_back(&process);
    }
    const auto graph = RegionGraph::build(bindings, signals, false, containers);
    require(graph.certificate_inventory().access_inventory_complete
            && !graph.processes()[0].dependencies_unknown
            && !graph.processes()[1].dependencies_unknown,
        "complete direct object bindings must close signal access inventory");
    for (const auto signal : { 1U, 2U, 3U, 4U, 5U }) {
        require(std::ranges::any_of(graph.signals()[signal].readers,
                    [](const auto& access) { return access.process == 0U; })
                && std::ranges::any_of(graph.signals()[signal].writers,
                    [](const auto& access) { return access.process == 1U; }),
            "mapped container reads and writes must retain signal dependencies");
    }
    require(std::ranges::any_of(graph.signals()[7U].writers,
                [](const auto& access) { return access.process == 1U; })
            && graph.signals()[6U].readers.empty()
            && graph.signals()[8U].readers.empty(),
        "container transaction signals are explicit writes and local storage adds none");
    require(graph.signals()[3U].writers_unknown
            && graph.signals()[3U].drivers == RegionDriverClass::unknown
            && std::ranges::find(
                graph.signals()[3U].invalidation_dependencies, 1U)
                != graph.signals()[3U].invalidation_dependencies.end()
            && !graph.signals()[4U].writers_unknown
            && graph.signals()[4U].drivers == RegionDriverClass::single_whole
            && !graph.signals()[5U].writers_unknown,
        "derived aggregate writers stay unknown while complete leaf ownership remains exact");

    Process partial_writer;
    partial_writer.id = 0U;
    partial_writer.name = "partially_owned_container_writer";
    partial_writer.register_count = 2U;
    // The object stores two packed elements in signal 2. This process owns
    // only the lane selected by its initialized zero index. The inventory
    // deliberately does not constant-fold object indexes, so it must retain
    // a full-signal access and decline whole-signal ownership certification.
    partial_writer.driver_regions = { { 2U, 8U, 8U, false } };
    partial_writer.operations = {
        LoadConstant { 0U, PackedLogic4 { 32U, Logic4::zero } },
        LoadConstant { 1U, PackedLogic4 { 8U, Logic4::zero } },
        WriteContainerObjectElement {
            1U, 0U, 1U, true, false, false, std::nullopt, std::nullopt
        },
        Halt { },
    };
    const std::vector<Process> partial_processes { partial_writer };
    auto partial_signals = signals;
    partial_signals[2U].width = 16U;
    const std::vector<RegionContainerDescriptor> partial_containers {
        { 1U, { { 2U, true, true } }, true },
    };
    bindings.clear();
    bindings.push_back(&partial_processes[0]);
    const auto partial = RegionGraph::build(
        bindings, partial_signals, false, partial_containers);
    require(partial.certificate_inventory().access_inventory_complete
            && !partial.processes()[0].dependencies_unknown
            && partial.signals()[2U].writers_unknown
            && std::ranges::any_of(partial.signals()[2U].writers,
                [](const auto& access) {
                    return access.process == 0U && access.offset == 0U
                        && access.width == 0U;
                }),
        "dynamic container indexes require complete signal ownership and full-range edges");

    const std::vector<RegionContainerDescriptor> incomplete_containers {
        { 4U, { { 6U, true, true } }, false },
    };
    Process incomplete_reader;
    incomplete_reader.id = 0U;
    incomplete_reader.name = "incomplete_container_reader";
    incomplete_reader.container_register_count = 1U;
    incomplete_reader.container_register_types = { ContainerType { } };
    incomplete_reader.operations = {
        ReadContainerObject { 0U, 4U },
        Halt { },
    };
    Process missing_writer;
    missing_writer.id = 1U;
    missing_writer.name = "missing_container_writer";
    missing_writer.register_count = 2U;
    missing_writer.operations = {
        LoadConstant { 0U, PackedLogic4 { 32U, Logic4::zero } },
        LoadConstant { 1U, PackedLogic4 { 8U, Logic4::zero } },
        WriteContainerObjectElement {
            5U, 0U, 1U, true, false, false, 8U, std::nullopt
        },
        Halt { },
    };
    const std::vector<Process> incomplete_processes {
        incomplete_reader, missing_writer
    };
    bindings.clear();
    for (const auto& process : incomplete_processes) {
        bindings.push_back(&process);
    }
    const auto incomplete = RegionGraph::build(
        bindings, signals, false, incomplete_containers);
    require(!incomplete.certificate_inventory().access_inventory_complete
            && incomplete.processes()[0].dependencies_unknown
            && incomplete.processes()[1].dependencies_unknown
            && std::ranges::any_of(incomplete.signals()[6U].readers,
                [](const auto& access) { return access.process == 0U; })
            && incomplete.signals()[8U].readers.empty(),
        "incomplete or missing object bindings stay fail-closed while known edges survive");

    const std::vector<const Process*> no_processes;
    const std::vector<RegionContainerDescriptor> duplicate_containers {
        { 0U, { }, true }, { 0U, { }, true },
    };
    const std::vector<RegionContainerDescriptor> invalid_signal_containers {
        { 0U, { { 9U, true, false } }, true },
    };
    const auto rejects_container_map = [&](
        const std::vector<RegionContainerDescriptor>& map) {
        try {
            (void)RegionGraph::build(no_processes, signals, false, map);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    require(rejects_container_map(duplicate_containers)
            && rejects_container_map(invalid_signal_containers),
        "duplicate container IDs and out-of-range bindings are rejected");
}

void check_aggregate_alias_projection_and_full_leaf_write()
{
    const std::vector<RegionSignalDescriptor> signals {
        { 4U, ResolutionKind::sv_wire },
        { 2U, ResolutionKind::sv_wire },
        { 2U, ResolutionKind::sv_wire },
        { 1U },
        { 4U },
    };
    const std::vector<RegionContainerDescriptor> containers {
        { 0U, {
            { 0U, true, true, true },
            { 1U, true, true, false },
            { 2U, true, true, false },
        }, true },
    };
    const std::vector<RegionSignalAliasFamilyDescriptor> families {
        { 0U, 0U, 4U,
            { { 1U, 0U, 2U, 2U }, { 2U, 1U, 0U, 2U } },
            true, true, true },
    };

    Process slice_writer;
    slice_writer.id = 0U;
    slice_writer.name = "alias_full_leaf_slice_writer";
    slice_writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    slice_writer.register_count = 1U;
    slice_writer.static_sensitivity = { { 3U, EdgeKind::any } };
    slice_writer.driver_regions = { { 0U, 0U, 2U, false } };
    slice_writer.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("10") },
        WriteUpdateSlice { 0U, 0U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process leaf_reader;
    leaf_reader.id = 1U;
    leaf_reader.name = "alias_leaf_reader";
    leaf_reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    leaf_reader.register_count = 1U;
    leaf_reader.static_sensitivity = { { 2U, EdgeKind::any } };
    leaf_reader.driver_regions = { { 1U, 0U, 0U, true } };
    leaf_reader.operations = {
        ReadSignal { 0U, 2U },
        WriteUpdate { 1U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process proxy_reader;
    proxy_reader.id = 2U;
    proxy_reader.name = "alias_proxy_reader";
    proxy_reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    proxy_reader.register_count = 1U;
    proxy_reader.static_sensitivity = { { 0U, EdgeKind::any } };
    proxy_reader.driver_regions = { { 4U, 0U, 0U, true } };
    proxy_reader.operations = {
        ReadSignal { 0U, 0U },
        WriteUpdate { 4U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    const std::vector<Process> processes {
        slice_writer, leaf_reader, proxy_reader,
    };
    const auto bindings = bind_processes(processes);
    auto graph = RegionGraph::build(
        bindings, signals, false, containers, { }, families);

    std::vector<RegionSignalAliasRange> projected;
    require(families[0U].project_range(0U, 2U, projected)
            && projected == std::vector<RegionSignalAliasRange> {
                { 2U, 0U, 2U } },
        "a low-order proxy range maps to its exact declared leaf");
    require(families[0U].project_range(1U, 2U, projected)
            && projected.size() == 2U
            && std::ranges::any_of(projected,
                [](const RegionSignalAliasRange& range) {
                    return range.signal == 1U && range.offset == 0U
                        && range.width == 1U;
                })
            && std::ranges::any_of(projected,
                [](const RegionSignalAliasRange& range) {
                    return range.signal == 2U && range.offset == 1U
                        && range.width == 1U;
                }),
        "a cross-leaf proxy range retains each leaf-local interval");
    require(!families[0U].project_range(3U, 2U, projected),
        "an out-of-bounds proxy range cannot be projected");

    const auto& components = graph.certificate_inventory().components;
    // The proxy reader's output (signal 4) has no external observer, so it
    // remains an internal candidate alongside the two physical leaves.
    require(components.size() == 1U
            && components[0U].members
                == std::vector<ProcessId> { 0U, 1U, 2U }
            && components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 1U, 2U, 4U }
            && std::ranges::any_of(graph.signals()[0U].readers,
                [](const RegionAccess& access) {
                    return access.process == 2U;
                })
            && std::ranges::any_of(graph.signals()[1U].readers,
                [](const RegionAccess& access) {
                    return access.process == 2U;
                })
            && std::ranges::any_of(graph.signals()[2U].readers,
                [](const RegionAccess& access) {
                    return access.process == 2U;
                }),
        "proxy reads stay boundary reads and depend on every physical leaf");

    const auto compute = graph.build_compute_program(0U, bindings);
    require(!compute.has_value(),
        "a proxy slice writer and proxy reader stay on the checked fallback");

    const std::vector<RegionSignalDescriptor> direct_signals {
        { 4U, ResolutionKind::sv_wire },
        { 2U, ResolutionKind::sv_wire },
        { 2U, ResolutionKind::sv_wire },
        { 1U, ResolutionKind::none },
        { 1U, ResolutionKind::none },
    };
    const std::vector<RegionContainerDescriptor> direct_containers {
        { 0U, {
            { 0U, true, true, true },
            { 1U, true, true, false },
            { 2U, true, true, false },
        }, true },
    };
    const std::vector<RegionSignalAliasFamilyDescriptor> direct_families {
        { 0U, 0U, 4U,
            { { 1U, 0U, 2U, 2U }, { 2U, 1U, 0U, 2U } },
            true, true, true },
    };

    Process direct_leaf_writer;
    direct_leaf_writer.id = 0U;
    direct_leaf_writer.name = "direct_alias_leaf_writer";
    direct_leaf_writer.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    direct_leaf_writer.register_count = 3U;
    direct_leaf_writer.static_sensitivity = { { 4U, EdgeKind::any } };
    direct_leaf_writer.driver_regions = {
        { 2U, 0U, 0U, true }, { 3U, 0U, 0U, true },
    };
    direct_leaf_writer.operations = {
        ReadSignal { 2U, 4U },
        LoadConstant { 0U, PackedLogic4::from_msb_string("10") },
        WriteUpdate { 2U, 0U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 1U, PackedLogic4 { 1U, Logic4::one } },
        WriteUpdate { 3U, 1U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process direct_leaf_reader;
    direct_leaf_reader.id = 1U;
    direct_leaf_reader.name = "direct_alias_leaf_reader";
    direct_leaf_reader.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    direct_leaf_reader.register_count = 2U;
    direct_leaf_reader.static_sensitivity = {
        { 2U, EdgeKind::any }, { 3U, EdgeKind::any },
    };
    direct_leaf_reader.operations = {
        ReadSignal { 0U, 2U },
        ReadSignal { 1U, 3U },
        WaitSensitivity { },
        Jump { 0U },
    };
    const std::vector<Process> direct_processes {
        direct_leaf_writer, direct_leaf_reader,
    };
    const auto direct_bindings = bind_processes(direct_processes);
    auto direct_graph = RegionGraph::build(direct_bindings, direct_signals,
        false, direct_containers, { }, direct_families);
    const auto& direct_components
        = direct_graph.certificate_inventory().components;
    require(direct_components.size() == 1U
            && direct_components[0U].members
                == std::vector<ProcessId> { 0U, 1U }
            && direct_components[0U].structural_internal_signal_candidates
                == std::vector<SignalId> { 2U, 3U },
        "the direct leaf and ordinary internal form one certified component");

    const auto direct_compute
        = direct_graph.build_compute_program(0U, direct_bindings);
    require(direct_compute.has_value()
            && direct_compute->members
                == std::vector<ProcessId> { 0U, 1U }
            && direct_compute->activation_kernel.internal_signals
                == std::vector<SignalId> { 3U },
        "the alias leaf moves to the boundary while ordinary signal 3 stays private");
    const auto direct_leaf_output = std::ranges::find_if(
        direct_compute->boundary_outputs,
        [](const RegionConeOutputBinding& output) {
            return output.owner == 0U && output.signal == 2U;
        });
    const auto ordinary_internal_output = std::ranges::find_if(
        direct_compute->internal_materializations,
        [](const RegionConeOutputBinding& output) {
            return output.owner == 0U && output.signal == 3U;
        });
    const auto original_leaf_write
        = direct_processes[0U].operations.expanded(2U);
    const auto* const direct_write
        = operation_get_if<WriteUpdate>(&original_leaf_write);
    require(direct_leaf_output != direct_compute->boundary_outputs.end()
            && direct_leaf_output->offset == 0U
            && direct_leaf_output->width == 2U
            && direct_leaf_output->domain
                == SignalUpdateDomain::systemverilog_active
            && direct_leaf_output->source_instruction == 2U
            && direct_write != nullptr && direct_write->signal == 2U
            && ordinary_internal_output
                != direct_compute->internal_materializations.end()
            && ordinary_internal_output->source_instruction == 4U
            && ordinary_internal_output->width == 1U,
        "the original whole leaf writer remains a boundary output beside ordinary materialization");
    require(std::ranges::any_of(direct_compute->activation_kernel.inputs,
                [](const RegionConeKernelInput& input) {
                    return input.signal == 2U && !input.internal;
                })
            && std::ranges::any_of(direct_compute->activation_kernel.inputs,
                [](const RegionConeKernelInput& input) {
                    return input.signal == 3U && input.internal;
                })
            && direct_compute->forwarding_kernel.has_value()
            && direct_compute->forwarding_kernel->internal_signals
                == std::vector<SignalId> { 3U }
            && direct_compute->forwarding_kernel->dependencies.size() == 1U
            && std::ranges::all_of(
                direct_compute->forwarding_kernel->dependencies,
                [](const RegionConeForwardingDependency& dependency) {
                    return dependency.signal == 3U;
                })
            && direct_compute->forwarding_kernel->internal_reads.size() == 1U
            && std::ranges::all_of(
                direct_compute->forwarding_kernel->internal_reads,
                [](const RegionConeForwardingRead& read) {
                    return read.signal == 3U;
                }),
        "the leaf is a boundary input, outside internal forwarding dependencies");

    auto proxy_slice_processes = direct_processes;
    proxy_slice_processes[0U].driver_regions[0U]
        = { 0U, 2U, 2U, false };
    proxy_slice_processes[0U].operations[2U] = WriteUpdateSlice {
        0U, 0U, 2U, SignalUpdateDomain::systemverilog_active };
    proxy_slice_processes[1U].static_sensitivity[0U].signal = 1U;
    proxy_slice_processes[1U].operations[0U] = ReadSignal { 0U, 1U };
    const auto proxy_slice_bindings = bind_processes(proxy_slice_processes);
    const auto proxy_slice_graph = RegionGraph::build(
        proxy_slice_bindings, direct_signals, false, direct_containers, { },
        direct_families);
    const auto proxy_slice_compute = proxy_slice_graph.build_compute_program(
        0U, proxy_slice_bindings);
    const auto proxy_slice_output
        = proxy_slice_compute
            ? std::ranges::find_if(proxy_slice_compute->boundary_outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.owner == 0U && output.signal == 1U;
                })
            : std::vector<RegionConeOutputBinding>::const_iterator { };
    const auto proxy_slice_operation
        = proxy_slice_processes[0U].operations.expanded(2U);
    const auto* const original_proxy_slice
        = operation_get_if<WriteUpdateSlice>(&proxy_slice_operation);
    require(proxy_slice_graph.certificate_inventory().components.size() == 1U
            && proxy_slice_graph.certificate_inventory().components[0U].members
                == std::vector<ProcessId> { 0U, 1U }
            && proxy_slice_graph.certificate_inventory().components[0U]
                .structural_internal_signal_candidates
                == std::vector<SignalId> { 1U, 3U }
            && proxy_slice_compute.has_value()
            && proxy_slice_compute->members
                == std::vector<ProcessId> { 0U, 1U }
            && proxy_slice_compute->activation_kernel.internal_signals
                == std::vector<SignalId> { 3U }
            && proxy_slice_output
                != proxy_slice_compute->boundary_outputs.end()
            && proxy_slice_output->offset == 0U
            && proxy_slice_output->width == 2U
            && proxy_slice_output->domain
                == SignalUpdateDomain::systemverilog_active
            && proxy_slice_output->source_instruction == 2U
            && original_proxy_slice != nullptr
            && original_proxy_slice->signal == 0U
            && original_proxy_slice->source == 0U
            && original_proxy_slice->offset == 2U
            && original_proxy_slice->domain
                == SignalUpdateDomain::systemverilog_active
            && proxy_slice_processes[0U].driver_regions[0U].signal == 0U
            && proxy_slice_processes[0U].driver_regions[0U].offset == 2U
            && proxy_slice_processes[0U].driver_regions[0U].width == 2U
            && !proxy_slice_processes[0U].driver_regions[0U].whole,
        "proxy offset 2 preserves the source slice while mapping to leaf 1 at local offset zero");

    auto proxy_read_processes = processes;
    proxy_read_processes[2U].static_sensitivity = {
        { 2U, EdgeKind::any },
    };
    const auto proxy_read_bindings = bind_processes(proxy_read_processes);
    const auto proxy_read_graph = RegionGraph::build(
        proxy_read_bindings, signals, false, containers, { }, families);
    bool proxy_read_admitted { };
    for (std::size_t index = 0U;
         index < proxy_read_graph.certificate_inventory().components.size();
         ++index) {
        const auto candidate = proxy_read_graph.build_compute_program(
            index, proxy_read_bindings);
        if (candidate
            && std::ranges::find(candidate->members, ProcessId { 2U })
                != candidate->members.end()) {
            proxy_read_admitted = true;
        }
    }
    require(!proxy_read_admitted
            && std::ranges::any_of(proxy_read_graph.processes()[2U].reads,
                [](const Sensitivity& read) { return read.signal == 0U; }),
        "a proxy body read stays checked when sensitivity names only a physical leaf");

    auto wrong_direct_owner = direct_processes;
    wrong_direct_owner[0U].driver_regions[0U]
        = { 1U, 0U, 0U, true };
    const auto wrong_direct_owner_bindings
        = bind_processes(wrong_direct_owner);
    require(!direct_graph.build_compute_program(0U,
                wrong_direct_owner_bindings),
        "a changed owner range cannot authenticate the physical leaf publication");

    auto wrong_direct_width = direct_processes;
    wrong_direct_width[0U].operations[1U] = LoadConstant {
        0U, PackedLogic4::from_msb_string("1") };
    const auto wrong_direct_width_bindings
        = bind_processes(wrong_direct_width);
    require(!direct_graph.build_compute_program(0U,
                wrong_direct_width_bindings),
        "a narrower source value cannot become a whole physical leaf output");

    const auto direct_epochs = std::array<std::uint64_t, 2U> {
        direct_graph.capability_epoch(0U),
        direct_graph.capability_epoch(1U),
    };
    const auto invalidated_direct = direct_graph.observe_signal(
        2U, RegionObservation::current);
    require(std::ranges::equal(invalidated_direct,
                std::array<ProcessId, 2U> { 0U, 1U })
            && direct_graph.capability_epoch(0U) > direct_epochs[0U]
            && direct_graph.capability_epoch(1U) > direct_epochs[1U]
            && !direct_graph.component_epochs_current(0U)
            && !captured_epochs_are_current(direct_graph, 0U)
            && direct_graph.signals()[2U].observations
                == RegionObservation::current
            && !direct_graph.build_compute_program(0U, direct_bindings),
        "observing the physical leaf demotes its direct boundary and ordinary internal component");

    auto crossing_processes = direct_processes;
    crossing_processes[0U].driver_regions[0U]
        = { 0U, 1U, 2U, false };
    crossing_processes[0U].operations[2U] = WriteUpdateSlice {
        0U, 0U, 1U, SignalUpdateDomain::systemverilog_active };
    const auto crossing_bindings = bind_processes(crossing_processes);
    const auto crossing_graph = RegionGraph::build(
        crossing_bindings, direct_signals, false, direct_containers, { },
        direct_families);
    const auto crossing_operation
        = crossing_processes[0U].operations.expanded(2U);
    const auto* const cross_leaf_slice
        = operation_get_if<WriteUpdateSlice>(&crossing_operation);
    bool crossing_admitted { };
    for (std::size_t index = 0U;
         index < crossing_graph.certificate_inventory().components.size();
         ++index) {
        const auto candidate = crossing_graph.build_compute_program(
            index, crossing_bindings);
        if (candidate
            && std::ranges::find(candidate->members, ProcessId { 0U })
                != candidate->members.end()) {
            crossing_admitted = true;
        }
    }
    require(!crossing_admitted
            && crossing_graph.processes().size() == crossing_processes.size()
            && crossing_graph.processes()[0U].writes
                == crossing_processes[0U].driver_regions
            && cross_leaf_slice != nullptr
            && cross_leaf_slice->signal == 0U
            && cross_leaf_slice->offset == 1U,
        "a cross-leaf owner range remains on the checked fallback");

    auto partial_processes = direct_processes;
    partial_processes[0U].driver_regions[0U] = { 0U, 0U, 1U, false };
    partial_processes[0U].operations[1U] = LoadConstant {
        0U, PackedLogic4::from_msb_string("1") };
    partial_processes[0U].operations[2U] = WriteUpdateSlice {
        0U, 0U, 0U, SignalUpdateDomain::systemverilog_active };
    const auto partial_bindings = bind_processes(partial_processes);
    const auto partial_graph = RegionGraph::build(
        partial_bindings, direct_signals, false, direct_containers, { },
        direct_families);
    const auto partial_operation
        = partial_processes[0U].operations.expanded(2U);
    const auto* const partial_proxy_slice
        = operation_get_if<WriteUpdateSlice>(&partial_operation);
    bool partial_admitted { };
    for (std::size_t index = 0U;
         index < partial_graph.certificate_inventory().components.size();
         ++index) {
        const auto candidate = partial_graph.build_compute_program(
            index, partial_bindings);
        if (candidate
            && std::ranges::find(candidate->members, ProcessId { 0U })
                != candidate->members.end()) {
            partial_admitted = true;
        }
    }
    require(!partial_admitted
            && partial_graph.processes().size() == partial_processes.size()
            && partial_graph.processes()[0U].writes
                == partial_processes[0U].driver_regions
            && partial_proxy_slice != nullptr
            && partial_proxy_slice->signal == 0U
            && partial_proxy_slice->offset == 0U
            && partial_processes[0U].driver_regions[0U].width == 1U,
        "a partial proxy slice cannot be promoted to a complete physical-leaf boundary");

    auto whole_proxy_processes = processes;
    whole_proxy_processes[0U].driver_regions[0U] = { 0U, 0U, 0U, true };
    whole_proxy_processes[0U].operations[1U] = WriteUpdate {
        0U, 0U, SignalUpdateDomain::systemverilog_active };

    Process unrelated_writer;
    unrelated_writer.id = 3U;
    unrelated_writer.name = "unrelated_internal_writer";
    unrelated_writer.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    unrelated_writer.register_count = 2U;
    unrelated_writer.static_sensitivity = { { 5U, EdgeKind::any } };
    unrelated_writer.driver_regions = { { 6U, 0U, 0U, true } };
    unrelated_writer.operations = {
        ReadSignal { 1U, 5U },
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::one } },
        WriteUpdate { 6U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    Process unrelated_reader;
    unrelated_reader.id = 4U;
    unrelated_reader.name = "unrelated_internal_reader";
    unrelated_reader.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    unrelated_reader.register_count = 1U;
    unrelated_reader.static_sensitivity = { { 6U, EdgeKind::any } };
    unrelated_reader.operations = {
        ReadSignal { 0U, 6U },
        WaitSensitivity { },
        Jump { 0U },
    };
    whole_proxy_processes.push_back(unrelated_writer);
    whole_proxy_processes.push_back(unrelated_reader);
    auto whole_proxy_signals = signals;
    whole_proxy_signals.push_back({ 1U, ResolutionKind::none });
    whole_proxy_signals.push_back({ 1U, ResolutionKind::none });
    const auto whole_proxy_bindings = bind_processes(whole_proxy_processes);
    const auto whole_proxy_graph = RegionGraph::build(
        whole_proxy_bindings, whole_proxy_signals, false, containers, { },
        families);
    bool whole_proxy_candidate_found { };
    bool sibling_kernel_found { };
    bool unrelated_kernel_found { };
    for (std::size_t index = 0U;
         index < whole_proxy_graph.certificate_inventory().components.size();
         ++index) {
        const auto candidate = whole_proxy_graph.build_compute_program(
            index, whole_proxy_bindings);
        if (!candidate) {
            continue;
        }
        whole_proxy_candidate_found
            = whole_proxy_candidate_found
            || std::ranges::find(candidate->members, 0U)
                != candidate->members.end();
        if (candidate->members == std::vector<ProcessId> { 1U, 2U }) {
            sibling_kernel_found
                = candidate->activation_kernel.internal_signals
                    == std::vector<SignalId> { 4U };
        }
        if (candidate->members
                == std::vector<ProcessId> { 3U, 4U }) {
            unrelated_kernel_found
                = candidate->activation_kernel.internal_signals
                    == std::vector<SignalId> { 6U };
        }
    }
    require(!whole_proxy_candidate_found && sibling_kernel_found
            && unrelated_kernel_found,
        "the whole-proxy writer stays checked while both unaffected kernels remain available");

    const auto epochs = std::vector<std::uint64_t> {
        graph.capability_epoch(0U), graph.capability_epoch(1U),
        graph.capability_epoch(2U),
    };
    const auto invalidated = graph.observe_signal(
        0U, RegionObservation::current);
    require(std::ranges::equal(invalidated,
                std::array<ProcessId, 3U> { 0U, 1U, 2U })
            && graph.capability_epoch(0U) > epochs[0U]
            && graph.capability_epoch(1U) > epochs[1U]
            && graph.capability_epoch(2U) > epochs[2U]
            && !graph.component_epochs_current(0U)
            && !captured_epochs_are_current(graph, 0U)
            && graph.signals()[0U].observations
                == RegionObservation::current
            && graph.signals()[1U].observations
                == RegionObservation::current
            && graph.signals()[2U].observations
                == RegionObservation::current
            && !graph.build_compute_program(0U, bindings),
        "observing the proxy invalidates the complete reader/writer family");

    auto incomplete_family = families;
    incomplete_family[0U].leaves[1U].offset = 1U;
    bool rejected_incomplete { };
    try {
        static_cast<void>(RegionGraph::build(bindings, signals, false,
            containers, { }, incomplete_family));
    } catch (const std::invalid_argument&) {
        rejected_incomplete = true;
    }
    require(rejected_incomplete,
        "overlapping or gapped alias ranges fail graph construction");

    const auto rejects_alias_family = [&](
        const RegionSignalAliasFamilyDescriptor& family) {
        const std::array<RegionSignalAliasFamilyDescriptor, 1U> invalid {
            family
        };
        try {
            static_cast<void>(RegionGraph::build(
                bindings, signals, false, containers, { }, invalid));
        } catch (const std::invalid_argument&) {
            return true;
        } catch (const std::out_of_range&) {
            return true;
        }
        return false;
    };

    auto out_of_range_proxy = families[0U];
    out_of_range_proxy.proxy = static_cast<SignalId>(signals.size());
    auto out_of_range_leaf = families[0U];
    out_of_range_leaf.leaves[0U].signal
        = static_cast<SignalId>(signals.size());
    auto duplicate_leaf = families[0U];
    duplicate_leaf.leaves[1U].signal = duplicate_leaf.leaves[0U].signal;
    auto partial_family = families[0U];
    partial_family.complete = false;
    require(rejects_alias_family(out_of_range_proxy)
            && rejects_alias_family(out_of_range_leaf)
            && rejects_alias_family(duplicate_leaf)
            && rejects_alias_family(partial_family),
        "invalid proxy/leaf IDs, duplicate leaves, and partial descriptors fail before family indexing");
}

} // namespace

void test_systemverilog_wave();

void test_region_graph()
{
    check_scheduler_batch_frontier();
    check_read_only_systemverilog_member();
    test_systemverilog_wave();
    check_chain_and_observation();
    check_selective_observation_epochs();
    check_component_epoch_fast_state();
    check_systemverilog_partial_boundary_output_slices();
    check_driver_classes();
    check_cycles_and_domains();
    check_projected_operation_inventory();
    check_sequential_and_language_boundaries();
    check_invalid_ranges_and_effects();
    check_empty_array_boundary();
    check_fork_ownership();
    check_known_nonpure_accesses();
    check_process_local_register_effects();
    check_output_callbacks_keep_access_inventory_known();
    check_fork_access_inventory_is_signal_local();
    check_invalid_fork_targets_remain_opaque();
    check_fixed_signal_effects();
    check_closed_component_certificates();
    check_certificate_boundaries();
    check_read_only_sibling_certificate_partition();
    check_range_aware_writer_components();
    check_certificate_signal_exclusions();
    check_blocking_write_graph_admission();
    check_certificate_process_exclusions();
    check_bound_container_accesses();
    check_aggregate_alias_projection_and_full_leaf_write();
    check_disconnected_certificate_inventory();
    check_compute_cone_program();
    check_compute_cone_debug_markers();
    check_compute_cone_sensitivity_ranges();
    check_compute_cone_multi_parent_join();
    check_compute_cone_wide_multi_parent_join();
    check_compute_cone_rejections();
    check_vhdl_projected_activation_program();
    check_coverage_operations_remain_unknown();
}

} // namespace fsim::tests::runtime
