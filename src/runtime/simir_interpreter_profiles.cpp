// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {
namespace {

constexpr std::array<std::pair<RegionProcessExclusionReason, std::string_view>,
    static_cast<std::size_t>(RegionProcessExclusionReason::count)>
    process_exclusion_fields {{
        { RegionProcessExclusionReason::not_pure,
            "process_exclusion_not_pure" },
        { RegionProcessExclusionReason::wrong_scheduling_domain,
            "process_exclusion_wrong_scheduling_domain" },
        { RegionProcessExclusionReason::wrong_update_kind,
            "process_exclusion_wrong_update_kind" },
        { RegionProcessExclusionReason::cyclic_or_dependent_on_cycle,
            "process_exclusion_cyclic_or_dependent_on_cycle" },
        { RegionProcessExclusionReason::unknown_dependencies,
            "process_exclusion_unknown_dependencies" },
        { RegionProcessExclusionReason::edge_sensitivity,
            "process_exclusion_edge_sensitivity" },
    }};

constexpr std::array<std::pair<RegionBoundaryReason, std::string_view>,
    static_cast<std::size_t>(RegionBoundaryReason::count)>
    boundary_reason_fields {{
        { RegionBoundaryReason::no_internal_writer,
            "boundary_no_internal_writer" },
        { RegionBoundaryReason::writer_outside_component,
            "boundary_writer_outside_component" },
        { RegionBoundaryReason::reader_outside_component,
            "boundary_reader_outside_component" },
        { RegionBoundaryReason::unknown_driver_ownership,
            "boundary_unknown_driver_ownership" },
        { RegionBoundaryReason::partial_driver,
            "boundary_partial_driver" },
        { RegionBoundaryReason::resolved_driver_class,
            "boundary_resolved_driver_class" },
        { RegionBoundaryReason::unsupported_resolution_mode,
            "boundary_unsupported_resolution_mode" },
        { RegionBoundaryReason::implicit_driver,
            "boundary_implicit_driver" },
        { RegionBoundaryReason::external_driver,
            "boundary_external_driver" },
        { RegionBoundaryReason::event_signal,
            "boundary_event_signal" },
        { RegionBoundaryReason::unsupported_width,
            "boundary_unsupported_width" },
        { RegionBoundaryReason::unsupported_value_kind,
            "boundary_unsupported_value_kind" },
        { RegionBoundaryReason::partial_projected_transactions,
            "boundary_partial_projected_transactions" },
        { RegionBoundaryReason::access_inventory_incomplete,
            "boundary_access_inventory_incomplete" },
        { RegionBoundaryReason::observed_current,
            "boundary_observed_current" },
        { RegionBoundaryReason::observed_previous,
            "boundary_observed_previous" },
        { RegionBoundaryReason::observed_drivers,
            "boundary_observed_drivers" },
        { RegionBoundaryReason::observed_pending,
            "boundary_observed_pending" },
        { RegionBoundaryReason::observed_events,
            "boundary_observed_events" },
        { RegionBoundaryReason::observed_mutation,
            "boundary_observed_mutation" },
        { RegionBoundaryReason::observed_coverage,
            "boundary_observed_coverage" },
        { RegionBoundaryReason::observed_unknown,
            "boundary_observed_unknown" },
    }};

template<typename Reason, std::size_t count>
void print_reason_incidence_counts(
    std::ostream& output,
    const std::array<std::size_t, count>& values,
    const std::array<std::pair<Reason, std::string_view>, count>& fields)
{
    static_assert(count == static_cast<std::size_t>(Reason::count));
    for (const auto& [reason, name] : fields) {
        output << ' ' << name << '='
               << values[static_cast<std::size_t>(reason)];
    }
}

void print_structural_census(std::ostream& output, const RegionGraph& graph)
{
    // This is a build-time structural inventory, not runtime admission. An
    // incomplete access inventory is graph-wide, and reason counts below are
    // overlapping process/reason or boundary-signal/reason incidences.
    const auto& inventory = graph.certificate_inventory();
    std::size_t structural_candidate_components { };
    std::size_t no_internal_state_components { };
    std::size_t incomplete_access_inventory_components { };
    std::size_t current_epoch_components { };
    std::size_t stale_epoch_components { };
    std::size_t current_epoch_structural_candidate_components { };
    std::size_t stale_epoch_structural_candidate_components { };
    std::size_t component_member_processes { };
    std::size_t structural_candidate_member_processes { };
    std::size_t boundary_signals { };
    std::size_t candidate_internal_signals { };
    std::size_t candidate_internal_none_signals { };
    std::size_t candidate_internal_sv_wire_proof_signals { };
    std::size_t candidate_internal_other_resolution_signals { };
    std::size_t opaque_operation_incidences { };
    std::vector<const RegionOpaqueOperationCount*> opaque_operation_kinds;
    opaque_operation_kinds.reserve(inventory.opaque_operation_counts.size());
    for (const auto& operation : inventory.opaque_operation_counts) {
        opaque_operation_incidences += operation.incidences;
        opaque_operation_kinds.push_back(&operation);
    }
    std::ranges::sort(opaque_operation_kinds,
        [](const auto* left, const auto* right) {
            return left->incidences != right->incidences
                ? left->incidences > right->incidences
                : left->type_name < right->type_name;
        });

    for (std::size_t component_index = 0U;
         component_index < inventory.components.size(); ++component_index) {
        const auto& component = inventory.components[component_index];
        component_member_processes += component.members.size();
        boundary_signals += component.boundary_signals.size();
        const bool epochs_current
            = graph.component_epochs_current(component_index);
        if (epochs_current) {
            ++current_epoch_components;
        } else {
            ++stale_epoch_components;
        }
        switch (component.status) {
        case RegionComponentCertificateStatus::structural_candidate:
            ++structural_candidate_components;
            structural_candidate_member_processes += component.members.size();
            if (epochs_current) {
                ++current_epoch_structural_candidate_components;
            } else {
                ++stale_epoch_structural_candidate_components;
            }
            break;
        case RegionComponentCertificateStatus::no_internal_state:
            ++no_internal_state_components;
            break;
        case RegionComponentCertificateStatus::incomplete_access_inventory:
            ++incomplete_access_inventory_components;
            break;
        }

        candidate_internal_signals
            += component.structural_internal_signal_candidates.size();
        candidate_internal_sv_wire_proof_signals
            += component.runtime_single_driver_proof_signals.size();
        for (const auto signal_id
            : component.structural_internal_signal_candidates) {
            const auto resolution = graph.signals()[signal_id].descriptor.resolution;
            if (resolution == ResolutionKind::none) {
                ++candidate_internal_none_signals;
            } else if (resolution != ResolutionKind::sv_wire) {
                ++candidate_internal_other_resolution_signals;
            }
        }
    }

    output << "fsim-profile: sv-region-structural-census"
           << " components=" << inventory.components.size()
           << " structural_candidate_components="
           << structural_candidate_components
           << " no_internal_state_components="
           << no_internal_state_components
           << " incomplete_access_inventory_components="
           << incomplete_access_inventory_components
           << " current_epoch_components=" << current_epoch_components
           << " stale_epoch_components=" << stale_epoch_components
           << " current_epoch_structural_candidate_components="
           << current_epoch_structural_candidate_components
           << " stale_epoch_structural_candidate_components="
           << stale_epoch_structural_candidate_components
           << " component_member_processes=" << component_member_processes
           << " structural_candidate_member_processes="
           << structural_candidate_member_processes
           << " boundary_signals=" << boundary_signals
           << " candidate_internal_signals=" << candidate_internal_signals
           << " candidate_internal_none_resolution_signals="
           << candidate_internal_none_signals
           << " candidate_internal_sv_wire_requires_runtime_single_driver_proof_signals="
           << candidate_internal_sv_wire_proof_signals
           << " candidate_internal_other_resolution_signals="
           << candidate_internal_other_resolution_signals
           << " graph_access_inventory_complete="
           << (inventory.access_inventory_complete ? 1 : 0)
           << " opaque_operation_incidences="
           << opaque_operation_incidences
           << " opaque_operation_kinds=";
    constexpr std::size_t maximum_printed_opaque_operation_kinds = 8U;
    const auto printed_opaque_operation_kinds = std::min(
        opaque_operation_kinds.size(),
        maximum_printed_opaque_operation_kinds);
    if (printed_opaque_operation_kinds == 0U) {
        output << "none";
    } else {
        for (std::size_t index = 0U;
             index < printed_opaque_operation_kinds; ++index) {
            if (index != 0U) {
                output << ';';
            }
            const auto& operation = *opaque_operation_kinds[index];
            for (const char character : operation.type_name) {
                output << (std::isspace(
                              static_cast<unsigned char>(character)) != 0
                        ? '_'
                        : character);
            }
            output << ':' << operation.incidences;
        }
    }
    output << " opaque_operation_kinds_omitted="
           << opaque_operation_kinds.size() - printed_opaque_operation_kinds
           << " opaque_operation_count_semantics=instruction_occurrences"
           << " access_inventory_scope=whole_graph"
           << " inventory_scope=build_snapshot"
           << " runtime_execution_proof=not_evaluated"
           << " reason_count_semantics=overlapping_incidences";
    print_reason_incidence_counts(output,
        inventory.process_exclusion_counts, process_exclusion_fields);
    print_reason_incidence_counts(output,
        inventory.boundary_reason_counts, boundary_reason_fields);
    output << '\n';
}

} // namespace

