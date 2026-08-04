// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/design.hpp"

#include "fsim/support/sha256.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

std::string checksum(const std::string_view bytes) {
  return fsim::support::Sha256::hex(fsim::support::Sha256::digest(bytes));
}

void make_tree_writable(const std::filesystem::path& root) {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
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

}  // namespace

int main() {
  const std::string state_bytes = "standalone-runtime-state";
  fsim::artifact::DesignMetadata metadata;
  metadata.producer = "fsim test";
  metadata.time_resolution = "1ns";
  metadata.delay_mode = "typ";
  metadata.optimization = "O2";
  metadata.cache_key = checksum("cache");
  metadata.seed = 17;
  metadata.search_libraries = {"vendor"};
  metadata.roots.push_back({"primary", "sv:work.tb", "sv:work.tb"});
  metadata.bindings.push_back({"primary.child", std::nullopt, "std_logic"});
  metadata.objects.push_back({
      checksum("metadata"), checksum("compilation"), "systemverilog",
      "2017", "work", {checksum("unit")}});
  metadata.payloads = {
      {"runtime", "state/runtime.bin", checksum(state_bytes)},
      {"semantics", "state/semantics.bin", checksum(state_bytes)},
      {"design-ir", "state/design-ir.bin", checksum(state_bytes)}};
  metadata.specialization_cache_keys = {checksum("specialization")};
  metadata.unit_count = 1;
  metadata.semantic_source_count = 1;
  metadata.specialization_count = 1;
  metadata.signal_count = 2;
  metadata.process_count = 1;
  metadata.design_digest = fsim::artifact::compute_design_digest(metadata);

  const auto encoded = fsim::artifact::serialize_design_metadata(metadata);
  assert(encoded == fsim::artifact::serialize_design_metadata(metadata));
  fsim::diagnostic::Engine decode_diagnostics;
  assert(fsim::artifact::deserialize_design_metadata(
      encoded, "design", decode_diagnostics) == metadata);
  assert(!decode_diagnostics.has_error());

  auto format_one = metadata;
  format_one.format = 1;
  format_one.design_digest =
      fsim::artifact::compute_design_digest(format_one);
  const auto format_one_encoded =
      fsim::artifact::serialize_design_metadata(format_one);
  fsim::diagnostic::Engine format_one_diagnostics;
  assert(fsim::artifact::deserialize_design_metadata(
      format_one_encoded, "format-one-design", format_one_diagnostics)
      == format_one);
  assert(!format_one_diagnostics.has_error());

  auto truncated = encoded;
  truncated.pop_back();
  fsim::diagnostic::Engine truncated_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      truncated, "truncated", truncated_diagnostics));
  auto trailing = encoded;
  trailing.push_back('\0');
  fsim::diagnostic::Engine trailing_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      trailing, "trailing", trailing_diagnostics));
  auto inconsistent = metadata;
  inconsistent.seed++;
  fsim::diagnostic::Engine inconsistent_diagnostics;
  assert(!fsim::artifact::deserialize_design_metadata(
      fsim::artifact::serialize_design_metadata(inconsistent),
      "inconsistent", inconsistent_diagnostics));

  const auto directory = std::filesystem::temp_directory_path()
      / ("fsim-design-artifact-test-"
         + std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::vector<fsim::library::PortablePayload> payloads{
      {metadata.payloads[0].artifact, state_bytes},
      {metadata.payloads[1].artifact, state_bytes},
      {metadata.payloads[2].artifact, state_bytes}};
  fsim::diagnostic::Engine publish_diagnostics;
  assert(fsim::artifact::publish_design(
      directory, metadata, payloads, publish_diagnostics));
  assert(!publish_diagnostics.has_error());
  fsim::diagnostic::Engine load_diagnostics;
  assert(fsim::artifact::load_design_metadata(directory, load_diagnostics)
      == metadata);
  assert(!load_diagnostics.has_error());
  assert(
      (std::filesystem::status(directory).permissions()
       & std::filesystem::perms::owner_write)
      == std::filesystem::perms::none);
  std::ofstream denied(directory / "write-attempt", std::ios::binary);
  assert(!denied);

  fsim::diagnostic::Engine overwrite_diagnostics;
  assert(!fsim::artifact::publish_design(
      directory, metadata, payloads, overwrite_diagnostics));
  auto mismatch = payloads;
  mismatch.front().bytes.push_back('x');
  const auto mismatch_directory = directory.parent_path()
      / (directory.filename().string() + "-mismatch");
  fsim::diagnostic::Engine mismatch_diagnostics;
  assert(!fsim::artifact::publish_design(
      mismatch_directory, metadata, mismatch, mismatch_diagnostics));
  assert(!std::filesystem::exists(mismatch_directory));

  make_tree_writable(directory);
  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
  assert(!cleanup_error);
  return 0;
}
