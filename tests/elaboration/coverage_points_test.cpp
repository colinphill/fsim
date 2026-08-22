// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_points.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fsim;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

elaboration::CoverageInventorySource source(const std::size_t bytes = 100U)
{
    const auto contents = std::string(bytes, 'x');
#if defined(_WIN32)
    const auto root = std::filesystem::path { "C:/coverage-exclusions" };
#else
    const auto root = std::filesystem::path { "/coverage-exclusions" };
#endif
    const auto path = root / "rtl/exclusions.sv";
    auto identity = frontend::make_code_coverage_source_identity(
        root, path,
        std::as_bytes(std::span { contents.data(), contents.size() }));
    require(identity.ok(), "Exclusion fixture source identity must be valid");
    return { path.generic_string(), std::move(*identity.identity) };
}

elaboration::CoverageInventoryPointDraft point(
    const std::uint64_t identity,
    const std::uint64_t begin,
    const std::uint64_t end,
    const runtime::CodeCoverageMetric metric
    = runtime::CodeCoverageMetric::Statement)
{
    return { { identity, identity + 100U }, metric, 0U,
        { begin, end }, 1U };
}

bool same_point(
    const elaboration::CoverageInventoryPointDraft& left,
    const elaboration::CoverageInventoryPointDraft& right)
{
    return left.id == right.id && left.metric == right.metric
        && left.source_index == right.source_index
        && left.span == right.span && left.line == right.line;
}

bool same_points(
    const std::span<const elaboration::CoverageInventoryPointDraft> left,
    const std::span<const elaboration::CoverageInventoryPointDraft> right)
{
    return std::ranges::equal(left, right, same_point);
}

void test_declarations_and_removed_constructs()
{
    const auto input_source = source();
    const std::vector input_points {
        point(1U, 5U, 50U),
        point(2U, 20U, 30U),
        point(3U, 22U, 25U, runtime::CodeCoverageMetric::Branch),
        point(4U, 40U, 45U),
    };
    const std::vector exclusions {
        elaboration::CoveragePointExclusion { 0U, { 10U, 15U },
            elaboration::CoveragePointExclusionKind::Declaration },
        elaboration::CoveragePointExclusion { 0U, { 20U, 30U },
            elaboration::CoveragePointExclusionKind::StaticallyRemoved },
    };
    const auto filtered = elaboration::exclude_coverage_points(
        std::span { &input_source, 1U }, input_points, exclusions);
    require(filtered.ok() && filtered.points.size() == 2U,
        "Only points wholly owned by removed syntax may be excluded");
    require(same_point(filtered.points[0], input_points[0])
            && same_point(filtered.points[1], input_points[3]),
        "Enclosing and neighboring executable identities must not change");

    const auto declaration_only = elaboration::exclude_coverage_points(
        std::span { &input_source, 1U }, input_points,
        std::span { exclusions.data(), 1U });
    require(declaration_only.ok()
            && same_points(declaration_only.points, input_points),
        "A declaration with no executable point must not affect neighbors");

    const auto no_exclusions = elaboration::exclude_coverage_points(
        std::span { &input_source, 1U }, input_points, { });
    require(no_exclusions.ok()
            && same_points(no_exclusions.points, input_points),
        "An empty exclusion set must preserve every identity and ordering");
}

void test_validation_and_resource_limits()
{
    const auto input_source = source();
    const std::vector input_points { point(1U, 20U, 30U) };
    const elaboration::CoveragePointExclusion exclusion { 0U, { 20U, 30U },
        elaboration::CoveragePointExclusionKind::StaticallyRemoved };

    const auto points_limited = elaboration::exclude_coverage_points(
        std::span { &input_source, 1U }, input_points,
        std::span { &exclusion, 1U }, { 0U, 1U });
    require(points_limited.error
            == elaboration::CoveragePointExclusionError::ResourceLimit,
        "The point ceiling must reject before publication");
    const auto exclusions_limited = elaboration::exclude_coverage_points(
        std::span { &input_source, 1U }, input_points,
        std::span { &exclusion, 1U }, { 1U, 0U });
    require(exclusions_limited.error
            == elaboration::CoveragePointExclusionError::ResourceLimit,
        "The exclusion ceiling must reject before publication");

    auto invalid_source = exclusion;
    invalid_source.source_index = 1U;
    require(elaboration::exclude_coverage_points(
                std::span { &input_source, 1U }, input_points,
                std::span { &invalid_source, 1U })
                .error
            == elaboration::CoveragePointExclusionError::InvalidSource,
        "An exclusion cannot name an unknown source");
    auto invalid_span = exclusion;
    invalid_span.span = { 30U, 30U };
    require(elaboration::exclude_coverage_points(
                std::span { &input_source, 1U }, input_points,
                std::span { &invalid_span, 1U })
                .error
            == elaboration::CoveragePointExclusionError::InvalidExclusionSpan,
        "An exclusion span must be nonempty and source-bounded");
    auto invalid_kind = exclusion;
    invalid_kind.kind
        = static_cast<elaboration::CoveragePointExclusionKind>(255U);
    require(elaboration::exclude_coverage_points(
                std::span { &input_source, 1U }, input_points,
                std::span { &invalid_kind, 1U })
                .error
            == elaboration::CoveragePointExclusionError::InvalidExclusionKind,
        "Unknown exclusion kinds must be rejected");

    const std::vector duplicate_exclusions { exclusion, exclusion };
    require(elaboration::exclude_coverage_points(
                std::span { &input_source, 1U }, input_points,
                duplicate_exclusions)
                .error
            == elaboration::CoveragePointExclusionError::DuplicateExclusion,
        "Duplicate exclusions must not create ambiguous ownership");
    const std::vector duplicate_points {
        input_points.front(), input_points.front()
    };
    require(elaboration::exclude_coverage_points(
                std::span { &input_source, 1U }, duplicate_points, { })
                .error
            == elaboration::CoveragePointExclusionError::DuplicatePoint,
        "Duplicate input identities must be rejected transactionally");
}

} // namespace

int main()
{
    test_declarations_and_removed_constructs();
    test_validation_and_resource_limits();
    std::cout << "coverage point exclusion tests passed\n";
}
