// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "native_frontier_v2_only_provider.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
    using Runtime = Interpreter::Impl::RegionFrontierComponentRuntime;

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

    struct BoundaryWriter {
        std::shared_ptr<Runtime> runtime;
        std::uint32_t member_index { UINT32_MAX };
        std::uint32_t pending_slot { UINT32_MAX };
        ProcessId process { UINT32_MAX };
    };

    struct CutSnapshot {
        std::shared_ptr<Runtime> runtime_pin;
        Key producer_origin;
        Key boundary_commit;
        std::array<ProcessId, 64U> component_processes { };
        std::size_t component_process_count { };
        std::uint32_t pending_slot { UINT32_MAX };
        std::uint64_t runtime_dispatches { };
        std::uint64_t profile_dispatches { };
        SimulationTick stopped_at_time { };
        std::uint64_t stopped_at_delta { };
        std::uint64_t stopped_at_round { };
        SchedulerPhase stopped_after_phase { SchedulerPhase::active };
        bool entered { };
    };

    struct PendingSnapshot {
        std::uint32_t flags { };
        Key origin;
        Key commit;
    };

    [[nodiscard]] static Key copy_key(
        const RegionFrontierKeyV1& key) noexcept
    {
        return { key.time, key.delta, key.systemverilog_round,
            key.stable_order, key.sequence, key.process_domain, key.phase };
    }

    [[nodiscard]] static BoundaryWriter boundary_writer_for_signal(
        fsim::app::Simulation& simulation, const SignalId signal)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the application has no interpreter for its frontier"
            };
        }
        auto& state = *application.interpreter->impl_;
        BoundaryWriter result;
        std::uint32_t internal_write_count { };
        bool outside_reader_found { };
        for (const auto& runtime : state.region_frontier_runtime_by_component) {
            if (!runtime || !runtime->backend || !runtime->backend->executor) {
                continue;
            }
            const auto& layout = runtime->backend->executor->layout();
            for (std::uint32_t site_index = 0U;
                 site_index < layout.write_site_count; ++site_index) {
                const auto& site = layout.write_sites[site_index];
                if (site.event_kind != static_cast<std::uint32_t>(
                        RegionFrontierEventKindV1::boundary_commit)
                    || site.signal_slot >= layout.signal_slot_count
                    || layout.signals[site.signal_slot].signal_id != signal) {
                    continue;
                }
                if (result.runtime) {
                    throw std::logic_error {
                        "the selected sink has more than one native boundary writer"
                    };
                }
                if (site.member_index >= layout.member_count
                    || site.pending_slot >= runtime->pending_writes.size()
                    || site.member_index >= runtime->members.size()) {
                    throw std::logic_error {
                        "the sink boundary writer has an invalid compiled descriptor"
                    };
                }
                const auto process = static_cast<ProcessId>(
                    layout.members[site.member_index].process_id);
                if (runtime->members[site.member_index].process_id != process) {
                    throw std::logic_error {
                        "the boundary writer disagrees with the runtime member map"
                    };
                }
                result.runtime = runtime;
                result.member_index = site.member_index;
                result.pending_slot = site.pending_slot;
                result.process = process;
            }
        }
        if (result.runtime && result.runtime->backend
            && result.runtime->backend->executor) {
            const auto& layout = result.runtime->backend->executor->layout();
            for (std::uint32_t site_index = 0U;
                 site_index < layout.write_site_count; ++site_index) {
                internal_write_count += layout.write_sites[site_index].event_kind
                    == static_cast<std::uint32_t>(
                        RegionFrontierEventKindV1::internal_commit);
            }
            for (ProcessId candidate = 0U;
                 candidate < state.processes.size(); ++candidate) {
                const auto& process = state.processes.public_program(candidate);
                bool observes_sink { };
                for (const auto& sensitivity : process.static_sensitivity) {
                    observes_sink = observes_sink || sensitivity.signal == signal;
                }
                if (!observes_sink) {
                    continue;
                }
                bool is_component_member { };
                for (std::uint32_t member = 0U;
                     member < layout.member_count; ++member) {
                    is_component_member = is_component_member
                        || layout.members[member].process_id == candidate;
                }
                if (!is_component_member) {
                    outside_reader_found = true;
                }
            }
        }
        if (!result.runtime || internal_write_count < 3U
            || !outside_reader_found) {
            throw std::logic_error {
                "the V2-only test provider did not prepare the internal chain, "
                "boundary writer, and outside sink reader"
            };
        }
        return result;
    }

    [[nodiscard]] static PendingSnapshot pending_snapshot(
        const CutSnapshot& cut)
    {
        if (!cut.runtime_pin
            || cut.pending_slot >= cut.runtime_pin->pending_writes.size()) {
            throw std::logic_error {
                "the retained ticket lost its runtime-owned pending descriptor"
            };
        }
        const auto& write = cut.runtime_pin->pending_writes[cut.pending_slot];
        return { write.flags, copy_key(write.origin), copy_key(write.commit_key) };
    }

    [[nodiscard]] static std::size_t active_pending_descriptor_count(
        const Runtime& runtime) noexcept
    {
        std::size_t active_count { };
        for (const auto& write : runtime.pending_writes) {
            if ((write.flags & pending_active) != 0U) {
                ++active_count;
            }
        }
        return active_count;
    }

    [[nodiscard]] static bool runtime_still_owned_by_component_map(
        fsim::app::Simulation& simulation, const CutSnapshot& cut)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter || !cut.runtime_pin) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        const auto component = cut.runtime_pin->component;
        return component < state.region_frontier_runtime_by_component.size()
            && state.region_frontier_runtime_by_component[component]
                == cut.runtime_pin;
    }

    [[nodiscard]] static bool authoritative_slots_bound(
        const CutSnapshot& cut) noexcept
    {
        return cut.runtime_pin && cut.runtime_pin->authoritative_state
            && cut.runtime_pin->authoritative_state->values()
                .packed_slots_bound();
    }

    [[nodiscard]] static std::uint64_t stop_after_native_boundary_stage(
        fsim::app::Simulation& simulation,
        const BoundaryWriter& writer,
        CutSnapshot& cut,
        const std::uint64_t baseline_dispatches)
    {
        if (!writer.runtime
            || writer.member_index >= writer.runtime->members.size()
            || writer.pending_slot >= writer.runtime->pending_writes.size()) {
            throw std::logic_error {
                "the timed source has no valid native boundary descriptor"
            };
        }
        const auto runtime = writer.runtime;
        return simulation.add_safe_point_hook(
            [runtime, writer, &cut, baseline_dispatches](
                Scheduler& scheduler, const SchedulerPhase phase) {
                if (scheduler.now() != 1U
                    || phase != SchedulerPhase::active
                    || runtime->owner == nullptr
                    || runtime->native_member_dispatches == 0U
                    || runtime->owner
                               ->systemverilog_wave_profile_native_frontier_member_dispatches
                        <= baseline_dispatches
                    || writer.pending_slot >= runtime->pending_writes.size()
                    || active_pending_descriptor_count(*runtime) == 0U
                    || runtime->frame.pending_write_capacity
                        > runtime->pending_writes.size()
                    || runtime->frame.pending_write_count
                        > runtime->frame.pending_write_capacity
                    || writer.pending_slot
                        >= runtime->frame.pending_write_capacity) {
                    return;
                }

                const auto& pending
                    = runtime->pending_writes[writer.pending_slot];
                if ((pending.flags
                        & (pending_active | pending_boundary_target))
                        != (pending_active | pending_boundary_target)
                    || (pending.flags & pending_committed) != 0U
                    || (pending.flags & pending_key_assigned) == 0U) {
                    return;
                }

                cut.entered = true;
                cut.runtime_pin = runtime;
                cut.pending_slot = writer.pending_slot;
                cut.runtime_dispatches = runtime->native_member_dispatches;
                cut.profile_dispatches = runtime->owner
                    ->systemverilog_wave_profile_native_frontier_member_dispatches;
                cut.stopped_at_time = scheduler.now();
                cut.stopped_at_delta = scheduler.delta();
                cut.stopped_at_round = scheduler.systemverilog_round();
                cut.stopped_after_phase = phase;
                cut.producer_origin = copy_key(
                    runtime->members[writer.member_index].activation_origin);
                if (runtime->members.size() > cut.component_processes.size()) {
                    throw std::logic_error {
                        "the retained-frontier fixture exceeded its small component bound"
                    };
                }
                cut.component_process_count = runtime->members.size();
                for (std::size_t index = 0U;
                     index < cut.component_process_count; ++index) {
                    cut.component_processes[index]
                        = runtime->members[index].process_id;
                }
                cut.boundary_commit = copy_key(pending.commit_key);
                scheduler.request_stop();
            });
    }

    [[nodiscard]] static bool scheduler_ticket_pins_runtime(
        fsim::app::Simulation& simulation,
        BoundaryWriter& writer,
        CutSnapshot& cut)
    {
        if (!simulation.impl_ || !simulation.impl_->interpreter
            || !writer.runtime || !cut.runtime_pin
            || writer.runtime != cut.runtime_pin) {
            return false;
        }
        auto& runtime_map = simulation.impl_->interpreter->impl_
                                ->region_frontier_runtime_by_component;
        auto* const raw_runtime = cut.runtime_pin.get();
        const auto component = raw_runtime->component;
        if (component >= runtime_map.size()
            || runtime_map[component].get() != raw_runtime) {
            return false;
        }
        const auto map_aliases = std::ranges::count_if(
            runtime_map, [raw_runtime](const auto& runtime) {
                return runtime.get() == raw_runtime;
            });
        if (map_aliases != 1) {
            return false;
        }

        const std::weak_ptr<Runtime> ticket_lifetime { cut.runtime_pin };
        writer.runtime.reset();
        cut.runtime_pin.reset();
        runtime_map[component].reset();

        auto retained = ticket_lifetime.lock();
        if (!retained || retained.get() != raw_runtime
            || cut.pending_slot >= retained->pending_writes.size()
            || component != retained->component) {
            return false;
        }
        const auto& retained_write
            = retained->pending_writes[cut.pending_slot];
        const bool selected_write_retained
            = (retained_write.flags
                  & (pending_active | pending_boundary_target
                      | pending_key_assigned))
                == (pending_active | pending_boundary_target
                    | pending_key_assigned)
            && (retained_write.flags & pending_committed) == 0U
            && copy_key(retained_write.origin) == cut.producer_origin
            && copy_key(retained_write.commit_key) == cut.boundary_commit;
        const auto active_count = active_pending_descriptor_count(*retained);
        runtime_map[component] = retained;
        cut.runtime_pin = std::move(retained);
        return selected_write_retained && active_count != 0U;
    }

    static void set_trace_hook(fsim::app::Simulation& simulation,
        void* context, fsim::runtime::Scheduler::TraceHook hook) noexcept
    {
        if (simulation.impl_ && simulation.impl_->interpreter) {
            simulation.impl_->interpreter->scheduler().set_trace_hook(
                context, hook);
        }
    }

    static void set_driver_change_hook(fsim::app::Simulation& simulation,
        Interpreter::DriverChangeHook hook)
    {
        auto& application = *simulation.impl_;
        if (application.interpreter) {
            application.interpreter->set_driver_change_hook(std::move(hook));
        }
    }

    [[nodiscard]] static std::uint64_t profile_dispatches(
        const fsim::app::Simulation& simulation) noexcept
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
using fsim::runtime::Scheduler;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::SchedulerTraceKind;
using fsim::runtime::SchedulerTraceRecord;
using fsim::runtime::SimulationTick;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using Key = NativeRegionAllocationTestAccess::Key;
using fsim::runtime::simir::SignalId;

