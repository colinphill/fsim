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
    const Root root{std::string{top}, simple_top_name(top)};
    return elaborate(
        parsed,
        std::span<const Root>{&root, 1},
        bindings,
        systemc_instances,
        systemc_provider,
        search_libraries);
}

ElaborationResult elaborate(
    const frontend::ParsedDesign& parsed,
    const std::span<const Root> roots,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries) {
    ElaborationResult result;
    if (roots.empty()) {
        result.diagnostics.push_back({
            "FSIM-ELAB-ROOT-002",
            "elaboration requires at least one top-level root",
            {}});
        return result;
    }

    std::vector<Root> normalized_roots{roots.begin(), roots.end()};
    std::unordered_set<std::string> aliases;
    for (auto& root : normalized_roots) {
        if (root.target.empty()) {
            result.diagnostics.push_back({
                "FSIM-ELAB-ROOT-002",
                "top-level root target must not be empty",
                {}});
            return result;
        }
        if (root.alias.empty()) {
            if (normalized_roots.size() == 1) {
                root.alias = simple_top_name(root.target);
            } else {
                result.diagnostics.push_back({
                    "FSIM-ELAB-ROOT-002",
                    "every root in a multiple-top elaboration requires an alias",
                    {}});
                return result;
            }
        }
        if (root.alias.empty()
            || (std::isalpha(
                    static_cast<unsigned char>(root.alias.front())) == 0
                && root.alias.front() != '_')
            || !std::ranges::all_of(
                root.alias,
                [](const unsigned char character) {
                    return std::isalnum(character) != 0
                        || character == '_';
                })) {
            result.diagnostics.push_back({
                "FSIM-ELAB-ROOT-002",
                "top-level root alias '" + root.alias
                    + "' is not a portable hierarchy identifier",
                {}});
            return result;
        }
        if (!aliases.insert(root.alias).second) {
            result.diagnostics.push_back({
                "FSIM-ELAB-ROOT-003",
                "duplicate top-level root alias '" + root.alias + "'",
                {}});
            return result;
        }
    }

    ElaboratedDesign design;
    design.roots_.reserve(normalized_roots.size());
    for (const auto& root : normalized_roots) {
        design.roots_.push_back(root.alias);
    }
    design.top_ = design.roots_.front();

    struct ResolvedRoot {
        std::string alias;
        const DesignUnit* unit = nullptr;
        const SystemCInstanceDescription* constructed_systemc{};
        std::optional<SystemCInstanceDescription> inferred_systemc;
    };
    std::vector<ResolvedRoot> resolved_roots;
    resolved_roots.reserve(normalized_roots.size());
    const auto systemc_candidates = systemc_provider != nullptr
        ? systemc_provider->candidates()
        : std::vector<SystemCFactoryCandidate>{};
    const auto systemc_libraries = systemc_provider != nullptr
        ? systemc_provider->libraries()
        : std::vector<std::string>{};

    for (const auto& root_request : normalized_roots) {
        const auto& top = root_request.target;
        const auto requested = simple_top_name(top);
        bool systemc_top = false;
        std::optional<TargetSpec> requested_target;
        if (top.find(':') != std::string::npos) {
            requested_target = parse_target(top);
            if (!requested_target) {
                result.diagnostics.push_back({
                    "FSIM-ELAB-003",
                    "malformed qualified top-level target '" + top + "'",
                    {}});
                return result;
            }
            systemc_top = requested_target->language == "systemc";
        }

        ResolvedRoot resolved{
            root_request.alias, nullptr, nullptr, std::nullopt};
        if (systemc_top) {
            const auto constructed = std::find_if(
                systemc_instances.begin(),
                systemc_instances.end(),
                [&](const SystemCInstanceDescription& instance) {
                    return instance.path == root_request.alias
                        && instance.target == top;
                });
            if (constructed == systemc_instances.end()) {
                result.diagnostics.push_back({
                    "FSIM-ELAB-001",
                    "top-level SystemC factory '" + top
                        + "' for root alias '" + root_request.alias
                        + "' was not constructed",
                    {}});
                return result;
            }
            resolved.constructed_systemc = &*constructed;
            resolved_roots.push_back(std::move(resolved));
            continue;
        }

        std::optional<std::string> inferred_systemc_target;
        std::string formatted_scope;
        if (requested_target) {
            resolved.unit = choose_top_unit(parsed, top);
        } else {
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
                    parsed, library, requested, true, false);
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
                resolved.unit = candidates.front().unit;
                inferred_systemc_target =
                    candidates.front().systemc_target;
            }
        }
        if (inferred_systemc_target.has_value()) {
            std::string error;
            resolved.inferred_systemc = systemc_provider->instantiate(
                root_request.alias,
                *inferred_systemc_target,
                std::span<const std::pair<std::string, std::int64_t>>{},
                error);
            if (!resolved.inferred_systemc) {
                result.diagnostics.push_back({
                    "FSIM-ELAB-007",
                    "cannot construct inferred top-level SystemC factory '"
                        + *inferred_systemc_target + "': " + error,
                {}});
                return result;
            }
        } else if (resolved.unit == nullptr) {
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
            && resolved.unit->kind
                != frontend::UnitKind::VhdlConfiguration) {
            result.diagnostics.push_back({
                "FSIM-ELAB-004",
                "a qualified VHDL entity top must name an architecture, "
                "or name a configuration declaration",
                {}});
            return result;
        }
        resolved_roots.push_back(std::move(resolved));
    }

    HierarchyBuilder builder{
        parsed,
        design,
        result.diagnostics,
        bindings,
        systemc_instances,
        systemc_provider,
        search_libraries};
    for (const auto& root : resolved_roots) {
        if (root.unit != nullptr) {
            builder.predeclare_root_globals(*root.unit, root.alias);
        }
    }
    for (const auto& root : resolved_roots) {
        if (root.constructed_systemc != nullptr) {
            builder.add_root(*root.constructed_systemc, root.alias);
        } else if (root.inferred_systemc) {
            builder.add_root(*root.inferred_systemc, root.alias);
        } else {
            builder.add_root(*root.unit, root.alias);
        }
    }
    builder.finalize();

    if (!result.diagnostics.empty()) {
        return result;
    }
    result.design = std::move(design);
    return result;
}


} // namespace fsim::elaboration
