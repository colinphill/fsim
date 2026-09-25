// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/library/source_mapping.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <map>
#include <set>

namespace fsim::app {
namespace {

std::optional<std::string> read_object_payload(
    const std::filesystem::path& path,
    const std::string_view expected_checksum,
    diagnostic::Engine& diagnostics,
    const std::optional<std::uintmax_t> maximum_bytes = std::nullopt) {
  auto payload = application_detail::read_binary_payload(path, maximum_bytes);
  if (!payload.bytes) {
    const auto budget = payload.budget_exceeded
        ? " exceeds the compiled-HIR decode byte budget: "
        : ": ";
    diagnostics.error(
        "FSIM-ART-0005",
        (payload.failure == application_detail::BinaryPayloadReadFailure::open
                ? "cannot open .fsimobj payload"
                : "cannot read .fsimobj payload")
            + std::string { budget } + support::path_to_utf8(path));
    return std::nullopt;
  }
  if (support::Sha256::hex(support::Sha256::digest(*payload.bytes))
      != expected_checksum) {
    diagnostics.error(
        "FSIM-ART-0005",
        ".fsimobj payload checksum mismatch: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  return std::move(payload.bytes);
}

std::filesystem::path object_source_root(
    const artifact::ObjectMetadata& metadata) {
  return std::filesystem::path{"objects"} / metadata.compilation_digest;
}

}  // namespace

std::optional<application_detail::CompilationWorkspace>
application_detail::load_object_workspace(
    const std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics,
    const bool validate_uvm_surface) {
  if (objects.empty()) {
    diagnostics.error(
        "FSIM-ART-0005", "elaboration requires at least one .fsimobj input");
    return std::nullopt;
  }

  CompilationWorkspace checked;
  std::set<std::string> known_units;
  std::map<std::string, std::string> known_sources;
  auto selected_release = project::SystemVerilogUvmRelease::none;
  compiler::CacheKeyBuilder uvm_identity;
  std::vector<semantic::CompiledDesign> compiled_bundles;
  std::map<std::string, std::size_t> compiled_bundle_indexes;
  std::vector<library::VhdlPackageDependency> vhdl_dependency_identity;
  std::vector<std::string> vhdl_dependency_libraries;
  std::optional<std::size_t> vhdl_dependency_source;
  std::optional<std::string> vhdl_dependency_bundle_bytes;
  std::optional<std::string> vhdl_object_standard;
  std::size_t decoded_bundle_count { };
  uvm_identity.add("uvm-provenance-schema", "fsim-uvm-object-v1");
  for (const auto& object : objects) {
    auto metadata = artifact::load_object_metadata(object, diagnostics);
    if (!metadata.has_value()) {
      return std::nullopt;
    }
    if (metadata->compilation_digest
        != artifact::compute_object_compilation_digest(*metadata)) {
      diagnostics.error(
          "FSIM-ART-0005",
          ".fsimobj compilation digest mismatch: "
              + support::path_to_utf8(object));
      return std::nullopt;
    }
    if (!metadata->trace_archive.empty()) {
      const auto archive = trace_archive_from_hex(metadata->trace_archive);
      auto decoded = decode_trace_archive(
          archive, TraceArchiveKind::Object);
      if (archive.empty() || !decoded.ok()) {
        for (const auto& diagnostic : decoded.diagnostics)
          application_detail::import_diagnostic(diagnostics, diagnostic);
        if (!diagnostics.has_error()) {
          diagnostics.error("FSIM-TRACE-ARCHIVE-002",
              ".fsimobj trace profile transport is malformed");
        }
        return std::nullopt;
      }
      if (checked.trace_archive
          && !trace_archive_profiles_compatible(
              *checked.trace_archive, decoded.snapshot)) {
        diagnostics.error("FSIM-TRACE-ARCHIVE-003",
            ".fsimobj inputs contain incompatible trace formats or profiles");
        return std::nullopt;
      }
      checked.trace_archive = std::make_shared<const TraceArchiveSnapshot>(
          std::move(decoded.snapshot));
    }
    const auto object_release =
        project::parse_systemverilog_uvm_release(metadata->uvm_release);
    if (!object_release) {
      diagnostics.error(
          "FSIM-UVM-VERSION-001",
          ".fsimobj names an unsupported governed UVM release");
      return std::nullopt;
    }
    if (*object_release != project::SystemVerilogUvmRelease::none) {
      if (selected_release != project::SystemVerilogUvmRelease::none
          && selected_release != *object_release) {
        diagnostics.error(
            "FSIM-UVM-VERSION-001",
            "mixed governed UVM 1.2 and UVM 2020.3.1 objects are not "
            "transactionally compatible");
        return std::nullopt;
      }
      selected_release = *object_release;
    }
    uvm_identity.add("object-release", metadata->uvm_release);
    uvm_identity.add("object-compilation", metadata->compilation_digest);

    CheckedProject::ObjectProvenance provenance;
    provenance.directory = object;
    provenance.metadata_digest = support::Sha256::hex(
        support::Sha256::digest(
            artifact::serialize_object_metadata(*metadata)));
    provenance.compilation_digest = metadata->compilation_digest;
    provenance.language = metadata->language;
    provenance.standard = metadata->standard;
    provenance.compatibility_profile = metadata->compatibility_profile;
    provenance.library = metadata->library;
    provenance.code_coverage = metadata->code_coverage;
    provenance.vhdl_package_dependencies =
        metadata->vhdl_package_dependencies;
    const auto source_language = project::parse_language(metadata->language);
    if (!source_language.has_value()
        || *source_language == project::Language::systemc) {
      diagnostics.error(
          "FSIM-ART-0005",
          ".fsimobj metadata names an unsupported source language");
      return std::nullopt;
    }
    if (*source_language == project::Language::vhdl) {
      if (vhdl_object_standard
          && *vhdl_object_standard != metadata->standard) {
        diagnostics.error(
            "FSIM-ART-VHDEP-001",
            ".fsimobj inputs select incompatible compiler-supplied VHDL "
            "package environments; recompile every object with one VHDL "
            "standard and this fsim build");
        return std::nullopt;
      }
      vhdl_object_standard = metadata->standard;
    }
    provenance.source_settings.language = *source_language;
    provenance.source_settings.standard = metadata->standard;
    if ((*source_language == project::Language::verilog
            || *source_language == project::Language::system_verilog)
        && metadata->compatibility_profile != "none") {
      std::size_t begin = 0;
      while (begin <= metadata->compatibility_profile.size()) {
        const auto end = metadata->compatibility_profile.find(',', begin);
        const auto item = metadata->compatibility_profile.substr(
            begin, end == std::string::npos
                ? std::string::npos : end - begin);
        if (!item.empty()) {
          provenance.source_settings.compatibility_switches.push_back(item);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
      }
    }
    provenance.source_settings.library = metadata->library;
    provenance.source_settings.compilation_unit = metadata->compilation_unit;
    provenance.source_settings.uvm_release = *object_release;
    provenance.source_settings.defines = metadata->defines;

    const auto source_root = object_source_root(*metadata);
    std::vector<library::SourceNameMapping> source_mappings;
    source_mappings.reserve(metadata->sources.size());
    for (const auto& indexed : metadata->sources) {
      auto bytes = read_object_payload(
          object / indexed.artifact, indexed.checksum, diagnostics);
      if (!bytes.has_value()) {
        return std::nullopt;
      }
      const auto logical =
          (source_root / support::path_from_utf8(indexed.logical_name))
              .lexically_normal();
      const auto logical_name = support::path_to_utf8(logical);
      const auto [existing, inserted] =
          known_sources.emplace(logical_name, indexed.checksum);
      if (!inserted && existing->second != indexed.checksum) {
        diagnostics.error(
            "FSIM-ART-0005",
            "object source identity collides with different content: "
                + logical_name);
        return std::nullopt;
      }
      source_mappings.push_back({indexed.logical_name, logical_name});
      provenance.source_settings.files.push_back(logical);
      if (inserted) {
          checked.hdl_sources.push_back({ logical, metadata->language, metadata->standard,
              indexed.checksum, { }, metadata->compilation_digest,
              object / indexed.artifact });
      }
    }
    for (const auto& include : metadata->include_roots) {
      provenance.source_settings.include_directories.push_back(
          (source_root / include).lexically_normal());
    }

    auto compiled_bytes = read_object_payload(
        object / metadata->compiled_hir_artifact,
        metadata->compiled_hir_checksum,
        diagnostics, kCompiledHirDecodeBudgetBytes);
    if (!compiled_bytes) {
      return std::nullopt;
    }
    auto compiled_bundle = deserialize_compiled_hir_bundle(
        *compiled_bytes,
        support::path_to_utf8(object / metadata->compiled_hir_artifact),
        diagnostics);
    if (!compiled_bundle
        || !application_detail::relocate_compiled_design_sources(
            *compiled_bundle, source_mappings, diagnostics)) {
      return std::nullopt;
    }
    ++decoded_bundle_count;

    auto remaining_compiled_units = compiled_unit_metadata_entries(
        *compiled_bundle, metadata->library);
    std::set<std::string> indexed_udp_keys;
    std::set<std::string> indexed_class_identities;
    for (const auto& indexed : metadata->units) {
      if (indexed.kind == "primitive") {
        const auto* declaration = find_compiled_udp(
            *compiled_bundle, metadata->library, indexed.name);
        if (declaration == nullptr
            || !compiled_udp_metadata_matches(
                indexed, *declaration, metadata->library)) {
          diagnostics.error(
              "FSIM-ART-0005",
              ".fsimobj UDP identity does not match its metadata index: "
                  + support::path_to_utf8(
                      object / metadata->compiled_hir_artifact));
          return std::nullopt;
        }
        const auto key = compiled_udp_key(*declaration);
        indexed_udp_keys.insert(key);
        if (!known_units.insert(key).second) {
          diagnostics.error(
              "FSIM-ART-0005",
              "object UDP declaration collides with an earlier input: '"
                  + key + "'");
          return std::nullopt;
        }
        provenance.unit_checksums.push_back(
            metadata->compiled_hir_checksum);
        continue;
      }
      if (indexed.kind == "class") {
        const auto* declaration = find_compiled_class(
            *compiled_bundle, metadata->library, indexed.name);
        if (declaration == nullptr
            || !compiled_class_metadata_matches(
                indexed, *compiled_bundle, *declaration,
                metadata->library)) {
          diagnostics.error(
              "FSIM-ART-0005",
              ".fsimobj class identity does not match its compiled-HIR index");
          return std::nullopt;
        }
        if (!indexed_class_identities.insert(indexed.name).second
            || !known_units.insert("class:" + indexed.name).second) {
          diagnostics.error(
              "FSIM-ART-0005",
              "object class declaration collides with an earlier input: '"
                  + indexed.name + "'");
          return std::nullopt;
        }
        provenance.unit_checksums.push_back(
            metadata->compiled_hir_checksum);
        continue;
      }
      const auto expected = std::ranges::find(
          remaining_compiled_units, indexed);
      if (expected == remaining_compiled_units.end()) {
        diagnostics.error(
            "FSIM-ART-0005",
            ".fsimobj unit identity does not match its compiled-HIR index");
        return std::nullopt;
      }
      const auto key = compiled_unit_metadata_key(
          indexed, metadata->library);
      if (!known_units.insert(key).second) {
        diagnostics.error(
            "FSIM-ART-0005",
            "object design unit collides with an earlier input: '"
                + key + "'");
        return std::nullopt;
      }
      remaining_compiled_units.erase(expected);
      provenance.unit_checksums.push_back(metadata->compiled_hir_checksum);
    }
    if (!remaining_compiled_units.empty()) {
      diagnostics.error(
          "FSIM-ART-0005",
          ".fsimobj compiled-HIR unit is missing from its metadata index");
      return std::nullopt;
    }
    for (const auto& declaration
        : compiled_bundle->systemverilog_hir.udps()) {
      const auto library = declaration.library.empty()
          ? std::string_view { "work" }
          : std::string_view { declaration.library };
      if (library == metadata->library
          && !indexed_udp_keys.contains(compiled_udp_key(declaration))) {
        diagnostics.error(
            "FSIM-ART-0005",
            ".fsimobj UDP identity does not match its metadata index: "
                + support::path_to_utf8(
                    object / metadata->compiled_hir_artifact));
        return std::nullopt;
      }
    }
    for (const auto& declaration
        : compiled_bundle->systemverilog_hir.classes()) {
      const auto* owner = compiled_class_owner(
          *compiled_bundle, declaration);
      const auto library = owner == nullptr || owner->library.empty()
          ? std::string_view { "work" }
          : std::string_view { owner->library };
      if (library == metadata->library
          && !indexed_class_identities.contains(
              semantic::sv::class_declaration_identity(declaration))) {
        diagnostics.error(
            "FSIM-ART-0005",
            ".fsimobj class identity is missing from its compiled-HIR index");
        return std::nullopt;
      }
    }
    std::vector<std::string> selected_libraries { metadata->library };
    bool first_vhdl_dependency_environment { false };
    if (!metadata->vhdl_package_dependencies.empty()) {
      if (!application_detail::validate_vhdl_package_dependencies(
              metadata->vhdl_package_dependencies,
              ".fsimobj", diagnostics)
          || !application_detail::compiled_vhdl_package_dependencies_match(
              *compiled_bundle, metadata->vhdl_package_dependencies,
              ".fsimobj", diagnostics)) {
        return std::nullopt;
      }
      std::vector<std::string> dependency_libraries;
      for (const auto& dependency
          : metadata->vhdl_package_dependencies) {
        const auto separator = dependency.package.find('.');
        const auto library = dependency.package.substr(0, separator);
        if (library != metadata->library
            && std::ranges::find(dependency_libraries, library)
                == dependency_libraries.end()) {
          dependency_libraries.push_back(library);
        }
      }
      first_vhdl_dependency_environment
          = vhdl_dependency_identity.empty();
      if (!first_vhdl_dependency_environment
          && metadata->vhdl_package_dependencies
              != vhdl_dependency_identity) {
        diagnostics.error(
            "FSIM-ART-VHDEP-001",
            ".fsimobj inputs select incompatible compiler-supplied VHDL "
            "package environments; recompile every object with one VHDL "
            "standard and this fsim build");
        return std::nullopt;
      }
      if (first_vhdl_dependency_environment) {
        vhdl_dependency_identity = metadata->vhdl_package_dependencies;
        vhdl_dependency_libraries = dependency_libraries;
        selected_libraries.insert(selected_libraries.end(),
            dependency_libraries.begin(), dependency_libraries.end());
      } else {
        if (!vhdl_dependency_bundle_bytes) {
          auto baseline_dependencies
              = semantic::extract_compiled_libraries(
                  compiled_bundles[*vhdl_dependency_source],
                  vhdl_dependency_libraries);
          if (!baseline_dependencies.ok()) {
            diagnostics.error(
                "FSIM-ART-0005",
                "cannot project .fsimobj compiler dependency HIR: "
                    + baseline_dependencies.error);
            return std::nullopt;
          }
          vhdl_dependency_bundle_bytes = serialize_compiled_hir_bundle(
              *baseline_dependencies.design, diagnostics);
          if (!vhdl_dependency_bundle_bytes) {
            return std::nullopt;
          }
        }
        auto dependencies = semantic::extract_compiled_libraries(
            *compiled_bundle, dependency_libraries);
        if (!dependencies.ok()) {
          diagnostics.error(
              "FSIM-ART-0005",
              "cannot project .fsimobj compiler dependency HIR: "
                  + dependencies.error);
          return std::nullopt;
        }
        auto dependency_bytes = serialize_compiled_hir_bundle(
            *dependencies.design, diagnostics);
        if (!dependency_bytes) {
          return std::nullopt;
        }
        if (*dependency_bytes != *vhdl_dependency_bundle_bytes) {
          diagnostics.error(
              "FSIM-ART-VHDEP-001",
              ".fsimobj inputs select incompatible compiler-supplied VHDL "
              "package environments; recompile every object with one VHDL "
              "standard and this fsim build");
          return std::nullopt;
        }
      }
    }
    auto selected_bundle = semantic::extract_compiled_libraries(
        std::move(*compiled_bundle), selected_libraries);
    if (!selected_bundle.ok()) {
      diagnostics.error(
          "FSIM-ART-0005",
          "cannot project .fsimobj primary library HIR: "
              + selected_bundle.error);
      return std::nullopt;
    }
    const auto [bundle, inserted] = compiled_bundle_indexes.try_emplace(
        metadata->compiled_hir_checksum, compiled_bundles.size());
    if (inserted) {
      compiled_bundles.push_back(std::move(*selected_bundle.design));
    }
    if (first_vhdl_dependency_environment) {
      vhdl_dependency_source = bundle->second;
    }
    checked.objects.push_back(std::move(provenance));
  }

  checked.source_count = checked.hdl_sources.size();
  if (decoded_bundle_count != objects.size()) {
    diagnostics.error(
        "FSIM-ART-0005",
        "each .fsimobj input must contain exactly one compiled-HIR bundle");
    return std::nullopt;
  }
  const auto linked = install_linked_compiled_design(
      checked, std::move(compiled_bundles));
  if (!linked.ok()) {
    diagnostics.error(
        linked.diagnostic_code.empty()
            ? "FSIM-ART-0005"
            : linked.diagnostic_code,
        "cannot link .fsimobj compiled-HIR bundles: " + linked.error);
    return std::nullopt;
  }
  if (!checked.semantics.valid()) {
    diagnostics.error(
        "FSIM-ART-0005",
        "loaded objects produced an invalid owning semantic projection");
    return std::nullopt;
  }
  application_detail::install_compiled_class_specializations(checked);
  if (selected_release != project::SystemVerilogUvmRelease::none) {
    const auto has_class = [&](const std::string_view suffix) {
      return std::ranges::any_of(
          checked.compiled_systemverilog_class_specializations,
          [&](const auto& specialization) {
            return specialization.declaration_identity.ends_with(suffix);
          });
    };
    const bool has_uvm_object = has_class("::uvm_object");
    const bool has_ieee_policy = has_class("::uvm_policy");
    const auto compatibility
        = project::systemverilog_uvm_compatibility(selected_release);
      if (validate_uvm_surface && (!has_uvm_object
          || has_ieee_policy != compatibility.ieee_policy_classes)) {
      diagnostics.error(
          "FSIM-UVM-VERSION-002",
          "governed UVM object release does not match its compiled-HIR "
          "uvm_pkg API surface");
      return std::nullopt;
    }
    checked.systemverilog_uvm_provenance.release = selected_release;
    checked.systemverilog_uvm_provenance.source_identity
        = uvm_identity.finish();
  }
  return checked;
}

std::optional<CheckedProject> load_objects(
    const std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics)
{
  auto workspace = application_detail::load_object_workspace(
      objects, diagnostics);
  if (!workspace) {
    return std::nullopt;
  }
  return application_detail::release_compiled_project(
      std::move(*workspace));
}

}  // namespace fsim::app
