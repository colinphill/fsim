// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_allocator_arena_statistics.hpp"
#include "simir_execution_context.hpp"
#include "simir_region_graph_bindings.hpp"
#include "simir_region_metadata_census.hpp"
#include "simir_storage_census_internal.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string_view>

namespace fsim::runtime::simir {
namespace {

void report_region_recertification_cause(const bool enabled,
    const char* const event, const char* const reason,
    const std::uint64_t runtime_generation, const std::size_t component,
    const bool authoritative_waiting, const std::source_location caller) noexcept
{
    if (!enabled) {
        return;
    }
    std::fprintf(stderr,
        "fsim-profile: sv-region-recertification event=%s reason=%s "
        "runtime_generation=%llu component=%zu authoritative_waiting=%u "
        "caller='%s:%u' function='%s'\n",
        event, reason, static_cast<unsigned long long>(runtime_generation),
        component, static_cast<unsigned>(authoritative_waiting),
        caller.file_name(), static_cast<unsigned>(caller.line()),
        caller.function_name());
}

void report_optional_region_backend_event(
    const bool enabled, const std::size_t component,
    const RegionConeActivationKernel& kernel, const std::string_view event,
    const std::string_view detail = { }) noexcept
{
    if (!enabled) {
        return;
    }
    std::fprintf(stderr,
        "[fsim region-native-create] layer=runtime component=%zu event=%.*s "
        "members=%zu inputs=%zu outputs=%zu",
        component, static_cast<int>(event.size()), event.data(),
        kernel.members.size(), kernel.inputs.size(), kernel.outputs.size());
    if (!detail.empty()) {
        std::fprintf(stderr, " detail=%.*s",
            static_cast<int>(detail.size()), detail.data());
    }
    std::fputc('\n', stderr);
}

} // namespace

namespace {

[[nodiscard]] bool same_region_forwarding_mapping(
    const RegionConeForwardingKernel& left,
    const RegionConeForwardingKernel& right)
{
    return same_region_kernel_mapping(
               left.execution_kernel, right.execution_kernel)
        && left.topological_member_indices
            == right.topological_member_indices
        && left.members == right.members
        && left.dependencies == right.dependencies
        && left.internal_reads == right.internal_reads
        && left.internal_signals == right.internal_signals;
}

} // namespace

bool Interpreter::Impl::process_signal_access_is_complete(
    const ProcessId id) const noexcept
{
    if (id >= processes.size()) {
        return false;
    }
    if (processes.is_compact_constant(id)) {
        return true;
    }
    const auto& process = processes[id];
    const auto& program = process.program();
    if (process.executor) {
        const auto* binding
            = process.executor->program_access_binding();
        if (process.cold().fork_parent) {
            const auto* const fork_state = process.cold().fork_state.get();
            if (binding == nullptr || fork_state == nullptr
                || !fork_state->access_binding_attestation) {
                return false;
            }
            const auto& attestation
                = *fork_state->access_binding_attestation;
            return binding->same_execution_binding(attestation)
                && program.matches_forked_binding(id, attestation);
        }
        return binding != nullptr
            && program.matches_registered_binding(id, *binding);
    }
    const auto& deferred = process.cold().deferred_executor;
    if (!deferred) {
        return true;
    }
    return !deferred->access_binding_rejected
        && deferred->contract.expected_access
        && program.matches_registered_binding(
            id, *deferred->contract.expected_access);
}

bool Interpreter::Impl::process_region_kernel_eligible(
    const ProcessId id) const noexcept
{
    if (id >= processes.size()
        || processes.is_compact_constant(id)
        || !process_signal_access_is_complete(id)) {
        return false;
    }
    const auto& process = processes[id];
    if (!process.region_kernel_equivalence_confirmed) {
        return false;
    }
    return process.executor
        && process.executor->region_kernel_equivalent();
}

Interpreter::Impl::RegionRuntimeSnapshot
Interpreter::Impl::prepare_region_runtime_snapshot(
    RegionGraph graph,
    const std::span<const Process* const> programs,
    const bool process_access_inventory_complete,
    const bool enable_region_kernel,
    const bool may_compile_backends,
    std::shared_ptr<const SignalDriverInventory> signal_driver_inventory)
{
    if (programs.size() != processes.size()) {
        throw std::invalid_argument {
            "region runtime snapshot has a mismatched process inventory"
        };
    }
    RegionRuntimeSnapshot snapshot;
    snapshot.graph = std::move(graph);
    snapshot.signal_driver_inventory = std::move(signal_driver_inventory);
    if (!snapshot.signal_driver_inventory
        || snapshot.signal_driver_inventory->version != 1U
        || snapshot.signal_driver_inventory->signals.size()
            != snapshot.graph.signals().size()) {
        throw std::invalid_argument {
            "region snapshot has no structurally bounded signal driver inventory"
        };
    }
    if (region_runtime_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error {
            "region runtime snapshot generation exhausted"
        };
    }
    snapshot.generation = region_runtime_generation + 1U;
    snapshot.value_only_recertification_by_component.assign(
        snapshot.graph.certificate_inventory().components.size(), 0U);
    snapshot.backend_pool = region_kernel_backend_pool;
    snapshot.forwarding_backend_pool
        = region_cone_forwarding_backend_pool;
    snapshot.frontier_backend_pool = region_frontier_backend_pool;
    snapshot.process_signal_access_inventory_complete
        = process_access_inventory_complete;
    snapshot.component_by_process.assign(processes.size(),
        std::numeric_limits<std::size_t>::max());
    if (region_readiness_queued_by_process.size() == processes.size()) {
        snapshot.readiness_queued_by_process
            = region_readiness_queued_by_process;
    } else {
        snapshot.readiness_queued_by_process.resize(processes.size());
    }
    if (!enable_region_kernel) {
        return snapshot;
    }

    const auto& components
        = snapshot.graph.certificate_inventory().components;
    snapshot.programs_by_component.resize(components.size());
    snapshot.backends_by_component.resize(components.size());
    snapshot.backend_generation_by_component.resize(components.size());
    snapshot.vhdl_projected_readiness_by_component.resize(components.size());
    snapshot.forwarding_backends_by_component.resize(components.size());
    snapshot.frontier_backends_by_component.resize(components.size());
    snapshot.authoritative_state_by_component.resize(components.size());
    snapshot.readiness_mask_by_component.resize(components.size());
    snapshot.readiness_member_index_by_process.assign(
        processes.size(), std::numeric_limits<std::size_t>::max());
    if (systemverilog_local_wave_enabled) {
        snapshot.local_wave_state_by_component.resize(components.size());
    }
    snapshot.authoritative_component_by_signal.assign(
        snapshot.graph.signals().size(),
        std::numeric_limits<std::size_t>::max());
    snapshot.authoritative_member_index_by_process.assign(
        processes.size(), std::numeric_limits<std::size_t>::max());
    std::vector<std::uint8_t> aliased_signals(
        snapshot.graph.signals().size(), 0U);
    for (const auto& family : snapshot.graph.signal_alias_families()) {
        if (family.proxy < aliased_signals.size()) {
            aliased_signals[family.proxy] = 1U;
        }
        for (const auto& leaf : family.leaves) {
            if (leaf.signal < aliased_signals.size()) {
                aliased_signals[leaf.signal] = 1U;
            }
        }
    }
    const bool profile_frontier_preparation
        = systemverilog_wave_profile_enabled
        || std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr;
    std::size_t frontier_preparation_declines { };
    std::size_t frontier_preparation_diagnostic_rows { };
    std::size_t authoritative_seed_failures { };
    for (std::size_t component = 0U;
         component < components.size(); ++component) {
        const auto& certificate = components[component];
        for (std::size_t member_index = 0U;
            member_index < certificate.members.size(); ++member_index) {
            const auto process = certificate.members[member_index];
            if (process >= snapshot.component_by_process.size()
                || snapshot.component_by_process[process]
                    != std::numeric_limits<std::size_t>::max()) {
                throw std::logic_error {
                    "region component membership is not a partition"
                };
            }
            snapshot.component_by_process[process] = component;
            snapshot.authoritative_member_index_by_process[process]
                = member_index;
        }
        RegionComputeProgramRejection program_rejection;
        const bool capture_program_rejection
            = profile_frontier_preparation
            && frontier_preparation_diagnostic_rows < 16U;
        auto compute_program = snapshot.graph.build_compute_program(
            component, programs,
            capture_program_rejection ? &program_rejection : nullptr);
        if (!compute_program && program_rejection.reason != nullptr
            && capture_program_rejection) {
            ++frontier_preparation_diagnostic_rows;
            std::fprintf(stderr,
                "[fsim region-program-build-decline] reason=%s "
                "component=%zu signal=%u family_proxy=%u writer=%u "
                "process=%u operation_index=%zu operation_type=%s\n",
                program_rejection.reason, program_rejection.component,
                static_cast<unsigned>(program_rejection.signal),
                static_cast<unsigned>(program_rejection.family_proxy),
                static_cast<unsigned>(program_rejection.writer_process),
                static_cast<unsigned>(program_rejection.process),
                program_rejection.operation_index,
                program_rejection.operation_type != nullptr
                    ? program_rejection.operation_type : "none");
        }
        snapshot.programs_by_component[component]
            = std::move(compute_program);
        const bool has_compute_program
            = snapshot.programs_by_component[component].has_value();
        const auto& graph_inventory
            = snapshot.graph.certificate_inventory();
        const bool component_storage_inventory_complete
            = process_access_inventory_complete
            && graph_inventory.access_inventory_complete
            && snapshot.graph.component_epochs_current(component)
            && certificate.status
                != RegionComponentCertificateStatus::incomplete_access_inventory;
        const bool wide_single_owner_inventory_complete
            = a4_wide_single_owner_commit_enabled
            && component_storage_inventory_complete;
        const bool wide_disjoint_owner_inventory_complete
            = a4_wide_disjoint_owner_commit_enabled
            && component_storage_inventory_complete;
        const bool wide_storage_inventory_complete
            = wide_single_owner_inventory_complete
            || wide_disjoint_owner_inventory_complete;
        if (!has_compute_program && !wide_storage_inventory_complete) {
            continue;
        }

        // Storage identity does not make a public resolved signal eligible
        // for hidden cone execution. This separate proof retains its exact
        // original owner and ordinary scheduling/publication boundary.
        const auto can_seed_wide_single_owner = [&](const SignalId signal) {
            const auto graph_signals = snapshot.graph.signals();
            if (!wide_single_owner_inventory_complete
                || signal >= graph_signals.size()
                || signal >= aliased_signals.size()
                || signal >= driver_values.size()
                || aliased_signals[signal] != 0U
                || owned_driver_active(signal)
                || (signal < switch_endpoint_adjacency.size()
                    && !switch_endpoint_adjacency[signal].empty())) {
                return false;
            }
            const auto& node = graph_signals[signal];
            const auto& descriptor = node.descriptor;
            if (node.drivers != RegionDriverClass::single_whole
                || node.writers.size() != 1U
                || node.writers_unknown || node.dynamic_fork_writers
                || node.partial_projected_transactions
                || node.observations != RegionObservation::none
                || descriptor.width <= 64U
                || descriptor.implicit_driver || descriptor.external_driver
                || descriptor.event_variable
                || (descriptor.value_kind == ValueKind::logic4
                    ? descriptor.resolution != ResolutionKind::sv_wire
                        && descriptor.resolution != ResolutionKind::none
                    : descriptor.value_kind != ValueKind::logic9
                        || (descriptor.resolution != ResolutionKind::std_logic
                            && descriptor.resolution
                                != ResolutionKind::none))) {
                return false;
            }
            const auto& writer = node.writers.front();
            const auto graph_processes = snapshot.graph.processes();
            if (writer.process >= graph_processes.size()) {
                return false;
            }
            const auto& writer_node = graph_processes[writer.process];
            const bool active_owner
                = writer_node.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                && writer_node.update_kind
                    == RegionUpdateKind::systemverilog_active;
            const bool projected_owner
                = writer_node.scheduling_domain
                    == ProcessSchedulingDomain::generic
                && writer_node.update_kind == RegionUpdateKind::vhdl_projected;
            const bool unresolved_projected_owner
                = projected_owner
                && descriptor.resolution == ResolutionKind::none;
            const bool unresolved_active_update_owner
                = active_owner
                && descriptor.resolution == ResolutionKind::none;
            const bool unresolved_owner_alias
                = unresolved_projected_owner
                || unresolved_active_update_owner;
            if (unresolved_owner_alias) {
                const auto* const owner_program
                    = writer.process < programs.size()
                    ? programs[writer.process] : nullptr;
                if (owner_program == nullptr
                    || owner_program->id != writer.process) {
                    return false;
                }
                const auto& operations = owner_program->operations;
                const auto writes_target = [signal](const auto* write) {
                    return write != nullptr && write->signal == signal;
                };
                bool found_target_write { };
                for (std::size_t operation_index = 0U;
                     operation_index < operations.size(); ++operation_index) {
                    const auto operation
                        = operations.expanded(operation_index);
                    if (const auto* const write
                            = operation_get_if<WriteProjected>(&operation)) {
                        if (write->signal == signal) {
                            if (!unresolved_projected_owner
                                || found_target_write
                                || write->delay != SimulationTick { }
                                || write->rejection != SimulationTick { }
                                || write->mode
                                    != ProjectedDelayMode::inertial) {
                                return false;
                            }
                            found_target_write = true;
                        }
                        continue;
                    }
                    if (const auto* const write
                            = operation_get_if<WriteUpdate>(&operation)) {
                        if (write->signal == signal) {
                            if (!unresolved_active_update_owner
                                || found_target_write
                                || write->domain
                                    != SignalUpdateDomain::systemverilog_active) {
                                return false;
                            }
                            found_target_write = true;
                        }
                        continue;
                    }
                    if (writes_target(operation_get_if<WriteBlocking>(
                            &operation))
                        || writes_target(operation_get_if<WriteUpdate>(
                            &operation))
                        || writes_target(operation_get_if<WriteAfter>(
                            &operation))
                        || writes_target(operation_get_if<WriteInertial>(
                            &operation))
                        || writes_target(operation_get_if<
                            WriteProjectedWaveform>(&operation))
                        || writes_target(operation_get_if<WriteBlockingSlice>(
                            &operation))
                        || writes_target(operation_get_if<WriteUpdateSlice>(
                            &operation))
                        || writes_target(operation_get_if<WriteAfterSlice>(
                            &operation))
                        || writes_target(operation_get_if<WriteInertialSlice>(
                            &operation))
                        || writes_target(operation_get_if<WriteProjectedSlice>(
                            &operation))
                        || writes_target(operation_get_if<
                            WriteProjectedWaveformSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteBlockingDynamicSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteUpdateDynamicSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteAfterDynamicSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteBlockingDynamicPartSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteUpdateDynamicPartSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteAfterDynamicPartSlice>(&operation))
                        || writes_target(operation_get_if<ForceSignalSlice>(
                            &operation))
                        || writes_target(operation_get_if<ReleaseSignalSlice>(
                            &operation))
                        || writes_target(operation_get_if<
                            WriteInertialDynamicSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteInertialDynamicPartSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteProjectedDynamicSlice>(&operation))
                        || writes_target(operation_get_if<
                            WriteProjectedWaveformDynamicSlice>(
                                &operation))) {
                        return false;
                    }
                    if (const auto* const vital
                            = operation_get_if<VitalDelay>(&operation);
                        vital != nullptr && vital->output == signal) {
                        return false;
                    }
                }
                if (!found_target_write) {
                    return false;
                }
                const Process::DriverRegion* target_region { };
                for (const auto& region : owner_program->driver_regions) {
                    if (region.signal != signal) {
                        continue;
                    }
                    if (target_region != nullptr) {
                        return false;
                    }
                    target_region = &region;
                }
                if (target_region == nullptr || !target_region->whole
                    || target_region->offset != 0U
                    || (target_region->width != 0U
                        && target_region->width != descriptor.width)) {
                    return false;
                }
            }
            // This storage proof does not fuse scheduling phases or grant
            // the writer a native entry. Each alias publication remains at
            // its original projected or SystemVerilog Active callback.
            if ((!active_owner && !projected_owner)
                || (descriptor.resolution == ResolutionKind::none
                    && !unresolved_owner_alias)
                || writer.offset != 0U
                || (writer.width != 0U && writer.width != descriptor.width)
                || !std::ranges::binary_search(certificate.members, writer.process)
                || (!unresolved_active_update_owner
                    && std::ranges::any_of(node.readers,
                        [&](const RegionAccess& reader) {
                            return !std::ranges::binary_search(
                                certificate.members, reader.process);
                        }))
                || (unresolved_owner_alias
                    ? !driver_values[signal].empty()
                    : driver_values[signal].size() != 1U)) {
                return false;
            }
            if (unresolved_owner_alias) {
                return true;
            }
            const auto* const owner = driver_values[signal].find(writer.process);
            return owner != nullptr && owner->strength == DriveStrength { };
        };

        // Narrow std_logic values are still public resolved boundaries. This
        // separate proof admits their four packed storage roles only; it does
        // not create a compute program or change the owner's generic Update
        // scheduling contract.
        const auto can_seed_narrow_logic9_boundary =
            [&](const SignalId signal) {
                const auto graph_signals = snapshot.graph.signals();
                if (!wide_single_owner_inventory_complete
                    || signal >= graph_signals.size()
                    || signal >= signals.size()
                    || signal >= aliased_signals.size()
                    || signal >= driver_values.size()
                    || signal >= external_driver_values.size()
                    || signal >= forced_values.size()
                    || signal >= forced_masks.size()
                    || signal >= forced_driver_values.size()
                    || signal >= forced_driver_masks.size()
                    || signal >= dynamic_fanout.size()
                    || signal >= signal_transaction_observed.size()
                    || signal >= signal_container_aliases.size()
                    || signal >= signal_container_element_aliases.size()
                    || signal >= signal_container_aggregate_aliases.size()
                    || signal >= switch_endpoint_adjacency.size()
                    || signal >= switch_control_adjacency.size()
                    || signal >= module_path_destination_mask.size()
                    || aliased_signals[signal] != 0U
                    || external_driver_values[signal] || forced_values[signal]
                    || forced_masks[signal] || forced_driver_values[signal]
                    || forced_driver_masks[signal]
                    || signal_transaction_observed[signal]
                    || !dynamic_fanout[signal].empty()
                    || !signal_container_aliases[signal].empty()
                    || signal_container_element_aliases[signal]
                    || signal_container_aggregate_aliases[signal]
                    || !switch_endpoint_adjacency[signal].empty()
                    || !switch_control_adjacency[signal].empty()
                    || module_path_destination_mask[signal] != 0U
                    || monitor_watches(signal)
                    || owned_driver_active(signal)) {
                    return false;
                }
                const auto& node = graph_signals[signal];
                const auto& descriptor = node.descriptor;
                if (descriptor.value_kind != ValueKind::logic9
                    || descriptor.resolution != ResolutionKind::std_logic
                    || descriptor.width == 0U || descriptor.width > 64U
                    || descriptor.implicit_driver || descriptor.external_driver
                    || descriptor.event_variable
                    || node.drivers != RegionDriverClass::single_whole
                    || node.writers.size() != 1U || node.writers_unknown
                    || node.dynamic_fork_writers
                    || node.partial_projected_transactions
                    || node.observations != RegionObservation::none
                    || signals[signal].value_kind != ValueKind::logic9
                    || signals[signal].resolution != ResolutionKind::std_logic
                    || !signals[signal].initial_value.is_logic9()
                    || signals[signal].initial_value.width() != descriptor.width
                    || signals[signal].public_value_reference_exposed
                    || signals[signal].has_implicit_driver
                    || signals[signal].has_charge_strength
                    || signals[signal].event_variable) {
                    return false;
                }

                const auto& writer = node.writers.front();
                if (writer.offset != 0U
                    || (writer.width != 0U
                        && writer.width != descriptor.width)) {
                    return false;
                }
                const auto graph_processes = snapshot.graph.processes();
                if (writer.process >= graph_processes.size()
                    || writer.process >= programs.size()
                    || programs[writer.process] == nullptr
                    || !std::ranges::binary_search(
                        certificate.members, writer.process)
                    || !process_signal_access_is_complete(writer.process)) {
                    return false;
                }
                const auto& writer_node = graph_processes[writer.process];
                const auto* const owner_program = programs[writer.process];
                if (writer_node.scheduling_domain
                        != ProcessSchedulingDomain::generic
                    || writer_node.update_kind != RegionUpdateKind::generic
                    || writer_node.dependencies_unknown
                    || owner_program->id != writer.process
                    || owner_program->scheduling_domain
                        != ProcessSchedulingDomain::generic) {
                    return false;
                }

                bool found_whole_generic_update { };
                for (std::size_t operation_index = 0U;
                     operation_index < owner_program->operations.size();
                     ++operation_index) {
                    const auto operation
                        = owner_program->operations.expanded(operation_index);
                    const auto* const update
                        = operation_get_if<WriteUpdate>(&operation);
                    if (update == nullptr || update->signal != signal) {
                        continue;
                    }
                    if (found_whole_generic_update
                        || update->domain != SignalUpdateDomain::generic) {
                        return false;
                    }
                    found_whole_generic_update = true;
                }
                if (!found_whole_generic_update) {
                    return false;
                }

                std::size_t matching_whole_driver_regions { };
                for (const auto& region : owner_program->driver_regions) {
                    if (region.signal != signal) {
                        continue;
                    }
                    if (!region.whole || region.offset != 0U
                        || (region.width != 0U
                            && region.width != descriptor.width)) {
                        return false;
                    }
                    ++matching_whole_driver_regions;
                }
                if (matching_whole_driver_regions != 1U
                    || std::ranges::any_of(node.readers,
                        [&](const RegionAccess& reader) {
                            return !std::ranges::binary_search(
                                certificate.members, reader.process);
                        })
                    || driver_values[signal].size() != 1U) {
                    return false;
                }
                const auto* const owner
                    = driver_values[signal].find(writer.process);
                return owner != nullptr
                    && owner->strength == DriveStrength { };
            };

        const auto graph_processes = snapshot.graph.processes();
        const auto graph_signals = snapshot.graph.signals();
        const auto vhdl_projected_slice_is_certified =
            [&](const SignalId signal, const RegionAccess& writer) {
                if (signal >= graph_signals.size()
                    || writer.process >= programs.size()
                    || programs[writer.process] == nullptr
                    || writer.process >= graph_processes.size()
                    || !std::ranges::binary_search(
                        certificate.members, writer.process)) {
                    return false;
                }
                const auto& process_node = graph_processes[writer.process];
                const auto* const process = programs[writer.process];
                if (process->id != writer.process
                    || process->scheduling_domain
                        != ProcessSchedulingDomain::generic
                    || process_node.scheduling_domain
                        != ProcessSchedulingDomain::generic
                    || process_node.update_kind
                        != RegionUpdateKind::vhdl_projected
                    || process_node.dependencies_unknown
                    // A4 changes only the backing raw/value planes; every
                    // writer still runs through its original generic
                    // scheduler and projected transaction callback. An
                    // LLVM or deferred executor is admissible only when its
                    // immutable registered-program binding is exact.
                    || !process_signal_access_is_complete(writer.process)
                    || writer.width == 0U
                    || writer.offset
                        >= graph_signals[signal].descriptor.width
                    || writer.width > graph_signals[signal].descriptor.width
                        - writer.offset) {
                    return false;
                }

                std::size_t matching_driver_regions { };
                for (const auto& region : process->driver_regions) {
                    if (region.signal != signal) {
                        continue;
                    }
                    ++matching_driver_regions;
                    if (region.whole || region.offset != writer.offset
                        || region.width != writer.width) {
                        return false;
                    }
                }
                if (matching_driver_regions != 1U) {
                    return false;
                }

                bool valid = true;
                std::size_t matching_projected_slices { };
                for (std::size_t instruction = 0U;
                     instruction < process->operations.size();
                     ++instruction) {
                    const auto operation
                        = process->operations.expanded(instruction);
                    visit_operation([&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T,
                                         WriteProjectedSlice>) {
                            if (value.signal == signal) {
                                ++matching_projected_slices;
                                valid = valid && value.offset == writer.offset
                                    && value.delay == 0U
                                    && value.rejection == 0U
                                    && value.mode
                                        == ProjectedDelayMode::inertial;
                            }
                        } else if constexpr (
                            std::is_same_v<T, WriteBlocking>
                            || std::is_same_v<T, WriteUpdate>
                            || std::is_same_v<T, WriteAfter>
                            || std::is_same_v<T, WriteInertial>
                            || std::is_same_v<T, WriteProjected>
                            || std::is_same_v<T, WriteProjectedWaveform>
                            || std::is_same_v<T, WriteBlockingSlice>
                            || std::is_same_v<T, WriteUpdateSlice>
                            || std::is_same_v<T, WriteAfterSlice>
                            || std::is_same_v<T, WriteInertialSlice>
                            || std::is_same_v<T, WriteProjectedWaveformSlice>
                            || std::is_same_v<T, WriteBlockingDynamicSlice>
                            || std::is_same_v<T, WriteUpdateDynamicSlice>
                            || std::is_same_v<T, WriteAfterDynamicSlice>
                            || std::is_same_v<T, WriteInertialDynamicSlice>
                            || std::is_same_v<T,
                                WriteInertialDynamicPartSlice>
                            || std::is_same_v<T, WriteProjectedDynamicSlice>
                            || std::is_same_v<T,
                                WriteProjectedWaveformDynamicSlice>
                            || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                            || std::is_same_v<T, WriteUpdateDynamicPartSlice>
                            || std::is_same_v<T, WriteAfterDynamicPartSlice>
                            || std::is_same_v<T, ForceSignalSlice>
                            || std::is_same_v<T, ReleaseSignalSlice>) {
                            if (value.signal == signal) {
                                valid = false;
                            }
                        } else if constexpr (
                            std::is_same_v<T, WriteContainerObject>
                            || std::is_same_v<T,
                                WriteContainerObjectElement>) {
                            // Container writes may reach a signal through
                            // bindings rather than a direct SignalId operand.
                            valid = false;
                        } else if constexpr (
                            std::is_same_v<T, VitalTimingCheck>) {
                            if (value.trigger_signal == signal) {
                                valid = false;
                            }
                        } else if constexpr (
                            std::is_same_v<T, VitalDelay>) {
                            if (value.output == signal) {
                                valid = false;
                            }
                        }
                    }, operation);
                }
                return valid && matching_projected_slices == 1U;
            };

        const auto generic_update_slice_is_certified =
            [&](const SignalId signal, const RegionAccess& writer) {
                if (signal >= graph_signals.size()
                    || writer.process >= programs.size()
                    || programs[writer.process] == nullptr
                    || writer.process >= graph_processes.size()
                    || !std::ranges::binary_search(
                        certificate.members, writer.process)) {
                    return false;
                }
                const auto& process_node = graph_processes[writer.process];
                const auto* const process = programs[writer.process];
                if (process->id != writer.process
                    || process->scheduling_domain
                        != ProcessSchedulingDomain::generic
                    || process_node.scheduling_domain
                        != ProcessSchedulingDomain::generic
                    || process_node.update_kind != RegionUpdateKind::generic
                    || !process_node.pure
                    || process_node.dependencies_unknown
                    || !process_node.operation_write_ranges_exact
                    || !process_signal_access_is_complete(writer.process)
                    || writer.width == 0U
                    || writer.offset
                        >= graph_signals[signal].descriptor.width
                    || writer.width > graph_signals[signal].descriptor.width
                        - writer.offset) {
                    return false;
                }

                std::size_t matching_driver_regions { };
                for (const auto& region : process->driver_regions) {
                    if (region.signal != signal) {
                        continue;
                    }
                    ++matching_driver_regions;
                    if (region.whole || region.offset != writer.offset
                        || region.width != writer.width) {
                        return false;
                    }
                }
                if (matching_driver_regions != 1U) {
                    return false;
                }

                bool valid = true;
                std::size_t matching_generic_update_slices { };
                for (std::size_t instruction = 0U;
                     instruction < process->operations.size();
                     ++instruction) {
                    const auto operation
                        = process->operations.expanded(instruction);
                    visit_operation([&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        if constexpr (std::is_same_v<T, WriteUpdateSlice>) {
                            if (value.signal == signal) {
                                ++matching_generic_update_slices;
                                valid = valid && value.offset == writer.offset
                                    && value.domain
                                        == SignalUpdateDomain::generic
                                    && value.source < process->register_count;
                            }
                        } else if constexpr (
                            std::is_same_v<T, WriteBlocking>
                            || std::is_same_v<T, WriteUpdate>
                            || std::is_same_v<T, WriteAfter>
                            || std::is_same_v<T, WriteInertial>
                            || std::is_same_v<T, WriteProjected>
                            || std::is_same_v<T, WriteProjectedWaveform>
                            || std::is_same_v<T, WriteBlockingSlice>
                            || std::is_same_v<T, WriteAfterSlice>
                            || std::is_same_v<T, WriteInertialSlice>
                            || std::is_same_v<T, WriteProjectedSlice>
                            || std::is_same_v<T, WriteProjectedWaveformSlice>
                            || std::is_same_v<T, WriteBlockingDynamicSlice>
                            || std::is_same_v<T, WriteUpdateDynamicSlice>
                            || std::is_same_v<T, WriteAfterDynamicSlice>
                            || std::is_same_v<T, WriteInertialDynamicSlice>
                            || std::is_same_v<T,
                                WriteInertialDynamicPartSlice>
                            || std::is_same_v<T, WriteProjectedDynamicSlice>
                            || std::is_same_v<T,
                                WriteProjectedWaveformDynamicSlice>
                            || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                            || std::is_same_v<T, WriteUpdateDynamicPartSlice>
                            || std::is_same_v<T, WriteAfterDynamicPartSlice>
                            || std::is_same_v<T, ForceSignalSlice>
                            || std::is_same_v<T, ReleaseSignalSlice>) {
                            if (value.signal == signal) {
                                valid = false;
                            }
                        } else if constexpr (
                            std::is_same_v<T, WriteContainerObject>
                            || std::is_same_v<T,
                                WriteContainerObjectElement>) {
                            valid = false;
                        } else if constexpr (
                            std::is_same_v<T, VitalTimingCheck>) {
                            if (value.trigger_signal == signal) {
                                valid = false;
                            }
                        } else if constexpr (
                            std::is_same_v<T, VitalDelay>) {
                            if (value.output == signal) {
                                valid = false;
                            }
                        }
                    }, operation);
                    if (!valid) {
                        return false;
                    }
                }
                return valid && matching_generic_update_slices == 1U;
            };

        const auto can_seed_wide_disjoint_boundary =
            [&](const SignalId signal) {
                if (!wide_disjoint_owner_inventory_complete
                    || signal >= graph_signals.size()
                    || signal >= aliased_signals.size()
                    || signal >= driver_values.size()
                    || aliased_signals[signal] != 0U
                    || owned_driver_active(signal)
                    || (signal < switch_endpoint_adjacency.size()
                        && !switch_endpoint_adjacency[signal].empty())) {
                    return false;
                }
                const auto& node = graph_signals[signal];
                const auto& descriptor = node.descriptor;
                const bool vhdl_projected_slice_family
                    = node.partial_projected_transactions
                    && node.writers.size() >= 2U
                    && std::ranges::all_of(node.writers,
                        [&](const RegionAccess& writer) {
                            return writer.process < graph_processes.size()
                                && graph_processes[writer.process]
                                        .scheduling_domain
                                    == ProcessSchedulingDomain::generic
                                && graph_processes[writer.process].update_kind
                                    == RegionUpdateKind::vhdl_projected
                                && vhdl_projected_slice_is_certified(
                                    signal, writer);
                        });
                const bool generic_update_slice_family
                    = !node.partial_projected_transactions
                    && descriptor.value_kind == ValueKind::logic4
                    && node.writers.size() >= 2U
                    && std::ranges::all_of(node.writers,
                        [&](const RegionAccess& writer) {
                            return generic_update_slice_is_certified(
                                signal, writer);
                        });
                if (node.drivers != RegionDriverClass::disjoint_partial
                    || node.writers.size() < 2U
                    || node.writers_unknown || node.dynamic_fork_writers
                    || (node.partial_projected_transactions
                        && !vhdl_projected_slice_family)
                    || node.observations != RegionObservation::none
                    || descriptor.width == 0U
                    || descriptor.implicit_driver
                    || descriptor.external_driver
                    || descriptor.event_variable
                    || (descriptor.value_kind == ValueKind::logic4
                        ? descriptor.resolution != ResolutionKind::sv_wire
                        : descriptor.value_kind != ValueKind::logic9
                            || descriptor.resolution
                                != ResolutionKind::std_logic)) {
                    return false;
                }
                const auto process_is_member =
                    [&](const ProcessId process) {
                        return std::ranges::binary_search(
                            certificate.members, process);
                    };
                const bool all_accesses_are_component_local
                    = std::ranges::all_of(node.writers,
                        [&](const RegionAccess& access) {
                            const bool owner_is_supported =
                                access.process < graph_processes.size()
                                && ((vhdl_projected_slice_family
                                        && graph_processes[access.process]
                                                .scheduling_domain
                                            == ProcessSchedulingDomain::generic
                                        && graph_processes[access.process]
                                                .update_kind
                                            == RegionUpdateKind::vhdl_projected)
                                    || (generic_update_slice_family
                                        && graph_processes[access.process]
                                                .scheduling_domain
                                            == ProcessSchedulingDomain::generic
                                        && graph_processes[access.process]
                                                .update_kind
                                            == RegionUpdateKind::generic)
                                    || (!vhdl_projected_slice_family
                                        && !generic_update_slice_family
                                        && graph_processes[access.process]
                                                .scheduling_domain
                                            == ProcessSchedulingDomain::systemverilog
                                        && graph_processes[access.process]
                                                .update_kind
                                            == RegionUpdateKind::systemverilog_active));
                            return process_is_member(access.process)
                                && owner_is_supported
                                && access.width != 0U
                                && access.offset < descriptor.width
                                && access.width
                                    <= descriptor.width - access.offset;
                        })
                    && std::ranges::all_of(node.readers,
                        [&](const RegionAccess& access) {
                            return process_is_member(access.process);
                        });
                if (!all_accesses_are_component_local) {
                    return false;
                }

                std::vector<ProcessId> owners;
                owners.reserve(node.writers.size());
                std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
                ranges.reserve(node.writers.size());
                for (const auto& writer : node.writers) {
                    owners.push_back(writer.process);
                    ranges.emplace_back(writer.offset, writer.width);
                }
                std::ranges::sort(owners);
                owners.erase(std::ranges::unique(owners).begin(),
                    owners.end());
                if (owners.size() < 2U) {
                    return false;
                }
                std::ranges::sort(ranges);
                std::uint64_t covered_end { };
                for (const auto& [offset, width] : ranges) {
                    if (offset != covered_end) {
                        return false;
                    }
                    covered_end += width;
                }
                if (covered_end != descriptor.width
                    || driver_values[signal].size() != owners.size()) {
                    return false;
                }
                return std::ranges::all_of(owners,
                    [&](const ProcessId owner) {
                        const auto* const record
                            = driver_values[signal].find(owner);
                        return record != nullptr
                            && record->strength == DriveStrength { };
                    });
            };

        const auto is_a2_driverless_blocking_alias = [
            &](const SignalId signal, const ProcessId owner) {
                if (signal >= snapshot.graph.signals().size()
                    || signal >= driver_values.size()
                    || !driver_values[signal].empty()
                    || component >= snapshot.programs_by_component.size()
                    || !snapshot.programs_by_component[component]) {
                    return false;
                }
                const auto& descriptor
                    = snapshot.graph.signals()[signal].descriptor;
                const auto& outputs
                    = snapshot.programs_by_component[component]
                          ->activation_kernel.outputs;
                const auto binding = std::ranges::find(outputs, signal,
                    &RegionConeOutputBinding::signal);
                const auto owner_output_count = std::ranges::count_if(
                    outputs, [owner](const RegionConeOutputBinding& output) {
                        return output.owner == owner;
                    });
                return descriptor.resolution == ResolutionKind::none
                    && binding != outputs.end()
                    && binding->owner == owner
                    && owner_output_count == 1U
                    && binding->offset == 0U
                    && binding->width == descriptor.width
                    && binding->value_kind == ValueKind::logic4
                    && binding->domain
                        == SignalUpdateDomain::systemverilog_active
                    && binding->update_kind
                        == RegionUpdateKind::systemverilog_active
                    && binding->publication_kind
                        == RegionOutputPublicationKind::blocking_immediate;
            };
        const auto can_seed_a2_forwarding_internal
            = [&](const SignalId signal) {
                if (!a4_wide_single_owner_commit_enabled
                    || !component_storage_inventory_complete
                    || !has_compute_program
                    || !snapshot.programs_by_component[component]
                            ->forwarding_kernel
                    || signal >= snapshot.graph.signals().size()
                    || signal >= signals.size()
                    || signal >= aliased_signals.size()
                    || signal >= driver_values.size()
                    || signal >= external_driver_values.size()
                    || signal >= forced_values.size()
                    || signal >= forced_masks.size()
                    || signal >= forced_driver_values.size()
                    || signal >= forced_driver_masks.size()
                    || signal >= dynamic_fanout.size()
                    || signal >= signal_transaction_observed.size()
                    || signal >= signal_container_aliases.size()
                    || signal >= signal_container_element_aliases.size()
                    || signal >= signal_container_aggregate_aliases.size()
                    || signal >= switch_endpoint_adjacency.size()
                    || signal >= module_path_destination_mask.size()
                    || owned_driver_active(signal)
                    || aliased_signals[signal] != 0U
                    || external_driver_values[signal] || forced_values[signal]
                    || forced_masks[signal] || forced_driver_values[signal]
                    || forced_driver_masks[signal]
                    || signal_transaction_observed[signal]
                    || !dynamic_fanout[signal].empty()
                    || !signal_container_aliases[signal].empty()
                    || signal_container_element_aliases[signal]
                    || signal_container_aggregate_aliases[signal]
                    || !switch_endpoint_adjacency[signal].empty()
                    || module_path_destination_mask[signal] != 0U
                    || monitor_watches(signal)) {
                    return false;
                }
                const auto& forwarding
                    = *snapshot.programs_by_component[component]
                            ->forwarding_kernel;
                if (!std::ranges::binary_search(
                        forwarding.internal_signals, signal)) {
                    return false;
                }
                const auto& node = snapshot.graph.signals()[signal];
                const auto& descriptor = node.descriptor;
                if (descriptor.width == 0U
                    || descriptor.value_kind != ValueKind::logic4
                    || (descriptor.resolution != ResolutionKind::sv_wire
                        && descriptor.resolution != ResolutionKind::none)
                    || descriptor.implicit_driver || descriptor.external_driver
                    || descriptor.event_variable
                    || node.drivers != RegionDriverClass::single_whole
                    || node.writers.size() != 1U || node.writers_unknown
                    || node.dynamic_fork_writers
                    || node.partial_projected_transactions
                    || node.observations != RegionObservation::none
                    || node.writers.front().offset != 0U
                    || (node.writers.front().width != 0U
                        && node.writers.front().width != descriptor.width)
                    || signals[signal].initial_value.width() != descriptor.width
                    || signals[signal].value_kind != ValueKind::logic4
                    || signals[signal].initial_value.is_logic9()
                    || signals[signal].has_implicit_driver
                    || signals[signal].has_charge_strength
                    || signals[signal].event_variable
                    || signals[signal].public_value_reference_exposed
                    || signals[signal].systemverilog_scalar
                        != SystemVerilogScalarKind::None
                    || (signals[signal].resolution
                            != ResolutionKind::sv_wire
                        && signals[signal].resolution != ResolutionKind::none)) {
                    return false;
                }
                const auto owner = node.writers.front().process;
                const auto graph_processes = snapshot.graph.processes();
                if (owner >= graph_processes.size()
                    || !std::ranges::binary_search(
                        certificate.members, owner)
                    || graph_processes[owner].scheduling_domain
                        != ProcessSchedulingDomain::systemverilog
                    || graph_processes[owner].update_kind
                        != RegionUpdateKind::systemverilog_active) {
                    return false;
                }
                if (driver_values[signal].empty()) {
                    return is_a2_driverless_blocking_alias(signal, owner);
                }
                if (driver_values[signal].size() != 1U) {
                    return false;
                }
                const auto* const record
                    = driver_values[signal].find(owner);
                return record != nullptr
                    && record->strength == DriveStrength { };
            };
        const bool a2_forwarding_internal_storage
            = std::ranges::any_of(
                certificate.structural_internal_signal_candidates,
                can_seed_a2_forwarding_internal);

        if ((has_compute_program && certificate.status
                == RegionComponentCertificateStatus::structural_candidate)
            || wide_storage_inventory_complete) {
            std::vector<SignalId> candidate_signals;
            std::vector<SignalId> certified_projected_slice_signals;
            std::vector<SignalId> certified_unresolved_owner_aliases;
            candidate_signals.reserve(
                certificate.structural_internal_signal_candidates.size()
                + certificate.boundary_signals.size());
            const bool include_single_owner_internal_signals
                = (has_compute_program
                    && (!a4_wide_disjoint_owner_commit_enabled
                        || a4_wide_single_owner_commit_enabled))
                || wide_single_owner_inventory_complete;
            if ((include_single_owner_internal_signals
                    || a2_forwarding_internal_storage)
                && certificate.status
                == RegionComponentCertificateStatus::structural_candidate) {
                for (const auto signal :
                    certificate.structural_internal_signal_candidates) {
                    if (signal >= aliased_signals.size()
                        || aliased_signals[signal] != 0U
                        || owned_driver_active(signal)
                        || (signal < switch_endpoint_adjacency.size()
                            && !switch_endpoint_adjacency[signal].empty())) {
                        continue;
                    }
                    const auto width
                        = snapshot.graph.signals()[signal].descriptor.width;
                    const bool a2_internal_slot
                        = can_seed_a2_forwarding_internal(signal);
                    if (!include_single_owner_internal_signals
                        && !a2_internal_slot) {
                        continue;
                    }
                    // Wide ownership has its own storage certificate and
                    // does not require a compute kernel for the component.
                    // Narrow private slots still require that kernel.
                    if (!has_compute_program
                        && !can_seed_wide_single_owner(signal)) {
                        continue;
                    }
                    // The wide policy binds only slots whose ordinary owner
                    // publication can preserve that authority. An unsupported
                    // downstream writer must not revoke unrelated wide slots.
                    if (a4_wide_single_owner_commit_enabled
                        && width > 64U
                        && !can_seed_wide_single_owner(signal)
                        && !a2_internal_slot) {
                        continue;
                    }
                    candidate_signals.push_back(signal);
                    const auto& graph_signal
                        = snapshot.graph.signals()[signal];
                    const auto owner = graph_signal.writers.size() == 1U
                        ? graph_signal.writers.front().process
                        : std::numeric_limits<ProcessId>::max();
                    if ((can_seed_wide_single_owner(signal)
                            || (a2_internal_slot
                                && is_a2_driverless_blocking_alias(
                                    signal, owner)))
                        && graph_signal.descriptor.resolution
                            == ResolutionKind::none) {
                        certified_unresolved_owner_aliases.push_back(signal);
                    }
                }
            }
            if (wide_single_owner_inventory_complete) {
                for (const auto signal : certificate.boundary_signals) {
                    if (can_seed_wide_single_owner(signal)
                        || can_seed_narrow_logic9_boundary(signal)) {
                        candidate_signals.push_back(signal);
                        if (snapshot.graph.signals()[signal].descriptor.resolution
                            == ResolutionKind::none) {
                            certified_unresolved_owner_aliases.push_back(
                                signal);
                        }
                    }
                }
            }
            if (wide_disjoint_owner_inventory_complete) {
                for (const auto signal : certificate.boundary_signals) {
                    if (can_seed_wide_disjoint_boundary(signal)) {
                        candidate_signals.push_back(signal);
                        if (snapshot.graph.signals()[signal]
                                .partial_projected_transactions) {
                            certified_projected_slice_signals.push_back(
                                signal);
                        }
                    }
                }
            }
            if (!candidate_signals.empty()) {
                std::ranges::sort(certified_unresolved_owner_aliases);
                certified_unresolved_owner_aliases.erase(
                    std::unique(
                        certified_unresolved_owner_aliases.begin(),
                        certified_unresolved_owner_aliases.end()),
                    certified_unresolved_owner_aliases.end());
                // Driver planes are optional. Explicit unresolved aliases
                // intentionally have no DriverTable record; other absent
                // direct owners stay on their retained path rather than
                // seeding an invented raw record from the visible value.
                const auto proposed_layout
                    = SignalDriverLayout::build_from_inventory(
                    snapshot.graph, candidate_signals,
                    *snapshot.signal_driver_inventory,
                    certified_projected_slice_signals,
                    certified_unresolved_owner_aliases);
                std::vector<SignalId> seeded_signals;
                seeded_signals.reserve(candidate_signals.size());
                for (const auto signal : candidate_signals) {
                    const auto& signal_layout
                        = proposed_layout.signal(signal);
                    const auto owners = proposed_layout.owners(signal);
                    const bool all_owners_registered
                        = std::ranges::all_of(owners,
                            [&](const SignalDriverOwnerLayout& owner) {
                                if (owner.aliases_stored) {
                                    return driver_values[signal].empty();
                                }
                                return driver_values[signal].find(
                                    owner.process) != nullptr;
                            });
                    if (signal_layout.storage_class
                            == SignalDriverStorageClass::resolved_table
                        || all_owners_registered) {
                        seeded_signals.push_back(signal);
                    }
                }
                if (!seeded_signals.empty()) {
                    try {
                        auto layout = SignalDriverLayout::build_from_inventory(
                            snapshot.graph, seeded_signals,
                            *snapshot.signal_driver_inventory,
                            certified_projected_slice_signals,
                            certified_unresolved_owner_aliases);
                        const bool has_seeded_wide_signal
                            = std::ranges::any_of(seeded_signals,
                                [&](const SignalId signal) {
                                    return snapshot.graph.signals()[signal]
                                        .descriptor.width > 64U;
                                });
                        const bool has_seeded_disjoint_owner_signal
                            = std::ranges::any_of(seeded_signals,
                                [&](const SignalId signal) {
                                    return can_seed_wide_disjoint_boundary(
                                        signal);
                                });
                        const bool use_versioned_disjoint_owner_storage
                            = a4_wide_disjoint_owner_versioned_storage_explicit
                            || (a4_wide_disjoint_owner_commit_enabled
                                && has_seeded_disjoint_owner_signal);
                        const bool use_versioned_single_owner_storage
                            = a4_wide_single_owner_versioned_storage_explicit
                            || (a4_wide_single_owner_commit_enabled
                                && has_seeded_wide_signal)
                            || (a2_forwarding_internal_storage
                                && std::ranges::any_of(seeded_signals,
                                    can_seed_a2_forwarding_internal));
                        const auto packed_slot_policy
                            = use_versioned_disjoint_owner_storage
                                ? PackedSlotBindingPolicy::
                                    experimental_wide_disjoint_owners
                            : use_versioned_single_owner_storage
                                ? PackedSlotBindingPolicy::experimental_wide
                                : PackedSlotBindingPolicy::narrow_only;
                        auto state = std::make_shared<
                            RegionAuthoritativeComponentState>(
                                snapshot.generation, std::move(layout),
                                has_compute_program
                                    ? RegionGroupedFanout::build(programs,
                                        certificate.members)
                                    : RegionGroupedFanout { },
                                has_compute_program
                                    ? certificate.members.size() : 0U,
                                packed_slot_policy);
                        for (const auto signal : seeded_signals) {
                            materialize_direct_signal(signal);
                            state->values().seed_signal(signal,
                                signals[signal].initial_value,
                                signal_last_values[signal],
                                driven_values[signal]);
                            for (const auto& owner :
                                state->values().layout().owners(signal)) {
                                if (owner.aliases_stored) {
                                    state->values().seed_owner(signal,
                                        owner.process,
                                        driven_values[signal]);
                                    continue;
                                }
                                const auto* record
                                    = driver_values[signal].find(
                                        owner.process);
                                if (record == nullptr) {
                                    throw std::invalid_argument {
                                        "component owner disappeared during seeding"
                                    };
                                }
                                state->values().seed_owner(signal,
                                    owner.process, record->value);
                            }
                            if (state->values()
                                    .supports_packed_slot_binding(signal)) {
                                state->values().stage_packed_signal_slots(
                                    signal, signals[signal].initial_value,
                                    signal_last_values[signal],
                                    driven_values[signal]);
                                for (const auto& owner :
                                    state->values().layout().owners(signal)) {
                                    if (owner.aliases_stored) {
                                        state->values()
                                            .stage_packed_owner_stored_alias(
                                                signal, owner.process);
                                        continue;
                                    }
                                    auto* record
                                        = driver_values[signal].find(
                                            owner.process);
                                    if (record == nullptr) {
                                        throw std::invalid_argument {
                                            "component owner disappeared while staging packed slots"
                                        };
                                    }
                                    state->values().stage_packed_owner_slot(
                                        signal, owner.process, record->value);
                                }
                            }
                        }
                        snapshot.authoritative_state_by_component[component]
                            = std::move(state);
                        for (const auto signal : seeded_signals) {
                            auto& mapped = snapshot
                                .authoritative_component_by_signal[signal];
                            if (mapped != std::numeric_limits<std::size_t>::max()) {
                                throw std::logic_error {
                                    "an internal signal belongs to multiple region components"
                                };
                            }
                            mapped = component;
                        }
                    } catch (const std::invalid_argument& error) {
                        // A4 storage is only a certified mirror. An
                        // unsupported current/owner value must not prevent
                        // the checked graph/kernel snapshot from publishing.
                        // Keep a quiet-point retry armed when later runtime
                        // traffic may make the complete component representable.
                        snapshot.authoritative_state_incomplete = true;
                        if (profile_frontier_preparation) {
                            ++authoritative_seed_failures;
                            if (authoritative_seed_failures == 1U) {
                                std::fprintf(stderr,
                                    "fsim-profile: sv-region-recertification "
                                    "event=authoritative-seed-failure "
                                    "runtime_generation=%llu component=%zu "
                                    "members=%zu reason='%s'\n",
                                    static_cast<unsigned long long>(snapshot.generation),
                                    component, certificate.members.size(), error.what());
                            }
                        }
                    }
                }
            }
        }


        if (!has_compute_program) {
            // Checked execution can use the proven owner storage above.
            // It has no native readiness mask, local wave, or backend entry.
            continue;
        }
        auto& kernel = snapshot.programs_by_component[component]
                                 ->activation_kernel;
        auto& readiness_descriptor
            = snapshot.readiness_mask_by_component[component];
        readiness_descriptor.offset = snapshot.readiness_mask_words.size();
        readiness_descriptor.word_count
            = (kernel.members.size() + 63U) / 64U;
        readiness_descriptor.generation = kernel.members.empty()
            ? 0U : snapshot.generation;
        snapshot.readiness_mask_words.resize(
            readiness_descriptor.offset
                + readiness_descriptor.word_count,
            0U);
        for (std::size_t member_index = 0U;
             member_index < kernel.members.size(); ++member_index) {
            const auto process = kernel.members[member_index].process;
            if (process >= snapshot.readiness_member_index_by_process.size()
                || snapshot.readiness_member_index_by_process[process]
                    != std::numeric_limits<std::size_t>::max()) {
                throw std::logic_error {
                    "region readiness membership is not a partition"
                };
            }
            snapshot.readiness_member_index_by_process[process]
                = member_index;
        }

        for (const auto& input : kernel.inputs) {
            if (input.internal || input.signal >= graph_signals.size()
                || (input.value_kind != ValueKind::logic4
                    && input.value_kind != ValueKind::logic9)) {
                continue;
            }
            const auto& signal = graph_signals[input.signal];
            if (signal.writers_unknown || signal.dynamic_fork_writers
                || signal.partial_projected_transactions
                || signal.writers.size() != 1U
                || aliased_signals[input.signal] != 0U
                || signal.descriptor.observations != RegionObservation::none
                || signal.descriptor.width != input.width
                || signal.descriptor.value_kind != input.value_kind
                || signal.descriptor.external_driver
                || signal.descriptor.implicit_driver
                || signal.descriptor.event_variable) {
                continue;
            }
            const auto owner = signal.writers.front().process;
            if (owner >= processes.size()) {
                continue;
            }
            const auto* const startup = processes.compact_constant(owner);
            if (startup == nullptr || startup->signal != input.signal
                || startup->slice
                || (input.signal < forced_values.size()
                    && forced_values[input.signal])
                || (input.signal < forced_masks.size()
                    && forced_masks[input.signal])
                || (input.signal < forced_driver_values.size()
                    && forced_driver_values[input.signal])
                || (input.signal < forced_driver_masks.size()
                    && forced_driver_masks[input.signal])
                || std::ranges::find(kernel.members, owner,
                    &RegionConeKernelMember::process)
                    != kernel.members.end()) {
                continue;
            }
            if (std::ranges::count(kernel.inputs, input.signal,
                    &RegionConeKernelInput::signal) != 1U) {
                continue;
            }
            std::optional<PackedLogic4> constant_value;
            if (startup->startup_write_bank != nullptr) {
                constant_value = startup->startup_write_bank->value;
            } else {
                const auto owner_operations
                    = processes.program_view(owner).operations();
                const auto statement_offset
                    = owner_operations.size() == 5U ? 1U : 0U;
                const auto load_operation
                    = owner_operations.expanded(1U + statement_offset);
                const auto* const load
                    = operation_get_if<LoadConstant>(&load_operation);
                if (load != nullptr) {
                    constant_value = load->value;
                }
            }
            if (!constant_value || constant_value->width() != input.width) {
                continue;
            }
            if (owner >= graph_processes.size()) {
                continue;
            }
            const auto& owner_node = graph_processes[owner];
            const bool active_startup = !startup->projected
                && startup->update_domain
                    == SignalUpdateDomain::systemverilog_active
                && owner_node.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                && owner_node.update_kind
                    == RegionUpdateKind::systemverilog_active;
            const bool projected_startup = startup->projected
                && startup->update_domain == SignalUpdateDomain::generic
                && owner_node.scheduling_domain
                    == ProcessSchedulingDomain::generic
                && owner_node.update_kind == RegionUpdateKind::vhdl_projected;
            if (!active_startup && !projected_startup) {
                continue;
            }
            RegionConeConstantInput constant {
                owner, input.signal, 0U, input.width,
                input.value_kind, startup->update_domain,
                coerce_value_kind(*constant_value, input.value_kind),
                projected_startup ? RegionUpdateKind::vhdl_projected
                                  : RegionUpdateKind::systemverilog_active,
                startup->projected_mode,
                startup->projected_delay,
                startup->projected_rejection
            };
            if (projected_startup) {
                const auto owner_operations
                    = processes.program_view(owner).operations();
                const auto statement_offset
                    = owner_operations.size() == 5U ? 1U : 0U;
                const auto load_operation
                    = owner_operations.expanded(1U + statement_offset);
                const auto* const load
                    = operation_get_if<LoadConstant>(&load_operation);
                const auto write_operation
                    = owner_operations.expanded(2U + statement_offset);
                const auto* const write
                    = operation_get_if<WriteProjected>(&write_operation);
                if (load == nullptr || write == nullptr
                    || write->signal != input.signal
                    || write->source != load->destination
                    || write->mode != startup->projected_mode
                    || write->delay != startup->projected_delay
                    || write->rejection != startup->projected_rejection) {
                    continue;
                }
            }
            if (constant.value.is_logic9()
                != (input.value_kind == ValueKind::logic9)) {
                continue;
            }
            kernel.constant_inputs.push_back(std::move(constant));
        }
        std::ranges::sort(kernel.constant_inputs,
            [](const RegionConeConstantInput& left,
               const RegionConeConstantInput& right) {
                if (left.signal != right.signal) {
                    return left.signal < right.signal;
                }
                return left.owner < right.owner;
            });
        auto& forwarding_kernel
            = snapshot.programs_by_component[component]->forwarding_kernel;
        if (systemverilog_local_wave_enabled
            && kernel.program.scheduling_domain
                == ProcessSchedulingDomain::systemverilog
            && !kernel.internal_signals.empty()
            && snapshot.authoritative_state_by_component[component]) {
            try {
                snapshot.local_wave_state_by_component[component]
                    = std::make_shared<RegionLocalWaveComponentState>(
                        snapshot.generation, kernel,
                        forwarding_kernel ? &*forwarding_kernel : nullptr);
            } catch (const std::bad_alloc&) {
                // Local register retention is optional. Keep the checked
                // activation program and ordinary update path available.
            } catch (const std::invalid_argument&) {
                // A malformed optional local bank cannot weaken the checked
                // program's normal activation path.
            }
        }
        if (forwarding_kernel
            && component < snapshot.local_wave_state_by_component.size()
            && snapshot.local_wave_state_by_component[component]) {
            const auto matching_forwarding_backend
                = std::ranges::find_if(snapshot.forwarding_backend_pool,
                    [&](const auto& entry) {
                        return entry
                            && entry->provider_identity
                                == region_kernel_backend_provider_identity
                            && same_region_forwarding_mapping(
                                entry->kernel, *forwarding_kernel);
                    });
            if (matching_forwarding_backend
                != snapshot.forwarding_backend_pool.end()) {
                forwarding_kernel->execution_kernel.program.operations
                    = (*matching_forwarding_backend)->kernel
                          .execution_kernel.program.operations;
                snapshot.forwarding_backends_by_component[component]
                    = *matching_forwarding_backend;
            } else if (may_compile_backends
                && region_kernel_backend_provider) {
                auto* const forwarding_provider
                    = dynamic_cast<RegionConeForwardingBackendProvider*>(
                        region_kernel_backend_provider.get());
                if (forwarding_provider != nullptr) {
                    std::unique_ptr<RegionConeForwardingBackend> backend;
                    try {
                        backend = forwarding_provider->create_forwarding(
                            *forwarding_kernel);
                    } catch (const std::bad_alloc&) {
                        throw;
                    } catch (...) {
                        // Forwarding is optional. A failed provider build
                        // leaves the ordinary activation backend intact.
                    }
                    if (backend) {
                        auto entry
                            = std::make_shared<
                                RegionConeForwardingBackendEntry>();
                        entry->kernel = *forwarding_kernel;
                        entry->provider_identity
                            = region_kernel_backend_provider_identity;
                        entry->executor = std::move(backend);
                        snapshot.forwarding_backend_pool.push_back(entry);
                        snapshot.forwarding_backends_by_component[component]
                            = std::move(entry);
                    }
                }
            }
        }
    }

