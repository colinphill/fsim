// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace elaboration_detail;

 ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed, const std::string_view top) {
    return elaborate(
        parsed,
        top,
        std::span<const Binding>{},
        std::span<const SystemCInstanceDescription>{});
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings) {
    return elaborate(
        parsed,
        top,
        bindings,
        std::span<const SystemCInstanceDescription>{});
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances) {
    return elaborate(
        parsed,
        top,
        bindings,
        systemc_instances,
        nullptr);
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances,
    SystemCFactoryProvider* systemc_provider) {
    ElaborationResult result;
    const auto requested = simple_top_name(top);
    bool systemc_top = false;
    std::optional<TargetSpec> requested_target;
    if (top.find(':') != std::string_view::npos) {
        requested_target = parse_target(top);
        if (!requested_target) {
            result.diagnostics.push_back({
                "FSIM-ELAB-003",
                "malformed qualified top-level target '"
                    + std::string{top} + "'",
                {}});
            return result;
        }
        systemc_top =
            requested_target->language == "systemc";
    }

    ElaboratedDesign design;
    design.top_ = requested;
    HierarchyBuilder builder{
        parsed,
        design,
        result.diagnostics,
        bindings,
        systemc_instances,
        systemc_provider};
    if (systemc_top) {
        const auto root = std::find_if(
            systemc_instances.begin(),
            systemc_instances.end(),
            [&](const SystemCInstanceDescription& instance) {
                return instance.path == requested
                    && instance.target == top;
            });
        if (root == systemc_instances.end()) {
            result.diagnostics.push_back({
                "FSIM-ELAB-001",
                "top-level SystemC factory '" + std::string{top}
                    + "' was not constructed",
                {}});
            return result;
        }
        builder.build(*root);
    } else {
        const DesignUnit* unit = nullptr;
        if (requested_target) {
            unit = choose_top_unit(parsed, top);
        } else {
            const auto candidates = resolve_unit_candidates(
                parsed, "work", requested, true);
            if (candidates.size() > 1) {
                result.diagnostics.push_back({
                    "FSIM-ELAB-005",
                    "top-level design unit '" + requested
                        + "' is ambiguous in logical library 'work'; "
                          "candidates: "
                        + format_resolution_candidates(candidates),
                    {}});
                return result;
            }
            if (!candidates.empty()) {
                unit = candidates.front().unit;
            }
        }
        if (unit == nullptr) {
            result.diagnostics.push_back({
                "FSIM-ELAB-001",
                "top-level design unit '" + requested
                    + "' was not found",
                {}});
            return result;
        }
        if (requested_target
            && requested_target->language == "vhdl"
            && !requested_target->architecture
            && unit->kind
                != frontend::UnitKind::VhdlConfiguration) {
            result.diagnostics.push_back({
                "FSIM-ELAB-004",
                "a qualified VHDL entity top must name an architecture, "
                "or name a configuration declaration",
                {}});
            return result;
        }
        builder.build(*unit);
    }

    if (!result.diagnostics.empty()) {
        return result;
    }
    result.design = std::move(design);
    return result;
}


} // namespace fsim::elaboration
