// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/coverage_database_merge.hpp"
#include "fsim/artifact/coverage_database_partial_merge.hpp"
#include "fsim/artifact/coverage_report_model.hpp"
#include "fsim/artifact/coverage_report_projection.hpp"
#include "fsim/artifact/coverage_report_render.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fsim::artifact;

CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 37U };
}

CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    CoverageDatabaseDigest result { };
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

CoverageDatabaseContents database(const std::uint64_t run_value)
{
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(70U);
    contents.sources = {
        { id(1U), "rtl/top.sv", 100U, digest(1U) },
        { id(2U), "rtl/leaf.vhd", 200U, digest(2U) },
        { id(3U), "rtl/properties.psl", 300U, digest(3U) },
    };
    contents.runs = {
        { id(run_value), "mixed-" + std::to_string(run_value),
            "fsim 3.0.0-dev", run_value, run_value * 10U, 2U,
            CoverageDatabaseRunStatus::Complete },
    };
    contents.metrics = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(10U), id(1U), { },
            id(run_value), run_value - 99U, 0U, false, false, 7U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Instance, id(20U), id(2U), id(200U),
            id(run_value), run_value & 1U, 0U, false, false, 12U },
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCoverpoint,
            CoverageDatabaseMetricScope::Instance, id(30U), id(1U), id(300U),
            id(run_value), 2U, 0U, false, false, 20U },
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Source, id(40U), id(3U), { },
            id(run_value), 3U, 0U, false, false, 4U },
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Instance, id(21U), id(2U), id(200U),
            "generated guard", 13U },
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslDirective,
            CoverageDatabaseMetricScope::Source, id(41U), id(3U), { },
            "disabled requirement", 5U },
    };
    auto made = make_coverage_database_contents(std::move(contents));
    assert(made.ok());
    return std::move(*made.contents);
}

std::filesystem::path temporary_directory()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    return std::filesystem::temp_directory_path()
        / ("fsim-coverage-robustness-" + std::to_string(nonce));
}

void write_u64(std::vector<std::byte>& bytes, const std::size_t offset,
    const std::uint64_t value)
{
    assert(offset <= bytes.size() && bytes.size() - offset >= 8U);
    for (std::size_t index = 0U; index < 8U; ++index) {
        bytes[offset + index]
            = static_cast<std::byte>((value >> (index * 8U)) & 0xffU);
    }
}

void test_mixed_language_database_flow()
{
    const std::array inputs { database(100U), database(101U) };
    const auto saved_inputs = inputs;
    const std::array reverse { inputs[1], inputs[0] };
    const auto first = merge_coverage_databases(inputs);
    const auto second = merge_coverage_databases(reverse);
    assert(first.ok() && second.ok() && first.contents == second.contents);
    assert(inputs == saved_inputs);
    assert(first.contents->sources.size() == 3U
        && first.contents->runs.size() == 2U
        && first.contents->metrics.size() == 8U);

    const auto encoded = serialize_coverage_database(*first.contents);
    assert(encoded.ok());
    const auto decoded = deserialize_coverage_database(encoded.bytes);
    assert(decoded.ok() && decoded.contents == first.contents);

    const auto report = make_coverage_report_model(*decoded.contents);
    assert(report.ok() && report.report->sources.size() == 3U
        && report.report->instances.size() == 2U
        && report.report->combined.metrics.size() == 5U);
    const auto json
        = render_coverage_report(*decoded.contents, CoverageReportFormat::Json);
    assert(json.ok());
    assert(json.output->find("rtl/top.sv") != std::string::npos
        && json.output->find("rtl/leaf.vhd") != std::string::npos
        && json.output->find("rtl/properties.psl") != std::string::npos
        && json.output->find("\"namespace\":\"code\"")
            != std::string::npos
        && json.output->find(
               "\"namespace\":\"systemverilog-functional\"")
            != std::string::npos
        && json.output->find("\"namespace\":\"psl\"")
            != std::string::npos);

    const auto lcov = project_coverage_report(
        *decoded.contents, CoverageReportProjectionFormat::Lcov);
    const auto cobertura = project_coverage_report(
        *decoded.contents, CoverageReportProjectionFormat::Cobertura);
    assert(lcov.ok() && cobertura.ok());
    assert(lcov.output->find("SF:rtl/top.sv\n") != std::string::npos
        && lcov.output->find("SF:rtl/leaf.vhd\n") != std::string::npos
        && lcov.output->find("SF:rtl/properties.psl\n")
            != std::string::npos
        && lcov.output->find("systemverilog-coverpoint")
            == std::string::npos
        && lcov.output->find("psl-property") == std::string::npos);
    assert(cobertura.output->find("filename=\"rtl/top.sv\"")
            != std::string::npos
        && cobertura.output->find("filename=\"rtl/leaf.vhd\"")
            != std::string::npos
        && cobertura.output->find("filename=\"rtl/properties.psl\"")
            != std::string::npos);
}

