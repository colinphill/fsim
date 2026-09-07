// SPDX-License-Identifier: Apache-2.0
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/elaboration/coverage_external_exclusions.hpp"
#include "fsim/project/project.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::optional<fsim::project::Config> parse_manifest(
    const std::string_view text, fsim::diagnostic::Engine& diagnostics)
{
    return fsim::project::parse(text, "fsim.toml",
        std::filesystem::path { "/checkout" }, diagnostics);
}

fsim::elaboration::CoverageExternalExclusionMatchResult match_one(
    const fsim::elaboration::CoverageExternalExclusionPlan& plan,
    const fsim::elaboration::CoverageExternalExclusionTarget& target,
    const fsim::elaboration::CoverageExternalExclusionLimits limits = { })
{
    return fsim::elaboration::match_coverage_external_exclusions(
        plan, std::span { &target, 1U }, limits);
}

void test_manifest_and_matching()
{
    using namespace fsim;
    constexpr std::string_view manifest = R"(
schema = 3

[project]
top = "tb"

[coverage]
enabled = true

[[library_map]]
library = "vendor"
path = "vendor.fsimlib"

[[coverage.exclude]]
source = "rtl/generated/*.sv"
metric = "statement"
reason = "generated protocol adapter"

[[coverage.exclude]]
hierarchy = "tb.u_*"
object = "status*"
metric = "toggle"
reason = "unused status plumbing"

[[coverage.exclude]]
metric = "psl_property"
reason = "property waived by verification plan"

[[coverage.exclude]]
hierarchy = "tb.*"
metric = "all"
reason = "integration wrapper"
)";
    diagnostic::Engine diagnostics;
    const auto config = parse_manifest(manifest, diagnostics);
    assert(config && !diagnostics.has_error());
    assert(config->coverage.enabled
        && config->coverage.exclusions.size() == 4U);

    const auto built = elaboration::make_coverage_external_exclusion_plan(
        config->coverage.exclusions);
    assert(built.ok() && built.plan->rules.size() == 4U
        && built.plan->identity.size() == 64U);
    assert(std::ranges::all_of(built.plan->rules, [](const auto& rule) {
        return rule.identity.size() == 64U && !rule.reason.empty();
    }));

    const auto statement = match_one(
        *built.plan,
        { "rtl/generated/adapter.sv", "tb.u_adapter", "assignment",
            frontend::CoverageSourceMetric::Statement });
    assert(statement.ok() && statement.targets.size() == 1U
        && statement.targets.front().rule_indices.size() == 2U);
    assert(std::ranges::any_of(statement.targets.front().rule_indices,
        [&](const auto index) {
            return built.plan->rules[index].source_pattern
                == std::optional<std::string> { "rtl/generated/*.sv" };
        }));

    const auto toggle = match_one(
        *built.plan,
        { "rtl/core.sv", "tb.u_encoder", "status_valid",
            frontend::CoverageSourceMetric::Toggle });
    assert(toggle.ok()
        && toggle.targets.front().rule_indices.size() == 2U);
    assert(std::ranges::any_of(toggle.targets.front().rule_indices,
        [&](const auto index) {
            const auto& rule = built.plan->rules[index];
            return rule.hierarchy_pattern
                    == std::optional<std::string> { "tb.u_*" }
                && rule.object_pattern
                    == std::optional<std::string> { "status*" };
        }));

    const auto property = match_one(
        *built.plan,
        { "properties/codec.psl", "tb.u_encoder", "never_overflow",
            frontend::CoverageSourceMetric::PslProperty });
    assert(property.ok()
        && property.targets.front().rule_indices.size() == 2U);

    const auto retained = match_one(
        *built.plan,
        { "rtl/core.sv", "other.u_encoder", "data_valid",
            frontend::CoverageSourceMetric::Toggle });
    assert(retained.ok()
        && retained.targets.front().rule_indices.empty());

    auto reordered = config->coverage.exclusions;
    std::ranges::reverse(reordered);
    const auto rebuilt
        = elaboration::make_coverage_external_exclusion_plan(reordered);
    assert(rebuilt.ok() && rebuilt.plan->identity == built.plan->identity);
    std::vector<std::string> identities;
    std::ranges::transform(built.plan->rules,
        std::back_inserter(identities), &elaboration::CoverageExternalExclusionRule::identity);
    std::vector<std::string> reordered_identities;
    std::ranges::transform(rebuilt.plan->rules,
        std::back_inserter(reordered_identities),
        &elaboration::CoverageExternalExclusionRule::identity);
    assert(identities == reordered_identities);
}

