// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/design.hpp"

#include "../diagnostic/artifact_identity.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <span>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fsim::artifact {
namespace {

constexpr std::array<char, 8> kMagic{'F', 'S', 'I', 'M', 'D', 'E', 'S', '\0'};
constexpr std::string_view kSchemaCode = "FSIM-ART-0010";
constexpr std::string_view kValueCode = "FSIM-ART-0011";
constexpr std::string_view kIoCode = "FSIM-ART-0012";
std::atomic_uint64_t staging_sequence{};

void report(
    diagnostic::Engine& diagnostics,
    const std::string_view code,
    std::string message,
    const std::string& source = {}) {
  diagnostic::SourceSpan span;
  span.path = source;
  span.begin = {1, 1, 0};
  span.end = span.begin;
  diagnostics.error(std::string{code}, std::move(message), std::move(span));
}

bool checksum_spelling(const std::string_view value) {
  return value.size() == 64
      && std::ranges::all_of(value, [](const unsigned char character) {
           return (character >= '0' && character <= '9')
               || (character >= 'a' && character <= 'f');
         });
}

bool safe_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_name()
      || path.has_root_directory() || path.lexically_normal() != path) {
    return false;
  }
  return std::ranges::none_of(path, [](const auto& component) {
    return component == "." || component == "..";
  });
}

