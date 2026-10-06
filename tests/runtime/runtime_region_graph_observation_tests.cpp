// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "runtime_fused_staging_failure_support.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_storage_census_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct CallbackCacheSignalSnapshot {
    SimulationTick time { };
    std::uint64_t delta { };
    Logic4Word current;
    Logic4Word last;
    Logic4Word stored;
    Logic4Word owner;
    Logic4Word raw_current;
    Logic4Word raw_last;
    bool owner_present { };
    bool direct_materialization_pending { };
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp event_stamp;
    std::uint64_t value_revision { };
    std::uint64_t runtime_generation { };
    std::uint64_t capability_epoch { };
};

struct PendingDirectMirrorProbe {
    Interpreter* interpreter { };
    SignalId signal { };
    std::array<CallbackCacheSignalSnapshot, 2U> publications { };
    std::size_t publication_count { };
};

struct NativeRegionAllocationTestAccess final {
    struct DynamicWaitSnapshot {
        bool sidecar_present { };
        bool waiting_on_signal { };
        bool waiting_on_static { };
        bool queued { };
        ProcessStatus status { ProcessStatus::running };
        InstructionIndex pc { };
        bool timeout_origin_present { };
        bool timeout_deadline_present { };
        std::size_t active_registrations { };
    };

    struct DynamicWaitAllocationProbe {
        Interpreter* interpreter { };
        ProcessId process { };
        SignalId signal { };
        DynamicWaitSnapshot snapshot;
        bool called { };
    };

    struct RuntimeIdentity {
        std::uint64_t generation { };
        std::uint64_t recertification_attempts { };
        std::uint64_t recertification_successes { };
        std::vector<const void*> authoritative_states;
        std::vector<std::uint8_t> packed_slots_bound;
    };

    struct ForkChildAccessState {
        bool access_complete { };
        bool halted { };
        bool waiting_on_static { };
        std::optional<ProcessId> fork_parent;
        std::optional<InstructionIndex> fork_site;
    };

    struct ForkAccessRuntimeSnapshot {
        std::uint64_t generation { };
        std::size_t process_count { };
        bool has_graph { };
        bool process_inventory_complete { };
        bool graph_inventory_complete { };
        std::uint64_t recertification_attempts { };
        std::uint64_t recertification_successes { };
        std::uint64_t recertification_failures { };
        bool parent_halted { };
        std::vector<ForkChildAccessState> children;
        std::array<bool, 2U> graph_has_children { };
        std::size_t survivor_component { };
        std::vector<ProcessId> survivor_members;
        std::optional<RegionComponentCertificateStatus> survivor_status;
        bool survivor_internal_candidate { };
        bool survivor_activation_program { };
        bool survivor_epochs_current { };
        std::array<bool, 2U> fork_output_dynamic { };
        std::array<bool, 2U> fork_output_writers_unknown { };
        std::array<RegionDriverClass, 2U> fork_output_driver_class { };
        std::array<bool, 2U> fork_output_is_candidate { };
    };

    [[nodiscard]] static ForkAccessRuntimeSnapshot fork_access_snapshot(
        const Interpreter& interpreter, const ProcessId parent,
        const std::array<ProcessId, 2U>& children,
        const ProcessId survivor_process, const SignalId survivor_internal,
        const std::array<SignalId, 2U>& fork_outputs)
    {
        const auto& state = *interpreter.impl_;
        ForkAccessRuntimeSnapshot result;
        result.generation = state.region_runtime_generation;
        result.process_count = state.processes.size();
        result.process_inventory_complete
            = state.process_signal_access_inventory_complete;
        result.recertification_attempts
            = state.systemverilog_wave_profile_region_recertification_attempts;
        result.recertification_successes
            = state.systemverilog_wave_profile_region_recertification_successes;
        result.recertification_failures
            = state.systemverilog_wave_profile_region_recertification_failures;
        result.parent_halted = parent < state.processes.size()
            && state.processes[parent].halted;
        result.children.reserve(children.size());
        for (const auto child_id : children) {
            ForkChildAccessState child;
            if (child_id < state.processes.size()) {
                const auto& process = state.processes[child_id];
                child.access_complete
                    = state.process_signal_access_is_complete(child_id);
                child.halted = process.halted;
                child.waiting_on_static = process.waiting_on_static;
                child.fork_parent = process.cold().fork_parent;
                child.fork_site = process.cold().fork_site;
            }
            result.children.push_back(child);
        }
        if (!state.region_graph) {
            return result;
        }
        result.has_graph = true;
        const auto& graph = *state.region_graph;
        const auto& inventory = graph.certificate_inventory();
        result.graph_inventory_complete = inventory.access_inventory_complete;
        for (std::size_t index = 0U; index < children.size(); ++index) {
            result.graph_has_children[index] = std::ranges::any_of(
                graph.processes(), [child_id = children[index]](
                                       const auto& process) {
                    return process.process == child_id;
                });
        }
        if (survivor_process < state.region_component_by_process.size()) {
            result.survivor_component
                = state.region_component_by_process[survivor_process];
        }
        const auto component = result.survivor_component;
        if (component < inventory.components.size()) {
            const auto& certificate = inventory.components[component];
            result.survivor_members = certificate.members;
            result.survivor_status = certificate.status;
            result.survivor_internal_candidate
                = std::ranges::find(
                    certificate.structural_internal_signal_candidates,
                    survivor_internal)
                != certificate.structural_internal_signal_candidates.end();
            result.survivor_epochs_current
                = graph.component_epochs_current(component);
            result.survivor_activation_program
                = component < state.region_activation_programs.size()
                && state.region_activation_programs[component].has_value();
        }
        for (std::size_t index = 0U; index < fork_outputs.size(); ++index) {
            const auto signal = fork_outputs[index];
            if (signal < graph.signals().size()) {
                result.fork_output_dynamic[index]
                    = graph.signals()[signal].dynamic_fork_writers;
                result.fork_output_writers_unknown[index]
                    = graph.signals()[signal].writers_unknown;
                result.fork_output_driver_class[index]
                    = graph.signals()[signal].drivers;
            }
            for (const auto& certificate : inventory.components) {
                if (std::ranges::find(
                        certificate.structural_internal_signal_candidates,
                        signal)
                    != certificate.structural_internal_signal_candidates.end()) {
                    result.fork_output_is_candidate[index] = true;
                    break;
                }
            }
        }
        return result;
    }

    [[nodiscard]] static RuntimeIdentity runtime_identity(
        const Interpreter& interpreter)
    {
        const auto& state = *interpreter.impl_;
        RuntimeIdentity result;
        result.generation = state.region_runtime_generation;
        result.recertification_attempts
            = state.systemverilog_wave_profile_region_recertification_attempts;
        result.recertification_successes
            = state.systemverilog_wave_profile_region_recertification_successes;
        result.authoritative_states.reserve(
            state.region_authoritative_state_by_component.size());
        result.packed_slots_bound.reserve(
            state.region_authoritative_state_by_component.size());
        for (const auto& component :
            state.region_authoritative_state_by_component) {
            result.authoritative_states.push_back(component.get());
            result.packed_slots_bound.push_back(static_cast<std::uint8_t>(
                component && component->values().packed_slots_bound()));
        }
        return result;
    }

    [[nodiscard]] static std::uint64_t build_unpublished_region_snapshot(
        Interpreter& interpreter)
    {
        auto& state = *interpreter.impl_;
        const auto snapshot = state.prepare_current_region_runtime_snapshot();
        return snapshot.generation;
    }

