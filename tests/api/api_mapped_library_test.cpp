// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/support/path.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

std::string_view view(const fsim_string_view_t value) {
  return {value.data, value.size};
}

}  // namespace

void test_mapped_library_api(const std::filesystem::path& directory) {
  const auto source = directory / "api_mapped_library.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << "module api_mapped_library; logic q; initial q = 1'b1; endmodule\n";
    assert(output.good());
  }
  fsim::project::Config producer;
  producer.base_directory = directory;
  producer.project.name = "api-mapped-library-producer";
  producer.project.top = "sv:vendor.api_mapped_library";
  producer.project.time_resolution = "1ns";
  producer.build.cache_path = directory / "api-mapped-producer-cache";
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "vendor";
  sources.files = {source};
  producer.source_sets.push_back(std::move(sources));
  const auto producer_artifact = directory / "api-vendor.fsimlib";
  fsim::diagnostic::Engine export_diagnostics;
  const auto exported = fsim::app::export_library(
      producer, "vendor", producer_artifact, export_diagnostics);
  if (!exported) {
    fsim::diagnostic::print_text(std::cerr, export_diagnostics);
  }
  assert(exported);

  const auto relocated_root = directory
      / fsim::support::path_from_utf8("relocated mapped packages \xc2\xb5");
  const auto artifact = relocated_root / "vendor library.fsimlib";
  std::filesystem::create_directories(relocated_root);
  std::filesystem::permissions(
      producer_artifact, std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
  std::filesystem::rename(producer_artifact, artifact);
  std::filesystem::rename(
      source, directory / "api_mapped_library.sv.producer-hidden");
  for (const auto& entry :
      std::filesystem::recursive_directory_iterator(artifact)) {
      std::filesystem::permissions(
          entry.path(), std::filesystem::perms::owner_write,
          std::filesystem::perm_options::remove);
  }
  std::filesystem::permissions(
      artifact, std::filesystem::perms::owner_write,
      std::filesystem::perm_options::remove);

  const auto manifest_path = directory / "api-mapped-library.toml";
  {
    std::ofstream manifest(manifest_path, std::ios::binary);
    manifest
        << "schema = 3\n"
        << "[project]\n"
        << "name = \"api-mapped-library-consumer\"\n"
        << "top = \"sv:vendor.api_mapped_library\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[library_map]]\n"
        << "library = \"vendor\"\n"
        << "path = \"relocated mapped packages \xc2\xb5/vendor library.fsimlib\"\n"
        << "[build]\n"
        << "cache_path = \"api-mapped-consumer-cache\"\n";
    assert(manifest.good());
  }

  fsim_session_options_t options{};
  options.struct_size = sizeof(options);
  options.api_version = FSIM_API_VERSION;
  options.max_deltas = 1000;
  fsim_session_t session = FSIM_INVALID_SESSION;
  assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
  assert(
      fsim_session_load_project(
          session, fsim::support::path_to_utf8(manifest_path).c_str())
      == FSIM_STATUS_OK);
  assert(fsim_session_check(session) == FSIM_STATUS_OK);
  assert(fsim_session_build(session) == FSIM_STATUS_OK);

  std::size_t count{};
  assert(
      fsim_session_mapped_library_count(session, &count)
      == FSIM_STATUS_OK);
  assert(count == 1);
  fsim_mapped_library_info_t info{};
  info.struct_size = sizeof(info);
  info.api_version = FSIM_API_VERSION;
  assert(
      fsim_session_get_mapped_library_info(session, 0, &info)
      == FSIM_STATUS_OK);
  assert(view(info.library) == "vendor");
  assert(info.metadata_digest.size == 64);
  assert(info.unit_count == 1);
#if defined(FSIM_HAS_LLVM)
  assert(info.native_accepted == 1);
  assert(view(info.native_kind) == "llvm_object");
  assert(info.native_fingerprint.size == 64);
#else
  assert(info.native_accepted == 0);
  assert(info.native_kind.size == 0);
  assert(info.native_fingerprint.size == 0);
#endif
  assert(
      fsim_session_get_mapped_library_info(session, 1, &info)
      == FSIM_STATUS_INVALID_ARGUMENT);
  info.struct_size -= 1;
  assert(
      fsim_session_get_mapped_library_info(session, 0, &info)
      == FSIM_STATUS_INCOMPATIBLE_ABI);
  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
  for (const auto& entry :
      std::filesystem::recursive_directory_iterator(artifact)) {
      std::filesystem::permissions(
          entry.path(), std::filesystem::perms::owner_write,
          std::filesystem::perm_options::add);
  }
  std::filesystem::permissions(
      artifact, std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
}
