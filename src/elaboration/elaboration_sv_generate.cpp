// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
namespace {

using Value = SystemVerilogConstantValue;

[[nodiscard]] std::uint64_t mask_for(
    const std::uint32_t width) noexcept {
    return width >= 64
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t{1} << width) - 1U;
}

[[nodiscard]] std::uint64_t extended_bits(
    const Value& value,
    const std::uint32_t width,
    const bool signed_context) noexcept {
    auto bits = value.bits & value.mask();
    if (signed_context && value.is_signed && width > value.width
        && (bits & (std::uint64_t{1} << (value.width - 1U))) != 0) {
        bits |= mask_for(width) & ~value.mask();
    }
    return bits & mask_for(width);
}

[[nodiscard]] bool equal_known(
    const Value& left,
    const Value& right) noexcept {
    if (!left.known() || !right.known()) {
        return false;
    }
    const auto width = std::max(left.width, right.width);
    const bool signed_context =
        left.is_signed && right.is_signed;
    return extended_bits(left, width, signed_context)
        == extended_bits(right, width, signed_context);
}

void set_integer_expression(
    Expression& expression,
    const std::int64_t value) {
    expression = constant_expression(
        value,
        expression.span,
        frontend::ValueDomain::Integer,
        frontend::Language::SystemVerilog2017);
}

[[nodiscard]] std::optional<Value> evaluate(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    const std::string_view description,
    const std::string_view code,
    std::vector<Diagnostic>& diagnostics) {
    std::string error;
    const auto value =
        evaluate_systemverilog_constant_expression(
            expression, environment, integer_environment, error);
    if (!value) {
        diagnostics.push_back({
            std::string{code},
            "cannot evaluate " + std::string{description}
                + ": " + error,
            expression.span});
    }
    return value;
}

void prepare_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment,
    const SystemVerilogStringEnvironment& string_environment,
    const ConstantEnvironment& integer_environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics);

void prepare_body(
    frontend::GenerateBody& body,
    const SystemVerilogConstantEnvironment& inherited_environment,
    const SystemVerilogStringEnvironment&
        inherited_string_environment,
    const ConstantEnvironment& inherited_integer_environment,
    const ConstantDomainEnvironment& inherited_domains,
    std::vector<Diagnostic>& diagnostics) {
    auto environment = inherited_environment;
    auto string_environment = inherited_string_environment;
    auto integer_environment = inherited_integer_environment;
    auto domains = inherited_domains;
    for (auto& constant : body.constants) {
        substitute_parameters(
            constant.type,
            integer_environment,
            domains,
            diagnostics,
            frontend::Language::SystemVerilog2017);
        substitute_systemverilog_parameters(
            constant.default_value, environment);
        substitute_systemverilog_strings(
            constant.default_value,
            string_environment,
            integer_environment);
        if (constant.type.spelling == "string") {
            std::string error;
            const auto value =
                evaluate_systemverilog_string_expression(
                    constant.default_value,
                    string_environment,
                    integer_environment,
                    error);
            if (!value) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-011",
                    "cannot evaluate generated string parameter '"
                        + constant.name + "': " + error,
                    constant.span});
            } else {
                string_environment[constant.name] = *value;
            }
            continue;
        }
        const auto evaluated = evaluate(
            constant.default_value,
            environment,
            integer_environment,
            "generated parameter '" + constant.name + "'",
            "FSIM-ELAB-GEN-011",
            diagnostics);
        if (!evaluated) {
            continue;
        }
        std::string error;
        const auto converted =
            convert_systemverilog_parameter_value(
                *evaluated, constant.type, error);
        if (!converted) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-012",
                "generated parameter '" + constant.name
                    + "' cannot be converted: " + error,
                constant.span});
            continue;
        }
        environment[constant.name] = *converted;
        if (const auto integer = converted->integer_value()) {
            integer_environment[constant.name] = *integer;
        }
        domains[constant.name] = ConstantTypeInfo{
            constant.type.domain, false, {}};
    }
    substitute_systemverilog_strings(
        body,
        string_environment,
        integer_environment,
        diagnostics);
    substitute_systemverilog_parameters(body, environment);
    body.constants.clear();
    prepare_regions(
        body.generate_regions,
        environment,
        string_environment,
        integer_environment,
        domains,
        diagnostics);
}

