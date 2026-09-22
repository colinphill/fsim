// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "fsim/frontend/coverage_persistence.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_design_artifact_codec_internal.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

template <typename Owner>
concept HasOwningParsedDesignMember = requires(Owner& owner) {
    owner.parsed;
};

template <typename Owner>
concept HasFrontendClassSpecializations = requires(Owner& owner) {
    owner.systemverilog_class_specializations;
};

template <typename Owner>
concept HasCovergroupConstructorActuals = requires(Owner& owner) {
    owner.constructor_actuals;
};

template <typename Owner>
concept HasCovergroupSampleCalls = requires(Owner& owner) {
    owner.sample_calls;
};

using FrontendCoverageInstances = decltype(
    fsim::frontend::SystemVerilogCoverageState::instances);
using RuntimeCoverageInstances = decltype(
    fsim::runtime::SystemVerilogCoverageState::instances);

template <typename Design>
concept HasSingularPublicElaborator = requires(const Design& design) {
    {
        fsim::elaboration::elaborate(design, std::string_view { })
    } -> std::same_as<fsim::elaboration::ElaborationResult>;
};

template <typename Design>
concept HasMultipleRootPublicElaborator = requires(const Design& design) {
    {
        fsim::elaboration::elaborate(
            design,
            std::span<const fsim::elaboration::Root> { },
            std::span<const fsim::elaboration::Binding> { },
            std::span<
                const fsim::elaboration::SystemCInstanceDescription> { },
            nullptr,
            std::span<const std::string> { })
    } -> std::same_as<fsim::elaboration::ElaborationResult>;
};

static_assert(std::derived_from<
    fsim::app::CheckedProject, fsim::semantic::CompiledDesign>);
static_assert(!HasOwningParsedDesignMember<fsim::app::CheckedProject>);
static_assert(!HasOwningParsedDesignMember<fsim::app::BuiltProject>);
static_assert(!HasOwningParsedDesignMember<fsim::semantic::CompiledDesign>);
static_assert(!HasFrontendClassSpecializations<fsim::app::CheckedProject>);
static_assert(!HasFrontendClassSpecializations<fsim::app::BuiltProject>);
static_assert(!HasCovergroupConstructorActuals<
    fsim::frontend::SystemVerilogCovergroupInstance>);
static_assert(!HasCovergroupSampleCalls<
    fsim::frontend::SystemVerilogCovergroupInstance>);
static_assert(std::same_as<
    FrontendCoverageInstances::value_type,
    fsim::frontend::SystemVerilogCovergroupInstance>);
static_assert(std::same_as<
    RuntimeCoverageInstances::value_type,
    fsim::semantic::sv::CovergroupInstance>);
static_assert(HasSingularPublicElaborator<fsim::semantic::CompiledDesign>);
static_assert(HasMultipleRootPublicElaborator<
    fsim::semantic::CompiledDesign>);
static_assert(!HasSingularPublicElaborator<fsim::frontend::ParsedDesign>);
static_assert(!HasMultipleRootPublicElaborator<
    fsim::frontend::ParsedDesign>);

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write_file(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output { path, std::ios::binary | std::ios::trunc };
    output << contents;
    assert(output.good());
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input { path, std::ios::binary };
    assert(input.good());
    return {
        std::istreambuf_iterator<char> { input },
        std::istreambuf_iterator<char> { }
    };
}

void make_tree_writable(const std::filesystem::path& root)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator {
             root, error }, end;
         !error && iterator != end; iterator.increment(error)) {
        std::filesystem::permissions(
            iterator->path(), std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, error);
        error.clear();
    }
    std::filesystem::permissions(
        root, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, error);
}

void copy_artifact_tree(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination);
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator { source }) {
        const auto target = destination
            / entry.path().lexically_relative(source);
        if (entry.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (entry.is_regular_file()) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.path(), target);
        }
    }
    make_tree_writable(destination);
}

std::string payload_checksum(const std::string_view bytes)
{
    return fsim::support::Sha256::hex(
        fsim::support::Sha256::digest(bytes));
}

void store_u32(
    std::string& bytes, std::size_t offset, std::uint32_t value);

enum class CompiledHirCorruption {
    checksum_mismatch,
    stale_schema,
    corrupt_magic,
};

struct CompiledHirCorruptionCase {
    CompiledHirCorruption corruption;
    std::string_view name;
};

constexpr std::array compiled_hir_corruption_cases {
    CompiledHirCorruptionCase {
        CompiledHirCorruption::checksum_mismatch, "checksum-mismatch" },
    CompiledHirCorruptionCase {
        CompiledHirCorruption::stale_schema, "stale-schema" },
    CompiledHirCorruptionCase {
        CompiledHirCorruption::corrupt_magic, "corrupt-magic" },
};

void mutate_compiled_hir_payload(
    std::string& payload, const CompiledHirCorruption corruption)
{
    assert(payload.size() > 12U);
    switch (corruption) {
    case CompiledHirCorruption::checksum_mismatch:
        payload.push_back('\0');
        break;
    case CompiledHirCorruption::stale_schema:
        store_u32(payload, 8U, 0U);
        break;
    case CompiledHirCorruption::corrupt_magic:
        payload.front() = 'X';
        break;
    }
}

void make_corrupt_object_copy(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const CompiledHirCorruption corruption)
{
    copy_artifact_tree(source, destination);
    fsim::diagnostic::Engine diagnostics;
    auto metadata = fsim::artifact::load_object_metadata(
        destination, diagnostics);
    assert(metadata && !diagnostics.has_error());
    const auto payload_path = destination / metadata->compiled_hir_artifact;
    auto payload = read_file(payload_path);
    mutate_compiled_hir_payload(payload, corruption);
    write_file(payload_path, payload);
    if (corruption == CompiledHirCorruption::checksum_mismatch) {
        return;
    }
    metadata->compiled_hir_checksum = payload_checksum(payload);
    metadata->compilation_digest
        = fsim::artifact::compute_object_compilation_digest(*metadata);
    write_file(
        destination / fsim::artifact::kObjectMetadataFilename,
        fsim::artifact::serialize_object_metadata(*metadata));
}

void make_corrupt_library_copy(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const CompiledHirCorruption corruption)
{
    copy_artifact_tree(source, destination);
    fsim::diagnostic::Engine diagnostics;
    auto metadata = fsim::library::load_metadata(
        destination, "work", diagnostics);
    assert(metadata && !diagnostics.has_error());
    const auto payload_path = destination / metadata->compiled_hir_artifact;
    auto payload = read_file(payload_path);
    mutate_compiled_hir_payload(payload, corruption);
    write_file(payload_path, payload);
    if (corruption == CompiledHirCorruption::checksum_mismatch) {
        return;
    }
    metadata->compiled_hir_checksum = payload_checksum(payload);
    write_file(
        destination / fsim::library::kMetadataFilename,
        fsim::library::serialize_metadata(*metadata));
}

void store_u32(
    std::string& bytes, const std::size_t offset, const std::uint32_t value)
{
    assert(offset + sizeof(value) <= bytes.size());
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes[offset + byte] = static_cast<char>(
            (value >> (byte * 8U)) & 0xffU);
    }
}

void store_u64(
    std::string& bytes, const std::size_t offset, const std::uint64_t value)
{
    assert(offset + sizeof(value) <= bytes.size());
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes[offset + byte] = static_cast<char>(
            (value >> (byte * 8U)) & 0xffU);
    }
}

using ArtifactSnapshot
    = std::vector<std::pair<std::string, std::string>>;

ArtifactSnapshot capture_artifact(
    const std::filesystem::path& directory)
{
    ArtifactSnapshot snapshot;
    for (const auto& entry : std::filesystem::recursive_directory_iterator { directory }) {
        if (!entry.is_regular_file()) {
            continue;
        }
        snapshot.emplace_back(
            entry.path().lexically_relative(directory).generic_string(),
            read_file(entry.path()));
    }
    std::ranges::sort(snapshot, { }, [](const auto& entry) {
        return entry.first;
    });
    return snapshot;
}

void assert_compiled_hir_codec_rejections(
    const std::string_view valid_bundle)
{
    assert(valid_bundle.size() > 12U);
    const auto assert_rejected = [](const std::string_view bytes,
                                     const std::string_view description,
                                     const std::string_view message = { }) {
        fsim::diagnostic::Engine diagnostics;
        assert(!fsim::app::deserialize_compiled_hir_bundle(
            bytes, std::string { description }, diagnostics));
        assert(std::ranges::any_of(
            diagnostics.diagnostics(), [&](const auto& diagnostic) {
                return diagnostic.code == "FSIM-ART-0013"
                    && (message.empty()
                        || diagnostic.message == message);
            }));
    };

    for (const std::uint32_t schema : { 0U, 2U }) {
        auto incompatible = std::string { valid_bundle };
        store_u32(incompatible, 8U, schema);
        assert_rejected(
            incompatible,
            schema == 0U ? "stale compiled-HIR bundle"
                         : "future compiled-HIR bundle",
            "unsupported design state FSIMCHIR identity: found schema "
                + std::to_string(schema)
                + "; required schema 1; regenerate compiled-HIR bundle "
                  "with this fsim build");
    }

    auto corrupt_magic = std::string { valid_bundle };
    corrupt_magic.front() = 'X';
    assert_rejected(corrupt_magic, "corrupt compiled-HIR magic");
    assert_rejected(
        valid_bundle.substr(0U, 11U), "truncated compiled-HIR envelope");
    auto trailing = std::string { valid_bundle };
    trailing.push_back('\0');
    assert_rejected(trailing, "compiled-HIR trailing byte");

    using Variant = std::variant<std::uint32_t, std::uint64_t>;
    fsim::diagnostic::Engine encode_diagnostics;
    auto invalid_variant = fsim::app::codec_detail::serialize(
        "FSIMVAR1", 1U, Variant { std::uint32_t { } },
        encode_diagnostics);
    assert(invalid_variant && !encode_diagnostics.has_error());
    store_u64(*invalid_variant, 12U, 2U);
    fsim::diagnostic::Engine variant_diagnostics;
    assert(!fsim::app::codec_detail::deserialize<Variant>(
        "FSIMVAR1", 1U, *invalid_variant, "invalid variant",
        variant_diagnostics));
    assert(std::ranges::any_of(
        variant_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0013"
                && diagnostic.message
                    == "design state contains an invalid variant alternative";
        }));
}

std::vector<std::string> semantic_expansion_stack(
    const fsim::semantic::Model& semantics,
    const fsim::semantic::SourceSpanId source)
{
    assert(source.valid() && source.value() < semantics.source_spans().size());
    auto expansion = semantics.source_spans()[source.value()].expansion;
    std::vector<std::string> innermost_to_outermost;
    while (expansion) {
        assert(expansion->valid()
            && expansion->value() < semantics.expansions().size());
        const auto& record = semantics.expansions()[expansion->value()];
        innermost_to_outermost.push_back(record.description);
        expansion = record.parent;
    }
    std::ranges::reverse(innermost_to_outermost);
    return innermost_to_outermost;
}

void assert_projected_source_token(
    const fsim::semantic::sv::SourceToken& source,
    const fsim::frontend::Token& projected,
    const fsim::semantic::Model& semantics)
{
    assert(source.source.valid()
        && source.source.value() < semantics.source_spans().size());
    const auto& semantic_span
        = semantics.source_spans()[source.source.value()];
    assert(semantic_span.file.valid()
        && semantic_span.file.value() < semantics.source_files().size());
    const auto& semantic_file
        = semantics.source_files()[semantic_span.file.value()];
    const auto logical_name = semantic_span.logical_name.empty()
        ? semantic_file.physical_name
        : semantic_span.logical_name;
    const auto expansions = semantic_expansion_stack(
        semantics, source.source);
    assert(static_cast<std::uint16_t>(projected.kind) == source.kind);
    assert(projected.text == source.text);
    assert(static_cast<std::uint8_t>(projected.generated_text)
        == static_cast<std::uint8_t>(source.generated_text));
    assert(projected.span.source_name.str() == logical_name);
    assert(projected.span.physical_source_name.str()
        == semantic_file.physical_name);
    assert(projected.span.begin.offset == semantic_span.begin.offset);
    assert(projected.span.begin.line == semantic_span.begin.line);
    assert(projected.span.begin.column == semantic_span.begin.column);
    assert(projected.span.end.offset == semantic_span.end.offset);
    assert(projected.span.end.line == semantic_span.end.line);
    assert(projected.span.end.column == semantic_span.end.column);
    assert(std::ranges::equal(
        projected.span.expansion_stack, expansions));
    assert(projected.expansion_stack == expansions);
}

