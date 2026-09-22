// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string_view>

namespace fsim::diagnostic {

// Stable diagnostic codes whose emitters are being moved from the removed
// syntax-tree elaborators to compiled-HIR elaboration. A code must leave this
// registry as soon as an active production emitter owns it again.
[[nodiscard]] std::span<const std::string_view>
compiled_hir_compatibility_codes() noexcept;

} // namespace fsim::diagnostic
