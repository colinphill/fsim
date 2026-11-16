// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// The payload schemas of a design artifact. They live apart from
// design_artifact.hpp so that a schema bump recompiles only the codecs and the
// tests that name a schema.

namespace fsim::app {

inline constexpr std::uint32_t kRuntimeStateSchema = 133;
inline constexpr std::uint32_t kSemanticStateSchema = 9;
inline constexpr std::uint32_t kDesignIrStateSchema = 18;
inline constexpr std::uint32_t kCompiledHirBundleSchema = 13;
inline constexpr std::uint32_t kSystemVerilogConstraintHirStateSchema = 38;
inline constexpr std::uint32_t kSystemVerilogCoverageStateSchema = 8;
inline constexpr std::uint32_t kSystemVerilogUvmStateSchema = 3;
inline constexpr std::uint32_t kSystemVerilogClassSpecializationStateSchema = 8;
inline constexpr std::uint32_t kVhdlHirStateSchema = 18;

} // namespace fsim::app
