// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cctype>
#include <string_view>

namespace fsim::support {

[[nodiscard]] inline std::string_view trim_ctype_whitespace(
    std::string_view value) noexcept
{
    while (!value.empty()
        && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    while (!value.empty()
        && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return value;
}

} // namespace fsim::support
