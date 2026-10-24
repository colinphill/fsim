// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace fsim::support {

/// The design library a unit was compiled into: an empty library name is the
/// working library, WORK (IEEE 1076-2008 13.2; IEEE 1800-2017 33.3).
[[nodiscard]] constexpr std::string_view normalized_library(
    const std::string_view library) noexcept
{
    return library.empty() ? std::string_view { "work" } : library;
}

}  // namespace fsim::support
