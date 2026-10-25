// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cstdint>

namespace fsim::support {

/// UFIXED_HIGH, UFIXED_LOW, SFIXED_HIGH and SFIXED_LOW of IEEE
/// fixed_generic_pkg: the index bound of the result of `operation` on
/// operands (left downto right) and (left2 downto right2).
[[nodiscard]] constexpr std::int64_t vhdl_fixed_bound(const bool high,
    const bool signed_value, const std::int64_t left,
    const std::int64_t right, const char operation, const std::int64_t left2,
    const std::int64_t right2) noexcept
{
    switch (operation) {
    case '+':
    case '-':
        return high ? std::max(left, left2) + 1 : std::min(right, right2);
    case '*':
        return high ? left + left2 + 1 : right + right2;
    case '/':
        return high ? left - right2 + (signed_value ? 1 : 0)
                    : right - left2 - (signed_value ? 0 : 1);
    case '1':
        return high ? -right + (signed_value ? 1 : 0)
                    : -left - (signed_value ? 0 : 1);
    case 'R':
    case 'r':
        return high ? std::min(left, left2) : std::min(right, right2);
    case 'M':
    case 'm':
        if (high) {
            return signed_value ? left2 : std::min(left, left2);
        }
        return std::min(right, right2);
    case 'A':
    case 'a':
    case 'N':
    case 'n':
        return high ? left + (signed_value ? 1 : 0) : right;
    default:
        return high ? left : right;
    }
}

} // namespace fsim::support
