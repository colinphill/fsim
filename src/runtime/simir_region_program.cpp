// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_graph.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

namespace fsim::runtime::simir {

std::optional<RegionConeProgram> RegionGraph::build_compute_program(
    const std::size_t component_index,
    const std::span<const Process* const> programs) const
{
    return build_compute_program(component_index, programs, nullptr);
}

std::optional<RegionConeProgram> RegionGraph::build_compute_program(
    const std::size_t component_index,
    const std::span<const Process* const> programs,
    RegionComputeProgramRejection* rejection) const
{
    if (rejection != nullptr) {
        *rejection = { };
        rejection->component = component_index;
    }
    const auto note_rejection = [rejection, component_index](
        const char* reason,
        const SignalId signal,
        const SignalId proxy,
        const ProcessId writer,
        const ProcessId process,
        const std::size_t operation_index,
        const char* operation_type) {
        if (rejection == nullptr) {
            return;
        }
        rejection->reason = reason;
        rejection->component = component_index;
        rejection->signal = signal;
        rejection->family_proxy = proxy;
        rejection->writer_process = writer;
        rejection->process = process;
        rejection->operation_index = operation_index;
        rejection->operation_type = operation_type;
    };
    const auto mark_post_alias_rejection = [rejection, component_index](
        const SignalId signal, const SignalId proxy,
        const ProcessId writer) {
        if (rejection == nullptr) {
            return;
        }
        rejection->reason = "post-alias-program-rejection";
        rejection->component = component_index;
        rejection->signal = signal;
        rejection->family_proxy = proxy;
        rejection->writer_process = writer;
        rejection->process = writer;
    };
    if (component_index >= certificate_inventory_.components.size()
        || programs.size() != processes_.size()
        || !certificate_inventory_.access_inventory_complete
        || !component_epochs_current(component_index)) {
        return std::nullopt;
    }

    const auto& certificate
        = certificate_inventory_.components[component_index];
    if ((certificate.status
            != RegionComponentCertificateStatus::structural_candidate
            && certificate.status
                != RegionComponentCertificateStatus::no_internal_state)
        || certificate.members.empty()) {
        return std::nullopt;
    }

    std::set<ProcessId> member_set;
    for (const auto process : certificate.members) {
        if (process >= programs.size() || !member_set.insert(process).second) {
            return std::nullopt;
        }
        const auto& captured = certificate.captured_epochs;
        const auto epoch = std::ranges::find(captured, process,
            &RegionCertificateEpoch::process);
        if (epoch == captured.end() || epoch->epoch == 0U
            || epoch->epoch != capability_epochs_[process]) {
            return std::nullopt;
        }
    }

    std::vector<ProcessId> ordered_members;
    ordered_members.reserve(member_set.size());
    constexpr auto no_topological_rank
        = std::numeric_limits<std::size_t>::max();
    for (const auto process : member_set) {
        if (process >= topological_rank_by_process_.size()) {
            return std::nullopt;
        }
        const auto rank = topological_rank_by_process_[process];
        if (rank == no_topological_rank || rank >= topological_order_.size()) {
            // Processes absent from the pure topological order, including
            // cycle-dependent and impure processes, keep the checked route.
            return std::nullopt;
        }
        ordered_members.push_back(process);
    }
    std::ranges::sort(ordered_members,
        [&](const ProcessId left, const ProcessId right) {
            return topological_rank_by_process_[left]
                < topological_rank_by_process_[right];
        });
    const auto scheduling_domain
        = processes_[ordered_members.front()].scheduling_domain;
    const auto update_kind = processes_[ordered_members.front()].update_kind;
    const bool systemverilog_active
        = scheduling_domain == ProcessSchedulingDomain::systemverilog
        && update_kind == RegionUpdateKind::systemverilog_active;
    const bool vhdl_projected
        = scheduling_domain == ProcessSchedulingDomain::generic
        && update_kind == RegionUpdateKind::vhdl_projected;
    const bool generic_update
        = scheduling_domain == ProcessSchedulingDomain::generic
        && update_kind == RegionUpdateKind::generic;
    const bool structural_internal_candidate
        = certificate.status
                == RegionComponentCertificateStatus::structural_candidate
        && !certificate.structural_internal_signal_candidates.empty();
    const bool generic_boundary_only_candidate
        = generic_update
        && certificate.status
            == RegionComponentCertificateStatus::no_internal_state
        && certificate.structural_internal_signal_candidates.empty();
    if ((!systemverilog_active && !vhdl_projected && !generic_update)
        || (!structural_internal_candidate
            && !generic_boundary_only_candidate)
        || std::ranges::any_of(ordered_members, [&](const ProcessId process) {
               const auto& node = processes_[process];
               return node.scheduling_domain != scheduling_domain
                   || node.update_kind != update_kind;
           })) {
        return std::nullopt;
    }

    const auto slice_output_candidate = [&](const SignalId signal,
                                            const std::uint32_t offset)
        -> std::optional<SignalId> {
        if (signal >= signals_.size()
            || offset >= signals_[signal].descriptor.width) {
            return std::nullopt;
        }
        const auto family_index = signal_alias_family_by_signal_[signal];
        if (family_index == std::numeric_limits<std::size_t>::max()) {
            return offset == 0U ? std::optional<SignalId> { signal }
                                : std::nullopt;
        }
        if (signal_alias_is_proxy_[signal] == 0U) {
            return offset == 0U ? std::optional<SignalId> { signal }
                                : std::nullopt;
        }
        const auto& family = signal_alias_families_[family_index];
        std::vector<RegionSignalAliasRange> projected;
        if (!family.project_range(offset, 1U, projected)
            || projected.size() != 1U || projected.front().offset != 0U) {
            return std::nullopt;
        }
        return projected.front().signal;
    };
    const auto systemverilog_slice_candidate = [&, systemverilog_active](
        const SignalId signal, const std::uint32_t offset)
        -> std::optional<SignalId> {
        const auto existing = slice_output_candidate(signal, offset);
        if (existing || !systemverilog_active || signal >= signals_.size()
            || offset >= signals_[signal].descriptor.width
            || signal_alias_family_by_signal_[signal]
                != std::numeric_limits<std::size_t>::max()
            || signal_alias_is_proxy_[signal] != 0U) {
            return existing;
        }
        return signal;
    };
    const auto normalized_output_signal = [&](const SignalId signal,
                                              const std::uint32_t offset,
                                              const std::uint32_t width,
                                              const bool allow_proxy_slice)
        -> std::optional<SignalId> {
        if (signal >= signals_.size() || width == 0U) {
            return std::nullopt;
        }
        const auto family_index = signal_alias_family_by_signal_[signal];
        if (family_index == std::numeric_limits<std::size_t>::max()
            || signal_alias_is_proxy_[signal] == 0U) {
            return offset == 0U
                    && width == signals_[signal].descriptor.width
                ? std::optional<SignalId> { signal }
                : std::nullopt;
        }
        if (!allow_proxy_slice) {
            return std::nullopt;
        }
        const auto& family = signal_alias_families_[family_index];
        if (!family.proxy_writable) {
            return std::nullopt;
        }
        std::vector<RegionSignalAliasRange> projected;
        if (!family.project_range(offset, width, projected)
            || projected.size() != 1U || projected.front().offset != 0U
            || projected.front().width != width
            || signals_[projected.front().signal].descriptor.width != width) {
            return std::nullopt;
        }
        return projected.front().signal;
    };
    const auto generic_output_signal = [&](const SignalId signal,
                                           const std::uint32_t offset,
                                           const std::uint32_t width)
        -> std::optional<SignalId> {
        if (signal >= signals_.size() || width == 0U
            || offset > signals_[signal].descriptor.width
            || width > signals_[signal].descriptor.width - offset
            || signal_alias_family_by_signal_[signal]
                != std::numeric_limits<std::size_t>::max()
            || signal_alias_is_proxy_[signal] != 0U) {
            return std::nullopt;
        }
        return signal;
    };
    const auto generic_slice_signal = [&](const SignalId signal,
                                          const std::uint32_t offset)
        -> std::optional<SignalId> {
        if (signal >= signals_.size()
            || offset >= signals_[signal].descriptor.width
            || signal_alias_family_by_signal_[signal]
                != std::numeric_limits<std::size_t>::max()
            || signal_alias_is_proxy_[signal] != 0U) {
            return std::nullopt;
        }
        return signal;
    };
    const auto has_driver_range = [&](const Process& process,
                                      const SignalId signal,
                                      const std::uint32_t offset,
                                      const std::uint32_t width) {
        if (signal >= signals_.size() || width == 0U
            || offset > signals_[signal].descriptor.width
            || width > signals_[signal].descriptor.width - offset) {
            return false;
        }
        // Generic slice admission requires the driver's recorded partial
        // range to match this write exactly. A slice through a whole-driver
        // declaration stays on the checked process route.
        return std::ranges::any_of(process.driver_regions,
            [&](const Process::DriverRegion& writer) {
                return writer.signal == signal && !writer.whole
                    && writer.offset == offset && writer.width == width;
            });
    };
    const auto has_full_driver = [&](const Process& process,
                                     const SignalId signal,
                                     const std::uint32_t width) {
        if (signal >= signals_.size() || width == 0U) {
            return false;
        }
        for (const auto& writer : process.driver_regions) {
            if (writer.signal == signal
                && (writer.whole || (writer.offset == 0U
                    && writer.width == width))) {
                return true;
            }
            if (writer.signal >= signal_alias_family_by_signal_.size()) {
                continue;
            }
            const auto family_index
                = signal_alias_family_by_signal_[writer.signal];
            if (family_index == std::numeric_limits<std::size_t>::max()
                || signal_alias_is_proxy_[writer.signal] == 0U) {
                continue;
            }
            const auto& family = signal_alias_families_[family_index];
            std::vector<RegionSignalAliasRange> projected;
            if (family.project_range(writer.whole ? 0U : writer.offset,
                    writer.whole ? 0U : writer.width, projected)
                && std::ranges::any_of(projected,
                    [signal, width](const RegionSignalAliasRange& range) {
                        return range.signal == signal && range.offset == 0U
                            && range.width == width;
                    })) {
                return true;
            }
        }
        return false;
    };
    const auto supported_member_shape = [&](const Process& program,
                                            const RegionProcessNode& node) {
        if (program.id != node.process
            || node.process >= processes_.size()
            || !node.pure || node.dependencies_unknown
            || node.cyclic_or_dependent_on_cycle
            || node.scheduling_domain != scheduling_domain
            || node.update_kind != update_kind
            || program.scheduling_domain != scheduling_domain
            || program.observed || program.reactive || program.postponed
            || program.final || program.switch_source || program.switch_target
            || program.switch_control || program.switch_bidirectional
            || program.switch_resistive
            || program.drive_strength != DriveStrength { }
            || program.string_register_count != 0U
            || program.container_register_count != 0U
            || !program.debug_locals.empty()
            || !program.debug_string_locals.empty()
            || !program.debug_container_locals.empty()
            || !program.static_trigger_regions.empty()
            || program.static_sensitivity.empty()
            || program.operations.size() < 3U
            || program.register_count
                > std::numeric_limits<RegisterId>::max()
            || (!program.register_value_kinds.empty()
                && program.register_value_kinds.size()
                    != program.register_count)) {
            return false;
        }
        if (!operation_holds<WaitSensitivity>(
                program.operations.expanded(program.operations.size() - 2U))
            || !operation_holds<Jump>(
                program.operations.expanded(program.operations.size() - 1U))
            || operation_get<Jump>(
                program.operations.expanded(program.operations.size() - 1U))
                    .target != 0U) {
            return false;
        }

        const auto sensitivity_shape_is_supported
            = [&](const Sensitivity& sensitivity) {
                if (sensitivity.signal >= signals_.size()
                    || sensitivity.edge != EdgeKind::any) {
                    return false;
                }
                const auto signal_width
                    = signals_[sensitivity.signal].descriptor.width;
                if (signal_width == 0U) {
                    return false;
                }
                if (sensitivity.width == 0U) {
                    return sensitivity.offset == 0U;
                }
                return sensitivity.offset < signal_width
                    && sensitivity.width
                        <= signal_width - sensitivity.offset;
            };
        if (std::ranges::any_of(program.static_sensitivity,
                [&](const Sensitivity& sensitivity) {
                    return !sensitivity_shape_is_supported(sensitivity);
                })) {
            return false;
        }

        auto expected_sensitivities = program.static_sensitivity;
        normalize_sensitivities(expected_sensitivities);
        if (expected_sensitivities != node.sensitivities
            || program.driver_regions != node.writes) {
            return false;
        }

        std::vector<Sensitivity> reads = program.static_sensitivity;
        std::set<SignalId> operation_writes;
        std::set<SignalId> blocking_output_signals;
        // This access-analysis pass treats DebugPoint as inert. Inspect
        // its effective const variant before expanded() copies metadata.
        for (std::size_t index = 0U;
             index + 2U < program.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &program.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = program.operations.expanded(index);
            bool valid_access { true };
            visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, ReadSignal>) {
                    if (value.signal >= signals_.size()
                        || signals_[value.signal].descriptor.width == 0U
                        || (vhdl_projected
                            && signals_[value.signal].descriptor.value_kind
                                    != ValueKind::logic4
                            && signals_[value.signal].descriptor.value_kind
                                    != ValueKind::logic9)
                        || value.kind != SignalReadKind::current
                        || value.ticks != 1U || value.clock || value.gate) {
                        valid_access = false;
                    } else {
                        reads.push_back({ value.signal, EdgeKind::any });
                    }
                } else if constexpr (std::is_same_v<Type, WriteBlocking>) {
                    const auto target
                        = !generic_update && systemverilog_active
                        && value.signal < signals_.size()
                        ? normalized_output_signal(value.signal, 0U,
                            signals_[value.signal].descriptor.width, false)
                        : std::nullopt;
                    if (!target
                        || signals_[*target].descriptor.value_kind
                            != ValueKind::logic4
                        || !has_full_driver(program, *target,
                            signals_[*target].descriptor.width)) {
                        valid_access = false;
                    } else {
                        operation_writes.insert(*target);
                        blocking_output_signals.insert(*target);
                    }
                } else if constexpr (std::is_same_v<Type, WriteUpdate>) {
                    const auto target = generic_update
                        ? generic_output_signal(value.signal, 0U,
                            value.signal < signals_.size()
                                ? signals_[value.signal].descriptor.width
                                : 0U)
                        : value.signal < signals_.size()
                            ? normalized_output_signal(value.signal, 0U,
                                signals_[value.signal].descriptor.width, false)
                            : std::nullopt;
                    const bool update_contract = generic_update
                        ? value.domain == SignalUpdateDomain::generic
                        : systemverilog_active
                            && value.domain
                                == SignalUpdateDomain::systemverilog_active;
                    if (!target || !update_contract) {
                        valid_access = false;
                    } else {
                        operation_writes.insert(*target);
                    }
                } else if constexpr (std::is_same_v<Type, WriteUpdateSlice>) {
                    const auto target = generic_update
                        ? generic_slice_signal(value.signal, value.offset)
                        : systemverilog_slice_candidate(
                            value.signal, value.offset);
                    const bool update_contract = generic_update
                        ? value.domain == SignalUpdateDomain::generic
                        : systemverilog_active
                            && value.domain
                                == SignalUpdateDomain::systemverilog_active;
                    if (!target || !update_contract) {
                        valid_access = false;
                    } else {
                        operation_writes.insert(*target);
                    }
                } else if constexpr (std::is_same_v<Type, WriteProjected>) {
                    const auto target = value.signal < signals_.size()
                        ? normalized_output_signal(value.signal, 0U,
                            signals_[value.signal].descriptor.width, false)
                        : std::nullopt;
                    if (!vhdl_projected || !target || value.delay != 0U
                        || value.rejection != 0U
                        || value.mode != ProjectedDelayMode::inertial
                        || (signals_[*target].descriptor.value_kind
                                != ValueKind::logic4
                            && signals_[*target].descriptor.value_kind
                                != ValueKind::logic9)
                        || signals_[*target].descriptor.resolution
                            != ResolutionKind::none) {
                        valid_access = false;
                    } else {
                        operation_writes.insert(*target);
                    }
                }
            }, operation);
            if (!valid_access) {
                return false;
            }
        }
        for (std::size_t index = 0U;
             index + 2U < program.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &program.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = program.operations.expanded(index);
            if (const auto* read = operation_get_if<ReadSignal>(&operation);
                read != nullptr
                && blocking_output_signals.contains(read->signal)) {
                return false;
            }
        }
        normalize_sensitivities(reads);
        if (reads != node.reads) {
            // The supplied source must still match the graph's recorded input
            // set. A dense ProcessId alone is not a dependency proof.
            return false;
        }

        std::set<SignalId> graph_writes;
        for (const auto& writer : node.writes) {
            if (writer.signal >= signal_alias_family_by_signal_.size()) {
                return false;
            }
            const auto family_index
                = signal_alias_family_by_signal_[writer.signal];
            if (family_index == std::numeric_limits<std::size_t>::max()
                || signal_alias_is_proxy_[writer.signal] == 0U) {
                graph_writes.insert(writer.signal);
                continue;
            }
            const auto& family = signal_alias_families_[family_index];
            std::vector<RegionSignalAliasRange> projected;
            if (!family.project_range(writer.whole ? 0U : writer.offset,
                    writer.whole ? 0U : writer.width, projected)) {
                return false;
            }
            for (const auto& range : projected) {
                graph_writes.insert(range.signal);
            }
        }
        return operation_writes == graph_writes;
    };

    for (const auto process : ordered_members) {
        if (programs[process] == nullptr
            || programs[process]->id != process
            || !supported_member_shape(*programs[process], processes_[process])) {
            return std::nullopt;
        }
    }

    struct AliasLeafBoundaryCandidate {
        std::size_t family_index { };
        ProcessId writer_process { };
    };
    std::set<SignalId> internal_signals;
    std::map<SignalId, AliasLeafBoundaryCandidate>
        alias_leaf_boundary_candidates;
    for (const auto signal : certificate.structural_internal_signal_candidates) {
        if (signal >= signals_.size()
            || internal_signals.contains(signal)
            || alias_leaf_boundary_candidates.contains(signal)) {
            return std::nullopt;
        }
        const auto& node = signals_[signal];
        const auto& descriptor = node.descriptor;
        const auto family_index = signal_alias_family_by_signal_[signal];
        if (family_index != std::numeric_limits<std::size_t>::max()
            && systemverilog_active) {
            const auto& family = signal_alias_families_[family_index];
            const auto writer = node.writers.size() == 1U
                ? node.writers.front().process
                : std::numeric_limits<ProcessId>::max();
            if (signal_alias_is_proxy_[signal] != 0U) {
                note_rejection("alias-proxy-candidate", signal,
                    family.proxy, writer, writer,
                    std::numeric_limits<std::size_t>::max(), nullptr);
                return std::nullopt;
            }
            const auto leaf = std::ranges::find(family.leaves, signal,
                &RegionSignalAliasLeaf::signal);
            if (!family.complete || leaf == family.leaves.end()
                || leaf->width == 0U || leaf->width != descriptor.width
                || leaf->offset > family.width
                || leaf->width > family.width - leaf->offset
                || descriptor.value_kind != ValueKind::logic4
                || descriptor.resolution != ResolutionKind::sv_wire
                || node.writers_unknown
                || node.drivers != RegionDriverClass::single_whole
                || node.writers.size() != 1U
                || node.writers.front().offset != 0U
                || (node.writers.front().width != 0U
                    && node.writers.front().width != descriptor.width)
                || !member_set.contains(node.writers.front().process)
                || descriptor.implicit_driver || descriptor.external_driver
                || descriptor.event_variable
                || node.observations != RegionObservation::none
                || node.partial_projected_transactions) {
                note_rejection("alias-leaf-qualification", signal,
                    family.proxy, writer, writer,
                    std::numeric_limits<std::size_t>::max(), nullptr);
                return std::nullopt;
            }
            if (!alias_leaf_boundary_candidates.emplace(signal,
                    AliasLeafBoundaryCandidate {
                        family_index, node.writers.front().process }).second) {
                note_rejection("alias-leaf-duplicate-candidate", signal,
                    family.proxy, writer, writer,
                    std::numeric_limits<std::size_t>::max(), nullptr);
                return std::nullopt;
            }
            mark_post_alias_rejection(signal, family.proxy, writer);
            continue;
        }
        if (!internal_signals.insert(signal).second) {
            return std::nullopt;
        }
        if (node.writers_unknown
            || node.drivers != RegionDriverClass::single_whole
            || node.writers.size() != 1U
            || node.writers.front().offset != 0U
            || (node.writers.front().width != 0U
                && node.writers.front().width != descriptor.width)
            || !member_set.contains(node.writers.front().process)
            || descriptor.width == 0U
            || (descriptor.value_kind != ValueKind::logic4
                && descriptor.value_kind != ValueKind::logic9)
            || (vhdl_projected
                ? descriptor.resolution != ResolutionKind::none
                : descriptor.resolution != ResolutionKind::none
                    && descriptor.resolution != ResolutionKind::sv_wire)
            || descriptor.implicit_driver || descriptor.external_driver
            || descriptor.event_variable
            || node.observations != RegionObservation::none
            || node.partial_projected_transactions) {
            return std::nullopt;
        }
        if (std::ranges::any_of(node.readers,
                [&](const RegionAccess& reader) {
                    return !member_set.contains(reader.process);
                })
            || std::ranges::any_of(node.writers,
                [&](const RegionAccess& writer) {
                    return !member_set.contains(writer.process);
                })) {
            return std::nullopt;
        }
    }

    for (const auto& [signal, candidate]
        : alias_leaf_boundary_candidates) {
        const auto& family = signal_alias_families_[candidate.family_index];
        for (const auto process : ordered_members) {
            const auto& member = *programs[process];
            if (std::ranges::any_of(member.static_sensitivity,
                    [&](const Sensitivity& sensitivity) {
                        return sensitivity.signal == family.proxy;
                    })) {
                note_rejection("alias-proxy-sensitivity", signal,
                    family.proxy, candidate.writer_process, process,
                    std::numeric_limits<std::size_t>::max(), "sensitivity");
                return std::nullopt;
            }
            for (std::size_t index = 0U;
                 index + 2U < member.operations.size(); ++index) {
                if (operation_get_if<DebugPoint>(
                        &member.operations[index]) != nullptr) {
                    continue;
                }
                const auto operation = member.operations.expanded(index);
                if (const auto* read
                    = operation_get_if<ReadSignal>(&operation);
                    read != nullptr && read->signal == family.proxy) {
                    note_rejection("alias-proxy-read", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "ReadSignal");
                    return std::nullopt;
                }
                if (const auto* read
                    = operation_get_if<ReadContainerObject>(&operation);
                    read != nullptr && read->object == family.object) {
                    note_rejection("alias-container-object-read", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "ReadContainerObject");
                    return std::nullopt;
                }
                if (const auto* write
                    = operation_get_if<WriteContainerObject>(&operation);
                    write != nullptr && write->object == family.object) {
                    note_rejection("alias-container-object-write", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "WriteContainerObject");
                    return std::nullopt;
                }
                if (const auto* write
                    = operation_get_if<WriteContainerObjectElement>(&operation);
                    write != nullptr && write->object == family.object) {
                    note_rejection("alias-container-element-write", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "WriteContainerObjectElement");
                    return std::nullopt;
                }
                if (const auto* write
                    = operation_get_if<WriteUpdate>(&operation);
                    write != nullptr && write->signal == family.proxy) {
                    note_rejection("alias-proxy-update-write", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "WriteUpdate");
                    return std::nullopt;
                }
                if (const auto* write
                    = operation_get_if<WriteUpdateSlice>(&operation);
                    write != nullptr && write->signal == family.proxy) {
                    const auto target
                        = write->domain
                                == SignalUpdateDomain::systemverilog_active
                        ? slice_output_candidate(
                            write->signal, write->offset)
                        : std::nullopt;
                    if (!target
                        || signal_alias_family_by_signal_[*target]
                            != candidate.family_index
                        || signal_alias_is_proxy_[*target] != 0U) {
                        note_rejection("alias-proxy-slice-not-leaf-start",
                            signal, family.proxy,
                            candidate.writer_process, process, index,
                            "WriteUpdateSlice");
                        return std::nullopt;
                    }
                    const auto target_leaf = std::ranges::find(
                        family.leaves, *target,
                        &RegionSignalAliasLeaf::signal);
                    const auto& target_node = signals_[*target];
                    const auto& target_descriptor = target_node.descriptor;
                    if (!family.complete || !family.proxy_writable
                        || target_leaf == family.leaves.end()
                        || target_leaf->offset != write->offset
                        || target_leaf->width == 0U
                        || target_leaf->width
                            != target_descriptor.width
                        || target_node.writers_unknown
                        || target_node.drivers
                            != RegionDriverClass::single_whole
                        || target_node.writers.size() != 1U
                        || target_node.writers.front().process != process
                        || target_node.writers.front().offset != 0U
                        || (target_node.writers.front().width != 0U
                            && target_node.writers.front().width
                                != target_descriptor.width)
                        || target_descriptor.value_kind
                            != ValueKind::logic4
                        || target_descriptor.resolution
                            != ResolutionKind::sv_wire
                        || target_descriptor.implicit_driver
                        || target_descriptor.external_driver
                        || target_descriptor.event_variable
                        || !has_full_driver(*programs[process], *target,
                            target_descriptor.width)) {
                        note_rejection("alias-proxy-slice-leaf-owner",
                            signal, family.proxy,
                            candidate.writer_process, process, index,
                            "WriteUpdateSlice");
                        return std::nullopt;
                    }
                }
                if (const auto* write
                    = operation_get_if<WriteBlocking>(&operation);
                    write != nullptr && write->signal == family.proxy) {
                    note_rejection("alias-proxy-blocking-write", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "WriteBlocking");
                    return std::nullopt;
                }
                if (const auto* write
                    = operation_get_if<WriteProjected>(&operation);
                    write != nullptr && write->signal == family.proxy) {
                    note_rejection("alias-proxy-projected-write", signal,
                        family.proxy, candidate.writer_process, process,
                        index, "WriteProjected");
                    return std::nullopt;
                }
            }
        }

        const auto& source = *programs[candidate.writer_process];
        std::size_t direct_full_write_count { };
        for (std::size_t index = 0U;
             index + 2U < source.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &source.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = source.operations.expanded(index);
            if (const auto* update
                = operation_get_if<WriteUpdate>(&operation);
                update != nullptr && update->signal == signal) {
                if (update->domain
                    != SignalUpdateDomain::systemverilog_active) {
                    note_rejection("alias-writer-update-domain", signal,
                        family.proxy, candidate.writer_process,
                        candidate.writer_process, index, "WriteUpdate");
                    return std::nullopt;
                }
                ++direct_full_write_count;
            } else if (const auto* slice
                = operation_get_if<WriteUpdateSlice>(&operation);
                slice != nullptr) {
                const auto target = slice->domain
                        == SignalUpdateDomain::systemverilog_active
                    ? slice_output_candidate(slice->signal, slice->offset)
                    : std::nullopt;
                if ((slice->signal == signal && slice->offset != 0U)
                    || (slice->signal != signal
                        && (slice->signal != family.proxy || !target
                            || *target != signal))) {
                    continue;
                }
                if (slice->domain
                    != SignalUpdateDomain::systemverilog_active) {
                    note_rejection("alias-writer-slice-shape-or-domain",
                        signal, family.proxy, candidate.writer_process,
                        candidate.writer_process, index,
                        "WriteUpdateSlice");
                    return std::nullopt;
                }
                ++direct_full_write_count;
            } else if (const auto* blocking
                = operation_get_if<WriteBlocking>(&operation);
                blocking != nullptr && blocking->signal == signal) {
                note_rejection("alias-writer-blocking-output", signal,
                    family.proxy, candidate.writer_process,
                    candidate.writer_process, index, "WriteBlocking");
                return std::nullopt;
            } else if (const auto* projected
                = operation_get_if<WriteProjected>(&operation);
                projected != nullptr && projected->signal == signal) {
                note_rejection("alias-writer-projected-output", signal,
                    family.proxy, candidate.writer_process,
                    candidate.writer_process, index, "WriteProjected");
                return std::nullopt;
            }
        }
        const auto width = signals_[signal].descriptor.width;
        if (direct_full_write_count != 1U) {
            note_rejection("alias-writer-direct-write-count", signal,
                family.proxy, candidate.writer_process,
                candidate.writer_process,
                std::numeric_limits<std::size_t>::max(), nullptr);
            return std::nullopt;
        }
        if (!has_full_driver(source, signal, width)) {
            note_rejection("alias-writer-driver-region-not-full", signal,
                family.proxy, candidate.writer_process,
                candidate.writer_process,
                std::numeric_limits<std::size_t>::max(), nullptr);
            return std::nullopt;
        }
    }

    if (!alias_leaf_boundary_candidates.empty()
        && internal_signals.empty()) {
        const auto& [signal, candidate]
            = *alias_leaf_boundary_candidates.begin();
        note_rejection("alias-reclassification-left-no-internals", signal,
            signal_alias_families_[candidate.family_index].proxy,
            candidate.writer_process, candidate.writer_process,
            std::numeric_limits<std::size_t>::max(), nullptr);
        return std::nullopt;
    }

    std::set<SignalId> boundary_signals;
    for (const auto signal : certificate.boundary_signals) {
        if (signal >= signals_.size() || internal_signals.contains(signal)
            || !boundary_signals.insert(signal).second) {
            return std::nullopt;
        }
    }
    for (const auto& [signal, candidate]
        : alias_leaf_boundary_candidates) {
        static_cast<void>(candidate);
        if (!boundary_signals.insert(signal).second) {
            note_rejection("alias-leaf-boundary-signal-duplicate", signal,
                signal_alias_families_[candidate.family_index].proxy,
                candidate.writer_process, candidate.writer_process,
                std::numeric_limits<std::size_t>::max(), nullptr);
            return std::nullopt;
        }
    }

    const auto systemverilog_boundary_slice_range = [&, systemverilog_active](
        const SignalId signal, const std::uint32_t offset,
        const std::uint32_t width) -> std::optional<SignalId> {
        if (!systemverilog_active || signal >= signals_.size() || width == 0U
            || offset > signals_[signal].descriptor.width
            || width > signals_[signal].descriptor.width - offset
            || signal_alias_family_by_signal_[signal]
                != std::numeric_limits<std::size_t>::max()
            || signal_alias_is_proxy_[signal] != 0U
            || !boundary_signals.contains(signal)
            || internal_signals.contains(signal)) {
            return std::nullopt;
        }
        return signal;
    };

    std::map<SignalId, std::pair<ProcessId, InstructionIndex>> internal_writers;
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        for (std::size_t index = 0U; index + 2U < source.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &source.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = source.operations.expanded(index);
            std::optional<SignalId> output_signal;
            if (const auto* blocking
                = operation_get_if<WriteBlocking>(&operation)) {
                output_signal = !generic_update && systemverilog_active
                    ? normalized_output_signal(blocking->signal, 0U,
                        blocking->signal < signals_.size()
                            ? signals_[blocking->signal].descriptor.width
                            : 0U, false)
                    : std::nullopt;
            } else if (const auto* write
                = operation_get_if<WriteUpdate>(&operation)) {
                output_signal = generic_update
                    ? generic_output_signal(write->signal, 0U,
                        signals_[write->signal].descriptor.width)
                    : normalized_output_signal(write->signal, 0U,
                        signals_[write->signal].descriptor.width, false);
            } else if (const auto* projected
                = operation_get_if<WriteProjected>(&operation)) {
                output_signal = normalized_output_signal(projected->signal, 0U,
                    signals_[projected->signal].descriptor.width, false);
            } else if (const auto* slice_write
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                output_signal = generic_update
                    ? (slice_write->offset == 0U
                            ? generic_slice_signal(slice_write->signal,
                                slice_write->offset)
                            : std::nullopt)
                    : slice_output_candidate(
                        slice_write->signal, slice_write->offset);
            }
            if (output_signal && internal_signals.contains(*output_signal)) {
                if (index > std::numeric_limits<InstructionIndex>::max()
                    || !internal_writers.emplace(*output_signal,
                                std::pair { process,
                                    static_cast<InstructionIndex>(index) })
                                .second
                    || signals_[*output_signal].writers.size() != 1U
                    || signals_[*output_signal].writers.front().process
                        != process
                    || !has_full_driver(source, *output_signal,
                        signals_[*output_signal].descriptor.width)) {
                    // Repeated writes can have distinct event/transaction
                    // effects even when the final settled value agrees.
                    return std::nullopt;
                }
            }
        }
    }
    if (internal_writers.size() != internal_signals.size()) {
        return std::nullopt;
    }
    std::map<ProcessId, std::size_t> member_position;
    for (std::size_t index = 0U; index < ordered_members.size(); ++index) {
        member_position.emplace(ordered_members[index], index);
    }
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        for (std::size_t index = 0U;
             index + 2U < source.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &source.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = source.operations.expanded(index);
            const auto* read = operation_get_if<ReadSignal>(&operation);
            if (read == nullptr || !internal_signals.contains(read->signal)) {
                continue;
            }
            const auto writer = internal_writers.find(read->signal);
            if (writer == internal_writers.end()
                || writer->second.first == process
                || member_position.at(writer->second.first)
                    >= member_position.at(process)) {
                return std::nullopt;
            }
        }
    }

    RegionConeProgram result;
    result.component_index = component_index;
    result.members = ordered_members;
    Process settled_program;
    std::size_t register_total { };
    std::map<ProcessId, RegisterId> register_bases;
    for (const auto process : ordered_members) {
        const auto count = programs[process]->register_count;
        if (count > std::numeric_limits<RegisterId>::max() - register_total) {
            return std::nullopt;
        }
        register_bases.emplace(process,
            static_cast<RegisterId>(register_total));
        register_total += count;
    }
    const auto allocate_register = [&]() -> std::optional<RegisterId> {
        if (register_total >= std::numeric_limits<RegisterId>::max()) {
            return std::nullopt;
        }
        return static_cast<RegisterId>(register_total++);
    };

    std::map<SignalId, RegisterId> materialization_registers;
    for (const auto signal : internal_signals) {
        const auto destination = allocate_register();
        if (!destination) {
            return std::nullopt;
        }
        materialization_registers.emplace(signal, *destination);
    }

    using OutputKey = std::pair<ProcessId, InstructionIndex>;
    std::map<OutputKey, RegisterId> output_registers;
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        for (std::size_t index = 0U; index + 2U < source.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &source.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = source.operations.expanded(index);
            std::optional<SignalId> output_signal;
            if (const auto* blocking
                = operation_get_if<WriteBlocking>(&operation)) {
                output_signal = !generic_update && systemverilog_active
                    ? normalized_output_signal(blocking->signal, 0U,
                        blocking->signal < signals_.size()
                            ? signals_[blocking->signal].descriptor.width
                            : 0U, false)
                    : std::nullopt;
            } else if (const auto* write
                = operation_get_if<WriteUpdate>(&operation)) {
                output_signal = generic_update
                    ? generic_output_signal(write->signal, 0U,
                        signals_[write->signal].descriptor.width)
                    : normalized_output_signal(write->signal, 0U,
                        signals_[write->signal].descriptor.width, false);
            } else if (const auto* projected
                = operation_get_if<WriteProjected>(&operation)) {
                output_signal = normalized_output_signal(projected->signal, 0U,
                    signals_[projected->signal].descriptor.width, false);
            } else if (const auto* slice_write
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                output_signal = generic_update
                    ? generic_slice_signal(slice_write->signal,
                        slice_write->offset)
                    : systemverilog_slice_candidate(
                        slice_write->signal, slice_write->offset);
            }
            if (!output_signal || internal_signals.contains(*output_signal)) {
                continue;
            }
            bool has_partial_boundary_slice_driver { };
            if (systemverilog_active) {
                if (const auto* slice
                    = operation_get_if<WriteUpdateSlice>(&operation);
                    slice != nullptr
                    && slice->signal == *output_signal
                    && slice->domain
                        == SignalUpdateDomain::systemverilog_active) {
                    const auto& writers = signals_[*output_signal].writers;
                    has_partial_boundary_slice_driver
                        = std::ranges::any_of(source.driver_regions,
                            [&](const Process::DriverRegion& driver) {
                                if (driver.signal != *output_signal
                                    || driver.whole
                                    || driver.offset != slice->offset
                                    || (driver.offset == 0U
                                        && driver.width
                                            == signals_[*output_signal]
                                                .descriptor.width)
                                    || !systemverilog_boundary_slice_range(
                                        *output_signal, driver.offset,
                                        driver.width)) {
                                    return false;
                                }
                                return std::ranges::any_of(writers,
                                    [&](const RegionAccess& writer) {
                                        return writer.process == process
                                            && writer.width != 0U
                                            && writer.offset == driver.offset
                                            && writer.width == driver.width;
                                    });
                            });
                }
            }
            if (!boundary_signals.contains(*output_signal)
                || index > std::numeric_limits<InstructionIndex>::max()
                || signals_[*output_signal].descriptor.width == 0U
                || (!generic_update
                    && !has_full_driver(source, *output_signal,
                        signals_[*output_signal].descriptor.width)
                    && !has_partial_boundary_slice_driver)) {
                return std::nullopt;
            }
            if (generic_update) {
                bool matching_source { };
                if (operation_holds<WriteUpdate>(operation)) {
                    matching_source = has_full_driver(source, *output_signal,
                        signals_[*output_signal].descriptor.width);
                } else if (const auto* slice
                    = operation_get_if<WriteUpdateSlice>(&operation)) {
                    matching_source = std::ranges::any_of(
                        source.driver_regions,
                        [&](const Process::DriverRegion& writer) {
                            return writer.signal == *output_signal
                                && !writer.whole
                                && writer.offset == slice->offset;
                        });
                }
                if (!matching_source) {
                    return std::nullopt;
                }
            }
            const auto destination = allocate_register();
            if (!destination
                || !output_registers.emplace(
                    OutputKey { process,
                        static_cast<InstructionIndex>(index) },
                    *destination).second) {
                return std::nullopt;
            }
        }
    }

    settled_program.name = "simir_region_compute_"
        + std::to_string(component_index);
    settled_program.initialize = false;
    settled_program.scheduling_domain = scheduling_domain;
    settled_program.register_count = register_total;
    settled_program.register_value_kinds.reserve(register_total);
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        const auto register_value_kinds
            = process_layout_detail::ProcessLayoutAccess::view(
                source.register_value_kinds);
        if (register_value_kinds.empty()) {
            settled_program.register_value_kinds.insert(
                settled_program.register_value_kinds.end(),
                source.register_count, ValueKind::logic4);
        } else {
            settled_program.register_value_kinds.insert(
                settled_program.register_value_kinds.end(),
                register_value_kinds.begin(),
                register_value_kinds.end());
        }
    }
    for (const auto signal : internal_signals) {
        settled_program.register_value_kinds.push_back(
            signals_[signal].descriptor.value_kind);
    }
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        for (std::size_t index = 0U; index + 2U < source.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &source.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = source.operations.expanded(index);
            std::optional<SignalId> output_signal;
            if (const auto* blocking
                = operation_get_if<WriteBlocking>(&operation)) {
                output_signal = !generic_update && systemverilog_active
                    ? normalized_output_signal(blocking->signal, 0U,
                        blocking->signal < signals_.size()
                            ? signals_[blocking->signal].descriptor.width
                            : 0U, false)
                    : std::nullopt;
            } else if (const auto* write
                = operation_get_if<WriteUpdate>(&operation)) {
                output_signal = generic_update
                    ? generic_output_signal(write->signal, 0U,
                        signals_[write->signal].descriptor.width)
                    : normalized_output_signal(write->signal, 0U,
                        signals_[write->signal].descriptor.width, false);
            } else if (const auto* projected
                = operation_get_if<WriteProjected>(&operation)) {
                output_signal = normalized_output_signal(projected->signal, 0U,
                    signals_[projected->signal].descriptor.width, false);
            } else if (const auto* slice_write
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                output_signal = generic_update
                    ? generic_slice_signal(slice_write->signal,
                        slice_write->offset)
                    : systemverilog_slice_candidate(
                        slice_write->signal, slice_write->offset);
            }
            if (output_signal && !internal_signals.contains(*output_signal)) {
                settled_program.register_value_kinds.push_back(
                    signals_[*output_signal].descriptor.value_kind);
            }
        }
    }
    if (settled_program.register_value_kinds.size() != register_total) {
        return std::nullopt;
    }

    std::vector<Sensitivity> boundary_sensitivities;
    for (const auto process : ordered_members) {
        const auto& node = processes_[process];
        for (const auto& sensitivity : node.reads) {
            if (!internal_signals.contains(sensitivity.signal)) {
                boundary_sensitivities.push_back(sensitivity);
            }
        }
    }
    normalize_sensitivities(boundary_sensitivities);
    result.boundary_sensitivities = std::move(boundary_sensitivities);

    std::set<SignalId> materialized_signals;
    std::map<ProcessId, std::vector<std::uint8_t>>
        defined_member_registers;
    std::map<ProcessId, std::vector<std::uint32_t>>
        member_register_widths;
    std::map<ProcessId, std::optional<RegionConeFinalDebugState>>
        final_debug_states;
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        const auto base = register_bases.at(process);
        std::vector<std::uint32_t> widths(source.register_count, 0U);
        const auto source_value_kinds
            = process_layout_detail::ProcessLayoutAccess::view(
                source.register_value_kinds);
        const auto is_logic4_register = [&](const RegisterId id) {
            return id < source.register_count
                && (source_value_kinds.empty()
                    || source_value_kinds[id] == ValueKind::logic4);
        };
        const auto register_width = [&](RegisterId id)
            -> std::optional<std::uint32_t> {
            if (id >= widths.size() || widths[id] == 0U) {
                return std::nullopt;
            }
            return widths[id];
        };
        const auto assign_width = [&](RegisterId id, std::uint64_t width) {
            if (id >= widths.size() || width == 0U
                || width > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            widths[id] = static_cast<std::uint32_t>(width);
            return true;
        };
        const auto global_register = [&](RegisterId id)
            -> std::optional<RegisterId> {
            if (id >= source.register_count) {
                return std::nullopt;
            }
            const auto global = std::uint64_t { base } + id;
            if (global >= std::numeric_limits<RegisterId>::max()) {
                return std::nullopt;
            }
            return static_cast<RegisterId>(global);
        };

        if (settled_program.operations.size()
            > std::numeric_limits<InstructionIndex>::max()) {
            return std::nullopt;
        }
        RegionConeMemberSpan member_span;
        member_span.process = process;
        member_span.begin = static_cast<InstructionIndex>(
            settled_program.operations.size());
        member_span.initialize = source.initialize;
        member_span.sensitivities = processes_[process].sensitivities;
        std::optional<RegionConeFinalDebugState> final_debug_state;

        for (std::size_t index = 0U; index + 2U < source.operations.size(); ++index) {
            const auto operation = source.operations.expanded(index);
            std::optional<Operation> rewritten;
            bool accepted = visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, DebugPoint>) {
                    rewritten.emplace(value);
                    final_debug_state = RegionConeFinalDebugState {
                        value.source, value.scope.str() };
                    return true;
                } else if constexpr (std::is_same_v<Type, LoadConstant>) {
                    auto destination = global_register(value.destination);
                    if (!destination || !assign_width(
                            value.destination, value.value.width())) {
                        return false;
                    }
                    auto copy = value;
                    copy.destination = *destination;
                    rewritten.emplace(std::move(copy));
                    return true;
                } else if constexpr (std::is_same_v<Type, ReadSignal>) {
                    auto destination = global_register(value.destination);
                    if (!destination || value.signal >= signals_.size()
                        || value.kind != SignalReadKind::current
                        || value.ticks != 1U || value.clock || value.gate
                        || !assign_width(value.destination,
                            signals_[value.signal].descriptor.width)) {
                        return false;
                    }
                    if (internal_signals.contains(value.signal)) {
                        rewritten.emplace(CopyRegister {
                            *destination,
                            materialization_registers.at(value.signal) });
                    } else {
                        if (!boundary_signals.contains(value.signal)) {
                            return false;
                        }
                        auto copy = value;
                        copy.destination = *destination;
                        rewritten.emplace(std::move(copy));
                    }
                    return true;
                } else if constexpr (std::is_same_v<Type, CopyRegister>) {
                    const auto width = register_width(value.source);
                    auto destination = global_register(value.destination);
                    auto source_register = global_register(value.source);
                    if (!width || !destination || !source_register
                        || !assign_width(value.destination, *width)) {
                        return false;
                    }
                    rewritten.emplace(CopyRegister {
                        *destination, *source_register });
                    return true;
                } else if constexpr (std::is_same_v<Type, UnaryNot>) {
                    const auto width = register_width(value.source);
                    auto destination = global_register(value.destination);
                    auto source_register = global_register(value.source);
                    if (!width || !destination || !source_register
                        || !assign_width(value.destination, *width)) {
                        return false;
                    }
                    rewritten.emplace(UnaryNot {
                        *destination, *source_register });
                    return true;
                } else if constexpr (std::is_same_v<Type, Shift>) {
                    const auto value_width = register_width(value.value);
                    const auto amount_width = register_width(value.amount);
                    auto destination = global_register(value.destination);
                    auto source_register = global_register(value.value);
                    auto amount_register = global_register(value.amount);
                    const bool valid_operation
                        = value.operation == ShiftOperator::logical_left
                        || value.operation == ShiftOperator::logical_right
                        || value.operation == ShiftOperator::arithmetic_right
                        || value.operation == ShiftOperator::arithmetic_left
                        || value.operation == ShiftOperator::rotate_left
                        || value.operation == ShiftOperator::rotate_right;
                    if (!valid_operation || !value_width || !amount_width
                        || !destination || !source_register
                        || !amount_register
                        || !assign_width(value.destination, *value_width)) {
                        return false;
                    }
                    auto copy = value;
                    copy.destination = *destination;
                    copy.value = *source_register;
                    copy.amount = *amount_register;
                    rewritten.emplace(std::move(copy));
                    return true;
                } else if constexpr (std::is_same_v<Type, Binary>) {
                    const bool bitwise_operation
                        = value.operation == BinaryOperator::bit_and
                        || value.operation == BinaryOperator::bit_or
                        || value.operation == BinaryOperator::bit_xor;
                    const bool supported_add
                        = value.operation == BinaryOperator::add_unsigned;
                    if (!bitwise_operation && !supported_add) {
                        return false;
                    }
                    const auto lhs_width = register_width(value.lhs);
                    const auto rhs_width = register_width(value.rhs);
                    const auto existing_destination_width
                        = register_width(value.destination);
                    auto destination = global_register(value.destination);
                    auto lhs = global_register(value.lhs);
                    auto rhs = global_register(value.rhs);
                    if (!lhs_width || !rhs_width || !destination || !lhs
                        || !rhs
                        || (supported_add
                            && (*lhs_width != *rhs_width
                                || !is_logic4_register(value.lhs)
                                || !is_logic4_register(value.rhs)
                                || !is_logic4_register(value.destination)
                                || (existing_destination_width
                                    && *existing_destination_width
                                        != *lhs_width)))
                        || !assign_width(value.destination,
                            supported_add ? *lhs_width
                                          : std::max(*lhs_width, *rhs_width))) {
                        return false;
                    }
                    auto copy = value;
                    copy.destination = *destination;
                    copy.lhs = *lhs;
                    copy.rhs = *rhs;
                    rewritten.emplace(std::move(copy));
                    return true;
                } else if constexpr (std::is_same_v<Type, Reduction>) {
                    if (value.operation != ReductionOperator::bit_and
                        && value.operation != ReductionOperator::bit_or
                        && value.operation != ReductionOperator::bit_xor) {
                        return false;
                    }
                    const auto width = register_width(value.source);
                    auto destination = global_register(value.destination);
                    auto source_register = global_register(value.source);
                    if (!width || !destination || !source_register
                        || !assign_width(value.destination, 1U)) {
                        return false;
                    }
                    auto copy = value;
                    copy.destination = *destination;
                    copy.source = *source_register;
                    rewritten.emplace(std::move(copy));
                    return true;
                } else if constexpr (std::is_same_v<Type, Extract>) {
                    const auto source_width = register_width(value.source);
                    auto destination = global_register(value.destination);
                    auto source_register = global_register(value.source);
                    if (!source_width || !destination || !source_register
                        || value.width == 0U || value.offset >= *source_width
                        || value.width > *source_width - value.offset
                        || !assign_width(value.destination, value.width)) {
                        return false;
                    }
                    auto copy = value;
                    copy.destination = *destination;
                    copy.source = *source_register;
                    rewritten.emplace(std::move(copy));
                    return true;
                } else if constexpr (std::is_same_v<Type, Concatenate>) {
                    std::uint64_t total_width { };
                    auto copy = value;
                    for (auto& operand : copy.operands) {
                        const auto width = register_width(operand);
                        auto global = global_register(operand);
                        if (!width || !global
                            || *width > std::numeric_limits<std::uint32_t>::max()
                                - total_width) {
                            return false;
                        }
                        total_width += *width;
                        operand = *global;
                    }
                    auto destination = global_register(value.destination);
                    if (copy.operands.empty() || !destination
                        || total_width != value.width
                        || !assign_width(value.destination, total_width)) {
                        return false;
                    }
                    copy.destination = *destination;
                    rewritten.emplace(std::move(copy));
                    return true;
                } else if constexpr (std::is_same_v<Type, ConditionalSelect>) {
                    const auto condition_width
                        = register_width(value.condition);
                    const auto true_width
                        = register_width(value.when_true);
                    const auto false_width
                        = register_width(value.when_false);
                    const auto existing_destination_width
                        = register_width(value.destination);
                    const auto destination
                        = global_register(value.destination);
                    const auto condition = global_register(value.condition);
                    const auto when_true = global_register(value.when_true);
                    const auto when_false = global_register(value.when_false);
                    if (!condition_width || *condition_width != 1U
                        || !true_width || !false_width
                        || *true_width == 0U || *true_width != *false_width
                        || (existing_destination_width
                            && *existing_destination_width != *true_width)
                        || !destination || !condition || !when_true
                        || !when_false
                        || !is_logic4_register(value.condition)
                        || !is_logic4_register(value.when_true)
                        || !is_logic4_register(value.when_false)
                        || !is_logic4_register(value.destination)
                        || !assign_width(value.destination, *true_width)) {
                        return false;
                    }
                    rewritten.emplace(ConditionalSelect {
                        *destination, *condition, *when_true, *when_false });
                    return true;
                } else if constexpr (std::is_same_v<Type, WriteBlocking>
                    || std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteUpdateSlice>
                    || std::is_same_v<Type, WriteProjected>) {
                    const auto width = register_width(value.source);
                    auto source_register = global_register(value.source);
                    std::optional<SignalId> output_signal;
                    if (width) {
                        if constexpr (std::is_same_v<Type, WriteBlocking>) {
                            output_signal = !generic_update
                                    && systemverilog_active
                                ? normalized_output_signal(value.signal,
                                    0U, *width, false)
                                : std::nullopt;
                        } else if constexpr (std::is_same_v<Type, WriteProjected>) {
                            output_signal = normalized_output_signal(
                                value.signal, 0U, *width, false);
                        } else if constexpr (std::is_same_v<Type, WriteUpdateSlice>) {
                            if (generic_update) {
                                output_signal = generic_output_signal(
                                    value.signal, value.offset, *width);
                            } else {
                                output_signal = normalized_output_signal(
                                    value.signal, value.offset, *width, true);
                                if (!output_signal) {
                                    output_signal
                                        = systemverilog_boundary_slice_range(
                                            value.signal, value.offset, *width);
                                }
                            }
                        } else {
                            output_signal = generic_update
                                ? generic_output_signal(value.signal, 0U,
                                    *width)
                                : normalized_output_signal(value.signal, 0U,
                                    *width, false);
                        }
                    }
                    bool update_contract_valid { };
                    if constexpr (std::is_same_v<Type, WriteBlocking>) {
                        update_contract_valid = !generic_update
                            && systemverilog_active;
                    } else if constexpr (std::is_same_v<Type, WriteProjected>) {
                        update_contract_valid = vhdl_projected
                                && value.delay == 0U
                                && value.rejection == 0U
                                && value.mode
                                    == ProjectedDelayMode::inertial;
                    } else if constexpr (std::is_same_v<Type, WriteUpdate>
                        || std::is_same_v<Type, WriteUpdateSlice>) {
                        if (generic_update) {
                            update_contract_valid
                                = value.domain == SignalUpdateDomain::generic;
                        } else {
                            update_contract_valid = systemverilog_active
                                && value.domain
                                    == SignalUpdateDomain::systemverilog_active;
                        }
                    }
                    std::uint32_t output_offset { };
                    bool output_range_valid { };
                    bool systemverilog_partial_boundary_slice { };
                    if constexpr (std::is_same_v<Type, WriteUpdateSlice>) {
                        if (generic_update && output_signal && width) {
                            output_offset = value.offset;
                            output_range_valid = has_driver_range(source,
                                *output_signal, output_offset, *width);
                            const auto& writers
                                = signals_[*output_signal].writers;
                            output_range_valid = output_range_valid
                                && std::ranges::any_of(writers,
                                    [&](const RegionAccess& writer) {
                                        return writer.process == process
                                            && writer.offset == output_offset
                                            && writer.width == *width;
                                    });
                        } else if (systemverilog_active && output_signal
                            && width) {
                            const auto direct_boundary
                                = systemverilog_boundary_slice_range(
                                    value.signal, value.offset, *width);
                            if (direct_boundary
                                && *direct_boundary == *output_signal
                                && (value.offset != 0U
                                    || *width
                                        != signals_[*output_signal]
                                            .descriptor.width)) {
                                output_offset = value.offset;
                                const auto& writers
                                    = signals_[*output_signal].writers;
                                output_range_valid = has_driver_range(source,
                                    *output_signal, output_offset, *width)
                                    && std::ranges::any_of(writers,
                                        [&](const RegionAccess& writer) {
                                            return writer.process == process
                                                && writer.width != 0U
                                                && writer.offset
                                                    == output_offset
                                                && writer.width == *width;
                                        });
                                systemverilog_partial_boundary_slice = true;
                            }
                        }
                    }
                    if constexpr (std::is_same_v<Type, WriteUpdate>) {
                        if (generic_update && output_signal && width) {
                            output_range_valid = *width
                                    == signals_[*output_signal].descriptor.width
                                && has_full_driver(source, *output_signal,
                                    *width);
                        }
                    }
                    if constexpr (std::is_same_v<Type, WriteBlocking>) {
                        if (output_signal && width) {
                            output_range_valid = *width
                                    == signals_[*output_signal]
                                        .descriptor.width
                                && has_full_driver(source, *output_signal,
                                    *width);
                        }
                    }
                    const bool shape_valid = generic_update
                        ? output_signal && width && output_range_valid
                            && output_offset <= signals_[*output_signal]
                                .descriptor.width
                            && *width <= signals_[*output_signal]
                                .descriptor.width - output_offset
                        : systemverilog_partial_boundary_slice
                            ? output_signal && width && output_range_valid
                                && output_offset <= signals_[*output_signal]
                                    .descriptor.width
                                && *width <= signals_[*output_signal]
                                    .descriptor.width - output_offset
                            : output_signal && width
                                && signals_[*output_signal].descriptor.width
                                    == *width
                                && has_full_driver(source,
                                    *output_signal, *width);
                    if (!width || !source_register || !output_signal
                        || !update_contract_valid || !shape_valid) {
                        return false;
                    }
                    if constexpr (std::is_same_v<Type, WriteBlocking>) {
                        if (signals_[*output_signal].descriptor.value_kind
                            != ValueKind::logic4) {
                            return false;
                        }
                    }
                    const bool internal
                        = internal_signals.contains(*output_signal);
                    if (generic_update && internal
                        && (output_offset != 0U
                            || *width
                                != signals_[*output_signal].descriptor.width)) {
                        return false;
                    }
                    const auto key = OutputKey { process,
                        static_cast<InstructionIndex>(index) };
                    RegisterId snapshot { };
                    if (internal) {
                        const auto destination
                            = materialization_registers.find(*output_signal);
                        if (destination == materialization_registers.end()
                            || internal_writers.at(*output_signal)
                                != key) {
                            return false;
                        }
                        snapshot = destination->second;
                    } else {
                        const auto output = output_registers.find(key);
                        if (!boundary_signals.contains(*output_signal)
                            || output == output_registers.end()) {
                            return false;
                        }
                        snapshot = output->second;
                    }
                    rewritten.emplace(CopyRegister {
                        snapshot, *source_register });
                    return true;
                }
                return false;
            }, operation);
            if (!accepted || !rewritten) {
                return std::nullopt;
            }

            if (settled_program.operations.size()
                >= std::numeric_limits<InstructionIndex>::max()) {
                return std::nullopt;
            }
            const auto compute_instruction = static_cast<InstructionIndex>(
                settled_program.operations.size());
            settled_program.operations.push_back(std::move(*rewritten));
            std::optional<RegionConeOutputBinding> output_binding;
            visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, WriteBlocking>
                    || std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteUpdateSlice>
                    || std::is_same_v<Type, WriteProjected>) {
                    const auto width = register_width(value.source);
                    std::optional<SignalId> target;
                    std::uint32_t output_offset { };
                    if (width) {
                        if constexpr (std::is_same_v<Type, WriteBlocking>) {
                            target = !generic_update && systemverilog_active
                                ? normalized_output_signal(value.signal,
                                    0U, *width, false)
                                : std::nullopt;
                        } else if constexpr (std::is_same_v<Type, WriteProjected>) {
                            target = normalized_output_signal(
                                value.signal, 0U, *width, false);
                        } else if constexpr (std::is_same_v<Type, WriteUpdateSlice>) {
                            if (generic_update) {
                                output_offset = value.offset;
                                target = generic_output_signal(value.signal,
                                    value.offset, *width);
                            } else {
                                target = normalized_output_signal(
                                    value.signal, value.offset, *width, true);
                                if (!target) {
                                    target = systemverilog_boundary_slice_range(
                                        value.signal, value.offset, *width);
                                    if (target) {
                                        output_offset = value.offset;
                                    }
                                }
                            }
                        } else {
                            target = generic_update
                                ? generic_output_signal(value.signal, 0U,
                                    *width)
                                : normalized_output_signal(value.signal, 0U,
                                    *width, false);
                        }
                    }
                    if (target && width) {
                        const bool internal = internal_signals.contains(*target);
                        const auto source_instruction
                            = static_cast<InstructionIndex>(index);
                        const auto value_register = internal
                            ? materialization_registers.at(*target)
                            : output_registers.at({ process,
                                source_instruction });
                        const auto output_domain = [&] {
                            if constexpr (std::is_same_v<Type, WriteProjected>) {
                                return SignalUpdateDomain::generic;
                            } else if constexpr (
                                std::is_same_v<Type, WriteBlocking>) {
                                return SignalUpdateDomain::systemverilog_active;
                            } else {
                                return value.domain;
                            }
                        }();
                        const auto output_update_kind
                            = std::is_same_v<Type, WriteProjected>
                            ? RegionUpdateKind::vhdl_projected
                            : update_kind;
                        const auto publication_kind
                            = std::is_same_v<Type, WriteBlocking>
                            ? RegionOutputPublicationKind::blocking_immediate
                            : RegionOutputPublicationKind::update;
                        const auto projected_mode = [&] {
                            if constexpr (std::is_same_v<Type, WriteProjected>) {
                                return value.mode;
                            } else {
                                return ProjectedDelayMode::inertial;
                            }
                        }();
                        const auto projected_delay = [&] {
                            if constexpr (std::is_same_v<Type, WriteProjected>) {
                                return value.delay;
                            } else {
                                return SimulationTick { };
                            }
                        }();
                        const auto projected_rejection = [&] {
                            if constexpr (std::is_same_v<Type, WriteProjected>) {
                                return value.rejection;
                            } else {
                                return SimulationTick { };
                            }
                        }();
                        output_binding.emplace(RegionConeOutputBinding {
                            process,
                            *target,
                            output_offset,
                            *width,
                            signals_[*target].descriptor.value_kind,
                            output_domain,
                            value_register,
                            source_instruction,
                            compute_instruction,
                            0U,
                            output_update_kind,
                            projected_mode,
                            projected_delay,
                            projected_rejection,
                            publication_kind,
                            signals_[*target].descriptor.width,
                        });
                    }
                }
            }, operation);
            if (output_binding) {
                auto binding = std::move(*output_binding);
                const bool internal
                    = internal_signals.contains(binding.signal);
                if (internal) {
                    if (!materialized_signals.insert(binding.signal).second) {
                        return std::nullopt;
                    }
                    result.internal_materializations.push_back(binding);
                } else {
                    result.boundary_outputs.push_back(binding);
                }
            }
        }

        if (settled_program.operations.size()
            > std::numeric_limits<InstructionIndex>::max()) {
            return std::nullopt;
        }
        member_span.end = static_cast<InstructionIndex>(
            settled_program.operations.size());
        result.member_spans.push_back(std::move(member_span));
        auto& defined_registers = defined_member_registers[process];
        defined_registers.reserve(widths.size());
        for (const auto width : widths) {
            defined_registers.push_back(
                static_cast<std::uint8_t>(width != 0U));
        }
        member_register_widths.emplace(process, std::move(widths));
        final_debug_states.emplace(process, std::move(final_debug_state));
    }

    if (materialized_signals != internal_signals
        || settled_program.operations.size()
            >= std::numeric_limits<InstructionIndex>::max()) {
        return std::nullopt;
    }
    settled_program.operations.push_back(Halt { });

    // Build the runtime form separately from the topological settled-value
    // oracle above. Every selected member reads the same committed input
    // registers, while every write captures a distinct owner-tagged output
    // register. There is deliberately no writer-to-reader forwarding inside
    // this kernel.
    RegionConeActivationKernel kernel;
    kernel.program = settled_program;
    kernel.program.name = "simir_region_activation_"
        + std::to_string(component_index);
    kernel.program.operations = OperationList { };
    kernel.program.static_sensitivity.clear();
    kernel.program.driver_regions.clear();
    kernel.program.initialize = true;
    kernel.internal_signals.assign(internal_signals.begin(),
        internal_signals.end());

    std::set<SignalId> input_signals;
    for (const auto process : ordered_members) {
        const auto& source = *programs[process];
        for (std::size_t index = 0U; index + 2U < source.operations.size(); ++index) {
            if (operation_get_if<DebugPoint>(
                    &source.operations[index]) != nullptr) {
                continue;
            }
            const auto operation = source.operations.expanded(index);
            if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                if (!input_signals.insert(read->signal).second) {
                    continue;
                }
            }
        }
    }
    if (generic_update) {
        for (const auto& output : result.boundary_outputs) {
            const auto signal_width
                = signals_[output.signal].descriptor.width;
            if ((output.offset != 0U || output.width != signal_width)
                && input_signals.contains(output.signal)) {
                return std::nullopt;
            }
        }
    }
    std::map<SignalId, RegisterId> input_registers;
    for (const auto signal : input_signals) {
        const auto input = allocate_register();
        if (!input || signal >= signals_.size()
            || !input_registers.emplace(signal, *input).second) {
            return std::nullopt;
        }
        kernel.inputs.push_back({ signal, *input,
            signals_[signal].descriptor.width,
            signals_[signal].descriptor.value_kind,
            internal_signals.contains(signal) });
        kernel.program.register_value_kinds.push_back(
            signals_[signal].descriptor.value_kind);
    }

    auto kernel_members = ordered_members;
    std::ranges::sort(kernel_members);
    std::map<ProcessId, RegisterId> readiness_registers;
    for (const auto process : kernel_members) {
        const auto readiness = allocate_register();
        if (!readiness
            || !readiness_registers.emplace(process, *readiness).second) {
            return std::nullopt;
        }
        kernel.program.register_value_kinds.push_back(ValueKind::logic4);
    }
    kernel.program.register_count = register_total;
    if (kernel.program.register_value_kinds.size() != register_total) {
        return std::nullopt;
    }

    auto all_outputs = result.internal_materializations;
    all_outputs.insert(all_outputs.end(), result.boundary_outputs.begin(),
        result.boundary_outputs.end());
    std::ranges::sort(all_outputs, [](const auto& left, const auto& right) {
        if (left.owner != right.owner) {
            return left.owner < right.owner;
        }
        return left.source_instruction < right.source_instruction;
    });
    // Consume candidates in the same owner/instruction order as the kernel.
    std::size_t output_binding_index { };
    for (const auto process : kernel_members) {
        const auto& source = *programs[process];
        const auto settled_span = std::ranges::find(result.member_spans,
            process, &RegionConeMemberSpan::process);
        if (settled_span == result.member_spans.end()
            || settled_span->end - settled_span->begin
                != source.operations.size() - 2U) {
            return std::nullopt;
        }
        if (kernel.program.operations.size()
            >= std::numeric_limits<InstructionIndex>::max()) {
            return std::nullopt;
        }
        const auto branch_instruction = static_cast<InstructionIndex>(
            kernel.program.operations.size());
        if (kernel.program.operations.size() + 1U
            > std::numeric_limits<InstructionIndex>::max()) {
            return std::nullopt;
        }
        const auto body_begin = static_cast<InstructionIndex>(
            kernel.program.operations.size() + 1U);
        kernel.program.operations.push_back(Branch {
            readiness_registers.at(process), body_begin, 0U,
            UnknownBranchPolicy::error });
        for (std::size_t index = 0U;
             index + 2U < source.operations.size(); ++index) {
            const auto source_operation = source.operations.expanded(index);
            auto operation = settled_program.operations.expanded(
                static_cast<std::size_t>(settled_span->begin) + index);
            if (const auto* read
                = operation_get_if<ReadSignal>(&source_operation)) {
                const auto destination = std::uint64_t {
                    register_bases.at(process) } + read->destination;
                if (destination >= std::numeric_limits<RegisterId>::max()) {
                    return std::nullopt;
                }
                operation = CopyRegister {
                    static_cast<RegisterId>(destination),
                    input_registers.at(read->signal) };
            }
            if (kernel.program.operations.size()
                >= std::numeric_limits<InstructionIndex>::max()) {
                return std::nullopt;
            }
            const auto kernel_instruction = static_cast<InstructionIndex>(
                kernel.program.operations.size());
            kernel.program.operations.push_back(std::move(operation));
            if (output_binding_index < all_outputs.size()) {
                const auto& next_binding = all_outputs[output_binding_index];
                if (next_binding.owner < process
                    || (next_binding.owner == process
                        && next_binding.source_instruction < index)) {
                    return std::nullopt;
                }
            }
            while (output_binding_index < all_outputs.size()
                && all_outputs[output_binding_index].owner == process
                && all_outputs[output_binding_index].source_instruction
                    == index) {
                auto binding = all_outputs[output_binding_index++];
                binding.kernel_instruction = kernel_instruction;
                kernel.outputs.push_back(std::move(binding));
            }
        }
        if (kernel.program.operations.size()
            > std::numeric_limits<InstructionIndex>::max()) {
            return std::nullopt;
        }
        const auto body_end = static_cast<InstructionIndex>(
            kernel.program.operations.size());
        kernel.program.operations[branch_instruction] = Branch {
            readiness_registers.at(process), body_begin, body_end,
            UnknownBranchPolicy::error };
        RegionConeKernelMember member;
        member.process = process;
        member.readiness_register = readiness_registers.at(process);
        member.branch_instruction = branch_instruction;
        member.begin = body_begin;
        member.end = body_end;
        member.initialize = source.initialize;
        member.sensitivities = processes_[process].sensitivities;
        // The forwarding kernel is built below from the same per-process
        // debug state, so keep the source metadata available for both forms.
        member.final_debug_state = final_debug_states.at(process);
        const auto& defined_registers
            = defined_member_registers.at(process);
        const auto& register_widths
            = member_register_widths.at(process);
        const auto register_value_kinds
            = process_layout_detail::ProcessLayoutAccess::view(
                source.register_value_kinds);
        const auto base = register_bases.at(process);
        member.register_bindings.reserve(defined_registers.size());
        for (std::size_t source_register = 0U;
             source_register < defined_registers.size(); ++source_register) {
            const auto activation_register
                = std::uint64_t { base } + source_register;
            if (activation_register
                >= std::numeric_limits<RegisterId>::max()) {
                return std::nullopt;
            }
            member.register_bindings.push_back({
                static_cast<RegisterId>(source_register),
                static_cast<RegisterId>(activation_register),
                defined_registers[source_register] != 0U,
                register_widths[source_register],
                register_value_kinds.empty()
                    ? ValueKind::logic4
                    : register_value_kinds[source_register] });
        }
        member.all_registers_definitely_defined
            = std::ranges::all_of(member.register_bindings,
                [](const RegionConeKernelRegisterBinding& binding) {
                    return binding.defined;
                });
        kernel.members.push_back(std::move(member));
    }
    if (kernel.program.operations.size()
        >= std::numeric_limits<InstructionIndex>::max()) {
        return std::nullopt;
    }
    kernel.program.operations.push_back(Halt { });
    if (kernel.outputs.size() != all_outputs.size()) {
        return std::nullopt;
    }

    const auto build_forwarding_kernel = [&]()
        -> std::optional<RegionConeForwardingKernel> {
        if (!systemverilog_active || internal_signals.empty()
            || std::ranges::any_of(settled_program.register_value_kinds,
                [](const ValueKind kind) {
                    return kind != ValueKind::logic4;
                })) {
            return std::nullopt;
        }
        for (const auto signal : internal_signals) {
            if (signal >= signals_.size()
                || signals_[signal].descriptor.width == 0U
                || signals_[signal].descriptor.value_kind
                    != ValueKind::logic4) {
                return std::nullopt;
            }
        }

        auto process_id_order = ordered_members;
        std::ranges::sort(process_id_order);
        std::map<ProcessId, std::size_t> member_index_by_process;
        std::map<ProcessId, std::size_t> topological_position;
        for (std::size_t index = 0U; index < process_id_order.size(); ++index) {
            if (!member_index_by_process.emplace(
                    process_id_order[index], index).second) {
                return std::nullopt;
            }
        }
        for (std::size_t index = 0U; index < ordered_members.size(); ++index) {
            topological_position.emplace(ordered_members[index], index);
        }

        std::vector<std::uint32_t> depth_by_member(
            process_id_order.size(), 0U);
        std::vector<std::vector<RegionConeForwardingDependency>>
            dependencies_by_member(process_id_order.size());
        std::vector<std::vector<RegionConeForwardingRead>>
            reads_by_member(process_id_order.size());
        std::size_t root_count { };
        for (const auto process : ordered_members) {
            const auto member_index = member_index_by_process.at(process);
            const auto process_position = topological_position.find(process);
            if (process_position == topological_position.end()) {
                return std::nullopt;
            }
            const auto& node = processes_[process];
            bool has_internal_sensitivity { };
            for (const auto& sensitivity : node.sensitivities) {
                if (sensitivity.edge != EdgeKind::any
                    || sensitivity.signal >= signals_.size()) {
                    return std::nullopt;
                }
                if (!internal_signals.contains(sensitivity.signal)) {
                    continue;
                }
                has_internal_sensitivity = true;
                const auto writer = internal_writers.find(sensitivity.signal);
                if (writer == internal_writers.end()
                    || writer->second.first == process) {
                    return std::nullopt;
                }
                const auto writer_member
                    = member_index_by_process.find(writer->second.first);
                const auto writer_position
                    = topological_position.find(writer->second.first);
                if (writer_member == member_index_by_process.end()
                    || writer_position == topological_position.end()
                    || writer_position->second >= process_position->second
                    || depth_by_member[writer_member->second]
                        == std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                depth_by_member[member_index] = std::max(
                    depth_by_member[member_index],
                    depth_by_member[writer_member->second] + 1U);
                dependencies_by_member[member_index].push_back({
                    sensitivity.signal, writer_member->second,
                    sensitivity.edge,
                    sensitivity.offset, sensitivity.width });
            }
            if (!has_internal_sensitivity) {
                ++root_count;
            }

            const auto& source = *programs[process];
            for (std::size_t index = 0U;
                 index + 2U < source.operations.size(); ++index) {
                if (operation_get_if<DebugPoint>(
                        &source.operations[index]) != nullptr) {
                    continue;
                }
                const auto operation = source.operations.expanded(index);
                const auto* const read
                    = operation_get_if<ReadSignal>(&operation);
                if (read == nullptr) {
                    continue;
                }
                if (read->signal >= signals_.size()
                    || signals_[read->signal].descriptor.width == 0U
                    || signals_[read->signal].descriptor.value_kind
                        != ValueKind::logic4) {
                    return std::nullopt;
                }
                if (internal_signals.contains(read->signal)) {
                    const auto writer = internal_writers.find(read->signal);
                    if (writer == internal_writers.end()) {
                        return std::nullopt;
                    }
                    const auto writer_member
                        = member_index_by_process.find(writer->second.first);
                    const auto writer_position
                        = topological_position.find(writer->second.first);
                    if (writer_member == member_index_by_process.end()
                        || writer_position == topological_position.end()
                        || writer_position->second >= process_position->second
                        || std::ranges::none_of(
                            dependencies_by_member[member_index],
                            [&](const RegionConeForwardingDependency& dependency) {
                                return dependency.signal == read->signal
                                    && dependency.writer_member_index
                                        == writer_member->second;
                            })) {
                        return std::nullopt;
                    }
                    auto& reads = reads_by_member[member_index];
                    if (std::ranges::none_of(reads,
                            [&](const RegionConeForwardingRead& existing) {
                                return existing.signal == read->signal;
                            })) {
                        reads.push_back({ read->signal,
                            writer_member->second });
                    }
                }
            }
        }
        if (root_count == 0U
            || std::ranges::any_of(all_outputs,
                [](const RegionConeOutputBinding& binding) {
                    return binding.width == 0U
                        || binding.value_kind != ValueKind::logic4;
                })) {
            return std::nullopt;
        }

        RegionConeForwardingKernel forwarding;
        forwarding.members.resize(process_id_order.size());
        forwarding.internal_signals.assign(internal_signals.begin(),
            internal_signals.end());
        forwarding.topological_member_indices.reserve(ordered_members.size());
        for (const auto process : ordered_members) {
            const auto member_index = member_index_by_process.at(process);
            forwarding.topological_member_indices.push_back(member_index);
            auto& member = forwarding.members[member_index];
            member.process = process;
            member.depth = depth_by_member[member_index];
            member.dependency_begin = forwarding.dependencies.size();
            const auto& dependencies
                = dependencies_by_member[member_index];
            member.dependency_count = dependencies.size();
            forwarding.dependencies.insert(forwarding.dependencies.end(),
                dependencies.begin(), dependencies.end());
            member.read_begin = forwarding.internal_reads.size();
            const auto& reads = reads_by_member[member_index];
            member.read_count = reads.size();
            forwarding.internal_reads.insert(
                forwarding.internal_reads.end(), reads.begin(), reads.end());
        }

        std::set<SignalId> boundary_inputs;
        for (const auto process : ordered_members) {
            for (const auto& sensitivity : processes_[process].sensitivities) {
                if (!internal_signals.contains(sensitivity.signal)) {
                    boundary_inputs.insert(sensitivity.signal);
                }
            }
            const auto& source = *programs[process];
            for (std::size_t index = 0U;
                 index + 2U < source.operations.size(); ++index) {
                if (operation_get_if<DebugPoint>(
                        &source.operations[index]) != nullptr) {
                    continue;
                }
                const auto operation = source.operations.expanded(index);
                if (const auto* const read
                    = operation_get_if<ReadSignal>(&operation);
                    read != nullptr
                    && !internal_signals.contains(read->signal)) {
                    boundary_inputs.insert(read->signal);
                }
            }
        }

        RegionConeActivationKernel execution;
        execution.program = settled_program;
        execution.program.name = "simir_region_forwarding_"
            + std::to_string(component_index);
        execution.program.operations = OperationList { };
        execution.program.static_sensitivity.clear();
        execution.program.static_trigger_regions.clear();
        execution.program.driver_regions.clear();
        execution.program.initialize = true;
        execution.program.observed = false;
        execution.program.reactive = false;
        execution.program.postponed = false;
        execution.program.final = false;
        execution.program.debug_locals.clear();
        execution.program.debug_string_locals.clear();
        execution.program.debug_container_locals.clear();
        execution.program.container_register_types.clear();
        execution.program.switch_source.reset();
        execution.program.switch_target.reset();
        execution.program.switch_control.reset();
        execution.program.switch_bidirectional = false;
        execution.program.switch_resistive = false;
        execution.internal_signals.clear();

        std::uint64_t next_register = execution.program.register_count;
        const auto allocate_private_register = [&]()
            -> std::optional<RegisterId> {
            if (next_register >= std::numeric_limits<RegisterId>::max()) {
                return std::nullopt;
            }
            return static_cast<RegisterId>(next_register++);
        };
        std::map<SignalId, RegisterId> boundary_input_registers;
        execution.inputs.reserve(boundary_inputs.size());
        for (const auto signal : boundary_inputs) {
            const auto& descriptor = signals_[signal].descriptor;
            if (descriptor.width == 0U
                || descriptor.value_kind != ValueKind::logic4) {
                return std::nullopt;
            }
            const auto input_register = allocate_private_register();
            if (!input_register
                || !boundary_input_registers.emplace(
                    signal, *input_register).second) {
                return std::nullopt;
            }
            execution.inputs.push_back({
                signal, *input_register, descriptor.width,
                ValueKind::logic4, false });
            execution.program.register_value_kinds.push_back(
                ValueKind::logic4);
        }

        std::map<ProcessId, RegisterId> readiness_registers;
        for (const auto process : process_id_order) {
            const auto readiness_register = allocate_private_register();
            if (!readiness_register
                || !readiness_registers.emplace(
                    process, *readiness_register).second) {
                return std::nullopt;
            }
            execution.program.register_value_kinds.push_back(
                ValueKind::logic4);
        }
        if (next_register > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        execution.program.register_count
            = static_cast<std::uint32_t>(next_register);
        execution.members.resize(process_id_order.size());
        execution.member_execution_order
            = forwarding.topological_member_indices;

        std::map<OutputKey, RegionConeOutputBinding> output_by_source;
        for (const auto& binding : all_outputs) {
            if (!output_by_source.emplace(
                    OutputKey { binding.owner,
                        binding.source_instruction },
                    binding).second) {
                return std::nullopt;
            }
        }
        std::set<OutputKey> emitted_outputs;
        for (const auto process : ordered_members) {
            const auto member_index = member_index_by_process.at(process);
            const auto& source = *programs[process];
            const auto settled_span = std::ranges::find(result.member_spans,
                process, &RegionConeMemberSpan::process);
            if (settled_span == result.member_spans.end()
                || settled_span->end - settled_span->begin
                    != source.operations.size() - 2U) {
                return std::nullopt;
            }
            if (execution.program.operations.size()
                >= std::numeric_limits<InstructionIndex>::max()) {
                return std::nullopt;
            }
            const auto branch_instruction = static_cast<InstructionIndex>(
                execution.program.operations.size());
            if (execution.program.operations.size() + 1U
                > std::numeric_limits<InstructionIndex>::max()) {
                return std::nullopt;
            }
            const auto body_begin = static_cast<InstructionIndex>(
                execution.program.operations.size() + 1U);
            execution.program.operations.push_back(Branch {
                readiness_registers.at(process), body_begin, 0U,
                UnknownBranchPolicy::error });

            auto& member = execution.members[member_index];
            member.process = process;
            member.readiness_register = readiness_registers.at(process);
            member.branch_instruction = branch_instruction;
            member.begin = body_begin;
            member.initialize = source.initialize;
            member.sensitivities = processes_[process].sensitivities;
            member.register_bindings.reserve(source.register_count);
            const auto& widths = member_register_widths.at(process);
            const auto& defined = defined_member_registers.at(process);
            const auto source_kinds
                = process_layout_detail::ProcessLayoutAccess::view(
                    source.register_value_kinds);
            if (widths.size() != source.register_count
                || defined.size() != source.register_count
                || (!source_kinds.empty()
                    && source_kinds.size() != source.register_count)) {
                return std::nullopt;
            }
            for (std::size_t source_register = 0U;
                 source_register < source.register_count;
                 ++source_register) {
                const auto activation_register
                    = std::uint64_t { register_bases.at(process) }
                    + source_register;
                if (activation_register
                    >= std::numeric_limits<RegisterId>::max()) {
                    return std::nullopt;
                }
                member.register_bindings.push_back({
                    static_cast<RegisterId>(source_register),
                    static_cast<RegisterId>(activation_register),
                    defined[source_register] != 0U,
                    widths[source_register],
                    source_kinds.empty()
                        ? ValueKind::logic4
                        : source_kinds[source_register] });
            }
            member.all_registers_definitely_defined
                = std::ranges::all_of(member.register_bindings,
                    [](const RegionConeKernelRegisterBinding& binding) {
                        return binding.defined;
                    });

            auto& forwarding_member = forwarding.members[member_index];
            forwarding_member.output_begin = execution.outputs.size();
            for (std::size_t source_index = 0U;
                 source_index + 2U < source.operations.size();
                 ++source_index) {
                const auto source_operation
                    = source.operations.expanded(source_index);
                auto operation = settled_program.operations.expanded(
                    static_cast<std::size_t>(settled_span->begin)
                    + source_index);
                if (const auto* const read
                    = operation_get_if<ReadSignal>(&source_operation);
                    read != nullptr
                    && !internal_signals.contains(read->signal)) {
                    const auto destination = std::uint64_t {
                        register_bases.at(process) } + read->destination;
                    const auto input = boundary_input_registers.find(
                        read->signal);
                    if (destination >= std::numeric_limits<RegisterId>::max()
                        || input == boundary_input_registers.end()) {
                        return std::nullopt;
                    }
                    operation = CopyRegister {
                        static_cast<RegisterId>(destination), input->second };
                }
                if (execution.program.operations.size()
                    >= std::numeric_limits<InstructionIndex>::max()) {
                    return std::nullopt;
                }
                const auto kernel_instruction
                    = static_cast<InstructionIndex>(
                        execution.program.operations.size());
                execution.program.operations.push_back(std::move(operation));

                const auto key = OutputKey { process,
                    static_cast<InstructionIndex>(source_index) };
                const auto output = output_by_source.find(key);
                if (output != output_by_source.end()) {
                    auto binding = output->second;
                    binding.kernel_instruction = kernel_instruction;
                    execution.outputs.push_back(std::move(binding));
                    emitted_outputs.insert(key);
                    ++forwarding_member.output_count;
                }
            }
            if (execution.program.operations.size()
                > std::numeric_limits<InstructionIndex>::max()) {
                return std::nullopt;
            }
            member.end = static_cast<InstructionIndex>(
                execution.program.operations.size());
            if (member.begin >= member.end) {
                return std::nullopt;
            }
            member.final_debug_state = final_debug_states.at(process);
            execution.program.operations[branch_instruction] = Branch {
                member.readiness_register, member.begin, member.end,
                UnknownBranchPolicy::error };
        }
        if (emitted_outputs.size() != output_by_source.size()
            || execution.outputs.size() != all_outputs.size()
            || execution.program.operations.size()
                >= std::numeric_limits<InstructionIndex>::max()) {
            return std::nullopt;
        }
        execution.program.operations.push_back(Halt { });
        forwarding.execution_kernel = std::move(execution);
        return forwarding;
    };
    result.forwarding_kernel = build_forwarding_kernel();

    result.activation_kernel = std::move(kernel);
    if (rejection != nullptr) {
        rejection->reason = nullptr;
    }
    return result;
}

} // namespace fsim::runtime::simir
