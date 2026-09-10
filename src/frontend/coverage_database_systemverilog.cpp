// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_database_systemverilog.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <string_view>

namespace fsim::frontend {
namespace {

    using artifact::CoverageDatabaseContents;
    using artifact::CoverageDatabaseIdentity;
    using artifact::CoverageDatabaseMetricFamily;
    using artifact::CoverageDatabaseMetricRecord;
    using artifact::CoverageDatabaseMetricScope;
    using artifact::CoverageDatabaseNamespace;

    bool valid_identity(const CoverageDatabaseIdentity identity) noexcept
    {
        return identity.high != 0U || identity.low != 0U;
    }

    bool valid_text(const std::string_view text, const std::size_t limit) noexcept
    {
        return !text.empty() && text.size() <= limit
            && text.find('\0') == std::string_view::npos;
    }

    CoverageDatabaseIdentity stable_identity(
        const std::string_view domain, const std::string_view value) noexcept
    {
        support::Sha256 hash;
        hash.update(domain);
        hash.update(std::string_view { "\0", 1U });
        hash.update(value);
        const auto digest = hash.finish();
        CoverageDatabaseIdentity result;
        for (std::size_t index = 0; index < 8U; ++index) {
            result.high = (result.high << 8U) | digest[index];
            result.low = (result.low << 8U) | digest[index + 8U];
        }
        return result;
    }

    std::string_view source_name(const SourceSpan& span) noexcept
    {
        return span.physical_source_name.empty()
            ? std::string_view { span.source_name }
            : std::string_view { span.physical_source_name };
    }

    bool owns_source(const CoverageDatabaseContents& contents,
        const CoverageDatabaseIdentity identity) noexcept
    {
        return std::ranges::binary_search(contents.sources, identity,
            std::ranges::less { }, &artifact::CoverageDatabaseSourceRecord::identity);
    }

    bool owns_run(const CoverageDatabaseContents& contents,
        const CoverageDatabaseIdentity identity) noexcept
    {
        return std::ranges::binary_search(contents.runs, identity,
            std::ranges::less { }, &artifact::CoverageDatabaseRunRecord::identity);
    }

    const SystemVerilogCoverageDeclaration* coverage_declaration(
        const SystemVerilogCovergroupDeclaration& declaration,
        const std::size_t index) noexcept
    {
        const auto found = std::ranges::find(declaration.coverage_declarations,
            index, &SystemVerilogCoverageDeclaration::declaration_index);
        return found == declaration.coverage_declarations.end()
            ? nullptr
            : &*found;
    }

    const SystemVerilogCoverageBin* coverage_bin(
        const SystemVerilogCoverageDeclaration& declaration,
        const std::size_t index) noexcept
    {
        const auto found = std::ranges::find(declaration.bins, index,
            &SystemVerilogCoverageBin::declaration_index);
        return found == declaration.bins.end() ? nullptr : &*found;
    }

    std::string coverpoint_bin_identity(
        const SystemVerilogCovergroupDeclaration& declaration,
        const SystemVerilogCoverageDeclaration& coverpoint,
        const SystemVerilogCoverageBin& bin)
    {
        const auto& origin = coverpoint.origin_covergroup_identity.empty()
            ? declaration.canonical_identity
            : coverpoint.origin_covergroup_identity;
        return origin + "::" + coverpoint.name + "." + bin.name;
    }

