// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
using namespace runtime::simir;

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
        for (auto& variable : function.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_parameters(
            function.statements,
            environment,
            domains,
            diagnostics,
            language);
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
        for (auto& variable : procedure.variables) {
            substitute_parameters(
                variable, environment, domains, diagnostics, language);
        }
        substitute_parameters(
            procedure.statements,
            environment,
            domains,
            diagnostics,
            language);
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
        for (auto& variable : function.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_parameters(
            function.statements,
            environment,
            domains,
            diagnostics,
            language);
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
        for (auto& variable : procedure.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_parameters(
            procedure.statements,
            environment,
            domains,
            diagnostics,
            language);
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
        for (auto& variable : process.variables) {
            substitute_parameters(
                variable,
                environment,
                domains,
                diagnostics,
                language);
        }
        for (auto& sensitivity : process.sensitivities) {
            substitute_parameters(
                sensitivity.expression,
                environment,
                domains,
                language);
        }
        substitute_parameters(
            process.statements,
            environment,
            domains,
            diagnostics,
            language);
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
