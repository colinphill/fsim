// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
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
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
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

        friend bool operator==(
            const SignalSnapshot&, const SignalSnapshot&) = default;
    };

    struct PendingChildReceipt {
        ProcessId process { };
        std::size_t component { };
        std::size_t member { };
        std::uint64_t generation { };
        RegionFrontierKeyV1 key;

        friend bool operator==(const PendingChildReceipt& left,
            const PendingChildReceipt& right)
        {
            return left.process == right.process
                && left.component == right.component
                && left.member == right.member
                && left.generation == right.generation
                && left.key.time == right.key.time
                && left.key.delta == right.key.delta
                && left.key.systemverilog_round
                    == right.key.systemverilog_round
                && left.key.stable_order == right.key.stable_order
                && left.key.sequence == right.key.sequence
                && left.key.process_domain == right.key.process_domain
                && left.key.phase == right.key.phase;
        }
    };

    struct PrivateRoleCut {
        bool reached { };
        std::size_t component { };
        std::size_t applied_rows { };
        std::size_t metadata_rows { };
        SignalId signal { };
        ProcessId owner { };
        std::size_t output_index { };
        bool owner_is_stored_alias { };
        bool owner_record_present { };
        bool journal_enabled { };
        bool private_epoch_retired { };
        PendingChildReceipt child;
        SignalSnapshot before;
        std::string expected_current;
        std::string expected_last;
        std::string expected_stored;
        std::string expected_owner;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        ProcessSchedulingDomain event_domain {
            ProcessSchedulingDomain::generic };
        SchedulerPhase event_phase { SchedulerPhase::active };
        std::uint64_t event_round { };
        std::uint64_t value_revision { };
        SimulationTick callback_time { };
        std::uint64_t callback_delta { };
        std::uint64_t callback_round { };
        std::uint64_t callback_order { };
    };

    struct PrivateJournalState {
        std::size_t applied_rows { };
        std::size_t metadata_rows { };
        bool journal_enabled { };
        bool private_epoch_retired { };
    };

    struct BoundaryRangeKernelShape {
        bool found { };
        std::size_t component { };
        std::size_t member_count { };
        bool has_external_boundary_input { };
        bool has_internal_mid_input { };
        bool has_systemverilog_scheduling_domain { };
        bool has_active_update_outputs { };
        bool has_update_publication_outputs { };
        bool boundary_is_not_output { };
        bool has_private_mid_output { };
        bool has_sink_output { };
        bool has_partial_root_member { };
        bool has_expected_child_member { };
        bool has_native_frontier_entry { };
        bool has_native_frontier_runtime { };
    };

    struct AliasBoundaryKernelShape {
        bool found { };
        std::size_t component { };
        std::size_t member_count { };
        ProcessId leaf_writer { };
        ProcessId leaf_reader { };
        bool has_original_proxy_slice_write { };
        bool has_before_internal { };
        bool has_after_internal { };
        bool leaf_is_boundary_input { };
        bool leaf_is_boundary_output { };
        bool leaf_has_full_leaf_owner { };
        bool proxy_is_excluded { };
        bool has_frontier_runtime { };
    };

    struct AliasProxySliceOwner {
        ProcessId process { };
        std::size_t instruction { };
    };

    struct BoundaryV2RouteSelection {
        std::size_t component { };
        std::uint64_t generation { };
        const void* activation_program { };
        const void* local_state { };
        const void* authoritative_state { };
        const void* frontier_runtime { };
        const void* frontier_backend { };
        const void* frontier_executor { };
        std::shared_ptr<void> forwarding_entry;
    };

    enum class PreparedRole : std::uint8_t {
        old_previous,
        old_current,
        old_stored,
        old_owner,
        new_current,
        new_stored,
        new_owner,
    };

    [[nodiscard]] static std::uint64_t native_frontier_dispatches(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the random DAG probe has no interpreter"
            };
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_native_frontier_member_dispatches;
    }

    [[nodiscard]] static std::size_t native_frontier_runtime_count(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the boundary-range probe has no interpreter"
            };
        }
        const auto& runtimes = application.interpreter->impl_
            ->region_frontier_runtime_by_component;
        return static_cast<std::size_t>(std::ranges::count_if(
            runtimes, [](const auto& runtime) {
                return runtime != nullptr;
            }));
    }

    [[nodiscard]] static BoundaryRangeKernelShape
    boundary_range_kernel_shape(const fsim::app::Simulation& simulation,
        const SignalId boundary, const SignalId mid, const SignalId sink,
        const std::uint32_t expected_child_offset,
        const std::uint32_t expected_child_width,
        const std::uint32_t sink_width)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the boundary-range probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        for (std::size_t component = 0U;
             component < state.region_activation_programs.size();
             ++component) {
            const auto& optional_program
                = state.region_activation_programs[component];
            if (!optional_program) {
                continue;
            }
            const auto& kernel = optional_program->activation_kernel;
            BoundaryRangeKernelShape shape;
            shape.component = component;
            shape.member_count = kernel.members.size();
            shape.has_systemverilog_scheduling_domain
                = kernel.program.scheduling_domain
                    == ProcessSchedulingDomain::systemverilog;
            shape.has_active_update_outputs
                = !kernel.outputs.empty()
                && std::ranges::all_of(kernel.outputs,
                    [](const auto& output) {
                        return output.domain
                            == SignalUpdateDomain::systemverilog_active;
                    });
            shape.has_update_publication_outputs
                = !kernel.outputs.empty()
                && std::ranges::all_of(kernel.outputs,
                    [](const auto& output) {
                        return output.publication_kind
                            == RegionOutputPublicationKind::update;
                    });
            shape.has_external_boundary_input = std::ranges::any_of(
                kernel.inputs, [boundary](const auto& input) {
                    return input.signal == boundary && input.width == 73U
                        && input.value_kind == ValueKind::logic4
                        && !input.internal;
                });
            shape.has_internal_mid_input = std::ranges::any_of(
                kernel.inputs, [mid](const auto& input) {
                    return input.signal == mid && input.width == 9U
                        && input.value_kind == ValueKind::logic4
                        && input.internal;
                });
            shape.boundary_is_not_output = std::ranges::none_of(
                kernel.outputs, [boundary](const auto& output) {
                    return output.signal == boundary;
                });
            shape.has_private_mid_output = std::ranges::any_of(
                kernel.outputs, [mid](const auto& output) {
                    return output.signal == mid && output.offset == 0U
                        && output.width == 9U
                        && output.value_kind == ValueKind::logic4;
                }) && std::ranges::find(kernel.internal_signals, mid)
                    != kernel.internal_signals.end();
            shape.has_sink_output = std::ranges::any_of(
                kernel.outputs, [sink, sink_width](const auto& output) {
                    return output.signal == sink && output.offset == 0U
                        && output.width == sink_width
                        && output.value_kind == ValueKind::logic4;
                });
            auto root_process = std::numeric_limits<ProcessId>::max();
            auto child_process = std::numeric_limits<ProcessId>::max();
            for (const auto& member : kernel.members) {
                if (std::ranges::any_of(member.sensitivities,
                        [boundary](const auto& sensitivity) {
                            return sensitivity.signal == boundary
                                && sensitivity.edge == EdgeKind::any
                                && sensitivity.offset == 61U
                                && sensitivity.width == 9U;
                        })) {
                    root_process = member.process;
                }
                if (std::ranges::any_of(member.sensitivities,
                        [mid, expected_child_offset, expected_child_width](
                            const auto& sensitivity) {
                            return sensitivity.signal == mid
                                && sensitivity.edge == EdgeKind::any
                                && sensitivity.offset == expected_child_offset
                                && sensitivity.width == expected_child_width;
                        })) {
                    child_process = member.process;
                }
            }
            shape.has_partial_root_member
                = root_process != std::numeric_limits<ProcessId>::max();
            shape.has_expected_child_member
                = child_process != std::numeric_limits<ProcessId>::max()
                && child_process != root_process;
            shape.has_private_mid_output
                = shape.has_private_mid_output
                && std::ranges::any_of(kernel.outputs,
                    [root_process, mid](const auto& output) {
                        return output.owner == root_process
                            && output.signal == mid;
                    });
            shape.has_sink_output = shape.has_sink_output
                && std::ranges::any_of(kernel.outputs,
                    [child_process, sink, sink_width](const auto& output) {
                        return output.owner == child_process
                            && output.signal == sink
                            && output.width == sink_width;
                    });
            shape.has_native_frontier_entry
                = component < state.region_frontier_backends_by_component.size()
                && state.region_frontier_backends_by_component[component]
                && state.region_frontier_backends_by_component[component]
                       ->executor
                && state.region_frontier_backends_by_component[component]
                       ->executor->step_entry() != nullptr;
            shape.has_native_frontier_runtime
                = component < state.region_frontier_runtime_by_component.size()
                && state.region_frontier_runtime_by_component[component]
                    != nullptr;
            shape.found = shape.member_count == 2U
                && shape.has_external_boundary_input
                && shape.has_internal_mid_input
                && shape.has_systemverilog_scheduling_domain
                && shape.has_active_update_outputs
                && shape.has_update_publication_outputs
                && shape.boundary_is_not_output
                && shape.has_private_mid_output
                && shape.has_sink_output
                && shape.has_partial_root_member
                && shape.has_expected_child_member
                && shape.has_native_frontier_entry
                && shape.has_native_frontier_runtime;
            if (shape.found) {
                return shape;
            }
        }
        return { };
    }

    [[nodiscard]] static std::optional<BoundaryV2RouteSelection>
    select_v2_only_component_route(fsim::app::Simulation& simulation,
        const std::size_t component, const bool shape_found)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter || !shape_found) {
            return std::nullopt;
        }
        auto& state = *application.interpreter->impl_;
        if (component >= state.region_activation_programs.size()
            || component >= state.region_local_wave_state_by_component.size()
            || component >= state.region_authoritative_state_by_component.size()
            || component >= state.region_cone_forwarding_backends_by_component.size()
            || component >= state.region_frontier_backends_by_component.size()
            || component >= state.region_frontier_runtime_by_component.size()
            || state.region_recertification_pending
            || state.region_recertification_requires_snapshot
            || state.region_forwarding_role_flush_pending_after_discard
            || application.interpreter->scheduler().stop_requested()) {
            return std::nullopt;
        }
        const auto& optional_program
            = state.region_activation_programs[component];
        auto& local = state.region_local_wave_state_by_component[component];
        auto& authoritative
            = state.region_authoritative_state_by_component[component];
        auto forwarding
            = state.region_cone_forwarding_backends_by_component[component];
        auto& frontier_backend
            = state.region_frontier_backends_by_component[component];
        auto& runtime = state.region_frontier_runtime_by_component[component];
        if (!optional_program || !local || !authoritative || !forwarding
            || !forwarding->executor || !frontier_backend
            || !frontier_backend->executor || !runtime
            || !frontier_backend->executor->step_entry()
            || runtime->owner != &state || runtime->component != component
            || runtime->runtime_generation != state.region_runtime_generation
            || runtime->invalidated || runtime->in_use.test(std::memory_order_acquire)
            || runtime->authoritative_state != authoritative
            || runtime->backend != frontier_backend
            || local->generation != state.region_runtime_generation
            || forwarding->in_use.test(std::memory_order_acquire)) {
            return std::nullopt;
        }
        if (!local->forwarding_results) {
            return std::nullopt;
        }
        const auto& bank = *local->forwarding_results;
        if (bank.active || bank.role_journal_enabled
            || bank.private_epoch_retired || bank.remaining_members != 0U
            || !bank.applied_role_mutations.empty()
            || !bank.applied_role_metadata.empty()) {
            return std::nullopt;
        }
        BoundaryV2RouteSelection selection;
        selection.component = component;
        selection.generation = state.region_runtime_generation;
        selection.activation_program = std::addressof(*optional_program);
        selection.local_state = local.get();
        selection.authoritative_state = authoritative.get();
        selection.frontier_runtime = runtime.get();
        selection.frontier_backend = frontier_backend.get();
        selection.frontier_executor = frontier_backend->executor.get();
        selection.forwarding_entry = forwarding;
        state.region_cone_forwarding_backends_by_component[component].reset();
        return selection;
    }

    [[nodiscard]] static std::optional<BoundaryV2RouteSelection>
    select_v2_only_boundary_route(fsim::app::Simulation& simulation,
        const BoundaryRangeKernelShape& shape)
    {
        return select_v2_only_component_route(
            simulation, shape.component, shape.found);
    }

    [[nodiscard]] static AliasBoundaryKernelShape
    alias_boundary_kernel_shape(const fsim::app::Simulation& simulation,
        const SignalId source, const SignalId before, const SignalId leaf,
        const SignalId proxy, const SignalId after, const SignalId sink,
        const bool expect_proxy_slice_write = false)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the alias-boundary probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        if (!state.region_graph
            || proxy >= state.signal_container_aggregate_aliases.size()
            || !state.signal_container_aggregate_aliases[proxy]) {
            return { };
        }
        const auto object = *state.signal_container_aggregate_aliases[proxy];
        const auto& graph = *state.region_graph;
        const auto family = std::ranges::find(graph.signal_alias_families(),
            proxy, &RegionSignalAliasFamilyDescriptor::proxy);
        if (family == graph.signal_alias_families().end()
            || !family->complete || !family->proxy_writable
            || family->width != 16U
            || family->leaves.size() != 2U
            || family->leaves[0U].signal != leaf
            || family->leaves[0U].ordinal != 0U
            || family->leaves[0U].offset != 8U
            || family->leaves[0U].width != 8U
            || family->object != object) {
            return { };
        }

        std::optional<AliasProxySliceOwner> slice_owner;
        if (expect_proxy_slice_write) {
            slice_owner = alias_proxy_slice_writer(
                simulation, proxy, leaf, family->leaves[0U].offset);
        }
        const auto leaf_writer_value = expect_proxy_slice_write
            ? (slice_owner
                    ? std::optional { slice_owner->process }
                    : std::nullopt)
            : alias_leaf_writer(simulation, leaf);
        if (!leaf_writer_value) {
            return { };
        }
        const auto leaf_writer = *leaf_writer_value;
        const auto leaf_proxy_offset = family->leaves[0U].offset;
        auto leaf_reader = std::numeric_limits<ProcessId>::max();
        for (ProcessId process = 0U; process < state.processes.size(); ++process) {
            const auto view = state.processes.program_view(process);
            const auto& operations = view.operations();
            bool reads_leaf { };
            bool touches_proxy { };
            for (std::size_t index = 0U; index < operations.size(); ++index) {
                const auto operation = operations.expanded(index);
                if (const auto* read = operation_get_if<ReadSignal>(&operation);
                    read != nullptr && read->signal == leaf) {
                    reads_leaf = true;
                }
                if (const auto* read
                    = operation_get_if<ReadSignal>(&operation);
                    read != nullptr && read->signal == proxy) {
                    touches_proxy = true;
                }
                if (const auto* read
                    = operation_get_if<ReadContainerObject>(&operation);
                    read != nullptr && read->object == object) {
                    touches_proxy = true;
                }
                if (const auto* write
                    = operation_get_if<WriteContainerObject>(&operation);
                    write != nullptr && write->object == object) {
                    touches_proxy = true;
                }
                if (const auto* write
                    = operation_get_if<WriteContainerObjectElement>(&operation);
                    write != nullptr && write->object == object) {
                    touches_proxy = true;
                }
                if (const auto* write
                    = operation_get_if<WriteUpdate>(&operation);
                    write != nullptr && write->signal == proxy) {
                    touches_proxy = true;
                }
                if (const auto* write
                    = operation_get_if<WriteUpdateSlice>(&operation);
                    write != nullptr && write->signal == proxy) {
                    const bool exact_owner_slice = slice_owner
                        && process == slice_owner->process
                        && index == slice_owner->instruction
                        && write->offset == leaf_proxy_offset
                        && write->domain
                            == SignalUpdateDomain::systemverilog_active;
                    touches_proxy = touches_proxy || !exact_owner_slice;
                }
            }
            if (reads_leaf) {
                if (leaf_reader != std::numeric_limits<ProcessId>::max()) {
                    return { };
                }
                leaf_reader = process;
            }
            if (touches_proxy) {
                return { };
            }
        }
        if (leaf_reader == std::numeric_limits<ProcessId>::max()) {
            return { };
        }

        for (std::size_t component = 0U;
             component < state.region_activation_programs.size(); ++component) {
            const auto& optional_program
                = state.region_activation_programs[component];
            if (!optional_program) {
                continue;
            }
            const auto& program = *optional_program;
            const auto& kernel = program.activation_kernel;
            const auto member_for = [&](const ProcessId process) {
                return std::ranges::find(kernel.members, process,
                    &RegionConeKernelMember::process)
                    != kernel.members.end();
            };
            const auto leaf_output = std::ranges::find(kernel.outputs, leaf,
                &RegionConeOutputBinding::signal);
            const auto leaf_boundary_output = std::ranges::find(
                program.boundary_outputs, leaf,
                &RegionConeOutputBinding::signal);
            const auto& leaf_node = graph.signals()[leaf];
            const bool leaf_has_full_leaf_owner
                = std::ranges::count_if(leaf_node.writers,
                    [leaf_writer, expect_proxy_slice_write](
                        const RegionAccess& writer) {
                        return writer.process == leaf_writer
                            && writer.offset == 0U
                            && (expect_proxy_slice_write
                                    ? writer.width == 8U
                                    : (writer.width == 0U
                                        || writer.width == 8U));
                    }) == 1U;
            const bool leaf_has_direct_reader
                = std::ranges::count_if(leaf_node.readers,
                    [leaf_reader](const RegionAccess& reader) {
                        return reader.process == leaf_reader;
                    }) == 1U;
            const bool leaf_is_boundary_input
                = std::ranges::any_of(kernel.inputs,
                    [leaf](const RegionConeKernelInput& input) {
                        return input.signal == leaf && !input.internal
                            && input.width == 8U
                            && input.value_kind == ValueKind::logic4;
                    });
            const bool leaf_has_supported_boundary_output
                = leaf_output != kernel.outputs.end()
                && leaf_output->owner == leaf_writer
                && leaf_output->offset == 0U && leaf_output->width == 8U
                && leaf_output->domain
                    == SignalUpdateDomain::systemverilog_active
                && leaf_output->publication_kind
                    == RegionOutputPublicationKind::update
                && leaf_boundary_output != program.boundary_outputs.end()
                && leaf_boundary_output->owner == leaf_writer
                && leaf_boundary_output->offset == 0U
                && leaf_boundary_output->width == 8U;
            bool has_original_proxy_slice_write { };
            if (expect_proxy_slice_write
                && slice_owner
                && leaf_boundary_output != program.boundary_outputs.end()
                && leaf_output != kernel.outputs.end()) {
                const auto source_program
                    = state.processes.program_view(leaf_writer);
                const auto& source_operations = source_program.operations();
                if (slice_owner->instruction < source_operations.size()) {
                    const auto source_operation
                        = source_operations.expanded(slice_owner->instruction);
                    const auto* source_slice
                        = operation_get_if<WriteUpdateSlice>(
                            &source_operation);
                    has_original_proxy_slice_write = source_slice != nullptr
                        && source_slice->signal == proxy
                        && source_slice->offset == leaf_proxy_offset
                        && source_slice->domain
                            == SignalUpdateDomain::systemverilog_active
                        && leaf_boundary_output->source_instruction
                            == slice_owner->instruction
                        && leaf_output->source_instruction
                            == slice_owner->instruction;
                }
            }
            const auto runtime = component
                    < state.region_frontier_runtime_by_component.size()
                ? state.region_frontier_runtime_by_component[component]
                : nullptr;
            AliasBoundaryKernelShape shape;
            shape.component = component;
            shape.member_count = kernel.members.size();
            shape.leaf_writer = leaf_writer;
            shape.leaf_reader = leaf_reader;
            shape.has_original_proxy_slice_write
                = has_original_proxy_slice_write;
            shape.has_before_internal
                = std::ranges::find(kernel.internal_signals, before)
                    != kernel.internal_signals.end()
                && std::ranges::any_of(program.internal_materializations,
                    [before](const RegionConeOutputBinding& output) {
                        return output.signal == before;
                    });
            shape.has_after_internal
                = std::ranges::find(kernel.internal_signals, after)
                    != kernel.internal_signals.end()
                && std::ranges::any_of(program.internal_materializations,
                    [after](const RegionConeOutputBinding& output) {
                        return output.signal == after;
                    });
            shape.leaf_is_boundary_input = leaf_is_boundary_input;
            shape.leaf_is_boundary_output
                = leaf_boundary_output != program.boundary_outputs.end()
                && std::ranges::find(program.internal_materializations, leaf,
                       &RegionConeOutputBinding::signal)
                    == program.internal_materializations.end()
                && std::ranges::find(kernel.internal_signals, leaf)
                    == kernel.internal_signals.end();
            shape.leaf_has_full_leaf_owner = leaf_has_full_leaf_owner
                && leaf_has_direct_reader
                && leaf_has_supported_boundary_output;
            shape.proxy_is_excluded
                = std::ranges::find(kernel.internal_signals, proxy)
                        == kernel.internal_signals.end()
                && std::ranges::find(kernel.inputs, proxy,
                       &RegionConeKernelInput::signal)
                    == kernel.inputs.end()
                && std::ranges::find(kernel.outputs, proxy,
                       &RegionConeOutputBinding::signal)
                    == kernel.outputs.end()
                && std::ranges::find(program.boundary_outputs, proxy,
                       &RegionConeOutputBinding::signal)
                    == program.boundary_outputs.end();
            shape.has_frontier_runtime = runtime != nullptr
                && runtime->owner == std::addressof(state)
                && runtime->component == component
                && runtime->runtime_generation == state.region_runtime_generation
                && !runtime->invalidated && runtime->backend
                && runtime->backend->executor
                && runtime->backend->executor->step_entry() != nullptr;
            shape.found = member_for(leaf_writer) && member_for(leaf_reader)
                && shape.member_count >= 2U
                && shape.has_before_internal && shape.has_after_internal
                && shape.leaf_is_boundary_input
                && shape.leaf_is_boundary_output
                && shape.leaf_has_full_leaf_owner && shape.proxy_is_excluded
                && (!expect_proxy_slice_write
                    || shape.has_original_proxy_slice_write)
                && shape.has_frontier_runtime
                && shape.member_count >= 4U
                && std::ranges::any_of(kernel.inputs,
                    [source](const RegionConeKernelInput& input) {
                        return input.signal == source && !input.internal
                            && input.width == 8U;
                    })
                && std::ranges::any_of(kernel.outputs,
                    [sink](const RegionConeOutputBinding& output) {
                        return output.signal == sink && output.offset == 0U
                            && output.width == 8U
                            && output.domain
                                == SignalUpdateDomain::systemverilog_active
                            && output.publication_kind
                                == RegionOutputPublicationKind::update;
                    });
            if (shape.found) {
                return shape;
            }
        }
        return { };
    }

    [[nodiscard]] static std::optional<ProcessId> alias_leaf_writer(
        const fsim::app::Simulation& simulation, const SignalId leaf)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        std::optional<ProcessId> owner;
        for (ProcessId process = 0U; process < state.processes.size(); ++process) {
            const auto view = state.processes.program_view(process);
            const auto& operations = view.operations();
            bool writes_leaf { };
            for (std::size_t index = 0U; index < operations.size(); ++index) {
                const auto operation = operations.expanded(index);
                if (const auto* write
                    = operation_get_if<WriteUpdate>(&operation)) {
                    writes_leaf = write->signal == leaf;
                } else if (const auto* slice
                    = operation_get_if<WriteUpdateSlice>(&operation)) {
                    writes_leaf = slice->signal == leaf && slice->offset == 0U;
                }
                if (writes_leaf) {
                    break;
                }
            }
            if (!writes_leaf) {
                continue;
            }
            if (owner) {
                return std::nullopt;
            }
            owner = process;
        }
        return owner;
    }

    [[nodiscard]] static std::optional<AliasProxySliceOwner>
    alias_proxy_slice_writer(const fsim::app::Simulation& simulation,
        const SignalId proxy, const SignalId leaf,
        const std::uint32_t leaf_offset)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter
            || proxy >= application.interpreter->impl_
                    ->signal_container_aggregate_aliases.size()
            || !application.interpreter->impl_
                    ->signal_container_aggregate_aliases[proxy]) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        const auto object
            = *state.signal_container_aggregate_aliases[proxy];
        if (object >= state.container_element_signal_aliases.size()) {
            return std::nullopt;
        }
        if (object >= state.container_objects.size()) {
            return std::nullopt;
        }
        const auto& aliases = state.container_element_signal_aliases[object];
        const auto alias = std::ranges::find(aliases, leaf,
            [](const auto& element) {
                return element ? element->signal
                               : std::numeric_limits<SignalId>::max();
            });
        const auto element_width = state.container_objects[object]
                                       .initial_value.type.element_width;
        if (alias == aliases.end() || !*alias || element_width == 0U
            || aliases.size() > std::numeric_limits<std::uint32_t>::max()
                / element_width) {
            return std::nullopt;
        }
        const auto ordinal = static_cast<std::size_t>(
            std::distance(aliases.begin(), alias));
        const auto mapped_offset = (aliases.size() - ordinal - 1U)
            * static_cast<std::size_t>(element_width);
        if (mapped_offset != leaf_offset) {
            return std::nullopt;
        }

        std::optional<AliasProxySliceOwner> owner;
        for (ProcessId process = 0U; process < state.processes.size(); ++process) {
            const auto view = state.processes.program_view(process);
            const auto& operations = view.operations();
            for (std::size_t index = 0U; index < operations.size(); ++index) {
                const auto operation = operations.expanded(index);
                const auto* const slice
                    = operation_get_if<WriteUpdateSlice>(&operation);
                if (slice == nullptr || slice->signal != proxy) {
                    continue;
                }
                if (owner || slice->offset != leaf_offset
                    || slice->domain
                        != SignalUpdateDomain::systemverilog_active) {
                    return std::nullopt;
                }
                owner = AliasProxySliceOwner { process, index };
            }
        }
        if (!owner) {
            return std::nullopt;
        }
        return owner;
    }

    [[nodiscard]] static std::string alias_proxy_driver_value(
        const fsim::app::Simulation& simulation, const ProcessId owner,
        const SignalId proxy)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the alias-boundary probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        if (proxy >= state.signal_container_aggregate_aliases.size()
            || !state.signal_container_aggregate_aliases[proxy]) {
            throw std::logic_error {
                "the alias-boundary proxy has no aggregate family"
            };
        }
        const auto object = *state.signal_container_aggregate_aliases[proxy];
        const auto& type = state.container_objects.at(object)
                               .initial_value.type;
        const auto& aliases = state.container_element_signal_aliases.at(object);
        auto projection = PackedLogic4(
            state.signals[proxy].initial_value.width(), Logic4::z);
        for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
            if (!aliases[ordinal]) {
                throw std::logic_error {
                    "the alias-boundary family has an absent physical leaf"
                };
            }
            const auto leaf = aliases[ordinal]->signal;
            const auto* const record = state.driver_values.at(leaf).find(owner);
            if (record != nullptr) {
                const auto offset = (aliases.size() - ordinal - 1U)
                    * static_cast<std::size_t>(type.element_width);
                projection.insert_bits(record->value, offset);
            }
        }
        return projection.to_msb_string();
    }

    [[nodiscard]] static bool boundary_v2_route_selection_is_stable(
        const fsim::app::Simulation& simulation,
        const BoundaryV2RouteSelection& selection)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        const auto& state = *application.interpreter->impl_;
        const auto component = selection.component;
        if (state.region_runtime_generation != selection.generation
            || state.region_recertification_pending
            || state.region_recertification_requires_snapshot
            || state.region_forwarding_role_flush_pending_after_discard
            || component >= state.region_activation_programs.size()
            || component >= state.region_local_wave_state_by_component.size()
            || component >= state.region_authoritative_state_by_component.size()
            || component >= state.region_cone_forwarding_backends_by_component.size()
            || component >= state.region_frontier_backends_by_component.size()
            || component >= state.region_frontier_runtime_by_component.size()
            || !state.region_activation_programs[component]
            || std::addressof(*state.region_activation_programs[component])
                != selection.activation_program
            || state.region_local_wave_state_by_component[component].get()
                != selection.local_state
            || state.region_authoritative_state_by_component[component].get()
                != selection.authoritative_state
            || state.region_cone_forwarding_backends_by_component[component]
            || state.region_frontier_runtime_by_component[component].get()
                != selection.frontier_runtime
            || state.region_frontier_backends_by_component[component].get()
                != selection.frontier_backend
            || state.region_frontier_backends_by_component[component]
                    ->executor.get() != selection.frontier_executor
            || !selection.forwarding_entry) {
            return false;
        }
        const auto& runtime
            = *state.region_frontier_runtime_by_component[component];
        const auto& local
            = *state.region_local_wave_state_by_component[component];
        using ForwardingVector = decltype(
            state.region_cone_forwarding_backends_by_component);
        using ForwardingEntry
            = typename ForwardingVector::value_type::element_type;
        const auto retained_forwarding
            = std::static_pointer_cast<ForwardingEntry>(
                selection.forwarding_entry);
        if (runtime.owner != &state || runtime.component != component
            || runtime.runtime_generation != selection.generation
            || runtime.invalidated
            || runtime.in_use.test(std::memory_order_acquire)
            || !retained_forwarding
            || retained_forwarding->in_use.test(std::memory_order_acquire)
            || runtime.authoritative_state
                != state.region_authoritative_state_by_component[component]
            || runtime.backend
                != state.region_frontier_backends_by_component[component]
            || local.generation != selection.generation
            || !local.forwarding_results) {
            return false;
        }
        const auto& bank = *local.forwarding_results;
        return !bank.active && !bank.role_journal_enabled
            && !bank.private_epoch_retired
            && bank.remaining_members == 0U
            && bank.applied_role_mutations.empty()
            && bank.applied_role_metadata.empty();
    }

    [[nodiscard]] static bool restore_boundary_forwarding_route(
        fsim::app::Simulation& simulation,
        const BoundaryV2RouteSelection& selection) noexcept
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        auto& state = *application.interpreter->impl_;
        const auto component = selection.component;
        if (state.region_runtime_generation != selection.generation
            || component >= state.region_cone_forwarding_backends_by_component.size()
            || state.region_cone_forwarding_backends_by_component[component]
            || !selection.forwarding_entry) {
            return false;
        }
        using ForwardingVector = decltype(
            state.region_cone_forwarding_backends_by_component);
        using ForwardingEntry
            = typename ForwardingVector::value_type::element_type;
        state.region_cone_forwarding_backends_by_component[component]
            = std::static_pointer_cast<ForwardingEntry>(
                selection.forwarding_entry);
        return true;
    }

    [[nodiscard]] static std::uint64_t forwarding_member_consumptions(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the random DAG probe has no interpreter"
            };
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_member_consumptions;
    }

    [[nodiscard]] static std::uint64_t private_parent_dispatches(
        const fsim::app::Simulation& simulation)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the random DAG probe has no interpreter"
            };
        }
        return application.interpreter->impl_
            ->systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
    }

    [[nodiscard]] static Scheduler::SafePointHookToken
    install_private_role_cut_hook(fsim::app::Simulation& simulation,
        const std::span<const SignalId> signals, PrivateRoleCut& cut)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the random DAG probe has no interpreter"
            };
        }
        return application.interpreter->scheduler().add_safe_point_hook(
            [&simulation, signals, &cut](Scheduler& scheduler,
                const SchedulerPhase phase) {
                if (phase != SchedulerPhase::active || cut.reached) {
                    return;
                }
                // This friend-side probe reads the journal and A4 backing
                // directly; it must not trigger public observation here.
                if (capture_private_role_cut(
                        simulation, scheduler, signals, cut)) {
                    scheduler.request_stop();
                }
            });
    }

    static void remove_private_role_cut_hook(
        fsim::app::Simulation& simulation,
        const Scheduler::SafePointHookToken token) noexcept
    {
        if (!simulation.impl_ || !simulation.impl_->interpreter) {
            return;
        }
        simulation.impl_->interpreter->scheduler().remove_safe_point_hook(
            token);
    }

    [[nodiscard]] static PrivateJournalState private_journal_state(
        const fsim::app::Simulation& simulation,
        const std::size_t component)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the random DAG probe has no interpreter"
            };
        }
        const auto& state = *application.interpreter->impl_;
        if (component >= state.region_local_wave_state_by_component.size()) {
            return { };
        }
        const auto& local = state.region_local_wave_state_by_component[
            component];
        if (!local || !local->forwarding_results) {
            return { };
        }
        const auto& bank = *local->forwarding_results;
        return { bank.applied_role_mutations.size(),
            bank.applied_role_metadata.size(), bank.role_journal_enabled,
            bank.private_epoch_retired };
    }

    [[nodiscard]] static std::optional<PendingChildReceipt>
    pending_child_receipt(const fsim::app::Simulation& simulation,
        const ProcessId process)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return std::nullopt;
        }
        const auto& state = *application.interpreter->impl_;
        if (process >= state.processes.size()
            || process >= state.region_readiness_queued_by_process.size()) {
            return std::nullopt;
        }
        const auto& process_state = state.processes[process];
        const auto& queued = state.region_readiness_queued_by_process[process];
        if (!process_state.queued || !queued.key_valid) {
            return std::nullopt;
        }
        return PendingChildReceipt { process, queued.component, queued.member,
            queued.generation, queued.queued_key };
    }

    [[nodiscard]] static SignalSnapshot snapshot(
        const fsim::app::Simulation& simulation, const SignalId signal,
        const bool include_applied_role_overlay = true,
        const bool allow_signal_alias = false)
    {
        const auto& application = *simulation.impl_;
        if (!application.interpreter) {
            throw std::logic_error {
                "the random DAG probe has no interpreter"
            };
        }
        auto& state = *application.interpreter->impl_;
        if (signal >= state.signals.size()
            || (!allow_signal_alias
                && state.has_container_signal_alias(signal))) {
            throw std::logic_error {
                "the random DAG probe requires direct signals"
            };
        }

        SignalSnapshot result;
        result.signal = signal;
        const auto width = state.signals[signal].initial_value.width();
        const AuthoritativeSignalPlanes* authoritative_values { };
        if (width > 64U
            && signal < state.region_authoritative_component_by_signal.size()) {
            const auto component
                = state.region_authoritative_component_by_signal[signal];
            if (component < state.region_authoritative_state_by_component.size()
                && state.region_authoritative_state_by_component[component]
                && state.region_authoritative_state_by_component[component]
                       ->values().packed_signal_slots_bound(signal)) {
                authoritative_values
                    = &state.region_authoritative_state_by_component[component]
                           ->values();
            }
        }

        const bool materialization_pending
            = signal < state.direct_signal_materialization_pending.size()
            && state.direct_signal_materialization_pending[signal] != 0U;
        PackedLogic4 current;
        PackedLogic4 last;
        PackedLogic4 stored;
        const bool aggregate_proxy
            = signal < state.signal_container_aggregate_aliases.size()
            && state.signal_container_aggregate_aliases[signal].has_value();
        if (aggregate_proxy) {
            const auto object
                = *state.signal_container_aggregate_aliases[signal];
            const auto& aliases
                = state.container_element_signal_aliases.at(object);
            const auto& type
                = state.container_objects.at(object).initial_value.type;
            const auto element_width
                = static_cast<std::size_t>(type.element_width);
            const auto projection_width = width;
            if (element_width == 0U || aliases.empty()
                || aliases.size()
                    > std::numeric_limits<std::size_t>::max() / element_width
                || aliases.size() * element_width != projection_width) {
                throw std::logic_error {
                    "the aggregate proxy snapshot has an invalid fixed shape"
                };
            }
            current = state.aggregate_signal_current_value(signal);
            last = state.aggregate_signal_last_value(signal);
            const auto stored_revision
                = state.container_aggregate_stored_revisions.at(object);
            const auto projection_revision
                = state.aggregate_signal_stored_projection_revisions.at(signal);
            const auto& stored_projection
                = state.aggregate_signal_stored_projection.at(signal);
            const bool stored_projection_current
                = stored_projection.has_value()
                && projection_revision == stored_revision;
            stored = stored_projection_current
                ? *stored_projection
                : PackedLogic4(projection_width, Logic4::z);
            for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
                if (!aliases[ordinal]) {
                    throw std::logic_error {
                        "the aggregate proxy snapshot has a missing physical leaf"
                    };
                }
                if (!stored_projection_current) {
                    const auto offset
                        = (aliases.size() - ordinal - 1U) * element_width;
                    stored.insert_bits(
                        state.driven_values.at(aliases[ordinal]->signal), offset);
                }
            }
        } else if (authoritative_values != nullptr) {
            current = authoritative_values->current(signal);
            last = authoritative_values->previous(signal);
            stored = authoritative_values->stored(signal);
        } else if (materialization_pending) {
            if (state.signals[signal].value_kind != ValueKind::logic4
                || width == 0U || width > 64U
                || signal >= state.direct_signal_aval.size()
                || signal >= state.direct_signal_bval.size()
                || signal >= state.direct_signal_last_aval.size()
                || signal >= state.direct_signal_last_bval.size()) {
                throw std::logic_error {
                    "the random DAG snapshot cannot read a pending direct plane"
                };
            }
            current = PackedLogic4::from_aval_bval(width,
                state.direct_signal_aval[signal],
                state.direct_signal_bval[signal]);
            last = PackedLogic4::from_aval_bval(width,
                state.direct_signal_last_aval[signal],
                state.direct_signal_last_bval[signal]);
            stored = current;
        } else {
            current = state.signals[signal].initial_value;
            last = state.signal_last_values.at(signal);
            stored = state.driven_values.at(signal);
        }
        result.current = current.to_msb_string();
        result.last = last.to_msb_string();
        result.stored = stored.to_msb_string();

        state.driver_values.at(signal).for_each_in_process_order(
            [&](const DriverRecord& record) {
                const auto value = [&] {
                    if (authoritative_values != nullptr) {
                        return authoritative_values->owner_value(
                            signal, record.process);
                    }
                    if (materialization_pending
                        && state.direct_single_driver_record(signal) == &record) {
                        return current;
                    }
                    if (state.owned_driver_active(signal)) {
                        return state.owned_driver_value(record.process, signal);
                    }
                    return record.value;
                }();
                result.raw_drivers.push_back({ record.process,
                    value.to_msb_string(), record.strength });
            });
        // A2 may defer only A4 role publication after the original callback
        // has already committed event/transaction metadata. Keep ordinary
        // snapshots logical by overlaying the exact applied journal row. A
        // private-cut probe can opt out to inspect the still-committed A4
        // roles before public observation flushes that row.
        if (include_applied_role_overlay
            && signal < state.region_authoritative_component_by_signal.size()) {
            const auto component
                = state.region_authoritative_component_by_signal[signal];
            if (component < state.region_local_wave_state_by_component.size()) {
                const auto local
                    = state.region_local_wave_state_by_component[component];
                if (local && local->forwarding_results) {
                    const auto& bank = *local->forwarding_results;
                    if (bank.applied_role_mutations.size()
                            != bank.applied_role_metadata.size()) {
                        throw std::logic_error {
                            "the random DAG A2 journal rows and metadata disagree"
                        };
                    }
                    std::optional<std::size_t> row_index;
                    for (std::size_t index = 0U;
                         index < bank.applied_role_metadata.size(); ++index) {
                        const auto& metadata
                            = bank.applied_role_metadata[index];
                        if (metadata.callback_order == 0U
                            || (index != 0U
                                && bank.applied_role_metadata[index - 1U]
                                       .callback_order
                                    >= metadata.callback_order)) {
                            throw std::logic_error {
                                "the random DAG A2 rows are not in exact callback order"
                            };
                        }
                        for (std::size_t previous = 0U; previous < index;
                             ++previous) {
                            if (bank.applied_role_metadata[previous].signal
                                == metadata.signal) {
                                throw std::logic_error {
                                    "the random DAG A2 journal repeats one signal"
                                };
                            }
                        }
                        if (metadata.signal == signal) {
                            row_index = index;
                        }
                    }
                    if (row_index) {
                        if ((!bank.role_journal_enabled
                                && !bank.private_epoch_retired)
                            || component
                                >= state.region_authoritative_state_by_component.size()
                            || component >= state.region_activation_programs.size()
                            || !state.region_activation_programs[component]
                            || !state.region_activation_programs[component]
                                    ->forwarding_kernel
                            || local->generation == 0U
                            || local->generation != bank.runtime_generation
                            || local->generation != state.region_runtime_generation
                            || signal >= state.signal_events.size()
                            || signal >= state.signal_transactions.size()
                            || signal >= state.signal_event_scheduling_stamps.size()
                            || signal >= state.signal_value_revisions.size()
                            || signal >= state.direct_signal_materialization_pending.size()
                            || state.direct_signal_materialization_pending[signal] != 0U
                            || !state.region_authoritative_state_by_component[component]
                            || !state.region_authoritative_state_by_component[component]
                                    ->valid()
                            || state.region_authoritative_state_by_component[component]
                                    ->generation() != local->generation) {
                            throw std::logic_error {
                                "the random DAG A2 journal is not bound to its live component"
                            };
                        }
                        const auto& mutation
                            = bank.applied_role_mutations[*row_index];
                        const auto& metadata
                            = bank.applied_role_metadata[*row_index];
                        const auto& program
                            = *state.region_activation_programs[component];
                        const auto& forwarding = *program.forwarding_kernel;
                        const auto& outputs = program.activation_kernel.outputs;
                        const auto& values
                            = state.region_authoritative_state_by_component[component]
                                  ->values();
                        const auto& layout = values.layout();
                        if (!values.requires_prewrite_unbind()
                            || !values.packed_slots_bound()
                            || !values.packed_signal_slots_bound(signal)
                            || !values.packed_owner_slot_bound(
                                signal, metadata.owner)) {
                            throw std::logic_error {
                                "the random DAG A2 row lacks its bound A4 signal and owner slots"
                            };
                        }
                        if (!layout.contains(signal)) {
                            throw std::logic_error {
                                "the random DAG A2 signal is absent from its A4 layout"
                            };
                        }
                        const auto& signal_layout = layout.signal(signal);
                        const auto owners = layout.owners(signal);
                        if (width > std::numeric_limits<std::uint32_t>::max()) {
                            throw std::logic_error {
                                "the random DAG A2 signal width exceeds its packed role decoder"
                            };
                        }
                        const auto signal_width
                            = static_cast<std::uint32_t>(width);
                        const auto word_count
                            = static_cast<std::size_t>(signal_width / 64U)
                            + static_cast<std::size_t>(signal_width % 64U != 0U);
                        const auto& live_stamp
                            = state.signal_event_scheduling_stamps[signal];
                        const auto& expected_stamp
                            = metadata.expected_event_stamp;
                        // Private A2 outputs exclude owned-driver composites;
                        // their separate `owned_raw` snapshot remains current.
                        if (metadata.signal != signal
                            || metadata.output_index >= outputs.size()
                            || metadata.owner >= state.processes.size()
                            || metadata.expected_signal_event
                                != state.signal_events[signal]
                            || metadata.expected_transaction
                                != state.signal_transactions[signal]
                            || metadata.expected_value_revision
                                != state.signal_value_revisions[signal]
                            || expected_stamp.origin.process_domain
                                != live_stamp.origin.process_domain
                            || expected_stamp.origin.phase != live_stamp.origin.phase
                            || expected_stamp.systemverilog_round
                                != live_stamp.systemverilog_round
                            || metadata.origin.process_domain
                                != ProcessSchedulingDomain::systemverilog
                            || metadata.origin.phase != SchedulerPhase::active
                            || metadata.callback_systemverilog_round == 0U
                            || mutation.signal != signal
                            || signal_layout.storage_class
                                != SignalDriverStorageClass::single_owner
                            || signal_layout.value_kind != ValueKind::logic4
                            || signal_layout.width != signal_width
                            || signal_layout.word_count != word_count
                            || owners.size() != 1U
                            || owners.front().process != metadata.owner
                            || mutation.owner_index != signal_layout.first_owner
                            || state.owned_driver_active(signal)
                            || (!mutation.has_owner
                                && !mutation.owner_is_stored_alias)
                            || !mutation.whole_signal
                            || !mutation.any_state_changed
                            || width == 0U || mutation.words.size() != word_count
                            || outputs[metadata.output_index].signal != signal
                            || outputs[metadata.output_index].owner
                                != metadata.owner
                            || outputs[metadata.output_index].offset != 0U
                            || outputs[metadata.output_index].width != signal_width
                            || outputs[metadata.output_index].value_kind
                                != ValueKind::logic4
                            || outputs[metadata.output_index].domain
                                != SignalUpdateDomain::systemverilog_active
                            || outputs[metadata.output_index].update_kind
                                != RegionUpdateKind::systemverilog_active
                            || std::ranges::find(forwarding.internal_signals,
                                   signal)
                                == forwarding.internal_signals.end()) {
                            throw std::logic_error {
                                "the random DAG A2 row does not match its exact "
                                "output and live metadata"
                            };
                        }
                        for (std::size_t word_index = 0U;
                             word_index < mutation.words.size(); ++word_index) {
                            if (mutation.words[word_index].signal_word
                                != signal_layout.first_value_word + word_index) {
                                throw std::logic_error {
                                    "the random DAG A2 row does not follow its A4 "
                                    "signal-word layout"
                                };
                            }
                        }
                        result.current = decode_role(mutation, signal_width,
                            PreparedRole::new_current);
                        result.last = decode_role(mutation, signal_width,
                            mutation.any_current_changed
                                ? PreparedRole::old_current
                                : PreparedRole::old_previous);
                        result.stored = decode_role(mutation, signal_width,
                            PreparedRole::new_stored);
                        const auto owner_value = decode_role(mutation, signal_width,
                            PreparedRole::new_owner);
                        const auto owner_record_count
                            = static_cast<std::size_t>(std::ranges::count(
                                result.raw_drivers, metadata.owner,
                                &RawDriver::process));
                        if (owner_record_count > 1U
                            || (mutation.has_owner
                                && owner_record_count != 1U)) {
                            throw std::logic_error {
                                "the random DAG A2 owner row lacks one exact raw driver"
                            };
                        }
                        if (owner_record_count == 1U) {
                            const auto owner_record = std::ranges::find(
                                result.raw_drivers, metadata.owner,
                                &RawDriver::process);
                            owner_record->value = owner_value;
                        }
                    }
                }
            }
        }
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
        const auto& stamp = state.signal_event_scheduling_stamps.at(signal);
        result.event_domain = stamp.origin.process_domain;
        result.event_phase = stamp.origin.phase;
        result.systemverilog_round = stamp.systemverilog_round;
        if (signal < state.signal_value_revisions.size()) {
            result.value_revision = state.signal_value_revisions[signal];
        }
        result.now = state.scheduler.now();
        result.delta = state.scheduler.delta();
        return result;
    }

