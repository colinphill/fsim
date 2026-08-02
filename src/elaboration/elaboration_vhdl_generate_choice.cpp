// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
namespace {

const frontend::Type* selection_type(
    const frontend::GenerateRegion& generate,
    const DesignUnit& unit) {
    const frontend::Type* result = nullptr;
    const auto select_nominal =
        [&](const frontend::Type& type) {
          if (result == nullptr
              && !generate.condition.nominal_type.empty()
              && (type.nominal_type
                      == generate.condition.nominal_type
                  || type.vhdl_type_declaration
                      == generate.condition.nominal_type)) {
              result = &type;
          }
        };
    visit_declared_types(unit, select_nominal, true);
    if (result != nullptr
        || generate.condition.kind
            != frontend::ExpressionKind::Identifier) {
        return result;
    }
    const auto find_object =
        [&](const auto& declarations) -> const frontend::Type* {
          const auto found = std::ranges::find(
              declarations,
              generate.condition.text,
              &std::remove_cvref_t<
                  decltype(declarations.front())>::name);
          return found == declarations.end()
              ? nullptr
              : &found->type;
        };
    if (const auto* type = find_object(unit.parameters)) {
        return type;
    }
    if (const auto* type = find_object(unit.ports)) {
        return type;
    }
    return find_object(unit.signals);
}

std::optional<std::int64_t> enumeration_ordinal(
    const frontend::Expression& expression,
    const frontend::Type& type,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::string& error) {
    if (!expression.nominal_type.empty()
        && expression.nominal_type != type.nominal_type
        && expression.nominal_type
            != type.vhdl_type_declaration) {
        error = "choice has a different enumeration type";
        return std::nullopt;
    }
    if (expression.kind == frontend::ExpressionKind::Identifier) {
        if (const auto domain = domains.find(expression.text);
            domain != domains.end()
            && domain->second.vhdl_enumeration
            && !domain->second.nominal_type.empty()
            && domain->second.nominal_type != type.nominal_type
            && domain->second.nominal_type
                != type.vhdl_type_declaration) {
            error = "choice constant has a different enumeration type";
            return std::nullopt;
        }
    }
    constexpr std::string_view folded_prefix{"@fsim-enum:"};
    if (expression.kind == frontend::ExpressionKind::LogicLiteral
        && expression.text.starts_with(folded_prefix)) {
        std::int64_t ordinal = 0;
        const auto text = std::string_view{expression.text}.substr(
            folded_prefix.size());
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), ordinal);
        if (parsed.ec == std::errc{}
            && parsed.ptr == text.data() + text.size()
            && ordinal >= 0
            && static_cast<std::uint64_t>(ordinal)
                < type.enumeration_literals.size()) {
            return ordinal;
        }
        error = "folded enumeration ordinal is outside its base type";
        return std::nullopt;
    }
    if (expression.kind == frontend::ExpressionKind::Identifier
        || expression.kind == frontend::ExpressionKind::LogicLiteral) {
        const auto separator = expression.text.find_last_of('.');
        const auto literal =
            separator == std::string::npos
            ? std::string_view{expression.text}
            : std::string_view{expression.text}.substr(separator + 1);
        const auto found = std::ranges::find(
            type.enumeration_literals, literal);
        if (found != type.enumeration_literals.end()) {
            return static_cast<std::int64_t>(
                std::distance(type.enumeration_literals.begin(), found));
        }
    }
    if (expression.kind == frontend::ExpressionKind::Identifier) {
        const auto value = evaluate_constant_expression(
            expression, environment, error);
        if (value && *value >= 0
            && static_cast<std::uint64_t>(*value)
                < type.enumeration_literals.size()) {
            return value;
        }
    }
    error = "choice is not a literal or constant of the selector's "
            "enumeration type";
    return std::nullopt;
}

std::optional<std::int64_t> choice_ordinal(
    const frontend::Expression& expression,
    const frontend::Type* type,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::string& error) {
    if (type != nullptr && !type->enumeration_literals.empty()) {
        return enumeration_ordinal(
            expression, *type, environment, domains, error);
    }
    return evaluate_constant_expression(expression, environment, error);
}

} // namespace

const frontend::GenerateAlternative* select_generate_alternative(
    const frontend::GenerateRegion& generate,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics) {
    const auto* type = selection_type(generate, unit);
    std::string error;
    const auto selector = choice_ordinal(
        generate.condition, type, environment, domains, error);
    if (!selector) {
        diagnostics.push_back({
            "FSIM-ELAB-GEN-008",
            "cannot evaluate selection-generate expression: " + error,
            generate.condition.span});
        return nullptr;
    }
    const frontend::GenerateAlternative* selected = nullptr;
    const frontend::GenerateAlternative* default_alternative = nullptr;
    struct ChoiceInterval {
        std::int64_t lower;
        std::int64_t upper;
    };
    std::vector<ChoiceInterval> intervals;
    bool invalid = false;
    for (const auto& alternative : generate.alternatives) {
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
        for (const auto& choice : alternative.choices) {
            error.clear();
            const auto left = choice_ordinal(
                choice.left, type, environment, domains, error);
            if (!left) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-009",
                    "cannot evaluate selection-generate choice: " + error,
                    choice.span});
                invalid = true;
                continue;
            }
            auto right = left;
            if (choice.right) {
                error.clear();
                right = choice_ordinal(
                    *choice.right,
                    type,
                    environment,
                    domains,
                    error);
                if (!right) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-009",
                        "cannot evaluate selection-generate range bound: "
                            + error,
                        choice.span});
                    invalid = true;
                    continue;
                }
            }
            const bool empty =
                choice.right
                && (choice.descending
                        ? *left < *right
                        : *left > *right);
            if (empty) {
                continue;
            }
            const ChoiceInterval interval{
                std::min(*left, *right),
                std::max(*left, *right)};
            const bool overlaps = std::ranges::any_of(
                intervals,
                [&](const ChoiceInterval& prior) {
                  return interval.lower <= prior.upper
                      && prior.lower <= interval.upper;
                });
            if (overlaps) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-010",
                    "selection generate has overlapping constant choices "
                    "or ranges",
                    choice.span});
                invalid = true;
            } else {
                intervals.push_back(interval);
            }
            matches = matches
                || (interval.lower <= *selector
                    && *selector <= interval.upper);
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
        return nullptr;
    }
    return selected != nullptr ? selected : default_alternative;
}

} // namespace fsim::elaboration::elaboration_detail
