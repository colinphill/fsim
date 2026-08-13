// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_schema.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

void require_diagnostic(const fsim::app::SdfSchemaDecodeResult& result,
    const std::string_view code)
{
    if (std::ranges::any_of(result.diagnostics,
            [&](const fsim::frontend::Diagnostic& diagnostic) {
                return diagnostic.code == code;
            })) {
        return;
    }
    std::string message = "missing expected schema diagnostic "
        + std::string { code } + "; observed";
    for (const auto& diagnostic : result.diagnostics)
        message += ' ' + diagnostic.code;
    throw std::runtime_error(message);
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    elaboration::SpecializationInfo specialization;
    specialization.id = 0U;
    specialization.unit = "sv:work.top";
    specialization.instance = "top";
    specialization.language = frontend::Language::SystemVerilog2017;
    state.specializations.push_back(std::move(specialization));
    for (const auto& [name, direction] : std::to_array<std::pair<std::string_view,
             frontend::PortDirection>>({ { "top.A", frontend::PortDirection::Input },
             { "top.Z", frontend::PortDirection::Output } })) {
        const auto id = static_cast<runtime::simir::SignalId>(
            state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        info.is_port = true;
        info.direction = direction;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(std::string { name },
            runtime::PackedLogic4(1U, runtime::Logic4::zero));
        state.signal_names.emplace_back(name, id);
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "schema design fixture must be valid");
    return std::move(*design);
}

std::string source(const std::string_view revision)
{
    return "(DELAYFILE\n"
           "  (SDFVERSION \""
        + std::string { revision }
    + "\")\n"
      "  (DESIGN \"top\")\n"
      "  (VENDOR \"fsim\")\n"
      "  (DIVIDER .)\n"
      "  (TIMESCALE 1 ns)\n"
      "  (CELL (CELLTYPE \"top\") (INSTANCE top)\n"
      "    (DELAY (ABSOLUTE (IOPATH A Z (1)))))\n"
      ")";
}

struct Fixture {
    fsim::frontend::SdfFile file;
    std::shared_ptr<const fsim::app::SdfAnnotationSummary> summary;
    std::string source_text;
};

Fixture make_fixture(const std::string_view revision,
    const fsim::elaboration::ElaboratedDesign& design)
{
    using namespace fsim::app;
    Fixture result;
    result.source_text = source(revision);
    auto parsed = fsim::frontend::parse_sdf(
        { "schema.sdf", result.source_text });
    require(parsed.ok(), "schema SDF fixture must parse");
    SdfAnnotationScopeRequest request;
    request.selection = SdfScopeSelection::All;
    request.expected_project_identity = "project:schema";
    request.expected_design_identity = "design:schema";
    const auto scope = bind_sdf_annotation_scope(parsed.file, design,
        "project:schema", "design:schema", request);
    require(scope.ok(), "schema scope must bind");
    const auto cells = resolve_sdf_cells(scope.scope, design);
    require(cells.ok(), "schema cells must resolve");
    const auto endpoints = resolve_sdf_endpoints(cells.resolution, design);
    require(endpoints.ok(), "schema endpoints must resolve");
    const auto mapping = validate_sdf_mapping(endpoints.resolution, design);
    require(mapping.ok(), "schema mapping must validate");
    result.file = std::move(parsed.file);
    result.summary = mapping.summary;
    return result;
}

fsim::app::SdfSchemaOptions options()
{
    return { "sdf-parse-options-v1", "sdf-normalization-options-v1",
        "fsim-compiler-compat-v1" };
}

std::vector<std::byte> encoded_fixture(
    const Fixture& fixture, const fsim::app::SdfSchemaLimits limits = { })
{
    const auto encoded = fsim::app::encode_sdf_schema(fixture.file,
        *fixture.summary, fixture.source_text, options(), limits);
    require(encoded.ok(), "schema envelope must encode");
    return encoded.bytes;
}

void test_roundtrip_metadata()
{
    const auto design = make_design();
    const auto fixture = make_fixture("4.0", design);
    const auto bytes = encoded_fixture(fixture);
    const auto decoded = fsim::app::decode_sdf_schema(bytes,
        "fsim-compiler-compat-v1", fixture.summary->semantic_identity());
    require(decoded.ok(), "schema envelope must decode");
    require(decoded.snapshot->revision() == fsim::frontend::SdfRevision::Sdf40
            && decoded.snapshot->headers().size() == fixture.file.headers.size()
            && decoded.snapshot->headers().front().span
                == fixture.file.headers.front().span
            && decoded.snapshot->source_span() == fixture.file.span
            && decoded.snapshot->cell_count()
                == fixture.file.normalized_ir->cells().size()
            && decoded.snapshot->node_count()
                == fixture.file.normalized_ir->nodes().size()
            && decoded.snapshot->mapping_count()
                == fixture.summary->endpoint_resolution()->nodes().size()
            && decoded.snapshot->options() == options()
            && decoded.snapshot->source_checksum().size() == 64U
            && decoded.snapshot->envelope_checksum().size() == 64U,
        "schema roundtrip must retain revision, headers, spans, options, counts, and checksums");
}

void test_revision_and_deterministic_envelopes()
{
    const auto design = make_design();
    const auto sdf21 = make_fixture("2.1", design);
    const auto sdf30 = make_fixture("3.0", design);
    const auto sdf40 = make_fixture("4.0", design);
    const auto first = encoded_fixture(sdf40);
    const auto second = encoded_fixture(sdf40);
    require(first == second, "identical schema inputs must encode identically");
    const auto decoded21 = fsim::app::decode_sdf_schema(
        encoded_fixture(sdf21), "fsim-compiler-compat-v1");
    const auto decoded30 = fsim::app::decode_sdf_schema(
        encoded_fixture(sdf30), "fsim-compiler-compat-v1");
    require(decoded21.ok() && decoded30.ok()
            && decoded21.snapshot->revision()
                == fsim::frontend::SdfRevision::Sdf21
            && decoded30.snapshot->revision()
                == fsim::frontend::SdfRevision::Sdf30,
        "schema envelope must retain canonical governed revisions");
}

void test_corruption_staleness_and_resources()
{
    const auto design = make_design();
    const auto fixture = make_fixture("4.0", design);
    const auto valid = encoded_fixture(fixture);

    auto truncated = valid;
    truncated.pop_back();
    auto result = fsim::app::decode_sdf_schema(
        truncated, "fsim-compiler-compat-v1");
    require(!result.ok(), "truncated schema must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-004");

    auto reordered = valid;
    reordered[0] = std::byte { 2U };
    result = fsim::app::decode_sdf_schema(
        reordered, "fsim-compiler-compat-v1");
    require(!result.ok(), "reordered/omitted first record must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-004");

    auto corrupt = valid;
    corrupt[20] ^= std::byte { 0x1U };
    result = fsim::app::decode_sdf_schema(
        corrupt, "fsim-compiler-compat-v1");
    require(!result.ok(), "checksum corruption must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-004");

    auto future = valid;
    constexpr std::size_t first_version_offset = 23U;
    future[first_version_offset] = std::byte { 2U };
    result = fsim::app::decode_sdf_schema(
        future, "fsim-compiler-compat-v1");
    require(!result.ok(), "future schema version must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-002");

    result = fsim::app::decode_sdf_schema(valid, "stale-compiler");
    require(!result.ok(), "stale compiler compatibility must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-003");
    result = fsim::app::decode_sdf_schema(
        valid, "fsim-compiler-compat-v1", "stale-summary");
    require(!result.ok(), "stale mapping identity must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-003");

    fsim::app::SdfSchemaLimits limits;
    limits.max_bytes = valid.size() - 1U;
    result = fsim::app::decode_sdf_schema(
        valid, "fsim-compiler-compat-v1", { }, limits);
    require(!result.ok(), "schema byte limit must fail");
    require_diagnostic(result, "FSIM-SDF-SCHEMA-005");

    limits = { };
    limits.max_headers = 1U;
    const auto encode_limit = fsim::app::encode_sdf_schema(fixture.file,
        *fixture.summary, fixture.source_text, options(), limits);
    require(!encode_limit.ok() && encode_limit.bytes.empty(),
        "schema header limit must publish no partial bytes");
    require(std::ranges::any_of(encode_limit.diagnostics,
                [](const fsim::frontend::Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SDF-SCHEMA-005";
                }),
        "schema encode resource failure must retain its diagnostic");
}
} // namespace

int main()
{
    try {
        test_roundtrip_metadata();
        test_revision_and_deterministic_envelopes();
        test_corruption_staleness_and_resources();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