    [[nodiscard]] static DynamicWaitSnapshot dynamic_wait_snapshot(
        const Interpreter& interpreter, const ProcessId process_id,
        const SignalId signal_id)
    {
        const auto& state = *interpreter.impl_;
        const auto& process = state.processes[process_id];
        DynamicWaitSnapshot result;
        result.waiting_on_signal = process.waiting_on_signal;
        result.waiting_on_static = process.waiting_on_static;
        result.queued = process.queued;
        result.status = process.status;
        result.pc = process.pc;
        result.timeout_origin_present
            = process.wait_timeout_origin.has_value();
        const auto* const dynamic_wait
            = process.cold().dynamic_wait_state_if_present();
        result.sidecar_present = dynamic_wait != nullptr;
        result.timeout_deadline_present = dynamic_wait != nullptr
            && dynamic_wait->wait_timeout_deadline.has_value();
        if (signal_id < state.dynamic_fanout.size()) {
            for (const auto& registration : state.dynamic_fanout[signal_id]) {
                if (registration.active
                    && registration.process == process_id) {
                    ++result.active_registrations;
                }
            }
        }
        return result;
    }

    static void retry_dynamic_wait_process(
        Interpreter& interpreter, const ProcessId process_id)
    {
        interpreter.impl_->queue_active_current(process_id);
    }

    static void capture_dynamic_wait_allocation_failure(
        void* const context) noexcept
    {
        auto& probe = *static_cast<DynamicWaitAllocationProbe*>(context);
        probe.snapshot = dynamic_wait_snapshot(
            *probe.interpreter, probe.process, probe.signal);
        probe.called = true;
    }

    [[nodiscard]] static CallbackCacheSignalSnapshot callback_snapshot(
        const Interpreter& interpreter, const SignalId signal,
        const ProcessId epoch_process)
    {
        const auto& state = *interpreter.impl_;
        CallbackCacheSignalSnapshot result;
        const auto width = state.signals.at(signal).initial_value.width();
        result.current
            = state.signals.at(signal).initial_value.unchecked_low_word();
        result.last = state.signal_last_values.at(signal).unchecked_low_word();
        result.stored = state.driven_values.at(signal).unchecked_low_word();
        if (const auto* const owner = state.direct_single_driver_record(signal)) {
            result.owner_present = true;
            result.owner = owner->value.unchecked_low_word();
        }
        result.raw_current = { width, state.direct_signal_aval.at(signal),
            state.direct_signal_bval.at(signal) };
        result.raw_last = { width,
            state.direct_signal_last_aval.at(signal),
            state.direct_signal_last_bval.at(signal) };
        result.direct_materialization_pending
            = state.direct_signal_materialization_pending.at(signal) != 0U;
        result.event = state.signal_events.at(signal);
        result.transaction = state.signal_transactions.at(signal);
        result.event_stamp = state.signal_event_scheduling_stamps.at(signal);
        result.value_revision = state.signal_value_revisions.at(signal);
        result.runtime_generation = state.region_runtime_generation;
        result.capability_epoch = state.region_graph->capability_epoch(
            epoch_process);
        return result;
    }

    static void capture_pending_direct_mirror(
        void* context, const SchedulerTraceRecord& trace) noexcept
    {
        auto& probe = *static_cast<PendingDirectMirrorProbe*>(context);
        if (probe.interpreter == nullptr
            || trace.kind != SchedulerTraceKind::signal_change
            || trace.signal != probe.signal
            || probe.publication_count >= probe.publications.size()) {
            return;
        }
        const auto& state = *probe.interpreter->impl_;
        const auto signal = probe.signal;
        if (signal >= state.direct_signal_materialization_pending.size()
            || state.direct_signal_materialization_pending[signal] == 0U) {
            return;
        }
        auto& result = probe.publications[probe.publication_count++];
        const auto width = state.signals[signal].initial_value.width();
        result.time = trace.time;
        result.delta = trace.delta;
        result.current
            = state.signals[signal].initial_value.unchecked_low_word();
        result.last = state.signal_last_values[signal].unchecked_low_word();
        result.stored = state.driven_values[signal].unchecked_low_word();
        if (const auto* const owner
            = state.direct_single_driver_record(signal)) {
            result.owner_present = true;
            result.owner = owner->value.unchecked_low_word();
        }
        result.raw_current = { width, state.direct_signal_aval[signal],
            state.direct_signal_bval[signal] };
        result.raw_last = { width, state.direct_signal_last_aval[signal],
            state.direct_signal_last_bval[signal] };
        result.direct_materialization_pending = true;
        result.event = state.signal_events[signal];
        result.transaction = state.signal_transactions[signal];
        result.event_stamp = state.signal_event_scheduling_stamps[signal];
    }
};

} // namespace fsim::runtime::simir

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using fsim::tests::runtime::staging_failure_support::arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::
    allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;
using fsim::tests::runtime::staging_failure_support::
    set_allocation_failure_observer;

void check_operation_body_payload_census()
{
    OperationList operations {
        Operation { Concatenate {
            0U, std::vector<RegisterId> { 1U, 2U }, 64U } },
        Operation { WaitOn {
            std::vector<SignalId> { 3U, 4U },
            std::vector<EdgeKind> { EdgeKind::any, EdgeKind::posedge } } },
        Operation { ReadSignal { 0U, 5U } },
        Operation { LoadStringConstant { 0U, "text" } },
    };
    const OperationList shared = operations;
    const auto& source = operations;
    const auto* const concatenate
        = operation_get_if<Concatenate>(&source[0U]);
    const auto* const wait = operation_get_if<WaitOn>(&source[1U]);
    require(concatenate != nullptr && wait != nullptr,
        "payload census fixture retains its nested-vector operations");

    auto overridden = operations;
    overridden.replace(0U, Operation { ReadSignal { 0U, 5U } });
    require(overridden.shares_body_with(operations),
        "an instruction override preserves its immutable shared body");
    storage_census_detail::UniqueOperationBodyStorageCensus census;
    // The first facade differs from its immutable body. Counting facade
    // iteration would lose the canonical Concatenate's nested operand vector.
    census.add(overridden);
    census.add(operations);
    census.add(shared);
    require(census.body_references == 3U && census.unique_bodies.size() == 1U,
        "COW-shared OperationLists and their overrides contribute one unique body");
    const auto& body = census.unique_bodies.begin()->second;
    const auto expected_vector_bytes
        = concatenate->operands.capacity() * sizeof(RegisterId)
        + wait->signals.capacity() * sizeof(SignalId)
        + wait->edges.capacity() * sizeof(EdgeKind);
    require(body.operation_count == operations.size()
            && body.operation_capacity == operations.capacity(),
        "payload census retains the outer body size and capacity");
    require(body.boxed_group_allocations == 2U
            && body.boxed_group_object_bytes
                == sizeof(SignalOperationGroup) + sizeof(StringOperationGroup),
        "payload census accounts exact boxed group object sizes");
    require(body.nested_vector_allocations == 3U
            && body.nested_vector_capacity_bytes == expected_vector_bytes,
        "payload census counts direct nested vector capacity once");
}

