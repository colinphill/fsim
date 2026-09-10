// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

std::string generated_scope(
    const std::string_view parent_scope,
    const std::string_view local_scope) {
    if (local_scope.empty()) {
        return std::string{parent_scope};
    }
    return parent_scope.empty()
        ? std::string{local_scope}
        : std::string{parent_scope} + "." + std::string{local_scope};
}

void substitute_parameters(
    frontend::VariableDeclaration& declaration,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    substitute_parameters(
        declaration.type,
        environment,
        domains,
        diagnostics,
        language);
    if (declaration.initializer) {
        substitute_parameters(
            *declaration.initializer,
            environment,
            domains,
            language);
    }
}

void qualify_generated_classes(
    std::vector<frontend::SystemVerilogClassDeclaration>& declarations,
    GeneratedNameEnvironment& names,
    const std::string_view scope,
    const std::string_view unit_name) {
    for (const auto& declaration : declarations) {
        names[declaration.name] = generated_scope(
            scope, declaration.name);
    }
    const auto qualify_base = [&](frontend::SystemVerilogClassBase& base) {
        if (const auto found = names.find(base.name);
            found != names.end()) {
            base.name = found->second;
            base.declaration_identity = std::string{unit_name}
                + "::" + base.name;
        }
        for (auto& actual : base.parameter_actuals) {
            qualify_generated_expression(actual.value, names);
            if (actual.type_actual) {
                qualify_generated_type(*actual.type_actual, names);
            }
        }
    };
    const auto qualify_class =
        [&](const auto& self,
            frontend::SystemVerilogClassDeclaration& declaration,
            const std::string_view parent_identity) -> void {
          const auto local_name = declaration.name;
          if (parent_identity.empty()) {
              declaration.name = names.at(local_name);
              declaration.enclosing_scope = std::string{unit_name};
              declaration.canonical_identity = std::string{unit_name}
                  + "::" + declaration.name;
          } else {
              declaration.enclosing_scope = std::string{parent_identity};
              declaration.canonical_identity = std::string{parent_identity}
                  + "::" + local_name;
          }
          for (auto& parameter : declaration.parameters) {
              qualify_generated_type(parameter.type, names);
              qualify_generated_expression(parameter.default_value, names);
              if (parameter.default_type) {
                  qualify_generated_type(*parameter.default_type, names);
              }
          }
          if (declaration.base) {
              qualify_base(*declaration.base);
          }
          for (auto& base : declaration.extended_interfaces) {
              qualify_base(base);
          }
          for (auto& base : declaration.implemented_interfaces) {
              qualify_base(base);
          }
          for (auto& alias : declaration.type_aliases) {
              qualify_generated_type(alias.type, names);
          }
          for (auto& property : declaration.properties) {
              qualify_generated_type(property.declaration.type, names);
              if (property.declaration.initializer) {
                  qualify_generated_expression(
                      *property.declaration.initializer, names);
              }
          }
          for (auto& method : declaration.methods) {
              method.canonical_identity = declaration.canonical_identity
                  + "::" + method.name;
              qualify_generated_type(method.return_type, names);
              for (auto& argument : method.arguments) {
                  qualify_generated_type(argument.type, names);
                  if (argument.default_value) {
                      qualify_generated_expression(
                          *argument.default_value, names);
                  }
              }
              for (auto& alias : method.type_aliases) {
                  qualify_generated_type(alias.type, names);
              }
              for (auto& variable : method.variables) {
                  qualify_generated_type(variable.type, names);
                  if (variable.initializer) {
                      qualify_generated_expression(
                          *variable.initializer, names);
                  }
              }
              qualify_generated_statements(method.statements, names);
          }
          for (auto& constraint : declaration.constraints) {
              constraint.canonical_identity = declaration.canonical_identity
                  + "::" + constraint.name;
              for (auto& expression : constraint.expressions) {
                  qualify_generated_expression(expression, names);
              }
          }
          for (auto& nested : declaration.nested_classes) {
              self(self, nested, declaration.canonical_identity);
          }
        };
    for (auto& declaration : declarations) {
        qualify_class(qualify_class, declaration, {});
    }
}

void qualify_generated_instance(
    frontend::Instance& instance,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    instance.name = generated_scope(scope, instance.name);
    for (auto& override : instance.parameter_overrides) {
        qualify_generated_expression(override.value, names);
    }
    for (auto& connection : instance.connections) {
        qualify_generated_expression(connection.value, names);
    }
}


} // namespace fsim::elaboration::elaboration_detail
