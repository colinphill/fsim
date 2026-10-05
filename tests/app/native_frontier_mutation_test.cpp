// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "native_frontier_v2_only_provider.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    using FrontierRuntime = Interpreter::Impl::RegionFrontierComponentRuntime;

    static void install_v2_frontier_only_provider(
        fsim::app::Simulation& simulation)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the app simulation has no interpreter for its V2 test route"
            };
        }
        auto& interpreter = *application.interpreter;
        auto provider
            = interpreter.impl_->region_kernel_backend_provider;
        interpreter.set_region_kernel_backend_provider(
            std::make_shared<test::V2FrontierOnlyRegionKernelBackendProvider>(
                std::move(provider)));
    }

    struct RawDriver {
        ProcessId process { };
        std::string value;
        DriveStrength strength;

        friend bool operator==(const RawDriver&, const RawDriver&) = default;
    };

    struct SignalSnapshot {
        SignalId signal { };
        std::string current;
        std::string last;
        std::string stored;
        std::vector<RawDriver> raw_drivers;
        std::optional<std::string> owned_raw;
        std::optional<std::string> external_raw;
        std::optional<std::string> force_value;
        std::optional<std::string> force_mask;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        ProcessSchedulingDomain event_domain {
            ProcessSchedulingDomain::generic };
        SchedulerPhase event_phase { SchedulerPhase::active };
        std::uint64_t systemverilog_round { };
        std::uint64_t value_revision { };
        SimulationTick now { };
        std::uint64_t delta { };
        bool materialization_pending { };
    };

    struct PendingWriteSnapshot {
        struct Key {
            std::uint64_t time { };
            std::uint64_t delta { };
            std::uint64_t systemverilog_round { };
            std::uint64_t stable_order { };
            std::uint64_t sequence { };
            std::uint32_t process_domain { };
            std::uint32_t phase { };

            friend bool operator==(const Key&, const Key&) = default;
        };

        std::uint32_t member_index { };
        std::uint32_t signal_slot { };
        SignalId signal { };
        ProcessId owner { };
        std::uint32_t source_instruction { };
        std::uint32_t update_kind { };
        std::uint32_t flags { };
        std::uint32_t value_kind { };
        std::uint32_t width { };
        std::uint32_t word_count { };
        std::uint32_t plane_count { };
        Key commit_key;
        Key origin;
        std::vector<std::uint64_t> aval;
        std::vector<std::uint64_t> bval;

        friend bool operator==(const PendingWriteSnapshot&,
            const PendingWriteSnapshot&) = default;
    };

    struct FrontierSnapshot {
        bool available { };
        bool authoritative_state_pinned { };
        bool certificate_current { };
        bool authoritative_state_valid { };
        bool packed_slots_bound { };
        bool requires_prewrite_unbind { };
        std::size_t component { };
        std::uint64_t runtime_generation { };
        std::uint64_t bound_runtime_generation { };
        std::uint64_t native_member_dispatches { };
        const void* runtime_identity { };
        bool invalidated { };
        std::uint32_t pending_write_count { };
        std::uint32_t staged_event_count { };
        std::uint32_t scheduler_task_cursor { };
        std::uint32_t scheduler_task_count { };
        std::vector<PendingWriteSnapshot> pending_writes;

        friend bool operator==(const FrontierSnapshot&,
            const FrontierSnapshot&) = default;
    };

    struct SchedulerTaskIdentity {
        fsim::runtime::StableOrder stable_order { };
        std::uint64_t sequence { };
        SimulationTick time { };
        std::uint64_t delta { };
        std::optional<SchedulerPhase> phase;
        std::uint64_t systemverilog_round { };
        bool systemverilog { };
        bool end_of_time_slot { };

        friend bool operator==(const SchedulerTaskIdentity&,
            const SchedulerTaskIdentity&) = default;
    };

    struct SchedulerTaskTraceCapture {
        fsim::runtime::StableOrder expected_order { };
        SimulationTick expected_time { };
        std::size_t matching_task_ends { };
        std::optional<SchedulerTaskIdentity> task;

        static void receive(void* context,
            const fsim::runtime::SchedulerTraceRecord& record) noexcept
        {
            auto& capture = *static_cast<SchedulerTaskTraceCapture*>(context);
            if (record.kind != fsim::runtime::SchedulerTraceKind::task_end
                || record.time != capture.expected_time
                || record.order != capture.expected_order) {
                return;
            }
            ++capture.matching_task_ends;
            capture.task = SchedulerTaskIdentity { record.order,
                record.sequence, record.time, record.delta, record.phase,
                record.systemverilog_round, record.systemverilog,
                record.end_of_time_slot };
        }
    };

    [[nodiscard]] static PendingWriteSnapshot::Key copy_key(
        const RegionFrontierKeyV2& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            key.stable_order, key.sequence, key.process_domain, key.phase };
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the frontier mutation probe has no interpreter"
            };
        }

        const auto& state = *application.interpreter->impl_;
        if (signal >= state.signals.size()
            || state.has_container_signal_alias(signal)) {
            throw std::logic_error {
                "the frontier mutation probe requires an ordinary signal"
            };
        }

        SignalSnapshot result;
        result.signal = signal;
        result.materialization_pending
            = signal < state.direct_signal_materialization_pending.size()
            && state.direct_signal_materialization_pending[signal] != 0U;
        const auto width = state.signals[signal].initial_value.width();
        const AuthoritativeSignalPlanes* authoritative_values { };
        if (width > 64U
            && signal < state.region_authoritative_component_by_signal.size()) {
            const auto component
                = state.region_authoritative_component_by_signal[signal];
            const RegionAuthoritativeComponentState* component_state { };
            if (component < state.region_frontier_runtime_by_component.size()
                && state.region_frontier_runtime_by_component[component]
                && state.region_frontier_runtime_by_component[component]
                       ->authoritative_state) {
                component_state
                    = state.region_frontier_runtime_by_component[component]
                          ->authoritative_state.get();
            } else if (component
                           < state.region_authoritative_state_by_component.size()
                && state.region_authoritative_state_by_component[component]) {
                component_state
                    = state.region_authoritative_state_by_component[component]
                          .get();
            }
            if (component_state != nullptr
                && component_state->values().packed_signal_slots_bound(signal)) {
                authoritative_values
                    = &component_state->values();
            }
        }

        if (authoritative_values != nullptr) {
            result.current = authoritative_values->current(signal).to_msb_string();
            result.last = authoritative_values->previous(signal).to_msb_string();
            result.stored = authoritative_values->stored(signal).to_msb_string();
        } else if (result.materialization_pending) {
            if (state.signals[signal].value_kind != ValueKind::logic4
                || width == 0U || width > 64U
                || signal >= state.direct_signal_aval.size()
                || signal >= state.direct_signal_bval.size()
                || signal >= state.direct_signal_last_aval.size()
                || signal >= state.direct_signal_last_bval.size()) {
                throw std::logic_error {
                    "the pending metadata probe cannot read this plane"
                };
            }
            result.current = PackedLogic4::from_aval_bval(width,
                state.direct_signal_aval[signal],
                state.direct_signal_bval[signal]).to_msb_string();
            result.last = PackedLogic4::from_aval_bval(width,
                state.direct_signal_last_aval[signal],
                state.direct_signal_last_bval[signal]).to_msb_string();
            result.stored = result.current;
        } else {
            result.current = state.signals[signal].initial_value.to_msb_string();
            result.last = state.signal_last_values.at(signal).to_msb_string();
            result.stored = state.driven_values.at(signal).to_msb_string();
        }

        state.driver_values.at(signal).for_each_in_process_order(
            [&](const DriverRecord& record) {
                const auto value = [&] {
                    if (authoritative_values != nullptr) {
                        return authoritative_values->owner_value(
                            signal, record.process);
                    }
                    if (result.materialization_pending
                        && state.direct_single_driver_record(signal) == &record) {
                        return PackedLogic4::from_aval_bval(width,
                            state.direct_signal_aval[signal],
                            state.direct_signal_bval[signal]);
                    }
                    if (state.owned_driver_active(signal)) {
                        return state.owned_driver_value(record.process, signal);
                    }
                    return record.value;
                }();
                result.raw_drivers.push_back({ record.process,
                    value.to_msb_string(), record.strength });
            });
        if (signal < state.owned_driver_composites.size()
            && state.owned_driver_composites[signal].active) {
            result.owned_raw
                = state.owned_driver_composites[signal].committed.to_msb_string();
        }
        if (signal < state.external_driver_values.size()
            && state.external_driver_values[signal]) {
            result.external_raw
                = state.external_driver_values[signal]->to_msb_string();
        }
        if (signal < state.forced_values.size() && state.forced_values[signal]) {
            result.force_value = state.forced_values[signal]->to_msb_string();
        }
        if (signal < state.forced_masks.size() && state.forced_masks[signal]) {
            result.force_mask = state.forced_masks[signal]->to_msb_string();
        }
        result.event = state.signal_events.at(signal);
        result.transaction = state.signal_transactions.at(signal);
        const auto& event_stamp
            = state.signal_event_scheduling_stamps.at(signal);
        result.event_domain = event_stamp.origin.process_domain;
        result.event_phase = event_stamp.origin.phase;
        result.systemverilog_round = event_stamp.systemverilog_round;
        if (signal < state.signal_value_revisions.size()) {
            result.value_revision = state.signal_value_revisions[signal];
        }
        result.now = state.scheduler.now();
        result.delta = state.scheduler.delta();
        return result;
    }

    [[nodiscard]] static FrontierSnapshot frontier_for_signal(
        const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return { };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            return { };
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_frontier_runtime_by_component.size()) {
            return { };
        }
        const auto& runtime
            = state.region_frontier_runtime_by_component[component];
        if (!runtime) {
            return { };
        }

        FrontierSnapshot result;
        result.available = true;
        result.authoritative_state_pinned
            = runtime->authoritative_state != nullptr;
        result.certificate_current = state.region_graph
            && state.region_graph->component_epochs_current(component);
        if (runtime->authoritative_state) {
            result.authoritative_state_valid
                = runtime->authoritative_state->valid();
            result.packed_slots_bound = runtime->authoritative_state
                ->values().packed_slots_bound();
            result.requires_prewrite_unbind = runtime->authoritative_state
                ->values().requires_prewrite_unbind();
        }
        result.component = component;
        result.runtime_generation = runtime->runtime_generation;
        result.bound_runtime_generation
            = runtime->frame.bound_runtime_generation;
        result.native_member_dispatches = runtime->native_member_dispatches;
        result.runtime_identity = runtime.get();
        result.invalidated = runtime->invalidated;
        result.pending_write_count = runtime->frame.pending_write_count;
        result.staged_event_count = runtime->frame.staged_event_count;
        result.scheduler_task_cursor = runtime->frame.scheduler_task_cursor;
        result.scheduler_task_count = runtime->frame.scheduler_task_count;
        if (result.pending_write_count > runtime->pending_writes.size()) {
            throw std::logic_error {
                "the stopped native frame has an invalid pending-write count"
            };
        }
        result.pending_writes.reserve(result.pending_write_count);
        for (std::size_t index = 0U;
             index < result.pending_write_count; ++index) {
            const auto& write = runtime->pending_writes[index];
            if (write.signal_slot >= runtime->planes.size()) {
                throw std::logic_error {
                    "the stopped native frame has an invalid write target slot"
                };
            }
            if (write.value_kind != RegionFrontierValueKindV2::logic4
                || write.word_count == 0U
                || write.plane_count != kRegionFrontierLogic4PlaneCountV2
                || write.value_planes[0U] == nullptr
                || write.value_planes[1U] == nullptr
                || write.value_planes[2U] != nullptr
                || write.value_planes[3U] != nullptr) {
                throw std::logic_error {
                    "the stopped native frame has a malformed Logic4 pending write"
                };
            }
            PendingWriteSnapshot pending;
            pending.member_index = write.member_index;
            pending.signal_slot = write.signal_slot;
            pending.signal = runtime->planes[write.signal_slot].signal_id;
            pending.owner = runtime->planes[write.signal_slot].owner_process_id;
            pending.source_instruction = write.source_instruction;
            pending.update_kind = write.update_kind;
            pending.flags = write.flags;
            pending.value_kind = static_cast<std::uint32_t>(write.value_kind);
            pending.width = write.width;
            pending.word_count = write.word_count;
            pending.plane_count = write.plane_count;
            pending.commit_key = copy_key(write.commit_key);
            pending.origin = copy_key(write.origin);
            pending.aval.assign(write.value_planes[0U],
                write.value_planes[0U] + write.word_count);
            pending.bval.assign(write.value_planes[1U],
                write.value_planes[1U] + write.word_count);
            result.pending_writes.push_back(std::move(pending));
        }
        return result;
    }

    [[nodiscard]] static std::shared_ptr<FrontierRuntime>
    pin_frontier_for_signal(const fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return { };
        }
        const auto& state = *application.interpreter->impl_;
        if (signal >= state.region_authoritative_component_by_signal.size()) {
            return { };
        }
        const auto component
            = state.region_authoritative_component_by_signal[signal];
        if (component >= state.region_frontier_runtime_by_component.size()) {
            return { };
        }
        return state.region_frontier_runtime_by_component[component];
    }

    [[nodiscard]] static ProcessId display_process_for_text(
        const fsim::app::Simulation& simulation,
        const std::string_view text)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the frontier mutation probe has no interpreter"
            };
        }

        const auto& processes = application.interpreter->impl_->processes;
        std::optional<ProcessId> result;
        for (ProcessId process = 0U; process < processes.size(); ++process) {
            const auto& operations = processes.program_view(process).operations();
            for (const auto& operation : operations) {
                const auto* const display = operation_get_if<Display>(&operation);
                if (display == nullptr || display->text != text) {
                    continue;
                }
                if (result) {
                    throw std::logic_error {
                        "the frontier mutation marker must identify one process"
                    };
                }
                result = process;
            }
        }
        if (!result) {
            throw std::logic_error {
                "the frontier mutation marker process was not found"
            };
        }
        return *result;
    }

    static void clear_runtime_output_callback(
        fsim::app::Simulation& simulation)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the frontier mutation probe has no interpreter"
            };
        }
        application.interpreter->set_output_hook({ });
    }

    static void schedule_foreign_cut_task(
        fsim::app::Simulation& simulation,
        const ProcessId stable_order,
        const SimulationTick time,
        fsim::runtime::Scheduler::Task task)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the frontier mutation probe has no interpreter"
            };
        }

        auto& scheduler = application.interpreter->impl_->scheduler;
        scheduler.schedule_systemverilog_at(time,
            SchedulerPhase::active,
            std::numeric_limits<fsim::runtime::StableOrder>::max(),
            [stable_order, task = std::move(task)](
                fsim::runtime::Scheduler& current) mutable {
                // The timed HDL assignments run earlier in this Active round.
                // Queue the foreign stop task for the next frozen Active round
                // so it sorts beside the resulting process activations.
                current.schedule_systemverilog_next_delta(
                    SchedulerPhase::active,
                    static_cast<fsim::runtime::StableOrder>(stable_order),
                    std::move(task));
            });
    }

    static void capture_scheduler_task_end(
        fsim::app::Simulation& simulation,
        const ProcessId process,
        const SimulationTick time,
        SchedulerTaskTraceCapture& capture) noexcept
    {
        const auto& application = *simulation.impl_;
        capture.expected_order = static_cast<fsim::runtime::StableOrder>(process);
        capture.expected_time = time;
        if (application.interpreter) {
            application.interpreter->impl_->scheduler.set_trace_hook(
                &capture, &SchedulerTaskTraceCapture::receive);
        }
    }

    static void clear_scheduler_trace(
        fsim::app::Simulation& simulation) noexcept
    {
        const auto& application = *simulation.impl_;
        if (application.interpreter) {
            application.interpreter->impl_->scheduler.set_trace_hook(
                nullptr, nullptr);
        }
    }

    [[nodiscard]] static std::uint64_t native_frontier_dispatches(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return 0U;
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_native_frontier_member_dispatches;
    }
};

} // namespace fsim::runtime::simir

