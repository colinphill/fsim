// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/object.hpp"

#include "fsim/support/sha256.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string checksum(const std::string_view bytes) {
  return fsim::support::Sha256::hex(fsim::support::Sha256::digest(bytes));
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
  const std::string source_bytes = "module child; endmodule\n";
  const std::string unit_bytes = "portable-unit";
  fsim::artifact::ObjectMetadata metadata;
  metadata.producer = "fsim test";
  metadata.language = "systemverilog";
  metadata.standard = "2017";
  metadata.library = "work";
  metadata.compilation_unit = "source-set";
  metadata.uvm_release = "1.2";
  metadata.defines = {"WIDTH=8", "TRACE"};
  metadata.include_roots = {"includes/00000000"};
  metadata.sources.push_back({
      "sources/child.sv", "sources/00000000/child.sv",
      checksum(source_bytes), "systemverilog", metadata.standard,
      metadata.compatibility_profile});
  metadata.units.push_back({
      "systemverilog", "module", "child", {}, {},
      "units/00000000.fsimir", checksum(unit_bytes), metadata.standard,
      metadata.compatibility_profile});
  metadata.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(metadata);

  const auto encoded = fsim::artifact::serialize_object_metadata(metadata);
  assert(encoded == fsim::artifact::serialize_object_metadata(metadata));
  fsim::diagnostic::Engine decode_diagnostics;
  const auto decoded = fsim::artifact::deserialize_object_metadata(
      encoded, "object", decode_diagnostics);
  assert(decoded == metadata);
  assert(!decode_diagnostics.has_error());

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
  vhdl_metadata.vhdl_package_dependencies = {{
      "1993", "ieee-1076-standard:1993:fsim-v1",
      "ieee.std_logic_unsigned",
      "synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2",
      checksum("synopsys-unsigned-source") }};
  vhdl_metadata.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(vhdl_metadata);
  fsim::diagnostic::Engine vhdl_decode_diagnostics;
  assert(fsim::artifact::deserialize_object_metadata(
      fsim::artifact::serialize_object_metadata(vhdl_metadata),
      "vhdl-object", vhdl_decode_diagnostics) == vhdl_metadata);
  assert(!vhdl_decode_diagnostics.has_error());
  auto stale_vhdl_metadata = vhdl_metadata;
  stale_vhdl_metadata.vhdl_package_dependencies.front().source_digest =
      checksum("stale-source");
  stale_vhdl_metadata.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(stale_vhdl_metadata);
  assert(stale_vhdl_metadata.compilation_digest
      != vhdl_metadata.compilation_digest);

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
  invalid_release.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(invalid_release);
  fsim::diagnostic::Engine invalid_release_diagnostics;
  assert(!fsim::artifact::deserialize_object_metadata(
      fsim::artifact::serialize_object_metadata(invalid_release),
      "invalid-release", invalid_release_diagnostics));
  auto stale_source_profile = metadata;
  stale_source_profile.sources.front().compatibility_profile
      = "implicit-net";
  stale_source_profile.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(stale_source_profile);
  fsim::diagnostic::Engine stale_source_profile_diagnostics;
  assert(!fsim::artifact::deserialize_object_metadata(
      fsim::artifact::serialize_object_metadata(stale_source_profile),
      "stale-source-profile", stale_source_profile_diagnostics));
  auto omitted_unit_profile = metadata;
  omitted_unit_profile.units.front().compatibility_profile.clear();
  omitted_unit_profile.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(omitted_unit_profile);
  fsim::diagnostic::Engine omitted_unit_profile_diagnostics;
  assert(!fsim::artifact::deserialize_object_metadata(
      fsim::artifact::serialize_object_metadata(omitted_unit_profile),
      "omitted-unit-profile", omitted_unit_profile_diagnostics));
  auto duplicate_source = metadata;
  duplicate_source.sources.push_back(duplicate_source.sources.front());
  duplicate_source.sources.back().artifact = "sources/duplicate.sv";
  duplicate_source.compilation_digest =
      fsim::artifact::compute_object_compilation_digest(duplicate_source);
  fsim::diagnostic::Engine duplicate_source_diagnostics;
  assert(!fsim::artifact::deserialize_object_metadata(
      fsim::artifact::serialize_object_metadata(duplicate_source),
      "duplicate-source", duplicate_source_diagnostics));

  const auto directory = std::filesystem::temp_directory_path()
      / ("fsim-object-artifact-test-"
         + std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::vector<fsim::library::PortablePayload> payloads{
      {metadata.sources.front().artifact, source_bytes},
      {metadata.units.front().artifact, unit_bytes}};
  fsim::diagnostic::Engine publish_diagnostics;
  assert(fsim::artifact::publish_object(
      directory, metadata, payloads, publish_diagnostics));
  assert(!publish_diagnostics.has_error());
  fsim::diagnostic::Engine load_diagnostics;
  assert(fsim::artifact::load_object_metadata(directory, load_diagnostics)
      == metadata);
  assert(!load_diagnostics.has_error());
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
        directory / metadata.units.front().artifact, std::ios::binary);
    assert((std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}} == unit_bytes));
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