void assert_included_header_auxiliary_records(
    const fsim::semantic::CompiledDesign& design)
{
    const auto assert_relocated_file_name = [](const std::string_view value) {
        assert(value.find("/producer-only/logical/included_auxiliary.svh")
            == std::string_view::npos);
        assert(std::filesystem::path { value }.filename()
            == "included_auxiliary.svh");
    };
    const auto source_is_header = [&](const fsim::semantic::SourceSpanId id) {
        assert(id.valid() && id.value() < design.semantics.source_spans().size());
        const auto& span = design.semantics.source_spans()[id.value()];
        return std::filesystem::path { span.logical_name }.filename()
            == "included_auxiliary.svh";
    };
    const auto owner_unit = [&](const fsim::semantic::ScopeId scope) {
        assert(scope.valid() && scope.value() < design.semantics.scopes().size());
        const auto unit = design.semantics.scopes()[scope.value()].unit;
        assert(unit.valid() && unit.value() < design.semantics.units().size());
        return &design.semantics.units()[unit.value()];
    };

    const auto dpi = std::ranges::find(
        design.systemverilog_hir.dpi_declarations(),
        std::string { "included_header_dpi" },
        &fsim::semantic::sv::DpiDeclaration::systemverilog_name);
    assert(dpi != design.systemverilog_hir.dpi_declarations().end());
    assert(dpi->owner_kind
        == fsim::semantic::sv::DpiOwnerKind::compilation_unit);
    assert(source_is_header(dpi->source));
    const auto* dpi_owner = owner_unit(dpi->owner_scope);
    assert(dpi_owner->kind
        == fsim::semantic::UnitKind::systemverilog_compilation_unit);
    assert(dpi_owner->library == "work");

    const auto included_package = std::ranges::find(
        design.systemverilog_hir.units(),
        std::string { "included_header_package" },
        &fsim::semantic::sv::Unit::name);
    assert(included_package != design.systemverilog_hir.units().end());
    assert(included_package->coverage.size() == 1U);
    const auto& included_group = included_package->coverage.front();
    assert(included_group.name == "included_group");
    assert(included_group.formals.size() == 1U);
    const auto macro_type_token = std::ranges::find(
        included_group.formals.front().type_tokens,
        std::string { "int" },
        &fsim::semantic::sv::SourceToken::text);
    assert(macro_type_token
        != included_group.formals.front().type_tokens.end());
    assert(source_is_header(macro_type_token->source));
    const auto& macro_source
        = design.semantics.source_spans()[macro_type_token->source.value()];
    assert(macro_source.logical_name.find(
               "/producer-only/logical/included_auxiliary.svh")
        == std::string::npos);
    const auto expansions = semantic_expansion_stack(
        design.semantics, macro_type_token->source);
    assert(std::ranges::any_of(expansions, [](const auto& expansion) {
        return expansion.find("included '") != std::string::npos;
    }));
    assert(std::ranges::any_of(expansions, [](const auto& expansion) {
        return expansion.find("macro `INCLUDED_FORMAL_TYPE'")
            != std::string::npos;
    }));
    const auto projected_group
        = fsim::app::application_detail::
            project_systemverilog_covergroup_declaration(
                included_group, design.semantics);
    const auto type_token_offset = static_cast<std::size_t>(
        std::distance(
            included_group.formals.front().type_tokens.begin(),
            macro_type_token));
    assert(projected_group.formals.size() == 1U);
    assert(type_token_offset
        < projected_group.formals.front().type_tokens.size());
    assert_projected_source_token(
        *macro_type_token,
        projected_group.formals.front().type_tokens[type_token_offset],
        design.semantics);

    const auto file_parameter = std::ranges::find(
        design.systemverilog_hir.declarations(),
        std::string { "included_file_parameter" },
        &fsim::semantic::sv::Declaration::name);
    assert(file_parameter != design.systemverilog_hir.declarations().end());
    assert(file_parameter->initializer);
    const auto file_expression = std::ranges::find(
        design.systemverilog_hir.expressions(),
        *file_parameter->initializer,
        &fsim::semantic::sv::Expression::id);
    assert(file_expression != design.systemverilog_hir.expressions().end());
    assert(file_expression->generated_text
        == fsim::semantic::sv::GeneratedTextKind::systemverilog_file_macro);
    assert(file_expression->decoded_string);
    assert_relocated_file_name(*file_expression->decoded_string);

    const auto included_owner = std::ranges::find(
        design.systemverilog_hir.units(),
        std::string { "included_header_owner" },
        &fsim::semantic::sv::Unit::name);
    assert(included_owner != design.systemverilog_hir.units().end());
    const auto file_assertion = std::ranges::find(
        included_owner->concurrent_assertions,
        std::string { "included_file_assertion" },
        &fsim::semantic::sv::ConcurrentAssertion::name);
    assert(file_assertion != included_owner->concurrent_assertions.end());
    const auto file_action_token = std::ranges::find(
        file_assertion->failure_action_tokens,
        fsim::semantic::sv::GeneratedTextKind::systemverilog_file_macro,
        &fsim::semantic::sv::SourceToken::generated_text);
    assert(file_action_token != file_assertion->failure_action_tokens.end());
    const auto decoded_action = fsim::frontend::
        decode_systemverilog_string_literal(file_action_token->text);
    assert(decoded_action);
    assert_relocated_file_name(*decoded_action);

    const auto file_output = std::ranges::find(
        design.systemverilog_hir.statements(),
        fsim::semantic::sv::GeneratedTextKind::systemverilog_file_macro,
        &fsim::semantic::sv::Statement::output_generated_text);
    assert(file_output != design.systemverilog_hir.statements().end());
    assert_relocated_file_name(file_output->output_text);

    const auto& covergroups
        = design.systemverilog_hir.covergroup_instances();
    const auto module_instance = std::ranges::find(
        covergroups, std::string { "included_coverage" },
        &fsim::semantic::sv::CovergroupInstance::name);
    const auto class_instance = std::ranges::find(
        covergroups, std::string { "included_class_group" },
        &fsim::semantic::sv::CovergroupInstance::name);
    assert(module_instance != covergroups.end());
    assert(class_instance != covergroups.end());
    assert(!module_instance->class_member_template);
    assert(class_instance->class_member_template);
    assert(source_is_header(module_instance->source));
    assert(source_is_header(class_instance->source));
    assert(owner_unit(module_instance->owner_scope)->library == "work");
    assert(owner_unit(class_instance->owner_scope)->library == "work");
}

void assert_included_header_auxiliary_bundle(
    const std::string_view bytes,
    const std::string_view description)
{
    fsim::diagnostic::Engine diagnostics;
    const auto decoded = fsim::app::deserialize_compiled_hir_bundle(
        bytes, std::string { description }, diagnostics);
    assert(decoded && decoded->valid() && !diagnostics.has_error());
    assert_included_header_auxiliary_records(*decoded);
}

void assert_compiled_hir_bundle(
    const std::filesystem::path& path,
    const std::string_view description)
{
    const auto bytes = read_file(path);
    assert(bytes.starts_with("FSIMCHIR"));
    fsim::diagnostic::Engine diagnostics;
    const auto decoded = fsim::app::deserialize_compiled_hir_bundle(
        bytes, std::string { description }, diagnostics);
    assert(decoded && decoded->valid() && !diagnostics.has_error());
}

void assert_compiled_hir_only_unit_index(
    const std::span<const fsim::library::UnitIndexEntry> units)
{
    assert(!units.empty());
    assert(std::ranges::all_of(units, [](const auto& unit) {
        return unit.artifact.empty() && unit.checksum.empty();
    }));
}

void assert_compiled_hir_only_unit_payloads(
    const std::filesystem::path& directory)
{
    std::size_t compiled_hir_payloads { };
    for (const auto& entry : std::filesystem::recursive_directory_iterator { directory }) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto extension = entry.path().extension();
        assert(extension != ".fsimir");
        assert(extension != ".fsimudp");
        assert(extension != ".fsimclass");
        if (extension == ".fsimhir") {
            ++compiled_hir_payloads;
        }
    }
    assert(compiled_hir_payloads == 1U);
}

void assert_parser_independent_object(
    const std::filesystem::path& directory,
    const bool expect_included_auxiliary = false)
{
    fsim::diagnostic::Engine diagnostics;
    const auto metadata = fsim::artifact::load_object_metadata(
        directory, diagnostics);
    assert(metadata && !diagnostics.has_error());
    assert(metadata->format == fsim::artifact::kObjectFormatVersion);
    assert(metadata->portable_schema
        == fsim::library::kPortableSchemaVersion);
    assert(metadata->compiled_hir_schema
        == fsim::library::kCompiledHirSchemaVersion);
    assert(metadata->compiled_hir_artifact.extension() == ".fsimhir");
    assert(!metadata->compiled_hir_checksum.empty());
    assert_compiled_hir_only_unit_index(metadata->units);
    assert_compiled_hir_only_unit_payloads(directory);
    assert_compiled_hir_bundle(
        directory / metadata->compiled_hir_artifact,
        "compiled-HIR object payload");
    if (expect_included_auxiliary) {
        assert_included_header_auxiliary_bundle(
            read_file(directory / metadata->compiled_hir_artifact),
            "included-header object payload");
    }
}

void assert_parser_independent_library(
    const std::filesystem::path& directory,
    const std::string_view logical_library,
    const bool expect_included_auxiliary = false)
{
    fsim::diagnostic::Engine diagnostics;
    const auto metadata = fsim::library::load_metadata(
        directory, logical_library, diagnostics);
    assert(metadata && !diagnostics.has_error());
    assert(metadata->format == fsim::library::kFormatVersion);
    assert(metadata->portable_schema
        == fsim::library::kPortableSchemaVersion);
    assert(metadata->compiled_hir_schema
        == fsim::library::kCompiledHirSchemaVersion);
    assert(metadata->compiled_hir_artifact.extension() == ".fsimhir");
    assert(!metadata->compiled_hir_checksum.empty());
    assert_compiled_hir_only_unit_index(metadata->units);
    assert_compiled_hir_only_unit_payloads(directory);
    assert_compiled_hir_bundle(
        directory / metadata->compiled_hir_artifact,
        "compiled-HIR mapped-library payload");
    if (expect_included_auxiliary) {
        assert_included_header_auxiliary_bundle(
            read_file(directory / metadata->compiled_hir_artifact),
            "included-header mapped-library payload");
    }
}

fsim::project::Config mixed_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& cache,
    const std::uint32_t jobs)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "compiled-hir-cache";
    config.project.tops = {
        { "sv:work.sv_top", "sv_root" },
        { "vhdl:work.vhdl_top(rtl)", "vhdl_root" }
    };
    config.project.time_resolution = "1ns";
    config.build.cache_path = cache;
    config.build.jobs = jobs;
    config.run.max_deltas = 100;

    fsim::project::SourceSet systemverilog;
    systemverilog.language = fsim::project::Language::system_verilog;
    systemverilog.standard = "2017";
    systemverilog.library = "work";
    systemverilog.files = { directory / "top.sv" };
    config.source_sets.push_back(std::move(systemverilog));

    fsim::project::SourceSet vhdl;
    vhdl.language = fsim::project::Language::vhdl;
    vhdl.standard = "2008";
    vhdl.library = "work";
    vhdl.files = { directory / "top.vhd" };
    config.source_sets.push_back(std::move(vhdl));
    return config;
}

fsim::project::Config systemverilog_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& cache)
{
    auto config = mixed_config(directory, cache, 8);
    config.project.tops = { { "sv:work.sv_top", "dut" } };
    config.source_sets.resize(1);
    return config;
}

std::vector<std::byte> raw_cache_payload(
    const fsim::project::Config& config,
    const std::string_view key)
{
    fsim::compiler::ObjectCache cache { config.build.cache_path };
    std::error_code error;
    auto payload = cache.load(key, error);
    assert(payload && !error);
    return *payload;
}

std::vector<std::byte> cache_payload(
    const fsim::project::Config& config,
    const std::string_view key)
{
    auto payload = raw_cache_payload(config, key);
    constexpr std::string_view envelope
        = "FSIM-COMPILED-HIR-CACHE-V2\n";
    const auto header_size = envelope.size() + key.size() + 1U;
    assert(payload.size() >= header_size
        + std::string_view { "FSIMCHIR" }.size());
    const auto payload_view = std::string_view {
        reinterpret_cast<const char*>(payload.data()), payload.size()
    };
    assert(payload_view.starts_with(envelope));
    assert(payload_view.substr(envelope.size(), key.size()) == key);
    assert(payload_view[header_size - 1U] == '\n');
    const auto bundle = payload_view.substr(header_size);
    const auto prefix = bundle.substr(
        0U, std::string_view { "FSIMCHIR" }.size());
    assert(prefix == "FSIMCHIR");
    fsim::diagnostic::Engine diagnostics;
    const auto decoded = fsim::app::deserialize_compiled_hir_bundle(
        bundle, "ordinary cache", diagnostics);
    assert(decoded && !diagnostics.has_error());
    return std::vector<std::byte> {
        payload.begin() + static_cast<std::ptrdiff_t>(header_size),
        payload.end()
    };
}

bool has_diagnostic(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

bool has_diagnostic_message(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code,
    const std::string_view message)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.code == code
                && diagnostic.message.find(message) != std::string::npos;
        });
}

struct CompiledHirIdentity {
    fsim::semantic::Language language;
    fsim::semantic::UnitId unit;
    fsim::semantic::SourceSpanId unit_source;
    fsim::semantic::OriginId unit_origin;
    fsim::semantic::ProcessId process;
    fsim::semantic::SourceSpanId process_source;
    fsim::semantic::OriginId process_origin;
    std::size_t origin_count { };

    friend bool operator==(
        const CompiledHirIdentity&, const CompiledHirIdentity&) = default;
};