void check_alias_observation_invalidation_is_allocation_free()
{
    std::array<Process, 2U> processes;
    processes[0U].id = 0U;
    processes[0U].static_sensitivity = { { 0U, EdgeKind::any } };
    processes[1U].id = 1U;
    processes[1U].static_sensitivity = { { 1U, EdgeKind::any } };
    const std::array<const Process*, 2U> programs {
        &processes[0U], &processes[1U] };
    const std::array<RegionSignalDescriptor, 3U> signals {
        RegionSignalDescriptor { 4U, ResolutionKind::sv_wire },
        RegionSignalDescriptor { 2U, ResolutionKind::sv_wire },
        RegionSignalDescriptor { 2U, ResolutionKind::sv_wire },
    };
    const RegionContainerDescriptor container {
        0U,
        { { 0U, true, true, true },
            { 1U, true, true, false },
            { 2U, true, true, false } },
        true,
    };
    const std::array<RegionContainerDescriptor, 1U> containers { container };
    const RegionSignalAliasFamilyDescriptor family {
        0U,
        0U,
        4U,
        { { 1U, 0U, 2U, 2U }, { 2U, 1U, 0U, 2U } },
        true,
        true,
        true,
    };
    const std::array<RegionSignalAliasFamilyDescriptor, 1U> families { family };
    auto graph = RegionGraph::build(
        programs, signals, false, containers, { }, families);

    std::span<const ProcessId> invalidated;
    bool allocation_failed { };
    begin_allocation_count();
    arm_allocation_failure(0U);
    try {
        invalidated = graph.observe_signal(0U, RegionObservation::current);
        const auto repeated = graph.observe_signal(
            2U, RegionObservation::events);
        require(repeated.data() == invalidated.data(),
            "family invalidation returns stable precomputed storage");
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    clear_allocation_failure();
    const auto allocation_count = end_allocation_count();

    require(!allocation_failed,
        "a valid family observation must not allocate");
    require(allocation_count == 0U,
        "a valid family observation must make zero allocation requests");
    require(std::ranges::equal(invalidated,
                std::array<ProcessId, 2U> { 0U, 1U })
            && graph.capability_epoch(0U) == 3U
            && graph.capability_epoch(1U) == 3U,
        "every family reader is invalidated synchronously and once per call");
}

void check_observation_query_hook_is_not_invoked_during_snapshot()
{
    Interpreter interpreter;
    static_cast<void>(interpreter.add_signal({
        "hook_probe", PackedLogic4(1U, Logic4::zero) }));
    std::size_t any_query_count { };
    std::size_t signal_query_count { };
    interpreter.set_native_signal_observation_any_hook([&any_query_count] {
        ++any_query_count;
        return false;
    });
    interpreter.set_native_signal_observation_required_hook(
        [&signal_query_count](const SignalId) {
            ++signal_query_count;
            return false;
        });
    interpreter.start();
    const auto before = NativeRegionAllocationTestAccess::runtime_identity(
        interpreter);
    const auto any_count_before = any_query_count;
    const auto signal_count_before = signal_query_count;
    const auto prepared_generation
        = NativeRegionAllocationTestAccess::build_unpublished_region_snapshot(
            interpreter);
    const auto after = NativeRegionAllocationTestAccess::runtime_identity(
        interpreter);
    require(prepared_generation == before.generation + 1U
            && after.generation == before.generation,
        "the direct region snapshot builder prepares, but does not publish, "
        "the next generation");
    require(any_query_count == any_count_before
            && signal_query_count == signal_count_before,
        "region snapshot construction must inspect hook presence without "
        "invoking application query hooks");
}


class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error("failed to set profile environment");
        }
    }

    explicit ScopedEnvironment(const char* name)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!unset()) {
            throw std::runtime_error("failed to unset profile environment");
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
            (void)set(previous_.c_str());
        } else {
            (void)unset();
        }
    }

private:
    bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value) == 0;
#else
        return ::setenv(name_.c_str(), value, 1) == 0;
#endif
    }

    bool unset() const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), "") == 0;
#else
        return ::unsetenv(name_.c_str()) == 0;
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

struct CapturedObservationCallback {
    std::string text;
    CallbackCacheSignalSnapshot signal;
};

bool same_callback_signal_snapshot(
    const CallbackCacheSignalSnapshot& lhs,
    const CallbackCacheSignalSnapshot& rhs) noexcept
{
    return lhs.current == rhs.current && lhs.last == rhs.last
        && lhs.stored == rhs.stored && lhs.owner == rhs.owner
        && lhs.raw_current == rhs.raw_current
        && lhs.raw_last == rhs.raw_last
        && lhs.owner_present == rhs.owner_present
        && lhs.direct_materialization_pending
            == rhs.direct_materialization_pending
        && lhs.event == rhs.event && lhs.transaction == rhs.transaction
        && lhs.event_stamp.origin.process_domain
            == rhs.event_stamp.origin.process_domain
        && lhs.event_stamp.origin.phase == rhs.event_stamp.origin.phase
        && lhs.event_stamp.systemverilog_round
            == rhs.event_stamp.systemverilog_round
        && lhs.value_revision == rhs.value_revision
        && lhs.runtime_generation == rhs.runtime_generation;
}

class CallbackCacheNativeWriter final : public ProcessExecutor {
public:
    CallbackCacheNativeWriter(
        const SignalId source,
        const SignalId output,
        ProcessExecutorProgramBinding binding)
        : source_(source)
        , output_(output)
        , binding_(std::move(binding))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        if (start_instruction != 0U && start_instruction != 3U) {
            throw std::logic_error {
                "callback-cache native writer resumed at an unexpected PC"
            };
        }
        context.write_blocking_word(
            output_, context.read_signal_word(source_));
        ProcessResumeResult result { 2U, 3U };
        result.external.kind = ExternalSuspendKind::wait_sensitivity;
        return result;
    }

private:
    SignalId source_ { };
    SignalId output_ { };
    ProcessExecutorProgramBinding binding_;
};

