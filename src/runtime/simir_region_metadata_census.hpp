// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "simir_internal.hpp"

#include <cstddef>
#include <cstdio>
#include <unordered_set>

namespace fsim::runtime::simir::region_graph_detail {

struct CapacityCensus final {
    std::size_t vector_count { };
    std::size_t vector_capacity_bytes { };
    std::size_t object_count { };
    std::size_t object_bytes { };
};

template <typename Vector>
void add_vector_capacity(CapacityCensus& census, const Vector& values) noexcept
{
    ++census.vector_count;
    census.vector_capacity_bytes += values.capacity()
        * sizeof(typename Vector::value_type);
}

inline void add_object(CapacityCensus& census,
    const std::size_t bytes) noexcept
{
    ++census.object_count;
    census.object_bytes += bytes;
}

inline void report_capacity_census(const char* const category,
    const std::uint64_t generation, const CapacityCensus& census) noexcept
{
    std::fprintf(stderr,
        "[fsim simir-metadata-census] generation=%llu "
        "scope=prepared-current-snapshot category=%s vectors=%zu "
        "vector_capacity_bytes_lower_bound=%zu objects=%zu "
        "object_bytes_lower_bound=%zu\n",
        static_cast<unsigned long long>(generation), category,
        census.vector_count, census.vector_capacity_bytes,
        census.object_count, census.object_bytes);
}

struct RegionGraphStorageCensusAccess final {
    static void add(CapacityCensus& census, const RegionGraph& graph) noexcept
    {
        add_vector_capacity(census, graph.processes_);
        add_vector_capacity(census, graph.signals_);
        add_vector_capacity(census, graph.signal_alias_families_);
        add_vector_capacity(census, graph.signal_alias_family_by_signal_);
        add_vector_capacity(census, graph.signal_alias_is_proxy_);
        add_vector_capacity(census,
            graph.signal_alias_invalidation_dependencies_);
        add_vector_capacity(census, graph.topological_order_);
        add_vector_capacity(census, graph.topological_rank_by_process_);
        add_vector_capacity(census, graph.capability_epochs_);
        add_vector_capacity(census, graph.certificate_component_by_process_);
        add_vector_capacity(census, graph.component_epochs_stale_);
        add_vector_capacity(census,
            graph.certificate_inventory_.components);
        add_vector_capacity(census,
            graph.certificate_inventory_.opaque_operation_counts);

        for (const auto& process : graph.processes_) {
            add_vector_capacity(census, process.sensitivities);
            add_vector_capacity(census, process.reads);
            add_vector_capacity(census, process.writes);
        }
        for (const auto& signal : graph.signals_) {
            add_vector_capacity(census, signal.readers);
            add_vector_capacity(census, signal.writers);
            add_vector_capacity(census,
                signal.invalidation_dependencies);
        }
        for (const auto& family : graph.signal_alias_families_) {
            add_vector_capacity(census, family.leaves);
        }
        for (const auto& dependencies
             : graph.signal_alias_invalidation_dependencies_) {
            add_vector_capacity(census, dependencies);
        }
        for (const auto& component
             : graph.certificate_inventory_.components) {
            add_vector_capacity(census, component.members);
            add_vector_capacity(census,
                component.structural_internal_signal_candidates);
            add_vector_capacity(census, component.boundary_signals);
            add_vector_capacity(census, component.captured_epochs);
            add_vector_capacity(census,
                component.runtime_single_driver_proof_signals);
        }
    }
};

template <typename Program>
void add_process_metadata(CapacityCensus& census,
    const Program& program) noexcept
{
    add_vector_capacity(census, program.debug_locals);
    add_vector_capacity(census, program.debug_string_locals);
    add_vector_capacity(census, program.debug_container_locals);
    add_vector_capacity(census, program.static_sensitivity);
    add_vector_capacity(census, program.driver_regions);
}

inline void add_activation_metadata(CapacityCensus& census,
    const RegionConeActivationKernel& kernel) noexcept
{
    add_process_metadata(census, kernel.program);
    add_vector_capacity(census, kernel.inputs);
    add_vector_capacity(census, kernel.members);
    add_vector_capacity(census, kernel.member_execution_order);
    add_vector_capacity(census, kernel.outputs);
    add_vector_capacity(census, kernel.internal_signals);
    add_vector_capacity(census, kernel.constant_inputs);
    for (const auto& member : kernel.members) {
        add_vector_capacity(census, member.sensitivities);
        add_vector_capacity(census, member.register_bindings);
    }
}

inline void add_program_metadata(CapacityCensus& census,
    const RegionConeProgram& program) noexcept
{
    add_activation_metadata(census, program.activation_kernel);
    add_vector_capacity(census, program.members);
    add_vector_capacity(census, program.member_spans);
    add_vector_capacity(census, program.boundary_sensitivities);
    add_vector_capacity(census, program.boundary_outputs);
    add_vector_capacity(census, program.internal_materializations);
    for (const auto& span : program.member_spans) {
        add_vector_capacity(census, span.sensitivities);
    }
    if (!program.forwarding_kernel) {
        return;
    }
    const auto& forwarding = *program.forwarding_kernel;
    add_activation_metadata(census, forwarding.execution_kernel);
    add_vector_capacity(census, forwarding.topological_member_indices);
    add_vector_capacity(census, forwarding.members);
    add_vector_capacity(census, forwarding.dependencies);
    add_vector_capacity(census, forwarding.internal_reads);
    add_vector_capacity(census, forwarding.internal_signals);
}

template <typename Snapshot>
void report_prepared_region_metadata_census(
    const Snapshot& snapshot)
{
    std::fprintf(stderr,
        "[fsim simir-metadata-census] generation=%llu "
        "scope=prepared-current-snapshot excludes=old-snapshots,transient-builders,"
        "operation-bodies,nested-element-payloads,frontier-arena,"
        "signal-value-planes,allocator-overhead,total-rss\n",
        static_cast<unsigned long long>(snapshot.generation));
    CapacityCensus graph;
    RegionGraphStorageCensusAccess::add(graph, snapshot.graph);
    report_capacity_census("region_graph", snapshot.generation, graph);

    CapacityCensus snapshot_vectors;
    add_vector_capacity(snapshot_vectors, snapshot.programs_by_component);
    add_vector_capacity(snapshot_vectors, snapshot.component_by_process);
    add_vector_capacity(snapshot_vectors, snapshot.backends_by_component);
    add_vector_capacity(snapshot_vectors,
        snapshot.backend_generation_by_component);
    add_vector_capacity(snapshot_vectors, snapshot.backend_pool);
    add_vector_capacity(snapshot_vectors,
        snapshot.forwarding_backends_by_component);
    add_vector_capacity(snapshot_vectors, snapshot.forwarding_backend_pool);
    add_vector_capacity(snapshot_vectors,
        snapshot.frontier_backends_by_component);
    add_vector_capacity(snapshot_vectors, snapshot.frontier_backend_pool);
    add_vector_capacity(snapshot_vectors,
        snapshot.frontier_runtime_by_component);
    add_vector_capacity(snapshot_vectors,
        snapshot.vhdl_projected_readiness_by_component);
    add_vector_capacity(snapshot_vectors,
        snapshot.authoritative_state_by_component);
    add_vector_capacity(snapshot_vectors, snapshot.local_wave_state_by_component);
    add_vector_capacity(snapshot_vectors,
        snapshot.value_only_recertification_by_component);
    add_vector_capacity(snapshot_vectors,
        snapshot.authoritative_component_by_signal);
    add_vector_capacity(snapshot_vectors,
        snapshot.authoritative_member_index_by_process);
    add_vector_capacity(snapshot_vectors, snapshot.readiness_mask_words);
    add_vector_capacity(snapshot_vectors, snapshot.readiness_mask_by_component);
    add_vector_capacity(snapshot_vectors,
        snapshot.readiness_member_index_by_process);
    add_vector_capacity(snapshot_vectors, snapshot.readiness_queued_by_process);
    add_vector_capacity(snapshot_vectors, snapshot.grouped_fanout_by_signal);
    add_vector_capacity(snapshot_vectors, snapshot.grouped_fanout_groups);
    add_vector_capacity(snapshot_vectors, snapshot.grouped_fanout_members);
    add_vector_capacity(snapshot_vectors,
        snapshot.grouped_fanout_sensitivity_ranges);
    add_vector_capacity(snapshot_vectors,
        snapshot.grouped_readiness_member_scratch);
    add_vector_capacity(snapshot_vectors,
        snapshot.grouped_readiness_receipt_scratch);
    add_vector_capacity(snapshot_vectors,
        snapshot.grouped_readiness_queue_scratch);
    add_vector_capacity(snapshot_vectors,
        snapshot.grouped_readiness_member_changed_scratch);
    add_vector_capacity(snapshot_vectors,
        snapshot.grouped_readiness_process_scratch);
    add_vector_capacity(snapshot_vectors,
        snapshot.prepared_successor_by_signal);
    add_vector_capacity(snapshot_vectors, snapshot.prepared_successor_readers);
    report_capacity_census("snapshot_vector_backings",
        snapshot.generation, snapshot_vectors);

    CapacityCensus programs;
    for (const auto& program : snapshot.programs_by_component) {
        if (program) {
            add_program_metadata(programs, *program);
        }
    }
    report_capacity_census("prepared_program_metadata",
        snapshot.generation, programs);

    CapacityCensus backends;
    std::unordered_set<const void*> seen_backends;
    std::size_t backend_references { };
    const auto add_backend = [&](const auto& backend) {
        if (!backend) {
            return;
        }
        ++backend_references;
        if (!seen_backends.insert(backend.get()).second) {
            return;
        }
        add_object(backends, sizeof(*backend));
        if constexpr (requires { backend->execution_kernel; }) {
            add_activation_metadata(backends, backend->execution_kernel);
            add_vector_capacity(backends, backend->topological_member_indices);
            add_vector_capacity(backends, backend->members);
            add_vector_capacity(backends, backend->dependencies);
            add_vector_capacity(backends, backend->internal_reads);
            add_vector_capacity(backends, backend->internal_signals);
        } else if constexpr (requires { backend->kernel; }) {
            if constexpr (requires { backend->kernel.execution_kernel; }) {
                add_activation_metadata(backends,
                    backend->kernel.execution_kernel);
                add_vector_capacity(backends,
                    backend->kernel.topological_member_indices);
                add_vector_capacity(backends, backend->kernel.members);
                add_vector_capacity(backends, backend->kernel.dependencies);
                add_vector_capacity(backends, backend->kernel.internal_reads);
                add_vector_capacity(backends, backend->kernel.internal_signals);
            } else {
                add_activation_metadata(backends, backend->kernel);
            }
        }
    };
    for (const auto& backend : snapshot.backends_by_component) {
        add_backend(backend);
    }
    for (const auto& backend : snapshot.backend_pool) {
        add_backend(backend);
    }
    for (const auto& backend : snapshot.forwarding_backends_by_component) {
        add_backend(backend);
    }
    for (const auto& backend : snapshot.forwarding_backend_pool) {
        add_backend(backend);
    }
    for (const auto& backend : snapshot.frontier_backends_by_component) {
        add_backend(backend);
    }
    for (const auto& backend : snapshot.frontier_backend_pool) {
        add_backend(backend);
    }
    CapacityCensus runtime_targets;
    std::unordered_set<const void*> seen_runtime_targets;
    std::size_t runtime_references { };
    std::size_t unique_current_frontier_runtimes { };
    std::size_t configured_frontier_arenas { };
    std::size_t frontier_arena_capacity_bytes { };
    std::size_t frontier_arena_used_bytes { };
    std::size_t writable_layout_index_capacity_bytes { };
    std::size_t writable_layout_index_vector_header_bytes { };
    const auto add_runtime_target = [&](const auto& target) {
        if (!target) {
            return false;
        }
        ++runtime_references;
        return seen_runtime_targets.insert(target.get()).second;
    };
    for (const auto& runtime : snapshot.frontier_runtime_by_component) {
        if (!add_runtime_target(runtime)) {
            continue;
        }
        ++unique_current_frontier_runtimes;
        writable_layout_index_vector_header_bytes
            += sizeof(runtime->writable_layout_indices);
        const auto arena_capacity = runtime->workspace_allocation.capacity();
        if (arena_capacity != 0U) {
            ++configured_frontier_arenas;
            writable_layout_index_capacity_bytes
                += runtime->writable_layout_indices.capacity()
                    * sizeof(std::size_t);
            frontier_arena_capacity_bytes += arena_capacity;
            frontier_arena_used_bytes
                += runtime->workspace_allocation.used();
        }
        add_object(runtime_targets, sizeof(*runtime));
        add_vector_capacity(runtime_targets, runtime->alias_certificate_ranges);
        add_vector_capacity(runtime_targets, runtime->alias_candidate_ranges);
        add_vector_capacity(runtime_targets, runtime->alias_sorted_indices);
        add_backend(runtime->backend);
        if (add_runtime_target(runtime->authoritative_state)) {
            add_object(runtime_targets,
                sizeof(*runtime->authoritative_state));
        }
    }
    for (const auto& task : snapshot.vhdl_projected_readiness_by_component) {
        if (!add_runtime_target(task)) {
            continue;
        }
        add_object(runtime_targets, sizeof(*task));
        add_vector_capacity(runtime_targets, task->members);
        add_vector_capacity(runtime_targets, task->ticket_member_offsets);
        add_backend(task->backend);
    }
    for (const auto& state : snapshot.authoritative_state_by_component) {
        if (add_runtime_target(state)) {
            add_object(runtime_targets, sizeof(*state));
        }
    }
    for (const auto& state : snapshot.local_wave_state_by_component) {
        if (!add_runtime_target(state)) {
            continue;
        }
        add_object(runtime_targets, sizeof(*state));
        add_vector_capacity(runtime_targets, state->internal_output_indices);
    }
    report_capacity_census("unique_backend_targets", snapshot.generation,
        backends);
    report_capacity_census("unique_runtime_targets", snapshot.generation,
        runtime_targets);
    std::fprintf(stderr,
        "[fsim simir-metadata-census] generation=%llu "
        "scope=prepared-current-snapshot category=target_references "
        "backend_references=%zu unique_backends=%zu "
        "runtime_references=%zu unique_runtime_targets=%zu\n",
        static_cast<unsigned long long>(snapshot.generation),
        backend_references, seen_backends.size(), runtime_references,
        seen_runtime_targets.size());
    std::fprintf(stderr,
        "[fsim frontier-runtime-arena-census] generation=%llu "
        "scope=distinct-current-snapshot-runtime-arenas "
        "unique_current_runtimes=%zu configured_arenas=%zu "
        "arena_capacity_bytes=%zu arena_used_bytes=%zu "
        "writable_layout_index_capacity_bytes=%zu "
        "writable_layout_index_vector_header_bytes=%zu "
        "index_capacity_scope=configured-runtime-arenas "
        "index_capacity_subset_of=arena_capacity_bytes "
        "index_headers_subset_of=unique_runtime_targets.object_bytes_lower_bound "
        "includes=workspace-vectors,authoritative-role-plane-words-when-rehomed "
        "excludes=nonarena-runtime-vectors,arenas-without-current-runtime-owner,"
        "allocator-overhead,process-RSS\n",
        static_cast<unsigned long long>(snapshot.generation),
        unique_current_frontier_runtimes, configured_frontier_arenas,
        frontier_arena_capacity_bytes, frontier_arena_used_bytes,
        writable_layout_index_capacity_bytes,
        writable_layout_index_vector_header_bytes);

    if (snapshot.signal_driver_inventory) {
        CapacityCensus inventory;
        add_object(inventory, sizeof(*snapshot.signal_driver_inventory));
        add_vector_capacity(inventory, snapshot.signal_driver_inventory->signals);
        add_vector_capacity(inventory, snapshot.signal_driver_inventory->writers);
        add_vector_capacity(inventory, snapshot.signal_driver_inventory->owners);
        add_vector_capacity(inventory,
            snapshot.signal_driver_inventory->owner_mask_words);
        report_capacity_census("signal_driver_inventory",
            snapshot.generation, inventory);
    }
}

} // namespace fsim::runtime::simir::region_graph_detail
