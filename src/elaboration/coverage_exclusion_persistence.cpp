// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_exclusion_persistence.hpp"

#include <algorithm>
#include <new>
#include <set>
#include <tuple>

namespace fsim::elaboration {
namespace {

    using artifact::CoverageDatabaseIdentity;
    using artifact::CoverageDatabaseMetricFamily;
    using artifact::CoverageDatabaseMetricScope;
    using artifact::CoverageDatabaseNamespace;

    bool valid_identity(const CoverageDatabaseIdentity identity) noexcept
    {
        return identity.high != 0U || identity.low != 0U;
    }

    std::optional<frontend::CoverageSourceMetric> source_metric(
        const CoverageDatabaseNamespace name_space,
        const CoverageDatabaseMetricFamily family) noexcept
    {
        switch (family) {
        case CoverageDatabaseMetricFamily::Statement:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::Statement }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::Branch:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::Branch }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::Line:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::Line }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::Condition:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::Condition }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::Expression:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::Expression }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::Toggle:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::Toggle }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::FsmState:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::FsmState }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::FsmTransition:
            return name_space == CoverageDatabaseNamespace::Code
                ? std::optional { frontend::CoverageSourceMetric::FsmTransition }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::SystemVerilogCoverpoint:
            return name_space == CoverageDatabaseNamespace::SystemVerilogFunctional
                ? std::optional {
                      frontend::CoverageSourceMetric::SystemVerilogCoverpoint
                  }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::SystemVerilogCross:
            return name_space == CoverageDatabaseNamespace::SystemVerilogFunctional
                ? std::optional { frontend::CoverageSourceMetric::SystemVerilogCross }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::PslDirective:
            return name_space == CoverageDatabaseNamespace::Psl
                ? std::optional { frontend::CoverageSourceMetric::PslDirective }
                : std::nullopt;
        case CoverageDatabaseMetricFamily::PslProperty:
            return name_space == CoverageDatabaseNamespace::Psl
                ? std::optional { frontend::CoverageSourceMetric::PslProperty }
                : std::nullopt;
        }
        return std::nullopt;
    }

    auto candidate_key(const CoverageExclusionCandidate& candidate) noexcept
    {
        return std::tuple { candidate.name_space, candidate.family,
            candidate.source_identity, candidate.instance_identity,
            candidate.point_identity };
    }

    auto record_key(
        const artifact::CoverageDatabaseExclusionRecord& record) noexcept
    {
        return std::tie(record.name_space, record.family, record.scope,
            record.source_identity, record.instance_identity,
            record.point_identity, record.reason);
    }

    bool valid_reason(const std::string_view reason) noexcept
    {
        return !reason.empty() && reason.find('\0') == std::string_view::npos;
    }

} // namespace