bool safe_name(const std::string_view value) {
  if (value.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(value.front());
  return (std::isalpha(first) != 0 || first == '_')
      && std::ranges::all_of(
          value.substr(1), [](const unsigned char character) {
            return std::isalnum(character) != 0 || character == '_';
          });
}

class Writer {
 public:
  void raw(const std::span<const char> value) {
    output_.append(value.data(), value.size());
  }
  void u32(const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      output_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
  }
  void u64(const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      output_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
  }
  void boolean(const bool value) { output_.push_back(value ? '\1' : '\0'); }
  void string(const std::string_view value) {
    u64(value.size());
    output_.append(value);
  }
  void optional_string(const std::optional<std::string>& value) {
    boolean(value.has_value());
    if (value) {
      string(*value);
    }
  }
  void path(const std::filesystem::path& value) {
    string(support::path_to_utf8(value));
  }
  template <typename Value, typename Callback>
  void sequence(const std::vector<Value>& values, Callback&& callback) {
    u64(values.size());
    for (const auto& value : values) {
      callback(value);
    }
  }
  std::string take() && { return std::move(output_); }

 private:
  std::string output_;
};

class Reader {
 public:
  explicit Reader(const std::string_view input) : input_(input) {}
  bool raw(const std::span<const char> expected) {
    if (remaining() < expected.size()
        || input_.substr(position_, expected.size())
            != std::string_view{expected.data(), expected.size()}) {
      return false;
    }
    position_ += expected.size();
    return true;
  }
  std::optional<std::uint32_t> u32() {
    if (remaining() < 4) {
      return std::nullopt;
    }
    std::uint32_t value{};
    for (unsigned shift = 0; shift < 32; shift += 8) {
      value |= static_cast<std::uint32_t>(
          static_cast<unsigned char>(input_[position_++])) << shift;
    }
    return value;
  }
  std::optional<std::uint64_t> u64() {
    if (remaining() < 8) {
      return std::nullopt;
    }
    std::uint64_t value{};
    for (unsigned shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(
          static_cast<unsigned char>(input_[position_++])) << shift;
    }
    return value;
  }
  std::optional<bool> boolean() {
    if (remaining() == 0
        || static_cast<unsigned char>(input_[position_]) > 1) {
      return std::nullopt;
    }
    return input_[position_++] != 0;
  }
  std::optional<std::string> string() {
    const auto size = u64();
    if (!size || *size > remaining()
        || *size > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    std::string value{
        input_.substr(position_, static_cast<std::size_t>(*size))};
    position_ += static_cast<std::size_t>(*size);
    return value;
  }
  std::optional<std::optional<std::string>> optional_string() {
    const auto present = boolean();
    if (!present) {
      return std::nullopt;
    }
    if (!*present) {
      return std::optional<std::string>{};
    }
    auto value = string();
    return value ? std::optional<std::optional<std::string>>{
                       std::move(*value)}
                 : std::nullopt;
  }
  std::optional<std::filesystem::path> path() {
    auto value = string();
    return value ? std::optional{support::path_from_utf8(*value)}
                 : std::nullopt;
  }
  std::optional<std::size_t> count() {
    const auto value = u64();
    if (!value || *value > remaining()
        || *value > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
  }
  std::size_t remaining() const noexcept { return input_.size() - position_; }

 private:
  std::string_view input_;
  std::size_t position_{};
};

void write_sdf_annotations(
    Writer& writer, const std::vector<DesignSdfAnnotation>& annotations) {
  writer.sequence(annotations, [&](const auto& annotation) {
    writer.u32(annotation.schema);
    writer.string(annotation.revision);
    writer.string(annotation.revision_adapter);
    writer.boolean(annotation.has_timescale);
    writer.string(annotation.timescale);
    writer.string(annotation.selection_policy);
    writer.string(annotation.scope_identity);
    writer.string(annotation.source_digest);
    writer.string(annotation.design_digest);
    writer.string(annotation.ir_identity);
    writer.string(annotation.resolution_identity);
    writer.string(annotation.mapping_identity);
    writer.sequence(annotation.selected_root_identities,
        [&](const auto& identity) { writer.string(identity); });
    writer.sequence(annotation.semantic_unit_identities,
        [&](const auto& identity) { writer.string(identity); });
    writer.sequence(annotation.semantic_object_identities,
        [&](const auto& identity) { writer.string(identity); });
    writer.string(annotation.cache_key);
  });
}

template <typename Callback>
bool read_sequence(Reader& reader, Callback&& callback) {
  const auto count = reader.count();
  if (!count) {
    return false;
  }
  for (std::size_t index = 0; index < *count; ++index) {
    if (!callback()) {
      return false;
    }
  }
  return true;
}

bool read_sdf_annotations(Reader& reader, DesignMetadata& metadata) {
  const auto read_string = [&](std::string& value) {
    auto read = reader.string();
    if (!read) {
      return false;
    }
    value = std::move(*read);
    return true;
  };
  return read_sequence(reader, [&] {
    DesignSdfAnnotation annotation;
    const auto schema = reader.u32();
    if (!schema || !read_string(annotation.revision)
        || !read_string(annotation.revision_adapter)) {
      return false;
    }
    const auto has_timescale = reader.boolean();
    if (!has_timescale || !read_string(annotation.timescale)
        || !read_string(annotation.selection_policy)
        || !read_string(annotation.scope_identity)
        || !read_string(annotation.source_digest)
        || !read_string(annotation.design_digest)
        || !read_string(annotation.ir_identity)
        || !read_string(annotation.resolution_identity)
        || !read_string(annotation.mapping_identity)) {
      return false;
    }
    annotation.schema = *schema;
    annotation.has_timescale = *has_timescale;
    const auto read_identities = [&](auto& identities) {
      return read_sequence(reader, [&] {
        auto identity = reader.string();
        if (identity) {
          identities.push_back(std::move(*identity));
        }
        return identity.has_value();
      });
    };
    if (!read_identities(annotation.selected_root_identities)
        || !read_identities(annotation.semantic_unit_identities)
        || !read_identities(annotation.semantic_object_identities)
        || !read_string(annotation.cache_key)) {
      return false;
    }
    metadata.sdf_annotations.push_back(std::move(annotation));
    return true;
  });
}

bool valid_ordered_identities(const std::vector<std::string>& values) {
  return !values.empty() && std::ranges::is_sorted(values)
      && std::adjacent_find(values.begin(), values.end()) == values.end()
      && std::ranges::none_of(
          values, [](const auto& value) { return value.empty(); });
}

void validate_sdf_annotations(
    const DesignMetadata& metadata,
    diagnostic::Engine& diagnostics,
    const std::string& source) {
  std::set<std::string> cache_keys;
  for (const auto& annotation : metadata.sdf_annotations) {
      if (annotation.schema != 1
          || (annotation.revision != "2.1" && annotation.revision != "3.0"
              && annotation.revision != "4.0")
          || annotation.revision_adapter.empty()
          || (annotation.has_timescale != !annotation.timescale.empty())
          || (annotation.selection_policy != "min"
              && annotation.selection_policy != "typ"
              && annotation.selection_policy != "max")
          || annotation.scope_identity.empty()
          || !checksum_spelling(annotation.source_digest)
          || !checksum_spelling(annotation.design_digest)
          || annotation.ir_identity.empty()
          || annotation.resolution_identity.empty()
          || annotation.mapping_identity.empty()
          || !valid_ordered_identities(annotation.selected_root_identities)
          || !valid_ordered_identities(annotation.semantic_unit_identities)
          || !valid_ordered_identities(annotation.semantic_object_identities)
          || !checksum_spelling(annotation.cache_key)
          || !cache_keys.insert(annotation.cache_key).second) {
          report(
              diagnostics, kValueCode,
              "design SDF annotations require complete ordered portable semantic "
              "identities, exact policy, and unique content cache keys",
              source);
      }
  }
}

void write_digest_fields(Writer& writer, const DesignMetadata& metadata) {
    writer.string("fsim-design-provenance-v10-code-coverage");
    writer.u32(metadata.runtime_abi);
    writer.string(metadata.time_resolution);
    writer.string(metadata.delay_mode);
    writer.string(metadata.optimization);
    writer.string(metadata.cache_key);
    writer.string(metadata.trace_archive);
    writer.u32(metadata.code_coverage.schema);
    writer.boolean(metadata.code_coverage.enabled);
    writer.string(metadata.code_coverage.model);
    writer.string(metadata.code_coverage.digest);
    writer.string(metadata.uvm_release);
    writer.string(metadata.uvm_source_identity);
    writer.u64(metadata.seed);
    writer.boolean(metadata.entropy_seed);
    writer.sequence(metadata.search_libraries, [&](const auto& value) {
        writer.string(value);
    });
    writer.sequence(metadata.roots, [&](const auto& root) {
        writer.string(root.alias);
        writer.string(root.target);
        writer.string(root.selected_identity);
    });
    writer.sequence(metadata.bindings, [&](const auto& binding) {
        writer.string(binding.instance);
        writer.optional_string(binding.target);
        writer.optional_string(binding.resolver);
    });
    writer.sequence(metadata.objects, [&](const auto& object) {
        writer.string(object.metadata_digest);
        writer.string(object.compilation_digest);
        writer.string(object.language);
        writer.string(object.standard);
        writer.string(object.compatibility_profile);
        writer.string(object.library);
        writer.u32(object.code_coverage.schema);
        writer.boolean(object.code_coverage.enabled);
        writer.string(object.code_coverage.model);
        writer.string(object.code_coverage.digest);
        writer.sequence(
            object.vhdl_package_dependencies, [&](const auto& dependency) {
                writer.string(dependency.standard);
                writer.string(dependency.predefined_environment);
                writer.string(dependency.package);
                writer.string(dependency.revision);
                writer.string(dependency.source_digest);
            });
        writer.sequence(object.unit_checksums, [&](const auto& checksum) {
            writer.string(checksum);
        });
    });
    writer.sequence(metadata.vhdl_unit_provenance, [&](const auto& unit) {
        writer.u32(unit.unit);
        writer.string(unit.standard);
        writer.string(unit.predefined_environment);
        writer.string(unit.compatibility_profile);
        writer.sequence(unit.package_dependencies, [&](const auto& dependency) {
            writer.string(dependency.standard);
            writer.string(dependency.predefined_environment);
            writer.string(dependency.package);
            writer.string(dependency.revision);
            writer.string(dependency.source_digest);
        });
    });
    writer.sequence(metadata.verilog_unit_provenance, [&](const auto& unit) {
        writer.u32(unit.unit);
        writer.string(unit.language);
        writer.string(unit.standard);
        writer.string(unit.compatibility_profile);
    });
    write_sdf_annotations(writer, metadata.sdf_annotations);
    writer.sequence(metadata.systemc_plugins, [&](const auto& plugin) {
        writer.string(plugin.logical_library);
        writer.string(plugin.input_digest);
        writer.string(plugin.link_digest);
        writer.string(plugin.compiler_fingerprint);
        writer.string(plugin.scv_compatibility);
        writer.string(plugin.metadata_checksum);
        writer.string(plugin.library_checksum);
        writer.sequence(plugin.factories, [&](const auto& factory) {
            writer.string(factory);
        });
    });
    writer.sequence(metadata.payloads, [&](const auto& payload) {
        writer.string(payload.kind);
        writer.string(payload.checksum);
    });
    writer.sequence(metadata.specialization_cache_keys, [&](const auto& key) {
        writer.string(key);
    });
    writer.u64(metadata.unit_count);
    writer.u64(metadata.semantic_source_count);
    writer.u64(metadata.specialization_count);
    writer.u64(metadata.signal_count);
    writer.u64(metadata.process_count);
}

bool validate(
    const DesignMetadata& metadata,
    diagnostic::Engine& diagnostics,
    const std::string& source) {
    if (metadata.format != kDesignFormatVersion
        || metadata.runtime_abi != runtime_abi_version) {
        report(
            diagnostics, kSchemaCode,
            diagnostic::unsupported_artifact_identity(
                ".fsimdesign", "format " + std::to_string(metadata.format) + " and runtime ABI " + std::to_string(metadata.runtime_abi),
                "format " + std::to_string(kDesignFormatVersion)
                    + " and runtime ABI "
                    + std::to_string(runtime_abi_version),
                ".fsimdesign"),
            source);
    }
    if (validate_code_coverage_artifact_identity(metadata.code_coverage)
        != CodeCoverageArtifactIdentityError::None) {
        report(
            diagnostics, kCodeCoverageArtifactDiagnostic,
            "design code-coverage identity is invalid or incompatible", source);
    }
    if (metadata.producer.empty() || metadata.time_resolution.empty()
        || (metadata.delay_mode != "min" && metadata.delay_mode != "typ"
            && metadata.delay_mode != "max")
        || (metadata.optimization != "O0" && metadata.optimization != "O2")
        || !checksum_spelling(metadata.cache_key)
        || (metadata.trace_archive.size() % 2U) != 0U
        || !std::ranges::all_of(metadata.trace_archive,
            [](const char value) {
                return (value >= '0' && value <= '9')
                    || (value >= 'a' && value <= 'f');
            })
        || (metadata.uvm_release != "none"
            && metadata.uvm_release != "1.2"
            && metadata.uvm_release != "2020.3.1")
        || (metadata.uvm_release == "none"
            && !metadata.uvm_source_identity.empty())
        || (metadata.uvm_release != "none"
            && !checksum_spelling(metadata.uvm_source_identity))
        || !checksum_spelling(metadata.design_digest)) {
        report(
            diagnostics, kValueCode,
            "design metadata requires producer, time resolution, delay and "
            "optimization modes, and lowercase SHA-256 cache/design digests",
            source);
    }
  std::set<std::string> libraries;
  for (const auto& library : metadata.search_libraries) {
    if (!safe_name(library) || !libraries.insert(library).second) {
      report(
          diagnostics, kValueCode,
          "design search libraries must be unique safe names", source);
    }
  }
  std::set<std::string> aliases;
  for (const auto& root : metadata.roots) {
    if (!safe_name(root.alias) || root.target.empty()
        || root.selected_identity.empty()
        || !aliases.insert(root.alias).second) {
      report(
          diagnostics, kValueCode,
          "design roots require unique safe aliases, targets, and selected "
          "canonical identities",
          source);
    }
  }
  for (const auto& binding : metadata.bindings) {
    if (binding.instance.empty()
        || (!binding.target.has_value() && !binding.resolver.has_value())) {
      report(
          diagnostics, kValueCode,
          "design bindings require an instance and target, resolver, or both",
          source);
    }
  }
  for (const auto& object : metadata.objects) {
      std::set<std::string> vhdl_packages;
      if (!checksum_spelling(object.metadata_digest)
          || !checksum_spelling(object.compilation_digest)
          || (object.language != "vhdl" && object.language != "verilog"
              && object.language != "systemverilog")
          || object.standard.empty()
          || object.compatibility_profile.empty()
          || (object.language == "vhdl"
              && object.compatibility_profile == "none")
          || !safe_name(object.library)
          || object.code_coverage != metadata.code_coverage
          || validate_code_coverage_artifact_identity(object.code_coverage)
              != CodeCoverageArtifactIdentityError::None
          || std::ranges::any_of(
              object.vhdl_package_dependencies,
              [&](const auto& dependency) {
                  return object.language != "vhdl"
                      || dependency.standard != object.standard
                      || dependency.predefined_environment.empty()
                      || dependency.package.empty() || dependency.revision.empty()
                      || !checksum_spelling(dependency.source_digest)
                      || !vhdl_packages.insert(dependency.package).second;
              })
          || object.unit_checksums.empty()
          || std::ranges::any_of(
              object.unit_checksums,
              [](const auto& checksum) { return !checksum_spelling(checksum); })) {
          report(
              diagnostics, kValueCode,
              "design object inputs require complete portable content identities",
              source);
      }
  }
  std::set<std::uint32_t> vhdl_units;
  const auto has_vhdl_object = std::ranges::any_of(
      metadata.objects,
      [](const auto& object) { return object.language == "vhdl"; });
  if (has_vhdl_object != !metadata.vhdl_unit_provenance.empty()
      || !std::ranges::is_sorted(
          metadata.vhdl_unit_provenance, { },
          &DesignVhdlUnitProvenance::unit)) {
      report(
          diagnostics, kValueCode,
          "current VHDL designs require ordered semantic-unit provenance",
          source);
  }
  for (const auto& unit : metadata.vhdl_unit_provenance) {
    std::set<std::string> packages;
    const auto matching_object = std::ranges::find_if(
        metadata.objects, [&](const auto& object) {
          return object.language == "vhdl"
              && object.standard == unit.standard
              && object.compatibility_profile
                  == unit.compatibility_profile
              && std::ranges::all_of(
                  unit.package_dependencies,
                  [&](const auto& dependency) {
                    return std::ranges::find(
                               object.vhdl_package_dependencies,
                               dependency)
                        != object.vhdl_package_dependencies.end();
                  });
        });
    if (unit.unit >= metadata.unit_count
        || !vhdl_units.insert(unit.unit).second || unit.standard.empty()
        || unit.predefined_environment.empty()
        || unit.compatibility_profile.empty()
        || unit.compatibility_profile == "none"
        || matching_object == metadata.objects.end()
        || std::ranges::any_of(
            unit.package_dependencies, [&](const auto& dependency) {
                return dependency.standard != unit.standard
                    || dependency.predefined_environment
                    != unit.predefined_environment
                    || dependency.package.empty() || dependency.revision.empty()
                    || !checksum_spelling(dependency.source_digest)
                    || !packages.insert(dependency.package).second;
            })) {
        report(
            diagnostics, kValueCode,
            "design VHDL unit provenance requires unique semantic units and "
            "complete revision, environment, profile, and package identities",
            source);
    }
  }
  std::set<std::uint32_t> verilog_units;
  if (!std::ranges::is_sorted(
          metadata.verilog_unit_provenance, { },
          &DesignVerilogUnitProvenance::unit)) {
      report(
          diagnostics, kValueCode,
          "current Verilog/SystemVerilog unit provenance must be ordered",
          source);
  }
  for (const auto& unit : metadata.verilog_unit_provenance) {
    const auto matching_object = std::ranges::find_if(
        metadata.objects, [&](const auto& object) {
          return object.language == unit.language
              && object.standard == unit.standard
              && object.compatibility_profile == unit.compatibility_profile;
        });
    if (unit.unit >= metadata.unit_count
        || !verilog_units.insert(unit.unit).second
        || (unit.language != "verilog"
            && unit.language != "systemverilog")
        || unit.standard.empty() || unit.compatibility_profile.empty()
        || matching_object == metadata.objects.end()) {
        report(
            diagnostics, kValueCode,
            "design Verilog/SystemVerilog unit provenance requires unique "
            "semantic units and matching language, standard, and compatibility "
            "object identities",
            source);
    }
  }
  validate_sdf_annotations(metadata, diagnostics, source);
  std::set<std::string> systemc_libraries;
  std::set<std::string> systemc_directories;
  for (const auto& plugin : metadata.systemc_plugins) {
    const auto directory = support::path_to_utf8(plugin.directory);
    if (!safe_name(plugin.logical_library)
        || !systemc_libraries.insert(plugin.logical_library).second
        || !checksum_spelling(plugin.input_digest)
        || !checksum_spelling(plugin.link_digest)
        || !checksum_spelling(plugin.compiler_fingerprint)
        || plugin.scv_compatibility.empty()
        || !safe_relative_path(plugin.directory)
        || !systemc_directories.insert(directory).second
        || !checksum_spelling(plugin.metadata_checksum)
        || !checksum_spelling(plugin.library_checksum)
        || plugin.factories.empty()
        || !std::ranges::is_sorted(plugin.factories)
        || std::adjacent_find(
               plugin.factories.begin(), plugin.factories.end())
            != plugin.factories.end()
        || std::ranges::any_of(
            plugin.factories,
            [](const auto& factory) { return factory.empty(); })) {
        report(
            diagnostics, kValueCode,
            "design SystemC plug-ins require unique safe libraries/directories, "
            "compatible checksums, and a sorted unique factory inventory",
            source);
    }
  }
  std::set<std::string> payload_kinds;
  std::set<std::string> payload_paths;
  for (const auto& payload : metadata.payloads) {
    const auto path = support::path_to_utf8(payload.artifact);
    if (payload.kind.empty() || !payload_kinds.insert(payload.kind).second
        || !safe_relative_path(payload.artifact)
        || !payload_paths.insert(path).second
        || !checksum_spelling(payload.checksum)) {
      report(
          diagnostics, kValueCode,
          "design payloads require unique kinds and contained checksummed paths",
          source);
    }
  }
  for (const auto required : {"runtime", "semantics", "design-ir"}) {
    if (!payload_kinds.contains(required)) {
      report(
          diagnostics, kValueCode,
          "design payload index is missing required kind '"
              + std::string{required} + "'",
          source);
    }
  }
  for (const auto& plugin : metadata.systemc_plugins) {
    const auto metadata_kind =
        "systemc-plugin-metadata:" + plugin.logical_library;
    const auto native_kind = "systemc-plugin-native:" + plugin.logical_library;
    const auto metadata_payload = std::ranges::find_if(
        metadata.payloads,
        [&](const auto& payload) { return payload.kind == metadata_kind; });
    const auto native_payload = std::ranges::find_if(
        metadata.payloads,
        [&](const auto& payload) { return payload.kind == native_kind; });
    const auto native_relative = native_payload == metadata.payloads.end()
        ? std::filesystem::path{}
        : native_payload->artifact.lexically_relative(plugin.directory);
    if (metadata_payload == metadata.payloads.end()
        || native_payload == metadata.payloads.end()
        || metadata_payload->artifact.parent_path() != plugin.directory
        || !safe_relative_path(native_relative)
        || metadata_payload->artifact.filename()
            != "fsim-systemc-plugin.bin"
        || metadata_payload->checksum != plugin.metadata_checksum
        || native_payload->checksum != plugin.library_checksum) {
      report(
          diagnostics, kValueCode,
          "design SystemC plug-in records must match their metadata and "
          "native payload indexes",
          source);
    }
  }
  if (std::ranges::any_of(
          metadata.specialization_cache_keys,
          [](const auto& key) { return !checksum_spelling(key); })) {
    report(
        diagnostics, kValueCode,
        "design specialization cache keys must be lowercase SHA-256 values",
        source);
  }
  if (metadata.roots.empty()
      || (metadata.objects.empty() && metadata.systemc_plugins.empty())
      || metadata.payloads.empty()
      || (metadata.unit_count == 0 && metadata.systemc_plugins.empty())) {
    report(
        diagnostics, kValueCode,
        "design metadata requires roots, compiled inputs, HDL units when "
        "applicable, and payloads",
        source);
  }
  if (checksum_spelling(metadata.design_digest)
      && metadata.design_digest != compute_design_digest(metadata)) {
    report(
        diagnostics, kValueCode,
        "design digest does not match its provenance and payload indexes",
        source);
  }
  return !diagnostics.has_error();
}

std::filesystem::path staging_path(const std::filesystem::path& destination) {
  const auto sequence = staging_sequence.fetch_add(1, std::memory_order_relaxed);
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return destination.parent_path()
      / ("." + destination.filename().string() + ".staging-"
         + std::to_string(nonce) + "-" + std::to_string(sequence));
}

void make_writable(const std::filesystem::path& root) noexcept {
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

class Cleanup {
 public:
  explicit Cleanup(std::filesystem::path path) : path_(std::move(path)) {}
  ~Cleanup() {
    if (!path_.empty()) {
      make_writable(path_);
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }
  }
  void release() noexcept { path_.clear(); }

 private:
  std::filesystem::path path_;
};

bool write_file(
    const std::filesystem::path& path,
    const std::string_view bytes,
    diagnostic::Engine& diagnostics) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (error || !output) {
    report(
        diagnostics, kIoCode,
        "cannot write design payload"
            + (error ? ": " + error.message() : std::string{}),
        support::path_to_utf8(path));
    return false;
  }
  return true;
}

std::optional<std::string> file_checksum(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    report(diagnostics, kIoCode, "cannot read generated design payload",
        support::path_to_utf8(path));
    return std::nullopt;
  }
  support::Sha256 checksum;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      checksum.update(std::string_view{
          buffer.data(), static_cast<std::size_t>(count)});
    }
  }
  if (!input.eof()) {
    report(diagnostics, kIoCode, "cannot read generated design payload",
        support::path_to_utf8(path));
    return std::nullopt;
  }
  return support::Sha256::hex(checksum.finish());
}

