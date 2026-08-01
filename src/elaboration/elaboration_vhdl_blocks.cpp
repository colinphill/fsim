// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

DesignUnit block_unit(
    const frontend::GenerateRegion& region,
    frontend::GenerateBody& body,
    const DesignUnit& owner) {
    DesignUnit result;
    result.kind = frontend::UnitKind::VhdlArchitecture;
    result.language = frontend::Language::Vhdl2008;
    result.library = owner.library;
    result.name = owner.name;
    result.primary_name = owner.primary_name;
    result.parameters = region.block_generics;
    result.parameters.insert(
        result.parameters.end(),
        std::make_move_iterator(body.constants.begin()),
        std::make_move_iterator(body.constants.end()));
    result.ports = region.block_ports;
    result.type_aliases = std::move(body.type_aliases);
    result.signals = std::move(body.signals);
    result.signal_aliases = std::move(body.signal_aliases);
    result.functions = std::move(body.functions);
    result.tasks = std::move(body.tasks);
    result.procedures = std::move(body.procedures);
    result.vhdl_component_declarations =
        std::move(body.vhdl_component_declarations);
    result.concurrent_statements =
        std::move(body.concurrent_statements);
    result.processes = std::move(body.processes);
    result.instances = std::move(body.instances);
    result.generate_regions = std::move(body.generate_regions);
    result.span = region.span;
    return result;
}

void unpack_block_unit(
    DesignUnit& source,
    frontend::GenerateRegion& region,
    frontend::GenerateBody& body) {
    region.block_generics.clear();
    body.constants.clear();
    for (auto& parameter : source.parameters) {
        if (parameter.local) {
            body.constants.push_back(std::move(parameter));
        } else {
            region.block_generics.push_back(std::move(parameter));
        }
    }
    region.block_ports = std::move(source.ports);
    body.type_aliases = std::move(source.type_aliases);
    body.signals = std::move(source.signals);
    body.signal_aliases = std::move(source.signal_aliases);
    body.functions = std::move(source.functions);
    body.tasks = std::move(source.tasks);
    body.procedures = std::move(source.procedures);
    body.vhdl_component_declarations =
        std::move(source.vhdl_component_declarations);
    body.concurrent_statements =
        std::move(source.concurrent_statements);
    body.processes = std::move(source.processes);
    body.instances = std::move(source.instances);
    body.generate_regions = std::move(source.generate_regions);
}

}  // namespace

