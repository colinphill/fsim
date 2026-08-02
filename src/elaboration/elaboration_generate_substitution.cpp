// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
using namespace runtime::simir;

void qualify_generated_expression(
    Expression& expression,
    const GeneratedNameEnvironment& names) {
    if (expression.kind == ExpressionKind::Identifier
        || expression.kind == ExpressionKind::Call) {
        if (const auto found = names.find(expression.text);
            found != names.end()) {
            expression.text = found->second;
        } else if (const auto separator = expression.text.find('.');
                   separator != std::string::npos) {
            if (const auto prefix = names.find(
                    expression.text.substr(0, separator));
                prefix != names.end()) {
                expression.text = prefix->second
                    + expression.text.substr(separator);
            }
        }
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            qualify_generated_expression(choice, names);
        }
    }
    for (auto& operand : expression.operands) {
        qualify_generated_expression(operand, names);
    }
}

void qualify_generated_type(
    frontend::Type& type,
    const GeneratedNameEnvironment& names) {
    if (!type.named_type.empty()) {
        if (const auto found = names.find(type.named_type);
            found != names.end()) {
            if (type.spelling == type.named_type) {
                type.spelling = found->second;
            }
            type.named_type = found->second;
        } else if (const auto separator = type.named_type.find('.');
                   separator != std::string::npos) {
            if (const auto prefix = names.find(
                    type.named_type.substr(0, separator));
                prefix != names.end()) {
                if (type.spelling == type.named_type) {
                    type.spelling = prefix->second
                        + type.named_type.substr(separator);
                }
                type.named_type = prefix->second
                    + type.named_type.substr(separator);
            }
        }
    }
    if (type.systemverilog_container
        && type.systemverilog_container->associative_index_type) {
        qualify_generated_type(
            *type.systemverilog_container->associative_index_type,
            names);
    }
}