namespace {

using fsim::app::Simulation;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunStatus;
using fsim::app::SimulationEngine;
using fsim::runtime::SimulationTick;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using FrontierKey = NativeRegionAllocationTestAccess::PendingWriteSnapshot::Key;
using fsim::runtime::simir::SignalId;
using fsim::runtime::simir::pending_active;
using fsim::runtime::simir::pending_committed;
using fsim::runtime::simir::pending_value_ready;
using fsim::runtime::simir::pending_internal_target;
using fsim::runtime::simir::pending_key_assigned;

constexpr std::size_t signal_count = 12U;
constexpr std::size_t frontier_width = 65U;

enum class MutationKind {
    force,
    deposit,
};

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error { "failed to update process environment" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        static_cast<void>(set(previous_ ? previous_->c_str() : nullptr));
    }

private:
    [[nodiscard]] bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        const auto status = value == nullptr
            ? ::unsetenv(name_.c_str())
            : ::setenv(name_.c_str(), value, 1);
        return status == 0;
#endif
    }

    std::string name_;
    std::optional<std::string> previous_;
};

struct SignalFrame {
    std::array<NativeRegionAllocationTestAccess::SignalSnapshot,
        signal_count> signals;
};

struct OutputEvent {
    fsim::runtime::simir::ProcessId process { };
    std::string text;
    bool newline { };
    SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const OutputEvent&, const OutputEvent&) = default;
};