void check_completed_callback_observation_cache()
{
    ScopedEnvironment region_kernel_disabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
    Interpreter interpreter;
    const auto source = interpreter.add_signal({
        "callback_cache.source", PackedLogic4(1U, Logic4::zero) });
    const auto output = interpreter.add_signal({
        "callback_cache.output", PackedLogic4(1U, Logic4::zero) });
    const auto probe_signal = interpreter.add_signal({
        "callback_cache.probe", PackedLogic4(1U, Logic4::zero) });

    Process writer;
    writer.id = 0U;
    writer.name = "callback_cache_native_writer";
    writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    writer.static_sensitivity = { { source, EdgeKind::any } };
    writer.register_count = 1U;
    writer.operations = { ReadSignal { 0U, source },
        WriteBlocking { output, 0U }, WaitSensitivity { }, Jump { 0U } };
    require(interpreter.add_process(std::move(writer)) == 0U,
        "callback cache writer has a stable process identity");
    const auto& registered_writer = interpreter.process_program(0U);
    interpreter.set_process_executor(0U,
        std::make_unique<CallbackCacheNativeWriter>(source, output,
            ProcessExecutorProgramBinding {
                registered_writer, registered_writer, 0U }));

    Process observer;
    observer.id = 1U;
    observer.name = "callback_cache_epoch_probe";
    observer.static_sensitivity = { { probe_signal, EdgeKind::any } };
    observer.register_count = 1U;
    observer.operations = { ReadSignal { 0U, probe_signal },
        WaitSensitivity { }, Jump { 0U } };
    require(interpreter.add_process(std::move(observer)) == 1U,
        "callback cache epoch probe has a stable process identity");

    const auto add_output_process = [&](const ProcessId id,
                                        const SimulationTick time,
                                        const char* text) {
        Process process;
        process.id = id;
        process.name = "callback_cache_display_" + std::to_string(id);
        process.operations = { WaitFor { time }, Display { text }, Halt { } };
        require(interpreter.add_process(std::move(process)) == id,
            "callback cache display receives its process identity");
    };
    add_output_process(2U, 2U, "cold-first");
    add_output_process(3U, 2U, "cold-hit");
    add_output_process(4U, 4U, "after-write");
    add_output_process(5U, 4U, "after-write-hit");

    Process stimulus;
    stimulus.id = 6U;
    stimulus.name = "callback_cache_stimulus";
    stimulus.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    stimulus.register_count = 1U;
    stimulus.driver_regions = { { source, 0U, 0U, true } };
    stimulus.operations = { WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { source, 0U },
        WaitFor { 2U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
        WriteBlocking { source, 0U }, Halt { } };
    require(interpreter.add_process(std::move(stimulus)) == 6U,
        "callback cache stimulus has a separate process identity");

    PendingDirectMirrorProbe pending_probe { &interpreter, output };
    std::vector<CapturedObservationCallback> callbacks;
    interpreter.set_output_hook(
        [&](const ProcessId, const std::string_view text, const bool,
            const SimulationTick time, const std::uint64_t delta) {
            CapturedObservationCallback callback;
            callback.text = text;
            callback.signal
                = NativeRegionAllocationTestAccess::callback_snapshot(
                    interpreter, output, 1U);
            callback.signal.time = time;
            callback.signal.delta = delta;
            callbacks.push_back(std::move(callback));
        });
    interpreter.scheduler().set_trace_hook(&pending_probe,
        &NativeRegionAllocationTestAccess::capture_pending_direct_mirror);

    interpreter.start();
    const auto initial_epoch
        = NativeRegionAllocationTestAccess::callback_snapshot(
            interpreter, output, 1U).capability_epoch;
    const auto run = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);

    require(run.status == RunStatus::completed
            && callbacks.size() == 4U
            && callbacks[0U].text == "cold-first"
            && callbacks[1U].text == "cold-hit"
            && callbacks[2U].text == "after-write"
            && callbacks[3U].text == "after-write-hit",
        "the two callback pairs execute at their requested simulation times");
    require(callbacks[0U].signal.time == 2U
            && callbacks[1U].signal.time == 2U
            && callbacks[1U].signal.delta == callbacks[0U].signal.delta
            && callbacks[2U].signal.time == 4U
            && callbacks[3U].signal.time == 4U
            && callbacks[3U].signal.delta == callbacks[2U].signal.delta,
        "each same-slot callback pair shares one callback key");

    const Logic4Word zero { 1U, 0U, 0U };
    const Logic4Word one { 1U, 1U, 0U };
    const auto& cold = callbacks[0U].signal;
    const auto& cold_hit = callbacks[1U].signal;
    const auto& invalidated = callbacks[2U].signal;
    const auto& invalidated_hit = callbacks[3U].signal;
    require(!cold.owner_present && cold.current == one && cold.last == zero
            && cold.stored == one
            && cold.raw_current == one && cold.raw_last == zero
            && !cold.direct_materialization_pending
            && cold.event && cold.transaction
            && cold.event->first == 1U && cold.transaction->first == 1U
            && cold.event == cold.transaction
            && cold.event_stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && cold.event_stamp.origin.phase == SchedulerPhase::active
            && cold.capability_epoch > initial_epoch
            && same_callback_signal_snapshot(cold, cold_hit)
            && cold_hit.capability_epoch == cold.capability_epoch,
        "the first callback publishes the pending native roles and the adjacent callback takes the completed observation path");
    require(invalidated.current == zero && invalidated.last == one
            && invalidated.stored == zero && !invalidated.owner_present
            && invalidated.raw_current == zero
            && invalidated.raw_last == one
            && !invalidated.direct_materialization_pending
            && invalidated.event && invalidated.transaction
            && invalidated.event->first == 3U
            && invalidated.transaction->first == 3U
            && invalidated.event == invalidated.transaction
            && invalidated.event_stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && invalidated.event_stamp.origin.phase == SchedulerPhase::active
            && invalidated.value_revision == cold.value_revision + 1U
            && invalidated.runtime_generation == cold.runtime_generation
            && invalidated.capability_epoch > cold_hit.capability_epoch
            && same_callback_signal_snapshot(invalidated, invalidated_hit)
            && invalidated_hit.capability_epoch
                == invalidated.capability_epoch,
        "a changed native publication invalidates the observation cache before the next callback, which materializes its new four-state roles");

    require(pending_probe.publication_count == 2U
            && pending_probe.publications[0U].direct_materialization_pending
            && pending_probe.publications[0U].time == 1U
            && pending_probe.publications[0U].raw_current == one
            && pending_probe.publications[0U].current == zero
            && pending_probe.publications[0U].last == zero
            && pending_probe.publications[0U].stored == zero
            && !pending_probe.publications[0U].owner_present
            && pending_probe.publications[0U].event
            && pending_probe.publications[0U].transaction
            && pending_probe.publications[0U].event
                == pending_probe.publications[0U].transaction
            && pending_probe.publications[1U].direct_materialization_pending
            && pending_probe.publications[1U].time == 3U
            && pending_probe.publications[1U].raw_current == zero
            && pending_probe.publications[1U].raw_last == one
            && pending_probe.publications[1U].current == one
            && pending_probe.publications[1U].last == zero
            && pending_probe.publications[1U].stored == one
            && !pending_probe.publications[1U].owner_present,
        "the trace proves both native writes expose a deferred raw plane before callback materialization");
    require(interpreter.signal_value_snapshot(output) == PackedLogic4(1U, Logic4::zero),
        "the final public signal value agrees with the last callback snapshot");
}

class ScopedCerrCapture final {
public:
    explicit ScopedCerrCapture(std::ostringstream& output)
        : previous_(std::cerr.rdbuf(output.rdbuf()))
    {
    }

    ScopedCerrCapture(const ScopedCerrCapture&) = delete;
    ScopedCerrCapture& operator=(const ScopedCerrCapture&) = delete;

    ~ScopedCerrCapture()
    {
        std::cerr.rdbuf(previous_);
    }

private:
    std::streambuf* previous_;
};

struct QuietRecertificationProbe {
    Interpreter* interpreter { };
    std::array<std::size_t, 5U> resumes { };
    std::function<bool(SignalId)> observation_query;
    std::size_t query_calls_at_quiet_point { };
    bool hook_installed { };
    bool trace_install_failed { };

    static void receive_trace(
        void* context, const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<QuietRecertificationProbe*>(context);
        if (probe.hook_installed || record.kind != SchedulerTraceKind::batch_end
            || record.time != 1U || record.delta != 0U
            || record.phase != SchedulerPhase::active
            || !record.systemverilog || record.order != 0U
            || record.count != 2U) {
            return;
        }
        try {
            probe.interpreter->set_native_signal_observation_required_hook(
                std::move(probe.observation_query));
            probe.hook_installed = true;
        } catch (...) {
            probe.trace_install_failed = true;
        }
    }
};

