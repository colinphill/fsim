// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "native_frontier_v2_only_provider.hpp"
#include "application_simulation_internal.hpp"
#include "simir_internal.hpp"
#include "simir_region_frontier_trusted_entry.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

/// Test-only access to the same startup snapshot builder used by the V2
/// frontier adapter. This target owns the sole definition of this friend type.
struct NativeRegionAllocationTestAccess {
    struct SchedulerSnapshot {
        SimulationTick now { };
        std::uint64_t delta { };
        std::uint64_t round { };
        std::uint64_t phase_revision { };
        std::optional<SimulationTick> next_time;
        std::optional<SchedulerPhase> phase;
        std::array<std::uint64_t, 9U> batch_stats { };
        bool pending { };
        bool stopped { };

        friend bool operator==(
            const SchedulerSnapshot&, const SchedulerSnapshot&) = default;
    };

    struct RuntimeSnapshot {
        std::uintptr_t runtime { };
        std::uintptr_t backend { };
        std::uintptr_t executor { };
        std::uintptr_t workspace_begin { };
        std::size_t workspace_capacity { };
        std::size_t workspace_used { };
        std::uint64_t native_dispatches { };
        std::array<std::uint64_t, 5U> runtime_control { };
        std::vector<std::byte> state_bytes;

        friend bool operator==(
            const RuntimeSnapshot&, const RuntimeSnapshot&) = default;
    };

    struct PublishedSnapshot {
        std::uint64_t generation { };
        std::vector<std::uintptr_t> frontier_runtime_by_component;
        std::vector<RuntimeSnapshot> runtimes;

        friend bool operator==(
            const PublishedSnapshot&, const PublishedSnapshot&) = default;
    };

    struct RuntimeSummary {
        std::size_t frontier_runtime_count { };
        std::size_t frontier_backend_count { };
        std::uint64_t native_dispatches { };
    };

    struct AliasCertificateSummary {
        std::size_t capable_runtime_count { };
        std::size_t storage_available_count { };
        std::size_t storage_unavailable_count { };
        std::size_t empty_unavailable_count { };
        std::size_t partial_unavailable_count { };
        std::size_t unavailable_component {
            std::numeric_limits<std::size_t>::max() };
        std::uint64_t checked_entries { };
        std::uint64_t trusted_entries { };
        std::uint64_t unavailable_checked_entries { };
        std::uint64_t unavailable_trusted_entries { };
    };

    struct RetainedPlaneLease {
        PackedLogic4PlaneReadLease lease;
        std::array<std::vector<std::uint64_t>, 4U> words;
    };

    struct RetainedA4Candidate {
        std::weak_ptr<RegionAuthoritativeComponentState> state;
        std::weak_ptr<void> workspace;
        std::vector<RetainedPlaneLease> leases;
        std::array<RetainedPlaneLease, 4U> selected_role_leases;
        std::array<PackedLogic4, 4U> old_values;
        PackedLogic4 new_value;
        std::size_t signal_width { };
    };

    using WorkspaceExtent = std::array<std::uintptr_t, 4U>;

    static void check_workspace_typed_destruction()
    {
        struct DestructionProbe final {
            explicit DestructionProbe(std::size_t& count) noexcept
                : count_(&count)
            {
            }

            ~DestructionProbe()
            {
                ++*count_;
            }

            std::size_t* count_ { };
        };

        using AllocationState
            = Interpreter::Impl::RegionFrontierWorkspaceAllocationState;
        using ProbeAllocator
            = Interpreter::Impl::RegionFrontierWorkspaceAllocator<
                DestructionProbe>;
        AllocationState allocation;
        const auto capacity = sizeof(DestructionProbe)
            + alignof(DestructionProbe) - 1U;
        std::size_t destructions { };
        {
            std::vector<DestructionProbe, ProbeAllocator> probes {
                ProbeAllocator { allocation } };
            probes.emplace_back(destructions);
        }
        if (destructions != 1U || !allocation.configure(capacity)) {
            throw std::runtime_error {
                "the workspace must configure after fallback storage is released"
            };
        }
        {
            std::vector<DestructionProbe, ProbeAllocator> probes {
                ProbeAllocator { allocation } };
            probes.emplace_back(destructions);
            if (probes.size() != 1U
                || !allocation.owns(probes.data(), sizeof(DestructionProbe))) {
                throw std::runtime_error {
                    "the typed workspace lifetime fixture must use its arena"
                };
            }
        }
        if (destructions != 2U) {
            throw std::runtime_error {
                "typed frame objects must be destroyed before the arena"
            };
        }
    }

