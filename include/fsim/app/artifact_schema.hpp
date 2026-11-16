// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// The payload schemas of a design artifact. They live apart from
// design_artifact.hpp so that a schema bump recompiles only the codecs and the
// tests that name a schema.

namespace fsim::app {

inline constexpr std::uint32_t kRuntimeStateSchema = 129;
inline constexpr std::uint32_t kSemanticStateSchema = 6;
inline constexpr std::uint32_t kDesignIrStateSchema = 15;
inline constexpr std::uint32_t kCompiledHirBundleSchema = 9;
inline constexpr std::uint32_t kSystemVerilogConstraintHirStateSchema = 35;
inline constexpr std::uint32_t kSystemVerilogCoverageStateSchema = 7;
inline constexpr std::uint32_t kSystemVerilogUvmStateSchema = 3;
inline constexpr std::uint32_t kSystemVerilogClassSpecializationStateSchema = 5;
inline constexpr std::uint32_t kVhdlHirStateSchema = 17;

} // namespace fsim::app
