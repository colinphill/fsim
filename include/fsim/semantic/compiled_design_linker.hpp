// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/compiled_design.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::semantic {

struct CompiledLinkResult {
    std::optional<CompiledDesign> design;
    std::string error;
    std::string diagnostic_code { };

    [[nodiscard]] bool ok() const noexcept
    {
        return design.has_value() && error.empty();
    }
};

/// Rebuild deterministic source-dependency and external-reference records from
/// the owning semantic model and both HIRs. Existing records are replaced.
void refresh_compiled_design_metadata(CompiledDesign& design);

/// Merge independently decoded compiled-HIR bundles. Every dense ID in later
/// bundles is relocated by the preceding record counts before external names
/// are resolved against the combined unit table.
[[nodiscard]] CompiledLinkResult link_compiled_designs(
    std::vector<CompiledDesign> inputs);

/// Project one logical library into a self-contained bundle. Semantic and HIR
/// IDs are made dense again; references to declarations outside the selected
/// library remain name-based unresolved edges for the linker.
[[nodiscard]] CompiledLinkResult extract_compiled_library(
    const CompiledDesign& design,
    std::string_view library);

/// Consume a bundle when the selected library covers every owning and
/// auxiliary record. Partial selections retain the ordinary projection
/// behavior.
[[nodiscard]] CompiledLinkResult extract_compiled_library(
    CompiledDesign&& design,
    std::string_view library);

/// Project a deterministic set of logical libraries into one self-contained
/// bundle. This is used by persistent artifacts that must carry the
/// compiler-owned package HIR needed to link their primary library without
/// reparsing syntax at load time.
[[nodiscard]] CompiledLinkResult extract_compiled_libraries(
    const CompiledDesign& design,
    std::span<const std::string> libraries);

/// Consume a bundle when the selected libraries cover every owning and
/// auxiliary record. Partial selections retain the ordinary projection
/// behavior.
[[nodiscard]] CompiledLinkResult extract_compiled_libraries(
    CompiledDesign&& design,
    std::span<const std::string> libraries);

/// Project selected semantic units and the named supporting libraries into a
/// self-contained bundle. Records owned by omitted units become unresolved
/// name-based edges and are resolved when independently published bundles are
/// linked.
[[nodiscard]] CompiledLinkResult extract_compiled_units(
    const CompiledDesign& design,
    std::span<const UnitId> units,
    std::span<const std::string> supporting_libraries);

/// Project every logical library except the named libraries into a
/// self-contained bundle. This is used when decoded compiled libraries must
/// replace compatibility-only syntax sidecars in a combined workspace.
[[nodiscard]] CompiledLinkResult exclude_compiled_libraries(
    const CompiledDesign& design,
    std::span<const std::string> libraries);

} // namespace fsim::semantic