    static void install_v2_frontier_only_provider(
        fsim::app::Simulation& simulation)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the preparation test requires an interpreter"
            };
        }
        auto& interpreter = *application.interpreter;
        auto provider
            = interpreter.impl_->region_kernel_backend_provider;
        interpreter.set_region_kernel_backend_provider(
            std::make_shared<test::V2FrontierOnlyRegionKernelBackendProvider>(
                std::move(provider)));
    }

    [[nodiscard]] static Interpreter::Impl& interpreter_impl(
        fsim::app::Simulation& simulation)
    {
        auto& app = *simulation.impl_;
        if (!app.interpreter) {
            throw std::logic_error {
                "the preparation test requires an interpreter"
            };
        }
        return *app.interpreter->impl_;
    }

    [[nodiscard]] static RuntimeSummary summarize(
        fsim::app::Simulation& simulation)
    {
        const auto& impl = interpreter_impl(simulation);
        RuntimeSummary result;
        result.frontier_runtime_count = 0U;
        result.frontier_backend_count = 0U;
        for (const auto& runtime : impl.region_frontier_runtime_by_component) {
            if (runtime) {
                ++result.frontier_runtime_count;
                result.native_dispatches += runtime->native_member_dispatches;
            }
        }
        for (const auto& backend : impl.region_frontier_backends_by_component) {
            if (backend) {
                ++result.frontier_backend_count;
            }
        }
        return result;
    }

    [[nodiscard]] static std::vector<WorkspaceExtent> workspace_extents(
        fsim::app::Simulation& simulation)
    {
        const auto& impl = interpreter_impl(simulation);
        std::vector<WorkspaceExtent> result;
        result.reserve(impl.region_frontier_runtime_by_component.size());
        for (const auto& runtime : impl.region_frontier_runtime_by_component) {
            if (!runtime) {
                continue;
            }
            result.push_back({
                reinterpret_cast<std::uintptr_t>(runtime.get()),
                reinterpret_cast<std::uintptr_t>(
                    runtime->workspace_allocation.begin()),
                static_cast<std::uintptr_t>(
                    runtime->workspace_allocation.capacity()),
                static_cast<std::uintptr_t>(
                    runtime->workspace_allocation.used())
            });
        }
        return result;
    }

    [[nodiscard]] static PackedLogic4 current_signal_value_passive(
        fsim::app::Simulation& simulation,
        const SignalId signal)
    {
        // Keep fixture verification from crossing the public observation
        // barrier before the retry has exercised the generated V2 frontier.
        return interpreter_impl(simulation).signals.at(signal).initial_value;
    }

    [[nodiscard]] static SchedulerSnapshot scheduler_snapshot(
        fsim::app::Simulation& simulation)
    {
        const auto& scheduler = interpreter_impl(simulation).scheduler;
        const auto batch_stats
            = scheduler.systemverilog_batch_compaction_stats();
        return { scheduler.now(), scheduler.delta(),
            scheduler.systemverilog_round(), scheduler.current_phase_revision(),
            scheduler.next_pending_time(), scheduler.current_phase(),
            { batch_stats.tickets, batch_stats.members,
                batch_stats.entries_elided, batch_stats.direct_dispatches,
                batch_stats.direct_members,
                batch_stats.readiness_ticket_queue_insertions,
                batch_stats.readiness_ticket_members,
                batch_stats.readiness_ticket_members_elided,
                batch_stats.readiness_ticket_fallback_members },
            scheduler.has_pending(), scheduler.stop_requested() };
    }

    [[nodiscard]] static std::uint64_t reserve_sequence_probe(
        fsim::app::Simulation& simulation)
    {
        auto& scheduler = interpreter_impl(simulation).scheduler;
        return scheduler.reserve_order_key(
            std::numeric_limits<StableOrder>::max()).sequence;
    }

    template <typename T>
    static void append_object_bytes(
        std::vector<std::byte>& destination, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* const first
            = reinterpret_cast<const std::byte*>(&value);
        destination.insert(destination.end(), first, first + sizeof(T));
    }

    template <typename T>
    static void append_span_bytes(
        std::vector<std::byte>& destination,
        const T* const data, const std::size_t count)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        append_object_bytes(destination, count);
        if (count != 0U) {
            const auto* const first
                = reinterpret_cast<const std::byte*>(data);
            destination.insert(destination.end(), first,
                first + count * sizeof(T));
        }
    }

    template <typename T, typename Allocator>
    static void append_vector_state(
        std::vector<std::byte>& destination,
        const std::vector<T, Allocator>& values)
    {
        const auto data = reinterpret_cast<std::uintptr_t>(values.data());
        append_object_bytes(destination, data);
        append_object_bytes(destination, values.capacity());
        if constexpr (std::is_trivially_copyable_v<T>) {
            append_span_bytes(destination, values.data(), values.size());
        } else {
            append_object_bytes(destination, values.size());
        }
    }

    struct WorkspaceRange {
        std::uintptr_t begin { };
        std::uintptr_t end { };
    };

    template <typename T, typename Allocator>
    static void append_workspace_range(
        const Interpreter::Impl::RegionFrontierComponentRuntime& runtime,
        std::array<WorkspaceRange, 25U>& ranges,
        std::size_t& range_count,
        const std::vector<T, Allocator>& values)
    {
        if (values.get_allocator().state() != &runtime.workspace_allocation) {
            throw std::runtime_error {
                "prepared frame vectors must share the runtime workspace allocator"
            };
        }
        const auto capacity = values.capacity();
        if (capacity == 0U) {
            return;
        }
        if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(T)
            || range_count >= ranges.size()) {
            throw std::runtime_error {
                "prepared frame vector capacity must have a bounded extent"
            };
        }
        const auto byte_count = capacity * sizeof(T);
        if (!runtime.workspace_allocation.owns(values.data(), byte_count)) {
            throw std::runtime_error {
                "prepared frame vector storage must lie in its shared workspace"
            };
        }
        const auto begin = reinterpret_cast<std::uintptr_t>(values.data());
        if (begin % alignof(T) != 0U
            || byte_count
                > std::numeric_limits<std::uintptr_t>::max() - begin) {
            throw std::runtime_error {
                "prepared frame vector slice must be aligned and bounded"
            };
        }
        ranges[range_count++] = { begin, begin + byte_count };
    }

    static void validate_workspace_slices(
        const Interpreter::Impl::RegionFrontierComponentRuntime& runtime)
    {
        const auto workspace_begin = reinterpret_cast<std::uintptr_t>(
            runtime.workspace_allocation.begin());
        if (runtime.workspace_allocation.begin() == nullptr
            || runtime.workspace_allocation.capacity() == 0U
            || runtime.workspace_allocation.used()
                > runtime.workspace_allocation.capacity()
            || workspace_begin % alignof(std::max_align_t) != 0U
            || runtime.workspace_allocation.capacity()
                > std::numeric_limits<std::uintptr_t>::max()
                    - workspace_begin) {
            throw std::runtime_error {
                "prepared runtime must own one bounded component workspace"
            };
        }
        std::array<WorkspaceRange, 25U> ranges { };
        std::size_t range_count { };
        append_workspace_range(runtime, ranges, range_count,
            runtime.ready_words);
        append_workspace_range(runtime, ranges, range_count,
            runtime.members);
        append_workspace_range(runtime, ranges, range_count,
            runtime.scheduler_tasks);
        append_workspace_range(runtime, ranges, range_count,
            runtime.original_scheduler_tasks);
        append_workspace_range(runtime, ranges, range_count,
            runtime.planes);
        append_workspace_range(runtime, ranges, range_count,
            runtime.metadata);
        append_workspace_range(runtime, ranges, range_count,
            runtime.fanout_edges);
        append_workspace_range(runtime, ranges, range_count,
            runtime.port_planes);
        append_workspace_range(runtime, ranges, range_count,
            runtime.pending_writes);
        append_workspace_range(runtime, ranges, range_count,
            runtime.staged_events);
        append_workspace_range(runtime, ranges, range_count,
            runtime.committed_signals);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_snapshot_plane_offsets);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_snapshot_words);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_member_task_indices);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_ticket_member_offsets);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_queued_ready_words);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_queued_members);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_parked_executors);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_prefix_processes);
        append_workspace_range(runtime, ranges, range_count,
            runtime.generic_prefix_members);
        append_workspace_range(runtime, ranges, range_count,
            runtime.writable_signals);
        append_workspace_range(runtime, ranges, range_count,
            runtime.pending_plane_offsets);
        append_workspace_range(runtime, ranges, range_count,
            runtime.pending_plane_words);
        append_workspace_range(runtime, ranges, range_count,
            runtime.compact_members);
        append_workspace_range(runtime, ranges, range_count,
            runtime.issued_sequences);
        std::ranges::sort(ranges.begin(),
            ranges.begin() + static_cast<std::ptrdiff_t>(range_count),
            [](const WorkspaceRange& left, const WorkspaceRange& right) {
                return left.begin < right.begin;
            });
        for (std::size_t index = 1U; index < range_count; ++index) {
            if (ranges[index - 1U].end > ranges[index].begin) {
                throw std::runtime_error {
                    "prepared frame vector slices must not overlap"
                };
            }
        }
    }

    [[nodiscard]] static RuntimeSnapshot runtime_snapshot(
        const Interpreter::Impl::RegionFrontierComponentRuntime& runtime)
    {
        RuntimeSnapshot result;
        result.runtime = reinterpret_cast<std::uintptr_t>(&runtime);
        result.backend = reinterpret_cast<std::uintptr_t>(
            runtime.backend.get());
        result.workspace_begin = reinterpret_cast<std::uintptr_t>(
            runtime.workspace_allocation.begin());
        result.workspace_capacity = runtime.workspace_allocation.capacity();
        result.workspace_used = runtime.workspace_allocation.used();
        result.executor = reinterpret_cast<std::uintptr_t>(
            runtime.backend && runtime.backend->executor
                ? runtime.backend->executor.get() : nullptr);
        result.native_dispatches = runtime.native_member_dispatches;
        result.runtime_control = {
            runtime.runtime_generation,
            static_cast<std::uint64_t>(runtime.component),
            runtime.stop_requested,
            static_cast<std::uint64_t>(runtime.frame_initialized),
            static_cast<std::uint64_t>(runtime.invalidated)
        };
        validate_workspace_slices(runtime);
        append_object_bytes(result.state_bytes, runtime.frame);
        if (runtime.backend && runtime.backend->executor) {
            const auto& layout = runtime.backend->executor->layout();
            append_object_bytes(result.state_bytes, layout);
            append_span_bytes(result.state_bytes, layout.members,
                layout.member_count);
            append_span_bytes(result.state_bytes, layout.signals,
                layout.signal_slot_count);
            append_span_bytes(result.state_bytes, layout.write_sites,
                layout.write_site_count);
            append_span_bytes(result.state_bytes,
                layout.max_member_write_counts, layout.member_count);
            append_span_bytes(result.state_bytes,
                layout.max_member_staged_event_counts, layout.member_count);
            append_span_bytes(result.state_bytes, layout.fanout_edges,
                layout.fanout_edge_count);
        }
        append_vector_state(result.state_bytes, runtime.ready_words);
        append_vector_state(result.state_bytes, runtime.members);
        append_vector_state(result.state_bytes, runtime.scheduler_tasks);
        append_vector_state(result.state_bytes,
            runtime.original_scheduler_tasks);
        append_vector_state(result.state_bytes, runtime.planes);
        append_vector_state(result.state_bytes, runtime.metadata);
        append_vector_state(result.state_bytes, runtime.fanout_edges);
        append_vector_state(result.state_bytes, runtime.port_planes);
        append_vector_state(result.state_bytes, runtime.pending_writes);
        append_vector_state(result.state_bytes, runtime.staged_events);
        append_vector_state(result.state_bytes, runtime.committed_signals);
        append_vector_state(result.state_bytes,
            runtime.generic_snapshot_plane_offsets);
        append_vector_state(result.state_bytes, runtime.generic_snapshot_words);
        append_vector_state(result.state_bytes,
            runtime.generic_member_task_indices);
        append_vector_state(result.state_bytes,
            runtime.generic_ticket_member_offsets);
        append_vector_state(result.state_bytes,
            runtime.generic_queued_ready_words);
        append_vector_state(result.state_bytes, runtime.generic_queued_members);
        append_vector_state(result.state_bytes,
            runtime.generic_parked_executors);
        append_vector_state(result.state_bytes,
            runtime.generic_prefix_processes);
        append_vector_state(result.state_bytes,
            runtime.generic_prefix_members);
        append_vector_state(result.state_bytes, runtime.writable_signals);
        append_vector_state(result.state_bytes, runtime.pending_plane_offsets);
        append_vector_state(result.state_bytes, runtime.pending_plane_words);
        append_vector_state(result.state_bytes, runtime.compact_members);
        append_vector_state(result.state_bytes, runtime.issued_sequences);
        return result;
    }

    [[nodiscard]] static PublishedSnapshot published_snapshot(
        fsim::app::Simulation& simulation)
    {
        const auto& impl = interpreter_impl(simulation);
        PublishedSnapshot result;
        result.generation = impl.region_runtime_generation;
        result.frontier_runtime_by_component.reserve(
            impl.region_frontier_runtime_by_component.size());
        result.runtimes.reserve(
            impl.region_frontier_runtime_by_component.size());
        for (const auto& runtime : impl.region_frontier_runtime_by_component) {
            result.frontier_runtime_by_component.push_back(
                reinterpret_cast<std::uintptr_t>(runtime.get()));
            if (runtime) {
                result.runtimes.push_back(runtime_snapshot(*runtime));
            }
        }
        return result;
    }

    [[nodiscard]] static std::size_t validate_candidate(
        const Interpreter::Impl::RegionRuntimeSnapshot& candidate)
    {
        const auto component_count
            = candidate.graph.certificate_inventory().components.size();
        if (candidate.generation == 0U
            || candidate.programs_by_component.size() != component_count
            || candidate.backends_by_component.size() != component_count
            || candidate.frontier_backends_by_component.size()
                != component_count
            || candidate.frontier_runtime_by_component.size()
                != component_count
            || candidate.authoritative_state_by_component.size()
                != component_count
            || candidate.local_wave_state_by_component.size()
                != component_count) {
            throw std::runtime_error {
                "the unpublished candidate must have coherent per-component inventories"
            };
        }
        std::size_t runtime_count { };
        for (std::size_t component = 0U;
             component < candidate.frontier_runtime_by_component.size();
             ++component) {
            const auto& runtime
                = candidate.frontier_runtime_by_component[component];
            if (!runtime) {
                continue;
            }
            ++runtime_count;
            if (!runtime->frame_initialized || !runtime->backend
                || !runtime->backend->executor
                || runtime->backend->executor->step_entry() == nullptr) {
                throw std::runtime_error {
                    "a prepared candidate must retain its real generated V2 entry"
                };
            }
            const auto& layout = runtime->backend->executor->layout();
            const auto& frame = runtime->frame;
            if (layout.member_count == 0U || layout.write_site_count == 0U
                || layout.members == nullptr || layout.signals == nullptr
                || layout.write_sites == nullptr
                || layout.max_member_write_counts == nullptr
                || layout.max_member_staged_event_counts == nullptr
                || layout.abi_version != kRegionFrontierAbiVersionV2
                || layout.struct_size != sizeof(RegionFrontierLayoutV2)
                || !region_frontier_layout_header_valid_v2(layout)
                || frame.abi_version != kRegionFrontierAbiVersionV2
                || frame.struct_size != sizeof(RegionFrontierFrameV2)
                || !region_frontier_frame_header_valid_v2(frame)
                || layout.certificate_generation
                    != frame.certificate_generation
                || layout.component_generation
                    != frame.component_generation
                || layout.member_count != frame.member_count
                || layout.signal_slot_count != frame.signal_slot_count
                || layout.metadata_count != frame.metadata_count
                || layout.pending_write_capacity
                    != frame.pending_write_capacity
                || layout.readiness_word_count != frame.readiness_word_count
                || layout.staged_event_capacity
                    != frame.staged_event_capacity
                || layout.fanout_edge_count != frame.fanout_edge_count
                || layout.committed_signal_capacity
                    != frame.committed_signal_capacity
                || frame.scheduler_task_capacity
                    != Interpreter::Impl::RegionFrontierComponentRuntime::
                        scheduler_task_capacity
                || frame.runtime_generation != runtime->runtime_generation
                || frame.bound_runtime_generation != runtime->runtime_generation
                || runtime->runtime_generation != candidate.generation
                || runtime->ready_words.size() != layout.readiness_word_count
                || runtime->members.size() != layout.member_count
                || runtime->scheduler_tasks.size()
                    != frame.scheduler_task_capacity
                || runtime->planes.size() != layout.signal_slot_count
                || runtime->metadata.size() != layout.metadata_count
                || runtime->fanout_edges.size() != layout.fanout_edge_count
                || runtime->port_planes.size() != layout.signal_slot_count
                || runtime->pending_writes.size()
                    != layout.pending_write_capacity
                || runtime->staged_events.size()
                    != layout.staged_event_capacity
                || runtime->committed_signals.size()
                    != layout.committed_signal_capacity
                || runtime->pending_plane_offsets.size()
                    != layout.pending_write_capacity
                || runtime->compact_members.size()
                    != layout.staged_event_capacity
                || runtime->issued_sequences.size()
                    != layout.staged_event_capacity
                || frame.pending_write_count > frame.pending_write_capacity
                || frame.staged_event_count > frame.staged_event_capacity
                || frame.committed_signal_count
                    > frame.committed_signal_capacity
                || frame.ready_words != runtime->ready_words.data()
                || frame.members != runtime->members.data()
                || frame.planes != runtime->planes.data()
                || frame.port_planes != runtime->port_planes.data()
                || frame.pending_writes != runtime->pending_writes.data()
                || frame.staged_events != runtime->staged_events.data()
                || frame.committed_signals != runtime->committed_signals.data()
                || frame.native_frontier_member_dispatches
                    != &runtime->native_member_dispatches) {
                throw std::runtime_error {
                    "the prepared runtime frame must match its retained layout"
                };
            }
            for (std::size_t slot = 0U;
                 slot < runtime->planes.size(); ++slot) {
                const auto& plane = runtime->planes[slot];
                const auto& signal = layout.signals[slot];
                if (plane.value_kind != signal.value_kind
                    || plane.width != signal.width
                    || plane.word_count != signal.word_count
                    || plane.plane_count != signal.plane_count
                    || !region_frontier_value_shape_valid_v2(
                        plane.value_kind, plane.width,
                        plane.word_count, plane.plane_count)) {
                    throw std::runtime_error {
                        "the prepared typed planes must match the immutable layout"
                    };
                }
                for (std::size_t index = 0U; index < 4U; ++index) {
                    if (plane.boundary_planes[index] != nullptr
                        || plane.current_planes[index] != nullptr
                        || plane.previous_planes[index] != nullptr
                        || plane.stored_planes[index] != nullptr
                        || plane.owner_planes[index] != nullptr) {
                        throw std::runtime_error {
                            "an unpublished frame must not bind typed role planes before its lease"
                        };
                    }
                }
            }
        }
        return runtime_count;
    }

    [[nodiscard]] static AliasCertificateSummary
    alias_certificate_summary(
        std::span<const std::shared_ptr<Interpreter::Impl::
            RegionFrontierComponentRuntime>> runtimes)
    {
        AliasCertificateSummary result;
        for (const auto& runtime : runtimes) {
            if (!runtime || !runtime->backend || !runtime->backend->executor
                || runtime->execution_mode
                    != RegionFrontierExecutionModeV2::systemverilog_active) {
                continue;
            }
            const auto* const capability
                = dynamic_cast<const detail::
                    RegionFrontierTrustedEntryCapability*>(
                        runtime->backend->executor.get());
            if (capability == nullptr) {
                continue;
            }
            const auto trusted_view = capability->trusted_entry();
            const auto& layout = runtime->backend->executor->layout();
            if (trusted_view.entry == nullptr
                || trusted_view.layout != &layout) {
                throw std::runtime_error {
                    "the built-in SV executor must expose its exact trusted entry"
                };
            }

            ++result.capable_runtime_count;
            result.checked_entries += runtime->alias_checked_entries;
            result.trusted_entries += runtime->alias_trusted_entries;
            if (runtime->alias_certificate_storage_available) {
                ++result.storage_available_count;
                if (runtime->alias_certificate_ranges.empty()
                    || runtime->alias_candidate_ranges.empty()
                    || runtime->alias_sorted_indices.empty()
                    || runtime->alias_candidate_ranges.size() < 13U
                    || runtime->alias_certificate_ranges.size()
                        != runtime->alias_candidate_ranges.size()
                    || runtime->alias_sorted_indices.size()
                        != runtime->alias_candidate_ranges.size() - 13U
                    || runtime->alias_certificate_ranges.capacity()
                        < runtime->alias_certificate_ranges.size()
                    || runtime->alias_candidate_ranges.capacity()
                        < runtime->alias_candidate_ranges.size()
                    || runtime->alias_sorted_indices.capacity()
                        < runtime->alias_sorted_indices.size()) {
                    ++result.partial_unavailable_count;
                }
                continue;
            }

            ++result.storage_unavailable_count;
            result.unavailable_component = runtime->component;
            result.unavailable_checked_entries
                += runtime->alias_checked_entries;
            result.unavailable_trusted_entries
                += runtime->alias_trusted_entries;
            const bool both_empty
                = runtime->alias_certificate_ranges.empty()
                && runtime->alias_candidate_ranges.empty()
                && runtime->alias_sorted_indices.empty()
                && runtime->alias_certificate_ranges.capacity() == 0U
                && runtime->alias_candidate_ranges.capacity() == 0U
                && runtime->alias_sorted_indices.capacity() == 0U;
            if (both_empty) {
                ++result.empty_unavailable_count;
            } else {
                ++result.partial_unavailable_count;
            }
        }
        return result;
    }

    [[nodiscard]] static AliasCertificateSummary
    alias_certificate_summary(
        const Interpreter::Impl::RegionRuntimeSnapshot& candidate)
    {
        return alias_certificate_summary(candidate.frontier_runtime_by_component);
    }

    [[nodiscard]] static AliasCertificateSummary
    alias_certificate_summary(fsim::app::Simulation& simulation)
    {
        return alias_certificate_summary(
            interpreter_impl(simulation).region_frontier_runtime_by_component);
    }

    [[nodiscard]] static std::size_t build_candidate(
        fsim::app::Simulation& simulation,
        AliasCertificateSummary* const alias_summary = nullptr)
    {
        auto& impl = interpreter_impl(simulation);
        auto candidate = impl.build_region_runtime_snapshot(false, true, true);
        if (alias_summary != nullptr) {
            *alias_summary = alias_certificate_summary(candidate);
        }
        return validate_candidate(candidate);
    }

    [[nodiscard]] static AliasCertificateSummary
    build_and_publish_candidate_with_alias_allocation_failure(
        fsim::app::Simulation& simulation,
        const std::size_t fail_after,
        const std::size_t expected_component)
    {
        auto& impl = interpreter_impl(simulation);
        using fsim::tests::runtime::staging_failure_support::
            allocation_failure_was_injected;
        using fsim::tests::runtime::staging_failure_support::
            arm_allocation_failure;
        using fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure;

        arm_allocation_failure(fail_after);
        try {
            auto candidate
                = impl.build_region_runtime_snapshot(false, true, true);
            const bool injected = allocation_failure_was_injected();
            clear_allocation_failure();
            if (!injected || validate_candidate(candidate) == 0U) {
                throw std::runtime_error {
                    "the selected optional alias allocation must yield a valid candidate"
                };
            }
            auto result = alias_certificate_summary(candidate);
            if (result.capable_runtime_count == 0U
                || result.storage_unavailable_count != 1U
                || result.unavailable_component != expected_component
                || result.empty_unavailable_count != 1U
                || result.partial_unavailable_count != 0U
                || result.unavailable_checked_entries != 0U
                || result.unavailable_trusted_entries != 0U) {
                throw std::runtime_error {
                    "failed optional alias allocations must leave all geometry buffers empty"
                };
            }
            impl.publish_region_runtime_snapshot(std::move(candidate));
            return result;
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
    }

    [[nodiscard]] static RetainedA4Candidate
    build_candidate_and_retain_wide_a4_planes(
        fsim::app::Simulation& simulation)
    {
        auto& impl = interpreter_impl(simulation);
        auto candidate = impl.build_region_runtime_snapshot(false, true, true);
        if (validate_candidate(candidate) == 0U) {
            throw std::runtime_error {
                "the wide A4 witness requires a prepared V2 component"
            };
        }

        for (const auto& runtime : candidate.frontier_runtime_by_component) {
            if (!runtime || !runtime->authoritative_state) {
                continue;
            }
            auto state = runtime->authoritative_state;
            auto& values = state->values();
            const auto counts = values.component_plane_word_counts();
            std::array<std::array<std::size_t, 4U>, 4U> observed_counts { };
            struct AddressRange {
                std::uintptr_t begin { };
                std::uintptr_t end { };
            };
            std::vector<AddressRange> ranges;
            RetainedA4Candidate result;
            result.state = state;
            result.workspace
                = runtime->workspace_allocation.storage_lifetime();

            SignalId selected_signal { };
            ProcessId selected_owner { };
            bool selected { };
            for (const auto signal : values.layout().signal_ids()) {
                const auto& signal_layout = values.layout().signal(signal);
                if (signal_layout.width <= 128U
                    || signal_layout.value_kind != ValueKind::logic4) {
                    continue;
                }
                const auto owners = values.layout().owners(signal);
                if (owners.size() != 1U) {
                    continue;
                }
                std::array<PackedLogic4PlaneReadLease, 4U> required_roles;
                constexpr std::array<PackedPlaneRole, 4U> roles {
                    PackedPlaneRole::current,
                    PackedPlaneRole::previous,
                    PackedPlaneRole::stored,
                    PackedPlaneRole::owner
                };
                for (std::size_t role = 0U; role < roles.size(); ++role) {
                    required_roles[role] = values.plane_read_lease(signal,
                        roles[role], owners.front().process);
                }
                if (std::ranges::any_of(required_roles,
                        [](const auto& lease) { return !lease; })) {
                    continue;
                }
                selected_signal = signal;
                selected_owner = owners.front().process;
                selected = true;
                result.signal_width = signal_layout.width;
                result.old_values = {
                    values.current(signal), values.previous(signal),
                    values.stored(signal),
                    values.owner_value(signal, selected_owner)
                };
                for (std::size_t role = 0U; role < roles.size(); ++role) {
                    result.selected_role_leases[role].lease
                        = required_roles[role];
                    for (std::size_t plane = 0U; plane < 4U; ++plane) {
                        const auto words
                            = required_roles[role].plane_words(plane);
                        result.selected_role_leases[role].words[plane]
                            .assign(words.begin(), words.end());
                    }
                }
                break;
            }
            if (!selected) {
                continue;
            }

            const auto append_lease = [&](const PackedLogic4PlaneReadLease& lease,
                                          const std::size_t role) {
                if (!lease) {
                    throw std::runtime_error {
                        "every seeded wide component role must retain its plane block"
                    };
                }
                RetainedPlaneLease held;
                held.lease = lease;
                for (std::size_t plane = 0U; plane < 4U; ++plane) {
                    const auto words = lease.plane_words(plane);
                    if (words.empty()) {
                        continue;
                    }
                    if (words.size()
                            > std::numeric_limits<std::size_t>::max()
                                / sizeof(std::uint64_t)
                        || !runtime->workspace_allocation.owns(
                            words.data(), words.size() * sizeof(std::uint64_t))) {
                        throw std::runtime_error {
                            "every retained A4 role span must be a component-slab slice"
                        };
                    }
                    const auto begin = reinterpret_cast<std::uintptr_t>(
                        words.data());
                    const auto bytes = words.size() * sizeof(std::uint64_t);
                    if (begin % alignof(std::uint64_t) != 0U
                        || bytes
                            > std::numeric_limits<std::uintptr_t>::max()
                                - begin) {
                        throw std::runtime_error {
                            "A4 role slices must be aligned and bounded"
                        };
                    }
                    ranges.push_back({ begin, begin + bytes });
                    observed_counts[role][plane] += words.size();
                    held.words[plane].assign(words.begin(), words.end());
                }
                result.leases.push_back(std::move(held));
            };

            constexpr std::array<PackedPlaneRole, 3U> value_roles {
                PackedPlaneRole::current,
                PackedPlaneRole::previous,
                PackedPlaneRole::stored
            };
            for (const auto signal : values.layout().signal_ids()) {
                for (std::size_t role = 0U; role < value_roles.size(); ++role) {
                    append_lease(values.plane_read_lease(
                        signal, value_roles[role]), role);
                }
                for (const auto& owner : values.layout().owners(signal)) {
                    if (owner.aliases_stored) {
                        continue;
                    }
                    append_lease(values.plane_read_lease(signal,
                        PackedPlaneRole::owner, owner.process), 3U);
                }
            }
            if (observed_counts != counts) {
                throw std::runtime_error {
                    "retained role leases must cover every component A4 plane word"
                };
            }
            std::ranges::sort(ranges,
                [](const AddressRange& left, const AddressRange& right) {
                    return left.begin < right.begin;
                });
            for (std::size_t index = 1U; index < ranges.size(); ++index) {
                if (ranges[index - 1U].end > ranges[index].begin) {
                    throw std::runtime_error {
                        "A4 component plane slices must not overlap"
                    };
                }
            }
            const auto plane_word_total = std::accumulate(
                counts.begin(), counts.end(), std::size_t { },
                [](const std::size_t total, const auto& role_counts) {
                    return total + std::accumulate(role_counts.begin(),
                        role_counts.end(), std::size_t { });
                });
            if (ranges.empty() || plane_word_total == 0U) {
                throw std::runtime_error {
                    "wide A4 witness must retain physical plane slices"
                };
            }
            if (plane_word_total
                > std::numeric_limits<std::size_t>::max()
                    / sizeof(std::uint64_t)) {
                throw std::runtime_error {
                    "A4 tail byte count must fit the workspace address range"
                };
            }
            const auto bytes = plane_word_total * sizeof(std::uint64_t);
            const auto end = reinterpret_cast<std::uintptr_t>(
                runtime->workspace_allocation.begin())
                + runtime->workspace_allocation.used();
            if (ranges.front().begin + bytes != end) {
                throw std::runtime_error {
                    "A4 role slices must occupy the reserved workspace tail"
                };
            }

            const auto next = PackedLogic4::from_msb_string(
                std::string(result.signal_width, '1'));
            auto mutation = values.prepare_owner_change(
                selected_signal, selected_owner, next, next, next);
            const auto workspace_used = runtime->workspace_allocation.used();
            values.publish(std::move(mutation));
            const auto current_after = values.plane_read_lease(
                selected_signal, PackedPlaneRole::current);
            const auto stored_after = values.plane_read_lease(
                selected_signal, PackedPlaneRole::stored);
            const auto owner_after = values.plane_read_lease(
                selected_signal, PackedPlaneRole::owner, selected_owner);
            if (!values.valid()
                || runtime->workspace_allocation.used() != workspace_used
                || values.current(selected_signal) != next
                || values.previous(selected_signal) != result.old_values[0U]
                || values.stored(selected_signal) != next
                || values.owner_value(selected_signal, selected_owner) != next
                || !current_after || !stored_after || !owner_after
                || runtime->workspace_allocation.owns(
                    current_after.plane_words(0U).data(),
                    current_after.plane_words(0U).size()
                        * sizeof(std::uint64_t))
                || runtime->workspace_allocation.owns(
                    stored_after.plane_words(0U).data(),
                    stored_after.plane_words(0U).size()
                        * sizeof(std::uint64_t))
                || runtime->workspace_allocation.owns(
                    owner_after.plane_words(0U).data(),
                    owner_after.plane_words(0U).size()
                        * sizeof(std::uint64_t))) {
                throw std::runtime_error {
                    "A4 COW must detach changed current/stored/owner roles without growing the slab"
                };
            }
            result.new_value = next;
            return result;
        }
        throw std::runtime_error {
            "the V2 candidate has no wide versioned Logic4 component"
        };
    }

    static void require_retained_a4_after_candidate_destruction(
        RetainedA4Candidate& retained)
    {
        if (!retained.state.expired() || retained.workspace.expired()
            || retained.leases.empty()) {
            throw std::runtime_error {
                "plane leases must outlive the unpublished runtime and A4 state"
            };
        }
        for (const auto& held : retained.leases) {
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                if (!std::ranges::equal(held.lease.plane_words(plane),
                        held.words[plane])) {
                    throw std::runtime_error {
                        "COW and teardown must preserve every retained A4 plane snapshot"
                    };
                }
            }
        }
        for (const auto& held : retained.selected_role_leases) {
            if (!held.lease) {
                throw std::runtime_error {
                    "the selected current/LAST/stored/owner snapshots remain pinned"
                };
            }
            for (std::size_t plane = 0U; plane < 4U; ++plane) {
                if (!std::ranges::equal(held.lease.plane_words(plane),
                        held.words[plane])) {
                    throw std::runtime_error {
                        "selected role snapshots remain immutable after teardown"
                    };
                }
            }
        }
        if (retained.old_values[0U].width() != retained.signal_width
            || retained.old_values[1U].width() != retained.signal_width
            || retained.old_values[2U].width() != retained.signal_width
            || retained.old_values[3U].width() != retained.signal_width
            || retained.old_values[0U] == retained.new_value
            || retained.old_values[1U] == retained.new_value
            || retained.old_values[2U] == retained.new_value
            || retained.old_values[3U] == retained.new_value) {
            throw std::runtime_error {
                "the detached COW snapshots must retain their original role values"
            };
        }
        const auto workspace = retained.workspace;
        retained.leases.clear();
        for (auto& held : retained.selected_role_leases) {
            held = RetainedPlaneLease { };
        }
        if (!workspace.expired()) {
            throw std::runtime_error {
                "the shared slab must be reclaimed after its final plane lease releases"
            };
        }
    }

    static void build_and_publish_candidate(
        fsim::app::Simulation& simulation)
    {
        auto& impl = interpreter_impl(simulation);
        auto candidate = impl.build_region_runtime_snapshot(false, true, true);
        if (validate_candidate(candidate) == 0U) {
            throw std::runtime_error {
                "the successful retry must prepare a frontier runtime"
            };
        }
        impl.publish_region_runtime_snapshot(std::move(candidate));
    }
};

} // namespace fsim::runtime::simir

