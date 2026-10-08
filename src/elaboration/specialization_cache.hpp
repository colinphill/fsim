// SPDX-License-Identifier: Apache-2.0
//
// Elaboration's derived data for one SpecializedHirUnit, kept in the unit's
// client cache so it lives exactly as long as the specialization (copies
// share it; replace() detaches it).
#pragma once

#include "hierarchy_sv_constant_evaluator.hpp"
#include "fsim/semantic/compiled_design_specialization.hpp"

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
    /// Resolved widths of declaration and callable types, by type record.
    std::unordered_map<const void*, std::optional<std::uint64_t>>
        record_type_widths;
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
/// result keeps the parent's unit-scope declaration values, which read no
/// hierarchy identity.
[[nodiscard]] inline semantic::SpecializedHirUnit
derive_occurrence_specialization(
    const semantic::SpecializedHirUnit& parent,
    const std::span<const semantic::SpecializedHirNamedIdentity> identities)
{
    auto derived = parent.with_hierarchy_identities(identities);
    specialization_cache(derived).constants->unit_declarations
        = specialization_cache(parent).constants->unit_declarations;
    return derived;
}

} // namespace fsim::elaboration
