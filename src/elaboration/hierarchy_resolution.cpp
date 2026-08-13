// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void HierarchyBuilder::finish()
{
    validate_process_drivers();
    std::stable_sort(
        design_.systemc_objects_.begin(),
        design_.systemc_objects_.end(),
        [](const SystemCNamedObjectInfo& left,
            const SystemCNamedObjectInfo& right) {
            return left.native_handle < right.native_handle;
        });
    for (const auto& [path, binding] : bindings_) {
        (void)binding;
        if (!used_bindings_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-011",
                "binding instance path '" + path
                    + "' was not found in the elaborated hierarchy",
                { });
        }
    }
    for (const auto& [path, instance] : systemc_instances_) {
        (void)instance;
        if (!used_systemc_instances_.contains(path)) {
            report(
                "FSIM-ELAB-BIND-033",
                "constructed SystemC instance path '" + path
                    + "' was not reached from the elaborated hierarchy",
                { });
        }
    }
    for (const auto* directive : compilation_unit_systemverilog_binds_) {
        if (directive != nullptr
            && !used_compilation_unit_systemverilog_binds_.contains(
                directive)) {
            report(
                "FSIM-ELAB-SVBIND-002",
                "bind target '" + directive->target
                    + "' was not found in the elaborated hierarchy",
                directive->span);
        }
    }
}

ResolutionKind HierarchyBuilder::native_resolution(
    const SignalInfo& signal)
{
    const auto& net_type = signal.systemverilog_net_type.empty()
        ? signal.type_name
        : signal.systemverilog_net_type;
    if (signal.type_name == "std_logic"
        || signal.type_name == "std_logic_vector") {
        return ResolutionKind::std_logic;
    }
    if (net_type == "wire"
        || net_type == "tri"
        || net_type == "tri0"
        || net_type == "tri1"
        || net_type == "trireg"
        || net_type == "supply0"
        || net_type == "supply1") {
        return ResolutionKind::sv_wire;
    }
    if (net_type == "wand"
        || net_type == "triand") {
        return ResolutionKind::sv_wand;
    }
    if (net_type == "wor"
        || net_type == "trior") {
        return ResolutionKind::sv_wor;
    }
    if (!signal.systemverilog_net_type.empty()
        && signal.systemverilog_net_type != "uwire") {
        return ResolutionKind::sv_wire;
    }
    return ResolutionKind::none;
}

std::optional<ResolutionKind>
HierarchyBuilder::explicit_resolution(
    const SignalId signal)
{
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
    if (const auto user = vhdl_resolution_kinds_.find(found->second);
        user != vhdl_resolution_kinds_.end()) {
        return user->second;
    }
    if (const auto user = systemverilog_resolution_kinds_.find(found->second);
        user != systemverilog_resolution_kinds_.end()) {
        return user->second;
    }
    report(
        "FSIM-ELAB-BIND-050",
        "unknown resolver '" + found->second
            + "'; expected \"std_logic\" or \"sv_wire\"",
        { });
    return ResolutionKind::none;
}