    snapshot.grouped_fanout_by_signal.resize(
        snapshot.graph.signals().size());
    snapshot.prepared_successor_by_signal.resize(
        snapshot.graph.signals().size());
    const auto graph_signal_descriptors = snapshot.graph.signals();
    // Keep exact-width tuples intact for graph and cache identity, but let
    // host readiness use the canonical whole-signal path for an explicit
    // full-width range when the signal is internal to this component.
    const auto is_whole_signal_any_sensitivity
        = [&graph_signal_descriptors](const Sensitivity& sensitivity,
              const bool allow_explicit_full_width) {
              if (sensitivity.edge != EdgeKind::any
                  || sensitivity.offset != 0U) {
                  return false;
              }
              if (sensitivity.width == 0U) {
                  return true;
              }
              return allow_explicit_full_width
                  && sensitivity.signal < graph_signal_descriptors.size()
                  && sensitivity.width
                      == graph_signal_descriptors[sensitivity.signal]
                             .descriptor.width;
          };
    const auto normalize_grouped_sensitivity_range
        = [&graph_signal_descriptors](const Sensitivity& sensitivity,
              const bool allow_internal_range)
              -> std::optional<RegionFrontierSensitivityRange> {
              if (sensitivity.edge != EdgeKind::any
                  || sensitivity.signal >= graph_signal_descriptors.size()) {
                  return std::nullopt;
              }
              const auto signal_width
                  = graph_signal_descriptors[sensitivity.signal]
                        .descriptor.width;
              if (signal_width == 0U) {
                  return std::nullopt;
              }
              if (sensitivity.offset == 0U && sensitivity.width == 0U) {
                  return RegionFrontierSensitivityRange {
                      0U, signal_width };
              }
              if (!allow_internal_range || sensitivity.width == 0U
                  || sensitivity.offset >= signal_width
                  || sensitivity.width
                      > signal_width - sensitivity.offset) {
                  return std::nullopt;
              }
              return RegionFrontierSensitivityRange {
                  sensitivity.offset, sensitivity.width };
          };
    const bool all_programs_present
        = std::ranges::all_of(programs,
            [](const Process* program) { return program != nullptr; });
    if (all_programs_present && process_access_inventory_complete
        && snapshot.graph.certificate_inventory().access_inventory_complete
        && snapshot.graph.processes().size() == programs.size()) {
        struct GroupedMember {
            std::uint64_t trigger_mask { };
            std::vector<RegionFrontierSensitivityRange> ranges;
        };
        using GroupMembers = std::map<ProcessId, GroupedMember>;
        using SignalGroups = std::map<std::size_t, GroupMembers>;
        std::vector<SignalGroups> grouped(snapshot.graph.signals().size());
        std::vector<std::uint8_t> signal_complete(
            snapshot.graph.signals().size(), 1U);
        const auto graph_processes = snapshot.graph.processes();
        for (std::size_t process_index = 0U;
             process_index < programs.size(); ++process_index) {
            const auto* const program = programs[process_index];
            if (program == nullptr) {
                continue;
            }
            const auto process = static_cast<ProcessId>(process_index);
            for (std::size_t sensitivity_index = 0U;
                 sensitivity_index < program->static_sensitivity.size();
                 ++sensitivity_index) {
                const auto& sensitivity
                    = program->static_sensitivity[sensitivity_index];
                if (sensitivity.signal
                    >= snapshot.grouped_fanout_by_signal.size()) {
                    continue;
                }
                const auto signal = sensitivity.signal;
                auto& valid = signal_complete[signal];
                const auto component
                    = snapshot.component_by_process[process_index];
                const bool internal_signal
                    = component
                        < snapshot.programs_by_component.size()
                    && snapshot.programs_by_component[component]
                    && std::ranges::find(
                        snapshot.programs_by_component[component]
                            ->activation_kernel.internal_signals,
                        signal)
                        != snapshot.programs_by_component[component]
                               ->activation_kernel.internal_signals.end();
                const auto range
                    = normalize_grouped_sensitivity_range(
                        sensitivity, internal_signal);
                const bool partial_range = range
                    && (range->offset != 0U
                        || range->width
                            != graph_signal_descriptors[signal]
                                   .descriptor.width);
                const bool trigger_regions_compatible
                    = !partial_range
                    || (component
                            < snapshot.programs_by_component.size()
                        && snapshot.programs_by_component[component]
                        && snapshot.programs_by_component[component]
                               ->activation_kernel.program
                               .static_trigger_regions.empty());
                const bool member_exact
                    = component
                        < snapshot.programs_by_component.size()
                    && snapshot.programs_by_component[component]
                    && process
                        < snapshot.readiness_member_index_by_process.size()
                    && snapshot.readiness_member_index_by_process[process]
                        != std::numeric_limits<std::size_t>::max()
                    && snapshot.readiness_member_index_by_process[process]
                        < snapshot.programs_by_component[component]
                               ->activation_kernel.members.size()
                    && snapshot.programs_by_component[component]
                               ->activation_kernel.members[
                                   snapshot.readiness_member_index_by_process[
                                       process]]
                               .process == process;
                const auto& node = graph_processes[process_index];
                const bool node_exact
                    = node.process == process && node.pure
                    && !node.dependencies_unknown
                    && !node.cyclic_or_dependent_on_cycle
                    && node.scheduling_domain
                        == ProcessSchedulingDomain::systemverilog
                    && node.update_kind
                        == RegionUpdateKind::systemverilog_active
                    && std::ranges::any_of(node.sensitivities,
                        [&](const Sensitivity& candidate) {
                            return candidate.signal == signal
                                && candidate.edge == sensitivity.edge
                                && candidate.offset == sensitivity.offset
                                && candidate.width == sensitivity.width;
                        });
                if (!range || !trigger_regions_compatible || !member_exact
                    || !node_exact) {
                    valid = 0U;
                    continue;
                }
                const auto trigger_mask
                    = sensitivity_index < 63U
                            && !program->static_trigger_regions.empty()
                    ? UINT64_C(1) << sensitivity_index
                    : Process::full_static_trigger_mask;
                auto& grouped_member
                    = grouped[signal][component][process];
                grouped_member.trigger_mask |= trigger_mask;
                grouped_member.ranges.push_back(*range);
            }
        }

        for (std::size_t signal = 0U;
             signal < grouped.size(); ++signal) {
            if (signal_complete[signal] == 0U
                || signal >= aliased_signals.size()
                || aliased_signals[signal] != 0U
                || grouped[signal].empty()) {
                continue;
            }
            const auto group_offset
                = snapshot.grouped_fanout_groups.size();
            const auto member_offset_start
                = snapshot.grouped_fanout_members.size();
            const auto sensitivity_range_offset_start
                = snapshot.grouped_fanout_sensitivity_ranges.size();
            bool signal_map_valid = true;
            for (auto& [component, members] : grouped[signal]) {
                const auto member_offset
                    = snapshot.grouped_fanout_members.size();
                if (members.size()
                    > snapshot.grouped_fanout_members.max_size()
                        - member_offset) {
                    signal_map_valid = false;
                    break;
                }
                for (auto& [process, grouped_member] : members) {
                    auto& ranges = grouped_member.ranges;
                    std::sort(ranges.begin(), ranges.end(),
                        [](const auto& left, const auto& right) {
                            return left.offset < right.offset
                                || (left.offset == right.offset
                                    && left.width < right.width);
                        });
                    ranges.erase(std::unique(ranges.begin(), ranges.end(),
                                      [](const auto& left, const auto& right) {
                                          return left.offset == right.offset
                                              && left.width == right.width;
                                      }),
                        ranges.end());
                    const auto range_offset
                        = snapshot.grouped_fanout_sensitivity_ranges.size();
                    if (ranges.empty()
                        || range_offset
                            > snapshot.grouped_fanout_sensitivity_ranges
                                  .max_size()
                        || ranges.size()
                            > snapshot.grouped_fanout_sensitivity_ranges
                                  .max_size() - range_offset) {
                        signal_map_valid = false;
                        break;
                    }
                    snapshot.grouped_fanout_sensitivity_ranges.insert(
                        snapshot.grouped_fanout_sensitivity_ranges.end(),
                        ranges.begin(), ranges.end());
                    snapshot.grouped_fanout_members.push_back({
                        process,
                        snapshot.readiness_member_index_by_process[process],
                        grouped_member.trigger_mask,
                        range_offset,
                        ranges.size(),
                        snapshot.generation });
                }
                if (!signal_map_valid) {
                    break;
                }
                if (snapshot.grouped_fanout_groups.size()
                    == snapshot.grouped_fanout_groups.max_size()) {
                    signal_map_valid = false;
                    break;
                }
                snapshot.grouped_fanout_groups.push_back({ component,
                    member_offset, members.size(), snapshot.generation });
            }
            if (!signal_map_valid) {
                snapshot.grouped_fanout_groups.resize(group_offset);
                snapshot.grouped_fanout_members.resize(member_offset_start);
                snapshot.grouped_fanout_sensitivity_ranges.resize(
                    sensitivity_range_offset_start);
                continue;
            }
            auto& descriptor = snapshot.grouped_fanout_by_signal[signal];
            descriptor.group_offset = group_offset;
            descriptor.group_count
                = snapshot.grouped_fanout_groups.size() - group_offset;
            descriptor.generation = descriptor.group_count == 0U
                ? 0U : snapshot.generation;
        }
    }

