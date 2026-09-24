// SPDX-License-Identifier: Apache-2.0
#include "elaboration_vhdl_configuration_hir.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "hierarchy_builder_internal.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace fsim::elaboration {
namespace {

    std::optional<std::string> compiled_vhdl_association_actual_name(
        const semantic::CompiledDesign& compiled,
        const semantic::vhdl::Association& association)
    {
        if (association.kind != semantic::vhdl::AssociationKind::expression
            || !association.expression) {
            return std::nullopt;
        }
        const auto expression = compiled.find_expression(*association.expression);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::name) {
            return std::nullopt;
        }
        if (expression->vhdl->referenced_name
            && expression->vhdl->referenced_name->selected) {
            const auto declaration = compiled.find_declaration(
                *expression->vhdl->referenced_name->selected);
            if (declaration && declaration->vhdl != nullptr) {
                return declaration->vhdl->name;
            }
        }
        return expression->vhdl->text;
    }

    std::vector<semantic::vhdl::Association>
    compose_compiled_vhdl_associations(
        const semantic::CompiledDesign& compiled,
        const std::span<const semantic::vhdl::Association> instance,
        const std::span<const semantic::vhdl::Association> binding,
        const std::span<const semantic::DeclarationId> component_formals)
    {
        if (binding.empty()) {
            return std::vector<semantic::vhdl::Association> {
                instance.begin(), instance.end()
            };
        }
        std::vector<semantic::vhdl::Association> result;
        result.reserve(binding.size() + instance.size());
        std::vector<bool> consumed(instance.size());
        for (const auto& mapped : binding) {
            auto composed = mapped;
            const auto component_formal = compiled_vhdl_association_actual_name(
                compiled, mapped);
            if (component_formal) {
                bool substituted { };
                for (std::size_t index = 0U; index < instance.size(); ++index) {
                    if (!instance[index].formal
                        || !vhdl_configuration_detail::configuration_name_equal(
                            instance[index].formal->spelling,
                            *component_formal)) {
                        continue;
                    }
                    composed.kind = instance[index].kind;
                    composed.expression = instance[index].expression;
                    composed.type = instance[index].type;
                    consumed[index] = true;
                    substituted = true;
                    break;
                }
                if (!substituted) {
                    const auto formal = std::ranges::find_if(
                        component_formals,
                        [&](const semantic::DeclarationId declaration_id) {
                            const auto declaration
                                = compiled.find_declaration(declaration_id);
                            return declaration
                                && declaration->vhdl != nullptr
                                && vhdl_configuration_detail::
                                    configuration_name_equal(
                                        declaration->vhdl->name,
                                        *component_formal);
                        });
                    const auto index = formal == component_formals.end()
                        ? component_formals.size()
                        : static_cast<std::size_t>(std::distance(
                              component_formals.begin(), formal));
                    if (index < instance.size()
                        && !instance[index].formal) {
                        composed.kind = instance[index].kind;
                        composed.expression = instance[index].expression;
                        composed.type = instance[index].type;
                        consumed[index] = true;
                    }
                }
            }
            result.push_back(std::move(composed));
        }
        for (std::size_t index = 0U; index < instance.size(); ++index) {
            if (consumed[index]) {
                continue;
            }
            auto residual = instance[index];
            if (!residual.formal && index < component_formals.size()) {
                const auto formal = compiled.find_declaration(
                    component_formals[index]);
                if (formal && formal->vhdl != nullptr) {
                    residual.formal = semantic::vhdl::Name {
                        formal->vhdl->name,
                        formal->vhdl->name,
                        residual.source,
                        component_formals[index],
                        { },
                    };
                }
            }
            if (!residual.formal
                || std::ranges::none_of(
                    result, [&](const auto& mapped) {
                        return mapped.formal
                            && vhdl_configuration_detail::
                                configuration_name_equal(
                                    mapped.formal->spelling,
                                    residual.formal->spelling);
                    })) {
                result.push_back(std::move(residual));
            }
        }
        return result;
    }

}

namespace hierarchy_vhdl_associations_detail {