namespace {

using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::app::SystemVerilogVpiRuntimeUpdates;
using fsim::project::Optimization;
using fsim::runtime::Logic4;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunStatus;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::tests::runtime::staging_failure_support::
    allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::
    arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::
    begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::
    clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::
    end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;

void set_environment(const char* const name, const char* const value)
{
#if defined(_WIN32)
    if (_putenv_s(name, value == nullptr ? "" : value) != 0) {
        throw std::runtime_error { "cannot update the fixture environment" };
    }
#else
    const auto result = value == nullptr
        ? unsetenv(name) : setenv(name, value, 1);
    if (result != 0) {
        throw std::runtime_error { "cannot update the fixture environment" };
    }
#endif
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name);
            previous != nullptr) {
            previous_ = previous;
        }
        set_environment(name_.c_str(), value);
    }

    ~ScopedEnvironment()
    {
        try {
            set_environment(name_.c_str(),
                previous_ ? previous_->c_str() : nullptr);
        } catch (...) {
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::optional<std::string> previous_;
};

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("fsim-native-frontier-preparation-"
                + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] fsim::project::Config make_fixture(
    const std::filesystem::path& root)
{
    const auto source = root / "native_frontier_preparation.sv";
    std::ofstream output { source, std::ios::binary };
    output << R"(
module native_frontier_preparation(
  input logic stimulus,
  output wire observed,
  input logic [128:0] wide_stimulus,
  output wire [128:0] wide_observed
);
  wire stage0;
  wire stage1;
  wire [128:0] wide_stage0;
  wire [128:0] wide_stage1;
  assign stage0 = stimulus;
  assign stage1 = stage0;
  assign observed = stage1;
  assign wide_stage0 = wide_stimulus;
  assign wide_stage1 = wide_stage0;
  assign wide_observed = wide_stage1;
  initial begin
    #100;
    $finish;
  end
endmodule
)";
    require(static_cast<bool>(output),
        "the preparation-failure HDL fixture must be written");

    fsim::project::Config config;
    config.project.name = "native-frontier-preparation-failure";
    config.project.top = "sv:work.native_frontier_preparation";
    config.base_directory = root;
    config.build.optimization = Optimization::o2;
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

[[nodiscard]] std::size_t count_candidate_allocations(
    Simulation& simulation,
    NativeRegionAllocationTestAccess::AliasCertificateSummary& alias_summary)
{
    begin_allocation_count();
    try {
        const auto frontier_runtime_count
            = NativeRegionAllocationTestAccess::build_candidate(
                simulation, &alias_summary);
        require(frontier_runtime_count != 0U,
            "a successful no-failpoint candidate must prepare a frontier runtime");
    } catch (...) {
        static_cast<void>(end_allocation_count());
        throw;
    }
    return end_allocation_count();
}

[[nodiscard]] std::array<std::size_t, 3U>
find_alias_storage_failure_indices(
    Simulation& simulation,
    const std::size_t expected_component)
{
    NativeRegionAllocationTestAccess::AliasCertificateSummary successful_summary;
    const auto allocation_count
        = count_candidate_allocations(simulation, successful_summary);
    require(allocation_count != 0U
            && successful_summary.capable_runtime_count != 0U
            && successful_summary.storage_available_count
                == successful_summary.capable_runtime_count
            && successful_summary.storage_unavailable_count == 0U
            && successful_summary.partial_unavailable_count == 0U,
        "the current scheduler state must build complete certificate storage");

    const auto published_before
        = NativeRegionAllocationTestAccess::published_snapshot(simulation);
    const auto scheduler_before
        = NativeRegionAllocationTestAccess::scheduler_snapshot(simulation);
    const auto workspace_before
        = NativeRegionAllocationTestAccess::workspace_extents(simulation);
    std::array<std::size_t, 3U> fail_indices { };
    std::size_t matches { };

    for (std::size_t fail_after = 0U;
         fail_after < allocation_count && matches < fail_indices.size();
         ++fail_after) {
        arm_allocation_failure(fail_after);
        bool candidate_returned { };
        NativeRegionAllocationTestAccess::AliasCertificateSummary candidate_summary;
        try {
            static_cast<void>(
                NativeRegionAllocationTestAccess::build_candidate(
                    simulation, &candidate_summary));
            candidate_returned = true;
        } catch (const std::bad_alloc&) {
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
        const bool injected = allocation_failure_was_injected();
        clear_allocation_failure();
        require(injected,
            "each current-state allocation probe must fire at its selected index");

        if (candidate_returned
            && candidate_summary.capable_runtime_count
                == successful_summary.capable_runtime_count
            && candidate_summary.storage_available_count
                + candidate_summary.storage_unavailable_count
                == candidate_summary.capable_runtime_count
            && candidate_summary.storage_unavailable_count == 1U
            && candidate_summary.unavailable_component == expected_component
            && candidate_summary.empty_unavailable_count == 1U
            && candidate_summary.partial_unavailable_count == 0U
            && candidate_summary.checked_entries == 0U
            && candidate_summary.trusted_entries == 0U) {
            fail_indices[matches++] = fail_after;
        }

        require(NativeRegionAllocationTestAccess::published_snapshot(simulation)
                    == published_before
                && NativeRegionAllocationTestAccess::scheduler_snapshot(simulation)
                    == scheduler_before
                && NativeRegionAllocationTestAccess::workspace_extents(simulation)
                    == workspace_before,
            "probing current-state certificate failures must not publish or mutate runtime state");
    }
    require(matches == fail_indices.size(),
        "the current allocator sequence must expose each optional geometry-buffer failure");
    return fail_indices;
}

void test_preparation_failure_is_transactional_and_retryable()
{
    NativeRegionAllocationTestAccess::check_workspace_typed_destruction();
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_commit {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", nullptr };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    TemporaryDirectory root;
    const auto config = make_fixture(root.path());
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the native-frontier preparation fixture must elaborate");

    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::compiled, SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() >= 3U,
        "the fixture must install its real compiled assignment processes");
    const auto stimulus
        = simulation.find_signal("native_frontier_preparation.stimulus");
    const auto observed
        = simulation.find_signal("native_frontier_preparation.observed");
    const auto wide_stimulus
        = simulation.find_signal("native_frontier_preparation.wide_stimulus");
    const auto wide_observed
        = simulation.find_signal("native_frontier_preparation.wide_observed");
    require(stimulus && observed && wide_stimulus && wide_observed,
        "the frontier fixture must retain narrow and wide boundary signals");
    // Keep this transactional preparation regression on the V2 path whose
    // generated entry it inspects. The shared test adapter delegates the real
    // LLVM activation/frontier provider while withholding flattened forwarding.
    NativeRegionAllocationTestAccess::install_v2_frontier_only_provider(
        simulation);
    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the fixture must stop at a quiet point with its delayed finish "
        "pending");

    auto retained_wide_a4
        = NativeRegionAllocationTestAccess::
            build_candidate_and_retain_wide_a4_planes(simulation);
    NativeRegionAllocationTestAccess::
        require_retained_a4_after_candidate_destruction(retained_wide_a4);

    const auto published_before
        = NativeRegionAllocationTestAccess::published_snapshot(simulation);
    const auto scheduler_before
        = NativeRegionAllocationTestAccess::scheduler_snapshot(simulation);
    const auto sequence_before
        = NativeRegionAllocationTestAccess::reserve_sequence_probe(simulation);
    const auto active_runtime
        = NativeRegionAllocationTestAccess::summarize(simulation);
    require(active_runtime.frontier_runtime_count > 0U
            && active_runtime.frontier_backend_count > 0U,
        "the live interpreter must retain its prepared V2 frontier instance");
    const auto workspace_before_failures
        = NativeRegionAllocationTestAccess::workspace_extents(simulation);

    NativeRegionAllocationTestAccess::AliasCertificateSummary
        successful_alias_summary;
    const auto successful_preparation_allocations
        = count_candidate_allocations(simulation, successful_alias_summary);
    require(successful_preparation_allocations != 0U,
        "the startup-equivalent builder must exercise the allocator failpoint");
    require(successful_alias_summary.capable_runtime_count != 0U
            && successful_alias_summary.storage_available_count
                == successful_alias_summary.capable_runtime_count
            && successful_alias_summary.storage_unavailable_count == 0U
            && successful_alias_summary.partial_unavailable_count == 0U,
        "the successful candidate must allocate both range vectors and the sorted index scratch");

    std::size_t alias_failure_component
        = std::numeric_limits<std::size_t>::max();

    for (std::size_t fail_after = 0U;
         fail_after < successful_preparation_allocations; ++fail_after) {
        arm_allocation_failure(fail_after);
        bool caught_bad_alloc { };
        bool candidate_returned { };
        NativeRegionAllocationTestAccess::AliasCertificateSummary
            candidate_alias_summary;
        try {
            static_cast<void>(
                NativeRegionAllocationTestAccess::build_candidate(
                    simulation, &candidate_alias_summary));
            candidate_returned = true;
        } catch (const std::bad_alloc&) {
            caught_bad_alloc = true;
        } catch (...) {
            clear_allocation_failure();
            throw;
        }
        const bool injected = allocation_failure_was_injected();
        clear_allocation_failure();
        require(injected && (caught_bad_alloc || candidate_returned),
            "each allocator failpoint must either propagate or return a "
            "coherent unpublished candidate after an optional decline");
        if (candidate_returned
            && candidate_alias_summary.capable_runtime_count
                == successful_alias_summary.capable_runtime_count
            && candidate_alias_summary.storage_unavailable_count != 0U) {
            require(candidate_alias_summary.storage_available_count
                        + candidate_alias_summary.storage_unavailable_count
                        == candidate_alias_summary.capable_runtime_count
                    && candidate_alias_summary.empty_unavailable_count
                        == candidate_alias_summary.storage_unavailable_count
                    && candidate_alias_summary.partial_unavailable_count == 0U,
                "an optional geometry allocation decline must release all scratch buffers");
            require(candidate_alias_summary.storage_unavailable_count == 1U,
                "one failpoint must disable storage for exactly one component");
            if (alias_failure_component
                == std::numeric_limits<std::size_t>::max()) {
                alias_failure_component
                    = candidate_alias_summary.unavailable_component;
            }
        }
        require(NativeRegionAllocationTestAccess::published_snapshot(simulation)
                    == published_before,
            "a failed preparation must preserve every published frontier "
            "frame, layout, and buffer");
        require(NativeRegionAllocationTestAccess::workspace_extents(simulation)
                    == workspace_before_failures,
            "a failed preparation must preserve published workspace identity, "
            "extent, and cursor");
        require(NativeRegionAllocationTestAccess::scheduler_snapshot(simulation)
                    == scheduler_before,
            "a failed preparation must leave the scheduler snapshot and "
            "pending work unchanged");
    }
    require(alias_failure_component
                != std::numeric_limits<std::size_t>::max(),
        "the allocation sweep must identify an optional certificate failure");
    const auto sequence_after
        = NativeRegionAllocationTestAccess::reserve_sequence_probe(simulation);
    require(sequence_after == sequence_before + 1U,
        "failed preparation must consume no scheduler insertion sequences");

    const auto dispatches_before_first_run
        = NativeRegionAllocationTestAccess::summarize(simulation)
              .native_dispatches;
    const auto workspace_before_first_run
        = NativeRegionAllocationTestAccess::workspace_extents(simulation);
    simulation.deposit_signal(*stimulus,
        PackedLogic4(1U, Logic4::one));
    require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
        "ordinary simulation execution must remain eligible after failed "
        "preparation");
    require(NativeRegionAllocationTestAccess::current_signal_value_passive(
                simulation, *observed).to_msb_string() == "1",
        "the retained runtime must still publish the boundary-driven output");
    const auto dispatches_after_first_run
        = NativeRegionAllocationTestAccess::summarize(simulation)
              .native_dispatches;
    require(NativeRegionAllocationTestAccess::workspace_extents(simulation)
                == workspace_before_first_run,
        "the retained runtime must execute without growing its bounded "
        "component workspace");
    require(dispatches_after_first_run > dispatches_before_first_run,
        "the retained V2 generated entry must execute after the preparation "
        "failures");

    const auto scheduler_before_alias_failures
        = NativeRegionAllocationTestAccess::scheduler_snapshot(simulation);
    const auto first_alias_failure_indices
        = find_alias_storage_failure_indices(
            simulation, alias_failure_component);
    const auto first_alias_failure
        = NativeRegionAllocationTestAccess::
            build_and_publish_candidate_with_alias_allocation_failure(
                simulation, first_alias_failure_indices[0U],
                alias_failure_component);
    require(first_alias_failure.capable_runtime_count
                == successful_alias_summary.capable_runtime_count
            && first_alias_failure.storage_unavailable_count == 1U
            && first_alias_failure.unavailable_component
                == alias_failure_component
            && first_alias_failure.empty_unavailable_count == 1U
            && first_alias_failure.checked_entries == 0U
            && first_alias_failure.trusted_entries == 0U,
        "a published candidate must keep the first allocation-failure "
        "buffers empty before dispatch");
    require(NativeRegionAllocationTestAccess::scheduler_snapshot(simulation)
                == scheduler_before_alias_failures,
        "publishing a certificate-unavailable candidate must not enqueue scheduler work");
    const auto first_checked_summary
        = NativeRegionAllocationTestAccess::alias_certificate_summary(simulation);
    require(first_checked_summary.storage_unavailable_count == 1U
            && first_checked_summary.unavailable_component
                == alias_failure_component
            && first_checked_summary.empty_unavailable_count == 1U
            && first_checked_summary.checked_entries == 0U
            && first_checked_summary.unavailable_checked_entries == 0U
            && first_checked_summary.trusted_entries == 0U,
        "the published first-allocation fallback must begin checked-only");
    const auto dispatches_before_first_alias_fallback
        = NativeRegionAllocationTestAccess::summarize(simulation)
              .native_dispatches;
    simulation.deposit_signal(*stimulus,
        PackedLogic4(1U, Logic4::zero));
    simulation.deposit_signal(*wide_stimulus,
        PackedLogic4(129U, Logic4::one));
    require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
        "a runtime with the first optional certificate allocation failed must execute");
    require(NativeRegionAllocationTestAccess::
                current_signal_value_passive(simulation, *observed)
                    .to_msb_string() == "0",
        "the first allocation fallback must preserve checked signal execution");
    require(NativeRegionAllocationTestAccess::
                current_signal_value_passive(simulation, *wide_observed)
                    == PackedLogic4(129U, Logic4::one),
        "the first allocation fallback must preserve wide checked propagation");
    const auto first_checked_after_dispatch
        = NativeRegionAllocationTestAccess::alias_certificate_summary(simulation);
    require(first_checked_after_dispatch.checked_entries != 0U
            && first_checked_after_dispatch.unavailable_checked_entries != 0U
            && first_checked_after_dispatch.unavailable_trusted_entries == 0U
            && first_checked_after_dispatch.storage_unavailable_count == 1U
            && first_checked_after_dispatch.unavailable_component
                == alias_failure_component
            && first_checked_after_dispatch.empty_unavailable_count == 1U
            && NativeRegionAllocationTestAccess::summarize(simulation)
                    .native_dispatches > dispatches_before_first_alias_fallback,
        "the first allocation fallback must execute the checked entry only");

    const auto second_alias_failure_indices
        = find_alias_storage_failure_indices(
            simulation, alias_failure_component);
    const auto second_alias_failure
        = NativeRegionAllocationTestAccess::
            build_and_publish_candidate_with_alias_allocation_failure(
                simulation, second_alias_failure_indices[1U],
                alias_failure_component);
    require(second_alias_failure.capable_runtime_count
                == successful_alias_summary.capable_runtime_count
            && second_alias_failure.storage_unavailable_count == 1U
            && second_alias_failure.unavailable_component
                == alias_failure_component
            && second_alias_failure.empty_unavailable_count == 1U
            && second_alias_failure.checked_entries == 0U
            && second_alias_failure.trusted_entries == 0U,
        "a failed second geometry-buffer allocation must release the first buffer too");
    const auto second_checked_before
        = NativeRegionAllocationTestAccess::alias_certificate_summary(simulation);
    require(second_checked_before.storage_unavailable_count == 1U
            && second_checked_before.unavailable_component
                == alias_failure_component
            && second_checked_before.empty_unavailable_count == 1U
            && second_checked_before.checked_entries == 0U
            && second_checked_before.unavailable_checked_entries == 0U
            && second_checked_before.trusted_entries == 0U,
        "the second-allocation fallback must begin with no partial certificate");
    const auto dispatches_before_second_alias_fallback
        = NativeRegionAllocationTestAccess::summarize(simulation)
              .native_dispatches;
    simulation.deposit_signal(*stimulus,
        PackedLogic4(1U, Logic4::one));
    simulation.deposit_signal(*wide_stimulus,
        PackedLogic4(129U, Logic4::zero));
    require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
        "a runtime with the second optional certificate allocation failed must execute");
    require(NativeRegionAllocationTestAccess::
                current_signal_value_passive(simulation, *observed)
                    .to_msb_string() == "1",
        "the second allocation fallback must preserve checked signal execution");
    require(NativeRegionAllocationTestAccess::
                current_signal_value_passive(simulation, *wide_observed)
                    == PackedLogic4(129U, Logic4::zero),
        "the second allocation fallback must preserve wide checked propagation");
    const auto second_checked_after_dispatch
        = NativeRegionAllocationTestAccess::alias_certificate_summary(simulation);
    require(second_checked_after_dispatch.checked_entries != 0U
            && second_checked_after_dispatch.unavailable_checked_entries != 0U
            && second_checked_after_dispatch.unavailable_trusted_entries == 0U
            && second_checked_after_dispatch.storage_unavailable_count == 1U
            && second_checked_after_dispatch.unavailable_component
                == alias_failure_component
            && second_checked_after_dispatch.empty_unavailable_count == 1U
            && NativeRegionAllocationTestAccess::summarize(simulation)
                    .native_dispatches > dispatches_before_second_alias_fallback,
        "the second allocation fallback must execute the checked entry only");

    const auto third_alias_failure_indices
        = find_alias_storage_failure_indices(
            simulation, alias_failure_component);
    const auto scheduler_before_third_alias_failure
        = NativeRegionAllocationTestAccess::scheduler_snapshot(simulation);
    const auto third_alias_failure
        = NativeRegionAllocationTestAccess::
            build_and_publish_candidate_with_alias_allocation_failure(
                simulation, third_alias_failure_indices[2U],
                alias_failure_component);
    require(third_alias_failure.capable_runtime_count
                == successful_alias_summary.capable_runtime_count
            && third_alias_failure.storage_unavailable_count == 1U
            && third_alias_failure.unavailable_component
                == alias_failure_component
            && third_alias_failure.empty_unavailable_count == 1U
            && third_alias_failure.checked_entries == 0U
            && third_alias_failure.trusted_entries == 0U,
        "a failed sorted-index allocation must release both range vectors");
    require(NativeRegionAllocationTestAccess::scheduler_snapshot(simulation)
                == scheduler_before_third_alias_failure,
        "publishing a candidate without sorted-index scratch must not enqueue scheduler work");
    const auto third_checked_before
        = NativeRegionAllocationTestAccess::alias_certificate_summary(simulation);
    require(third_checked_before.storage_unavailable_count == 1U
            && third_checked_before.unavailable_component
                == alias_failure_component
            && third_checked_before.empty_unavailable_count == 1U
            && third_checked_before.checked_entries == 0U
            && third_checked_before.unavailable_checked_entries == 0U
            && third_checked_before.trusted_entries == 0U,
        "the sorted-index allocation fallback must begin with every geometry buffer empty");
    const auto dispatches_before_third_alias_fallback
        = NativeRegionAllocationTestAccess::summarize(simulation)
              .native_dispatches;
    simulation.deposit_signal(*stimulus,
        PackedLogic4(1U, Logic4::zero));
    simulation.deposit_signal(*wide_stimulus,
        PackedLogic4(129U, Logic4::one));
    require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
        "a runtime without sorted-index scratch must execute through the checked entry");
    require(NativeRegionAllocationTestAccess::
                current_signal_value_passive(simulation, *observed)
                    .to_msb_string() == "0",
        "the sorted-index allocation fallback must preserve narrow checked propagation");
    require(NativeRegionAllocationTestAccess::
                current_signal_value_passive(simulation, *wide_observed)
                    == PackedLogic4(129U, Logic4::one),
        "the sorted-index allocation fallback must preserve wide checked propagation");
    const auto third_checked_after_dispatch
        = NativeRegionAllocationTestAccess::alias_certificate_summary(simulation);
    require(third_checked_after_dispatch.checked_entries != 0U
            && third_checked_after_dispatch.unavailable_checked_entries != 0U
            && third_checked_after_dispatch.unavailable_trusted_entries == 0U
            && third_checked_after_dispatch.storage_unavailable_count == 1U
            && third_checked_after_dispatch.unavailable_component
                == alias_failure_component
            && third_checked_after_dispatch.empty_unavailable_count == 1U
            && NativeRegionAllocationTestAccess::summarize(simulation)
                    .native_dispatches > dispatches_before_third_alias_fallback,
        "the sorted-index allocation fallback must execute the checked entry only");

    const auto scheduler_before_retry
        = NativeRegionAllocationTestAccess::scheduler_snapshot(simulation);
    NativeRegionAllocationTestAccess::build_and_publish_candidate(simulation);
    require(NativeRegionAllocationTestAccess::scheduler_snapshot(simulation)
                == scheduler_before_retry,
        "publishing a successfully rebuilt frontier snapshot must not "
        "enqueue scheduler work");
    const auto dispatches_before_retry_run
        = NativeRegionAllocationTestAccess::summarize(simulation)
              .native_dispatches;
    const auto workspace_before_retry_run
        = NativeRegionAllocationTestAccess::workspace_extents(simulation);

    simulation.deposit_signal(*stimulus,
        PackedLogic4(1U, Logic4::one));
    require(simulation.run(simulation.now() + 1U).status
                == RunStatus::time_limit,
        "simulation must retry through the rebuilt frontier runtime");
    require(NativeRegionAllocationTestAccess::summarize(simulation)
                .native_dispatches > dispatches_before_retry_run,
        "the rebuilt V2 frame must execute its real generated entry");
    require(NativeRegionAllocationTestAccess::workspace_extents(simulation)
                == workspace_before_retry_run,
        "the rebuilt runtime must execute without growing its bounded "
        "component workspace");
    require(simulation.read_signal_snapshot(*observed).to_msb_string() == "1",
        "the successful preparation retry must preserve boundary propagation");
}

} // namespace

int main()
{
    try {
        test_preparation_failure_is_transactional_and_retryable();
        std::cout << "native-frontier preparation failure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << "native-frontier preparation failure test failed: "
                  << error.what() << '\n';
        return 1;
    }
}
