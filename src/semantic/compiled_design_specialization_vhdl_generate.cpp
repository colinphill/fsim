// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::semantic {
namespace {

constexpr std::size_t maximum_generate_iterations = 1'000'000U;

bool vhdl_name_equal(
    const std::string_view left, const std::string_view right)
{
    return left.size() == right.size()
        && std::equal(left.begin(), left.end(), right.begin(), right.end(),
            [](const unsigned char lhs, const unsigned char rhs) {
                return std::tolower(lhs) == std::tolower(rhs);
            });
}

std::string append_path(
    const std::string_view parent, const std::string_view component)
{
    if (parent.empty()) {
        return std::string { component };
    }
    if (component.empty()) {
        return std::string { parent };
    }
    return std::string { parent } + "." + std::string { component };
}

std::string indexed_component(
    const vhdl::GenerateRegion& region, const std::int64_t value)
{
    return region.label + "[" + std::to_string(value) + "]";
}

SpecializedVhdlGenerateResult failure(
    std::string code, std::string message, const SourceSpanId source)
{
    SpecializedVhdlGenerateResult result;
    result.diagnostic_code = std::move(code);
    result.error = std::move(message);
    result.error_source = source;
    return result;
}

class VhdlGenerateSpecializer final {
public:
    explicit VhdlGenerateSpecializer(const SpecializedHirUnit& unit)
        : unit_ { unit }
    {
    }

    [[nodiscard]] SpecializedVhdlGenerateResult run()
    {
        if (unit_.language() != Language::vhdl) {
            return failure("FSIM-ELAB-HIR-001",
                "VHDL generate occurrence specialization requires a "
                "VHDL HIR unit",
                { });
        }
        const auto selected = unit_.design().find_unit(unit_.unit());
        if (!selected || selected->vhdl == nullptr) {
            return failure("FSIM-ELAB-HIR-001",
                "selected specialization has no VHDL HIR unit", { });
        }
        for (const auto& region : selected->vhdl->generates) {
            if (!specialize(region, std::string_view { },
                    std::span<const SpecializedHirNamedIdentity> { })) {
                return std::move(result_);
            }
        }
        return std::move(result_);
    }

private:
    [[nodiscard]] SpecializedHirUnit working_unit(
        const std::span<const SpecializedHirNamedIdentity> identities) const
    {
        return unit_.with_hierarchy_identities(identities);
    }

    [[nodiscard]] std::optional<std::int64_t> evaluate(
        const ExpressionId expression,
        const std::span<const SpecializedHirNamedIdentity> identities) const
    {
        return working_unit(identities).evaluate_integral_expression(
            expression);
    }

    [[nodiscard]] bool reject(
        std::string code, std::string message, const SourceSpanId source)
    {
        result_.diagnostic_code = std::move(code);
        result_.error = std::move(message);
        result_.error_source = source;
        return false;
    }

    void append_occurrence(const vhdl::GenerateRegion& region,
        std::string relative_path,
        const std::optional<std::int64_t> iteration,
        const std::span<const SpecializedHirNamedIdentity> identities)
    {
        SpecializedVhdlGenerateOccurrence occurrence;
        occurrence.region = &region;
        occurrence.relative_path = std::move(relative_path);
        occurrence.iteration = iteration;
        occurrence.hierarchy_identities.assign(
            identities.begin(), identities.end());
        result_.occurrences.push_back(std::move(occurrence));
    }