struct RouteResult {
    SignalFrame initial;
    SignalFrame at_cut;
    SignalFrame after_stop;
    SignalFrame after_mutation;
    SignalFrame after_resume;
    NativeRegionAllocationTestAccess::FrontierSnapshot cut_frontier;
    NativeRegionAllocationTestAccess::FrontierSnapshot stopped_frontier;
    NativeRegionAllocationTestAccess::FrontierSnapshot mutated_frontier;
    NativeRegionAllocationTestAccess::FrontierSnapshot bystander_frontier_at_cut;
    NativeRegionAllocationTestAccess::FrontierSnapshot bystander_frontier_after_mutation;
    NativeRegionAllocationTestAccess::FrontierSnapshot bystander_frontier_after_resume;
    std::optional<NativeRegionAllocationTestAccess::SchedulerTaskIdentity>
        scheduler_task_at_cut;
    std::size_t scheduler_task_trace_matches { };
    fsim::runtime::simir::ProcessId foreign_process { };
    std::vector<OutputEvent> output_events;
    std::uint64_t dispatches_before_cut { };
    std::uint64_t dispatches_at_cut { };
    std::uint64_t dispatches_after_mutation { };
    std::uint64_t dispatches_after_resume { };
    bool reached_foreign_cut { };
};

[[nodiscard]] bool same_signal_semantics(
    const NativeRegionAllocationTestAccess::SignalSnapshot& left,
    const NativeRegionAllocationTestAccess::SignalSnapshot& right)
{
    return left.signal == right.signal
        && left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.raw_drivers == right.raw_drivers
        && left.owned_raw == right.owned_raw
        && left.external_raw == right.external_raw
        && left.force_value == right.force_value
        && left.force_mask == right.force_mask
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_domain == right.event_domain
        && left.event_phase == right.event_phase
        && left.systemverilog_round == right.systemverilog_round
        && left.value_revision == right.value_revision
        && left.now == right.now
        && left.delta == right.delta;
}

