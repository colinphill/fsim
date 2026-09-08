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
    const auto parts = selected_name_parts(view_name);
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
        const auto imported = selected_name_parts(selected);
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
