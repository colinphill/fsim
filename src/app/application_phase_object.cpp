// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/artifact/object.hpp"
#include "fsim/frontend/class_inheritance.hpp"
#include "fsim/frontend/class_resolution.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fsim::app {
namespace {

std::optional<std::string> read_object_payload(
    const std::filesystem::path& path,
    const std::string_view expected_checksum,
    diagnostic::Engine& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.error(
        "FSIM-ART-0005",
        "cannot open .fsimobj payload: " + support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    diagnostics.error(
        "FSIM-ART-0005",
        "cannot read .fsimobj payload: " + support::path_to_utf8(path));
    return std::nullopt;
  }
  auto bytes = contents.str();
  if (support::Sha256::hex(support::Sha256::digest(bytes))
      != expected_checksum) {
    diagnostics.error(
        "FSIM-ART-0005",
        ".fsimobj payload checksum mismatch: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  return bytes;
}

std::string expected_language(const frontend::Language language) {
  switch (language) {
    case frontend::Language::Vhdl2008:
      return "vhdl";
    case frontend::Language::Verilog2005:
      return "verilog";
    case frontend::Language::SystemVerilog2017:
      return "systemverilog";
  }
  return {};
}

std::string expected_kind(const frontend::UnitKind kind) {
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
  }
  return {};
}

bool identity_matches(
    const artifact::ObjectMetadata& metadata,
    const library::UnitIndexEntry& indexed,
    const frontend::DesignUnit& unit) {
  const auto architecture =
      unit.kind == frontend::UnitKind::VhdlArchitecture
      ? unit.name : std::string{};
  return unit.library == metadata.library
      && indexed.language == metadata.language
      && indexed.language == expected_language(unit.language)
      && indexed.kind == expected_kind(unit.kind)
      && indexed.name == unit.name
      && indexed.primary_name == unit.primary_name
      && indexed.architecture == architecture;
}

bool identity_matches(
    const artifact::ObjectMetadata& metadata,
    const library::UnitIndexEntry& indexed,
    const frontend::VerilogUdpDeclaration& declaration) {
  return declaration.library == metadata.library
      && indexed.language == metadata.language
      && indexed.language == expected_language(declaration.language)
      && indexed.kind == "primitive"
      && indexed.name == declaration.name
      && indexed.primary_name.empty()
      && indexed.architecture.empty();
}

std::filesystem::path object_source_root(
    const artifact::ObjectMetadata& metadata) {
  return std::filesystem::path{"objects"} / metadata.compilation_digest;
}

}  // namespace

std::optional<CheckedProject> load_objects(
    const std::span<const std::filesystem::path> objects,
    diagnostic::Engine& diagnostics) {
  if (objects.empty()) {
    diagnostics.error(
        "FSIM-ART-0005", "elaboration requires at least one .fsimobj input");
    return std::nullopt;
  }

  CheckedProject checked;
  std::set<std::string> known_units;
  std::map<std::string, std::string> known_sources;
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

    CheckedProject::ObjectProvenance provenance;
    provenance.directory = object;
    provenance.metadata_digest = support::Sha256::hex(
        support::Sha256::digest(
            artifact::serialize_object_metadata(*metadata)));
    provenance.compilation_digest = metadata->compilation_digest;
    provenance.language = metadata->language;
    provenance.standard = metadata->standard;
    provenance.library = metadata->library;
    const auto source_language = project::parse_language(metadata->language);
    if (!source_language.has_value()
        || *source_language == project::Language::systemc) {
      diagnostics.error(
          "FSIM-ART-0005",
          ".fsimobj metadata names an unsupported source language");
      return std::nullopt;
    }
    provenance.source_settings.language = *source_language;
    provenance.source_settings.standard = metadata->standard;
    provenance.source_settings.library = metadata->library;
    provenance.source_settings.compilation_unit = metadata->compilation_unit;
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
        checked.hdl_sources.push_back({
            logical, indexed.checksum, {}, metadata->compilation_digest,
            object / indexed.artifact});
      }
    }
    for (const auto& include : metadata->include_roots) {
      provenance.source_settings.include_directories.push_back(
          (source_root / include).lexically_normal());
    }

    for (const auto& indexed : metadata->units) {
      auto bytes = read_object_payload(
          object / indexed.artifact, indexed.checksum, diagnostics);
      if (!bytes.has_value()) {
        return std::nullopt;
      }
      if (indexed.kind == "class-unit") {
        auto unit = library::deserialize_portable_class_unit(
            *bytes,
            support::path_to_utf8(object / indexed.artifact),
            diagnostics);
        if (!unit || unit->library != metadata->library
            || indexed.language != "systemverilog"
            || indexed.name != unit->compilation_unit_identity) {
          diagnostics.error(
              "FSIM-ART-0005",
              ".fsimobj class-unit identity does not match its index");
          return std::nullopt;
        }
        if (!library::relocate_class_unit_sources(
                *unit, source_mappings, diagnostics)) {
          return std::nullopt;
        }
        for (auto& declaration : unit->declarations) {
          const auto key = "class:" + declaration.canonical_identity;
          if (!known_units.insert(key).second) {
            diagnostics.error(
                "FSIM-ART-0005",
                "object class declaration collides with an earlier input: '"
                    + key + "'");
            return std::nullopt;
          }
          checked.parsed.systemverilog_classes.push_back(
              std::move(declaration));
        }
        for (auto& method : unit->method_definitions) {
          checked.parsed.systemverilog_class_method_definitions.push_back(
              std::move(method));
        }
        provenance.unit_checksums.push_back(indexed.checksum);
        continue;
      }
      if (indexed.kind == "primitive") {
        auto declaration = library::deserialize_portable_udp(
            *bytes,
            support::path_to_utf8(object / indexed.artifact),
            diagnostics);
        if (!declaration.has_value()
            || !identity_matches(*metadata, indexed, *declaration)) {
          if (declaration.has_value()) {
            diagnostics.error(
                "FSIM-ART-0005",
                ".fsimobj UDP identity does not match its metadata index: "
                    + support::path_to_utf8(object / indexed.artifact));
          }
          return std::nullopt;
        }
        if (!library::relocate_udp_sources(
                *declaration, source_mappings, diagnostics)) {
          return std::nullopt;
        }
        const auto key = "udp:"
            + (declaration->library.empty()
                   ? std::string{"work"} : declaration->library)
            + "." + declaration->name;
        if (!known_units.insert(key).second) {
          diagnostics.error(
              "FSIM-ART-0005",
              "object UDP declaration collides with an earlier input: '"
                  + key + "'");
          return std::nullopt;
        }
        provenance.unit_checksums.push_back(indexed.checksum);
        checked.parsed.udp_declarations.push_back(
            std::move(*declaration));
        continue;
      }
      auto unit = library::deserialize_portable_unit(
          *bytes, support::path_to_utf8(object / indexed.artifact),
          diagnostics);
      if (!unit.has_value()) {
        return std::nullopt;
      }
      if (!identity_matches(*metadata, indexed, *unit)) {
        diagnostics.error(
            "FSIM-ART-0005",
            ".fsimobj unit identity does not match its metadata index: "
                + support::path_to_utf8(object / indexed.artifact));
        return std::nullopt;
      }
      if (!library::relocate_unit_sources(
              *unit, source_mappings, diagnostics)) {
        return std::nullopt;
      }
      const auto key = application_detail::unit_key(*unit);
      if (!known_units.insert(key).second) {
        diagnostics.error(
            "FSIM-ART-0005",
            "object design unit collides with an earlier input: '"
                + key + "'");
        return std::nullopt;
      }
      provenance.unit_checksums.push_back(indexed.checksum);
      checked.parsed.units.push_back(std::move(*unit));
    }
    checked.objects.push_back(std::move(provenance));
  }

  checked.source_count = checked.hdl_sources.size();
  application_detail::inject_vhdl_standard_libraries(checked, diagnostics);
  application_detail::validate_vhdl_analysis_order(
      checked.parsed.units, diagnostics);
  std::vector<frontend::Diagnostic> class_diagnostics;
  (void)frontend::resolve_systemverilog_classes(
      checked.parsed, class_diagnostics);
  for (const auto& diagnostic : class_diagnostics) {
    application_detail::import_diagnostic(diagnostics, diagnostic);
  }
  class_diagnostics.clear();
  (void)frontend::validate_systemverilog_class_inheritance(
      checked.parsed, class_diagnostics);
  for (const auto& diagnostic : class_diagnostics) {
    application_detail::import_diagnostic(diagnostics, diagnostic);
  }
  auto class_specializations =
      frontend::specialize_systemverilog_classes(checked.parsed);
  for (const auto& diagnostic : class_specializations.diagnostics) {
    application_detail::import_diagnostic(diagnostics, diagnostic);
  }
  checked.systemverilog_class_specializations =
      std::move(class_specializations.specializations);
  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  checked.semantics = application_detail::build_semantic_model(
      checked.parsed, checked.hdl_sources, checked.systemc_sources,
      checked.standard_sources);
  checked.vhdl_hir = application_detail::build_vhdl_hir(
      checked.parsed, checked.semantics);
  checked.systemverilog_hir =
      application_detail::build_systemverilog_hir(
          checked.parsed,
          checked.semantics,
          checked.systemverilog_class_specializations);
  if (!checked.semantics.valid()) {
    diagnostics.error(
        "FSIM-ART-0005",
        "loaded objects produced an invalid owning semantic projection");
    return std::nullopt;
  }
  return checked;
}

}  // namespace fsim::app