private:
    [[nodiscard]] static const std::array<std::uint64_t, 4U>&
    role_words(const AuthoritativeSignalPlanes::PreparedWord& word,
        const PreparedRole role)
    {
        switch (role) {
        case PreparedRole::old_previous:
            return word.old_previous;
        case PreparedRole::old_current:
            return word.old_current;
        case PreparedRole::old_stored:
            return word.old_stored;
        case PreparedRole::old_owner:
            return word.old_owner;
        case PreparedRole::new_current:
            return word.new_current;
        case PreparedRole::new_stored:
            return word.new_stored;
        case PreparedRole::new_owner:
            return word.new_owner;
        }
        throw std::logic_error { "unknown A4 prepared role" };
    }

    [[nodiscard]] static std::string decode_role(
        const AuthoritativeSignalPlanes::PreparedMutation& mutation,
        const std::uint32_t width, const PreparedRole role)
    {
        const auto word_count = static_cast<std::size_t>(width / 64U)
            + static_cast<std::size_t>(width % 64U != 0U);
        if (width == 0U || mutation.words.size() != word_count) {
            throw std::logic_error {
                "the random DAG A2 row has an invalid A4 word extent"
            };
        }
        PackedLogic4 value(width, Logic4::zero);
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            const auto& planes = role_words(mutation.words[bit / 64U], role);
            const auto mask = UINT64_C(1) << (bit % 64U);
            const bool aval = (planes[0U] & mask) != 0U;
            const bool bval = (planes[1U] & mask) != 0U;
            const auto logic = !bval
                ? (aval ? Logic4::one : Logic4::zero)
                : (aval ? Logic4::x : Logic4::z);
            value.set(bit, logic);
        }
        return value.to_msb_string();
    }

    [[nodiscard]] static bool capture_private_role_cut(
        fsim::app::Simulation& simulation, Scheduler& scheduler,
        const std::span<const SignalId> signals, PrivateRoleCut& cut)
    {
        auto& application = *simulation.impl_;
        if (!application.interpreter) {
            return false;
        }
        auto& state = *application.interpreter->impl_;
        const auto component_count = std::min(
            state.region_local_wave_state_by_component.size(),
            state.region_authoritative_state_by_component.size());
        for (std::size_t component = 0U;
             component < component_count; ++component) {
            const auto local
                = state.region_local_wave_state_by_component[component];
            const auto authoritative
                = state.region_authoritative_state_by_component[component];
            if (!local || !local->forwarding_results || !authoritative
                || !authoritative->valid()
                || component >= state.region_activation_programs.size()
                || !state.region_activation_programs[component]) {
                continue;
            }
            const auto& program
                = *state.region_activation_programs[component];
            if (!program.forwarding_kernel) {
                continue;
            }
            const auto& forwarding = *program.forwarding_kernel;
            const auto& bank = *local->forwarding_results;
            if (!bank.role_journal_enabled || bank.private_epoch_retired
                || bank.applied_role_mutations.empty()
                || bank.applied_role_mutations.size()
                    != bank.applied_role_metadata.size()) {
                continue;
            }

            for (std::size_t row_index = 0U;
                 row_index < bank.applied_role_mutations.size(); ++row_index) {
                const auto& mutation = bank.applied_role_mutations[row_index];
                const auto& metadata = bank.applied_role_metadata[row_index];
                const auto signal = metadata.signal;
                if (signal >= state.signals.size()
                    || signal >= state.region_authoritative_component_by_signal.size()
                    || state.region_authoritative_component_by_signal[signal]
                        != component
                    || mutation.signal != signal || !mutation.whole_signal
                    || (!mutation.has_owner && !mutation.owner_is_stored_alias)
                    || !mutation.any_current_changed
                    || metadata.origin.process_domain
                        != ProcessSchedulingDomain::systemverilog
                    || metadata.origin.phase != SchedulerPhase::active
                    || metadata.callback_time != scheduler.now()
                    || metadata.callback_delta != scheduler.delta()
                    || metadata.callback_systemverilog_round
                        != scheduler.systemverilog_round()
                    || metadata.output_index
                        >= program.activation_kernel.outputs.size()
                    || std::ranges::find(program.activation_kernel.internal_signals,
                           signal)
                        == program.activation_kernel.internal_signals.end()) {
                    continue;
                }
                const auto& output =
                    program.activation_kernel.outputs[metadata.output_index];
                if (output.signal != signal || output.owner != metadata.owner
                    || output.offset != 0U
                    || output.width
                        != state.signals[signal].initial_value.width()
                    || output.value_kind != ValueKind::logic4) {
                    continue;
                }
                const auto& values = authoritative->values();
                if (!values.layout().contains(signal)) {
                    continue;
                }
                const auto& signal_layout = values.layout().signal(signal);
                const auto owners = values.layout().owners(signal);
                if (signal_layout.storage_class
                        != SignalDriverStorageClass::single_owner
                    || signal_layout.width != output.width
                    || signal_layout.value_kind != ValueKind::logic4
                    || signal_layout.word_count != mutation.words.size()
                    || owners.size() != 1U
                    || owners.front().process != metadata.owner
                    || mutation.owner_index != signal_layout.first_owner) {
                    continue;
                }
                bool words_match_layout = true;
                for (std::size_t word_index = 0U;
                     word_index < mutation.words.size(); ++word_index) {
                    words_match_layout = words_match_layout
                        && mutation.words[word_index].signal_word
                            == signal_layout.first_value_word + word_index;
                }
                if (!words_match_layout) {
                    continue;
                }
                const auto& live_stamp
                    = state.signal_event_scheduling_stamps[signal];
                if (metadata.expected_signal_event
                        != state.signal_events[signal]
                    || metadata.expected_transaction
                        != state.signal_transactions[signal]
                    || metadata.expected_value_revision
                        != state.signal_value_revisions[signal]
                    || metadata.expected_event_stamp.origin.process_domain
                        != metadata.origin.process_domain
                    || metadata.expected_event_stamp.origin.phase
                        != metadata.origin.phase
                    || metadata.expected_event_stamp.systemverilog_round
                        != metadata.callback_systemverilog_round
                    || live_stamp.origin.process_domain
                        != metadata.origin.process_domain
                    || live_stamp.origin.phase != metadata.origin.phase
                    || live_stamp.systemverilog_round
                        != metadata.callback_systemverilog_round) {
                    continue;
                }

                const auto parent = std::ranges::find(forwarding.members,
                    metadata.owner, &RegionConeForwardingMember::process);
                if (parent == forwarding.members.end()) {
                    continue;
                }
                const auto parent_index = static_cast<std::size_t>(
                    parent - forwarding.members.begin());
                std::optional<PendingChildReceipt> pending_child;
                for (std::size_t child_index = 0U;
                     child_index < forwarding.members.size(); ++child_index) {
                    if (child_index == parent_index) {
                        continue;
                    }
                    const auto& child = forwarding.members[child_index];
                    if (child.dependency_begin > forwarding.dependencies.size()
                        || child.dependency_count
                            > forwarding.dependencies.size()
                                - child.dependency_begin) {
                        continue;
                    }
                    const auto dependencies
                        = std::span<const RegionConeForwardingDependency> {
                            forwarding.dependencies }
                              .subspan(child.dependency_begin,
                                  child.dependency_count);
                    if (std::ranges::none_of(dependencies,
                            [&](const auto& dependency) {
                                return dependency.signal == signal
                                    && dependency.writer_member_index
                                        == parent_index;
                            })) {
                        continue;
                    }
                    if (child.process >= state.processes.size()
                        || child.process
                            >= state.region_readiness_queued_by_process.size()) {
                        continue;
                    }
                    const auto& process_state = state.processes[child.process];
                    const auto& queued
                        = state.region_readiness_queued_by_process[child.process];
                    const auto& key = queued.queued_key;
                    if (!process_state.queued || !queued.key_valid
                        || queued.component != component
                        || queued.member != child_index
                        || queued.generation != local->generation
                        || key.time != scheduler.now()
                        || key.systemverilog_round == 0U
                        || key.stable_order != child.process
                        || key.process_domain != static_cast<std::uint32_t>(
                            ProcessSchedulingDomain::systemverilog)
                        || key.phase != static_cast<std::uint32_t>(
                            SchedulerPhase::active)) {
                        continue;
                    }
                    pending_child = PendingChildReceipt { child.process,
                        queued.component, child_index, queued.generation, key };
                    break;
                }
                if (!pending_child) {
                    continue;
                }

                const auto measured_width
                    = state.signals[signal].initial_value.width();
                if (measured_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                const auto signal_width
                    = static_cast<std::uint32_t>(measured_width);
                const auto before_value = decode_role(mutation, signal_width,
                    PreparedRole::old_current);
                const auto before_last = decode_role(mutation, signal_width,
                    PreparedRole::old_previous);
                const auto before_stored = decode_role(mutation, signal_width,
                    PreparedRole::old_stored);
                const auto before_owner = decode_role(mutation, signal_width,
                    PreparedRole::old_owner);
                const auto next_value = decode_role(mutation, signal_width,
                    PreparedRole::new_current);
                const auto next_stored = decode_role(mutation, signal_width,
                    PreparedRole::new_stored);
                const auto next_owner = decode_role(mutation, signal_width,
                    PreparedRole::new_owner);
                if (next_value == before_value) {
                    continue;
                }

                PrivateRoleCut observed;
                observed.component = component;
                observed.applied_rows = bank.applied_role_mutations.size();
                observed.metadata_rows = bank.applied_role_metadata.size();
                observed.signal = signal;
                observed.owner = metadata.owner;
                observed.output_index = metadata.output_index;
                observed.owner_is_stored_alias
                    = mutation.owner_is_stored_alias;
                observed.journal_enabled = bank.role_journal_enabled;
                observed.private_epoch_retired = bank.private_epoch_retired;
                observed.child = *pending_child;
                observed.expected_current = next_value;
                observed.expected_last = before_value;
                observed.expected_stored = next_stored;
                observed.expected_owner = next_owner;
                observed.event = state.signal_events[signal];
                observed.transaction = state.signal_transactions[signal];
                observed.event_domain = live_stamp.origin.process_domain;
                observed.event_phase = live_stamp.origin.phase;
                observed.event_round = live_stamp.systemverilog_round;
                observed.value_revision
                    = state.signal_value_revisions[signal];
                observed.callback_time = metadata.callback_time;
                observed.callback_delta = metadata.callback_delta;
                observed.callback_round
                    = metadata.callback_systemverilog_round;
                observed.callback_order = metadata.callback_order;
                if (std::ranges::find(signals, signal) == signals.end()) {
                    continue;
                }
                observed.before = snapshot(simulation, signal, false);
                if (observed.before.current != before_value
                    || observed.before.last != before_last
                    || observed.before.stored != before_stored) {
                    continue;
                }
                const auto raw_owner = std::ranges::find(
                    observed.before.raw_drivers, metadata.owner,
                    &RawDriver::process);
                observed.owner_record_present
                    = raw_owner != observed.before.raw_drivers.end();
                if (observed.owner_record_present
                    && raw_owner->value != before_owner) {
                    continue;
                }
                if (!observed.owner_record_present
                    && (!mutation.owner_is_stored_alias
                        || before_owner != before_stored)) {
                    continue;
                }
                observed.reached = true;
                cut = std::move(observed);
                return true;
            }
        }
        return false;
    }
};

} // namespace fsim::runtime::simir