constexpr std::string_view outside_observer_text
    = "NATIVE_FRONTIER_OUTSIDE_OBSERVER";

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error {
                "failed to configure native frontier test environment"
            };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
            static_cast<void>(set(previous_.c_str()));
        } else {
            static_cast<void>(set(nullptr));
        }
    }

private:
    [[nodiscard]] bool set(const char* const value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        return value == nullptr
            ? ::unsetenv(name_.c_str()) == 0
            : ::setenv(name_.c_str(), value, 1) == 0;
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

struct TemporaryDirectory {
    explicit TemporaryDirectory(const std::string_view suffix)
    {
        const auto ticks = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("fsim-native-frontier-retained-"
                + std::to_string(ticks) + "-" + std::string { suffix });
        std::filesystem::create_directories(path);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

[[nodiscard]] fsim::project::Config make_config(
    const std::filesystem::path& root,
    const fsim::project::Optimization optimization)
{
    const auto source = root / "native_frontier_retained.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_frontier_retained(output wire sink);
  logic source;
  wire stage0;
  wire stage1;
  wire stage2;

  assign stage0 = source;
  assign stage1 = stage0;
  assign stage2 = stage1;
  assign sink = stage2;

  // This side-effecting reader stays outside the certified assignment cone.
  always @(sink)
    $display("NATIVE_FRONTIER_OUTSIDE_OBSERVER");

  initial begin
    source = 1'b0;
    #1 source = 1'b1;
    #99;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the app-level native frontier source must be written completely");

    fsim::project::Config config;
    config.project.name = "native-frontier-retained-stop-resume";
    config.project.top = "sv:work.native_frontier_retained";
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

[[nodiscard]] bool same_task_key(
    const SchedulerTraceRecord& record, const Key& key) noexcept
{
    return record.systemverilog && record.phase
        && record.time == key.time && record.delta == key.delta
        && record.systemverilog_round == key.systemverilog_round
        && record.order == key.stable_order
        && record.sequence == key.sequence
        && static_cast<std::uint32_t>(*record.phase) == key.phase;
}

struct TraceProbe {
    Key producer_origin;
    Key boundary_commit;
    std::array<fsim::runtime::simir::ProcessId, 64U>
        component_processes { };
    std::size_t component_process_count { };
    fsim::runtime::RuntimeSignalId boundary_signal { };
    std::uint64_t producer_begins { };
    std::uint64_t producer_ends { };
    std::uint64_t boundary_begins { };
    std::uint64_t boundary_ends { };
    std::uint64_t replayed_component_begins { };
    std::uint64_t boundary_transactions { };

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<TraceProbe*>(context);
        if (record.kind == SchedulerTraceKind::signal_transaction
            && record.signal == probe.boundary_signal) {
            ++probe.boundary_transactions;
        }
        if (same_task_key(record, probe.producer_origin)) {
            if (record.kind == SchedulerTraceKind::task_begin) {
                ++probe.producer_begins;
            } else if (record.kind == SchedulerTraceKind::task_end) {
                ++probe.producer_ends;
            }
        }
        if (same_task_key(record, probe.boundary_commit)) {
            if (record.kind == SchedulerTraceKind::task_begin) {
                ++probe.boundary_begins;
            } else if (record.kind == SchedulerTraceKind::task_end) {
                ++probe.boundary_ends;
            }
            return;
        }
        if (record.kind == SchedulerTraceKind::task_begin
            && record.systemverilog) {
            for (std::size_t index = 0U;
                 index < probe.component_process_count; ++index) {
                if (record.order == probe.component_processes[index]) {
                    ++probe.replayed_component_begins;
                    break;
                }
            }
        }
    }
};

struct AppCaseResult {
    std::uint64_t native_dispatches_before { };
    std::uint64_t native_dispatches_at_cut { };
    std::uint64_t native_dispatches_after { };
    std::size_t outside_observer_calls { };
    std::size_t signal_observer_calls { };
    std::size_t raw_driver_publications { };
    TraceProbe trace;
};

void run_retained_boundary_case(
    const fsim::project::Optimization optimization,
    const bool stop_in_publication_observer,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the retained-frontier app design must parse and elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the real app route must install compiled process executors");

    const auto source = simulation.find_signal(
        "native_frontier_retained.source");
    const auto sink = simulation.find_signal(
        "native_frontier_retained.sink");
    require(source.has_value() && sink.has_value(),
        "the app route must retain source and sink signal handles");

    AppCaseResult result;
    simulation.set_output_hook(
        [&result](const fsim::runtime::simir::ProcessId,
            const std::string_view text, const bool,
            const SimulationTick, const std::uint64_t) {
            if (text == outside_observer_text) {
                ++result.outside_observer_calls;
            }
        });

    // Keep this regression focused on retained V2 pending-write descriptors.
    // Delegate activation and frontier construction to LLVM, but suppress its
    // optional flattened forwarding capability; the separate A2 witness
    // covers the default flattened route.
    NativeRegionAllocationTestAccess::install_v2_frontier_only_provider(
        simulation);
    simulation.start();
    require(simulation.run(0U).status == RunStatus::time_limit,
        "time-zero startup must warm topology before the timed Active source");
    result.outside_observer_calls = 0U;
    auto writer
        = NativeRegionAllocationTestAccess::boundary_writer_for_signal(
            simulation, *sink);
    result.native_dispatches_before
        = NativeRegionAllocationTestAccess::profile_dispatches(simulation);

    NativeRegionAllocationTestAccess::CutSnapshot cut;
    const auto cut_hook
        = NativeRegionAllocationTestAccess::stop_after_native_boundary_stage(
            simulation, writer, cut, result.native_dispatches_before);
    const auto stopped = simulation.run(1U);
    simulation.remove_safe_point_hook(cut_hook);
    require(stopped.status == RunStatus::stopped && cut.entered
            && cut.stopped_at_time == 1U
            && cut.stopped_after_phase == SchedulerPhase::active,
        "the timed HDL Active transition must stop at the safe point after native staging");
    require(cut.runtime_pin != nullptr
            && cut.runtime_dispatches != 0U
            && cut.profile_dispatches > result.native_dispatches_before,
        "the delegated LLVM V2 entry must dispatch generated members before the cut");
    result.native_dispatches_at_cut = cut.profile_dispatches;
    require(cut.producer_origin.sequence != 0U
            && cut.boundary_commit.sequence != 0U
            && cut.producer_origin != cut.boundary_commit
            && cut.producer_origin.time == 1U
            && cut.boundary_commit.time == 1U
            && cut.boundary_commit.delta == cut.stopped_at_delta
            && cut.boundary_commit.systemverilog_round
                == cut.stopped_at_round + 1U
            && cut.producer_origin.process_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && cut.producer_origin.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && cut.boundary_commit.stable_order == writer.process
            && cut.boundary_commit.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active),
        "the timed SV writer must retain distinct original causal and boundary Active keys");

    auto pending = NativeRegionAllocationTestAccess::pending_snapshot(cut);
    require((pending.flags & (fsim::runtime::simir::pending_active
                    | fsim::runtime::simir::pending_boundary_target
                    | fsim::runtime::simir::pending_key_assigned))
                == (fsim::runtime::simir::pending_active
                    | fsim::runtime::simir::pending_boundary_target
                    | fsim::runtime::simir::pending_key_assigned)
            && (pending.flags & fsim::runtime::simir::pending_committed) == 0U
            && pending.origin == cut.producer_origin
            && pending.commit == cut.boundary_commit
            && NativeRegionAllocationTestAccess::active_pending_descriptor_count(
                   *cut.runtime_pin)
                != 0U,
        "the selected boundary descriptor must remain active and uncommitted at its original key");
    require(NativeRegionAllocationTestAccess::scheduler_ticket_pins_runtime(
                simulation, writer, cut),
        "the unconsumed scheduler work must retain the runtime after fixture pins are released");
    require(NativeRegionAllocationTestAccess::authoritative_slots_bound(cut),
        "the generated app route must have live A4 role slots before late observation");

    simulation.set_signal_change_hook(
        [&simulation, &result, sink = *sink,
            stop_in_publication_observer](
            const SignalId signal, const PackedLogic4& value,
            const SimulationTick, const std::uint64_t) {
            if (signal != sink
                || value != PackedLogic4(1U, fsim::runtime::Logic4::one)) {
                return;
            }
            ++result.signal_observer_calls;
            if (stop_in_publication_observer) {
                simulation.request_stop();
            }
        });
    NativeRegionAllocationTestAccess::set_driver_change_hook(simulation,
        [&result, sink = *sink, writer = writer.process](
            const fsim::runtime::simir::ProcessId process,
            const SignalId signal, const SimulationTick) {
            if (process == writer && signal == sink) {
                ++result.raw_driver_publications;
            }
        });
    const auto pending_after_late_observation
        = NativeRegionAllocationTestAccess::pending_snapshot(cut);
    const auto active_before_ack
        = NativeRegionAllocationTestAccess::active_pending_descriptor_count(
            *cut.runtime_pin);
    require(NativeRegionAllocationTestAccess::runtime_still_owned_by_component_map(
                simulation, cut)
            && !NativeRegionAllocationTestAccess::authoritative_slots_bound(cut)
            && active_before_ack != 0U
            && (pending_after_late_observation.flags
                    & (fsim::runtime::simir::pending_active
                        | fsim::runtime::simir::pending_boundary_target
                        | fsim::runtime::simir::pending_key_assigned))
                == (fsim::runtime::simir::pending_active
                    | fsim::runtime::simir::pending_boundary_target
                    | fsim::runtime::simir::pending_key_assigned)
            && (pending_after_late_observation.flags
                    & fsim::runtime::simir::pending_committed)
                == 0U
            && pending_after_late_observation.origin == cut.producer_origin
            && pending_after_late_observation.commit == cut.boundary_commit
            && result.signal_observer_calls == 0U
            && result.raw_driver_publications == 0U,
        "late observation must retain the exact active, uncommitted descriptor and original pending key");

    result.trace.producer_origin = cut.producer_origin;
    result.trace.boundary_commit = cut.boundary_commit;
    result.trace.component_processes = cut.component_processes;
    result.trace.component_process_count = cut.component_process_count;
    result.trace.boundary_signal = static_cast<fsim::runtime::RuntimeSignalId>(
        *sink);
    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, &result.trace, &TraceProbe::receive);
    simulation.clear_stop();

    const auto first_resume = simulation.run();
    if (stop_in_publication_observer) {
        require(first_resume.status == RunStatus::stopped
                && first_resume.time == 1U,
            "the boundary observer must stop from inside checked publication");
    } else {
        require(first_resume.status == RunStatus::stopped
                && first_resume.time == 100U,
            "fallback must publish the retained boundary key and finish the app");
    }

    require(result.signal_observer_calls == 1U
            && result.raw_driver_publications == 1U
            && result.trace.boundary_transactions == 1U
            && result.trace.producer_begins == 0U
            && result.trace.producer_ends == 0U
            && result.trace.replayed_component_begins == 0U
            && result.trace.boundary_begins == 1U
            && result.trace.boundary_ends == 1U,
        "resume must publish the exact boundary key once without replaying its consumed producer");
    require(NativeRegionAllocationTestAccess::profile_dispatches(simulation)
                == result.native_dispatches_at_cut,
        "the observation-demoted component must not enter generated member bodies again");

    const auto retired
        = NativeRegionAllocationTestAccess::pending_snapshot(cut);
    const auto active_after_ack
        = NativeRegionAllocationTestAccess::active_pending_descriptor_count(
            *cut.runtime_pin);
    require((retired.flags & fsim::runtime::simir::pending_active) == 0U
            && (retired.flags & fsim::runtime::simir::pending_committed) != 0U
            && retired.origin == cut.producer_origin
            && retired.commit == cut.boundary_commit
            && active_before_ack != 0U
            && (active_before_ack == 1U
                ? active_after_ack == 0U
                : active_after_ack < active_before_ack),
        "checked publication must ACK the exact selected descriptor and leave no active descriptor when no unrelated work remains");

    if (stop_in_publication_observer) {
        require(result.outside_observer_calls == 0U,
            "stop from the boundary observer must precede its outside consumer callback");
        simulation.clear_stop();
        const auto final_resume = simulation.run();
        require(final_resume.status == RunStatus::stopped
                && final_resume.time == 100U,
            "resume after the publication ACK must drain the outside consumer");
    }

    NativeRegionAllocationTestAccess::set_trace_hook(
        simulation, nullptr, nullptr);
    require(result.outside_observer_calls == 1U
            && result.signal_observer_calls == 1U
            && result.raw_driver_publications == 1U
            && result.trace.boundary_transactions == 1U
            && result.trace.boundary_begins == 1U
            && result.trace.boundary_ends == 1U
            && result.trace.producer_begins == 0U
            && result.trace.producer_ends == 0U
            && result.trace.replayed_component_begins == 0U,
        "later resume must not replay the producer or republish an ACKed boundary");
    require(simulation.read_signal(*sink)
                == PackedLogic4(1U, fsim::runtime::Logic4::one),
        "the retained checked boundary publication must reach the public app signal");
    result.native_dispatches_after
        = NativeRegionAllocationTestAccess::profile_dispatches(simulation);
    require(result.native_dispatches_after == result.native_dispatches_at_cut,
        "resuming an observed retained ticket must never rerun generated members");
}

} // namespace

int main()
{
    try {
        ScopedEnvironment region_kernel {
            "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave {
            "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile {
            "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment jit_profile {
            "FSIM_PROFILE_JIT", nullptr };

        for (const auto optimization : {
                 fsim::project::Optimization::o0,
                 fsim::project::Optimization::o2 }) {
            const auto suffix
                = optimization == fsim::project::Optimization::o0
                ? "o0" : "o2";
            run_retained_boundary_case(optimization, false,
                std::string { suffix } + "-uncommitted");
            run_retained_boundary_case(optimization, true,
                std::string { suffix } + "-observer-stop");
        }
    } catch (const std::exception& error) {
        std::cerr << "native frontier retained app lifecycle test failed: "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "native frontier retained app lifecycle test passed\n";
    return 0;
}