Interpreter::Interpreter(
    SchedulerOptions options,
    const std::uint64_t seed)
    : impl_(std::make_unique<Impl>(options, seed))
{
}
Interpreter::~Interpreter()
{
    if (!impl_) {
        return;
    }
    if (impl_->systemverilog_wave_profile_enabled) {
        const auto batch_compaction
            = impl_->scheduler.systemverilog_batch_compaction_stats();
        const auto alias_misses = [&](
                                      const Impl::RegionFrontierAliasMissReason reason) {
            return impl_->systemverilog_wave_profile_alias_misses[
                static_cast<std::size_t>(reason)];
        };
        std::cerr << "fsim-profile: sv-frontier-alias-summary checked="
                  << impl_->systemverilog_wave_profile_alias_checked_entries
                  << " trusted="
                  << impl_->systemverilog_wave_profile_alias_trusted_entries
                  << " unavailable="
                  << impl_->systemverilog_wave_profile_alias_unavailable_entries
                  << " forced_staged="
                  << impl_->systemverilog_wave_profile_alias_forced_staged_entries
                  << " unprimed="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::unprimed)
                  << " context_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::context)
                  << " collector_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::collector)
                  << " task_extent_only_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::task_extent_only)
                  << " task_extent_with_other_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::task_extent_with_other)
                  << " pointer_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::pointer)
                  << " other_extent_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::extent)
                  << " tag_misses="
                  << alias_misses(Impl::RegionFrontierAliasMissReason::alias_tag)
                  << " confirmation_attempts="
                  << impl_->systemverilog_wave_profile_alias_confirmation_attempts
                  << " confirmation_successes="
                  << impl_->systemverilog_wave_profile_alias_confirmation_successes
                  << " confirmation_failures="
                  << impl_->systemverilog_wave_profile_alias_confirmation_failures
                  << '\n';
        std::cerr << "fsim-profile: sv-ordered-wave-summary offered_batches="
                  << impl_->systemverilog_wave_profile_calls
                  << " offered_members=" << impl_->systemverilog_wave_profile_offered_members
                  << " accepted_batches=" << impl_->systemverilog_wave_profile_accepted_calls
                  << " accepted_members=" << impl_->systemverilog_wave_profile_accepted_members
                  << " zero_accepted_batches=" << impl_->systemverilog_wave_profile_declined_calls
                  << " failed_batches=" << impl_->systemverilog_wave_profile_failed_calls
                  << " region_kernel_attempts="
                  << impl_->systemverilog_wave_profile_region_kernel_attempts
                  << " region_kernel_runs="
                  << impl_->systemverilog_wave_profile_region_kernel_runs
                  << " native_frontier_member_dispatches="
                  << impl_->systemverilog_wave_profile_native_frontier_member_dispatches
                  << " native_frontier_budget_trims="
                  << impl_->systemverilog_wave_profile_native_frontier_budget_trims
                  << " v2_selected_forwarding_prefixes="
                  << impl_->systemverilog_wave_profile_v2_selected_forwarding_prefixes
                  << " v2_selected_forwarding_members="
                  << impl_->systemverilog_wave_profile_v2_selected_forwarding_members
                  << " region_forwarding_attempts="
                  << impl_->systemverilog_wave_profile_region_forwarding_attempts
                  << " region_forwarding_evaluations="
                  << impl_->systemverilog_wave_profile_region_forwarding_evaluations
                  << " region_forwarding_member_consumptions="
                  << impl_->systemverilog_wave_profile_region_forwarding_member_consumptions
                  << " region_forwarding_declines="
                  << impl_->systemverilog_wave_profile_region_forwarding_declines
                  << " region_forwarding_stage_attempts="
                  << impl_->systemverilog_wave_profile_region_forwarding_stage_attempts
                  << " region_forwarding_stage_dispatches="
                  << impl_->systemverilog_wave_profile_region_forwarding_stage_dispatches
                  << " region_forwarding_stage_members="
                  << impl_->systemverilog_wave_profile_region_forwarding_stage_members
                  << " region_forwarding_stage_declines="
                  << impl_->systemverilog_wave_profile_region_forwarding_stage_declines
                  << " region_forwarding_stage_fallback_descriptors="
                  << impl_->systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors
                  << " region_forwarding_private_parent_slots_elided="
                  << impl_->systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
                  << " region_forwarding_private_parent_dispatches="
                  << impl_->systemverilog_wave_profile_region_forwarding_private_parent_dispatches
                  << " region_forwarding_private_parent_fallbacks="
                  << impl_->systemverilog_wave_profile_region_forwarding_private_parent_fallbacks
                  << " region_forwarding_public_update_tokens="
                  << impl_->systemverilog_wave_profile_region_forwarding_public_update_tokens
                  << " region_kernel_members="
                  << impl_->systemverilog_wave_profile_region_kernel_members
                  << " region_kernel_publications="
                  << impl_->systemverilog_wave_profile_region_kernel_publications
                  << " region_kernel_failures="
                  << impl_->systemverilog_wave_profile_region_kernel_failures
                  << " region_backend_attempts="
                  << impl_->systemverilog_wave_profile_region_backend_attempts
                  << " region_backend_runs="
                  << impl_->systemverilog_wave_profile_region_backend_runs
                  << " region_backend_completions="
                  << impl_->systemverilog_wave_profile_region_backend_completions
                  << " component_batch_tickets="
                  << batch_compaction.tickets
                  << " component_batch_members="
                  << batch_compaction.members
                  << " component_batch_entries_elided="
                  << batch_compaction.entries_elided
                  << " component_batch_direct_dispatches="
                  << batch_compaction.direct_dispatches
                  << " component_batch_direct_members="
                  << batch_compaction.direct_members
                  << " component_readiness_queue_insertions="
                  << batch_compaction.readiness_ticket_queue_insertions
                  << " component_readiness_members="
                  << batch_compaction.readiness_ticket_members
                  << " component_readiness_members_elided="
                  << batch_compaction.readiness_ticket_members_elided
                  << " component_readiness_fallback_members="
                  << batch_compaction.readiness_ticket_fallback_members
                  << " component_readiness_mask_images="
                  << impl_->systemverilog_wave_profile_readiness_mask_images
                  << " region_trace_declines="
                  << impl_->systemverilog_wave_profile_region_trace_declines
                  << " region_recert_attempts="
                  << impl_->systemverilog_wave_profile_region_recertification_attempts
                  << " region_recert_successes="
                  << impl_->systemverilog_wave_profile_region_recertification_successes
                  << " region_recert_failures="
                  << impl_->systemverilog_wave_profile_region_recertification_failures
                  << " region_recert_processes="
                  << impl_->systemverilog_wave_profile_region_recertification_processes
                  << " region_recert_signals="
                  << impl_->systemverilog_wave_profile_region_recertification_signals
                  << " region_recert_components="
                  << impl_->systemverilog_wave_profile_region_recertification_components
                  << " a4_native_input_handoffs="
                  << impl_->systemverilog_wave_profile_a4_native_input_handoffs
                  << " a4_native_input_completions="
                  << impl_->systemverilog_wave_profile_a4_native_input_completions
                  << " prepared_output_batches="
                  << impl_->systemverilog_wave_profile_prepared_output_batches
                  << " generated_successor_mask_batches="
                  << impl_->systemverilog_wave_profile_generated_successor_mask_batches
                  << " direct_ready_window_attempts="
                  << impl_->systemverilog_wave_profile_direct_ready_window_attempts
                  << " direct_ready_window_completions="
                  << impl_->systemverilog_wave_profile_direct_ready_window_completions
                  << " prepared_output_seals="
                  << impl_->systemverilog_wave_profile_prepared_output_seals
                  << " prepared_output_fallbacks="
                  << impl_->systemverilog_wave_profile_prepared_output_fallbacks
                  << " prepared_output_group_dispatches="
                  << impl_->systemverilog_wave_profile_prepared_output_group_dispatches
                  << " prepared_output_group_members="
                  << impl_->systemverilog_wave_profile_prepared_output_group_members
                  << " a2_local_update_dispatches="
                  << impl_->systemverilog_wave_profile_a2_local_update_dispatches
                  << " a2_local_update_fallbacks="
                  << impl_->systemverilog_wave_profile_a2_local_update_fallbacks
                  << " a2_local_fanout_suppressions="
                  << impl_->systemverilog_wave_profile_a2_local_fanout_suppressions
                  << " a2_ordinary_internal_updates="
                  << impl_->systemverilog_wave_profile_a2_ordinary_internal_updates
                  << " a2_internal_seed_reads="
                  << impl_->systemverilog_wave_profile_a2_internal_seed_reads
                  << " a2_internal_state_seeds="
                  << impl_->systemverilog_wave_profile_a2_internal_state_seeds
                  << " a2_internal_state_reuses="
                  << impl_->systemverilog_wave_profile_a2_internal_state_reuses
                  << " a3_private_update_ticket_entries="
                  << impl_->systemverilog_wave_profile_a3_private_update_ticket_entries
                  << " a3_private_update_ticket_members="
                  << impl_->systemverilog_wave_profile_a3_private_update_ticket_members
                  << " a3_private_update_entries_elided="
                  << impl_->systemverilog_wave_profile_a3_private_update_entries_elided
                  << " a3_private_update_fallback_members="
                  << impl_->systemverilog_wave_profile_a3_private_update_fallback_members
                  << " a2_grouped_fanout_members="
                  << impl_->systemverilog_wave_profile_a2_grouped_fanout_members
                  << " a2_completion_fast_batches="
                  << impl_->systemverilog_wave_profile_a2_completion_fast_batches
                  << " a2_completion_fast_members="
                  << impl_->systemverilog_wave_profile_a2_completion_fast_members
                  << " a2_completion_preflights="
                  << impl_->systemverilog_wave_profile_a2_completion_preflights
                  << " a2_completion_register_declines="
                  << impl_->systemverilog_wave_profile_a2_completion_register_declines
                  << " p3_group_fanout_groups="
                  << impl_->systemverilog_wave_profile_p3_group_fanout_groups
                  << " p3_group_fanout_tickets="
                  << impl_->systemverilog_wave_profile_p3_group_fanout_tickets
                  << " p3_group_fanout_members="
                  << impl_->systemverilog_wave_profile_p3_group_fanout_members
                  << " p3_group_fanout_declines="
                  << impl_->systemverilog_wave_profile_p3_group_fanout_declines
                  << " a3_mapped_successor_batches="
                  << impl_->systemverilog_wave_profile_a3_mapped_successor_batches
                  << " a3_mapped_successor_readers="
                  << impl_->systemverilog_wave_profile_a3_mapped_successor_readers
                  << " p3_oversized_group_fallbacks="
                  << impl_->region_grouped_fanout_oversized_group_fallbacks
                  << '\n';
        std::cerr << "fsim-profile: generic-projected-region-summary attempts="
                  << impl_->generic_projected_region_attempts
                  << " backend_runs="
                  << impl_->generic_projected_region_backend_runs
                  << " completions="
                  << impl_->generic_projected_region_completions
                  << " members="
                  << impl_->generic_projected_region_members
                  << " publications="
                  << impl_->generic_projected_region_publications
                  << " declines="
                  << impl_->generic_projected_region_declines
                  << " failures="
                  << impl_->generic_projected_region_failures << '\n';
        std::cerr << "fsim-profile: a4-state-summary seeded_components="
                  << impl_->systemverilog_wave_profile_a4_seeded_components
                  << " seeded_signals="
                  << impl_->systemverilog_wave_profile_a4_seeded_signals
                  << " seeded_owners="
                  << impl_->systemverilog_wave_profile_a4_seeded_owners
                  << " slot_bind_components="
                  << impl_->systemverilog_wave_profile_a4_slot_bind_components
                  << " slot_bindings="
                  << impl_->systemverilog_wave_profile_a4_slot_bindings
                  << " slot_rebinds="
                  << impl_->systemverilog_wave_profile_a4_slot_rebinds
                  << " bound_components="
                  << impl_->systemverilog_wave_profile_a4_bound_components
                  << " bound_slots="
                  << impl_->systemverilog_wave_profile_a4_bound_slots
                  << " materialized_components="
                  << impl_->systemverilog_wave_profile_a4_materialized_components
                  << " materialized_slots="
                  << impl_->systemverilog_wave_profile_a4_materialized_slots
                  << " authoritative_slot_writes="
                  << impl_->systemverilog_wave_profile_a4_authoritative_slot_writes
                  << " unresolved_owner_alias_commits="
                  << impl_->systemverilog_wave_profile_a4_unresolved_owner_alias_commits
                  << " stored_mirrors="
                  << impl_->systemverilog_wave_profile_a4_stored_mirrors
                  << " visible_mirrors="
                  << impl_->systemverilog_wave_profile_a4_visible_mirrors
                  << " owner_mirrors="
                  << impl_->systemverilog_wave_profile_a4_owner_mirrors
                  << " invalidations="
                  << impl_->systemverilog_wave_profile_a4_invalidations
                  << " value_marks="
                  << impl_->systemverilog_wave_profile_a4_value_marks
                  << " transaction_marks="
                  << impl_->systemverilog_wave_profile_a4_transaction_marks
                  << " ready_consumptions="
                  << impl_->systemverilog_wave_profile_a4_ready_consumptions
                  << '\n';
        if (impl_->region_graph) {
            print_structural_census(std::cerr, *impl_->region_graph);
        }
    }
    if (impl_->native_phase_profile_enabled) {
        std::cerr << "fsim-profile: native-phase attempts="
                  << impl_->native_phase_profile_attempts
                  << " published=" << impl_->native_phase_profile_published
                  << " shape_certificate_hits="
                  << impl_->native_phase_profile_shape_certificate_hits
                  << " shape_certificate_misses="
                  << impl_->native_phase_profile_shape_certificate_misses
                  << " rejected_structure="
                  << impl_->native_phase_profile_rejected_structure
                  << " rejected_observer="
                  << impl_->native_phase_profile_rejected_observer
                  << " rejected_route="
                  << impl_->native_phase_profile_rejected_route
                  << " rejected_semantics="
                  << impl_->native_phase_profile_rejected_semantics
                  << " rejected_runtime="
                  << impl_->native_phase_profile_rejected_runtime
                  << " single_resumes="
                  << impl_->native_phase_profile_single_resumes
                  << " cohort_resumes="
                  << impl_->native_phase_profile_cohort_resumes
                  << " cohort_members="
                  << impl_->native_phase_profile_cohort_members
                  << " module_paths=" << impl_->module_paths.size()
                  << " timing_checks=" << impl_->module_timing_checks.size()
                  << " sampled=" << impl_->requires_sampled_values
                  << " signal_hook=" << static_cast<bool>(impl_->signal_change_hook)
                  << " stored_hook="
                  << static_cast<bool>(impl_->stored_signal_change_hook)
                  << " driver_hook="
                  << static_cast<bool>(impl_->driver_change_hook)
                  << " scalar_hook="
                  << static_cast<bool>(impl_->scalar_signal_change_hook)
                  << " container_hook="
                  << static_cast<bool>(impl_->container_object_change_hook)
                  << " monitor=" << static_cast<bool>(impl_->monitor)
                  << '\n';
    }
    if (impl_->native_process_count_profile_enabled) {
        for (std::size_t id = 0; id < impl_->processes.size(); ++id) {
            const auto interpreted
                = impl_->processes.interpreter_operations(
                    static_cast<ProcessId>(id));
            if (interpreted == 0U) {
                continue;
            }
            const auto* const compact = impl_->processes.compact_constant(
                static_cast<ProcessId>(id));
            const auto* const full = impl_->processes.full_state_if_present(
                static_cast<ProcessId>(id));
            const auto program = impl_->processes.program_view(
                static_cast<ProcessId>(id));
            const auto native_resumes
                = id < impl_->native_process_resume_counts.size()
                    ? impl_->native_process_resume_counts[id] : 0U;
            std::cerr << "fsim-profile: interpreted-process id=" << id
                      << " operations=" << interpreted
                      << " native_resumes=" << native_resumes
                      << " executor="
                      << (full != nullptr
                              && static_cast<bool>(full->executor))
                      << " deferred="
                      << (full != nullptr
                              && static_cast<bool>(
                                  full->cold().deferred_executor))
                      << " halted="
                      << (compact != nullptr ? compact->halted
                                             : full->halted)
                      << " suspended="
                      << (compact != nullptr ? compact->suspended
                                             : full->suspended)
                      << " program_operations="
                      << impl_->processes.operation_count(
                          static_cast<ProcessId>(id))
                      << " name='" << program.name() << "'\n";
        }
        const auto total = std::accumulate(
            impl_->native_process_resume_counts.begin(),
            impl_->native_process_resume_counts.end(),
            std::uint64_t { });
        const auto single_total = std::accumulate(
            impl_->native_process_single_resume_counts.begin(),
            impl_->native_process_single_resume_counts.end(),
            std::uint64_t { });
        const auto single_static_wait = std::accumulate(
            impl_->native_process_single_static_wait_counts.begin(),
            impl_->native_process_single_static_wait_counts.end(),
            std::uint64_t { });
        const auto cohort_total = std::accumulate(
            impl_->native_process_cohort_resume_counts.begin(),
            impl_->native_process_cohort_resume_counts.end(),
            std::uint64_t { });
        const auto cohort_static_wait = std::accumulate(
            impl_->native_process_cohort_static_wait_counts.begin(),
            impl_->native_process_cohort_static_wait_counts.end(),
            std::uint64_t { });
        std::cerr << "fsim-profile: native-process-counts processes="
                  << impl_->native_process_resume_counts.size()
                  << " resumes=" << total
                  << " single=" << single_total
                  << " single_static_wait=" << single_static_wait
                  << " cohort=" << cohort_total
                  << " cohort_static_wait=" << cohort_static_wait
                  << " word_changes=" << impl_->native_process_word_changes
                  << " word_fanout_matches="
                  << impl_->native_process_word_fanout_matches
                  << " word_fanout_ready="
                  << impl_->native_process_word_fanout_ready
                  << " single_boundaries="
                  << impl_->native_process_single_boundary_counts[0] << ','
                  << impl_->native_process_single_boundary_counts[1] << ','
                  << impl_->native_process_single_boundary_counts[2] << ','
                  << impl_->native_process_single_boundary_counts[3] << ','
                  << impl_->native_process_single_boundary_counts[4] << ','
                  << impl_->native_process_single_boundary_counts[5]
                  << " cohort_boundaries="
                  << impl_->native_process_cohort_boundary_counts[0] << ','
                  << impl_->native_process_cohort_boundary_counts[1] << ','
                  << impl_->native_process_cohort_boundary_counts[2] << ','
                  << impl_->native_process_cohort_boundary_counts[3] << ','
                  << impl_->native_process_cohort_boundary_counts[4] << ','
                  << impl_->native_process_cohort_boundary_counts[5]
                  << " simir_groups="
                  << impl_->native_process_simir_boundary_groups[0] << ','
                  << impl_->native_process_simir_boundary_groups[1] << ','
                  << impl_->native_process_simir_boundary_groups[2] << ','
                  << impl_->native_process_simir_boundary_groups[3] << ','
                  << impl_->native_process_simir_boundary_groups[4] << ','
                  << impl_->native_process_simir_boundary_groups[5] << ','
                  << impl_->native_process_simir_boundary_groups[6] << ','
                  << impl_->native_process_simir_boundary_groups[7] << ','
                  << impl_->native_process_simir_boundary_groups[8] << '\n';
        for (std::size_t index = 0;
             index < impl_->native_process_scheduling_boundaries.size();
             ++index) {
            if (impl_->native_process_scheduling_boundaries[index] != 0U) {
                std::cerr << "fsim-profile: native-scheduling-boundary index="
                          << index << " count="
                          << impl_->native_process_scheduling_boundaries[index]
                          << '\n';
            }
        }
        for (std::size_t id = 0;
             id < impl_->native_process_resume_counts.size(); ++id) {
            const auto count = impl_->native_process_resume_counts[id];
            if (count == 0U || id >= impl_->processes.size()) {
                continue;
            }
            const auto process_id = static_cast<ProcessId>(id);
            const auto program = impl_->processes.program_view(process_id);
            std::cerr << "fsim-profile: native-process-count id=" << id
                      << " resumes=" << count
                      << " single="
                      << impl_->native_process_single_resume_counts[id]
                      << " single_static_wait="
                      << impl_->native_process_single_static_wait_counts[id]
                      << " cohort="
                      << impl_->native_process_cohort_resume_counts[id]
                      << " cohort_static_wait="
                      << impl_->native_process_cohort_static_wait_counts[id]
                      << " word_fanout_ready="
                      << impl_->native_process_word_fanout_ready_counts[id]
                      << " sensitivity="
                      << program.static_sensitivity().size()
                      << " operations="
                      << impl_->processes.operation_count(process_id)
                      << " name='" << program.name()
                      << "'\n";
            if (!program.static_sensitivity().empty()) {
                std::cerr << "fsim-profile: native-process-sensitivity id=" << id
                          << " signals=";
                const auto& sensitivity = program.static_sensitivity();
                for (std::size_t item = 0; item < sensitivity.size(); ++item) {
                    if (item != 0U) {
                        std::cerr << ',';
                    }
                    const auto signal = sensitivity[item].signal;
                    std::cerr << static_cast<std::size_t>(signal);
                    if (signal < impl_->signals.size()) {
                        std::cerr << ":'" << impl_->signal_cold[signal].name
                                  << "'";
                    }
                }
                std::cerr << '\n';
            }
            if (id < impl_->native_process_static_trigger_counts.size()
                && !impl_->native_process_static_trigger_counts[id].empty()) {
                std::vector<std::pair<SignalId, std::uint64_t>> triggers(
                    impl_->native_process_static_trigger_counts[id].begin(),
                    impl_->native_process_static_trigger_counts[id].end());
                std::ranges::sort(
                    triggers,
                    [](const auto& left, const auto& right) {
                        return left.second != right.second
                            ? left.second > right.second
                            : left.first < right.first;
                    });
                std::cerr << "fsim-profile: native-process-triggers id=" << id
                          << " total=";
                const auto trigger_total = std::accumulate(
                    triggers.begin(), triggers.end(), std::uint64_t { },
                    [](const auto accumulated, const auto& trigger) {
                        return accumulated + trigger.second;
                    });
                std::cerr << trigger_total << " signals=";
                for (std::size_t trigger = 0; trigger < triggers.size();
                     ++trigger) {
                    if (trigger != 0U) {
                        std::cerr << ',';
                    }
                    std::cerr << static_cast<std::size_t>(
                                     triggers[trigger].first)
                              << ':' << triggers[trigger].second;
                }
                std::cerr << '\n';
            }
        }

        const auto process_count = std::min(
            {impl_->processes.size(),
             impl_->native_process_single_resume_counts.size(),
             impl_->native_process_single_static_wait_counts.size(),
             impl_->native_process_cohort_resume_counts.size(),
             impl_->native_process_word_fanout_ready_counts.size()});
        std::vector<bool> eligible(process_count, false);
        std::vector<std::size_t> parent(process_count);
        std::vector<std::size_t> component_size(process_count, 0U);
        std::vector<std::uint64_t> component_resumes(process_count, 0U);
        std::vector<std::uint64_t> component_fanout(process_count, 0U);
        std::vector<std::set<SignalId>> process_outputs(process_count);
        std::iota(parent.begin(), parent.end(), std::size_t { });
        std::size_t eligible_processes { };
        std::uint64_t eligible_resumes { };
        for (std::size_t id = 0; id < process_count; ++id) {
            const auto process_id = static_cast<ProcessId>(id);
            const auto singles
                = impl_->native_process_single_resume_counts[id];
            eligible[id] = singles != 0U
                && singles
                    == impl_->native_process_single_static_wait_counts[id]
                && impl_->native_process_cohort_resume_counts[id] == 0U
                && !impl_->processes.program_view(process_id)
                        .static_sensitivity().empty();
            if (eligible[id]) {
                ++eligible_processes;
                eligible_resumes += singles;
            }
        }
        const auto root = [&](std::size_t id) {
            while (parent[id] != id) {
                parent[id] = parent[parent[id]];
                id = parent[id];
            }
            return id;
        };
        const auto join = [&](const std::size_t left,
                              const std::size_t right) {
            const auto left_root = root(left);
            const auto right_root = root(right);
            if (left_root != right_root) {
                parent[right_root] = left_root;
            }
        };
        std::set<std::uint64_t> edges;
        for (std::size_t id = 0; id < process_count; ++id) {
            if (!eligible[id]) {
                continue;
            }
            const auto process_id = static_cast<ProcessId>(id);
            auto& outputs = process_outputs[id];
            const auto& program = impl_->processes.program_view(process_id);
            for (const auto& region : program.driver_regions()) {
                outputs.insert(region.signal);
            }
            if (outputs.empty()) {
                for (const auto& operation : program.operations()) {
                    if (const auto signal = output_signal(operation)) {
                        outputs.insert(*signal);
                    }
                }
            }
            for (const auto signal : outputs) {
                for (const auto& fanout : impl_->static_fanout_for(signal)) {
                    const auto target
                        = static_cast<std::size_t>(fanout.process);
                    if (target >= process_count || !eligible[target]) {
                        continue;
                    }
                    const auto edge
                        = (static_cast<std::uint64_t>(id) << 32U)
                        | static_cast<std::uint64_t>(fanout.process);
                    if (edges.insert(edge).second) {
                        join(id, target);
                    }
                }
            }
        }
        for (std::size_t id = 0; id < process_count; ++id) {
            if (!eligible[id]) {
                continue;
            }
            const auto component = root(id);
            ++component_size[component];
            component_resumes[component]
                += impl_->native_process_single_resume_counts[id];
            component_fanout[component]
                += impl_->native_process_word_fanout_ready_counts[id];
        }
        std::vector<std::set<SignalId>> component_outputs(process_count);
        for (std::size_t id = 0; id < process_count; ++id) {
            if (!eligible[id]) {
                continue;
            }
            auto& outputs = component_outputs[root(id)];
            outputs.insert(
                process_outputs[id].begin(), process_outputs[id].end());
        }
        std::vector<std::size_t> component_direct_outputs(
            process_count, 0U);
        std::vector<std::size_t> component_unsafe_outputs(
            process_count, 0U);
        std::vector<std::size_t> component_internal_fanout(
            process_count, 0U);
        std::vector<std::size_t> component_external_fanout(
            process_count, 0U);
        std::vector<std::size_t> component_direct_internal_fanout(
            process_count, 0U);
        std::vector<std::size_t> component_unsafe_internal_fanout(
            process_count, 0U);
        std::array<std::uint64_t, 5U> sensitivity_kinds { };
        std::size_t direct_components { };
        std::size_t closed_direct_components { };
        std::uint64_t direct_component_resumes { };
        std::uint64_t closed_direct_component_resumes { };
        for (std::size_t component = 0; component < process_count;
             ++component) {
            if (component_size[component] == 0U) {
                continue;
            }
            for (const auto signal : component_outputs[component]) {
                const auto route_process
                    = signal < impl_->direct_single_driver_routes.size()
                        ? impl_->direct_single_driver_routes[signal].process
                        : ProcessId { };
                const bool direct
                    = impl_->can_publish_native_word(signal, route_process);
                if (direct) {
                    ++component_direct_outputs[component];
                } else {
                    ++component_unsafe_outputs[component];
                }
                for (const auto& fanout : impl_->static_fanout_for(signal)) {
                    const auto kind = static_cast<std::size_t>(fanout.edge);
                    if (kind < sensitivity_kinds.size()) {
                        ++sensitivity_kinds[kind];
                    }
                    const auto target
                        = static_cast<std::size_t>(fanout.process);
                    if (target < process_count && eligible[target]
                        && root(target) == component) {
                        ++component_internal_fanout[component];
                        if (direct) {
                            ++component_direct_internal_fanout[component];
                        } else {
                            ++component_unsafe_internal_fanout[component];
                        }
                    } else {
                        ++component_external_fanout[component];
                    }
                }
            }
            if (component_unsafe_outputs[component] == 0U) {
                ++direct_components;
                direct_component_resumes += component_resumes[component];
                if (component_external_fanout[component] == 0U) {
                    ++closed_direct_components;
                    closed_direct_component_resumes
                        += component_resumes[component];
                }
            }
        }
        std::vector<std::uint32_t> wave_component_counts(
            process_count, 0U);
        std::vector<std::size_t> touched_wave_components;
        std::array<std::uint64_t, 4U> wave_size_buckets { };
        std::uint64_t eligible_wave_activations { };
        std::uint64_t component_waves { };
        std::uint64_t multi_process_component_waves { };
        std::uint64_t multi_process_wave_activations { };
        std::uint32_t maximum_processes_per_component_wave { };
        for (std::size_t wave = 0;
             wave < impl_->native_process_single_wave_offsets.size(); ++wave) {
            const auto begin
                = impl_->native_process_single_wave_offsets[wave];
            const auto end
                = wave + 1U
                        < impl_->native_process_single_wave_offsets.size()
                ? impl_->native_process_single_wave_offsets[wave + 1U]
                : impl_->native_process_single_wave_processes.size();
            touched_wave_components.clear();
            for (auto index = begin; index < end; ++index) {
                const auto id = static_cast<std::size_t>(
                    impl_->native_process_single_wave_processes[index]);
                if (id >= process_count || !eligible[id]) {
                    continue;
                }
                const auto component = root(id);
                if (wave_component_counts[component]++ == 0U) {
                    touched_wave_components.push_back(component);
                }
                ++eligible_wave_activations;
            }
            for (const auto component : touched_wave_components) {
                const auto count = wave_component_counts[component];
                wave_component_counts[component] = 0U;
                ++component_waves;
                maximum_processes_per_component_wave = std::max(
                    maximum_processes_per_component_wave, count);
                if (count == 1U) {
                    ++wave_size_buckets[0];
                } else if (count <= 4U) {
                    ++wave_size_buckets[1];
                } else if (count <= 16U) {
                    ++wave_size_buckets[2];
                } else {
                    ++wave_size_buckets[3];
                }
                if (count > 1U) {
                    ++multi_process_component_waves;
                    multi_process_wave_activations += count;
                }
            }
        }
        std::vector<std::size_t> components;
        for (std::size_t id = 0; id < process_count; ++id) {
            if (component_size[id] != 0U) {
                components.push_back(id);
            }
        }
        std::ranges::sort(
            components,
            [&](const auto left, const auto right) {
                return component_resumes[left] > component_resumes[right];
            });
        std::cerr << "fsim-profile: native-static-regions processes="
                  << eligible_processes
                  << " components=" << components.size()
                  << " edges=" << edges.size()
                  << " resumes=" << eligible_resumes
                  << " direct_components=" << direct_components
                  << " direct_resumes=" << direct_component_resumes
                  << " closed_direct_components="
                  << closed_direct_components
                  << " closed_direct_resumes="
                  << closed_direct_component_resumes
                  << " sensitivity_kinds=" << sensitivity_kinds[0] << ','
                  << sensitivity_kinds[1] << ',' << sensitivity_kinds[2]
                  << ',' << sensitivity_kinds[3] << ','
                  << sensitivity_kinds[4]
                  << " wave_activations=" << eligible_wave_activations
                  << " component_waves=" << component_waves
                  << " multi_component_waves="
                  << multi_process_component_waves
                  << " multi_wave_activations="
                  << multi_process_wave_activations
                  << " wave_sizes=" << wave_size_buckets[0] << ','
                  << wave_size_buckets[1] << ',' << wave_size_buckets[2]
                  << ',' << wave_size_buckets[3]
                  << " max_wave_size="
                  << maximum_processes_per_component_wave << '\n';
        for (const auto component : components | std::views::take(20U)) {
            auto first = process_count;
            for (std::size_t id = 0; id < process_count; ++id) {
                if (eligible[id] && root(id) == component) {
                    first = id;
                    break;
                }
            }
            std::string_view first_name;
            if (first < process_count) {
                const auto first_process_id = static_cast<ProcessId>(first);
                first_name
                    = impl_->processes.program_view(first_process_id).name();
            }
            std::cerr << "fsim-profile: native-static-region size="
                      << component_size[component]
                      << " resumes=" << component_resumes[component]
                      << " word_fanout_ready="
                      << component_fanout[component]
                      << " outputs="
                      << component_outputs[component].size()
                      << " direct_outputs="
                      << component_direct_outputs[component]
                      << " unsafe_outputs="
                      << component_unsafe_outputs[component]
                      << " internal_fanout="
                      << component_internal_fanout[component]
                      << " direct_internal_fanout="
                      << component_direct_internal_fanout[component]
                      << " unsafe_internal_fanout="
                      << component_unsafe_internal_fanout[component]
                      << " external_fanout="
                      << component_external_fanout[component]
                      << " first='" << first_name << "'\n";
        }
    }
    if (impl_->native_update_profile_enabled) {
        std::cerr << "fsim-profile: native-update calls="
                  << impl_->native_update_profile_calls
                  << " fallbacks=" << impl_->native_update_profile_fallbacks
                  << " batches=" << impl_->native_update_profile_batches
                  << " slots=" << impl_->native_update_profile_slots
                  << " inactive=" << impl_->native_update_profile_inactive
                  << " untouched=" << impl_->native_update_profile_untouched
                  << " unchanged=" << impl_->native_update_profile_unchanged
                  << " unchanged_direct_word="
                  << impl_->native_update_profile_unchanged_direct_word
                  << " unchanged_direct_packed="
                  << impl_->native_update_profile_unchanged_direct_packed
                  << " unchanged_unresolved="
                  << impl_->native_update_profile_unchanged_unresolved
                  << " unchanged_resolved="
                  << impl_->native_update_profile_unchanged_resolved
                  << " direct_word="
                  << impl_->native_update_profile_direct_word
                  << " direct_packed="
                  << impl_->native_update_profile_direct_packed
                  << " unresolved="
                  << impl_->native_update_profile_unresolved
                  << " resolved=" << impl_->native_update_profile_resolved
                  << " unchanged_owned="
                  << impl_->native_update_profile_unchanged_owned
                  << " changed_owned="
                  << impl_->native_update_profile_changed_owned
                  << " schedule_requests="
                  << impl_->native_update_profile_schedule_requests
                  << " schedule_coalesced="
                  << impl_->native_update_profile_schedule_coalesced
                  << " commits=" << impl_->native_update_profile_commits
                  << " commit_word_signals="
                  << impl_->native_update_profile_commit_word_signals
                  << " commit_value_signals="
                  << impl_->native_update_profile_commit_value_signals
                  << " word_calls="
                  << impl_->native_update_profile_word_calls
                  << " words=" << impl_->native_update_profile_words
                  << " word_fallbacks="
                  << impl_->native_update_profile_word_fallbacks
                  << " word_scalar="
                  << impl_->native_update_profile_word_scalar
                  << " word_unchanged="
                  << impl_->native_update_profile_word_unchanged
                  << " word_direct="
                  << impl_->native_update_profile_word_direct
                  << " word_unresolved="
                  << impl_->native_update_profile_word_unresolved
                  << " word_resolved="
                  << impl_->native_update_profile_word_resolved
                  << '\n';
    }
    impl_->report_process_profile();
    impl_->report_update_profile();
}
Interpreter::Interpreter(Interpreter&&) noexcept = default;
Interpreter& Interpreter::operator=(Interpreter&&) noexcept = default;

