// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/incremental.hpp"

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/support/path.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void write_file(
    const std::filesystem::path& path,
    const std::string& contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << contents;
  assert(output.good());
}

void make_writable(const std::filesystem::path& root) {
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

void copy_tree(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
  std::filesystem::create_directories(destination);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(source)) {
    const auto relative = std::filesystem::relative(entry.path(), source);
    if (entry.is_directory()) {
      std::filesystem::create_directories(destination / relative);
    } else {
      std::filesystem::copy_file(entry.path(), destination / relative);
    }
  }
}

std::string module_source(
    const std::string& type,
    const std::string& alias,
    const bool parameterized) {
  std::string result =
      "#include \"fsim/systemc.hpp\"\n"
      "SC_MODULE(" + type + ") {\n"
      "  sc_core::sc_in<sc_dt::sc_logic> value{\"value\"};\n"
      "  sc_core::sc_out<sc_dt::sc_logic> result{\"result\"};\n";
  if (parameterized) {
    result +=
        "  inline static constexpr auto fsim_factory_parameters =\n"
        "      fsim::systemc::make_factory_parameters(\n"
        "          fsim::systemc::factory_parameter{\"width\", "
        "FSIM_SC_CONSTRUCTION_POSITIVE, true, 1});\n";
  }
  result +=
      "  SC_CTOR(" + type + ") {}\n"
      "};\n"
      "SC_FSIM_EXPORT_AS(" + type + ", \"" + alias + "\");\n";
  return result;
}

}  // namespace

