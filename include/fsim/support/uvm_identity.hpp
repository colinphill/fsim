// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace fsim::support {

/// Whether a canonical identity names a declaration of the UVM library
/// package `uvm_pkg` or a native UVM service (`@uvm-...`). A user
/// declaration that only has a `uvm_` name, or lives in a package such as
/// `ivl_uvm_pkg`, is not part of the library.
[[nodiscard]] constexpr bool is_uvm_library_identity(
    const std::string_view identity) noexcept
{
    constexpr std::string_view package { "uvm_pkg::" };
    if (identity.starts_with("@uvm-") || identity.starts_with(package)) {
        return true;
    }
    for (auto at = identity.find(package); at != std::string_view::npos;
         at = identity.find(package, at + 1U)) {
        if (at >= 2U && identity.substr(at - 2U, 2U) == "::") {
            return true;
        }
    }
    return false;
}

} // namespace fsim::support
