// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/library/artifact.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc_abi.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fsim::app::application_detail {
namespace {

std::optional<std::string> read_payload(
    const std::filesystem::path& path,
    const std::string_view expected_checksum,
    diagnostic::Engine& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.error(
        "FSIM-LIB-0008",
        "cannot open mapped library payload: " + support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    diagnostics.error(
        "FSIM-LIB-0008",
        "cannot read mapped library payload: " + support::path_to_utf8(path));
    return std::nullopt;
  }
  auto bytes = contents.str();
  if (support::Sha256::hex(support::Sha256::digest(bytes))
      != expected_checksum) {
    diagnostics.error(
        "FSIM-LIB-0008",
        "mapped library payload checksum mismatch: "
            + support::path_to_utf8(path));
    return std::nullopt;
  }
  return bytes;
}

bool body_has_instances(const frontend::GenerateBody& body);

bool regions_have_instances(
    const std::vector<frontend::GenerateRegion>& regions) {
  return std::ranges::any_of(regions, [](const auto& region) {
    if (body_has_instances(region.then_body)
        || body_has_instances(region.else_body)) {
      return true;
    }
    return std::ranges::any_of(
        region.alternatives,
        [](const auto& alternative) {
          return body_has_instances(alternative.body);
        });
  });
}

bool body_has_instances(const frontend::GenerateBody& body) {
  return !body.instances.empty() || regions_have_instances(body.generate_regions);
}

bool design_has_hierarchy_queries(const frontend::ParsedDesign& parsed) {
  return std::ranges::any_of(parsed.units, [](const auto& unit) {
    return !unit.instances.empty()
        || regions_have_instances(unit.generate_regions);
  });
}

std::optional<std::string> qualified_library(const std::string_view target) {
  const auto colon = target.find(':');
  if (colon == std::string_view::npos) {
    return std::nullopt;
  }
  auto remainder = target.substr(colon + 1);
  const auto open = remainder.find('(');
  if (open != std::string_view::npos) {
    remainder = remainder.substr(0, open);
  }
  const auto dot = remainder.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return std::nullopt;
  }
  return std::string{remainder.substr(0, dot)};
}

bool metadata_identity_matches(
    const library::UnitIndexEntry& entry,
    const frontend::DesignUnit& unit) {
  const auto language = unit.language == frontend::Language::Vhdl2008
      ? std::string_view{"vhdl"} : std::string_view{"systemverilog"};
  const auto kind = [&]() -> std::string_view {
    switch (unit.kind) {
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
      case frontend::UnitKind::VhdlPslVerificationUnit:
        return "psl-verification-unit";
      case frontend::UnitKind::SystemVerilogInterface:
        return "interface";
      case frontend::UnitKind::VerilogModule:
        return "module";
      case frontend::UnitKind::SystemVerilogProgram:
        return "program";
      case frontend::UnitKind::SystemVerilogConfiguration:
          return "configuration";
      case frontend::UnitKind::SystemVerilogBind:
          return "bind";
      }
    return "unit";
  }();
  const auto architecture =
      unit.kind == frontend::UnitKind::VhdlArchitecture
      ? std::string_view{unit.name} : std::string_view{};
  return entry.name == unit.name
      && entry.primary_name == unit.primary_name
      && entry.language == language && entry.kind == kind
      && entry.architecture == architecture;
}

bool metadata_identity_matches(
    const library::UnitIndexEntry& entry,
    const frontend::VerilogUdpDeclaration& declaration) {
  const auto language =
      declaration.language == frontend::Language::Verilog2005
      ? std::string_view{"verilog"}
      : std::string_view{"systemverilog"};
  return entry.name == declaration.name
      && entry.primary_name.empty() && entry.architecture.empty()
      && entry.language == language && entry.kind == "primitive";
}

#if defined(FSIM_HAS_LLVM)
std::string feature_identity(const std::vector<std::string>& features) {
  std::string result;
  for (const auto& feature : features) {
    if (!result.empty()) {
      result.push_back('\n');
    }
    result += feature;
  }
  return result;
}
#endif

void record_native_acceptance(
    CheckedProject::MappedLibrary& provenance,
    const std::string_view kind,
    const std::string_view fingerprint) {
  if (provenance.native_kind == kind
      && provenance.native_fingerprint == fingerprint) {
    return;
  }
  if (provenance.native_accepted) {
    provenance.native_kind += "+" + std::string{kind};
    provenance.native_fingerprint += ";" + std::string{fingerprint};
  } else {
    provenance.native_accepted = true;
    provenance.native_kind = kind;
    provenance.native_fingerprint = fingerprint;
  }
}

