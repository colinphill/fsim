// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_percentage.hpp"
#include "fsim/frontend/coverage_options.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
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

    // The coverage `covered` / `eligible` as a percentage (IEEE 1800-2023
    // 19.11): the goal is the target that coverage is compared with (Table
    // 19-1, Table 19-3), it does not scale the value. `exact_covered` is
    // `covered` before any rounding of its terms, in the same units; it
    // defaults to `covered`.
    SystemVerilogCoveragePercentage percentage(
        const std::uint64_t covered,
        const std::uint64_t eligible,
        const std::uint32_t goal,
        const std::optional<double> exact_covered = std::nullopt)
    {
        SystemVerilogCoveragePercentage result;
        result.goal = goal;
        if (eligible == 0U)
            return result;
        result.empty = false;
        result.set_percent(std::min(
            100.0,
            100.0 * exact_covered.value_or(static_cast<double>(covered))
                / static_cast<double>(eligible)));
        result.basis_points = std::min(
            full_coverage_basis_points,
            rounded_ratio(covered, eligible, full_coverage_basis_points));
        result.goal_reached = result.basis_points >= goal * 100U;
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
        const SystemVerilogCoverageDeclaration& item,
        const SystemVerilogCoverageOptionValues& options)
    {
        SystemVerilogCoverageDeclarationPercentage result;
        result.coverage_declaration_index = item.declaration_index;
        result.kind = item.kind;
        result.identity = declaration.canonical_identity + "::" + item.name;

        // An item's coverage is the share of its bins covered (19.11.1,
        // 19.11.2): each bin counts once; the item's weight applies only in
        // the covergroup's average (19.11).
        if (item.kind == SystemVerilogCoverageDeclarationKind::Coverpoint) {
            for (const auto& bin : item.bins) {
                if (bin.kind != SystemVerilogCoverageBinKind::Regular) {
                    continue;
                }
                if (bin.selection
                    == SystemVerilogCoverageBinSelection::Automatic) {
                    for (const auto& hit : instance.bin_hits) {
                        if (hit.coverage_declaration_index != item.declaration_index
                            || hit.bin_declaration_index != bin.declaration_index) {
                            continue;
                        }
                        result.eligible_weight = saturating_add(result.eligible_weight, 1U);
                        if (hit.covered) {
                            result.covered_weight = saturating_add(result.covered_weight, 1U);
                        }
                    }
                } else {
                    result.eligible_weight = saturating_add(result.eligible_weight, 1U);
                    if (coverpoint_bin_covered(instance, item, bin)) {
                        result.covered_weight = saturating_add(result.covered_weight, 1U);
                    }
                }
            }
        } else if (item.bins.empty()) {
            for (const auto& state : instance.cross_bin_state) {
                if (state.coverage_declaration_index != item.declaration_index
                    || state.excluded) {
                    continue;
                }
                result.eligible_weight = saturating_add(result.eligible_weight, 1U);
                if (state.covered) {
                    result.covered_weight = saturating_add(result.covered_weight, 1U);
                }
            }
        } else {
            for (const auto& bin : item.bins) {
                if (bin.kind != SystemVerilogCoverageBinKind::Regular) {
                    continue;
                }
                result.eligible_weight = saturating_add(result.eligible_weight, 1U);
                const auto covered = std::ranges::any_of(
                    instance.cross_bin_state,
                    [&](const SystemVerilogCoverageCrossBinState& state) {
                        return state.coverage_declaration_index
                            == item.declaration_index
                            && state.bin_declaration_index == bin.declaration_index
                            && !state.excluded && state.covered;
                    });
                if (covered) {
                    result.covered_weight = saturating_add(result.covered_weight, 1U);
                }
            }
            if (declaration.effective_cross_retain_auto_bins) {
                for (const auto& state : instance.cross_bin_state) {
                    if (state.coverage_declaration_index
                            != item.declaration_index
                        || state.bin_declaration_index || state.excluded) {
                        continue;
                    }
                    result.eligible_weight = saturating_add(result.eligible_weight, 1U);
                    if (state.covered) {
                        result.covered_weight = saturating_add(result.covered_weight, 1U);
                    }
                }
            }
        }
        result.coverage = percentage(
            result.covered_weight, result.eligible_weight, options.goal);
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

    // The coverage of `instance` with options `options`: the weighted
    // average of its coverpoints and crosses, by each one's weight (19.11).
    SystemVerilogCovergroupInstancePercentage instance_percentage(
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCovergroupInstance& instance,
        const SystemVerilogCovergroupInstanceOptions& options)
    {
        SystemVerilogCovergroupInstancePercentage result;
        result.runtime_identity = instance.runtime_identity;
        std::uint64_t weighted_coverage { };
        double exact_weighted_coverage { };
        std::uint64_t total_weight { };
        result.declarations.reserve(declaration.coverage_declarations.size());
        for (std::size_t index = 0U;
            index < declaration.coverage_declarations.size(); ++index) {
            const auto& item = declaration.coverage_declarations[index];
            const auto& item_options = options.items[index];
            auto item_result = declaration_percentage(
                declaration, instance, item, item_options);
            if (item_options.weight != 0U) {
                total_weight = saturating_add(total_weight, item_options.weight);
                weighted_coverage = saturating_add(
                    weighted_coverage,
                    static_cast<std::uint64_t>(item_result.coverage.basis_points)
                        * item_options.weight);
                exact_weighted_coverage += item_result.coverage.percent() * 100.0
                    * static_cast<double>(item_options.weight);
            }
            result.declarations.push_back(std::move(item_result));
        }
        result.coverage = percentage(
            weighted_coverage,
            total_weight * full_coverage_basis_points,
            options.covergroup.goal,
            exact_weighted_coverage);
        return result;
    }

} // namespace