namespace {

using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::app::SystemVerilogVpiRuntimeUpdates;
using fsim::project::Optimization;
using fsim::runtime::RunStatus;
using fsim::runtime::Scheduler;
using fsim::runtime::SimulationTick;
using fsim::runtime::PackedLogic4;
using fsim::runtime::simir::ContainerAggregateSignalAlias;
using fsim::runtime::simir::ContainerElementSignalAlias;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::Process;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::ProcessSchedulingDomain;
using fsim::runtime::simir::ReadSignal;
using fsim::runtime::simir::RegisterId;
using fsim::runtime::simir::SignalId;
using fsim::runtime::simir::SignalUpdateDomain;
using fsim::runtime::simir::ValueKind;
using fsim::runtime::simir::WriteUpdate;
using fsim::runtime::simir::WriteUpdateSlice;
using Snapshot = NativeRegionAllocationTestAccess::SignalSnapshot;

class ScopedBoundaryV2RouteSelection {
public:
    explicit ScopedBoundaryV2RouteSelection(Simulation& simulation)
        : simulation_(simulation)
    {
    }

    ScopedBoundaryV2RouteSelection(
        const ScopedBoundaryV2RouteSelection&) = delete;
    ScopedBoundaryV2RouteSelection& operator=(
        const ScopedBoundaryV2RouteSelection&) = delete;