    const char* generic_diagnostic_code(
        const semantic::SpecializedHirAssociationDiagnostic diagnostic)
    {
        using Diagnostic = semantic::SpecializedHirAssociationDiagnostic;
        switch (diagnostic) {
        case Diagnostic::invalid_actual:
        case Diagnostic::ambiguous_name:
            return "FSIM-ELAB-GENERIC-001";
        case Diagnostic::duplicate_actual:
            return "FSIM-ELAB-GENERIC-002";
        case Diagnostic::association_order:
            return "FSIM-ELAB-GENERIC-003";
        case Diagnostic::subtype_constraint:
            return "FSIM-ELAB-GENERIC-008";
        case Diagnostic::missing_type_actual:
        case Diagnostic::type_actual_for_value_parameter:
        case Diagnostic::value_actual_for_type_parameter:
            return "FSIM-ELAB-GENERIC-001";
        case Diagnostic::callable_result_profile:
            return "FSIM-ELAB-VHFUNC-003";
        case Diagnostic::callable_name_required:
        case Diagnostic::callable_no_match:
        case Diagnostic::callable_ambiguous:
        case Diagnostic::callable_body_missing:
        case Diagnostic::callable_impure:
        case Diagnostic::callable_wrong_kind:
        case Diagnostic::callable_scoped:
        case Diagnostic::callable_language_mismatch:
        case Diagnostic::callable_time_dependent:
            return "FSIM-ELAB-GENERIC-001";
        case Diagnostic::package_language_mismatch:
            return "FSIM-ELAB-VHPKG-002";
        }
        return "FSIM-ELAB-GENERIC-001";
    }

}

