// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

/// The number of index steps between two bounds, in either direction,
/// without signed overflow.
[[nodiscard]] constexpr std::uint64_t index_distance(
    const std::int64_t left, const std::int64_t right) noexcept
{
    return left >= right
        ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right)
        : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left);
}

} // namespace fsim::support