    std::size_t maximum_grouped_readiness_members { };
    for (const auto& group : snapshot.grouped_fanout_groups) {
        maximum_grouped_readiness_members = std::max(
            maximum_grouped_readiness_members, group.member_count);
    }
    snapshot.grouped_readiness_member_scratch.resize(
        maximum_grouped_readiness_members);
    snapshot.grouped_readiness_receipt_scratch.resize(
        maximum_grouped_readiness_members);
    snapshot.grouped_readiness_queue_scratch.resize(
        maximum_grouped_readiness_members);
    snapshot.grouped_readiness_member_changed_scratch.resize(
        maximum_grouped_readiness_members);
    snapshot.grouped_readiness_process_scratch.resize(
        maximum_grouped_readiness_members);

    const auto collect_kernel_sensitivity_ranges
        = [&](const auto& sensitivities, const SignalId signal,
              const bool static_trigger_regions_empty,
              std::vector<RegionFrontierSensitivityRange>& ranges) {
              ranges.clear();
              for (const auto& sensitivity : sensitivities) {
                  if (sensitivity.signal != signal) {
                      continue;
                  }
                  const auto range
                      = normalize_grouped_sensitivity_range(
                          sensitivity, true);
                  if (!range) {
                      return false;
                  }
                  const bool partial
                      = range->offset != 0U
                      || range->width
                          != graph_signal_descriptors[signal]
                                 .descriptor.width;
                  if (partial && !static_trigger_regions_empty) {
                      return false;
                  }
                  ranges.push_back(*range);
              }
              std::sort(ranges.begin(), ranges.end(),
                  [](const auto& left, const auto& right) {
                      return left.offset < right.offset
                          || (left.offset == right.offset
                              && left.width < right.width);
                  });
              ranges.erase(std::unique(ranges.begin(), ranges.end(),
                                [](const auto& left, const auto& right) {
                                    return left.offset == right.offset
                                        && left.width == right.width;
                                }),
                  ranges.end());
              return true;
          };
    const auto binding_matches_ranges
        = [&snapshot](const RegionGroupedFanoutMember& binding,
              const std::span<const RegionFrontierSensitivityRange> ranges) {
              if (binding.sensitivity_range_generation
                      != snapshot.generation
                  || binding.sensitivity_range_count == 0U
                  || binding.sensitivity_range_count != ranges.size()
                  || binding.sensitivity_range_offset
                      > snapshot.grouped_fanout_sensitivity_ranges.size()
                  || binding.sensitivity_range_count
                      > snapshot.grouped_fanout_sensitivity_ranges.size()
                          - binding.sensitivity_range_offset) {
                  return false;
              }
              const auto bound_ranges
                  = std::span<const RegionFrontierSensitivityRange> {
                        snapshot.grouped_fanout_sensitivity_ranges }
                        .subspan(binding.sensitivity_range_offset,
                            binding.sensitivity_range_count);
              return std::equal(ranges.begin(), ranges.end(),
                  bound_ranges.begin(), bound_ranges.end(),
                  [](const auto& left, const auto& right) {
                      return left.offset == right.offset
                          && left.width == right.width;
                  });
          };

