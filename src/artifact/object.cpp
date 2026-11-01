// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/object.hpp"

#include "../diagnostic/artifact_identity.hpp"
#include "path_validation.hpp"
#include "fsim/artifact/container.hpp"
#include "fsim/support/bounded_bytes.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/payload_checksum.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fsim::artifact {
namespace {

using detail::safe_relative_path;

constexpr std::array<char, 8> kMagic{'F', 'S', 'I', 'M', 'O', 'B', 'J', '\0'};
constexpr std::string_view kSchemaCode = "FSIM-ART-0001";
constexpr std::string_view kValueCode = "FSIM-ART-0002";
constexpr std::string_view kIoCode = "FSIM-ART-0003";
constexpr std::uint64_t kMaximumMetadataBytes = 256U * 1024U * 1024U;

// A referenced source: a relative path that may climb out of the compiling
// workspace with "..", but is never absolute and has no empty or "."
// components.
bool source_reference_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_name()
      || path.has_root_directory() || path.lexically_normal() != path) {
    return false;
  }
  return std::ranges::none_of(path, [](const auto& component) {
    return component.empty() || component == ".";
  });
}

bool valid_coverage_metric(const frontend::CoverageSourceMetric metric) {
  return static_cast<std::uint8_t>(metric)
      <= static_cast<std::uint8_t>(frontend::CoverageSourceMetric::PslProperty);
}

bool valid_control_error(const frontend::CoverageSourceControlError value) {
  return static_cast<std::uint8_t>(value) <= static_cast<std::uint8_t>(
      frontend::CoverageSourceControlError::ConflictingAllMetric);
}

bool valid_identifier_form(const library::VhdlIdentifierForm form) {
  return form == library::VhdlIdentifierForm::unknown
      || form == library::VhdlIdentifierForm::basic
      || form == library::VhdlIdentifierForm::extended;
}

bool valid_unit_identifier_forms(const library::UnitIndexEntry& unit) {
  return valid_identifier_form(unit.name_form)
      && valid_identifier_form(unit.primary_name_form)
      && (unit.language == "vhdl"
          || (unit.name_form == library::VhdlIdentifierForm::unknown
              && unit.primary_name_form
                  == library::VhdlIdentifierForm::unknown))
      && (!unit.primary_name.empty()
          || unit.primary_name_form
              == library::VhdlIdentifierForm::unknown);
}

