// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/sdf_portable_archive.hpp"

#include "fsim/support/sha256.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

std::string checksum(const std::string_view value)
{
    return fsim::support::Sha256::hex(fsim::support::Sha256::digest(value));
}

template <typename Result>
void require_diagnostic(const Result& result, const std::string_view code)
{
    require(std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) { return diagnostic.code == code; }),
        "missing expected SDF artifact diagnostic");
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
    require(design.has_value(), "artifact identity design must be valid");
    return std::move(*design);
}

std::string source(const std::string_view revision, const unsigned delay)
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
      "    (DELAY (ABSOLUTE (IOPATH A Z ("
        + std::to_string(delay) + ")))))\n"
                                  ")";
}

struct Fixture {
    fsim::frontend::SdfFile file;
    std::shared_ptr<const fsim::app::SdfAnnotationSummary> summary;
    std::shared_ptr<const fsim::app::SdfSchemaSnapshot> schema;
    std::vector<std::byte> envelope;
};

Fixture make_fixture(const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view path, const std::string_view revision,
    const unsigned delay = 1U)
{
    using namespace fsim::app;
    Fixture result;
    const auto text = source(revision, delay);
    auto parsed = fsim::frontend::parse_sdf({ std::string { path }, text });
    require(parsed.ok(), "artifact identity SDF must parse");
    SdfAnnotationScopeRequest request;
    request.selection = SdfScopeSelection::All;
    request.expected_project_identity = "project:artifact";
    request.expected_design_identity = "design:artifact";
    const auto scope = bind_sdf_annotation_scope(parsed.file, design,
        "project:artifact", "design:artifact", request);
    require(scope.ok(), "artifact identity scope must bind");
    const auto cells = resolve_sdf_cells(scope.scope, design);
    require(cells.ok(), "artifact identity cells must resolve");
    const auto endpoints = resolve_sdf_endpoints(cells.resolution, design);
    require(endpoints.ok(), "artifact identity endpoints must resolve");
    const auto mapping = validate_sdf_mapping(endpoints.resolution, design);
    require(mapping.ok(), "artifact identity mapping must validate");
    const SdfSchemaOptions options { "sdf-parse-options-v1",
        "sdf-normalization-options-v1", "fsim-compiler-compat-v1" };
    const auto encoded
        = encode_sdf_schema(parsed.file, *mapping.summary, text, options);
    require(encoded.ok(), "artifact identity schema must encode");
    const auto decoded = decode_sdf_schema(encoded.bytes,
        "fsim-compiler-compat-v1", mapping.summary->semantic_identity());
    require(decoded.ok(), "artifact identity schema must decode");
    result.file = std::move(parsed.file);
    result.summary = mapping.summary;
    result.schema = decoded.snapshot;
    result.envelope = encoded.bytes;
    return result;
}

fsim::artifact::DesignMetadata base_metadata()
{
    fsim::artifact::DesignMetadata metadata;
    const auto coverage_identity
        = fsim::artifact::make_code_coverage_artifact_identity(false).identity;
    metadata.producer = "fsim SDF artifact test";
    metadata.time_resolution = "1ns";
    metadata.delay_mode = "typ";
    metadata.optimization = "O2";
    metadata.cache_key = checksum("base-design-cache");
    metadata.code_coverage = coverage_identity;
    metadata.roots.push_back({ "top", "sv:work.top", "sv:work.top" });
    metadata.objects.push_back({ checksum("object-metadata"),
        checksum("object-compilation"), "systemverilog", "2017", "none",
        "work", coverage_identity, { }, { checksum("unit") } });
    metadata.verilog_unit_provenance.push_back(
        { 0U, "systemverilog", "2017", "none" });
    metadata.payloads = {
        { "runtime", "state/runtime.bin", checksum("runtime") },
        { "semantics", "state/semantics.bin", checksum("semantics") },
        { "design-ir", "state/design-ir.bin", checksum("design-ir") }
    };
    metadata.specialization_cache_keys = { checksum("specialization") };
    metadata.unit_count = 1U;
    metadata.semantic_source_count = 1U;
    metadata.specialization_count = 1U;
    metadata.signal_count = 2U;
    metadata.process_count = 1U;
    metadata.design_digest = fsim::artifact::compute_design_digest(metadata);
    return metadata;
}

fsim::artifact::DesignSdfAnnotation annotation(
    const Fixture& fixture, const std::string& design_digest,
    const fsim::app::SdfDelaySelectionPolicy selection
    = fsim::app::SdfDelaySelectionPolicy::Typical)
{
    const auto result = fsim::app::build_sdf_artifact_identity(*fixture.schema,
        *fixture.summary, design_digest, selection);
    require(result.ok(), "SDF artifact identity must build");
    return *result.annotation;
}

std::string byte_string(const std::span<const std::byte> bytes)
{
    return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
}

void make_tree_writable(const std::filesystem::path& root)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
        !error && iterator != end; iterator.increment(error)) {
        std::filesystem::permissions(iterator->path(),
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, error);
        error.clear();
    }
    std::filesystem::permissions(root, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, error);
}

