// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

frontend::SourceSpan compiled_source_span(
    const semantic::CompiledDesign& compiled,
    const semantic::SourceSpanId source)
{
    frontend::SourceSpan result;
    const auto& spans = compiled.semantics.source_spans();
    if (!source.valid() || source.value() >= spans.size()) {
        return result;
    }
    const auto& span = spans[source.value()];
    result.source_name = span.logical_name;
    result.begin = { static_cast<std::size_t>(span.begin.offset),
        span.begin.line, span.begin.column };
    result.end = { static_cast<std::size_t>(span.end.offset),
        span.end.line, span.end.column };
    const auto& files = compiled.semantics.source_files();
    if (span.file.valid() && span.file.value() < files.size()) {
        result.physical_source_name
            = files[span.file.value()].physical_name;
    }
    const auto& expansions = compiled.semantics.expansions();
    auto expansion = span.expansion;
    while (expansion && expansion->valid()
        && expansion->value() < expansions.size()) {
        const auto& record = expansions[expansion->value()];
        result.expansion_stack.push_back(record.description);
        expansion = record.parent;
    }
    std::ranges::reverse(result.expansion_stack);
    return result;
}

void append_compiled_constant_effects(
    const semantic::ValidatedCompiledDesign validated,
    const semantic::CompiledUnitView& root,
    ElaborationResult& result)
{
    const auto& compiled = validated.design();
    if (root.systemverilog == nullptr || root.identity == nullptr) {
        return;
    }
    const auto specialized = semantic::make_specialized_hir_unit(
        validated, root.identity->id,
        std::span<const semantic::SpecializedHirActualIdentity> { });
    if (!specialized) {
        return;
    }
    const auto append_effects = [&](auto effects) {
        for (auto& effect : effects) {
            const auto span = compiled_source_span(
                compiled, effect.source);
            if (effect.severity
                    == semantic::SpecializedHirConstantEffectSeverity::note
                || effect.severity
                    == semantic::SpecializedHirConstantEffectSeverity::warning) {
                result.messages.push_back(frontend::Diagnostic {
                    effect.severity
                            == semantic::SpecializedHirConstantEffectSeverity::note
                        ? frontend::DiagnosticSeverity::Note
                        : frontend::DiagnosticSeverity::Warning,
                    std::move(effect.code),
                    std::move(effect.message),
                    span,
                    { },
                });
            } else {
                result.diagnostics.push_back({
                    std::move(effect.code),
                    std::move(effect.message),
                    span,
                });
            }
        }
    };
    for (const auto declaration_id : root.systemverilog->declarations) {
        const auto declaration
            = specialized->find_declaration(declaration_id);
        if (!declaration || declaration->systemverilog == nullptr
            || !declaration->systemverilog->initializer) {
            continue;
        }
        const auto form = declaration->systemverilog->form;
        if (form != semantic::sv::DeclarationForm::parameter
            && form != semantic::sv::DeclarationForm::local_parameter) {
            continue;
        }
        auto evaluation
            = specialized->evaluate_integral_expression_with_effects(
                *declaration->systemverilog->initializer);
        append_effects(std::move(evaluation.effects));
    }
    if (root.systemverilog->kind == semantic::sv::UnitKind::program) {
        std::vector<semantic::StatementId> program_statements {
            root.systemverilog->concurrent_statements.begin(),
            root.systemverilog->concurrent_statements.end()
        };
        const auto selected_generates = specialized->selected_generates();
        const auto append_selected_generate_statements
            = [&](const auto& self,
                  const semantic::sv::GenerateRegion& generate) -> void {
            if (std::ranges::find(
                    selected_generates, generate.declaration)
                != selected_generates.end()) {
                program_statements.insert(program_statements.end(),
                    generate.concurrent_statements.begin(),
                    generate.concurrent_statements.end());
            }
            for (const auto& nested : generate.nested) {
                self(self, nested);
            }
        };
        for (const auto& generate : root.systemverilog->generates) {
            append_selected_generate_statements(
                append_selected_generate_statements, generate);
        }
        if (program_statements.empty()) {
            return;
        }
        auto effects
            = specialized->evaluate_systemverilog_program_statements(
                program_statements);
        if (!effects) {
            result.diagnostics.push_back({
                "FSIM-ELAB-SVPROGRAM-001",
                "SystemVerilog program elaboration statement is not a "
                "constant severity action",
                compiled_source_span(compiled, root.systemverilog->source),
            });
        } else {
            append_effects(std::move(*effects));
        }
    }
}

