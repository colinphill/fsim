// SPDX-License-Identifier: Apache-2.0
//
// Elaboration's derived data for one SpecializedHirUnit, kept in the unit's
// client cache so it lives exactly as long as the specialization (copies
// share it; replace() detaches it).
#pragma once

#include "hierarchy_sv_constant_evaluator.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

namespace fsim::elaboration {

/// Constant-evaluation results for one specialization. Specializations in
/// the same overlay class whose overlay determines them share one instance
/// (HierarchyBuilder::overlay_class).
struct SpecializationConstants {
    /// evaluate_hir_systemverilog_constant results and errors.
    std::unordered_map<std::uint32_t,
        std::pair<std::optional<HirSystemVerilogConstant>, std::string>>
        expressions;
    /// Values of declarations outside any constant-function frame that
    /// depend on hierarchy identities, by declaration.
    std::unordered_map<std::uint32_t, HirSystemVerilogConstant> declarations;
    /// Values of unit-scope declarations that read no hierarchy identity,
    /// shared with derived occurrence specializations.
    std::shared_ptr<std::unordered_map<std::uint32_t, HirSystemVerilogConstant>>
        unit_declarations { std::make_shared<std::unordered_map<std::uint32_t,
            HirSystemVerilogConstant>>() };
    /// Values of expressions whose evaluation reads no hierarchy identity,
    /// shared with derived occurrence specializations like
    /// unit_declarations.
    std::shared_ptr<std::unordered_map<std::uint32_t, HirSystemVerilogConstant>>
        unit_expressions { std::make_shared<std::unordered_map<std::uint32_t,
            HirSystemVerilogConstant>>() };
    /// A constant function call's converted result and the work units its
    /// evaluation consumed.
    struct CallResult {
        HirSystemVerilogConstant value;
        std::uint64_t work { };
    };
    /// Results of constant function calls that read no hierarchy identity,
    /// by callable and converted argument values (call_memo_key); shared
    /// with derived occurrence specializations like unit_declarations.
    std::shared_ptr<std::unordered_map<std::string, CallResult>> unit_calls {
        std::make_shared<std::unordered_map<std::string, CallResult>>()
    };
    /// Resolved widths of declaration and callable types, by type record.
    std::unordered_map<const void*, std::optional<std::uint64_t>>
        record_type_widths;
    /// Whether declaration types without unpacked dimensions are unpacked
    /// aggregates, by type record; with no dimension bounds to evaluate the
    /// answer reads no constant-function frame.
    std::unordered_map<const void*, bool> record_unpacked_aggregates;
    /// Parsed integer and logic literals and their errors, by expression
    /// record; index 1 holds parses as integer literals.
    std::array<std::unordered_map<const void*,
        std::pair<std::optional<HirSystemVerilogConstant>, std::string>>, 2>
        literals;
    /// HirConstantEvaluator::declaration_type results, by declaration.
    std::unordered_map<std::uint32_t,
        std::optional<semantic::sv::TypeReference>> declaration_types;
    /// Whether a named packed array base can hold packed structures, by
    /// name expression record.
    std::unordered_map<const void*, bool> structure_array_names;
    /// Self-determined widths of expressions (HirConstantEvaluator), by
    /// expression.
    std::unordered_map<std::uint32_t, std::optional<std::uint32_t>>
        expression_widths;
    /// Whether expressions are real-valued (HirConstantEvaluator), by
    /// expression.
    std::unordered_map<std::uint32_t, bool> real_valued;
    /// The facts HirConstantEvaluator::evaluate_binary reads from a binary
    /// expression's operator and declared operands.
    struct BinaryPlan {
        bool real_operator { };
        bool logical_operator { };
        bool arithmetic_context { };
        bool relational_context { };
        bool left_context { };
        bool contextual { };
        bool real_operand { };
        bool chandle_operand { };
        bool left_context_determined { };
        bool right_context_determined { };
        std::optional<std::uint32_t> left_width;
        std::optional<std::uint32_t> right_width;
    };
    /// By expression.
    std::unordered_map<std::uint32_t, BinaryPlan> binary_plans;
    /// Fixed one-dimensional unpacked array types through typedefs, by type
    /// record, with the scope they were resolved in.
    std::unordered_map<const void*, std::pair<semantic::ScopeId,
        std::optional<semantic::sv::TypeReference>>>
        unpacked_array_types;
    /// systemverilog_constant_name_declaration results, by expression record.
    std::unordered_map<const void*, std::optional<semantic::DeclarationId>>
        name_declarations;
};

struct SpecializationCache {
    static constexpr std::uint32_t unassigned
        = std::numeric_limits<std::uint32_t>::max();

    std::shared_ptr<SpecializationConstants> constants {
        std::make_shared<SpecializationConstants>()
    };
    /// HierarchyBuilder::overlay_class.
    std::uint32_t overlay_class { unassigned };
};

[[nodiscard]] inline SpecializationCache& specialization_cache(
    const semantic::SpecializedHirUnit& specialization)
{
    auto& cache = specialization.client_cache();
    if (!cache) {
        cache = std::make_shared<SpecializationCache>();
    }
    return *static_cast<SpecializationCache*>(cache.get());
}

/// `parent` with other hierarchy identities (with_hierarchy_identities). The
/// result keeps the parent's unit-scope declaration and expression values
/// and constant function results, which read no hierarchy identity.
[[nodiscard]] inline semantic::SpecializedHirUnit
derive_occurrence_specialization(
    const semantic::SpecializedHirUnit& parent,
    const std::span<const semantic::SpecializedHirNamedIdentity> identities)
{
    auto derived = parent.with_hierarchy_identities(identities);
    auto& derived_constants = *specialization_cache(derived).constants;
    const auto& parent_constants = *specialization_cache(parent).constants;
    derived_constants.unit_declarations = parent_constants.unit_declarations;
    derived_constants.unit_expressions = parent_constants.unit_expressions;
    derived_constants.unit_calls = parent_constants.unit_calls;
    return derived;
}

} // namespace fsim::elaboration