HierarchyBuilder::CompiledVhdlAssociationResolution
HierarchyBuilder::resolve_compiled_vhdl_associations(
    const semantic::UnitId child_id,
    const semantic::vhdl::Instance& record,
    const semantic::vhdl::Instance& effective_instance,
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::vhdl::ComponentConfiguration* const selected_rule,
    const semantic::vhdl::Unit* const target_entity)
{
    CompiledVhdlAssociationResolution result;
    result.generic_bindings
        = semantic::resolve_specialized_hir_associations(
            *compiled_, child_id,
            semantic::CompiledInstanceView { nullptr, &effective_instance },
            semantic::SpecializedHirAssociationSurface::parameters,
            &working_specialization);
    result.port_bindings
        = semantic::resolve_specialized_hir_associations(
            *compiled_, child_id,
            semantic::CompiledInstanceView { nullptr, &effective_instance },
            semantic::SpecializedHirAssociationSurface::ports,
            &working_specialization);

    const auto association_has_source
        = [](const std::span<const semantic::vhdl::Association> associations,
              const semantic::SourceSpanId source) {
              return source.valid()
                  && std::ranges::any_of(
                      associations,
                      [&](const semantic::vhdl::Association& association) {
                          return association.source == source;
                      });
          };
    const auto append_diagnostic
        = [&](std::string code, std::string message,
              const semantic::SourceSpanId source) {
              result.diagnostics.push_back({ std::move(code), std::move(message), source });
          };
    const auto& generic_bindings = result.generic_bindings;
    const auto& port_bindings = result.port_bindings;

    if (!generic_bindings && generic_bindings.issues.empty()) {
        const auto diagnostic = record.component
                && association_has_source(
                    record.generic_map, generic_bindings.error_source)
            ? std::string { "FSIM-ELAB-VHCOMP-008" }
            : selected_rule != nullptr && record.component
            ? std::string { "FSIM-ELAB-VHCOMP-010" }
            : selected_rule != nullptr
            ? std::string { "FSIM-ELAB-VHCONFIG-016" }
            : std::string {
                  hierarchy_vhdl_associations_detail::generic_diagnostic_code(
                      semantic::SpecializedHirAssociationDiagnostic::
                          invalid_actual)
              };
        append_diagnostic(
            diagnostic,
            "compiled VHDL generic association for '" + record.name
                + "' is unsupported: " + generic_bindings.error,
            generic_bindings.error_source.valid()
                ? generic_bindings.error_source
                : record.source);
    }
    for (const auto& issue : generic_bindings.issues) {
        const bool component_actual = record.component
            && association_has_source(record.generic_map, issue.source);
        auto diagnostic = component_actual
            ? std::string { "FSIM-ELAB-VHCOMP-008" }
            : selected_rule != nullptr && record.component
            ? std::string { "FSIM-ELAB-VHCOMP-010" }
            : selected_rule != nullptr
            ? std::string { "FSIM-ELAB-VHCONFIG-016" }
            : std::string {
                  hierarchy_vhdl_associations_detail::generic_diagnostic_code(
                      issue.diagnostic)
              };
        if (issue.callable_function
            && issue.diagnostic
                == semantic::SpecializedHirAssociationDiagnostic::
                    callable_language_mismatch) {
            diagnostic = *issue.callable_function
                ? "FSIM-ELAB-VHFUNC-002"
                : "FSIM-ELAB-VHPROC-002";
        }
        if (issue.formal) {
            const auto formal = compiled_->find_declaration(*issue.formal);
            const bool unspecified_type_formal
                = formal && formal->vhdl != nullptr
                && formal->vhdl->subtype
                && formal->vhdl->subtype->unspecified_class
                    != semantic::vhdl::UnspecifiedTypeClass::none;
            if (unspecified_type_formal) {
                diagnostic = "FSIM-ELAB-VHUNSPEC-002";
            }
            if (formal && formal->vhdl != nullptr
                && formal->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_type
                && !unspecified_type_formal) {
                using AssociationDiagnostic
                    = semantic::SpecializedHirAssociationDiagnostic;
                switch (issue.diagnostic) {
                case AssociationDiagnostic::missing_type_actual:
                    diagnostic = "FSIM-ELAB-GENTYPE-001";
                    break;
                case AssociationDiagnostic::invalid_actual:
                    diagnostic = "FSIM-ELAB-GENTYPE-002";
                    break;
                case AssociationDiagnostic::value_actual_for_type_parameter:
                    diagnostic = "FSIM-ELAB-GENTYPE-003";
                    break;
                default:
                    break;
                }
            }
            if ((!component_actual
                    || issue.diagnostic
                        == semantic::SpecializedHirAssociationDiagnostic::
                            callable_language_mismatch)
                && formal && formal->vhdl != nullptr
                && (formal->vhdl->form
                        == semantic::vhdl::DeclarationForm::generic_function
                    || formal->vhdl->form
                        == semantic::vhdl::DeclarationForm::generic_procedure)) {
                using AssociationDiagnostic
                    = semantic::SpecializedHirAssociationDiagnostic;
                const bool function = formal->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_function;
                switch (issue.diagnostic) {
                case AssociationDiagnostic::callable_result_profile:
                case AssociationDiagnostic::callable_name_required:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-003"
                                          : "FSIM-ELAB-VHPROC-003";
                    break;
                case AssociationDiagnostic::callable_no_match:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-005"
                                          : "FSIM-ELAB-VHPROC-005";
                    break;
                case AssociationDiagnostic::callable_ambiguous:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-006"
                                          : "FSIM-ELAB-VHPROC-006";
                    break;
                case AssociationDiagnostic::callable_body_missing:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-007"
                                          : "FSIM-ELAB-VHPROC-007";
                    break;
                case AssociationDiagnostic::callable_impure:
                    diagnostic = "FSIM-ELAB-VHFUNC-008";
                    break;
                case AssociationDiagnostic::callable_wrong_kind:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-005"
                                          : "FSIM-ELAB-VHPROC-008";
                    break;
                case AssociationDiagnostic::callable_scoped:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-005"
                                          : "FSIM-ELAB-VHPROC-012";
                    break;
                case AssociationDiagnostic::callable_language_mismatch:
                    diagnostic = function ? "FSIM-ELAB-VHFUNC-002"
                                          : "FSIM-ELAB-VHPROC-002";
                    break;
                case AssociationDiagnostic::callable_time_dependent:
                    diagnostic = "FSIM-ELAB-VHPROC-009";
                    break;
                default:
                    break;
                }
            }
        }
        if (issue.diagnostic
            == semantic::SpecializedHirAssociationDiagnostic::
                type_actual_for_value_parameter) {
            diagnostic = "FSIM-ELAB-GENTYPE-005";
        }
        if (issue.diagnostic
            == semantic::SpecializedHirAssociationDiagnostic::
                value_actual_for_type_parameter) {
            diagnostic = "FSIM-ELAB-GENTYPE-003";
        }
        append_diagnostic(
            std::move(diagnostic), issue.message,
            issue.source.valid() ? issue.source : record.source);
    }

    if (!port_bindings && port_bindings.issues.empty()) {
        const auto diagnostic = record.component
                && association_has_source(
                    record.port_map, port_bindings.error_source)
            ? std::string { "FSIM-ELAB-VHCOMP-009" }
            : selected_rule != nullptr
            ? std::string { "FSIM-ELAB-VHCONFIG-016" }
            : std::string { "FSIM-ELAB-HIR-001" };
        append_diagnostic(
            diagnostic,
            "compiled VHDL port association for '" + record.name
                + "' is unsupported: " + port_bindings.error,
            port_bindings.error_source.valid()
                ? port_bindings.error_source
                : record.source);
    }
    for (const auto& issue : port_bindings.issues) {
        const auto diagnostic = record.component
                && association_has_source(record.port_map, issue.source)
            ? "FSIM-ELAB-VHCOMP-009"
            : selected_rule != nullptr
            ? "FSIM-ELAB-VHCONFIG-016"
            : "FSIM-ELAB-HIR-001";
        append_diagnostic(
            diagnostic, issue.message,
            issue.source.valid() ? issue.source : record.source);
    }
    if (record.component && target_entity != nullptr) {
        for (const auto declaration_id : target_entity->declarations) {
            const auto declaration = compiled_->find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || declaration->vhdl->form
                    != semantic::vhdl::DeclarationForm::generic_package) {
                continue;
            }
            const auto binding = std::ranges::find(
                generic_bindings.bindings, declaration_id,
                &semantic::SpecializedHirAssociationBinding::formal);
            if (binding != generic_bindings.bindings.end()
                && binding->actual_declaration) {
                continue;
            }
            append_diagnostic(
                "FSIM-ELAB-VHCOMP-008",
                "component generic package '" + declaration->vhdl->name
                    + "' requires an actual package instance",
                record.source);
        }
    }

    return result;
}