    [[nodiscard]] bool specialize_children(
        const std::span<const vhdl::GenerateRegion> children,
        const std::string_view parent_path,
        const std::span<const SpecializedHirNamedIdentity> identities,
        const vhdl::GenerateRegion* excluded = nullptr)
    {
        for (const auto& child : children) {
            if (&child != excluded
                && !specialize(child, parent_path, identities)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] const vhdl::GenerateRegion* else_region(
        const vhdl::GenerateRegion& region) const
    {
        const auto label = region.alternative_label.empty()
            ? region.label + ".else"
            : region.alternative_label;
        const auto found = std::ranges::find_if(
            region.nested, [&](const vhdl::GenerateRegion& candidate) {
                return vhdl_name_equal(candidate.label, label);
            });
        return found == region.nested.end() ? nullptr : &*found;
    }

    [[nodiscard]] const vhdl::GenerateRegion* alternative_region(
        const vhdl::GenerateRegion& region,
        const vhdl::GenerateRegion::Alternative& alternative) const
    {
        const auto label = alternative.label.empty()
            ? region.label + ".alternative"
            : alternative.label;
        const auto found = std::ranges::find_if(
            region.nested, [&](const vhdl::GenerateRegion& candidate) {
                return vhdl_name_equal(candidate.label, label);
            });
        return found == region.nested.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::optional<bool> choice_matches(
        const vhdl::GenerateRegion::Choice& choice,
        const std::int64_t selector,
        const std::span<const SpecializedHirNamedIdentity> identities,
        const std::optional<TypeId> selector_type,
        std::optional<std::pair<std::int64_t, std::int64_t>>*
            interval = nullptr) const
    {
        const auto choice_value = [&](const ExpressionId expression) {
            const auto candidate = unit_.find_expression(expression);
            std::optional<DeclarationId> choice_declaration;
            if (candidate && candidate->vhdl != nullptr
                && candidate->vhdl->referenced_name) {
                choice_declaration
                    = candidate->vhdl->referenced_name->selected;
            }
            if (!choice_declaration && selector_type && candidate
                && candidate->vhdl != nullptr
                && candidate->vhdl->kind
                    == vhdl::ExpressionKind::name) {
                bool ambiguous { };
                const auto consider = [&](const vhdl::Declaration& value) {
                    if (!vhdl_name_equal(
                            value.name, candidate->vhdl->text)) {
                        return;
                    }
                    if (choice_declaration
                        && *choice_declaration != value.id) {
                        ambiguous = true;
                        return;
                    }
                    choice_declaration = value.id;
                };
                for (const auto& value :
                    unit_.design().vhdl_hir.declarations()) {
                    consider(value);
                }
                for (const auto& value : unit_.vhdl_declarations()) {
                    consider(value);
                }
                if (ambiguous) {
                    return std::optional<std::int64_t> { };
                }
            }
            if (selector_type && choice_declaration) {
                const auto declaration = unit_.find_declaration(
                    *choice_declaration);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype
                    && declaration->vhdl->subtype->type_mark.target.valid()
                    && declaration->vhdl->subtype->type_mark.target
                        != *selector_type) {
                    return std::optional<std::int64_t> { };
                }
            }
            if (const auto value = evaluate(expression, identities)) {
                return value;
            }
            if (!selector_type) {
                return std::optional<std::int64_t> { };
            }
            if (!candidate || candidate->vhdl == nullptr
                || (candidate->vhdl->kind
                        != vhdl::ExpressionKind::name
                    && candidate->vhdl->kind
                        != vhdl::ExpressionKind::logic_literal)) {
                return std::optional<std::int64_t> { };
            }
            const auto spelling
                = std::string_view { candidate->vhdl->text };
            const auto matches = [&](const std::string_view literal) {
                return spelling.starts_with('\'')
                    ? spelling == literal
                    : vhdl_name_equal(spelling, literal);
            };
            std::set<TypeId> visiting;
            const auto resolve = [&](const auto& self,
                                     const TypeId type_id)
                -> std::optional<std::int64_t> {
                if (!type_id.valid()
                    || !visiting.insert(type_id).second) {
                    return std::nullopt;
                }
                const auto type = unit_.find_type(type_id);
                if (!type || type->vhdl == nullptr) {
                    return std::nullopt;
                }
                const auto literal = std::ranges::find_if(
                    type->vhdl->enumeration_literals,
                    [&](const vhdl::EnumerationLiteral& value) {
                        return matches(value.spelling);
                    });
                if (literal
                    != type->vhdl->enumeration_literals.end()) {
                    return static_cast<std::int64_t>(literal->ordinal);
                }
                return type->vhdl->base.type_mark.target.valid()
                    ? self(self, type->vhdl->base.type_mark.target)
                    : std::nullopt;
            };
            return resolve(resolve, *selector_type);
        };
        const auto left = choice_value(choice.left);
        if (!left) {
            return std::nullopt;
        }
        auto right = left;
        if (choice.right) {
            right = choice_value(*choice.right);
            if (!right) {
                return std::nullopt;
            }
            const bool empty = choice.descending
                ? *left < *right
                : *left > *right;
            if (empty) {
                return false;
            }
        }
        const auto low = std::min(*left, *right);
        const auto high = std::max(*left, *right);
        if (interval != nullptr) {
            *interval = std::pair { low, high };
        }
        return low <= selector && selector <= high;
    }

    [[nodiscard]] bool specialize_iterative(
        const vhdl::GenerateRegion& region,
        const std::string_view parent_path,
        const std::span<const SpecializedHirNamedIdentity> identities)
    {
        if (region.iterator.empty() || !region.initial
            || !region.condition || !region.iteration) {
            return reject("FSIM-ELAB-GEN-002",
                "compiled VHDL loop-generate has an incomplete iteration "
                "record",
                region.source);
        }
        if (std::ranges::any_of(identities, [&](const auto& identity) {
                return vhdl_name_equal(identity.name, region.iterator);
            })) {
            return reject("FSIM-ELAB-GEN-007",
                "nested loop-generate variable '" + region.iterator
                    + "' shadows an enclosing hierarchy identity",
                region.source);
        }
        const auto initial = evaluate(*region.initial, identities);
        if (!initial) {
            return reject("FSIM-ELAB-GEN-002",
                "cannot evaluate compiled VHDL loop-generate initial "
                "value",
                region.source);
        }

        auto value = *initial;
        std::size_t count { };
        std::set<std::int64_t> visited_values;
        while (true) {
            auto iteration_identities = std::vector<
                SpecializedHirNamedIdentity> {
                identities.begin(), identities.end()
            };
            iteration_identities.push_back(
                { region.iterator, std::to_string(value) });
            const auto condition = evaluate(
                *region.condition, iteration_identities);
            if (!condition) {
                return reject("FSIM-ELAB-GEN-003",
                    "cannot evaluate compiled VHDL loop-generate "
                    "condition",
                    region.source);
            }
            if (*condition == 0) {
                return true;
            }
            if (!visited_values.insert(value).second) {
                return reject("FSIM-ELAB-GEN-014",
                    "compiled VHDL loop-generate revisits genvar value '"
                        + std::to_string(value) + "'",
                    region.source);
            }
            if (count++ == maximum_generate_iterations) {
                return reject("FSIM-ELAB-GEN-004",
                    "compiled VHDL loop-generate exceeds the bounded "
                    "1,000,000-iteration elaboration limit",
                    region.source);
            }

            const auto relative_path = append_path(
                parent_path, indexed_component(region, value));
            append_occurrence(region, relative_path, value,
                iteration_identities);
            if (!specialize_children(region.nested, relative_path,
                    iteration_identities)) {
                return false;
            }
            const auto next = evaluate(
                *region.iteration, iteration_identities);
            if (!next) {
                return reject("FSIM-ELAB-GEN-005",
                    "cannot evaluate compiled VHDL loop-generate "
                    "iteration",
                    region.source);
            }
            if (*next == value) {
                return reject("FSIM-ELAB-GEN-006",
                    "compiled VHDL loop-generate iteration does not "
                    "advance",
                    region.source);
            }
            value = *next;
        }
    }

    [[nodiscard]] bool specialize_selection(
        const vhdl::GenerateRegion& region,
        const std::string_view parent_path,
        const std::span<const SpecializedHirNamedIdentity> identities)
    {
        if (!region.condition) {
            return reject("FSIM-ELAB-GEN-001",
                "compiled VHDL selection-generate has no selector",
                region.source);
        }
        const auto selector = evaluate(*region.condition, identities);
        if (!selector) {
            return reject("FSIM-ELAB-GEN-001",
                "cannot evaluate compiled VHDL selection-generate "
                "selector",
                region.source);
        }
        const auto selector_type = [&]() -> std::optional<TypeId> {
            const auto expression = unit_.find_expression(
                *region.condition);
            if (!expression || expression->vhdl == nullptr
                || !expression->vhdl->referenced_name) {
                return std::nullopt;
            }
            const auto declaration_id
                = expression->vhdl->referenced_name->selected;
            if (!declaration_id) {
                return std::nullopt;
            }
            const auto declaration = unit_.find_declaration(
                *declaration_id);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->subtype
                || !declaration->vhdl->subtype->type_mark.target.valid()) {
                return std::nullopt;
            }
            return declaration->vhdl->subtype->type_mark.target;
        }();
        const vhdl::GenerateRegion::Alternative* selected = nullptr;
        const vhdl::GenerateRegion::Alternative* fallback = nullptr;
        std::vector<std::pair<std::int64_t, std::int64_t>> intervals;
        for (const auto& alternative : region.alternatives) {
            if (alternative.is_default) {
                fallback = &alternative;
                continue;
            }
            for (const auto& choice : alternative.choices) {
                std::optional<std::pair<std::int64_t, std::int64_t>>
                    interval;
                const auto matches = choice_matches(
                    choice, *selector, identities, selector_type,
                    &interval);
                if (!matches) {
                    return reject("FSIM-ELAB-GEN-009",
                        "cannot evaluate compiled VHDL "
                        "selection-generate choice",
                        choice.source);
                }
                if (interval
                    && std::ranges::any_of(
                        intervals, [&](const auto& existing) {
                            return interval->first <= existing.second
                                && existing.first <= interval->second;
                        })) {
                    return reject("FSIM-ELAB-GEN-010",
                        "compiled VHDL selection-generate choices "
                        "overlap",
                        choice.source);
                }
                if (interval) {
                    intervals.push_back(*interval);
                }
                if (*matches) {
                    selected = &alternative;
                }
            }
        }
        selected = selected != nullptr ? selected : fallback;
        if (selected == nullptr) {
            return true;
        }
        const auto* nested = alternative_region(region, *selected);
        if (nested == nullptr) {
            return reject("FSIM-ELAB-GEN-001",
                "compiled VHDL selection-generate has no retained body "
                "for alternative '" + selected->label + "'",
                selected->source);
        }
        return specialize(*nested, parent_path, identities);
    }

    [[nodiscard]] bool specialize(
        const vhdl::GenerateRegion& region,
        const std::string_view parent_path,
        const std::span<const SpecializedHirNamedIdentity> identities)
    {
        if (region.kind == vhdl::GenerateKind::iterative) {
            return specialize_iterative(region, parent_path, identities);
        }
        if (region.kind == vhdl::GenerateKind::selection) {
            return specialize_selection(region, parent_path, identities);
        }
        if (region.kind == vhdl::GenerateKind::conditional) {
            if (!region.condition) {
                return reject("FSIM-ELAB-GEN-001",
                    "compiled VHDL conditional-generate has no condition",
                    region.source);
            }
            const auto condition = evaluate(*region.condition, identities);
            if (!condition) {
                return reject("FSIM-ELAB-GEN-001",
                    "cannot evaluate compiled VHDL conditional-generate "
                    "condition",
                    region.source);
            }
            const auto* alternative = else_region(region);
            if (*condition == 0) {
                return alternative == nullptr
                    || specialize(*alternative, parent_path, identities);
            }
            const auto relative_path = append_path(
                parent_path, region.label);
            append_occurrence(
                region, relative_path, std::nullopt, identities);
            return specialize_children(region.nested, relative_path,
                identities, alternative);
        }

        const auto relative_path = append_path(parent_path, region.label);
        append_occurrence(region, relative_path, std::nullopt, identities);
        return specialize_children(
            region.nested, relative_path, identities);
    }

    const SpecializedHirUnit& unit_;
    SpecializedVhdlGenerateResult result_;
};

} // namespace

SpecializedVhdlGenerateResult specialize_vhdl_generate_occurrences(
    const SpecializedHirUnit& unit)
{
    return VhdlGenerateSpecializer { unit }.run();
}

} // namespace fsim::semantic