void HierarchyBuilder::register_systemverilog_resolution_functions(
    const DesignUnit& unit)
{
    if (unit.language != frontend::Language::SystemVerilog2017) {
        return;
    }
    for (const auto& alias : unit.type_aliases) {
        const auto& resolver = alias.systemverilog_resolution_function;
        if (alias.declaration_kind
                != frontend::TypeDeclarationKind::SystemVerilogNettype
            || resolver.empty()) {
            continue;
        }
        const frontend::DesignUnit* owner = &unit;
        auto designator = resolver;
        if (const auto separator = resolver.rfind("::");
            separator != std::string::npos) {
            const auto package_name = resolver.substr(0, separator);
            owner = find_systemverilog_package(unit, package_name);
            designator = resolver.substr(separator + 2);
        }
        std::vector<const frontend::FunctionDeclaration*> matches;
        if (owner != nullptr) {
            for (const auto& function : owner->functions) {
                if (function.name == designator && function.defined) {
                    matches.push_back(&function);
                }
            }
        }
        if (matches.size() != 1) {
            report(
                matches.empty()
                    ? "FSIM-ELAB-SVNETTYPE-001"
                    : "FSIM-ELAB-SVNETTYPE-002",
                matches.empty()
                    ? "SystemVerilog nettype resolution function '"
                        + resolver
                        + "' is not visible with an executable body"
                    : "SystemVerilog nettype resolution function '"
                        + resolver + "' is ambiguous",
                alias.span);
            continue;
        }
        const auto& function = *matches.front();
        const auto alias_width = alias.type.width();
        const auto return_width = function.return_type.width();
        const bool profile_matches = function.arguments.size() == 1
            && function.arguments.front().type.systemverilog_container
            && function.arguments.front().type.systemverilog_container->kind
                == frontend::SystemVerilogContainerKind::DynamicArray
            && alias_width && return_width
            && *alias_width == *return_width
            && alias.type.domain == function.return_type.domain;
        if (!profile_matches) {
            report(
                "FSIM-ELAB-SVNETTYPE-003",
                "SystemVerilog nettype resolution function '" + resolver
                    + "' must take one dynamic array of the net base type "
                      "and return that base type",
                function.span);
            continue;
        }
        const auto& body = function.statements;
        const bool returns_first = body.size() == 1
            && body.front().kind == frontend::StatementKind::Return
            && body.front().value.kind == frontend::ExpressionKind::Index
            && body.front().value.operands.size() == 2
            && body.front().value.operands.front().kind
                == frontend::ExpressionKind::Identifier
            && body.front().value.operands.front().text
                == function.arguments.front().name
            && body.front().value.operands.back().kind
                == frontend::ExpressionKind::IntegerLiteral
            && body.front().value.operands.back().text == "0";
        if (!returns_first) {
            report(
                "FSIM-ELAB-SVNETTYPE-004",
                "the executable SystemVerilog nettype resolver '" + resolver
                    + "' is outside the retained deterministic resolution "
                      "forms",
                function.span);
            continue;
        }
        const auto [entry, inserted] = systemverilog_resolution_kinds_.emplace(
            resolver, ResolutionKind::sv_user_first);
        if (inserted) {
            systemverilog_resolution_kind_insertions_.push_back(resolver);
        } else if (entry->second != ResolutionKind::sv_user_first) {
            report(
                "FSIM-ELAB-SVNETTYPE-002",
                "SystemVerilog nettype resolution function '" + resolver
                    + "' has conflicting visible bodies",
                alias.span);
        }
    }
}

void HierarchyBuilder::register_vhdl_resolution_functions(
    const DesignUnit& unit)
{
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
            const bool supported_base = alias.type.domain == frontend::ValueDomain::Bit2
                || alias.type.domain == frontend::ValueDomain::Logic9;
            if (function->pure
                && function->arguments.size() == 1
                && function->arguments.front().type.vhdl_array
                && supported_base
                && function->arguments.front().type.vhdl_array->element_domain == alias.type.domain
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
        const auto supported_return = body.size() == 1
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
        const auto [existing, inserted] = vhdl_resolution_kinds_.emplace(resolver, kind);
        if (inserted) {
            vhdl_resolution_kind_insertions_.push_back(resolver);
        }
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
    const ResolutionKind resolution)
{
    design_.signal_info_.at(signal).resolution = resolution;
    design_.signals_.at(signal).resolution = resolution;
}

void HierarchyBuilder::validate_process_drivers()
{
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
        const auto left_end = static_cast<std::uint64_t>(left.offset) + left.width;
        const auto right_end = static_cast<std::uint64_t>(right.offset) + right.width;
        return left.offset < right_end && right.offset < left_end;
    };
    for (const auto& [signal, process_drivers] : drivers) {
        if (process_drivers.size() <= 1
            || vhdl_1993_shared_signals_.contains(signal)
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
        report(
            "FSIM-ELAB-DRV-001",
            "unresolved variable '" + info.name
                + "' has multiple process drivers",
            { });
    }
}

} // namespace fsim::elaboration