std::unordered_set<std::uint32_t>
HierarchyBuilder::materialize_compiled_vhdl_component_port_defaults(
    const semantic::vhdl::Instance& record,
    const semantic::vhdl::Declaration* const component,
    const semantic::vhdl::Unit* const target_entity,
    std::vector<semantic::SpecializedHirAssociationBinding>& port_bindings)
{
    std::unordered_set<std::uint32_t> defaulted_port_formals;
    if (component == nullptr || !component->component
        || target_entity == nullptr) {
        return defaulted_port_formals;
    }

    std::vector<semantic::DeclarationId> target_ports;
    for (const auto declaration_id : target_entity->declarations) {
        const auto declaration = compiled_->find_declaration(declaration_id);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->form
                == semantic::vhdl::DeclarationForm::port) {
            target_ports.push_back(declaration_id);
        }
    }
    std::vector<const semantic::vhdl::Association*> positional_actuals;
    for (const auto& association : record.port_map) {
        if (!association.formal) {
            positional_actuals.push_back(&association);
        }
    }
    for (auto& binding : port_bindings) {
        const auto target = std::ranges::find(target_ports, binding.formal);
        if (target == target_ports.end()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(target_ports.begin(), target));
        if (index >= component->component->ports.size()) {
            continue;
        }
        const auto component_formal = compiled_->find_declaration(
            component->component->ports[index]);
        if (!component_formal || component_formal->vhdl == nullptr
            || !component_formal->vhdl->initializer) {
            continue;
        }
        const auto named_actual = std::ranges::find_if(
            record.port_map,
            [&](const semantic::vhdl::Association& association) {
                return association.formal
                    && vhdl_configuration_detail::
                        configuration_name_equal(
                            association.formal->spelling,
                            component_formal->vhdl->name);
            });
        const auto* explicit_actual
            = named_actual != record.port_map.end()
            ? &*named_actual
            : index < positional_actuals.size()
            ? positional_actuals[index]
            : nullptr;
        if (explicit_actual != nullptr
            && explicit_actual->kind
                == semantic::vhdl::AssociationKind::expression) {
            continue;
        }
        binding.kind
            = semantic::SpecializedHirAssociationKind::expression;
        binding.expression = component_formal->vhdl->initializer;
        binding.actual_declaration.reset();
        defaulted_port_formals.insert(binding.formal.value());
    }
    return defaulted_port_formals;
}

