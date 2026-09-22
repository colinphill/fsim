// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design_specialization.hpp"

#include <optional>
#include <string_view>

namespace fsim::elaboration::elaboration_detail {

/// Parser-independent identity for one selected child occurrence. The
/// semantic IDs remain stable across compiled-bundle decoding and linker
/// relocation; the HIR spelling is retained only for diagnostics and legacy
/// lowering-adapter fallback.
struct HierarchyCompiledOccurrence {
    semantic::InstanceId instance;
    semantic::ScopeId scope;
    semantic::UnitId owner;
    semantic::SourceSpanId source;
    semantic::OriginId origin;
    std::string_view name;
    std::string_view target_spelling;
    const semantic::CompiledReference* reference { };
    std::optional<semantic::CompiledUnitView> linked_target;
};

/// Resolve the link edge belonging to an effective specialized-HIR instance.
/// A valid occurrence is returned even when the edge is absent or unresolved,
/// allowing callers to retain the spelling-based compatibility path. When
/// linked_target is present its UnitId is authoritative.
[[nodiscard]] std::optional<HierarchyCompiledOccurrence>
resolve_compiled_occurrence(
    const semantic::SpecializedHirUnit& specialization,
    semantic::CompiledInstanceView instance) noexcept;

} // namespace fsim::elaboration::elaboration_detail
