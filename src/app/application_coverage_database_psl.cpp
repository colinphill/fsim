// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/coverage_database_psl.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <string_view>
#include <tuple>

namespace fsim::app {
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

    bool valid_kind(const ConcurrentAssertionCoverageKind kind) noexcept
    {
        switch (kind) {
        case ConcurrentAssertionCoverageKind::assertion:
        case ConcurrentAssertionCoverageKind::assumption:
        case ConcurrentAssertionCoverageKind::cover:
        case ConcurrentAssertionCoverageKind::restriction:
            return true;
        }
        return false;
    }

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8> bytes;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(
                value >> ((bytes.size() - 1U - index) * 8U));
        }
        hash.update(bytes);
    }

    void update_text(
        support::Sha256& hash, const std::string_view value) noexcept
    {
        update_u64(hash, value.size());
        hash.update(value);
    }

    CoverageDatabaseIdentity identity_from_digest(
        const support::Sha256::Digest& digest) noexcept
    {
        CoverageDatabaseIdentity result;
        for (std::size_t index = 0; index < 8U; ++index) {
            result.high = (result.high << 8U) | digest[index];
            result.low = (result.low << 8U) | digest[index + 8U];
        }
        return result;
    }

    CoverageDatabaseIdentity instance_identity(
        const std::string_view value) noexcept
    {
        support::Sha256 hash;
        update_text(hash, "fsim-psl-coverage-instance-v3");
        update_text(hash, value);
        return identity_from_digest(hash.finish());
    }

    CoverageDatabaseIdentity bin_identity(
        const ConcurrentAssertionCoverage& item,
        const std::string_view role) noexcept
    {
        support::Sha256 hash;
        update_text(hash, "fsim-psl-coverage-bin-v3");
        update_text(hash, item.name);
        update_text(hash, item.process);
        update_u64(hash, static_cast<std::uint8_t>(item.kind));
        update_u64(hash, item.slot);
        update_u64(hash, item.source_span);
        update_text(hash, role);
        return identity_from_digest(hash.finish());
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

    bool checked_add(
        std::uint64_t& total, const std::uint64_t amount) noexcept
    {
        if (amount > std::numeric_limits<std::uint64_t>::max() - total) {
            return false;
        }
        total += amount;
        return true;
    }

    PslCoverageDatabaseResult fail(const PslCoverageDatabaseError error,
        const std::size_t index = 0U) noexcept
    {
        return { { }, error, artifact::CoverageDatabaseModelError::None, index };
    }

} // namespace

PslCoverageDatabaseResult project_psl_coverage_namespace(
    CoverageDatabaseContents contents,
    const std::span<const ConcurrentAssertionCoverage> coverage,
    const std::span<const PslCoverageDatabaseSource> sources,
    const CoverageDatabaseIdentity run_identity,
    const PslCoverageDatabaseLimits& limits) noexcept
{
    try {
        const auto model_validation
            = artifact::validate_coverage_database_contents(contents);
        if (!model_validation.ok()) {
            return { { }, PslCoverageDatabaseError::InvalidDatabaseModel,
                model_validation.error, model_validation.index };
        }
        if (!valid_identity(run_identity) || !owns_run(contents, run_identity)) {
            return fail(PslCoverageDatabaseError::InvalidRun);
        }
        if (std::ranges::any_of(contents.metrics, [](const auto& metric) {
                return metric.name_space == CoverageDatabaseNamespace::Psl;
            })
            || std::ranges::any_of(contents.exclusions, [](const auto& exclusion) {
                   return exclusion.name_space == CoverageDatabaseNamespace::Psl;
               })) {
            return fail(PslCoverageDatabaseError::NamespaceNotEmpty);
        }
        if (sources.size() > limits.maximum_source_bindings
            || coverage.size() > limits.maximum_directives) {
            return fail(PslCoverageDatabaseError::ResourceLimit);
        }

        std::map<std::uint32_t, CoverageDatabaseIdentity> source_map;
        for (std::size_t index = 0; index < sources.size(); ++index) {
            const auto& source = sources[index];
            if (!valid_identity(source.source_identity)
                || !owns_source(contents, source.source_identity)) {
                return fail(
                    PslCoverageDatabaseError::InvalidSourceBinding, index);
            }
            if (!source_map.emplace(source.source, source.source_identity)
                    .second) {
                return fail(
                    PslCoverageDatabaseError::DuplicateSourceBinding, index);
            }
        }

        std::set<std::tuple<std::string, std::string,
            ConcurrentAssertionCoverageKind, std::uint32_t, std::uint32_t>>
            identities;
        static constexpr std::array property_roles {
            std::string_view { "pass" }, std::string_view { "failure" },
            std::string_view { "vacuous" }, std::string_view { "aborted" }
        };
        for (std::size_t index = 0; index < coverage.size(); ++index) {
            const auto& item = coverage[index];
            if (!valid_text(item.name, limits.maximum_identity_bytes)
                || !valid_text(item.process, limits.maximum_identity_bytes)
                || !valid_text(
                    item.instance_identity, limits.maximum_identity_bytes)
                || !valid_kind(item.kind)) {
                return fail(PslCoverageDatabaseError::InvalidDirective, index);
            }
            const auto key = std::tie(item.name, item.process, item.kind,
                item.slot, item.source_span);
            if (!identities.emplace(key).second) {
                return fail(PslCoverageDatabaseError::DuplicateDirective, index);
            }
            const auto source = source_map.find(item.source_span);
            if (source == source_map.end()) {
                return fail(PslCoverageDatabaseError::UnknownSource, index);
            }
            std::uint64_t completed { };
            if (!checked_add(completed, item.passes)
                || !checked_add(completed, item.failures)
                || !checked_add(completed, item.vacuous)
                || !checked_add(completed, item.aborted)
                || completed != item.attempts) {
                return fail(PslCoverageDatabaseError::InvalidDirective, index);
            }
            const auto owner = instance_identity(item.instance_identity);
            const auto append = [&](const CoverageDatabaseMetricFamily family,
                                    const std::string_view role,
                                    const std::uint64_t hits) {
                contents.metrics.push_back(CoverageDatabaseMetricRecord {
                    CoverageDatabaseNamespace::Psl, family,
                    CoverageDatabaseMetricScope::Instance,
                    bin_identity(item, role), source->second, owner, run_identity,
                    hits, 0U,
                    hits == std::numeric_limits<std::uint64_t>::max(), false });
            };
            append(CoverageDatabaseMetricFamily::PslDirective,
                "attempt", item.attempts);
            const std::array property_counts {
                item.passes, item.failures, item.vacuous, item.aborted
            };
            for (std::size_t role = 0; role < property_roles.size(); ++role) {
                append(CoverageDatabaseMetricFamily::PslProperty,
                    property_roles[role], property_counts[role]);
            }
        }

        auto made = artifact::make_coverage_database_contents(
            std::move(contents));
        if (!made.ok()) {
            return { { }, PslCoverageDatabaseError::InvalidDatabaseModel,
                made.error, made.index };
        }
        return { std::move(made.contents), PslCoverageDatabaseError::None,
            artifact::CoverageDatabaseModelError::None, 0U };
    } catch (const std::bad_alloc&) {
        return fail(PslCoverageDatabaseError::AllocationFailure);
    }
}

} // namespace fsim::app