SystemVerilogCovergroupInstancePercentage
calculate_systemverilog_covergroup_instance_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const SystemVerilogCovergroupInstance& instance)
{
    if (instance.declaration_identity != declaration.canonical_identity) {
        SystemVerilogCovergroupInstancePercentage result;
        result.runtime_identity = instance.runtime_identity;
        return result;
    }
    return instance_percentage(declaration, instance,
        systemverilog_covergroup_options(declaration, instance));
}

namespace {

    // The instances of `declaration` among `instances`, in runtime identity
    // order.
    std::vector<const SystemVerilogCovergroupInstance*> declaration_instances(
        const SystemVerilogCovergroupDeclaration& declaration,
        const std::span<const SystemVerilogCovergroupInstance> instances)
    {
        std::vector<const SystemVerilogCovergroupInstance*> ordered;
        for (const auto& instance : instances) {
            if (instance.declaration_identity == declaration.canonical_identity) {
                ordered.push_back(&instance);
            }
        }
        std::ranges::sort(
            ordered, { }, &SystemVerilogCovergroupInstance::runtime_identity);
        return ordered;
    }

    // The union of the hit state of `ordered` (19.11.3).
    SystemVerilogCovergroupInstance merged_instance(
        const SystemVerilogCovergroupDeclaration& declaration,
        const std::span<const SystemVerilogCovergroupInstance* const> ordered)
    {
        SystemVerilogCovergroupInstance merged;
        merged.declaration_identity = declaration.canonical_identity;
        merged.runtime_identity = declaration.canonical_identity + "@<merged>";
        for (const auto* instance : ordered) {
            merge_instance_state(merged, *instance);
        }
        return merged;
    }

    // The type coverage of a covergroup or of one of its coverage items
    // (19.11.3) with type_option.merge_instances false: the weighted
    // average sum(Wi * Ii) / sum(Wi) of the coverage Ii `coverage` gives
    // for each instance at the weight Wi `weight` gives, at `goal`.
    template <typename Coverage, typename Weight>
    SystemVerilogCoveragePercentage weighted_type_coverage(
        const SystemVerilogCovergroupDeclaration& declaration,
        const std::span<const SystemVerilogCovergroupInstance* const> ordered,
        const std::uint32_t goal,
        const Coverage& coverage,
        const Weight& weight)
    {
        std::uint64_t sum { };
        std::uint64_t total_weight { };
        double exact_sum { };
        bool empty { true };
        for (const auto* instance : ordered) {
            const auto options
                = systemverilog_covergroup_options(declaration, *instance);
            const auto instance_weight = weight(options);
            const auto instance_coverage = coverage(*instance, options);
            empty = empty && instance_coverage.empty;
            if (instance_weight == 0U) {
                continue;
            }
            total_weight = saturating_add(total_weight, instance_weight);
            sum = saturating_add(sum,
                static_cast<std::uint64_t>(instance_coverage.basis_points)
                    * instance_weight);
            exact_sum += instance_coverage.percent() * 100.0
                * static_cast<double>(instance_weight);
        }
        if (total_weight == 0U || empty) {
            SystemVerilogCoveragePercentage result;
            result.goal = goal;
            return result;
        }
        return percentage(rounded_ratio(sum, total_weight, 1U),
            full_coverage_basis_points, goal,
            exact_sum / static_cast<double>(total_weight));
    }

} // namespace

