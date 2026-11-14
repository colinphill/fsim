// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

// The payload schemas of a design artifact. They live apart from
// design_artifact.hpp so that a schema bump recompiles only the codecs and the
// tests that name a schema.

namespace fsim::app {

inline constexpr std::uint32_t kRuntimeStateSchema = 126;
inline constexpr std::uint32_t kSemanticStateSchema = 6;
inline constexpr std::uint32_t kDesignIrStateSchema = 12;
inline constexpr std::uint32_t kCompiledHirBundleSchema = 6;
inline constexpr std::uint32_t kSystemVerilogConstraintHirStateSchema = 32;
inline constexpr std::uint32_t kSystemVerilogCoverageStateSchema = 7;
inline constexpr std::uint32_t kSystemVerilogUvmStateSchema = 3;
inline constexpr std::uint32_t kSystemVerilogClassSpecializationStateSchema = 5;
inline constexpr std::uint32_t kVhdlHirStateSchema = 15;

} // namespace fsim::app
