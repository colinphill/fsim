// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_options.hpp"
#include "fsim/frontend/design.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::frontend {

// A coverage percentage (IEEE 1800-2023 19.11): the value is not scaled by
// the goal; the goal only decides whether the target is reached (Table 19-1,
// Table 19-3).
struct SystemVerilogCoveragePercentage {
    // The percentage rounded to hundredths of a percent.
    std::uint32_t basis_points { };
    std::uint32_t goal { 100U };
    bool empty { true };
    // The percentage is at least the goal.
    bool goal_reached { };
    // The unrounded percentage in [0, 100], the real value the coverage
    // methods return (19.8, 19.9, 19.11), kept as its IEEE 754 bits so the
    // artifact stores it exactly.
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

/// The type coverage of `declaration` over its instances among `instances`
/// (IEEE 1800-2023 19.11.3), with the type options `type_options` (the
/// definition's when null).
[[nodiscard]] SystemVerilogCovergroupTypePercentage
calculate_systemverilog_covergroup_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupInstance> instances,
    const SystemVerilogCovergroupTypeOptions* type_options = nullptr);

/// The type coverage of coverage item `item` (an index of
/// `declaration.coverage_declarations`) over the instances of `declaration`
/// among `instances` (19.9, 19.11.3): the item of the merged instance when
/// type_option.merge_instances is set, else the item's coverage in each
/// instance weighted by the item's option.weight in that instance.
[[nodiscard]] SystemVerilogCoveragePercentage
calculate_systemverilog_coverage_item_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    std::span<const SystemVerilogCovergroupInstance> instances,
    std::size_t item,
    const SystemVerilogCovergroupTypeOptions* type_options = nullptr);

/// Return the type-weighted overall functional coverage. Covergroup types
/// without a materialized instance do not contribute to the simulation-wide
/// result, and a zero type weight excludes that type from the aggregate.
/// `type_options`, when not empty, holds the type options of each of
/// `declarations`, in the same order.
[[nodiscard]] SystemVerilogCoveragePercentage
calculate_systemverilog_overall_coverage_percentage(
    std::span<const SystemVerilogCovergroupDeclaration> declarations,
    std::span<const SystemVerilogCovergroupInstance> instances,
    std::span<const SystemVerilogCovergroupTypeOptions> type_options = { });

} // namespace fsim::frontend