[[nodiscard]] bool same_frame(
    const SignalFrame& left,
    const SignalFrame& right)
{
    for (std::size_t index = 0U; index < signal_count; ++index) {
        if (!same_signal_semantics(left.signals[index], right.signals[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool key_is_before(
    const FrontierKey& left,
    const FrontierKey& right)
{
    return std::tie(left.time, left.delta, left.systemverilog_round,
               left.process_domain, left.phase, left.stable_order,
               left.sequence)
        < std::tie(right.time, right.delta, right.systemverilog_round,
            right.process_domain, right.phase, right.stable_order,
            right.sequence);
}

[[nodiscard]] bool raw_driver_has_value(
    const NativeRegionAllocationTestAccess::SignalSnapshot& signal,
    const fsim::runtime::simir::ProcessId owner,
    const std::string_view value)
{
    return std::ranges::any_of(signal.raw_drivers,
        [&](const NativeRegionAllocationTestAccess::RawDriver& driver) {
            return driver.process == owner && driver.value == value;
        });
}

void require_pending_writes_after_cut(
    const NativeRegionAllocationTestAccess::FrontierSnapshot& frontier,
    const NativeRegionAllocationTestAccess::SchedulerTaskIdentity& cut)
{
    using fsim::runtime::SchedulerPhase;
    using fsim::runtime::simir::ProcessSchedulingDomain;
    require(frontier.available && frontier.authoritative_state_pinned
            && !frontier.invalidated
            && frontier.pending_write_count != 0U
            && frontier.pending_writes.size() == frontier.pending_write_count
            && frontier.scheduler_task_cursor <= frontier.scheduler_task_count,
        "the foreign cut must retain a valid native frame with pending writes");
    const FrontierKey cut_key { cut.time, cut.delta,
        cut.systemverilog_round, cut.stable_order, cut.sequence,
        static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog),
        static_cast<std::uint32_t>(SchedulerPhase::active) };
    constexpr auto required_flags
        = pending_active | pending_value_ready | pending_key_assigned;
    std::size_t active_writes { };
    for (const auto& write : frontier.pending_writes) {
        // The descriptor high-water mark also includes inactive write sites.
        if ((write.flags & pending_active) == 0U) {
            continue;
        }
        ++active_writes;
        // The consumed borrowed prefix excludes tickets issued into the next
        // Active round. Their assigned keys prove that publication is pending.
        require((write.flags & required_flags) == required_flags
                && (write.flags & pending_committed) == 0U
                && write.commit_key.process_domain == cut_key.process_domain
                && write.commit_key.phase == cut_key.phase
                && write.commit_key.time == cut_key.time
                && write.commit_key.delta == cut_key.delta
                && write.commit_key.systemverilog_round
                    > cut_key.systemverilog_round
                && key_is_before(cut_key, write.commit_key),
            "each active retained write must have an uncommitted key in a later Active round");
    }
    require(active_writes != 0U,
        "the foreign cut must retain actual active writes, not only allocated descriptors");
}

void require_pending_writes_delivered(
    const NativeRegionAllocationTestAccess::FrontierSnapshot& stopped_frontier,
    const SignalFrame& final_frame,
    const SignalId witness_signal)
{
    const NativeRegionAllocationTestAccess::PendingWriteSnapshot* latest_write { };
    for (const auto& write : stopped_frontier.pending_writes) {
        if (write.signal != witness_signal
            || (write.flags & pending_internal_target) == 0U
            || (write.flags & pending_key_assigned) == 0U) {
            continue;
        }
        if (latest_write == nullptr
            || key_is_before(latest_write->commit_key, write.commit_key)) {
            latest_write = &write;
        }
    }
    require(latest_write != nullptr,
        "the stopped native frame must retain the shallow member's keyed internal write");

    const auto signal = std::ranges::find_if(final_frame.signals,
        [&](const auto& candidate) { return candidate.signal == witness_signal; });
    require(signal != final_frame.signals.end(),
        "the retained shallow-member write must target a captured signal");
    const auto pending_value = PackedLogic4::from_word_planes(
        latest_write->width, latest_write->aval, latest_write->bval)
                                  .to_msb_string();
    require(raw_driver_has_value(*signal, latest_write->owner, pending_value),
        "the retained value must reach its raw owner without producer replay");
    require(latest_write->commit_key.delta
            < std::numeric_limits<std::uint64_t>::max(),
        "the fixture commit delta must admit the runtime event stamp");
    const auto stamp_delta = latest_write->commit_key.delta + 1U;
    require(signal->transaction
            && signal->transaction->first == latest_write->commit_key.time
            && signal->transaction->second == stamp_delta,
        "the retained write must publish transaction metadata at its original key");
    require(signal->event
            && signal->event->first == latest_write->commit_key.time
            && signal->event->second == stamp_delta
            && signal->systemverilog_round
                == latest_write->commit_key.systemverilog_round
            && static_cast<std::uint32_t>(signal->event_domain)
                == latest_write->origin.process_domain
            && static_cast<std::uint32_t>(signal->event_phase)
                == latest_write->origin.phase,
        "the changed retained write must publish its event stamp at the original scheduler key");
}

[[nodiscard]] fsim::project::Config make_mutation_config(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root)
{
    const auto source = root / "native_frontier_mutation.sv";
    std::string source_a(frontier_width, '0');
    source_a.front() = '1';
    std::string source_b(frontier_width, '1');
    source_b.back() = '0';

    std::ofstream output { source, std::ios::binary };
    output << "\nmodule native_frontier_mutation("
           << "output wire [64:0] sink_a, output wire [64:0] sink_b);\n"
           << "  logic [64:0] source_a;\n"
           << "  logic [64:0] source_b;\n"
           << "  wire [64:0] a_stage;\n"
           << "  wire [64:0] a_value;\n"
           << "  wire [64:0] a_complement;\n"
           << "  wire [64:0] a_joined;\n"
           << "  wire [64:0] b_stage;\n"
           << "  wire [64:0] b_value;\n"
           << "  wire [64:0] b_complement;\n"
           << "  wire [64:0] b_joined;\n\n"
           << "  assign a_joined = a_value ^ a_complement;\n"
           << "  assign a_complement = ~source_a;\n"
           << "  always @(source_a) begin\n"
           << "    $display(\"NATIVE_FRONTIER_MUTATION_CUT\");\n"
           << "  end\n"
           << "  assign a_value = a_stage;\n"
           << "  assign a_stage = source_a;\n"
           << "  assign sink_a = a_joined;\n\n"
           << "  assign b_joined = b_value ^ b_complement;\n"
           << "  assign b_complement = ~source_b;\n"
           << "  assign b_value = b_stage;\n"
           << "  assign b_stage = source_b;\n"
           << "  assign sink_b = b_joined;\n\n"
           << "  initial begin\n"
           << "    source_a = 65'b" << std::string(frontier_width, '0') << ";\n"
           << "    source_b = 65'b" << std::string(frontier_width, '0') << ";\n"
           << "    #2; source_a = 65'b" << source_a
           << "; source_b = 65'b" << source_b << ";\n"
           << "    #100;\n"
           << "    $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the native frontier mutation design must be written completely");

    fsim::project::Config config;
    config.project.name = "native-frontier-force-deposit-foreign-cut";
    config.project.top = "sv:work.native_frontier_mutation";
    config.base_directory = root;
    config.build.optimization = optimization;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

[[nodiscard]] RouteResult run_mutation_route(
    const fsim::project::Optimization optimization,
    const std::filesystem::path& root,
    const SimulationEngine engine,
    const MutationKind mutation)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave_kernel {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_mutation_config(optimization, root);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the native frontier force/deposit fixture must parse and elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    if (compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the force/deposit fixture must install compiled processes");
    }
    // The app layer installs a nonempty forwarding callback even when the
    // public Simulation output hook is empty. That wrapper prepares all
    // signals before each Display, so clear it for this scheduler-only cut.
    NativeRegionAllocationTestAccess::clear_runtime_output_callback(
        simulation);

    constexpr std::array<std::string_view, signal_count> signal_names {
        "native_frontier_mutation.source_a",
        "native_frontier_mutation.a_stage",
        "native_frontier_mutation.a_value",
        "native_frontier_mutation.a_complement",
        "native_frontier_mutation.a_joined",
        "native_frontier_mutation.sink_a",
        "native_frontier_mutation.source_b",
        "native_frontier_mutation.b_stage",
        "native_frontier_mutation.b_value",
        "native_frontier_mutation.b_complement",
        "native_frontier_mutation.b_joined",
        "native_frontier_mutation.sink_b",
    };
    std::array<SignalId, signal_count> signals { };
    for (std::size_t index = 0U; index < signal_count; ++index) {
        const auto signal = simulation.find_signal(signal_names[index]);
        require(signal.has_value(),
            "the force/deposit fixture must retain all signal handles");
        signals[index] = *signal;
    }

    const auto capture_frame = [&] {
        SignalFrame frame;
        for (std::size_t index = 0U; index < signal_count; ++index) {
            frame.signals[index]
                = NativeRegionAllocationTestAccess::snapshot(
                    simulation, signals[index]);
        }
        return frame;
    };

    RouteResult result;
    const auto target_value = std::string(frontier_width, '1');
    const auto target_signal = signals[1U];
    constexpr std::string_view cut_marker
        = "NATIVE_FRONTIER_MUTATION_CUT";
    const auto foreign_process
        = NativeRegionAllocationTestAccess::display_process_for_text(
            simulation, cut_marker);
    result.foreign_process = foreign_process;
    NativeRegionAllocationTestAccess::SchedulerTaskTraceCapture task_trace;
    const auto capture_foreign_cut =
        [&](fsim::runtime::Scheduler& scheduler) {
            result.reached_foreign_cut = true;
            result.at_cut = capture_frame();
            // Install tracing only from the foreign task itself. This records
            // its exact scheduler key without changing native ticket admission
            // for the tasks that lead up to this cut.
            NativeRegionAllocationTestAccess::capture_scheduler_task_end(
                simulation, foreign_process, 2U, task_trace);
            if (compiled) {
                result.dispatches_at_cut
                    = NativeRegionAllocationTestAccess::native_frontier_dispatches(
                        simulation);
                result.cut_frontier
                    = NativeRegionAllocationTestAccess::frontier_for_signal(
                        simulation, target_signal);
                result.bystander_frontier_at_cut
                    = NativeRegionAllocationTestAccess::frontier_for_signal(
                        simulation, signals[7U]);
            }
            scheduler.request_stop();
        };

    if (compiled) {
        // Preserve this test's exact V2 pending-frame assertions while the
        // separate A2 app witness covers default flattened forwarding.
        NativeRegionAllocationTestAccess::install_v2_frontier_only_provider(
            simulation);
    }
    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the fixture must settle its initial values before the foreign cut");
    result.initial = capture_frame();
    if (compiled) {
        result.dispatches_before_cut
            = NativeRegionAllocationTestAccess::native_frontier_dispatches(
                simulation);
        const auto target_frontier
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, target_signal);
        const auto bystander_frontier
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, signals[7U]);
        require(target_frontier.available && bystander_frontier.available
                && target_frontier.authoritative_state_pinned
                && bystander_frontier.authoritative_state_pinned
                && target_frontier.certificate_current
                && bystander_frontier.certificate_current
                && target_frontier.authoritative_state_valid
                && bystander_frontier.authoritative_state_valid
                && target_frontier.packed_slots_bound
                && bystander_frontier.packed_slots_bound
                && target_frontier.runtime_generation != 0U
                && target_frontier.runtime_generation
                    == target_frontier.bound_runtime_generation
                && bystander_frontier.runtime_generation != 0U
                && bystander_frontier.runtime_generation
                    == bystander_frontier.bound_runtime_generation
                && target_frontier.component != bystander_frontier.component,
            "the force target and bystander must belong to separate certified frontier components");
        require(!target_frontier.invalidated && !bystander_frontier.invalidated,
            "both native components must start with a live frontier runtime");
    }

    NativeRegionAllocationTestAccess::schedule_foreign_cut_task(
        simulation, foreign_process, 2U, capture_foreign_cut);
    const auto stopped = simulation.run(2U);
    NativeRegionAllocationTestAccess::clear_scheduler_trace(simulation);
    result.scheduler_task_at_cut = task_trace.task;
    result.scheduler_task_trace_matches = task_trace.matching_task_ends;
    require(stopped.status == RunStatus::stopped,
        "the foreign scheduler task must stop inside the native frontier window");
    require(result.reached_foreign_cut,
        "the foreign scheduler task must capture the requested time-2 cut");
    require(result.output_events.empty(),
        "the mutation fixture must reach its cut without an output observer");
    const auto has_exact_foreign_key = [&] {
        if (!result.scheduler_task_at_cut) {
            return false;
        }
        const auto& task = *result.scheduler_task_at_cut;
        return task.phase == fsim::runtime::SchedulerPhase::active
            && task.time == 2U
            && task.systemverilog
            && !task.end_of_time_slot
            && task.delta == result.at_cut.signals[0U].delta
            && result.scheduler_task_trace_matches == 1U
            && task.stable_order
                == static_cast<fsim::runtime::StableOrder>(result.foreign_process);
    };
    require(has_exact_foreign_key(),
        "the ordinary stop task must be one exact Active scheduler key at the captured cut");
    result.after_stop = capture_frame();
    require(same_frame(result.at_cut, result.after_stop),
        "stopping at the foreign key must preserve current/LAST/raw and event metadata");
    std::shared_ptr<NativeRegionAllocationTestAccess::FrontierRuntime>
        target_runtime_at_cut;
    std::shared_ptr<NativeRegionAllocationTestAccess::FrontierRuntime>
        bystander_runtime_at_cut;
    if (compiled) {
        target_runtime_at_cut
            = NativeRegionAllocationTestAccess::pin_frontier_for_signal(
                simulation, target_signal);
        bystander_runtime_at_cut
            = NativeRegionAllocationTestAccess::pin_frontier_for_signal(
                simulation, signals[7U]);
        require(target_runtime_at_cut != nullptr
                && bystander_runtime_at_cut != nullptr
                && target_runtime_at_cut.get()
                    == result.cut_frontier.runtime_identity
                && bystander_runtime_at_cut.get()
                    == result.bystander_frontier_at_cut.runtime_identity,
            "the test pins both exact native runtimes present at the foreign cut");
        result.stopped_frontier
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, target_signal);
        require(result.dispatches_at_cut > result.dispatches_before_cut,
            "the V2-only app route must dispatch generated members before the cut");
        require(result.cut_frontier.packed_slots_bound
                && result.bystander_frontier_at_cut.packed_slots_bound,
            "both native A4 bindings remain active at the foreign scheduler cut");
        require_pending_writes_after_cut(
            result.cut_frontier, *result.scheduler_task_at_cut);
        require(result.stopped_frontier == result.cut_frontier,
            "the stopped runtime must retain its exact pending native write descriptors");
    }

    const auto mutation_value = PackedLogic4::from_msb_string(target_value);
    if (mutation == MutationKind::force) {
        simulation.force_signal(target_signal, mutation_value);
    } else {
        simulation.deposit_signal(target_signal, mutation_value);
    }
    result.after_mutation = capture_frame();
    require(result.after_mutation.signals[1U].current == target_value,
        "the public force/deposit API must publish its requested value at the cut");
    if (mutation == MutationKind::force) {
        require(result.after_mutation.signals[1U].force_value == target_value,
            "public force must retain its visible force state");
    }
    if (compiled) {
        result.dispatches_after_mutation
            = NativeRegionAllocationTestAccess::native_frontier_dispatches(
                simulation);
        result.mutated_frontier
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, target_signal);
        result.bystander_frontier_after_mutation
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, signals[7U]);
        require(result.dispatches_after_mutation == result.dispatches_at_cut,
            "the external API mutation must not replay a consumed native body synchronously");
        require(result.mutated_frontier.available
                && !result.mutated_frontier.invalidated
                && result.mutated_frontier.certificate_current
                && result.mutated_frontier.authoritative_state_valid
                && !result.mutated_frontier.packed_slots_bound
                && !result.mutated_frontier.requires_prewrite_unbind
                && result.bystander_frontier_after_mutation.available
                && !result.bystander_frontier_after_mutation.invalidated
                && result.bystander_frontier_after_mutation.certificate_current
                && result.bystander_frontier_after_mutation.authoritative_state_valid
                && result.bystander_frontier_after_mutation.packed_slots_bound
                && result.bystander_frontier_after_mutation.requires_prewrite_unbind,
            "the mutation unbinds only its own A4 roles while retaining both pending runtimes");
        require(result.mutated_frontier.pending_write_count
                    == result.cut_frontier.pending_write_count
                && result.mutated_frontier.pending_writes
                    == result.cut_frontier.pending_writes
                && result.mutated_frontier.runtime_generation
                    == result.cut_frontier.runtime_generation
                && result.mutated_frontier.bound_runtime_generation
                    == result.cut_frontier.bound_runtime_generation,
            "the public mutation must retain pending writes and their original keys");
    }

    simulation.clear_stop();
    require(simulation.run(2U).status == RunStatus::time_limit,
        "same-time resume must finish the pending native and checked suffix work");
    result.after_resume = capture_frame();
    if (compiled) {
        result.dispatches_after_resume
            = NativeRegionAllocationTestAccess::native_frontier_dispatches(
                simulation);
        result.mutated_frontier
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, target_signal);
        result.bystander_frontier_after_resume
            = NativeRegionAllocationTestAccess::frontier_for_signal(
                simulation, signals[7U]);
        const auto bystander_native_delta
            = bystander_runtime_at_cut->native_member_dispatches
            - result.bystander_frontier_at_cut.native_member_dispatches;
        require(target_runtime_at_cut->native_member_dispatches
                    == result.cut_frontier.native_member_dispatches
                && bystander_native_delta != 0U
                && result.dispatches_after_resume
                    - result.dispatches_after_mutation
                    == bystander_native_delta
                && result.bystander_frontier_after_resume.available
                && !result.bystander_frontier_after_resume.invalidated
                && result.bystander_frontier_after_resume.certificate_current
                && result.bystander_frontier_after_resume.authoritative_state_valid
                && result.bystander_frontier_after_resume.packed_slots_bound,
            "only the pinned bystander V2 runtime dispatches native members on resume");
        require_pending_writes_delivered(
            result.cut_frontier, result.after_resume, signals[3U]);
        require(result.after_resume.signals[7U].current
                    == result.after_resume.signals[6U].current
                && result.after_resume.signals[8U].current
                    == result.after_resume.signals[7U].current,
            "the unaffected native component must finish its queued source transition");
    }
    return result;
}

