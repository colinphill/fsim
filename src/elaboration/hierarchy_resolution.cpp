// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <map>

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
    if (const auto user =
            vhdl_resolution_kinds_.find(found->second);
        user != vhdl_resolution_kinds_.end()) {
        return user->second;
    }
    report(
        "FSIM-ELAB-BIND-050",
        "unknown resolver '" + found->second
            + "'; expected \"std_logic\" or \"sv_wire\"",
        {});
    return ResolutionKind::none;
}

void HierarchyBuilder::register_vhdl_resolution_functions(
    const DesignUnit& unit) {
    if (unit.language != frontend::Language::Vhdl2008) {
        return;
    }
    for (const auto& alias : unit.type_aliases) {
        const auto& resolver = alias.type.vhdl_resolution_function;
        if (resolver.empty()) {
            continue;
        }
        std::vector<const frontend::FunctionDeclaration*> matches;
        for (const auto& function : unit.functions) {
            if (function.name == resolver && function.defined) {
                matches.push_back(&function);
            }
        }
        if (matches.empty()) {
            report(
                "FSIM-ELAB-VHRESOLVE-001",
                "VHDL resolution function '" + resolver
                    + "' is not visible with an executable body",
                alias.span);
            continue;
        }
        std::vector<const frontend::FunctionDeclaration*> profiles;
        for (const auto* function : matches) {
            const bool supported_base =
                alias.type.domain == frontend::ValueDomain::Bit2
                || alias.type.domain == frontend::ValueDomain::Logic9;
            if (function->pure
                && function->arguments.size() == 1
                && function->arguments.front().type.vhdl_array
                && supported_base
                && function->arguments.front().type.vhdl_array
                       ->element_domain == alias.type.domain
                && function->return_type.domain == alias.type.domain) {
                profiles.push_back(function);
            }
        }
        if (profiles.size() != 1) {
            report(
                profiles.empty()
                    ? "FSIM-ELAB-VHRESOLVE-002"
                    : "FSIM-ELAB-VHRESOLVE-003",
                profiles.empty()
                    ? "VHDL resolution function '" + resolver
                        + "' must be pure with one array-of-base-type "
                          "input and a base-type result"
                    : "VHDL resolution function '" + resolver
                        + "' is ambiguous for subtype '" + alias.name
                        + "'",
                alias.span);
            continue;
        }
        const auto& body = profiles.front()->statements;
        const auto supported_return =
            body.size() == 1
            && body.front().kind == StatementKind::Return
            && body.front().value.kind == ExpressionKind::Binary
            && (body.front().value.text == "or"
                || body.front().value.text == "and");
        if (!supported_return) {
            report(
                "FSIM-ELAB-VHRESOLVE-004",
                "bounded VHDL resolution function '" + resolver
                    + "' must return one scalar OR or AND expression",
                profiles.front()->span);
            continue;
        }
        const auto kind = body.front().value.text == "or"
            ? ResolutionKind::vhdl_user_or
            : ResolutionKind::vhdl_user_and;
        const auto [existing, inserted] =
            vhdl_resolution_kinds_.emplace(resolver, kind);
        if (!inserted && existing->second != kind) {
            report(
                "FSIM-ELAB-VHRESOLVE-003",
                "VHDL resolution function designator '" + resolver
                    + "' denotes conflicting visible bodies",
                alias.span);
        }
    }
}

void HierarchyBuilder::set_resolution(
    const SignalId signal,
    const ResolutionKind resolution) {
    design_.signal_info_.at(signal).resolution = resolution;
    design_.signals_.at(signal).resolution = resolution;
}

void HierarchyBuilder::validate_process_drivers() {
    using DriverRegion = Process::DriverRegion;
    using ProcessDriver = std::vector<DriverRegion>;
    std::unordered_map<SignalId, std::vector<ProcessDriver>> drivers;
    for (const auto& process : design_.processes_) {
        std::map<SignalId, std::vector<DriverRegion>> process_outputs;
        for (const auto& region : process.driver_regions) {
            process_outputs[region.signal].push_back(region);
        }
        for (auto& [signal, regions] : process_outputs) {
            drivers[signal].push_back(std::move(regions));
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
    const auto regions_overlap = [](
                                     const DriverRegion& left,
                                     const DriverRegion& right) {
        if (left.whole || right.whole) {
            return true;
        }
        const auto left_end =
            static_cast<std::uint64_t>(left.offset) + left.width;
        const auto right_end =
            static_cast<std::uint64_t>(right.offset) + right.width;
        return left.offset < right_end && right.offset < left_end;
    };
    for (const auto& [signal, process_drivers] : drivers) {
        if (process_drivers.size() <= 1
            || design_.signal_info_.at(signal).resolution
                != ResolutionKind::none) {
            continue;
        }
        bool overlap = false;
        for (std::size_t left = 0;
             left < process_drivers.size() && !overlap; ++left) {
            for (std::size_t right = left + 1;
                 right < process_drivers.size() && !overlap; ++right) {
                overlap = std::ranges::any_of(
                    process_drivers[left],
                    [&](const auto& left_region) {
                        return std::ranges::any_of(
                            process_drivers[right],
                            [&](const auto& right_region) {
                                return regions_overlap(
                                    left_region, right_region);
                            });
                    });
            }
        }
        const auto& info = design_.signal_info_.at(signal);
        if (!overlap && info.vhdl_array) {
            continue;
        }
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