void substitute_parameters(
    frontend::GenerateBody& body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics) {
    // Generated bodies retain local constants until a concrete branch or
    // iteration is selected. Substituting the rest of the body here would
    // diagnose legitimate constraint references before those declarations
    // have been evaluated; append_generated_body performs the complete
    // substitution after establishing the selected-body environment.
    if (!body.constants.empty()) {
        return;
    }
    for (auto& alias : body.type_aliases) {
        substitute_parameters(
            alias.type,
            environment,
            domains,
            diagnostics,
            language);
    }
    for (auto& constant : body.constants) {
        substitute_parameters(
            constant.type,
            environment,
            domains,
            diagnostics,
            language);
        substitute_parameters(
            constant.default_value,
            environment,
            domains,
            language);
    }
    for (auto& signal : body.signals) {
        substitute_parameters(
            signal,
            environment,
            domains,
            diagnostics,
            language);
    }
    for (auto& alias : body.signal_aliases) {
        substitute_parameters(
            alias.type,
            environment,
            domains,
            diagnostics,
            language);
    }
    const auto substitute_local_declarations =
        [&](auto&& self,
            auto& owner,
            const ConstantEnvironment& parent_environment,
            const ConstantDomainEnvironment& parent_domains) -> void {
          auto local_environment = parent_environment;
          auto local_domains = parent_domains;
          frontend::GenerateBody declarations;
          declarations.constants = std::move(owner.constants);
          declarations.type_aliases = std::move(owner.type_aliases);
          evaluate_generated_constants(
              declarations,
              local_environment,
              local_domains,
              language,
              diagnostics);
          owner.constants = std::move(declarations.constants);
          owner.type_aliases =
              std::move(declarations.type_aliases);
          for (auto& alias : owner.type_aliases) {
              substitute_parameters(
                  alias.type,
                  local_environment,
                  local_domains,
                  diagnostics,
                  language);
          }
          for (auto& alias : owner.signal_aliases) {
              substitute_parameters(
                  alias.type,
                  local_environment,
                  local_domains,
                  diagnostics,
                  language);
          }
          for (auto& variable : owner.variables) {
              substitute_parameters(
                  variable,
                  local_environment,
                  local_domains,
                  diagnostics,
                  language);
          }
          for (auto& package : owner.package_instances) {
              for (auto& actual : package.generic_map) {
                  substitute_parameters(
                      actual.value,
                      local_environment,
                      local_domains,
                      language);
                  if (actual.type_value) {
                      substitute_parameters(
                          *actual.type_value,
                          local_environment,
                          local_domains,
                          diagnostics,
                          language);
                  }
              }
          }
          for (auto& function : owner.functions) {
              self(
                  self,
                  function,
                  local_environment,
                  local_domains);
          }
          for (auto& procedure : owner.procedures) {
              self(
                  self,
                  procedure,
                  local_environment,
                  local_domains);
          }
          substitute_parameters(
              owner.statements,
              local_environment,
              local_domains,
              diagnostics,
              language);
        };
    const auto substitute_deferred_local_declarations =
        [&](auto&& self, auto& owner) -> void {
          for (auto& constant : owner.constants) {
              substitute_parameters(
                  constant.type,
                  environment,
                  domains,
                  diagnostics,
                  language);
              substitute_parameters(
                  constant.default_value,
                  environment,
                  domains,
                  language);
          }
          for (auto& alias : owner.type_aliases) {
              substitute_parameters(
                  alias.type,
                  environment,
                  domains,
                  diagnostics,
                  language);
          }
          for (auto& alias : owner.signal_aliases) {
              substitute_parameters(
                  alias.type,
                  environment,
                  domains,
                  diagnostics,
                  language);
          }
          for (auto& variable : owner.variables) {
              substitute_parameters(
                  variable,
                  environment,
                  domains,
                  diagnostics,
                  language);
          }
          for (auto& package : owner.package_instances) {
              for (auto& actual : package.generic_map) {
                  substitute_parameters(
                      actual.value,
                      environment,
                      domains,
                      language);
                  if (actual.type_value) {
                      substitute_parameters(
                          *actual.type_value,
                          environment,
                          domains,
                          diagnostics,
                          language);
                  }
              }
          }
          for (auto& function : owner.functions) {
              self(self, function);
          }
          for (auto& procedure : owner.procedures) {
              self(self, procedure);
          }
          substitute_parameters(
              owner.statements,
              environment,
              domains,
              diagnostics,
              language);
        };
    for (auto& function : body.functions) {
        substitute_parameters(
            function.return_type,
            environment,
            domains,
            diagnostics,
            language);
        for (auto& argument : function.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                language);
            if (argument.default_value) {
                substitute_parameters(
                    *argument.default_value,
                    environment,
                    domains,
                    language);
            }
        }
        substitute_local_declarations(
            substitute_local_declarations,
            function,
            environment,
            domains);
    }
    for (auto& task : body.tasks) {
        for (auto& argument : task.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                language);
            if (argument.default_value) {
                substitute_parameters(
                    *argument.default_value,
                    environment,
                    domains,
                    language);
            }
        }
        for (auto& variable : task.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_parameters(
            task.statements,
            environment,
            domains,
            diagnostics,
            language);
    }
    for (auto& procedure : body.procedures) {
        for (auto& argument : procedure.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                language);
            if (argument.default_value) {
                substitute_parameters(
                    *argument.default_value,
                    environment,
                    domains,
                    language);
            }
        }
        substitute_local_declarations(
            substitute_local_declarations,
            procedure,
            environment,
            domains);
    }
    const auto substitute_generic_parameters =
        [&](auto& parameters) {
          for (auto& parameter : parameters) {
              if (parameter.kind == frontend::ParameterKind::Type
                  && parameter.default_type) {
                  substitute_parameters(
                      *parameter.default_type,
                      environment,
                      domains,
                      diagnostics,
                      language);
              } else if (
                  parameter.kind == frontend::ParameterKind::Function
                  && parameter.function_profile) {
                  substitute_parameters(
                      parameter.function_profile->return_type,
                      environment,
                      domains,
                      diagnostics,
                      language);
                  for (auto& argument :
                       parameter.function_profile->arguments) {
                      substitute_parameters(
                          argument.type,
                          environment,
                          domains,
                          diagnostics,
                          language);
                  }
              } else if (
                  parameter.kind == frontend::ParameterKind::Procedure
                  && parameter.procedure_profile) {
                  for (auto& argument :
                       parameter.procedure_profile->arguments) {
                      substitute_parameters(
                          argument.type,
                          environment,
                          domains,
                          diagnostics,
                          language);
                  }
              } else {
                  substitute_parameters(
                      parameter.type,
                      environment,
                      domains,
                      diagnostics,
                      language);
                  substitute_parameters(
                      parameter.default_value,
                      environment,
                      domains,
                      language);
              }
          }
        };
    for (auto& generic : body.generic_function_templates) {
        substitute_generic_parameters(generic.generic_parameters);
        auto& function = generic.function;
        substitute_parameters(
            function.return_type,
            environment,
            domains,
            diagnostics,
            language);
        for (auto& argument : function.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_deferred_local_declarations(
            substitute_deferred_local_declarations,
            function);
    }
    for (auto& generic : body.generic_procedure_templates) {
        substitute_generic_parameters(generic.generic_parameters);
        auto& procedure = generic.procedure;
        for (auto& argument : procedure.arguments) {
            substitute_parameters(
                argument.type,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_deferred_local_declarations(
            substitute_deferred_local_declarations,
            procedure);
    }
    const auto substitute_generic_maps =
        [&](auto& instances) {
          for (auto& instance : instances) {
              for (auto& actual : instance.generic_map) {
                  substitute_parameters(
                      actual.value,
                      environment,
                      domains,
                      language);
                  if (actual.type_value) {
                      substitute_parameters(
                          *actual.type_value,
                          environment,
                          domains,
                          diagnostics,
                          language);
                  }
              }
          }
        };
    substitute_generic_maps(body.generic_function_instances);
    substitute_generic_maps(body.generic_procedure_instances);
    substitute_generic_maps(body.package_instances);
    substitute_parameters(
        body.concurrent_statements,
        environment,
        domains,
        diagnostics,
        language);
    for (auto& process : body.processes) {
        substitute_local_declarations(
            substitute_local_declarations,
            process,
            environment,
            domains);
        for (auto& sensitivity : process.sensitivities) {
            substitute_parameters(
                sensitivity.expression,
                environment,
                domains,
                language);
        }
    }
    substitute_parameters(
        body.instances, environment, domains, language);
    substitute_parameters(
        body.generate_regions,
        environment,
        domains,
        language,
        diagnostics);
}
void substitute_parameters(
    std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& generate : generates) {
        for (auto& actual : generate.block_generic_map) {
            substitute_parameters(
                actual.value, environment, domains, language);
        }
        for (auto& actual : generate.block_port_map) {
            substitute_parameters(
                actual.value, environment, domains, language);
        }
        substitute_parameters(
            generate.initial, environment, domains, language);
        auto body_environment = environment;
        auto body_domains = domains;
        if (generate.kind == frontend::GenerateKind::Iterative) {
            body_environment.erase(generate.variable);
            body_domains.erase(generate.variable);
        }
        substitute_parameters(
            generate.condition,
            body_environment,
            body_domains,
            language);
        substitute_parameters(
            generate.iteration,
            body_environment,
            body_domains,
            language);
        if (generate.kind != frontend::GenerateKind::Iterative) {
            substitute_parameters(
                generate.then_body,
                body_environment,
                body_domains,
                language,
                diagnostics);
        }
        substitute_parameters(
            generate.else_body,
            body_environment,
            body_domains,
            language,
            diagnostics);
        for (auto& alternative : generate.alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_parameters(
                    choice.left,
                    body_environment,
                    body_domains,
                    language);
                if (choice.right) {
                    substitute_parameters(
                        *choice.right,
                        body_environment,
                        body_domains,
                        language);
                }
            }
            substitute_parameters(
                alternative.body,
                body_environment,
                body_domains,
                language,
                diagnostics);
        }
    }
}

}  // namespace fsim::elaboration::elaboration_detail