class QuietRecertificationExecutor final : public ProcessExecutor {
public:
    QuietRecertificationExecutor(
        QuietRecertificationProbe& probe, const ProcessId id,
        const SignalId input, const SignalId output,
        const InstructionIndex wait_instruction,
        ProcessExecutorProgramBinding binding)
        : probe_(probe)
        , id_(id)
        , input_(input)
        , output_(output)
        , wait_instruction_(wait_instruction)
        , binding_(std::move(binding))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return true;
    }

    class Completion final : public PreparedRegionCompletion {
    public:
        Completion(const void* identity, QuietRecertificationProbe& probe,
            const ProcessId process) noexcept
            : identity_(identity)
            , probe_(probe)
            , process_(process)
        {
        }

        [[nodiscard]] const void* storage_identity() const noexcept override
        {
            return identity_;
        }

        void commit() noexcept override
        {
            // The native prefix has committed its reserved publications.
            // Arm tracing now so its batch_end can install the arbitrary
            // query hook before the already-ready second component runs.
            if (process_ == 0U && probe_.interpreter != nullptr
                && probe_.interpreter->scheduler().now() == 1U) {
                probe_.interpreter->scheduler().set_trace_hook(
                    &probe_, &QuietRecertificationProbe::receive_trace);
            }
        }

    private:
        const void* identity_ { };
        QuietRecertificationProbe& probe_;
        ProcessId process_ { };
    };

    [[nodiscard]] std::unique_ptr<PreparedRegionCompletion>
    prepare_region_completion(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        std::span<const RegionRegisterBinding>,
        std::span<const PackedLogic4>) override
    {
        if (process != id_ || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U) {
            return { };
        }
        return std::make_unique<Completion>(this, probe_, id_);
    }

    void redirect(InstructionIndex) override { }

    ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex) override
    {
        ++probe_.resumes[id_];
        context.write_update_in_domain(output_, context.read_signal(input_),
            SignalUpdateDomain::systemverilog_active);
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    std::size_t resume_ordered_cohort(
        std::span<ProcessCohortResumeEntry> entries) override
    {
        std::size_t completed { };
        for (auto& entry : entries) {
            *entry.queued = false;
            *entry.waiting_on_static = false;
            *entry.status = ProcessStatus::running;
            entry.result = entry.executor->resume(
                *entry.context, entry.start_instruction);
            ++completed;
        }
        return completed;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

private:
    QuietRecertificationProbe& probe_;
    ProcessId id_ { };
    SignalId input_ { };
    SignalId output_ { };
    InstructionIndex wait_instruction_ { };
    ProcessExecutorProgramBinding binding_;
};

constexpr ProcessId fork_access_parent_id = 5U;

enum class ForkCloneAccessMode : std::uint8_t {
    exact,
    changed_remap,
    opaque,
};

class ForkAccessInventoryExecutor final : public ProcessExecutor {
public:
    ForkAccessInventoryExecutor(
        Process registered_program, Process generated_program,
        const ProcessId generated_process,
        std::shared_ptr<const ProcessSignalRemap> signal_remap,
        const ForkCloneAccessMode clone_mode, const bool expose_binding,
        const SignalId remap_source, const SignalId remap_target,
        const std::optional<InstructionIndex> branch = std::nullopt)
        : registered_program_(std::move(registered_program))
        , generated_program_(std::move(generated_program))
        , generated_process_(generated_process)
        , signal_remap_(std::move(signal_remap))
        , clone_mode_(clone_mode)
        , expose_binding_(expose_binding)
        , remap_source_(remap_source)
        , remap_target_(remap_target)
        , branch_(branch)
        , binding_(registered_program_, generated_program_, generated_process_,
              signal_remap_)
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return expose_binding_ ? &binding_ : nullptr;
    }

    [[nodiscard]] std::unique_ptr<ProcessExecutor> fork_clone(
        const InstructionIndex branch) override
    {
        auto remap = signal_remap_;
        bool expose_binding = true;
        auto child_mode = ForkCloneAccessMode::exact;
        if (clone_mode_ == ForkCloneAccessMode::changed_remap) {
            auto changed_remap = std::make_shared<ProcessSignalRemap>(
                remap ? *remap : ProcessSignalRemap { });
            changed_remap->emplace_back(remap_source_, remap_target_);
            remap = std::move(changed_remap);
        } else if (clone_mode_ == ForkCloneAccessMode::opaque) {
            expose_binding = false;
        }
        return std::make_unique<ForkAccessInventoryExecutor>(
            registered_program_, generated_program_, generated_process_,
            std::move(remap), child_mode, expose_binding,
            remap_source_, remap_target_, branch);
    }

    void redirect(InstructionIndex) override { }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&, const InstructionIndex start) override
    {
        if (!branch_) {
            if (start == 0U) {
                return { 0U, 1U };
            }
            if (start == 1U) {
                return { 2U, 3U };
            }
            throw std::logic_error {
                "fork inventory parent resumed at an unexpected instruction"
            };
        }
        if (start >= *branch_ && start <= *branch_ + 3U) {
            return { start, start + 1U };
        }
        throw std::logic_error {
            "fork inventory child resumed at an unexpected instruction"
        };
    }

private:
    Process registered_program_;
    Process generated_program_;
    ProcessId generated_process_ { };
    std::shared_ptr<const ProcessSignalRemap> signal_remap_;
    ForkCloneAccessMode clone_mode_ { ForkCloneAccessMode::exact };
    bool expose_binding_ { };
    SignalId remap_source_ { };
    SignalId remap_target_ { };
    std::optional<InstructionIndex> branch_;
    ProcessExecutorProgramBinding binding_;
};

struct ForkAccessCaseResult {
    NativeRegionAllocationTestAccess::ForkAccessRuntimeSnapshot after_recertification;
    std::optional<NativeRegionAllocationTestAccess::ForkAccessRuntimeSnapshot>
        after_remap_mutation;
};

