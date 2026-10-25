// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_graph.hpp"

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
    const std::vector<RegionSignalDescriptor>& signals)
{
    std::vector<const Process*> bindings;
    for (const auto& process : processes) {
        bindings.push_back(&process);
    }
    return RegionGraph::build(bindings, signals);
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
                           const bool expects_partial) {
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
    };

    check(WriteProjected { 2U, 0U, 0U, 0U,
              ProjectedDelayMode::inertial }, false);
    check(WriteProjectedWaveform { 2U, { { 0U, 0U } }, 0U,
              ProjectedDelayMode::inertial }, false);
    check(WriteProjectedSlice { 2U, 0U, 0U, 0U, 0U,
              ProjectedDelayMode::inertial }, true);
    check(WriteProjectedWaveformSlice { 2U, { { 0U, 0U } }, 0U, 0U,
              ProjectedDelayMode::inertial }, true);
    check(WriteProjectedDynamicSlice { 2U, 0U, { 0U, 0, 63, 0U, true },
              0U, 0U, ProjectedDelayMode::inertial }, true);
    check(WriteProjectedWaveformDynamicSlice { 2U, { { 0U, 0U } },
              { 0U, 0, 63, 0U, true }, 0U,
              ProjectedDelayMode::inertial }, true);
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
    require(!sequential.processes()[2].pure
            && !sequential.processes()[0].cyclic_or_dependent_on_cycle
            && !sequential.processes()[1].cyclic_or_dependent_on_cycle,
        "sequential dependencies must be cut only in pure-region cycle analysis");
    require(sequential.signals()[0].writers.front().process == 2U
            && sequential.signals()[2].readers.front().process == 2U,
        "boundary ownership and reads must remain available");

    // A feedback path crossing SV Active and VHDL projected transactions is
    // two domain-local DAGs. Each must still yield at its own cycle boundary.
    processes = { transfer(0U, 1U, 0U), transfer(1U, 0U, 1U) };
    processes[1].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[1].operations[1] = WriteProjected { 1U, 0U };
    const auto mixed = build(processes, signals);
    require(mixed.processes()[0].pure && mixed.processes()[1].pure
            && !mixed.processes()[0].cyclic_or_dependent_on_cycle
            && !mixed.processes()[1].cyclic_or_dependent_on_cycle,
        "mixed-language boundaries must not become same-wave cycle edges");

    // Publication contracts also form a boundary when both process domains
    // use the generic scheduler (for example a VHDL/SystemC crossing).
    processes[0].scheduling_domain = ProcessSchedulingDomain::generic;
    processes[0].operations[1] = WriteUpdate { 0U, 0U };
    require(!build(processes, signals).processes()[0]
                .cyclic_or_dependent_on_cycle,
        "distinct publication contracts must remain separate in a shared scheduler");
    processes[0].operations[1] = WriteProjected { 0U, 0U };
    const auto projected_loop = build(processes, signals);
    require(projected_loop.processes()[0].cyclic_or_dependent_on_cycle
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
            && graph.access_inventory_complete(),
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
            && missing.signals()[1].drivers == RegionDriverClass::unknown,
        "undeclared writers must be classified as unknown ownership");
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
    require(!graph.processes()[0].pure && !graph.processes()[1].pure
            && !graph.processes()[0].dependencies_unknown
            && !graph.processes()[1].dependencies_unknown
            && graph.access_inventory_complete()
            && graph.processes()[2].pure,
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
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && graph.access_inventory_complete()
            && graph.processes()[1].pure,
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
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && graph.access_inventory_complete()
            && graph.processes()[1].pure
            && has_reader(graph.signals()[0U], 0U)
            && has_reader(graph.signals()[2U], 0U)
            && has_writer(graph.signals()[1U], 0U)
            && graph.signals()[5].readers.empty()
            && graph.signals()[5].writers.empty(),
        "callee signal accesses stay inventoried across local string/container "
        "register work, while unrelated pure work stays pure");
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
        { output, transfer(1U, 4U, 5U) }, signals);
    require(!graph.processes()[0].pure
            && !graph.processes()[0].dependencies_unknown
            && graph.access_inventory_complete()
            && graph.processes()[1].pure
            && has_reader(graph.signals()[0U], 0U)
            && has_writer(graph.signals()[1U], 0U),
        "output and assertion effects stay non-pure while fixed signal access "
        "is inventoried and unrelated pure work stays pure");
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
    const auto graph = build(processes, signals);
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
            && graph.access_inventory_complete()
            && graph.processes()[1].pure && graph.processes()[2].pure,
        "fork control keeps fixed accesses known and only its written signals "
        "have dynamic ownership; unrelated pure state stays pure");
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
            && !graph.access_inventory_complete();
    };
    const auto valid_empty_fork
        = graph_for({ Fork { { }, ForkJoinKind::none }, Halt { } });
    require(!valid_empty_fork.processes()[0U].pure
            && !valid_empty_fork.processes()[0U].dependencies_unknown
            && valid_empty_fork.access_inventory_complete(),
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
    require(graph.access_inventory_complete()
            && !graph.processes()[0].dependencies_unknown,
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
            1U, 0U, 2U, true, false, false, 7U, std::nullopt, { }, { }
        },
        WriteContainerObjectElement {
            2U, 0U, 2U, true, false, false, std::nullopt, std::nullopt, { }, { }
        },
        Halt { },
    };
    const std::vector<Process> processes { reader, writer };
    std::vector<const Process*> bindings;
    for (const auto& process : processes) {
        bindings.push_back(&process);
    }
    const auto graph = RegionGraph::build(bindings, signals, containers);
    require(graph.access_inventory_complete()
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
            1U, 0U, 1U, true, false, false, std::nullopt, std::nullopt, { }, { }
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
        bindings, partial_signals, partial_containers);
    require(partial.access_inventory_complete()
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
            5U, 0U, 1U, true, false, false, 8U, std::nullopt, { }, { }
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
        bindings, signals, incomplete_containers);
    require(!incomplete.access_inventory_complete()
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
            (void)RegionGraph::build(no_processes, signals, map);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    require(rejects_container_map(duplicate_containers)
            && rejects_container_map(invalid_signal_containers),
        "duplicate container IDs and out-of-range bindings are rejected");
}

} // namespace

void test_region_graph()
{
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
    check_bound_container_accesses();
}

} // namespace fsim::tests::runtime
