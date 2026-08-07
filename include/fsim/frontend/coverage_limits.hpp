// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"

#include <cstddef>
#include <vector>

namespace fsim::frontend {

inline constexpr std::size_t kSystemVerilogCoverageMaximumDeclarations = 4096;
inline constexpr std::size_t kSystemVerilogCoverageMaximumBins = 65'536;
inline constexpr std::size_t kSystemVerilogCoverageMaximumCrossProduct =
    1'048'576;
inline constexpr std::size_t kSystemVerilogCoverageMaximumWork = 1'048'576;
inline constexpr std::size_t kSystemVerilogCoverageMaximumTransactionInputs =
    4096;
inline constexpr std::size_t kSystemVerilogCoverageMaximumStateRecords =
    65'536;

[[nodiscard]] bool validate_systemverilog_coverage_resources(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::vector<Diagnostic>& diagnostics);

[[nodiscard]] std::size_t systemverilog_coverage_state_records(
    const SystemVerilogCovergroupInstance& instance) noexcept;

[[nodiscard]] bool validate_systemverilog_coverage_sample_resources(
    const SystemVerilogCovergroupInstance& instance,
    const SystemVerilogCovergroupDeclaration& declaration,
    std::size_t input_count,
    std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend
