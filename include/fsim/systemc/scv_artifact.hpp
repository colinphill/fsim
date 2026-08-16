// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <cstddef>
#include <string_view>

namespace fsim::systemc {

inline constexpr std::size_t scv_artifact_identity_limit = 4096;

// Validates the bounded canonical SCV producer identity before an artifact
// consumer opens or publishes any native payload.
[[nodiscard]] bool validate_scv_artifact_compatibility(
    std::string_view candidate,
    std::string_view artifact_kind,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
