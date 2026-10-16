// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/object.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string checksum(const std::string_view bytes) {
  return fsim::support::Sha256::hex(fsim::support::Sha256::digest(bytes));
}

void store_u32(
    std::string& bytes, const std::size_t offset, const std::uint32_t value)
{
    assert(offset + 4U <= bytes.size());
    for (std::size_t byte = 0; byte < 4U; ++byte) {
        bytes[offset + byte] = static_cast<char>((value >> (byte * 8U)) & 0xffU);
    }
}

void store_u64(
    std::string& bytes, const std::size_t offset, const std::uint64_t value)
{
    assert(offset + 8U <= bytes.size());
    for (std::size_t byte = 0; byte < 8U; ++byte) {
        bytes[offset + byte] = static_cast<char>((value >> (byte * 8U)) & 0xffU);
    }
}

std::uint64_t load_u64(const std::string& bytes, std::size_t offset)
{
    assert(offset + 8U <= bytes.size());
    std::uint64_t value = 0U;
    for (std::size_t byte = 0; byte < 8U; ++byte) {
        value |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(bytes[offset + byte])) << (byte * 8U);
    }
    return value;
}

std::size_t skip_string(const std::string& bytes, std::size_t offset)
{
    const auto size = load_u64(bytes, offset);
    assert(size <= bytes.size() - offset - 8U);
    return offset + 8U + static_cast<std::size_t>(size);
}

std::size_t skip_string_sequence(
    const std::string& bytes, std::size_t offset, const std::size_t fields_per_item)
{
    const auto count = load_u64(bytes, offset);
    offset += 8U;
    assert(count <= bytes.size());
    for (std::uint64_t item = 0U; item < count; ++item) {
        for (std::size_t field = 0U; field < fields_per_item; ++field)
            offset = skip_string(bytes, offset);
    }
    return offset;
}

std::size_t object_unit_name_form_offset(
    const std::string& bytes, const std::size_t target_unit)
{
    std::size_t offset = 20U;
    for (std::size_t field = 0U; field < 11U; ++field)
        offset = skip_string(bytes, offset);
    offset += 5U;
    offset = skip_string(bytes, offset);
    offset = skip_string(bytes, offset);
    offset = skip_string_sequence(bytes, offset, 1U);
    offset = skip_string_sequence(bytes, offset, 1U);
    offset = skip_string_sequence(bytes, offset, 5U);
    offset = skip_string_sequence(bytes, offset, 6U);
    const auto unit_count = load_u64(bytes, offset);
    offset += 8U;
    assert(target_unit < unit_count);
    for (std::size_t unit = 0U; unit < unit_count; ++unit) {
        for (std::size_t field = 0U; field < 9U; ++field)
            offset = skip_string(bytes, offset);
        if (unit == target_unit)
            return offset;
        offset += 8U;
    }
    assert(false);
    return 0U;
}

std::size_t object_compilation_digest_offset(const std::string& bytes)
{
    std::size_t offset = 20U;
    for (std::size_t field = 0U; field < 9U; ++field)
        offset = skip_string(bytes, offset);
    const auto size = load_u64(bytes, offset);
    assert(size <= bytes.size() - offset - 8U);
    assert(size == 64U);
    return offset + 8U;
}

std::string make_v8_object_bytes(const fsim::artifact::ObjectMetadata& metadata)
{
    auto legacy = metadata;
    legacy.format = 8U;
    legacy.portable_schema = 15U;
    for (auto& unit : legacy.units) {
        unit.name_form = fsim::library::VhdlIdentifierForm::unknown;
        unit.primary_name_form = fsim::library::VhdlIdentifierForm::unknown;
    }
    legacy.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(legacy);
    auto bytes = fsim::artifact::serialize_object_metadata(legacy);
    const auto digest_offset = object_compilation_digest_offset(bytes);
    std::copy(legacy.compilation_digest.begin(), legacy.compilation_digest.end(),
        bytes.begin() + static_cast<std::ptrdiff_t>(digest_offset));

    std::size_t offset = 20U;
    for (std::size_t field = 0U; field < 11U; ++field)
        offset = skip_string(bytes, offset);
    offset += 4U + 1U;
    offset = skip_string(bytes, offset);
    offset = skip_string(bytes, offset);
    offset = skip_string_sequence(bytes, offset, 1U); // defines
    offset = skip_string_sequence(bytes, offset, 1U); // include roots
    offset = skip_string_sequence(bytes, offset, 5U); // VHDL dependencies
    offset = skip_string_sequence(bytes, offset, 6U); // sources
    const auto unit_count = load_u64(bytes, offset);
    offset += 8U;
    std::vector<std::size_t> form_offsets;
    for (std::uint64_t unit = 0U; unit < unit_count; ++unit) {
        for (std::size_t field = 0U; field < 9U; ++field)
            offset = skip_string(bytes, offset);
        form_offsets.push_back(offset);
        offset += 8U;
    }
    assert(offset == bytes.size());
    for (auto position = form_offsets.rbegin(); position != form_offsets.rend(); ++position)
        bytes.erase(*position, 8U);
    store_u32(bytes, 8U, 8U);
    store_u32(bytes, 12U, 15U);
    return bytes;
}