void test_validation_and_resources()
{
    using namespace fsim;
    using Error = elaboration::CoverageExternalExclusionError;
    const auto expect = [](const project::CoverageExclusionEntry& entry,
                            const Error error) {
        const auto result = elaboration::make_coverage_external_exclusion_plan(
            std::span { &entry, 1U });
        assert(!result.ok() && result.error == error);
    };
    expect({ .source = std::nullopt, .hierarchy = std::nullopt,
               .object = std::nullopt, .metric = "", .reason = "missing" },
        Error::MissingMetric);
    expect({ .source = std::nullopt, .hierarchy = std::nullopt,
               .object = std::nullopt, .metric = "unknown", .reason = "bad" },
        Error::UnknownMetric);
    expect({ .source = std::nullopt, .hierarchy = std::nullopt,
               .object = std::nullopt, .metric = "line", .reason = "" },
        Error::MissingReason);
    expect({ .source = "", .hierarchy = std::nullopt,
               .object = std::nullopt, .metric = "line", .reason = "empty" },
        Error::EmptySelector);
    expect({ .source = "../rtl/*.sv", .hierarchy = std::nullopt,
               .object = std::nullopt, .metric = "line", .reason = "unsafe" },
        Error::InvalidSourcePattern);
    expect({ .source = std::nullopt, .hierarchy = ".tb",
               .object = std::nullopt, .metric = "line", .reason = "bad" },
        Error::InvalidHierarchyPattern);
    expect({ .source = std::nullopt, .hierarchy = std::nullopt,
               .object = "bad/name", .metric = "toggle", .reason = "bad" },
        Error::InvalidObjectPattern);

    const project::CoverageExclusionEntry one {
        .source = "rtl/*.sv", .hierarchy = std::nullopt,
        .object = std::nullopt, .metric = "statement", .reason = "one"
    };
    const std::array duplicate { one, one };
    assert(elaboration::make_coverage_external_exclusion_plan(duplicate).error
        == Error::DuplicateRule);
    auto conflicting = one;
    conflicting.reason = "two";
    const std::array conflict { one, conflicting };
    assert(elaboration::make_coverage_external_exclusion_plan(conflict).error
        == Error::ConflictingReason);

    auto limits = elaboration::CoverageExternalExclusionLimits { };
    limits.maximum_rules = 0U;
    assert(elaboration::make_coverage_external_exclusion_plan(
               std::span { &one, 1U }, limits)
               .error
        == Error::ResourceLimit);
    limits = { };
    limits.maximum_pattern_bytes = 1U;
    assert(elaboration::make_coverage_external_exclusion_plan(
               std::span { &one, 1U }, limits)
               .error
        == Error::ResourceLimit);
    limits = { };
    limits.maximum_reason_bytes = 1U;
    assert(elaboration::make_coverage_external_exclusion_plan(
               std::span { &one, 1U }, limits)
               .error
        == Error::ResourceLimit);
    limits = { };
    limits.maximum_total_bytes = 1U;
    assert(elaboration::make_coverage_external_exclusion_plan(
               std::span { &one, 1U }, limits)
               .error
        == Error::ResourceLimit);

    const auto plan = elaboration::make_coverage_external_exclusion_plan(
        std::span { &one, 1U });
    assert(plan.ok());
    const std::array targets {
        elaboration::CoverageExternalExclusionTarget { "rtl/a.sv", "tb",
            "x", frontend::CoverageSourceMetric::Statement },
        elaboration::CoverageExternalExclusionTarget { "rtl/b.sv", "tb",
            "x", frontend::CoverageSourceMetric::Branch },
    };
    const auto batch = elaboration::match_coverage_external_exclusions(
        *plan.plan, targets);
    assert(batch.ok() && batch.targets.size() == 2U
        && batch.targets[0].rule_indices.size() == 1U
        && batch.targets[1].rule_indices.empty());

    auto tampered = *plan.plan;
    tampered.identity.front() = tampered.identity.front() == '0' ? '1' : '0';
    assert(elaboration::match_coverage_external_exclusions(tampered, targets)
               .error
        == Error::InvalidPlan);
    assert(match_one(*plan.plan,
               { "../bad.sv", "tb", "x",
                   frontend::CoverageSourceMetric::Statement })
               .error
        == Error::InvalidTarget);
    limits = { };
    limits.maximum_targets = 0U;
    assert(elaboration::match_coverage_external_exclusions(
               *plan.plan, targets, limits)
               .error
        == Error::ResourceLimit);
    limits = { };
    limits.maximum_match_operations = 0U;
    assert(match_one(*plan.plan,
               { "rtl/a.sv", "tb", "x",
                   frontend::CoverageSourceMetric::Statement },
               limits)
               .error
        == Error::ResourceLimit);
}

void test_manifest_rejections()
{
    const auto reject = [](const std::string_view table) {
        fsim::diagnostic::Engine diagnostics;
        const auto config = parse_manifest(
            std::string { "schema = 3\n[project]\ntop = \"tb\"\n"
                          "[[library_map]]\nlibrary = \"vendor\"\n"
                          "path = \"vendor.fsimlib\"\n" }
                + std::string { table },
            diagnostics);
        assert(!config && diagnostics.has_error());
    };
    reject("[[coverage.exclude]]\nreason = \"missing metric\"\n");
    reject("[[coverage.exclude]]\nmetric = \"statement\"\n");
    reject("[[coverage.exclude]]\nsource = \"\"\nmetric = \"statement\"\nreason = \"empty\"\n");
    reject("[[coverage.exclude]]\nmetric = \"statement\"\nreason = \"ok\"\nunknown = \"bad\"\n");
}

} // namespace

int main()
{
    test_manifest_and_matching();
    test_validation_and_resources();
    test_manifest_rejections();
}