void prepare_selection(
    frontend::GenerateRegion& region,
    const SystemVerilogConstantEnvironment& environment,
    const SystemVerilogStringEnvironment& string_environment,
    const ConstantEnvironment& integer_environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics) {
    std::string string_error;
    if (const auto string_selector =
            evaluate_systemverilog_string_expression(
                region.condition,
                string_environment,
                integer_environment,
                string_error)) {
        const frontend::GenerateAlternative* selected = nullptr;
        const frontend::GenerateAlternative*
            default_alternative = nullptr;
        std::unordered_set<std::string> prior_choices;
        bool invalid = false;
        for (auto& alternative : region.alternatives) {
            if (alternative.is_default) {
                if (default_alternative != nullptr) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-010",
                        "string selection generate has more than one "
                        "default alternative",
                        alternative.span});
                    invalid = true;
                } else {
                    default_alternative = &alternative;
                }
                continue;
            }
            bool matches = false;
            for (auto& choice : alternative.choices) {
                if (choice.right) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-009",
                        "string selection generate supports exact "
                        "choices only",
                        choice.span});
                    invalid = true;
                    continue;
                }
                std::string error;
                const auto value =
                    evaluate_systemverilog_string_expression(
                        choice.left,
                        string_environment,
                        integer_environment,
                        error);
                if (!value) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-009",
                        "cannot evaluate string selection-generate "
                        "choice: " + error,
                        choice.span});
                    invalid = true;
                    continue;
                }
                if (!prior_choices.insert(value->bytes).second) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-010",
                        "string selection generate has duplicate "
                        "constant choices",
                        choice.span});
                    invalid = true;
                }
                matches =
                    matches
                    || value->bytes == string_selector->bytes;
            }
            if (matches) {
                if (selected != nullptr) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-010",
                        "string selection generate has overlapping "
                        "matching alternatives",
                        alternative.span});
                    invalid = true;
                } else {
                    selected = &alternative;
                }
            }
        }
        if (invalid) {
            return;
        }
        if (selected == nullptr) {
            selected = default_alternative;
        }
        frontend::GenerateBody selected_body;
        std::string selected_scope;
        if (selected != nullptr) {
            selected_body = selected->body;
            selected_scope = selected->scope;
        }
        prepare_body(
            selected_body,
            environment,
            string_environment,
            integer_environment,
            domains,
            diagnostics);
        region.kind = frontend::GenerateKind::StaticBlock;
        region.then_scope = std::move(selected_scope);
        region.then_body = std::move(selected_body);
        region.else_body = {};
        region.alternatives.clear();
        return;
    }
    const auto selector = evaluate(
        region.condition,
        environment,
        integer_environment,
        "selection-generate expression",
        "FSIM-ELAB-GEN-008",
        diagnostics);
    if (!selector || !selector->known()) {
        if (selector) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-008",
                "selection-generate expression contains X or Z",
                region.condition.span});
        }
        return;
    }
    if (const auto integer = selector->integer_value()) {
        set_integer_expression(region.condition, *integer);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                const auto left = evaluate(
                    choice.left,
                    environment,
                    integer_environment,
                    "selection-generate choice",
                    "FSIM-ELAB-GEN-009",
                    diagnostics);
                if (left) {
                    const auto converted = left->integer_value();
                    if (!converted) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-009",
                            "selection-generate choice is outside the "
                            "signed 64-bit range required by a range",
                            choice.span});
                    } else {
                        set_integer_expression(
                            choice.left, *converted);
                    }
                }
                if (choice.right) {
                    const auto right = evaluate(
                        *choice.right,
                        environment,
                        integer_environment,
                        "selection-generate range bound",
                        "FSIM-ELAB-GEN-009",
                        diagnostics);
                    if (right) {
                        const auto converted =
                            right->integer_value();
                        if (!converted) {
                            diagnostics.push_back({
                                "FSIM-ELAB-GEN-009",
                                "selection-generate range bound is outside "
                                "the signed 64-bit range",
                                choice.span});
                        } else {
                            set_integer_expression(
                                *choice.right, *converted);
                        }
                    }
                }
            }
            prepare_body(
                alternative.body,
                environment,
                string_environment,
                integer_environment,
                domains,
                diagnostics);
        }
        return;
    }

    const frontend::GenerateAlternative* selected = nullptr;
    const frontend::GenerateAlternative* default_alternative = nullptr;
    std::vector<Value> prior_choices;
    bool invalid = false;
    for (auto& alternative : region.alternatives) {
        if (alternative.is_default) {
            if (default_alternative != nullptr) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-010",
                    "selection generate has more than one default "
                    "alternative",
                    alternative.span});
                invalid = true;
            } else {
                default_alternative = &alternative;
            }
            continue;
        }
        bool matches = false;
        for (auto& choice : alternative.choices) {
            if (choice.right) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-009",
                    "an unsigned-64 selection-generate selector supports "
                    "exact choices; range choices require signed-64 bounds",
                    choice.span});
                invalid = true;
                continue;
            }
            const auto value = evaluate(
                choice.left,
                environment,
                integer_environment,
                "selection-generate choice",
                "FSIM-ELAB-GEN-009",
                diagnostics);
            if (!value || !value->known()) {
                invalid = true;
                continue;
            }
            if (std::ranges::any_of(
                    prior_choices,
                    [&](const auto& prior) {
                        return equal_known(prior, *value);
                    })) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-010",
                    "selection generate has duplicate constant choices",
                    choice.span});
                invalid = true;
            } else {
                prior_choices.push_back(*value);
            }
            matches = matches || equal_known(*selector, *value);
        }
        if (matches) {
            if (selected != nullptr) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-010",
                    "selection generate has overlapping matching "
                    "alternatives",
                    alternative.span});
                invalid = true;
            } else {
                selected = &alternative;
            }
        }
    }
    if (invalid) {
        return;
    }
    if (selected == nullptr) {
        selected = default_alternative;
    }
    frontend::GenerateBody selected_body;
    std::string selected_scope;
    if (selected != nullptr) {
        selected_body = selected->body;
        selected_scope = selected->scope;
    }
    prepare_body(
        selected_body,
        environment,
        string_environment,
        integer_environment,
        domains,
        diagnostics);
    region.kind = frontend::GenerateKind::StaticBlock;
    region.then_scope = std::move(selected_scope);
    region.then_body = std::move(selected_body);
    region.else_body = {};
    region.alternatives.clear();
}

