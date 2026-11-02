// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/support/path.hpp"

#include "api_workspace_support.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::string_view view(const fsim_string_view_t value) {
  return {value.data, value.size};
}

}  // namespace

void test_mapped_library_api(const std::filesystem::path& directory) {
  fsim::diagnostic::Engine accessor_diagnostics;
  assert(accessor_diagnostics.empty());
  assert(accessor_diagnostics.diagnostics().empty());
  accessor_diagnostics.note("TEST", "accessor probe");
  assert(!accessor_diagnostics.empty());
  assert(accessor_diagnostics.diagnostics().size() == 1);

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
  std::filesystem::rename(producer_artifact, artifact);
  std::filesystem::rename(
      source, directory / "api_mapped_library.sv.producer-hidden");
  // A consumer only reads the library, so a read-only copy must load.
  std::filesystem::permissions(
      artifact, std::filesystem::perms::owner_write,
      std::filesystem::perm_options::remove);

  // The consumer workspace maps the relocated, read-only .fsimlib and
  // elaborates its top in place; it compiles nothing of its own.
  namespace workspace = fsim::tests::api_workspace;
  const auto consumer = directory / "api-mapped-library-consumer";
  workspace::write_configuration(
      consumer, "fsim::config set elaborate.time_resolution 1ns\n");
  workspace::expect(
      consumer,
      {"library", "map", "vendor", fsim::support::path_to_utf8(artifact)});
  // Elaborate imports only the exported libraries the design reaches, so a
  // mapped library whose file has since gone missing does not stop it. The
  // mapping is written as `library map` leaves it, after its [exported] entry.
  {
    std::ofstream mappings(consumer / ".fsim" / "libraries.toml",
        std::ios::binary | std::ios::app);
    mappings << "unused = \"missing.fsimlib\"\n";
    assert(mappings.good());
  }
  const auto listed = workspace::run(consumer, {"library", "list"});
  assert(listed.status == 0);
  assert(listed.output.find("vendor\t") != std::string::npos);
  assert(listed.output.find("\texported\n") != std::string::npos);
  {
    std::ofstream extra(consumer / "extra.sv", std::ios::binary);
    extra << "module api_mapped_extra; endmodule\n";
    assert(extra.good());
  }
  const auto refused = workspace::run(
      consumer, {"compile", "--library", "vendor", "extra.sv"});
  assert(refused.status != 0);
  assert(refused.output.find("exported library") != std::string::npos);
  workspace::expect(consumer, {"elaborate", "sv:vendor.api_mapped_library"});

  fsim_session_options_t options{};
  options.struct_size = sizeof(options);
  options.api_version = FSIM_API_VERSION;
  options.max_deltas = 1000;
  fsim_session_t session = FSIM_INVALID_SESSION;
  assert(fsim_session_create(&options, &session) == FSIM_STATUS_OK);
  assert(
      fsim_session_open_snapshot(
          session, fsim::support::path_to_utf8(consumer).c_str(), nullptr)
      == FSIM_STATUS_OK);

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
  assert(info.native_accepted == 0);
  assert(info.native_kind.size == 0);
  assert(info.native_fingerprint.size == 0);
  assert(
      fsim_session_get_mapped_library_info(session, 1, &info)
      == FSIM_STATUS_INVALID_ARGUMENT);
  info.struct_size -= 1;
  assert(
      fsim_session_get_mapped_library_info(session, 0, &info)
      == FSIM_STATUS_INCOMPATIBLE_ABI);
  assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
  std::filesystem::permissions(
      artifact, std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
}
