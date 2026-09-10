// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_percentage.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::frontend {
namespace {

    constexpr std::uint32_t full_coverage_basis_points = 10'000U;

    std::uint64_t saturating_add(
        const std::uint64_t left, const std::uint64_t right)
    {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return right > maximum - left ? maximum : left + right;
    }

    std::uint32_t rounded_ratio(
        const std::uint64_t numerator,
        const std::uint64_t denominator,
        const std::uint32_t scale)
    {
        if (denominator == 0U)
            return 0U;
        const auto scaled = numerator * scale;
        const auto quotient = scaled / denominator;
        const auto remainder = scaled % denominator;
        return static_cast<std::uint32_t>(
            quotient + (remainder >= (denominator + 1U) / 2U ? 1U : 0U));
    }

    SystemVerilogCoveragePercentage percentage(
        const std::uint64_t covered,
        const std::uint64_t eligible,
        const std::uint32_t goal)
    {
        SystemVerilogCoveragePercentage result;
        result.goal = goal;
        if (eligible == 0U)
            return result;
        result.empty = false;
        result.raw_basis_points = std::min(
            full_coverage_basis_points,
            rounded_ratio(covered, eligible, full_coverage_basis_points));
        result.goal_reached = result.raw_basis_points >= goal * 100U;
        result.basis_points = goal == 0U
            ? full_coverage_basis_points
            : std::min(
                  full_coverage_basis_points,
                  rounded_ratio(result.raw_basis_points, goal, 100U));
        return result;
    }

    bool coverpoint_bin_covered(
        const SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCoverageDeclaration& item,
        const SystemVerilogCoverageBin& bin)
    {
        return std::ranges::any_of(
            instance.bin_hits,
            [&](const SystemVerilogCoverageBinHit& hit) {
                return hit.coverage_declaration_index == item.declaration_index
                    && hit.bin_declaration_index == bin.declaration_index
                    && hit.covered;
            });
    }

    SystemVerilogCoverageDeclarationPercentage declaration_percentage(
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCoverageDeclaration& item)
    {
        SystemVerilogCoverageDeclarationPercentage result;
        result.coverage_declaration_index = item.declaration_index;
        result.kind = item.kind;
        result.identity = declaration.canonical_identity + "::" + item.name;

        if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
            for (const auto& bin : item.bins) {
                if (bin.kind != SystemVerilogCoverageBinKind::Regular
                    || bin.weight == 0U) {
                    continue;
                }
                if (bin.selection
                    == SystemVerilogCoverageBinSelection::Automatic) {
                    for (const auto& hit : instance.bin_hits) {
                        if (hit.coverage_declaration_index != item.declaration_index
                            || hit.bin_declaration_index != bin.declaration_index) {
                            continue;
                        }
                        result.eligible_weight = saturating_add(result.eligible_weight, bin.weight);
                        if (hit.covered) {
                            result.covered_weight = saturating_add(result.covered_weight, bin.weight);
                        }
                    }
                } else {
                    result.eligible_weight = saturating_add(result.eligible_weight, bin.weight);
                    if (coverpoint_bin_covered(instance, item, bin)) {
                        result.covered_weight = saturating_add(result.covered_weight, bin.weight);
                    }
                }
            }
        } else if (item.bins.empty()) {
            for (const auto& state : instance.cross_bin_state) {
                if (state.coverage_declaration_index != item.declaration_index
                    || state.excluded || state.weight == 0U) {
                    continue;
                }
                result.eligible_weight = saturating_add(result.eligible_weight, state.weight);
                if (state.covered) {
                    result.covered_weight = saturating_add(result.covered_weight, state.weight);
                }
            }
        } else {
            for (const auto& bin : item.bins) {
                if (bin.kind != SystemVerilogCoverageBinKind::Regular
                    || bin.weight == 0U) {
                    continue;
                }
                result.eligible_weight = saturating_add(result.eligible_weight, bin.weight);
                const auto covered = std::ranges::any_of(
                    instance.cross_bin_state,
                    [&](const SystemVerilogCoverageCrossBinState& state) {
                        return state.coverage_declaration_index
                            == item.declaration_index
                            && state.bin_declaration_index == bin.declaration_index
                            && !state.excluded && state.covered;
                    });
                if (covered) {
                    result.covered_weight = saturating_add(result.covered_weight, bin.weight);
                }
            }
            if (declaration.effective_cross_retain_auto_bins) {
                for (const auto& state : instance.cross_bin_state) {
                    if (state.coverage_declaration_index
                            != item.declaration_index
                        || state.bin_declaration_index || state.excluded
                        || state.weight == 0U) {
                        continue;
                    }
                    result.eligible_weight = saturating_add(
                        result.eligible_weight, state.weight);
                    if (state.covered) {
                        result.covered_weight = saturating_add(
                            result.covered_weight, state.weight);
                    }
                }
            }
        }
        result.coverage = percentage(
            result.covered_weight, result.eligible_weight, item.effective_goal);
        return result;
    }

    void merge_instance_state(
        SystemVerilogCovergroupInstance& merged,
        const SystemVerilogCovergroupInstance& instance)
    {
        for (const auto& hit : instance.bin_hits) {
            auto found = std::ranges::find(
                merged.bin_hits, hit.identity,
                &SystemVerilogCoverageBinHit::identity);
            if (found == merged.bin_hits.end()) {
                merged.bin_hits.push_back(hit);
                continue;
            }
            found->hit_count = saturating_add(found->hit_count, hit.hit_count);
            found->covered = found->hit_count >= found->at_least;
        }
        for (const auto& state : instance.cross_bin_state) {
            auto found = std::ranges::find(
                merged.cross_bin_state, state.identity,
                &SystemVerilogCoverageCrossBinState::identity);
            if (found == merged.cross_bin_state.end()) {
                merged.cross_bin_state.push_back(state);
                continue;
            }
            found->hit_count = saturating_add(found->hit_count, state.hit_count);
            found->exclusion_count = saturating_add(found->exclusion_count, state.exclusion_count);
            found->excluded = found->excluded || state.excluded;
            found->covered = !found->excluded && found->hit_count >= found->at_least;
        }
    }

} // namespace

