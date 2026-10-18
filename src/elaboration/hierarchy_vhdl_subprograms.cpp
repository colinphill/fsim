// SPDX-License-Identifier: Apache-2.0
#include "elaboration_vhdl_configuration_hir.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "hierarchy_builder_internal.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {

HierarchyBuilder::CompiledVhdlSubprogramValidationResult
HierarchyBuilder::validate_compiled_vhdl_subprograms(
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture,
    const semantic::SpecializedHirUnit& specialized)
{
    CompiledVhdlSubprogramValidationResult result;
    const auto append_diagnostic
        = [&](std::string code, std::string message,
              const semantic::SourceSpanId source) {
              result.diagnostics.push_back({ std::move(code), std::move(message), source });
          };
    const auto name_equal = [](const std::string_view left,
                                const std::string_view right) {
        return vhdl_configuration_detail::configuration_name_equal(
            left, right);
    };
    const auto name_part_count = [](const std::string_view spelling) {
        std::size_t count { };
        std::size_t begin { };
        while (begin < spelling.size()) {
            const auto end = spelling.find('.', begin);
            if (end != begin) {
                ++count;
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1U;
        }
        return count;
    };
    const auto library_equal = [&](const std::string_view left,
                                   const std::string_view right) {
        return name_equal(
            left.empty() ? std::string_view { "work" } : left,
            right.empty() ? std::string_view { "work" } : right);
    };

    const semantic::CompiledDesignResolver resolver { specialized };
    bool callable_profiles_valid { true };
    const auto validate_callable_profiles
        = [&](const std::span<const semantic::DeclarationId> declarations) {
              for (const auto& duplicate :
                  resolver.duplicate_vhdl_callable_profiles(declarations)) {
                  const auto declaration = specialized.find_declaration(
                      duplicate.duplicate);
                  if (!declaration || declaration->vhdl == nullptr) {
                      continue;
                  }
                  append_diagnostic(
                      duplicate.function
                          ? "FSIM-ELAB-VHOVER-003"
                          : "FSIM-ELAB-VHOVER-006",
                      "duplicate VHDL "
                          + std::string { duplicate.function
                                  ? "function profile '"
                                  : "procedure profile '" }
                          + declaration->vhdl->name + "'",
                      declaration->vhdl->source);
                  callable_profiles_valid = false;
              }
          };
    validate_callable_profiles(entity.declarations);
    validate_callable_profiles(architecture.declarations);
    if (!callable_profiles_valid) {
        result.valid = false;
        return result;
    }

    bool generic_callables_valid { true };
    const auto validate_generic_callables
        = [&](const std::span<const semantic::DeclarationId> declarations) {
              using Form = semantic::vhdl::DeclarationForm;
              const auto scope_within = [&](const semantic::ScopeId inner,
                                            const semantic::ScopeId outer) {
                  return semantic::scope_within(
                      compiled_->semantics, inner, outer);
              };
              for (const auto declaration_id : declarations) {
                  const auto declaration
                      = specialized.find_declaration(declaration_id);
                  if (!declaration || declaration->vhdl == nullptr
                      || !declaration->vhdl->package) {
                      continue;
                  }
                  const auto form = declaration->vhdl->form;
                  const auto function
                      = form == Form::generic_function_instance;
                  if (!function
                      && form != Form::generic_procedure_instance) {
                      continue;
                  }
                  const auto& template_name
                      = declaration->vhdl->package->template_name;
                  const auto spelling = template_name.text();
                  if (name_part_count(spelling) != 1U) {
                      append_diagnostic(
                          "FSIM-ELAB-VHGSUB-009",
                          "selected or scoped generic subprogram templates "
                          "are outside the bounded directly visible subset",
                          declaration->vhdl->source);
                      generic_callables_valid = false;
                      continue;
                  }
                  const auto template_resolution = resolver.resolve_vhdl(
                      template_name, declaration->vhdl->scope);
                  const auto template_id = template_resolution.unique();
                  if (!template_id) {
                      append_diagnostic(
                          template_resolution.status
                                  == semantic::CompiledResolutionStatus::
                                      ambiguous
                              ? "FSIM-ELAB-VHGSUB-002"
                              : "FSIM-ELAB-VHGSUB-001",
                          template_resolution.status
                                  == semantic::CompiledResolutionStatus::
                                      ambiguous
                              ? "generic subprogram template is ambiguous"
                              : "generic subprogram template is not visible",
                          declaration->vhdl->source);
                      generic_callables_valid = false;
                      continue;
                  }
                  const auto template_declaration
                      = specialized.find_declaration(*template_id);
                  const auto expected_form = function
                      ? Form::generic_function_template
                      : Form::generic_procedure_template;
                  if (!template_declaration
                      || template_declaration->vhdl == nullptr
                      || template_declaration->vhdl->form
                          != expected_form) {
                      append_diagnostic(
                          "FSIM-ELAB-VHGSUB-003",
                          "generic subprogram instantiation selects the "
                          "wrong subprogram kind or a nongeneric subprogram",
                          declaration->vhdl->source);
                      generic_callables_valid = false;
                      continue;
                  }
                  const auto callable
                      = resolver.resolve_vhdl_callable(declaration_id)
                            .unique();
                  if (!callable) {
                      append_diagnostic(
                          "FSIM-ELAB-VHGSUB-004",
                          "generic subprogram template has no conforming "
                          "executable body",
                          declaration->vhdl->source);
                      generic_callables_valid = false;
                      continue;
                  }
                  const auto body = specialized.find_declaration(
                      callable->body);
                  if (!body || body->vhdl == nullptr
                      || !body->vhdl->nested_scope) {
                      continue;
                  }
                  const auto recursive = std::ranges::any_of(
                      compiled_->vhdl_hir.expressions(),
                      [&](const semantic::vhdl::Expression& expression) {
                          if (expression.kind
                                  != semantic::vhdl::ExpressionKind::call
                              || !expression.referenced_name
                              || !scope_within(expression.scope,
                                  *body->vhdl->nested_scope)) {
                              return false;
                          }
                          const auto& reference
                              = *expression.referenced_name;
                          return (reference.selected
                                     && (*reference.selected
                                             == *template_id
                                         || *reference.selected
                                             == callable->body))
                              || name_equal(
                                  reference.text(),
                                  template_declaration->vhdl->name);
                      });
                  if (recursive) {
                      append_diagnostic(
                          "FSIM-ELAB-VHGSUB-007",
                          "bounded generic subprogram template contains a "
                          "recursive call",
                          declaration->vhdl->source);
                      generic_callables_valid = false;
                  }
              }
          };
    validate_generic_callables(entity.declarations);
    validate_generic_callables(architecture.declarations);
    for (const auto& expression : compiled_->vhdl_hir.expressions()) {
        using Form = semantic::vhdl::DeclarationForm;
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || !expression.referenced_name) {
            continue;
        }
        std::vector<semantic::DeclarationId> targets;
        if (expression.referenced_name->selected) {
            targets.push_back(*expression.referenced_name->selected);
        }
        targets.insert(targets.end(),
            expression.referenced_name->overloads.begin(),
            expression.referenced_name->overloads.end());
        const auto template_call = std::ranges::any_of(
            targets, [&](const semantic::DeclarationId target_id) {
                const auto target = specialized.find_declaration(target_id);
                return target && target->vhdl != nullptr
                    && (target->vhdl->form
                            == Form::generic_function_template
                        || target->vhdl->form
                            == Form::generic_procedure_template);
            });
        if (!template_call) {
            continue;
        }
        const auto& scopes = compiled_->semantics.scopes();
        const auto expression_scope = std::ranges::find(
            scopes, expression.scope, &semantic::Scope::id);
        if (expression_scope == scopes.end()
            || (expression_scope->unit != entity.id
                && expression_scope->unit != architecture.id)) {
            continue;
        }
        append_diagnostic(
            "FSIM-ELAB-VHGSUB-006",
            "generic subprogram template is called before being "
            "instantiated",
            expression.source);
        generic_callables_valid = false;
    }

    const auto generic_template_profile
        = [&](const semantic::vhdl::Declaration& declaration) {
              std::vector<semantic::DeclarationId> generics;
              std::optional<semantic::DeclarationId> callable;
              for (const auto child : declaration.children) {
                  const auto record = specialized.find_declaration(child);
                  if (!record || record->vhdl == nullptr) {
                      continue;
                  }
                  using Form = semantic::vhdl::DeclarationForm;
                  const auto form = record->vhdl->form;
                  if (semantic::vhdl::is_generic_formal(form)) {
                      generics.push_back(child);
                  } else if (form == Form::function
                      || form == Form::procedure) {
                      callable = child;
                  }
              }
              return std::pair { std::move(generics), callable };
          };
    // Two subtype indications conform when they name the same type mark
    // in their own scopes; a generic type of the declaration's generic list
    // corresponds to the same-named one of the body's (IEEE 1076-2008 4.10).
    // Names of the generic types common to the two generic lists being
    // compared; such a type mark denotes corresponding generics.
    std::vector<std::string> corresponding_generic_types;
    const auto both_generic_types
        = [&](const semantic::vhdl::SubtypeIndication& left,
              const semantic::vhdl::SubtypeIndication& right) {
              const auto name = simple_name_of(left.type_mark.spelling);
              return left.constraints.empty() && right.constraints.empty()
                  && name_equal(
                      name, simple_name_of(right.type_mark.spelling))
                  && std::ranges::any_of(corresponding_generic_types,
                      [&](const std::string& generic) {
                          return name_equal(generic, name);
                      });
          };
    const auto subtypes_conform
        = [&](const semantic::vhdl::SubtypeIndication& left,
              const semantic::ScopeId left_scope,
              const semantic::vhdl::SubtypeIndication& right,
              const semantic::ScopeId right_scope) {
              const auto left_name
                  = simple_name_of(left.type_mark.spelling);
              const auto right_name
                  = simple_name_of(right.type_mark.spelling);
              return (left_name.empty() || right_name.empty()
                         || name_equal(left_name, right_name))
                  && (both_generic_types(left, right)
                      || semantic::CompiledDesignResolver { specialized }
                             .vhdl_subtype_profiles_match(
                                 left, left_scope, right, right_scope));
          };
    const auto callables_conform
        = [&](const semantic::DeclarationId left_id,
              const semantic::DeclarationId right_id) {
              if (resolver.vhdl_callable_profile_matches(
                      left_id, right_id, true)) {
                  return true;
              }
              const auto left = specialized.find_declaration(left_id);
              const auto right = specialized.find_declaration(right_id);
              if (!left || left->vhdl == nullptr || !left->vhdl->callable
                  || !right || right->vhdl == nullptr
                  || !right->vhdl->callable) {
                  return false;
              }
              const auto& left_callable = *left->vhdl->callable;
              const auto& right_callable = *right->vhdl->callable;
              if (left_callable.function != right_callable.function
                  || left_callable.formals.size()
                      != right_callable.formals.size()
                  || left_callable.return_type.has_value()
                      != right_callable.return_type.has_value()) {
                  return false;
              }
              if (left_callable.return_type
                  && !subtypes_conform(*left_callable.return_type,
                      left->vhdl->scope, *right_callable.return_type,
                      right->vhdl->scope)) {
                  return false;
              }
              for (std::size_t index { };
                  index < left_callable.formals.size(); ++index) {
                  const auto left_formal = specialized.find_declaration(
                      left_callable.formals[index]);
                  const auto right_formal = specialized.find_declaration(
                      right_callable.formals[index]);
                  if (!left_formal || left_formal->vhdl == nullptr
                      || !left_formal->vhdl->subtype || !right_formal
                      || right_formal->vhdl == nullptr
                      || !right_formal->vhdl->subtype
                      || !subtypes_conform(*left_formal->vhdl->subtype,
                          left_formal->vhdl->scope,
                          *right_formal->vhdl->subtype,
                          right_formal->vhdl->scope)) {
                      return false;
                  }
              }
              return true;
          };
    const auto generic_formals_conform
        = [&](const std::span<const semantic::DeclarationId> specification,
              const std::span<const semantic::DeclarationId> body) {
              if (specification.size() != body.size()) {
                  return false;
              }
              for (std::size_t index { }; index < specification.size();
                  ++index) {
                  const auto left = specialized.find_declaration(
                      specification[index]);
                  const auto right = specialized.find_declaration(
                      body[index]);
                  if (!left || left->vhdl == nullptr
                      || !right || right->vhdl == nullptr
                      || left->vhdl->form != right->vhdl->form
                      || !name_equal(
                          left->vhdl->name, right->vhdl->name)) {
                      return false;
                  }
                  if ((left->vhdl->subtype || right->vhdl->subtype)
                      && (!left->vhdl->subtype || !right->vhdl->subtype
                          || !subtypes_conform(*left->vhdl->subtype,
                              left->vhdl->scope, *right->vhdl->subtype,
                              right->vhdl->scope))) {
                      return false;
                  }
                  if ((left->vhdl->callable || right->vhdl->callable)
                      && !callables_conform(
                          specification[index], body[index])) {
                      return false;
                  }
              }
              return true;
          };
    for (const auto& specification_unit : compiled_->vhdl_units()) {
        if (specification_unit.kind != semantic::vhdl::UnitKind::package
            || !specification_unit.primary_name.empty()) {
            continue;
        }
        for (const auto& body_unit : compiled_->vhdl_units()) {
            if (body_unit.kind != semantic::vhdl::UnitKind::package
                || body_unit.primary_name.empty()
                || !library_equal(
                    specification_unit.library, body_unit.library)
                || !name_equal(
                    specification_unit.name, body_unit.name)) {
                continue;
            }
            for (const auto specification_id :
                specification_unit.declarations) {
                const auto specification
                    = specialized.find_declaration(specification_id);
                if (!specification || specification->vhdl == nullptr) {
                    continue;
                }
                using Form = semantic::vhdl::DeclarationForm;
                const auto form = specification->vhdl->form;
                if (form != Form::generic_function_template
                    && form != Form::generic_procedure_template) {
                    continue;
                }
                const auto body_id = std::ranges::find_if(
                    body_unit.declarations,
                    [&](const semantic::DeclarationId candidate_id) {
                        const auto candidate
                            = specialized.find_declaration(candidate_id);
                        return candidate && candidate->vhdl != nullptr
                            && candidate->vhdl->form == form
                            && name_equal(
                                candidate->vhdl->name,
                                specification->vhdl->name);
                    });
                if (body_id == body_unit.declarations.end()) {
                    continue;
                }
                const auto body = specialized.find_declaration(*body_id);
                const auto specification_profile
                    = generic_template_profile(*specification->vhdl);
                const auto body_profile
                    = generic_template_profile(*body->vhdl);
                corresponding_generic_types.clear();
                for (const auto generic : specification_profile.first) {
                    const auto record = specialized.find_declaration(generic);
                    if (record && record->vhdl != nullptr
                        && record->vhdl->form == Form::generic_type) {
                        corresponding_generic_types.push_back(
                            record->vhdl->name);
                    }
                }
                const auto callable_conforms
                    = specification_profile.second
                    && body_profile.second
                    && callables_conform(
                        *specification_profile.second,
                        *body_profile.second);
                if (generic_formals_conform(
                        specification_profile.first,
                        body_profile.first)
                    && callable_conforms) {
                    continue;
                }
                append_diagnostic(
                    "FSIM-ELAB-VHGSUB-013",
                    "generic subprogram declaration and body do not have "
                    "conforming generic lists and callable profiles",
                    body->vhdl->source);
                generic_callables_valid = false;
            }
        }
    }
    result.valid = generic_callables_valid;
    return result;
}

} // namespace fsim::elaboration
