// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/scv_smart_ptr.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fsim::systemc {

struct ScvExtensionLimits {
    std::size_t max_nodes { 65536U };
    std::size_t max_depth { 256U };
    std::size_t max_value_bits { 1024U * 1024U };
    std::size_t max_total_words { 1024U * 1024U };
    std::size_t max_string_bytes { 1024U * 1024U };
    std::size_t max_total_string_bytes { 16U * 1024U * 1024U };
};

enum class ScvExtensionRelation : std::uint8_t {
    root = 1,
    field = 2,
    element = 3,
};

struct ScvExtensionNode {
    std::size_t id { };
    std::optional<std::size_t> parent;
    ScvExtensionRelation relation { ScvExtensionRelation::root };
    std::size_t ordinal { };
    std::string name;
    std::string nominal_type;
    ScvNativeExtensionKind kind { ScvNativeExtensionKind::unsupported };
    std::size_t bit_width { };
    bool signed_type { };
    std::optional<std::size_t> range_left;
    std::optional<std::size_t> range_right;
    std::optional<std::int64_t> signed_value;
    std::optional<std::uint64_t> unsigned_value;
    std::optional<std::int64_t> enum_value;
    std::string enum_name;
    std::string string_value;
    std::vector<std::uint64_t> aval_words;
    std::vector<std::uint64_t> bval_words;
};

struct ScvExtensionSnapshot {
    ScvObjectId object;
    std::vector<ScvExtensionNode> nodes;
};

[[nodiscard]] std::optional<ScvExtensionSnapshot> capture_scv_extensions(
    const ScvNativeSmartPtrRegistry& registry,
    ScvNativeSmartPtrHandle handle,
    const ScvExtensionLimits& limits,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