    if (all_programs_present && process_access_inventory_complete
        && snapshot.graph.certificate_inventory().access_inventory_complete
        && snapshot.graph.processes().size() == programs.size()) {
        for (std::size_t signal = 0U;
             signal < snapshot.grouped_fanout_by_signal.size(); ++signal) {
            const auto& fanout = snapshot.grouped_fanout_by_signal[signal];
            if (fanout.generation != snapshot.generation
                || fanout.group_count != 1U
                || fanout.group_offset
                    >= snapshot.grouped_fanout_groups.size()) {
                continue;
            }
            const auto& group
                = snapshot.grouped_fanout_groups[fanout.group_offset];
            if (group.generation != snapshot.generation
                || group.member_count == 0U || group.member_count > 64U
                || group.member_offset > snapshot.grouped_fanout_members.size()
                || group.member_count
                    > snapshot.grouped_fanout_members.size()
                        - group.member_offset
                || group.component
                    >= snapshot.programs_by_component.size()
                || !snapshot.programs_by_component[group.component]
                || group.component
                    >= snapshot.authoritative_state_by_component.size()
                || !snapshot.authoritative_state_by_component[group.component]
                || group.component
                    >= snapshot.readiness_mask_by_component.size()) {
                continue;
            }

            const auto& kernel
                = snapshot.programs_by_component[group.component]
                      ->activation_kernel;
            if (std::ranges::find(kernel.internal_signals,
                    static_cast<SignalId>(signal))
                == kernel.internal_signals.end()) {
                continue;
            }
            const auto& mask_descriptor
                = snapshot.readiness_mask_by_component[group.component];
            const auto& authoritative
                = snapshot.authoritative_state_by_component[group.component];
            if (mask_descriptor.generation != snapshot.generation
                || authoritative->generation() != snapshot.generation
                || !authoritative->valid()) {
                continue;
            }

            const auto bindings
                = std::span<const RegionGroupedFanoutMember> {
                      snapshot.grouped_fanout_members }
                      .subspan(group.member_offset, group.member_count);
            const auto reader_offset
                = snapshot.prepared_successor_readers.size();
            bool valid = reader_offset
                <= snapshot.prepared_successor_readers.max_size();
            std::size_t reader_ordinal { };
            std::uint64_t expected_mask { };
            std::vector<RegionFrontierSensitivityRange> kernel_ranges;
            for (const auto& member : kernel.members) {
                if (!collect_kernel_sensitivity_ranges(
                        member.sensitivities, static_cast<SignalId>(signal),
                        kernel.program.static_trigger_regions.empty(),
                        kernel_ranges)) {
                    valid = false;
                    break;
                }
                if (kernel_ranges.empty()) {
                    continue;
                }
                if (reader_ordinal >= 64U
                    || reader_ordinal >= group.member_count) {
                    valid = false;
                    break;
                }
                const auto binding = std::ranges::find(bindings,
                    member.process, &RegionGroupedFanoutMember::process);
                const auto process = member.process;
                if (binding == bindings.end()
                    || binding->static_trigger_mask == 0U
                    || !binding_matches_ranges(*binding, kernel_ranges)
                    || process >= snapshot.component_by_process.size()
                    || process
                        >= snapshot.readiness_member_index_by_process.size()
                    || process
                        >= snapshot.authoritative_member_index_by_process.size()
                    || snapshot.component_by_process[process]
                        != group.component
                    || snapshot.readiness_member_index_by_process[process]
                        != binding->readiness_member
                    || binding->readiness_member
                        >= authoritative->readiness().member_count()
                    || snapshot.authoritative_member_index_by_process[process]
                        >= authoritative->readiness().member_count()
                    || binding->readiness_member / 64U
                        >= mask_descriptor.word_count
                    || mask_descriptor.offset
                        > snapshot.readiness_mask_words.size()
                    || mask_descriptor.word_count
                        > snapshot.readiness_mask_words.size()
                            - std::min(mask_descriptor.offset,
                                snapshot.readiness_mask_words.size())) {
                    valid = false;
                    break;
                }
                const auto readiness_word
                    = binding->readiness_member / 64U;
                const auto queued_mask_word
                    = mask_descriptor.offset + readiness_word;
                if (queued_mask_word
                        >= snapshot.readiness_mask_words.size()
                    || reader_offset
                        > snapshot.prepared_successor_readers.max_size()
                    || reader_ordinal
                        >= snapshot.prepared_successor_readers.max_size()
                            - reader_offset) {
                    valid = false;
                    break;
                }
                const auto readiness_bit
                    = UINT64_C(1)
                    << (binding->readiness_member % 64U);
                const auto authoritative_member
                    = snapshot.authoritative_member_index_by_process[process];
                const auto authoritative_readiness_word
                    = authoritative_member / 64U;
                const auto authoritative_readiness_bit
                    = UINT64_C(1) << (authoritative_member % 64U);
                snapshot.prepared_successor_readers.push_back({
                    process,
                    binding->readiness_member,
                    readiness_word,
                    queued_mask_word,
                    readiness_bit,
                    authoritative_member,
                    authoritative_readiness_word,
                    authoritative_readiness_bit,
                    binding->static_trigger_mask,
                    binding->sensitivity_range_offset,
                    binding->sensitivity_range_count,
                    binding->sensitivity_range_generation,
                });
                expected_mask |= UINT64_C(1) << reader_ordinal;
                ++reader_ordinal;
            }

            if (valid && reader_ordinal == group.member_count) {
                for (const auto& binding : bindings) {
                    const auto member = std::ranges::find(kernel.members,
                        binding.process, &RegionConeKernelMember::process);
                    if (member == kernel.members.end()
                        || !collect_kernel_sensitivity_ranges(
                            member->sensitivities,
                            static_cast<SignalId>(signal),
                            kernel.program.static_trigger_regions.empty(),
                            kernel_ranges)
                        || !binding_matches_ranges(binding, kernel_ranges)
                        || kernel_ranges.empty()) {
                        valid = false;
                        break;
                    }
                }
            } else {
                valid = false;
            }
            if (!valid) {
                snapshot.prepared_successor_readers.resize(reader_offset);
                continue;
            }

            auto& successor = snapshot.prepared_successor_by_signal[signal];
            successor.reader_offset = reader_offset;
            successor.reader_count = reader_ordinal;
            successor.component = group.component;
            successor.generation = snapshot.generation;
            successor.expected_mask = expected_mask;
        }
    }

