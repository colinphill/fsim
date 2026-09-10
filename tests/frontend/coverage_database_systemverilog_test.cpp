// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/coverage_database_systemverilog.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>

namespace {

using namespace fsim;

artifact::CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 19U };
}

artifact::CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    artifact::CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

artifact::CoverageDatabaseContents database()
{
    artifact::CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(80U);
    contents.sources = {
        { id(1U), "tb/codec_tb.sv", 100U, digest(1U) },
        { id(2U), "tb/coverage.svh", 200U, digest(2U) },
    };
    contents.runs = { { id(3U), "run-1", "fsim 3.0.0-dev", 9U, 50U,
        2U, artifact::CoverageDatabaseRunStatus::Complete } };
    return contents;
}

frontend::SourceSpan span(const char* source)
{
    frontend::SourceSpan result;
    result.source_name = source;
    result.physical_source_name = source;
    result.begin.offset = 1U;
    result.begin.line = 17U;
    result.end.offset = 2U;
    result.end.line = 17U;
    return result;
}

frontend::SystemVerilogCoverageState coverage()
{
    using namespace frontend;
    SystemVerilogCoverageState state;
    SystemVerilogCovergroupDeclaration declaration;
    declaration.name = "codec_cg";
    declaration.canonical_identity = "work::codec_tb::codec_cg";
    declaration.span = span("tb/codec_tb.sv");

    SystemVerilogCoverageDeclaration coverpoint;
    coverpoint.kind = SystemVerilogCoverageDeclarationKind::Coverpoint;
    coverpoint.name = "symbol";
    coverpoint.declaration_index = 10U;
    coverpoint.span = span("tb/codec_tb.sv");
    SystemVerilogCoverageBin regular;
    regular.name = "data";
    regular.declaration_index = 20U;
    regular.span = coverpoint.span;
    coverpoint.bins.push_back(regular);
    auto unhit = regular;
    unhit.name = "unhit";
    unhit.declaration_index = 22U;
    coverpoint.bins.push_back(unhit);

    SystemVerilogCoverageDeclaration cross;
    cross.kind = SystemVerilogCoverageDeclarationKind::Cross;
    cross.name = "symbol_x_ready";
    cross.declaration_index = 11U;
    cross.span = span("tb/coverage.svh");
    SystemVerilogCoverageBin cross_bin;
    cross_bin.kind = SystemVerilogCoverageBinKind::Ignore;
    cross_bin.name = "accepted";
    cross_bin.declaration_index = 21U;
    cross_bin.span = cross.span;
    cross.bins.push_back(cross_bin);
    declaration.coverage_declarations = { coverpoint, cross };
    state.declarations.push_back(declaration);

    SystemVerilogCovergroupInstance first;
    first.name = "cg0";
    first.declaration_identity = declaration.canonical_identity;
    first.runtime_identity = "top.encoder.cg0";
    SystemVerilogCoverageBinHit hit;
    hit.coverage_declaration_index = 10U;
    hit.bin_declaration_index = 20U;
    hit.identity = "work::codec_tb::codec_cg::symbol.data";
    hit.hit_count = 5U;
    hit.at_least = 3U;
    hit.covered = true;
    first.bin_hits.push_back(hit);
    SystemVerilogCoverageCrossBinState cross_state;
    cross_state.coverage_declaration_index = 11U;
    cross_state.bin_declaration_index = 21U;
    cross_state.identity
        = "work::codec_tb::codec_cg::symbol_x_ready.accepted";
    cross_state.hit_count = std::numeric_limits<std::uint64_t>::max();
    cross_state.exclusion_count
        = std::numeric_limits<std::uint64_t>::max();
    cross_state.at_least = 1U;
    cross_state.excluded = true;
    cross_state.covered = false;
    first.cross_bin_state.push_back(cross_state);
    auto zero_cross_state = cross_state;
    zero_cross_state.bin_declaration_index.reset();
    zero_cross_state.identity
        = "work::codec_tb::codec_cg::symbol_x_ready<symbol.data,ready.one>";
    zero_cross_state.hit_count = 0U;
    zero_cross_state.exclusion_count = 0U;
    zero_cross_state.excluded = false;
    first.cross_bin_state.push_back(zero_cross_state);

    auto second = first;
    second.name = "cg1";
    second.runtime_identity = "top.decoder.cg1";
    second.bin_hits.front().hit_count = 1U;
    second.bin_hits.front().covered = false;
    second.cross_bin_state.clear();
    state.instances = { second, first };
    return state;
}

std::array<frontend::SystemVerilogCoverageDatabaseSource, 2> sources()
{
    return { frontend::SystemVerilogCoverageDatabaseSource {
                 "tb/coverage.svh", id(2U) },
        frontend::SystemVerilogCoverageDatabaseSource {
            "tb/codec_tb.sv", id(1U) } };
}

} // namespace

