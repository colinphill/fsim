// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers of the standard library artifact build (gate BD build D2).
#pragma once

#include "application_standard_library_artifact.hpp"

#include <string>

namespace fsim::app {

/// vhdl::Unit::standard_package_revision of the units of `standard`'s
/// artifact: fsim-stdlib:<cache version>:<edition>:<sources digest>.
[[nodiscard]] std::string standard_library_package_revision(
    frontend::VhdlStandard standard);

/// Whether `revision` is a standard_library_package_revision (of any
/// edition): such a package is checked against this build's precompiled
/// library, so the editions of one family may differ there.
[[nodiscard]] bool is_standard_library_package_revision(
    std::string_view revision) noexcept;

} // namespace fsim::app