    auto* const frontier_provider = region_kernel_backend_provider
        ? dynamic_cast<RegionFrontierBackendProvider*>(
              region_kernel_backend_provider.get())
        : nullptr;
    const bool frontier_preparation_enabled
        = frontier_provider != nullptr
        && (systemverilog_local_wave_enabled || enable_region_kernel)
        && !process_profile_enabled && !execution_point_hook
        && !scheduler.trace_hook_installed() && !driver_change_hook
        && !signal_change_hook && !stored_signal_change_hook
        && !scalar_signal_change_hook && !container_object_change_hook
        && !container_element_change_hook
        && !native_signal_observation_any_hook
        && !native_signal_observation_required_hook;
    const auto report_frontier_preparation_decline
        = [&](const std::size_t component,
              const RegionConeActivationKernel& kernel,
              const char* reason) {
              ++frontier_preparation_declines;
              if (!profile_frontier_preparation
                  || frontier_preparation_diagnostic_rows >= 16U) {
                  return;
              }
              std::size_t full_width_internal_any_sensitivities { };
              for (const auto& member : kernel.members) {
                  for (const auto& sensitivity : member.sensitivities) {
                      if (sensitivity.width != 0U
                          && is_whole_signal_any_sensitivity(
                              sensitivity, true)
                          && std::ranges::find(kernel.internal_signals,
                                 sensitivity.signal)
                              != kernel.internal_signals.end()) {
                          ++full_width_internal_any_sensitivities;
                      }
                  }
              }
              ++frontier_preparation_diagnostic_rows;
              std::fprintf(stderr,
                  "[fsim frontier-prepare-decline] reason=%s component=%zu "
                  "domain=%u members=%zu outputs=%zu internals=%zu "
                  "full_width_internal_any_sensitivities=%zu\n",
                  reason != nullptr ? reason : "unknown",
                  component,
                  static_cast<unsigned>(kernel.program.scheduling_domain),
                  kernel.members.size(), kernel.outputs.size(),
                  kernel.internal_signals.size(),
                  full_width_internal_any_sensitivities);
          };
    if (frontier_preparation_enabled) {
        struct FrontierPreparationWeight final {
            std::size_t components { };
            std::size_t members { };
            std::size_t operations { };

            void add(const RegionConeActivationKernel& kernel) noexcept
            {
                ++components;
                members += kernel.members.size();
                operations += kernel.program.operations.size();
            }
        };
        struct FrontierPreparationCandidate final {
            enum class Kind : std::uint8_t {
                pool_hit,
                prepared,
                legacy
            };

            std::size_t component { };
            Kind kind { Kind::legacy };
            std::shared_ptr<RegionFrontierBackendEntry> pool_entry;
            std::unique_ptr<RegionFrontierPreparedBackend> prepared;
            std::optional<std::string> census_identity;
        };

        struct FrontierPreparationCensus final {
            FrontierPreparationWeight eligible;
            FrontierPreparationWeight pool_hits;
            FrontierPreparationWeight planner_declines;
            FrontierPreparationWeight preflight_declines;
            FrontierPreparationWeight staged_passes;
            FrontierPreparationWeight legacy_candidates;
            std::size_t identity_available { };
            std::size_t identity_unavailable { };
            std::size_t duplicate_identity_groups { };
            FrontierPreparationWeight duplicate_identity_candidates;
            FrontierPreparationWeight largest_duplicate_group;
            std::size_t prepared_materialize_attempts { };
            std::size_t legacy_create_attempts { };
            std::size_t nonnull_backend_returns { };
            std::size_t same_pass_pool_reuses { };
            std::size_t final_accepts { };
            std::size_t final_declines { };
        };

        snapshot.frontier_runtime_by_component.resize(components.size());
        auto* const preparing_provider
            = dynamic_cast<RegionFrontierPreparingProvider*>(
                region_kernel_backend_provider.get());
        std::vector<FrontierPreparationCandidate> candidates;
        candidates.reserve(components.size());
        FrontierPreparationCensus census;
        const auto find_matching_backend
            = [&](const RegionConeActivationKernel& kernel) {
                  const auto found = std::ranges::find_if(
                      snapshot.frontier_backend_pool,
                      [&](const auto& entry) {
                          return entry
                              && entry->provider_identity
                                  == region_kernel_backend_provider_identity
                              && same_region_kernel_mapping(
                                  entry->kernel, kernel);
                      });
                  return found == snapshot.frontier_backend_pool.end()
                      ? std::shared_ptr<RegionFrontierBackendEntry> { }
                      : *found;
              };
        for (std::size_t component = 0U;
             component < components.size(); ++component) {
            if (!snapshot.programs_by_component[component]
                || !snapshot.graph.component_epochs_current(component)) {
                continue;
            }
            const auto& kernel
                = snapshot.programs_by_component[component]->activation_kernel;
            const bool generic_mode = kernel.program.scheduling_domain
                == ProcessSchedulingDomain::generic;
            const bool systemverilog_mode = kernel.program.scheduling_domain
                == ProcessSchedulingDomain::systemverilog;
            if ((!generic_mode && !systemverilog_mode)
                || (generic_mode && !enable_region_kernel)
                || (systemverilog_mode
                    && (!systemverilog_local_wave_enabled
                        || !snapshot.authoritative_state_by_component[component]
                        || !snapshot.local_wave_state_by_component[component]))
                || kernel.members.empty() || kernel.outputs.empty()) {
                continue;
            }
            const auto& graph_processes = snapshot.graph.processes();
            const auto& graph_signals = snapshot.graph.signals();
            const bool members_are_supported = std::ranges::all_of(
                kernel.members, [&](const RegionConeKernelMember& member) {
                    if (member.process >= graph_processes.size()
                        || member.process >= processes.size()) {
                        return false;
                    }
                    const auto& graph_member = graph_processes[member.process];
                    const auto view = processes.program_view(member.process);
                    const auto expected_domain = generic_mode
                        ? ProcessSchedulingDomain::generic
                        : ProcessSchedulingDomain::systemverilog;
                    const auto expected_update = generic_mode
                        ? RegionUpdateKind::generic
                        : RegionUpdateKind::systemverilog_active;
                    return graph_member.pure
                        && !graph_member.dependencies_unknown
                        && graph_member.scheduling_domain == expected_domain
                        && graph_member.update_kind == expected_update
                        && view.scheduling_domain() == expected_domain
                        && !processes.is_compact_constant(member.process)
                        && process_signal_access_is_complete(member.process);
                });
            const bool outputs_are_supported = std::ranges::all_of(
                kernel.outputs, [&](const RegionConeOutputBinding& output) {
                    const auto expected_domain = generic_mode
                        ? SignalUpdateDomain::generic
                        : SignalUpdateDomain::systemverilog_active;
                    const auto expected_update = generic_mode
                        ? RegionUpdateKind::generic
                        : RegionUpdateKind::systemverilog_active;
                    if (output.signal >= graph_signals.size()
                        || output.owner >= processes.size()
                        || output.width == 0U
                        || output.publication_kind
                            != RegionOutputPublicationKind::update
                        || (generic_mode
                            && output.value_kind != ValueKind::logic4
                            && output.value_kind != ValueKind::logic9)
                        || output.domain != expected_domain
                        || output.update_kind != expected_update
                        || output.projected_delay != SimulationTick { }
                        || output.projected_rejection != SimulationTick { }
                        || output.projected_mode
                            != ProjectedDelayMode::inertial) {
                        return false;
                    }

                    const auto signal_width
                        = graph_signals[output.signal].descriptor.width;
                    const auto bound_signal_width = output.signal_width == 0U
                        ? output.width : output.signal_width;
                    if (bound_signal_width != signal_width
                        || output.offset > signal_width
                        || output.width > signal_width - output.offset) {
                        return false;
                    }

                    const bool internal_output
                        = std::ranges::find(kernel.internal_signals,
                               output.signal)
                        != kernel.internal_signals.end();
                    if (internal_output) {
                        return output.offset == 0U
                            && output.width == signal_width;
                    }

                    const bool full_signal_output
                        = output.offset == 0U
                        && output.width == signal_width;
                    if (full_signal_output) {
                        return true;
                    }
                    if (!systemverilog_mode
                        || output.signal_width == 0U) {
                        return false;
                    }

                    const auto owner_program
                        = processes.program_view(output.owner);
                    const auto& operations = owner_program.operations();
                    if (output.source_instruction >= operations.size()) {
                        return false;
                    }
                    const auto source_operation
                        = operations.expanded(output.source_instruction);
                    const auto* const slice
                        = operation_get_if<WriteUpdateSlice>(
                            &source_operation);
                    if (slice == nullptr
                        || slice->signal != output.signal
                        || slice->offset != output.offset
                        || slice->domain
                            != SignalUpdateDomain::systemverilog_active) {
                        return false;
                    }

                    bool matching_driver_region { };
                    for (const auto& region : owner_program.driver_regions()) {
                        if (region.signal == output.signal && !region.whole
                            && region.offset == output.offset
                            && region.width == output.width) {
                            matching_driver_region = true;
                        }
                    }
                    return matching_driver_region;
                });
            if (!members_are_supported || !outputs_are_supported) {
                continue;
            }

            census.eligible.add(kernel);
            auto entry = find_matching_backend(kernel);
            if (entry) {
                census.pool_hits.add(kernel);
                FrontierPreparationCandidate candidate;
                candidate.component = component;
                candidate.kind = FrontierPreparationCandidate::Kind::pool_hit;
                candidate.pool_entry = std::move(entry);
                candidates.push_back(std::move(candidate));
                continue;
            }
            if (!may_compile_backends) {
                continue;
            }
            if (preparing_provider == nullptr) {
                census.legacy_candidates.add(kernel);
                FrontierPreparationCandidate candidate;
                candidate.component = component;
                candidate.kind = FrontierPreparationCandidate::Kind::legacy;
                candidates.push_back(std::move(candidate));
                continue;
            }

            std::unique_ptr<RegionFrontierPreparedBackend> prepared;
            try {
                prepared = preparing_provider->prepare_frontier(kernel);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                census.planner_declines.add(kernel);
                report_frontier_preparation_decline(component, kernel,
                    "planner-exception");
                continue;
            }
            if (!prepared) {
                census.planner_declines.add(kernel);
                report_frontier_preparation_decline(component, kernel,
                    "planner-declined");
                continue;
            }

            const char* preflight_reason = nullptr;
            bool preflight_passed { };
            try {
                preflight_passed = preflight_region_frontier_component_layout(
                    snapshot, component, kernel, prepared->layout(),
                    profile_frontier_preparation ? &preflight_reason : nullptr);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (...) {
                preflight_reason = "preflight-exception";
            }
            if (!preflight_passed) {
                census.preflight_declines.add(kernel);
                report_frontier_preparation_decline(component, kernel,
                    preflight_reason != nullptr
                        ? preflight_reason : "layout-preflight-rejected");
                continue;
            }

            FrontierPreparationCandidate candidate;
            candidate.component = component;
            candidate.kind = FrontierPreparationCandidate::Kind::prepared;
            if (profile_frontier_preparation) {
                const auto identity = prepared->structural_census_identity();
                if (identity) {
                    candidate.census_identity = std::string { *identity };
                    ++census.identity_available;
                } else {
                    ++census.identity_unavailable;
                }
            }
            candidate.prepared = std::move(prepared);
            census.staged_passes.add(kernel);
            candidates.push_back(std::move(candidate));
        }

        if (profile_frontier_preparation) {
            struct IdentityGroup final {
                std::size_t candidates { };
                FrontierPreparationWeight weight;
            };
            std::map<std::string_view, IdentityGroup, std::less<>> groups;
            for (const auto& candidate : candidates) {
                if (!candidate.census_identity) {
                    continue;
                }
                const auto& kernel = snapshot.programs_by_component[
                    candidate.component]->activation_kernel;
                auto& group = groups[*candidate.census_identity];
                ++group.candidates;
                group.weight.add(kernel);
            }
            for (const auto& [identity, group] : groups) {
                static_cast<void>(identity);
                if (group.candidates > 1U) {
                    ++census.duplicate_identity_groups;
                    census.duplicate_identity_candidates.components
                        += group.weight.components;
                    census.duplicate_identity_candidates.members
                        += group.weight.members;
                    census.duplicate_identity_candidates.operations
                        += group.weight.operations;
                    if (group.weight.operations
                            > census.largest_duplicate_group.operations
                        || (group.weight.operations
                                == census.largest_duplicate_group.operations
                            && group.weight.members
                                > census.largest_duplicate_group.members)
                        || (group.weight.operations
                                == census.largest_duplicate_group.operations
                            && group.weight.members
                                == census.largest_duplicate_group.members
                            && group.weight.components
                                > census.largest_duplicate_group.components)) {
                        census.largest_duplicate_group = group.weight;
                    }
                }
            }
            const auto print_weight = [](const char* name,
                                          const FrontierPreparationWeight& weight) {
                std::fprintf(stderr, " %s=%zu/%zu/%zu", name,
                    weight.components, weight.members, weight.operations);
            };
            std::fprintf(stderr, "[fsim frontier-plan-census]");
            print_weight("eligible", census.eligible);
            print_weight("pool_hits", census.pool_hits);
            print_weight("planner_declines", census.planner_declines);
            print_weight("preflight_declines", census.preflight_declines);
            print_weight("staged_passes", census.staged_passes);
            print_weight("legacy_candidates", census.legacy_candidates);
            print_weight("duplicate_identity_candidates",
                census.duplicate_identity_candidates);
            print_weight("largest_duplicate_group",
                census.largest_duplicate_group);
            std::fprintf(stderr,
                " identities=%zu missing_identities=%zu duplicate_groups=%zu\n",
                census.identity_available, census.identity_unavailable,
                census.duplicate_identity_groups);
        }

        auto& backend_pool = snapshot.frontier_backend_pool;
        const auto new_backend_candidates = static_cast<std::size_t>(
            std::ranges::count_if(candidates, [](const auto& candidate) {
                return candidate.kind
                    != FrontierPreparationCandidate::Kind::pool_hit;
            }));
        if (new_backend_candidates > backend_pool.max_size()
                - backend_pool.size()) {
            throw std::length_error {
                "frontier backend pool candidate count exceeds capacity"
            };
        }
        backend_pool.reserve(backend_pool.size() + new_backend_candidates);
        for (auto& candidate : candidates) {
            const auto component = candidate.component;
            auto& kernel
                = snapshot.programs_by_component[component]->activation_kernel;
            std::shared_ptr<RegionFrontierBackendEntry> entry
                = std::move(candidate.pool_entry);
            bool newly_materialized { };
            if (!entry && candidate.kind != FrontierPreparationCandidate::Kind::pool_hit) {
                entry = find_matching_backend(kernel);
                if (entry) {
                    ++census.same_pass_pool_reuses;
                    candidate.prepared.reset();
                } else {
                    std::unique_ptr<RegionFrontierBackend> backend;
                    try {
                        if (candidate.kind
                            == FrontierPreparationCandidate::Kind::prepared) {
                            ++census.prepared_materialize_attempts;
                            backend = std::move(*candidate.prepared).materialize();
                        } else {
                            ++census.legacy_create_attempts;
                            backend = frontier_provider->create_frontier(kernel);
                        }
                    } catch (const std::bad_alloc&) {
                        throw;
                    } catch (...) {
                        report_frontier_preparation_decline(component, kernel,
                            "backend-materialization-exception");
                        ++census.final_declines;
                        continue;
                    }
                    if (!backend) {
                        report_frontier_preparation_decline(component, kernel,
                            "backend-materialization-declined");
                        ++census.final_declines;
                        continue;
                    }
                    ++census.nonnull_backend_returns;
                    entry = std::make_shared<RegionFrontierBackendEntry>();
                    entry->kernel = kernel;
                    entry->provider_identity
                        = region_kernel_backend_provider_identity;
                    entry->executor = std::move(backend);
                    newly_materialized = true;
                }
            }
            if (!entry || !entry->executor) {
                continue;
            }
            try {
                auto runtime
                    = std::make_shared<RegionFrontierComponentRuntime>();
                runtime->owner = this;
                runtime->component = component;
                runtime->runtime_generation = snapshot.generation;
                runtime->backend = entry;
                const bool capture_decline_reason
                    = profile_frontier_preparation
                    && frontier_preparation_diagnostic_rows < 16U;
                const char* decline_reason = nullptr;
                if (!prepare_region_frontier_component(snapshot, component,
                        kernel, entry, *runtime,
                        capture_decline_reason ? &decline_reason : nullptr)) {
                    report_frontier_preparation_decline(component, kernel,
                        decline_reason);
                    ++census.final_declines;
                    continue;
                }
                if (!newly_materialized) {
                    kernel.program.operations
                        = entry->kernel.program.operations;
                }
                if (newly_materialized) {
                    snapshot.frontier_backend_pool.push_back(entry);
                }
                ++census.final_accepts;
                if (component
                        < snapshot.local_wave_state_by_component.size()) {
                    const auto& local_wave_state
                        = snapshot.local_wave_state_by_component[component];
                    if (local_wave_state
                        && local_wave_state->generation == snapshot.generation
                        && local_wave_state->activation_kernel_identity
                            .matches(kernel)
                        && same_region_kernel_mapping(entry->kernel, kernel)) {
                        local_wave_state->frontier_kernel_identity.bind(
                            entry->kernel);
                    }
                }
                snapshot.frontier_backends_by_component[component] = entry;
                snapshot.frontier_runtime_by_component[component]
                    = std::move(runtime);
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::invalid_argument&) {
                report_frontier_preparation_decline(component, kernel,
                    "invalid-argument-exception");
                ++census.final_declines;
                continue;
            }
        }
        if (profile_frontier_preparation) {
            std::fprintf(stderr,
                "[fsim frontier-plan-result] prepared_materialize_attempts=%zu "
                "legacy_create_attempts=%zu nonnull_backend_returns=%zu "
                "same_pass_pool_reuses=%zu "
                "final_accepts=%zu final_declines=%zu\n",
                census.prepared_materialize_attempts,
                census.legacy_create_attempts, census.nonnull_backend_returns,
                census.same_pass_pool_reuses, census.final_accepts,
                census.final_declines);
        }
    }
    if (profile_frontier_preparation) {
        std::fprintf(stderr,
            "[fsim frontier-prepare-summary] declines=%zu sampled=%zu\n",
            frontier_preparation_declines,
            frontier_preparation_diagnostic_rows);
        std::fprintf(stderr,
            "fsim-profile: sv-region-recertification "
            "event=authoritative-seed-summary runtime_generation=%llu "
            "failures=%zu components=%zu authoritative_incomplete=%u\n",
            static_cast<unsigned long long>(snapshot.generation),
            authoritative_seed_failures, components.size(),
            static_cast<unsigned>(snapshot.authoritative_state_incomplete));
    }
    // Build the legacy activation backend only after the optional frontier
    // runtime has been prepared. Generic components selected for V2 own their
    // generated executor and detached workspace already, so constructing a
    // second V1 executor and native workspace would duplicate the route.
    for (std::size_t component = 0U;
         component < components.size(); ++component) {
        if (!snapshot.programs_by_component[component]) {
            continue;
        }
        auto& kernel
            = snapshot.programs_by_component[component]->activation_kernel;
        const auto* const frontier_runtime
            = component < snapshot.frontier_runtime_by_component.size()
                ? snapshot.frontier_runtime_by_component[component].get()
                : nullptr;
        const bool generic_frontier_ready
            = frontier_runtime != nullptr
            && frontier_runtime->execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update
            && frontier_runtime->backend
            && frontier_runtime->backend->executor;
        if (generic_frontier_ready) {
            continue;
        }
        const auto matching_backend = std::ranges::find_if(
            snapshot.backend_pool,
            [&](const auto& entry) {
                return entry
                    && entry->provider_identity
                        == region_kernel_backend_provider_identity
                    && same_region_kernel_mapping(entry->kernel, kernel);
            });
        if (matching_backend != snapshot.backend_pool.end()) {
            kernel.program.operations
                = (*matching_backend)->kernel.program.operations;
            snapshot.backends_by_component[component] = *matching_backend;
            snapshot.backend_generation_by_component[component]
                = snapshot.generation;
            continue;
        }
        if (!may_compile_backends || !region_kernel_backend_provider) {
            continue;
        }
        std::unique_ptr<RegionKernelBackend> backend;
        try {
            backend = region_kernel_backend_provider->create(kernel);
        } catch (const std::bad_alloc&) {
            report_optional_region_backend_event(
                std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr,
                component, kernel,
                "exception", "std::bad_alloc propagated");
            throw;
        } catch (const std::exception& error) {
            report_optional_region_backend_event(
                std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr,
                component, kernel,
                "exception", error.what());
            // Optional native construction must not prevent the interpreter
            // from publishing a valid checked-C++ activation program. Keep
            // this catch limited to the provider boundary so graph and
            // certificate failures still abort snapshot preparation.
            continue;
        } catch (...) {
            report_optional_region_backend_event(
                std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr,
                component, kernel,
                "exception", "non-standard exception swallowed");
            continue;
        }
        if (!backend) {
            report_optional_region_backend_event(
                std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr,
                component, kernel,
                "null-return", "provider declined optional backend");
            continue;
        }
        auto entry = std::make_shared<RegionKernelBackendEntry>();
        entry->kernel = kernel;
        entry->provider_identity = region_kernel_backend_provider_identity;
        entry->executor = std::move(backend);
        entry->native_workspace
            = std::make_unique<RegionKernelNativeWorkspace>(entry->kernel);
        snapshot.backend_pool.push_back(entry);
        snapshot.backends_by_component[component] = std::move(entry);
        snapshot.backend_generation_by_component[component]
            = snapshot.generation;
    }

    // Build one stable ordered-ticket task for each bounded homogeneous
    // VHDL projected component. The task owns receipt sidecars for its
    // original Generic Active keys and delegates execution to the existing
    // V1 projected-write path; it does not replace Update transactions.
    for (std::size_t component = 0U;
         component < snapshot.programs_by_component.size(); ++component) {
        if (!snapshot.programs_by_component[component]
            || !snapshot.graph.component_epochs_current(component)
            || component >= snapshot.backends_by_component.size()
            || component >= snapshot.backend_generation_by_component.size()) {
            continue;
        }
        const auto backend = snapshot.backends_by_component[component];
        if (!backend || !backend->executor
            || snapshot.backend_generation_by_component[component]
                != snapshot.generation
            || (component < snapshot.frontier_runtime_by_component.size()
                && snapshot.frontier_runtime_by_component[component]
                && snapshot.frontier_runtime_by_component[component]
                        ->execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update)) {
            continue;
        }
        const auto& kernel
            = snapshot.programs_by_component[component]->activation_kernel;
        if (kernel.program.scheduling_domain
                != ProcessSchedulingDomain::generic
            || kernel.members.size() < 2U || kernel.members.size() > 64U
            || kernel.outputs.empty()
            || std::ranges::any_of(kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.publication_kind
                            != RegionOutputPublicationKind::update
                        || output.domain != SignalUpdateDomain::generic
                        || output.update_kind
                            != RegionUpdateKind::vhdl_projected;
                })) {
            continue;
        }
        bool members_are_supported = true;
        for (std::size_t index = 0U;
             members_are_supported && index < kernel.members.size(); ++index) {
            const auto process = kernel.members[index].process;
            if (process >= snapshot.graph.processes().size()
                || process >= processes.size()
                || process >= snapshot.component_by_process.size()
                || snapshot.component_by_process[process] != component
                || processes.is_compact_constant(process)) {
                members_are_supported = false;
                break;
            }
            const auto& node = snapshot.graph.processes()[process];
            members_are_supported
                = node.pure && !node.dependencies_unknown
                && node.scheduling_domain == ProcessSchedulingDomain::generic
                && node.update_kind == RegionUpdateKind::vhdl_projected
                && processes.program_view(process).scheduling_domain()
                    == ProcessSchedulingDomain::generic
                && std::ranges::none_of(kernel.members.begin(),
                    kernel.members.begin()
                        + static_cast<std::ptrdiff_t>(index),
                    [process](const RegionConeKernelMember& earlier) {
                        return earlier.process == process;
                    });
            if (!members_are_supported) {
                break;
            }
            const auto cohort = process
                    < static_sensitivity_cohort_by_process.size()
                ? static_sensitivity_cohort_by_process[process]
                : std::numeric_limits<std::size_t>::max();
            if (cohort != std::numeric_limits<std::size_t>::max()
                && (cohort >= static_sensitivity_cohorts.size()
                    || static_sensitivity_cohorts[cohort].members.size() > 1U)) {
                members_are_supported = false;
            }
        }
        if (!members_are_supported
            || component >= snapshot.graph.certificate_inventory()
                    .components.size()
            || snapshot.graph.certificate_inventory()
                    .components[component].status
                != RegionComponentCertificateStatus::structural_candidate) {
            continue;
        }

        auto readiness = std::make_shared<VhdlProjectedReadinessTask>();
        readiness->owner = this;
        readiness->component = component;
        readiness->runtime_generation = snapshot.generation;
        readiness->backend = backend;
        readiness->members.reserve(kernel.members.size());
        readiness->ticket_member_offsets.resize(kernel.members.size(),
            std::numeric_limits<std::size_t>::max());
        for (const auto& member : kernel.members) {
            readiness->members.push_back({ member.process, { }, 0U });
        }
        snapshot.vhdl_projected_readiness_by_component[component]
            = std::move(readiness);
    }