#if defined(FSIM_HAS_LLVM)
bool admit_llvm_artifact(
    const project::Config& config,
    const std::filesystem::path& directory,
    const library::NativeArtifact& native,
    CheckedProject::MappedLibrary& provenance,
    diagnostic::Engine& diagnostics) {
  const auto optimization = config.build.optimization == project::Optimization::o0
      ? compiler::JitOptimizationLevel::o0
      : compiler::JitOptimizationLevel::o2;
  const auto host = compiler::LlvmJit::native_host_identity(optimization);
  const auto expected_optimization =
      optimization == compiler::JitOptimizationLevel::o0 ? "O0" : "O2";
  if (native.runtime_abi != runtime_abi_version
      || native.llvm_version != host.llvm_version
      || native.target != host.target
      || native.data_layout != host.data_layout
      || native.cpu != host.cpu
      || native.features != feature_identity(host.features)
      || native.optimization != expected_optimization) {
    return true;
  }
  auto bytes = read_payload(
      directory / native.artifact, native.checksum, diagnostics);
  if (!bytes.has_value()) {
    return false;
  }
  auto cache_directory = config.build.cache_path;
  if (!cache_directory.is_absolute()) {
    cache_directory = config.base_directory / cache_directory;
  }
  compiler::ObjectCache cache{
      cache_directory / "llvm-native" / "llvm" / "objects"};
  const auto payload = std::as_bytes(
      std::span<const char>{bytes->data(), bytes->size()});
  std::error_code error;
  if (!cache.store(native.cache_key, payload, error)) {
    diagnostics.error(
        "FSIM-LIB-0008",
        "cannot admit mapped LLVM object into the consumer cache: "
            + error.message());
    return false;
  }
  record_native_acceptance(provenance, "llvm_object", host.fingerprint);
  return true;
}
#endif

bool admit_systemc_artifact(
    const project::Config& config,
    const std::filesystem::path& directory,
    const library::NativeArtifact& native,
    CheckedProject::MappedLibrary& provenance,
    diagnostic::Engine& diagnostics) {
  diagnostic::Engine fingerprint_diagnostics;
  const auto fingerprint = systemc::plugin_host_fingerprint(
      config.systemc, config.base_directory, fingerprint_diagnostics);
  if (!fingerprint.has_value()
      || native.runtime_abi != runtime_abi_version
      || native.systemc_abi != FSIM_SYSTEMC_ABI_VERSION
      || native.compiler_fingerprint != *fingerprint
      || native.target != target_name()
      || native.cpu != "compiler-default") {
    return true;
  }
  if (!read_payload(
          directory / native.artifact, native.checksum, diagnostics)) {
    return false;
  }
  provenance.systemc_plugin = directory / native.artifact;
  record_native_acceptance(
      provenance, "systemc_plugin", native.compiler_fingerprint);
  return true;
}

}  // namespace