void prepare_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment,
    const SystemVerilogStringEnvironment& string_environment,
    const ConstantEnvironment& integer_environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& region : regions) {
        substitute_systemverilog_strings(
            region.initial,
            string_environment,
            integer_environment);
        substitute_systemverilog_strings(
            region.condition,
            string_environment,
            integer_environment);
        substitute_systemverilog_strings(
            region.iteration,
            string_environment,
            integer_environment);
        substitute_systemverilog_parameters(
            region.initial, environment);
        substitute_systemverilog_parameters(
            region.condition, environment);
        substitute_systemverilog_parameters(
            region.iteration, environment);
        if (region.kind == frontend::GenerateKind::Selection) {
            prepare_selection(
                region,
                environment,
                string_environment,
                integer_environment,
                domains,
                diagnostics);
            continue;
        }
        if (region.kind == frontend::GenerateKind::Conditional) {
            const auto condition = evaluate(
                region.condition,
                environment,
                integer_environment,
                "conditional-generate expression",
                "FSIM-ELAB-GEN-001",
                diagnostics);
            if (condition) {
                const auto truth = condition->truth_value();
                if (!truth) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-001",
                        "conditional-generate expression contains X or Z",
                        region.condition.span});
                } else {
                    set_integer_expression(
                        region.condition, *truth ? 1 : 0);
                }
            }
        }
        if (region.kind != frontend::GenerateKind::Iterative) {
            prepare_body(
                region.then_body,
                environment,
                string_environment,
                integer_environment,
                domains,
                diagnostics);
            prepare_body(
                region.else_body,
                environment,
                string_environment,
                integer_environment,
                domains,
                diagnostics);
        } else {
            substitute_systemverilog_strings(
                region.then_body,
                string_environment,
                integer_environment,
                diagnostics);
            substitute_systemverilog_parameters(
                region.then_body, environment);
        }
    }
}

} // namespace

void prepare_systemverilog_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment,
    const SystemVerilogStringEnvironment& string_environment,
    const ConstantEnvironment& integer_environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics) {
    prepare_regions(
        regions,
        environment,
        string_environment,
        integer_environment,
        domains,
        diagnostics);
}

} // namespace fsim::elaboration::elaboration_detail