    // Quiet-point recertification may carry a V1 entry for a mapping that is
    // now served exclusively by Generic V2. Drop only matching pool entries
    // that no retained component binds; entries still used by a V1 component
    // remain available to that route.
    for (std::size_t component = 0U;
         component < snapshot.frontier_runtime_by_component.size();
         ++component) {
        const auto& frontier_runtime
            = snapshot.frontier_runtime_by_component[component];
        if (!frontier_runtime
            || frontier_runtime->execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update
            || !frontier_runtime->backend) {
            continue;
        }
        const auto& frontier_kernel = frontier_runtime->backend->kernel;
        snapshot.backend_pool.erase(
            std::remove_if(snapshot.backend_pool.begin(),
                snapshot.backend_pool.end(), [&](const auto& entry) {
                    if (!entry
                        || entry->provider_identity
                            != region_kernel_backend_provider_identity
                        || !same_region_kernel_mapping(
                            entry->kernel, frontier_kernel)) {
                        return false;
                    }
                    return std::ranges::none_of(
                        snapshot.backends_by_component,
                        [&](const auto& bound) { return bound == entry; });
                }),
            snapshot.backend_pool.end());
    }

    // A fixed topology can have more simultaneously ready components than
    // the scheduler's standalone 64-ticket pool. Size that pool from the
    // actual certified grouped-fanout components, with room for two queued
    // epochs. This is optional admission storage: if counting or allocation
    // cannot complete, ordinary per-member scheduling remains available.
    try {
        std::vector<std::uint8_t> component_has_ticket(
            snapshot.programs_by_component.size(), 0U);
        std::size_t distinct_components { };
        for (const auto& group : snapshot.grouped_fanout_groups) {
            if (group.member_count == 0U
                || group.component >= component_has_ticket.size()
                || !snapshot.programs_by_component[group.component]
                || component_has_ticket[group.component] != 0U) {
                continue;
            }
            component_has_ticket[group.component] = 1U;
            ++distinct_components;
        }
        if (distinct_components
            <= std::numeric_limits<std::size_t>::max() / 2U) {
            const auto required_capacity = distinct_components * 2U;
            if (required_capacity != 0U) {
                try {
                    static_cast<void>(scheduler
                        .prepare_systemverilog_readiness_ticket_capacity(
                            required_capacity));
                } catch (const std::bad_alloc&) {
                    // Pool growth is optional; preserve checked scheduling.
                } catch (const std::length_error&) {
                    // An unrepresentable fixed topology stays on fallback.
                }
            }
        }
    } catch (const std::bad_alloc&) {
        // Optional sizing scratch must not make snapshot preparation fail.
    } catch (const std::length_error&) {
        // Optional sizing scratch must not make snapshot preparation fail.
    }

    // Generic V2 components have a separate scheduler pool from SV
    // readiness tickets. Prepare one physical ticket per certified Generic
    // component; each ticket retains the exact component-sized key suffix.
    std::size_t generic_ticket_components { };
    for (std::size_t component = 0U;
         component < snapshot.frontier_runtime_by_component.size();
         ++component) {
        const auto* const runtime
            = snapshot.frontier_runtime_by_component[component].get();
        if (component >= snapshot.programs_by_component.size()
            || !snapshot.programs_by_component[component] || runtime == nullptr
            || runtime->owner != this || runtime->component != component
            || runtime->invalidated || !runtime->frame_initialized
            || runtime->runtime_generation != snapshot.generation
            || runtime->execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update
            || !runtime->backend || !runtime->backend->executor
            || runtime->members.empty()
            || runtime->members.size()
                != runtime->backend->executor->layout().member_count
            || runtime->generic_queued_members.size()
                != runtime->members.size()
            || runtime->generic_queued_ready_words.size()
                != (runtime->members.size() + 63U) / 64U) {
            continue;
        }
        ++generic_ticket_components;
    }
    for (const auto& readiness
         : snapshot.vhdl_projected_readiness_by_component) {
        if (readiness && readiness->owner == this
            && readiness->runtime_generation == snapshot.generation
            && readiness->members.size() > 1U
            && readiness->members.size() <= 64U
            && readiness->backend && readiness->backend->executor) {
            ++generic_ticket_components;
        }
    }
    if (generic_ticket_components
        <= std::numeric_limits<std::size_t>::max() / 2U) {
        const auto required_capacity = generic_ticket_components * 2U;
        if (required_capacity != 0U) {
            try {
                static_cast<void>(scheduler
                    .prepare_generic_readiness_ticket_capacity(
                        required_capacity));
            } catch (const std::bad_alloc&) {
                // Generic physical-ticket growth is optional; preserve the
                // ordinary per-member route when storage cannot be prepared.
            } catch (const std::length_error&) {
                // An unrepresentable fixed topology stays on fallback.
            }
        }
    }

    return snapshot;
}

void Interpreter::Impl::publish_region_runtime_snapshot(
    RegionRuntimeSnapshot&& snapshot) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<RegionGraph>);
    static_assert(std::is_nothrow_swappable_v<std::optional<RegionGraph>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<std::optional<RegionConeProgram>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<std::size_t>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<std::shared_ptr<RegionKernelBackendEntry>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<
        std::shared_ptr<RegionConeForwardingBackendEntry>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<
        std::shared_ptr<RegionFrontierBackendEntry>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<
        std::shared_ptr<RegionFrontierComponentRuntime>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<
        std::shared_ptr<VhdlProjectedReadinessTask>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<std::uint64_t>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<std::shared_ptr<RegionAuthoritativeComponentState>>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<std::shared_ptr<RegionLocalWaveComponentState>>>);
    static_assert(std::is_nothrow_swappable_v<std::vector<std::uint8_t>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<RegionReadinessMaskDescriptor>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<RegionReadinessQueueMember>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<RegionPreparedSuccessorSignalMap>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<RegionPreparedSuccessorReaderBinding>>);
    static_assert(std::is_nothrow_swappable_v<
        std::vector<RegionFrontierSensitivityRange>>);

    // Bound slots remain stable objects in the interpreter's signal and
    // DriverTable storage. Drop every old view before replacing its plane
    // owner, then bind the prepared new views after the no-throw snapshot
    // swap. Both operations copy a single Logic4 word and cannot allocate.
    for (std::size_t component = 0U;
         component < region_authoritative_state_by_component.size();
         ++component) {
        const auto& state
            = region_authoritative_state_by_component[component];
        if (state && state->values().packed_slots_bound()) {
            demote_region_authoritative_slots(component, false);
            if (snapshot.profile_systemverilog_waves) {
                ++systemverilog_wave_profile_a4_slot_rebinds;
            }
        }
    }

    if (snapshot.profile_systemverilog_waves) {
        for (const auto& state : snapshot.authoritative_state_by_component) {
            if (!state) {
                continue;
            }
            ++systemverilog_wave_profile_a4_seeded_components;
            systemverilog_wave_profile_a4_seeded_signals
                += state->values().layout().signal_count();
            systemverilog_wave_profile_a4_seeded_owners
                += state->values().layout().owner_count();
        }
    }

    std::optional<RegionGraph> replacement {
        std::move(snapshot.graph) };
    region_graph.swap(replacement);
    region_activation_programs.swap(snapshot.programs_by_component);
    region_component_by_process.swap(snapshot.component_by_process);
    region_kernel_backends_by_component.swap(
        snapshot.backends_by_component);
    region_kernel_backend_generation_by_component.swap(
        snapshot.backend_generation_by_component);
    region_kernel_backend_pool.swap(snapshot.backend_pool);
    region_cone_forwarding_backends_by_component.swap(
        snapshot.forwarding_backends_by_component);
    region_cone_forwarding_backend_pool.swap(
        snapshot.forwarding_backend_pool);
    region_frontier_backends_by_component.swap(
        snapshot.frontier_backends_by_component);
    region_frontier_backend_pool.swap(snapshot.frontier_backend_pool);
    region_frontier_runtime_by_component.swap(
        snapshot.frontier_runtime_by_component);
    vhdl_projected_readiness_by_component.swap(
        snapshot.vhdl_projected_readiness_by_component);
    region_authoritative_state_by_component.swap(
        snapshot.authoritative_state_by_component);
    region_local_wave_state_by_component.swap(
        snapshot.local_wave_state_by_component);
    region_value_only_recertification_by_component.swap(
        snapshot.value_only_recertification_by_component);
    // Every prepared snapshot starts with an all-zero candidate vector.
    region_value_only_recertification_nonempty_components = 0U;
    region_authoritative_component_by_signal.swap(
        snapshot.authoritative_component_by_signal);
    region_authoritative_member_index_by_process.swap(
        snapshot.authoritative_member_index_by_process);
    region_readiness_mask_words.swap(snapshot.readiness_mask_words);
    region_readiness_mask_by_component.swap(
        snapshot.readiness_mask_by_component);
    region_readiness_member_index_by_process.swap(
        snapshot.readiness_member_index_by_process);
    region_readiness_queued_by_process.swap(
        snapshot.readiness_queued_by_process);
    region_grouped_fanout_by_signal.swap(
        snapshot.grouped_fanout_by_signal);
    region_grouped_fanout_groups.swap(snapshot.grouped_fanout_groups);
    region_grouped_fanout_members.swap(snapshot.grouped_fanout_members);
    region_grouped_fanout_sensitivity_ranges.swap(
        snapshot.grouped_fanout_sensitivity_ranges);
    region_grouped_readiness_member_scratch.swap(
        snapshot.grouped_readiness_member_scratch);
    region_grouped_readiness_receipt_scratch.swap(
        snapshot.grouped_readiness_receipt_scratch);
    region_grouped_readiness_queue_scratch.swap(
        snapshot.grouped_readiness_queue_scratch);
    region_grouped_readiness_member_changed_scratch.swap(
        snapshot.grouped_readiness_member_changed_scratch);
    region_grouped_readiness_process_scratch.swap(
        snapshot.grouped_readiness_process_scratch);
    region_prepared_successor_by_signal.swap(
        snapshot.prepared_successor_by_signal);
    region_prepared_successor_readers.swap(
        snapshot.prepared_successor_readers);
    region_grouped_fanout_oversized_group_fallbacks
        = snapshot.grouped_fanout_oversized_group_fallbacks;
    region_authoritative_recertification_waiting
        = snapshot.authoritative_state_incomplete;
    region_runtime_generation = snapshot.generation;
    completed_callback_observation_generation = 0U;
    region_recertification_requires_snapshot = false;
    process_signal_access_inventory_complete
        = snapshot.process_signal_access_inventory_complete;
    region_signal_driver_inventory
        = std::move(snapshot.signal_driver_inventory);
    for (const auto& state : region_authoritative_state_by_component) {
        if (!state) {
            continue;
        }
        const auto bound_slots = state->values().bind_packed_slots();
        if (bound_slots != 0U && snapshot.profile_systemverilog_waves) {
            ++systemverilog_wave_profile_a4_slot_bind_components;
            systemverilog_wave_profile_a4_slot_bindings += bound_slots;
            systemverilog_wave_profile_a4_bound_components++;
            systemverilog_wave_profile_a4_bound_slots += bound_slots;
        }
    }
}

Interpreter::Impl::RegionRuntimeSnapshot
Interpreter::Impl::build_region_runtime_snapshot(
    const bool profile_systemverilog_waves,
    const bool enable_region_kernel,
    const bool may_compile_backends)
{
    // A replacement snapshot can unbind the exact A4 roles used by a private
    // forwarding epoch. Publish every applied role journal before capturing a
    // replacement graph or swapping authoritative state.
    require_all_region_forwarding_role_journals_flushed();

    std::vector<Process> program_snapshots;
    program_snapshots.reserve(processes.size());
    std::vector<const Process*> programs;
    programs.reserve(processes.size());
    // Dormant kernel members share one stub body.
    std::optional<OperationList> dormant_stub_operations;
    std::vector<std::uint8_t> process_access_complete;
    process_access_complete.reserve(processes.size());
    bool access_inventory_complete = true;
    std::size_t incomplete_access_count = 0U;
    std::size_t incomplete_access_rows = 0U;
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        const auto program = processes.program_view(id);
        // RegionGraph is an immutable startup/quiet-point snapshot API that
        // still consumes the public Process shape. Keep these facades
        // temporary; runtime process state remains compact and materializes a
        // retained facade only when the public accessor is called.
        if (static_kernel && id < fusion_dormant_process.size()
            && fusion_dormant_process[id] != 0U) {
            // A static kernel member never runs on the scheduler; its effects
            // reach the host only through the kernel host process. A stub
            // keeps process identities dense without materializing it.
            if (!dormant_stub_operations) {
                dormant_stub_operations.emplace(
                    std::vector<Operation> { WaitForever { } });
            }
            auto& stub = program_snapshots.emplace_back();
            stub.id = id;
            stub.scheduling_domain = program.scheduling_domain();
            stub.language_standard = program.language_standard();
            stub.operations = *dormant_stub_operations;
            stub.initialize = false;
        } else {
            program_snapshots.push_back(program.materialize_transient());
        }
        programs.push_back(&program_snapshots.back());
        // An inert kernel member has no executor (its stub accesses nothing).
        const auto access_complete = (static_kernel
                                         && id < fusion_dormant_process.size()
                                         && fusion_dormant_process[id] == 1U)
            || process_signal_access_is_complete(id);
        access_inventory_complete &= access_complete;
        process_access_complete.push_back(
            static_cast<std::uint8_t>(access_complete));
        if (access_complete) {
            continue;
        }
        ++incomplete_access_count;
        if (!profile_systemverilog_waves
            || incomplete_access_rows >= 32U) {
            continue;
        }

        const auto& state = processes[id];
        const auto& deferred = state.cold().deferred_executor;
        const ProcessExecutorProgramBinding* binding { };
        if (state.executor) {
            binding = state.executor->program_access_binding();
        } else if (deferred && deferred->contract.expected_access) {
            binding = &*deferred->contract.expected_access;
        }
        const bool registered_match = binding != nullptr
            && program.matches_registered_binding(id, *binding);
        const bool forked_match = binding != nullptr
            && program.matches_forked_binding(id, *binding);
        const auto& name = program_snapshots.back().name;
        const auto name_length = static_cast<int>(
            std::min<std::size_t>(name.size(), 96U));
        const auto fork_parent = state.cold().fork_parent;
        const auto fork_site = state.cold().fork_site;
        std::fprintf(stderr,
            "[fsim region-access-census] process=%zu name=%.*s "
            "fork_parent_present=%u fork_parent=%zu "
            "fork_site_present=%u fork_site=%zu concrete=%u deferred=%u "
            "deferred_binding_rejected=%u binding_present=%u "
            "registered_match=%u forked_match=%u revision=%llu\n",
            static_cast<std::size_t>(id), name_length, name.data(),
            fork_parent ? 1U : 0U,
            static_cast<std::size_t>(fork_parent.value_or(0U)),
            fork_site ? 1U : 0U,
            static_cast<std::size_t>(fork_site.value_or(0U)),
            state.executor ? 1U : 0U, deferred ? 1U : 0U,
            deferred && deferred->access_binding_rejected ? 1U : 0U,
            binding != nullptr ? 1U : 0U,
            registered_match ? 1U : 0U, forked_match ? 1U : 0U,
            static_cast<unsigned long long>(
                program.operations().access_revision()));
        ++incomplete_access_rows;
    }
    if (profile_systemverilog_waves) {
        std::fprintf(stderr,
            "[fsim region-access-census] total_processes=%zu incomplete=%zu "
            "rows=%zu prior_global_complete=%u\n",
            processes.size(), incomplete_access_count,
            incomplete_access_rows,
            process_signal_access_inventory_complete ? 1U : 0U);
    }
    const bool observes_all = process_profile_enabled || execution_point_hook
        || driver_change_hook || signal_change_hook || stored_signal_change_hook
        || scalar_signal_change_hook || container_object_change_hook
        || container_element_change_hook
        // An installed query hook may observe arbitrary signals. Treat its
        // presence conservatively instead of invoking application code while
        // building this snapshot.
        || static_cast<bool>(native_signal_observation_any_hook)
        || static_cast<bool>(native_signal_observation_required_hook);
    std::vector<ContainerSignalAlias> direct_aliases;
    std::vector<ContainerElementSignalAlias> element_aliases;
    std::vector<ContainerAggregateSignalAlias> aggregate_aliases;
    for (const auto& alias : container_signal_aliases) {
        if (alias) {
            direct_aliases.push_back(*alias);
        }
    }
    for (const auto& object_aliases : container_element_signal_aliases) {
        for (const auto& alias : object_aliases) {
            if (alias) {
                element_aliases.push_back(*alias);
            }
        }
    }
    for (const auto& alias : container_aggregate_signal_aliases) {
        if (alias) {
            aggregate_aliases.push_back(*alias);
        }
    }
    auto graph_bindings = detail::build_region_graph_container_bindings(
        signals, container_objects, direct_aliases, element_aliases,
        aggregate_aliases);
    auto& container_descriptors = graph_bindings.containers;
    auto& signal_alias_families = graph_bindings.alias_families;
    auto& complete_alias_member = graph_bindings.complete_alias_member;
    std::vector<RegionSignalDescriptor> descriptors;
    descriptors.reserve(signals.size());
    for (SignalId id = 0U; id < signals.size(); ++id) {
        const auto& signal = signals[id];
        auto observations = observes_all ? RegionObservation::unknown
                                        : RegionObservation::none;
        if (signal_transaction_observed[id] || signal.event_variable) {
            observations = observations | RegionObservation::events;
        }
        const bool has_alias_family = complete_alias_member[id] != 0U;
        const bool runtime_dependency = has_alias_family
            ? native_signal_has_non_alias_runtime_dependency(id, true)
            : native_signal_has_runtime_dependency(id, true);
        if (runtime_dependency || !signal_container_aliases[id].empty()
            || signal.has_charge_strength
            // An installed per-signal query may observe arbitrary state.
            // Do not call application code while preparing this snapshot.
            || static_cast<bool>(native_signal_observation_required_hook)) {
            observations = observations | RegionObservation::unknown;
        }
        if (signal.public_value_reference_exposed) {
            observations = observations | RegionObservation::unknown;
        }
        if (forced_values[id] || forced_driver_values[id]) {
            observations = observations | RegionObservation::mutation;
        }
        if (monitor_watches(id)) {
            observations = observations | RegionObservation::current;
        }
        const auto width = signal.initial_value.width();
        if (width > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("RegionGraph signal width is not representable");
        }
        descriptors.push_back({ static_cast<std::uint32_t>(width), signal.resolution,
            signal.value_kind, signal.has_implicit_driver,
            static_cast<bool>(external_driver_values[id]), signal.event_variable,
            observations });
    }
    auto graph = RegionGraph::build(programs, descriptors,
        profile_systemverilog_waves, container_descriptors,
        process_access_complete, signal_alias_families);
    std::shared_ptr<const SignalDriverInventory> driver_inventory;
    if (elaborated_signal_driver_inventory
        && signal_driver_inventory_matches_structure(
            *elaborated_signal_driver_inventory, graph)) {
        driver_inventory = elaborated_signal_driver_inventory;
    } else if (region_signal_driver_inventory
        && signal_driver_inventory_matches_structure(
            *region_signal_driver_inventory, graph)) {
        driver_inventory = region_signal_driver_inventory;
    } else {
        driver_inventory = std::make_shared<SignalDriverInventory>(
            SignalDriverLayout::inventory(graph));
    }
    auto snapshot = prepare_region_runtime_snapshot(std::move(graph), programs,
        access_inventory_complete, enable_region_kernel,
        may_compile_backends, std::move(driver_inventory));
    snapshot.profile_systemverilog_waves = profile_systemverilog_waves;
    if (profile_systemverilog_waves) {
        report_prepared_allocator_arena_statistics(snapshot.generation);
        // Count current source views and the prepared snapshot only, without
        // expanding lazy startup banks. Register slots are occurrences across
        // views, not unique allocated runtime slots. Storage totals cover
        // unique OperationList bodies, boxed group object sizes, and direct
        // capacities for common nested vectors. Vector element payloads,
        // string character buffers, other nested payloads, per-instance remaps,
        // allocator metadata, transient builder storage, old snapshots, and
        // escaped facades are excluded. These lower bounds are not total RSS.
        struct StorageCensus {
            std::size_t views { };
            std::size_t operation_occurrences { };
            std::size_t register_slot_occurrences { };
            std::size_t uncached_startup_banks { };
            storage_census_detail::UniqueOperationBodyStorageCensus bodies;
        };

        StorageCensus original_views;
        StorageCensus activation_programs_census;
        StorageCensus forwarding_programs_census;
        StorageCensus backend_pool_kernel_refs;
        StorageCensus all_views;
        const auto add_body = [](StorageCensus& census,
                                  const OperationList& operations) {
            census.bodies.add(operations);
        };
        const auto add_operations = [&](StorageCensus& census,
                                        const OperationList& operations,
                                        const std::size_t register_count) {
            ++census.views;
            census.operation_occurrences += operations.size();
            census.register_slot_occurrences += register_count;
            add_body(census, operations);
            ++all_views.views;
            all_views.operation_occurrences += operations.size();
            all_views.register_slot_occurrences += register_count;
            add_body(all_views, operations);
        };
        const auto add_uncached_startup_bank = [&](StorageCensus& census,
                                                    const std::size_t count,
                                                    const std::size_t registers) {
            ++census.views;
            census.operation_occurrences += count;
            census.register_slot_occurrences += registers;
            ++census.uncached_startup_banks;
            ++all_views.views;
            all_views.operation_occurrences += count;
            all_views.register_slot_occurrences += registers;
            ++all_views.uncached_startup_banks;
        };
        for (ProcessId id = 0U; id < processes.size(); ++id) {
            const auto program = processes.program_view(id);
            const auto* const compact = processes.compact_constant(id);
            if (compact != nullptr && compact->startup_write_bank != nullptr) {
                const auto cached
                    = compact->startup_write_bank->cached_operations();
                if (cached) {
                    add_operations(original_views, *cached,
                        program.register_count());
                } else {
                    add_uncached_startup_bank(original_views,
                        compact->startup_write_bank->operation_count,
                        program.register_count());
                }
                continue;
            }
            add_operations(original_views, program.operations(),
                program.register_count());
        }
        for (const auto& program : snapshot.programs_by_component) {
            if (!program) {
                continue;
            }
            const auto& activation = program->activation_kernel.program;
            add_operations(activation_programs_census,
                activation.operations, activation.register_count);
            if (!program->forwarding_kernel) {
                continue;
            }
            const auto& forwarding
                = program->forwarding_kernel->execution_kernel.program;
            add_operations(forwarding_programs_census,
                forwarding.operations, forwarding.register_count);
        }
        for (const auto& entry : snapshot.backend_pool) {
            if (entry) {
                add_operations(backend_pool_kernel_refs,
                    entry->kernel.program.operations,
                    entry->kernel.program.register_count);
            }
        }
        for (const auto& entry : snapshot.forwarding_backend_pool) {
            if (entry) {
                const auto& forwarding = entry->kernel.execution_kernel.program;
                add_operations(backend_pool_kernel_refs,
                    forwarding.operations, forwarding.register_count);
            }
        }
        for (const auto& entry : snapshot.frontier_backend_pool) {
            if (entry) {
                add_operations(backend_pool_kernel_refs,
                    entry->kernel.program.operations,
                    entry->kernel.program.register_count);
            }
        }
        const auto report_census = [&](const char* const category,
                                       const StorageCensus& census) {
            std::size_t unique_operations { };
            std::size_t operation_capacity_bytes { };
            std::size_t boxed_group_allocations { };
            std::size_t boxed_group_object_bytes { };
            std::size_t nested_vector_allocations { };
            std::size_t nested_vector_capacity_elements { };
            std::size_t nested_vector_capacity_bytes { };
            for (const auto& [identity, body] : census.bodies.unique_bodies) {
                static_cast<void>(identity);
                unique_operations += body.operation_count;
                operation_capacity_bytes
                    += body.operation_capacity * sizeof(Operation);
                boxed_group_allocations += body.boxed_group_allocations;
                boxed_group_object_bytes += body.boxed_group_object_bytes;
                nested_vector_allocations += body.nested_vector_allocations;
                nested_vector_capacity_elements
                    += body.nested_vector_capacity_elements;
                nested_vector_capacity_bytes
                    += body.nested_vector_capacity_bytes;
            }
            const auto body_references = census.bodies.body_references;
            const auto unique_bodies = census.bodies.unique_bodies.size();
            std::fprintf(stderr,
                "[fsim simir-storage-census] generation=%llu "
                "scope=current-source-views-and-prepared-snapshot "
                "bytes_scope=outer-operations-boxed-groups-common-nested-vectors "
                "unique_capacity_bytes_scope=Operation-vector-only "
                "excluded=instance-overrides,string-buffers,vector-element-payloads,"
                "RareVector-wrapper-shells "
                "category=%s views=%zu operation_occurrences=%zu "
                "register_slot_occurrences=%zu body_references=%zu "
                "unique_bodies=%zu duplicate_body_references=%zu "
                "unique_body_operations=%zu "
                "unique_capacity_bytes_lower_bound=%zu "
                "boxed_group_allocations=%zu "
                "boxed_group_object_bytes_lower_bound=%zu "
                "nested_vector_allocations=%zu "
                "nested_vector_capacity_elements=%zu "
                "nested_vector_capacity_bytes_lower_bound=%zu "
                "uncached_startup_banks=%zu unbacked_views=%zu\n",
                static_cast<unsigned long long>(snapshot.generation), category,
                census.views, census.operation_occurrences,
                census.register_slot_occurrences, body_references, unique_bodies,
                body_references - unique_bodies, unique_operations,
                operation_capacity_bytes, boxed_group_allocations,
                boxed_group_object_bytes, nested_vector_allocations,
                nested_vector_capacity_elements, nested_vector_capacity_bytes,
                census.uncached_startup_banks, census.bodies.unbacked_views);
        };
        report_census("original_process_views", original_views);
        report_census("activation_programs", activation_programs_census);
        report_census("optional_forwarding_programs",
            forwarding_programs_census);
        report_census("backend_pool_kernel_refs", backend_pool_kernel_refs);
        report_census("all_categories", all_views);
        region_graph_detail::report_prepared_region_metadata_census(snapshot);
    }
    if (profile_systemverilog_waves) {
        auto* const frontier_provider = region_kernel_backend_provider
            ? dynamic_cast<RegionFrontierBackendProvider*>(
                  region_kernel_backend_provider.get())
            : nullptr;
        const bool frontier_preparation_enabled
            = frontier_provider != nullptr
            && (systemverilog_local_wave_enabled || enable_region_kernel)
            && !process_profile_enabled && !execution_point_hook
            && !scheduler.trace_hook_installed() && !driver_change_hook
            && !signal_change_hook && !stored_signal_change_hook
            && !scalar_signal_change_hook && !container_object_change_hook
            && !container_element_change_hook
            && !native_signal_observation_any_hook
            && !native_signal_observation_required_hook;
        const auto activation_programs = static_cast<std::size_t>(
            std::ranges::count_if(snapshot.programs_by_component,
                [](const auto& program) { return program.has_value(); }));
        std::size_t systemverilog_frontier_runtimes { };
        std::size_t systemverilog_frontier_members { };
        std::size_t generic_frontier_runtimes { };
        std::size_t generic_frontier_members { };
        std::size_t other_frontier_runtimes { };
        for (const auto& runtime : snapshot.frontier_runtime_by_component) {
            if (!runtime || !runtime->backend) {
                continue;
            }
            const auto member_count = runtime->backend->kernel.members.size();
            if (runtime->execution_mode
                == RegionFrontierExecutionModeV2::systemverilog_active) {
                ++systemverilog_frontier_runtimes;
                systemverilog_frontier_members += member_count;
            } else if (runtime->execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update) {
                ++generic_frontier_runtimes;
                generic_frontier_members += member_count;
            } else {
                ++other_frontier_runtimes;
            }
        }
        std::fprintf(stderr,
            "[fsim frontier-snapshot] generation=%llu processes=%zu "
            "provider=%u preparation_enabled=%u may_compile=%u "
            "activation_programs=%zu backend_pool_entries=%zu "
            "sv_active_runtimes=%zu sv_active_members=%zu "
            "generic_runtimes=%zu generic_members=%zu "
            "other_runtimes=%zu v2_selected_forwarding_prefixes=%llu "
            "v2_selected_forwarding_members=%llu\n",
            static_cast<unsigned long long>(snapshot.generation),
            processes.size(), frontier_provider != nullptr ? 1U : 0U,
            frontier_preparation_enabled ? 1U : 0U,
            may_compile_backends ? 1U : 0U, activation_programs,
            snapshot.frontier_backend_pool.size(),
            systemverilog_frontier_runtimes, systemverilog_frontier_members,
            generic_frontier_runtimes, generic_frontier_members,
            other_frontier_runtimes,
            static_cast<unsigned long long>(
                systemverilog_wave_profile_v2_selected_forwarding_prefixes),
            static_cast<unsigned long long>(
                systemverilog_wave_profile_v2_selected_forwarding_members));
    }
    return snapshot;
}