SystemVerilogCovergroupTypePercentage
calculate_systemverilog_covergroup_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::span<const SystemVerilogCovergroupInstance> instances,
    const SystemVerilogCovergroupTypeOptions* const type_options)
{
    const auto options = type_options != nullptr
        ? *type_options
        : systemverilog_covergroup_initial_type_options(declaration);
    SystemVerilogCovergroupTypePercentage result;
    result.declaration_identity = declaration.canonical_identity;
    result.per_instance = declaration.effective_per_instance;
    result.merge_instances = options.merge_instances;
    const auto ordered = declaration_instances(declaration, instances);
    result.instances.reserve(ordered.size());
    for (const auto* instance : ordered) {
        result.instances.push_back(
            calculate_systemverilog_covergroup_instance_percentage(
                declaration, *instance));
    }
    result.coverage.goal = options.covergroup.goal;
    if (ordered.empty())
        return result;
    if (options.merge_instances) {
        const auto merged = merged_instance(declaration, ordered);
        const auto merged_result = instance_percentage(declaration, merged,
            systemverilog_covergroup_type_options(declaration, options));
        result.coverage = merged_result.coverage;
        return result;
    }
    // Each instance's coverage at its option.weight (19.11.3).
    result.coverage = weighted_type_coverage(declaration, ordered,
        options.covergroup.goal,
        [&](const SystemVerilogCovergroupInstance& instance,
            const SystemVerilogCovergroupInstanceOptions& instance_options) {
            return instance_percentage(declaration, instance, instance_options)
                .coverage;
        },
        [](const SystemVerilogCovergroupInstanceOptions& instance_options) {
            return instance_options.covergroup.weight;
        });
    return result;
}

SystemVerilogCoveragePercentage
calculate_systemverilog_coverage_item_type_percentage(
    const SystemVerilogCovergroupDeclaration& declaration,
    const std::span<const SystemVerilogCovergroupInstance> instances,
    const std::size_t item,
    const SystemVerilogCovergroupTypeOptions* const type_options)
{
    if (item >= declaration.coverage_declarations.size()) {
        throw std::out_of_range { "covergroup coverage item is unavailable" };
    }
    const auto options = type_options != nullptr
        ? *type_options
        : systemverilog_covergroup_initial_type_options(declaration);
    const auto& coverage_item = declaration.coverage_declarations[item];
    const auto goal = options.items.at(item).goal;
    const auto ordered = declaration_instances(declaration, instances);
    if (ordered.empty()) {
        SystemVerilogCoveragePercentage result;
        result.goal = goal;
        return result;
    }
    if (options.merge_instances) {
        // The item of the merged instance (19.11.3).
        const auto merged = merged_instance(declaration, ordered);
        return declaration_percentage(declaration, merged, coverage_item,
            systemverilog_covergroup_type_options(declaration, options)
                .items[item])
            .coverage;
    }
    // The item's coverage in each instance at the item's option.weight in
    // that instance (19.11.3).
    return weighted_type_coverage(declaration, ordered, goal,
        [&](const SystemVerilogCovergroupInstance& instance,
            const SystemVerilogCovergroupInstanceOptions& instance_options) {
            return declaration_percentage(declaration, instance, coverage_item,
                instance_options.items[item])
                .coverage;
        },
        [&](const SystemVerilogCovergroupInstanceOptions& instance_options) {
            return instance_options.items[item].weight;
        });
}

SystemVerilogCoveragePercentage
calculate_systemverilog_overall_coverage_percentage(
    const std::span<const SystemVerilogCovergroupDeclaration> declarations,
    const std::span<const SystemVerilogCovergroupInstance> instances,
    const std::span<const SystemVerilogCovergroupTypeOptions> type_options)
{
    if (!type_options.empty() && type_options.size() != declarations.size()) {
        throw std::logic_error {
            "covergroup type options do not match the declarations"
        };
    }
    std::uint64_t weighted_coverage { };
    double exact_weighted_coverage { };
    std::uint64_t total_weight { };
    for (std::size_t index = 0U; index < declarations.size(); ++index) {
        const auto& declaration = declarations[index];
        const auto* const options
            = type_options.empty() ? nullptr : &type_options[index];
        const auto weight = options != nullptr
            ? options->covergroup.weight
            : declaration.effective_type_weight;
        if (weight == 0U)
            continue;
        const auto coverage = calculate_systemverilog_covergroup_type_percentage(
            declaration, instances, options)
                                  .coverage;
        if (coverage.empty)
            continue;
        total_weight = saturating_add(total_weight, weight);
        weighted_coverage = saturating_add(
            weighted_coverage,
            static_cast<std::uint64_t>(coverage.basis_points) * weight);
        exact_weighted_coverage += coverage.percent() * 100.0
            * static_cast<double>(weight);
    }
    return percentage(
        weighted_coverage,
        total_weight * full_coverage_basis_points,
        100U,
        exact_weighted_coverage);
}

} // namespace fsim::frontend