void error(
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

bool library_name(const std::string_view value) {
  if (value.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(value.front());
  if (std::isalpha(first) == 0 && first != '_') {
    return false;
  }
  return std::ranges::all_of(
      value.substr(1), [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
      });
}

class Writer {
 public:
  void bytes(const std::span<const char> value) {
    append(std::string_view{value.data(), value.size()});
  }

  void u32(const std::uint32_t value) {
    require_write(writer_.write_u32_le(value));
  }

  void u64(const std::uint64_t value) {
    require_write(writer_.write_u64_le(value));
  }

  void boolean(const bool value) {
    require_write(writer_.write_u8(static_cast<std::uint8_t>(value ? 1U : 0U)));
  }

  void string(const std::string_view value) {
    u64(static_cast<std::uint64_t>(value.size()));
    append(value);
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

  [[nodiscard]] std::string take() && {
    return std::move(output_);
  }

 private:
  static void require_write(const bool succeeded) {
    if (!succeeded) {
      throw std::length_error("Object metadata exceeds the output size limit.");
    }
  }

  void append(const std::string_view value) {
    require_write(writer_.append(value));
  }

  std::string output_;
  support::BoundedByteWriter<std::string> writer_{output_, output_.max_size()};
};

class Reader {
 public:
  explicit Reader(const std::string_view input)
      : input_(input),
        reader_(std::as_bytes(std::span{input.data(), input.size()})) {}

  bool bytes(const std::span<const char> expected) {
    if (expected.size() > remaining()
        || input_.substr(reader_.position(), expected.size())
            != std::string_view{expected.data(), expected.size()}) {
      return false;
    }
    std::span<const std::byte> ignored;
    return reader_.take(expected.size(), ignored);
  }

  std::optional<std::uint32_t> u32() {
    std::uint32_t value{};
    if (!reader_.read_u32_le(value)) {
      return std::nullopt;
    }
    return value;
  }

  std::optional<std::uint64_t> u64() {
    std::uint64_t value{};
    if (!reader_.read_u64_le(value)) {
      return std::nullopt;
    }
    return value;
  }

  std::optional<bool> boolean() {
    std::uint8_t value{};
    if (!reader_.read_u8(value) || value > 1U) {
      return std::nullopt;
    }
    return value != 0U;
  }

  std::optional<std::string> string() {
    const auto size = u64();
    if (!size.has_value() || *size > remaining()
        || *size > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    std::span<const std::byte> value;
    if (!reader_.take(static_cast<std::size_t>(*size), value)) {
      return std::nullopt;
    }
    std::string result;
    if (!value.empty()) {
      result.assign(reinterpret_cast<const char*>(value.data()), value.size());
    }
    return result;
  }

  std::optional<std::filesystem::path> path() {
    const auto value = string();
    return value.has_value()
        ? std::optional{support::path_from_utf8(*value)} : std::nullopt;
  }

  std::optional<std::size_t> count() {
    const auto value = u64();
    if (!value.has_value() || *value > remaining()
        || *value > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return reader_.remaining();
  }

 private:
  std::string_view input_;
  support::BoundedByteReader reader_;
};

void write_source_coverage(
    Writer& writer, const std::vector<ObjectSourceCoverage>& coverage) {
  writer.sequence(coverage, [&](const auto& item) {
    writer.u64(item.content_bytes);
    writer.u32(static_cast<std::uint32_t>(item.control_error));
    writer.sequence(item.exclusions, [&](const auto& exclusion) {
      writer.u32(static_cast<std::uint32_t>(exclusion.metric));
      writer.u64(exclusion.begin_offset);
      writer.u64(exclusion.end_offset);
      writer.string(exclusion.reason);
      writer.u64(exclusion.off_line);
      writer.u64(exclusion.on_line);
    });
  });
}

std::optional<std::vector<ObjectSourceCoverage>> read_source_coverage(
    Reader& canonical, const std::string& source_name,
    diagnostic::Engine& diagnostics) {
  std::vector<ObjectSourceCoverage> coverage;
  const auto coverage_count = canonical.count();
  if (!coverage_count.has_value()) {
    error(diagnostics, kSchemaCode, "invalid object source coverage count", source_name);
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *coverage_count; ++index) {
    ObjectSourceCoverage item;
    const auto content_bytes = canonical.u64();
    const auto control_error = canonical.u32();
    const auto exclusion_count = canonical.count();
    if (!content_bytes || !control_error || !exclusion_count || *control_error > 255U) {
      error(diagnostics, kSchemaCode, "truncated object source coverage facts", source_name);
      return std::nullopt;
    }
    item.content_bytes = *content_bytes;
    item.control_error
        = static_cast<frontend::CoverageSourceControlError>(*control_error);
    for (std::size_t exclusion_index = 0; exclusion_index < *exclusion_count;
         ++exclusion_index) {
      frontend::CoverageSourceExclusion exclusion;
      const auto metric = canonical.u32();
      const auto begin_offset = canonical.u64();
      const auto end_offset = canonical.u64();
      auto reason = canonical.string();
      const auto off_line = canonical.u64();
      const auto on_line = canonical.u64();
      if (!metric || *metric > 255U || !begin_offset || !end_offset || !reason
          || !off_line || !on_line
          || *begin_offset > std::numeric_limits<std::size_t>::max()
          || *end_offset > std::numeric_limits<std::size_t>::max()) {
        error(diagnostics, kSchemaCode, "truncated object coverage exclusion", source_name);
        return std::nullopt;
      }
      exclusion.metric = static_cast<frontend::CoverageSourceMetric>(*metric);
      exclusion.begin_offset = static_cast<std::size_t>(*begin_offset);
      exclusion.end_offset = static_cast<std::size_t>(*end_offset);
      exclusion.reason = std::move(*reason);
      exclusion.off_line = *off_line;
      exclusion.on_line = *on_line;
      item.exclusions.push_back(std::move(exclusion));
    }
    coverage.push_back(std::move(item));
  }
  return coverage;
}

std::string compilation_digest(const ObjectMetadata& metadata) {
  Writer writer;
  writer.string("fsim-object-compilation-v10-compiled-hir");
  writer.u32(metadata.compiled_hir_schema);
  writer.path(metadata.compiled_hir_artifact);
  writer.string(metadata.compiled_hir_checksum);
  writer.string(metadata.language);
  writer.string(metadata.standard);
  writer.string(metadata.compatibility_profile);
  writer.string(metadata.library);
  writer.string(metadata.compilation_unit);
  writer.string(metadata.uvm_release);
  writer.string(metadata.trace_archive);
  writer.u32(metadata.code_coverage.schema);
  writer.boolean(metadata.code_coverage.enabled);
  writer.string(metadata.code_coverage.model);
  writer.string(metadata.code_coverage.digest);
  writer.sequence(metadata.defines, [&](const auto& item) {
    writer.string(item);
  });
  writer.sequence(metadata.vhdl_package_dependencies, [&](const auto& item) {
    writer.string(item.standard);
    writer.string(item.predefined_environment);
    writer.string(item.package);
    writer.string(item.revision);
    writer.string(item.source_digest);
  });
  writer.sequence(metadata.sources, [&](const auto& item) {
    writer.string(item.logical_name);
    writer.string(item.checksum);
    writer.string(item.language);
    writer.string(item.standard);
    writer.string(item.compatibility_profile);
  });
  writer.sequence(metadata.units, [&](const auto& item) {
    writer.string(item.language);
    writer.string(item.kind);
    writer.string(item.name);
    writer.string(item.primary_name);
    writer.string(item.architecture);
    writer.string(item.checksum);
    writer.string(item.standard);
    writer.string(item.compatibility_profile);
    writer.u32(static_cast<std::uint32_t>(item.name_form));
    writer.u32(static_cast<std::uint32_t>(item.primary_name_form));
  });
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

bool validate_metadata(
    const ObjectMetadata& metadata,
    diagnostic::Engine& diagnostics,
    const std::string& source) {
  if (metadata.format != kObjectFormatVersion
      || metadata.portable_schema != library::kPortableSchemaVersion
      || metadata.compiled_hir_schema
          != library::kCompiledHirSchemaVersion) {
      error(
          diagnostics, kSchemaCode,
          diagnostic::unsupported_artifact_identity(
              ".fsimobj", "format " + std::to_string(metadata.format)
                  + ", portable-unit schema "
                  + std::to_string(metadata.portable_schema)
                  + ", and compiled-HIR schema "
                  + std::to_string(metadata.compiled_hir_schema),
              "format " + std::to_string(kObjectFormatVersion)
                  + ", portable-unit schema "
                  + std::to_string(library::kPortableSchemaVersion)
                  + ", and compiled-HIR schema "
                  + std::to_string(library::kCompiledHirSchemaVersion),
              ".fsimobj"),
          source);
  }
  if (validate_code_coverage_artifact_identity(metadata.code_coverage)
      != CodeCoverageArtifactIdentityError::None) {
    error(
        diagnostics, kCodeCoverageArtifactDiagnostic,
        "object code-coverage identity is invalid or incompatible", source);
  }
  const bool known_language = metadata.language == "vhdl"
      || metadata.language == "verilog"
      || metadata.language == "systemverilog";
  if (metadata.producer.empty() || !known_language || metadata.standard.empty()
      || metadata.compatibility_profile.empty()
      || (metadata.language == "vhdl"
          && metadata.compatibility_profile == "none")
      || !library_name(metadata.library)
      || (metadata.compilation_unit != "file"
          && metadata.compilation_unit != "source-set")
      || (metadata.uvm_release != "none" && metadata.uvm_release != "1.2"
          && metadata.uvm_release != "2020.3.1")
      || (metadata.uvm_release != "none"
          && metadata.language != "systemverilog")
      || (metadata.trace_archive.size() % 2U != 0U)
      || !std::ranges::all_of(metadata.trace_archive, [](const char value) {
           return (value >= '0' && value <= '9')
               || (value >= 'a' && value <= 'f');
         })
      || !checksum_spelling(metadata.compilation_digest)) {
      error(
          diagnostics, kValueCode,
          "object metadata requires producer, supported HDL language/standard "
          "and compatibility profile, safe library, compilation mode, UVM "
          "release, and compilation digest",
          source);
  }
  std::unordered_set<std::string> include_roots;
  for (const auto& include : metadata.include_roots) {
    const auto spelling = support::path_to_utf8(include);
    if (!source_reference_path(include) || !include_roots.insert(spelling).second) {
      error(
          diagnostics, kValueCode,
          "object include roots must be unique contained paths", source);
    }
  }
  std::unordered_set<std::string> vhdl_packages;
  for (const auto& item : metadata.vhdl_package_dependencies) {
    if (metadata.language != "vhdl" || item.standard != metadata.standard
        || item.predefined_environment.empty() || item.package.empty()
        || item.revision.empty() || !checksum_spelling(item.source_digest)
        || !vhdl_packages.insert(item.package).second) {
      error(
          diagnostics, kValueCode,
          "VHDL package dependencies require unique package names, the "
          "selected standard, predefined-environment and revision identities, "
          "and lowercase SHA-256 source digests",
          source);
    }
  }
  std::unordered_set<std::string> logical_sources;
  std::unordered_set<std::string> payload_paths;
  const auto compiled_hir_path
      = support::path_to_utf8(metadata.compiled_hir_artifact);
  if (!safe_relative_path(metadata.compiled_hir_artifact)
      || !support::payload_checksum_spelling(metadata.compiled_hir_checksum)
      || !payload_paths.insert(compiled_hir_path).second) {
    error(
        diagnostics, kValueCode,
        "object compiled-HIR bundle requires a unique contained payload and "
        "payload checksum",
        source);
  }
  for (const auto& item : metadata.sources) {
    if (item.logical_name.empty()
        || !source_reference_path(support::path_from_utf8(item.logical_name))
        || !logical_sources.insert(item.logical_name).second
        || !item.artifact.empty()
        || !checksum_spelling(item.checksum)
        || item.language != metadata.language
        || item.standard != metadata.standard
        || item.compatibility_profile != metadata.compatibility_profile) {
      error(
          diagnostics, kValueCode,
          "object sources require unique relative source references, no "
          "source text, matching language, standard, compatibility profile, "
          "and lowercase SHA-256 content digests", source);
    }
  }
  if (metadata.source_coverage.size() != metadata.sources.size()) {
    error(
        diagnostics, kValueCode,
        "object source coverage facts must match the source index", source);
  }
  for (const auto& item : metadata.source_coverage) {
    if (!valid_control_error(item.control_error)
        || !std::ranges::all_of(item.exclusions, [](const auto& exclusion) {
             return valid_coverage_metric(exclusion.metric)
                 && exclusion.begin_offset <= exclusion.end_offset;
           })) {
      error(
          diagnostics, kValueCode,
          "object source coverage facts are invalid", source);
    }
  }
  for (const auto& unit : metadata.units) {
    if (!valid_unit_identifier_forms(unit)) {
      error(
          diagnostics, kValueCode,
          "object unit identifier-form metadata is invalid", source);
    }
  }
  for (const auto& item : metadata.units) {
    if (item.language != metadata.language || item.kind.empty()
        || item.name.empty() || !item.artifact.empty()
        || !item.checksum.empty()
        || item.standard != metadata.standard
        || item.compatibility_profile != metadata.compatibility_profile) {
      error(
          diagnostics, kValueCode,
          "object units require matching language, standard, compatibility "
          "profile, identity, and metadata-only compiled-HIR inventory",
          source);
    }
  }
  if (metadata.sources.empty() || metadata.units.empty()) {
    error(
        diagnostics, kValueCode,
        "object metadata must index source and unit inventory", source);
  }
  if (checksum_spelling(metadata.compilation_digest)
      && metadata.compilation_digest != compilation_digest(metadata)) {
    error(
        diagnostics, kValueCode,
        "object compilation digest does not match its indexed inputs and units",
        source);
  }
  return !diagnostics.has_error();
}

}  // namespace

std::string compute_object_compilation_digest(
    const ObjectMetadata& metadata) {
  return compilation_digest(metadata);
}

std::string serialize_object_metadata(const ObjectMetadata& metadata) {
  if (metadata.format != kObjectFormatVersion
      || metadata.portable_schema != library::kPortableSchemaVersion
      || metadata.compiled_hir_schema != library::kCompiledHirSchemaVersion) {
    throw std::invalid_argument(
        "cannot serialize .fsimobj metadata with unsupported identity");
  }
  Writer writer;
  writer.bytes(kMagic);
  writer.u32(kObjectFormatVersion);
  writer.u32(library::kPortableSchemaVersion);
  writer.u32(library::kCompiledHirSchemaVersion);
  writer.path(metadata.compiled_hir_artifact);
  writer.string(metadata.compiled_hir_checksum);
  writer.string(metadata.producer);
  writer.string(metadata.language);
  writer.string(metadata.standard);
  writer.string(metadata.compatibility_profile);
  writer.string(metadata.library);
  writer.string(metadata.compilation_unit);
  writer.string(metadata.uvm_release);
  writer.string(metadata.compilation_digest);
  writer.string(metadata.trace_archive);
  writer.u32(metadata.code_coverage.schema);
  writer.boolean(metadata.code_coverage.enabled);
  writer.string(metadata.code_coverage.model);
  writer.string(metadata.code_coverage.digest);
  writer.sequence(metadata.defines, [&](const auto& item) {
    writer.string(item);
  });
  writer.sequence(metadata.include_roots, [&](const auto& item) {
    writer.path(item);
  });
  writer.sequence(metadata.vhdl_package_dependencies, [&](const auto& item) {
    writer.string(item.standard);
    writer.string(item.predefined_environment);
    writer.string(item.package);
    writer.string(item.revision);
    writer.string(item.source_digest);
  });
  writer.sequence(metadata.sources, [&](const auto& item) {
    writer.string(item.logical_name);
    writer.path(item.artifact);
    writer.string(item.checksum);
    writer.string(item.language);
    writer.string(item.standard);
    writer.string(item.compatibility_profile);
  });
  write_source_coverage(writer, metadata.source_coverage);
  writer.sequence(metadata.units, [&](const auto& item) {
    writer.string(item.language);
    writer.string(item.kind);
    writer.string(item.name);
    writer.string(item.primary_name);
    writer.string(item.architecture);
    writer.path(item.artifact);
    writer.string(item.checksum);
    writer.string(item.standard);
    writer.string(item.compatibility_profile);
    writer.u32(static_cast<std::uint32_t>(item.name_form));
    writer.u32(static_cast<std::uint32_t>(item.primary_name_form));
  });
  return std::move(writer).take();
}

std::optional<ObjectMetadata> deserialize_object_metadata(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  ObjectMetadata metadata;
  Reader canonical{bytes};
  if (!canonical.bytes(kMagic)) {
    error(diagnostics, kSchemaCode, "invalid .fsimobj metadata magic", source_name);
    return std::nullopt;
  }
  const auto read_format = canonical.u32();
  const auto read_schema = canonical.u32();
  const auto read_compiled_hir_schema = canonical.u32();
  if (!read_format.has_value() || !read_schema.has_value()
      || !read_compiled_hir_schema.has_value()) {
    error(diagnostics, kSchemaCode, "truncated .fsimobj metadata header", source_name);
    return std::nullopt;
  }
  metadata.format = *read_format;
  metadata.portable_schema = *read_schema;
  metadata.compiled_hir_schema = *read_compiled_hir_schema;
  if (metadata.format != kObjectFormatVersion
      || metadata.portable_schema != library::kPortableSchemaVersion
      || metadata.compiled_hir_schema
          != library::kCompiledHirSchemaVersion) {
      error(
          diagnostics, kSchemaCode,
          diagnostic::unsupported_artifact_identity(
              ".fsimobj", "format " + std::to_string(metadata.format)
                  + ", portable-unit schema "
                  + std::to_string(metadata.portable_schema)
                  + ", and compiled-HIR schema "
                  + std::to_string(metadata.compiled_hir_schema),
              "format " + std::to_string(kObjectFormatVersion)
                  + ", portable-unit schema "
                  + std::to_string(library::kPortableSchemaVersion)
                  + ", and compiled-HIR schema "
                  + std::to_string(library::kCompiledHirSchemaVersion),
              ".fsimobj"),
          source_name);
      return std::nullopt;
  }
  auto read_compiled_hir_artifact = canonical.path();
  auto read_compiled_hir_checksum = canonical.string();
  if (!read_compiled_hir_artifact || !read_compiled_hir_checksum) {
    error(
        diagnostics, kSchemaCode,
        "truncated .fsimobj compiled-HIR index", source_name);
    return std::nullopt;
  }
  metadata.compiled_hir_artifact
      = std::move(*read_compiled_hir_artifact);
  metadata.compiled_hir_checksum
      = std::move(*read_compiled_hir_checksum);
  const auto read_string = [&](std::string& output) {
    auto value = canonical.string();
    if (!value.has_value()) {
      return false;
    }
    output = std::move(*value);
    return true;
  };
  if (!read_string(metadata.producer) || !read_string(metadata.language)
      || !read_string(metadata.standard)
      || !read_string(metadata.compatibility_profile)
      || !read_string(metadata.library)
      || !read_string(metadata.compilation_unit)
      || !read_string(metadata.uvm_release)
      || !read_string(metadata.compilation_digest)
      || !read_string(metadata.trace_archive)) {
      error(diagnostics, kSchemaCode, "truncated .fsimobj metadata root", source_name);
      return std::nullopt;
  }
  const auto coverage_schema = canonical.u32();
  const auto coverage_enabled = canonical.boolean();
  if (!coverage_schema || !coverage_enabled
      || !read_string(metadata.code_coverage.model)
      || !read_string(metadata.code_coverage.digest)) {
    error(
        diagnostics, kSchemaCode,
        "truncated .fsimobj code-coverage identity", source_name);
    return std::nullopt;
  }
  metadata.code_coverage.schema = *coverage_schema;
  metadata.code_coverage.enabled = *coverage_enabled;
  const auto define_count = canonical.count();
  if (!define_count.has_value()) {
    error(diagnostics, kSchemaCode, "invalid object define count", source_name);
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *define_count; ++index) {
    auto value = canonical.string();
    if (!value.has_value()) {
      error(diagnostics, kSchemaCode, "truncated object define", source_name);
      return std::nullopt;
    }
    metadata.defines.push_back(std::move(*value));
  }
  const auto include_count = canonical.count();
  if (!include_count.has_value()) {
    error(diagnostics, kSchemaCode, "invalid object include-root count", source_name);
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *include_count; ++index) {
    auto value = canonical.path();
    if (!value.has_value()) {
      error(diagnostics, kSchemaCode, "truncated object include root", source_name);
      return std::nullopt;
    }
    metadata.include_roots.push_back(std::move(*value));
  }
  const auto dependency_count = canonical.count();
  if (!dependency_count.has_value()) {
    error(diagnostics, kSchemaCode, "invalid VHDL package dependency count", source_name);
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *dependency_count; ++index) {
    library::VhdlPackageDependency item;
    if (!read_string(item.standard)
        || !read_string(item.predefined_environment)
        || !read_string(item.package) || !read_string(item.revision)
        || !read_string(item.source_digest)) {
      error(diagnostics, kSchemaCode, "truncated VHDL package dependency", source_name);
      return std::nullopt;
    }
    metadata.vhdl_package_dependencies.push_back(std::move(item));
  }
  const auto source_count = canonical.count();
  if (!source_count.has_value()) {
    error(diagnostics, kSchemaCode, "invalid object source count", source_name);
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *source_count; ++index) {
    library::SourceIndexEntry item;
    if (!read_string(item.logical_name)) {
      error(diagnostics, kSchemaCode, "truncated object source index", source_name);
      return std::nullopt;
    }
    auto artifact = canonical.path();
    if (!artifact.has_value() || !read_string(item.checksum)
        || !read_string(item.language) || !read_string(item.standard)
        || !read_string(item.compatibility_profile)) {
      error(diagnostics, kSchemaCode, "truncated object source index", source_name);
      return std::nullopt;
    }
    item.artifact = std::move(*artifact);
    metadata.sources.push_back(std::move(item));
  }
  auto coverage = read_source_coverage(canonical, source_name, diagnostics);
  if (!coverage) {
    return std::nullopt;
  }
  metadata.source_coverage = std::move(*coverage);
  const auto unit_count = canonical.count();
  if (!unit_count.has_value()) {
    error(diagnostics, kSchemaCode, "invalid object unit count", source_name);
    return std::nullopt;
  }
  for (std::size_t index = 0; index < *unit_count; ++index) {
    library::UnitIndexEntry item;
    if (!read_string(item.language) || !read_string(item.kind)
        || !read_string(item.name) || !read_string(item.primary_name)
        || !read_string(item.architecture)) {
      error(diagnostics, kSchemaCode, "truncated object unit index", source_name);
      return std::nullopt;
    }
    auto artifact = canonical.path();
    if (!artifact.has_value() || !read_string(item.checksum)
        || !read_string(item.standard)
        || !read_string(item.compatibility_profile)) {
      error(diagnostics, kSchemaCode, "truncated object unit index", source_name);
      return std::nullopt;
    }
    const auto name_form = canonical.u32();
    const auto primary_name_form = canonical.u32();
    if (!name_form || !primary_name_form
        || *name_form > static_cast<std::uint32_t>(
            library::VhdlIdentifierForm::extended)
        || *primary_name_form > static_cast<std::uint32_t>(
            library::VhdlIdentifierForm::extended)) {
      error(diagnostics, kSchemaCode,
          "invalid object VHDL identifier-form value", source_name);
      return std::nullopt;
    }
    item.name_form = static_cast<library::VhdlIdentifierForm>(*name_form);
    item.primary_name_form
        = static_cast<library::VhdlIdentifierForm>(*primary_name_form);
    item.artifact = std::move(*artifact);
    metadata.units.push_back(std::move(item));
  }
  if (canonical.remaining() != 0) {
    error(diagnostics, kSchemaCode, "trailing object metadata bytes", source_name);
    return std::nullopt;
  }
  return validate_metadata(metadata, diagnostics, source_name)
      ? std::optional{std::move(metadata)} : std::nullopt;
}

std::optional<ObjectMetadata> load_object_metadata(
    const std::filesystem::path& object,
    diagnostic::Engine& diagnostics) {
  std::string failure;
  auto reader = ContainerReader::open(object, kObjectArtifactKind, failure);
  auto bytes = reader
      ? reader->read(kObjectMetadataFilename, kMaximumMetadataBytes, failure)
      : std::nullopt;
  if (!bytes) {
    error(diagnostics, kIoCode, "cannot read .fsimobj metadata: " + failure,
        support::path_to_utf8(object));
    return std::nullopt;
  }
  return deserialize_object_metadata(
      *bytes, support::path_to_utf8(object), diagnostics);
}

std::optional<std::string> read_object_payload(
    const std::filesystem::path& object,
    const std::filesystem::path& payload,
    const std::string_view expected_checksum,
    const std::uint64_t maximum_bytes,
    diagnostic::Engine& diagnostics) {
  std::string failure;
  const auto name = support::path_to_utf8(payload);
  auto reader = ContainerReader::open(object, kObjectArtifactKind, failure);
  const auto entry = reader ? reader->entry(name, failure) : std::nullopt;
  if (entry && entry->size > maximum_bytes) {
    error(diagnostics, "FSIM-ART-0005",
        ".fsimobj payload " + name + " exceeds the "
            + (payload.extension() == ".fsimhir" ? "compiled-HIR " : "")
            + "decode byte budget", support::path_to_utf8(object));
    return std::nullopt;
  }
  auto bytes = entry ? reader->read(name, maximum_bytes, failure) : std::nullopt;
  if (!bytes && failure.empty()) {
    failure = "payload '" + name + "' is missing";
  }
  if (!bytes) {
    error(diagnostics, "FSIM-ART-0005", "cannot read .fsimobj payload: " + failure,
        support::path_to_utf8(object));
    return std::nullopt;
  }
  if (!support::payload_checksum_matches(*bytes, expected_checksum)) {
    error(diagnostics, "FSIM-ART-0005", ".fsimobj payload checksum mismatch: "
        + support::path_to_utf8(payload), support::path_to_utf8(object));
    return std::nullopt;
  }
  return bytes;
}

bool publish_object(
    const std::filesystem::path& destination,
    const ObjectMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics) {
  if (destination.empty()) {
    error(diagnostics, kIoCode, "object output path must not be empty");
    return false;
  }
  if (metadata.format != kObjectFormatVersion
      || metadata.portable_schema != library::kPortableSchemaVersion
      || metadata.compiled_hir_schema
          != library::kCompiledHirSchemaVersion) {
      error(
          diagnostics, kSchemaCode,
          diagnostic::unsupported_artifact_identity(
              ".fsimobj publication",
              "format " + std::to_string(metadata.format)
                  + ", portable-unit schema "
                  + std::to_string(metadata.portable_schema)
                  + ", and compiled-HIR schema "
                  + std::to_string(metadata.compiled_hir_schema),
              "format " + std::to_string(kObjectFormatVersion)
                  + ", portable-unit schema "
                  + std::to_string(library::kPortableSchemaVersion)
                  + ", and compiled-HIR schema "
                  + std::to_string(library::kCompiledHirSchemaVersion),
              ".fsimobj"));
      return false;
  }
  diagnostic::Engine validation;
  if (!validate_metadata(
          metadata, validation, std::string { kObjectMetadataFilename })) {
      error(
          diagnostics, kIoCode,
          "invalid object metadata supplied for publication");
      return false;
  }
  const auto canonical = serialize_object_metadata(metadata);
  if (!deserialize_object_metadata(
          canonical, std::string{kObjectMetadataFilename}, validation)) {
    error(diagnostics, kIoCode, "invalid object metadata supplied for publication");
    return false;
  }
  std::unordered_map<std::string, std::string> expected;
  for (const auto& unit : metadata.units) {
    if (unit.artifact.empty()) {
      continue;
    }
    expected.emplace(support::path_to_utf8(unit.artifact), unit.checksum);
  }
  expected.emplace(
      support::path_to_utf8(metadata.compiled_hir_artifact),
      metadata.compiled_hir_checksum);
  std::unordered_set<std::string> supplied;
  for (const auto& payload : payloads) {
    const auto path = support::path_to_utf8(payload.path);
    const auto found = expected.find(path);
    if (!safe_relative_path(payload.path) || found == expected.end()
        || !supplied.insert(path).second
        || !support::payload_checksum_matches(payload.bytes, found->second)) {
      error(
          diagnostics, kIoCode,
          "object payload is unsafe, duplicate, unindexed, or has a checksum "
          "mismatch: " + path);
      return false;
    }
  }
  if (supplied.size() != expected.size()) {
    error(diagnostics, kIoCode, "object payload set is incomplete");
    return false;
  }
  const auto parent = destination.parent_path().empty()
      ? std::filesystem::path{"."} : destination.parent_path();
  std::error_code parent_error;
  support::native_fs::create_directories(parent, parent_error);
  if (parent_error) {
    error(
        diagnostics, kIoCode,
        "cannot create object output parent: " + parent_error.message());
    return false;
  }
  std::string failure;
  auto writer = ContainerWriter::create(
      parent / destination.filename(), kObjectArtifactKind,
      artifact::ContainerPages::compact, failure);
  bool written = writer.has_value()
      && writer->meta("format", std::to_string(kObjectFormatVersion), failure)
      && writer->meta("library", metadata.library, failure)
      && writer->meta("language", metadata.language, failure)
      && writer->put(kObjectMetadataFilename, canonical, failure);
  std::string units;
  for (const auto& unit : metadata.units) {
    units += units.empty() ? "" : " ";
    units += unit.architecture.empty() ? unit.name : unit.name + "(" + unit.architecture + ")";
  }
  written = written && writer->meta("units", units, failure);
  for (const auto& payload : payloads) {
    written = written
        && writer->put(support::path_to_utf8(payload.path), payload.bytes, failure);
  }
  if (!written || !writer->publish(failure)) {
    error(diagnostics, kIoCode, "cannot publish object artifact: " + failure,
        support::path_to_utf8(destination));
    return false;
  }
  return true;
}

std::string serialize_source_coverage(
    const std::vector<ObjectSourceCoverage>& coverage) {
  Writer writer;
  writer.string(kSourceCoverageMagic);
  write_source_coverage(writer, coverage);
  return std::move(writer).take();
}

std::optional<std::vector<ObjectSourceCoverage>> deserialize_source_coverage(
    const std::string_view bytes,
    const std::string& source_name,
    diagnostic::Engine& diagnostics) {
  Reader canonical{bytes};
  if (canonical.string() != std::optional<std::string>{std::string{kSourceCoverageMagic}}) {
    error(diagnostics, kSchemaCode, "invalid source coverage payload header", source_name);
    return std::nullopt;
  }
  auto coverage = read_source_coverage(canonical, source_name, diagnostics);
  if (!coverage) {
    return std::nullopt;
  }
  if (canonical.remaining() != 0U) {
    error(diagnostics, kSchemaCode, "trailing bytes after source coverage facts", source_name);
    return std::nullopt;
  }
  for (const auto& item : *coverage) {
    if (!valid_control_error(item.control_error)
        || !std::ranges::all_of(item.exclusions, [](const auto& exclusion) {
             return valid_coverage_metric(exclusion.metric)
                 && exclusion.begin_offset <= exclusion.end_offset;
           })) {
      error(diagnostics, kValueCode, "source coverage facts are invalid", source_name);
      return std::nullopt;
    }
  }
  return coverage;
}

}  // namespace fsim::artifact