std::filesystem::path unique_path(const std::string_view suffix)
{
    return std::filesystem::temp_directory_path()
        / ("fsim-sdf-portable-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count())
            + '-' + std::string { suffix });
}

void test_path_independent_identity_and_invalidation()
{
    const auto design = make_design();
    const auto metadata = base_metadata();
    const auto first = make_fixture(design, "/producer/a/timing.sdf", "4.0");
    const auto relocated
        = make_fixture(design, "/relocated/b/timing.sdf", "4.0");
    const auto base = annotation(first, metadata.design_digest);
    const auto moved = annotation(relocated, metadata.design_digest);
    require(first.envelope != relocated.envelope
            && base.cache_key == moved.cache_key && base == moved,
        "portable SDF identity must exclude source paths while the coordinate envelope retains them");

    const auto source_edit = annotation(
        make_fixture(design, "timing.sdf", "4.0", 2U), metadata.design_digest);
    const auto revision_edit = annotation(
        make_fixture(design, "timing.sdf", "2.1"), metadata.design_digest);
    const auto option_edit = annotation(first, metadata.design_digest,
        fsim::app::SdfDelaySelectionPolicy::Maximum);
    const auto hierarchy_edit = annotation(first, checksum("other-design"));
    auto root_edit = base;
    root_edit.selected_root_identities.front() = "sv:work.other_top";
    root_edit.cache_key
        = fsim::app::compute_sdf_artifact_cache_key(root_edit);
    require(source_edit.cache_key != base.cache_key
            && revision_edit.cache_key != base.cache_key
            && option_edit.cache_key != base.cache_key
            && hierarchy_edit.cache_key != base.cache_key
            && root_edit.cache_key != base.cache_key,
        "source, revision, option, hierarchy, and selected-root changes must deterministically invalidate SDF cache identity");
}

void test_design_roundtrip_cache_composition_and_negatives()
{
    const auto design = make_design();
    const auto fixture = make_fixture(design, "timing.sdf", "3.0");
    const auto metadata = base_metadata();
    const auto identity = annotation(fixture, metadata.design_digest);
    const auto applied
        = fsim::app::apply_sdf_artifact_identity(metadata, identity);
    require(applied.ok() && applied.metadata->cache_key != metadata.cache_key
            && applied.metadata->specialization_cache_keys
                != metadata.specialization_cache_keys
            && applied.metadata->design_digest != metadata.design_digest,
        "SDF identity must enter design, specialization, and artifact digests");
    const auto bytes
        = fsim::artifact::serialize_design_metadata(*applied.metadata);
    fsim::diagnostic::Engine diagnostics;
    const auto decoded = fsim::artifact::deserialize_design_metadata(
        bytes, "sdf-design", diagnostics);
    require(decoded == applied.metadata && !diagnostics.has_error(),
        "portable design metadata must round-trip exact SDF identity");

    const auto duplicate
        = fsim::app::apply_sdf_artifact_identity(*applied.metadata, identity);
    require(!duplicate.ok(), "duplicate SDF identity must reject");
    require_diagnostic(duplicate, "FSIM-SDF-ARTIFACT-002");
    auto stale = identity;
    stale.source_digest = checksum("stale-source");
    const auto stale_result
        = fsim::app::apply_sdf_artifact_identity(metadata, stale);
    require(!stale_result.ok(), "stale cache identity must reject");
    require_diagnostic(stale_result, "FSIM-SDF-ARTIFACT-001");

    fsim::app::SdfArtifactIdentityLimits limits;
    limits.max_semantic_objects = 0U;
    const auto exhausted = fsim::app::build_sdf_artifact_identity(
        *fixture.schema, *fixture.summary, metadata.design_digest,
        fsim::app::SdfDelaySelectionPolicy::Typical, limits);
    require(!exhausted.ok(), "SDF artifact identity resource limit must reject");
    require_diagnostic(exhausted, "FSIM-SDF-ARTIFACT-003");
}

