// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/version.hpp"

#include <fstream>
#include <sstream>

namespace fsim::app {
namespace {

std::optional<std::string> read_design_payload(
    const std::filesystem::path& path,
    const std::string_view checksum,
    diagnostic::Engine& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.error(
        "FSIM-ART-0014",
        "cannot open .fsimdesign payload: " + support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    diagnostics.error(
        "FSIM-ART-0014",
        "cannot read .fsimdesign payload: " + support::path_to_utf8(path));
    return std::nullopt;
  }
  auto bytes = contents.str();
  if (support::Sha256::hex(support::Sha256::digest(bytes)) != checksum) {
    diagnostics.error(
        "FSIM-ART-0014",
        ".fsimdesign payload checksum mismatch: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  return bytes;
}

std::string selected_root_identity(
    const BuiltProject& project,
    const std::string_view alias,
    const std::string_view fallback) {
  const auto found = std::ranges::find_if(
      project.design.specializations(), [&](const auto& specialization) {
        return specialization.instance == alias;
      });
  return found == project.design.specializations().end()
      ? std::string{fallback} : found->unit;
}

const artifact::DesignPayload* payload_by_kind(
    const artifact::DesignMetadata& metadata,
    const std::string_view kind) {
  const auto found = std::ranges::find_if(
      metadata.payloads,
      [&](const auto& payload) { return payload.kind == kind; });
  return found == metadata.payloads.end() ? nullptr : &*found;
}

}  // namespace

bool publish_design_artifact(
    const project::Config& config,
    const BuiltProject& project,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics) {
  auto runtime = serialize_runtime_state(project.design, diagnostics);
  auto semantics = serialize_semantic_state(project.semantics, diagnostics);
  auto design_ir = serialize_design_ir_state(project.design_ir, diagnostics);
  if (!runtime || !semantics || !design_ir) {
    return false;
  }

  artifact::DesignMetadata metadata;
  metadata.producer = std::string{"fsim "} + std::string{version};
  metadata.time_resolution = project.time_resolution;
  metadata.delay_mode = std::string{project::to_string(config.run.delay_mode)};
  metadata.optimization = std::string{project::to_string(project.optimization)};
  metadata.cache_key = project.cache_key;
  metadata.seed = project.seed;
  metadata.entropy_seed = project.entropy_seed;
  metadata.search_libraries = config.elaboration.search_libraries;
  for (const auto& root : config.project.tops) {
    metadata.roots.push_back({
        root.alias, root.target,
        selected_root_identity(project, root.alias, root.target)});
  }
  if (metadata.roots.empty() && !config.project.top.empty()) {
    const auto alias = project.design.roots().empty()
        ? std::string{"top"} : project.design.roots().front();
    metadata.roots.push_back({
        alias, config.project.top,
        selected_root_identity(project, alias, config.project.top)});
  }
  for (const auto& binding : config.bindings) {
    metadata.bindings.push_back(
        {binding.instance, binding.target, binding.resolver});
  }
  for (const auto& object : project.objects) {
    metadata.objects.push_back({
        object.metadata_digest, object.compilation_digest, object.language,
        object.standard, object.library, object.unit_checksums});
  }
  if (metadata.objects.empty()) {
    diagnostics.error(
        "FSIM-ART-0014",
        "standalone design publication requires explicit .fsimobj provenance");
    return false;
  }
  const auto add_payload = [&](
      const std::string_view kind,
      const std::filesystem::path& path,
      const std::string& bytes) {
    metadata.payloads.push_back({
        std::string{kind}, path,
        support::Sha256::hex(support::Sha256::digest(bytes))});
  };
  add_payload("runtime", "state/runtime.bin", *runtime);
  add_payload("semantics", "state/semantics.bin", *semantics);
  add_payload("design-ir", "state/design-ir.bin", *design_ir);
  metadata.specialization_cache_keys = project.specialization_cache_keys;
  metadata.unit_count = project.semantics.units().size();
  metadata.semantic_source_count = project.semantics.source_files().size();
  metadata.specialization_count = project.design.specializations().size();
  metadata.signal_count = project.design.signals().size();
  metadata.process_count = project.design.processes().size();
  metadata.design_digest = artifact::compute_design_digest(metadata);
  const std::vector<library::PortablePayload> payloads{
      {metadata.payloads[0].artifact, std::move(*runtime)},
      {metadata.payloads[1].artifact, std::move(*semantics)},
      {metadata.payloads[2].artifact, std::move(*design_ir)}};
  return artifact::publish_design(
      destination, metadata, payloads, diagnostics);
}

std::optional<BuiltProject> load_design_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics) {
  auto metadata = artifact::load_design_metadata(directory, diagnostics);
  if (!metadata) {
    return std::nullopt;
  }
  const auto* runtime_index = payload_by_kind(*metadata, "runtime");
  const auto* semantic_index = payload_by_kind(*metadata, "semantics");
  const auto* design_ir_index = payload_by_kind(*metadata, "design-ir");
  if (runtime_index == nullptr || semantic_index == nullptr
      || design_ir_index == nullptr) {
    diagnostics.error(
        "FSIM-ART-0014", ".fsimdesign is missing a required state payload");
    return std::nullopt;
  }
  auto runtime_bytes = read_design_payload(
      directory / runtime_index->artifact, runtime_index->checksum,
      diagnostics);
  auto semantic_bytes = read_design_payload(
      directory / semantic_index->artifact, semantic_index->checksum,
      diagnostics);
  auto design_ir_bytes = read_design_payload(
      directory / design_ir_index->artifact, design_ir_index->checksum,
      diagnostics);
  if (!runtime_bytes || !semantic_bytes || !design_ir_bytes) {
    return std::nullopt;
  }
  auto runtime = deserialize_runtime_state(
      *runtime_bytes, support::path_to_utf8(runtime_index->artifact),
      diagnostics);
  auto semantics = deserialize_semantic_state(
      *semantic_bytes, support::path_to_utf8(semantic_index->artifact),
      diagnostics);
  auto design_ir = deserialize_design_ir_state(
      *design_ir_bytes, support::path_to_utf8(design_ir_index->artifact),
      diagnostics);
  if (!runtime || !semantics || !design_ir
      || !design_ir->valid(*semantics)
      || !application_detail::valid_runtime_projection(*design_ir, *runtime)) {
    if (!diagnostics.has_error()) {
      diagnostics.error(
          "FSIM-ART-0014",
          ".fsimdesign state projections are inconsistent");
    }
    return std::nullopt;
  }
  std::vector<std::string> roots;
  roots.reserve(metadata->roots.size());
  for (const auto& root : metadata->roots) {
    roots.push_back(root.alias);
  }
  if (runtime->roots() != roots || design_ir->roots() != roots
      || semantics->units().size() != metadata->unit_count
      || semantics->source_files().size() != metadata->semantic_source_count
      || runtime->specializations().size() != metadata->specialization_count
      || runtime->signals().size() != metadata->signal_count
      || runtime->processes().size() != metadata->process_count) {
    diagnostics.error(
        "FSIM-ART-0014",
        ".fsimdesign metadata counts or roots disagree with state payloads");
    return std::nullopt;
  }
  std::vector<CheckedProject::ObjectProvenance> objects;
  objects.reserve(metadata->objects.size());
  for (const auto& object : metadata->objects) {
    CheckedProject::ObjectProvenance provenance;
    provenance.metadata_digest = object.metadata_digest;
    provenance.compilation_digest = object.compilation_digest;
    provenance.language = object.language;
    provenance.standard = object.standard;
    provenance.library = object.library;
    provenance.unit_checksums = object.unit_checksums;
    objects.push_back(std::move(provenance));
  }
  const auto optimization = metadata->optimization == "O0"
      ? project::Optimization::o0 : project::Optimization::o2;
  return BuiltProject{
      std::move(*runtime), std::move(*design_ir), std::move(*semantics),
      metadata->cache_key, metadata->time_resolution,
      directory.parent_path() / ".fsim-sim-cache", optimization,
      metadata->specialization_cache_keys, {}, {}, {}, metadata->seed,
      metadata->entropy_seed, false, directory.parent_path(), {}, {},
      std::move(objects), metadata->design_digest};
}

bool elaborate_artifact(
    const project::Config& config,
    const std::span<const std::filesystem::path> objects,
    const std::filesystem::path& destination,
    diagnostic::Engine& diagnostics) {
  auto built = build_objects(config, objects, diagnostics);
  return built
      && publish_design_artifact(
          config, *built, destination, diagnostics);
}

std::optional<ArtifactInspection> inspect_artifact(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics) {
  const auto object_metadata =
      std::filesystem::exists(directory / artifact::kObjectMetadataFilename);
  const auto design_metadata =
      std::filesystem::exists(directory / artifact::kDesignMetadataFilename);
  if (object_metadata == design_metadata) {
    diagnostics.error(
        "FSIM-ART-0014",
        "artifact inspection requires exactly one .fsimobj or .fsimdesign "
        "metadata record: " + support::path_to_utf8(directory));
    return std::nullopt;
  }
  ArtifactInspection result;
  result.compatible = true;
  if (object_metadata) {
    const auto metadata = artifact::load_object_metadata(
        directory, diagnostics);
    if (!metadata) {
      return std::nullopt;
    }
    result.phase = ArtifactPhaseKind::compilation;
    result.format = metadata->format;
    result.language = metadata->language;
    result.standard = metadata->standard;
    result.library = metadata->library;
    result.digests.push_back(metadata->compilation_digest);
    for (const auto& unit : metadata->units) {
      result.units.push_back(
          unit.language + ":" + metadata->library + "." + unit.name);
      result.digests.push_back(unit.checksum);
    }
    return result;
  }
  const auto metadata = artifact::load_design_metadata(directory, diagnostics);
  if (!metadata) {
    return std::nullopt;
  }
  result.phase = ArtifactPhaseKind::elaboration;
  result.format = metadata->format;
  result.runtime_abi = metadata->runtime_abi;
  result.digests = {metadata->design_digest, metadata->cache_key};
  result.process_count = metadata->process_count;
  for (const auto& root : metadata->roots) {
    result.roots.push_back(root.alias);
    result.units.push_back(root.selected_identity);
  }
  for (const auto& object : metadata->objects) {
    result.digests.push_back(object.metadata_digest);
    result.digests.push_back(object.compilation_digest);
    result.digests.insert(
        result.digests.end(), object.unit_checksums.begin(),
        object.unit_checksums.end());
  }
  for (const auto& payload : metadata->payloads) {
    result.digests.push_back(payload.checksum);
  }
  return result;
}

}  // namespace fsim::app

namespace fsim::app::application_detail {

int handle_elaborate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
  if (!invocation.artifact_output) {
    return 1;
  }
  auto built = build_objects(config, invocation.objects, diagnostics);
  if (!built || !publish_design_artifact(
          config, *built, *invocation.artifact_output, diagnostics)) {
    return 1;
  }
  output << "elaborated " << built->design.roots().size()
         << " root(s) into "
         << support::path_to_utf8(*invocation.artifact_output) << '\n';
  return 0;
}

int handle_simulate(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&) {
  if (!invocation.design) {
    return 1;
  }
  const auto metadata = artifact::load_design_metadata(
      *invocation.design, diagnostics);
  if (!metadata) {
    return 1;
  }
  if (invocation.delay_mode
      && metadata->delay_mode
          != project::to_string(*invocation.delay_mode)) {
    diagnostics.error(
        "FSIM-ART-0014",
        "requested delay mode '"
            + std::string{project::to_string(*invocation.delay_mode)}
            + "' does not match the elaborated .fsimdesign delay mode '"
            + metadata->delay_mode + "'");
    return 1;
  }
  auto built = load_design_artifact(*invocation.design, diagnostics);
  if (!built) {
    return 1;
  }
  if (invocation.cache_directory) {
    built->cache_path = *invocation.cache_directory;
  }
  if (invocation.file_root) {
    built->file_root = *invocation.file_root;
  }
  if (invocation.random_seed) {
    built->seed = entropy_seed();
    built->entropy_seed = true;
  } else if (invocation.seed) {
    built->seed = *invocation.seed;
    built->entropy_seed = false;
  }
  const auto engine = invocation.engine.value_or("compiled");
  const auto simulation_engine = engine == "interpreter"
      ? SimulationEngine::interpreter
      : engine == "debug" ? SimulationEngine::debug
                          : SimulationEngine::compiled;
  return run_built_project(
      std::move(*built), simulation_engine,
      config, diagnostics, output);
}

}  // namespace fsim::app::application_detail