ElaborationResult elaborate_impl(
    const semantic::CompiledDesign& compiled,
    const std::span<const Root> roots,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription>
        systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries) {
    ElaborationResult result;
    const auto validated = semantic::validate_compiled_design(compiled);
    if (!validated) {
        result.diagnostics.push_back({
            "FSIM-ELAB-HIR-001",
            "elaboration rejected an invalid compiled-HIR design",
            {}});
        return result;
    }
    const auto recovered_vhdl = std::ranges::find_if(
        compiled.vhdl_units(),
        [](const semantic::vhdl::Unit& unit) {
            return !unit.profile_compatible;
        });
    if (recovered_vhdl != compiled.vhdl_units().end()) {
        result.diagnostics.push_back({
            "FSIM-ELAB-VHPROFILE-001",
            "elaboration rejected VHDL recovery nodes containing a "
            "construct unavailable in the selected language revision",
            compiled_source_span(compiled, recovered_vhdl->source)});
        return result;
    }
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

    ElaboratedDesign design { normalized_roots };

    struct ResolvedRoot {
        std::string alias;
        std::optional<semantic::CompiledUnitView> compiled_unit;
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
            root_request.alias,
            std::nullopt,
            nullptr,
            std::nullopt};
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
            const auto selected = choose_top_unit(compiled, top);
            if (selected) {
                resolved.compiled_unit = *selected;
            }
        } else {
            const auto scope = effective_search_scope(
                "work", search_libraries);
            std::vector<UnitResolutionCandidate> candidates;
            std::vector<std::string> unavailable_libraries;
            for (std::size_t index = 0; index < scope.size(); ++index) {
                const auto& library = scope[index];
                const auto library_available = has_logical_library(
                    compiled, systemc_candidates,
                    systemc_libraries, library);
                if (!library_available) {
                    if (index != 0) {
                        unavailable_libraries.push_back(library);
                    }
                    continue;
                }
                auto library_candidates = resolve_unit_candidates(
                    compiled, library, requested, true, false);
                candidates.insert(
                    candidates.end(),
                    std::make_move_iterator(library_candidates.begin()),
                    std::make_move_iterator(library_candidates.end()));
                for (const auto& factory : systemc_candidates) {
                    if (factory.library == library
                        && factory.name == requested) {
                        candidates.push_back({
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
                resolved.compiled_unit =
                    candidates.front().compiled_unit;
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
        } else if (!resolved.compiled_unit) {
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
            && (resolved.compiled_unit->vhdl == nullptr
                || resolved.compiled_unit->vhdl->kind
                    != semantic::vhdl::UnitKind::configuration)) {
            result.diagnostics.push_back({
                "FSIM-ELAB-004",
                "a qualified VHDL entity top must name an architecture, "
                "or name a configuration declaration",
                {}});
            return result;
        }
        resolved_roots.push_back(std::move(resolved));
    }

    auto builder = std::make_unique<HierarchyBuilder>(
        *validated, design, result.diagnostics, bindings,
        systemc_instances, systemc_provider, search_libraries);
    std::vector<const ResolvedRoot*> elaboration_roots;
    elaboration_roots.reserve(resolved_roots.size());
    for (const auto& root : resolved_roots) {
        elaboration_roots.push_back(&root);
    }
    const auto conventional_global = [](const ResolvedRoot& root) {
        return root.alias == "glbl"
            || (root.compiled_unit
                && root.compiled_unit->systemverilog != nullptr
                && root.compiled_unit->systemverilog->name == "glbl");
    };
    const auto references_root = [&](const ResolvedRoot& consumer,
                                     const ResolvedRoot& dependency) {
        if (!consumer.compiled_unit
            || consumer.compiled_unit->identity == nullptr
            || consumer.compiled_unit->systemverilog == nullptr
            || dependency.alias.empty()) {
            return false;
        }
        const auto prefix = dependency.alias + ".";
        const auto& scopes = compiled.semantics.scopes();
        return std::ranges::any_of(
            compiled.systemverilog_hir.expressions(),
            [&](const semantic::sv::Expression& expression) {
                if (expression.kind
                        != semantic::sv::ExpressionKind::name
                    || !expression.text.starts_with(prefix)
                    || !expression.scope.valid()
                    || expression.scope.value() >= scopes.size()) {
                    return false;
                }
                return scopes[expression.scope.value()].unit
                    == consumer.compiled_unit->identity->id;
            });
    };
    std::vector<const ResolvedRoot*> dependency_order;
    dependency_order.reserve(elaboration_roots.size());
    std::vector<bool> emitted(elaboration_roots.size());
    while (dependency_order.size() < elaboration_roots.size()) {
        std::optional<std::size_t> selected;
        for (std::size_t index { }; index < elaboration_roots.size();
            ++index) {
            if (emitted[index]) {
                continue;
            }
            auto blocked = false;
            for (std::size_t dependency { };
                dependency < elaboration_roots.size(); ++dependency) {
                if (dependency != index && !emitted[dependency]
                    && references_root(
                        *elaboration_roots[index],
                        *elaboration_roots[dependency])) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                continue;
            }
            if (!selected
                || (conventional_global(*elaboration_roots[index])
                    && !conventional_global(
                        *elaboration_roots[*selected]))) {
                selected = index;
            }
        }
        if (!selected) {
            // Preserve the requested order for a cycle. The ordinary
            // cross-root diagnostic remains responsible for rejecting an
            // unmaterializable cyclic hierarchy reference.
            for (std::size_t index { }; index < elaboration_roots.size();
                ++index) {
                if (!emitted[index]) {
                    dependency_order.push_back(elaboration_roots[index]);
                    emitted[index] = true;
                }
            }
            break;
        }
        dependency_order.push_back(elaboration_roots[*selected]);
        emitted[*selected] = true;
    }
    elaboration_roots = std::move(dependency_order);
    for (const auto* const root : elaboration_roots) {
        if (root->compiled_unit) {
            append_compiled_constant_effects(
                *validated, *root->compiled_unit, result);
        }
        if (root->constructed_systemc != nullptr) {
            builder->add_root(*root->constructed_systemc, root->alias);
        } else if (root->inferred_systemc) {
            builder->add_root(*root->inferred_systemc, root->alias);
        } else if (root->compiled_unit) {
            builder->add_root(*root->compiled_unit, root->alias);
        }
    }
    builder->finalize();

    if (!result.diagnostics.empty()) {
        return result;
    }
    result.selected_systemverilog_classes =
        builder->take_selected_systemverilog_classes();
    result.design = std::move(design);
    return result;
}

} // namespace

ElaborationResult elaborate(
    const semantic::CompiledDesign& compiled,
    const std::string_view top)
{
    const Root root { std::string { top }, simple_top_name(top) };
    return elaborate(
        compiled,
        std::span<const Root> { &root, 1U },
        { },
        { },
        nullptr,
        { });
}

ElaborationResult elaborate(
    const semantic::CompiledDesign& compiled,
    const std::span<const Root> roots,
    const std::span<const Binding> bindings,
    const std::span<const SystemCInstanceDescription> systemc_instances,
    SystemCFactoryProvider* systemc_provider,
    const std::span<const std::string> search_libraries)
{
    return elaborate_impl(
        compiled,
        roots,
        bindings,
        systemc_instances,
        systemc_provider,
        search_libraries);
}

} // namespace fsim::elaboration