    ~ScopedBoundaryV2RouteSelection()
    {
        (void)restore();
    }

    [[nodiscard]] bool select(
        const NativeRegionAllocationTestAccess::BoundaryRangeKernelShape& shape)
    {
        return select_component(shape.found, shape.component);
    }

    [[nodiscard]] bool select_component(
        const bool shape_found, const std::size_t component)
    {
        if (selection_) {
            return false;
        }
        selection_ = NativeRegionAllocationTestAccess::
            select_v2_only_component_route(
                simulation_, component, shape_found);
        return selection_.has_value();
    }

    [[nodiscard]] bool remains_selected() const
    {
        return selection_
            && NativeRegionAllocationTestAccess::
                boundary_v2_route_selection_is_stable(
                    simulation_, *selection_);
    }

    [[nodiscard]] bool restore() noexcept
    {
        if (!selection_) {
            return true;
        }
        const auto restored = NativeRegionAllocationTestAccess::
            restore_boundary_forwarding_route(simulation_, *selection_);
        selection_.reset();
        return restored;
    }

private:
    Simulation& simulation_;
    std::optional<
        NativeRegionAllocationTestAccess::BoundaryV2RouteSelection> selection_;
};

constexpr std::size_t network_count { 3U };
constexpr std::size_t input_count { 4U };
constexpr std::size_t node_count { 12U };
constexpr std::size_t sink_count { 2U };
constexpr std::size_t stimulus_phase_count { 9U };
constexpr std::array<std::uint32_t, network_count> network_seeds {
    0x13579bdfU, 0x2468ace1U, 0x5a17c3e9U };

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
    ScopedEnvironment(const char* const name, const char* const value)
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
    std::optional<std::string> previous_;
};

struct Node {
    char operation { '^' };
    std::uint32_t left { };
    std::uint32_t right { };
};

struct Network {
    std::uint32_t seed { };
    std::vector<Node> nodes;
};

struct InputBinding {
    std::size_t signal_index { };
    std::size_t network { };
    std::size_t port { };
};

struct DagDesign {
    std::array<Network, network_count> networks;
    std::vector<std::string> signal_names;
    std::vector<InputBinding> input_bindings;
    std::vector<std::string> sink_names;
};

