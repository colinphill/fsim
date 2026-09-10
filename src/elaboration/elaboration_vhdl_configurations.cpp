// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

#include <sstream>

namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

std::string expression_identity(
    const frontend::Expression& expression) {
    std::ostringstream output;
    output << static_cast<int>(expression.kind)
           << ':' << expression.text;
    for (const auto& operand : expression.operands) {
        output << '(' << expression_identity(operand) << ')';
    }
    return output.str();
}

void append_map_identity(
    std::ostringstream& output,
    const std::span<
        const frontend::ParameterOverride> map) {
    for (const auto& actual : map) {
        output << ";generic:"
               << actual.name.value_or("#")
               << '=';
        if (actual.default_box) {
            output << "<>";
        } else if (actual.type_value) {
            output << "type:"
                   << actual.type_value->spelling;
        } else {
            output << expression_identity(actual.value);
        }
    }
}

void append_port_map_identity(
    std::ostringstream& output,
    const std::span<
        const frontend::PortConnection> map) {
    for (const auto& actual : map) {
        output << ";port:"
               << actual.port.value_or("#")
               << '='
               << expression_identity(actual.value);
    }
}

std::string normalized_library(
    const DesignUnit& unit) {
    return unit.library.empty()
        ? std::string{"work"}
        : unit.library;
}

const frontend::VhdlComponentConfiguration*
select_configuration_rule(
    const std::span<
        const frontend::VhdlComponentConfiguration> rules,
    const frontend::Instance& instance,
    const std::string_view local_label) {
    const frontend::VhdlComponentConfiguration* all = nullptr;
    const frontend::VhdlComponentConfiguration* others = nullptr;
    for (const auto& rule : rules) {
        if (rule.component_name != instance.unit_name) {
            continue;
        }
        if (rule.selection
                == frontend::VhdlInstantiationSelectionKind::Labels
            && std::ranges::find(
                   rule.labels, local_label)
                != rule.labels.end()) {
            return &rule;
        }
        if (rule.selection
                == frontend::VhdlInstantiationSelectionKind::All
            && all == nullptr) {
            all = &rule;
        }
        if (rule.selection
                == frontend::VhdlInstantiationSelectionKind::Others
            && others == nullptr) {
            others = &rule;
        }
    }
    return all != nullptr ? all : others;
}

std::optional<std::string> configured_block_scope(
    const frontend::VhdlBlockConfiguration& block) {
    if (!block.generate_index) {
        return block.block_name;
    }
    std::string error;
    const auto index = evaluate_constant_expression(
        *block.generate_index, {}, error);
    if (!index) {
        return std::nullopt;
    }
    return block.block_name + "[" + std::to_string(*index) + "]";
}