CoverageExclusionPersistenceResult make_coverage_exclusion_records(
    const CoverageExternalExclusionPlan& external_plan,
    const std::span<const CoverageExclusionCandidate> candidates,
    const CoverageExclusionPersistenceLimits limits) noexcept
{
    CoverageExclusionPersistenceResult result;
    const auto reject = [&](const CoverageExclusionPersistenceError error,
                            const std::size_t index = 0U,
                            const CoverageExternalExclusionError external
                            = CoverageExternalExclusionError::None) {
        result.records.clear();
        result.error = error;
        result.external_error = external;
        result.index = index;
        return result;
    };
    if (candidates.size() > limits.maximum_candidates) {
        return reject(CoverageExclusionPersistenceError::ResourceLimit);
    }

    try {
        std::vector<CoverageExternalExclusionTarget> targets;
        targets.reserve(candidates.size());
        std::set<decltype(candidate_key(candidates.front()))> candidate_keys;
        std::size_t source_reason_count { };
        std::size_t total_reason_bytes { };
        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            const auto metric = source_metric(
                candidate.name_space, candidate.family);
            if (!metric || !valid_identity(candidate.point_identity)
                || !valid_identity(candidate.source_identity)
                || !valid_identity(candidate.instance_identity)
                || candidate.source_line == 0U
                || candidate.source_line > limits.maximum_source_line
                || candidate.source_path.empty()
                || candidate.hierarchy_path.empty()
                || candidate.source_path.find('\0') != std::string::npos
                || candidate.hierarchy_path.find('\0') != std::string::npos
                || candidate.object_path.find('\0') != std::string::npos) {
                return reject(
                    CoverageExclusionPersistenceError::InvalidCandidate,
                    index);
            }
            if (!candidate_keys.insert(candidate_key(candidate)).second) {
                return reject(
                    CoverageExclusionPersistenceError::DuplicateCandidate,
                    index);
            }
            if (candidate.source_reasons.size()
                > limits.maximum_source_reasons - source_reason_count) {
                return reject(
                    CoverageExclusionPersistenceError::ResourceLimit, index);
            }
            source_reason_count += candidate.source_reasons.size();
            for (const auto& reason : candidate.source_reasons) {
                if (!valid_reason(reason)) {
                    return reject(
                        CoverageExclusionPersistenceError::InvalidReason,
                        index);
                }
                if (reason.size() > limits.maximum_reason_bytes
                    || total_reason_bytes
                        > limits.maximum_total_reason_bytes
                    || reason.size()
                        > limits.maximum_total_reason_bytes
                            - total_reason_bytes) {
                    return reject(
                        CoverageExclusionPersistenceError::ResourceLimit,
                        index);
                }
                total_reason_bytes += reason.size();
            }
            targets.push_back({ candidate.source_path,
                candidate.hierarchy_path, candidate.object_path, *metric });
        }

        const auto external = match_coverage_external_exclusions(
            external_plan, targets, limits.external);
        if (!external.ok()) {
            return reject(CoverageExclusionPersistenceError::ExternalExclusion,
                external.index, external.error);
        }

        for (std::size_t index = 0U; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            const auto append = [&](const CoverageDatabaseMetricScope scope,
                                    const CoverageDatabaseIdentity instance,
                                    const std::string& reason) {
                if (result.records.size() >= limits.maximum_records)
                    return false;
                result.records.push_back({ candidate.name_space,
                    candidate.family, scope, candidate.point_identity,
                    candidate.source_identity, instance, reason,
                    candidate.source_line });
                return true;
            };
            for (const auto& reason : candidate.source_reasons) {
                if (!append(CoverageDatabaseMetricScope::Source, { }, reason)) {
                    return reject(
                        CoverageExclusionPersistenceError::ResourceLimit,
                        index);
                }
            }
            for (const auto rule_index : external.targets[index].rule_indices) {
                const auto& reason = external_plan.rules[rule_index].reason;
                if (reason.size() > limits.maximum_reason_bytes
                    || total_reason_bytes
                        > limits.maximum_total_reason_bytes
                    || reason.size()
                        > limits.maximum_total_reason_bytes
                            - total_reason_bytes) {
                    return reject(
                        CoverageExclusionPersistenceError::ResourceLimit,
                        index);
                }
                total_reason_bytes += reason.size();
                if (!append(CoverageDatabaseMetricScope::Instance,
                        candidate.instance_identity, reason)) {
                    return reject(
                        CoverageExclusionPersistenceError::ResourceLimit,
                        index);
                }
            }
        }
        std::ranges::sort(result.records,
            [](const auto& left, const auto& right) {
                return record_key(left) < record_key(right);
            });
        result.records.erase(
            std::unique(result.records.begin(), result.records.end()),
            result.records.end());
        return result;
    } catch (const std::bad_alloc&) {
        return reject(CoverageExclusionPersistenceError::AllocationFailure);
    } catch (...) {
        return reject(CoverageExclusionPersistenceError::ResourceLimit);
    }
}

std::string_view coverage_exclusion_persistence_error_name(
    const CoverageExclusionPersistenceError error) noexcept
{
    switch (error) {
    case CoverageExclusionPersistenceError::None:
        return "none";
    case CoverageExclusionPersistenceError::ResourceLimit:
        return "resource-limit";
    case CoverageExclusionPersistenceError::InvalidCandidate:
        return "invalid-candidate";
    case CoverageExclusionPersistenceError::DuplicateCandidate:
        return "duplicate-candidate";
    case CoverageExclusionPersistenceError::InvalidReason:
        return "invalid-reason";
    case CoverageExclusionPersistenceError::ExternalExclusion:
        return "external-exclusion";
    case CoverageExclusionPersistenceError::AllocationFailure:
        return "allocation-failure";
    }
    return "invalid";
}

} // namespace fsim::elaboration
