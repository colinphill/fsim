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
    return elaborate(
        parsed,
        top,
        bindings,
        systemc_instances,
        systemc_provider,
        std::span<const std::string>{});
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::string_view top,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries) {
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
        systemc_provider,
        search_libraries};
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
        std::optional<std::string> inferred_systemc_target;
        std::string formatted_scope;
        if (requested_target) {
            unit = choose_top_unit(parsed, top);
        } else {
            const auto systemc_candidates = systemc_provider != nullptr
                ? systemc_provider->candidates()
                : std::vector<SystemCFactoryCandidate>{};
            const auto systemc_libraries = systemc_provider != nullptr
                ? systemc_provider->libraries()
                : std::vector<std::string>{};
            const auto scope = effective_search_scope(
                "work", search_libraries);
            std::vector<UnitResolutionCandidate> candidates;
            std::vector<std::string> unavailable_libraries;
            for (std::size_t index = 0; index < scope.size(); ++index) {
                const auto& library = scope[index];
                if (!has_logical_library(
                        parsed, systemc_candidates,
                        systemc_libraries, library)) {
                    if (index != 0) {
                        unavailable_libraries.push_back(library);
                    }
                    continue;
                }
                auto library_candidates = resolve_unit_candidates(
                    parsed, library, requested, true);
                candidates.insert(
                    candidates.end(),
                    std::make_move_iterator(library_candidates.begin()),
                    std::make_move_iterator(library_candidates.end()));
                for (const auto& factory : systemc_candidates) {
                    if (factory.library == library
                        && factory.name == requested) {
                        candidates.push_back({
                            nullptr,
                            factory.target,
                            "systemc:" + factory.library + "."
                                + factory.name});
                    }
                }
            }
            std::stable_sort(
                candidates.begin(), candidates.end(),
                [](const auto& left, const auto& right) {
                    return left.identity < right.identity;
                });
            formatted_scope = [&] {
                std::string formatted;
                for (const auto& library : scope) {
                    if (!formatted.empty()) {
                        formatted += ", ";
                    }
                    formatted += library;
                }
                return formatted;
            }();
            if (!unavailable_libraries.empty()) {
                std::string unavailable;
                for (const auto& library : unavailable_libraries) {
                    if (!unavailable.empty()) {
                        unavailable += ", ";
                    }
                    unavailable += library;
                }
                result.diagnostics.push_back({
                    "FSIM-ELAB-006",
                    "top-level lookup queried unavailable logical "
                    "library/libraries [" + unavailable
                        + "] while resolving '" + requested
                        + "' in search scope [" + formatted_scope + "]",
                    {}});
                return result;
            }
            if (candidates.size() > 1) {
                result.diagnostics.push_back({
                    "FSIM-ELAB-005",
                    "top-level design unit '" + requested
                        + "' is ambiguous in search scope ["
                        + formatted_scope + "]; candidates: "
                        + format_resolution_candidates(candidates),
                    {}});
                return result;
            }
            if (!candidates.empty()) {
                unit = candidates.front().unit;
                inferred_systemc_target =
                    candidates.front().systemc_target;
            }
        }
        if (inferred_systemc_target.has_value()) {
            std::string error;
            auto root = systemc_provider->instantiate(
                requested,
                *inferred_systemc_target,
                std::span<const std::pair<std::string, std::int64_t>>{},
                error);
            if (!root) {
                result.diagnostics.push_back({
                    "FSIM-ELAB-007",
                    "cannot construct inferred top-level SystemC factory '"
                        + *inferred_systemc_target + "': " + error,
                    {}});
                return result;
            }
            builder.build(*root);
        } else if (unit == nullptr) {
            result.diagnostics.push_back({
                "FSIM-ELAB-001",
                "top-level design unit '" + requested
                    + (requested_target
                           ? "' was not found"
                           : "' was not found in search scope ["
                                 + formatted_scope
                                 + "]; candidates: <none>"),
                {}});
            return result;
        } else if (requested_target
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
        } else {
            builder.build(*unit);
        }
    }

    if (!result.diagnostics.empty()) {
        return result;
    }
    result.design = std::move(design);
    return result;
}


} // namespace fsim::elaboration