CompiledHirIdentity lifetime_identity(
    const fsim::semantic::CompiledDesign& compiled,
    const fsim::semantic::Language language)
{
    if (language == fsim::semantic::Language::vhdl) {
        const auto unit = std::ranges::find_if(
            compiled.vhdl_hir.units(),
            [](const fsim::semantic::vhdl::Unit& candidate) {
                return candidate.kind
                    == fsim::semantic::vhdl::UnitKind::architecture
                    && candidate.primary_name == "lifetime_vhdl"
                    && candidate.name == "rtl";
            });
        assert(unit != compiled.vhdl_hir.units().end());
        assert(unit->processes.size() == 1U);
        const auto process = std::ranges::find(
            compiled.vhdl_hir.processes(),
            unit->processes.front(),
            &fsim::semantic::vhdl::Process::id);
        assert(process != compiled.vhdl_hir.processes().end());
        return {
            language,
            unit->id,
            unit->source,
            unit->origin,
            process->id,
            process->source,
            process->origin,
            compiled.semantics.origins().size(),
        };
    }
    const auto unit = std::ranges::find(
        compiled.systemverilog_hir.units(),
        std::string { "lifetime" },
        &fsim::semantic::sv::Unit::name);
    assert(unit != compiled.systemverilog_hir.units().end());
    assert(unit->processes.size() == 1U);
    const auto process = std::ranges::find(
        compiled.systemverilog_hir.processes(),
        unit->processes.front(),
        &fsim::semantic::sv::Process::id);
    assert(process != compiled.systemverilog_hir.processes().end());
    return {
        language,
        unit->id,
        unit->source,
        unit->origin,
        process->id,
        process->source,
        process->origin,
        compiled.semantics.origins().size(),
    };
}

struct CompiledHirHandoff {
    std::string bytes;
    std::vector<fsim::library::SourceNameMapping> consumer_mappings;
    CompiledHirIdentity identity;
};

CompiledHirHandoff compile_lifetime_bundle(
    const fsim::project::Config& config,
    const fsim::semantic::Language language,
    fsim::diagnostic::Engine& diagnostics)
{
    auto checked = fsim::app::check_project(config, diagnostics);
    if (!checked) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(checked);
    const auto identity = lifetime_identity(*checked, language);
    const auto source_mappings
        = fsim::app::application_detail::compiled_cache_source_mappings(
            *checked, config.base_directory, diagnostics);
    assert(source_mappings);
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *checked, *source_mappings, diagnostics));
    auto bytes = fsim::app::serialize_compiled_hir_bundle(
        *checked, diagnostics);
    if (!bytes) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(bytes && !diagnostics.has_error());

    std::vector<fsim::library::SourceNameMapping> consumer_mappings;
    consumer_mappings.reserve(source_mappings->size());
    for (const auto& mapping : *source_mappings) {
        consumer_mappings.push_back(
            { mapping.logical_name, mapping.producer_name });
    }
    return {
        std::move(*bytes),
        std::move(consumer_mappings),
        identity,
    };
}

void run_compiled_hir_lifetime_test(
    const std::filesystem::path& directory)
{
    const auto source = directory / "lifetime.sv";
    write_file(source, R"(
module lifetime(output logic q);
  initial q = 1'b1;
endmodule
)");
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "compiled-hir-lifetime";
    config.project.tops = { { "sv:work.lifetime", "dut" } };
    config.build.cache_path = directory / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto handoff = compile_lifetime_bundle(
        config, fsim::semantic::Language::system_verilog, diagnostics);
    // The helper has returned, destroying check_project()'s compile-local AST
    // workspace and the public CheckedProject. Only owning compiled-HIR bytes
    // and scalar identity expectations cross this lifetime boundary.
    auto compiled = fsim::app::deserialize_compiled_hir_bundle(
        handoff.bytes, "compiled-hir-lifetime", diagnostics);
    assert(compiled && compiled->valid() && !diagnostics.has_error());
    assert(lifetime_identity(
               *compiled, fsim::semantic::Language::system_verilog)
        == handoff.identity);
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *compiled, handoff.consumer_mappings, diagnostics));
    assert(lifetime_identity(
               *compiled, fsim::semantic::Language::system_verilog)
        == handoff.identity);

    const fsim::elaboration::Root root {
        "sv:work.lifetime", "dut"
    };
    auto elaborated = fsim::elaboration::elaborate(
        *compiled,
        std::span<const fsim::elaboration::Root> { &root, 1U },
        { },
        { },
        nullptr,
        { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->processes().size() == 1U);
    const auto q = elaborated.design->find_signal("dut.q");
    assert(q);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "1");
}

void run_vhdl_compiled_hir_lifetime_test(
    const std::filesystem::path& directory)
{
    const auto source = directory / "lifetime.vhd";
    write_file(source, R"(
entity lifetime_vhdl is
  port (
    a : in bit;
    q : out bit
  );
end entity lifetime_vhdl;

architecture rtl of lifetime_vhdl is
  signal bridge : bit;
begin
  bridge <= a;

  drive_q : process (bridge)
  begin
    q <= bridge;
  end process drive_q;
end architecture rtl;
)");
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "compiled-hir-vhdl-lifetime";
    config.project.tops = {
        { "vhdl:work.lifetime_vhdl(rtl)", "dut" }
    };
    config.build.cache_path = directory / "cache";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto handoff = compile_lifetime_bundle(
        config, fsim::semantic::Language::vhdl, diagnostics);
    // Neither the parser nor a lowering adapter survives this handoff.
    auto compiled = fsim::app::deserialize_compiled_hir_bundle(
        handoff.bytes, "compiled-hir-vhdl-lifetime", diagnostics);
    assert(compiled && compiled->valid() && !diagnostics.has_error());
    assert(lifetime_identity(*compiled, fsim::semantic::Language::vhdl)
        == handoff.identity);
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *compiled, handoff.consumer_mappings, diagnostics));
    assert(lifetime_identity(*compiled, fsim::semantic::Language::vhdl)
        == handoff.identity);

    const fsim::elaboration::Root root {
        "vhdl:work.lifetime_vhdl(rtl)", "dut"
    };
    auto elaborated = fsim::elaboration::elaborate(
        *compiled,
        std::span<const fsim::elaboration::Root> { &root, 1U },
        { },
        { },
        nullptr,
        { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->processes().size() == 2U);
    assert(elaborated.design->specializations().size() == 1U);
    const auto& specialization
        = elaborated.design->specializations().front();
    assert(specialization.source_unit == handoff.identity.unit);
    assert(specialization.source_span == handoff.identity.unit_source);
    assert(specialization.origin == handoff.identity.unit_origin);
    const auto a = elaborated.design->find_signal("dut.a");
    const auto q = elaborated.design->find_signal("dut.q");
    assert(a && q);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->deposit_signal(
        *a, fsim::runtime::PackedLogic4::from_msb_string("1"));
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*q).to_msb_string() == "1");
}