std::vector<const semantic::vhdl::Declaration*>
HierarchyBuilder::collect_visible_compiled_vhdl_components(
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::ScopeId instance_scope,
    const std::string_view target_name)
{
    std::vector<const semantic::vhdl::Declaration*> visible_components;
    const auto& scopes = compiled_->semantics.scopes();
    const auto& units = compiled_->semantics.units();
    const auto find_scope = [&](const semantic::ScopeId id) {
        const auto scope = std::ranges::find(
            scopes, id, &semantic::Scope::id);
        return scope == scopes.end() ? nullptr : &*scope;
    };
    const auto name_equal = [](
                                const std::string_view left,
                                const std::string_view right) {
        return vhdl_configuration_detail::configuration_name_equal(
            left, right);
    };
    const auto library_equal = [&](
                                   const std::string_view left,
                                   const std::string_view right) {
        return name_equal(
            left.empty() ? std::string_view { "work" } : left,
            right.empty() ? std::string_view { "work" } : right);
    };
    const auto declaration_visible_from_instance = [&](
                                                       const semantic::ScopeId declaration_scope) {
        const auto* declared_scope = find_scope(declaration_scope);
        if (declared_scope == nullptr) {
            return false;
        }
        if (declared_scope->unit != architecture.id) {
            return true;
        }
        auto scope = std::optional<semantic::ScopeId> { instance_scope };
        for (std::size_t depth { };
            scope && depth <= scopes.size(); ++depth) {
            if (*scope == declaration_scope) {
                return true;
            }
            const auto* current = find_scope(*scope);
            scope = current != nullptr ? current->parent : std::nullopt;
        }
        return false;
    };
    const auto associated_entity = [&](const semantic::vhdl::Unit& owner) {
        return owner.kind == semantic::vhdl::UnitKind::entity
            && library_equal(owner.library, architecture.library)
            && name_equal(owner.name, architecture.primary_name);
    };

    for (const auto& declaration : compiled_->vhdl_hir.declarations()) {
        if (!declaration.component
            || !name_equal(declaration.name, target_name)) {
            continue;
        }
        const auto* scope = find_scope(declaration.scope);
        if (scope == nullptr) {
            continue;
        }
        const auto unit
            = std::ranges::find(units, scope->unit, &semantic::Unit::id);
        if (unit == units.end()) {
            continue;
        }
        const auto owner_unit = compiled_->find_unit(unit->id);
        const auto* owner = owner_unit ? owner_unit->vhdl : nullptr;
        if (owner == nullptr
            || (owner->id == architecture.id
                && !declaration_visible_from_instance(declaration.scope))
            || (owner->id != architecture.id
                && !associated_entity(*owner)
                && !semantic::CompiledDesignResolver {
                    *compiled_, architecture.id, &working_specialization }
                    .vhdl_package_member_visible(architecture, *owner, target_name))) {
            continue;
        }
        if (std::ranges::find(visible_components, &declaration)
            == visible_components.end()) {
            visible_components.push_back(&declaration);
        }
    }
    return visible_components;
}

