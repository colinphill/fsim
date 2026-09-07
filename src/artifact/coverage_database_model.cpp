// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_model.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <ranges>
#include <tuple>
#include <utility>

namespace fsim::artifact {
namespace {

    bool valid_identity(const CoverageDatabaseIdentity identity) noexcept
    {
        return identity.high != 0U || identity.low != 0U;
    }

    bool empty_identity(const CoverageDatabaseIdentity identity) noexcept
    {
        return identity.high == 0U && identity.low == 0U;
    }

    bool digest_present(const CoverageDatabaseDigest& digest) noexcept
    {
        return std::ranges::any_of(
            digest, [](const std::byte value) { return value != std::byte { }; });
    }

    bool valid_run_status(const CoverageDatabaseRunStatus status) noexcept
    {
        switch (status) {
        case CoverageDatabaseRunStatus::Complete:
        case CoverageDatabaseRunStatus::Stopped:
        case CoverageDatabaseRunStatus::Failed:
            return true;
        }
        return false;
    }

    bool valid_scope(const CoverageDatabaseMetricScope scope) noexcept
    {
        return scope == CoverageDatabaseMetricScope::Source
            || scope == CoverageDatabaseMetricScope::Instance;
    }

    bool valid_namespace(const CoverageDatabaseNamespace name_space) noexcept
    {
        return !coverage_database_namespace_name(name_space).empty();
    }

    bool valid_family(const CoverageDatabaseMetricFamily family) noexcept
    {
        return !coverage_database_metric_family_name(family).empty();
    }

    bool valid_pairing(const CoverageDatabaseNamespace name_space,
        const CoverageDatabaseMetricFamily family) noexcept
    {
        switch (name_space) {
        case CoverageDatabaseNamespace::Code:
            return family >= CoverageDatabaseMetricFamily::Statement
                && family <= CoverageDatabaseMetricFamily::FsmTransition;
        case CoverageDatabaseNamespace::SystemVerilogFunctional:
            return family == CoverageDatabaseMetricFamily::SystemVerilogCoverpoint
                || family == CoverageDatabaseMetricFamily::SystemVerilogCross;
        case CoverageDatabaseNamespace::Psl:
            return family == CoverageDatabaseMetricFamily::PslDirective
                || family == CoverageDatabaseMetricFamily::PslProperty;
        }
        return false;
    }

    bool valid_text(const std::string_view text) noexcept
    {
        return text.find('\0') == std::string_view::npos;
    }

    bool valid_logical_path(const std::string_view path) noexcept
    {
        const auto has_drive_prefix = path.size() >= 2U
            && ((path.front() >= 'A' && path.front() <= 'Z')
                || (path.front() >= 'a' && path.front() <= 'z'))
            && path[1U] == ':';
        if (path.empty() || path.front() == '/' || path.back() == '/'
            || has_drive_prefix || path.find('\\') != std::string_view::npos
            || !valid_text(path)) {
            return false;
        }
        std::size_t begin { };
        while (begin < path.size()) {
            const auto end = path.find('/', begin);
            const auto component = path.substr(
                begin, end == std::string_view::npos ? path.size() - begin : end - begin);
            if (component.empty() || component == "." || component == "..") {
                return false;
            }
            if (end == std::string_view::npos) {
                break;
            }
            begin = end + 1U;
        }
        return true;
    }

    bool checked_text_add(
        std::size_t& total, const std::size_t amount) noexcept
    {
        if (amount > std::numeric_limits<std::size_t>::max() - total) {
            return false;
        }
        total += amount;
        return true;
    }

    auto metric_key(const CoverageDatabaseMetricRecord& record) noexcept
    {
        return std::tie(record.name_space, record.family, record.scope,
            record.source_identity, record.instance_identity,
            record.bin_identity, record.run_identity);
    }

    auto metric_point_key(const CoverageDatabaseMetricRecord& record) noexcept
    {
        return std::tie(record.name_space, record.family, record.scope,
            record.source_identity, record.instance_identity,
            record.bin_identity);
    }

    auto exclusion_key(const CoverageDatabaseExclusionRecord& record) noexcept
    {
        return std::tie(record.name_space, record.family, record.scope,
            record.source_identity, record.instance_identity,
            record.point_identity, record.reason);
    }

    auto exclusion_point_key(
        const CoverageDatabaseExclusionRecord& record) noexcept
    {
        return std::tie(record.name_space, record.family, record.scope,
            record.source_identity, record.instance_identity,
            record.point_identity);
    }

    bool has_source(const CoverageDatabaseContents& contents,
        const CoverageDatabaseIdentity identity) noexcept
    {
        return std::ranges::binary_search(contents.sources, identity,
            std::ranges::less { }, &CoverageDatabaseSourceRecord::identity);
    }

    bool has_run(const CoverageDatabaseContents& contents,
        const CoverageDatabaseIdentity identity) noexcept
    {
        return std::ranges::binary_search(
            contents.runs, identity, std::ranges::less { },
            &CoverageDatabaseRunRecord::identity);
    }

