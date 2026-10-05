// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::app::application_detail {

// Keep this producer-only cache revision out of application_internal.hpp so
// changing it does not rebuild every application translation unit.
inline constexpr std::uint32_t compiled_hir_cache_producer_revision = 9U;

} // namespace fsim::app::application_detail