void make_tree_writable(const std::filesystem::path& root) {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end;
       iterator.increment(error)) {
    std::filesystem::permissions(
        iterator->path(), std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, error);
    error.clear();
  }
  std::filesystem::permissions(
      root, std::filesystem::perms::owner_all,
      std::filesystem::perm_options::add, error);
}

}  // namespace

int main() {
    static_assert(fsim::artifact::kObjectFormatVersion == 9U);
    static_assert(fsim::library::kPortableSchemaVersion == 16U);
    static_assert(fsim::library::kCompiledHirSchemaVersion == 3U);
    const std::string source_bytes = "module child; endmodule\n";
    const std::string compiled_hir_bytes = "compiled-hir";
    fsim::artifact::ObjectMetadata metadata;
    metadata.code_coverage
        = fsim::artifact::make_code_coverage_artifact_identity(false).identity;
    metadata.compiled_hir_artifact = "compiled/design.fsimhir";
    metadata.compiled_hir_checksum = checksum(compiled_hir_bytes);
    metadata.producer = "fsim test";
    metadata.language = "systemverilog";
    metadata.standard = "2017";
    metadata.library = "work";
    metadata.compilation_unit = "source-set";
    metadata.uvm_release = "1.2";
    metadata.defines = { "WIDTH=8", "TRACE" };
    metadata.include_roots = { "includes/00000000" };
    metadata.sources.push_back({ "sources/child.sv", "sources/00000000/child.sv",
        checksum(source_bytes), "systemverilog", metadata.standard,
        metadata.compatibility_profile });
    metadata.units.push_back({ "systemverilog", "module", "child", { }, { },
        { }, { }, metadata.standard,
        metadata.compatibility_profile });
    metadata.units.push_back({ "systemverilog", "primitive", "invert",
        { }, { }, { }, { }, metadata.standard,
        metadata.compatibility_profile });
    metadata.units.push_back({ "systemverilog", "class",
        "work::$unit@fixture::Packet", "fixture", { }, { }, { },
        metadata.standard, metadata.compatibility_profile });
    metadata.compilation_digest = fsim::artifact::compute_object_compilation_digest(metadata);

    const auto encoded = fsim::artifact::serialize_object_metadata(metadata);
    assert(encoded.size() >= 20U);
    assert(static_cast<unsigned char>(encoded[16U]) == 3U);
    assert(static_cast<unsigned char>(encoded[17U]) == 0U);
    assert(static_cast<unsigned char>(encoded[18U]) == 0U);
    assert(static_cast<unsigned char>(encoded[19U]) == 0U);
    for (std::size_t length = 0U; length < 20U; ++length) {
        fsim::diagnostic::Engine truncated_diagnostics;
        assert(!fsim::artifact::deserialize_object_metadata(
            encoded.substr(0U, length), "truncated-prefix",
            truncated_diagnostics));
        assert(truncated_diagnostics.has_error());
    }
    assert(encoded == fsim::artifact::serialize_object_metadata(metadata));
    fsim::diagnostic::Engine decode_diagnostics;
    const auto decoded = fsim::artifact::deserialize_object_metadata(
        encoded, "object", decode_diagnostics);
    assert(decoded == metadata);
    assert(decoded->units.back().kind == "class");
    assert(decoded->units.back().artifact.empty());
    assert(decoded->units.back().checksum.empty());
    assert(!decode_diagnostics.has_error());
    assert(encoded == fsim::artifact::serialize_object_metadata(*decoded));
    assert(encoded.starts_with(std::string_view { "FSIMOBJ\0", 8U }));
    auto legacy_class_metadata = metadata;
    legacy_class_metadata.units.push_back({ "systemverilog",
        "class-runtime-adapter", "fixture", { }, { },
        "units/fixture.fsimclass", std::string(64, 'b'), metadata.standard,
        metadata.compatibility_profile });
    legacy_class_metadata.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(
            legacy_class_metadata);
    fsim::diagnostic::Engine legacy_class_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(legacy_class_metadata),
        "legacy-class", legacy_class_diagnostics));
    auto enabled_metadata = metadata;
    enabled_metadata.code_coverage
        = fsim::artifact::make_code_coverage_artifact_identity(true).identity;
    enabled_metadata.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(enabled_metadata);
    assert(enabled_metadata.compilation_digest != metadata.compilation_digest);
    auto stale_coverage = metadata;
    stale_coverage.code_coverage.schema = 2U;
    fsim::diagnostic::Engine stale_coverage_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(stale_coverage),
        "stale-coverage", stale_coverage_diagnostics));
    assert(std::ranges::any_of(
        stale_coverage_diagnostics.diagnostics(), [](const auto& diagnostic) {
          return diagnostic.code
              == fsim::artifact::kCodeCoverageArtifactDiagnostic;
        }));

    const auto expect_metadata_rejection = [&](std::string bytes,
                                               const char* source) {
        fsim::diagnostic::Engine diagnostics;
        assert(!fsim::artifact::deserialize_object_metadata(
            bytes, source, diagnostics));
        assert(diagnostics.has_error());
    };
    const auto expect_identity_rejection = [&](std::string bytes,
                                               const char* source,
                                               const std::string& found) {
        fsim::diagnostic::Engine diagnostics;
        assert(!fsim::artifact::deserialize_object_metadata(
            bytes, source, diagnostics));
        const auto expected = "unsupported .fsimobj identity: found " + found
            + "; required format 9, portable-unit schema 16, and compiled-HIR "
              "schema 3; regenerate "
              ".fsimobj with this fsim build";
        assert(std::ranges::any_of(
            diagnostics.diagnostics(), [&](const auto& diagnostic) {
                return diagnostic.code == "FSIM-ART-0001"
                    && diagnostic.message == expected;
            }));
    };
    auto corrupt_magic = encoded;
    corrupt_magic[0] = 'X';
    expect_metadata_rejection(std::move(corrupt_magic), "corrupt-magic");
    for (std::uint32_t format = 0;
        format < fsim::artifact::kObjectFormatVersion; ++format) {
        auto noncurrent_header = encoded.substr(0, 20U);
        store_u32(noncurrent_header, 8U, format);
        expect_identity_rejection(
            std::move(noncurrent_header), "stale-format",
            "format " + std::to_string(format)
                + ", portable-unit schema 16, and compiled-HIR schema 3");
    }
    auto v2_header = encoded.substr(0, 20U);
    store_u32(v2_header, 8U, 6U);
    store_u32(v2_header, 12U, 10U);
    expect_identity_rejection(
        v2_header, "v2-object",
        "format 6, portable-unit schema 10, and compiled-HIR schema 3");
    expect_identity_rejection(
        std::move(v2_header), "v2-object-repeat",
        "format 6, portable-unit schema 10, and compiled-HIR schema 3");
    auto future_format = encoded.substr(0, 20U);
    store_u32(
        future_format, 8U, fsim::artifact::kObjectFormatVersion + 1U);
    expect_identity_rejection(
        std::move(future_format), "future-format",
        "format 10, portable-unit schema 16, and compiled-HIR schema 3");
    for (std::uint32_t schema = 0;
        schema < fsim::library::kPortableSchemaVersion; ++schema) {
        auto noncurrent_header = encoded.substr(0, 20U);
        store_u32(noncurrent_header, 12U, schema);
        expect_identity_rejection(
            std::move(noncurrent_header), "stale-portable-schema",
            "format 9, portable-unit schema " + std::to_string(schema)
                + ", and compiled-HIR schema 3");
    }
    auto future_portable_schema = encoded.substr(0, 20U);
    store_u32(
        future_portable_schema, 12U,
        fsim::library::kPortableSchemaVersion + 1U);
    expect_identity_rejection(
        std::move(future_portable_schema), "future-portable-schema",
        "format 9, portable-unit schema 17, and compiled-HIR schema 3");
    auto stale_compiled_hir_schema = encoded.substr(0, 20U);
    store_u32(stale_compiled_hir_schema, 16U, 0U);
    expect_identity_rejection(
        std::move(stale_compiled_hir_schema), "stale-compiled-hir-schema",
        "format 9, portable-unit schema 16, and compiled-HIR schema 0");
    auto prior_compiled_hir_schema = encoded.substr(0, 20U);
    store_u32(prior_compiled_hir_schema, 16U, 1U);
    expect_identity_rejection(
        std::move(prior_compiled_hir_schema), "prior-compiled-hir-schema",
        "format 9, portable-unit schema 16, and compiled-HIR schema 1");
    auto prior_compiled_hir_schema_v2 = encoded.substr(0, 20U);
    store_u32(prior_compiled_hir_schema_v2, 16U, 2U);
    expect_identity_rejection(
        std::move(prior_compiled_hir_schema_v2),
        "prior-compiled-hir-schema-v2",
        "format 9, portable-unit schema 16, and compiled-HIR schema 2");
    auto future_compiled_hir_schema = encoded.substr(0, 20U);
    store_u32(future_compiled_hir_schema, 16U, 4U);
    expect_identity_rejection(
        std::move(future_compiled_hir_schema), "future-compiled-hir-schema",
        "format 9, portable-unit schema 16, and compiled-HIR schema 4");
    expect_metadata_rejection(encoded.substr(0, 19U), "truncated-header");
    auto oversized_root = encoded;
    store_u64(oversized_root, 20U, UINT64_MAX);
    expect_metadata_rejection(std::move(oversized_root), "oversized-root");

    auto vhdl_metadata = metadata;
    vhdl_metadata.language = "vhdl";
    vhdl_metadata.standard = "1993";
    vhdl_metadata.compatibility_profile = "fsim-synopsys-ieee-compat-v2";
    vhdl_metadata.uvm_release = "none";
    vhdl_metadata.sources.front().language = "vhdl";
    vhdl_metadata.sources.front().standard = vhdl_metadata.standard;
    vhdl_metadata.sources.front().compatibility_profile
        = vhdl_metadata.compatibility_profile;
    vhdl_metadata.units.front().language = "vhdl";
    vhdl_metadata.units.front().name_form
        = fsim::library::VhdlIdentifierForm::extended;
    vhdl_metadata.units.front().standard = vhdl_metadata.standard;
    vhdl_metadata.units.front().compatibility_profile
        = vhdl_metadata.compatibility_profile;
    vhdl_metadata.units.resize(1);
    vhdl_metadata.vhdl_package_dependencies = { { "1993", "ieee-1076-standard:1993:fsim-v3",
        "ieee.std_logic_unsigned",
        "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
        checksum("synopsys-unsigned-source") } };
    vhdl_metadata.compilation_digest = fsim::artifact::compute_object_compilation_digest(vhdl_metadata);
    fsim::diagnostic::Engine vhdl_decode_diagnostics;
    const auto vhdl_encoded
        = fsim::artifact::serialize_object_metadata(vhdl_metadata);
    const auto vhdl_decoded = fsim::artifact::deserialize_object_metadata(
        vhdl_encoded, "vhdl-object", vhdl_decode_diagnostics);
    assert(vhdl_decoded == std::optional { vhdl_metadata });
    assert(vhdl_decoded->units.front().name_form
        == fsim::library::VhdlIdentifierForm::extended);
    auto changed_form = vhdl_metadata;
    changed_form.units.front().name_form
        = fsim::library::VhdlIdentifierForm::basic;
    changed_form.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(changed_form);
    assert(changed_form.compilation_digest != vhdl_metadata.compilation_digest);

    auto v8_bytes = make_v8_object_bytes(vhdl_metadata);
    auto v8_expected = vhdl_metadata;
    v8_expected.format = 8U;
    v8_expected.portable_schema = 15U;
    v8_expected.units.front().name_form
        = fsim::library::VhdlIdentifierForm::unknown;
    v8_expected.units.front().primary_name_form
        = fsim::library::VhdlIdentifierForm::unknown;
    v8_expected.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(v8_expected);
    fsim::diagnostic::Engine v8_decode_diagnostics;
    const auto v8_decoded = fsim::artifact::deserialize_object_metadata(
        v8_bytes, "v8-object", v8_decode_diagnostics);
    assert(v8_decoded == std::optional { v8_expected });
    assert(!v8_decode_diagnostics.has_error());
    assert(!vhdl_decode_diagnostics.has_error());

    auto legacy_for_serialization = vhdl_metadata;
    legacy_for_serialization.format = 8U;
    legacy_for_serialization.portable_schema = 15U;
    for (auto& unit : legacy_for_serialization.units) {
        unit.name_form = fsim::library::VhdlIdentifierForm::unknown;
        unit.primary_name_form = fsim::library::VhdlIdentifierForm::unknown;
    }
    legacy_for_serialization.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(
            legacy_for_serialization);
    const auto upgraded_bytes
        = fsim::artifact::serialize_object_metadata(legacy_for_serialization);
    fsim::diagnostic::Engine upgraded_diagnostics;
    const auto upgraded = fsim::artifact::deserialize_object_metadata(
        upgraded_bytes, "upgraded-legacy-object", upgraded_diagnostics);
    auto upgraded_expected = legacy_for_serialization;
    upgraded_expected.format = fsim::artifact::kObjectFormatVersion;
    upgraded_expected.portable_schema
        = fsim::library::kPortableSchemaVersion;
    upgraded_expected.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(upgraded_expected);
    assert(upgraded == std::optional { upgraded_expected });
    assert(!upgraded_diagnostics.has_error());

    const auto expect_direct_serializer_identity_rejection = [](
        fsim::artifact::ObjectMetadata invalid) {
        invalid.compilation_digest
            = fsim::artifact::compute_object_compilation_digest(invalid);
        bool rejected = false;
        try {
            static_cast<void>(fsim::artifact::serialize_object_metadata(invalid));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
    };
    const auto expect_non_vhdl_form_deserialize_rejection = [](
        fsim::artifact::ObjectMetadata invalid) {
        invalid.compilation_digest
            = fsim::artifact::compute_object_compilation_digest(invalid);
        const auto bytes = fsim::artifact::serialize_object_metadata(invalid);
        fsim::diagnostic::Engine diagnostics;
        assert(!fsim::artifact::deserialize_object_metadata(
            bytes, "invalid-non-vhdl-identifier-form", diagnostics));
        assert(std::ranges::any_of(
            diagnostics.diagnostics(), [](const auto& item) {
                return item.code == "FSIM-ART-0002"
                    && item.message.find(
                        "object unit identifier-form metadata is invalid")
                        != std::string::npos;
            }));
    };
    auto non_vhdl_name_form = metadata;
    non_vhdl_name_form.units.front().name_form
        = fsim::library::VhdlIdentifierForm::basic;
    expect_non_vhdl_form_deserialize_rejection(non_vhdl_name_form);
    auto non_vhdl_primary_form = metadata;
    non_vhdl_primary_form.units.front().primary_name_form
        = fsim::library::VhdlIdentifierForm::basic;
    expect_non_vhdl_form_deserialize_rejection(non_vhdl_primary_form);
    auto mismatched_legacy_schema = metadata;
    mismatched_legacy_schema.format = 8U;
    mismatched_legacy_schema.portable_schema = 14U;
    expect_direct_serializer_identity_rejection(mismatched_legacy_schema);
    auto current_format_old_schema = metadata;
    current_format_old_schema.portable_schema = 15U;
    expect_direct_serializer_identity_rejection(current_format_old_schema);
    auto old_format_current_schema = metadata;
    old_format_current_schema.format = 8U;
    expect_direct_serializer_identity_rejection(old_format_current_schema);
    auto future_metadata_format = metadata;
    future_metadata_format.format = fsim::artifact::kObjectFormatVersion + 1U;
    expect_direct_serializer_identity_rejection(future_metadata_format);
    auto stale_compiled_hir_identity = metadata;
    stale_compiled_hir_identity.compiled_hir_schema
        = fsim::library::kCompiledHirSchemaVersion - 1U;
    expect_direct_serializer_identity_rejection(stale_compiled_hir_identity);
    auto legacy_stale_compiled_hir_identity = legacy_for_serialization;
    legacy_stale_compiled_hir_identity.compiled_hir_schema
        = fsim::library::kCompiledHirSchemaVersion - 1U;
    expect_direct_serializer_identity_rejection(
        legacy_stale_compiled_hir_identity);

    auto invalid_legacy_for_serialization = legacy_for_serialization;
    invalid_legacy_for_serialization.compilation_digest.front()
        = invalid_legacy_for_serialization.compilation_digest.front() == '0'
            ? '1' : '0';
    bool invalid_legacy_rejected = false;
    try {
        static_cast<void>(fsim::artifact::serialize_object_metadata(
            invalid_legacy_for_serialization));
    } catch (const std::invalid_argument&) {
        invalid_legacy_rejected = true;
    }
    assert(invalid_legacy_rejected);

    auto invalid_form_bytes = vhdl_encoded;
    store_u32(invalid_form_bytes,
        object_unit_name_form_offset(invalid_form_bytes, 0U), 3U);
    fsim::diagnostic::Engine invalid_form_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        invalid_form_bytes, "invalid-identifier-form", invalid_form_diagnostics));
    assert(std::ranges::any_of(
        invalid_form_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0001"
                && diagnostic.message.find(
                    "invalid object VHDL identifier-form value")
                    != std::string::npos;
        }));
    auto stale_vhdl_metadata = vhdl_metadata;
    stale_vhdl_metadata.vhdl_package_dependencies.front().source_digest = checksum("stale-source");
    stale_vhdl_metadata.compilation_digest = fsim::artifact::compute_object_compilation_digest(stale_vhdl_metadata);
    assert(stale_vhdl_metadata.compilation_digest
        != vhdl_metadata.compilation_digest);
    auto inconsistent_digest = metadata;
    inconsistent_digest.compilation_digest.front() = inconsistent_digest.compilation_digest.front() == '0' ? '1' : '0';
    fsim::diagnostic::Engine inconsistent_digest_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(inconsistent_digest),
        "inconsistent-digest", inconsistent_digest_diagnostics));

    auto truncated = encoded;
    truncated.pop_back();
    fsim::diagnostic::Engine truncated_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        truncated, "truncated", truncated_diagnostics));
    auto trailing = encoded;
    trailing.push_back('\0');
    fsim::diagnostic::Engine trailing_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        trailing, "trailing", trailing_diagnostics));
    auto invalid = metadata;
    invalid.sources.front().logical_name = "../producer/child.sv";
    fsim::diagnostic::Engine invalid_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(invalid),
        "invalid", invalid_diagnostics));
    auto invalid_release = metadata;
    invalid_release.uvm_release = "2020.4";
    invalid_release.compilation_digest = fsim::artifact::compute_object_compilation_digest(invalid_release);
    fsim::diagnostic::Engine invalid_release_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(invalid_release),
        "invalid-release", invalid_release_diagnostics));
    auto stale_source_profile = metadata;
    stale_source_profile.sources.front().compatibility_profile
        = "implicit-net";
    stale_source_profile.compilation_digest = fsim::artifact::compute_object_compilation_digest(stale_source_profile);
    fsim::diagnostic::Engine stale_source_profile_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(stale_source_profile),
        "stale-source-profile", stale_source_profile_diagnostics));
    auto omitted_unit_profile = metadata;
    omitted_unit_profile.units.front().compatibility_profile.clear();
    omitted_unit_profile.compilation_digest = fsim::artifact::compute_object_compilation_digest(omitted_unit_profile);
    fsim::diagnostic::Engine omitted_unit_profile_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(omitted_unit_profile),
        "omitted-unit-profile", omitted_unit_profile_diagnostics));
    auto duplicate_source = metadata;
    duplicate_source.sources.push_back(duplicate_source.sources.front());
    duplicate_source.sources.back().artifact = "sources/duplicate.sv";
    duplicate_source.compilation_digest = fsim::artifact::compute_object_compilation_digest(duplicate_source);
    fsim::diagnostic::Engine duplicate_source_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(duplicate_source),
        "duplicate-source", duplicate_source_diagnostics));
    auto legacy_udp_payload = metadata;
    legacy_udp_payload.units.back().artifact = "units/invert.fsimudp";
    legacy_udp_payload.units.back().checksum = checksum("portable-udp");
    legacy_udp_payload.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(
            legacy_udp_payload);
    fsim::diagnostic::Engine legacy_udp_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(legacy_udp_payload),
        "legacy-udp-payload", legacy_udp_diagnostics));
    auto legacy_unit_payload = metadata;
    legacy_unit_payload.units.front().artifact = "units/child.fsimir";
    legacy_unit_payload.units.front().checksum = checksum("portable-unit");
    legacy_unit_payload.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(
            legacy_unit_payload);
    fsim::diagnostic::Engine legacy_unit_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        fsim::artifact::serialize_object_metadata(legacy_unit_payload),
        "legacy-unit-payload", legacy_unit_diagnostics));

    const auto directory = std::filesystem::temp_directory_path()
        / ("fsim-object-artifact-test-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::vector<fsim::library::PortablePayload> payloads {
        { metadata.sources.front().artifact, source_bytes },
        { metadata.compiled_hir_artifact, compiled_hir_bytes }
    };
    auto stale_publication_metadata = metadata;
    stale_publication_metadata.format = 8U;
    stale_publication_metadata.portable_schema = 15U;
    for (auto& unit : stale_publication_metadata.units) {
        unit.name_form = fsim::library::VhdlIdentifierForm::unknown;
        unit.primary_name_form = fsim::library::VhdlIdentifierForm::unknown;
    }
    stale_publication_metadata.compilation_digest
        = fsim::artifact::compute_object_compilation_digest(
            stale_publication_metadata);
    const auto stale_publication_directory = directory.parent_path()
        / (directory.filename().string() + "-stale-publication");
    fsim::diagnostic::Engine stale_publication_diagnostics;
    assert(!fsim::artifact::publish_object(
        stale_publication_directory, stale_publication_metadata, payloads,
        stale_publication_diagnostics));
    assert(stale_publication_diagnostics.has_error());
    assert(std::ranges::any_of(
        stale_publication_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ART-0001"
                && diagnostic.message.find(".fsimobj publication")
                    != std::string::npos;
        }));
    assert(!std::filesystem::exists(stale_publication_directory));
    fsim::diagnostic::Engine publish_diagnostics;
    assert(fsim::artifact::publish_object(
        directory, metadata, payloads, publish_diagnostics));
    assert(!publish_diagnostics.has_error());
    fsim::diagnostic::Engine load_diagnostics;
    assert(fsim::artifact::load_object_metadata(directory, load_diagnostics)
        == metadata);
    assert(!load_diagnostics.has_error());
    assert(std::ranges::none_of(
        std::filesystem::recursive_directory_iterator { directory },
        [](const auto& entry) {
            return entry.path().extension() == ".fsimudp"
                || entry.path().extension() == ".fsimir";
        }));
    assert(
        (std::filesystem::status(directory).permissions()
            & std::filesystem::perms::owner_write)
        == std::filesystem::perms::none);