void test_library_design_relocation_and_source_hidden_archive()
{
    const auto design = make_design();
    const auto fixture = make_fixture(
        design, "/producer/hidden/timing.sdf", "4.0");
    const auto metadata = base_metadata();
    const auto identity = annotation(fixture, metadata.design_digest);
    const auto encoded = fsim::app::encode_sdf_portable_archive(*fixture.schema,
        *fixture.summary, identity, fixture.envelope);
    require(encoded.ok() && !encoded.snapshot->normalized().empty()
            && !encoded.snapshot->mappings().empty(),
        "portable SDF archive must retain normalized and resolved records");
    const auto decoded = fsim::app::decode_sdf_portable_archive(
        encoded.bytes, identity);
    require(decoded.ok()
            && std::ranges::equal(decoded.snapshot->normalized(),
                encoded.snapshot->normalized())
            && std::ranges::equal(decoded.snapshot->mappings(),
                encoded.snapshot->mappings()),
        "portable SDF archive must round-trip exact normalized and resolved state");

    fsim::library::Metadata library_metadata;
    library_metadata.library = "vendor";
    library_metadata.producer = "fsim SDF portable test";
    library_metadata.runtime_schema = 1U;
    library_metadata.standards = { { "sdf", identity.revision } };
    library_metadata.units.push_back(fsim::app::make_sdf_library_index_entry(
        identity, "sdf/annotation.bin", encoded.bytes));
    const auto original = unique_path("original.fsimlib");
    const auto relocated = unique_path("relocated.fsimlib");
    fsim::diagnostic::Engine publish_diagnostics;
    require(fsim::library::publish(original, library_metadata,
                { { "sdf/annotation.bin", byte_string(encoded.bytes) } },
                publish_diagnostics)
            && !publish_diagnostics.has_error(),
        "analyzed library must publish its SDF archive");
    std::error_code rename_error;
    std::filesystem::rename(original, relocated, rename_error);
    require(!rename_error, "read-only mapped SDF library must relocate");
    fsim::diagnostic::Engine load_diagnostics;
    const auto loaded_metadata
        = fsim::library::load_metadata(relocated, "vendor", load_diagnostics);
    require(loaded_metadata.has_value() && !load_diagnostics.has_error(),
        "relocated mapped library metadata must load");
    const auto loaded = fsim::app::load_sdf_library_archive(
        relocated, *loaded_metadata, identity);
    require(loaded.ok() && loaded.snapshot->annotation() == identity,
        "mapped library must consume archived SDF without producer source");

    const auto applied
        = fsim::app::apply_sdf_artifact_identity(metadata, identity);
    require(applied.ok(), "design metadata must accept portable SDF identity");
    auto design_metadata = *applied.metadata;
    design_metadata.payloads.push_back(fsim::app::make_sdf_design_payload(
        identity, "sdf/annotation.bin", encoded.bytes));
    design_metadata.design_digest
        = fsim::artifact::compute_design_digest(design_metadata);
    const auto design_path = unique_path("design.fsimdesign");
    const std::vector<fsim::library::PortablePayload> payloads {
        { "state/runtime.bin", "runtime" },
        { "state/semantics.bin", "semantics" },
        { "state/design-ir.bin", "design-ir" },
        { "sdf/annotation.bin", byte_string(encoded.bytes) }
    };
    fsim::diagnostic::Engine design_publish_diagnostics;
    require(fsim::artifact::publish_design(design_path, design_metadata,
                payloads, design_publish_diagnostics)
            && !design_publish_diagnostics.has_error(),
        "standalone design must publish its indexed SDF archive");
    fsim::diagnostic::Engine design_load_diagnostics;
    const auto loaded_design = fsim::artifact::load_design_metadata(
        design_path, design_load_diagnostics);
    require(loaded_design == design_metadata
            && !design_load_diagnostics.has_error(),
        "standalone design must reload exact SDF payload identity");

    auto corrupt = encoded.bytes;
    corrupt[20] ^= std::byte { 1U };
    const auto corrupt_result
        = fsim::app::decode_sdf_portable_archive(corrupt, identity);
    require(!corrupt_result.ok(), "corrupt portable SDF archive must reject");
    require_diagnostic(corrupt_result, "FSIM-SDF-PORTABLE-002");
    auto stale_identity = identity;
    stale_identity.design_digest = checksum("incompatible-design");
    stale_identity.cache_key
        = fsim::app::compute_sdf_artifact_cache_key(stale_identity);
    const auto stale_result = fsim::app::decode_sdf_portable_archive(
        encoded.bytes, stale_identity);
    require(!stale_result.ok(), "incompatible design/SDF archive must reject");
    require_diagnostic(stale_result, "FSIM-SDF-PORTABLE-003");
    fsim::app::SdfPortableArchiveLimits limits;
    limits.max_mapping_records = 0U;
    const auto exhausted = fsim::app::encode_sdf_portable_archive(*fixture.schema,
        *fixture.summary, identity, fixture.envelope, limits);
    require(!exhausted.ok(), "portable SDF archive resource limit must reject");
    require_diagnostic(exhausted, "FSIM-SDF-PORTABLE-004");
    limits = { };
    limits.max_bytes = encoded.bytes.size() - 1U;
    const auto library_exhausted = fsim::app::load_sdf_library_archive(
        relocated, *loaded_metadata, identity, limits);
    require(!library_exhausted.ok(),
        "mapped-library SDF archive byte limit must reject");
    require_diagnostic(library_exhausted, "FSIM-SDF-PORTABLE-004");

    make_tree_writable(relocated);
    make_tree_writable(design_path);
    std::error_code cleanup_error;
    std::filesystem::remove_all(relocated, cleanup_error);
    require(!cleanup_error, "relocated SDF library cleanup must succeed");
    std::filesystem::remove_all(design_path, cleanup_error);
    require(!cleanup_error, "SDF design cleanup must succeed");
}
} // namespace

int main()
{
    try {
        test_path_independent_identity_and_invalidation();
        test_design_roundtrip_cache_composition_and_negatives();
        test_library_design_relocation_and_source_hidden_archive();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
