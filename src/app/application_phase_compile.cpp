// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/library/portable_unit.hpp"
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

std::string unit_language(const frontend::DesignUnit& unit) {
  return unit.language == frontend::Language::Vhdl2008
      ? "vhdl"
      : unit.language == frontend::Language::Verilog2005
          ? "verilog" : "systemverilog";
}

std::string unit_kind(const frontend::UnitKind kind) {
  switch (kind) {
    case frontend::UnitKind::VhdlEntity:
      return "entity";
    case frontend::UnitKind::VhdlArchitecture:
      return "architecture";
    case frontend::UnitKind::VhdlConfiguration:
      return "configuration";
    case frontend::UnitKind::VhdlPackage:
    case frontend::UnitKind::SystemVerilogPackage:
      return "package";
    case frontend::UnitKind::VhdlContext:
      return "context";
    case frontend::UnitKind::SystemVerilogInterface:
      return "interface";
    case frontend::UnitKind::VerilogModule:
      return "module";
    case frontend::UnitKind::SystemVerilogProgram:
      return "program";
  }
  return "unit";
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

bool compile_object(
    const project::Config& config,
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
  auto checked = check_project(config, diagnostics);
  if (!checked.has_value()) {
    return false;
  }

  artifact::ObjectMetadata metadata;
  metadata.producer = std::string{"fsim "} + std::string{version};
  metadata.language = std::string{project::to_string(source_set.language)};
  metadata.standard = source_set.standard;
  metadata.library = source_set.library;
  metadata.compilation_unit = source_set.compilation_unit;
  metadata.defines = source_set.defines;
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
  for (const auto& source : checked->hdl_sources) {
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
        support::path_to_utf8(logical), logical, checksum, metadata.language});
    source_mappings.push_back({support::path_to_utf8(path),
                               support::path_to_utf8(logical)});
    payloads.push_back({logical, std::move(*contents)});
  }

  std::size_t unit_index = 0;
  for (const auto& original : checked->parsed.units) {
    const auto unit_library = original.library.empty()
        ? std::string_view{"work"} : std::string_view{original.library};
    if (unit_library != source_set.library) {
      continue;
    }
    auto unit = original;
    // Class resolution has copied every valid out-of-block method body into
    // its package-owned class declaration.  Do not archive the raw definition
    // list too, because object loading performs semantic resolution again.
    unit.systemverilog_class_method_definitions.clear();
    for (auto& dependency : unit.source_dependencies) {
      const auto found = std::ranges::find_if(
          source_mappings, [&](const auto& mapping) {
            return std::filesystem::path(mapping.producer_name).lexically_normal()
                == std::filesystem::path(dependency).lexically_normal();
          });
      if (found != source_mappings.end()) {
        dependency = found->logical_name;
      }
    }
    if (!library::relocate_unit_sources(unit, source_mappings, diagnostics)) {
      return false;
    }
    auto bytes = library::serialize_portable_unit(unit, diagnostics);
    if (!bytes.has_value()) {
      return false;
    }
    const auto path = std::filesystem::path{indexed_path(
        "units", unit_index++, ".fsimir")};
    const auto checksum = support::Sha256::hex(
        support::Sha256::digest(*bytes));
    metadata.units.push_back({
        unit_language(unit), unit_kind(unit.kind), unit.name,
        unit.primary_name,
        unit.kind == frontend::UnitKind::VhdlArchitecture
            ? unit.name : std::string{},
        path, checksum});
    payloads.push_back({path, std::move(*bytes)});
  }
  for (const auto& original : checked->parsed.udp_declarations) {
    const auto unit_library = original.library.empty()
        ? std::string_view{"work"} : std::string_view{original.library};
    if (unit_library != source_set.library) {
      continue;
    }
    auto declaration = original;
    if (!library::relocate_udp_sources(
            declaration, source_mappings, diagnostics)) {
      return false;
    }
    auto bytes = library::serialize_portable_udp(declaration, diagnostics);
    if (!bytes.has_value()) {
      return false;
    }
    const auto path = std::filesystem::path{indexed_path(
        "units", unit_index++, ".fsimudp")};
    const auto checksum = support::Sha256::hex(
        support::Sha256::digest(*bytes));
    metadata.units.push_back({
        declaration.language == frontend::Language::Verilog2005
            ? "verilog" : "systemverilog",
        "primitive", declaration.name, {}, {}, path, checksum});
    payloads.push_back({path, std::move(*bytes)});
  }
  std::map<std::string, library::PortableSystemVerilogClassUnit> class_units;
  for (const auto& declaration : checked->parsed.systemverilog_classes) {
    const auto declaration_library = declaration.library.empty()
        ? std::string_view{"work"} : std::string_view{declaration.library};
    if (declaration_library != source_set.library) continue;
    auto& unit = class_units[declaration.compilation_unit_identity];
    unit.library = std::string{declaration_library};
    unit.compilation_unit_identity = declaration.compilation_unit_identity;
    unit.declarations.push_back(declaration);
  }
  // Class resolution has already transactionally linked every valid
  // out-of-block definition into its owning declaration.  Persisting the raw
  // definitions as well would ask object loading to link them a second time.
  for (auto& [identity, class_unit] : class_units) {
    if (identity.empty()
        || !library::relocate_class_unit_sources(
            class_unit, source_mappings, diagnostics)) {
      if (identity.empty()) {
        diagnostics.error(
            "FSIM-ART-0004",
            "class declaration has no compilation-unit identity");
      }
      return false;
    }
    auto bytes = library::serialize_portable_class_unit(
        class_unit, diagnostics);
    if (!bytes) return false;
    const auto path = std::filesystem::path{indexed_path(
        "units", unit_index++, ".fsimclass")};
    const auto checksum = support::Sha256::hex(
        support::Sha256::digest(*bytes));
    metadata.units.push_back({
        "systemverilog", "class-unit", identity, {}, {}, path, checksum});
    payloads.push_back({path, std::move(*bytes)});
  }
  if (metadata.units.empty()) {
    diagnostics.error(
        "FSIM-ART-0004",
        "compilation produced no owning units for logical library '"
            + source_set.library + "'");
    return false;
  }

  metadata.compilation_digest =
      artifact::compute_object_compilation_digest(metadata);
  return artifact::publish_object(
      destination, metadata, payloads, diagnostics);
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
  request.scratch_directory = config.build.cache_path / "systemc-phase-scratch";
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
  request.scratch_directory = config.build.cache_path / "systemc-phase-scratch";
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