bool load_required_mapped_libraries(
    const project::Config& config,
    CheckedProject& checked,
    diagnostic::Engine& diagnostics) {
  std::map<std::string, const project::LibraryMapping*> mappings;
  for (const auto& mapping : config.library_mappings) {
    mappings.emplace(mapping.library, &mapping);
  }
  if (mappings.empty()) {
    return true;
  }

  std::set<std::string> required;
  bool unqualified_root = false;
  for (const auto& top : config.project.tops) {
    if (const auto library_name = qualified_library(top.target)) {
      if (mappings.contains(*library_name)) {
        required.insert(*library_name);
      }
    } else {
      unqualified_root = true;
    }
  }
  if (config.project.tops.empty() && !config.project.top.empty()) {
    if (const auto library_name = qualified_library(config.project.top)) {
      if (mappings.contains(*library_name)) {
        required.insert(*library_name);
      }
    } else {
      unqualified_root = true;
    }
  }
  for (const auto& binding : config.bindings) {
    if (binding.target.has_value()) {
      if (const auto library_name = qualified_library(*binding.target);
          library_name && mappings.contains(*library_name)) {
        required.insert(*library_name);
      }
    }
  }
  const bool hierarchy_query = design_has_hierarchy_queries(checked.parsed)
      || std::ranges::any_of(
          config.source_sets,
          [](const auto& source_set) {
            return source_set.language == project::Language::systemc;
          });
  if (unqualified_root || hierarchy_query) {
    for (const auto& library_name : config.elaboration.search_libraries) {
      if (mappings.contains(library_name)) {
        required.insert(library_name);
      }
    }
  }
  for (const auto& unit : checked.parsed.units) {
    for (const auto& context : unit.vhdl_context) {
      for (const auto& selected : context.selected_names) {
        const auto dot = selected.find('.');
        const auto library_name = std::string{
            selected.substr(0, dot)};
        if (mappings.contains(library_name)) {
          required.insert(library_name);
        }
      }
    }
  }

  enum class State { loading, loaded };
  std::map<std::string, State> states;
  const auto load_one = [&](const auto& self, const std::string& library_name)
      -> bool {
    if (const auto state = states.find(library_name); state != states.end()) {
      if (state->second == State::loaded) {
        return true;
      }
      diagnostics.error(
          "FSIM-LIB-0008",
          "mapped library dependency cycle reaches '" + library_name + "'");
      return false;
    }
    const auto mapping = mappings.find(library_name);
    if (mapping == mappings.end()) {
      diagnostics.error(
          "FSIM-LIB-0008",
          "mapped library dependency '" + library_name
              + "' has no declared mapping");
      return false;
    }
    states.emplace(library_name, State::loading);
    auto metadata = library::load_metadata(
        mapping->second->path, library_name, diagnostics);
    if (!metadata.has_value()) {
      return false;
    }
    for (const auto& dependency : metadata->dependencies) {
      if (!self(self, dependency)) {
        return false;
      }
    }
    CheckedProject::MappedLibrary provenance;
    provenance.library = library_name;
    provenance.directory = mapping->second->path;
    provenance.metadata_digest = support::Sha256::hex(
        support::Sha256::digest(library::serialize_metadata(*metadata)));
    provenance.vhdl_package_dependencies =
        metadata->vhdl_package_dependencies;
    std::optional<frontend::VhdlStandard> mapped_vhdl_standard;
    for (const auto& standard : metadata->standards) {
        const auto language = project::parse_language(standard.language);
        if (!language.has_value()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped library metadata names unsupported language '"
                    + standard.language + "'");
            return false;
        }
        project::SourceSet settings;
        settings.language = *language;
        settings.standard = standard.revision;
        settings.library = library_name;
        settings.compilation_unit = "file";
        provenance.source_settings.push_back(std::move(settings));
        if (*language == project::Language::vhdl) {
            if (const auto selected = project::parse_vhdl_standard(standard.revision)) {
                mapped_vhdl_standard = frontend_vhdl_standard(*selected);
            }
        }
    }
    if (mapped_vhdl_standard) {
        for (const auto& owner : checked.parsed.units) {
            if (owner.language != frontend::Language::Vhdl2008) {
                continue;
            }
            for (const auto& context : owner.vhdl_context) {
                for (const auto& selected : context.selected_names) {
                    if (selected.starts_with(library_name + ".")
                        && owner.vhdl_standard != *mapped_vhdl_standard) {
                        diagnostics.error(
                            "FSIM-FE-VHORDER-011",
                            "mapped VHDL library '" + library_name
                                + "' was analyzed as "
                                + std::string { frontend::to_string(
                                    *mapped_vhdl_standard) }
                                + " but the owning source uses "
                                + std::string { frontend::to_string(
                                    owner.vhdl_standard) },
                            span(context.span));
                    }
                }
            }
        }
    }
    for (const auto& source_entry : metadata->sources) {
      if (source_entry.artifact.empty()) {
        continue;
      }
      auto source_bytes = read_payload(
          mapping->second->path / source_entry.artifact,
          source_entry.checksum,
          diagnostics);
      if (!source_bytes.has_value()) {
        return false;
      }
      const auto logical_path =
          support::path_from_utf8(source_entry.logical_name);
      std::string source_standard;
      for (const auto& standard : metadata->standards) {
          if (standard.language == source_entry.language) {
              source_standard = standard.revision;
              break;
          }
      }
      CheckedSource checked_source {
          logical_path, source_entry.language, std::move(source_standard), source_entry.checksum, { }, source_entry.checksum,
          mapping->second->path / source_entry.artifact
      };
      if (source_entry.language == "systemc") {
          provenance.systemc_sources.push_back(
              mapping->second->path / source_entry.artifact);
          checked.systemc_sources.push_back(std::move(checked_source));
      } else {
          checked.hdl_sources.push_back(std::move(checked_source));
      }
      for (auto& settings : provenance.source_settings) {
          if (source_entry.language.empty()
              || project::to_string(settings.language)
                  == source_entry.language) {
              settings.files.push_back(logical_path);
          }
      }
    }
    for (const auto& entry : metadata->units) {
      auto bytes = read_payload(
          mapping->second->path / entry.artifact,
          entry.checksum,
          diagnostics);
      if (!bytes.has_value()) {
        return false;
      }
      if (entry.kind == "class-unit") {
        auto unit = library::deserialize_portable_class_unit(
            *bytes,
            support::path_to_utf8(
                mapping->second->path / entry.artifact),
            diagnostics);
        if (!unit || unit->library != library_name
            || entry.language != "systemverilog"
            || entry.name != unit->compilation_unit_identity) {
          diagnostics.error(
              "FSIM-LIB-0008",
              "mapped class-unit identity does not match its metadata index");
          return false;
        }
        for (auto& declaration : unit->declarations) {
          const auto duplicate = std::ranges::find(
              checked.parsed.systemverilog_classes,
              declaration.canonical_identity,
              &frontend::SystemVerilogClassDeclaration::canonical_identity);
          if (duplicate != checked.parsed.systemverilog_classes.end()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped class collides with an already loaded declaration '"
                    + declaration.canonical_identity + "'");
            return false;
          }
          checked.parsed.systemverilog_classes.push_back(
              std::move(declaration));
        }
        for (auto& method : unit->method_definitions) {
          checked.parsed.systemverilog_class_method_definitions.push_back(
              std::move(method));
        }
        provenance.unit_checksums.push_back(entry.checksum);
        continue;
      }
      if (entry.kind == "primitive") {
        auto declaration = library::deserialize_portable_udp(
            *bytes,
            support::path_to_utf8(
                mapping->second->path / entry.artifact),
            diagnostics);
        if (!declaration.has_value()
            || declaration->library != library_name
            || !metadata_identity_matches(entry, *declaration)) {
          if (declaration.has_value()) {
            diagnostics.error(
                "FSIM-LIB-0008",
                "mapped UDP identity does not match its metadata index");
          }
          return false;
        }
        const auto duplicate = std::ranges::find_if(
            checked.parsed.udp_declarations,
            [&](const auto& existing) {
              const auto existing_library = existing.library.empty()
                  ? std::string_view{"work"}
                  : std::string_view{existing.library};
              return existing_library == library_name
                  && existing.name == declaration->name;
            });
        if (duplicate != checked.parsed.udp_declarations.end()) {
          diagnostics.error(
              "FSIM-LIB-0008",
              "mapped UDP collides with an already loaded declaration 'udp:"
                  + library_name + "." + declaration->name + "'");
          return false;
        }
        provenance.unit_checksums.push_back(entry.checksum);
        checked.parsed.udp_declarations.push_back(
            std::move(*declaration));
        continue;
      }
      auto unit = library::deserialize_portable_unit(
          *bytes,
          support::path_to_utf8(mapping->second->path / entry.artifact),
          diagnostics);
      if (!unit.has_value() || unit->library != library_name
          || !metadata_identity_matches(entry, *unit)) {
        if (unit.has_value()) {
          diagnostics.error(
              "FSIM-LIB-0008",
              "mapped unit identity does not match its metadata index");
        }
        return false;
      }
      if (unit->language == frontend::Language::Vhdl2008
          && mapped_vhdl_standard) {
          unit->vhdl_standard = *mapped_vhdl_standard;
          unit->vhdl_compatibility_profile = vhdl_compatibility_profile();
      }
      const auto duplicate = std::ranges::find_if(
          checked.parsed.units,
          [&](const auto& existing) {
              return unit_key(existing) == unit_key(*unit);
          });
      if (duplicate != checked.parsed.units.end()) {
        diagnostics.error(
            "FSIM-LIB-0008",
            "mapped unit collides with an already loaded design unit '"
                + unit_key(*unit) + "'");
        return false;
      }
      provenance.unit_checksums.push_back(entry.checksum);
      checked.parsed.units.push_back(std::move(*unit));
    }
    for (const auto& native : metadata->native_artifacts) {
      if (native.kind == "systemc_plugin") {
        if (!admit_systemc_artifact(
                config, mapping->second->path, native, provenance,
                diagnostics)) {
          return false;
        }
      } else if (native.kind == "llvm_object") {
#if defined(FSIM_HAS_LLVM)
        if (!admit_llvm_artifact(
                config, mapping->second->path, native, provenance,
                diagnostics)) {
          return false;
        }
#endif
      }
    }
    checked.mapped_libraries.push_back(std::move(provenance));
    states[library_name] = State::loaded;
    return true;
  };

  for (const auto& library_name : required) {
    if (!load_one(load_one, library_name)) {
      return false;
    }
  }
  return true;
}

}  // namespace fsim::app::application_detail
