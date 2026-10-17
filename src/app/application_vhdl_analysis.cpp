// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/support/vhdl_identifier.hpp"
#include "application_vhdl_hir_internal.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"


namespace fsim::app::application_detail {
namespace {

using frontend::DesignUnit;
namespace vh = semantic::vhdl;

using semantic::same_physical_source;

[[nodiscard]] bool same_preceding_subprogram_specification(
    const semantic::CompiledDesign& design,
    const vh::Name& name,
    const vh::Declaration& selected,
    const vh::Expression& use)
{
    if (!selected.callable
        || selected.callable->specification_lexemes.empty()) {
        return false;
    }
    const auto* const use_span = semantic::find_source_span(
        design.semantics, use.source);
    if (use_span == nullptr) {
        return false;
    }
    for (const auto candidate_id : name.overloads) {
        if (candidate_id == selected.id) {
            continue;
        }
        const auto candidate = design.find_declaration(candidate_id);
        if (!candidate || candidate->vhdl == nullptr) {
            continue;
        }
        const auto& declaration = *candidate->vhdl;
        if (declaration.scope != selected.scope
            || !declaration.callable || declaration.callable->defined
            || declaration.callable->function
                != selected.callable->function
            || declaration.callable->specification_lexemes
                != selected.callable->specification_lexemes
            || !same_physical_source(
                design.semantics, declaration.source, use.source)) {
            continue;
        }
        const auto* const declaration_span = semantic::find_source_span(
            design.semantics, declaration.source);
        if (declaration_span != nullptr
            && declaration_span->begin.offset < use_span->begin.offset) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool compiler_predefined_name(
    const vh::Unit& unit,
    const std::string_view canonical_name)
{
    const auto contains = [&](const std::vector<std::string>& names) {
        return std::ranges::any_of(names, [&](const std::string& name) {
            return support::canonical_vhdl_spelling(name) == canonical_name;
        });
    };
    return contains(unit.predefined_environment.declarations)
        || contains(unit.predefined_environment.time_units);
}

[[nodiscard]] bool subprogram_scope_declaration(
    const vh::Declaration& declaration)
{
    return declaration.callable.has_value()
        && declaration.nested_scope.has_value();
}

[[nodiscard]] bool vhdl_type_denotes_access(
    const semantic::CompiledDesign& design,
    semantic::TypeId type_id)
{
    std::vector<semantic::TypeId> visited;
    while (type_id.valid()
        && std::ranges::find(visited, type_id) == visited.end()) {
        visited.push_back(type_id);
        const auto type = design.find_type(type_id);
        if (!type || type->vhdl == nullptr) {
            return false;
        }
        const auto& definition = *type->vhdl;
        if (definition.form == vh::TypeForm::access) {
            return true;
        }
        if (definition.form != vh::TypeForm::subtype
            && definition.form != vh::TypeForm::alias) {
            return false;
        }
        type_id = definition.base.type_mark.target;
    }
    return false;
}

void validate_out_access_prefix_read(
    const semantic::CompiledDesign& design,
    const vh::Expression& expression,
    diagnostic::Engine& diagnostics)
{
    if (expression.kind != vh::ExpressionKind::call
        || expression.text != "@vhdl-dereference"
        || expression.operands.size() != 1U) {
        return;
    }
    const auto prefix = design.find_expression(expression.operands.front());
    if (!prefix || prefix->vhdl == nullptr
        || prefix->vhdl->kind != vh::ExpressionKind::name
        || !prefix->vhdl->referenced_name
        || !prefix->vhdl->referenced_name->selected) {
        return;
    }

    const auto formal_view = design.find_declaration(
        *prefix->vhdl->referenced_name->selected);
    if (!formal_view || formal_view->vhdl == nullptr) {
        return;
    }
    const auto& formal = *formal_view->vhdl;
    if (formal.scope != prefix->vhdl->scope
        || formal.form != vh::DeclarationForm::port
        || formal.direction != vh::Direction::output
        || !formal.subtype
        || !vhdl_type_denotes_access(
            design, formal.subtype->type_mark.target)) {
        return;
    }
    const auto& declarations = design.vhdl_hir.declarations();
    const bool procedure_formal = std::ranges::any_of(
        declarations, [&](const vh::Declaration& callable) {
            return callable.callable && !callable.callable->function
                && std::ranges::find(
                       callable.callable->formals, formal.id)
                    != callable.callable->formals.end();
        });
    if (!procedure_formal) {
        return;
    }

    const auto* unit = design.vhdl_scope_unit(expression.scope);
    if (unit == nullptr || unit->standard >= std::string_view { "2008" }) {
        return;
    }

    diagnostics.error(
        "FSIM-ELAB-VHACCESS-034",
        "an access-typed VHDL name prefix denotes a procedure formal "
        "of mode out before VHDL-2008 (IEEE 1076-1993 6.1)",
        compiled_source_span(design.semantics, expression.source));
}

void validate_vhdl_source_visibility_impl(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics)
{
    const auto& model = design.semantics;
    const auto& hir = design.vhdl_hir;
    const auto& declarations = hir.declarations();
    const auto& scopes = model.scopes();

    for (const auto& expression : hir.expressions()) {
        validate_out_access_prefix_read(design, expression, diagnostics);
        const auto* const use_span = semantic::find_source_span(
            model, expression.source);
        const auto* const use_file = use_span == nullptr
            ? nullptr
            : semantic::find_source_file(model, *use_span);
        if (!expression.referenced_name || !expression.scope.valid()) {
            continue;
        }
        const auto& name = *expression.referenced_name;
        if (use_span == nullptr || use_file == nullptr) {
            continue;
        }

        // HIR projection retains the exact selected local callable identity.
        // Check source order only when that same-scope declaration comes from
        // this physical source; package and compiler-owned declarations remain
        // governed by normal name resolution.
        if (expression.kind == vh::ExpressionKind::call && name.selected) {
            const auto selected = design.find_declaration(*name.selected);
            if (selected && selected->vhdl != nullptr
                && selected->vhdl->scope == expression.scope
                && selected->vhdl->callable
                && selected->vhdl->callable->function
                && same_physical_source(
                    model, selected->vhdl->source, expression.source)) {
                const auto* const declaration_span = semantic::find_source_span(
                    model, selected->vhdl->source);
                if (declaration_span != nullptr
                    && declaration_span->begin.offset
                        > use_span->begin.offset
                    && !same_preceding_subprogram_specification(
                        design, name, *selected->vhdl, expression)) {
                    diagnostics.error(
                        "FSIM-ELAB-VHNAME-001",
                        "VHDL subprogram '" + name.spelling
                            + "' is referenced before its declaration in the "
                              "same declarative region (IEEE 1076-2008 12.3)",
                        compiled_source_span(model, expression.source));
                    continue;
                }
            }
        }

        // A bare value name in a package-body subprogram can be unresolved
        // because a same-spelled object exists only in a sibling subprogram.
        // Use the resolver first, then require an actual sibling callable and
        // local constant identity in this package unit and source file.
        if (expression.kind != vh::ExpressionKind::name
            || name.selected || !name.overloads.empty()) {
            continue;
        }
        const bool extended_identifier
            = support::is_extended_vhdl_identifier(name.spelling);
        if (!extended_identifier
            && name.spelling.find('.') != std::string::npos) {
            continue;
        }
        if (expression.scope.value() >= scopes.size()) {
            continue;
        }
        const auto owner = design.find_unit(
            scopes[expression.scope.value()].unit);
        if (!owner || owner->identity == nullptr
            || owner->vhdl == nullptr
            || owner->vhdl->kind != vh::UnitKind::package) {
            continue;
        }
        semantic::CompiledDesignResolver resolver {
            design, owner->identity->id };
        const auto resolution = resolver.resolve_vhdl(
            name, expression.scope);
        if (resolution.status != semantic::CompiledResolutionStatus::not_found
            || resolver.vhdl_standard_package_member_visible(
                name, expression.scope)
            || compiler_predefined_name(*owner->vhdl,
                support::canonical_vhdl_spelling(name.spelling))) {
            continue;
        }

        const vh::Declaration* enclosing_callable { };
        auto visible_scope = expression.scope;
        for (std::size_t depth { };
             visible_scope.valid() && visible_scope.value() < scopes.size()
                 && depth <= scopes.size();
             ++depth) {
            const auto callable = std::ranges::find_if(
                declarations, [&](const vh::Declaration& declaration) {
                    return subprogram_scope_declaration(declaration)
                        && declaration.nested_scope == visible_scope;
                });
            if (callable != declarations.end()) {
                enclosing_callable = &*callable;
                break;
            }
            const auto& scope = scopes[visible_scope.value()];
            if (!scope.parent) {
                break;
            }
            visible_scope = *scope.parent;
        }
        if (enclosing_callable == nullptr
            || !enclosing_callable->callable->function
            || !same_physical_source(
                model, enclosing_callable->source, expression.source)) {
            continue;
        }

        const auto canonical_name
            = support::canonical_vhdl_spelling(name.spelling);
        std::string sibling_name;
        bool sibling_constant_exists { };
        for (const auto& sibling : declarations) {
            if (&sibling == enclosing_callable
                || !subprogram_scope_declaration(sibling)
                || sibling.scope != enclosing_callable->scope
                || sibling.nested_scope == enclosing_callable->nested_scope
                || !same_physical_source(
                    model, sibling.source, expression.source)) {
                continue;
            }
            const auto local_constant = std::ranges::find_if(
                declarations, [&](const vh::Declaration& candidate) {
                    return candidate.form == vh::DeclarationForm::constant
                        && candidate.scope == *sibling.nested_scope
                        && support::canonical_vhdl_spelling(candidate.name)
                            == canonical_name
                        && same_physical_source(
                            model, candidate.source, expression.source);
                });
            if (local_constant != declarations.end()) {
                sibling_name = sibling.name;
                sibling_constant_exists = true;
                break;
            }
        }
        if (sibling_constant_exists) {
            diagnostics.error(
                "FSIM-ELAB-VHNAME-002",
                "VHDL name '" + name.spelling
                    + "' is not visible here; the matching constant is "
                      "declared only in sibling subprogram '"
                    + sibling_name + "' (IEEE 1076-2008 12.3)",
                compiled_source_span(model, expression.source));
        }
    }
}

std::string library_of(const DesignUnit& unit) {
  return unit.library.empty() ? std::string{"work"} : unit.library;
}

std::string vhdl_identifier_key(
    const std::string_view name, const bool extended)
{
  return extended
      ? support::encode_vhdl_identifier(name, true)
      : support::canonical_vhdl_spelling(std::string { name });
}

std::optional<std::string> vhdl_metadata_identifier_key(
    const std::string_view name,
    const library::VhdlIdentifierForm form)
{
  switch (form) {
  case library::VhdlIdentifierForm::basic:
    return vhdl_identifier_key(name, false);
  case library::VhdlIdentifierForm::extended:
    return vhdl_identifier_key(name, true);
  case library::VhdlIdentifierForm::unknown:
    return std::nullopt;
  }
  return std::nullopt;
}

std::string context_lookup_name(const std::string_view spelling)
{
  return support::is_extended_vhdl_identifier(spelling)
      ? vhdl_identifier_key(support::decode_vhdl_identifier(spelling), true)
      : support::canonical_vhdl_spelling(std::string { spelling });
}

std::string selected_library(
    const std::string_view requested,
    const std::string_view owner) {
    const auto canonical
        = support::canonical_vhdl_spelling(std::string { requested });
    return canonical == "work" ? std::string { owner } : canonical;
}

std::string primary_key(
    const std::string_view library,
    const std::string_view name) {
  return std::string{library} + '\n' + std::string{name};
}

std::string architecture_key(
    const std::string_view library,
    const std::string_view entity,
    const std::string_view architecture) {
  return primary_key(library, entity) + '\n' + std::string{architecture};
}

class AnalysisOrderValidator {
 public:
  explicit AnalysisOrderValidator(
      diagnostic::Engine& diagnostics,
      const bool allow_external_architecture_primary,
      const semantic::CompiledDesign* imported,
      const std::span<const CompiledUnitAvailability> available_units)
      : diagnostics_(diagnostics),
        allow_external_architecture_primary_(
            allow_external_architecture_primary) {
    for (const auto& unit : available_units) {
      record_available(unit);
    }
    if (imported != nullptr) {
      for (const auto& unit : imported->vhdl_units()) {
        record_imported(unit);
      }
    }
  }

  void run(const std::span<const DesignUnit> units) {
    if (allow_external_architecture_primary_) {
      for (const auto& unit : units) {
        if (unit.language == frontend::Language::Vhdl2008
            && unit.kind == frontend::UnitKind::VhdlEntity) {
          local_entities_.insert(primary_key(
              library_of(unit),
              vhdl_identifier_key(unit.name, unit.vhdl_extended_name)));
        }
      }
    }
    for (const auto& unit : units) {
      if (unit.language == frontend::Language::Vhdl2008
          && unit.kind == frontend::UnitKind::VhdlArchitecture) {
        local_architectures_.insert_or_assign(
            architecture_key(
                library_of(unit),
                vhdl_identifier_key(
                    unit.primary_name, unit.vhdl_extended_primary_name),
                vhdl_identifier_key(unit.name, unit.vhdl_extended_name)),
            &unit);
      }
    }
    for (const auto& unit : units) {
      if (unit.language != frontend::Language::Vhdl2008) {
        continue;
      }
      validate_context(unit);
      validate_unit(unit);
      record(unit);
    }
  }

 private:
     using RevisionIndex = std::map<std::string, std::string>;

     void record_available(const CompiledUnitAvailability& available)
     {
         const auto& unit = available.unit;
         if (unit.language != "vhdl") {
             return;
         }
         const auto name = vhdl_metadata_identifier_key(
             unit.name, unit.name_form);
         if (!name) {
             // Old metadata omitted the form. It remains loadable, but cannot
             // satisfy an exact VHDL name lookup without risking a false match.
             return;
         }
         const auto library = available.library.empty()
             ? std::string_view { "work" }
             : std::string_view { available.library };
         const auto key = primary_key(library, *name);
         if (unit.kind == "entity") {
             entities_.insert_or_assign(key, unit.standard);
         } else if (unit.kind == "architecture") {
             const auto primary = vhdl_metadata_identifier_key(
                 unit.primary_name, unit.primary_name_form);
             if (!primary) {
                 return;
             }
             architectures_.insert_or_assign(
                 architecture_key(library, *primary, *name),
                 unit.standard);
         } else if (unit.kind == "package" && unit.primary_name.empty()) {
             packages_.insert_or_assign(key, unit.standard);
         } else if (unit.kind == "context") {
             contexts_.insert_or_assign(key, unit.standard);
         } else if (unit.kind == "configuration") {
             configurations_.insert_or_assign(key, unit.standard);
         }
     }

     void record_imported(const semantic::vhdl::Unit& unit)
     {
         const auto library = unit.library.empty()
             ? std::string_view { "work" }
             : std::string_view { unit.library };
         const auto key = primary_key(
             library, vhdl_identifier_key(unit.name, unit.extended_name));
         switch (unit.kind) {
         case semantic::vhdl::UnitKind::entity:
             entities_.insert_or_assign(key, unit.standard);
             break;
         case semantic::vhdl::UnitKind::architecture:
             architectures_.insert_or_assign(
                 architecture_key(
                     library,
                     vhdl_identifier_key(
                         unit.primary_name, unit.extended_primary_name),
                     vhdl_identifier_key(unit.name, unit.extended_name)),
                 unit.standard);
             break;
         case semantic::vhdl::UnitKind::package:
             if (unit.primary_name.empty()) {
                 packages_.insert_or_assign(key, unit.standard);
             }
             break;
         case semantic::vhdl::UnitKind::context:
             contexts_.insert_or_assign(
                 primary_key(library, support::encode_vhdl_identifier(
                     unit.name, unit.extended_name)),
                 unit.standard);
             break;
         case semantic::vhdl::UnitKind::configuration:
             configurations_.insert_or_assign(key, unit.standard);
             break;
         case semantic::vhdl::UnitKind::psl_verification_unit:
             break;
         }
     }

     void report(
         const std::string_view code,
         std::string message,
         const frontend::SourceSpan& source)
     {
         diagnostics_.error(std::string { code }, std::move(message), span(source));
     }

     [[nodiscard]] bool validate_dependency(
         const RevisionIndex& index,
         const std::string& key,
         const std::string_view kind,
         const DesignUnit& owner,
         const frontend::SourceSpan& source)
     {
         const auto found = index.find(key);
         if (found == index.end()) {
             return false;
         }
         if (found->second != frontend::to_string(owner.vhdl_standard)) {
             report(
                 "FSIM-FE-VHORDER-011",
                 "VHDL " + std::string { kind } + " '" + key
                     + "' was analyzed as "
                     + found->second
                     + " but the owning source uses "
                     + std::string { frontend::to_string(owner.vhdl_standard) },
                 source);
         }
         return true;
     }

  void validate_context(const DesignUnit& unit) {
    const auto owner_library = library_of(unit);
    for (const auto& item : unit.vhdl_context) {
      for (const auto& selected_name : item.selected_names) {
        const auto parts = support::split_vhdl_selected_name(selected_name);
        if (item.kind == frontend::VhdlContextItemKind::UseClause
            && parts.size() == 3) {
          if (parts[0] == "ieee" || parts[0] == "std") {
            continue;
          }
          const auto library = selected_library(parts[0], owner_library);
          if (!validate_dependency(
                  packages_, primary_key(
                      library, context_lookup_name(parts[1])), "package",
                  unit, item.span)) {
              report(
                  "FSIM-FE-VHORDER-004",
                  "VHDL package '" + library + "."
                      + std::string { parts[1] }
                      + "' must be analyzed before its use clause",
                  item.span);
          }
        } else if (item.kind == frontend::VhdlContextItemKind::UseClause
            && parts.size() == 2 && parts[1] != "all"
            && parts[0] != "ieee" && parts[0] != "std") {
          // `use library.unit;` names a primary unit (IEEE 1076-2008
          // 12.4); an architecture or package body is a secondary unit.
          const auto library = selected_library(parts[0], owner_library);
          const auto key = primary_key(
              library, context_lookup_name(parts[1]));
          if (!packages_.contains(key) && !entities_.contains(key)
              && !configurations_.contains(key)
              && !contexts_.contains(primary_key(
                  library, context_lookup_name(parts[1])))) {
              report(
                  "FSIM-FE-VHORDER-004",
                  "VHDL primary unit '" + library + "."
                      + std::string { parts[1] }
                      + "' must be analyzed before its use clause",
                  item.span);
          }
        } else if (
            item.kind
                == frontend::VhdlContextItemKind::ContextReference
            && parts.size() == 2) {
          if (parts[0] == "ieee" || parts[0] == "std") {
            continue;
          }
          const auto library = selected_library(parts[0], owner_library);
          if (!validate_dependency(
                  contexts_, primary_key(
                      library, context_lookup_name(parts[1])), "context",
                  unit, item.span)) {
              report(
                  "FSIM-FE-VHORDER-003",
                  "VHDL context '" + library + "."
                      + std::string { parts[1] }
                      + "' must be analyzed before it is referenced",
                  item.span);
          }
        }
      }
    }
  }

  void validate_binding(
      const frontend::VhdlBindingIndication& binding,
      const DesignUnit& owner)
  {
      const auto owner_library = library_of(owner);
      if (binding.kind == frontend::VhdlBindingAspectKind::Open) {
          return;
      }
      if (binding.kind == frontend::VhdlBindingAspectKind::Configuration) {
          const auto& configuration_spelling
              = binding.configuration_reference_spelling.empty()
              ? binding.configuration_name
              : binding.configuration_reference_spelling;
          const auto parts
              = support::split_vhdl_selected_name(configuration_spelling);
          if (parts.empty() || parts.size() > 2) {
              return;
          }
          const auto library = parts.size() == 2
              ? selected_library(parts[0], owner_library)
              : std::string { owner_library };
          const auto configuration = binding.configuration_reference_spelling.empty()
              ? vhdl_identifier_key(
                    parts.back(), binding.configuration_name_extended)
              : context_lookup_name(parts.back());
          if (!validate_dependency(
                  configurations_, primary_key(library, configuration),
                  "configuration", owner, binding.span)) {
              report(
                  "FSIM-FE-VHORDER-008",
                  "VHDL configuration '" + library + "."
                      + std::string { parts.back() }
                      + "' must be analyzed before it is used in a binding",
                  binding.span);
          }
          return;
      }

      const auto& entity_spelling = binding.entity_reference_spelling.empty()
          ? binding.entity_name : binding.entity_reference_spelling;
      const auto parts = support::split_vhdl_selected_name(entity_spelling);
      if (parts.empty() || parts.size() > 2
          || binding.architecture_name.empty()) {
          return;
      }
      const auto library = parts.size() == 2
          ? selected_library(parts[0], owner_library)
          : std::string { owner_library };
      const auto entity = binding.entity_reference_spelling.empty()
          ? vhdl_identifier_key(
                parts.back(), binding.entity_name_extended)
          : context_lookup_name(parts.back());
      const auto architecture = binding.architecture_name_spelling.empty()
          ? vhdl_identifier_key(
                binding.architecture_name,
                binding.architecture_name_extended)
          : context_lookup_name(binding.architecture_name_spelling);
      if (!validate_dependency(
              entities_, primary_key(library, entity), "entity", owner,
              binding.span)) {
          report(
              "FSIM-FE-VHORDER-007",
              "VHDL entity '" + library + "." + std::string { entity }
                  + "' must be analyzed before it is used in a binding",
              binding.span);
      }
      if (!validate_dependency(
              architectures_, architecture_key(library, entity, architecture),
              "architecture", owner, binding.span)) {
          report(
              "FSIM-FE-VHORDER-006",
              "VHDL architecture '" + library + "."
                  + std::string { entity } + "("
                  + binding.architecture_name
                  + ")' must be analyzed before it is used in a binding",
              binding.span);
      }
  }

  // IEEE 1076-2008 3.4.2: a block configuration inside another names a
  // block or generate statement of the enclosing block.
  void validate_block_labels(
      const frontend::VhdlBlockConfiguration& block,
      const std::span<const frontend::GenerateRegion> regions)
  {
      for (const auto& child : block.block_configurations) {
          const auto region = std::ranges::find_if(regions,
              [&](const frontend::GenerateRegion& candidate) {
                  return std::ranges::equal(candidate.then_scope, child.block_name,
                      [](const unsigned char left, const unsigned char right) {
                          return std::tolower(left) == std::tolower(right);
                      });
              });
          if (region == regions.end()) {
              report("FSIM-FE-VHCONFIG-001",
                  "block configuration '" + child.block_name
                      + "' does not name a block or generate statement of the "
                        "enclosing block",
                  child.span);
              continue;
          }
          validate_block_labels(child, region->then_body.generate_regions);
      }
  }

  void validate_block(
      const frontend::VhdlBlockConfiguration& block,
      const DesignUnit& owner)
  {
      for (const auto& component : block.component_configurations) {
          validate_binding(component.binding, owner);
          if (component.block_configuration) {
              const auto& nested = *component.block_configuration;
              if (component.binding.kind
                      == frontend::VhdlBindingAspectKind::Entity
                  && !component.binding.architecture_name.empty()
                  && (nested.block_name
                          != component.binding.architecture_name
                      || nested.block_name_extended
                          != component.binding.architecture_name_extended)) {
                  report(
                      "FSIM-FE-VHCONFIG-002",
                      "nested block configuration '" + nested.block_name
                          + "' must name the architecture '"
                          + component.binding.architecture_name
                          + "' selected by its component binding "
                            "(IEEE 1076-1993 1.3.1)",
                      nested.span);
              }
              validate_block(nested, owner);
          }
      }
      for (const auto& child : block.block_configurations) {
          validate_block(child, owner);
      }
  }

  void validate_instances(
      const std::span<const frontend::Instance> instances,
      const DesignUnit& owner)
  {
      const auto owner_library = library_of(owner);
      for (const auto& instance : instances) {
          if (!instance.vhdl_configuration_instance) {
              continue;
          }
          const auto& instance_spelling = instance.vhdl_unit_name_spelling.empty()
              ? instance.unit_name : instance.vhdl_unit_name_spelling;
          const auto parts
              = support::split_vhdl_selected_name(instance_spelling);
          if (parts.empty() || parts.size() > 2) {
              continue;
          }
          const auto library = parts.size() == 2
              ? selected_library(parts[0], owner_library)
              : std::string { owner_library };
          if (!validate_dependency(
                  configurations_, primary_key(
                      library, instance.vhdl_unit_name_spelling.empty()
                          ? vhdl_identifier_key(
                                parts.back(), instance.vhdl_extended_unit_name)
                          : context_lookup_name(parts.back())),
                  "configuration", owner, instance.span)) {
              report(
                  "FSIM-FE-VHORDER-008",
                  "VHDL configuration '" + library + "."
                      + std::string { parts.back() }
                      + "' must be analyzed before its direct instantiation",
                  instance.span);
          }
      }
  }

  void validate_generate_body(
      const frontend::GenerateBody& body,
      const DesignUnit& owner)
  {
      validate_instances(body.instances, owner);
      for (const auto& region : body.generate_regions) {
          validate_generate_region(region, owner);
      }
  }

  void validate_generate_region(
      const frontend::GenerateRegion& region,
      const DesignUnit& owner)
  {
      validate_generate_body(region.then_body, owner);
      validate_generate_body(region.else_body, owner);
      for (const auto& alternative : region.alternatives) {
          validate_generate_body(alternative.body, owner);
      }
  }

  void validate_unit(const DesignUnit& unit) {
    const auto library = library_of(unit);
    switch (unit.kind) {
      case frontend::UnitKind::VhdlArchitecture:
        {
          const auto key = primary_key(
              library, vhdl_identifier_key(
                  unit.primary_name, unit.vhdl_extended_primary_name));
          if (!validate_dependency(entities_, key, "entity", unit, unit.span)
              && (!allow_external_architecture_primary_
                  || local_entities_.contains(key))) {
              report(
                  "FSIM-FE-VHORDER-001",
                  "VHDL entity '" + library + "." + unit.primary_name
                      + "' must be analyzed before architecture '"
                      + unit.name + "'",
                  unit.span);
          }
        }
        break;
      case frontend::UnitKind::VhdlPackage:
          if (!unit.primary_name.empty()
              && !validate_dependency(
                  packages_, primary_key(
                      library, vhdl_identifier_key(
                          unit.name, unit.vhdl_extended_name)), "package",
                  unit, unit.span)) {
              report(
                  "FSIM-FE-VHORDER-002",
                  "VHDL package '" + library + "." + unit.name
                      + "' must be analyzed before its body",
                  unit.span);
          }
        break;
      case frontend::UnitKind::VhdlConfiguration:
          if (!validate_dependency(
                entities_, primary_key(
                    library, vhdl_identifier_key(
                        unit.primary_name,
                        unit.vhdl_extended_primary_name)),
                  "entity", unit, unit.span)) {
              report(
                  "FSIM-FE-VHORDER-005",
                  "VHDL entity '" + library + "." + unit.primary_name
                      + "' must be analyzed before configuration '"
                      + unit.name + "'",
                  unit.span);
          }
        if (unit.vhdl_configuration) {
          const auto& block = unit.vhdl_configuration->block;
          if (!validate_dependency(
                  architectures_, architecture_key(
                      library,
                      vhdl_identifier_key(
                          unit.primary_name,
                          unit.vhdl_extended_primary_name),
                      vhdl_identifier_key(
                          block.block_name, block.block_name_extended)),
                  "architecture", unit, block.span)) {
              report(
                  "FSIM-FE-VHORDER-006",
                  "VHDL architecture '" + library + "."
                      + unit.primary_name + "(" + block.block_name
                      + ")' must be analyzed before configuration '"
                      + unit.name + "'",
                  block.span);
          }
          validate_block(block, unit);
          if (const auto architecture = local_architectures_.find(
                  architecture_key(
                      library,
                      vhdl_identifier_key(
                          unit.primary_name,
                          unit.vhdl_extended_primary_name),
                      vhdl_identifier_key(
                          block.block_name, block.block_name_extended)));
              architecture != local_architectures_.end()) {
            validate_block_labels(block, architecture->second->generate_regions);
          }
        }
        break;
      default:
        break;
    }
    for (const auto& specification :
         unit.vhdl_configuration_specifications) {
        validate_binding(specification.binding, unit);
    }
    validate_instances(unit.instances, unit);
    for (const auto& region : unit.generate_regions) {
        validate_generate_region(region, unit);
    }
  }

  void record(const DesignUnit& unit) {
    const auto library = library_of(unit);
    const auto standard = std::string {
        frontend::to_string(unit.vhdl_standard) };
    const auto record_primary =
        [&] {
          const auto key = primary_key(
              library, vhdl_identifier_key(
                  unit.name, unit.vhdl_extended_name));
          if (!primary_units_.insert(key).second) {
            report_vhdl_duplicate_design_unit(unit, diagnostics_);
          }
        };
    const auto record_secondary =
        [&](std::string key) {
          if (!secondary_units_.insert(std::move(key)).second) {
            report_vhdl_duplicate_design_unit(unit, diagnostics_);
          }
        };
    switch (unit.kind) {
      case frontend::UnitKind::VhdlEntity:
        record_primary();
        entities_.insert_or_assign(primary_key(
            library, vhdl_identifier_key(
                unit.name, unit.vhdl_extended_name)), standard);
        break;
      case frontend::UnitKind::VhdlArchitecture:
        record_secondary(
            architecture_key(
                library,
                vhdl_identifier_key(
                    unit.primary_name, unit.vhdl_extended_primary_name),
                vhdl_identifier_key(unit.name, unit.vhdl_extended_name)));
        architectures_.insert_or_assign(architecture_key(
                                            library,
                                            vhdl_identifier_key(
                                                unit.primary_name,
                                                unit.vhdl_extended_primary_name),
                                            vhdl_identifier_key(
                                                unit.name,
                                                unit.vhdl_extended_name)),
            standard);
        break;
      case frontend::UnitKind::VhdlConfiguration:
        record_primary();
        configurations_.insert_or_assign(primary_key(
            library, vhdl_identifier_key(
                unit.name, unit.vhdl_extended_name)), standard);
        break;
      case frontend::UnitKind::VhdlPackage:
        if (unit.primary_name.empty()) {
          record_primary();
          packages_.insert_or_assign(primary_key(
              library, vhdl_identifier_key(
                  unit.name, unit.vhdl_extended_name)), standard);
        } else {
          record_secondary(
              primary_key(library, vhdl_identifier_key(
                  unit.name, unit.vhdl_extended_name)) + "\npackage-body");
        }
        break;
      case frontend::UnitKind::VhdlContext:
        if (!primary_units_.insert(primary_key(
                library,
                vhdl_identifier_key(unit.name,
                    unit.vhdl_extended_name))).second) {
          report_vhdl_duplicate_design_unit(unit, diagnostics_);
        }
        contexts_.insert_or_assign(
            primary_key(library, vhdl_identifier_key(
                unit.name, unit.vhdl_extended_name)),
            standard);
        break;
      case frontend::UnitKind::VhdlPslVerificationUnit:
        record_primary();
        break;
      default:
        break;
    }
  }

  diagnostic::Engine& diagnostics_;
  RevisionIndex entities_;
  RevisionIndex architectures_;
  RevisionIndex packages_;
  RevisionIndex contexts_;
  RevisionIndex configurations_;
  std::set<std::string> primary_units_;
  std::set<std::string> secondary_units_;
  std::set<std::string> local_entities_;
  // Architectures of this design file, for configuration block checks.
  std::map<std::string, const DesignUnit*> local_architectures_;
  bool allow_external_architecture_primary_ { };
};

}  // namespace

void report_vhdl_duplicate_design_unit(
    const frontend::DesignUnit& unit,
    diagnostic::Engine& diagnostics) {
  const auto library = library_of(unit);
  std::string_view kind;
  std::string_view code;
  switch (unit.kind) {
    case frontend::UnitKind::VhdlEntity:
      kind = "entity";
      code = "FSIM-FE-VHORDER-009";
      break;
    case frontend::UnitKind::VhdlArchitecture:
      kind = "architecture";
      code = "FSIM-FE-VHORDER-010";
      break;
    case frontend::UnitKind::VhdlConfiguration:
      kind = "configuration";
      code = "FSIM-FE-VHORDER-009";
      break;
    case frontend::UnitKind::VhdlPackage:
      kind = unit.primary_name.empty() ? "package" : "package-body";
      code = unit.primary_name.empty()
          ? "FSIM-FE-VHORDER-009" : "FSIM-FE-VHORDER-010";
      break;
    case frontend::UnitKind::VhdlContext:
      kind = "context";
      code = "FSIM-FE-VHORDER-009";
      break;
    case frontend::UnitKind::VhdlPslVerificationUnit:
      kind = "PSL verification";
      code = "FSIM-FE-VHORDER-009";
      break;
    default:
      return;
  }
  const auto identity = library + "." + unit.name;
  diagnostics.error(
      std::string{code},
      "duplicate VHDL "
          + std::string{code.ends_with("009") ? "primary " : "secondary "}
          + std::string{kind} + " unit '" + identity + "'",
      span(unit.span));
}

void validate_vhdl_analysis_order(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics,
    const bool allow_external_architecture_primary,
    const semantic::CompiledDesign* imported,
    const std::span<const CompiledUnitAvailability> available_units) {
  AnalysisOrderValidator{
      diagnostics, allow_external_architecture_primary, imported,
      available_units}.run(units);
}

void validate_vhdl_source_visibility(
    const semantic::CompiledDesign& design,
    diagnostic::Engine& diagnostics)
{
    validate_vhdl_source_visibility_impl(design, diagnostics);
}

void validate_vhdl_simulator_api(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics) {
  const auto validate_expression = [&](const auto& self,
                                       const frontend::Expression& expression)
      -> void {
    const auto api = frontend::vhdl_simulator_api(expression.text);
    if (api == frontend::VhdlSimulatorApi::stop
        || api == frontend::VhdlSimulatorApi::finish
        || api == frontend::VhdlSimulatorApi::set_psl_cover_assert
        || api == frontend::VhdlSimulatorApi::clear_psl_state
        || api == frontend::VhdlSimulatorApi::clear_vhdl_assert
        || api == frontend::VhdlSimulatorApi::set_vhdl_assert_enable
        || api == frontend::VhdlSimulatorApi::set_vhdl_assert_format
        || api == frontend::VhdlSimulatorApi::set_vhdl_read_severity) {
      diagnostics.error(
          "FSIM-FE-VHENV-001",
          "this STD.ENV declaration is a procedure and cannot be used as a value",
          span(expression.span));
    } else if (api == frontend::VhdlSimulatorApi::resolution_limit
               && expression.kind == frontend::ExpressionKind::Call
               && !expression.operands.empty()) {
      diagnostics.error(
          "FSIM-FE-VHENV-002",
          "STD.ENV RESOLUTION_LIMIT does not accept arguments",
          span(expression.span));
    } else if (api == frontend::VhdlSimulatorApi::dayofweek
               || api == frontend::VhdlSimulatorApi::time_record
               || api == frontend::VhdlSimulatorApi::directory_items
               || api == frontend::VhdlSimulatorApi::directory
               || api == frontend::VhdlSimulatorApi::call_path_element
               || api == frontend::VhdlSimulatorApi::call_path_vector
               || api == frontend::VhdlSimulatorApi::call_path_vector_ptr
               || api == frontend::VhdlSimulatorApi::dir_open_status
               || api == frontend::VhdlSimulatorApi::dir_create_status
               || api == frontend::VhdlSimulatorApi::dir_delete_status
               || api == frontend::VhdlSimulatorApi::file_delete_status) {
      diagnostics.error(
          "FSIM-FE-VHENV-001",
          "STD.ENV DAYOFWEEK and TIME_RECORD are types and cannot be used as values",
          span(expression.span));
    } else if (api != frontend::VhdlSimulatorApi::none) {
      const auto names_valid = [&](const std::span<const std::string_view> names) {
        if (expression.call_argument_names.empty()) {
          return true;
        }
        return expression.call_argument_names.size()
                == expression.operands.size()
            && std::ranges::all_of(
                expression.call_argument_names,
                [&](const std::string& name) {
                  return name.empty()
                      || std::ranges::find(names, name) != names.end();
                });
      };
      bool valid = true;
      switch (api) {
        case frontend::VhdlSimulatorApi::localtime:
        case frontend::VhdlSimulatorApi::gmtime: {
          static constexpr std::array names{
              std::string_view{"timer"}, std::string_view{"trec"}};
          valid = expression.operands.size() <= 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::epoch: {
          static constexpr std::array names{std::string_view{"trec"}};
          valid = expression.operands.size() <= 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::time_to_seconds: {
          static constexpr std::array names{std::string_view{"time_val"}};
          valid = expression.operands.size() == 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::seconds_to_time: {
          static constexpr std::array names{std::string_view{"real_val"}};
          valid = expression.operands.size() == 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::to_string: {
          static constexpr std::array names{
              std::string_view{"trec"},
              std::string_view{"frac_digits"},
              std::string_view{"call_path"},
              std::string_view{"separator"}};
          valid = !expression.operands.empty()
              && expression.operands.size() <= 2U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::getenv: {
          static constexpr std::array names{std::string_view{"name"}};
          valid = expression.operands.size() == 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::vhdl_version:
        case frontend::VhdlSimulatorApi::tool_type:
        case frontend::VhdlSimulatorApi::tool_vendor:
        case frontend::VhdlSimulatorApi::tool_name:
        case frontend::VhdlSimulatorApi::tool_edition:
        case frontend::VhdlSimulatorApi::tool_version:
        case frontend::VhdlSimulatorApi::get_call_path:
        case frontend::VhdlSimulatorApi::file_name:
        case frontend::VhdlSimulatorApi::file_path:
        case frontend::VhdlSimulatorApi::file_line:
        case frontend::VhdlSimulatorApi::psl_assert_failed:
        case frontend::VhdlSimulatorApi::psl_is_covered:
        case frontend::VhdlSimulatorApi::get_psl_cover_assert:
        case frontend::VhdlSimulatorApi::psl_is_assert_covered:
        case frontend::VhdlSimulatorApi::get_vhdl_read_severity:
          valid = expression.operands.empty();
          break;
        case frontend::VhdlSimulatorApi::is_vhdl_assert_failed:
        case frontend::VhdlSimulatorApi::get_vhdl_assert_count:
        case frontend::VhdlSimulatorApi::get_vhdl_assert_enable: {
          static constexpr std::array names{std::string_view{"level"}};
          valid = expression.operands.size() <= 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::get_vhdl_assert_format: {
          static constexpr std::array names{std::string_view{"level"}};
          valid = expression.operands.size() == 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_open: {
          static constexpr std::array names{
              std::string_view{"dir"}, std::string_view{"path"}};
          valid = expression.operands.size() == 2U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_itemexists:
        case frontend::VhdlSimulatorApi::dir_itemisdir:
        case frontend::VhdlSimulatorApi::dir_itemisfile: {
          static constexpr std::array names{std::string_view{"path"}};
          valid = expression.operands.size() == 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_workingdir: {
          static constexpr std::array names{std::string_view{"path"}};
          valid = expression.operands.size() <= 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_createdir: {
          static constexpr std::array names{
              std::string_view{"path"}, std::string_view{"parents"}};
          valid = !expression.operands.empty()
              && expression.operands.size() <= 2U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_deletedir: {
          static constexpr std::array names{
              std::string_view{"path"}, std::string_view{"recursive"}};
          valid = !expression.operands.empty()
              && expression.operands.size() <= 2U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_deletefile: {
          static constexpr std::array names{std::string_view{"path"}};
          valid = expression.operands.size() == 1U && names_valid(names);
          break;
        }
        case frontend::VhdlSimulatorApi::dir_separator:
          valid = expression.operands.empty();
          break;
        case frontend::VhdlSimulatorApi::dir_close:
        case frontend::VhdlSimulatorApi::set_psl_cover_assert:
        case frontend::VhdlSimulatorApi::clear_psl_state:
        case frontend::VhdlSimulatorApi::clear_vhdl_assert:
        case frontend::VhdlSimulatorApi::set_vhdl_assert_enable:
        case frontend::VhdlSimulatorApi::set_vhdl_assert_format:
        case frontend::VhdlSimulatorApi::set_vhdl_read_severity:
          valid = false;
          break;
        case frontend::VhdlSimulatorApi::none:
        case frontend::VhdlSimulatorApi::stop:
        case frontend::VhdlSimulatorApi::finish:
        case frontend::VhdlSimulatorApi::resolution_limit:
        case frontend::VhdlSimulatorApi::dayofweek:
        case frontend::VhdlSimulatorApi::time_record:
        case frontend::VhdlSimulatorApi::directory_items:
        case frontend::VhdlSimulatorApi::directory:
        case frontend::VhdlSimulatorApi::call_path_element:
        case frontend::VhdlSimulatorApi::call_path_vector:
        case frontend::VhdlSimulatorApi::call_path_vector_ptr:
        case frontend::VhdlSimulatorApi::dir_open_status:
        case frontend::VhdlSimulatorApi::dir_create_status:
        case frontend::VhdlSimulatorApi::dir_delete_status:
        case frontend::VhdlSimulatorApi::file_delete_status:
          break;
      }
      if (!valid) {
          diagnostics.error(
              "FSIM-FE-VHENV-002",
              "STD.ENV call has no matching standardized association profile",
              span(expression.span));
      }
    }
    for (const auto& operand : expression.operands) {
      self(self, operand);
    }
  };
  const auto validate_statements = [
      &](const auto& self,
          const std::span<const frontend::Statement> statements) -> void {
    for (const auto& statement : statements) {
      if (statement.kind == frontend::StatementKind::ProcedureCall) {
        const auto api = frontend::vhdl_simulator_api(
            statement.procedure_name);
        if (api == frontend::VhdlSimulatorApi::resolution_limit) {
          diagnostics.error(
              "FSIM-FE-VHENV-001",
              "STD.ENV RESOLUTION_LIMIT is a function and cannot be called as a procedure",
              span(statement.span));
        } else if (api == frontend::VhdlSimulatorApi::stop
                   || api == frontend::VhdlSimulatorApi::finish) {
          const bool invalid_actuals =
              statement.procedure_arguments.size() > 1U
              || std::ranges::any_of(
                  statement.procedure_arguments,
                  [](const auto& association) {
                    return association.formal
                        && *association.formal != "status";
                  });
          if (invalid_actuals) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV STOP and FINISH accept at most one INTEGER STATUS actual",
              span(statement.span));
          }
        } else if (api
                       == frontend::VhdlSimulatorApi::set_psl_cover_assert) {
          const bool invalid_actuals
              = statement.procedure_arguments.size() > 1U
              || std::ranges::any_of(
                  statement.procedure_arguments,
                  [](const auto& association) {
                    return association.formal
                        && *association.formal != "enable";
                  });
          if (invalid_actuals) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV SETPSLCOVERASSERT accepts at most one BOOLEAN ENABLE actual",
                span(statement.span));
          }
        } else if (api == frontend::VhdlSimulatorApi::clear_psl_state) {
          if (!statement.procedure_arguments.empty()) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV CLEARPSLSTATE does not accept arguments",
              span(statement.span));
          }
        } else if (api == frontend::VhdlSimulatorApi::clear_vhdl_assert) {
          if (!statement.procedure_arguments.empty()) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV CLEARVHDLASSERT does not accept arguments",
                span(statement.span));
          }
        } else if (api
                       == frontend::VhdlSimulatorApi::set_vhdl_assert_enable) {
          const bool invalid_actuals
              = statement.procedure_arguments.size() > 2U
              || std::ranges::any_of(
                  statement.procedure_arguments,
                  [](const auto& association) {
                    return association.formal
                        && *association.formal != "level"
                        && *association.formal != "enable";
                  });
          if (invalid_actuals) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV SETVHDLASSERTENABLE has no matching standardized association profile",
                span(statement.span));
          }
        } else if (api
                       == frontend::VhdlSimulatorApi::set_vhdl_assert_format) {
          const auto count = statement.procedure_arguments.size();
          const bool invalid_actuals
              = (count != 2U && count != 3U)
              || std::ranges::any_of(
                  statement.procedure_arguments,
                  [](const auto& association) {
                    return association.formal
                        && *association.formal != "level"
                        && *association.formal != "format"
                        && *association.formal != "valid";
                  });
          if (invalid_actuals) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV SETVHDLASSERTFORMAT has no matching standardized association profile",
                span(statement.span));
          }
        } else if (api
                       == frontend::VhdlSimulatorApi::set_vhdl_read_severity) {
          const bool invalid_actuals
              = statement.procedure_arguments.size() > 1U
              || std::ranges::any_of(
                  statement.procedure_arguments,
                  [](const auto& association) {
                    return association.formal
                        && *association.formal != "level";
                  });
          if (invalid_actuals) {
            diagnostics.error(
                "FSIM-FE-VHENV-002",
                "STD.ENV SETVHDLREADSEVERITY accepts at most one SEVERITY_LEVEL actual",
                span(statement.span));
          }
        } else if (api == frontend::VhdlSimulatorApi::dir_open
                   || api == frontend::VhdlSimulatorApi::dir_close
                   || api == frontend::VhdlSimulatorApi::dir_workingdir
                   || api == frontend::VhdlSimulatorApi::dir_createdir
                   || api == frontend::VhdlSimulatorApi::dir_deletedir
                   || api == frontend::VhdlSimulatorApi::dir_deletefile) {
          const auto count = statement.procedure_arguments.size();
          const bool valid_count = api == frontend::VhdlSimulatorApi::dir_open
              ? count == 3U
              : api == frontend::VhdlSimulatorApi::dir_close
              ? count == 1U
              : (api == frontend::VhdlSimulatorApi::dir_createdir
                    || api == frontend::VhdlSimulatorApi::dir_deletedir)
              ? count == 2U || count == 3U
              : count == 2U;
          if (!valid_count) {
            diagnostics.error(
                "FSIM-FE-VHENV-003",
                "STD.ENV directory procedure has no matching standardized association profile",
                span(statement.span));
          }
        } else if (api != frontend::VhdlSimulatorApi::none) {
          diagnostics.error(
              "FSIM-FE-VHENV-001",
              "STD.ENV functions, constants, and types cannot be called as procedures",
              span(statement.span));
        }
      }
      validate_expression(validate_expression, statement.target);
      validate_expression(validate_expression, statement.value);
      validate_expression(validate_expression, statement.condition);
      validate_expression(validate_expression, statement.vhdl_guard);
      validate_expression(validate_expression, statement.loop_initial);
      validate_expression(validate_expression, statement.loop_limit);
      for (const auto& association : statement.procedure_arguments) {
        validate_expression(validate_expression, association.value);
      }
      for (const auto& alternative : statement.case_alternatives) {
        for (const auto& choice : alternative.choices) {
          validate_expression(validate_expression, choice);
        }
        self(self, alternative.statements);
      }
      self(self, statement.statements);
      self(self, statement.else_statements);
      self(self, statement.loop_updates);
    }
  };
  for (const auto& unit : units) {
    if (unit.language != frontend::Language::Vhdl2008) {
      continue;
    }
    for (const auto& process : unit.processes) {
      validate_statements(validate_statements, process.statements);
    }
    for (const auto& function : unit.functions) {
      validate_statements(validate_statements, function.statements);
    }
    for (const auto& procedure : unit.procedures) {
      validate_statements(validate_statements, procedure.statements);
    }
  }
}

void validate_vhdl_mode_view_interfaces(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics) {
  const auto is_view = [](const auto& declaration) {
    return declaration.declaration_kind
        == frontend::TypeDeclarationKind::VhdlModeView;
  };
  const auto package = [&](const std::string_view library,
                           const std::string_view name) {
    return std::ranges::find_if(
        units,
        [&](const frontend::DesignUnit& candidate) {
          return candidate.kind == frontend::UnitKind::VhdlPackage
              && candidate.primary_name.empty()
              && library_of(candidate) == library
              && candidate.name == name;
        });
  };
  const auto visible_count = [&](const frontend::DesignUnit& unit,
                                 const std::string_view view_name)
      -> std::size_t {
    const auto parts = support::split_vhdl_selected_name(view_name);
    const auto owner_library = library_of(unit);
    const auto package_has_view = [&](const std::string_view library,
                                      const std::string_view package_name,
                                      const std::string_view name) {
      const auto found = package(
          selected_library(library, owner_library), package_name);
      return found != units.end()
          && std::ranges::any_of(
              found->type_aliases,
              [&](const auto& declaration) {
                return is_view(declaration) && declaration.name == name;
              });
    };
    if (parts.size() == 2) {
      return package_has_view(owner_library, parts[0], parts[1]) ? 1U : 0U;
    }
    if (parts.size() == 3) {
      return package_has_view(parts[0], parts[1], parts[2]) ? 1U : 0U;
    }
    if (parts.size() != 1) {
      return 0U;
    }
    std::size_t count = static_cast<std::size_t>(
        std::ranges::count_if(
            unit.type_aliases,
            [&](const auto& declaration) {
              return is_view(declaration)
                  && declaration.name == parts.front();
            }));
    for (const auto& item : unit.vhdl_context) {
      if (item.kind != frontend::VhdlContextItemKind::UseClause) {
        continue;
      }
      for (const auto& selected : item.selected_names) {
        const auto imported = support::split_vhdl_selected_name(selected);
        if (imported.size() == 3
            && (imported[2] == "all" || imported[2] == parts.front())
            && package_has_view(imported[0], imported[1], parts.front())) {
          ++count;
        }
      }
    }
    return count;
  };
  const auto validate = [&](const frontend::DesignUnit& unit,
                            const auto& port) {
    if (!port.vhdl_mode_view) {
      return;
    }
    const auto count = visible_count(unit, port.vhdl_mode_view->view);
    if (count == 1U) {
      return;
    }
    diagnostics.error(
        "FSIM-VHDL-SEM-109",
        count == 0U
            ? "mode view '" + port.vhdl_mode_view->view
                + "' is not visible for interface '" + port.name + "'"
            : "mode view '" + port.vhdl_mode_view->view
                + "' is visible from multiple packages for interface '"
                + port.name + "'",
        span(port.vhdl_mode_view->span));
  };
  for (const auto& unit : units) {
    if (unit.language != frontend::Language::Vhdl2008) {
      continue;
    }
    for (const auto& port : unit.ports) {
      validate(unit, port);
    }
    for (const auto& component : unit.vhdl_component_declarations) {
      for (const auto& port : component.ports) {
        validate(unit, port);
      }
    }
  }
}

void validate_vhdl_package_body_subprograms(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics) {
  const auto lower = [](std::string_view text) {
    std::string result { text };
    std::ranges::transform(result, result.begin(),
        [](const unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    return result;
  };
  for (const auto& unit : units) {
    if (unit.kind != frontend::UnitKind::VhdlPackage
        || unit.primary_name.empty()) {
      continue;
    }
    std::set<std::string> defined;
    for (const auto& procedure : unit.procedures) {
      if (procedure.defined) {
        defined.insert(lower(procedure.name));
      }
    }
    for (const auto& function : unit.functions) {
      if (function.defined) {
        defined.insert(lower(function.name));
      }
    }
    const auto check = [&](const auto& callable) {
      if (!callable.defined && !defined.contains(lower(callable.name))) {
        diagnostics.error("FSIM-VHDL-SEM-122",
            "subprogram '" + callable.name
                + "' is declared in a package body without a body there",
            span(callable.span));
      }
    };
    for (const auto& procedure : unit.procedures) {
      check(procedure);
    }
    for (const auto& function : unit.functions) {
      check(function);
    }
  }
}

void validate_vhdl_port_association_modes(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics) {
  using frontend::PortDirection;
  const auto lower = [](std::string_view text) {
    std::string result { text };
    std::ranges::transform(result, result.begin(),
        [](const unsigned char character) {
          return static_cast<char>(std::tolower(character));
        });
    return result;
  };
  const auto find_entity = [&](const std::string& library,
                               const std::string& identifier_key)
      -> const DesignUnit* {
    for (const auto& unit : units) {
      if (unit.kind == frontend::UnitKind::VhdlEntity
          && lower(library_of(unit)) == lower(library)
          && vhdl_identifier_key(
                 unit.name, unit.vhdl_extended_name) == identifier_key) {
        return &unit;
      }
    }
    return nullptr;
  };
  // VHDL-1993 rules; VHDL-2008 6.5.6.3 relaxes buffer associations.
  const auto compatible = [](const PortDirection formal,
                             const PortDirection actual) {
    switch (formal) {
    case PortDirection::Input:
      return actual == PortDirection::Input || actual == PortDirection::Inout
          || actual == PortDirection::Buffer;
    case PortDirection::Output:
      return actual == PortDirection::Output
          || actual == PortDirection::Inout;
    case PortDirection::Inout:
      return actual == PortDirection::Inout;
    case PortDirection::Buffer:
      return actual == PortDirection::Buffer;
    default:
      return true;
    }
  };
  const auto mode_name = [](const PortDirection direction) {
    switch (direction) {
    case PortDirection::Input: return "in";
    case PortDirection::Output: return "out";
    case PortDirection::Inout: return "inout";
    case PortDirection::Buffer: return "buffer";
    default: return "linkage";
    }
  };
  for (const auto& architecture : units) {
    if (architecture.kind != frontend::UnitKind::VhdlArchitecture
        || architecture.vhdl_standard >= frontend::VhdlStandard::Vhdl2008) {
      continue;
    }
    const auto* entity = find_entity(
        library_of(architecture), vhdl_identifier_key(
            architecture.primary_name,
            architecture.vhdl_extended_primary_name));
    if (entity == nullptr) {
      continue;
    }
    const auto actual_mode = [&](const frontend::Expression& value)
        -> std::optional<PortDirection> {
      if (value.kind != frontend::ExpressionKind::Identifier) {
        return std::nullopt;
      }
      const auto name = lower(value.text);
      // A signal of the architecture hides a port of the same name.
      if (std::ranges::any_of(architecture.signals, [&](const auto& signal) {
            return lower(signal.name) == name;
          })) {
        return std::nullopt;
      }
      for (const auto& port : entity->ports) {
        if (lower(port.name) == name
            && port.direction != PortDirection::Unknown) {
          return port.direction;
        }
      }
      return std::nullopt;
    };
    for (const auto& instance : architecture.instances) {
      std::vector<std::pair<std::string, PortDirection>> formals;
      if (instance.vhdl_component_instance) {
        const auto component = std::ranges::find_if(
            architecture.vhdl_component_declarations,
            [&](const frontend::VhdlComponentDeclaration& declaration) {
              return lower(declaration.name) == lower(instance.unit_name)
                  && declaration.scope_path.empty();
            });
        if (component == architecture.vhdl_component_declarations.end()) {
          continue;
        }
        // A linkage formal accepts an actual of any mode (IEEE
        // 1076-1993 1.1.1.2).
        for (const auto& port : component->ports) {
          formals.emplace_back(lower(port.name),
              port.vhdl_linkage ? PortDirection::Unknown : port.direction);
        }
      } else if (!instance.vhdl_configuration_instance) {
        const auto& spelling = instance.vhdl_unit_name_spelling.empty()
            ? instance.unit_name : instance.vhdl_unit_name_spelling;
        const auto parts = support::split_vhdl_selected_name(spelling);
        if (parts.empty()) {
          continue;
        }
        const auto* target = find_entity(
            library_of(architecture), context_lookup_name(parts.back()));
        if (target == nullptr) {
          continue;
        }
        for (const auto& port : target->ports) {
          formals.emplace_back(lower(port.name),
              port.vhdl_linkage ? PortDirection::Unknown : port.direction);
        }
      } else {
        continue;
      }
      for (std::size_t index = 0U; index < instance.connections.size();
           ++index) {
        const auto& connection = instance.connections[index];
        if (connection.kind != frontend::PortActualKind::Expression) {
          continue;
        }
        std::optional<PortDirection> formal;
        if (!connection.port) {
          if (index < formals.size()) {
            formal = formals[index].second;
          }
        } else {
          const auto port = lower(*connection.port);
          for (const auto& [name, direction] : formals) {
            if (name == port) {
              formal = direction;
            }
          }
        }
        const auto actual = actual_mode(connection.value);
        if (!formal || !actual || *formal == PortDirection::Unknown
            || compatible(*formal, *actual)) {
          continue;
        }
        diagnostics.error("FSIM-VHDL-SEM-121",
            std::string { "an actual port of mode " } + mode_name(*actual)
                + " cannot be associated with a formal port of mode "
                + mode_name(*formal),
            span(connection.span));
      }
    }
  }
}

void validate_vhdl_package_declarations(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics,
    const bool allow_missing_package_bodies) {
  for (const auto& package : units) {
    if (package.kind != frontend::UnitKind::VhdlPackage
        || !package.primary_name.empty()) {
      continue;
    }
    const auto package_library = library_of(package);
    const auto body = std::ranges::find_if(
        units, [&](const frontend::DesignUnit& candidate) {
          return candidate.kind == frontend::UnitKind::VhdlPackage
              && candidate.primary_name == package.name
              && candidate.name == package.name
              && library_of(candidate) == package_library;
        });
    if (body == units.end() && allow_missing_package_bodies) {
      continue;
    }
    for (const auto& declaration : package.parameters) {
      if (declaration.kind != frontend::ParameterKind::Value) {
        continue;
      }
      const frontend::ParameterDeclaration* completion = nullptr;
      if (body != units.end()) {
        const auto found = std::ranges::find_if(
            body->parameters, [&](const auto& candidate) {
              return candidate.kind == frontend::ParameterKind::Value
                  && candidate.name == declaration.name;
            });
        if (found != body->parameters.end()) {
          completion = &*found;
        }
      }
      if (declaration.vhdl_deferred) {
        if (completion == nullptr) {
          diagnostics.error(
              "FSIM-FE-VHDECL-001",
              "deferred VHDL package constant '" + declaration.name
                  + "' has no full declaration in the package body",
              span(declaration.span));
        } else if (!frontend::vhdl_subtype_indications_conform(
                       declaration.type, completion->type)) {
          diagnostics.error(
              "FSIM-FE-VHDECL-002",
              "full declaration of deferred VHDL package constant '"
                  + declaration.name
                  + "' does not conform to its subtype indication",
              span(completion->span));
        }
      } else if (completion != nullptr) {
        diagnostics.error(
            "FSIM-FE-VHDECL-003",
            "VHDL package body redeclares nondeferred constant '"
                + declaration.name + "' from the package declaration",
            span(completion->span));
      }
    }
  }
}

bool validate_vhdl_profile_compatibility(
    const frontend::ParsedDesign& parsed,
    diagnostic::Engine& diagnostics) {
  if (parsed.vhdl_profile_compatible) {
    return true;
  }
  if (!diagnostics.has_error()) {
    diagnostics.error(
        "FSIM-FE-VHSTD-003",
        "source analysis retained a recovery node for a VHDL construct "
        "that is unavailable in the selected language revision");
  }
  return false;
}

}  // namespace fsim::app::application_detail
