// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::support {

/// How a compiler-served VHDL library unit (a unit of the libraries STD and
/// IEEE that fsim supplies, IEEE 1076-2019 13.2, 16) was made.
enum class VhdlStandardPackageForm : std::uint8_t {
    /// An ordinary design unit.
    none,
    /// Analyzed from the standard's source into a precompiled standard
    /// library artifact: its declarations are real, its analysis results
    /// stored, and its subprograms with native implementations carry their
    /// intrinsic (vhdl::Declaration::native_intrinsic).
    artifact,
    /// Projected by the compiler (the packages the artifacts do not yet
    /// carry: Synopsys, VITAL, fixed and float, the IEEE contexts); resolved
    /// again at every link.
    projection,
};

} // namespace fsim::support
