// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/hierarchy_path.hpp"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace fsim::app::runtime_path_codec {

inline constexpr std::uint64_t kMaximumPayloadBytes
    = 1024ULL * 1024U * 1024U;
/// Allow bounded in-memory expansion up to twice the runtime wire limit.
/// This remains an aggregate allocation cap; larger expansions are rejected.
inline constexpr std::size_t kMaximumDecodeAllocationBytes
    = static_cast<std::size_t>(2ULL * kMaximumPayloadBytes);

/// Serialize runtime state with all hierarchy paths represented by IDs.
/// A null table writes a canonical inline FSIMHPT1 table; a non-null table
/// writes references into that externally owned canonical table.
[[nodiscard]] std::optional<std::string> serialize_runtime_path_state(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable* external_paths,
    diagnostic::Engine& diagnostics);

/// Streaming counterpart of serialize_runtime_path_state().
[[nodiscard]] bool serialize_runtime_path_state(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable* external_paths,
    std::ostream& output,
    diagnostic::Engine& diagnostics);

/// Hash the exact runtime-state bytes, including the path table binding.
[[nodiscard]] std::optional<std::string> runtime_path_state_checksum(
    const elaboration::ElaboratedDesignState& state,
    const semantic::HierarchyPathTable* external_paths,
    diagnostic::Engine& diagnostics);

/// Decode runtime path IDs and reconstruct the path-bearing strings in a
/// runtime state. The caller performs ElaboratedDesign structural validation.
[[nodiscard]] std::optional<elaboration::ElaboratedDesignState>
deserialize_runtime_path_state(
    std::string_view bytes,
    std::string source_name,
    const semantic::HierarchyPathTable* external_paths,
    diagnostic::Engine& diagnostics);

/// Bounded streaming counterpart of deserialize_runtime_path_state().
[[nodiscard]] std::optional<elaboration::ElaboratedDesignState>
deserialize_runtime_path_state(
    std::istream& input,
    std::uint64_t size,
    std::string source_name,
    const semantic::HierarchyPathTable* external_paths,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app::runtime_path_codec