namespace elaboration_detail {

bool prepare_vhdl_block_interface(
    const frontend::GenerateRegion& region,
    frontend::GenerateBody& body,
    const GeneratedNameEnvironment& visible_names,
    std::vector<Diagnostic>& diagnostics) {
    bool valid = true;
    const auto report = [&](const std::string_view code,
                            std::string message,
                            const frontend::SourceSpan& span) {
      diagnostics.push_back(
          {std::string{code}, std::move(message), span});
      valid = false;
    };
    const auto writes_formal = [&](const std::string_view name) {
      std::function<bool(const std::vector<frontend::Statement>&)> writes;
      writes = [&](const auto& statements) {
        return std::ranges::any_of(statements, [&](const auto& statement) {
          if ((statement.kind == frontend::StatementKind::Assignment
               && statement.target.text == name)
              || writes(statement.statements)
              || writes(statement.else_statements)) {
              return true;
          }
          return std::ranges::any_of(
              statement.case_alternatives, [&](const auto& alternative) {
                return writes(alternative.statements);
              });
        });
      };
      return writes(body.concurrent_statements)
          || std::ranges::any_of(body.processes, [&](const auto& process) {
               return writes(process.statements);
             });
    };

    std::vector<const frontend::ParameterOverride*> generic_actuals(
        region.block_generics.size());
    std::size_t positional = 0;
    bool saw_named = false;
    for (const auto& actual : region.block_generic_map) {
        std::size_t index = region.block_generics.size();
        if (actual.name) {
            saw_named = true;
            const auto found = std::ranges::find(
                region.block_generics, *actual.name,
                &frontend::ParameterDeclaration::name);
            if (found != region.block_generics.end()) {
                index = static_cast<std::size_t>(std::distance(
                    region.block_generics.begin(), found));
            }
        } else if (saw_named) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                "a positional block generic actual follows a named actual",
                actual.span);
            continue;
        } else {
            index = positional++;
        }
        if (index >= generic_actuals.size()) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                actual.name
                    ? "unknown block generic formal '" + *actual.name + "'"
                    : "too many positional block generic actuals",
                actual.span);
        } else if (generic_actuals[index] != nullptr) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                "block generic formal '"
                    + region.block_generics[index].name
                    + "' is associated more than once",
                actual.span);
        } else {
            generic_actuals[index] = &actual;
        }
    }

    std::vector<frontend::ParameterDeclaration> constants;
    constants.reserve(region.block_generics.size());
    for (std::size_t index = 0;
         index < region.block_generics.size(); ++index) {
        auto constant = region.block_generics[index];
        if (constant.kind != frontend::ParameterKind::Value) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                "block generic formal '" + constant.name
                    + "' is not a bounded value generic",
                constant.span);
            continue;
        }
        const auto* actual = generic_actuals[index];
        if (actual != nullptr && !actual->default_box) {
            if (actual->type_value || !actual->value.valid()) {
                report(
                    "FSIM-ELAB-VHBLOCK-001",
                    "block value generic '" + constant.name
                        + "' requires an expression actual",
                    actual->span);
                continue;
            }
            constant.default_value = actual->value;
        } else if (!constant.default_value.valid()) {
            report(
                "FSIM-ELAB-VHBLOCK-001",
                "required block generic '" + constant.name
                    + "' has no actual or default",
                actual ? actual->span : constant.span);
            continue;
        }
        constant.local = true;
        constants.push_back(std::move(constant));
    }
    body.constants.insert(
        body.constants.begin(),
        std::make_move_iterator(constants.begin()),
        std::make_move_iterator(constants.end()));

    std::vector<const frontend::PortConnection*> port_actuals(
        region.block_ports.size());
    positional = 0;
    saw_named = false;
    for (const auto& actual : region.block_port_map) {
        std::size_t index = region.block_ports.size();
        if (actual.port) {
            saw_named = true;
            const auto found = std::ranges::find(
                region.block_ports, *actual.port,
                &frontend::SignalDeclaration::name);
            if (found != region.block_ports.end()) {
                index = static_cast<std::size_t>(std::distance(
                    region.block_ports.begin(), found));
            }
        } else if (saw_named) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "a positional block port actual follows a named actual",
                actual.span);
            continue;
        } else {
            index = positional++;
        }
        if (index >= port_actuals.size()) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                actual.port
                    ? "unknown block port formal '" + *actual.port + "'"
                    : "too many positional block port actuals",
                actual.span);
        } else if (port_actuals[index] != nullptr) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "block port formal '" + region.block_ports[index].name
                    + "' is associated more than once",
                actual.span);
        } else {
            port_actuals[index] = &actual;
        }
    }

    for (std::size_t index = 0;
         index < region.block_ports.size(); ++index) {
        auto port = region.block_ports[index];
        if (port.direction == frontend::PortDirection::Input
            && writes_formal(port.name)) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "input block port '" + port.name
                    + "' is read-only within the block",
                port.span);
            continue;
        }
        const auto* actual = port_actuals[index];
        frontend::PortConnection effective;
        if (actual != nullptr) {
            effective = *actual;
        } else if (port.direction == frontend::PortDirection::Input
                   && port.default_value) {
            effective.kind = frontend::PortActualKind::Default;
            effective.value = *port.default_value;
            effective.span = port.span;
        } else if (port.direction != frontend::PortDirection::Input) {
            effective.kind = frontend::PortActualKind::Open;
            effective.span = port.span;
        } else {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "required input block port '" + port.name
                    + "' has no actual or default",
                port.span);
            continue;
        }
        if (effective.kind == frontend::PortActualKind::Open
            && port.direction == frontend::PortDirection::Input) {
            if (!port.default_value) {
                report(
                    "FSIM-ELAB-VHBLOCK-002",
                    "input block port '" + port.name
                        + "' is open but has no default",
                    effective.span);
                continue;
            }
            effective.kind = frontend::PortActualKind::Default;
            effective.value = *port.default_value;
        }
        const bool conflicts = std::ranges::any_of(
            body.signals,
            [&](const frontend::SignalDeclaration& signal) {
              return signal.name == port.name;
            });
        if (conflicts) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "block port '" + port.name
                    + "' conflicts with a local signal declaration",
                port.span);
            continue;
        }
        if (effective.kind == frontend::PortActualKind::Expression
            && effective.value.kind
                == frontend::ExpressionKind::Identifier) {
            auto actual_expression = effective.value;
            qualify_generated_expression(
                actual_expression, visible_names);
            body.signal_aliases.push_back(
                frontend::SignalAliasDeclaration{
                    port.name,
                    std::move(actual_expression.text),
                    port.type,
                    port.direction,
                    effective.span});
            continue;
        }
        if (effective.kind == frontend::PortActualKind::Expression
            && port.direction != frontend::PortDirection::Input) {
            report(
                "FSIM-ELAB-VHBLOCK-002",
                "output, buffer, or inout block port '" + port.name
                    + "' requires a writable signal actual",
                effective.span);
            continue;
        }
        port.is_port = false;
        body.signals.insert(body.signals.begin(), port);
        if (effective.kind == frontend::PortActualKind::Open) {
            continue;
        }
        auto value = effective.value;
        qualify_generated_expression(value, visible_names);
        frontend::Statement driver;
        driver.kind = frontend::StatementKind::Assignment;
        driver.assignment_kind = frontend::AssignmentKind::Continuous;
        driver.target = frontend::Expression{
            frontend::ExpressionKind::Identifier,
            port.name, {}, effective.span};
        driver.value = std::move(value);
        driver.vhdl_delay_mechanism =
            frontend::VhdlDelayMechanism::ImplicitInertial;
        driver.span = effective.span;
        driver.label = "@vhdl-block-input-driver";
        body.concurrent_statements.insert(
            body.concurrent_statements.begin(), std::move(driver));
    }
    return valid;
}

}  // namespace elaboration_detail

