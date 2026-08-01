// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {



const char* specialization_diagnostic_code(
    const bool is_vhdl,
    const bool is_package,
    const bool is_systemverilog_package,
    const SpecializationDiagnostic diagnostic) {
    switch (diagnostic) {
    case SpecializationDiagnostic::invalid_actual:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-001"
            : "FSIM-ELAB-PARAM-001";
    case SpecializationDiagnostic::duplicate_actual:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-002"
            : "FSIM-ELAB-PARAM-002";
    case SpecializationDiagnostic::association_order:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-003"
            : "FSIM-ELAB-PARAM-003";
    case SpecializationDiagnostic::actual_evaluation:
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-004"
            : "FSIM-ELAB-PARAM-004";
    case SpecializationDiagnostic::default_evaluation:
        if (is_package) {
            return "FSIM-ELAB-PKG-005";
        }
        if (is_systemverilog_package) {
            return "FSIM-ELAB-SVPKG-006";
        }
        return is_vhdl
            ? "FSIM-ELAB-GENERIC-005"
            : "FSIM-ELAB-PARAM-005";
    case SpecializationDiagnostic::subtype_constraint:
        return is_package
            ? "FSIM-ELAB-PKG-006"
            : "FSIM-ELAB-GENERIC-008";
    case SpecializationDiagnostic::ambiguous_name:
        return "FSIM-ELAB-PARAM-009";
    }
    return is_vhdl
        ? "FSIM-ELAB-GENERIC-001"
        : "FSIM-ELAB-PARAM-001";
}



bool parameter_name_matches(
    const std::string_view formal,
    const std::string_view actual,
    const frontend::Language target_language,
    const frontend::Language association_language) {
    if (target_language != frontend::Language::Vhdl2008
        && association_language
            != frontend::Language::Vhdl2008) {
        return formal == actual;
    }
    if (formal.size() != actual.size()) {
        return false;
    }
    for (std::size_t index = 0; index < formal.size(); ++index) {
        const auto formal_character =
            static_cast<unsigned char>(formal[index]);
        const auto actual_character =
            static_cast<unsigned char>(actual[index]);
        if (std::tolower(formal_character)
            != std::tolower(actual_character)) {
            return false;
        }
    }
    return true;
}

namespace {

std::optional<std::int64_t> packed_vhdl_static_value(
    const frontend::Expression& expression,
    const frontend::Type& type,
    std::string& error) {
    if (!type.packed_range
        || (expression.kind != frontend::ExpressionKind::Aggregate
            && expression.kind
                != frontend::ExpressionKind::StringLiteral
            && expression.kind
                != frontend::ExpressionKind::LogicLiteral)) {
        return std::nullopt;
    }
    const auto packed = static_vhdl_value(expression, type, error);
    if (!packed || packed->width() > 64) {
        if (packed && error.empty()) {
            error = "the packed aggregate exceeds 64 bits";
        }
        return std::nullopt;
    }
    const auto word = packed->low_word();
    if (word.bval != 0) {
        error = "the packed aggregate contains an unknown or high-impedance "
                "element";
        return std::nullopt;
    }
    return static_cast<std::int64_t>(word.aval);
}

std::array<std::uint64_t, 3> transition_delays(
    const frontend::Delay& delay) {
    const auto rise = delay.magnitude;
    const auto fall = delay.additional_values.empty()
        ? rise
        : delay.additional_values.front().magnitude;
    const auto turnoff = delay.additional_values.size() < 2
        ? std::min(rise, fall)
        : delay.additional_values[1].magnitude;
    return {rise, fall, turnoff};
}

std::optional<frontend::Delay> combined_delay(
    const frontend::Delay& driver,
    const frontend::Delay& net,
    const frontend::SourceSpan& span,
    std::vector<Diagnostic>& diagnostics) {
    const auto driver_values = transition_delays(driver);
    const auto net_values = transition_delays(net);
    std::array<std::uint64_t, 3> combined{};
    for (std::size_t index = 0; index < combined.size(); ++index) {
        if (driver_values[index]
            > std::numeric_limits<std::uint64_t>::max()
                - net_values[index]) {
            diagnostics.push_back({
                "FSIM-ELAB-SVDELAY-003",
                "combined continuous-assignment and net-declaration delay "
                "overflows the 64-bit simulation time range",
                span});
            return std::nullopt;
        }
        combined[index] = driver_values[index] + net_values[index];
    }
    frontend::Delay result;
    result.magnitude = combined[0];
    result.additional_values.resize(2);
    result.additional_values[0].magnitude = combined[1];
    result.additional_values[1].magnitude = combined[2];
    result.span = span;
    return result;
}

const frontend::Expression* delay_target_base(
    const frontend::Expression& expression) {
    if ((expression.kind == frontend::ExpressionKind::Index
         || expression.kind == frontend::ExpressionKind::Slice)
        && !expression.operands.empty()) {
        return delay_target_base(expression.operands.front());
    }
    return &expression;
}

void apply_net_delays(
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics) {
    std::vector<const frontend::SignalDeclaration*> delayed;
    for (const auto& port : unit.ports) {
        if (port.net_delay) {
            delayed.push_back(&port);
        }
    }
    for (const auto& signal : unit.signals) {
        if (signal.net_delay) {
            delayed.push_back(&signal);
        }
    }
    for (auto& statement : unit.concurrent_statements) {
        if (statement.kind != frontend::StatementKind::Assignment
            || statement.assignment_kind
                != frontend::AssignmentKind::Continuous) {
            continue;
        }
        const auto* base = delay_target_base(statement.target);
        if (base->kind != frontend::ExpressionKind::Identifier) {
            continue;
        }
        const frontend::SignalDeclaration* declaration = nullptr;
        for (const auto* candidate : delayed) {
            const bool matches = base->text == candidate->name
                || (base->text.starts_with(candidate->name)
                    && base->text.size() > candidate->name.size()
                    && base->text[candidate->name.size()] == '.');
            if (matches
                && (declaration == nullptr
                    || candidate->name.size()
                        > declaration->name.size())) {
                declaration = candidate;
            }
        }
        if (declaration == nullptr) {
            continue;
        }
        if (!statement.delay) {
            statement.delay = declaration->net_delay;
        } else {
            statement.delay = combined_delay(
                *statement.delay,
                *declaration->net_delay,
                statement.span,
                diagnostics);
        }
    }
}

}  // namespace