ForkAccessCaseResult run_fork_access_recertification_case(
    const ForkCloneAccessMode clone_mode, const bool mutate_remap_after_attestation)
{
    QuietRecertificationProbe quiet_probe;
    Interpreter interpreter;
    std::array<SignalId, 2U> inputs { };
    std::array<SignalId, 2U> internals { };
    std::array<SignalId, 2U> outputs { };
    std::array<SignalId, 2U> triggers { };
    for (std::size_t component = 0U; component < inputs.size(); ++component) {
        const auto prefix = "fork_access_component_" + std::to_string(component);
        inputs[component] = interpreter.add_signal({ prefix + ".input",
            PackedLogic4(1U, Logic4::zero) });
        internals[component] = interpreter.add_signal({ prefix + ".internal",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        outputs[component] = interpreter.add_signal({ prefix + ".output",
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });
        triggers[component] = interpreter.add_signal({ prefix + ".trigger",
            PackedLogic4(1U, Logic4::zero) });
    }
    const auto barrier_signal = interpreter.add_signal({
        "fork_access_recertification_barrier",
        PackedLogic4(1U, Logic4::zero) });
    const auto fork_input_a = interpreter.add_signal({
        "fork_access_input_a", PackedLogic4(1U, Logic4::zero) });
    const auto fork_input_b = interpreter.add_signal({
        "fork_access_input_b", PackedLogic4(1U, Logic4::zero) });
    const auto fork_output_a = interpreter.add_signal({
        "fork_access_output_a", PackedLogic4(1U, Logic4::x),
        ResolutionKind::sv_wire });
    const auto fork_output_b = interpreter.add_signal({
        "fork_access_output_b", PackedLogic4(1U, Logic4::x),
        ResolutionKind::sv_wire });

    for (ProcessId component = 0U; component < 2U; ++component) {
        const auto producer_id = component == 0U ? 0U : 3U;
        const auto consumer_id = producer_id + 1U;
        Process producer;
        producer.id = producer_id;
        producer.name = "fork_access_quiet_producer_"
            + std::to_string(component);
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.initialize = true;
        producer.register_count = 1U;
        producer.static_sensitivity = {
            { triggers[component], EdgeKind::any } };
        producer.driver_regions = {
            { internals[component], 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, inputs[component] },
            WriteUpdate { internals[component], 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == producer_id,
            "fork inventory fixture keeps dense producer identities");

        Process consumer;
        consumer.id = consumer_id;
        consumer.name = "fork_access_quiet_consumer_"
            + std::to_string(component);
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.initialize = true;
        consumer.register_count = 1U;
        consumer.static_sensitivity = {
            { triggers[component], EdgeKind::any },
            { internals[component], EdgeKind::any },
        };
        consumer.driver_regions = {
            { outputs[component], 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, internals[component] },
            WriteUpdate { outputs[component], 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(consumer)) == consumer_id,
            "fork inventory fixture keeps dense consumer identities");

        for (const auto id : { producer_id, consumer_id }) {
            const auto& registered = interpreter.process_program(id);
            const ProcessExecutorProgramBinding binding {
                registered, registered, id };
            const auto wait_instruction = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            const auto source = id == producer_id
                ? inputs[component] : internals[component];
            const auto destination = id == producer_id
                ? internals[component] : outputs[component];
            interpreter.set_process_executor(id,
                std::make_unique<QuietRecertificationExecutor>(
                    quiet_probe, id, source, destination,
                    wait_instruction, binding));
        }
        if (component == 0U) {
            Process barrier;
            barrier.id = 2U;
            barrier.name = "fork_access_recertification_barrier";
            barrier.scheduling_domain = ProcessSchedulingDomain::systemverilog;
            barrier.initialize = false;
            barrier.static_sensitivity = {
                { barrier_signal, EdgeKind::any } };
            barrier.operations = { WaitSensitivity { }, Jump { 0U } };
            require(interpreter.add_process(std::move(barrier)) == 2U,
                "fork inventory fixture separates the pure components");
        }
    }

    Process fork_parent;
    fork_parent.id = fork_access_parent_id;
    fork_parent.name = "fork_access_inventory_parent";
    fork_parent.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    fork_parent.register_count = 1U;
    fork_parent.static_sensitivity = {
        { fork_input_a, EdgeKind::any },
        { fork_input_b, EdgeKind::any },
    };
    fork_parent.driver_regions = {
        { fork_output_a, 0U, 0U, true },
        { fork_output_b, 0U, 0U, true },
    };
    fork_parent.operations = {
        Fork { { 3U, 7U }, ForkJoinKind::none },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
        Halt { },
        WaitSensitivity { },
        ReadSignal { 0U, fork_input_a },
        WriteUpdate { fork_output_a, 0U,
            SignalUpdateDomain::systemverilog_active },
        ForkEnd { },
        WaitSensitivity { },
        ReadSignal { 0U, fork_input_b },
        WriteUpdate { fork_output_b, 0U,
            SignalUpdateDomain::systemverilog_active },
        ForkEnd { },
    };
    require(interpreter.add_process(std::move(fork_parent)) == fork_access_parent_id,
        "fork inventory parent receives its dense identity");
    const auto& registered_fork_parent
        = interpreter.process_program(fork_access_parent_id);
    auto mutable_remap = std::make_shared<ProcessSignalRemap>(
        ProcessSignalRemap { { fork_input_a, fork_input_a } });
    interpreter.set_process_executor(fork_access_parent_id,
        std::make_unique<ForkAccessInventoryExecutor>(
            registered_fork_parent, registered_fork_parent, fork_access_parent_id,
            mutable_remap, clone_mode, true, fork_input_a, fork_input_b));

    Process clock;
    clock.id = 6U;
    clock.name = "fork_access_recertification_clock";
    clock.register_count = 0U;
    clock.operations = { WaitFor { 1U }, WaitFor { 1U }, Halt { } };
    require(interpreter.add_process(std::move(clock)) == 6U,
        "fork inventory fixture has a real future quiet-point boundary");

    interpreter.start();
    const auto started = NativeRegionAllocationTestAccess::runtime_identity(
        interpreter);
    // Observe one certified component before the fork executes. This forces a
    // full quiet-point snapshot that includes the newly created children,
    // while leaving the independent second pure component eligible.
    interpreter.prepare_signal_observation(internals[0U]);
    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::time_limit,
        "the fork and child waits settle before the scheduled recertification point");

    constexpr std::array<ProcessId, 2U> child_ids { 7U, 8U };
    const std::array<SignalId, 2U> fork_outputs {
        fork_output_a, fork_output_b };
    const auto first = NativeRegionAllocationTestAccess::fork_access_snapshot(
        interpreter, fork_access_parent_id, child_ids, 3U, internals[1U],
        fork_outputs);
    require(first.process_count == 9U && first.children.size() == 2U
            && first.parent_halted
            && std::ranges::all_of(first.children,
                [](const auto& child) {
                    return child.fork_parent == fork_access_parent_id
                        && child.fork_site == 0U
                        && !child.halted && child.waiting_on_static;
                }),
        "the parent completes after spawning two parked children");
    require(first.has_graph && first.generation > started.generation
            && std::ranges::all_of(first.graph_has_children,
                [](const bool present) { return present; })
            && first.recertification_attempts
                > started.recertification_attempts
            && first.recertification_successes
                > started.recertification_successes,
        "a full successful runtime recertification includes the fork children");

    ForkAccessCaseResult result;
    result.after_recertification = first;
    if (mutate_remap_after_attestation) {
        // The alias is mutable only from this test; executor bindings expose it
        // as const. A correct child attestation owns a deep snapshot.
        mutable_remap->front().second = fork_input_b;
        interpreter.prepare_signal_observation(internals[1U]);
        const auto later = interpreter.run(1U);
        require(later.status == RunStatus::time_limit,
            "the clock provides a second drained slot for remap recertification");
        result.after_remap_mutation
            = NativeRegionAllocationTestAccess::fork_access_snapshot(
                interpreter, fork_access_parent_id, child_ids, 3U, internals[1U],
                fork_outputs);
    }
    return result;
}

void check_fork_executor_access_survives_live_recertification()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment profile_enabled {
        "FSIM_PROFILE_SV_WAVES", "1" };
    const auto exact = run_fork_access_recertification_case(
        ForkCloneAccessMode::exact, true);
    const auto& first = exact.after_recertification;
    require(first.process_inventory_complete && first.graph_inventory_complete
            && first.children.size() == 2U
            && std::ranges::all_of(first.children,
                [](const auto& child) { return child.access_complete; }),
        "an exactly validated fork clone remains complete by child identity after recertification");
    require(first.survivor_status
                == RegionComponentCertificateStatus::structural_candidate
            && first.survivor_members
                == std::vector<ProcessId> { 3U, 4U }
            && first.survivor_internal_candidate
            && first.survivor_activation_program
            && first.survivor_epochs_current,
        "the unrelated pure component retains a current activation certificate");
    require(std::ranges::all_of(first.fork_output_dynamic,
                [](const bool dynamic) { return dynamic; })
            && std::ranges::all_of(first.fork_output_writers_unknown,
                [](const bool unknown) { return unknown; })
            && std::ranges::all_of(first.fork_output_driver_class,
                [](const auto driver_class) {
                    return driver_class == RegionDriverClass::unknown;
                })
            && std::ranges::none_of(first.fork_output_is_candidate,
                [](const bool candidate) { return candidate; }),
        "fork-owned outputs remain dynamic and excluded from private candidates");

    require(exact.after_remap_mutation.has_value(),
        "the remap mutation control performs a second real recertification");
    const auto& changed = *exact.after_remap_mutation;
    require(changed.generation > first.generation
            && changed.recertification_attempts
                > first.recertification_attempts
            && changed.recertification_successes
                > first.recertification_successes
            && std::ranges::all_of(changed.children,
                [](const auto& child) {
                    return child.fork_parent == fork_access_parent_id
                        && child.fork_site == 0U
                        && !child.halted && child.waiting_on_static;
                })
            && !changed.process_inventory_complete
            && !changed.graph_inventory_complete
            && changed.children.size() == 2U
            && std::ranges::none_of(changed.children,
                [](const auto& child) { return child.access_complete; }),
        "mutating the shared remap after attestation is rejected by the next snapshot");

    for (const auto mode : { ForkCloneAccessMode::changed_remap,
             ForkCloneAccessMode::opaque }) {
        const auto declined = run_fork_access_recertification_case(mode, false)
                                  .after_recertification;
        require(declined.parent_halted && declined.children.size() == 2U
                && std::ranges::all_of(declined.children,
                    [](const auto& child) {
                        return !child.access_complete
                            && child.fork_parent.has_value()
                            && child.fork_site == 0U;
                    })
                && !declined.process_inventory_complete
                && !declined.graph_inventory_complete
                && declined.survivor_status
                    == RegionComponentCertificateStatus::incomplete_access_inventory
                && !declined.survivor_activation_program,
            "a remapped or opaque clone fails closed without disabling the fork itself");
    }
}

std::uint64_t region_profile_metric(
    const std::string& profile, const std::string_view metric)
{
    const auto key = std::string { metric } + '=';
    const auto start = profile.find(key);
    require(start != std::string::npos,
        "region profile includes the requested route counter");
    const auto value_start = start + key.size();
    const auto end = profile.find(' ', value_start);
    return static_cast<std::uint64_t>(std::stoull(profile.substr(
        value_start, end == std::string::npos
            ? std::string::npos : end - value_start)));
}

void check_quiet_recertification_after_same_slot_revocation()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    QuietRecertificationProbe probe;
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        probe.interpreter = &interpreter;
        std::array<SignalId, 2U> inputs { };
        std::array<SignalId, 2U> internals { };
        std::array<SignalId, 2U> outputs { };
        std::array<SignalId, 2U> triggers { };
        for (std::size_t component = 0U; component < inputs.size(); ++component) {
            const auto prefix = "quiet_component_" + std::to_string(component);
            inputs[component] = interpreter.add_signal({ prefix + ".input",
                PackedLogic4(1U, Logic4::zero) });
            internals[component] = interpreter.add_signal({ prefix + ".internal",
                PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
            outputs[component] = interpreter.add_signal({ prefix + ".output",
                PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });
            triggers[component] = interpreter.add_signal({ prefix + ".trigger",
                PackedLogic4(1U, Logic4::zero) });
        }
        const auto barrier_trigger = interpreter.add_signal({
            "quiet_recertification_barrier_trigger",
            PackedLogic4(1U, Logic4::zero) });

        for (ProcessId component = 0U; component < 2U; ++component) {
            const auto producer_id = component == 0U ? 0U : 3U;
            const auto consumer_id = producer_id + 1U;
            Process producer;
            producer.id = producer_id;
            producer.name = "quiet_producer_" + std::to_string(component);
            producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
            producer.initialize = true;
            producer.register_count = 1U;
            producer.static_sensitivity = {
                { triggers[component], EdgeKind::any } };
            producer.driver_regions = { { internals[component], 0U, 0U, true } };
            producer.operations = {
                ReadSignal { 0U, inputs[component] },
                WriteUpdate { internals[component], 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
            require(interpreter.add_process(std::move(producer)) == producer_id,
                "quiet producer receives its stable component process identity");

            Process consumer;
            consumer.id = consumer_id;
            consumer.name = "quiet_consumer_" + std::to_string(component);
            consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
            consumer.initialize = true;
            consumer.register_count = 1U;
            consumer.static_sensitivity = {
                { triggers[component], EdgeKind::any },
                { internals[component], EdgeKind::any },
            };
            consumer.driver_regions = {
                { outputs[component], 0U, 0U, true } };
            consumer.operations = {
                ReadSignal { 0U, internals[component] },
                WriteUpdate { outputs[component], 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
            require(interpreter.add_process(std::move(consumer)) == consumer_id,
                "quiet consumer receives its stable component process identity");

            for (const auto id : { producer_id, consumer_id }) {
                const auto& registered = interpreter.process_program(id);
                const ProcessExecutorProgramBinding binding {
                    registered, registered, id };
                const auto wait_instruction = static_cast<InstructionIndex>(
                    registered.operations.size() - 2U);
                DeferredProcessExecutorContract contract;
                contract.expected_access = binding;
                contract.callbacks_observation_safe = true;
                contract.expected_region_kernel_equivalent = true;
                const auto source = id == producer_id
                    ? inputs[component] : internals[component];
                const auto destination = id == producer_id
                    ? internals[component] : outputs[component];
                interpreter.set_deferred_process_executor(id,
                    [] { return true; },
                    [&probe, id, source, destination, wait_instruction, binding] {
                        return std::make_unique<QuietRecertificationExecutor>(
                            probe, id, source, destination, wait_instruction,
                            binding);
                    }, std::move(contract));
            }
            if (component == 0U) {
                // The first component must form a native prefix before the
                // second becomes visible to the diagnostic hook. Keep a real
                // ineligible SV task between their stable process orders.
                Process barrier;
                barrier.id = 2U;
                barrier.name = "quiet_recertification_barrier";
                barrier.scheduling_domain
                    = ProcessSchedulingDomain::systemverilog;
                barrier.initialize = false;
                barrier.static_sensitivity = {
                    { barrier_trigger, EdgeKind::any } };
                barrier.operations = { WaitSensitivity { }, Jump { 0U } };
                require(interpreter.add_process(std::move(barrier)) == 2U,
                    "quiet components retain a distinct SV batch boundary");
            }
        }
        interpreter.materialize_ready_process_executors();

        Process clock;
        clock.id = 5U;
        clock.name = "quiet_recertification_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.operations = {
            WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { inputs[0U], 0U },
            WriteBlocking { triggers[0U], 0U },
            WriteBlocking { barrier_trigger, 0U },
            WriteBlocking { inputs[1U], 0U },
            WriteBlocking { triggers[1U], 0U },
            WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
            WriteBlocking { triggers[0U], 0U },
            WaitFor { 5U }, Halt { },
        };
        require(interpreter.add_process(std::move(clock)) == 5U,
            "quiet recertification clock has a separate process identity");

        probe.observation_query = [&probe](const SignalId) {
            if (probe.interpreter->scheduler().at_runtime_slot_quiet_point()) {
                ++probe.query_calls_at_quiet_point;
            }
            return false;
        };
        interpreter.start();
        const auto started_identity
            = NativeRegionAllocationTestAccess::runtime_identity(interpreter);
        const auto startup = interpreter.run(0U);
        require(startup.status == RunStatus::time_limit,
            "quiet recertification members reach their startup waits");
        const auto startup_quiet_identity
            = NativeRegionAllocationTestAccess::runtime_identity(interpreter);
        require(startup_quiet_identity.generation == started_identity.generation
                && startup_quiet_identity.authoritative_states
                    == started_identity.authoritative_states
                && startup_quiet_identity.recertification_attempts
                    == started_identity.recertification_attempts + 1U
                && startup_quiet_identity.recertification_successes
                    == started_identity.recertification_successes + 1U
                && std::ranges::any_of(
                    started_identity.authoritative_states,
                    [](const void* state) { return state != nullptr; })
                && std::ranges::any_of(
                    startup_quiet_identity.packed_slots_bound,
                    [](const std::uint8_t bound) { return bound != 0U; }),
            "startup value-only recertification rebinds the same A4 snapshot");
        const auto startup_resumes = probe.resumes;

        const auto first_slot = interpreter.run(1U);
        require(first_slot.status == RunStatus::time_limit
                && probe.hook_installed && !probe.trace_install_failed,
            "the trace installs an arbitrary query hook after the first region prefix");
        const auto observed_quiet_identity
            = NativeRegionAllocationTestAccess::runtime_identity(interpreter);
        const bool authoritative_state_replaced
            = startup_quiet_identity.authoritative_states.size()
                == observed_quiet_identity.authoritative_states.size()
            && [&] {
                bool saw_replaced_state { };
                for (std::size_t index = 0U;
                     index < startup_quiet_identity.authoritative_states.size();
                     ++index) {
                    if (startup_quiet_identity.authoritative_states[index]
                        != nullptr) {
                        saw_replaced_state = true;
                        if (startup_quiet_identity.authoritative_states[index]
                            == observed_quiet_identity.authoritative_states[index]) {
                            return false;
                        }
                    }
                }
                return saw_replaced_state;
            }();
        require(observed_quiet_identity.generation
                    == startup_quiet_identity.generation + 1U
                && observed_quiet_identity.recertification_attempts
                    == startup_quiet_identity.recertification_attempts + 1U
                && observed_quiet_identity.recertification_successes
                    == startup_quiet_identity.recertification_successes + 1U
                && authoritative_state_replaced,
            "the arbitrary observation hook publishes a new runtime generation and A4 state snapshot");
        require(probe.resumes[0U] == startup_resumes[0U]
                && probe.resumes[3U] > startup_resumes[3U]
                && probe.resumes[4U] > startup_resumes[4U],
            "same-slot graph revocation sends the already-ready second component through its original executors");

        const auto first_slot_resumes = probe.resumes;
        const auto second_slot = interpreter.run(2U);
        require(second_slot.status == RunStatus::time_limit
                && probe.resumes[0U] > first_slot_resumes[0U],
            "the quiet-point snapshot keeps the newly installed query hook conservative in the next slot");
        require(probe.query_calls_at_quiet_point == 0U,
            "snapshot preparation does not call untrusted observation queries");
        interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    }

    const auto profile = captured.str();
    const auto wave_start = profile.find(
        "fsim-profile: sv-ordered-wave-summary ");
    require(wave_start != std::string::npos,
        "quiet-point fixture emits the wave profile");
    const auto wave_end = profile.find('\n', wave_start);
    const auto wave_line = profile.substr(wave_start,
        wave_end == std::string::npos ? std::string::npos
                                     : wave_end - wave_start);
    const auto recert_start = profile.find(
        "region_recert_attempts=", wave_start);
    // Startup reaches the waits through the ordinary route. The t1 component
    // ticket executes two native members before the hook revokes the graph.
    // Readiness may also count singleton work outside this grouped prefix;
    // the prefix still supplies two tickets and four offered members.
    require(recert_start != std::string::npos
            && region_profile_metric(wave_line, "region_kernel_runs") == 1U
            && region_profile_metric(wave_line, "region_kernel_members") == 2U
            && region_profile_metric(wave_line, "component_batch_tickets")
                >= 2U
            && region_profile_metric(wave_line, "component_batch_members")
                >= 4U
            && region_profile_metric(wave_line,
                "component_batch_entries_elided") == 2U
            && region_profile_metric(wave_line, "region_recert_attempts") == 2U
            && region_profile_metric(wave_line, "region_recert_successes") == 2U,
        "startup value-only rebind and observer-forced full snapshot both succeed at quiet points");
}

void check_dynamic_wait_state_is_lazy_and_first_arm_is_transactional()
{
    Interpreter static_interpreter;
    const auto static_trigger = static_interpreter.add_signal({
        "static_wait_trigger", PackedLogic4(1U, Logic4::zero) });
    Process static_process;
    static_process.id = 0U;
    static_process.static_sensitivity = {
        { static_trigger, EdgeKind::any } };
    static_process.operations = { WaitSensitivity { }, Halt { } };
    require(static_interpreter.add_process(std::move(static_process)) == 0U,
        "static wait process keeps its process identity");

    const auto before_static_wait
        = NativeRegionAllocationTestAccess::dynamic_wait_snapshot(
            static_interpreter, 0U, static_trigger);
    static_interpreter.start();
    const auto after_static_wait
        = NativeRegionAllocationTestAccess::dynamic_wait_snapshot(
            static_interpreter, 0U, static_trigger);
    require(!before_static_wait.sidecar_present
            && !after_static_wait.sidecar_present
            && after_static_wait.waiting_on_static
            && !after_static_wait.waiting_on_signal
            && after_static_wait.active_registrations == 0U,
        "a process parked on static sensitivity never allocates dynamic wait state");

    Interpreter dynamic_interpreter;
    const auto dynamic_trigger = dynamic_interpreter.add_signal({
        "dynamic_wait_trigger", PackedLogic4(1U, Logic4::zero) });
    Process dynamic_process;
    dynamic_process.id = 0U;
    dynamic_process.operations = { Display { "arm dynamic wait" },
        WaitOn { { dynamic_trigger } }, Display { "dynamic wait woke" },
        Halt { } };
    require(dynamic_interpreter.add_process(std::move(dynamic_process)) == 0U,
        "dynamic wait process keeps its process identity");

    dynamic_interpreter.schedule_signal_at(
        dynamic_trigger, PackedLogic4(1U, Logic4::one), 1U);
    std::size_t arm_hook_calls { };
    std::size_t wake_hook_calls { };
    NativeRegionAllocationTestAccess::DynamicWaitAllocationProbe probe {
        &dynamic_interpreter, 0U, dynamic_trigger, {}, false };
    set_allocation_failure_observer(
        &probe,
        &NativeRegionAllocationTestAccess::
            capture_dynamic_wait_allocation_failure);
    dynamic_interpreter.set_output_hook(
        [&arm_hook_calls, &wake_hook_calls](
            ProcessId, std::string_view text, bool, SimulationTick,
            std::uint64_t) {
            if (text == "arm dynamic wait") {
                ++arm_hook_calls;
                arm_allocation_failure(0U);
            } else if (text == "dynamic wait woke") {
                ++wake_hook_calls;
            }
        });
    dynamic_interpreter.start();
    bool allocation_failed { };
    try {
        static_cast<void>(dynamic_interpreter.run());
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    clear_allocation_failure();
    set_allocation_failure_observer(nullptr, nullptr);

    const auto failed_first_arm
        = NativeRegionAllocationTestAccess::dynamic_wait_snapshot(
            dynamic_interpreter, 0U, dynamic_trigger);
    require(arm_hook_calls == 1U && allocation_failed && probe.called
            && allocation_failure_was_injected(),
        "the injected failure reaches the first dynamic wait sidecar allocation");
    require(!probe.snapshot.sidecar_present
            && !probe.snapshot.waiting_on_signal
            && !probe.snapshot.waiting_on_static
            && !probe.snapshot.queued
            && probe.snapshot.status == ProcessStatus::running
            && probe.snapshot.pc == 1U
            && !probe.snapshot.timeout_origin_present
            && !probe.snapshot.timeout_deadline_present
            && probe.snapshot.active_registrations == 0U,
        "allocation failure observes the unchanged WaitOn boundary "
        "before publication");
    require(!failed_first_arm.sidecar_present
            && !failed_first_arm.waiting_on_signal
            && !failed_first_arm.waiting_on_static
            && !failed_first_arm.queued
            && failed_first_arm.status == ProcessStatus::running
            && failed_first_arm.pc == 1U
            && !failed_first_arm.timeout_origin_present
            && !failed_first_arm.timeout_deadline_present
            && failed_first_arm.active_registrations == 0U,
        "failed first-arm allocation preserves the WaitOn boundary "
        "without registrations or timeout state");

    NativeRegionAllocationTestAccess::retry_dynamic_wait_process(
        dynamic_interpreter, 0U);
    const auto retried = dynamic_interpreter.run();
    const auto after_wake
        = NativeRegionAllocationTestAccess::dynamic_wait_snapshot(
            dynamic_interpreter, 0U, dynamic_trigger);
    require(retried.status == RunStatus::completed
            && arm_hook_calls == 1U
            && wake_hook_calls == 1U
            && after_wake.sidecar_present
            && !after_wake.waiting_on_signal
            && after_wake.status == ProcessStatus::finished
            && after_wake.active_registrations == 0U,
        "retry from the preserved WaitOn boundary registers and wakes exactly once");
}

} // namespace

int main()
{
    try {
        check_operation_body_payload_census();
        check_alias_observation_invalidation_is_allocation_free();
        check_observation_query_hook_is_not_invoked_during_snapshot();
        check_completed_callback_observation_cache();
        check_quiet_recertification_after_same_slot_revocation();
        check_fork_executor_access_survives_live_recertification();
        check_dynamic_wait_state_is_lazy_and_first_arm_is_transactional();
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << "region graph observation test failed: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
