// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_workspace.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/app/trace_archive.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/library/source_mapping.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc/incremental.hpp"
#include "fsim/version.hpp"

#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace fsim::app::application_detail {
namespace {

std::string indexed_path(
    const std::string_view directory,
    const std::size_t index,
    const std::string_view suffix) {
  std::ostringstream output;
  output << directory << '/' << std::setw(8) << std::setfill('0') << index
         << suffix;
  return output.str();
}

std::optional<std::string> read_checked_source(
    const std::filesystem::path& path,
    const std::string_view digest,
    diagnostic::Engine& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.error(
        "FSIM-ART-0004",
        "cannot reopen checked compilation source: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    diagnostics.error(
        "FSIM-ART-0004",
        "cannot reread checked compilation source: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  auto bytes = contents.str();
  if (support::Sha256::hex(support::Sha256::digest(bytes)) != digest) {
    diagnostics.error(
        "FSIM-ART-0004",
        "source changed while publishing compilation object: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  return bytes;
}

std::optional<std::filesystem::path> relative_to(
    const std::filesystem::path& path,
    const std::filesystem::path& root) {
  const auto relative = path.lexically_normal().lexically_relative(
      root.lexically_normal());
  if (relative.empty() || relative.is_absolute()
      || std::ranges::any_of(relative, [](const auto& component) {
           return component == "..";
         })) {
    return std::nullopt;
  }
  return relative;
}

}  // namespace

bool publish_workspace_object(
    const project::Config& config,
    const CheckedProject& checked,
    semantic::CompiledDesign compiled_bundle,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics) {
  if (config.source_sets.size() != 1
      || config.source_sets.front().language == project::Language::systemc) {
    diagnostics.error(
        "FSIM-ART-0004",
        "one .fsimobj compile request must contain exactly one HDL source set");
    return false;
  }
  const auto& source_set = config.source_sets.front();
  artifact::ObjectMetadata metadata;
  metadata.producer = std::string{"fsim "} + std::string{version};
  metadata.language = std::string{project::to_string(source_set.language)};
  metadata.standard = source_set.standard;
  metadata.compatibility_profile = source_set.language == project::Language::vhdl
      ? std::string { application_detail::vhdl_compatibility_profile() }
      : project::compatibility_profile(source_set.compatibility_switches);
  metadata.library = source_set.library;
  metadata.compilation_unit = source_set.compilation_unit;
  metadata.uvm_release = std::string{project::to_string(source_set.uvm_release)};
  const auto coverage_identity = artifact::make_code_coverage_artifact_identity(
      app::code_coverage_enabled(config));
  if (!coverage_identity.ok()) {
    diagnostics.error(
        std::string{artifact::kCodeCoverageArtifactDiagnostic},
        "could not construct the v3 object code-coverage identity");
    return false;
  }
  metadata.code_coverage = coverage_identity.identity;
  metadata.defines = source_set.defines;
  metadata.vhdl_package_dependencies =
      application_detail::vhdl_package_dependencies(checked);
  for (std::size_t index = 0;
       index < source_set.include_directories.size(); ++index) {
    metadata.include_roots.push_back(
        std::filesystem::path{"includes"} / indexed_path("roots", index, ""));
  }

  std::vector<library::PortablePayload> payloads;
  std::vector<library::SourceNameMapping> source_mappings;
  std::map<std::filesystem::path, std::string> expected_digests;
  std::vector<std::filesystem::path> ordered_paths;
  const auto add_source = [&](
      const std::filesystem::path& raw_path,
      const std::string& digest) {
    const auto path = raw_path.lexically_normal();
    if (!expected_digests.contains(path)) {
      expected_digests.emplace(path, digest);
      ordered_paths.push_back(path);
    }
  };
  for (const auto& source : checked.hdl_sources) {
    add_source(source.path, source.content_digest);
    for (const auto& dependency : source.dependencies) {
      add_source(dependency.path, dependency.content_digest);
    }
  }
  std::set<std::filesystem::path> root_sources;
  for (const auto& source : source_set.files) {
    root_sources.insert(source.lexically_normal());
  }
  std::size_t root_index = 0;
  std::size_t dependency_index = 0;
  for (const auto& path : ordered_paths) {
    std::filesystem::path logical;
    if (root_sources.contains(path)) {
      logical = std::filesystem::path{"sources"} / "roots"
          / indexed_path("root", root_index++, "") / path.filename();
    } else {
      for (std::size_t include_index = 0;
           include_index < source_set.include_directories.size();
           ++include_index) {
        if (const auto relative = relative_to(
                path, source_set.include_directories[include_index])) {
          logical = metadata.include_roots[include_index] / *relative;
          break;
        }
      }
      if (logical.empty()) {
        logical = std::filesystem::path{"sources"} / "dependencies"
            / indexed_path("dependency", dependency_index++, "")
            / path.filename();
      }
    }
    auto contents = read_checked_source(
        path, expected_digests.at(path), diagnostics);
    if (!contents.has_value()) {
      return false;
    }
    const auto checksum = support::Sha256::hex(
        support::Sha256::digest(*contents));
    metadata.sources.push_back({
        support::path_to_utf8(logical), logical, checksum, metadata.language,
        metadata.standard, metadata.compatibility_profile});
    source_mappings.push_back({support::path_to_utf8(path),
                               support::path_to_utf8(logical)});
    payloads.push_back({logical, std::move(*contents)});
  }
  auto provenance_mappings = compiled_cache_source_mappings(
      checked, config.base_directory, diagnostics);
  if (!provenance_mappings) {
    return false;
  }
  for (auto& mapping : *provenance_mappings) {
    const auto existing = std::ranges::find(
        source_mappings, mapping.producer_name,
        &library::SourceNameMapping::producer_name);
    if (existing == source_mappings.end()) {
      source_mappings.push_back(std::move(mapping));
    }
  }

  if (!relocate_compiled_design_sources(
          compiled_bundle, source_mappings, diagnostics)) {
    return false;
  }
  auto compiled_units = compiled_unit_metadata_entries(
      compiled_bundle, source_set.library);
  for (auto& entry : compiled_units) {
    metadata.units.push_back(std::move(entry));
  }
  for (const auto& declaration : compiled_bundle.systemverilog_hir.udps()) {
    const auto library = declaration.library.empty()
        ? std::string_view { "work" }
        : std::string_view { declaration.library };
    if (library != source_set.library) {
      continue;
    }
    metadata.units.push_back({
        declaration.language == semantic::Language::verilog
            ? "verilog"
            : "systemverilog",
        "primitive", declaration.name, { }, { }, { }, { },
        std::string { compiled_udp_standard(declaration) },
        declaration.compatibility_profile });
  }
  for (const auto& declaration :
       compiled_bundle.systemverilog_hir.classes()) {
    auto entry = compiled_class_metadata_entry(
        compiled_bundle, declaration, source_set.library);
    if (!entry) {
      const auto* owner = compiled_class_owner(
          compiled_bundle, declaration);
      if (owner != nullptr) {
        const auto library = owner->library.empty()
            ? std::string_view { "work" }
            : std::string_view { owner->library };
        if (library != source_set.library) {
          continue;
        }
      }
      diagnostics.error(
          "FSIM-ART-0004",
          "compiled class declaration has no valid HIR owner: '"
              + semantic::sv::class_declaration_identity(declaration)
              + "'");
      return false;
    }
    metadata.units.push_back(std::move(*entry));
  }
  auto compiled_bytes = serialize_compiled_hir_bundle(
      compiled_bundle, diagnostics);
  if (!compiled_bytes) {
    return false;
  }
  const auto compiled_path
      = std::filesystem::path { "compiled/design.fsimhir" };
  const auto compiled_checksum = support::Sha256::hex(
      support::Sha256::digest(*compiled_bytes));
  metadata.compiled_hir_schema = library::kCompiledHirSchemaVersion;
  metadata.compiled_hir_artifact = compiled_path;
  metadata.compiled_hir_checksum = compiled_checksum;
  payloads.push_back(
      { compiled_path, std::move(*compiled_bytes) });
  if (metadata.units.empty()) {
    diagnostics.error(
        "FSIM-ART-0004",
        "compilation produced no units for logical library '"
            + source_set.library + "'");
    return false;
  }

  if (config.run.trace_file && config.run.trace_enabled) {
    auto request = trace_control_request(config.run,
        TraceControlSurface::NonProjectCompile, TraceControlPhase::Compile);
    auto control = apply_trace_control(std::move(request));
    if (!control.ok()) {
      for (const auto& diagnostic : control.diagnostics)
        application_detail::import_diagnostic(diagnostics, diagnostic);
      return false;
    }
    const auto snapshot = make_trace_archive_snapshot(
        *control.application, config.base_directory);
    auto archive = encode_trace_archive(snapshot, TraceArchiveKind::Object);
    if (!archive.ok()) {
      for (const auto& diagnostic : archive.diagnostics)
        application_detail::import_diagnostic(diagnostics, diagnostic);
      return false;
    }
    metadata.trace_archive = trace_archive_hex(archive.archive);
  }

  metadata.compilation_digest =
      artifact::compute_object_compilation_digest(metadata);
  return artifact::publish_object(
      destination, metadata, payloads, diagnostics);
}

bool compile_object(const project::Config& config,
    const std::filesystem::path& destination, diagnostic::Engine& diagnostics)
{
    if (config.source_sets.size() != 1
        || config.source_sets.front().language == project::Language::systemc) {
        diagnostics.error("FSIM-ART-0004",
            "one .fsimobj compile request must contain exactly one HDL source set");
        return false;
    }
    auto workspace = check_project_for_object(config, diagnostics);
    if (!workspace) {
        return false;
    }
    auto checked = release_compiled_project(std::move(*workspace));
    std::vector<std::string> libraries { config.source_sets.front().library };
    for (const auto& dependency : vhdl_package_dependencies(checked)) {
        const auto library = dependency.package.substr(0, dependency.package.find('.'));
        if (std::ranges::find(libraries, library) == libraries.end()) {
            libraries.push_back(library);
        }
    }
    auto projected = semantic::extract_compiled_libraries(checked, libraries);
    if (!projected.ok()) {
        diagnostics.error("FSIM-ART-0004", projected.error);
        return false;
    }
    return publish_workspace_object(config, checked, std::move(*projected.design),
        destination, diagnostics);
}

int handle_compile(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
  if (!invocation.artifact_output.has_value()
      || !compile_object(config, *invocation.artifact_output, diagnostics)) {
    return 1;
  }
  output << "compiled " << config.source_sets.front().files.size()
         << " source file(s) into "
         << support::path_to_utf8(*invocation.artifact_output) << '\n';
  return 0;
}

int handle_systemc_compile(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
  if (invocation.files.size() != 1 || !invocation.artifact_output) {
    return 1;
  }
  systemc::IncrementalCompileRequest request;
  request.source = invocation.files.front();
  request.output = *invocation.artifact_output;
  request.settings = config.systemc;
  request.working_directory = config.base_directory;
  if (!systemc::compile_incremental_object(request, diagnostics)) {
    return 1;
  }
  output << "compiled SystemC translation unit into "
         << support::path_to_utf8(request.output) << '\n';
  return 0;
}

int handle_systemc_link(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
  if (invocation.objects.empty() || !invocation.artifact_output) {
    return 1;
  }
  systemc::IncrementalLinkRequest request;
  request.objects = invocation.objects;
  request.output = *invocation.artifact_output;
  request.logical_library = invocation.library;
  request.settings = config.systemc;
  request.working_directory = config.base_directory;
  if (!systemc::link_incremental_plugin(request, diagnostics)) {
    return 1;
  }
  output << "linked " << request.objects.size()
         << " SystemC object(s) into "
         << support::path_to_utf8(request.output) << '\n';
  return 0;
}

}  // namespace fsim::app::application_detail

namespace fsim::app {

bool compile_artifact(
    const project::Config& config,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics) {
  return application_detail::compile_object(
      config, destination, diagnostics);
}

bool compile_systemc_artifact(
    const systemc::IncrementalCompileRequest& request,
    diagnostic::Engine& diagnostics) {
  return systemc::compile_incremental_object(request, diagnostics);
}

bool link_systemc_artifact(
    const systemc::IncrementalLinkRequest& request,
    diagnostic::Engine& diagnostics) {
  return systemc::link_incremental_plugin(request, diagnostics);
}

}  // namespace fsim::app