[[nodiscard]] std::uint32_t mix_random(
    const std::uint32_t seed, const std::uint32_t stream) noexcept
{
    auto value = seed + 0x9e3779b9U * (stream + 1U);
    value ^= value >> 16U;
    value *= 0x85ebca6bU;
    value ^= value >> 13U;
    value *= 0xc2b2ae35U;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] Network make_network(const std::uint32_t seed)
{
    Network network;
    network.seed = seed;
    // The fixed prefix gives every network shared fanout and reconvergence;
    // its seeded suffix varies operator and dependency choices.
    network.nodes = {
        { '^', 0U, 1U },
        { '&', 2U, 3U },
        { '|', 4U, 5U },
        { '~', 4U, 0U },
        { '^', 6U, 7U },
        { '&', 6U, 8U },
        { '|', 7U, 9U },
        { '^', 4U, 10U },
    };

    constexpr std::array<char, 4U> operations { '&', '|', '^', '~' };
    while (network.nodes.size() < node_count) {
        const auto node_index = static_cast<std::uint32_t>(
            network.nodes.size());
        const auto available = static_cast<std::uint32_t>(
            input_count + network.nodes.size());
        const auto operation_index = static_cast<std::size_t>(
            mix_random(seed, node_index * 3U) % operations.size());
        const auto operation = operations[operation_index];
        const auto left = mix_random(seed, node_index * 3U + 1U) % available;
        auto right = mix_random(seed, node_index * 3U + 2U) % available;
        if (right == left && available > 1U) {
            right = (right + 1U) % available;
        }
        network.nodes.push_back({ operation, left, right });
    }

    std::size_t first_node_uses { };
    std::size_t reconverged_node_uses { };
    for (const auto& node : network.nodes) {
        first_node_uses += node.left == 4U ? 1U : 0U;
        reconverged_node_uses += node.left == 6U ? 1U : 0U;
        if (node.operation != '~') {
            first_node_uses += node.right == 4U ? 1U : 0U;
            reconverged_node_uses += node.right == 6U ? 1U : 0U;
        }
    }
    require(first_node_uses >= 3U && reconverged_node_uses >= 2U,
        "every generated network retains the shared fanout and reconvergent backbone");
    return network;
}

[[nodiscard]] std::string topology_signature(const Network& network)
{
    std::string signature;
    for (const auto& node : network.nodes) {
        signature.push_back(node.operation);
        signature += std::to_string(node.left);
        signature.push_back(':');
        signature += std::to_string(node.right);
        signature.push_back(';');
    }
    return signature;
}

[[nodiscard]] std::string input_name(
    const std::size_t network, const std::size_t input)
{
    return "in_" + std::to_string(network) + "_" + std::to_string(input);
}

[[nodiscard]] std::string node_name(
    const std::size_t network, const std::size_t node)
{
    return "node_" + std::to_string(network) + "_" + std::to_string(node);
}

[[nodiscard]] std::string sink_name(
    const std::size_t network, const std::size_t sink)
{
    return "sink_" + std::to_string(network) + "_" + std::to_string(sink);
}

[[nodiscard]] std::string value_name(
    const std::size_t network, const std::uint32_t reference)
{
    if (reference < input_count) {
        return input_name(network, reference);
    }
    return node_name(network, reference - static_cast<std::uint32_t>(input_count));
}

[[nodiscard]] DagDesign make_design()
{
    DagDesign design;
    for (std::size_t network = 0U; network < network_count; ++network) {
        design.networks[network] = make_network(network_seeds[network]);
        for (std::size_t port = 0U; port < input_count; ++port) {
            design.signal_names.push_back(input_name(network, port));
            design.input_bindings.push_back({
                design.signal_names.size() - 1U, network, port });
        }
        for (std::size_t node = 0U; node < node_count; ++node) {
            design.signal_names.push_back(node_name(network, node));
        }
        for (std::size_t sink = 0U; sink < sink_count; ++sink) {
            const auto name = sink_name(network, sink);
            design.signal_names.push_back(name);
            design.sink_names.push_back(name);
        }
    }
    return design;
}

[[nodiscard]] std::string make_stimulus(
    const std::size_t width,
    const std::uint32_t seed,
    const std::size_t port,
    const std::size_t phase)
{
    constexpr std::string_view states { "01XZ" };
    std::string value(width, '0');
    const auto seed_offset = static_cast<std::size_t>(seed & 3U);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        const auto state = (bit * 3U + phase + port + seed_offset) % states.size();
        value[bit] = states[state];
    }
    return value;
}

[[nodiscard]] std::string width_range(const std::size_t width)
{
    if (width == 1U) {
        return { };
    }
    return "[" + std::to_string(width - 1U) + ":0]";
}

void write_stimulus_assignment(
    std::ostream& output,
    const Network& network,
    const std::size_t network_index,
    const std::size_t port,
    const std::size_t phase,
    const std::size_t width)
{
    output << "    " << input_name(network_index, port) << " = " << width
           << "'b" << make_stimulus(width, network.seed, port, phase) << ";\n";
}

[[nodiscard]] fsim::project::Config make_config(
    const Optimization optimization,
    const std::filesystem::path& root,
    const DagDesign& design,
    const std::size_t width)
{
    const auto source = root / "native_frontier_random_dag.sv";
    std::ofstream output { source, std::ios::binary };
    const auto range = width_range(width);
    output << "module native_frontier_random_dag(\n";
    for (std::size_t network = 0U; network < network_count; ++network) {
        for (std::size_t sink = 0U; sink < sink_count; ++sink) {
            output << "  output wire " << range << " "
                   << sink_name(network, sink);
            if (network + 1U != network_count || sink + 1U != sink_count) {
                output << ",";
            }
            output << "\n";
        }
    }
    output << ");\n";

    for (std::size_t network = 0U; network < network_count; ++network) {
        for (std::size_t port = 0U; port < input_count; ++port) {
            output << "  logic " << range << " "
                   << input_name(network, port) << ";\n";
        }
        for (std::size_t node = 0U; node < node_count; ++node) {
            output << "  wire " << range << " " << node_name(network, node)
                   << ";\n";
        }
    }
    output << "\n";

    for (std::size_t network_index = 0U;
         network_index < network_count; ++network_index) {
        const auto& network = design.networks[network_index];
        for (std::size_t node = 0U; node < network.nodes.size(); ++node) {
            const auto& definition = network.nodes[node];
            output << "  assign " << node_name(network_index, node) << " = ";
            if (definition.operation == '~') {
                output << "~" << value_name(network_index, definition.left);
            } else {
                output << value_name(network_index, definition.left) << " "
                       << definition.operation << " "
                       << value_name(network_index, definition.right);
            }
            output << ";\n";
        }
        output << "  assign " << sink_name(network_index, 0U) << " = "
               << node_name(network_index, node_count - 1U) << ";\n";
        output << "  assign " << sink_name(network_index, 1U) << " = "
               << node_name(network_index, 7U) << " ^ "
               << node_name(network_index, 10U) << ";\n\n";
    }

    output << "  initial begin\n";
    for (std::size_t network = 0U; network < network_count; ++network) {
        for (std::size_t port = 0U; port < input_count; ++port) {
            write_stimulus_assignment(output, design.networks[network],
                network, port, 0U, width);
        }
    }
    for (std::size_t phase = 1U; phase < stimulus_phase_count; ++phase) {
        output << "    #2;\n";
        for (std::size_t network = 0U; network < network_count; ++network) {
            for (std::size_t port = 0U; port < input_count; ++port) {
                write_stimulus_assignment(output, design.networks[network],
                    network, port, phase, width);
            }
        }
    }
    output << "    #100; $finish;\n"
           << "  end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the deterministic random DAG design must be written completely");

    fsim::project::Config config;
    config.project.name = "native-frontier-deterministic-random-dag";
    config.project.top = "sv:work.native_frontier_random_dag";
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

struct RouteResult {
    std::vector<std::vector<Snapshot>> frames;
    std::vector<std::uint64_t> dispatch_deltas;
    std::vector<std::uint64_t> frontier_dispatch_deltas;
    std::vector<std::uint64_t> forwarding_member_deltas;
    NativeRegionAllocationTestAccess::PrivateRoleCut private_role_cut;
};

[[nodiscard]] std::vector<std::string> qualified_names(const DagDesign& design)
{
    std::vector<std::string> result;
    result.reserve(design.signal_names.size());
    for (const auto& name : design.signal_names) {
        result.push_back("native_frontier_random_dag." + name);
    }
    return result;
}

void require_stimulus(
    const DagDesign& design,
    const std::vector<Snapshot>& frame,
    const std::size_t width,
    const std::size_t phase)
{
    for (const auto& binding : design.input_bindings) {
        const auto expected = make_stimulus(width,
            design.networks[binding.network].seed, binding.port, phase);
        require(frame.at(binding.signal_index).current == expected,
            "each deterministic four-state input must match its phase pattern");
    }
}

[[nodiscard]] RouteResult run_route(
    const Optimization optimization,
    const std::filesystem::path& root,
    const DagDesign& design,
    const std::size_t width,
    const SimulationEngine engine,
    const bool capture_private_role_cut = false)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_config(optimization, root, design, width);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the deterministic combinational DAG must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, SystemVerilogVpiRuntimeUpdates::omitted);
    if (compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the native DAG route must install compiled processes");
    }

    const auto names = qualified_names(design);
    std::vector<SignalId> signals;
    signals.reserve(names.size());
    for (const auto& name : names) {
        const auto signal = simulation.find_signal(name);
        require(signal.has_value(),
            "every generated DAG signal must retain a handle");
        signals.push_back(*signal);
    }
    for (const auto& sink : design.sink_names) {
        const auto full_name = "native_frontier_random_dag." + sink;
        const auto found = simulation.find_signal(full_name);
        require(found.has_value(), "every DAG sink must be observable");
        if (!capture_private_role_cut) {
            // The private-cut route delays public exposure until its
            // authentic journal row and child receipt have been captured.
            static_cast<void>(simulation.read_signal(*found));
        }
    }

    const auto capture_frame = [&] {
        std::vector<Snapshot> frame;
        frame.reserve(signals.size());
        for (const auto signal : signals) {
            frame.push_back(
                NativeRegionAllocationTestAccess::snapshot(simulation, signal));
        }
        return frame;
    };

    simulation.start();
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the deterministic stimulus must keep the simulation open past time one");
    if (compiled) {
        simulation.await_all_native_compilation();
    }

    RouteResult result;
    result.frames.reserve(stimulus_phase_count);
    result.dispatch_deltas.reserve(stimulus_phase_count - 1U);
    result.frontier_dispatch_deltas.reserve(stimulus_phase_count - 1U);
    result.forwarding_member_deltas.reserve(stimulus_phase_count - 1U);
    auto frame = capture_frame();
    require_stimulus(design, frame, width, 0U);
    result.frames.push_back(std::move(frame));

    for (std::size_t phase = 1U; phase < stimulus_phase_count; ++phase) {
        const auto before = compiled
            ? NativeRegionAllocationTestAccess::native_frontier_dispatches(
                  simulation)
            : 0U;
        const auto before_forwarded = compiled
            ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                  simulation)
            : 0U;
        const auto private_parent_before = compiled
            ? NativeRegionAllocationTestAccess::private_parent_dispatches(
                  simulation)
            : 0U;
        const auto target_time = static_cast<SimulationTick>(phase * 2U + 1U);
        std::optional<Scheduler::SafePointHookToken> private_cut_hook;
        if (compiled && capture_private_role_cut && phase == 1U) {
            private_cut_hook
                = NativeRegionAllocationTestAccess::install_private_role_cut_hook(
                    simulation, signals, result.private_role_cut);
        }
        auto run = simulation.run(target_time);
        if (private_cut_hook) {
            NativeRegionAllocationTestAccess::remove_private_role_cut_hook(
                simulation, *private_cut_hook);
            require(run.status == RunStatus::stopped
                    && result.private_role_cut.reached,
                "the random DAG must stop at an authentic private A2 role row");
            const auto& cut = result.private_role_cut;
            require(cut.applied_rows != 0U
                    && cut.applied_rows == cut.metadata_rows
                    && cut.journal_enabled
                    && !cut.private_epoch_retired
                    && cut.child.generation != 0U
                    && cut.child.component == cut.component
                    && cut.child.key.time == cut.before.now
                    && cut.child.key.stable_order == cut.child.process
                    && cut.child.key.systemverilog_round != 0U
                    && cut.child.key.process_domain
                        == static_cast<std::uint32_t>(
                            fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
                    && cut.child.key.phase
                        == static_cast<std::uint32_t>(
                            fsim::runtime::SchedulerPhase::active),
                "the private cut must retain its role rows and exact queued child receipt");
            const auto after_private_parent
                = NativeRegionAllocationTestAccess::private_parent_dispatches(
                    simulation);
            require(after_private_parent > private_parent_before,
                "the private cut must follow an actual forwarding parent callback");

            const auto flushed_value
                = simulation.read_signal_snapshot(cut.signal);
            require(flushed_value.to_msb_string() == cut.expected_current,
                "public observation must materialize the exact private row value");
            const auto after_observation = capture_frame();
            const auto cut_signal_index = std::ranges::find(signals, cut.signal);
            require(cut_signal_index != signals.end(),
                "the private output must remain in the captured DAG signal set");
            const auto& published = after_observation[
                static_cast<std::size_t>(cut_signal_index - signals.begin())];
            require(published.current == cut.expected_current
                    && published.last == cut.expected_last
                    && published.stored == cut.expected_stored
                    && published.event == cut.event
                    && published.transaction == cut.transaction
                    && published.event_domain == cut.event_domain
                    && published.event_phase == cut.event_phase
                    && published.systemverilog_round == cut.event_round
                    && published.value_revision == cut.value_revision,
                "role flush must publish current/LAST/stored without replaying event metadata");
            const auto published_owner = std::ranges::find(
                published.raw_drivers, cut.owner,
                &NativeRegionAllocationTestAccess::RawDriver::process);
            if (cut.owner_record_present) {
                require(published_owner != published.raw_drivers.end()
                        && published_owner->value == cut.expected_owner,
                    "role flush must publish the original raw owner value");
            } else {
                require(cut.owner_is_stored_alias
                        && published_owner == published.raw_drivers.end()
                        && cut.expected_owner == cut.expected_stored,
                    "an aliased owner must remain represented by the stored role");
            }
            const auto journal
                = NativeRegionAllocationTestAccess::private_journal_state(
                    simulation, cut.component);
            require(journal.applied_rows == 0U
                    && journal.metadata_rows == 0U
                    && !journal.journal_enabled
                    && journal.private_epoch_retired,
                "public observation must drain the private role journal before resume");
            const auto retained_child
                = NativeRegionAllocationTestAccess::pending_child_receipt(
                    simulation, cut.child.process);
            require(retained_child.has_value()
                    && *retained_child == cut.child,
                "role flush must preserve the exact queued child receipt and full key");
            simulation.clear_stop();
            run = simulation.run(target_time);
        }
        require(run.status == RunStatus::time_limit,
            "each deterministic stimulus phase must settle before finish");
        const auto after = compiled
            ? NativeRegionAllocationTestAccess::native_frontier_dispatches(
                  simulation)
            : 0U;
        const auto after_forwarded = compiled
            ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                  simulation)
            : 0U;
        if (compiled) {
            const auto frontier_delta = after - before;
            const auto forwarding_delta = after_forwarded - before_forwarded;
            const auto successful_member_delta
                = frontier_delta + forwarding_delta;
            if (!capture_private_role_cut) {
                require(successful_member_delta != 0U,
                    "every changed default-route DAG phase must consume a generated native-frontier or flattened forwarding member");
            }
            result.frontier_dispatch_deltas.push_back(frontier_delta);
            result.forwarding_member_deltas.push_back(forwarding_delta);
            result.dispatch_deltas.push_back(successful_member_delta);
        }
        frame = capture_frame();
        require_stimulus(design, frame, width, phase);
        result.frames.push_back(std::move(frame));
    }
    return result;
}

void require_four_state_stimulus_coverage(
    const DagDesign& design, const std::size_t width)
{
    std::array<bool, 4U> seen { };
    constexpr std::string_view states { "01XZ" };
    for (std::size_t phase = 0U; phase < stimulus_phase_count; ++phase) {
        for (const auto& binding : design.input_bindings) {
            const auto pattern = make_stimulus(width,
                design.networks[binding.network].seed, binding.port, phase);
            for (const auto state : pattern) {
                const auto found = states.find(state);
                require(found != std::string_view::npos,
                    "generated stimulus contains a supported Logic4 state");
                seen[found] = true;
            }
        }
    }
    require(std::ranges::all_of(seen, [](const bool present) { return present; }),
        "the deterministic patterns must exercise 0, 1, X, and Z");
}

