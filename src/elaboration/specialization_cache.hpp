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
#include <string>
#include <unordered_map>
#include <utility>

namespace fsim::elaboration {

struct SpecializationCache {
    static constexpr std::uint32_t unassigned
        = std::numeric_limits<std::uint32_t>::max();

    /// evaluate_hir_systemverilog_constant results and errors.
    std::unordered_map<std::uint32_t,
        std::pair<std::optional<HirSystemVerilogConstant>, std::string>>
        constants;
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

} // namespace fsim::elaboration