bool make_read_only(
    const std::filesystem::path& root,
    diagnostic::Engine& diagnostics) {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    const auto permissions = iterator->is_directory()
        ? std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_read
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_read
            | std::filesystem::perms::others_exec
        : std::filesystem::perms::owner_read
            | std::filesystem::perms::group_read
            | std::filesystem::perms::others_read;
    std::filesystem::permissions(
        iterator->path(), permissions,
        std::filesystem::perm_options::replace, error);
  }
  if (!error) {
    std::filesystem::permissions(
        root,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_exec
            | std::filesystem::perms::group_read
            | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_read
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace, error);
  }
  if (error) {
    report(
        diagnostics, kIoCode,
        "cannot make staged design read-only: " + error.message(),
        support::path_to_utf8(root));
  }
  return !error;
}

}  // namespace

std::string compute_design_digest(const DesignMetadata& metadata) {
  Writer writer;
  write_digest_fields(writer, metadata);
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

std::string serialize_design_metadata(const DesignMetadata& metadata) {
  Writer writer;
  writer.raw(kMagic);
  writer.u32(kDesignFormatVersion);
  writer.u32(metadata.runtime_abi);
  writer.string(metadata.producer);
  writer.string(metadata.design_digest);
  writer.string(metadata.time_resolution);
  writer.string(metadata.delay_mode);
  writer.string(metadata.optimization);
  writer.string(metadata.cache_key);
  writer.string(metadata.trace_archive);
  writer.string(metadata.uvm_release);
  writer.string(metadata.uvm_source_identity);
  writer.u32(metadata.code_coverage.schema);
  writer.boolean(metadata.code_coverage.enabled);
  writer.string(metadata.code_coverage.model);
  writer.string(metadata.code_coverage.digest);
  writer.u64(metadata.seed);
  writer.boolean(metadata.entropy_seed);
  writer.sequence(metadata.search_libraries, [&](const auto& value) {
    writer.string(value);
  });
  writer.sequence(metadata.roots, [&](const auto& root) {
    writer.string(root.alias);
    writer.string(root.target);
    writer.string(root.selected_identity);
  });
  writer.sequence(metadata.bindings, [&](const auto& binding) {
    writer.string(binding.instance);
    writer.optional_string(binding.target);
    writer.optional_string(binding.resolver);
  });
  writer.sequence(metadata.objects, [&](const auto& object) {
    writer.string(object.metadata_digest);
    writer.string(object.compilation_digest);
    writer.string(object.language);
    writer.string(object.standard);
    writer.string(object.compatibility_profile);
    writer.string(object.library);
    writer.u32(object.code_coverage.schema);
    writer.boolean(object.code_coverage.enabled);
    writer.string(object.code_coverage.model);
    writer.string(object.code_coverage.digest);
    writer.sequence(
        object.vhdl_package_dependencies, [&](const auto& dependency) {
            writer.string(dependency.standard);
            writer.string(dependency.predefined_environment);
            writer.string(dependency.package);
            writer.string(dependency.revision);
            writer.string(dependency.source_digest);
        });
    writer.sequence(object.unit_checksums, [&](const auto& checksum) {
      writer.string(checksum);
    });
  });
  writer.sequence(metadata.vhdl_unit_provenance, [&](const auto& unit) {
      writer.u32(unit.unit);
      writer.string(unit.standard);
      writer.string(unit.predefined_environment);
      writer.string(unit.compatibility_profile);
      writer.sequence(unit.package_dependencies, [&](const auto& dependency) {
        writer.string(dependency.standard);
        writer.string(dependency.predefined_environment);
        writer.string(dependency.package);
        writer.string(dependency.revision);
        writer.string(dependency.source_digest);
      });
  });
  writer.sequence(metadata.verilog_unit_provenance, [&](const auto& unit) {
      writer.u32(unit.unit);
      writer.string(unit.language);
      writer.string(unit.standard);
      writer.string(unit.compatibility_profile);
  });
  write_sdf_annotations(writer, metadata.sdf_annotations);
  writer.sequence(metadata.systemc_plugins, [&](const auto& plugin) {
      writer.string(plugin.logical_library);
      writer.string(plugin.input_digest);
      writer.string(plugin.link_digest);
      writer.string(plugin.compiler_fingerprint);
      writer.string(plugin.scv_compatibility);
      writer.path(plugin.directory);
      writer.string(plugin.metadata_checksum);
      writer.string(plugin.library_checksum);
      writer.sequence(plugin.factories, [&](const auto& factory) {
        writer.string(factory);
      });
  });
  writer.sequence(metadata.payloads, [&](const auto& payload) {
    writer.string(payload.kind);
    writer.path(payload.artifact);
    writer.string(payload.checksum);
  });
  writer.sequence(metadata.specialization_cache_keys, [&](const auto& key) {
    writer.string(key);
  });
  writer.u64(metadata.unit_count);
  writer.u64(metadata.semantic_source_count);
  writer.u64(metadata.specialization_count);
  writer.u64(metadata.signal_count);
  writer.u64(metadata.process_count);
  return std::move(writer).take();
}

std::optional<DesignMetadata> deserialize_design_metadata(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Reader reader{bytes};
  DesignMetadata metadata;
  if (!reader.raw(kMagic)) {
    report(diagnostics, kSchemaCode, "invalid .fsimdesign metadata magic", source_name);
    return std::nullopt;
  }
  const auto format = reader.u32();
  const auto runtime_abi = reader.u32();
  if (!format || !runtime_abi) {
    report(diagnostics, kSchemaCode, "truncated .fsimdesign header", source_name);
    return std::nullopt;
  }
  metadata.format = *format;
  metadata.runtime_abi = *runtime_abi;
  if (metadata.format != kDesignFormatVersion
      || metadata.runtime_abi != runtime_abi_version) {
      report(
          diagnostics, kSchemaCode,
          diagnostic::unsupported_artifact_identity(
              ".fsimdesign", "format " + std::to_string(metadata.format) + " and runtime ABI " + std::to_string(metadata.runtime_abi),
              "format " + std::to_string(kDesignFormatVersion)
                  + " and runtime ABI "
                  + std::to_string(runtime_abi_version),
              ".fsimdesign"),
          source_name);
      return std::nullopt;
  }
  const auto read_string = [&](std::string& value) {
    auto read = reader.string();
    if (!read) {
      return false;
    }
    value = std::move(*read);
    return true;
  };
  if (!read_string(metadata.producer)
      || !read_string(metadata.design_digest)
      || !read_string(metadata.time_resolution)
      || !read_string(metadata.delay_mode)
      || !read_string(metadata.optimization)
      || !read_string(metadata.cache_key)
      || !read_string(metadata.trace_archive)
      || !read_string(metadata.uvm_release)
      || !read_string(metadata.uvm_source_identity)) {
      report(diagnostics, kSchemaCode, "truncated .fsimdesign root", source_name);
      return std::nullopt;
  }
  const auto coverage_schema = reader.u32();
  const auto coverage_enabled = reader.boolean();
  if (!coverage_schema || !coverage_enabled
      || !read_string(metadata.code_coverage.model)
      || !read_string(metadata.code_coverage.digest)) {
    report(
        diagnostics, kSchemaCode,
        "truncated .fsimdesign code-coverage identity", source_name);
    return std::nullopt;
  }
  metadata.code_coverage.schema = *coverage_schema;
  metadata.code_coverage.enabled = *coverage_enabled;
  const auto seed = reader.u64();
  const auto entropy = reader.boolean();
  if (!seed || !entropy) {
    report(diagnostics, kSchemaCode, "truncated .fsimdesign root", source_name);
    return std::nullopt;
  }
  metadata.seed = *seed;
  metadata.entropy_seed = *entropy;
  const auto read_sequence = [&](auto&& callback) {
    const auto count = reader.count();
    if (!count) {
      return false;
    }
    for (std::size_t index = 0; index < *count; ++index) {
      if (!callback()) {
        return false;
      }
    }
    return true;
  };
  if (!read_sequence([&] {
          auto value = reader.string();
          if (value)
              metadata.search_libraries.push_back(std::move(*value));
          return value.has_value();
      })
      || !read_sequence([&] {
             DesignRoot root;
             if (!read_string(root.alias) || !read_string(root.target)
                 || !read_string(root.selected_identity))
                 return false;
             metadata.roots.push_back(std::move(root));
             return true;
         })
      || !read_sequence([&] {
             DesignBinding binding;
             if (!read_string(binding.instance))
                 return false;
             auto target = reader.optional_string();
             auto resolver = reader.optional_string();
             if (!target || !resolver)
                 return false;
             binding.target = std::move(*target);
             binding.resolver = std::move(*resolver);
             metadata.bindings.push_back(std::move(binding));
             return true;
         })
      || !read_sequence([&] {
             DesignObjectInput object;
             if (!read_string(object.metadata_digest)
                 || !read_string(object.compilation_digest)
                 || !read_string(object.language)
                 || !read_string(object.standard)
                 || !read_string(object.compatibility_profile)
                 || !read_string(object.library))
                 return false;
             const auto object_coverage_schema = reader.u32();
             const auto object_coverage_enabled = reader.boolean();
             if (!object_coverage_schema || !object_coverage_enabled
                 || !read_string(object.code_coverage.model)
                 || !read_string(object.code_coverage.digest))
                 return false;
             object.code_coverage.schema = *object_coverage_schema;
             object.code_coverage.enabled = *object_coverage_enabled;
             if (!read_sequence([&] {
                     library::VhdlPackageDependency dependency;
                     if (!read_string(dependency.standard)
                         || !read_string(dependency.predefined_environment)
                         || !read_string(dependency.package)
                         || !read_string(dependency.revision)
                         || !read_string(dependency.source_digest))
                         return false;
                     object.vhdl_package_dependencies.push_back(
                         std::move(dependency));
                     return true;
                 }))
                 return false;
             if (!read_sequence([&] {
                     auto checksum = reader.string();
                     if (checksum)
                         object.unit_checksums.push_back(
                             std::move(*checksum));
                     return checksum.has_value();
                 }))
                 return false;
             metadata.objects.push_back(std::move(object));
             return true;
         })
      || !read_sequence([&] {
             DesignVhdlUnitProvenance unit;
             const auto unit_id = reader.u32();
             if (!unit_id || !read_string(unit.standard)
                 || !read_string(unit.predefined_environment)
                 || !read_string(unit.compatibility_profile))
                 return false;
             unit.unit = *unit_id;
             if (!read_sequence([&] {
                     library::VhdlPackageDependency dependency;
                     if (!read_string(dependency.standard)
                         || !read_string(dependency.predefined_environment)
                         || !read_string(dependency.package)
                         || !read_string(dependency.revision)
                         || !read_string(dependency.source_digest))
                         return false;
                     unit.package_dependencies.push_back(std::move(dependency));
                     return true;
                 }))
                 return false;
             metadata.vhdl_unit_provenance.push_back(std::move(unit));
             return true;
         })
      || !read_sequence([&] {
             DesignVerilogUnitProvenance unit;
             const auto unit_id = reader.u32();
             if (!unit_id || !read_string(unit.language)
                 || !read_string(unit.standard)
                 || !read_string(unit.compatibility_profile))
                 return false;
             unit.unit = *unit_id;
             metadata.verilog_unit_provenance.push_back(std::move(unit));
             return true;
         })
      || !read_sdf_annotations(reader, metadata)
      || !read_sequence([&] {
             DesignSystemCPlugin plugin;
             if (!read_string(plugin.logical_library)
                 || !read_string(plugin.input_digest)
                 || !read_string(plugin.link_digest)
                 || !read_string(plugin.compiler_fingerprint)
                 || !read_string(plugin.scv_compatibility))
                 return false;
             auto directory = reader.path();
             if (!directory || !read_string(plugin.metadata_checksum)
                 || !read_string(plugin.library_checksum))
                 return false;
             plugin.directory = std::move(*directory);
             if (!read_sequence([&] {
                     auto factory = reader.string();
                     if (factory)
                         plugin.factories.push_back(std::move(*factory));
                     return factory.has_value();
                 }))
                 return false;
             metadata.systemc_plugins.push_back(std::move(plugin));
             return true;
         })
      || !read_sequence([&] {
             DesignPayload payload;
             if (!read_string(payload.kind))
                 return false;
             auto path = reader.path();
             if (!path || !read_string(payload.checksum))
                 return false;
             payload.artifact = std::move(*path);
             metadata.payloads.push_back(std::move(payload));
             return true;
         })
      || !read_sequence([&] {
             auto key = reader.string();
             if (key)
                 metadata.specialization_cache_keys.push_back(
                     std::move(*key));
             return key.has_value();
         })) {
      report(diagnostics, kSchemaCode, "truncated .fsimdesign index", source_name);
      return std::nullopt;
  }
  const auto units = reader.u64();
  const auto semantic_sources = reader.u64();
  const auto specializations = reader.u64();
  const auto signals = reader.u64();
  const auto processes = reader.u64();
  if (!units || !semantic_sources || !specializations || !signals
      || !processes || reader.remaining() != 0) {
    report(
        diagnostics, kSchemaCode,
        reader.remaining() == 0 ? "truncated .fsimdesign counts"
                                : "trailing .fsimdesign metadata bytes",
        source_name);
    return std::nullopt;
  }
  metadata.unit_count = *units;
  metadata.semantic_source_count = *semantic_sources;
  metadata.specialization_count = *specializations;
  metadata.signal_count = *signals;
  metadata.process_count = *processes;
  return validate(metadata, diagnostics, source_name)
      ? std::optional{std::move(metadata)} : std::nullopt;
}

std::optional<DesignMetadata> load_design_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics) {
  const auto path = directory / kDesignMetadataFilename;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    report(
        diagnostics, kIoCode, "cannot open .fsimdesign metadata",
        support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    report(
        diagnostics, kIoCode, "cannot read .fsimdesign metadata",
        support::path_to_utf8(path));
    return std::nullopt;
  }
  return deserialize_design_metadata(
      contents.str(), support::path_to_utf8(path), diagnostics);
}

