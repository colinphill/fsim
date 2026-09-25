// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/hierarchy_path.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::app::hierarchy_path_codec {

inline constexpr std::string_view kMagic = "FSIMHPT1";
inline constexpr std::uint32_t kSchema = 1U;

struct Limits {
    std::size_t maximum_payload_bytes { 128U * 1024U * 1024U };
    std::size_t maximum_paths { 1'000'000U };
    std::size_t maximum_path_bytes { 1024U * 1024U };
    std::size_t maximum_total_path_bytes { 96U * 1024U * 1024U };
};

enum class Error : std::uint8_t {
    none,
    payload_limit,
    path_count_limit,
    path_length_limit,
    total_path_bytes_limit,
    invalid_magic,
    unsupported_schema,
    truncated,
    trailing_bytes,
    invalid_utf8,
    embedded_nul,
    duplicate_path,
    unsorted_path,
    invalid_reference,
    unmapped_reference,
    allocation_failure,
};

/// Schema-1 bytes own a canonical table whose IDs equal the path positions.
/// The digest is lowercase SHA-256 over the exact bytes, including the header.
struct Payload {
    semantic::HierarchyPathTable paths;
    std::string bytes;
    std::string digest;
};

struct Result {
    std::optional<Payload> payload;
    Error error { Error::none };

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return payload.has_value();
    }
};

/// Form the sorted union of runtime and DesignIR spellings for a project.
[[nodiscard]] Result encode_canonical_hierarchy_paths(
    const semantic::HierarchyPathTable& runtime_paths,
    const semantic::HierarchyPathTable& design_ir_paths,
    Limits limits = { });

/// Use the same required table format for standalone component payloads.
[[nodiscard]] Result encode_inline_hierarchy_paths(
    const semantic::HierarchyPathTable& paths,
    Limits limits = { });

/// Reject noncanonical ordering, duplicate/invalid UTF-8, malformed lengths,
/// excess allocation budgets, and all bytes following the final path.
[[nodiscard]] Result decode_hierarchy_paths(
    std::string_view bytes,
    Limits limits = { });

/// Validate a serialized numeric reference before constructing a table-local
/// ID. UINT32_MAX is reserved as the invalid-ID sentinel.
[[nodiscard]] std::optional<semantic::HierarchyPathId>
decode_path_reference(
    const semantic::HierarchyPathTable& paths,
    std::uint32_t raw_id) noexcept;

/// Translate a valid source-table ID by spelling into the canonical table.
/// References to absent paths are rejected, never assigned a new ID.
[[nodiscard]] std::optional<semantic::HierarchyPathId>
remap_path_reference(
    const semantic::HierarchyPathTable& source,
    semantic::HierarchyPathId source_id,
    const semantic::HierarchyPathTable& canonical) noexcept;

} // namespace fsim::app::hierarchy_path_codec