void require_same_frames(
    const RouteResult& compiled,
    const RouteResult& interpreted)
{
    require(compiled.reached_foreign_cut && interpreted.reached_foreign_cut,
        "both execution routes must reach the same foreign scheduler cut");
    require(compiled.output_events == interpreted.output_events,
        "the foreign callback identity and scheduler stamp must match the interpreter");
    require(compiled.scheduler_task_at_cut && interpreted.scheduler_task_at_cut
            && compiled.foreign_process == interpreted.foreign_process
            && compiled.scheduler_task_at_cut->stable_order
                == interpreted.scheduler_task_at_cut->stable_order
            && compiled.scheduler_task_at_cut->time
                == interpreted.scheduler_task_at_cut->time
            && compiled.scheduler_task_at_cut->delta
                == interpreted.scheduler_task_at_cut->delta
            && compiled.scheduler_task_at_cut->sequence
                == interpreted.scheduler_task_at_cut->sequence
            && compiled.scheduler_task_at_cut->phase
                == interpreted.scheduler_task_at_cut->phase
            && compiled.scheduler_task_at_cut->systemverilog
                == interpreted.scheduler_task_at_cut->systemverilog
            && compiled.scheduler_task_at_cut->systemverilog_round
                == interpreted.scheduler_task_at_cut->systemverilog_round,
        "compiled and interpreter runs must stop at the same foreign full-key time-slot cut");
    require(same_frame(compiled.initial, interpreted.initial)
            && same_frame(compiled.at_cut, interpreted.at_cut)
            && same_frame(compiled.after_stop, interpreted.after_stop)
            && same_frame(compiled.after_mutation, interpreted.after_mutation)
            && same_frame(compiled.after_resume, interpreted.after_resume),
        "O0/O2 mutations must match interpreter values, drivers, and stamps");
}

void test_frontier_force_and_deposit_at_foreign_cut()
{
    // libc++'s file clock counts in __int128, which to_string does not take.
    const auto nonce = static_cast<long long>(
        std::filesystem::file_time_type::clock::now()
            .time_since_epoch().count());
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-force-deposit-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto optimization_name
            = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
        for (const auto mutation : { MutationKind::force, MutationKind::deposit }) {
            const auto mutation_name
                = mutation == MutationKind::force ? "force" : "deposit";
            const auto compiled_root
                = root.path / optimization_name / mutation_name / "compiled";
            const auto interpreter_root
                = root.path / optimization_name / mutation_name / "interpreter";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(interpreter_root);

            const auto compiled = run_mutation_route(optimization,
                compiled_root, SimulationEngine::compiled, mutation);
            const auto interpreted = run_mutation_route(optimization,
                interpreter_root, SimulationEngine::interpreter, mutation);
            require_same_frames(compiled, interpreted);
        }
    }
}

} // namespace

int main()
{
    try {
        test_frontier_force_and_deposit_at_foreign_cut();
        std::cout << "native frontier force/deposit foreign-cut tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "native frontier force/deposit foreign-cut tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