SystemVerilogCovergroupInstancePercentage
calculate_systemverilog_covergroup_instance_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupInstance& instance)
{
    SystemVerilogCovergroupInstancePercentage result;
    result.runtime_identity = instance.runtime_identity;
    if (instance.declaration_identity != declaration.canonical_identity) {
        return result;
    }
    std::uint64_t weighted_coverage { };
    std::uint64_t total_weight { };
    result.declarations.reserve(declaration.coverage_declarations.size());
    for (const auto& item : declaration.coverage_declarations) {
        auto item_result = declaration_percentage(declaration, instance, item);
        if (item.effective_weight != 0U) {
            total_weight = saturating_add(total_weight, item.effective_weight);
            weighted_coverage = saturating_add(
                weighted_coverage,
                static_cast<std::uint64_t>(item_result.coverage.basis_points)
                    * item.effective_weight);
        }
        result.declarations.push_back(std::move(item_result));
    }
    result.coverage = percentage(
        weighted_coverage,
        total_weight * full_coverage_basis_points,
        declaration.effective_instance_goal);
    return result;
}

SystemVerilogCovergroupTypePercentage
calculate_systemverilog_covergroup_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::span<const SystemVerilogCovergroupInstance> instances)
{
    SystemVerilogCovergroupTypePercentage result;
    result.declaration_identity = declaration.canonical_identity;
    result.per_instance = declaration.effective_per_instance;
    result.merge_instances = declaration.effective_merge_instances;
    std::vector<const SystemVerilogCovergroupInstance*> ordered;
    for (const auto& instance : instances) {
        if (instance.declaration_identity == declaration.canonical_identity) {
            ordered.push_back(&instance);
        }
    }
    std::ranges::sort(
        ordered, { }, &SystemVerilogCovergroupInstance::runtime_identity);
    result.instances.reserve(ordered.size());
    for (const auto* instance : ordered) {
        result.instances.push_back(
            calculate_systemverilog_covergroup_instance_percentage(
                declaration, *instance));
    }
    if (ordered.empty())
        return result;

    std::uint32_t type_raw { };
    bool empty { true };
    if (declaration.effective_merge_instances) {
        SystemVerilogCovergroupInstance merged;
        merged.declaration_identity = declaration.canonical_identity;
        merged.runtime_identity = declaration.canonical_identity + "@<merged>";
        for (const auto* instance : ordered) {
            merge_instance_state(merged, *instance);
        }
        const auto merged_result = calculate_systemverilog_covergroup_instance_percentage(
            declaration, merged);
        type_raw = merged_result.coverage.basis_points;
        empty = merged_result.coverage.empty;
    } else {
        std::uint64_t sum { };
        for (const auto& instance : result.instances) {
            sum = saturating_add(sum, instance.coverage.basis_points);
            empty = empty && instance.coverage.empty;
        }
        type_raw = rounded_ratio(sum, result.instances.size(), 1U);
    }
    if (empty)
        return result;
    result.coverage = percentage(
        type_raw, full_coverage_basis_points,
        declaration.effective_type_goal);
    return result;
}

SystemVerilogCoveragePercentage
calculate_systemverilog_overall_coverage_percentage(
    const std::span<const SystemVerilogCovergroupDeclaration> declarations,
    const std::span<const SystemVerilogCovergroupInstance> instances)
{
    std::uint64_t weighted_coverage { };
    std::uint64_t total_weight { };
    for (const auto& declaration : declarations) {
        if (declaration.effective_type_weight == 0U)
            continue;
        const auto coverage = calculate_systemverilog_covergroup_type_percentage(
            declaration, instances)
                                  .coverage;
        if (coverage.empty)
            continue;
        total_weight = saturating_add(
            total_weight, declaration.effective_type_weight);
        weighted_coverage = saturating_add(
            weighted_coverage,
            static_cast<std::uint64_t>(coverage.basis_points)
                * declaration.effective_type_weight);
    }
    return percentage(
        weighted_coverage,
        total_weight * full_coverage_basis_points,
        100U);
}

SystemVerilogCoveragePercentage
calculate_systemverilog_overall_instance_coverage_percentage(
    const std::span<const SystemVerilogCovergroupDeclaration> declarations,
    const std::span<const SystemVerilogCovergroupInstance> instances)
{
    std::uint64_t weighted_coverage { };
    std::uint64_t total_weight { };
    for (const auto& declaration : declarations) {
        if (declaration.effective_instance_weight == 0U)
            continue;
        for (const auto& instance : instances) {
            if (instance.declaration_identity
                != declaration.canonical_identity) {
                continue;
            }
            const auto coverage
                = calculate_systemverilog_covergroup_instance_percentage(
                    declaration, instance)
                      .coverage;
            if (coverage.empty)
                continue;
            total_weight = saturating_add(
                total_weight, declaration.effective_instance_weight);
            weighted_coverage = saturating_add(
                weighted_coverage,
                static_cast<std::uint64_t>(coverage.basis_points)
                    * declaration.effective_instance_weight);
        }
    }
    return percentage(
        weighted_coverage,
        total_weight * full_coverage_basis_points,
        100U);
}

} // namespace fsim::frontend
