// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace fsim::frontend::detail {

[[nodiscard]] inline char ctype_lower_char(const char value) noexcept
{
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] inline std::string ctype_lower_copy(
    const std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        result.push_back(ctype_lower_char(character));
    }
    return result;
}

[[nodiscard]] inline bool ctype_upper_equal(
    const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (std::toupper(static_cast<unsigned char>(left[index]))
            != std::toupper(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace fsim::frontend::detail
