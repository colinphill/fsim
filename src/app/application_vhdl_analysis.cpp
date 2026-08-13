// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

namespace fsim::app::application_detail {
namespace {

using frontend::DesignUnit;

std::string library_of(const DesignUnit& unit) {
  return unit.library.empty() ? std::string{"work"} : unit.library;
}

std::vector<std::string_view> selected_name_parts(
    const std::string_view name) {
  std::vector<std::string_view> result;
  std::size_t begin = 0;
  while (begin <= name.size()) {
    const auto separator = name.find('.', begin);
    result.push_back(name.substr(
        begin,
        separator == std::string_view::npos
            ? name.size() - begin
            : separator - begin));
    if (separator == std::string_view::npos) {
      break;
    }
    begin = separator + 1;
  }
  return result;
}

std::string selected_library(
    const std::string_view requested,
    const std::string_view owner) {
  return requested == "work" ? std::string{owner} : std::string{requested};
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
  explicit AnalysisOrderValidator(diagnostic::Engine& diagnostics)
      : diagnostics_(diagnostics) {}

  void run(const std::span<const DesignUnit> units) {
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
     using RevisionIndex = std::map<std::string, frontend::VhdlStandard>;

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
         if (found->second != owner.vhdl_standard) {
             report(
                 "FSIM-FE-VHORDER-011",
                 "VHDL " + std::string { kind } + " '" + key
                     + "' was analyzed as "
                     + std::string { frontend::to_string(found->second) }
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
        const auto parts = selected_name_parts(selected_name);
        if (item.kind == frontend::VhdlContextItemKind::UseClause
            && parts.size() == 3) {
          if (parts[0] == "ieee" || parts[0] == "std") {
            continue;
          }
          const auto library = selected_library(parts[0], owner_library);
          if (!validate_dependency(
                  packages_, primary_key(library, parts[1]), "package",
                  unit, item.span)) {
              report(
                  "FSIM-FE-VHORDER-004",
                  "VHDL package '" + library + "."
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
                  contexts_, primary_key(library, parts[1]), "context",
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
          const auto parts = selected_name_parts(binding.configuration_name);
          if (parts.empty() || parts.size() > 2) {
              return;
          }
          const auto library = parts.size() == 2
              ? selected_library(parts[0], owner_library)
              : std::string { owner_library };
          if (!validate_dependency(
                  configurations_, primary_key(library, parts.back()),
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

      const auto parts = selected_name_parts(binding.entity_name);
      if (parts.empty() || parts.size() > 2
          || binding.architecture_name.empty()) {
          return;
      }
      const auto library = parts.size() == 2
          ? selected_library(parts[0], owner_library)
          : std::string { owner_library };
      const auto entity = parts.back();
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
              architectures_, architecture_key(library, entity, binding.architecture_name),
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

  void validate_block(
      const frontend::VhdlBlockConfiguration& block,
      const DesignUnit& owner)
  {
      for (const auto& component : block.component_configurations) {
          validate_binding(component.binding, owner);
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
          const auto parts = selected_name_parts(instance.unit_name);
          if (parts.empty() || parts.size() > 2) {
              continue;
          }
          const auto library = parts.size() == 2
              ? selected_library(parts[0], owner_library)
              : std::string { owner_library };
          if (!validate_dependency(
                  configurations_, primary_key(library, parts.back()),
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
          if (!validate_dependency(
                  entities_, primary_key(library, unit.primary_name),
                  "entity", unit, unit.span)) {
              report(
                  "FSIM-FE-VHORDER-001",
                  "VHDL entity '" + library + "." + unit.primary_name
                      + "' must be analyzed before architecture '"
                      + unit.name + "'",
                  unit.span);
          }
        break;
      case frontend::UnitKind::VhdlPackage:
          if (!unit.primary_name.empty()
              && !validate_dependency(
                  packages_, primary_key(library, unit.name), "package",
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
                  entities_, primary_key(library, unit.primary_name),
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
                  architectures_, architecture_key(library, unit.primary_name, block.block_name),
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
    const auto record_primary =
        [&] {
          const auto key = primary_key(library, unit.name);
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
        entities_.insert_or_assign(primary_key(library, unit.name), unit.vhdl_standard);
        break;
      case frontend::UnitKind::VhdlArchitecture:
        record_secondary(
            architecture_key(library, unit.primary_name, unit.name));
        architectures_.insert_or_assign(architecture_key(
                                            library, unit.primary_name, unit.name),
            unit.vhdl_standard);
        break;
      case frontend::UnitKind::VhdlConfiguration:
        record_primary();
        configurations_.insert_or_assign(primary_key(library, unit.name), unit.vhdl_standard);
        break;
      case frontend::UnitKind::VhdlPackage:
        if (unit.primary_name.empty()) {
          record_primary();
          packages_.insert_or_assign(primary_key(library, unit.name), unit.vhdl_standard);
        } else {
          record_secondary(
              primary_key(library, unit.name) + "\npackage-body");
        }
        break;
      case frontend::UnitKind::VhdlContext:
        record_primary();
        contexts_.insert_or_assign(primary_key(library, unit.name), unit.vhdl_standard);
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
    diagnostic::Engine& diagnostics) {
  AnalysisOrderValidator{diagnostics}.run(units);
}

void validate_vhdl_package_declarations(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics) {
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

}  // namespace fsim::app::application_detail
