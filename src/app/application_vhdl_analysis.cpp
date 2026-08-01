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
  void report(
      const std::string_view code,
      std::string message,
      const frontend::SourceSpan& source) {
    diagnostics_.error(std::string{code}, std::move(message), span(source));
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
          if (!packages_.contains(primary_key(library, parts[1]))) {
            report(
                "FSIM-FE-VHORDER-004",
                "VHDL package '" + library + "."
                    + std::string{parts[1]}
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
          if (!contexts_.contains(primary_key(library, parts[1]))) {
            report(
                "FSIM-FE-VHORDER-003",
                "VHDL context '" + library + "."
                    + std::string{parts[1]}
                    + "' must be analyzed before it is referenced",
                item.span);
          }
        }
      }
    }
  }

  void validate_binding(
      const frontend::VhdlBindingIndication& binding,
      const std::string_view owner_library) {
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
          : std::string{owner_library};
      if (!configurations_.contains(primary_key(library, parts.back()))) {
        report(
            "FSIM-FE-VHORDER-008",
            "VHDL configuration '" + library + "."
                + std::string{parts.back()}
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
        : std::string{owner_library};
    const auto entity = parts.back();
    if (!entities_.contains(primary_key(library, entity))) {
      report(
          "FSIM-FE-VHORDER-007",
          "VHDL entity '" + library + "." + std::string{entity}
              + "' must be analyzed before it is used in a binding",
          binding.span);
    }
    if (!architectures_.contains(architecture_key(
            library, entity, binding.architecture_name))) {
      report(
          "FSIM-FE-VHORDER-006",
          "VHDL architecture '" + library + "."
              + std::string{entity} + "("
              + binding.architecture_name
              + ")' must be analyzed before it is used in a binding",
          binding.span);
    }
  }

  void validate_block(
      const frontend::VhdlBlockConfiguration& block,
      const std::string_view owner_library) {
    for (const auto& component : block.component_configurations) {
      validate_binding(component.binding, owner_library);
    }
    for (const auto& child : block.block_configurations) {
      validate_block(child, owner_library);
    }
  }

  void validate_instances(
      const std::span<const frontend::Instance> instances,
      const std::string_view owner_library) {
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
          : std::string{owner_library};
      if (!configurations_.contains(
              primary_key(library, parts.back()))) {
        report(
            "FSIM-FE-VHORDER-008",
            "VHDL configuration '" + library + "."
                + std::string{parts.back()}
                + "' must be analyzed before its direct instantiation",
            instance.span);
      }
    }
  }

  void validate_generate_body(
      const frontend::GenerateBody& body,
      const std::string_view owner_library) {
    validate_instances(body.instances, owner_library);
    for (const auto& region : body.generate_regions) {
      validate_generate_region(region, owner_library);
    }
  }

  void validate_generate_region(
      const frontend::GenerateRegion& region,
      const std::string_view owner_library) {
    validate_generate_body(region.then_body, owner_library);
    validate_generate_body(region.else_body, owner_library);
    for (const auto& alternative : region.alternatives) {
      validate_generate_body(alternative.body, owner_library);
    }
  }

  void validate_unit(const DesignUnit& unit) {
    const auto library = library_of(unit);
    switch (unit.kind) {
      case frontend::UnitKind::VhdlArchitecture:
        if (!entities_.contains(primary_key(library, unit.primary_name))) {
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
            && !packages_.contains(primary_key(library, unit.name))) {
          report(
              "FSIM-FE-VHORDER-002",
              "VHDL package '" + library + "." + unit.name
                  + "' must be analyzed before its body",
              unit.span);
        }
        break;
      case frontend::UnitKind::VhdlConfiguration:
        if (!entities_.contains(primary_key(library, unit.primary_name))) {
          report(
              "FSIM-FE-VHORDER-005",
              "VHDL entity '" + library + "." + unit.primary_name
                  + "' must be analyzed before configuration '"
                  + unit.name + "'",
              unit.span);
        }
        if (unit.vhdl_configuration) {
          const auto& block = unit.vhdl_configuration->block;
          if (!architectures_.contains(architecture_key(
                  library, unit.primary_name, block.block_name))) {
            report(
                "FSIM-FE-VHORDER-006",
                "VHDL architecture '" + library + "."
                    + unit.primary_name + "(" + block.block_name
                    + ")' must be analyzed before configuration '"
                    + unit.name + "'",
                block.span);
          }
          validate_block(block, library);
        }
        break;
      default:
        break;
    }
    for (const auto& specification :
         unit.vhdl_configuration_specifications) {
      validate_binding(specification.binding, library);
    }
    validate_instances(unit.instances, library);
    for (const auto& region : unit.generate_regions) {
      validate_generate_region(region, library);
    }
  }

  void record(const DesignUnit& unit) {
    const auto library = library_of(unit);
    switch (unit.kind) {
      case frontend::UnitKind::VhdlEntity:
        entities_.insert(primary_key(library, unit.name));
        break;
      case frontend::UnitKind::VhdlArchitecture:
        architectures_.insert(architecture_key(
            library, unit.primary_name, unit.name));
        break;
      case frontend::UnitKind::VhdlConfiguration:
        configurations_.insert(primary_key(library, unit.name));
        break;
      case frontend::UnitKind::VhdlPackage:
        if (unit.primary_name.empty()) {
          packages_.insert(primary_key(library, unit.name));
        }
        break;
      case frontend::UnitKind::VhdlContext:
        contexts_.insert(primary_key(library, unit.name));
        break;
      default:
        break;
    }
  }

  diagnostic::Engine& diagnostics_;
  std::set<std::string> entities_;
  std::set<std::string> architectures_;
  std::set<std::string> packages_;
  std::set<std::string> contexts_;
  std::set<std::string> configurations_;
};

}  // namespace

void validate_vhdl_analysis_order(
    const std::span<const frontend::DesignUnit> units,
    diagnostic::Engine& diagnostics) {
  AnalysisOrderValidator{diagnostics}.run(units);
}

}  // namespace fsim::app::application_detail
