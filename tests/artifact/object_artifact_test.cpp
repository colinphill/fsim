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
#include <string>

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
    static_assert(fsim::artifact::kObjectFormatVersion == 8U);
    static_assert(fsim::library::kPortableSchemaVersion == 15U);
    static_assert(fsim::library::kCompiledHirSchemaVersion == 1U);
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
    assert(checksum(encoded)
        == "52e4d0689ec62986d1451563c9a63ed8a5a397a150a50bec2b34364a2dcdb1ed");
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
            + "; required format 8, portable-unit schema 15, and compiled-HIR "
              "schema 1; regenerate "
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
                + ", portable-unit schema 15, and compiled-HIR schema 1");
    }
    auto v2_header = encoded.substr(0, 20U);
    store_u32(v2_header, 8U, 6U);
    store_u32(v2_header, 12U, 10U);
    expect_identity_rejection(
        v2_header, "v2-object",
        "format 6, portable-unit schema 10, and compiled-HIR schema 1");
    expect_identity_rejection(
        std::move(v2_header), "v2-object-repeat",
        "format 6, portable-unit schema 10, and compiled-HIR schema 1");
    auto future_format = encoded.substr(0, 20U);
    store_u32(
        future_format, 8U, fsim::artifact::kObjectFormatVersion + 1U);
    expect_identity_rejection(
        std::move(future_format), "future-format",
        "format 9, portable-unit schema 15, and compiled-HIR schema 1");
    for (std::uint32_t schema = 0;
        schema < fsim::library::kPortableSchemaVersion; ++schema) {
        auto noncurrent_header = encoded.substr(0, 20U);
        store_u32(noncurrent_header, 12U, schema);
        expect_identity_rejection(
            std::move(noncurrent_header), "stale-portable-schema",
            "format 8, portable-unit schema " + std::to_string(schema)
                + ", and compiled-HIR schema 1");
    }
    auto future_portable_schema = encoded.substr(0, 20U);
    store_u32(
        future_portable_schema, 12U,
        fsim::library::kPortableSchemaVersion + 1U);
    expect_identity_rejection(
        std::move(future_portable_schema), "future-portable-schema",
        "format 8, portable-unit schema 16, and compiled-HIR schema 1");
    auto stale_compiled_hir_schema = encoded.substr(0, 20U);
    store_u32(stale_compiled_hir_schema, 16U, 0U);
    expect_identity_rejection(
        std::move(stale_compiled_hir_schema), "stale-compiled-hir-schema",
        "format 8, portable-unit schema 15, and compiled-HIR schema 0");
    auto future_compiled_hir_schema = encoded.substr(0, 20U);
    store_u32(future_compiled_hir_schema, 16U, 2U);
    expect_identity_rejection(
        std::move(future_compiled_hir_schema), "future-compiled-hir-schema",
        "format 8, portable-unit schema 15, and compiled-HIR schema 2");
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
    assert(fsim::artifact::deserialize_object_metadata(
               fsim::artifact::serialize_object_metadata(vhdl_metadata),
               "vhdl-object", vhdl_decode_diagnostics)
        == vhdl_metadata);
    assert(!vhdl_decode_diagnostics.has_error());
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
    stale_publication_metadata.format = fsim::artifact::kObjectFormatVersion - 1U;
    const auto stale_publication_directory = directory.parent_path()
        / (directory.filename().string() + "-stale-publication");
    fsim::diagnostic::Engine stale_publication_diagnostics;
    assert(!fsim::artifact::publish_object(
        stale_publication_directory, stale_publication_metadata, payloads,
        stale_publication_diagnostics));
    assert(stale_publication_diagnostics.has_error());
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