int main() {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root = std::filesystem::temp_directory_path()
      / ("fsim-systemc-incremental-test-" + unique);
  std::filesystem::create_directories(root);
  const auto first_source = root / "first.cpp";
  const auto second_source = root / "second.cpp";
  const auto legacy_source = root / "legacy.cpp";
  write_file(first_source, module_source("FirstModule", "first", true));
  write_file(second_source, module_source("SecondModule", "second", false));
  write_file(
      legacy_source,
      "#include \"fsim/systemc_abi.h\"\n"
      "namespace {\n"
      "void* create(void*, const char*, fsim_sc_handle_v1) {\n"
      "  return reinterpret_cast<void*>(1);\n"
      "}\n"
      "void destroy(void*, void*) {}\n"
      "}\n"
      "extern \"C\" FSIM_SC_EXPORT fsim_sc_status_v1 fsim_plugin_init_v1(\n"
      "    const fsim_sc_host_v1*, fsim_sc_registrar_v1* registrar) {\n"
      "  return registrar->register_factory(\n"
      "      registrar->context, \"legacy\", create, destroy, nullptr);\n"
      "}\n");

  fsim::project::SystemCSection settings;
  fsim::diagnostic::Engine diagnostics;
  fsim::systemc::IncrementalCompileRequest first_request;
  first_request.source = first_source;
  first_request.output = root / "first.fsimscobj";
  first_request.settings = settings;
  first_request.working_directory = root;
  first_request.scratch_directory = root / "scratch";
  assert(fsim::systemc::compile_incremental_object(
      first_request, diagnostics));
  assert(!diagnostics.has_error());

  auto cached_first_request = first_request;
  cached_first_request.output.clear();
  const auto incremental_cache = root / "incremental-cache";
  auto cached_first = fsim::systemc::compile_incremental_object_cached(
      cached_first_request, incremental_cache, diagnostics);
  assert(cached_first.success && !cached_first.cache_hit);
  auto cached_first_warm = fsim::systemc::compile_incremental_object_cached(
      cached_first_request, incremental_cache, diagnostics);
  assert(cached_first_warm.success && cached_first_warm.cache_hit);
  assert(cached_first_warm.artifact == cached_first.artifact);

  auto first_metadata = fsim::systemc::load_incremental_object_metadata(
      first_request.output, diagnostics);
  assert(first_metadata.has_value());
  assert(first_metadata->inputs.size() > 1);
  assert(first_metadata->compilation_digest
      == fsim::systemc::compute_incremental_object_digest(*first_metadata));
  const auto first_round_trip =
      fsim::systemc::serialize_incremental_object_metadata(*first_metadata);
  auto first_decoded = fsim::systemc::deserialize_incremental_object_metadata(
      first_round_trip, "first-round-trip", diagnostics);
  assert(first_decoded == first_metadata);

  fsim::diagnostic::Engine overwrite_diagnostics;
  assert(!fsim::systemc::compile_incremental_object(
      first_request, overwrite_diagnostics));
  assert(overwrite_diagnostics.has_error());

  auto missing_request = first_request;
  missing_request.source = root / "missing.cpp";
  missing_request.output = root / "missing.fsimscobj";
  fsim::diagnostic::Engine missing_diagnostics;
  assert(!fsim::systemc::compile_incremental_object(
      missing_request, missing_diagnostics));
  assert(missing_diagnostics.has_error());
  assert(!std::filesystem::exists(missing_request.output));

  const auto corrupt_object = root / "corrupt.fsimscobj";
  copy_tree(first_request.output, corrupt_object);
  make_writable(corrupt_object);
  {
    std::ofstream corrupt(
        corrupt_object / first_metadata->object,
        std::ios::binary | std::ios::app);
    corrupt.put('x');
  }
  fsim::diagnostic::Engine corrupt_object_diagnostics;
  assert(!fsim::systemc::load_incremental_object_metadata(
      corrupt_object, corrupt_object_diagnostics));
  assert(corrupt_object_diagnostics.has_error());

  fsim::systemc::IncrementalCompileRequest second_request = first_request;
  second_request.source = second_source;
  second_request.output = root / "second.fsimscobj";
  assert(fsim::systemc::compile_incremental_object(
      second_request, diagnostics));
  auto cached_second_request = second_request;
  cached_second_request.output.clear();
  auto cached_second = fsim::systemc::compile_incremental_object_cached(
      cached_second_request, incremental_cache, diagnostics);
  assert(cached_second.success && !cached_second.cache_hit);

  const auto incompatible_object = root / "incompatible.fsimscobj";
  copy_tree(second_request.output, incompatible_object);
  make_writable(incompatible_object);
  fsim::diagnostic::Engine incompatible_metadata_diagnostics;
  auto incompatible_metadata =
      fsim::systemc::load_incremental_object_metadata(
          incompatible_object, incompatible_metadata_diagnostics);
  assert(incompatible_metadata && !incompatible_metadata_diagnostics.has_error());
  incompatible_metadata->compiler_fingerprint = std::string(64, 'a');
  incompatible_metadata->input_digest =
      fsim::systemc::compute_incremental_object_input_digest(
          *incompatible_metadata);
  incompatible_metadata->compilation_digest =
      fsim::systemc::compute_incremental_object_digest(*incompatible_metadata);
  write_file(
      incompatible_object
          / fsim::systemc::kIncrementalObjectMetadataFilename,
      fsim::systemc::serialize_incremental_object_metadata(
          *incompatible_metadata));

  fsim::systemc::IncrementalLinkRequest link_request;
  link_request.objects = {first_request.output, second_request.output};
  link_request.output = root / "work.fsimscplugin";
  link_request.logical_library = "work";
  link_request.settings = settings;
  link_request.working_directory = root;
  link_request.scratch_directory = root / "scratch";
  const auto linked = fsim::systemc::link_incremental_plugin(
      link_request, diagnostics);
  if (!linked) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(linked);
  assert(!diagnostics.has_error());

  auto plugin_metadata = fsim::systemc::load_incremental_plugin_metadata(
      link_request.output, diagnostics);
  assert(plugin_metadata.has_value());
  assert(plugin_metadata->object_digests.size() == 2);
  assert(plugin_metadata->factories.size() == 2);
  assert(plugin_metadata->factories[0].name == "first");
  assert(plugin_metadata->factories[0].parameters.size() == 1);
  assert(plugin_metadata->factories[1].name == "second");
  assert(plugin_metadata->link_digest
      == fsim::systemc::compute_incremental_plugin_digest(*plugin_metadata));
  auto registry = fsim::systemc::load_incremental_plugin(
      link_request.output, diagnostics);
  assert(registry != nullptr);
  assert(registry->has_factory("first"));
  assert(registry->has_factory("second"));

  fsim::systemc::IncrementalLinkRequest incompatible_link = link_request;
  incompatible_link.objects = {first_request.output, incompatible_object};
  incompatible_link.output = root / "incompatible.fsimscplugin";
  fsim::diagnostic::Engine incompatible_link_diagnostics;
  assert(!fsim::systemc::link_incremental_plugin(
      incompatible_link, incompatible_link_diagnostics));
  assert(incompatible_link_diagnostics.has_error());
  assert(!std::filesystem::exists(incompatible_link.output));

  const auto corrupt_plugin = root / "corrupt.fsimscplugin";
  copy_tree(link_request.output, corrupt_plugin);
  make_writable(corrupt_plugin);
  {
    std::ofstream corrupt(
        corrupt_plugin / plugin_metadata->library,
        std::ios::binary | std::ios::app);
    corrupt.put('x');
  }
  fsim::diagnostic::Engine corrupt_plugin_diagnostics;
  assert(!fsim::systemc::load_incremental_plugin_metadata(
      corrupt_plugin, corrupt_plugin_diagnostics));
  assert(corrupt_plugin_diagnostics.has_error());

  auto cached_link_request = link_request;
  cached_link_request.objects = {
      cached_first.artifact, cached_second.artifact};
  cached_link_request.output.clear();
  auto cached_link = fsim::systemc::link_incremental_plugin_cached(
      cached_link_request, incremental_cache, diagnostics);
  if (!cached_link.success) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(cached_link.success && !cached_link.cache_hit);
  auto cached_link_warm = fsim::systemc::link_incremental_plugin_cached(
      cached_link_request, incremental_cache, diagnostics);
  assert(cached_link_warm.success && cached_link_warm.cache_hit);
  assert(cached_link_warm.artifact == cached_link.artifact);

  write_file(first_source, module_source("FirstModule", "first_changed", true));
  auto cached_first_changed = fsim::systemc::compile_incremental_object_cached(
      cached_first_request, incremental_cache, diagnostics);
  assert(cached_first_changed.success && !cached_first_changed.cache_hit);
  assert(cached_first_changed.artifact != cached_first.artifact);
  auto cached_second_unchanged = fsim::systemc::compile_incremental_object_cached(
      cached_second_request, incremental_cache, diagnostics);
  assert(cached_second_unchanged.success && cached_second_unchanged.cache_hit);
  assert(cached_second_unchanged.artifact == cached_second.artifact);

  fsim::diagnostic::Engine duplicate_diagnostics;
  auto duplicate_request = link_request;
  duplicate_request.output = root / "duplicate.fsimscplugin";
  duplicate_request.objects.push_back(first_request.output);
  assert(!fsim::systemc::link_incremental_plugin(
      duplicate_request, duplicate_diagnostics));
  assert(duplicate_diagnostics.has_error());
  assert(!std::filesystem::exists(duplicate_request.output));

  const auto duplicate_source = root / "duplicate.cpp";
  write_file(
      duplicate_source,
      module_source("DuplicateModule", "first", false));
  auto duplicate_compile = first_request;
  duplicate_compile.source = duplicate_source;
  duplicate_compile.output = root / "duplicate.fsimscobj";
  fsim::diagnostic::Engine duplicate_factory_diagnostics;
  assert(fsim::systemc::compile_incremental_object(
      duplicate_compile, duplicate_factory_diagnostics));
  auto duplicate_factory_link = link_request;
  duplicate_factory_link.objects = {
      first_request.output, duplicate_compile.output};
  duplicate_factory_link.output = root / "duplicate-factory.fsimscplugin";
  assert(!fsim::systemc::link_incremental_plugin(
      duplicate_factory_link, duplicate_factory_diagnostics));
  assert(duplicate_factory_diagnostics.has_error());
  assert(!std::filesystem::exists(duplicate_factory_link.output));

  const auto unexported_source = root / "unexported.cpp";
  write_file(
      unexported_source,
      "#include \"fsim/systemc.hpp\"\n"
      "SC_MODULE(Unexported) { SC_CTOR(Unexported) {} };\n");
  auto unexported_compile = first_request;
  unexported_compile.source = unexported_source;
  unexported_compile.output = root / "unexported.fsimscobj";
  fsim::diagnostic::Engine unexported_diagnostics;
  assert(fsim::systemc::compile_incremental_object(
      unexported_compile, unexported_diagnostics));
  auto unexported_link = link_request;
  unexported_link.objects = {unexported_compile.output};
  unexported_link.output = root / "unexported.fsimscplugin";
  assert(!fsim::systemc::link_incremental_plugin(
      unexported_link, unexported_diagnostics));
  assert(unexported_diagnostics.has_error());
  assert(!std::filesystem::exists(unexported_link.output));

  fsim::systemc::IncrementalCompileRequest legacy_request = first_request;
  legacy_request.source = legacy_source;
  legacy_request.output = root / "legacy.fsimscobj";
  fsim::diagnostic::Engine legacy_diagnostics;
  assert(fsim::systemc::compile_incremental_object(
      legacy_request, legacy_diagnostics));
  auto legacy_metadata = fsim::systemc::load_incremental_object_metadata(
      legacy_request.output, legacy_diagnostics);
  assert(legacy_metadata && legacy_metadata->defines_plugin_entry_point);
  assert(!legacy_metadata->contains_macro_export);
  fsim::systemc::IncrementalLinkRequest legacy_link = link_request;
  legacy_link.objects = {legacy_request.output};
  legacy_link.output = root / "legacy.fsimscplugin";
  assert(fsim::systemc::link_incremental_plugin(
      legacy_link, legacy_diagnostics));
  auto legacy_registry = fsim::systemc::load_incremental_plugin(
      legacy_link.output, legacy_diagnostics);
  assert(legacy_registry && legacy_registry->has_factory("legacy"));
  assert(!legacy_diagnostics.has_error());

  registry.reset();
  legacy_registry.reset();
  make_writable(root);
  std::filesystem::remove_all(root);
}
