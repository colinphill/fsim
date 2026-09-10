// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

void evaluate_generated_constants(
    frontend::GenerateBody& body,
    ConstantEnvironment& environment,
    ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::vector<frontend::FunctionDeclaration>& functions,
    std::vector<Diagnostic>& diagnostics) {
    if (language == frontend::Language::Vhdl2008) {
        // Type constraints in a generated declarative region may refer to a
        // scalar constant declared earlier in that same region.  The parsed
        // representation stores constants and type declarations separately,
        // so seed the successfully evaluable scalar constants before type
        // substitution.  The main pass below still owns diagnostics and the
        // final, declaration-complete values.
        for (const auto& constant : body.constants) {
            if (constant.type.vhdl_array
                || !constant.type.packed_members.empty()) {
                continue;
            }
            auto value_expression = constant.default_value;
            substitute_parameters(
                value_expression, environment, domains, language);
            std::string error;
            const auto value = evaluate_constant_expression(
                value_expression, environment, error);
            if (!value) {
                continue;
            }
            environment.insert_or_assign(constant.name, *value);
            domains.insert_or_assign(
                constant.name,
                ConstantTypeInfo{
                    constant.type.domain,
                    !constant.type.enumeration_literals.empty(),
                    constant.type.nominal_type});
        }
        for (auto& alias : body.type_aliases) {
            substitute_parameters(
                alias.type,
                environment,
                domains,
                diagnostics,
                language);
        }
        const auto simple_name = [](const std::string_view name) {
            const auto separator = name.find_last_of('.');
            return name.substr(
                separator == std::string_view::npos
                    ? 0U : separator + 1U);
        };
        const auto resolve_type =
            [&](auto&& self, frontend::Type& type) -> void {
              const auto reference = type.named_type.empty()
                  ? std::string_view{type.spelling}
                  : std::string_view{type.named_type};
              const auto alias = std::ranges::find_if(
                  body.type_aliases,
                  [&](const auto& candidate) {
                    return simple_name(candidate.name)
                        == simple_name(reference);
                  });
              if (alias != body.type_aliases.end()
                  && (!type.vhdl_array
                      || type.width().value_or(0U) == 0U)
                  && type.packed_members.empty()) {
                  type = alias->type;
              }
              if (type.vhdl_array) {
                  for (auto& element :
                       type.vhdl_array->element_types) {
                      self(self, element);
                  }
              }
              for (auto& member : type.packed_members) {
                  for (auto& nested : member.nested_types) {
                      self(self, nested);
                  }
              }
            };
        const auto resolve_function =
            [&](auto&& self,
                frontend::FunctionDeclaration& function) -> void {
              resolve_type(resolve_type, function.return_type);
              for (auto& argument : function.arguments) {
                  resolve_type(resolve_type, argument.type);
              }
              for (auto& variable : function.variables) {
                  resolve_type(resolve_type, variable.type);
              }
              for (auto& constant : function.constants) {
                  resolve_type(resolve_type, constant.type);
              }
              for (auto& nested : function.functions) {
                  self(self, nested);
              }
            };
        for (auto& function : body.functions) {
            resolve_function(resolve_function, function);
        }
    }
    auto available_functions = functions;
    available_functions.insert(
        available_functions.end(),
        body.functions.begin(),
        body.functions.end());
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
        std::string error;
        const bool vhdl_composite =
            language == frontend::Language::Vhdl2008
            && (constant.type.vhdl_array
                || !constant.type.packed_members.empty()
                || (constant.type.packed_range
                    && constant.type.domain
                        != frontend::ValueDomain::Integer));
        std::optional<SystemVerilogConstantValue> function_value;
        std::string function_error;
        if (language == frontend::Language::Vhdl2008) {
            function_value =
                evaluate_systemverilog_constant_function_expression(
                    constant.default_value,
                    { },
                    environment,
                    available_functions,
                    function_error);
        }
        if (vhdl_composite && function_value) {
            auto nominal_type = constant.type.nominal_type;
            const auto constant_name = constant.type.named_type.empty()
                ? std::string_view{constant.type.spelling}
                : std::string_view{constant.type.named_type};
            const auto separator = constant_name.find_last_of('.');
            const auto constant_simple = constant_name.substr(
                separator == std::string_view::npos
                    ? 0U : separator + 1U);
            if (const auto alias = std::ranges::find_if(
                    body.type_aliases,
                    [&](const auto& candidate) {
                      const auto candidate_separator =
                          candidate.name.find_last_of('.');
                      return std::string_view{candidate.name}.substr(
                          candidate_separator == std::string::npos
                              ? 0U : candidate_separator + 1U)
                          == constant_simple;
                    });
                alias != body.type_aliases.end()
                && !alias->type.nominal_type.empty()) {
                nominal_type = alias->type.nominal_type;
            }
            auto info = ConstantTypeInfo{
                constant.type.domain,
                false,
                std::move(nominal_type)};
            auto composite_value = function_value->expression(
                constant.default_value.span);
            composite_value.call_result_width =
                constant.type.width().value_or(0U);
            composite_value.call_result_domain =
                constant.type.domain;
            composite_value.call_result_signed =
                constant.type.is_signed;
            info.vhdl_composite_value =
                std::move(composite_value);
            domains.insert_or_assign(
                constant.name, std::move(info));
            continue;
        }
        auto value = function_value
            ? function_value->integer_value()
            : evaluate_constant_expression(
                constant.default_value, environment, error);
        if (!value && !function_error.empty()) {
            error = std::move(function_error);
        }
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-011",
                "cannot evaluate generated "
                    + std::string{
                        language == frontend::Language::Vhdl2008
                            ? "constant"
                            : "parameter"}
                    + " '" + constant.name + "': " + error,
                constant.span});
            value = 0;
        }
        if (language != frontend::Language::Vhdl2008) {
            *value = normalize_systemverilog_parameter_value(
                *value, constant.type);
        }
        const bool exceeds_word =
            constant.type.packed_range
            && constant.type.packed_range->width() > 64;
        if (exceeds_word) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-012",
                "generated "
                    + std::string{
                        language == frontend::Language::Vhdl2008
                            ? "constant"
                            : "parameter"}
                    + " '" + constant.name
                    + "' exceeds the bounded 64-bit integral width",
                constant.span});
        } else if (language == frontend::Language::Vhdl2008) {
            const auto& spelling = constant.type.spelling;
            const bool violates_natural =
                spelling == "natural" && *value < 0;
            const bool violates_positive =
                spelling == "positive" && *value <= 0;
            const bool violates_boolean =
                constant.type.domain == frontend::ValueDomain::Boolean
                && *value != 0 && *value != 1;
            const bool violates_bit =
                constant.type.domain == frontend::ValueDomain::Bit2
                && *value != 0 && *value != 1;
            if (violates_natural || violates_positive
                || violates_boolean || violates_bit) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-012",
                    "generated constant '" + constant.name
                        + "' value is outside subtype '"
                        + constant.type.spelling + "'",
                    constant.span});
            }
        }
        environment[constant.name] = *value;
        domains[constant.name] = ConstantTypeInfo{
            constant.type.domain,
            language == frontend::Language::Vhdl2008
                && !constant.type.enumeration_literals.empty(),
            constant.type.nominal_type};
    }
}

} // namespace fsim::elaboration::elaboration_detail