void write_sources(const std::filesystem::path& directory)
{
    constexpr std::string_view shared = "// identical dependency contents\n";
    write_file(directory / "include-left" / "shared.svh", shared);
    write_file(directory / "include-right" / "shared.svh", shared);
    write_file(directory / "included_auxiliary.svh", R"(
`line 400 "/producer-only/logical/included_auxiliary.svh" 0
`define INCLUDED_FORMAL_TYPE int
import "DPI-C" function int included_header_dpi(input int value);

package included_header_package;
  localparam string included_file_parameter = `__FILE__;
  covergroup included_group(`INCLUDED_FORMAL_TYPE seed = 1)
      with function sample(input int value);
    value_point: coverpoint value;
  endgroup

  class included_holder;
    covergroup included_class_group
        with function sample(input int value);
      value_point: coverpoint value;
    endgroup
  endclass
endpackage

module included_header_owner;
  included_header_package::included_group included_coverage = new(3);
  initial $display(`__FILE__);
  included_file_assertion: assert property (1'b1)
    else $error(`__FILE__);
endmodule
)");
    write_file(directory / "top.sv", R"(
`include "include-left/shared.svh"
`include "include-right/shared.svh"
`include "included_auxiliary.svh"
module sv_leaf #(parameter integer width = 1) (
  input logic input_value,
  output logic output_value
);
  assign output_value = input_value;
endmodule
module sv_top;
  logic value;
  sv_leaf #(.width(7)) child (
    .input_value(value),
    .output_value()
  );
  initial value = 1'b1;
endmodule
)");
    write_file(directory / "top.vhd", R"(
entity vhdl_leaf is
  generic (width : integer := 1);
  port (
    input_value : in bit;
    output_value : out bit);
end entity vhdl_leaf;

architecture rtl of vhdl_leaf is
begin
  output_value <= input_value;
end architecture rtl;

entity vhdl_top is
end entity vhdl_top;

architecture rtl of vhdl_top is
  signal value : bit := '1';
begin
  child: entity work.vhdl_leaf(rtl)
    generic map (9)
    port map (
      input_value => value,
      output_value => open);
end architecture rtl;
)");
}

void run_artifact_worker_determinism_test(
    const std::filesystem::path& directory)
{
    std::optional<std::vector<std::byte>> cache_reference;
    std::optional<ArtifactSnapshot> object_reference;
    std::optional<ArtifactSnapshot> library_reference;
    std::optional<ArtifactSnapshot> design_reference;
    for (const std::uint32_t jobs : { 1U, 2U, 4U, 8U }) {
        const auto checkout = directory
            / ("relocated-checkout-" + std::to_string(jobs));
        write_sources(checkout);

        auto cache_config = mixed_config(
            checkout,
            directory / ("ordinary-cache-" + std::to_string(jobs)),
            jobs);
        fsim::diagnostic::Engine cache_diagnostics;
        const auto cached = fsim::app::build_project(
            cache_config, cache_diagnostics);
        assert(cached && !cache_diagnostics.has_error()
            && !cached->cache_hit);
        const auto cache_snapshot = cache_payload(
            cache_config, cached->cache_key);
        if (!cache_reference) {
            cache_reference = cache_snapshot;
        } else {
            assert(cache_snapshot == *cache_reference);
        }

        auto object_config = systemverilog_config(
            checkout, directory / ("object-cache-" + std::to_string(jobs)));
        object_config.build.jobs = jobs;
        const auto object = directory
            / ("object-" + std::to_string(jobs) + ".fsimobj");
        fsim::diagnostic::Engine object_diagnostics;
        assert(fsim::app::compile_artifact(
            object_config, object, object_diagnostics));
        assert(!object_diagnostics.has_error());
        assert_parser_independent_object(object, true);
        const auto object_snapshot = capture_artifact(object);
        if (!object_reference) {
            object_reference = object_snapshot;
        } else {
            assert(object_snapshot == *object_reference);
        }

        auto design_config = object_config;
        design_config.source_sets.clear();
        design_config.build.cache_path = directory
            / ("design-cache-" + std::to_string(jobs));
        const auto design = directory
            / ("design-" + std::to_string(jobs) + ".fsimdesign");
        const std::array objects { object };
        fsim::diagnostic::Engine design_diagnostics;
        assert(fsim::app::elaborate_artifact(
            design_config, objects, design, design_diagnostics));
        assert(!design_diagnostics.has_error());
        const auto design_snapshot = capture_artifact(design);
        if (!design_reference) {
            design_reference = design_snapshot;
        } else {
            assert(design_snapshot == *design_reference);
        }

        auto library_config = mixed_config(
            checkout,
            directory / ("library-cache-" + std::to_string(jobs)),
            jobs);
        const auto library = directory
            / ("library-" + std::to_string(jobs) + ".fsimlib");
        fsim::diagnostic::Engine library_diagnostics;
        assert(fsim::app::export_library(
            library_config, "work", library, library_diagnostics));
        assert(!library_diagnostics.has_error());
        assert_parser_independent_library(library, "work", true);
        const auto library_snapshot = capture_artifact(library);
        if (!library_reference) {
            library_reference = library_snapshot;
        } else {
            assert(library_snapshot == *library_reference);
        }
    }
}

bool has_specialization_parameter(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view instance, const std::string_view name,
    const std::string_view value)
{
    const auto specialization = std::ranges::find(
        design.specializations(), instance,
        &fsim::elaboration::SpecializationInfo::instance);
    return specialization != design.specializations().end()
        && std::ranges::any_of(
            specialization->parameter_values,
            [&](const auto& parameter) {
                return parameter.first == name
                    && parameter.second == value;
            });
}

void assert_decoded_design_ir_origins(
    const fsim::app::BuiltProject& project)
{
    const auto top = std::ranges::find(
        project.systemverilog_hir.units(), std::string { "sv_top" },
        &fsim::semantic::sv::Unit::name);
    const auto leaf = std::ranges::find(
        project.systemverilog_hir.units(), std::string { "sv_leaf" },
        &fsim::semantic::sv::Unit::name);
    assert(top != project.systemverilog_hir.units().end());
    assert(leaf != project.systemverilog_hir.units().end());
    assert(top->instances.size() == 1U);
    assert(top->processes.size() == 1U);

    const auto declaration_named = [&](const fsim::semantic::sv::Unit& unit,
                                       const std::string_view name) {
        const auto declaration = std::ranges::find_if(
            unit.declarations, [&](const fsim::semantic::DeclarationId id) {
                const auto record = std::ranges::find(
                    project.systemverilog_hir.declarations(), id,
                    &fsim::semantic::sv::Declaration::id);
                return record
                        != project.systemverilog_hir.declarations().end()
                    && record->name == name;
            });
        assert(declaration != unit.declarations.end());
        return *declaration;
    };
    const auto width_declaration = declaration_named(*leaf, "width");
    const auto input_declaration = declaration_named(*leaf, "input_value");

    const auto& design = project.design_ir;
    const auto root = std::ranges::find(
        design.instances(), std::string { "sv_root" },
        &fsim::semantic::design::InstanceOccurrence::path);
    const auto child = std::ranges::find(
        design.instances(), std::string { "sv_root.child" },
        &fsim::semantic::design::InstanceOccurrence::path);
    assert(root != design.instances().end());
    assert(child != design.instances().end());

    const auto& root_specialization
        = design.specializations()[root->specialization.value()];
    const auto& child_specialization
        = design.specializations()[child->specialization.value()];
    assert(root_specialization.unit.valid());
    assert(root_specialization.unit == top->id);
    assert(child_specialization.unit.valid());
    assert(child_specialization.unit == leaf->id);
    assert(!root->source_instance);
    assert(child->source_instance == top->instances.front());
    assert(child->origin == leaf->origin);

    const auto width = std::ranges::find(
        child_specialization.parameters, std::string { "width" },
        &fsim::semantic::design::ParameterValue::name);
    assert(width != child_specialization.parameters.end());
    assert(width->declaration == width_declaration);

    const auto input = std::ranges::find(
        design.objects(), std::string { "sv_root.child.input_value" },
        &fsim::semantic::design::Object::path);
    assert(input != design.objects().end());
    const auto input_port = std::ranges::find(
        design.ports(), input->id,
        &fsim::semantic::design::Port::object);
    assert(input_port != design.ports().end());
    assert(input_port->instance == child->id);
    assert(input_port->declaration == input_declaration);

    const auto process = std::ranges::find_if(
        design.processes(), [&](const auto& occurrence) {
            return occurrence.source_process == top->processes.front();
        });
    assert(process != design.processes().end());
    assert(process->specialization == root_specialization.id);
}

void run_systemverilog_child_hierarchy_handoff_test(
    const std::filesystem::path& directory)
{
    auto config = systemverilog_config(
        directory, directory / "decoded-hierarchy-cache");
    std::string bytes;
    std::vector<fsim::library::SourceNameMapping> consumer_mappings;
    fsim::semantic::UnitId top_id;
    fsim::semantic::UnitId leaf_id;
    fsim::semantic::InstanceId child_id;
    fsim::semantic::SourceSpanId leaf_source;
    fsim::semantic::OriginId leaf_origin;
    {
        fsim::diagnostic::Engine diagnostics;
        auto checked = fsim::app::check_project(config, diagnostics);
        assert(checked && !diagnostics.has_error());
        const auto top = std::ranges::find(
            checked->systemverilog_hir.units(), std::string { "sv_top" },
            &fsim::semantic::sv::Unit::name);
        const auto leaf = std::ranges::find(
            checked->systemverilog_hir.units(), std::string { "sv_leaf" },
            &fsim::semantic::sv::Unit::name);
        assert(top != checked->systemverilog_hir.units().end());
        assert(leaf != checked->systemverilog_hir.units().end());
        assert(top->instances.size() == 1U);
        top_id = top->id;
        leaf_id = leaf->id;
        child_id = top->instances.front();
        leaf_source = leaf->source;
        leaf_origin = leaf->origin;
        const auto source_mappings
            = fsim::app::application_detail::compiled_cache_source_mappings(
                *checked, config.base_directory, diagnostics);
        assert(source_mappings);
        assert(fsim::app::application_detail::relocate_compiled_design_sources(
            *checked, *source_mappings, diagnostics));
        auto encoded = fsim::app::serialize_compiled_hir_bundle(
            *checked, diagnostics);
        assert(encoded && !diagnostics.has_error());
        bytes = std::move(*encoded);
        for (const auto& mapping : *source_mappings) {
            consumer_mappings.push_back(
                { mapping.logical_name, mapping.producer_name });
        }
    }

    fsim::diagnostic::Engine diagnostics;
    auto compiled = fsim::app::deserialize_compiled_hir_bundle(
        bytes, "decoded SystemVerilog hierarchy", diagnostics);
    assert(compiled && !diagnostics.has_error());
    const auto relocated
        = fsim::app::application_detail::relocate_compiled_design_sources(
            *compiled, consumer_mappings, diagnostics);
    if (!relocated) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(relocated);
    const fsim::elaboration::Root root { "sv:work.sv_top", "dut" };
    auto elaborated = fsim::elaboration::elaborate(
        *compiled,
        std::span<const fsim::elaboration::Root> { &root, 1U },
        { }, { }, nullptr, { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->specializations().size() == 2U);
    const auto top = std::ranges::find(
        elaborated.design->specializations(), std::string { "dut" },
        &fsim::elaboration::SpecializationInfo::instance);
    const auto child = std::ranges::find(
        elaborated.design->specializations(), std::string { "dut.child" },
        &fsim::elaboration::SpecializationInfo::instance);
    assert(top != elaborated.design->specializations().end());
    assert(child != elaborated.design->specializations().end());
    assert(top->source_unit == top_id && !top->source_instance);
    assert(child->source_unit == leaf_id);
    assert(child->source_instance == child_id);
    assert(child->source_span == leaf_source);
    assert(child->origin == leaf_origin);
    assert(has_specialization_parameter(
        *elaborated.design, "dut.child", "width", "7"));
    assert(child->parameter_identity_values.size() == 1U);
    assert(child->parameter_identity_values.front().first == "width");
    assert(child->parameter_identity_values.front().second.starts_with(
        "svconst-v3:b=0:w=32:s=1:"));

    const auto value = elaborated.design->find_signal("dut.value");
    const auto input
        = elaborated.design->find_signal("dut.child.input_value");
    const auto output
        = elaborated.design->find_signal("dut.child.output_value");
    assert(value && input && output);
    assert(*value == *input && *value != *output);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*output).to_msb_string() == "1");
}

void run_vhdl_child_hierarchy_handoff_test(
    const std::filesystem::path& directory)
{
    auto config = mixed_config(
        directory, directory / "decoded-vhdl-hierarchy-cache", 8);
    config.project.tops = {
        { "vhdl:work.vhdl_top(rtl)", "dut" }
    };
    config.source_sets.erase(config.source_sets.begin());
    std::string bytes;
    std::vector<fsim::library::SourceNameMapping> consumer_mappings;
    fsim::semantic::UnitId top_id;
    fsim::semantic::UnitId leaf_id;
    fsim::semantic::InstanceId child_id;
    fsim::semantic::SourceSpanId leaf_source;
    fsim::semantic::OriginId leaf_origin;
    {
        fsim::diagnostic::Engine diagnostics;
        auto checked = fsim::app::check_project(config, diagnostics);
        assert(checked && !diagnostics.has_error());
        const auto top = std::ranges::find_if(
            checked->vhdl_hir.units(),
            [](const fsim::semantic::vhdl::Unit& unit) {
                return unit.kind
                    == fsim::semantic::vhdl::UnitKind::architecture
                    && unit.primary_name == "vhdl_top"
                    && unit.name == "rtl";
            });
        const auto leaf = std::ranges::find_if(
            checked->vhdl_hir.units(),
            [](const fsim::semantic::vhdl::Unit& unit) {
                return unit.kind
                    == fsim::semantic::vhdl::UnitKind::architecture
                    && unit.primary_name == "vhdl_leaf"
                    && unit.name == "rtl";
            });
        assert(top != checked->vhdl_hir.units().end());
        assert(leaf != checked->vhdl_hir.units().end());
        assert(top->instances.size() == 1U);
        top_id = top->id;
        leaf_id = leaf->id;
        child_id = top->instances.front();
        leaf_source = leaf->source;
        leaf_origin = leaf->origin;
        const auto source_mappings
            = fsim::app::application_detail::compiled_cache_source_mappings(
                *checked, config.base_directory, diagnostics);
        assert(source_mappings);
        assert(fsim::app::application_detail::relocate_compiled_design_sources(
            *checked, *source_mappings, diagnostics));
        auto encoded = fsim::app::serialize_compiled_hir_bundle(
            *checked, diagnostics);
        assert(encoded && !diagnostics.has_error());
        bytes = std::move(*encoded);
        for (const auto& mapping : *source_mappings) {
            consumer_mappings.push_back(
                { mapping.logical_name, mapping.producer_name });
        }
    }

    fsim::diagnostic::Engine diagnostics;
    auto compiled = fsim::app::deserialize_compiled_hir_bundle(
        bytes, "decoded VHDL hierarchy", diagnostics);
    assert(compiled && !diagnostics.has_error());
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *compiled, consumer_mappings, diagnostics));
    const fsim::elaboration::Root root {
        "vhdl:work.vhdl_top(rtl)", "dut"
    };
    auto elaborated = fsim::elaboration::elaborate(
        *compiled,
        std::span<const fsim::elaboration::Root> { &root, 1U },
        { }, { }, nullptr, { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->specializations().size() == 2U);
    const auto top = std::ranges::find(
        elaborated.design->specializations(), std::string { "dut" },
        &fsim::elaboration::SpecializationInfo::instance);
    const auto child = std::ranges::find(
        elaborated.design->specializations(), std::string { "dut.child" },
        &fsim::elaboration::SpecializationInfo::instance);
    assert(top != elaborated.design->specializations().end());
    assert(child != elaborated.design->specializations().end());
    assert(top->source_unit == top_id && !top->source_instance);
    assert(child->source_unit == leaf_id);
    assert(child->source_instance == child_id);
    assert(child->source_span == leaf_source);
    assert(child->origin == leaf_origin);
    assert(has_specialization_parameter(
        *elaborated.design, "dut.child", "width", "9"));
    assert(child->parameter_identity_values.size() == 1U);
    assert(child->parameter_identity_values.front().first == "width");
    const auto& width_identity
        = child->parameter_identity_values.front().second;
    assert(width_identity.starts_with("vhdlconst-v1;"));
    assert(width_identity.find(";type=integer;")
        != std::string::npos);
    assert(width_identity.find(";nominal=integer;")
        != std::string::npos);
    assert(width_identity.ends_with(";value=9"));

    const auto value = elaborated.design->find_signal("dut.value");
    const auto input
        = elaborated.design->find_signal("dut.child.input_value");
    const auto output
        = elaborated.design->find_signal("dut.child.output_value");
    assert(value && input && output);
    assert(*value == *input && *value != *output);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*output).to_msb_string() == "1");
}

fsim::project::Config decoded_feature_corpus_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& cache)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "compiled-hir-feature-corpus";
    config.project.tops = {
        { "sv:work.feature_configuration", "configured" },
        { "sv:work.feature_udp_top", "udp" },
        { "sv:work.feature_recursive_top", "recursive" },
        { "sv:work.feature_specify_top", "specify" },
        { "sv:work.feature_rom_top", "rom" },
        { "sv:work.feature_type_parameter_top", "sv_type" },
        { "vhdl:work.feature_type_generic_top(rtl)", "vhdl_type" },
    };
    config.project.time_resolution = "1ns";
    config.build.cache_path = cache;
    config.build.jobs = 8;
    config.run.max_deltas = 100;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = { directory / "feature-corpus.sv" };
    config.source_sets.push_back(std::move(sources));

    fsim::project::SourceSet vhdl_sources;
    vhdl_sources.language = fsim::project::Language::vhdl;
    vhdl_sources.standard = "2008";
    vhdl_sources.library = "work";
    vhdl_sources.compilation_unit = "file";
    vhdl_sources.files = { directory / "feature-corpus.vhd" };
    config.source_sets.push_back(std::move(vhdl_sources));
    return config;
}

void write_decoded_feature_corpus(
    const std::filesystem::path& directory)
{
    write_file(directory / "feature-corpus.sv", R"(
package feature_rom_package;
  localparam FEATURE_ROM_WORD_0 = 2 + 3;
  localparam logic [15:0] FEATURE_ROM = 16'ha53c;
  localparam logic [31:0] FEATURE_ROM_CELLS = {
    8'h12,
    8'h30 + 8'h05,
    8'h56,
    8'h78
  };
endpackage

module feature_probe;
  logic observed;
  initial observed = 1'b1;
endmodule

module feature_leaf #(
  parameter logic [7:0] VALUE = 8'h00
) (
  output logic [7:0] value
);
  assign value = VALUE;
endmodule

module feature_top;
  logic [7:0] child_value;
  feature_leaf child(child_value);
  defparam child.VALUE = 8'ha5;

  for (genvar lane = 0; lane < 2; ++lane) begin : outer
    if (lane == 0) begin : active
      feature_leaf #(.VALUE(8'h3c)) generated();
    end
  end

  bind feature_leaf feature_probe bound_probe();
endmodule

config feature_configuration;
  design work.feature_top;
endconfig

primitive feature_udp_inv (q, d);
  output q;
  input d;
  table
    0 : 1;
    1 : 0;
    x : x;
  endtable
endprimitive

module feature_udp_top;
  logic drive;
  wire result;
  feature_udp_inv selected(result, drive);
  initial drive = 1'b1;
endmodule

module feature_recursive_top;
  logic [7:0] result;
  function automatic logic [7:0] recurse(input int depth);
    if (depth == 0)
      return 8'd1;
    return recurse(depth - 1) + depth;
  endfunction
  initial result = recurse(4);
endmodule

module feature_specify_leaf #(
  parameter BASE = 2
) (
  input a,
  input clock,
  output z
);
  reg notifier;
  specify
    specparam PATH_DELAY = BASE + 1;
    (a => z) = PATH_DELAY;
    $setup(posedge a, posedge clock, PATH_DELAY, notifier);
  endspecify
  assign z = a;
endmodule

module feature_specify_top;
  wire a;
  wire clock;
  wire z;
  feature_specify_leaf #(.BASE(5)) child(a, clock, z);
endmodule

module feature_rom_top;
  logic [15:0] observed;
  logic [31:0] folded_word;
  logic [15:0] aggregate_observed;
  initial begin
    observed = feature_rom_package::FEATURE_ROM;
    folded_word = feature_rom_package::FEATURE_ROM_WORD_0;
    aggregate_observed = {
      feature_rom_package::FEATURE_ROM_CELLS[23:16],
      feature_rom_package::FEATURE_ROM_CELLS[15:8]
    };
  end
endmodule

module feature_typed_value #(
  parameter type T = logic [3:0],
  parameter T INIT = 4'h3
) (
  output T value
);
  initial value = INIT;
endmodule

module feature_type_parameter_top;
  logic [3:0] default_value;
  bit [7:0] selected_value;
  feature_typed_value defaults(default_value);
  feature_typed_value #(
    .T(bit [7:0]),
    .INIT(8'ha6)
  ) selected(selected_value);
endmodule
)");
    write_file(directory / "feature-corpus.vhd", R"(
package feature_values is
  subtype feature_word_t is bit_vector(3 downto 0);
  constant feature_initial : feature_word_t := "1010";
end package feature_values;

entity feature_generic_copy is
  generic (type Data_T);
  port (
    input_value : in Data_T;
    output_value : out Data_T
  );
end entity feature_generic_copy;

architecture rtl of feature_generic_copy is
  signal local_value : Data_T;
begin
  local_value <= input_value;
  output_value <= local_value;
end architecture rtl;

entity feature_type_generic_top is
end entity feature_type_generic_top;

architecture rtl of feature_type_generic_top is
  signal word_input : work.feature_values.feature_word_t
    := work.feature_values.feature_initial;
  signal word_output : work.feature_values.feature_word_t;
begin
  child: entity work.feature_generic_copy(rtl)
    generic map (
      Data_T => work.feature_values.feature_word_t
    )
    port map (
      input_value => word_input,
      output_value => word_output
    );
end architecture rtl;
)");
}

void assert_systemverilog_feature_corpus(
    const fsim::semantic::CompiledDesign& design)
{
    const auto unit_named = [&](const std::string_view name)
        -> const fsim::semantic::sv::Unit& {
        const auto unit = std::ranges::find(
            design.systemverilog_hir.units(), name,
            &fsim::semantic::sv::Unit::name);
        assert(unit != design.systemverilog_hir.units().end());
        return *unit;
    };
    const auto& top = unit_named("feature_top");
    assert(top.defparams.size() == 1U);
    assert(top.binds.size() == 1U);
    assert(top.generates.size() == 1U);
    assert(!top.generates.front().nested.empty());
    assert(unit_named("feature_configuration").configuration);

    const auto& recursive = unit_named("feature_recursive_top");
    assert(std::ranges::any_of(
        recursive.declarations, [&](const auto declaration_id) {
            const auto declaration = std::ranges::find(
                design.systemverilog_hir.declarations(), declaration_id,
                &fsim::semantic::sv::Declaration::id);
            return declaration
                    != design.systemverilog_hir.declarations().end()
                && declaration->form
                    == fsim::semantic::sv::DeclarationForm::function
                && declaration->name == "recurse"
                && declaration->callable;
        }));
    assert(!unit_named("feature_specify_leaf").timing.empty());
    assert(std::ranges::any_of(
        design.systemverilog_hir.udps(), [](const auto& udp) {
            return udp.name == "feature_udp_inv" && !udp.rows.empty();
        }));

    const auto rom_word = std::ranges::find(
        design.systemverilog_hir.declarations(),
        std::string { "FEATURE_ROM_WORD_0" },
        &fsim::semantic::sv::Declaration::name);
    assert(rom_word != design.systemverilog_hir.declarations().end());
    assert(rom_word->initializer);
    const auto initializer = std::ranges::find(
        design.systemverilog_hir.expressions(), *rom_word->initializer,
        &fsim::semantic::sv::Expression::id);
    assert(initializer != design.systemverilog_hir.expressions().end());
    assert(initializer->folded);
    assert(initializer->kind
        == fsim::semantic::sv::ExpressionKind::integer_literal);
    assert(initializer->text == "5");
    const auto rom = std::ranges::find(
        design.systemverilog_hir.declarations(),
        std::string { "FEATURE_ROM" },
        &fsim::semantic::sv::Declaration::name);
    assert(rom != design.systemverilog_hir.declarations().end());
    assert(rom->initializer);
    const auto rom_initializer = std::ranges::find(
        design.systemverilog_hir.expressions(), *rom->initializer,
        &fsim::semantic::sv::Expression::id);
    assert(rom_initializer != design.systemverilog_hir.expressions().end());
    assert(rom_initializer->folded);
    assert(rom_initializer->kind
        == fsim::semantic::sv::ExpressionKind::logic_literal);
}

void assert_vhdl_feature_corpus(
    const fsim::semantic::CompiledDesign& design)
{
    const auto package = std::ranges::find_if(
        design.vhdl_hir.units(), [](const auto& unit) {
            return unit.kind
                    == fsim::semantic::vhdl::UnitKind::package
                && unit.name == "feature_values";
        });
    assert(package != design.vhdl_hir.units().end());
    assert(std::ranges::any_of(
        design.vhdl_hir.declarations(), [](const auto& declaration) {
            return declaration.name == "feature_word_t"
                && declaration.declared_type;
        }));
    const auto generic_copy = std::ranges::find_if(
        design.vhdl_hir.units(), [](const auto& unit) {
            return unit.kind
                    == fsim::semantic::vhdl::UnitKind::entity
                && unit.name == "feature_generic_copy";
    });
    assert(generic_copy != design.vhdl_hir.units().end());
    assert(std::ranges::any_of(
        generic_copy->declarations, [&](const auto declaration_id) {
            const auto declaration = std::ranges::find(
                design.vhdl_hir.declarations(), declaration_id,
                &fsim::semantic::vhdl::Declaration::id);
            return declaration != design.vhdl_hir.declarations().end()
                && declaration->name == "data_t"
                && declaration->form
                    == fsim::semantic::vhdl::DeclarationForm::generic_type;
        }));
}

void assert_systemverilog_feature_corpus_design(
    const fsim::elaboration::ElaboratedDesign& design)
{
    assert(design.find_signal("configured.child.value"));
    constexpr std::string_view generated
        = "configured.outer[0].active.generated";
    assert(design.find_signal(std::string { generated } + ".value"));
    assert(has_specialization_parameter(
        design, generated, "VALUE", "60"));
    assert(std::ranges::count_if(
               design.specializations(), [](const auto& specialization) {
                   return specialization.instance.starts_with("configured.")
                       && specialization.instance.ends_with(
                           ".bound_probe");
               })
        >= 2);
    assert(design.find_signal("udp.result"));
    assert(design.find_signal("recursive.result"));
    assert(design.find_signal("rom.observed"));
    assert(design.find_signal("rom.aggregate_observed"));
    assert(design.find_signal("sv_type.default_value"));
    assert(design.find_signal("sv_type.selected_value"));
    assert(design.find_signal("vhdl_type.word_output"));
    assert(design.verilog_specify_paths().size() == 1U);
    assert(design.verilog_timing_checks().size() == 1U);
    assert(has_specialization_parameter(
        design, "configured.child", "VALUE", "165"));
}

using DecodedFeatureValues = std::array<std::string, 6>;

DecodedFeatureValues execute_decoded_feature_corpus(
    const fsim::elaboration::ElaboratedDesign& design)
{
    assert_systemverilog_feature_corpus_design(design);
    constexpr std::array<std::string_view, 6> signal_names {
        "recursive.result",
        "rom.observed",
        "rom.folded_word",
        "rom.aggregate_observed",
        "sv_type.default_value",
        "sv_type.selected_value",
    };
    std::array<fsim::runtime::simir::SignalId, signal_names.size()> signals { };
    for (std::size_t index = 0; index < signal_names.size(); ++index) {
        const auto signal = design.find_signal(signal_names[index]);
        assert(signal);
        signals[index] = *signal;
    }

    auto interpreter = design.create_interpreter();
    interpreter->start();
    const auto result = interpreter->run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    DecodedFeatureValues values { };
    for (std::size_t index = 0; index < signals.size(); ++index) {
        values[index]
            = interpreter->signal_value(signals[index]).to_msb_string();
    }
    const DecodedFeatureValues expected {
        "00001011",
        "1010010100111100",
        "00000000000000000000000000000101",
        "0011010101010110",
        "0011",
        "10100110",
    };
    assert(values == expected);
    return values;
}

void run_decoded_feature_object_library_test(
    const std::filesystem::path& directory)
{
    write_decoded_feature_corpus(directory);
    const auto systemverilog_object
        = directory / "feature-corpus-sv.fsimobj";
    const auto vhdl_object = directory / "feature-corpus-vhdl.fsimobj";
    const auto library = directory / "feature-corpus.fsimlib";
    auto producer = decoded_feature_corpus_config(
        directory, directory / "producer-cache");

    fsim::diagnostic::Engine direct_diagnostics;
    const auto direct_project = fsim::app::build_project(
        producer, direct_diagnostics);
    if (!direct_project || direct_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, direct_diagnostics);
    }
    assert(direct_project && !direct_diagnostics.has_error());
    assert(!direct_project->cache_hit);
    const auto direct_values
        = execute_decoded_feature_corpus(direct_project->design);

    fsim::diagnostic::Engine cache_diagnostics;
    const auto cached_project = fsim::app::build_project(
        producer, cache_diagnostics);
    if (!cached_project || cache_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, cache_diagnostics);
    }
    assert(cached_project && !cache_diagnostics.has_error());
    assert(cached_project->cache_hit);
    const auto cached_values
        = execute_decoded_feature_corpus(cached_project->design);
    assert(cached_values == direct_values);

    auto systemverilog_producer = producer;
    systemverilog_producer.project.name
        = "compiled-hir-feature-sv-object-producer";
    systemverilog_producer.project.tops.resize(6U);
    systemverilog_producer.source_sets.resize(1U);
    fsim::diagnostic::Engine systemverilog_object_diagnostics;
    assert(fsim::app::compile_artifact(
        systemverilog_producer, systemverilog_object,
        systemverilog_object_diagnostics));
    assert(!systemverilog_object_diagnostics.has_error());
    assert_parser_independent_object(systemverilog_object);
    fsim::diagnostic::Engine systemverilog_metadata_diagnostics;
    const auto systemverilog_metadata
        = fsim::artifact::load_object_metadata(
            systemverilog_object, systemverilog_metadata_diagnostics);
    assert(systemverilog_metadata
        && !systemverilog_metadata_diagnostics.has_error());
    fsim::diagnostic::Engine systemverilog_decode_diagnostics;
    const auto systemverilog_compiled
        = fsim::app::deserialize_compiled_hir_bundle(
            read_file(systemverilog_object
                / systemverilog_metadata->compiled_hir_artifact),
            "SystemVerilog feature object",
            systemverilog_decode_diagnostics);
    assert(systemverilog_compiled
        && !systemverilog_decode_diagnostics.has_error());
    assert_systemverilog_feature_corpus(*systemverilog_compiled);

    auto vhdl_producer = producer;
    vhdl_producer.project.name
        = "compiled-hir-feature-vhdl-object-producer";
    vhdl_producer.project.tops = {
        { "vhdl:work.feature_type_generic_top(rtl)", "vhdl_type" }
    };
    vhdl_producer.source_sets.erase(vhdl_producer.source_sets.begin());
    vhdl_producer.build.cache_path = directory / "vhdl-producer-cache";
    fsim::diagnostic::Engine vhdl_object_diagnostics;
    assert(fsim::app::compile_artifact(
        vhdl_producer, vhdl_object, vhdl_object_diagnostics));
    assert(!vhdl_object_diagnostics.has_error());
    assert_parser_independent_object(vhdl_object);
    fsim::diagnostic::Engine vhdl_metadata_diagnostics;
    const auto vhdl_metadata = fsim::artifact::load_object_metadata(
        vhdl_object, vhdl_metadata_diagnostics);
    assert(vhdl_metadata && !vhdl_metadata_diagnostics.has_error());
    fsim::diagnostic::Engine vhdl_decode_diagnostics;
    const auto vhdl_compiled = fsim::app::deserialize_compiled_hir_bundle(
        read_file(vhdl_object / vhdl_metadata->compiled_hir_artifact),
        "VHDL feature object", vhdl_decode_diagnostics);
    assert(vhdl_compiled && !vhdl_decode_diagnostics.has_error());
    assert_vhdl_feature_corpus(*vhdl_compiled);

    fsim::diagnostic::Engine library_diagnostics;
    assert(fsim::app::export_library(
        producer, "work", library, library_diagnostics));
    assert(!library_diagnostics.has_error());
    assert_parser_independent_library(library, "work");
    fsim::diagnostic::Engine library_metadata_diagnostics;
    const auto library_metadata = fsim::library::load_metadata(
        library, "work", library_metadata_diagnostics);
    assert(library_metadata && !library_metadata_diagnostics.has_error());
    fsim::diagnostic::Engine library_decode_diagnostics;
    const auto library_compiled
        = fsim::app::deserialize_compiled_hir_bundle(
            read_file(library / library_metadata->compiled_hir_artifact),
            "SystemVerilog feature library", library_decode_diagnostics);
    assert(library_compiled && !library_decode_diagnostics.has_error());
    assert_systemverilog_feature_corpus(*library_compiled);
    assert_vhdl_feature_corpus(*library_compiled);

    assert(std::filesystem::remove(directory / "feature-corpus.sv"));
    assert(std::filesystem::remove(directory / "feature-corpus.vhd"));
    auto object_consumer = producer;
    object_consumer.project.name = "compiled-hir-feature-object-consumer";
    object_consumer.source_sets.clear();
    object_consumer.build.cache_path = directory / "object-consumer-cache";
    const std::array objects { systemverilog_object, vhdl_object };
    fsim::diagnostic::Engine object_consumer_diagnostics;
    const auto object_project = fsim::app::build_objects(
        object_consumer, objects, object_consumer_diagnostics);
    if (!object_project || object_consumer_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, object_consumer_diagnostics);
    }
    assert(object_project && !object_consumer_diagnostics.has_error());
    const auto object_values
        = execute_decoded_feature_corpus(object_project->design);
    assert(object_values == direct_values);

    auto library_consumer = object_consumer;
    library_consumer.project.name
        = "compiled-hir-feature-library-consumer";
    library_consumer.build.cache_path = directory / "library-consumer-cache";
    library_consumer.library_mappings.push_back({ "work", library });
    fsim::diagnostic::Engine library_consumer_diagnostics;
    const auto library_project = fsim::app::build_project(
        library_consumer, library_consumer_diagnostics);
    if (!library_project || library_consumer_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, library_consumer_diagnostics);
    }
    assert(library_project && !library_consumer_diagnostics.has_error());
    const auto library_values
        = execute_decoded_feature_corpus(library_project->design);
    assert(library_values == object_values);
}

fsim::project::Config vhdl_object_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::filesystem::path& cache,
    const std::string_view name)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = std::string { name };
    config.project.time_resolution = "1ns";
    config.build.cache_path = cache;
    config.build.jobs = 8;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

void run_split_vhdl_entity_architecture_object_test(
    const std::filesystem::path& directory)
{
    const auto entity_source = directory / "split_entity.vhd";
    const auto architecture_source = directory / "split_architecture.vhd";
    const auto entity_object = directory / "split-entity.fsimobj";
    const auto architecture_object
        = directory / "split-architecture.fsimobj";
    const auto incompatible_architecture_object
        = directory / "split-architecture-vhdl93.fsimobj";
    write_file(entity_source, R"(
entity split_object_handoff is
  generic (width : positive := 4);
  port (
    input_value : in bit_vector(width - 1 downto 0) := "1010";
    output_value : out bit_vector(width - 1 downto 0)
  );
  type payload_t is array (natural range <>) of bit;
end entity split_object_handoff;
)");
    write_file(architecture_source, R"(
architecture rtl of split_object_handoff is
  signal result : payload_t(width - 1 downto 0) := (others => '0');
begin
  drive_result : process
  begin
    result <= input_value;
    wait for 1 ns;
    output_value <= result;
    wait;
  end process drive_result;
end architecture rtl;
)");

    auto entity_config = vhdl_object_config(
        directory, entity_source, directory / "split-entity-cache",
        "split-vhdl-entity");
    auto architecture_config = vhdl_object_config(
        directory, architecture_source,
        directory / "split-architecture-cache",
        "split-vhdl-architecture");
    fsim::diagnostic::Engine entity_diagnostics;
    const auto entity_published = fsim::app::compile_artifact(
        entity_config, entity_object, entity_diagnostics);
    if (!entity_published || entity_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, entity_diagnostics);
    }
    assert(entity_published && !entity_diagnostics.has_error());
    fsim::diagnostic::Engine architecture_diagnostics;
    const auto architecture_published = fsim::app::compile_artifact(
        architecture_config, architecture_object,
        architecture_diagnostics);
    if (!architecture_published || architecture_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, architecture_diagnostics);
    }
    assert(architecture_published && !architecture_diagnostics.has_error());
    auto incompatible_architecture_config = architecture_config;
    incompatible_architecture_config.project.name
        = "split-vhdl-architecture-vhdl93";
    incompatible_architecture_config.source_sets.front().standard = "1993";
    incompatible_architecture_config.build.cache_path
        = directory / "split-architecture-vhdl93-cache";
    fsim::diagnostic::Engine incompatible_publish_diagnostics;
    const auto incompatible_architecture_published
        = fsim::app::compile_artifact(
            incompatible_architecture_config,
            incompatible_architecture_object,
            incompatible_publish_diagnostics);
    if (!incompatible_architecture_published
        || incompatible_publish_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, incompatible_publish_diagnostics);
    }
    assert(incompatible_architecture_published);
    assert(!incompatible_publish_diagnostics.has_error());
    assert_parser_independent_object(entity_object);
    assert_parser_independent_object(architecture_object);
    assert_parser_independent_object(incompatible_architecture_object);

    const std::array incompatible_objects {
        entity_object, incompatible_architecture_object
    };
    fsim::diagnostic::Engine incompatible_link_diagnostics;
    assert(!fsim::app::load_objects(
        incompatible_objects, incompatible_link_diagnostics));
    assert(has_diagnostic_message(
        incompatible_link_diagnostics,
        "FSIM-ART-VHDEP-001",
        "incompatible compiler-supplied VHDL package environments"));

    fsim::diagnostic::Engine entity_metadata_diagnostics;
    const auto entity_metadata = fsim::artifact::load_object_metadata(
        entity_object, entity_metadata_diagnostics);
    fsim::diagnostic::Engine architecture_metadata_diagnostics;
    const auto architecture_metadata = fsim::artifact::load_object_metadata(
        architecture_object, architecture_metadata_diagnostics);
    assert(entity_metadata && architecture_metadata);
    assert(!entity_metadata_diagnostics.has_error());
    assert(!architecture_metadata_diagnostics.has_error());
    assert(entity_metadata->units.size() == 1U);
    assert(entity_metadata->units.front().kind == "entity");
    assert(entity_metadata->units.front().name
        == "split_object_handoff");
    assert(architecture_metadata->units.size() == 1U);
    assert(architecture_metadata->units.front().kind == "architecture");
    assert(architecture_metadata->units.front().name == "rtl");
    assert(architecture_metadata->units.front().primary_name
        == "split_object_handoff");

    const std::array architecture_only { architecture_object };
    fsim::diagnostic::Engine unresolved_diagnostics;
    auto unresolved = fsim::app::load_objects(
        architecture_only, unresolved_diagnostics);
    assert(unresolved && !unresolved_diagnostics.has_error());
    const auto unresolved_architecture = std::ranges::find_if(
        unresolved->vhdl_hir.units(), [](const auto& unit) {
            return unit.kind
                    == fsim::semantic::vhdl::UnitKind::architecture
                && unit.primary_name == "split_object_handoff"
                && unit.name == "rtl";
        });
    assert(unresolved_architecture != unresolved->vhdl_hir.units().end());
    assert(std::ranges::any_of(
        unresolved->references(), [&](const auto& reference) {
            return reference.kind
                    == fsim::semantic::CompiledReferenceKind::entity
                && reference.owner == unresolved_architecture->id
                && reference.library == "work"
                && reference.name == "split_object_handoff"
                && !reference.target;
        }));

    const auto link_with_architecture_profile = [&](
        const std::string_view standard,
        const std::string_view compatibility_profile) {
        const std::array entity_input { entity_object };
        const std::array architecture_input { architecture_object };
        fsim::diagnostic::Engine entity_load_diagnostics;
        auto entity_design = fsim::app::load_objects(
            entity_input, entity_load_diagnostics);
        fsim::diagnostic::Engine architecture_load_diagnostics;
        auto architecture_design = fsim::app::load_objects(
            architecture_input, architecture_load_diagnostics);
        assert(entity_design && architecture_design);
        assert(!entity_load_diagnostics.has_error());
        assert(!architecture_load_diagnostics.has_error());
        // Each standalone load also installs compiler-owned packages. Project
        // the two work-library payloads before invoking the raw HIR linker so
        // duplicate package definitions cannot mask the profile diagnostic.
        auto entity_primary = fsim::semantic::extract_compiled_library(
            *entity_design, "work");
        auto architecture_primary
            = fsim::semantic::extract_compiled_library(
                *architecture_design, "work");
        assert(entity_primary.ok() && architecture_primary.ok());
        auto selected_architecture = std::ranges::find_if(
            architecture_primary.design->vhdl_hir.mutable_units(),
            [](const auto& unit) {
                return unit.kind
                        == fsim::semantic::vhdl::UnitKind::architecture
                    && unit.primary_name == "split_object_handoff"
                    && unit.name == "rtl";
            });
        assert(selected_architecture
            != architecture_primary.design->vhdl_hir.mutable_units().end());
        if (!standard.empty()) {
            selected_architecture->standard = standard;
        }
        if (!compatibility_profile.empty()) {
            selected_architecture->compatibility_profile
                = compatibility_profile;
        }
        std::vector<fsim::semantic::CompiledDesign> inputs;
        inputs.emplace_back(std::move(*entity_primary.design));
        inputs.emplace_back(std::move(*architecture_primary.design));
        return fsim::semantic::link_compiled_designs(std::move(inputs));
    };
    const auto standard_mismatch = link_with_architecture_profile(
        "1993", { });
    assert(!standard_mismatch.ok());
    assert(standard_mismatch.diagnostic_code
        == "FSIM-FE-VHORDER-011");
    assert(standard_mismatch.error.find(
        "entity 'work.split_object_handoff' was analyzed as 2008")
        != std::string::npos);
    assert(standard_mismatch.error.find(
        "architecture 'rtl' uses 1993") != std::string::npos);
    const auto compatibility_mismatch = link_with_architecture_profile(
        { }, "incompatible-profile");
    assert(!compatibility_mismatch.ok());
    assert(compatibility_mismatch.diagnostic_code
        == "FSIM-FE-VHORDER-011");
    assert(compatibility_mismatch.error.find(
        "compatibility profile 'incompatible-profile'")
        != std::string::npos);

    // Neither compilation-local parser workspace survives compile_artifact().
    // Removing both producer sources makes the decoded object bundles the only
    // possible inputs to entity/architecture linking and elaboration.
    assert(std::filesystem::remove(entity_source));
    assert(std::filesystem::remove(architecture_source));
    const std::array objects { entity_object, architecture_object };
    fsim::diagnostic::Engine load_diagnostics;
    auto loaded = fsim::app::load_objects(objects, load_diagnostics);
    if (!loaded || load_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, load_diagnostics);
    }
    assert(loaded && loaded->valid() && !load_diagnostics.has_error());
    assert(loaded->objects.size() == 2U);
    const auto entity = std::ranges::find_if(
        loaded->vhdl_hir.units(), [](const auto& unit) {
            return unit.kind == fsim::semantic::vhdl::UnitKind::entity
                && unit.name == "split_object_handoff";
        });
    const auto architecture = std::ranges::find_if(
        loaded->vhdl_hir.units(), [](const auto& unit) {
            return unit.kind
                    == fsim::semantic::vhdl::UnitKind::architecture
                && unit.primary_name == "split_object_handoff"
                && unit.name == "rtl";
        });
    assert(entity != loaded->vhdl_hir.units().end());
    assert(architecture != loaded->vhdl_hir.units().end());
    const auto declaration_in = [&](
        const fsim::semantic::vhdl::Unit& unit,
        const std::string_view name,
        const fsim::semantic::vhdl::DeclarationForm form)
        -> const fsim::semantic::vhdl::Declaration* {
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = std::ranges::find(
                loaded->vhdl_hir.declarations(), declaration_id,
                &fsim::semantic::vhdl::Declaration::id);
            if (declaration != loaded->vhdl_hir.declarations().end()
                && declaration->name == name
                && declaration->form == form) {
                return &*declaration;
            }
        }
        return nullptr;
    };
    assert(declaration_in(
        *entity, "width",
        fsim::semantic::vhdl::DeclarationForm::generic_constant));
    assert(declaration_in(
        *entity, "input_value",
        fsim::semantic::vhdl::DeclarationForm::port));
    assert(declaration_in(
        *entity, "output_value",
        fsim::semantic::vhdl::DeclarationForm::port));
    assert(declaration_in(
        *entity, "payload_t",
        fsim::semantic::vhdl::DeclarationForm::type));
    const auto* result_declaration = declaration_in(
        *architecture, "result",
        fsim::semantic::vhdl::DeclarationForm::signal);
    assert(result_declaration && result_declaration->subtype);
    assert(result_declaration->subtype->type_mark.spelling == "payload_t");
    assert(std::ranges::any_of(
        loaded->references(), [&](const auto& reference) {
            return reference.kind
                    == fsim::semantic::CompiledReferenceKind::entity
                && reference.owner == architecture->id
                && reference.target == entity->id;
        }));

    const fsim::elaboration::Root root {
        "vhdl:work.split_object_handoff(rtl)", "dut"
    };
    auto elaborated = fsim::elaboration::elaborate(
        *loaded,
        std::span<const fsim::elaboration::Root> { &root, 1U },
        { }, { }, nullptr, { });
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok() && elaborated.design);
    assert(elaborated.design->specializations().size() == 1U);
    assert(elaborated.design->specializations().front().source_unit
        == architecture->id);
    assert(has_specialization_parameter(
        *elaborated.design, "dut", "width", "4"));
    const auto input = elaborated.design->find_signal("dut.input_value");
    const auto output = elaborated.design->find_signal("dut.output_value");
    const auto result = elaborated.design->find_signal("dut.result");
    assert(input && output && result);
    assert(*input != *output && *input != *result && *output != *result);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(interpreter->signal_value(*input).to_msb_string() == "1010");
    assert(interpreter->signal_value(*result).to_msb_string() == "1010");
    assert(interpreter->signal_value(*output).to_msb_string() == "1010");
}

void run_shared_vhdl_object_dependency_test(
    const std::filesystem::path& directory)
{
    const auto left_source = directory / "shared_left.vhd";
    const auto right_source = directory / "shared_right.vhd";
    const auto left_object = directory / "shared-left.fsimobj";
    const auto right_object = directory / "shared-right.fsimobj";
    write_file(left_source, R"(
library ieee;
use ieee.std_logic_1164.all;

entity shared_left is
  port (value : out std_logic);
end entity;

architecture rtl of shared_left is
begin
  value <= '1';
end architecture;
)");
    write_file(right_source, R"(
library ieee;
use ieee.std_logic_1164.all;

entity shared_right is
  port (value : out std_logic);
end entity;

architecture rtl of shared_right is
begin
  value <= '0';
end architecture;
)");

    auto left_config = vhdl_object_config(
        directory, left_source, directory / "left-cache", "shared-left");
    auto right_config = vhdl_object_config(
        directory, right_source, directory / "right-cache", "shared-right");
    fsim::diagnostic::Engine left_compile_diagnostics;
    const auto left_compile_ok = fsim::app::compile_artifact(
        left_config, left_object, left_compile_diagnostics);
    if (!left_compile_ok || left_compile_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, left_compile_diagnostics);
    }
    assert(left_compile_ok);
    assert(!left_compile_diagnostics.has_error());
    fsim::diagnostic::Engine right_compile_diagnostics;
    const auto right_compile_ok = fsim::app::compile_artifact(
        right_config, right_object, right_compile_diagnostics);
    if (!right_compile_ok || right_compile_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, right_compile_diagnostics);
    }
    assert(right_compile_ok);
    assert(!right_compile_diagnostics.has_error());
    assert_parser_independent_object(left_object);
    assert_parser_independent_object(right_object);

    fsim::diagnostic::Engine left_metadata_diagnostics;
    const auto left_metadata = fsim::artifact::load_object_metadata(
        left_object, left_metadata_diagnostics);
    fsim::diagnostic::Engine right_metadata_diagnostics;
    const auto right_metadata = fsim::artifact::load_object_metadata(
        right_object, right_metadata_diagnostics);
    assert(left_metadata && right_metadata);
    assert(!left_metadata_diagnostics.has_error());
    assert(!right_metadata_diagnostics.has_error());
    assert(!left_metadata->vhdl_package_dependencies.empty());
    assert(left_metadata->vhdl_package_dependencies
        == right_metadata->vhdl_package_dependencies);

    const auto decode_object = [](
                                   const std::filesystem::path& object,
                                   const fsim::artifact::ObjectMetadata& metadata,
                                   const std::string_view description) {
        fsim::diagnostic::Engine diagnostics;
        auto decoded = fsim::app::deserialize_compiled_hir_bundle(
            read_file(object / metadata.compiled_hir_artifact),
            std::string { description }, diagnostics);
        assert(decoded && decoded->valid() && !diagnostics.has_error());
        return std::move(*decoded);
    };
    auto left_compiled = decode_object(
        left_object, *left_metadata, "left VHDL object compiled HIR");
    auto right_compiled = decode_object(
        right_object, *right_metadata, "right VHDL object compiled HIR");
    const auto has_unit = [](const fsim::semantic::CompiledDesign& design,
                              const std::string_view library,
                              const std::string_view name) {
        return std::ranges::any_of(
            design.semantics.units(), [&](const auto& unit) {
                return unit.library == library && unit.name == name;
            });
    };
    assert(has_unit(left_compiled, "work", "shared_left"));
    assert(has_unit(right_compiled, "work", "shared_right"));
    assert(has_unit(left_compiled, "ieee", "std_logic_1164"));
    assert(has_unit(right_compiled, "ieee", "std_logic_1164"));

    const auto left_ieee = fsim::semantic::extract_compiled_library(
        left_compiled, "ieee");
    const auto right_ieee = fsim::semantic::extract_compiled_library(
        right_compiled, "ieee");
    assert(left_ieee.ok() && right_ieee.ok());
    const auto has_primary_source_provenance = [](const auto& design) {
        const auto is_primary_source = [](const std::string_view name) {
            return name.find("shared_left") != std::string_view::npos
                || name.find("shared_right") != std::string_view::npos;
        };
        return std::ranges::any_of(
                   design.semantics.source_files(), [&](const auto& file) {
                       return is_primary_source(file.physical_name);
                   })
            || std::ranges::any_of(design.semantics.source_spans(), [&](const auto& span) {
                   return is_primary_source(span.logical_name);
               });
    };
    assert(!has_primary_source_provenance(*left_ieee.design));
    assert(!has_primary_source_provenance(*right_ieee.design));
    fsim::diagnostic::Engine dependency_codec_diagnostics;
    const auto left_ieee_bytes = fsim::app::serialize_compiled_hir_bundle(
        *left_ieee.design, dependency_codec_diagnostics);
    const auto right_ieee_bytes = fsim::app::serialize_compiled_hir_bundle(
        *right_ieee.design, dependency_codec_diagnostics);
    assert(left_ieee_bytes && right_ieee_bytes);
    assert(!dependency_codec_diagnostics.has_error());
    assert(*left_ieee_bytes == *right_ieee_bytes);

    // The compile-local parser workspaces are gone before either source is
    // removed. Loading must therefore use the two persisted HIR bundles and
    // link their identical compiler-owned package environment exactly once.
    assert(std::filesystem::remove(left_source));
    assert(std::filesystem::remove(right_source));
    const std::array objects { left_object, right_object };
    fsim::diagnostic::Engine load_diagnostics;
    const auto loaded = fsim::app::load_objects(objects, load_diagnostics);
    if (!loaded || load_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, load_diagnostics);
    }
    assert(loaded && !load_diagnostics.has_error());
    assert(loaded->objects.size() == 2U);
    assert(has_unit(*loaded, "work", "shared_left"));
    assert(has_unit(*loaded, "work", "shared_right"));
    const auto count_std_logic_units = [](const auto& design) {
        return std::ranges::count_if(
            design.semantics.units(), [](const auto& unit) {
                return unit.library == "ieee"
                    && unit.name == "std_logic_1164";
            });
    };
    const auto expected_std_logic_count = count_std_logic_units(
        *left_ieee.design);
    const auto loaded_std_logic_count = count_std_logic_units(*loaded);
    if (loaded_std_logic_count != expected_std_logic_count) {
        std::cerr << "loaded ieee.std_logic_1164 unit count: "
                  << loaded_std_logic_count << "; expected: "
                  << expected_std_logic_count << '\n';
    }
    assert(expected_std_logic_count > 0);
    assert(loaded_std_logic_count == expected_std_logic_count);
}

} // namespace

int main()
{
    const auto relative_source = std::filesystem::path {
        "compiled-hir-source-key" } / "top.sv";
    assert(fsim::app::application_detail::source_path_key(relative_source)
        == fsim::app::application_detail::source_path_key(
            std::filesystem::current_path() / relative_source));
#if defined(_WIN32)
    assert(fsim::app::application_detail::source_path_key(
               std::filesystem::path { "D:\\Build\\Sources\\Top.sv" })
        == fsim::app::application_detail::source_path_key(
            std::filesystem::path { "d:/build/sources/top.sv" }));
    assert(fsim::app::application_detail::same_source_path(
        std::filesystem::path { "D:\\Build\\Sources\\Top.sv" },
        std::filesystem::path { "d:/build/sources/top.sv" }));
#endif
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory temporary {
        std::filesystem::temp_directory_path()
        / ("fsim-compiled-hir-cache-" + unique)
    };
    const auto first_directory = temporary.path / "first";
    write_sources(first_directory);
    run_systemverilog_child_hierarchy_handoff_test(first_directory);
    run_vhdl_child_hierarchy_handoff_test(first_directory);
    run_decoded_feature_object_library_test(
        temporary.path / "decoded-feature-artifacts");
    run_compiled_hir_lifetime_test(temporary.path / "lifetime");
    run_vhdl_compiled_hir_lifetime_test(
        temporary.path / "vhdl-lifetime");
    run_split_vhdl_entity_architecture_object_test(
        temporary.path / "split-vhdl-objects");
    run_shared_vhdl_object_dependency_test(
        temporary.path / "shared-vhdl-objects");
    run_artifact_worker_determinism_test(
        temporary.path / "worker-determinism");

    static_assert(
        fsim::app::application_detail::compiled_hir_cache_producer_revision
        > 0U);
    fsim::compiler::CacheKeyBuilder current_producer;
    fsim::app::application_detail::add_compiled_hir_cache_key_identity(
        current_producer);
    fsim::compiler::CacheKeyBuilder previous_producer;
    fsim::app::application_detail::add_compiled_hir_cache_key_identity(
        previous_producer,
        fsim::app::application_detail::compiled_hir_cache_producer_revision
            - 1U);
    assert(current_producer.finish() != previous_producer.finish());

    auto direct = mixed_config(
        first_directory, temporary.path / "direct-cache", 1);
    fsim::diagnostic::Engine cold_diagnostics;
    auto cold = fsim::app::build_project(direct, cold_diagnostics);
    assert(cold && !cold_diagnostics.has_error() && !cold->cache_hit);
    assert(has_specialization_parameter(
        cold->design, "sv_root.child", "width", "7"));
    assert(has_specialization_parameter(
        cold->design, "vhdl_root.child", "width", "9"));
    const auto cold_key = cold->cache_key;
    const auto cold_roots = cold->design.roots();
    const auto cold_payload = cache_payload(direct, cold_key);
    const auto cold_payload_view = std::string_view {
        reinterpret_cast<const char*>(cold_payload.data()),
        cold_payload.size()
    };
    assert(cold_payload_view.find(
               fsim::support::path_to_utf8(first_directory))
        == std::string_view::npos);
    assert(cold_payload_view.find(
               "/producer-only/logical/included_auxiliary.svh")
        == std::string_view::npos);
    assert(cold_payload_view.find("included 'cache-sources/")
        != std::string_view::npos);
    assert_included_header_auxiliary_bundle(
        cold_payload_view,
        "included-header direct cache payload");
    assert_compiled_hir_codec_rejections(cold_payload_view);

    fsim::diagnostic::Engine collision_decode_diagnostics;
    auto collision_design = fsim::app::deserialize_compiled_hir_bundle(
        std::string_view {
            reinterpret_cast<const char*>(cold_payload.data()),
            cold_payload.size() },
        "collision test", collision_decode_diagnostics);
    assert(collision_design && !collision_decode_diagnostics.has_error());
    assert(collision_design->semantics.source_files().size() >= 2U);
    const auto& collision_sources
        = collision_design->semantics.source_files();
    const std::array collision_mapping {
        fsim::library::SourceNameMapping {
            collision_sources[0].physical_name,
            collision_sources[1].physical_name }
    };
    fsim::diagnostic::Engine collision_diagnostics;
    assert(!fsim::app::application_detail::relocate_compiled_design_sources(
        *collision_design, collision_mapping, collision_diagnostics));
    assert(has_diagnostic(collision_diagnostics, "FSIM-ART-HIR-001"));

    fsim::diagnostic::Engine embedded_path_decode_diagnostics;
    auto embedded_path_design = fsim::app::deserialize_compiled_hir_bundle(
        std::string_view {
            reinterpret_cast<const char*>(cold_payload.data()),
            cold_payload.size() },
        "embedded expansion path test", embedded_path_decode_diagnostics);
    assert(embedded_path_design
        && !embedded_path_decode_diagnostics.has_error());
    auto embedded_path_records = embedded_path_design->semantics.records();
    assert(!embedded_path_records.expansions.empty());
    embedded_path_records.expansions.front().description
        += ", expanded at /unmapped/producer-only.svh:7:3";
    auto embedded_path_semantics = fsim::semantic::Model::from_records(
        std::move(embedded_path_records));
    assert(embedded_path_semantics);
    embedded_path_design->semantics = std::move(*embedded_path_semantics);
    fsim::diagnostic::Engine embedded_path_diagnostics;
    assert(!fsim::app::application_detail::relocate_compiled_design_sources(
        *embedded_path_design,
        std::span<const fsim::library::SourceNameMapping> { },
        embedded_path_diagnostics));
    assert(has_diagnostic(
        embedded_path_diagnostics, "FSIM-ART-HIR-001"));

    fsim::diagnostic::Engine repeated_decode_diagnostics;
    auto repeated_design = fsim::app::deserialize_compiled_hir_bundle(
        std::string_view {
            reinterpret_cast<const char*>(cold_payload.data()),
            cold_payload.size() },
        "repeated source identity test", repeated_decode_diagnostics);
    assert(repeated_design && !repeated_decode_diagnostics.has_error());
    auto repeated_records = repeated_design->semantics.records();
    auto repeated_source = repeated_records.source_files.front();
    repeated_source.id = fsim::semantic::SourceFileId::from_index(
        static_cast<std::uint32_t>(repeated_records.source_files.size()));
    repeated_records.source_files.push_back(std::move(repeated_source));
    auto repeated_semantics = fsim::semantic::Model::from_records(
        std::move(repeated_records));
    assert(repeated_semantics);
    repeated_design->semantics = std::move(*repeated_semantics);
    assert(repeated_design->valid());
    fsim::diagnostic::Engine repeated_diagnostics;
    assert(fsim::app::application_detail::relocate_compiled_design_sources(
        *repeated_design,
        std::span<const fsim::library::SourceNameMapping> { },
        repeated_diagnostics));
    assert(!repeated_diagnostics.has_error());
    auto conflicting_records = repeated_design->semantics.records();
    conflicting_records.source_files.back().content_digest += "-conflict";
    auto conflicting_semantics = fsim::semantic::Model::from_records(
        std::move(conflicting_records));
    assert(conflicting_semantics);
    repeated_design->semantics = std::move(*conflicting_semantics);
    assert(repeated_design->valid());
    fsim::diagnostic::Engine conflicting_diagnostics;
    assert(!fsim::app::application_detail::relocate_compiled_design_sources(
        *repeated_design,
        std::span<const fsim::library::SourceNameMapping> { },
        conflicting_diagnostics));
    assert(has_diagnostic(conflicting_diagnostics, "FSIM-ART-HIR-001"));

    fsim::diagnostic::Engine warm_diagnostics;
    auto warm = fsim::app::build_project(direct, warm_diagnostics);
    assert(warm && !warm_diagnostics.has_error() && warm->cache_hit);
    assert(has_specialization_parameter(
        warm->design, "sv_root.child", "width", "7"));
    assert(has_specialization_parameter(
        warm->design, "vhdl_root.child", "width", "9"));
    assert(warm->cache_key == cold_key);
    assert(warm->design.roots() == cold_roots);
    assert_decoded_design_ir_origins(*warm);

    fsim::compiler::ObjectCache storage { direct.build.cache_path };
    std::error_code cache_error;

    const auto relocated_directory = temporary.path / "relocated";
    write_sources(relocated_directory);
    auto relocated = mixed_config(
        relocated_directory, direct.build.cache_path, 8);
    fsim::diagnostic::Engine relocated_diagnostics;
    const auto relocated_project = fsim::app::build_project(
        relocated, relocated_diagnostics);
    assert(relocated_project && !relocated_diagnostics.has_error());
    assert(relocated_project->cache_key == cold_key);
    assert(relocated_project->cache_hit);

    const auto wrong_directory = temporary.path / "wrong-valid";
    write_sources(wrong_directory);
    write_file(wrong_directory / "top.sv", R"(
`include "include-left/shared.svh"
`include "include-right/shared.svh"
module sv_top;
  logic value;
  initial value = 1'b0;
endmodule
)");
    auto wrong = mixed_config(
        wrong_directory, temporary.path / "wrong-cache", 8);
    fsim::diagnostic::Engine wrong_diagnostics;
    const auto wrong_project = fsim::app::build_project(
        wrong, wrong_diagnostics);
    assert(wrong_project && !wrong_diagnostics.has_error());
    const auto wrong_payload = raw_cache_payload(
        wrong, wrong_project->cache_key);
    assert(storage.store(
        cold_key,
        std::span<const std::byte> {
            wrong_payload.data(), wrong_payload.size() },
        cache_error));
    fsim::diagnostic::Engine wrong_key_diagnostics;
    const auto wrong_key_repaired = fsim::app::build_project(
        direct, wrong_key_diagnostics);
    assert(wrong_key_repaired && !wrong_key_repaired->cache_hit);
    assert(has_diagnostic(wrong_key_diagnostics, "FSIM-CACHE-0002"));
    assert(cache_payload(direct, cold_key) == cold_payload);

    const std::string old_receipt = "FSIM-DESIGN-CACHE-V3\nobsolete\n";
    assert(storage.store(
        cold_key,
        std::as_bytes(std::span { old_receipt.data(), old_receipt.size() }),
        cache_error));
    fsim::diagnostic::Engine old_receipt_diagnostics;
    auto recovered = fsim::app::build_project(
        direct, old_receipt_diagnostics);
    assert(recovered && !recovered->cache_hit);
    assert(has_diagnostic(old_receipt_diagnostics, "FSIM-CACHE-0002"));
    assert(cache_payload(direct, cold_key) == cold_payload);

    {
        std::ofstream output(
            storage.path_for(cold_key), std::ios::binary | std::ios::app);
        output.put('x');
        assert(output.good());
    }
    fsim::diagnostic::Engine outer_corrupt_diagnostics;
    auto outer_repaired = fsim::app::build_project(
        direct, outer_corrupt_diagnostics);
    assert(outer_repaired && !outer_repaired->cache_hit);
    assert(has_diagnostic(
        outer_corrupt_diagnostics, "FSIM-CACHE-0002"));
    assert(cache_payload(direct, cold_key) == cold_payload);

    const std::string corrupt_bundle = "FSIMCHIR";
    cache_error.clear();
    assert(storage.store(
        cold_key,
        std::as_bytes(
            std::span { corrupt_bundle.data(), corrupt_bundle.size() }),
        cache_error));
    fsim::diagnostic::Engine corrupt_diagnostics;
    auto repaired = fsim::app::build_project(direct, corrupt_diagnostics);
    assert(repaired && !repaired->cache_hit);
    assert(has_diagnostic(corrupt_diagnostics, "FSIM-CACHE-0002"));
    assert(cache_payload(direct, cold_key) == cold_payload);

    write_file(first_directory / "top.sv", R"(
module sv_top;
  logic value;
  initial value = 1'b0;
endmodule
)");
    fsim::diagnostic::Engine changed_diagnostics;
    const auto changed = fsim::app::build_project(
        direct, changed_diagnostics);
    assert(changed && !changed_diagnostics.has_error());
    assert(!changed->cache_hit && changed->cache_key != cold_key);

    const auto object = temporary.path / "work.fsimobj";
    auto object_compile = systemverilog_config(
        relocated_directory, temporary.path / "object-compile-cache");
    fsim::diagnostic::Engine object_compile_diagnostics;
    assert(fsim::app::compile_artifact(
        object_compile, object, object_compile_diagnostics));
    assert(!object_compile_diagnostics.has_error());
    // The publisher has returned, so none of its compile-local parser storage
    // remains. Every indexed unit is metadata-only and the sole owning unit
    // representation is an independently decodable compiled-HIR bundle.
    assert_parser_independent_object(object, true);
    fsim::project::Config object_config;
    object_config.base_directory = relocated_directory;
    object_config.project.name = "compiled-hir-object-cache";
    object_config.project.tops = { { "sv:work.sv_top", "dut" } };
    object_config.project.time_resolution = "1ns";
    object_config.build.cache_path = temporary.path / "object-cache";
    const std::array objects { object };
    fsim::diagnostic::Engine object_cold_diagnostics;
    const auto object_cold = fsim::app::build_objects(
        object_config, objects, object_cold_diagnostics);
    if (!object_cold || object_cold_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, object_cold_diagnostics);
    }
    assert(object_cold && !object_cold_diagnostics.has_error());
    assert(!object_cold->cache_hit);
    (void)cache_payload(object_config, object_cold->cache_key);
    fsim::diagnostic::Engine object_warm_diagnostics;
    const auto object_warm = fsim::app::build_objects(
        object_config, objects, object_warm_diagnostics);
    assert(
        object_warm && !object_warm_diagnostics.has_error()
        && object_warm->cache_hit);

    for (const auto& corruption_case : compiled_hir_corruption_cases) {
        const auto corrupted_object = temporary.path
            / ("corrupt-object-" + std::string { corruption_case.name }
                + ".fsimobj");
        make_corrupt_object_copy(
            object, corrupted_object, corruption_case.corruption);
        auto corrupted_config = object_config;
        corrupted_config.build.cache_path = temporary.path
            / ("corrupt-object-cache-"
                + std::string { corruption_case.name });
        const std::array corrupted_inputs { corrupted_object };
        fsim::diagnostic::Engine corrupted_diagnostics;
        assert(!fsim::app::build_objects(
            corrupted_config, corrupted_inputs, corrupted_diagnostics));
        if (corruption_case.corruption
            == CompiledHirCorruption::checksum_mismatch) {
            assert(has_diagnostic_message(
                corrupted_diagnostics, "FSIM-ART-0005",
                ".fsimobj payload checksum mismatch"));
        } else {
            assert(has_diagnostic(
                corrupted_diagnostics, "FSIM-ART-0013"));
        }
        if (corruption_case.corruption
            == CompiledHirCorruption::stale_schema) {
            assert(has_diagnostic_message(
                corrupted_diagnostics, "FSIM-ART-0013",
                "regenerate compiled-HIR bundle with this fsim build"));
            assert(!has_diagnostic_message(
                corrupted_diagnostics, "FSIM-ART-0013", ".fsimdesign"));
        }
    }

    const auto library = temporary.path / "work.fsimlib";
    auto export_config = mixed_config(
        relocated_directory, temporary.path / "export-cache", 8);
    fsim::diagnostic::Engine export_diagnostics;
    assert(fsim::app::export_library(
        export_config, "work", library, export_diagnostics));
    assert(!export_diagnostics.has_error());
    assert_parser_independent_library(library, "work", true);
    fsim::project::Config mapped_config;
    mapped_config.base_directory = relocated_directory;
    mapped_config.project.name = "compiled-hir-mapped-cache";
    mapped_config.project.tops = { { "sv:work.sv_top", "dut" } };
    mapped_config.project.time_resolution = "1ns";
    mapped_config.build.cache_path = temporary.path / "mapped-cache";
    mapped_config.library_mappings.push_back({ "work", library });
    fsim::diagnostic::Engine mapped_cold_diagnostics;
    const auto mapped_cold = fsim::app::build_project(
        mapped_config, mapped_cold_diagnostics);
    if (!mapped_cold || mapped_cold_diagnostics.has_error()) {
        fsim::diagnostic::print_text(
            std::cerr, mapped_cold_diagnostics);
    }
    assert(mapped_cold && !mapped_cold_diagnostics.has_error());
    assert(!mapped_cold->cache_hit);
    const auto mapped_payload = cache_payload(
        mapped_config, mapped_cold->cache_key);
    assert_included_header_auxiliary_bundle(
        std::string_view {
            reinterpret_cast<const char*>(mapped_payload.data()),
            mapped_payload.size() },
        "included-header mapped cache payload");
    fsim::diagnostic::Engine mapped_warm_diagnostics;
    const auto mapped_warm = fsim::app::build_project(
        mapped_config, mapped_warm_diagnostics);
    assert(
        mapped_warm && !mapped_warm_diagnostics.has_error()
        && mapped_warm->cache_hit);

    for (const auto& corruption_case : compiled_hir_corruption_cases) {
        const auto corrupted_library = temporary.path
            / ("corrupt-library-" + std::string { corruption_case.name }
                + ".fsimlib");
        make_corrupt_library_copy(
            library, corrupted_library, corruption_case.corruption);
        auto corrupted_config = mapped_config;
        corrupted_config.build.cache_path = temporary.path
            / ("corrupt-library-cache-"
                + std::string { corruption_case.name });
        corrupted_config.library_mappings.clear();
        corrupted_config.library_mappings.push_back(
            { "work", corrupted_library });
        fsim::diagnostic::Engine corrupted_diagnostics;
        assert(!fsim::app::build_project(
            corrupted_config, corrupted_diagnostics));
        if (corruption_case.corruption
            == CompiledHirCorruption::checksum_mismatch) {
            assert(has_diagnostic_message(
                corrupted_diagnostics, "FSIM-LIB-0008",
                "mapped library payload checksum mismatch"));
        } else {
            assert(has_diagnostic(
                corrupted_diagnostics, "FSIM-ART-0013"));
        }
        if (corruption_case.corruption
            == CompiledHirCorruption::stale_schema) {
            assert(has_diagnostic_message(
                corrupted_diagnostics, "FSIM-ART-0013",
                "regenerate compiled-HIR bundle with this fsim build"));
            assert(!has_diagnostic_message(
                corrupted_diagnostics, "FSIM-ART-0013", ".fsimdesign"));
        }
    }

    std::cout << "compiled HIR cache application tests passed\n";
}