#if !defined(_WIN32)
  // POSIX mode bits deny publication-tree mutation. Windows maps those bits
  // to file attributes, not directory ACLs, so creation remains permitted.
  std::ofstream denied(directory / "write-attempt", std::ios::binary);
  assert(!denied);
#endif

  fsim::diagnostic::Engine overwrite_diagnostics;
  assert(!fsim::artifact::publish_object(
      directory, metadata, payloads, overwrite_diagnostics));
  fsim::diagnostic::Engine rollback_diagnostics;
  assert(fsim::artifact::load_object_metadata(
      directory, rollback_diagnostics) == metadata);
  {
    std::ifstream input(
        directory / metadata.compiled_hir_artifact, std::ios::binary);
    assert((std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}} == compiled_hir_bytes));
  }
  auto mismatched_payloads = payloads;
  mismatched_payloads.front().bytes.push_back('x');
  const auto mismatch_directory = directory.parent_path()
      / (directory.filename().string() + "-mismatch");
  fsim::diagnostic::Engine mismatch_diagnostics;
  assert(!fsim::artifact::publish_object(
      mismatch_directory, metadata, mismatched_payloads,
      mismatch_diagnostics));
  assert(!std::filesystem::exists(mismatch_directory));

  make_tree_writable(directory);
  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  assert(!cleanup_error);
  return 0;
}
