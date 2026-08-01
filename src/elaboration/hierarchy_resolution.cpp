// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void HierarchyBuilder::finish() {
    validate_process_drivers();
    for (const auto& [path, binding] : bindings_) {
        (void)binding;
        if (!used_bindings_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-011",
                "binding instance path '" + path
                    + "' was not found in the elaborated hierarchy",
                {});
        }
    }
    for (const auto& [path, instance] : systemc_instances_) {
        (void)instance;
        if (!used_systemc_instances_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-033",
                "constructed SystemC instance path '" + path
                    + "' was not reached from the elaborated hierarchy",
                {});
        }
    }
}

ResolutionKind HierarchyBuilder::native_resolution(
    const SignalInfo& signal) {
    if (signal.type_name == "std_logic"
        || signal.type_name == "std_logic_vector") {
        return ResolutionKind::std_logic;
    }
    if (signal.type_name == "wire"
        || signal.type_name == "tri") {
        return ResolutionKind::sv_wire;
    }
    return ResolutionKind::none;
}

std::optional<ResolutionKind>
HierarchyBuilder::explicit_resolution(
    const SignalId signal) {
    const auto found = resolver_by_signal_.find(signal);
    if (found == resolver_by_signal_.end()) {
        return std::nullopt;
    }
    if (found->second == "std_logic") {
        return ResolutionKind::std_logic;
    }
    if (found->second == "sv_wire") {
        return ResolutionKind::sv_wire;
    }
    report(
        "FSIM-ELAB-BIND-050",
        "unknown resolver '" + found->second
            + "'; expected \"std_logic\" or \"sv_wire\"",
        {});
    return ResolutionKind::none;
}

void HierarchyBuilder::set_resolution(
    const SignalId signal,
    const ResolutionKind resolution) {
    design_.signal_info_.at(signal).resolution = resolution;
    design_.signals_.at(signal).resolution = resolution;
}

void HierarchyBuilder::validate_process_drivers() {
    std::unordered_map<SignalId, std::vector<ProcessId>> drivers;
    for (const auto& process : design_.processes_) {
        std::set<SignalId> process_outputs;
        for (const auto& operation : process.operations) {
            if (const auto* blocking =
                    fsim::runtime::simir::operation_get_if<WriteBlocking>(&operation)) {
                process_outputs.insert(blocking->signal);
            } else if (const auto* update =
                           fsim::runtime::simir::operation_get_if<WriteUpdate>(&operation)) {
                process_outputs.insert(update->signal);
            } else if (const auto* delayed =
                           fsim::runtime::simir::operation_get_if<WriteAfter>(&operation)) {
                process_outputs.insert(delayed->signal);
            } else if (const auto* inertial =
                           fsim::runtime::simir::operation_get_if<WriteInertial>(&operation)) {
                process_outputs.insert(inertial->signal);
            } else if (const auto* projected =
                           fsim::runtime::simir::operation_get_if<WriteProjected>(&operation)) {
                process_outputs.insert(projected->signal);
            } else if (const auto* waveform =
                           fsim::runtime::simir::operation_get_if<WriteProjectedWaveform>(
                               &operation)) {
                process_outputs.insert(waveform->signal);
            } else if (const auto* blocking_slice =
                           fsim::runtime::simir::operation_get_if<WriteBlockingSlice>(
                               &operation)) {
                process_outputs.insert(blocking_slice->signal);
            } else if (const auto* update_slice =
                           fsim::runtime::simir::operation_get_if<WriteUpdateSlice>(
                               &operation)) {
                process_outputs.insert(update_slice->signal);
            } else if (const auto* delayed_slice =
                           fsim::runtime::simir::operation_get_if<WriteAfterSlice>(
                               &operation)) {
                process_outputs.insert(delayed_slice->signal);
            } else if (const auto* inertial_slice =
                           fsim::runtime::simir::operation_get_if<WriteInertialSlice>(
                               &operation)) {
                process_outputs.insert(inertial_slice->signal);
            } else if (const auto* projected_slice =
                           fsim::runtime::simir::operation_get_if<WriteProjectedSlice>(
                               &operation)) {
                process_outputs.insert(projected_slice->signal);
            } else if (const auto* waveform_slice =
                           fsim::runtime::simir::operation_get_if<WriteProjectedWaveformSlice>(
                               &operation)) {
                process_outputs.insert(waveform_slice->signal);
            }
        }
        for (const auto signal : process_outputs) {
            drivers[signal].push_back(process.id);
        }
    }
    for (SignalId signal = 0;
         signal < design_.signal_info_.size();
         ++signal) {
        const auto selected = explicit_resolution(signal);
        set_resolution(
            signal,
            selected.value_or(
                native_resolution(
                    design_.signal_info_.at(signal))));
    }
    for (const auto& [signal, processes] : drivers) {
        if (processes.size() <= 1
            || design_.signal_info_.at(signal).resolution
                != ResolutionKind::none) {
            continue;
        }
        const auto& info = design_.signal_info_.at(signal);
        if (info.type_name == "wand"
            || info.type_name == "triand"
            || info.type_name == "wor"
            || info.type_name == "trior") {
            report(
                "FSIM-ELAB-DRV-002",
                "wired-AND/OR resolution for signal '"
                    + info.name + "' is not implemented",
                {});
            continue;
        }
        report(
            "FSIM-ELAB-DRV-001",
            "unresolved variable '" + info.name
                + "' has multiple process drivers",
            {});
    }
}

} // namespace fsim::elaboration