void test_merge_conflicts_are_transactional()
{
    const auto reference = database(100U);
    auto candidate = database(101U);

    candidate.fingerprint.digest = digest(71U);
    std::array inputs { reference, candidate };
    auto saved = inputs;
    auto rejected = merge_coverage_databases(inputs);
    assert(rejected.error == CoverageDatabaseMergeError::FingerprintMismatch
        && !rejected.contents.has_value() && inputs == saved);

    candidate = database(101U);
    candidate.sources[1].content_digest = digest(22U);
    inputs = { reference, candidate };
    saved = inputs;
    rejected = merge_coverage_databases(inputs);
    assert(rejected.error
            == CoverageDatabaseMergeError::SourceInventoryMismatch
        && !rejected.contents.has_value() && inputs == saved);

    candidate = database(101U);
    candidate.exclusions[0].reason = "conflicting waiver";
    inputs = { reference, candidate };
    saved = inputs;
    rejected = merge_coverage_databases(inputs);
    assert(rejected.error
            == CoverageDatabaseMergeError::ExclusionInventoryMismatch
        && !rejected.contents.has_value() && inputs == saved);

    inputs = { reference, reference };
    saved = inputs;
    rejected = merge_coverage_databases(inputs);
    assert(rejected.error == CoverageDatabaseMergeError::DuplicateRun
        && !rejected.contents.has_value() && inputs == saved);
}

void test_partial_merge_omits_changed_vhdl()
{
    const auto history = database(100U);
    auto changed = database(200U);
    changed.fingerprint.digest = digest(80U);
    changed.sources[1].content_bytes += 1U;
    changed.sources[1].content_digest = digest(12U);
    for (auto& metric : changed.metrics) {
        if (metric.source_identity == id(2U)) {
            metric.bin_identity = id(22U);
        }
    }
    for (auto& exclusion : changed.exclusions) {
        if (exclusion.source_identity == id(2U)) {
            exclusion.point_identity = id(23U);
        }
    }
    const auto target = make_coverage_database_contents(std::move(changed));
    assert(target.ok());
    const std::array historical { history };
    const auto merged = merge_coverage_databases_partially(
        *target.contents, historical);
    assert(merged.ok());
    assert(merged.statistics.unchanged_source_matches == 2U
        && merged.statistics.retained_runs == 1U
        && merged.statistics.omitted_runs == 0U
        && merged.statistics.retained_metrics == 3U
        && merged.statistics.omitted_metrics == 1U
        && merged.statistics.omitted_exclusions == 2U);
    assert(std::ranges::none_of(merged.contents->metrics,
        [](const auto& metric) {
            return metric.run_identity == id(100U)
                && metric.source_identity == id(2U);
        }));
}