void test_o0_and_o2_random_dag_frontier()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-random-dag-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);
    const auto design = make_design();
    const std::array<std::string, network_count> signatures {
        topology_signature(design.networks[0U]),
        topology_signature(design.networks[1U]),
        topology_signature(design.networks[2U]) };
    require(signatures[0U] != signatures[1U]
            && signatures[0U] != signatures[2U]
            && signatures[1U] != signatures[2U],
        "fixed seeds must generate three distinct acyclic DAG suffixes");

    for (const auto width : { 1U, 65U, 129U }) {
        require_four_state_stimulus_coverage(design, width);
        for (const auto optimization : { Optimization::o0, Optimization::o2 }) {
            const auto optimization_name
                = optimization == Optimization::o0 ? "o0" : "o2";
            const auto case_root = root.path / optimization_name
                / ("width-" + std::to_string(width));
            const auto compiled_root = case_root / "compiled";
            const auto interpreter_root = case_root / "interpreter";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(interpreter_root);

            const auto compiled = run_route(optimization, compiled_root,
                design, width, SimulationEngine::compiled);
            const auto interpreted = run_route(optimization, interpreter_root,
                design, width, SimulationEngine::interpreter);
            require(compiled.frames == interpreted.frames,
                "O0/O2 DAGs must match interpreter values, raw owners, and "
                "event/transaction metadata at every phase");
            if (width == 129U) {
                const auto private_root = case_root / "compiled-private-cut";
                std::filesystem::create_directories(private_root);
                const auto private_cut = run_route(optimization,
                    private_root, design, width,
                    SimulationEngine::compiled, true);
                require(private_cut.private_role_cut.reached,
                    "the O0/O2 private route must capture an applied A2 row");
                require(private_cut.frames == interpreted.frames,
                    "private-cut flush/resume must match the full interpreter trace");
                require(private_cut.frames == compiled.frames,
                    "private-cut flush/resume must preserve the generated DAG result");
            }
            for (std::size_t network = 0U; network < network_count; ++network) {
                const auto first_node_name = node_name(network, 0U);
                const auto first_node = std::ranges::find(
                    design.signal_names, first_node_name);
                require(first_node != design.signal_names.end(),
                    "the shared source node must be captured");
                const auto signal_index = static_cast<std::size_t>(
                    first_node - design.signal_names.begin());
                require(std::ranges::any_of(
                            compiled.frames | std::views::drop(1U),
                            [&](const auto& frame) {
                                return frame[signal_index].current
                                    != compiled.frames.front()[signal_index].current;
                            }),
                    "each seeded DAG must produce a changing shared node");
            }
            require(compiled.dispatch_deltas.size()
                    == stimulus_phase_count - 1U
                && std::ranges::all_of(compiled.dispatch_deltas,
                        [](const std::uint64_t delta) { return delta != 0U; }),
                "every measured phase must consume a successful native member through the frontier or forwarding route");
        }
    }
}

constexpr std::size_t boundary_bus_width { 73U };
constexpr std::size_t boundary_slice_offset { 61U };
constexpr std::size_t boundary_slice_width { 9U };

enum class BoundaryChildSensitivity : std::uint8_t {
    whole_signal,
    exact_full_width,
    strict_subrange,
};

struct BoundaryRangeFrame {
    Snapshot boundary;
    Snapshot mid;
    Snapshot sink;

    friend bool operator==(
        const BoundaryRangeFrame&, const BoundaryRangeFrame&) = default;
};

struct BoundaryRangeRouteResult {
    std::vector<BoundaryRangeFrame> frames;
    std::vector<std::uint64_t> frontier_dispatch_deltas;
};

[[nodiscard]] fsim::project::Config make_boundary_range_config(
    const Optimization optimization, const std::filesystem::path& root,
    const BoundaryChildSensitivity child_sensitivity)
{
    const auto source = root / "native_frontier_boundary_range.sv";
    std::ofstream output { source, std::ios::binary };
    const auto sink_width = child_sensitivity
            == BoundaryChildSensitivity::strict_subrange
        ? boundary_slice_width - 1U
        : boundary_slice_width;
    const auto sink_range = "[" + std::to_string(sink_width - 1U) + ":0]";
    const auto sink_expression = child_sensitivity
            == BoundaryChildSensitivity::strict_subrange
        ? "mid[7:0]"
        : "mid";
    output << "module native_frontier_boundary_range(\n"
           << "  input wire [72:0] boundary,\n"
           << "  output wire " << sink_range << " sink\n"
           << ");\n"
           << "  wire [8:0] mid;\n"
           << "  assign mid = boundary[69:61];\n"
           << "  assign sink = " << sink_expression << ";\n"
           << "  initial begin #100; $finish; end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the boundary-range design must be written completely");

    fsim::project::Config config;
    config.project.name = "native-frontier-boundary-range";
    config.project.top = "sv:work.native_frontier_boundary_range";
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

void set_exact_full_width_child_sensitivity(
    fsim::app::BuiltProject& project)
{
    const auto mid = project.design.find_signal(
        "native_frontier_boundary_range.mid");
    const auto sink = project.design.find_signal(
        "native_frontier_boundary_range.sink");
    require(mid.has_value() && sink.has_value(),
        "the copied design exposes the child input and output IDs");

    // Move the already elaborated per-instance registration records out of the
    // copied project, edit only the child Process sensitivity, and restore the
    // design before Interpreter construction. Shared HIR/template metadata and
    // operation bodies remain untouched.
    auto design_state = std::move(project.design).state();
    std::size_t sink_processes = 0U;
    std::size_t changed_sensitivities = 0U;
    for (auto& process : design_state.processes) {
        const bool drives_sink = std::ranges::any_of(process.driver_regions,
            [sink = *sink](const auto& region) {
                return region.signal == sink;
            });
        if (!drives_sink) {
            continue;
        }
        ++sink_processes;
        require(process.scheduling_domain
                == fsim::runtime::simir::ProcessSchedulingDomain::systemverilog,
            "the child registration remains in the SystemVerilog domain");
        const bool reads_mid = std::ranges::any_of(process.operations,
            [mid = *mid](const auto& operation) {
                const auto* read = fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::ReadSignal>(&operation);
                return read != nullptr && read->signal == mid;
            });
        const bool queues_active_sink_update
            = std::ranges::any_of(process.operations,
                [sink = *sink](const auto& operation) {
                    const auto* update = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteUpdate>(&operation);
                    return update != nullptr && update->signal == sink
                        && update->domain
                            == fsim::runtime::simir::SignalUpdateDomain::
                                systemverilog_active;
                });
        require(reads_mid && queues_active_sink_update,
            "the mutated instance is the continuous-assignment child with an unchanged Active update body");

        for (auto& sensitivity : process.static_sensitivity) {
            if (sensitivity.signal == *mid
                && sensitivity.edge == fsim::runtime::simir::EdgeKind::any
                && sensitivity.offset == 0U && sensitivity.width == 0U) {
                sensitivity.width = static_cast<std::uint32_t>(
                    boundary_slice_width);
                ++changed_sensitivities;
            }
        }
    }
    require(sink_processes == 1U && changed_sensitivities == 1U,
        "exactly the child registration receives explicit (0, 9) sensitivity");

    auto restored = fsim::elaboration::ElaboratedDesign::from_state(
        std::move(design_state));
    require(restored.has_value(),
        "the copied SimIR with the explicit full-width tuple remains valid");
    project.design = std::move(*restored);

    const auto& processes = project.design.processes();
    const auto child = std::ranges::find_if(processes,
        [sink = *sink](const auto& process) {
            return std::ranges::any_of(process.driver_regions,
                [sink](const auto& region) {
                    return region.signal == sink;
                });
        });
    require(child != processes.end()
            && std::ranges::count_if(child->static_sensitivity,
                [mid = *mid](const auto& sensitivity) {
                    return sensitivity.signal == mid
                        && sensitivity.edge
                            == fsim::runtime::simir::EdgeKind::any
                        && sensitivity.offset == 0U
                        && sensitivity.width == boundary_slice_width;
                }) == 1,
        "the restored Process registration retains the exact (0, 9) tuple");
}

[[nodiscard]] std::string boundary_bus_value(
    const std::string_view slice, const bool toggle_outside)
{
    require(slice.size() == boundary_slice_width,
        "the boundary-range stimulus has the exact selected width");
    std::string result(boundary_bus_width, '0');
    for (std::size_t index = 0U; index < slice.size(); ++index) {
        const auto bit = boundary_slice_offset + slice.size() - 1U - index;
        result[boundary_bus_width - 1U - bit] = slice[index];
    }
    if (toggle_outside) {
        result[boundary_bus_width - 1U - 60U] = '1';
        result[boundary_bus_width - 1U - 70U] = '1';
    }
    return result;
}

[[nodiscard]] bool same_boundary_effect(
    const Snapshot& left, const Snapshot& right)
{
    return left.current == right.current && left.last == right.last
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
        && left.value_revision == right.value_revision;
}

[[nodiscard]] BoundaryRangeRouteResult run_boundary_range_case(
    const Optimization optimization, const std::filesystem::path& root,
    const SimulationEngine engine,
    const BoundaryChildSensitivity child_sensitivity)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_boundary_range_config(
        optimization, root, child_sensitivity);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the boundary-range two-member chain must elaborate");
    if (child_sensitivity == BoundaryChildSensitivity::exact_full_width) {
        set_exact_full_width_child_sensitivity(*project);
    }
    Simulation simulation(std::move(*project), config.run.max_deltas, engine,
        SystemVerilogVpiRuntimeUpdates::omitted);
    if (compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() >= 2U,
            "both boundary-chain members must retain compiled executors");
    }

    const auto sink_width = child_sensitivity
            == BoundaryChildSensitivity::strict_subrange
        ? boundary_slice_width - 1U
        : boundary_slice_width;
    const auto child_offset = 0U;
    const auto child_width = child_sensitivity
            == BoundaryChildSensitivity::whole_signal
        ? 0U
        : static_cast<std::uint32_t>(sink_width);
    const auto boundary = simulation.find_signal(
        "native_frontier_boundary_range.boundary");
    const auto mid = simulation.find_signal(
        "native_frontier_boundary_range.mid");
    const auto sink = simulation.find_signal(
        "native_frontier_boundary_range.sink");
    require(boundary.has_value() && mid.has_value() && sink.has_value(),
        "the external bus and both chain signals must retain handles");

    simulation.start();
    ScopedBoundaryV2RouteSelection v2_only_route { simulation };
    const auto forwarding_before_route = compiled
        ? NativeRegionAllocationTestAccess::forwarding_member_consumptions(
              simulation)
        : 0U;
    if (compiled) {
        simulation.await_all_native_compilation();
        if (child_sensitivity == BoundaryChildSensitivity::strict_subrange) {
            require(NativeRegionAllocationTestAccess::
                        native_frontier_runtime_count(simulation) == 0U,
                "a strict internal subrange must not prepare a V2 frontier runtime");
        } else {
            const auto shape
                = NativeRegionAllocationTestAccess::
                    boundary_range_kernel_shape(simulation, *boundary, *mid,
                        *sink, child_offset, child_width,
                        static_cast<std::uint32_t>(sink_width));
            require(shape.found && shape.member_count == 2U
                    && shape.has_systemverilog_scheduling_domain
                    && shape.has_active_update_outputs
                    && shape.has_update_publication_outputs
                    && shape.has_expected_child_member,
                "the child tuple and Active update outputs belong to the two-member V2 frontier");
            require(v2_only_route.select(shape)
                    && v2_only_route.remains_selected(),
                "the quiet component can select its existing V2 backend without replacing the frontier certificate");
        }
    }
    require(simulation.run(1U).status == RunStatus::time_limit,
        "the delayed finish must keep the boundary-range case open");
    if (compiled
        && child_sensitivity != BoundaryChildSensitivity::strict_subrange) {
        require(v2_only_route.remains_selected(),
            "the first scheduler run must retain the selected V2 route and runtime generation");
        require(NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                    simulation) == forwarding_before_route,
            "the selected V2 route must not consume a flattened member after startup");
    }
    if (compiled
        && child_sensitivity == BoundaryChildSensitivity::strict_subrange) {
        require(NativeRegionAllocationTestAccess::
                    native_frontier_runtime_count(simulation) == 0U,
            "the strict subrange remains outside V2 after its first run");
    }

    const auto capture = [&] {
        return BoundaryRangeFrame {
            NativeRegionAllocationTestAccess::snapshot(simulation, *boundary),
            NativeRegionAllocationTestAccess::snapshot(simulation, *mid),
            NativeRegionAllocationTestAccess::snapshot(simulation, *sink),
        };
    };
    BoundaryRangeRouteResult result;
    result.frames.reserve(8U);
    result.frontier_dispatch_deltas.reserve(7U);
    result.frames.push_back(capture());

    struct Stimulus {
        std::string_view slice;
        bool toggle_outside;
        bool root_range_changed;
        bool low_eight_changed;
    };
    constexpr std::array<Stimulus, 7U> stimuli {
        Stimulus { "101001011", false, true, true },
        Stimulus { "101001011", true, false, false },
        Stimulus { "001001011", true, true, false },
        Stimulus { "010110100", true, true, true },
        Stimulus { "XXXXXXXXX", true, true, true },
        Stimulus { "ZZZZZZZZZ", true, true, true },
        Stimulus { "001101100", true, true, true },
    };

    auto target_time = SimulationTick { 2U };
    for (const auto& stimulus : stimuli) {
        const auto before = result.frames.back();
        const auto dispatch_before = compiled
            ? NativeRegionAllocationTestAccess::native_frontier_dispatches(
                  simulation)
            : 0U;
        const auto bus_value
            = boundary_bus_value(stimulus.slice, stimulus.toggle_outside);
        simulation.deposit_signal(*boundary,
            PackedLogic4::from_msb_string(bus_value));
        if (compiled
            && child_sensitivity != BoundaryChildSensitivity::strict_subrange) {
            require(v2_only_route.remains_selected(),
                "an external boundary deposit must not recertify or restore the competing route");
        }
        const auto run = simulation.run(target_time++);
        require(run.status == RunStatus::time_limit,
            "every boundary-range stimulus must settle before finish");
        if (compiled
            && child_sensitivity != BoundaryChildSensitivity::strict_subrange) {
            require(v2_only_route.remains_selected(),
                "each boundary stimulus must preserve the selected V2 certificate and quiet A2 bank");
            require(NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                        simulation) == forwarding_before_route,
                    "boundary changes must not consume flattened forwarding members");
        }
        const auto after = capture();
        require(after.boundary.current == bus_value,
            "the deposited external bus must preserve all wide four-state bits");

        const auto dispatch_after = compiled
            ? NativeRegionAllocationTestAccess::native_frontier_dispatches(
                  simulation)
            : 0U;
        const auto dispatch_delta = dispatch_after - dispatch_before;
        result.frontier_dispatch_deltas.push_back(dispatch_delta);
        if (stimulus.root_range_changed) {
            require(after.mid.current == stimulus.slice,
                "the root must transfer the exact selected external bus slice");
        } else {
            require(after.mid.current == before.mid.current,
                "off-slice changes must preserve the private mid value");
        }
        if (stimulus.root_range_changed) {
            require(after.mid.event != before.mid.event
                    && after.mid.transaction != before.mid.transaction
                    && after.mid.value_revision > before.mid.value_revision
                    && after.mid.last == before.mid.current,
                "each selected bus-slice change must publish the private mid update and LAST value");
        } else {
            require(same_boundary_effect(before.mid, after.mid),
                "outside-slice bus changes must not alter private-mid metadata");
        }

        const bool child_range_changed
            = child_sensitivity == BoundaryChildSensitivity::strict_subrange
                ? stimulus.low_eight_changed
                : stimulus.root_range_changed;
        const auto expected_sink = child_sensitivity
                == BoundaryChildSensitivity::strict_subrange
            ? stimulus.slice.substr(1U)
            : stimulus.slice;
        if (child_range_changed) {
            require(after.sink.current == expected_sink
                    && after.sink.event != before.sink.event
                    && after.sink.transaction != before.sink.transaction
                    && after.sink.value_revision > before.sink.value_revision
                    && after.sink.last == before.sink.current,
                "the child must publish exactly its selected whole or low-eight range and LAST value");
            require(after.mid.event.has_value() && after.sink.event.has_value()
                    && (after.mid.event->first < after.sink.event->first
                        || (after.mid.event->first == after.sink.event->first
                            && (after.mid.event->second
                                    < after.sink.event->second
                                || (after.mid.event->second
                                        == after.sink.event->second
                                    && after.mid.systemverilog_round
                                        < after.sink.systemverilog_round)))),
                "the private mid update must precede its child's sink event");
        } else {
            require(same_boundary_effect(before.sink, after.sink),
                "a transition outside the child's exact sensitivity must preserve sink value and metadata");
        }
        if (compiled) {
            if (child_sensitivity == BoundaryChildSensitivity::strict_subrange) {
                require(dispatch_delta == 0U
                        && NativeRegionAllocationTestAccess::
                            native_frontier_runtime_count(simulation) == 0U,
                    "strict internal subranges must neither prepare nor execute a V2 frontier");
            } else if (stimulus.root_range_changed) {
                require(dispatch_delta == 2U,
                    "each selected full-range change must dispatch both native frontier members");
            } else {
                require(dispatch_delta == 0U,
                    "outside-slice changes must execute neither V2 frontier member");
            }
        }
        result.frames.push_back(after);
    }
    if (compiled
        && child_sensitivity != BoundaryChildSensitivity::strict_subrange) {
        require(v2_only_route.restore(),
            "the original forwarding entry must be restored after the V2-only stimulus window");
    }
    return result;
}

