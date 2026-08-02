// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <sstream>

namespace fsim::elaboration {
using namespace elaboration_detail;

namespace {

bool conforming_type(
    const frontend::Type& left,
    const frontend::Type& right) {
    return left.spelling == right.spelling
        && left.named_type == right.named_type
        && left.domain == right.domain
        && left.is_signed == right.is_signed;
}

bool conforming_generic_parameters(
    const std::vector<frontend::ParameterDeclaration>& left,
    const std::vector<frontend::ParameterDeclaration>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.name != rhs.name || lhs.kind != rhs.kind) {
            return false;
        }
        if (lhs.kind == frontend::ParameterKind::Value
            && !conforming_type(lhs.type, rhs.type)) {
            return false;
        }
        if (lhs.kind == frontend::ParameterKind::Function) {
            if (!lhs.function_profile
                || !rhs.function_profile) {
                return false;
            }
            const auto& lprofile = *lhs.function_profile;
            const auto& rprofile = *rhs.function_profile;
            if (!conforming_type(
                    lprofile.return_type,
                    rprofile.return_type)
                || lprofile.arguments.size()
                    != rprofile.arguments.size()) {
                return false;
            }
            for (std::size_t argument = 0;
                 argument < lprofile.arguments.size();
                 ++argument) {
                if (!conforming_type(
                        lprofile.arguments[argument].type,
                        rprofile.arguments[argument].type)) {
                    return false;
                }
            }
        }
        if (lhs.kind == frontend::ParameterKind::Procedure) {
            if (!lhs.procedure_profile
                || !rhs.procedure_profile) {
                return false;
            }
            const auto& lprofile = *lhs.procedure_profile;
            const auto& rprofile = *rhs.procedure_profile;
            if (lprofile.arguments.size()
                != rprofile.arguments.size()) {
                return false;
            }
            for (std::size_t argument = 0;
                 argument < lprofile.arguments.size();
                 ++argument) {
                const auto& larg =
                    lprofile.arguments[argument];
                const auto& rarg =
                    rprofile.arguments[argument];
                if (larg.direction != rarg.direction
                    || larg.object_class
                        != rarg.object_class
                    || !conforming_type(
                        larg.type, rarg.type)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool conforming_function(
    const frontend::FunctionDeclaration& left,
    const frontend::FunctionDeclaration& right) {
    if (left.name != right.name
        || left.pure != right.pure
        || !conforming_type(
            left.return_type, right.return_type)
        || left.arguments.size() != right.arguments.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.arguments.size(); ++index) {
        if (left.arguments[index].direction
                != right.arguments[index].direction
            || !conforming_type(
                left.arguments[index].type,
                right.arguments[index].type)) {
            return false;
        }
    }
    return true;
}

bool conforming_procedure(
    const frontend::ProcedureDeclaration& left,
    const frontend::ProcedureDeclaration& right) {
    if (left.name != right.name
        || left.arguments.size() != right.arguments.size()) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.arguments.size(); ++index) {
        const auto& lhs = left.arguments[index];
        const auto& rhs = right.arguments[index];
        if (lhs.direction != rhs.direction
            || lhs.object_class != rhs.object_class
            || !conforming_type(lhs.type, rhs.type)) {
            return false;
        }
    }
    return true;
}

bool expression_calls(
    const frontend::Expression& expression,
    const std::string_view name) {
    if (expression.kind == frontend::ExpressionKind::Call
        && expression.text == name) {
        return true;
    }
    for (const auto& operand : expression.operands) {
        if (expression_calls(operand, name)) {
            return true;
        }
    }
    for (const auto& association :
         expression.aggregate_choice_expressions) {
        for (const auto& choice : association) {
            if (expression_calls(choice, name)) {
                return true;
            }
        }
    }
    return false;
}

bool statements_call(
    const std::vector<frontend::Statement>& statements,
    const std::string_view name,
    const bool procedure) {
    for (const auto& statement : statements) {
        if ((procedure
             && statement.kind
                 == frontend::StatementKind::ProcedureCall
             && statement.procedure_name == name)
            || expression_calls(statement.target, name)
            || expression_calls(statement.value, name)
            || expression_calls(statement.condition, name)
            || expression_calls(statement.loop_initial, name)
            || expression_calls(statement.loop_limit, name)
            || statements_call(
                statement.statements, name, procedure)
            || statements_call(
                statement.else_statements, name, procedure)) {
            return true;
        }
        for (const auto& argument : statement.task_arguments) {
            if (expression_calls(argument, name)) {
                return true;
            }
        }
        for (const auto& association :
             statement.procedure_arguments) {
            if (expression_calls(
                    association.value, name)) {
                return true;
            }
        }
        for (const auto& alternative :
             statement.case_alternatives) {
            if (statements_call(
                    alternative.statements,
                    name,
                    procedure)) {
                return true;
            }
        }
    }
    return false;
}

void rename_expression_calls(
    frontend::Expression& expression,
    const std::string_view from,
    const std::string_view to) {
    if (expression.kind == frontend::ExpressionKind::Call
        && expression.text == from) {
        expression.text = std::string{to};
    }
    for (auto& operand : expression.operands) {
        rename_expression_calls(operand, from, to);
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            rename_expression_calls(choice, from, to);
        }
    }
}

void rename_statement_calls(
    std::vector<frontend::Statement>& statements,
    const std::string_view from,
    const std::string_view to) {
    for (auto& statement : statements) {
        if (statement.kind
                == frontend::StatementKind::ProcedureCall
            && statement.procedure_name == from) {
            statement.procedure_name = std::string{to};
        }
        rename_expression_calls(statement.target, from, to);
        rename_expression_calls(statement.value, from, to);
        rename_expression_calls(
            statement.condition, from, to);
        rename_expression_calls(
            statement.loop_initial, from, to);
        rename_expression_calls(
            statement.loop_limit, from, to);
        for (auto& argument : statement.task_arguments) {
            rename_expression_calls(argument, from, to);
        }
        for (auto& association :
             statement.procedure_arguments) {
            rename_expression_calls(
                association.value, from, to);
        }
        rename_statement_calls(
            statement.statements, from, to);
        rename_statement_calls(
            statement.else_statements, from, to);
        for (auto& alternative :
             statement.case_alternatives) {
            rename_statement_calls(
                alternative.statements, from, to);
        }
    }
}

ConstantDomainEnvironment subprogram_domains(
    const DesignUnit& unit,
    const ConstantEnvironment& environment) {
    ConstantDomainEnvironment result;
    for (const auto& parameter : unit.parameters) {
        if (environment.contains(parameter.name)) {
            result.insert_or_assign(
                parameter.name,
                ConstantTypeInfo{
                    parameter.type.domain,
                    !parameter.type.enumeration_literals.empty(),
                    parameter.type.nominal_type});
        }
    }
    return result;
}

bool visible_before(
    const frontend::SourceSpan& declaration,
    const frontend::SourceSpan& use) {
    const auto declaration_source =
        frontend::physical_source(declaration);
    const auto use_source = frontend::physical_source(use);
    return declaration_source != use_source
        || declaration.begin.offset < use.begin.offset;
}

void append_dependency(
    DesignUnit& unit,
    const std::string_view source) {
    if (!source.empty()
        && std::ranges::find(
               unit.source_dependencies, source)
            == unit.source_dependencies.end()) {
        unit.source_dependencies.emplace_back(source);
    }
}

std::string generic_identity(
    const std::string_view kind,
    const std::string_view template_name,
    const frontend::SourceSpan& template_span,
    const frontend::SourceSpan& body_span,
    const std::vector<frontend::ParameterDeclaration>& formals,
    const std::vector<std::pair<std::string, std::string>>&
        interface_values,
    const std::vector<std::pair<std::string, std::string>>&
        value_identities) {
    std::unordered_map<std::string, std::string> identities;
    for (const auto& value : interface_values) {
        identities.insert_or_assign(
            value.first, value.second);
    }
    for (const auto& value : value_identities) {
        identities.insert_or_assign(
            value.first, value.second);
    }
    std::ostringstream output;
    output << "vhdl-generic-subprogram-v1;kind=" << kind
           << ";template=" << template_name
           << ";declaration="
           << frontend::physical_source(template_span)
           << ':' << template_span.begin.offset
           << ";body=" << frontend::physical_source(body_span)
           << ':' << body_span.begin.offset;
    for (const auto& formal : formals) {
        output << ";generic=" << formal.name << '=';
        if (const auto found = identities.find(formal.name);
            found != identities.end()) {
            output << found->second;
        } else {
            output << "<unresolved>";
        }
    }
    return output.str();
}

} // namespace

void HierarchyBuilder::instantiate_vhdl_generic_subprograms(
    SpecializedUnit& specialized) {
    if (specialized.unit.language
        != frontend::Language::Vhdl2008) {
        return;
    }
    auto& unit = specialized.unit;

    std::vector<frontend::GenericFunctionTemplate>
        function_templates;
    for (const auto& candidate :
         unit.generic_function_templates) {
        if (candidate.function.defined) {
            const bool has_declaration =
                std::ranges::any_of(
                    unit.generic_function_templates,
                    [&](const auto& declaration) {
                      return !declaration.function.defined
                          && conforming_generic_parameters(
                              declaration.generic_parameters,
                              candidate.generic_parameters)
                          && conforming_function(
                              declaration.function,
                              candidate.function);
                    });
            if (has_declaration) {
                continue;
            }
            function_templates.push_back(candidate);
            continue;
        }
        const auto body = std::ranges::find_if(
            unit.generic_function_templates,
            [&](const auto& possible) {
              return possible.function.defined
                  && conforming_generic_parameters(
                      candidate.generic_parameters,
                      possible.generic_parameters)
                  && conforming_function(
                      candidate.function,
                      possible.function);
            });
        if (body
            != unit.generic_function_templates.end()) {
            auto merged = candidate;
            merged.function = body->function;
            function_templates.push_back(std::move(merged));
            continue;
        }
        const bool mismatched_body =
            std::ranges::any_of(
                unit.generic_function_templates,
                [&](const auto& possible) {
                  return possible.function.defined
                      && possible.function.name
                          == candidate.function.name;
                });
        if (mismatched_body) {
            report(
                "FSIM-ELAB-VHGSUB-013",
                "generic function body does not conform to declaration '"
                    + candidate.function.name + "'",
                candidate.span);
        }
        function_templates.push_back(candidate);
    }

    std::vector<frontend::GenericProcedureTemplate>
        procedure_templates;
    for (const auto& candidate :
         unit.generic_procedure_templates) {
        if (candidate.procedure.defined) {
            const bool has_declaration =
                std::ranges::any_of(
                    unit.generic_procedure_templates,
                    [&](const auto& declaration) {
                      return !declaration.procedure.defined
                          && conforming_generic_parameters(
                              declaration.generic_parameters,
                              candidate.generic_parameters)
                          && conforming_procedure(
                              declaration.procedure,
                              candidate.procedure);
                    });
            if (has_declaration) {
                continue;
            }
            procedure_templates.push_back(candidate);
            continue;
        }
        const auto body = std::ranges::find_if(
            unit.generic_procedure_templates,
            [&](const auto& possible) {
              return possible.procedure.defined
                  && conforming_generic_parameters(
                      candidate.generic_parameters,
                      possible.generic_parameters)
                  && conforming_procedure(
                      candidate.procedure,
                      possible.procedure);
            });
        if (body
            != unit.generic_procedure_templates.end()) {
            auto merged = candidate;
            merged.procedure = body->procedure;
            procedure_templates.push_back(std::move(merged));
            continue;
        }
        const bool mismatched_body =
            std::ranges::any_of(
                unit.generic_procedure_templates,
                [&](const auto& possible) {
                  return possible.procedure.defined
                      && possible.procedure.name
                          == candidate.procedure.name;
                });
        if (mismatched_body) {
            report(
                "FSIM-ELAB-VHGSUB-013",
                "generic procedure body does not conform to declaration '"
                    + candidate.procedure.name + "'",
                candidate.span);
        }
        procedure_templates.push_back(candidate);
    }
    unit.generic_function_templates = function_templates;
    unit.generic_procedure_templates = procedure_templates;

    struct Pending {
        bool function{};
        const frontend::GenericSubprogramInstantiation* instance{};
    };
    std::vector<Pending> pending;
    for (const auto& instance :
         unit.generic_function_instances) {
        pending.push_back({true, &instance});
    }
    for (const auto& instance :
         unit.generic_procedure_instances) {
        pending.push_back({false, &instance});
    }
    std::stable_sort(
        pending.begin(),
        pending.end(),
        [](const Pending& left, const Pending& right) {
          return left.instance->span.begin.offset
              < right.instance->span.begin.offset;
        });

    auto domains =
        subprogram_domains(unit, specialized.environment);
    for (const auto& item : pending) {
        const auto& instance = *item.instance;
        const auto exact_generated_template =
            std::ranges::any_of(
                function_templates,
                [&](const auto& candidate) {
                  return candidate.function.name
                      == instance.template_name;
                })
            || std::ranges::any_of(
                procedure_templates,
                [&](const auto& candidate) {
                  return candidate.procedure.name
                      == instance.template_name;
                });
        if (instance.template_name.find('.')
                != std::string::npos
            && !exact_generated_template) {
            report(
                "FSIM-ELAB-VHGSUB-009",
                "selected or scoped generic subprogram template '"
                    + instance.template_name
                    + "' is outside the bounded directly visible subset",
                instance.span);
            continue;
        }

        const auto visible_function =
            [&](const auto& candidate) {
              return candidate.function.name
                      == instance.template_name
                  && visible_before(
                      candidate.span, instance.span);
            };
        const auto visible_procedure =
            [&](const auto& candidate) {
              return candidate.procedure.name
                      == instance.template_name
                  && visible_before(
                      candidate.span, instance.span);
            };
        const auto function_match_count =
            std::ranges::count_if(
                function_templates, visible_function);
        const auto procedure_match_count =
            std::ranges::count_if(
                procedure_templates, visible_procedure);
        const auto expected_count =
            item.function
                ? function_match_count
                : procedure_match_count;
        const auto wrong_count =
            item.function
                ? procedure_match_count
                : function_match_count;
        if (expected_count == 0) {
            const bool ordinary_kind =
                std::ranges::any_of(
                    unit.functions,
                    [&](const auto& function) {
                      return function.name
                          == instance.template_name;
                    })
                || std::ranges::any_of(
                    unit.procedures,
                    [&](const auto& procedure) {
                      return procedure.name
                          == instance.template_name;
                    });
            report(
                wrong_count != 0 || ordinary_kind
                    ? "FSIM-ELAB-VHGSUB-003"
                    : "FSIM-ELAB-VHGSUB-001",
                wrong_count != 0
                    ? "generic subprogram template '"
                        + instance.template_name
                        + "' has the wrong subprogram kind"
                    : ordinary_kind
                        ? "subprogram '" + instance.template_name
                            + "' is not a generic template"
                        : "generic subprogram template '"
                            + instance.template_name
                            + "' is not directly visible before this "
                              "instantiation",
                instance.span);
            continue;
        }
        if (expected_count != 1) {
            report(
                "FSIM-ELAB-VHGSUB-002",
                "generic subprogram template '"
                    + instance.template_name
                    + "' is ambiguous",
                instance.span);
            continue;
        }

        const auto function_template =
            item.function
                ? std::ranges::find_if(
                      function_templates, visible_function)
                : function_templates.end();
        const auto procedure_template =
            item.function
                ? procedure_templates.end()
                : std::ranges::find_if(
                      procedure_templates, visible_procedure);
        const auto& generic_parameters =
            item.function
                ? function_template->generic_parameters
                : procedure_template->generic_parameters;
        const bool nested_package =
            std::ranges::any_of(
                generic_parameters,
                [](const auto& parameter) {
                  return parameter.kind
                      == frontend::ParameterKind::Package;
                });
        if (nested_package) {
            report(
                "FSIM-ELAB-VHGSUB-011",
                "interface package formals are outside the bounded "
                "generic subprogram subset",
                instance.span);
            continue;
        }
        const bool defined =
            item.function
                ? function_template->function.defined
                : procedure_template->procedure.defined;
        if (!defined) {
            report(
                "FSIM-ELAB-VHGSUB-004",
                "generic subprogram template '"
                    + instance.template_name
                    + "' has no conforming executable body",
                instance.span);
            continue;
        }
        const bool recursive =
            item.function
                ? statements_call(
                      function_template->function.statements,
                      function_template->function.name,
                      false)
                : statements_call(
                      procedure_template->procedure.statements,
                      procedure_template->procedure.name,
                      true);
        if (recursive) {
            report(
                "FSIM-ELAB-VHGSUB-007",
                "recursive generic subprogram template '"
                    + instance.template_name
                    + "' is not supported",
                instance.span);
            continue;
        }
        const bool conflict =
            std::ranges::any_of(
                unit.functions,
                [&](const auto& function) {
                  return function.name == instance.name;
                })
            || std::ranges::any_of(
                unit.procedures,
                [&](const auto& procedure) {
                  return procedure.name == instance.name;
                });
        if (conflict) {
            report(
                "FSIM-ELAB-VHGSUB-008",
                "generic subprogram instance '" + instance.name
                    + "' conflicts with a callable subprogram",
                instance.span);
            continue;
        }

        DesignUnit template_unit;
        template_unit.kind =
            frontend::UnitKind::VhdlArchitecture;
        template_unit.language =
            frontend::Language::Vhdl2008;
        template_unit.library = unit.library;
        template_unit.name = instance.template_name;
        template_unit.parameters = generic_parameters;
        template_unit.type_aliases = unit.type_aliases;
        if (item.function) {
            template_unit.functions.push_back(
                function_template->function);
        } else {
            template_unit.procedures.push_back(
                procedure_template->procedure);
        }

        auto actuals = instance.generic_map;
        std::erase_if(
            actuals,
            [](const auto& actual) {
              return actual.default_box;
            });
        std::vector<frontend::FunctionDeclaration>
            visible_functions;
        for (const auto& function : unit.functions) {
            if (visible_before(
                    function.span, instance.span)) {
                visible_functions.push_back(function);
            }
        }
        std::vector<frontend::ProcedureDeclaration>
            visible_procedures;
        for (const auto& procedure : unit.procedures) {
            if (visible_before(
                    procedure.span, instance.span)) {
                visible_procedures.push_back(procedure);
            }
        }
        const auto diagnostic_count = diagnostics_.size();
        auto interface_specialized =
            specialize_vhdl_interface_types(
                template_unit,
                actuals,
                specialized.environment,
                domains,
                local_vhdl_type_environment(unit),
                visible_functions,
                visible_procedures,
                frontend::Language::Vhdl2008,
                diagnostics_);
        if (interface_specialized.applied) {
            resolve_named_types(
                interface_specialized.unit,
                {},
                true,
                false);
        }
        auto selected = specialize_unit(
            interface_specialized.unit,
            interface_specialized.value_overrides,
            specialized.environment,
            frontend::Language::Vhdl2008,
            diagnostics_);
        if (diagnostics_.size() != diagnostic_count) {
            continue;
        }
        const auto& selected_identities =
            selected.identity_values.empty()
                ? selected.values
                : selected.identity_values;
        const auto declaration_span =
            item.function
                ? function_template->span
                : procedure_template->span;
        const auto body_span =
            item.function
                ? function_template->function.span
                : procedure_template->procedure.span;
        const auto identity = generic_identity(
            item.function ? "function" : "procedure",
            instance.template_name,
            declaration_span,
            body_span,
            generic_parameters,
            interface_specialized.values,
            selected_identities);

        std::vector<std::pair<std::string, std::string>>
            renamed_functions;
        std::vector<std::pair<std::string, std::string>>
            renamed_procedures;
        for (const auto& formal : generic_parameters) {
            if (formal.kind
                == frontend::ParameterKind::Function) {
                renamed_functions.emplace_back(
                    formal.name,
                    instance.name + ".__generic."
                        + formal.name);
            } else if (
                formal.kind
                == frontend::ParameterKind::Procedure) {
                renamed_procedures.emplace_back(
                    formal.name,
                    instance.name + ".__generic."
                        + formal.name);
            }
        }

        if (item.function) {
            const auto selected_function =
                std::ranges::find_if(
                    selected.unit.functions,
                    [&](const auto& function) {
                      return function.name
                              == instance.template_name
                          && function.span.begin.offset
                              == body_span.begin.offset
                          && frontend::physical_source(
                                 function.span)
                              == frontend::physical_source(
                                 body_span);
                    });
            if (selected_function
                == selected.unit.functions.end()) {
                report(
                    "FSIM-ELAB-VHGSUB-012",
                    "generic function specialization lost its template "
                    "body",
                    instance.span);
                continue;
            }
            auto callable = *selected_function;
            callable.name = instance.name;
            callable.specialization_identity = identity;
            for (const auto& [from, to] :
                 renamed_functions) {
                rename_statement_calls(
                    callable.statements, from, to);
            }
            for (const auto& [from, to] :
                 renamed_procedures) {
                rename_statement_calls(
                    callable.statements, from, to);
            }
            unit.functions.push_back(std::move(callable));
        } else {
            const auto selected_procedure =
                std::ranges::find_if(
                    selected.unit.procedures,
                    [&](const auto& procedure) {
                      return procedure.name
                              == instance.template_name
                          && procedure.span.begin.offset
                              == body_span.begin.offset
                          && frontend::physical_source(
                                 procedure.span)
                              == frontend::physical_source(
                                 body_span);
                    });
            if (selected_procedure
                == selected.unit.procedures.end()) {
                report(
                    "FSIM-ELAB-VHGSUB-012",
                    "generic procedure specialization lost its template "
                    "body",
                    instance.span);
                continue;
            }
            auto callable = *selected_procedure;
            callable.name = instance.name;
            callable.specialization_identity = identity;
            for (const auto& [from, to] :
                 renamed_functions) {
                rename_statement_calls(
                    callable.statements, from, to);
            }
            for (const auto& [from, to] :
                 renamed_procedures) {
                rename_statement_calls(
                    callable.statements, from, to);
            }
            unit.procedures.push_back(std::move(callable));
        }

        for (const auto& [from, to] :
             renamed_functions) {
            const auto bound = std::ranges::find_if(
                selected.unit.functions,
                [&](const auto& function) {
                  return function.name == from;
                });
            if (bound == selected.unit.functions.end()) {
                continue;
            }
            auto auxiliary = *bound;
            auxiliary.name = to;
            unit.functions.push_back(std::move(auxiliary));
        }
        for (const auto& [from, to] :
             renamed_procedures) {
            const auto bound = std::ranges::find_if(
                selected.unit.procedures,
                [&](const auto& procedure) {
                  return procedure.name == from;
                });
            if (bound == selected.unit.procedures.end()) {
                continue;
            }
            auto auxiliary = *bound;
            auxiliary.name = to;
            unit.procedures.push_back(std::move(auxiliary));
        }

        append_dependency(
            unit,
            frontend::physical_source(declaration_span));
        append_dependency(
            unit,
            frontend::physical_source(body_span));
        for (const auto& dependency :
             selected.unit.source_dependencies) {
            append_dependency(unit, dependency);
        }
        specialized.values.emplace_back(
            instance.name, identity);
        specialized.identity_values.emplace_back(
            instance.name, identity);
    }

    const auto diagnose_unspecialized =
        [&](const std::string& name,
            const bool procedure,
            const frontend::SourceSpan& span) {
          bool called = false;
          for (const auto& function : unit.functions) {
            called = called
                || statements_call(
                    function.statements, name, procedure);
          }
          for (const auto& candidate : unit.procedures) {
            called = called
                || statements_call(
                    candidate.statements, name, procedure);
          }
          for (const auto& process : unit.processes) {
            called = called
                || statements_call(
                    process.statements, name, procedure);
          }
          called = called
              || statements_call(
                  unit.concurrent_statements,
                  name,
                  procedure);
          for (const auto& child : unit.instances) {
            for (const auto& actual :
                 child.parameter_overrides) {
              called = called
                  || (actual.value.kind
                          == frontend::ExpressionKind::Identifier
                      && actual.value.text == name);
            }
          }
          if (called) {
            report(
                "FSIM-ELAB-VHGSUB-006",
                "generic subprogram template '" + name
                    + "' must be instantiated before use",
                span);
          }
        };
    for (const auto& generic : function_templates) {
        diagnose_unspecialized(
            generic.function.name, false, generic.span);
    }
    for (const auto& generic : procedure_templates) {
        diagnose_unspecialized(
            generic.procedure.name, true, generic.span);
    }
    unit.generic_function_instances.clear();
    unit.generic_procedure_instances.clear();
}

} // namespace fsim::elaboration