void test_corruption_and_size_ceilings()
{
    const auto input = database(100U);
    const auto encoded = serialize_coverage_database(input);
    assert(encoded.ok());

    for (std::size_t size = 0U; size < encoded.bytes.size(); ++size) {
        const auto prefix
            = std::span<const std::byte> { encoded.bytes }.first(size);
        assert(!deserialize_coverage_database(prefix).ok());
    }
    for (std::size_t index = 0U; index < encoded.bytes.size(); ++index) {
        auto corrupted = encoded.bytes;
        corrupted[index] ^= std::byte { 1U };
        assert(!deserialize_coverage_database(corrupted).ok());
    }
    auto corrupted = encoded.bytes;
    corrupted.push_back(std::byte { });
    assert(!deserialize_coverage_database(corrupted).ok());

    corrupted = encoded.bytes;
    write_u64(corrupted, 24U, std::numeric_limits<std::uint64_t>::max());
    auto rejected = deserialize_coverage_database(corrupted);
    assert(rejected.error == CoverageDatabaseCodecError::InvalidSchema
        && rejected.schema_error == CoverageDatabaseSchemaError::ResourceLimit);
    corrupted = encoded.bytes;
    write_u64(corrupted, 88U, std::numeric_limits<std::uint64_t>::max());
    rejected = deserialize_coverage_database(corrupted);
    assert(rejected.error == CoverageDatabaseCodecError::InvalidSchema
        && rejected.schema_error == CoverageDatabaseSchemaError::ResourceLimit);

    auto codec_limits = CoverageDatabaseCodecLimits { };
    codec_limits.container.maximum_container_bytes = encoded.bytes.size() - 1U;
    const auto encode_limited
        = serialize_coverage_database(input, codec_limits);
    assert(encode_limited.error == CoverageDatabaseCodecError::InvalidSchema
        && encode_limited.schema_error
            == CoverageDatabaseSchemaError::ResourceLimit);
    assert(deserialize_coverage_database(encoded.bytes, codec_limits).error
        == CoverageDatabaseCodecError::ResourceLimit);
    codec_limits = { };
    codec_limits.model.maximum_metrics = 3U;
    assert(serialize_coverage_database(input, codec_limits).error
        == CoverageDatabaseCodecError::InvalidModel);
    assert(deserialize_coverage_database(encoded.bytes, codec_limits).error
        == CoverageDatabaseCodecError::ResourceLimit);

    const std::array strict_inputs { input, database(101U) };
    auto merge_limits = CoverageDatabaseMergeLimits { };
    merge_limits.maximum_inputs = 1U;
    assert(merge_coverage_databases(strict_inputs, merge_limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    merge_limits = { };
    merge_limits.model.maximum_runs = 1U;
    assert(merge_coverage_databases(strict_inputs, merge_limits).error
        == CoverageDatabaseMergeError::ResourceLimit);

    auto partial_limits = CoverageDatabasePartialMergeLimits { };
    partial_limits.maximum_historical_metrics = 3U;
    const std::array history { database(101U) };
    assert(merge_coverage_databases_partially(input, history, partial_limits)
               .error
        == CoverageDatabaseMergeError::ResourceLimit);

    auto model_limits = CoverageReportModelLimits { };
    model_limits.maximum_exact_points = 0U;
    assert(make_coverage_report_model(input, model_limits).error
        == CoverageReportModelError::ResourceLimit);
    auto render_limits = CoverageReportRenderLimits { };
    render_limits.maximum_output_bytes = 1U;
    const auto rendered = render_coverage_report(
        input, CoverageReportFormat::Json, render_limits);
    assert(rendered.error == CoverageReportRenderError::ResourceLimit
        && !rendered.output.has_value());
    auto projection_limits = CoverageReportProjectionLimits { };
    projection_limits.maximum_lines = 0U;
    const auto projected = project_coverage_report(
        input, CoverageReportProjectionFormat::Lcov, projection_limits);
    assert(projected.error == CoverageReportProjectionError::ResourceLimit
        && !projected.output.has_value());
}

void test_path_and_atomic_file_safety()
{
    const auto input = database(100U);
    auto unsafe = input;
    unsafe.sources[0].logical_path = "C:/checkout/rtl/top.sv";
    assert(make_coverage_database_contents(unsafe).error
        == CoverageDatabaseModelError::InvalidSource);
    assert(serialize_coverage_database(unsafe).error
        == CoverageDatabaseCodecError::InvalidModel);
    assert(render_coverage_report(unsafe, CoverageReportFormat::Json).error
        == CoverageReportRenderError::InvalidReportModel);
    assert(project_coverage_report(
               unsafe, CoverageReportProjectionFormat::Cobertura)
               .error
        == CoverageReportProjectionError::InvalidReportModel);

    const auto directory = temporary_directory();
    const auto path = directory / "nested" / "results.fsimcov";
    assert(write_coverage_database_atomically(path, input).ok());
    assert(read_coverage_database(path).contents == std::optional { input });
    assert(write_coverage_database_atomically(path, unsafe).error
        == CoverageDatabaseCodecError::InvalidModel);
    assert(read_coverage_database(path).contents == std::optional { input });

    const auto encoded = serialize_coverage_database(input);
    assert(encoded.ok());
    auto corrupted = encoded.bytes;
    corrupted.back() ^= std::byte { 1U };
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        assert(stream);
        stream.write(reinterpret_cast<const char*>(corrupted.data()),
            static_cast<std::streamsize>(corrupted.size()));
        assert(stream);
    }
    assert(!read_coverage_database(path).ok());
    std::filesystem::remove_all(directory);
}

} // namespace

int main()
{
    test_mixed_language_database_flow();
    test_merge_conflicts_are_transactional();
    test_partial_merge_omits_changed_vhdl();
    test_corruption_and_size_ceilings();
    test_path_and_atomic_file_safety();
}