void test_internal_full_width_boundary_range_readiness()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-boundary-range-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);

    for (const auto optimization : { Optimization::o0, Optimization::o2 }) {
        const auto optimization_name
            = optimization == Optimization::o0 ? "o0" : "o2";
        std::array<BoundaryRangeRouteResult, 3U> compiled_cases;
        std::array<BoundaryRangeRouteResult, 3U> interpreted_cases;
        const std::array modes {
            BoundaryChildSensitivity::whole_signal,
            BoundaryChildSensitivity::exact_full_width,
            BoundaryChildSensitivity::strict_subrange,
        };
        for (std::size_t index = 0U; index < modes.size(); ++index) {
            const auto case_root = root.path / optimization_name
                / std::to_string(index);
            const auto compiled_root = case_root / "compiled";
            const auto interpreter_root = case_root / "interpreter";
            std::filesystem::create_directories(compiled_root);
            std::filesystem::create_directories(interpreter_root);
            compiled_cases[index] = run_boundary_range_case(optimization,
                compiled_root, SimulationEngine::compiled, modes[index]);
            interpreted_cases[index] = run_boundary_range_case(optimization,
                interpreter_root, SimulationEngine::interpreter, modes[index]);
            require(compiled_cases[index].frames
                    == interpreted_cases[index].frames,
                "native O0/O2 and interpreter must match boundary, value, LAST, driver, event, and transaction state");
            const auto& dispatches
                = compiled_cases[index].frontier_dispatch_deltas;
            require(dispatches.size() == 7U,
                "each sensitivity mode records every boundary and off-slice dispatch interval");
            if (modes[index]
                == BoundaryChildSensitivity::strict_subrange) {
                require(std::ranges::all_of(dispatches,
                            [](const std::uint64_t count) {
                                return count == 0U;
                            }),
                    "the strict internal subrange must never enter V2");
            } else {
                require(dispatches[0U] == 2U && dispatches[1U] == 0U
                        && dispatches[2U] == 2U
                        && std::ranges::all_of(
                            dispatches | std::views::drop(3U),
                            [](const std::uint64_t count) {
                                return count == 2U;
                            }),
                    "whole and exact-full-width sensitivities dispatch the same two members only for selected bus changes");
            }
        }
        require(compiled_cases[0U].frames == compiled_cases[1U].frames
                && interpreted_cases[0U].frames
                    == interpreted_cases[1U].frames,
            "canonical whole-signal and explicit full-width tuples preserve identical simulation semantics");
    }
}

struct AliasBoundaryFrame {
    Snapshot source;
    Snapshot before;
    Snapshot leaf;
    Snapshot sibling;
    Snapshot proxy;
    Snapshot after;
    Snapshot sink;
    std::string proxy_driver;

    friend bool operator==(
        const AliasBoundaryFrame&, const AliasBoundaryFrame&) = default;
};

struct AliasBoundaryRouteResult {
    std::vector<AliasBoundaryFrame> frames;
    std::vector<std::uint64_t> frontier_dispatch_deltas;
    std::size_t member_count { };
};

[[nodiscard]] fsim::project::Config make_alias_boundary_config(
    const Optimization optimization, const std::filesystem::path& root)
{
    const auto source = root / "native_frontier_alias_boundary.sv";
    std::ofstream output { source, std::ios::binary };
    output << "module native_frontier_alias_boundary(\n"
           << "  input wire [7:0] source,\n"
           << "  output wire [7:0] sink\n"
           << ");\n"
           << "  wire [7:0] before_leaf;\n"
           << "  wire [7:0] words [0:1];\n"
           << "  wire [7:0] after_leaf;\n"
           << "  assign before_leaf = source;\n"
           << "  assign words[0] = before_leaf;\n"
           << "  assign after_leaf = words[0];\n"
           << "  assign sink = after_leaf;\n"
           << "  initial begin #100; $finish; end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "the parsed alias-boundary chain must be written completely");

    fsim::project::Config config;
    config.project.name = "native-frontier-alias-leaf-boundary";
    config.project.top = "sv:work.native_frontier_alias_boundary";
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

struct AliasProxySliceArtifactWrite {
    struct InstructionIdentity {
        std::size_t group { };
        std::size_t alternative { };
        std::optional<ReadSignal> read_signal;
    };

    ProcessId process { };
    std::size_t instruction { };
    std::size_t instruction_count { };
    std::vector<InstructionIdentity> original_instruction_identities;
    SignalId leaf { };
    SignalId proxy { };
    RegisterId source { };
    std::uint32_t offset { };
    std::uint32_t width { };
    SignalUpdateDomain domain { SignalUpdateDomain::generic };
};

[[nodiscard]] AliasProxySliceArtifactWrite
rewrite_alias_leaf_write_for_artifact(
    fsim::app::BuiltProject& project)
{
    auto state = project.design.state();
    const auto leaf = project.design.find_signal(
        "native_frontier_alias_boundary.words[0]");
    const auto proxy = project.design.find_signal(
        "native_frontier_alias_boundary.words");
    require(leaf.has_value() && proxy.has_value(),
        "the parsed source design must expose the physical leaf and aggregate proxy");
    require(*leaf < state.signals.size() && *proxy < state.signals.size(),
        "the source aliases must index the runtime signal table");

    const auto aggregate_alias = std::ranges::find(
        state.container_aggregate_signal_aliases, *proxy,
        &ContainerAggregateSignalAlias::signal);
    const auto leaf_alias = std::ranges::find_if(
        state.container_element_signal_aliases,
        [&](const ContainerElementSignalAlias& alias) {
            return alias.signal == *leaf;
        });
    require(aggregate_alias != state.container_aggregate_signal_aliases.end()
            && aggregate_alias->writable
            && leaf_alias != state.container_element_signal_aliases.end()
            && leaf_alias->object == aggregate_alias->object,
        "the source leaf must belong to the writable aggregate alias family");
    const auto& object = state.container_objects.at(aggregate_alias->object);
    const auto element_width = object.initial_value.type.element_width;
    const auto family_size = static_cast<std::size_t>(std::ranges::count_if(
        state.container_element_signal_aliases,
        [&](const ContainerElementSignalAlias& alias) {
            return alias.object == aggregate_alias->object;
        }));
    require(element_width == 8U && family_size == 2U
            && leaf_alias->ordinal < family_size
            && state.signals[*leaf].initial_value.width() == element_width,
        "the direct-leaf fixture must retain two full eight-bit family members");
    const auto offset = (family_size - leaf_alias->ordinal - 1U)
        * static_cast<std::size_t>(element_width);
    require(offset == 8U
            && offset <= state.signals[*proxy].initial_value.width()
            && element_width
                <= state.signals[*proxy].initial_value.width() - offset,
        "the selected leaf must map to one in-bounds full-width proxy slice");
    const auto source_signal = project.design.find_signal(
        "native_frontier_alias_boundary.before_leaf");
    require(source_signal.has_value(),
        "the source design must retain the physical leaf's direct source signal");

    std::optional<AliasProxySliceArtifactWrite> selected;
    for (auto& process : state.processes) {
        if (process.scheduling_domain
            != ProcessSchedulingDomain::systemverilog) {
            continue;
        }
        for (std::size_t instruction = 0U;
            instruction < process.operations.size(); ++instruction) {
            const auto operation = process.operations.expanded(instruction);
            const auto* write = operation_get_if<WriteUpdate>(&operation);
            const auto* slice = operation_get_if<WriteUpdateSlice>(&operation);
            RegisterId source { };
            SignalUpdateDomain domain { SignalUpdateDomain::generic };
            if (write != nullptr) {
                if (write->signal != *leaf) {
                    continue;
                }
                source = write->source;
                domain = write->domain;
            } else {
                if (slice == nullptr || slice->signal != *leaf
                    || slice->offset != 0U) {
                    continue;
                }
                source = slice->source;
                domain = slice->domain;
            }
            require(!selected.has_value()
                    && domain == SignalUpdateDomain::systemverilog_active
                    && source < process.register_count,
                "the physical leaf must have one full-leaf Active update owner");
            require(process.driver_regions.size() == 1U,
                "the selected source owner must have one driver region");
            const auto& driver = process.driver_regions.front();
            const bool whole_leaf = driver.signal == *leaf
                && driver.whole && driver.offset == 0U && driver.width == 0U;
            const bool explicit_full_leaf = driver.signal == *leaf
                && !driver.whole && driver.offset == 0U
                && driver.width == element_width;
            require(whole_leaf || explicit_full_leaf,
                "the direct source driver region must cover exactly the complete physical leaf");
            AliasProxySliceArtifactWrite candidate;
            candidate.process = process.id;
            candidate.instruction = instruction;
            candidate.leaf = *leaf;
            candidate.proxy = *proxy;
            candidate.source = source;
            candidate.offset = static_cast<std::uint32_t>(offset);
            candidate.width = element_width;
            candidate.domain = domain;
            selected = std::move(candidate);
        }
    }
    require(selected.has_value(),
        "the parsed reference source must contain one direct full-leaf Active update");

    auto& process = state.processes.at(selected->process);
    selected->instruction_count = process.operations.size();
    selected->original_instruction_identities.reserve(
        selected->instruction_count);
    for (std::size_t instruction = 0U;
        instruction < selected->instruction_count; ++instruction) {
        const auto operation = process.operations.expanded(instruction);
        AliasProxySliceArtifactWrite::InstructionIdentity identity;
        identity.group = operation_group_index(operation);
        identity.alternative = operation_alternative_index(operation);
        if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
            identity.read_signal = *read;
        }
        selected->original_instruction_identities.push_back(
            std::move(identity));
    }
    require(selected->instruction > 0U
            && selected->instruction < selected->instruction_count,
        "the selected full-leaf write must retain an instruction before it");
    const auto previous = process.operations.expanded(
        selected->instruction - 1U);
    const auto* previous_read = operation_get_if<ReadSignal>(&previous);
    require(previous_read != nullptr
            && previous_read->signal == *source_signal
            && previous_read->destination == selected->source,
        "the adjacent source read must produce the selected write register");
    require(selected->source < process.register_value_kinds.size()
            && process.register_value_kinds[selected->source]
                == ValueKind::logic4
            && state.signals[*source_signal].value_kind == ValueKind::logic4
            && state.signals[*source_signal].initial_value.width()
                == element_width,
        "the selected source register must be an unconverted full-width Logic4 leaf value");
    const auto* operation_body = process.operations.body_identity();
    process.operations.replace(selected->instruction,
        WriteUpdateSlice { selected->proxy, selected->source,
            selected->offset, selected->domain });
    require(process.operations.body_identity() == operation_body,
        "the source-form rewrite must remain an instance-local operation override");
    process.driver_regions.front() = Process::DriverRegion {
        selected->proxy, selected->offset, selected->width, false };
    auto rewritten = fsim::elaboration::ElaboratedDesign::from_state(
        std::move(state));
    require(rewritten.has_value(),
        "the one-process SimIR source-form rewrite must retain valid design structure");
    project.design = std::move(*rewritten);
    return *selected;
}

void require_alias_proxy_slice_artifact_write(
    const fsim::elaboration::ElaboratedDesign& design,
    const AliasProxySliceArtifactWrite& expected)
{
    require(expected.process < design.process_count(),
        "the artifact must preserve the selected owner process ID");
    const auto& process = design.processes().at(expected.process);
    require(process.id == expected.process
            && process.scheduling_domain
                == ProcessSchedulingDomain::systemverilog
            && expected.instruction_count == process.operations.size()
            && expected.original_instruction_identities.size()
                == expected.instruction_count
            && expected.instruction < process.operations.size(),
        "the artifact must preserve the source instruction owner and domain");
    const auto rewritten_operation
        = process.operations.expanded(expected.instruction);
    const auto* slice = operation_get_if<WriteUpdateSlice>(
        &rewritten_operation);
    require(slice != nullptr
            && slice->signal == expected.proxy
            && slice->source == expected.source
            && slice->offset == expected.offset
            && slice->domain == expected.domain
            && process.driver_regions.size() == 1U
            && process.driver_regions.front()
                == Process::DriverRegion { expected.proxy,
                    expected.offset, expected.width, false },
        "the artifact must retain the exact proxy-slice operation and matching driver provenance");
    for (std::size_t instruction = 0U;
        instruction < expected.instruction_count; ++instruction) {
        if (instruction == expected.instruction) {
            continue;
        }
        const auto operation = process.operations.expanded(instruction);
        const auto& identity
            = expected.original_instruction_identities[instruction];
        require(operation_group_index(operation) == identity.group
                && operation_alternative_index(operation)
                    == identity.alternative,
            "the artifact must preserve neighboring operation alternatives and instruction count");
        if (identity.read_signal) {
            const auto* read = operation_get_if<ReadSignal>(&operation);
            require(read != nullptr
                    && read->destination
                        == identity.read_signal->destination
                    && read->signal == identity.read_signal->signal
                    && read->kind == identity.read_signal->kind
                    && read->ticks == identity.read_signal->ticks
                    && read->clock == identity.read_signal->clock
                    && read->clock_edge
                        == identity.read_signal->clock_edge
                    && read->gate == identity.read_signal->gate,
                "the artifact must preserve neighboring signal-read operands");
        }
    }
}