bool publish_design(
    const std::filesystem::path& destination,
    const DesignMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    const std::span<const GeneratedDesignPayload> generated_payloads,
    diagnostic::Engine& diagnostics) {
  if (destination.empty()) {
    report(diagnostics, kIoCode, "design output path must not be empty");
    return false;
  }
  std::error_code exists_error;
  if (std::filesystem::exists(destination, exists_error) || exists_error) {
    report(
        diagnostics, kIoCode,
        "design output already exists or cannot be inspected",
        support::path_to_utf8(destination));
    return false;
  }
  if (metadata.format != kDesignFormatVersion
      || metadata.runtime_abi != runtime_abi_version) {
      report(
          diagnostics, kSchemaCode,
          diagnostic::unsupported_artifact_identity(
              ".fsimdesign", "format " + std::to_string(metadata.format) + " and runtime ABI " + std::to_string(metadata.runtime_abi),
              "format " + std::to_string(kDesignFormatVersion)
                  + " and runtime ABI "
                  + std::to_string(runtime_abi_version),
              ".fsimdesign"));
      return false;
  }
  diagnostic::Engine validation;
  if (!validate(
          metadata, validation, std::string { kDesignMetadataFilename })) {
      diagnostic::Diagnostic failure;
      failure.severity = diagnostic::Severity::error;
      failure.code = std::string { kIoCode };
      failure.message = "invalid design metadata supplied for publication";
      for (const auto& detail : validation.diagnostics()) {
          failure.notes.push_back({ detail.code + ": " + detail.message, { } });
      }
      diagnostics.report(std::move(failure));
      return false;
  }
  const auto canonical = serialize_design_metadata(metadata);
  if (!deserialize_design_metadata(
          canonical, std::string{kDesignMetadataFilename}, validation)) {
    diagnostic::Diagnostic failure;
    failure.severity = diagnostic::Severity::error;
    failure.code = std::string{kIoCode};
    failure.message = "invalid design metadata supplied for publication";
    for (const auto& detail : validation.diagnostics()) {
      failure.notes.push_back({detail.code + ": " + detail.message, {}});
    }
    diagnostics.report(std::move(failure));
    return false;
  }
  std::unordered_map<std::string, std::string> expected;
  for (const auto& payload : metadata.payloads) {
    expected.emplace(support::path_to_utf8(payload.artifact), payload.checksum);
  }
  std::unordered_set<std::string> supplied;
  for (const auto& payload : payloads) {
    const auto path = support::path_to_utf8(payload.path);
    const auto found = expected.find(path);
    if (!safe_relative_path(payload.path) || found == expected.end()
        || !supplied.insert(path).second
        || found->second != support::Sha256::hex(
            support::Sha256::digest(payload.bytes))) {
      report(
          diagnostics, kIoCode,
          "design payload is unsafe, duplicate, unindexed, or has a checksum "
          "mismatch: " + path);
      return false;
    }
  }
  for (const auto& payload : generated_payloads) {
    const auto path = support::path_to_utf8(payload.path);
    const auto found = expected.find(path);
    if (!safe_relative_path(payload.path) || found == expected.end()
        || !supplied.insert(path).second || !payload.write
        || found->second != payload.checksum) {
      report(
          diagnostics, kIoCode,
          "generated design payload is unsafe, duplicate, unindexed, or has "
          "a checksum mismatch: " + path);
      return false;
    }
  }
  if (supplied.size() != expected.size()) {
    report(diagnostics, kIoCode, "design payload set is incomplete");
    return false;
  }
  const auto parent = destination.parent_path().empty()
      ? std::filesystem::path{"."} : destination.parent_path();
  std::error_code parent_error;
  std::filesystem::create_directories(parent, parent_error);
  if (parent_error) {
    report(
        diagnostics, kIoCode,
        "cannot create design output parent: " + parent_error.message());
    return false;
  }
  const auto staging = staging_path(destination);
  Cleanup cleanup{staging};
  std::error_code stage_error;
  if (!std::filesystem::create_directory(staging, stage_error) || stage_error) {
    report(
        diagnostics, kIoCode,
        "cannot create design staging directory: " + stage_error.message());
    return false;
  }
  for (const auto& payload : payloads) {
    if (!write_file(staging / payload.path, payload.bytes, diagnostics)) {
      return false;
    }
  }
  for (const auto& payload : generated_payloads) {
    const auto path = staging / payload.path;
    std::error_code directory_error;
    std::filesystem::create_directories(path.parent_path(), directory_error);
    if (directory_error || !payload.write(path, diagnostics)) {
      if (directory_error) {
        report(diagnostics, kIoCode,
            "cannot create generated design payload directory: "
                + directory_error.message(),
            support::path_to_utf8(path.parent_path()));
      }
      return false;
    }
    const auto checksum = file_checksum(path, diagnostics);
    if (!checksum || *checksum != payload.checksum) {
      if (checksum) {
        report(diagnostics, kIoCode,
            "generated design payload checksum mismatch",
            support::path_to_utf8(path));
      }
      return false;
    }
  }
  if (!write_file(staging / kDesignMetadataFilename, canonical, diagnostics)
      || !make_read_only(staging, diagnostics)) {
    return false;
  }
  std::error_code install_error;
  std::filesystem::rename(staging, destination, install_error);
  if (install_error) {
    report(
        diagnostics, kIoCode,
        "cannot atomically install design artifact: " + install_error.message());
    return false;
  }
  cleanup.release();
  return true;
}

bool publish_design(
    const std::filesystem::path& destination,
    const DesignMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics) {
  return publish_design(
      destination, metadata, payloads,
      std::span<const GeneratedDesignPayload>{}, diagnostics);
}

}  // namespace fsim::artifact
