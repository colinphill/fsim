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



SpecializedUnit specialize_unit(
    const DesignUnit& source,
    const std::vector<frontend::ParameterOverride>& overrides,
    const ConstantEnvironment& parent_environment,
    const frontend::Language association_language,
    std::vector<Diagnostic>& diagnostics) {
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
            std::string error;
            bool range_error = false;
            auto actual_expression = override.value;
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
            auto value =
                is_vhdl
                    ? vhdl_enumeration_ordinal(
                          actual_expression,
                          overridable[*actual_index]->type)
                    : std::optional<std::int64_t>{};
            if (!value) {
                value = evaluate_constant_expression(
                    actual_expression, parent_environment, error);
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
        std::optional<std::int64_t> value;
        if (!parameter.local) {
            value = actuals.at(overridable_index++);
        }
        if (!value) {
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
                if (!value) {
                    value =
                        is_vhdl
                            ? vhdl_enumeration_ordinal(
                                  default_expression,
                                  parameter_type)
                            : std::optional<std::int64_t>{};
                    if (!value) {
                        value = evaluate_constant_expression(
                            default_expression,
                            result.environment,
                            error);
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
        if (is_vhdl) {
            const auto spelling = parameter_type.spelling;
            const bool enumeration =
                !parameter_type.enumeration_literals.empty();
            const bool violates_supported_type =
                (!enumeration
                 && parameter_type.packed_range.has_value())
                || !parameter_type.packed_members.empty()
                || (parameter_type.domain
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
                && *value != 0 && *value != 1;
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
                        + "' resolves outside the bounded scalar integer, "
                          "Boolean, or bit subtype set",
                    parameter.span});
            }
            if (violates_natural || violates_positive
                || violates_boolean || violates_bit
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
            parameter_type.domain,
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
        std::unordered_map<std::int64_t, std::string>
            enum_values;
        for (const auto& literal : alias.enum_literals) {
            const auto value =
                result.environment.find(literal.name);
            if (value == result.environment.end()) {
                continue;
            }
            bool in_range = false;
            if (alias.type.is_signed) {
                if (*width == 64) {
                    in_range = true;
                } else {
                    const auto limit =
                        std::int64_t{1} << (*width - 1U);
                    in_range =
                        value->second >= -limit
                        && value->second < limit;
                }
            } else if (value->second >= 0) {
                in_range =
                    *width == 64
                    || static_cast<std::uint64_t>(
                           value->second)
                        < (std::uint64_t{1} << *width);
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
                    value->second, literal.name);
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
            port.type,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
    for (auto& signal : result.unit.signals) {
        substitute_parameters(
            signal.type,
            result.environment,
            domains,
            diagnostics,
            source.language);
    }
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
    expand_generate_regions(
        result.unit.generate_regions,
        result.environment,
        domains,
        source.language,
        {},
        {},
        result.unit,
        diagnostics);
    result.unit.generate_regions.clear();
    return result;
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