[[nodiscard]] std::optional<fsim::app::BuiltProject>
build_alias_boundary_artifact_project(
    const fsim::project::Config& source_config,
    const std::filesystem::path& root)
{
    const auto object = root / "alias-boundary-source.fsimobj";
    fsim::diagnostic::Engine compile_diagnostics;
    const bool compiled = fsim::app::compile_artifact(
        source_config, object, compile_diagnostics);
    if (!compiled || compile_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, compile_diagnostics);
        require(false,
            "the public source-compilation phase must produce an alias-boundary object");
    }

    auto artifact_config = source_config;
    artifact_config.source_sets.clear();
    artifact_config.build.cache_path = root / "artifact-cache";
    const auto direct_artifact = root / "direct-alias-boundary.fsimdesign";
    const std::array objects { object };
    fsim::diagnostic::Engine elaborate_diagnostics;
    const bool elaborated = fsim::app::elaborate_artifact(
        artifact_config, objects, direct_artifact, elaborate_diagnostics);
    if (!elaborated || elaborate_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, elaborate_diagnostics);
        require(false,
            "the public object-elaboration phase must publish the direct alias-boundary design");
    }

    fsim::diagnostic::Engine load_diagnostics;
    auto loaded = fsim::app::load_design_artifact(
        direct_artifact, load_diagnostics);
    if (!loaded || load_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, load_diagnostics);
        require(false,
            "the public object-elaboration artifact must reload before SimIR rewriting");
    }
    return loaded;
}

void roundtrip_alias_proxy_slice_artifact(
    const fsim::project::Config& config,
    fsim::app::BuiltProject& project,
    const std::filesystem::path& directory,
    AliasProxySliceArtifactWrite& expected)
{
    expected = rewrite_alias_leaf_write_for_artifact(project);
    require_alias_proxy_slice_artifact_write(project.design, expected);
    fsim::diagnostic::Engine publish_diagnostics;
    require(fsim::app::publish_design_artifact(
                config, project, directory, publish_diagnostics),
        "the rewritten runtime SimIR must publish through the public design artifact API");
    fsim::diagnostic::Engine load_diagnostics;
    auto loaded = fsim::app::load_design_artifact(
        directory, load_diagnostics);
    require(loaded.has_value(),
        "the rewritten runtime SimIR design artifact must reload");
    require_alias_proxy_slice_artifact_write(loaded->design, expected);
    project = std::move(*loaded);
}

[[nodiscard]] bool alias_output_changed(
    const Snapshot& before, const Snapshot& after)
{
    return before.current != after.current
        && after.last == before.current
        && after.stored == after.current
        && after.event != before.event
        && after.transaction != before.transaction
        && after.value_revision > before.value_revision;
}

[[nodiscard]] std::tuple<SimulationTick, std::uint64_t, std::uint64_t>
alias_event_stamp(const Snapshot& snapshot)
{
    require(snapshot.event.has_value(),
        "each selected alias-chain output has an event stamp");
    return { snapshot.event->first, snapshot.event->second,
        snapshot.systemverilog_round };
}

[[nodiscard]] AliasBoundaryRouteResult run_alias_boundary_case(
    const Optimization optimization, const std::filesystem::path& root,
    const SimulationEngine engine, const bool proxy_slice_owner = false)
{
    const bool compiled = engine == SimulationEngine::compiled;
    ScopedEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", compiled ? "1" : "0" };
    ScopedEnvironment local_wave {
        "FSIM_ENABLE_SV_LOCAL_WAVE", compiled ? "1" : "0" };
    ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };

    const auto config = make_alias_boundary_config(optimization, root);
    fsim::diagnostic::Engine diagnostics;
    auto project = proxy_slice_owner
        ? build_alias_boundary_artifact_project(config, root)
        : fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the parsed physical-alias boundary chain must elaborate");
    AliasProxySliceArtifactWrite artifact_write;
    if (proxy_slice_owner) {
        // The source/DesignIR/HIR stay direct-leaf. This synthetic runtime
        // SimIR source-form checks the retained artifact compatibility path.
        auto artifact_config = config;
        artifact_config.source_sets.clear();
        roundtrip_alias_proxy_slice_artifact(artifact_config, *project,
            root / "runtime-simir-artifact", artifact_write);
        project->optimization = optimization;
        project->cache_path = config.build.cache_path;
    }
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, SystemVerilogVpiRuntimeUpdates::omitted);
    if (compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() >= 4U,
            "the four continuous assignments retain compiled executors");
    }

    const auto find = [&](const std::string_view signal_name) {
        const auto signal = simulation.find_signal(signal_name);
        require(signal.has_value(),
            "each physical-alias chain signal must retain a handle");
        return *signal;
    };
    const auto source = find("native_frontier_alias_boundary.source");
    const auto before = find("native_frontier_alias_boundary.before_leaf");
    const auto leaf = find("native_frontier_alias_boundary.words[0]");
    const auto proxy = find("native_frontier_alias_boundary.words");
    const auto after = find("native_frontier_alias_boundary.after_leaf");
    const auto sink = find("native_frontier_alias_boundary.sink");
    const auto sibling = find("native_frontier_alias_boundary.words[1]");
    std::optional<NativeRegionAllocationTestAccess::AliasProxySliceOwner>
        proxy_slice;
    if (proxy_slice_owner) {
        proxy_slice
            = NativeRegionAllocationTestAccess::alias_proxy_slice_writer(
                simulation, proxy, leaf, 8U);
        require(proxy_slice.has_value()
                && proxy_slice->process == artifact_write.process
                && proxy_slice->instruction == artifact_write.instruction,
            "the reloaded artifact must preserve the exact selected proxy-slice owner and instruction");
    }
    const auto leaf_owner = proxy_slice_owner
        ? (proxy_slice
                ? std::optional { proxy_slice->process }
                : std::nullopt)
        : NativeRegionAllocationTestAccess::alias_leaf_writer(simulation, leaf);
    require(leaf_owner.has_value(),
        "the physical leaf has one source owner in every engine");

    simulation.start();
    ScopedBoundaryV2RouteSelection v2_only_route { simulation };
    auto shape = NativeRegionAllocationTestAccess::AliasBoundaryKernelShape { };
    bool forwarding_route_selected { };
    auto forwarding_before = std::uint64_t { };
    if (compiled) {
        simulation.await_all_native_compilation();
        shape = NativeRegionAllocationTestAccess::alias_boundary_kernel_shape(
            simulation, source, before, leaf, proxy, after, sink,
            proxy_slice_owner);
        require(shape.found && shape.member_count >= 4U,
            "the alias leaf is a boundary edge between two private states in one V2 component");
        require(shape.leaf_writer == *leaf_owner,
            "the V2 boundary output retains the physical leaf source owner");
        if (proxy_slice_owner) {
            require(shape.has_original_proxy_slice_write
                    && shape.leaf_writer == artifact_write.process,
                "the V2 boundary binding must retain the rewritten proxy-slice source instruction");
        }
        forwarding_before
            = NativeRegionAllocationTestAccess::forwarding_member_consumptions(
                simulation);
        forwarding_route_selected
            = v2_only_route.select_component(shape.found, shape.component);
        if (forwarding_route_selected) {
            require(v2_only_route.remains_selected(),
                "the quiet selector removes only the competing A2 route");
        }
    }

    require(simulation.run(1U).status == RunStatus::time_limit,
        "the delayed finish must keep the alias-boundary case open");
    if (compiled && forwarding_route_selected) {
        require(v2_only_route.remains_selected(),
            "the initial settle preserves the selected V2 backend");
    }

    const auto capture = [&] {
        return AliasBoundaryFrame {
            NativeRegionAllocationTestAccess::snapshot(simulation, source),
            NativeRegionAllocationTestAccess::snapshot(simulation, before),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, leaf, false, true),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, sibling, false, true),
            NativeRegionAllocationTestAccess::snapshot(
                simulation, proxy, false, true),
            NativeRegionAllocationTestAccess::snapshot(simulation, after),
            NativeRegionAllocationTestAccess::snapshot(simulation, sink),
            NativeRegionAllocationTestAccess::alias_proxy_driver_value(
                simulation, *leaf_owner, proxy),
        };
    };

    AliasBoundaryRouteResult result;
    result.member_count = compiled ? shape.member_count : 0U;
    result.frames.reserve(6U);
    result.frontier_dispatch_deltas.reserve(5U);
    result.frames.push_back(capture());
    constexpr std::array<std::string_view, 5U> stimuli {
        "00000000", "10100101", "XXXXXXXX", "ZZZZZZZZ", "01011010" };
    auto target_time = SimulationTick { 2U };
    for (const auto value : stimuli) {
        const auto prior = result.frames.back();
        require(prior.source.current != value,
            "each external source stimulus changes all eight Logic4 bits");
        const auto dispatch_before = compiled
            ? NativeRegionAllocationTestAccess::native_frontier_dispatches(
                  simulation)
            : 0U;
        simulation.deposit_signal(
            source, PackedLogic4::from_msb_string(value));
        const auto run = simulation.run(target_time++);
        require(run.status == RunStatus::time_limit,
            "the alias-boundary stimulus must settle before the delayed finish");
        if (compiled && forwarding_route_selected) {
            require(v2_only_route.remains_selected(),
                "the source deposit must preserve the V2-only component selection");
            require(NativeRegionAllocationTestAccess::
                        forwarding_member_consumptions(simulation)
                    == forwarding_before,
                "the selected V2 witness must not consume flattened forwarding members");
        }

        const auto frame = capture();
        const auto proxy_value = std::string { value } + "ZZZZZZZZ";
        require(frame.source.current == value
                && frame.before.current == value
                && frame.leaf.current == value
                && frame.after.current == value
                && frame.sink.current == value,
            "the private-before, physical leaf, private-after, and sink signals carry the selected value");
        require(frame.proxy.current == proxy_value
                && frame.proxy.stored == proxy_value
                && frame.proxy.last == prior.proxy.current
                && frame.proxy_driver == proxy_value,
            "the aggregate proxy preserves exact projected current/LAST/stored and raw leaf-driver values");
        auto expected_sibling = prior.sibling;
        // The snapshot clock belongs to the scheduler, not this signal.
        expected_sibling.now = frame.sibling.now;
        expected_sibling.delta = frame.sibling.delta;
        require(frame.sibling == expected_sibling,
            "publishing one physical leaf leaves its sibling value and metadata untouched");

        require(alias_output_changed(prior.before, frame.before)
                && alias_output_changed(prior.leaf, frame.leaf)
                && alias_output_changed(prior.after, frame.after)
                && alias_output_changed(prior.sink, frame.sink)
                && alias_output_changed(prior.proxy, frame.proxy),
            "each selected transition advances all chain and aggregate-proxy event/transaction metadata");
        require(frame.source.last == prior.source.current
                && frame.before.last == prior.before.current
                && frame.leaf.last == prior.leaf.current
                && frame.after.last == prior.after.current
                && frame.sink.last == prior.sink.current,
            "the source and every physical/private output retain the prior value in LAST");

        const auto owner = std::ranges::find(frame.leaf.raw_drivers,
            *leaf_owner, &NativeRegionAllocationTestAccess::RawDriver::process);
        require(owner != frame.leaf.raw_drivers.end()
                && owner->value == value,
            "the checked physical leaf owner record carries the exact new value");
        const auto stamps = std::array {
            alias_event_stamp(frame.source), alias_event_stamp(frame.before),
            alias_event_stamp(frame.leaf), alias_event_stamp(frame.after),
            alias_event_stamp(frame.sink) };
        require(std::ranges::is_sorted(stamps),
            "source and alias-chain event stamps never move backward through checked leaf publication");

        const auto dispatch_after = compiled
            ? NativeRegionAllocationTestAccess::native_frontier_dispatches(
                  simulation)
            : 0U;
        const auto dispatch_delta = dispatch_after - dispatch_before;
        result.frontier_dispatch_deltas.push_back(dispatch_delta);
        if (compiled) {
            require(dispatch_delta == shape.member_count,
                "each external source transition dispatches every member through the V2 frontier");
        }
        result.frames.push_back(frame);
    }
    if (compiled && forwarding_route_selected) {
        require(v2_only_route.restore(),
            "the original A2 forwarding route is restored after the V2 witness");
    }
    return result;
}

void test_o0_and_o2_alias_leaf_v2_boundary()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-alias-boundary-" + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);
    std::array<AliasBoundaryRouteResult, 2U> compiled_cases;
    for (std::size_t index = 0U; index < compiled_cases.size(); ++index) {
        const auto optimization
            = index == 0U ? Optimization::o0 : Optimization::o2;
        const auto optimization_name = index == 0U ? "o0" : "o2";
        const auto case_root = root.path / optimization_name;
        const auto compiled_root = case_root / "compiled";
        const auto interpreter_root = case_root / "interpreter";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(interpreter_root);
        compiled_cases[index] = run_alias_boundary_case(optimization,
            compiled_root, SimulationEngine::compiled);
        const auto interpreted = run_alias_boundary_case(optimization,
            interpreter_root, SimulationEngine::interpreter);
        require(compiled_cases[index].frames == interpreted.frames,
            "O0/O2 V2 and interpreter must match leaf/proxy current, LAST, drivers, events, and transactions");
        require(compiled_cases[index].frontier_dispatch_deltas.size() == 5U
                && std::ranges::all_of(
                    compiled_cases[index].frontier_dispatch_deltas,
                    [&](const std::uint64_t count) {
                        return count == compiled_cases[index].member_count;
                    }),
            "every four-state alias transition must traverse the complete V2 component");
    }
    require(compiled_cases[0U].frames == compiled_cases[1U].frames,
        "the parsed physical-alias boundary trace must match at O0 and O2");
}

void test_o0_and_o2_alias_proxy_slice_v2_boundary()
{
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    TemporaryDirectory root { std::filesystem::temp_directory_path()
        / ("fsim-native-frontier-alias-proxy-slice-"
            + std::to_string(nonce)) };
    std::filesystem::create_directories(root.path);
    std::array<AliasBoundaryRouteResult, 2U> compiled_cases;
    for (std::size_t index = 0U; index < compiled_cases.size(); ++index) {
        const auto optimization
            = index == 0U ? Optimization::o0 : Optimization::o2;
        const auto optimization_name = index == 0U ? "o0" : "o2";
        const auto case_root = root.path / optimization_name;
        const auto compiled_root = case_root / "compiled";
        const auto interpreter_root = case_root / "interpreter";
        const auto artifact_interpreter_root
            = case_root / "artifact-interpreter";
        std::filesystem::create_directories(compiled_root);
        std::filesystem::create_directories(interpreter_root);
        std::filesystem::create_directories(artifact_interpreter_root);
        compiled_cases[index] = run_alias_boundary_case(optimization,
            compiled_root, SimulationEngine::compiled, true);
        const auto source_interpreted = run_alias_boundary_case(
            optimization, interpreter_root, SimulationEngine::interpreter);
        const auto artifact_interpreted = run_alias_boundary_case(
            optimization, artifact_interpreter_root,
            SimulationEngine::interpreter, true);
        require(compiled_cases[index].frames == source_interpreted.frames,
            "proxy-slice V2 O0/O2 and source interpreter must match leaf/proxy current, LAST, stored, driver, event, and transaction state");
        require(compiled_cases[index].frames == artifact_interpreted.frames
                && source_interpreted.frames == artifact_interpreted.frames,
            "the reloaded proxy-slice runtime SimIR artifact must match the source interpreter and compiled V2 trace");
        require(compiled_cases[index].frontier_dispatch_deltas.size() == 5U
                && std::ranges::all_of(
                    compiled_cases[index].frontier_dispatch_deltas,
                    [&](const std::uint64_t count) {
                        return count == compiled_cases[index].member_count;
                    }),
            "each full-leaf proxy-slice publication must dispatch the complete V2 component");
    }
    require(compiled_cases[0U].frames == compiled_cases[1U].frames,
        "the reloaded runtime-SimIR proxy-slice artifact trace must match at O0 and O2");
}

} // namespace

int main()
{
    try {
        test_o0_and_o2_random_dag_frontier();
        test_internal_full_width_boundary_range_readiness();
        test_o0_and_o2_alias_leaf_v2_boundary();
        test_o0_and_o2_alias_proxy_slice_v2_boundary();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native frontier random DAG test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