    CoverageDatabaseModelValidationResult validate_impl(
        const CoverageDatabaseContents& contents,
        const CoverageDatabaseModelLimits& limits) noexcept
    {
        if (contents.sources.size() > limits.maximum_sources
            || contents.runs.size() > limits.maximum_runs
            || contents.metrics.size() > limits.maximum_metrics
            || contents.exclusions.size() > limits.maximum_exclusions) {
            return { CoverageDatabaseModelError::ResourceLimit, 0U };
        }
        if (contents.fingerprint.schema != kCoverageDatabaseSchema
            || contents.fingerprint.model != kCoverageDatabaseModel
            || !digest_present(contents.fingerprint.digest)) {
            return { CoverageDatabaseModelError::InvalidFingerprint, 0U };
        }

        std::size_t text_bytes { };
        for (std::size_t index = 0; index < contents.sources.size(); ++index) {
            const auto& source = contents.sources[index];
            if (!valid_identity(source.identity)
                || !valid_logical_path(source.logical_path)
                || source.logical_path.size() > limits.maximum_logical_path_bytes
                || !digest_present(source.content_digest)) {
                return { CoverageDatabaseModelError::InvalidSource, index };
            }
            if (!checked_text_add(text_bytes, source.logical_path.size())) {
                return { CoverageDatabaseModelError::ArithmeticOverflow, index };
            }
            if (index != 0U
                && !(contents.sources[index - 1U].identity < source.identity)) {
                return { contents.sources[index - 1U].identity == source.identity
                        ? CoverageDatabaseModelError::DuplicateSource
                        : CoverageDatabaseModelError::NonCanonicalOrder,
                    index };
            }
        }
        for (std::size_t index = 0; index < contents.runs.size(); ++index) {
            const auto& run = contents.runs[index];
            if (!valid_identity(run.identity) || run.label.empty()
                || run.producer.empty() || !valid_text(run.label)
                || !valid_text(run.producer)
                || run.label.size() > limits.maximum_label_bytes
                || run.producer.size() > limits.maximum_producer_bytes
                || !valid_run_status(run.status)) {
                return { CoverageDatabaseModelError::InvalidRun, index };
            }
            if (!checked_text_add(text_bytes, run.label.size())
                || !checked_text_add(text_bytes, run.producer.size())) {
                return { CoverageDatabaseModelError::ArithmeticOverflow, index };
            }
            if (index != 0U
                && !(contents.runs[index - 1U].identity < run.identity)) {
                return { contents.runs[index - 1U].identity == run.identity
                        ? CoverageDatabaseModelError::DuplicateRun
                        : CoverageDatabaseModelError::NonCanonicalOrder,
                    index };
            }
        }
        if (text_bytes > limits.maximum_text_bytes) {
            return { CoverageDatabaseModelError::ResourceLimit, 0U };
        }

        for (std::size_t index = 0; index < contents.metrics.size(); ++index) {
            const auto& metric = contents.metrics[index];
            if (!valid_namespace(metric.name_space)) {
                return { CoverageDatabaseModelError::InvalidNamespace, index };
            }
            if (!valid_family(metric.family)
                || !valid_pairing(metric.name_space, metric.family)) {
                return { CoverageDatabaseModelError::InvalidMetricFamily, index };
            }
            if (!valid_scope(metric.scope)
                || (metric.scope == CoverageDatabaseMetricScope::Source
                    && !empty_identity(metric.instance_identity))
                || (metric.scope == CoverageDatabaseMetricScope::Instance
                    && !valid_identity(metric.instance_identity))) {
                return { CoverageDatabaseModelError::InvalidMetricScope, index };
            }
            if (!valid_identity(metric.bin_identity)
                || !valid_identity(metric.source_identity)
                || !valid_identity(metric.run_identity)
                || !has_source(contents, metric.source_identity)
                || !has_run(contents, metric.run_identity)
                || metric.source_line > limits.maximum_source_line
                || (metric.overflow
                    && metric.hits != std::numeric_limits<std::uint64_t>::max())
                || (metric.excluded_overflow
                    && metric.excluded_hits
                        != std::numeric_limits<std::uint64_t>::max())) {
                return { CoverageDatabaseModelError::InvalidMetric, index };
            }
            if (index != 0U) {
                if (metric_point_key(contents.metrics[index - 1U])
                        == metric_point_key(metric)
                    && contents.metrics[index - 1U].source_line
                        != metric.source_line) {
                    return { CoverageDatabaseModelError::InvalidMetric,
                        index };
                }
                const auto previous = metric_key(contents.metrics[index - 1U]);
                const auto current = metric_key(metric);
                if (!(previous < current)) {
                    return { previous == current
                            ? CoverageDatabaseModelError::DuplicateMetric
                            : CoverageDatabaseModelError::NonCanonicalOrder,
                        index };
                }
            }
        }

        for (std::size_t index = 0; index < contents.exclusions.size(); ++index) {
            const auto& exclusion = contents.exclusions[index];
            if (!valid_namespace(exclusion.name_space)) {
                return { CoverageDatabaseModelError::InvalidNamespace, index };
            }
            if (!valid_family(exclusion.family)
                || !valid_pairing(exclusion.name_space, exclusion.family)) {
                return { CoverageDatabaseModelError::InvalidMetricFamily, index };
            }
            if (!valid_scope(exclusion.scope)
                || (exclusion.scope == CoverageDatabaseMetricScope::Source
                    && !empty_identity(exclusion.instance_identity))
                || (exclusion.scope == CoverageDatabaseMetricScope::Instance
                    && !valid_identity(exclusion.instance_identity))) {
                return { CoverageDatabaseModelError::InvalidMetricScope, index };
            }
            if (!valid_identity(exclusion.point_identity)
                || !valid_identity(exclusion.source_identity)
                || !has_source(contents, exclusion.source_identity)
                || exclusion.reason.empty() || !valid_text(exclusion.reason)
                || exclusion.source_line > limits.maximum_source_line
                || exclusion.reason.size() > limits.maximum_reason_bytes) {
                return { CoverageDatabaseModelError::InvalidExclusion, index };
            }
            if (!checked_text_add(text_bytes, exclusion.reason.size())) {
                return { CoverageDatabaseModelError::ArithmeticOverflow, index };
            }
            if (text_bytes > limits.maximum_text_bytes) {
                return { CoverageDatabaseModelError::ResourceLimit, index };
            }
            if (index != 0U) {
                if (exclusion_point_key(contents.exclusions[index - 1U])
                        == exclusion_point_key(exclusion)
                    && contents.exclusions[index - 1U].source_line
                        != exclusion.source_line) {
                    return { CoverageDatabaseModelError::InvalidExclusion,
                        index };
                }
                const auto previous
                    = exclusion_key(contents.exclusions[index - 1U]);
                const auto current = exclusion_key(exclusion);
                if (!(previous < current)) {
                    return { previous == current
                            ? CoverageDatabaseModelError::DuplicateExclusion
                            : CoverageDatabaseModelError::NonCanonicalOrder,
                        index };
                }
            }
        }
        return { CoverageDatabaseModelError::None, 0U };
    }

} // namespace

std::string_view coverage_database_metric_family_name(
    const CoverageDatabaseMetricFamily value) noexcept
{
    switch (value) {
    case CoverageDatabaseMetricFamily::Statement:
        return "statement";
    case CoverageDatabaseMetricFamily::Branch:
        return "branch";
    case CoverageDatabaseMetricFamily::Line:
        return "line";
    case CoverageDatabaseMetricFamily::Condition:
        return "condition";
    case CoverageDatabaseMetricFamily::Expression:
        return "expression";
    case CoverageDatabaseMetricFamily::Toggle:
        return "toggle";
    case CoverageDatabaseMetricFamily::FsmState:
        return "fsm-state";
    case CoverageDatabaseMetricFamily::FsmTransition:
        return "fsm-transition";
    case CoverageDatabaseMetricFamily::SystemVerilogCoverpoint:
        return "systemverilog-coverpoint";
    case CoverageDatabaseMetricFamily::SystemVerilogCross:
        return "systemverilog-cross";
    case CoverageDatabaseMetricFamily::PslDirective:
        return "psl-directive";
    case CoverageDatabaseMetricFamily::PslProperty:
        return "psl-property";
    }
    return { };
}

CoverageDatabaseModelResult make_coverage_database_contents(
    CoverageDatabaseContents contents,
    const CoverageDatabaseModelLimits& limits) noexcept
{
    try {
        std::ranges::sort(contents.sources, std::ranges::less { },
            &CoverageDatabaseSourceRecord::identity);
        std::ranges::sort(
            contents.runs, std::ranges::less { },
            &CoverageDatabaseRunRecord::identity);
        std::ranges::sort(contents.metrics,
            [](const auto& left, const auto& right) {
                return metric_key(left) < metric_key(right);
            });
        std::ranges::sort(contents.exclusions,
            [](const auto& left, const auto& right) {
                return exclusion_key(left) < exclusion_key(right);
            });
        const auto validation = validate_impl(contents, limits);
        if (!validation.ok()) {
            return { { }, validation.error, validation.index };
        }
        return { std::move(contents), CoverageDatabaseModelError::None, 0U };
    } catch (const std::bad_alloc&) {
        return { { }, CoverageDatabaseModelError::AllocationFailure, 0U };
    }
}

CoverageDatabaseModelValidationResult validate_coverage_database_contents(
    const CoverageDatabaseContents& contents,
    const CoverageDatabaseModelLimits& limits) noexcept
{
    return validate_impl(contents, limits);
}

} // namespace fsim::artifact