bool HierarchyBuilder::prepare_vhdl_block_nonvalue_interface(
    frontend::GenerateRegion& region,
    frontend::GenerateBody& body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const std::string_view scope,
    GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    PackageEnvironment& packages,
    std::vector<std::pair<std::string, std::string>>& values,
    std::vector<std::pair<std::string, std::string>>& identities) {
    const bool has_nonvalue = std::ranges::any_of(
        region.block_generics,
        [](const auto& generic) {
          return generic.kind != frontend::ParameterKind::Value;
        });
    if (!has_nonvalue) {
        return true;
    }

    auto actuals = region.block_generic_map;
    for (auto& actual : actuals) {
        qualify_generated_expression(actual.value, visible_names);
        if (actual.type_value) {
            auto& type = *actual.type_value;
            if (!type.named_type.empty()) {
                if (const auto found = visible_names.find(type.named_type);
                    found != visible_names.end()) {
                    type.named_type = found->second;
                    type.spelling = found->second;
                }
            }
        }
    }

    auto local = block_unit(region, body, unit);
    const auto diagnostic_count = diagnostics_.size();
    PackageEnvironment package_bindings;
    std::vector<std::pair<std::string, std::string>>
        package_identities;
    if (std::ranges::any_of(
            local.parameters,
            [](const auto& parameter) {
              return parameter.kind == frontend::ParameterKind::Package;
            })) {
        bind_vhdl_interface_packages(
            local,
            actuals,
            packages,
            environment,
            domains,
            local_vhdl_type_environment(unit),
            unit.functions,
            unit.procedures,
            frontend::Language::Vhdl2008,
            package_bindings,
            package_identities);
    }
    std::vector<const DesignUnit*> import_stack;
    auto block_types = local_vhdl_type_environment(unit);
    import_qualified_vhdl_package_constants(
        local, import_stack);
    import_qualified_vhdl_package_types(
        local, block_types, import_stack);
    auto nonvalue = specialize_vhdl_interface_types(
        local,
        actuals,
        environment,
        domains,
        block_types,
        unit.functions,
        unit.procedures,
        frontend::Language::Vhdl2008,
        diagnostics_);
    if (nonvalue.applied) {
        resolve_named_types(nonvalue.unit, {}, true);
    }
    region.block_generic_map =
        std::move(nonvalue.value_overrides);
    for (const auto& binding : package_bindings) {
        const auto scoped_name =
            generated_scope(scope, binding.first);
        packages.insert_or_assign(
            scoped_name, binding.second);
        visible_names.insert_or_assign(
            binding.first, scoped_name);
    }
    for (const auto& dependency : nonvalue.unit.source_dependencies) {
        if (!dependency.empty()
            && std::ranges::find(
                   unit.source_dependencies, dependency)
                == unit.source_dependencies.end()) {
            unit.source_dependencies.push_back(dependency);
        }
    }
    const auto append_identities =
        [&](const auto& source) {
          for (const auto& [name, identity] : source) {
              const auto scoped_name =
                  "__block:" + std::string{scope} + ":" + name;
              values.emplace_back(scoped_name, identity);
              identities.emplace_back(scoped_name, identity);
          }
        };
    append_identities(nonvalue.values);
    append_identities(package_identities);
    unpack_block_unit(nonvalue.unit, region, body);
    return diagnostics_.size() == diagnostic_count;
}

void HierarchyBuilder::expand_vhdl_block_generates(
    SpecializedUnit& specialized) {
    VhdlBlockInterfacePreparer block_preparer =
        [&](frontend::GenerateRegion& region,
            frontend::GenerateBody& body,
            const ConstantEnvironment& environment,
            const ConstantDomainEnvironment& domains,
            const std::string_view scope,
            GeneratedNameEnvironment& visible_names,
            DesignUnit& unit,
            std::vector<Diagnostic>&) {
          return prepare_vhdl_block_nonvalue_interface(
              region,
              body,
              environment,
              domains,
              scope,
              visible_names,
              unit,
              specialized.packages,
              specialized.values,
              specialized.identity_values);
        };
    expand_specialized_unit_generates(
        specialized, diagnostics_, &block_preparer);
}

}  // namespace fsim::elaboration
