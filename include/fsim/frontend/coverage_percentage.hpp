// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

struct SystemVerilogCoveragePercentage {
    std::uint32_t raw_basis_points { };
    std::uint32_t basis_points { };
    std::uint32_t goal { 100U };
    bool empty { true };
    bool goal_reached { };
    // The unrounded goal-scaled percentage in [0, 100], the real value the
    // coverage methods return (IEEE 1800-2023 19.8, 19.11), kept as its
    // IEEE 754 bits so the artifact stores it exactly.
    std::uint64_t percent_bits { };

    [[nodiscard]] double percent() const noexcept
    {
        return std::bit_cast<double>(percent_bits);
    }
    void set_percent(const double value) noexcept
    {
        percent_bits = std::bit_cast<std::uint64_t>(value);
    }
};

struct SystemVerilogCoverageDeclarationPercentage {
    std::size_t coverage_declaration_index { };
    SystemVerilogCoverageDeclarationKind kind {
        SystemVerilogCoverageDeclarationKind::Coverpoint
    };
    std::string identity;
    std::uint64_t eligible_weight { };
    std::uint64_t covered_weight { };
    SystemVerilogCoveragePercentage coverage;
};

struct SystemVerilogCovergroupInstancePercentage {
    std::string runtime_identity;
    std::vector<SystemVerilogCoverageDeclarationPercentage> declarations;
    SystemVerilogCoveragePercentage coverage;
};

struct SystemVerilogCovergroupTypePercentage {
    std::string declaration_identity;
    bool per_instance { };
    bool merge_instances { };
    std::vector<SystemVerilogCovergroupInstancePercentage> instances;
    SystemVerilogCoveragePercentage coverage;
};

[[nodiscard]] SystemVerilogCovergroupInstancePercentage
calculate_systemverilog_covergroup_instance_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupInstance& instance);

[[nodiscard]] SystemVerilogCovergroupTypePercentage
calculate_systemverilog_covergroup_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupInstance> instances);

/// Return the type-weighted overall functional coverage. Covergroup types
/// without a materialized instance do not contribute to the simulation-wide
/// result, and a zero type weight excludes that type from the aggregate.
[[nodiscard]] SystemVerilogCoveragePercentage
calculate_systemverilog_overall_coverage_percentage(
    std::span<const SystemVerilogCovergroupDeclaration> declarations,
    std::span<const SystemVerilogCovergroupInstance> instances);

/// Return the instance-weighted overall functional coverage. Every
/// materialized instance contributes independently using its covergroup
/// declaration's effective instance weight.
[[nodiscard]] SystemVerilogCoveragePercentage
calculate_systemverilog_overall_instance_coverage_percentage(
    std::span<const SystemVerilogCovergroupDeclaration> declarations,
    std::span<const SystemVerilogCovergroupInstance> instances);

} // namespace fsim::frontend
