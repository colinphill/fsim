// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/design.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fsim::library {

// Stable little-endian owning-unit schema. This is independent of the host
// compiler ABI and the LLVM native-object schema.
inline constexpr std::uint32_t kOwningUnitSchemaVersion = 1;

struct SourceNameMapping {
  std::string producer_name;
  std::string logical_name;
};

// Rewrites producer-absolute names in every nested source span. Relative
// language-level names (including `line mappings) remain unchanged.
[[nodiscard]] bool relocate_unit_sources(
    frontend::DesignUnit& unit,
    std::span<const SourceNameMapping> mappings,
    diagnostic::Engine& diagnostics);

// Serializes one elaboration-ready owning front-end unit. Source spans must
// already use relocatable logical names; producer-absolute paths are rejected.
[[nodiscard]] std::optional<std::string> serialize_portable_unit(
    const frontend::DesignUnit& unit,
    diagnostic::Engine& diagnostics);

// Restores one owning unit without invoking an HDL preprocessor or parser.
// Unknown schemas, truncation, trailing bytes, and out-of-range values reject
// transactionally.
[[nodiscard]] std::optional<frontend::DesignUnit> deserialize_portable_unit(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::library