void Interpreter::Impl::report_process_profile()
{
    if (!process_profile_enabled || process_profile_reported) {
        return;
    }
    process_profile_reported = true;
    std::vector<std::size_t> order(processes.size());
    std::iota(order.begin(), order.end(), std::size_t { });
    std::ranges::sort(order, [&](const auto left, const auto right) {
        if (update_profile_enabled
            && processes.profile_updates(static_cast<ProcessId>(left))
                != processes.profile_updates(static_cast<ProcessId>(right))) {
            return processes.profile_updates(static_cast<ProcessId>(left))
                > processes.profile_updates(static_cast<ProcessId>(right));
        }
        return processes.profile_total_nanoseconds(
                   static_cast<ProcessId>(left))
            > processes.profile_total_nanoseconds(
                static_cast<ProcessId>(right));
    });
    std::uint64_t total_nanoseconds { };
    std::uint64_t native_nanoseconds { };
    std::uint64_t calls { };
    std::uint64_t native_resumes { };
    std::uint64_t interpreter_operations { };
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        total_nanoseconds += processes.profile_total_nanoseconds(id);
        native_nanoseconds += processes.profile_native_nanoseconds(id);
        calls += processes.profile_calls(id);
        native_resumes += processes.profile_native_resumes(id);
        interpreter_operations
            += processes.profile_interpreter_operations(id);
    }
    std::cerr << "fsim-profile: process-summary processes=" << processes.size()
              << " calls=" << calls
              << " interpreter_operations=" << interpreter_operations
              << " native_resumes=" << native_resumes
              << " total_ms="
              << static_cast<double>(total_nanoseconds) / 1'000'000.0
              << " native_ms="
              << static_cast<double>(native_nanoseconds) / 1'000'000.0
              << '\n';
    const auto count =
        profile_processes_all_enabled
        ? order.size()
        : std::min<std::size_t>(30U, order.size());
    for (std::size_t rank = 0; rank < count; ++rank) {
        const auto id = static_cast<ProcessId>(order[rank]);
        const auto program = processes.program_view(id);
        const auto profile_calls = processes.profile_calls(id);
        if (profile_calls == 0U) {
            break;
        }
        std::cerr << "fsim-profile: process rank=" << rank + 1U
                  << " id=" << id
                  << " compiled=" << (processes.has_executor(id) ? 1 : 0)
                  << " static_operations="
                  << processes.operation_count(id)
                  << " sensitivity="
                  << program.static_sensitivity().size()
                  << " calls=" << profile_calls
                  << " interpreter_operations="
                  << processes.profile_interpreter_operations(id)
                  << " native_resumes=" << processes.profile_native_resumes(id)
                  << " updates=" << processes.profile_updates(id)
                  << " total_ms="
                  << static_cast<double>(
                         processes.profile_total_nanoseconds(id))
                / 1'000'000.0
                  << " native_ms="
                  << static_cast<double>(
                         processes.profile_native_nanoseconds(id))
                / 1'000'000.0
                  << " name=" << program.name() << '\n';
    }
}

void Interpreter::Impl::report_update_profile()
{
    if (!update_profile_enabled || update_profile_reported) {
        return;
    }
    update_profile_reported = true;
    std::cerr << "fsim-profile: update commits=" << update_profile_commits
              << " updates=" << update_profile_updates
              << " whole=" << update_profile_whole
              << " slices=" << update_profile_slices
              << " unresolved=" << update_profile_unresolved
              << " resolved=" << update_profile_resolved
              << " resolved_single_driver="
              << update_profile_resolved_single_driver
              << " bits=" << update_profile_bits << '\n';
}

} // namespace fsim::runtime::simir
