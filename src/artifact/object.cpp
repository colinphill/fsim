// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/object.hpp"

#include "fsim/support/path.hpp"
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
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fsim::artifact {
namespace {

constexpr std::array<char, 8> kMagic{'F', 'S', 'I', 'M', 'O', 'B', 'J', '\0'};
constexpr std::string_view kSchemaCode = "FSIM-ART-0001";
constexpr std::string_view kValueCode = "FSIM-ART-0002";
constexpr std::string_view kIoCode = "FSIM-ART-0003";
std::atomic_uint64_t staging_sequence{};

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

bool safe_relative_path(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_name()
      || path.has_root_directory()) {
    return false;
  }
  const auto normalized = path.lexically_normal();
  return normalized == path
      && std::ranges::none_of(path, [](const auto& component) {
           return component == ".." || component == ".";
         });
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
    output_.append(value.data(), value.size());
  }

  void u32(const std::uint32_t value) {
    for (std::size_t shift = 0; shift < 32; shift += 8) {
      output_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
  }

  void u64(const std::uint64_t value) {
    for (std::size_t shift = 0; shift < 64; shift += 8) {
      output_.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
  }

  void string(const std::string_view value) {
    u64(value.size());
    output_.append(value);
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
  std::string output_;
};

class Reader {
 public:
  explicit Reader(const std::string_view input) : input_(input) {}

  bool bytes(const std::span<const char> expected) {
    if (expected.size() > remaining()
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
    std::uint32_t value = 0;
    for (std::size_t shift = 0; shift < 32; shift += 8) {
      value |= static_cast<std::uint32_t>(
          static_cast<unsigned char>(input_[position_++])) << shift;
    }
    return value;
  }

  std::optional<std::uint64_t> u64() {
    if (remaining() < 8) {
      return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(
          static_cast<unsigned char>(input_[position_++])) << shift;
    }
    return value;
  }

  std::optional<std::string> string() {
    const auto size = u64();
    if (!size.has_value() || *size > remaining()
        || *size > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    std::string result{input_.substr(position_, static_cast<std::size_t>(*size))};
    position_ += static_cast<std::size_t>(*size);
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
    return input_.size() - position_;
  }

 private:
  std::string_view input_;
  std::size_t position_{};
};

std::string compilation_digest(const ObjectMetadata& metadata) {
  Writer writer;
  writer.string("fsim-object-compilation-v6-trace-profile");
  writer.string(metadata.language);
  writer.string(metadata.standard);
  writer.string(metadata.compatibility_profile);
  writer.string(metadata.library);
  writer.string(metadata.compilation_unit);
  writer.string(metadata.uvm_release);
  writer.string(metadata.trace_archive);
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
  });
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

bool validate_metadata(
    const ObjectMetadata& metadata,
    diagnostic::Engine& diagnostics,
    const std::string& source) {
  if (metadata.format != kObjectFormatVersion
      || metadata.portable_schema != library::kPortableSchemaVersion) {
    error(
        diagnostics, kSchemaCode,
        "unsupported .fsimobj format or portable-unit schema", source);
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
    if (!safe_relative_path(include) || !include_roots.insert(spelling).second) {
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
  for (const auto& item : metadata.sources) {
    const auto path = support::path_to_utf8(item.artifact);
    if (item.logical_name.empty() || !safe_relative_path(item.logical_name)
        || !logical_sources.insert(item.logical_name).second
        || !safe_relative_path(item.artifact)
        || !checksum_spelling(item.checksum)
        || item.language != metadata.language
        || item.standard != metadata.standard
        || item.compatibility_profile != metadata.compatibility_profile
        || !payload_paths.insert(path).second) {
      error(
          diagnostics, kValueCode,
          "object sources require unique relative logical/payload paths, "
          "matching language, standard, compatibility profile, and lowercase "
          "SHA-256 checksums", source);
    }
  }
  for (const auto& item : metadata.units) {
    const auto path = support::path_to_utf8(item.artifact);
    if (item.language != metadata.language || item.kind.empty()
        || item.name.empty() || !safe_relative_path(item.artifact)
        || !checksum_spelling(item.checksum)
        || item.standard != metadata.standard
        || item.compatibility_profile != metadata.compatibility_profile
        || !payload_paths.insert(path).second) {
      error(
          diagnostics, kValueCode,
          "object units require matching language, standard, compatibility "
          "profile, identity, unique contained payload, and lowercase SHA-256 "
          "checksum", source);
    }
  }
  if (metadata.sources.empty() || metadata.units.empty()) {
    error(
        diagnostics, kValueCode,
        "object metadata must index source and owning-unit payloads", source);
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

std::filesystem::path staging_path(const std::filesystem::path& destination) {
  const auto sequence = staging_sequence.fetch_add(1, std::memory_order_relaxed);
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return destination.parent_path()
      / ("." + destination.filename().string() + ".staging-"
         + std::to_string(nonce) + "-" + std::to_string(sequence));
}

class StagingCleanup {
 public:
  explicit StagingCleanup(std::filesystem::path path) : path_(std::move(path)) {}
  ~StagingCleanup() {
    if (!path_.empty()) {
      std::error_code error;
      for (std::filesystem::recursive_directory_iterator iterator(
               path_, error), end;
           !error && iterator != end;
           iterator.increment(error)) {
        std::filesystem::permissions(
            iterator->path(), std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, error);
        error.clear();
      }
      std::filesystem::permissions(
          path_, std::filesystem::perms::owner_all,
          std::filesystem::perm_options::add, error);
      error.clear();
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
  if (error) {
    fsim::artifact::error(
        diagnostics, kIoCode,
        "cannot create object payload directory: " + error.message(),
        support::path_to_utf8(path));
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    fsim::artifact::error(
        diagnostics, kIoCode, "cannot write object payload",
        support::path_to_utf8(path));
    return false;
  }
  return true;
}

bool make_tree_read_only(
    const std::filesystem::path& root,
    diagnostic::Engine& diagnostics) {
  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end;
       iterator.increment(error)) {
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
    fsim::artifact::error(
        diagnostics, kIoCode,
        "cannot make staged object artifact read-only: " + error.message(),
        support::path_to_utf8(root));
    return false;
  }
  return true;
}

}  // namespace

std::string compute_object_compilation_digest(
    const ObjectMetadata& metadata) {
  return compilation_digest(metadata);
}

std::string serialize_object_metadata(const ObjectMetadata& metadata) {
  Writer writer;
  writer.bytes(kMagic);
  writer.u32(metadata.format);
  writer.u32(metadata.portable_schema);
  writer.string(metadata.producer);
  writer.string(metadata.language);
  writer.string(metadata.standard);
  writer.string(metadata.compatibility_profile);
  writer.string(metadata.library);
  writer.string(metadata.compilation_unit);
  writer.string(metadata.uvm_release);
  writer.string(metadata.compilation_digest);
  writer.string(metadata.trace_archive);
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
  if (!read_format.has_value() || !read_schema.has_value()) {
    error(diagnostics, kSchemaCode, "truncated .fsimobj metadata header", source_name);
    return std::nullopt;
  }
  metadata.format = *read_format;
  metadata.portable_schema = *read_schema;
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
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics) {
  const auto path = directory / kObjectMetadataFilename;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error(
        diagnostics, kIoCode, "cannot open .fsimobj metadata",
        support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    error(
        diagnostics, kIoCode, "cannot read .fsimobj metadata",
        support::path_to_utf8(path));
    return std::nullopt;
  }
  return deserialize_object_metadata(
      contents.str(), support::path_to_utf8(path), diagnostics);
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
  std::error_code exists_error;
  if (std::filesystem::exists(destination, exists_error) || exists_error) {
    error(
        diagnostics, kIoCode,
        "object output already exists or cannot be inspected",
        support::path_to_utf8(destination));
    return false;
  }
  diagnostic::Engine validation;
  const auto canonical = serialize_object_metadata(metadata);
  if (!deserialize_object_metadata(
          canonical, std::string{kObjectMetadataFilename}, validation)) {
    error(diagnostics, kIoCode, "invalid object metadata supplied for publication");
    return false;
  }
  std::unordered_map<std::string, std::string> expected;
  for (const auto& source : metadata.sources) {
    expected.emplace(support::path_to_utf8(source.artifact), source.checksum);
  }
  for (const auto& unit : metadata.units) {
    expected.emplace(support::path_to_utf8(unit.artifact), unit.checksum);
  }
  std::unordered_set<std::string> supplied;
  for (const auto& payload : payloads) {
    const auto path = support::path_to_utf8(payload.path);
    const auto found = expected.find(path);
    if (!safe_relative_path(payload.path) || found == expected.end()
        || !supplied.insert(path).second
        || found->second != support::Sha256::hex(
            support::Sha256::digest(payload.bytes))) {
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
  std::filesystem::create_directories(parent, parent_error);
  if (parent_error) {
    error(
        diagnostics, kIoCode,
        "cannot create object output parent: " + parent_error.message());
    return false;
  }
  const auto staging = staging_path(destination);
  StagingCleanup cleanup{staging};
  std::error_code stage_error;
  if (!std::filesystem::create_directory(staging, stage_error) || stage_error) {
    error(
        diagnostics, kIoCode,
        "cannot create object staging directory: " + stage_error.message());
    return false;
  }
  for (const auto& payload : payloads) {
    if (!write_file(staging / payload.path, payload.bytes, diagnostics)) {
      return false;
    }
  }
  if (!write_file(
          staging / kObjectMetadataFilename, canonical, diagnostics)
      || !make_tree_read_only(staging, diagnostics)) {
    return false;
  }
  std::error_code install_error;
  std::filesystem::rename(staging, destination, install_error);
  if (install_error) {
    error(
        diagnostics, kIoCode,
        "cannot atomically install object artifact: "
            + install_error.message());
    return false;
  }
  cleanup.release();
  return true;
}

}  // namespace fsim::artifact
