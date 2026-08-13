// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_persistence.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>

namespace fsim::frontend {
namespace {

    void collect_class_declarations(
        const SystemVerilogClassDeclaration& owner,
        std::vector<SystemVerilogCovergroupDeclaration>& declarations)
    {
        declarations.insert(
            declarations.end(), owner.covergroups.begin(), owner.covergroups.end());
        for (const auto& nested : owner.nested_classes) {
            collect_class_declarations(nested, declarations);
        }
    }

} // namespace

void refresh_systemverilog_coverage_reports(
    SystemVerilogCoverageState& state)
{
    state.reports.clear();
    state.reports.reserve(state.declarations.size());
    for (const auto& declaration : state.declarations) {
        std::vector<SystemVerilogCovergroupInstance> instances;
        std::ranges::copy_if(
            state.instances,
            std::back_inserter(instances),
            [&](const SystemVerilogCovergroupInstance& instance) {
                return instance.declaration_identity
                    == declaration.canonical_identity;
            });
        state.reports.push_back(build_systemverilog_coverage_report(
            declaration, instances));
    }
}

bool merge_systemverilog_coverage_state(
    SystemVerilogCoverageState& destination,
    const SystemVerilogCoverageState& source,
    std::string& error)
{
    const auto saturating_add = [](const std::uint64_t lhs,
                                    const std::uint64_t rhs) {
        return rhs > std::numeric_limits<std::uint64_t>::max() - lhs
            ? std::numeric_limits<std::uint64_t>::max()
            : lhs + rhs;
    };
    std::set<std::string, std::less<>> destination_declarations;
    for (const auto& declaration : destination.declarations) {
        if (declaration.canonical_identity.empty()
            || !destination_declarations.insert(
                                            declaration.canonical_identity)
                .second) {
            error = "live coverage model has duplicate declaration identities";
            return false;
        }
    }
    std::set<std::string, std::less<>> source_declarations;
    for (const auto& declaration : source.declarations) {
        if (declaration.canonical_identity.empty()
            || !source_declarations.insert(
                                       declaration.canonical_identity)
                .second) {
            error = "coverage database has duplicate declaration identities";
            return false;
        }
    }
    if (source_declarations != destination_declarations) {
        error = "coverage database describes a different declaration model";
        return false;
    }

    auto merged = destination;
    std::set<std::string, std::less<>> source_instances;
    for (const auto& source_instance : source.instances) {
        if (source_instance.runtime_identity.empty()
            || !source_instances.insert(source_instance.runtime_identity).second) {
            error = "coverage database has duplicate instance identities";
            return false;
        }
        auto destination_instance = std::ranges::find(
            merged.instances, source_instance.runtime_identity,
            &SystemVerilogCovergroupInstance::runtime_identity);
        if (destination_instance == merged.instances.end()
            || destination_instance->declaration_identity
                != source_instance.declaration_identity) {
            error = "coverage database describes a different instance model";
            return false;
        }
        for (const auto& source_hit : source_instance.bin_hits) {
            auto hit = std::ranges::find(
                destination_instance->bin_hits, source_hit.identity,
                &SystemVerilogCoverageBinHit::identity);
            if (hit == destination_instance->bin_hits.end()) {
                destination_instance->bin_hits.push_back(source_hit);
                continue;
            }
            if (hit->coverage_declaration_index
                    != source_hit.coverage_declaration_index
                || hit->bin_declaration_index
                    != source_hit.bin_declaration_index
                || hit->at_least != source_hit.at_least) {
                error = "coverage database bin metadata does not match the live model";
                return false;
            }
            hit->hit_count = saturating_add(
                hit->hit_count, source_hit.hit_count);
            hit->covered = hit->hit_count >= hit->at_least;
        }
        for (const auto& source_cross : source_instance.cross_bin_state) {
            auto cross = std::ranges::find(
                destination_instance->cross_bin_state,
                source_cross.identity,
                &SystemVerilogCoverageCrossBinState::identity);
            if (cross == destination_instance->cross_bin_state.end()) {
                destination_instance->cross_bin_state.push_back(source_cross);
                continue;
            }
            if (cross->coverage_declaration_index
                    != source_cross.coverage_declaration_index
                || cross->bin_declaration_index
                    != source_cross.bin_declaration_index
                || cross->operand_bin_identities
                    != source_cross.operand_bin_identities
                || cross->at_least != source_cross.at_least) {
                error = "coverage database cross metadata does not match the live model";
                return false;
            }
            cross->hit_count = saturating_add(
                cross->hit_count, source_cross.hit_count);
            cross->exclusion_count = saturating_add(
                cross->exclusion_count, source_cross.exclusion_count);
            cross->excluded = cross->excluded || source_cross.excluded;
            cross->covered = !cross->excluded
                && cross->hit_count >= cross->at_least;
        }
    }
    destination = std::move(merged);
    refresh_systemverilog_coverage_reports(destination);
    error.clear();
    return true;
}

SystemVerilogCoverageState capture_systemverilog_coverage_state(
    const ParsedDesign& design)
{
    SystemVerilogCoverageState state;
    for (const auto& unit : design.units) {
        state.declarations.insert(
            state.declarations.end(),
            unit.systemverilog_covergroups.begin(),
            unit.systemverilog_covergroups.end());
        for (const auto& owner : unit.systemverilog_classes) {
            collect_class_declarations(owner, state.declarations);
        }
    }
    for (const auto& owner : design.systemverilog_classes) {
        collect_class_declarations(owner, state.declarations);
    }
    state.instances = design.systemverilog_covergroup_instances;
    std::ranges::sort(
        state.declarations, { },
        &SystemVerilogCovergroupDeclaration::canonical_identity);
    std::ranges::sort(
        state.instances, { }, &SystemVerilogCovergroupInstance::runtime_identity);
    refresh_systemverilog_coverage_reports(state);
    return state;
}

} // namespace fsim::frontend