int main()
{
    using namespace fsim;
    using frontend::SystemVerilogCoverageDatabaseError;

    static_assert(frontend::kSystemVerilogCoverageDatabaseDiagnostic
        == "FSIM-COV-033");
    const auto bindings = sources();
    const auto made = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U));
    assert(made.ok());
    assert(frontend::project_systemverilog_coverage_namespace(
               database(), coverage(), bindings, id(3U))
               .contents
        == made.contents);
    assert(made.contents->metrics.size() == 6U);
    assert(made.contents->exclusions.size() == 1U);
    assert(std::ranges::all_of(made.contents->metrics, [](const auto& metric) {
        return metric.name_space
            == artifact::CoverageDatabaseNamespace::SystemVerilogFunctional
            && metric.scope
            == artifact::CoverageDatabaseMetricScope::Instance
            && metric.run_identity == id(3U) && metric.source_line == 17U;
    }));
    const auto five_hit = std::ranges::find(
        made.contents->metrics, 5U,
        &artifact::CoverageDatabaseMetricRecord::hits);
    const auto one_hit = std::ranges::find(
        made.contents->metrics, 1U,
        &artifact::CoverageDatabaseMetricRecord::hits);
    assert(five_hit != made.contents->metrics.end()
        && one_hit != made.contents->metrics.end()
        && five_hit->family
            == artifact::CoverageDatabaseMetricFamily::SystemVerilogCoverpoint
        && one_hit->family
            == artifact::CoverageDatabaseMetricFamily::SystemVerilogCoverpoint
        && five_hit->instance_identity != one_hit->instance_identity);
    const auto cross = std::ranges::find_if(
        made.contents->metrics,
        [](const auto& metric) {
            return metric.family
                    == artifact::CoverageDatabaseMetricFamily::SystemVerilogCross
                && metric.hits
                    == std::numeric_limits<std::uint64_t>::max();
        });
    assert(cross != made.contents->metrics.end()
        && cross->source_identity == id(2U)
        && cross->hits == std::numeric_limits<std::uint64_t>::max()
        && cross->excluded_hits == std::numeric_limits<std::uint64_t>::max()
        && cross->overflow && cross->excluded_overflow);
    assert(std::ranges::any_of(made.contents->metrics, [](const auto& metric) {
        return metric.family
                == artifact::CoverageDatabaseMetricFamily::SystemVerilogCross
            && metric.hits == 0U && metric.excluded_hits == 0U
            && !metric.overflow && !metric.excluded_overflow;
    }));
    assert(std::ranges::count_if(
               made.contents->metrics,
               [](const auto& metric) {
                   return metric.family
                           == artifact::CoverageDatabaseMetricFamily::SystemVerilogCoverpoint
                       && metric.hits == 0U;
               })
        == 2U);
    assert(made.contents->exclusions.front().point_identity
            == cross->bin_identity
        && made.contents->exclusions.front().source_line == 17U);

    auto result = frontend::project_systemverilog_coverage_namespace(
        *made.contents, coverage(), bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::NamespaceNotEmpty);
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(99U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidRun);

    auto invalid_database = database();
    invalid_database.fingerprint.schema = 2U;
    result = frontend::project_systemverilog_coverage_namespace(
        std::move(invalid_database), coverage(), bindings, id(3U));
    assert(result.error
            == SystemVerilogCoverageDatabaseError::InvalidDatabaseModel
        && result.model_error
            == artifact::CoverageDatabaseModelError::InvalidFingerprint);

    auto invalid_bindings = bindings;
    invalid_bindings[0].source_identity = id(99U);
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), invalid_bindings, id(3U));
    assert(result.error
        == SystemVerilogCoverageDatabaseError::InvalidSourceBinding);
    invalid_bindings = bindings;
    invalid_bindings[1].source_name = invalid_bindings[0].source_name;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), invalid_bindings, id(3U));
    assert(result.error
        == SystemVerilogCoverageDatabaseError::DuplicateSourceBinding);

    auto invalid = coverage();
    invalid.declarations[0].canonical_identity.clear();
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error
        == SystemVerilogCoverageDatabaseError::InvalidDeclaration);
    invalid = coverage();
    invalid.declarations.push_back(invalid.declarations.front());
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error
        == SystemVerilogCoverageDatabaseError::DuplicateDeclaration);
    invalid = coverage();
    invalid.instances[0].runtime_identity.clear();
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidInstance);
    invalid = coverage();
    invalid.instances[1].runtime_identity
        = invalid.instances[0].runtime_identity;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::DuplicateInstance);
    invalid = coverage();
    invalid.instances[0].declaration_identity = "missing";
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidInstance);

    invalid = coverage();
    invalid.instances[0].bin_hits[0].identity.clear();
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidBin);
    invalid = coverage();
    invalid.instances[0].bin_hits[0].bin_declaration_index = 999U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidBin);
    invalid = coverage();
    invalid.declarations[0].coverage_declarations[0].bins[0].kind
        = frontend::SystemVerilogCoverageBinKind::Ignore;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidBin);
    invalid = coverage();
    invalid.instances[0].bin_hits[0].covered = true;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidBin);
    invalid = coverage();
    invalid.declarations[0].coverage_declarations[0].span
        = span("missing.sv");
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::UnknownSource);
    invalid = coverage();
    invalid.instances[1].cross_bin_state[0].covered = true;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidBin);

    auto limits = frontend::SystemVerilogCoverageDatabaseLimits { };
    limits.maximum_source_bindings = 1U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == SystemVerilogCoverageDatabaseError::ResourceLimit);
    limits = { };
    limits.maximum_declarations = 0U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == SystemVerilogCoverageDatabaseError::ResourceLimit);
    limits = { };
    limits.maximum_instances = 1U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == SystemVerilogCoverageDatabaseError::ResourceLimit);
    limits = { };
    limits.maximum_bins = 3U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == SystemVerilogCoverageDatabaseError::ResourceLimit);
    limits = { };
    limits.maximum_identity_bytes = 3U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == SystemVerilogCoverageDatabaseError::InvalidDeclaration);
    limits = { };
    limits.maximum_source_name_bytes = 3U;
    result = frontend::project_systemverilog_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error
        == SystemVerilogCoverageDatabaseError::InvalidSourceBinding);
}