Interpreter::Impl::RegionRuntimeSnapshot
Interpreter::Impl::prepare_current_region_runtime_snapshot()
{
    // Quiet-point refresh reuses the startup policy. Environment changes made
    // after start cannot silently change profiling or execution admission.
    return build_region_runtime_snapshot(systemverilog_wave_profile_enabled,
        systemverilog_region_kernel_enabled, false);
}

void Interpreter::Impl::build_region_graph()
{
    // The only environment reads occur during startup. The resulting flags are
    // cached and reused by a later drained-slot graph recertification.
    const bool profile_systemverilog_waves
        = std::getenv("FSIM_PROFILE_SV_WAVES") != nullptr;
    const auto* const region_kernel_environment
        = std::getenv("FSIM_ENABLE_SV_REGION_KERNEL");
    // Certified regions are opt-in. On the representative throughput designs
    // they executed no native region kernels or frontier members while their
    // admission, authority and recertification work cost simulation time
    // (r37 analysis). Disabling them retains checked execution and does not
    // change any language scheduling rules; FSIM_ENABLE_SV_REGION_KERNEL=1
    // enables them for differential validation and further development.
    const bool enable_region_kernel = region_kernel_environment != nullptr
        && std::string_view { region_kernel_environment } == "1";
    const auto* const wide_commit_environment
        = std::getenv("FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT");
    a4_wide_single_owner_commit_enabled = enable_region_kernel
        && (wide_commit_environment == nullptr
            || std::string_view { wide_commit_environment } == "1");
    a4_wide_single_owner_versioned_storage_explicit
        = enable_region_kernel && wide_commit_environment != nullptr
        && std::string_view { wide_commit_environment } == "1";
    const auto* const disjoint_commit_environment
        = std::getenv("FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT");
    a4_wide_disjoint_owner_commit_enabled = enable_region_kernel
        && (disjoint_commit_environment == nullptr
            || std::string_view { disjoint_commit_environment } == "1");
    a4_wide_disjoint_owner_versioned_storage_explicit
        = enable_region_kernel && disjoint_commit_environment != nullptr
        && std::string_view { disjoint_commit_environment } == "1";
    const auto* const local_wave_environment
        = std::getenv("FSIM_ENABLE_SV_LOCAL_WAVE");
    // Certified local state is the default within enabled regions. The
    // explicit off setting retains the checked publication comparison route.
    systemverilog_local_wave_enabled = enable_region_kernel
        && (local_wave_environment == nullptr
            || std::string_view { local_wave_environment } == "1");
    auto snapshot = build_region_runtime_snapshot(
        profile_systemverilog_waves, enable_region_kernel, true);
    // Reserve checked wave scratch before simulation can publish updates. If
    // either reservation fails, the old graph/cache pair remains installed.
    cohort_overflow_contexts.reserve(processes.size());
    cohort_overflow_entries.reserve(processes.size());
    publish_region_runtime_snapshot(std::move(snapshot));
    systemverilog_wave_profile_enabled = profile_systemverilog_waves;
    systemverilog_region_kernel_enabled = enable_region_kernel;
}

void Interpreter::Impl::try_recertify_region_graph() noexcept
{
    if (!started || !region_recertification_pending
        || !scheduler.at_runtime_slot_quiet_point()) {
        return;
    }
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_region_recertification_attempts;
    }
    if (!region_recertification_requires_snapshot
        && try_rebind_value_only_region_components()) {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_recertification_successes;
            systemverilog_wave_profile_region_recertification_processes
                += region_graph->processes().size();
            systemverilog_wave_profile_region_recertification_signals
                += region_graph->signals().size();
            systemverilog_wave_profile_region_recertification_components
                += region_graph->certificate_inventory().components.size();
        }
        region_recertification_pending = false;
        return;
    }
    request_full_region_recertification();
    try {
        auto snapshot = prepare_current_region_runtime_snapshot();
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_recertification_successes;
            systemverilog_wave_profile_region_recertification_processes
                += snapshot.graph.processes().size();
            systemverilog_wave_profile_region_recertification_signals
                += snapshot.graph.signals().size();
            systemverilog_wave_profile_region_recertification_components
                += snapshot.graph.certificate_inventory().components.size();
        }
        publish_region_runtime_snapshot(std::move(snapshot));
        region_recertification_pending = false;
    } catch (...) {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_recertification_failures;
        }
        // The previous graph was revoked before arbitrary code ran. Keep it
        // stale and retry after a later successfully prepared drained slot.
    }
}

void Interpreter::Impl::request_full_region_recertification(
    const std::source_location caller) noexcept
{
    if (!region_recertification_requires_snapshot) {
        report_region_recertification_cause(
            systemverilog_wave_profile_enabled, "full-snapshot-transition",
            "request-full", region_runtime_generation,
            std::numeric_limits<std::size_t>::max(),
            region_authoritative_recertification_waiting, caller);
    }
    completed_callback_observation_generation = 0U;
    region_recertification_pending = true;
    region_recertification_requires_snapshot = true;
    if (region_value_only_recertification_nonempty_components != 0U) {
        std::ranges::fill(region_value_only_recertification_by_component, 0U);
        region_value_only_recertification_nonempty_components = 0U;
    }
}

void Interpreter::Impl::clear_value_only_region_recertification(
    const std::source_location caller) noexcept
{
    if (region_value_only_recertification_nonempty_components != 0U) {
        std::ranges::fill(region_value_only_recertification_by_component, 0U);
        region_value_only_recertification_nonempty_components = 0U;
    }
    if (region_recertification_pending) {
        if (!region_recertification_requires_snapshot) {
            report_region_recertification_cause(
                systemverilog_wave_profile_enabled, "full-snapshot-transition",
                "clear-value-only", region_runtime_generation,
                std::numeric_limits<std::size_t>::max(),
                region_authoritative_recertification_waiting, caller);
        }
        region_recertification_requires_snapshot = true;
    }
}

void Interpreter::Impl::mark_value_only_region_recertification(
    const std::size_t component) noexcept
{
    completed_callback_observation_generation = 0U;
    if (region_recertification_requires_snapshot) {
        region_recertification_pending = true;
        return;
    }
    if (region_authoritative_recertification_waiting || !region_graph
        || component >= region_value_only_recertification_by_component.size()
        || component >= region_authoritative_state_by_component.size()
        || component >= region_graph->certificate_inventory().components.size()
        || !region_graph->component_epochs_current(component)) {
        request_full_region_recertification();
        return;
    }
    const auto& state = region_authoritative_state_by_component[component];
    if (!state || !state->valid()
        || state->generation() != region_runtime_generation
        || !state->values().requires_prewrite_unbind()) {
        request_full_region_recertification();
        return;
    }
    auto& candidate
        = region_value_only_recertification_by_component[component];
    if (candidate == 0U) {
        candidate = 1U;
        ++region_value_only_recertification_nonempty_components;
    }
    region_recertification_pending = true;
}

bool Interpreter::Impl::try_rebind_value_only_region_components() noexcept
{
    const auto reject = [this](const char* const reason,
                            const std::size_t component,
                            const std::source_location caller
                                = std::source_location::current()) noexcept {
        report_region_recertification_cause(
            systemverilog_wave_profile_enabled, "value-rebind-decline", reason,
            region_runtime_generation, component,
            region_authoritative_recertification_waiting, caller);
        return false;
    };
    const auto component_count
        = region_value_only_recertification_by_component.size();
    if (region_recertification_requires_snapshot || !region_graph
        || region_authoritative_recertification_waiting
        || region_forwarding_role_flush_pending_after_discard
        || region_forwarding_role_journal_nonempty_components != 0U
        || component_count == 0U
        || component_count != region_authoritative_state_by_component.size()
        || component_count != region_kernel_backends_by_component.size()
        || component_count
            != region_cone_forwarding_backends_by_component.size()
        || (!region_local_wave_state_by_component.empty()
            && component_count != region_local_wave_state_by_component.size())
        || (!region_frontier_runtime_by_component.empty()
            && component_count != region_frontier_runtime_by_component.size())
        || component_count
            != region_graph->certificate_inventory().components.size()) {
        return reject("snapshot-or-layout", std::numeric_limits<std::size_t>::max());
    }

    bool any_candidate { };
    for (std::size_t component = 0U;
         component < component_count;
         ++component) {
        if (region_value_only_recertification_by_component[component] == 0U) {
            continue;
        }
        any_candidate = true;
        if (!region_graph->component_epochs_current(component)) {
            return reject("component-epochs", component);
        }
        const auto& state = region_authoritative_state_by_component[component];
        if (!state || !state->valid()
            || state->generation() != region_runtime_generation
            || !state->values().can_bind_packed_slots()) {
            return reject("authoritative-state-generation-or-bindability", component);
        }
        for (const auto signal : state->values().layout().signal_ids()) {
            if (signal < direct_signal_materialization_pending.size()
                && direct_signal_materialization_pending[signal] != 0U) {
                return reject("pending-direct-materialization", component);
            }
        }

        const auto* const local =
            component < region_local_wave_state_by_component.size()
            ? region_local_wave_state_by_component[component].get() : nullptr;
        if (local != nullptr) {
            if (local->generation != region_runtime_generation) {
                return reject("local-runtime-generation", component);
            }
            for (const auto& storage : local->private_update_ticket_storage) {
                if (storage && !storage->available()) {
                    return reject("private-update-ticket-active", component);
                }
            }
            if (local->forwarding_results) {
                const auto& bank = *local->forwarding_results;
                if (bank.active || bank.role_journal_enabled
                    || bank.remaining_members != 0U
                    || !bank.applied_role_mutations.empty()
                    || !bank.applied_role_metadata.empty()
                    || std::ranges::any_of(
                        bank.prepared_role_mutation_ready,
                        [](const std::uint8_t ready) {
                            return ready != 0U;
                        })
                    || std::ranges::any_of(
                        bank.prepared_role_mutations,
                        [](const auto& mutation) {
                            return mutation.preflighted
                                || mutation.preflight_lock_count != 0U
                                || std::ranges::any_of(
                                    mutation.replacements,
                                    [](const auto& replacement) {
                                        return static_cast<bool>(replacement);
                                    });
                        })) {
                    return reject("forwarding-role-or-mutation-state", component);
                }
                if (bank.stage_batch
                    && std::ranges::count(
                        bank.stage_batch_pool, bank.stage_batch) != 1) {
                    return reject("stage-batch-membership", component);
                }
                for (const auto& batch : bank.stage_batch_pool) {
                    if (!batch) {
                        return reject("missing-pooled-stage-batch", component);
                    }
                    std::size_t pool_references { };
                    for (const auto& pooled_batch : bank.stage_batch_pool) {
                        if (pooled_batch == batch) {
                            ++pool_references;
                        }
                    }
                    const auto expected_references = pool_references
                        + static_cast<std::size_t>(
                            bank.stage_batch == batch);
                    if (pool_references != 1U
                        || static_cast<std::size_t>(batch.use_count())
                            != expected_references) {
                        return reject("pooled-stage-batch-reference-count", component);
                    }
                }
            }
        }

        const auto* const runtime =
            component < region_frontier_runtime_by_component.size()
            ? region_frontier_runtime_by_component[component].get() : nullptr;
        if (runtime != nullptr) {
            const auto& frame = runtime->frame;
            if (!runtime->backend || !runtime->backend->executor
                || runtime->owner != this
                || runtime->component != component
                || runtime->runtime_generation != region_runtime_generation
                || runtime->invalidated
                || runtime->in_use.test(std::memory_order_acquire)
                || runtime->authoritative_state != state
                || runtime->stop_requested != 0U
                || frame.runtime_generation != region_runtime_generation
                || frame.bound_runtime_generation != region_runtime_generation
                || frame.certificate_generation
                    != runtime->backend->executor->layout()
                        .certificate_generation
                || frame.component_generation
                    != runtime->backend->executor->layout()
                        .component_generation
                || frame.scheduler_task_cursor != 0U
                || frame.pending_write_count != 0U
                || frame.staged_event_count != 0U
                || frame.committed_signal_count != 0U
                || frame.generic_update_ack_count != 0U
                || frame.current_commit_changed != 0U) {
                if (systemverilog_wave_profile_enabled) {
                    // Snapshot the failed guard's state for diagnosis only.
                    // Virtual layout queries remain solely in the guard above.
                    std::fprintf(stderr,
                        "fsim-profile: sv-region-recertification "
                        "event=value-rebind-frontier-detail "
                        "component=%zu backend=%u executor=%u "
                        "owner_matches=%u runtime_component=%zu "
                        "runtime_generation=%llu expected_generation=%llu "
                        "invalidated=%u in_use=%u state_matches=%u stop=%u "
                        "frame_generation=%llu bound_generation=%llu "
                        "certificate_generation=%llu component_generation=%llu "
                        "task_cursor=%u task_count=%u pending=%u staged=%u committed=%u "
                        "generic_ack=%u current_changed=%u\n",
                        component, static_cast<unsigned>(!!runtime->backend),
                        static_cast<unsigned>(runtime->backend
                            && !!runtime->backend->executor),
                        static_cast<unsigned>(runtime->owner == this),
                        runtime->component,
                        static_cast<unsigned long long>(
                            runtime->runtime_generation),
                        static_cast<unsigned long long>(
                            region_runtime_generation),
                        static_cast<unsigned>(runtime->invalidated),
                        static_cast<unsigned>(runtime->in_use.test(
                            std::memory_order_acquire)),
                        static_cast<unsigned>(
                            runtime->authoritative_state == state),
                        static_cast<unsigned>(runtime->stop_requested),
                        static_cast<unsigned long long>(
                            frame.runtime_generation),
                        static_cast<unsigned long long>(
                            frame.bound_runtime_generation),
                        static_cast<unsigned long long>(
                            frame.certificate_generation),
                        static_cast<unsigned long long>(
                            frame.component_generation),
                        frame.scheduler_task_cursor, frame.scheduler_task_count,
                        frame.pending_write_count,
                        frame.staged_event_count, frame.committed_signal_count,
                        frame.generic_update_ack_count,
                        frame.current_commit_changed);
                }
                return reject("frontier-runtime-or-frame-state", component);
            }
        }

        const auto& backend
            = region_kernel_backends_by_component[component];
        if (backend && backend->in_use.test(std::memory_order_acquire)) {
            return reject("native-backend-in-use", component);
        }
        const auto& forwarding
            = region_cone_forwarding_backends_by_component[component];
        if (forwarding && forwarding->in_use.test(std::memory_order_acquire)) {
            return reject("forwarding-backend-in-use", component);
        }
    }
    if (!any_candidate
        || scheduler.stop_requested()
        || std::ranges::any_of(systemverilog_update_slots,
            [](const SystemVerilogUpdateSlot& slot) {
                return static_cast<bool>(slot.prepared_output_batch);
            })) {
        return reject("no-candidate-or-stop-or-systemverilog-update", std::numeric_limits<std::size_t>::max());
    }
    for (const auto& backend : region_kernel_backend_pool) {
        if (!backend || !backend->native_workspace
            || !backend->native_workspace->prepared_output_batch) {
            continue;
        }
        const auto& batch
            = *backend->native_workspace->prepared_output_batch;
        if (batch.active || batch.group_ticket_active
            || batch.pending_tickets != 0U
            || batch.pending_dispatch_members != 0U) {
            return reject("native-prepared-output-state", std::numeric_limits<std::size_t>::max());
        }
    }

    // Every candidate was preflighted above before the first live facade is
    // rebound. Quiet-point serialization keeps that checked state stable.
    for (std::size_t component = 0U;
         component < component_count;
         ++component) {
        if (region_value_only_recertification_by_component[component] == 0U) {
            continue;
        }
        const auto& state = region_authoritative_state_by_component[component];
        const auto bound_slots = state->values().bind_packed_slots();
        if (bound_slots == 0U) {
            std::terminate();
        }
        if (component < region_local_wave_state_by_component.size()) {
            const auto& local
                = region_local_wave_state_by_component[component];
            if (local) {
                local->authoritative_revision = state->values().revision();
                local->seeded = false;
            }
        }
        if (component < region_frontier_runtime_by_component.size()) {
            const auto& runtime
                = region_frontier_runtime_by_component[component];
            if (runtime) {
                for (auto& plane : runtime->planes) {
                    for (std::size_t index = 0U; index < 4U; ++index) {
                        plane.current_planes[index] = nullptr;
                        plane.previous_planes[index] = nullptr;
                        plane.stored_planes[index] = nullptr;
                        plane.owner_planes[index] = nullptr;
                        plane.boundary_planes[index] = nullptr;
                    }
                }
            }
        }
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_a4_slot_bind_components;
            systemverilog_wave_profile_a4_slot_bindings += bound_slots;
            ++systemverilog_wave_profile_a4_bound_components;
            systemverilog_wave_profile_a4_bound_slots += bound_slots;
        }
    }
    assert(region_value_only_recertification_nonempty_components != 0U);
    std::ranges::fill(region_value_only_recertification_by_component, 0U);
    region_value_only_recertification_nonempty_components = 0U;
    region_recertification_requires_snapshot = false;
    completed_callback_observation_generation = 0U;
    return true;
}