std::vector<std::string_view> hierarchy_parts(
    const std::string_view name) {
    std::vector<std::string_view> result;
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const auto separator = name.find('.', begin);
        result.push_back(
            name.substr(
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

std::vector<const frontend::VhdlBlockConfiguration*>
configuration_block_chain(
    const frontend::VhdlBlockConfiguration& root,
    const frontend::Instance& instance) {
    const auto parts = hierarchy_parts(instance.name);
    std::vector<const frontend::VhdlBlockConfiguration*> result{
        &root};
    const auto* selected = &root;
    if (parts.size() < 2) {
        return result;
    }
    for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
        const frontend::VhdlBlockConfiguration* next = nullptr;
        for (const auto& child : selected->block_configurations) {
            const auto scope = configured_block_scope(child);
            if (scope && *scope == parts[index]) {
                if (next != nullptr) {
                    return result;
                }
                next = &child;
            }
        }
        if (next == nullptr) {
            break;
        }
        selected = next;
        result.push_back(selected);
    }
    return result;
}

void append_block_identity(
    std::ostringstream& output,
    const frontend::VhdlBlockConfiguration& block) {
    output << ";block=" << block.block_name;
    if (block.generate_index) {
        output << '['
               << expression_identity(*block.generate_index)
               << ']';
    }
    for (const auto& rule : block.component_configurations) {
        output << ";component=" << rule.component_name
               << ";selection="
               << static_cast<int>(rule.selection);
        for (const auto& label : rule.labels) {
            output << ',' << label;
        }
        output << ";aspect="
               << static_cast<int>(rule.binding.kind)
               << ";entity=" << rule.binding.entity_name
               << ";architecture="
               << rule.binding.architecture_name
               << ";configuration="
               << rule.binding.configuration_name;
        append_map_identity(
            output, rule.binding.generic_map);
        append_port_map_identity(
            output, rule.binding.port_map);
    }
    for (const auto& child : block.block_configurations) {
        append_block_identity(output, child);
    }
    output << ";end-block";
}

}  // namespace

const DesignUnit*
HierarchyBuilder::select_vhdl_configuration_root(
    const DesignUnit& configuration) {
    if (configuration.kind
            != frontend::UnitKind::VhdlConfiguration
        || !configuration.vhdl_configuration) {
        report(
            "FSIM-ELAB-VHCONFIG-001",
            "VHDL configuration '" + configuration.name
                + "' has no retained block configuration",
            configuration.span);
        return nullptr;
    }
    const auto library =
        normalized_library(configuration);
    const auto entity_count =
        std::ranges::count_if(
            parsed_.units,
            [&](const DesignUnit& candidate) {
              return candidate.kind
                      == frontend::UnitKind::VhdlEntity
                  && candidate.name
                      == configuration.primary_name
                  && normalized_library(candidate) == library;
            });
    if (entity_count != 1) {
        report(
            "FSIM-ELAB-VHCONFIG-002",
            "configuration '" + configuration.name
                + "' requires one entity '" + library + "."
                + configuration.primary_name + "'",
            configuration.span);
        return nullptr;
    }
    const auto& block =
        configuration.vhdl_configuration->block;
    const DesignUnit* selected = nullptr;
    std::size_t matches = 0;
    for (const auto& candidate : parsed_.units) {
        if (candidate.kind
                != frontend::UnitKind::VhdlArchitecture
            || candidate.primary_name
                != configuration.primary_name
            || candidate.name != block.block_name
            || normalized_library(candidate) != library) {
            continue;
        }
        selected = &candidate;
        ++matches;
    }
    if (matches == 0) {
        report(
            "FSIM-ELAB-VHCONFIG-003",
            "configuration '" + configuration.name
                + "' selects missing architecture '"
                + block.block_name + "' of entity '"
                + configuration.primary_name + "'",
            block.span);
        return nullptr;
    }
    if (matches != 1) {
        report(
            "FSIM-ELAB-VHCONFIG-004",
            "configuration '" + configuration.name
                + "' selects an ambiguous architecture '"
                + block.block_name + "'",
            block.span);
        return nullptr;
    }
    return selected;
}

std::string HierarchyBuilder::vhdl_configuration_identity(
    const DesignUnit& configuration) const {
    std::ostringstream output;
    output << "vhdl-configuration-v2;library="
           << normalized_library(configuration)
           << ";name=" << configuration.name
           << ";entity=" << configuration.primary_name;
    if (!configuration.vhdl_configuration) {
        return output.str();
    }
    const auto& block =
        configuration.vhdl_configuration->block;
    output << ";architecture=" << block.block_name;
    append_block_identity(output, block);
    return output.str();
}

HierarchyBuilder::ConfiguredVhdlInstance
HierarchyBuilder::bind_vhdl_direct_configuration_instance(
    const DesignUnit& unit,
    const frontend::Instance& instance,
    const std::string& path) {
    ConfiguredVhdlInstance result;
    result.instance = instance;
    if (unit.language != frontend::Language::Vhdl2008
        || !instance.vhdl_configuration_instance) {
        return result;
    }
    const auto parts = selected_name_parts(instance.unit_name);
    if (parts.empty() || parts.size() > 2) {
        report(
            "FSIM-ELAB-VHCONFIG-013",
            "direct configuration instance '" + path
                + "' has a malformed selected name",
            instance.span);
        result.valid = false;
        return result;
    }
    const auto parent_library = normalized_library(unit);
    const auto requested_library = parts.size() == 2
        ? (parts.front() == "work" ? parent_library : parts.front())
        : parent_library;
    const DesignUnit* configuration = nullptr;
    std::size_t matches = 0;
    for (const auto& candidate : parsed_.units) {
        if (candidate.kind == frontend::UnitKind::VhdlConfiguration
            && candidate.name == parts.back()
            && normalized_library(candidate) == requested_library) {
            configuration = &candidate;
            ++matches;
        }
    }
    if (matches != 1 || configuration == nullptr) {
        report(
            matches == 0
                ? "FSIM-ELAB-VHCONFIG-013"
                : "FSIM-ELAB-VHCONFIG-014",
            "direct configuration instance '" + path + "' selects "
                + (matches == 0 ? "missing" : "ambiguous")
                + " configuration '" + requested_library + "."
                + parts.back() + "'",
            instance.span);
        result.valid = false;
        return result;
    }
    const auto* root = select_vhdl_configuration_root(*configuration);
    if (root == nullptr) {
        result.valid = false;
        return result;
    }
    result.target = *root;
    const auto source = std::string{
        frontend::physical_source(configuration->span)};
    if (!source.empty()
        && std::ranges::find(
               result.target->source_dependencies, source)
            == result.target->source_dependencies.end()) {
        result.target->source_dependencies.push_back(source);
    }
    result.referenced_configuration = configuration;
    result.configuration_identity =
        vhdl_configuration_identity(*configuration);
    result.applied = true;
    return result;
}

void HierarchyBuilder::validate_vhdl_component_configurations(
    const DesignUnit& unit,
    const std::string& path) {
    const auto validate =
        [&](const std::span<
                const frontend::VhdlComponentConfiguration> rules,
            const std::optional<std::string_view> scope) {
          const auto in_scope =
              [&](const frontend::Instance& instance) {
                if (!scope) {
                    return true;
                }
                return scope->empty()
                    || std::string_view{instance.name}.starts_with(
                        std::string{*scope} + ".");
              };
          const auto local_label =
              [](const std::string_view name) {
                const auto separator =
                    name.find_last_of('.');
                return name.substr(
                    separator == std::string_view::npos
                        ? 0
                        : separator + 1);
              };
          std::unordered_map<
              std::string,
              std::unordered_set<std::string>>
              explicit_labels;
          std::unordered_map<std::string, std::size_t> all_counts;
          std::unordered_map<std::string, std::size_t> others_counts;
          for (const auto& rule : rules) {
              const bool component_exists =
                  std::ranges::any_of(
                      unit.instances,
                      [&](const auto& instance) {
                        return instance.vhdl_component_instance
                            && in_scope(instance)
                            && instance.unit_name
                                == rule.component_name;
                      });
              if (!component_exists) {
                  report(
                      "FSIM-ELAB-VHCONFIG-005",
                      "configuration in '" + path
                          + "' names component '"
                          + rule.component_name
                          + "' with no component instances",
                      rule.span);
              }
              if (rule.selection
                  == frontend::VhdlInstantiationSelectionKind::
                      Labels) {
                  auto& seen =
                      explicit_labels[rule.component_name];
                  for (const auto& label : rule.labels) {
                      const bool matches =
                          std::ranges::any_of(
                              unit.instances,
                              [&](const auto& instance) {
                                return instance
                                           .vhdl_component_instance
                                    && in_scope(instance)
                                    && local_label(instance.name)
                                        == label
                                    && instance.unit_name
                                        == rule.component_name;
                              });
                      if (!matches) {
                          report(
                              "FSIM-ELAB-VHCONFIG-006",
                              "configuration label '" + label
                                  + "' does not select component '"
                                  + rule.component_name + "' in '"
                                  + path + "'",
                              rule.span);
                      }
                      if (!seen.insert(label).second) {
                          report(
                              "FSIM-ELAB-VHCONFIG-007",
                              "configuration label '" + label
                                  + "' is bound more than once for "
                                    "component '"
                                  + rule.component_name + "'",
                              rule.span);
                      }
                  }
              } else if (
                  rule.selection
                  == frontend::VhdlInstantiationSelectionKind::All) {
                  ++all_counts[rule.component_name];
              } else {
                  ++others_counts[rule.component_name];
              }
          }
          for (const auto& [component, count] : all_counts) {
              if (count != 1
                  || others_counts.contains(component)
                  || explicit_labels.contains(component)) {
                  report(
                      "FSIM-ELAB-VHCONFIG-007",
                      "an all configuration for component '"
                          + component
                          + "' overlaps another binding",
                      unit.span);
              }
          }
          for (const auto& [component, count] : others_counts) {
              if (count != 1) {
                  report(
                      "FSIM-ELAB-VHCONFIG-007",
                      "component '" + component
                          + "' has multiple others bindings",
                      unit.span);
              }
          }
        };

    validate(unit.vhdl_configuration_specifications, std::nullopt);
    const auto active = vhdl_configurations_by_path_.find(path);
    if (active != vhdl_configurations_by_path_.end()
        && active->second->vhdl_configuration) {
        const auto validate_block =
            [&](const auto& self,
                const frontend::VhdlBlockConfiguration& block,
                const std::string& scope) -> void {
              validate(
                  block.component_configurations,
                  std::string_view{scope});
              std::unordered_set<std::string> sibling_scopes;
              for (const auto& child :
                   block.block_configurations) {
                  const auto local =
                      configured_block_scope(child);
                  if (!local) {
                      report(
                          "FSIM-ELAB-VHCONFIG-010",
                          "configuration block '" + child.block_name
                              + "' requires a locally static generate index",
                          child.span);
                      continue;
                  }
                  if (!sibling_scopes.insert(*local).second) {
                      report(
                          "FSIM-ELAB-VHCONFIG-015",
                          "configuration block scope '" + *local
                              + "' is selected more than once",
                          child.span);
                      continue;
                  }
                  const auto nested_scope =
                      scope.empty()
                          ? *local
                          : scope + "." + *local;
                  const auto exists =
                      std::ranges::any_of(
                          unit.instances,
                          [&](const auto& instance) {
                            return instance.name.starts_with(
                                nested_scope + ".");
                          });
                  if (!exists) {
                      report(
                          "FSIM-ELAB-VHCONFIG-010",
                          "configuration block scope '"
                              + nested_scope
                              + "' does not select an elaborated "
                                "block or generate occurrence in '"
                              + path + "'",
                          child.span);
                      continue;
                  }
                  self(self, child, nested_scope);
              }
            };
        validate_block(
            validate_block,
            active->second->vhdl_configuration->block,
            "");
    }
}

HierarchyBuilder::ConfiguredVhdlInstance
HierarchyBuilder::configure_vhdl_component_instance(
    const DesignUnit& unit,
    const frontend::Instance& instance,
    const std::string& path) {
    ConfiguredVhdlInstance result;
    result.instance = instance;
    if (unit.language != frontend::Language::Vhdl2008
        || !instance.vhdl_component_instance) {
        return result;
    }

    const frontend::VhdlComponentConfiguration* rule = nullptr;
    const DesignUnit* active_configuration = nullptr;
    std::size_t active_path_size = 0;
    for (const auto& [configured_path, configuration] :
         vhdl_configurations_by_path_) {
        if ((path == configured_path
             || path.starts_with(configured_path + "."))
            && configured_path.size() >= active_path_size) {
            active_configuration = configuration;
            active_path_size = configured_path.size();
        }
    }
    if (active_configuration != nullptr
        && active_configuration->vhdl_configuration) {
        const auto blocks =
            configuration_block_chain(
                active_configuration->vhdl_configuration->block,
                instance);
        const auto parts = hierarchy_parts(instance.name);
        for (auto block = blocks.rbegin();
             block != blocks.rend() && rule == nullptr;
             ++block) {
            rule = select_configuration_rule(
                (*block)->component_configurations,
                instance,
                parts.back());
        }
    }
    if (rule == nullptr) {
        const auto parts = hierarchy_parts(instance.name);
        rule = select_configuration_rule(
            unit.vhdl_configuration_specifications,
            instance,
            parts.back());
    }
    if (rule == nullptr) {
        return result;
    }
    result.configuration_rule = rule;
    std::ostringstream binding_identity;
    binding_identity << "vhdl-configuration-binding-v2;aspect="
                     << static_cast<int>(rule->binding.kind)
                     << ";entity=" << rule->binding.entity_name
                     << ";architecture="
                     << rule->binding.architecture_name
                     << ";configuration="
                     << rule->binding.configuration_name;
    append_map_identity(
        binding_identity, rule->binding.generic_map);
    append_port_map_identity(
        binding_identity, rule->binding.port_map);
    result.configuration_identity = binding_identity.str();
    if (rule->binding.kind
        == frontend::VhdlBindingAspectKind::Open) {
        return result;
    }
    result.applied = true;

    const auto parent_library = normalized_library(unit);
    const DesignUnit* selected = nullptr;
    if (rule->binding.kind
        == frontend::VhdlBindingAspectKind::Configuration) {
        const auto parts =
            selected_name_parts(
                rule->binding.configuration_name);
        if (parts.empty() || parts.size() > 2) {
            report(
                "FSIM-ELAB-VHCONFIG-013",
                "configuration binding for '" + path
                    + "' has a malformed configuration aspect",
                rule->binding.span);
            result.valid = false;
            return result;
        }
        const auto requested_library =
            parts.size() == 2
                ? (parts[0] == "work"
                       ? parent_library
                       : parts[0])
                : parent_library;
        std::size_t matches = 0;
        const DesignUnit* referenced = nullptr;
        for (const auto& candidate : parsed_.units) {
            if (candidate.kind
                    == frontend::UnitKind::VhdlConfiguration
                && candidate.name == parts.back()
                && normalized_library(candidate)
                    == requested_library) {
                referenced = &candidate;
                ++matches;
            }
        }
        if (matches != 1 || referenced == nullptr) {
            report(
                matches == 0
                    ? "FSIM-ELAB-VHCONFIG-013"
                    : "FSIM-ELAB-VHCONFIG-014",
                "configuration binding for '" + path
                    + "' selects "
                    + (matches == 0
                           ? "missing"
                           : "ambiguous")
                    + " configuration '"
                    + requested_library + "."
                    + parts.back() + "'",
                rule->binding.span);
            result.valid = false;
            return result;
        }
        selected = select_vhdl_configuration_root(*referenced);
        if (selected == nullptr) {
            result.valid = false;
            return result;
        }
        result.referenced_configuration = referenced;
        result.configuration_identity +=
            ";reference="
            + vhdl_configuration_identity(*referenced);
    } else {
        const auto parts =
            selected_name_parts(rule->binding.entity_name);
        if (parts.empty() || parts.size() > 2
            || rule->binding.architecture_name.empty()) {
            report(
                "FSIM-ELAB-VHCONFIG-008",
                "configuration binding for '" + path
                    + "' has a malformed entity aspect",
                rule->binding.span);
            result.valid = false;
            return result;
        }
        const auto requested_library =
            parts.size() == 2
                ? (parts[0] == "work"
                       ? parent_library
                       : parts[0])
                : parent_library;
        const auto& entity_name = parts.back();
        std::size_t matches = 0;
        for (const auto& candidate : parsed_.units) {
            if (candidate.kind
                    != frontend::UnitKind::VhdlArchitecture
                || candidate.primary_name != entity_name
                || candidate.name
                    != rule->binding.architecture_name
                || normalized_library(candidate)
                    != requested_library) {
                continue;
            }
            selected = &candidate;
            ++matches;
        }
        if (matches == 0) {
            report(
                "FSIM-ELAB-VHCONFIG-008",
                "configuration binding for '" + path
                    + "' selects missing VHDL target '"
                    + requested_library + "." + entity_name
                    + "(" + rule->binding.architecture_name + ")'",
                rule->binding.span);
            result.valid = false;
            return result;
        }
        if (matches != 1) {
            report(
                "FSIM-ELAB-VHCONFIG-009",
                "configuration binding for '" + path
                    + "' selects an ambiguous VHDL target",
                rule->binding.span);
            result.valid = false;
            return result;
        }
    }

    if (!rule->binding.generic_map.empty()) {
        std::unordered_set<std::string> consumed;
        std::vector<frontend::ParameterOverride> mapped;
        for (const auto& binding_actual :
             rule->binding.generic_map) {
            auto actual = binding_actual;
            if (actual.value.kind
                == frontend::ExpressionKind::Identifier) {
                const auto component_actual =
                    std::ranges::find_if(
                        instance.parameter_overrides,
                        [&](const auto& candidate) {
                          return candidate.name
                              == actual.value.text;
                        });
                if (component_actual
                    != instance.parameter_overrides.end()) {
                    const auto target_name = actual.name;
                    actual = *component_actual;
                    actual.name = target_name;
                    consumed.insert(
                        component_actual->name.value());
                }
            }
            mapped.push_back(std::move(actual));
        }
        for (const auto& actual :
             instance.parameter_overrides) {
            if (!actual.name) {
                report(
                    "FSIM-ELAB-VHCONFIG-011",
                    "positional component generic maps cannot be "
                    "composed with a configuration binding on '"
                        + path + "'",
                    actual.span);
                result.valid = false;
                continue;
            }
            if (!consumed.contains(*actual.name)
                && std::ranges::none_of(
                    mapped,
                    [&](const auto& candidate) {
                      return candidate.name == actual.name;
                    })) {
                mapped.push_back(actual);
            }
        }
        result.instance.parameter_overrides =
            std::move(mapped);
    }

    if (!rule->binding.port_map.empty()) {
        std::unordered_set<std::string> consumed;
        std::vector<frontend::PortConnection> mapped;
        for (const auto& binding_actual :
             rule->binding.port_map) {
            if (!binding_actual.port
                || binding_actual.value.kind
                    != frontend::ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-VHCONFIG-012",
                    "configuration port maps require named target "
                    "ports and simple component-port names on '"
                        + path + "'",
                    binding_actual.span);
                result.valid = false;
                continue;
            }
            const auto component_actual =
                std::ranges::find_if(
                    instance.connections,
                    [&](const auto& candidate) {
                      return candidate.port
                          == binding_actual.value.text;
                    });
            if (component_actual
                == instance.connections.end()) {
                report(
                    "FSIM-ELAB-VHCONFIG-012",
                    "configuration port map for target port '"
                        + *binding_actual.port
                        + "' names unconnected component port '"
                        + binding_actual.value.text + "' on '"
                        + path + "'",
                    binding_actual.span);
                result.valid = false;
                continue;
            }
            auto actual = *component_actual;
            consumed.insert(
                component_actual->port.value());
            actual.port = binding_actual.port;
            mapped.push_back(std::move(actual));
        }
        for (const auto& actual : instance.connections) {
            if (!actual.port) {
                report(
                    "FSIM-ELAB-VHCONFIG-011",
                    "positional component port maps cannot be composed "
                    "with a configuration binding on '"
                        + path + "'",
                    actual.span);
                result.valid = false;
                continue;
            }
            if (!consumed.contains(*actual.port)
                && std::ranges::none_of(
                    mapped,
                    [&](const auto& candidate) {
                      return candidate.port == actual.port;
                    })) {
                mapped.push_back(actual);
            }
        }
        result.instance.connections = std::move(mapped);
    }

    auto target = *selected;
    const auto configuration_source = std::string{
        frontend::physical_source(rule->span)};
    if (!configuration_source.empty()
        && configuration_source
            != frontend::physical_source(target.span)
        && std::ranges::find(
               target.source_dependencies,
               configuration_source)
            == target.source_dependencies.end()) {
        target.source_dependencies.push_back(
            configuration_source);
    }
    if (result.referenced_configuration != nullptr) {
        const auto referenced_source = std::string{
            frontend::physical_source(
                result.referenced_configuration->span)};
        if (!referenced_source.empty()
            && referenced_source
                != frontend::physical_source(target.span)
            && std::ranges::find(
                   target.source_dependencies,
                   referenced_source)
                == target.source_dependencies.end()) {
            target.source_dependencies.push_back(
                referenced_source);
        }
    }
    result.target = std::move(target);
    return result;
}

}  // namespace fsim::elaboration