SpecializedUnit specialize_unit(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics,
    const bool expand_generates) {
    SpecializedUnit result;
    result.unit = source;
    const bool is_vhdl =
        source.language == frontend::Language::Vhdl2008;
    const bool is_package =
        source.kind == frontend::UnitKind::VhdlPackage;
    const bool is_systemverilog_package =
        source.kind
        == frontend::UnitKind::SystemVerilogPackage;
    const bool is_verilog =
        source.language == frontend::Language::SystemVerilog2017
        || source.language == frontend::Language::Verilog2005;
    const bool association_is_vhdl =
        association_language == frontend::Language::Vhdl2008;
    const auto code = [&](const SpecializationDiagnostic diagnostic) {
        return specialization_diagnostic_code(
            is_vhdl,
            is_package,
            is_systemverilog_package,
            diagnostic);
    };
    const auto object_kind =
        (is_package || is_systemverilog_package)
            ? std::string_view{"package constant"}
        : is_vhdl ? std::string_view{"generic"}
                : std::string_view{"parameter"};
    if (!is_vhdl && !is_verilog) {
        if (!overrides.empty()) {
            diagnostics.push_back({
                code(SpecializationDiagnostic::invalid_actual),
                "generic or parameter actuals cannot target this design "
                "unit",
                overrides.front().span});
        }
        return result;
    }

    std::vector<const frontend::ParameterDeclaration*> overridable;
    for (const auto& parameter : source.parameters) {
        if (!parameter.local) {
            overridable.push_back(&parameter);
        }
    }
    std::vector<std::optional<std::int64_t>> actuals(
        overridable.size());
    std::vector<std::optional<SystemVerilogConstantValue>>
        systemverilog_actuals(overridable.size());
    std::vector<std::optional<SystemVerilogStringValue>>
        systemverilog_string_actuals(overridable.size());
    std::vector<bool> explicitly_associated(overridable.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    bool saw_positional = false;
    for (const auto& override : overrides) {
        std::optional<std::size_t> actual_index;
        if (override.name) {
            saw_named = true;
            std::vector<
                const frontend::ParameterDeclaration*> matches;
            std::copy_if(
                overridable.begin(),
                overridable.end(),
                std::back_inserter(matches),
                [&](const frontend::ParameterDeclaration* parameter) {
                    return parameter_name_matches(
                        parameter->name,
                        *override.name,
                        source.language,
                        association_language);
                });
            if (matches.size() > 1) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::ambiguous_name),
                    "VHDL generic name '" + *override.name
                        + "' ambiguously matches multiple "
                        "case-sensitive target parameters",
                    override.span});
                continue;
            }
            if (matches.empty()) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    "unknown or local " + std::string{object_kind}
                        + " actual '" + *override.name + "'",
                    override.span});
                continue;
            }
            actual_index = static_cast<std::size_t>(
                std::distance(
                    overridable.begin(),
                    std::find(
                        overridable.begin(),
                        overridable.end(),
                        matches.front())));
        } else {
            saw_positional = true;
            if (association_is_vhdl && saw_named) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::association_order),
                    "a positional generic actual cannot follow a named "
                    "actual",
                    override.span});
            }
            if (next_positional >= overridable.size()) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    "too many positional "
                        + std::string{object_kind} + " actuals",
                    override.span});
                continue;
            }
            actual_index = next_positional++;
        }
        if (actual_index) {
            if (explicitly_associated[*actual_index]) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::duplicate_actual),
                    "duplicate " + std::string{object_kind}
                        + " actual for '"
                        + overridable[*actual_index]->name + "'",
                    override.span});
                continue;
            }
            explicitly_associated[*actual_index] = true;
            if (override.default_box) {
                if (!is_vhdl
                    || !overridable[*actual_index]
                            ->default_value.valid()) {
                    diagnostics.push_back({
                        code(SpecializationDiagnostic::invalid_actual),
                        std::string{object_kind} + " '"
                            + overridable[*actual_index]->name
                            + "' has no default selected by open",
                        override.span});
                    actuals[*actual_index] = 0;
                }
                continue;
            }
            std::string error;
            bool range_error = false;
            auto actual_expression = override.value;
            if (is_verilog) {
                if (overridable[*actual_index]->type.spelling
                    == "string") {
                    if (association_language
                        != frontend::Language::
                            SystemVerilog2017) {
                        diagnostics.push_back({
                            "FSIM-ELAB-SVSTRING-004",
                            "string parameter actuals require a "
                            "same-language SystemVerilog association",
                            override.span});
                        continue;
                    }
                    const auto value =
                        evaluate_systemverilog_string_expression(
                            actual_expression,
                            {},
                            parent_environment,
                            error);
                    if (!value) {
                        diagnostics.push_back({
                            "FSIM-ELAB-SVSTRING-002",
                            "cannot evaluate string parameter actual: "
                                + error,
                            override.span});
                    } else if (
                        systemverilog_string_actuals[
                            *actual_index]) {
                        diagnostics.push_back({
                            code(
                                SpecializationDiagnostic::
                                    duplicate_actual),
                            "duplicate " + std::string{object_kind}
                                + " actual for '"
                                + overridable[*actual_index]->name + "'",
                            override.span});
                    } else {
                        systemverilog_string_actuals[
                            *actual_index] = *value;
                    }
                    continue;
                }
                std::string string_error;
                if (evaluate_systemverilog_string_expression(
                        actual_expression,
                        {},
                        parent_environment,
                        string_error)) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVSTRING-002",
                        "an integral parameter cannot receive a string "
                        "actual",
                        override.span});
                    continue;
                }
                const auto value =
                    evaluate_systemverilog_constant_function_expression(
                        actual_expression,
                        {},
                        parent_environment,
                        source.functions,
                        error);
                if (!value) {
                    diagnostics.push_back({
                        code(
                            SpecializationDiagnostic::
                                actual_evaluation),
                        "cannot evaluate " + std::string{object_kind}
                            + " actual: " + error,
                        override.span});
                    continue;
                }
                if (systemverilog_actuals[*actual_index]) {
                    diagnostics.push_back({
                        code(SpecializationDiagnostic::duplicate_actual),
                        "duplicate " + std::string{object_kind}
                            + " actual for '"
                            + overridable[*actual_index]->name + "'",
                        override.span});
                } else {
                    systemverilog_actuals[*actual_index] = *value;
                }
                continue;
            }
            if (is_vhdl
                && !fold_vhdl_enumeration_attributes(
                    actual_expression,
                    source,
                    parent_environment,
                    error,
                    range_error)) {
                diagnostics.push_back({
                    range_error
                        ? "FSIM-ELAB-VHENUMATTR-002"
                        : "FSIM-ELAB-VHENUMATTR-001",
                    "cannot evaluate enumeration attribute in "
                    + std::string{object_kind}
                    + " actual: " + error,
                    override.span});
                continue;
            }
            if (is_vhdl
                && !fold_vhdl_static_expressions(
                    actual_expression,
                    source,
                    parent_environment,
                    error,
                    range_error)) {
                diagnostics.push_back({
                    range_error
                        ? "FSIM-ELAB-VHSTATIC-002"
                        : "FSIM-ELAB-VHSTATIC-001",
                    "cannot evaluate VHDL static expression in "
                    + std::string{object_kind}
                    + " actual: " + error,
                    override.span});
                continue;
            }
            auto value = is_vhdl
                ? packed_vhdl_static_value(
                      actual_expression,
                      overridable[*actual_index]->type,
                      error)
                : std::optional<std::int64_t>{};
            if (!value && is_vhdl) {
                value = vhdl_enumeration_ordinal(
                    actual_expression,
                    overridable[*actual_index]->type);
            }
            if (!value) {
                value = evaluate_constant_expression(
                    actual_expression, parent_environment, error);
            }
            if (!value && is_vhdl) {
                const auto function_value =
                    evaluate_systemverilog_constant_function_expression(
                        actual_expression,
                        {},
                        parent_environment,
                        source.functions,
                        error);
                if (function_value) {
                    value = function_value->integer_value();
                }
            }
            if (!value) {
                diagnostics.push_back({
                    code(
                        SpecializationDiagnostic::
                            actual_evaluation),
                    "cannot evaluate " + std::string{object_kind}
                        + " actual: " + error,
                    override.span});
                continue;
            }
            if (actuals[*actual_index]) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::duplicate_actual),
                    "duplicate " + std::string{object_kind}
                        + " actual for '"
                        + overridable[*actual_index]->name + "'",
                    override.span});
            } else {
                actuals[*actual_index] = *value;
            }
        }
    }
    if (!association_is_vhdl && saw_named && saw_positional) {
        diagnostics.push_back({
            code(SpecializationDiagnostic::association_order),
            "named and positional parameter overrides cannot be mixed",
            overrides.front().span});
    }

    std::size_t overridable_index = 0;
    ConstantDomainEnvironment domains;
    SystemVerilogConstantEnvironment systemverilog_environment;
    SystemVerilogStringEnvironment
        systemverilog_string_environment;
    for (std::size_t parameter_index = 0;
         parameter_index < source.parameters.size();
         ++parameter_index) {
        const auto& parameter =
            source.parameters[parameter_index];
        auto& specialized_parameter =
            result.unit.parameters[parameter_index];
        substitute_parameters(
            specialized_parameter.type,
            result.environment,
            domains,
            diagnostics,
            source.language);
        const auto& parameter_type =
            specialized_parameter.type;
        std::optional<SystemVerilogConstantValue>
            systemverilog_value;
        std::optional<SystemVerilogStringValue>
            systemverilog_string_value;
        std::optional<std::int64_t> value;
        if (!parameter.local) {
            if (is_verilog) {
                if (parameter_type.spelling == "string") {
                    systemverilog_string_value =
                        systemverilog_string_actuals.at(
                            overridable_index++);
                } else {
                    systemverilog_value =
                        systemverilog_actuals.at(
                            overridable_index++);
                }
            } else {
                value = actuals.at(overridable_index++);
            }
        }
        if (is_verilog
            && parameter_type.spelling == "string") {
            if (!systemverilog_string_value) {
                if (parameter.default_value.kind
                    == ExpressionKind::Invalid) {
                    diagnostics.push_back({
                        code(
                            SpecializationDiagnostic::
                                invalid_actual),
                        std::string{object_kind} + " '"
                            + parameter.name
                            + "' requires an actual because it has no "
                              "default",
                        parameter.span});
                    systemverilog_string_value =
                        SystemVerilogStringValue{{}, parameter.span};
                } else {
                    std::string error;
                    auto default_expression =
                        parameter.default_value;
                    substitute_systemverilog_strings(
                        default_expression,
                        systemverilog_string_environment,
                        result.environment);
                    systemverilog_string_value =
                        evaluate_systemverilog_string_expression(
                            default_expression,
                            systemverilog_string_environment,
                            result.environment,
                            error);
                    if (!systemverilog_string_value) {
                        diagnostics.push_back({
                            "FSIM-ELAB-SVSTRING-001",
                            "cannot evaluate default for "
                                + std::string{object_kind} + " '"
                                + parameter.name + "': " + error,
                            parameter.span});
                        systemverilog_string_value =
                            SystemVerilogStringValue{
                                {}, parameter.span};
                    }
                }
            }
            systemverilog_string_environment[parameter.name] =
                *systemverilog_string_value;
            result.values.emplace_back(
                parameter.name,
                systemverilog_string_value->display());
            result.identity_values.emplace_back(
                parameter.name,
                systemverilog_string_value->canonical());
            continue;
        }
        if (is_verilog && !systemverilog_value) {
            if (parameter.default_value.kind
                == ExpressionKind::Invalid) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    std::string{object_kind} + " '"
                        + parameter.name
                        + "' requires an actual because it has no default",
                    parameter.span});
                systemverilog_value =
                    SystemVerilogConstantValue{
                        0, 0, 0, 32, true, true, parameter.span};
            } else {
                std::string error;
                auto default_expression =
                    parameter.default_value;
                substitute_systemverilog_strings(
                    default_expression,
                    systemverilog_string_environment,
                    result.environment);
                systemverilog_value =
                    evaluate_systemverilog_constant_function_expression(
                        default_expression,
                        systemverilog_environment,
                        result.environment,
                        source.functions,
                        error);
                if (!systemverilog_value) {
                    diagnostics.push_back({
                        code(
                            SpecializationDiagnostic::
                                default_evaluation),
                        "cannot evaluate default for "
                            + std::string{object_kind} + " '"
                            + parameter.name + "': " + error,
                        parameter.span});
                    systemverilog_value =
                        SystemVerilogConstantValue{
                            0, 0, 0, 32, true, true, parameter.span};
                }
            }
        } else if (!is_verilog && !value) {
            if (parameter.default_value.kind
                == ExpressionKind::Invalid) {
                diagnostics.push_back({
                    code(SpecializationDiagnostic::invalid_actual),
                    std::string{object_kind} + " '"
                        + parameter.name
                        + "' requires an actual because it has no default",
                    parameter.span});
                value = 0;
            } else {
                std::string error;
                bool range_error = false;
                auto default_expression =
                    parameter.default_value;
                if (is_vhdl
                    && !fold_vhdl_enumeration_attributes(
                        default_expression,
                        source,
                        result.environment,
                        error,
                        range_error)) {
                    diagnostics.push_back({
                        range_error
                            ? "FSIM-ELAB-VHENUMATTR-002"
                            : "FSIM-ELAB-VHENUMATTR-001",
                        "cannot evaluate enumeration attribute in "
                        + std::string{object_kind} + " '"
                        + parameter.name + "': " + error,
                        parameter.span});
                    value = 0;
                }
                if (is_vhdl && !value
                    && !fold_vhdl_static_expressions(
                        default_expression,
                        source,
                        result.environment,
                        error,
                        range_error)) {
                    diagnostics.push_back({
                        range_error
                            ? "FSIM-ELAB-VHSTATIC-002"
                            : "FSIM-ELAB-VHSTATIC-001",
                        "cannot evaluate VHDL static expression in "
                        + std::string{object_kind} + " '"
                        + parameter.name + "': " + error,
                        parameter.span});
                    value = 0;
                }
                if (!value) {
                    value = is_vhdl
                        ? packed_vhdl_static_value(
                              default_expression,
                              parameter_type,
                              error)
                        : std::optional<std::int64_t>{};
                    if (!value && is_vhdl) {
                        value = vhdl_enumeration_ordinal(
                            default_expression, parameter_type);
                    }
                    if (!value) {
                        value = evaluate_constant_expression(
                            default_expression,
                            result.environment,
                            error);
                    }
                    if (!value) {
                        const auto function_value =
                            evaluate_systemverilog_constant_function_expression(
                                default_expression,
                                {},
                                result.environment,
                                result.unit.functions,
                                error);
                        if (function_value) {
                            value =
                                function_value->integer_value();
                        }
                    }
                    if (!value) {
                        diagnostics.push_back({
                            code(
                                SpecializationDiagnostic::
                                    default_evaluation),
                            "cannot evaluate default for "
                                + std::string{object_kind} + " '"
                                + parameter.name + "': " + error,
                            parameter.span});
                        value = 0;
                    }
                }
            }
        }
        const bool enum_literal_parameter =
            is_verilog
            && std::ranges::any_of(
                source.type_aliases,
                [&](const auto& alias) {
                  return std::ranges::any_of(
                      alias.enum_literals,
                      [&](const auto& literal) {
                        return literal.name == parameter.name
                            && literal.span.source_name
                                == parameter.span.source_name
                            && literal.span.begin.offset
                                == parameter.span.begin.offset;
                      });
                });
        if (is_verilog) {
            if (!enum_literal_parameter) {
                std::string conversion_error;
                const auto converted =
                    convert_systemverilog_parameter_value(
                        *systemverilog_value,
                        parameter_type,
                        conversion_error);
                if (!converted) {
                    diagnostics.push_back({
                        "FSIM-ELAB-SVCONST-001",
                        "cannot convert " + std::string{object_kind}
                            + " '" + parameter.name + "': "
                            + conversion_error,
                        parameter.span});
                    systemverilog_value =
                        SystemVerilogConstantValue{
                            0,
                            0,
                            0,
                            static_cast<std::uint32_t>(
                                parameter_type.width().value_or(32)),
                            parameter_type.is_signed,
                            false,
                            parameter.span};
                } else {
                    systemverilog_value = *converted;
                }
            }
            systemverilog_environment[parameter.name] =
                *systemverilog_value;
            if (const auto integer =
                    systemverilog_value->integer_value()) {
                result.environment[parameter.name] = *integer;
            }
            domains[parameter.name] = ConstantTypeInfo{
                parameter_type.domain,
                false,
                parameter_type.nominal_type};
            result.values.emplace_back(
                parameter.name,
                systemverilog_value->display());
            result.identity_values.emplace_back(
                parameter.name,
                systemverilog_value->canonical());
            continue;
        }
        if (is_vhdl) {
            const auto spelling = parameter_type.spelling;
            const bool enumeration =
                !parameter_type.enumeration_literals.empty();
            const bool supported_packed =
                parameter_type.packed_range
                && parameter_type.packed_members.empty()
                && parameter_type.width().value_or(0) <= 64
                && (parameter_type.domain
                        == frontend::ValueDomain::Bit2
                    || parameter_type.domain
                        == frontend::ValueDomain::Logic4
                    || parameter_type.domain
                        == frontend::ValueDomain::Logic9);
            const bool violates_supported_type =
                (parameter_type.packed_range && !supported_packed)
                || !parameter_type.packed_members.empty()
                || (!parameter_type.packed_range
                    && parameter_type.domain
                        != frontend::ValueDomain::Integer
                    && parameter_type.domain
                        != frontend::ValueDomain::Boolean
                    && parameter_type.domain
                        != frontend::ValueDomain::Bit2);
            const bool violates_natural =
                spelling == "natural" && *value < 0;
            const bool violates_positive =
                spelling == "positive" && *value <= 0;
            const bool violates_boolean =
                parameter_type.domain
                    == frontend::ValueDomain::Boolean
                && *value != 0 && *value != 1;
            const bool violates_bit =
                parameter_type.domain == frontend::ValueDomain::Bit2
                && !enumeration
                && !parameter_type.packed_range
                && *value != 0 && *value != 1;
            const auto packed_width =
                parameter_type.width().value_or(0);
            const bool violates_packed =
                supported_packed
                && (*value < 0
                    || (packed_width < 64
                        && static_cast<std::uint64_t>(*value)
                            >= (std::uint64_t{1} << packed_width)));
            const bool violates_enumeration =
                enumeration
                && (*value < 0
                    || static_cast<std::uint64_t>(*value)
                        >= parameter_type
                               .enumeration_literals.size());
            const bool violates_enumeration_range =
                enumeration
                && parameter_type.enumeration_range
                && !parameter_type.enumeration_range
                        ->contains(*value);
            const bool violates_integer_range =
                parameter_type.domain
                    == frontend::ValueDomain::Integer
                && parameter_type.integer_range
                && (*value
                        < std::min(
                            parameter_type.integer_range->left,
                            parameter_type.integer_range->right)
                    || *value
                        > std::max(
                            parameter_type.integer_range->left,
                            parameter_type.integer_range->right));
            if (violates_supported_type) {
                diagnostics.push_back({
                    code(
                        SpecializationDiagnostic::
                            subtype_constraint),
                    std::string{object_kind} + " '"
                        + parameter.name
                        + "' resolves outside the bounded scalar or "
                          "up-to-64-bit packed subtype set",
                    parameter.span});
            }
            if (violates_natural || violates_positive
                || violates_boolean || violates_bit
                || violates_packed
                || violates_integer_range
                || violates_enumeration
                || violates_enumeration_range) {
                diagnostics.push_back({
                    code(
                        SpecializationDiagnostic::
                            subtype_constraint),
                    std::string{object_kind} + " '"
                        + parameter.name
                        + "' value is outside subtype '"
                        + parameter_type.spelling + "'",
                    parameter.span});
            }
        }
        result.environment[parameter.name] = *value;
        domains[parameter.name] = ConstantTypeInfo{
            parameter_type.packed_range
                ? frontend::ValueDomain::Integer
                : parameter_type.domain,
            is_vhdl
                && !parameter_type.enumeration_literals.empty(),
            parameter_type.nominal_type};
        result.values.emplace_back(
            parameter.name,
            is_vhdl
                    && parameter_type.domain
                        == frontend::ValueDomain::Boolean
                ? (*value == 0 ? "false" : "true")
                : is_vhdl
                        && !parameter_type
                                .enumeration_literals.empty()
                        && *value >= 0
                        && static_cast<std::uint64_t>(*value)
                            < parameter_type
                                  .enumeration_literals.size()
                    ? parameter_type.enumeration_literals[
                          static_cast<std::size_t>(*value)]
                : std::to_string(*value));
    }

    if (is_verilog) {
        substitute_systemverilog_parameters(
            result.unit, systemverilog_environment);
        fold_systemverilog_constant_functions(
            result.unit,
            systemverilog_environment,
            result.environment,
            diagnostics);
        substitute_systemverilog_strings(
            result.unit,
            systemverilog_string_environment,
            result.environment,
            diagnostics);
        prepare_systemverilog_generate_regions(
            result.unit.generate_regions,
            systemverilog_environment,
            systemverilog_string_environment,
            result.environment,
            domains,
            diagnostics);
        result.string_environment =
            std::move(systemverilog_string_environment);
    } else if (is_vhdl) {
        fold_vhdl_static_type_expressions(
            result.unit, result.environment, diagnostics);
        // The bounded VHDL and SystemVerilog function HIR intentionally
        // shares the same time-free integral evaluator. Calls whose operands
        // are not locally static remain in the tree for runtime lowering.
        fold_systemverilog_constant_functions(
            result.unit,
            {},
            result.environment,
            diagnostics);
    }
    for (auto& parameter : result.unit.parameters) {
        substitute_parameters(
            parameter.type,
            result.environment,
            domains,
            diagnostics,
            source.language);
        substitute_parameters(
            parameter.default_value,
            result.environment,
            domains,
            source.language);
    }
    for (auto& alias : result.unit.type_aliases) {
        substitute_parameters(
            alias.type,
            result.environment,
            domains,
            diagnostics,
            source.language);
        if (alias.enum_literals.empty()
            || alias.declaration_kind
                == frontend::TypeDeclarationKind::
                    VhdlEnumeration) {
            continue;
        }
        const auto width = alias.type.width();
        if (!width || *width == 0 || *width > 64) {
            diagnostics.push_back({
                "FSIM-ELAB-SVENUM-001",
                "enum '" + alias.name
                    + "' has an unsupported base width",
                alias.span});
            continue;
        }
        std::unordered_map<std::uint64_t, std::string>
            enum_values;
        for (const auto& literal : alias.enum_literals) {
            const auto value =
                systemverilog_environment.find(literal.name);
            if (value == systemverilog_environment.end()) {
                continue;
            }
            bool in_range = false;
            std::uint64_t normalized_bits = 0;
            const auto base_mask =
                *width == 64
                    ? std::numeric_limits<std::uint64_t>::max()
                    : (std::uint64_t{1} << *width) - 1U;
            if (alias.type.is_signed) {
                const auto integer =
                    value->second.integer_value();
                if (integer && *width == 64) {
                    in_range = true;
                } else if (integer) {
                    const auto limit =
                        std::int64_t{1} << (*width - 1U);
                    in_range =
                        *integer >= -limit
                        && *integer < limit;
                }
                if (integer) {
                    normalized_bits =
                        static_cast<std::uint64_t>(*integer)
                        & base_mask;
                }
            } else if (value->second.known()) {
                if (value->second.is_signed) {
                    const auto integer =
                        value->second.integer_value();
                    in_range = integer && *integer >= 0
                        && (*width == 64
                            || static_cast<std::uint64_t>(*integer)
                                <= base_mask);
                    if (integer && *integer >= 0) {
                        normalized_bits =
                            static_cast<std::uint64_t>(*integer)
                            & base_mask;
                    }
                } else {
                    normalized_bits =
                        value->second.bits
                        & value->second.mask();
                    in_range =
                        *width == 64
                        || normalized_bits <= base_mask;
                    normalized_bits &= base_mask;
                }
            }
            if (!in_range) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVENUM-001",
                    "enum literal '" + literal.name
                        + "' does not fit the base type of '"
                        + alias.name + "'",
                    literal.span});
            }
            const auto [duplicate, inserted] =
                enum_values.emplace(
                    normalized_bits, literal.name);
            if (!inserted) {
                diagnostics.push_back({
                    "FSIM-ELAB-SVENUM-002",
                    "enum literals '" + duplicate->second
                        + "' and '" + literal.name
                        + "' have the same value",
                    literal.span});
            }
        }
    }
    for (auto& port : result.unit.ports) {
        substitute_parameters(
            port,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& signal : result.unit.signals) {
        substitute_parameters(
            signal,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& variable : result.unit.variables) {
        substitute_parameters(
            variable,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& function : result.unit.functions) {
        substitute_parameters(
            function.return_type,
            result.environment,
            domains,
            diagnostics,
            source.language);
        for (auto& argument : function.arguments) {
            substitute_parameters(
                argument.type,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        for (auto& variable : function.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            function.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& task : result.unit.tasks) {
        for (auto& argument : task.arguments) {
            substitute_parameters(
                argument.type,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        for (auto& variable : task.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            task.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& procedure : result.unit.procedures) {
        for (auto& argument : procedure.arguments) {
            substitute_parameters(
                argument.type,
                result.environment,
                domains,
                diagnostics,
                source.language);
            if (argument.default_value) {
                substitute_parameters(
                    *argument.default_value,
                    result.environment,
                    domains,
                    source.language);
            }
        }
        for (auto& variable : procedure.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            procedure.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    const auto substitute_generic_parameters =
        [&](auto& parameters) {
          for (auto& parameter : parameters) {
            if (parameter.kind
                    == frontend::ParameterKind::Type
                && parameter.default_type) {
              substitute_parameters(
                  *parameter.default_type,
                  result.environment,
                  domains,
                  diagnostics,
                  source.language);
            } else if (
                parameter.kind
                    == frontend::ParameterKind::Function
                && parameter.function_profile) {
              substitute_parameters(
                  parameter.function_profile->return_type,
                  result.environment,
                  domains,
                  diagnostics,
                  source.language);
              for (auto& argument :
                   parameter.function_profile->arguments) {
                substitute_parameters(
                    argument.type,
                    result.environment,
                    domains,
                    diagnostics,
                    source.language);
              }
            } else if (
                parameter.kind
                    == frontend::ParameterKind::Procedure
                && parameter.procedure_profile) {
              for (auto& argument :
                   parameter.procedure_profile->arguments) {
                substitute_parameters(
                    argument.type,
                    result.environment,
                    domains,
                    diagnostics,
                    source.language);
              }
            } else {
              substitute_parameters(
                  parameter.type,
                  result.environment,
                  domains,
                  diagnostics,
                  source.language);
              substitute_parameters(
                  parameter.default_value,
                  result.environment,
                  domains,
                  source.language);
            }
          }
        };
    for (auto& generic :
         result.unit.generic_function_templates) {
        substitute_generic_parameters(
            generic.generic_parameters);
        auto& function = generic.function;
        substitute_parameters(
            function.return_type,
            result.environment,
            domains,
            diagnostics,
            source.language);
        for (auto& argument : function.arguments) {
            substitute_parameters(
                argument.type,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        for (auto& variable : function.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            function.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& generic :
         result.unit.generic_procedure_templates) {
        substitute_generic_parameters(
            generic.generic_parameters);
        auto& procedure = generic.procedure;
        for (auto& argument : procedure.arguments) {
            substitute_parameters(
                argument.type,
                result.environment,
                domains,
                diagnostics,
                source.language);
            if (argument.default_value) {
                substitute_parameters(
                    *argument.default_value,
                    result.environment,
                    domains,
                    source.language);
            }
        }
        for (auto& variable : procedure.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            procedure.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    const auto substitute_generic_maps =
        [&](auto& instances) {
          for (auto& instance : instances) {
            for (auto& actual : instance.generic_map) {
              substitute_parameters(
                  actual.value,
                  result.environment,
                  domains,
                  source.language);
              if (actual.type_value) {
                substitute_parameters(
                    *actual.type_value,
                    result.environment,
                    domains,
                    diagnostics,
                    source.language);
              }
            }
          }
        };
    substitute_generic_maps(
        result.unit.generic_function_instances);
    substitute_generic_maps(
        result.unit.generic_procedure_instances);
    substitute_parameters(
        result.unit.concurrent_statements,
        result.environment,
        domains,
        diagnostics,
        source.language);
    for (auto& process : result.unit.processes) {
        for (auto& variable : process.variables) {
            substitute_parameters(
                variable,
                result.environment,
                domains,
                diagnostics,
                source.language);
        }
        substitute_parameters(
            process.statements,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    substitute_parameters(
        result.unit.instances,
        result.environment,
        domains,
        source.language);
    substitute_parameters(
        result.unit.generate_regions,
        result.environment,
        domains,
        source.language,
        diagnostics);
    result.domains = domains;
    if (expand_generates) {
        expand_specialized_unit_generates(result, diagnostics);
    }
    return result;
}

void expand_specialized_unit_generates(
    SpecializedUnit& specialized,
    std::vector<Diagnostic>& diagnostics,
    const VhdlBlockInterfacePreparer* block_preparer) {
    expand_generate_regions(
        specialized.unit.generate_regions,
        specialized.environment,
        specialized.domains,
        specialized.unit.language,
        {},
        {},
        specialized.unit,
        diagnostics,
        block_preparer);
    specialized.unit.generate_regions.clear();
    apply_net_delays(specialized.unit, diagnostics);
    std::vector<frontend::Instance> expanded_instances;
    for (auto& instance : specialized.unit.instances) {
      if (instance.array_indices.empty()) {
        expanded_instances.push_back(std::move(instance));
        continue;
      }
      for (const auto index : instance.array_indices) {
        auto expanded = instance;
        expanded.name += "[" + std::to_string(index) + "]";
        expanded.array_indices.clear();
        expanded_instances.push_back(std::move(expanded));
      }
    }
    specialized.unit.instances = std::move(expanded_instances);
}



bool valid_systemc_construction_value(
    const fsim_sc_construction_type_v1 type,
    const std::int64_t value) {
    switch (type) {
    case FSIM_SC_CONSTRUCTION_INTEGER:
        return true;
    case FSIM_SC_CONSTRUCTION_NATURAL:
        return value >= 0;
    case FSIM_SC_CONSTRUCTION_POSITIVE:
        return value > 0;
    case FSIM_SC_CONSTRUCTION_BOOLEAN:
    case FSIM_SC_CONSTRUCTION_BIT:
        return value == 0 || value == 1;
    }
    return false;
}



std::optional<std::vector<std::pair<std::string, std::int64_t>>>
specialize_systemc_construction(
    const std::vector<SystemCConstructionParameter>& schema,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
    const auto initial_diagnostic_count = diagnostics.size();
    const bool vhdl_association =
        association_language == frontend::Language::Vhdl2008;
    std::vector<std::optional<std::int64_t>> actuals(schema.size());
    std::size_t next_positional = 0;
    bool saw_named = false;
    bool saw_positional = false;
    for (const auto& override : overrides) {
        std::string evaluation_error;
        const auto value = evaluate_constant_expression(
            override.value, parent_environment, evaluation_error);
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-004",
                "cannot evaluate SystemC construction actual: "
                    + evaluation_error,
                override.span});
            continue;
        }
        std::optional<std::size_t> index;
        if (override.name) {
            saw_named = true;
            std::vector<std::size_t> matches;
            for (std::size_t candidate = 0;
                 candidate < schema.size();
                 ++candidate) {
                const auto matches_name =
                    vhdl_association
                    ? parameter_name_matches(
                          schema[candidate].name,
                          *override.name,
                          frontend::Language::SystemVerilog2017,
                          association_language)
                    : schema[candidate].name == *override.name;
                if (matches_name) {
                    matches.push_back(candidate);
                }
            }
            if (matches.size() > 1) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-006",
                    "VHDL generic name '" + *override.name
                        + "' ambiguously matches multiple case-sensitive "
                          "SystemC construction parameters",
                    override.span});
                continue;
            }
            if (matches.empty()) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-001",
                    "unknown SystemC construction parameter '"
                        + *override.name + "'",
                    override.span});
                continue;
            }
            index = matches.front();
        } else {
            saw_positional = true;
            if (vhdl_association && saw_named) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-003",
                    "a positional SystemC construction actual cannot "
                    "follow a named VHDL actual",
                    override.span});
            }
            if (next_positional >= schema.size()) {
                diagnostics.push_back({
                    "FSIM-ELAB-SC-PARAM-001",
                    "too many positional SystemC construction actuals",
                    override.span});
                continue;
            }
            index = next_positional++;
        }
        if (actuals[*index]) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-002",
                "duplicate SystemC construction actual for '"
                    + schema[*index].name + "'",
                override.span});
        } else {
            actuals[*index] = *value;
        }
    }
    if (!vhdl_association && saw_named && saw_positional) {
        diagnostics.push_back({
            "FSIM-ELAB-SC-PARAM-003",
            "named and positional SystemC construction actuals cannot "
            "be mixed",
            overrides.empty() ? frontend::SourceSpan{}
                              : overrides.front().span});
    }

    std::vector<std::pair<std::string, std::int64_t>> values;
    values.reserve(schema.size());
    for (std::size_t index = 0; index < schema.size(); ++index) {
        const auto value =
            actuals[index].has_value()
            ? actuals[index]
            : schema[index].default_value;
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-001",
                "SystemC construction parameter '"
                    + schema[index].name + "' requires an actual",
                {}});
            continue;
        }
        if (!valid_systemc_construction_value(
                schema[index].type, *value)) {
            diagnostics.push_back({
                "FSIM-ELAB-SC-PARAM-005",
                "SystemC construction parameter '"
                    + schema[index].name
                    + "' violates its declared scalar subtype",
                {}});
            continue;
        }
        values.emplace_back(schema[index].name, *value);
    }
    if (diagnostics.size() != initial_diagnostic_count) {
        return std::nullopt;
    }
    return values;
}

} // namespace fsim::elaboration::elaboration_detail