void Interpreter::Impl::note_region_graph_policy_change() noexcept
{
    if (!started || !systemverilog_region_kernel_enabled || !region_graph) {
        return;
    }
    const auto signal_count = region_graph->signals().size();
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        static_cast<void>(region_graph->observe_signal(
            signal, RegionObservation::unknown));
    }
    request_full_region_recertification();
    demote_all_region_authoritative_slots(true);
}

void Interpreter::Impl::demote_region_authoritative_slots(
    const std::size_t component, const bool observation,
    const std::source_location caller) noexcept
{
    if (component < region_value_only_recertification_by_component.size()
        && region_value_only_recertification_by_component[component] != 0U) {
        region_value_only_recertification_by_component[component] = 0U;
        assert(region_value_only_recertification_nonempty_components != 0U);
        --region_value_only_recertification_nonempty_components;
        if (region_recertification_pending) {
            if (!region_recertification_requires_snapshot) {
                report_region_recertification_cause(
                    systemverilog_wave_profile_enabled,
                    "full-snapshot-transition",
                    observation ? "demote-observed-candidate" : "demote-candidate",
                    region_runtime_generation, component,
                    region_authoritative_recertification_waiting, caller);
            }
            region_recertification_requires_snapshot = true;
        }
    }
    unbind_region_authoritative_slots(component, observation);
}

void Interpreter::Impl::unbind_region_authoritative_slots(
    const std::size_t component, const bool observation) noexcept
{
    if (component >= region_authoritative_state_by_component.size()) {
        return;
    }
    const auto& state = region_authoritative_state_by_component[component];
    if (!state || !state->values().packed_slots_bound()) {
        return;
    }
    const auto count = state->values().unbind_packed_slots();
    if (!systemverilog_wave_profile_enabled) {
        return;
    }
    if (systemverilog_wave_profile_a4_bound_components != 0U) {
        --systemverilog_wave_profile_a4_bound_components;
    }
    if (count <= systemverilog_wave_profile_a4_bound_slots) {
        systemverilog_wave_profile_a4_bound_slots -= count;
    } else {
        systemverilog_wave_profile_a4_bound_slots = 0U;
    }
    if (observation) {
        ++systemverilog_wave_profile_a4_materialized_components;
        systemverilog_wave_profile_a4_materialized_slots += count;
    }
}

void Interpreter::Impl::demote_all_region_authoritative_slots(
    const bool observation, const std::source_location caller) noexcept
{
    for (std::size_t component = 0U;
         component < region_authoritative_state_by_component.size();
         ++component) {
        demote_region_authoritative_slots(component, observation, caller);
    }
}

void Interpreter::Impl::prepare_region_authoritative_write(
    const SignalId signal) noexcept
{
    // Versioned wide roles may have owning snapshots or read leases. Ordinary
    // PackedLogic4 writers must detach those slots before changing their
    // storage; the unversioned narrow path keeps its existing in-place mirror
    // behavior.
    if (signal >= region_authoritative_component_by_signal.size()) {
        return;
    }
    const auto component = region_authoritative_component_by_signal[signal];
    if (component >= region_authoritative_state_by_component.size()) {
        return;
    }
    const auto& state = region_authoritative_state_by_component[component];
    if (state && state->values().requires_prewrite_unbind()) {
        // Rebind at a quiet point from the committed current, LAST, stored,
        // and owner roles after this write has drained.
        mark_value_only_region_recertification(component);
        unbind_region_authoritative_slots(component, false);
    }
}

void Interpreter::Impl::prepare_region_authoritative_write(
    const std::span<const SignalId> signal_ids) noexcept
{
    for (const auto signal : signal_ids) {
        prepare_region_authoritative_write(signal);
    }
}

void Interpreter::Impl::prepare_region_authoritative_family_write(
    const ContainerObjectId object) noexcept
{
    if (object < container_element_signal_aliases.size()) {
        for (const auto& alias : container_element_signal_aliases[object]) {
            if (alias) {
                prepare_region_authoritative_write(alias->signal);
            }
        }
    }
    if (object < container_aggregate_signal_aliases.size()
        && container_aggregate_signal_aliases[object]) {
        prepare_region_authoritative_write(
            container_aggregate_signal_aliases[object]->signal);
    }
}

void Interpreter::Impl::require_region_forwarding_role_journal_flushed_for_signal(
    const SignalId signal)
{
    if (region_forwarding_role_journal_nonempty_components == 0U) {
        return;
    }
    if ((signal < signal_container_aggregate_aliases.size()
            && signal_container_aggregate_aliases[signal])
        || (signal < signal_container_element_aliases.size()
            && signal_container_element_aliases[signal])) {
        // A leaf or aggregate proxy can update every member of its packed
        // container family. Flush all rows before any family member changes.
        require_all_region_forwarding_role_journals_flushed();
        return;
    }

    const auto bank_has_rows = [this](const std::size_t component) {
        if (component >= region_local_wave_state_by_component.size()) {
            return false;
        }
        const auto& local = region_local_wave_state_by_component[component];
        if (!local || !local->forwarding_results) {
            return false;
        }
        const auto& bank = *local->forwarding_results;
        return !bank.applied_role_mutations.empty()
            || !bank.applied_role_metadata.empty();
    };
    const auto bank_mentions_signal = [signal](
        const RegionConeForwardingResultBank& bank) {
        if (std::ranges::any_of(
                bank.applied_role_metadata,
                [signal](const auto& metadata) {
                    return metadata.signal == signal;
                })) {
            return true;
        }
        return std::ranges::find(bank.boundary_signals, signal)
            != bank.boundary_signals.end();
    };

    // Internal outputs have a direct component index. Try it first, then
    // visit only banks with applied rows to find a component that captured
    // this signal as an external boundary input.
    if (signal < region_authoritative_component_by_signal.size()) {
        const auto component = region_authoritative_component_by_signal[signal];
        if (bank_has_rows(component)
            && !try_flush_region_forwarding_role_journal(component)) {
            throw std::logic_error {
                "cannot access a signal while its private forwarding roles "
                "remain unmaterialized"
            };
        }
    }

    for (std::size_t component = 0U;
         component < region_local_wave_state_by_component.size();
         ++component) {
        if (!bank_has_rows(component)) {
            continue;
        }
        const auto& bank = *region_local_wave_state_by_component[component]
                                ->forwarding_results;
        if (!bank_mentions_signal(bank)) {
            continue;
        }
        if (!try_flush_region_forwarding_role_journal(component)) {
            throw std::logic_error {
                "cannot access a signal while its private forwarding roles "
                "remain unmaterialized"
            };
        }
    }
}

void Interpreter::Impl::require_all_region_forwarding_role_journals_flushed()
{
    if (region_forwarding_role_journal_nonempty_components == 0U) {
        region_forwarding_role_flush_pending_after_discard = false;
        return;
    }
    if (!try_flush_all_region_forwarding_role_journals()) {
        throw std::logic_error {
            "cannot proceed while private forwarding roles remain "
            "unmaterialized"
        };
    }
}

void Interpreter::Impl::prepare_signal_observation(const SignalId signal)
{
    if (signal >= signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    // Initial state is already public. A registration installed before start
    // is included in the ordinary startup observation inventory.
    if (!started) {
        return;
    }
    if (!fused_cone_materializing && signal < fused_cone_by_signal.size()
        && fused_cone_by_signal[signal]
            != std::numeric_limits<std::uint32_t>::max()) {
        // A hidden net of a fused cone becomes visible only through this
        // barrier; publish its current value before any observer reads it.
        materialize_fused_cone(fused_cone_by_signal[signal]);
    }
    if (!static_kernel_materializing
        && signal < static_kernel_owned_signal.size()
        && static_kernel_owned_signal[signal] != 0U) {
        // Kernel-owned signals are hidden until observed.
        materialize_static_kernel_signal(signal);
    }
    completed_callback_observation_generation = 0U;
    if (!region_graph) {
        throw std::logic_error("started interpreter has no observation graph");
    }
    // The role journal must reach the original A4 planes before observing the
    // signal revokes the graph epoch or materializes its public facade.
    require_region_forwarding_role_journal_flushed_for_signal(signal);
    clear_value_only_region_recertification();
    // Invalidate first. If materialization needs memory and fails, no trusted
    // entry may continue using the old observation certificate on retry.
    const auto dependencies = region_graph->observe_signal(
        signal, RegionObservation::unknown);
    for (const auto process : dependencies) {
        if (process < region_component_by_process.size()) {
            const auto component = region_component_by_process[process];
            if (component < region_activation_programs.size()
                && region_activation_programs[component]) {
                request_full_region_recertification();
                demote_region_authoritative_slots(component, true);
            }
        }
    }
    for (const auto process : dependencies) {
        if (process < static_sensitivity_cohort_by_process.size()) {
            const auto cohort = static_sensitivity_cohort_by_process[process];
            if (cohort < fused_static_cohorts.size()) {
                auto& plan = fused_static_cohorts[cohort];
                if (plan.certified) {
                    assert(fused_static_certified_plan_count != 0U);
                    --fused_static_certified_plan_count;
                    plan.certified = false;
                    if (fused_static_counters_enabled) {
                        ++fused_static_counts.observation_invalidations;
                    }
                }
            }
        }
    }
    // The dependency epochs revoke every affected component. Unrelated
    // components retain their exact backing until the next quiet-point
    // recertification; a pending refresh alone does not revoke their proof.
    if (signal < region_authoritative_component_by_signal.size()) {
        demote_region_authoritative_slots(
            region_authoritative_component_by_signal[signal], true);
    }
    promote_container_alias_authority(signal);
    materialize_direct_signal(signal);
    demote_owned_driver(signal);
}

void Interpreter::Impl::expose_signal_value_reference(
    const SignalId signal)
{
    if (started) {
        // get_signal() can materialize a direct mirror; retire hidden A4 role
        // rows before it is allowed to touch that facade.
        require_region_forwarding_role_journal_flushed_for_signal(signal);
    }
    auto& state = get_signal(signal);
    if (!state.public_value_reference_exposed) {
        if (started) {
            // Install the durable pin only after the observation barrier has
            // completed. A failed getter can then retry the same preparation.
            prepare_signal_observation(signal);
        }
        state.public_value_reference_exposed = true;
        advance_direct_signal_read_capability_epoch();
    }
    if (state.public_value_aliases_exposed
        || state.public_value_alias_exposure_in_progress) {
        return;
    }
    state.public_value_alias_exposure_in_progress = true;
    try {
        if (signal < signal_container_aliases.size()) {
            for (const auto object : signal_container_aliases[signal]) {
                expose_container_value_reference(object);
            }
        }
        if (signal < signal_container_element_aliases.size()
            && signal_container_element_aliases[signal]) {
            expose_container_value_reference(
                signal_container_element_aliases[signal]->first);
        }
        if (signal < signal_container_aggregate_aliases.size()
            && signal_container_aggregate_aliases[signal]) {
            expose_container_value_reference(
                *signal_container_aggregate_aliases[signal]);
        }
    } catch (...) {
        state.public_value_alias_exposure_in_progress = false;
        throw;
    }
    state.public_value_alias_exposure_in_progress = false;
    state.public_value_aliases_exposed = true;
}

void Interpreter::Impl::expose_container_value_reference(
    const ContainerObjectId object)
{
    (void)get_container_object(object);
    if (container_value_reference_exposed[object] != 0U
        || container_value_reference_exposure_in_progress[object] != 0U) {
        return;
    }
    container_value_reference_exposure_in_progress[object] = 1U;
    try {
        if (started) {
            prepare_container_value_observation(object);
        }
        // Materialize the initial projection before pinning it. Once exposed,
        // reads return the same backing without lazily replacing its elements.
        (void)read_container_object_value(object);

        // A callback may first expose this backing while one or more nested
        // publications have already preflighted their current phase. Give
        // every such frame an independent scratch projection before returning
        // the reference; each frame will consume its own reservation in LIFO
        // order.
        prepare_active_container_value_reference_refresh(object);

        const auto& container = get_container_object(object);
        if (object < container_signal_aliases.size()
            && container_signal_aliases[object]) {
            expose_signal_value_reference(
                container_signal_aliases[object]->signal);
        }
        if (object < container_element_signal_aliases.size()) {
            for (const auto& alias : container_element_signal_aliases[object]) {
                if (alias) {
                    expose_signal_value_reference(alias->signal);
                }
            }
        }
        if (object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]) {
            expose_signal_value_reference(
                container_aggregate_signal_aliases[object]->signal);
        }
        if (container.slice_alias) {
            expose_container_value_reference(
                container.slice_alias->object);
        }

        auto& exposed = exposed_container_value_references;
        const auto position = std::ranges::lower_bound(exposed, object);
        if (position == exposed.end() || *position != object) {
            exposed.insert(position, object);
        }
        container_value_reference_exposed[object] = 1U;
    } catch (...) {
        container_value_reference_exposure_in_progress[object] = 0U;
        throw;
    }
    container_value_reference_exposure_in_progress[object] = 0U;
}

void Interpreter::Impl::prepare_container_value_observation(
    const ContainerObjectId object)
{
    auto selected = object;
    for (std::size_t depth = 0U; depth < container_objects.size(); ++depth) {
        const auto& container = get_container_object(selected);
        if (selected < container_signal_aliases.size()
            && container_signal_aliases[selected]) {
            prepare_signal_observation(
                container_signal_aliases[selected]->signal);
        }
        if (selected < container_element_signal_aliases.size()) {
            for (const auto& alias : container_element_signal_aliases[selected]) {
                if (alias) {
                    prepare_signal_observation(alias->signal);
                }
            }
        }
        if (selected < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[selected]) {
            prepare_signal_observation(
                container_aggregate_signal_aliases[selected]->signal);
        }
        if (!container.slice_alias) {
            return;
        }
        selected = container.slice_alias->object;
    }
    throw std::logic_error("container slice alias chain is cyclic");
}

void Interpreter::Impl::prepare_callback_observation()
{
    if (!started) {
        return;
    }
    if (!region_graph) {
        throw std::logic_error("started interpreter has no observation graph");
    }
    const auto candidate_generation = region_runtime_generation;
    if (candidate_generation != 0U
        && completed_callback_observation_generation
            == candidate_generation) {
        if (region_forwarding_role_journal_nonempty_components != 0U) {
            completed_callback_observation_generation = 0U;
        }
        // A callback may follow a private publication that occurred after
        // the preceding observation completed. Preserve the ordinary cold
        // path's per-signal ordering, but drain any such rows before taking
        // the whole-snapshot fast path.
        require_all_region_forwarding_role_journals_flushed();
        if (region_runtime_generation == candidate_generation
            && completed_callback_observation_generation
                == candidate_generation) {
            return;
        }
    }
    // Leave the key invalid until every signal has completed successfully;
    // a failed preparation must retry the complete barrier next time.
    completed_callback_observation_generation = 0U;
    // A user callback can inspect or mutate any signal through the public
    // interpreter API. Prepare each signal before entering it, preserving the
    // ordinary pending scheduler queue instead of publishing early. A valid
    // completion key is retained only while no later state transition can
    // leave public roles stale or restore private execution.
    for (SignalId signal = 0U; signal < signals.size(); ++signal) {
        prepare_signal_observation(signal);
    }
    completed_callback_observation_generation = region_runtime_generation;
}

void Interpreter::Impl::OutputCallback::operator()(
    const ProcessId process, const std::string_view text,
    const bool newline, const SimulationTick time,
    const std::uint64_t delta) const
{
    const auto selected = callback;
    if (!selected) {
        return;
    }
    if (owner == nullptr) {
        throw std::logic_error("output callback has no interpreter owner");
    }
    if (!trusted_text_only) {
        owner->prepare_callback_observation();
    }
    (*selected)(process, text, newline, time, delta);
}

void Interpreter::Impl::ReportCallback::operator()(
    const ProcessId process, const std::string_view message,
    const AssertionSeverity severity, const SourceLocation& source,
    const SimulationTick time, const std::uint64_t delta) const
{
    const auto selected = callback;
    if (!selected) {
        return;
    }
    if (owner == nullptr) {
        throw std::logic_error("report callback has no interpreter owner");
    }
    if (!trusted_text_only) {
        owner->prepare_callback_observation();
    }
    (*selected)(process, message, severity, source, time, delta);
}

void Interpreter::prepare_signal_observation(const SignalId signal)
{
    impl_->prepare_signal_observation(signal);
}

} // namespace fsim::runtime::simir