std::vector<semantic::SourceSpanId>
HierarchyBuilder::prepare_compiled_vhdl_component_associations(
    const semantic::vhdl::Instance& record,
    const semantic::vhdl::ComponentConfiguration* const selected_rule,
    const semantic::vhdl::Declaration* const component,
    const semantic::vhdl::Unit* const target_entity,
    semantic::vhdl::Instance& effective_instance)
{
    if (selected_rule != nullptr && component != nullptr
        && component->component) {
        effective_instance.generic_map
            = compose_compiled_vhdl_associations(
                *compiled_, record.generic_map,
                selected_rule->binding.generic_map,
                component->component->generics);
        effective_instance.port_map
            = compose_compiled_vhdl_associations(
                *compiled_, record.port_map,
                selected_rule->binding.port_map,
                component->component->ports);
    }

    std::vector<semantic::SourceSpanId> missing_generic_default_sources;
    const auto remap_surface = [&](
                                   std::vector<semantic::vhdl::Association>& associations,
                                   const bool ports) {
        if (component == nullptr || !component->component
            || target_entity == nullptr) {
            return;
        }
        const auto& component_formals = ports
            ? component->component->ports
            : component->component->generics;
        std::vector<semantic::DeclarationId> target_formals;
        for (const auto declaration_id : target_entity->declarations) {
            const auto declaration
                = compiled_->find_declaration(declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            const auto form = declaration->vhdl->form;
            if ((ports
                    && form == semantic::vhdl::DeclarationForm::port)
                || (!ports
                    && (form
                            == semantic::vhdl::DeclarationForm::
                                generic_constant
                        || form
                            == semantic::vhdl::DeclarationForm::generic_type
                        || form
                            == semantic::vhdl::DeclarationForm::
                                generic_function
                        || form
                            == semantic::vhdl::DeclarationForm::
                                generic_procedure
                        || form
                            == semantic::vhdl::DeclarationForm::
                                generic_package))) {
                target_formals.push_back(declaration_id);
            }
        }
        std::size_t positional { };
        std::vector<bool> mapped(component_formals.size());
        for (auto& association : associations) {
            std::size_t index = component_formals.size();
            if (association.formal) {
                const auto found = std::ranges::find_if(
                    component_formals,
                    [&](const auto formal_id) {
                        const auto formal
                            = compiled_->find_declaration(formal_id);
                        return formal && formal->vhdl != nullptr
                            && vhdl_configuration_detail::
                                configuration_name_equal(
                                    formal->vhdl->name,
                                    association.formal->spelling);
                    });
                if (found != component_formals.end()) {
                    index = static_cast<std::size_t>(
                        std::distance(component_formals.begin(), found));
                } else {
                    const auto target_found = std::ranges::find_if(
                        target_formals,
                        [&](const auto formal_id) {
                            const auto formal
                                = compiled_->find_declaration(formal_id);
                            return formal && formal->vhdl != nullptr
                                && vhdl_configuration_detail::
                                    configuration_name_equal(
                                        formal->vhdl->name,
                                        association.formal->spelling);
                        });
                    if (target_found != target_formals.end()) {
                        index = static_cast<std::size_t>(
                            std::distance(target_formals.begin(), target_found));
                        if (index < mapped.size()) {
                            mapped[index] = true;
                        }
                        continue;
                    }
                }
            } else {
                index = positional++;
            }
            if (index >= target_formals.size()) {
                continue;
            }
            if (index < mapped.size()) {
                mapped[index] = true;
            }
            if (!ports
                && (association.kind
                        == semantic::vhdl::AssociationKind::open
                    || association.kind
                        == semantic::vhdl::AssociationKind::default_box)
                && index < component_formals.size()) {
                const auto component_formal = compiled_->find_declaration(
                    component_formals[index]);
                if (component_formal && component_formal->vhdl != nullptr
                    && component_formal->vhdl->initializer) {
                    association.kind
                        = semantic::vhdl::AssociationKind::expression;
                    association.expression
                        = component_formal->vhdl->initializer;
                } else if (association.kind
                    == semantic::vhdl::AssociationKind::default_box) {
                    missing_generic_default_sources.push_back(
                        association.source);
                }
            }
            const auto target = compiled_->find_declaration(
                target_formals[index]);
            if (!target || target->vhdl == nullptr) {
                continue;
            }
            association.formal = semantic::vhdl::Name {
                target->vhdl->name,
                target->vhdl->name,
                association.source,
                target_formals[index],
                { }
            };
        }
        for (std::size_t index { };
            index < component_formals.size()
            && index < target_formals.size();
            ++index) {
            if (mapped[index]) {
                continue;
            }
            const auto component_formal = compiled_->find_declaration(
                component_formals[index]);
            const auto target = compiled_->find_declaration(
                target_formals[index]);
            if (!component_formal || component_formal->vhdl == nullptr
                || !component_formal->vhdl->initializer
                || !target || target->vhdl == nullptr) {
                continue;
            }
            associations.push_back(
                semantic::vhdl::Association {
                    semantic::vhdl::Name {
                        target->vhdl->name,
                        target->vhdl->name,
                        component_formal->vhdl->source,
                        target_formals[index],
                        { },
                    },
                    semantic::vhdl::AssociationKind::expression,
                    component_formal->vhdl->initializer,
                    std::nullopt,
                    component_formal->vhdl->source,
                });
        }
    };
    remap_surface(effective_instance.generic_map, false);
    remap_surface(effective_instance.port_map, true);
    return missing_generic_default_sources;
}

}