    SystemVerilogCoverageDatabaseResult fail(
        const SystemVerilogCoverageDatabaseError error,
        const std::size_t index = 0U) noexcept
    {
        return { { }, error, artifact::CoverageDatabaseModelError::None, index };
    }

} // namespace

SystemVerilogCoverageDatabaseResult project_systemverilog_coverage_namespace(
    CoverageDatabaseContents contents,
    const SystemVerilogCoverageState& state,
    const std::span<const SystemVerilogCoverageDatabaseSource> sources,
    const CoverageDatabaseIdentity run_identity,
    const SystemVerilogCoverageDatabaseLimits& limits) noexcept
{
    try {
        const auto model_validation
            = artifact::validate_coverage_database_contents(contents);
        if (!model_validation.ok()) {
            return { { },
                SystemVerilogCoverageDatabaseError::InvalidDatabaseModel,
                model_validation.error, model_validation.index };
        }
        if (!valid_identity(run_identity) || !owns_run(contents, run_identity)) {
            return fail(SystemVerilogCoverageDatabaseError::InvalidRun);
        }
        if (std::ranges::any_of(contents.metrics, [](const auto& metric) {
                return metric.name_space
                    == CoverageDatabaseNamespace::SystemVerilogFunctional;
            })
            || std::ranges::any_of(contents.exclusions, [](const auto& exclusion) {
                   return exclusion.name_space
                       == CoverageDatabaseNamespace::SystemVerilogFunctional;
               })) {
            return fail(SystemVerilogCoverageDatabaseError::NamespaceNotEmpty);
        }
        if (sources.size() > limits.maximum_source_bindings
            || state.declarations.size() > limits.maximum_declarations
            || state.instances.size() > limits.maximum_instances) {
            return fail(SystemVerilogCoverageDatabaseError::ResourceLimit);
        }

        std::map<std::string, CoverageDatabaseIdentity, std::less<>> source_map;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (!valid_text(source.source_name, limits.maximum_source_name_bytes)
                || !valid_identity(source.source_identity)
                || !owns_source(contents, source.source_identity)) {
                return fail(
                    SystemVerilogCoverageDatabaseError::InvalidSourceBinding,
                    index);
            }
            if (!source_map.emplace(source.source_name, source.source_identity)
                    .second) {
                return fail(
                    SystemVerilogCoverageDatabaseError::DuplicateSourceBinding,
                    index);
            }
        }

        std::map<std::string, const SystemVerilogCovergroupDeclaration*,
            std::less<>>
            declarations;
        for (std::size_t index = 0; index < state.declarations.size(); ++index) {
            const auto& declaration = state.declarations[index];
            if (!valid_text(declaration.canonical_identity,
                    limits.maximum_identity_bytes)) {
                return fail(
                    SystemVerilogCoverageDatabaseError::InvalidDeclaration,
                    index);
            }
            if (!declarations.emplace(
                                 declaration.canonical_identity, &declaration)
                    .second) {
                return fail(
                    SystemVerilogCoverageDatabaseError::DuplicateDeclaration,
                    index);
            }
        }

        std::set<std::string, std::less<>> instances;
        std::size_t bin_count { };
        for (std::size_t instance_index = 0;
            instance_index < state.instances.size(); ++instance_index) {
            const auto& instance = state.instances[instance_index];
            if (!valid_text(
                    instance.runtime_identity, limits.maximum_identity_bytes)
                || !valid_text(instance.declaration_identity,
                    limits.maximum_identity_bytes)) {
                return fail(
                    SystemVerilogCoverageDatabaseError::InvalidInstance,
                    instance_index);
            }
            if (!instances.insert(instance.runtime_identity).second) {
                return fail(
                    SystemVerilogCoverageDatabaseError::DuplicateInstance,
                    instance_index);
            }
            const auto declaration_found
                = declarations.find(instance.declaration_identity);
            if (declaration_found == declarations.end()) {
                return fail(
                    SystemVerilogCoverageDatabaseError::InvalidInstance,
                    instance_index);
            }
            const auto& declaration = *declaration_found->second;
            if (bin_count > limits.maximum_bins
                || instance.bin_hits.size() > limits.maximum_bins - bin_count) {
                return fail(
                    SystemVerilogCoverageDatabaseError::ResourceLimit,
                    instance_index);
            }
            bin_count += instance.bin_hits.size();
            if (instance.cross_bin_state.size()
                > limits.maximum_bins - bin_count) {
                return fail(
                    SystemVerilogCoverageDatabaseError::ResourceLimit,
                    instance_index);
            }
            bin_count += instance.cross_bin_state.size();
            const auto instance_identity = stable_identity(
                "fsim-systemverilog-coverage-instance-v3",
                instance.runtime_identity);

            const auto append_metric = [&](const std::string_view identity,
                                           const std::size_t declaration_index,
                                           const CoverageDatabaseMetricFamily family,
                                           const std::uint64_t hits,
                                           const std::uint64_t excluded_hits,
                                           const bool excluded,
                                           const std::size_t item_index)
                -> std::optional<SystemVerilogCoverageDatabaseResult> {
                if (!valid_text(identity, limits.maximum_identity_bytes)) {
                    return fail(SystemVerilogCoverageDatabaseError::InvalidBin,
                        item_index);
                }
                const auto* item
                    = coverage_declaration(declaration, declaration_index);
                const auto expected_kind
                    = family == CoverageDatabaseMetricFamily::SystemVerilogCross
                    ? SystemVerilogCoverageDeclarationKind::Cross
                    : SystemVerilogCoverageDeclarationKind::Coverpoint;
                if (item == nullptr || item->kind != expected_kind) {
                    return fail(SystemVerilogCoverageDatabaseError::InvalidBin,
                        item_index);
                }
                auto item_source = source_name(item->span);
                const auto* source_span = &item->span;
                if (item_source.empty()) {
                    item_source = source_name(declaration.span);
                    source_span = &declaration.span;
                }
                const auto source_found = source_map.find(item_source);
                if (source_found == source_map.end()) {
                    return fail(SystemVerilogCoverageDatabaseError::UnknownSource,
                        item_index);
                }
                const auto bin_identity = stable_identity(
                    family == CoverageDatabaseMetricFamily::SystemVerilogCross
                        ? "fsim-systemverilog-cross-bin-v3"
                        : "fsim-systemverilog-coverpoint-bin-v3",
                    identity);
                contents.metrics.push_back(CoverageDatabaseMetricRecord {
                    CoverageDatabaseNamespace::SystemVerilogFunctional,
                    family, CoverageDatabaseMetricScope::Instance,
                    bin_identity, source_found->second, instance_identity,
                    run_identity, hits, excluded_hits,
                    hits == std::numeric_limits<std::uint64_t>::max(),
                    excluded_hits == std::numeric_limits<std::uint64_t>::max(),
                    static_cast<std::uint64_t>(source_span->begin.line) });
                if (excluded) {
                    contents.exclusions.push_back({ CoverageDatabaseNamespace::SystemVerilogFunctional,
                        family, CoverageDatabaseMetricScope::Instance,
                        bin_identity, source_found->second, instance_identity,
                        "SystemVerilog functional coverage bin excluded",
                        static_cast<std::uint64_t>(source_span->begin.line) });
                }
                return std::nullopt;
            };

            for (std::size_t index = 0; index < instance.bin_hits.size();
                ++index) {
                const auto& hit = instance.bin_hits[index];
                const auto* item = coverage_declaration(
                    declaration, hit.coverage_declaration_index);
                const auto* bin = item == nullptr
                    ? nullptr
                    : coverage_bin(*item, hit.bin_declaration_index);
                if (item == nullptr || bin == nullptr
                    || bin->kind != SystemVerilogCoverageBinKind::Regular
                    || hit.at_least == 0U
                    || hit.covered != (hit.hit_count >= hit.at_least)) {
                    return fail(SystemVerilogCoverageDatabaseError::InvalidBin,
                        index);
                }
                if (auto error = append_metric(hit.identity,
                        hit.coverage_declaration_index,
                        CoverageDatabaseMetricFamily::SystemVerilogCoverpoint,
                        hit.hit_count, 0U, false, index)) {
                    return std::move(*error);
                }
            }
            for (const auto& item : declaration.coverage_declarations) {
                if (item.kind
                    != SystemVerilogCoverageDeclarationKind::Coverpoint) {
                    continue;
                }
                for (const auto& bin : item.bins) {
                    if (bin.selection
                        == SystemVerilogCoverageBinSelection::Automatic) {
                        continue;
                    }
                    const auto identity = coverpoint_bin_identity(
                        declaration, item, bin);
                    if (std::ranges::any_of(
                            instance.bin_hits,
                            [&](const SystemVerilogCoverageBinHit& hit) {
                                return hit.identity == identity;
                            })) {
                        continue;
                    }
                    if (bin_count >= limits.maximum_bins) {
                        return fail(
                            SystemVerilogCoverageDatabaseError::ResourceLimit,
                            bin.declaration_index);
                    }
                    ++bin_count;
                    const auto excluded
                        = bin.kind != SystemVerilogCoverageBinKind::Regular
                        || bin.weight == 0U;
                    if (auto error = append_metric(identity,
                            item.declaration_index,
                            CoverageDatabaseMetricFamily::SystemVerilogCoverpoint,
                            0U, 0U, excluded, bin.declaration_index)) {
                        return std::move(*error);
                    }
                }
            }
            for (std::size_t index = 0;
                index < instance.cross_bin_state.size(); ++index) {
                const auto& cross = instance.cross_bin_state[index];
                const auto* item = coverage_declaration(
                    declaration, cross.coverage_declaration_index);
                const auto* bin = item == nullptr
                        || !cross.bin_declaration_index
                    ? nullptr
                    : coverage_bin(*item, *cross.bin_declaration_index);
                if (item == nullptr
                    || (cross.bin_declaration_index
                        && (bin == nullptr
                            || (bin->kind
                                    != SystemVerilogCoverageBinKind::Regular
                                && !cross.excluded)))
                    || cross.at_least == 0U
                    || cross.covered
                        != (!cross.excluded
                            && cross.hit_count >= cross.at_least)) {
                    return fail(SystemVerilogCoverageDatabaseError::InvalidBin,
                        index);
                }
                if (auto error = append_metric(cross.identity,
                        cross.coverage_declaration_index,
                        CoverageDatabaseMetricFamily::SystemVerilogCross,
                        cross.hit_count, cross.exclusion_count, cross.excluded,
                        index)) {
                    return std::move(*error);
                }
            }
        }
        auto made = artifact::make_coverage_database_contents(
            std::move(contents));
        if (!made.ok()) {
            return { { },
                SystemVerilogCoverageDatabaseError::InvalidDatabaseModel,
                made.error, made.index };
        }
        return { std::move(made.contents),
            SystemVerilogCoverageDatabaseError::None,
            artifact::CoverageDatabaseModelError::None, 0U };
    } catch (const std::bad_alloc&) {
        return fail(SystemVerilogCoverageDatabaseError::AllocationFailure);
    }
}

} // namespace fsim::frontend
