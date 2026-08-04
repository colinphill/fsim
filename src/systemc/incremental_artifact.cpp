// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/incremental.hpp"

#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc_abi.h"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <span>
#include <system_error>
#include <unordered_set>

namespace fsim::systemc {
namespace {

constexpr std::array<char, 8> kObjectMagic{
    'F', 'S', 'I', 'M', 'S', 'C', 'O', '\0'};
constexpr std::array<char, 8> kPluginMagic{
    'F', 'S', 'I', 'M', 'S', 'C', 'P', '\0'};
constexpr std::string_view kSchemaCode = "FSIM-SC-I001";
constexpr std::string_view kValueCode = "FSIM-SC-I002";
constexpr std::string_view kIoCode = "FSIM-SC-I003";
std::atomic_uint64_t staging_sequence{};

void report(
    diagnostic::Engine& diagnostics,
    const std::string_view code,
    std::string message,
    const std::string& path = {}) {
  diagnostics.error(
      std::string{code}, std::move(message),
      {path, {1, 1, 0}, {1, 1, 0}});
}

bool checksum(const std::string_view value) {
  return value.size() == 64
      && std::ranges::all_of(value, [](const unsigned char character) {
           return (character >= '0' && character <= '9')
               || (character >= 'a' && character <= 'f');
         });
}

bool safe_path(const std::filesystem::path& path) {
  return !path.empty() && !path.is_absolute() && !path.has_root_name()
      && path == path.lexically_normal()
      && std::ranges::none_of(path, [](const auto& component) {
           return component == "." || component == "..";
         });
}

bool library_name(const std::string_view value) {
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
  void raw(const std::span<const char> bytes) {
    output_.append(bytes.data(), bytes.size());
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
  void i64(const std::int64_t value) {
    u64(std::bit_cast<std::uint64_t>(value));
  }
  void boolean(const bool value) { output_.push_back(value ? '\1' : '\0'); }
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
  std::string take() && { return std::move(output_); }

 private:
  std::string output_;
};

class Reader {
 public:
  explicit Reader(const std::string_view input) : input_(input) {}
  bool raw(const std::span<const char> expected) {
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
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
      result |= static_cast<std::uint32_t>(
          static_cast<unsigned char>(input_[position_++])) << shift;
    }
    return result;
  }
  std::optional<std::uint64_t> u64() {
    if (remaining() < 8) {
      return std::nullopt;
    }
    std::uint64_t result = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
      result |= static_cast<std::uint64_t>(
          static_cast<unsigned char>(input_[position_++])) << shift;
    }
    return result;
  }
  std::optional<std::int64_t> i64() {
    const auto value = u64();
    return value
        ? std::optional{std::bit_cast<std::int64_t>(*value)} : std::nullopt;
  }
  std::optional<bool> boolean() {
    if (remaining() == 0 || static_cast<unsigned char>(input_[position_]) > 1) {
      return std::nullopt;
    }
    return input_[position_++] != '\0';
  }
  std::optional<std::string> string() {
    const auto size = u64();
    if (!size || *size > remaining()
        || *size > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    std::string result{
        input_.substr(position_, static_cast<std::size_t>(*size))};
    position_ += static_cast<std::size_t>(*size);
    return result;
  }
  std::optional<std::filesystem::path> path() {
    auto value = string();
    return value
        ? std::optional{support::path_from_utf8(*value)} : std::nullopt;
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

void write_strings(Writer& writer, const std::vector<std::string>& values) {
  writer.sequence(values, [&](const auto& value) { writer.string(value); });
}

bool read_strings(Reader& reader, std::vector<std::string>& values) {
  const auto count = reader.count();
  if (!count) {
    return false;
  }
  values.reserve(*count);
  for (std::size_t index = 0; index < *count; ++index) {
    auto value = reader.string();
    if (!value) {
      return false;
    }
    values.push_back(std::move(*value));
  }
  return true;
}

void write_inputs(
    Writer& writer, const std::vector<NativeInputIdentity>& values) {
  writer.sequence(values, [&](const auto& value) {
    writer.string(value.logical_name);
    writer.string(value.checksum);
  });
}

bool read_inputs(
    Reader& reader, std::vector<NativeInputIdentity>& values) {
  const auto count = reader.count();
  if (!count) {
    return false;
  }
  values.reserve(*count);
  for (std::size_t index = 0; index < *count; ++index) {
    auto name = reader.string();
    auto digest = reader.string();
    if (!name || !digest) {
      return false;
    }
    values.push_back({std::move(*name), std::move(*digest)});
  }
  return true;
}

std::string object_input_digest(const IncrementalObjectMetadata& metadata) {
  Writer writer;
  writer.string("fsim-systemc-object-input-v1");
  writer.u32(metadata.runtime_abi);
  writer.u32(metadata.systemc_abi);
  writer.string(metadata.toolchain);
  writer.string(metadata.target);
  writer.string(metadata.compiler_fingerprint);
  write_strings(writer, metadata.defines);
  write_strings(writer, metadata.compile_options);
  write_inputs(writer, metadata.inputs);
  writer.boolean(metadata.defines_plugin_entry_point);
  writer.boolean(metadata.contains_macro_export);
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

std::string object_digest(const IncrementalObjectMetadata& metadata) {
  Writer writer;
  writer.string("fsim-systemc-object-v1");
  writer.string(metadata.input_digest);
  writer.string(metadata.object_checksum);
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

std::string plugin_input_digest(const IncrementalPluginMetadata& metadata) {
  Writer writer;
  writer.string("fsim-systemc-plugin-input-v1");
  writer.u32(metadata.runtime_abi);
  writer.u32(metadata.systemc_abi);
  writer.string(metadata.logical_library);
  writer.string(metadata.toolchain);
  writer.string(metadata.target);
  writer.string(metadata.compiler_fingerprint);
  write_strings(writer, metadata.object_digests);
  write_strings(writer, metadata.link_options);
  write_strings(writer, metadata.libraries);
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

std::string plugin_digest(const IncrementalPluginMetadata& metadata) {
  Writer writer;
  writer.string("fsim-systemc-plugin-v1");
  writer.string(metadata.input_digest);
  writer.sequence(metadata.factories, [&](const auto& factory) {
    writer.string(factory.name);
    writer.sequence(factory.parameters, [&](const auto& parameter) {
      writer.string(parameter.name);
      writer.u32(parameter.type);
      writer.boolean(parameter.has_default);
      writer.i64(parameter.default_value);
    });
  });
  writer.string(metadata.library_checksum);
  return support::Sha256::hex(
      support::Sha256::digest(std::move(writer).take()));
}

bool validate_object(
    const IncrementalObjectMetadata& metadata,
    diagnostic::Engine& diagnostics,
    const std::string& source) {
  if (metadata.format != kIncrementalObjectFormatVersion
      || metadata.runtime_abi != runtime_abi_version
      || metadata.systemc_abi != FSIM_SYSTEMC_ABI_VERSION) {
    report(
        diagnostics, kSchemaCode,
        "incompatible .fsimscobj format or runtime/SystemC ABI", source);
  }
  if (metadata.producer.empty() || metadata.toolchain.empty()
      || metadata.target.empty() || !checksum(metadata.compiler_fingerprint)
      || !checksum(metadata.input_digest)
      || !checksum(metadata.compilation_digest)
      || !safe_path(metadata.object) || !checksum(metadata.object_checksum)
      || metadata.inputs.empty()) {
    report(
        diagnostics, kValueCode,
        "SystemC object metadata is missing a required identity or payload",
        source);
  }
  std::unordered_set<std::string> names;
  for (const auto& input : metadata.inputs) {
    if (input.logical_name.empty() || !checksum(input.checksum)
        || !names.insert(input.logical_name).second) {
      report(
          diagnostics, kValueCode,
          "SystemC object inputs require unique logical names and checksums",
          source);
    }
  }
  if (checksum(metadata.compilation_digest)
      && metadata.compilation_digest != object_digest(metadata)) {
    report(
        diagnostics, kValueCode,
        "SystemC object compilation digest does not match its inputs", source);
  }
  if (checksum(metadata.input_digest)
      && metadata.input_digest != object_input_digest(metadata)) {
    report(
        diagnostics, kValueCode,
        "SystemC object input digest does not match its inputs", source);
  }
  return !diagnostics.has_error();
}

bool validate_plugin(
    const IncrementalPluginMetadata& metadata,
    diagnostic::Engine& diagnostics,
    const std::string& source) {
  if (metadata.format != kIncrementalPluginFormatVersion
      || metadata.runtime_abi != runtime_abi_version
      || metadata.systemc_abi != FSIM_SYSTEMC_ABI_VERSION) {
    report(
        diagnostics, kSchemaCode,
        "incompatible .fsimscplugin format or runtime/SystemC ABI", source);
  }
  if (metadata.producer.empty() || !library_name(metadata.logical_library)
      || metadata.toolchain.empty() || metadata.target.empty()
      || !checksum(metadata.compiler_fingerprint)
      || !checksum(metadata.input_digest)
      || !checksum(metadata.link_digest) || metadata.object_digests.empty()
      || !safe_path(metadata.library) || !checksum(metadata.library_checksum)
      || metadata.factories.empty()) {
    report(
        diagnostics, kValueCode,
        "SystemC plug-in metadata is missing a required identity, object, "
        "factory, or payload", source);
  }
  std::unordered_set<std::string> objects;
  for (const auto& digest : metadata.object_digests) {
    if (!checksum(digest) || !objects.insert(digest).second) {
      report(
          diagnostics, kValueCode,
          "SystemC plug-in object identities must be unique checksums", source);
    }
  }
  std::string previous;
  for (const auto& factory : metadata.factories) {
    if (factory.name.empty() || (!previous.empty() && factory.name <= previous)) {
      report(
          diagnostics, kValueCode,
          "SystemC factory inventory must be non-empty, unique, and sorted",
          source);
    }
    previous = factory.name;
    std::unordered_set<std::string> parameters;
    for (const auto& parameter : factory.parameters) {
      if (parameter.name.empty()
          || !parameters.insert(parameter.name).second
          || parameter.type > FSIM_SC_CONSTRUCTION_BIT) {
        report(
            diagnostics, kValueCode,
            "SystemC factory parameters must have unique names and valid types",
            source);
      }
    }
  }
  if (checksum(metadata.link_digest)
      && metadata.link_digest != plugin_digest(metadata)) {
    report(
        diagnostics, kValueCode,
        "SystemC plug-in link digest does not match its inputs", source);
  }
  if (checksum(metadata.input_digest)
      && metadata.input_digest != plugin_input_digest(metadata)) {
    report(
        diagnostics, kValueCode,
        "SystemC plug-in input digest does not match its inputs", source);
  }
  return !diagnostics.has_error();
}

std::optional<std::string> read_file(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics,
    const std::string_view description) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    report(
        diagnostics, kIoCode,
        "cannot open " + std::string{description}, support::path_to_utf8(path));
    return std::nullopt;
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  if (!input.good() && !input.eof()) {
    report(
        diagnostics, kIoCode,
        "cannot read " + std::string{description}, support::path_to_utf8(path));
    return std::nullopt;
  }
  return std::move(bytes).str();
}

std::filesystem::path staging_path(const std::filesystem::path& destination) {
  const auto serial = staging_sequence.fetch_add(1, std::memory_order_relaxed);
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  return destination.parent_path()
      / ("." + destination.filename().string() + ".staging-"
         + std::to_string(tick) + "-" + std::to_string(serial));
}

class Cleanup {
 public:
  explicit Cleanup(std::filesystem::path path) : path_(std::move(path)) {}
  ~Cleanup() {
    if (path_.empty()) {
      return;
    }
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(path_, error), end;
         !error && iterator != end; iterator.increment(error)) {
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
    report(
        diagnostics, kIoCode,
        "cannot create native artifact payload directory: " + error.message(),
        support::path_to_utf8(path));
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    report(
        diagnostics, kIoCode, "cannot write native artifact payload",
        support::path_to_utf8(path));
    return false;
  }
  return true;
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
        "cannot make native artifact read-only: " + error.message(),
        support::path_to_utf8(root));
    return false;
  }
  return true;
}

bool publish(
    const std::filesystem::path& destination,
    const std::filesystem::path& metadata_name,
    const std::string_view metadata,
    const std::filesystem::path& payload_name,
    const std::string_view payload,
    diagnostic::Engine& diagnostics) {
  if (destination.empty()) {
    report(diagnostics, kIoCode, "native artifact output must not be empty");
    return false;
  }
  std::error_code error;
  if (std::filesystem::exists(destination, error) || error) {
    report(
        diagnostics, kIoCode,
        "native artifact output already exists or cannot be inspected",
        support::path_to_utf8(destination));
    return false;
  }
  const auto parent = destination.parent_path().empty()
      ? std::filesystem::path{"."} : destination.parent_path();
  std::filesystem::create_directories(parent, error);
  if (error) {
    report(
        diagnostics, kIoCode,
        "cannot create native artifact parent: " + error.message(),
        support::path_to_utf8(parent));
    return false;
  }
  const auto staging = staging_path(destination);
  Cleanup cleanup{staging};
  if (!std::filesystem::create_directory(staging, error) || error
      || !write_file(staging / payload_name, payload, diagnostics)
      || !write_file(staging / metadata_name, metadata, diagnostics)
      || !make_read_only(staging, diagnostics)) {
    return false;
  }
  std::filesystem::rename(staging, destination, error);
  if (error) {
    report(
        diagnostics, kIoCode,
        "cannot atomically install native artifact: " + error.message(),
        support::path_to_utf8(destination));
    return false;
  }
  cleanup.release();
  return true;
}

}  // namespace

std::string compute_incremental_object_digest(
    const IncrementalObjectMetadata& metadata) {
  return object_digest(metadata);
}

std::string compute_incremental_object_input_digest(
    const IncrementalObjectMetadata& metadata) {
  return object_input_digest(metadata);
}

std::string compute_incremental_plugin_digest(
    const IncrementalPluginMetadata& metadata) {
  return plugin_digest(metadata);
}

std::string compute_incremental_plugin_input_digest(
    const IncrementalPluginMetadata& metadata) {
  return plugin_input_digest(metadata);
}

std::string serialize_incremental_object_metadata(
    const IncrementalObjectMetadata& metadata) {
  Writer writer;
  writer.raw(kObjectMagic);
  writer.u32(metadata.format);
  writer.u32(metadata.runtime_abi);
  writer.u32(metadata.systemc_abi);
  writer.string(metadata.producer);
  writer.string(metadata.toolchain);
  writer.string(metadata.target);
  writer.string(metadata.compiler_fingerprint);
  writer.string(metadata.input_digest);
  writer.string(metadata.compilation_digest);
  write_strings(writer, metadata.defines);
  write_strings(writer, metadata.compile_options);
  write_inputs(writer, metadata.inputs);
  writer.boolean(metadata.defines_plugin_entry_point);
  writer.boolean(metadata.contains_macro_export);
  writer.path(metadata.object);
  writer.string(metadata.object_checksum);
  return std::move(writer).take();
}

std::string serialize_incremental_plugin_metadata(
    const IncrementalPluginMetadata& metadata) {
  Writer writer;
  writer.raw(kPluginMagic);
  writer.u32(metadata.format);
  writer.u32(metadata.runtime_abi);
  writer.u32(metadata.systemc_abi);
  writer.string(metadata.producer);
  writer.string(metadata.logical_library);
  writer.string(metadata.toolchain);
  writer.string(metadata.target);
  writer.string(metadata.compiler_fingerprint);
  writer.string(metadata.input_digest);
  writer.string(metadata.link_digest);
  write_strings(writer, metadata.object_digests);
  write_strings(writer, metadata.link_options);
  write_strings(writer, metadata.libraries);
  writer.sequence(metadata.factories, [&](const auto& factory) {
    writer.string(factory.name);
    writer.sequence(factory.parameters, [&](const auto& parameter) {
      writer.string(parameter.name);
      writer.u32(parameter.type);
      writer.boolean(parameter.has_default);
      writer.i64(parameter.default_value);
    });
  });
  writer.path(metadata.library);
  writer.string(metadata.library_checksum);
  return std::move(writer).take();
}

std::optional<IncrementalObjectMetadata>
deserialize_incremental_object_metadata(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Reader reader{bytes};
  IncrementalObjectMetadata metadata;
  if (!reader.raw(kObjectMagic)) {
    report(diagnostics, kSchemaCode, "invalid .fsimscobj metadata magic", source_name);
    return std::nullopt;
  }
  const auto format = reader.u32();
  const auto runtime = reader.u32();
  const auto abi = reader.u32();
  auto producer = reader.string();
  auto toolchain = reader.string();
  auto target = reader.string();
  auto compiler = reader.string();
  auto input_digest = reader.string();
  auto digest = reader.string();
  if (!format || !runtime || !abi || !producer || !toolchain || !target
      || !compiler || !input_digest || !digest) {
    report(diagnostics, kSchemaCode, "truncated .fsimscobj metadata", source_name);
    return std::nullopt;
  }
  metadata.format = *format;
  metadata.runtime_abi = *runtime;
  metadata.systemc_abi = *abi;
  metadata.producer = std::move(*producer);
  metadata.toolchain = std::move(*toolchain);
  metadata.target = std::move(*target);
  metadata.compiler_fingerprint = std::move(*compiler);
  metadata.input_digest = std::move(*input_digest);
  metadata.compilation_digest = std::move(*digest);
  if (!read_strings(reader, metadata.defines)
      || !read_strings(reader, metadata.compile_options)
      || !read_inputs(reader, metadata.inputs)) {
    report(diagnostics, kSchemaCode, "truncated .fsimscobj inputs", source_name);
    return std::nullopt;
  }
  const auto entry_point = reader.boolean();
  const auto macro_export = reader.boolean();
  auto object = reader.path();
  auto object_checksum = reader.string();
  if (!entry_point || !macro_export || !object || !object_checksum
      || reader.remaining() != 0) {
    report(
        diagnostics, kSchemaCode,
        "truncated or trailing .fsimscobj payload metadata", source_name);
    return std::nullopt;
  }
  metadata.defines_plugin_entry_point = *entry_point;
  metadata.contains_macro_export = *macro_export;
  metadata.object = std::move(*object);
  metadata.object_checksum = std::move(*object_checksum);
  return validate_object(metadata, diagnostics, source_name)
      ? std::optional{std::move(metadata)} : std::nullopt;
}

std::optional<IncrementalPluginMetadata>
deserialize_incremental_plugin_metadata(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics) {
  Reader reader{bytes};
  IncrementalPluginMetadata metadata;
  if (!reader.raw(kPluginMagic)) {
    report(
        diagnostics, kSchemaCode, "invalid .fsimscplugin metadata magic",
        source_name);
    return std::nullopt;
  }
  const auto format = reader.u32();
  const auto runtime = reader.u32();
  const auto abi = reader.u32();
  auto producer = reader.string();
  auto library = reader.string();
  auto toolchain = reader.string();
  auto target = reader.string();
  auto compiler = reader.string();
  auto input_digest = reader.string();
  auto digest = reader.string();
  if (!format || !runtime || !abi || !producer || !library || !toolchain
      || !target || !compiler || !input_digest || !digest) {
    report(diagnostics, kSchemaCode, "truncated .fsimscplugin metadata", source_name);
    return std::nullopt;
  }
  metadata.format = *format;
  metadata.runtime_abi = *runtime;
  metadata.systemc_abi = *abi;
  metadata.producer = std::move(*producer);
  metadata.logical_library = std::move(*library);
  metadata.toolchain = std::move(*toolchain);
  metadata.target = std::move(*target);
  metadata.compiler_fingerprint = std::move(*compiler);
  metadata.input_digest = std::move(*input_digest);
  metadata.link_digest = std::move(*digest);
  if (!read_strings(reader, metadata.object_digests)
      || !read_strings(reader, metadata.link_options)
      || !read_strings(reader, metadata.libraries)) {
    report(diagnostics, kSchemaCode, "truncated .fsimscplugin inputs", source_name);
    return std::nullopt;
  }
  const auto factory_count = reader.count();
  if (!factory_count) {
    report(diagnostics, kSchemaCode, "invalid SystemC factory count", source_name);
    return std::nullopt;
  }
  metadata.factories.reserve(*factory_count);
  for (std::size_t factory_index = 0; factory_index < *factory_count;
       ++factory_index) {
    auto name = reader.string();
    const auto parameter_count = reader.count();
    if (!name || !parameter_count) {
      report(diagnostics, kSchemaCode, "truncated SystemC factory", source_name);
      return std::nullopt;
    }
    IncrementalFactory factory;
    factory.name = std::move(*name);
    factory.parameters.reserve(*parameter_count);
    for (std::size_t parameter_index = 0;
         parameter_index < *parameter_count; ++parameter_index) {
      auto parameter_name = reader.string();
      const auto type = reader.u32();
      const auto has_default = reader.boolean();
      const auto default_value = reader.i64();
      if (!parameter_name || !type || !has_default || !default_value) {
        report(
            diagnostics, kSchemaCode,
            "truncated SystemC factory parameter", source_name);
        return std::nullopt;
      }
      factory.parameters.push_back({
          std::move(*parameter_name), *type, *has_default, *default_value});
    }
    metadata.factories.push_back(std::move(factory));
  }
  auto payload = reader.path();
  auto payload_checksum = reader.string();
  if (!payload || !payload_checksum || reader.remaining() != 0) {
    report(
        diagnostics, kSchemaCode,
        "truncated or trailing .fsimscplugin payload metadata", source_name);
    return std::nullopt;
  }
  metadata.library = std::move(*payload);
  metadata.library_checksum = std::move(*payload_checksum);
  return validate_plugin(metadata, diagnostics, source_name)
      ? std::optional{std::move(metadata)} : std::nullopt;
}

std::optional<IncrementalObjectMetadata> load_incremental_object_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics) {
  const auto path = directory / kIncrementalObjectMetadataFilename;
  auto bytes = read_file(path, diagnostics, ".fsimscobj metadata");
  if (!bytes) {
    return std::nullopt;
  }
  auto metadata = deserialize_incremental_object_metadata(
      *bytes, support::path_to_utf8(path), diagnostics);
  if (!metadata) {
    return std::nullopt;
  }
  auto payload = read_file(
      directory / metadata->object, diagnostics, ".fsimscobj native payload");
  if (!payload
      || support::Sha256::hex(support::Sha256::digest(*payload))
          != metadata->object_checksum) {
    if (payload) {
      report(
          diagnostics, kIoCode, ".fsimscobj native payload checksum mismatch",
          support::path_to_utf8(directory / metadata->object));
    }
    return std::nullopt;
  }
  return metadata;
}

std::optional<IncrementalPluginMetadata> load_incremental_plugin_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics) {
  const auto path = directory / kIncrementalPluginMetadataFilename;
  auto bytes = read_file(path, diagnostics, ".fsimscplugin metadata");
  if (!bytes) {
    return std::nullopt;
  }
  auto metadata = deserialize_incremental_plugin_metadata(
      *bytes, support::path_to_utf8(path), diagnostics);
  if (!metadata) {
    return std::nullopt;
  }
  auto payload = read_file(
      directory / metadata->library, diagnostics,
      ".fsimscplugin native payload");
  if (!payload
      || support::Sha256::hex(support::Sha256::digest(*payload))
          != metadata->library_checksum) {
    if (payload) {
      report(
          diagnostics, kIoCode,
          ".fsimscplugin native payload checksum mismatch",
          support::path_to_utf8(directory / metadata->library));
    }
    return std::nullopt;
  }
  return metadata;
}

bool publish_incremental_object(
    const std::filesystem::path& destination,
    const IncrementalObjectMetadata& metadata,
    const std::string_view payload,
    diagnostic::Engine& diagnostics) {
  diagnostic::Engine validation;
  const auto bytes = serialize_incremental_object_metadata(metadata);
  if (!deserialize_incremental_object_metadata(
          bytes, std::string{kIncrementalObjectMetadataFilename}, validation)
      || support::Sha256::hex(support::Sha256::digest(payload))
          != metadata.object_checksum) {
    report(diagnostics, kIoCode, "invalid SystemC object supplied for publication");
    return false;
  }
  return publish(
      destination, kIncrementalObjectMetadataFilename, bytes,
      metadata.object, payload, diagnostics);
}

bool publish_incremental_plugin(
    const std::filesystem::path& destination,
    const IncrementalPluginMetadata& metadata,
    const std::string_view payload,
    diagnostic::Engine& diagnostics) {
  diagnostic::Engine validation;
  const auto bytes = serialize_incremental_plugin_metadata(metadata);
  if (!deserialize_incremental_plugin_metadata(
          bytes, std::string{kIncrementalPluginMetadataFilename}, validation)
      || support::Sha256::hex(support::Sha256::digest(payload))
          != metadata.library_checksum) {
    report(diagnostics, kIoCode, "invalid SystemC plug-in supplied for publication");
    return false;
  }
  return publish(
      destination, kIncrementalPluginMetadataFilename, bytes,
      metadata.library, payload, diagnostics);
}

}  // namespace fsim::systemc
