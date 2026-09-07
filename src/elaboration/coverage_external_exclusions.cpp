// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_external_exclusions.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace fsim::elaboration {
namespace {

bool contains_parent_component(const std::string_view value) noexcept
{
    std::size_t begin { };
    while (begin <= value.size()) {
        const auto end = value.find('/', begin);
        const auto component = value.substr(begin,
            end == std::string_view::npos ? value.size() - begin
                                          : end - begin);
        if (component.empty() || component == "." || component == "..")
            return true;
        if (end == std::string_view::npos)
            break;
        begin = end + 1U;
    }
    return false;
}

std::optional<std::string> canonical_pattern(
    const std::optional<std::string>& input)
{
    if (!input)
        return std::nullopt;
    if (input->empty() || input->find('\0') != std::string::npos) {
        return std::string { };
    }
    std::string result;
    result.reserve(input->size());
    for (const auto character : *input) {
        if (character != '*' || result.empty() || result.back() != '*')
            result.push_back(character);
    }
    return result;
}

bool valid_source_pattern(const std::string_view pattern) noexcept
{
    return !pattern.empty() && pattern.front() != '/'
        && pattern.find('\\') == std::string_view::npos
        && pattern.find(':') == std::string_view::npos
        && !contains_parent_component(pattern);
}

bool valid_hierarchy_pattern(const std::string_view pattern) noexcept
{
    return !pattern.empty() && pattern.front() != '.' && pattern.back() != '.'
        && pattern.find("..") == std::string_view::npos
        && pattern.find('/') == std::string_view::npos
        && pattern.find('\\') == std::string_view::npos;
}

bool valid_object_pattern(const std::string_view pattern) noexcept
{
    return !pattern.empty() && pattern.find('/') == std::string_view::npos
        && pattern.find('\\') == std::string_view::npos;
}

std::string rule_identity(const CoverageExternalExclusionRule& rule)
{
    support::Sha256 hash;
    const auto add = [&](const std::string_view label,
                         const std::optional<std::string>& value) {
        hash.update(label);
        hash.update(std::string_view { "\0", 1U });
        hash.update(value ? std::string_view { *value }
                          : std::string_view { "<any>" });
        hash.update(std::string_view { "\0", 1U });
    };
    hash.update(kCoverageExternalExclusionSchema);
    hash.update(std::string_view { "\0", 1U });
    add("source", rule.source_pattern);
    add("hierarchy", rule.hierarchy_pattern);
    add("object", rule.object_pattern);
    hash.update(frontend::coverage_source_metric_name(rule.metric));
    hash.update(std::string_view { "\0reason\0", 8U });
    hash.update(rule.reason);
    return support::Sha256::hex(hash.finish());
}

auto selector_key(const CoverageExternalExclusionRule& rule)
{
    return std::tuple { rule.source_pattern, rule.hierarchy_pattern,
        rule.object_pattern, rule.metric };
}

bool glob_match(const std::string_view pattern,
    const std::string_view value,
    std::size_t& operations,
    const std::size_t maximum_operations) noexcept
{
    std::size_t pattern_index { };
    std::size_t value_index { };
    std::size_t star = std::string_view::npos;
    std::size_t retry_value { };
    while (value_index < value.size()) {
        if (++operations > maximum_operations)
            return false;
        if (pattern_index < pattern.size()
            && (pattern[pattern_index] == '?'
                || pattern[pattern_index] == value[value_index])) {
            ++pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size()
            && pattern[pattern_index] == '*') {
            star = pattern_index++;
            retry_value = value_index;
        } else if (star != std::string_view::npos) {
            pattern_index = star + 1U;
            value_index = ++retry_value;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size()
        && pattern[pattern_index] == '*') {
        if (++operations > maximum_operations)
            return false;
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

CoverageExternalExclusionError validate_plan(
    const CoverageExternalExclusionPlan& plan,
    const CoverageExternalExclusionLimits limits)
{
    if (plan.rules.size() > limits.maximum_rules)
        return CoverageExternalExclusionError::ResourceLimit;
    std::size_t total_bytes { };
    support::Sha256 hash;
    hash.update(kCoverageExternalExclusionSchema);
    for (std::size_t index = 0U; index < plan.rules.size(); ++index) {
        const auto& rule = plan.rules[index];
        if ((rule.source_pattern
                && rule.source_pattern->size()
                    > limits.maximum_pattern_bytes)
            || (rule.hierarchy_pattern
                && rule.hierarchy_pattern->size()
                    > limits.maximum_pattern_bytes)
            || (rule.object_pattern
                && rule.object_pattern->size()
                    > limits.maximum_pattern_bytes)
            || rule.reason.size() > limits.maximum_reason_bytes) {
            return CoverageExternalExclusionError::ResourceLimit;
        }
        const auto patterns_valid
            = (!rule.source_pattern
                  || valid_source_pattern(*rule.source_pattern))
            && (!rule.hierarchy_pattern
                || valid_hierarchy_pattern(*rule.hierarchy_pattern))
            && (!rule.object_pattern
                || valid_object_pattern(*rule.object_pattern));
        if (!patterns_valid || rule.reason.empty()
            || frontend::coverage_source_metric_name(rule.metric) == "invalid"
            || rule.identity != rule_identity(rule)) {
            return CoverageExternalExclusionError::InvalidPlan;
        }
        if (index > 0U
            && std::tuple { selector_key(plan.rules[index - 1U]),
                   plan.rules[index - 1U].reason }
                >= std::tuple { selector_key(rule), rule.reason }) {
            return CoverageExternalExclusionError::InvalidPlan;
        }
        const auto bytes = rule.reason.size()
            + (rule.source_pattern ? rule.source_pattern->size() : 0U)
            + (rule.hierarchy_pattern ? rule.hierarchy_pattern->size() : 0U)
            + (rule.object_pattern ? rule.object_pattern->size() : 0U);
        if (total_bytes > limits.maximum_total_bytes
            || bytes > limits.maximum_total_bytes - total_bytes) {
            return CoverageExternalExclusionError::ResourceLimit;
        }
        total_bytes += bytes;
        hash.update(std::string_view { "\0", 1U });
        hash.update(rule.identity);
    }
    return plan.identity == support::Sha256::hex(hash.finish())
        ? CoverageExternalExclusionError::None
        : CoverageExternalExclusionError::InvalidPlan;
}

} // namespace

CoverageExternalExclusionPlanResult make_coverage_external_exclusion_plan(
    const std::span<const project::CoverageExclusionEntry> entries,
    const CoverageExternalExclusionLimits limits) noexcept
{
    const auto reject = [](const CoverageExternalExclusionError error,
                            const std::size_t index = 0U) {
        return CoverageExternalExclusionPlanResult { std::nullopt, error,
            index };
    };
    if (entries.size() > limits.maximum_rules)
        return reject(CoverageExternalExclusionError::ResourceLimit);
    try {
        CoverageExternalExclusionPlan plan;
        plan.rules.reserve(entries.size());
        std::size_t total_bytes { };
        for (std::size_t index = 0U; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            if (entry.metric.empty())
                return reject(CoverageExternalExclusionError::MissingMetric,
                    index);
            const auto metric
                = frontend::coverage_source_metric_from_name(entry.metric);
            if (!metric)
                return reject(CoverageExternalExclusionError::UnknownMetric,
                    index);
            if (entry.reason.empty())
                return reject(CoverageExternalExclusionError::MissingReason,
                    index);
            if (entry.reason.size() > limits.maximum_reason_bytes)
                return reject(CoverageExternalExclusionError::ResourceLimit,
                    index);
            if ((entry.source
                    && entry.source->size() > limits.maximum_pattern_bytes)
                || (entry.hierarchy
                    && entry.hierarchy->size()
                        > limits.maximum_pattern_bytes)
                || (entry.object
                    && entry.object->size()
                        > limits.maximum_pattern_bytes)) {
                return reject(CoverageExternalExclusionError::ResourceLimit,
                    index);
            }

            CoverageExternalExclusionRule rule;
            rule.source_pattern = canonical_pattern(entry.source);
            rule.hierarchy_pattern = canonical_pattern(entry.hierarchy);
            rule.object_pattern = canonical_pattern(entry.object);
            if ((rule.source_pattern && rule.source_pattern->empty())
                || (rule.hierarchy_pattern
                    && rule.hierarchy_pattern->empty())
                || (rule.object_pattern && rule.object_pattern->empty())) {
                return reject(CoverageExternalExclusionError::EmptySelector,
                    index);
            }
            if (rule.source_pattern
                && !valid_source_pattern(*rule.source_pattern)) {
                return reject(
                    CoverageExternalExclusionError::InvalidSourcePattern,
                    index);
            }
            if (rule.hierarchy_pattern
                && !valid_hierarchy_pattern(*rule.hierarchy_pattern)) {
                return reject(
                    CoverageExternalExclusionError::InvalidHierarchyPattern,
                    index);
            }
            if (rule.object_pattern
                && !valid_object_pattern(*rule.object_pattern)) {
                return reject(
                    CoverageExternalExclusionError::InvalidObjectPattern,
                    index);
            }
            const auto bytes = entry.reason.size()
                + (rule.source_pattern ? rule.source_pattern->size() : 0U)
                + (rule.hierarchy_pattern
                        ? rule.hierarchy_pattern->size()
                        : 0U)
                + (rule.object_pattern ? rule.object_pattern->size() : 0U);
            if (total_bytes > limits.maximum_total_bytes
                || bytes > limits.maximum_total_bytes - total_bytes) {
                return reject(CoverageExternalExclusionError::ResourceLimit,
                    index);
            }
            total_bytes += bytes;
            rule.metric = *metric;
            rule.reason = entry.reason;
            rule.manifest_index = index;
            rule.identity = rule_identity(rule);
            plan.rules.push_back(std::move(rule));
        }
        std::ranges::sort(plan.rules, [](const auto& lhs, const auto& rhs) {
            return std::tuple { selector_key(lhs), lhs.reason }
                < std::tuple { selector_key(rhs), rhs.reason };
        });
        for (std::size_t index = 1U; index < plan.rules.size(); ++index) {
            if (selector_key(plan.rules[index - 1U])
                != selector_key(plan.rules[index])) {
                continue;
            }
            return reject(plan.rules[index - 1U].reason
                        == plan.rules[index].reason
                    ? CoverageExternalExclusionError::DuplicateRule
                    : CoverageExternalExclusionError::ConflictingReason,
                plan.rules[index].manifest_index);
        }
        support::Sha256 hash;
        hash.update(kCoverageExternalExclusionSchema);
        for (const auto& rule : plan.rules) {
            hash.update(std::string_view { "\0", 1U });
            hash.update(rule.identity);
        }
        plan.identity = support::Sha256::hex(hash.finish());
        return { std::move(plan), CoverageExternalExclusionError::None, 0U };
    } catch (...) {
        return reject(CoverageExternalExclusionError::ResourceLimit);
    }
}

CoverageExternalExclusionMatchResult match_coverage_external_exclusions(
    const CoverageExternalExclusionPlan& plan,
    const std::span<const CoverageExternalExclusionTarget> targets,
    const CoverageExternalExclusionLimits limits) noexcept
{
    CoverageExternalExclusionMatchResult result;
    const auto reject = [&](const CoverageExternalExclusionError error,
                            const std::size_t index = 0U) {
        result.targets.clear();
        result.error = error;
        result.index = index;
        return result;
    };
    try {
        const auto plan_error = validate_plan(plan, limits);
        if (plan_error != CoverageExternalExclusionError::None)
            return reject(plan_error);
        if (targets.size() > limits.maximum_targets)
            return reject(CoverageExternalExclusionError::ResourceLimit);
        result.targets.resize(targets.size());
        std::size_t operations { };
        for (std::size_t target_index = 0U;
            target_index < targets.size(); ++target_index) {
            const auto& target = targets[target_index];
            if (target.source_path.empty() || target.hierarchy_path.empty()
                || target.source_path.size() > limits.maximum_target_bytes
                || target.hierarchy_path.size()
                    > limits.maximum_target_bytes
                || target.object_path.size() > limits.maximum_target_bytes
                || target.metric == frontend::CoverageSourceMetric::All
                || frontend::coverage_source_metric_name(target.metric)
                    == "invalid"
                || !valid_source_pattern(target.source_path)
                || !valid_hierarchy_pattern(target.hierarchy_path)) {
                return reject(
                    CoverageExternalExclusionError::InvalidTarget,
                    target_index);
            }
            for (std::size_t rule_index = 0U;
                rule_index < plan.rules.size(); ++rule_index) {
                const auto& rule = plan.rules[rule_index];
                if (rule.metric != frontend::CoverageSourceMetric::All
                    && rule.metric != target.metric) {
                    continue;
                }
                const auto matches
                    = [&](const std::optional<std::string>& pattern,
                          const std::string_view value) {
                          return !pattern
                              || glob_match(*pattern, value, operations,
                                  limits.maximum_match_operations);
                      };
                if (matches(rule.source_pattern, target.source_path)
                    && matches(rule.hierarchy_pattern,
                        target.hierarchy_path)
                    && matches(rule.object_pattern, target.object_path)) {
                    result.targets[target_index].rule_indices.push_back(
                        rule_index);
                }
                if (operations > limits.maximum_match_operations) {
                    return reject(
                        CoverageExternalExclusionError::ResourceLimit,
                        target_index);
                }
            }
        }
        return result;
    } catch (...) {
        return reject(CoverageExternalExclusionError::ResourceLimit);
    }
}

std::string_view coverage_external_exclusion_error_name(
    const CoverageExternalExclusionError error) noexcept
{
    switch (error) {
    case CoverageExternalExclusionError::None:
        return "none";
    case CoverageExternalExclusionError::ResourceLimit:
        return "resource-limit";
    case CoverageExternalExclusionError::MissingMetric:
        return "missing-metric";
    case CoverageExternalExclusionError::UnknownMetric:
        return "unknown-metric";
    case CoverageExternalExclusionError::MissingReason:
        return "missing-reason";
    case CoverageExternalExclusionError::EmptySelector:
        return "empty-selector";
    case CoverageExternalExclusionError::InvalidSourcePattern:
        return "invalid-source-pattern";
    case CoverageExternalExclusionError::InvalidHierarchyPattern:
        return "invalid-hierarchy-pattern";
    case CoverageExternalExclusionError::InvalidObjectPattern:
        return "invalid-object-pattern";
    case CoverageExternalExclusionError::DuplicateRule:
        return "duplicate-rule";
    case CoverageExternalExclusionError::ConflictingReason:
        return "conflicting-reason";
    case CoverageExternalExclusionError::InvalidPlan:
        return "invalid-plan";
    case CoverageExternalExclusionError::InvalidTarget:
        return "invalid-target";
    }
    return "invalid";
}

} // namespace fsim::elaboration
