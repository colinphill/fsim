// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_constant_evaluator.hpp"
#include "hierarchy_sv_generate_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

namespace fsim::elaboration::hierarchy_sv_generate_detail {
namespace {

    std::string generated_path(
        const std::string_view parent,
        const std::string_view local)
    {
        if (parent.empty()) {
            return std::string { local };
        }
        if (local.empty()) {
            return std::string { parent };
        }
        return std::string { parent } + "." + std::string { local };
    }

}

CollectionResult collect_occurrences(
    const semantic::sv::Unit& unit,
    const std::string_view parent_path,
    const semantic::SpecializedHirUnit& specialization)
{
    CollectionResult result;
    const auto fail = [&](
                          std::string code,
                          std::string message,
                          const semantic::SourceSpanId source) {
        result.diagnostic = Diagnostic {
            std::move(code), std::move(message), source
        };
    };
    const auto generate_subtree_selected = [&](const auto& self,
                                               const semantic::sv::GenerateRegion& generate,
                                               const semantic::SpecializedHirUnit& selected_specialization) -> bool {
        const auto selected = selected_specialization.selected_generates();
        if (std::ranges::find(selected, generate.declaration)
            != selected.end()) {
            return true;
        }
        return std::ranges::any_of(generate.nested,
            [&](const auto& nested) {
                return self(self, nested, selected_specialization);
            });
    };
    const auto collect_generate_occurrences = [&](const auto& self,
                                                  const semantic::sv::GenerateRegion& generate,
                                                  const std::string& current_path,
                                                  const semantic::SpecializedHirUnit& parent_specialization)
        -> bool {
        using Kind = semantic::sv::GenerateKind;
        if (generate.kind == Kind::iterative) {
            if (std::ranges::any_of(
                    parent_specialization.specialization()
                        .hierarchy_identities,
                    [&](const auto& hierarchy_identity) {
                        return hierarchy_identity.name == generate.iterator;
                    })) {
                fail(
                    "FSIM-ELAB-GEN-007",
                    "nested loop-generate variable '"
                        + generate.iterator
                        + "' shadows an enclosing hierarchy identity",
                    unit.source);
                return false;
            }
            if (generate.iterator.empty() || !generate.initial
                || !generate.condition || !generate.iteration) {
                fail(
                    "FSIM-ELAB-GEN-002",
                    "compiled loop-generate has incomplete iteration "
                    "metadata",
                    unit.source);
                return false;
            }
            const auto initial
                = parent_specialization.evaluate_integral_expression(
                    *generate.initial);
            if (!initial) {
                fail(
                    "FSIM-ELAB-GEN-002",
                    "cannot evaluate compiled loop-generate initial "
                    "value",
                    unit.source);
                return false;
            }
            std::int64_t value = *initial;
            std::unordered_set<std::int64_t> visited;
            constexpr std::size_t maximum_iterations = 1'000'000U;
            for (std::size_t count = 0U;; ++count) {
                auto identities = parent_specialization
                                      .specialization()
                                      .hierarchy_identities;
                identities.push_back(
                    { generate.iterator, std::to_string(value) });
                auto occurrence_specialization
                    = parent_specialization.with_hierarchy_identities(
                        identities);
                const auto condition = occurrence_specialization
                                           .evaluate_integral_expression(
                                               *generate.condition);
                if (!condition) {
                    fail(
                        "FSIM-ELAB-GEN-003",
                        "cannot evaluate compiled loop-generate "
                        "condition",
                        unit.source);
                    return false;
                }
                if (*condition == 0) {
                    break;
                }
                if (!visited.insert(value).second) {
                    fail(
                        "FSIM-ELAB-GEN-014",
                        "compiled loop-generate revisits genvar value '"
                            + std::to_string(value) + "'",
                        unit.source);
                    return false;
                }
                if (count == maximum_iterations) {
                    fail(
                        "FSIM-ELAB-GEN-004",
                        "compiled loop-generate exceeds the bounded "
                        "1,000,000-iteration elaboration limit",
                        unit.source);
                    return false;
                }
                const auto local_path = generate.label + "["
                    + std::to_string(value) + "]";
                const auto occurrence_path
                    = generated_path(current_path, local_path);
                result.occurrences.push_back({ &generate, occurrence_path, occurrence_specialization });
                for (const auto& nested : generate.nested) {
                    if (!self(self, nested, occurrence_path,
                            occurrence_specialization)) {
                        return false;
                    }
                }
                const auto next = occurrence_specialization
                                      .evaluate_integral_expression(
                                          *generate.iteration);
                if (!next) {
                    fail(
                        "FSIM-ELAB-GEN-005",
                        "cannot evaluate compiled loop-generate "
                        "iteration",
                        unit.source);
                    return false;
                }
                if (*next == value) {
                    fail(
                        "FSIM-ELAB-GEN-006",
                        "compiled loop-generate iteration does not "
                        "advance",
                        unit.source);
                    return false;
                }
                value = *next;
            }
            return true;
        }

        if (generate.kind == Kind::conditional) {
            const auto constant = generate.condition
                ? parent_specialization
                      .evaluate_systemverilog_truth_expression(
                          *generate.condition)
                : std::nullopt;
            if (!constant) {
                fail(
                    "FSIM-ELAB-GEN-001",
                    "cannot evaluate compiled conditional-generate "
                    "condition as an elaboration-time constant",
                    generate.source);
                return false;
            }
        }

        if (generate.kind == Kind::selection) {
            const auto selector_integral = generate.condition
                ? parent_specialization.evaluate_integral_expression(
                      *generate.condition)
                : std::nullopt;
            const auto selector_bits
                = generate.condition && !selector_integral
                ? parent_specialization
                      .evaluate_systemverilog_bits_expression(
                          *generate.condition)
                : std::nullopt;
            const auto selector_string
                = generate.condition && !selector_integral
                    && !selector_bits
                ? parent_specialization.evaluate_string_expression(
                      *generate.condition)
                : std::nullopt;
            if (!selector_integral && !selector_bits
                && !selector_string) {
                fail(
                    "FSIM-ELAB-GEN-008",
                    "cannot evaluate compiled case-generate selector as "
                    "an elaboration-time constant",
                    generate.source);
                return false;
            }

            std::vector<std::pair<std::int64_t, std::int64_t>>
                integral_choices;
            std::unordered_set<std::string> bit_choices;
            std::unordered_set<std::string> string_choices;
            for (const auto& alternative : generate.alternatives) {
                if (alternative.is_default) {
                    continue;
                }
                for (const auto& choice : alternative.choices) {
                    const auto left_integral
                        = parent_specialization
                              .evaluate_integral_expression(choice.left);
                    const auto left_bits = !left_integral
                        ? parent_specialization
                              .evaluate_systemverilog_bits_expression(
                                  choice.left)
                        : std::nullopt;
                    const auto left_string = !left_integral && !left_bits
                        ? parent_specialization
                              .evaluate_string_expression(choice.left)
                        : std::nullopt;
                    const auto right_integral = choice.right
                        ? parent_specialization
                              .evaluate_integral_expression(*choice.right)
                        : left_integral;
                    const auto right_bits
                        = choice.right && !right_integral
                        ? parent_specialization
                              .evaluate_systemverilog_bits_expression(
                                  *choice.right)
                        : left_bits;
                    const auto right_string
                        = choice.right && !right_integral && !right_bits
                        ? parent_specialization
                              .evaluate_string_expression(*choice.right)
                        : left_string;
                    const bool integral_range
                        = left_integral && right_integral;
                    const bool bit_value = !choice.right && left_bits;
                    const bool string_value
                        = !choice.right && left_string;
                    if (!integral_range && !bit_value && !string_value) {
                        fail(
                            "FSIM-ELAB-GEN-009",
                            "cannot evaluate compiled case-generate "
                            "choice as an elaboration-time constant",
                            choice.source);
                        return false;
                    }
                    bool overlaps { };
                    if (integral_range) {
                        const auto low = std::min(
                            *left_integral, *right_integral);
                        const auto high = std::max(
                            *left_integral, *right_integral);
                        overlaps = std::ranges::any_of(
                            integral_choices,
                            [&](const auto& previous) {
                                return low <= previous.second
                                    && previous.first <= high;
                            });
                        integral_choices.emplace_back(low, high);
                    } else if (bit_value) {
                        overlaps
                            = !bit_choices.insert(*left_bits).second;
                    } else {
                        overlaps
                            = !string_choices.insert(*left_string).second;
                    }
                    if (overlaps) {
                        fail(
                            "FSIM-ELAB-GEN-010",
                            "compiled case-generate choices overlap",
                            choice.source);
                        return false;
                    }
                }
            }
        }

        const auto selected = parent_specialization.selected_generates();
        const bool active = std::ranges::find(
                                selected, generate.declaration)
            != selected.end();
        auto nested_parent = current_path;
        if (active) {
            // The selected region's label is always its own hierarchy
            // name. alternative_label names the sibling else branch and is
            // only used while selecting that nested region.
            nested_parent = generated_path(current_path, generate.label);
            result.occurrences.push_back({ &generate, nested_parent, parent_specialization });
        }
        for (const auto& nested : generate.nested) {
            const bool same_alternative
                = nested.alternative_discriminator
                == generate.alternative_discriminator;
            if (!(active && same_alternative)
                && !generate_subtree_selected(
                    generate_subtree_selected, nested,
                    parent_specialization)) {
                continue;
            }
            if (!self(self, nested, nested_parent,
                    parent_specialization)) {
                return false;
            }
        }
        return true;
    };

    for (const auto& generate : unit.generates) {
        if (!collect_generate_occurrences(
                collect_generate_occurrences, generate,
                std::string { parent_path }, specialization)) {
            return result;
        }
    }
    return result;
}

ValidationResult validate_generated_constant(
    const Occurrence& occurrence,
    const semantic::DeclarationId declaration_id)
{
    const auto fail = [](
                          std::string code,
                          std::string message,
                          const semantic::SourceSpanId source) {
        return ValidationResult {
            Diagnostic {
                std::move(code), std::move(message), source }
        };
    };

    const auto declaration = occurrence.specialization.find_declaration(
        declaration_id);
    if (!declaration || declaration->systemverilog == nullptr) {
        return fail(
            "FSIM-ELAB-HIR-001",
            "generated declaration in '" + occurrence.path
                + "' has no SystemVerilog HIR record",
            occurrence.region->source);
    }

    const auto& record = *declaration->systemverilog;
    using Form = semantic::sv::DeclarationForm;
    if ((record.form != Form::parameter
            && record.form != Form::local_parameter)
        || !record.initializer) {
        return { };
    }

    const auto description = "generated parameter '" + record.name + "'";
    if (hierarchy_sv_parameters_detail::
            compiled_systemverilog_string_declaration(record)) {
        if (occurrence.specialization.evaluate_string_declaration(
                declaration_id)) {
            return { };
        }
        return fail(
            "FSIM-ELAB-GEN-011",
            "cannot evaluate " + description
                + " in its declaration-order environment",
            record.source);
    }

    if (record.type) {
        const auto width
            = hierarchy_sv_type_layout_detail::
                systemverilog_declaration_width(
                    occurrence.specialization, record);
        if (!width
            || *width
                > static_cast<std::size_t>(
                    hir_systemverilog_maximum_constant_width)) {
            return fail(
                "FSIM-ELAB-GEN-012",
                description
                    + " exceeds the configured constant materialization "
                      "width",
                record.source);
        }
    }

    const auto scalar_applicable
        = (record.type
              && hierarchy_sv_parameters_detail::
                      compiled_systemverilog_scalar_kind(
                          record.type->target.spelling)
                  != frontend::SystemVerilogScalarKind::None)
        || hir_systemverilog_scalar_expression_applicable(
            occurrence.specialization, *record.initializer);
    std::string error;
    if (scalar_applicable) {
        if (evaluate_hir_systemverilog_scalar_declaration(
                occurrence.specialization, declaration_id, error)) {
            return { };
        }
        return fail(
            "FSIM-ELAB-GEN-011",
            "cannot evaluate " + description + ": " + error,
            record.source);
    }

    auto constant = evaluate_hir_systemverilog_constant(
        occurrence.specialization, *record.initializer, error);
    if (!constant) {
        return fail(
            "FSIM-ELAB-GEN-011",
            "cannot evaluate " + description + ": " + error,
            record.source);
    }
    if (record.type
        && hir_systemverilog_explicit_integral_type(*record.type)
        && !convert_hir_systemverilog_constant(
            std::move(*constant), *record.type, error,
            &occurrence.specialization)) {
        return fail(
            "FSIM-ELAB-GEN-012",
            description + " cannot be converted: " + error,
            record.source);
    }

    return { };
}

} // namespace fsim::elaboration::hierarchy_sv_generate_detail
