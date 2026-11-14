// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/incremental.hpp"

#include "fsim/artifact/container.hpp"
#include "fsim/compiler/cache_support.hpp"
#include "../diagnostic/artifact_identity.hpp"
#include "fsim/support/bounded_bytes.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"
#include "fsim/systemc_abi.h"
#include "fsim/version.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/systemc/scv.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cctype>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace fsim::systemc {
namespace {

constexpr std::array<char, 8> kObjectMagic{
    'F', 'S', 'I', 'M', 'S', 'C', 'O', '\0'};
constexpr std::array<char, 8> kPluginMagic{
    'F', 'S', 'I', 'M', 'S', 'C', 'P', '\0'};
constexpr std::string_view kSchemaCode = "FSIM-SC-I001";
constexpr std::string_view kValueCode = "FSIM-SC-I002";
constexpr std::string_view kIoCode = "FSIM-SC-I003";

void report(
    diagnostic::Engine& diagnostics,
    const std::string_view code,
    std::string message,
    const std::string& path = {}) {
  diagnostics.error(
      std::string{code}, std::move(message),
      {path, {1, 1, 0}, {1, 1, 0}});
}

std::string incremental_identity_diagnostic(
    const std::string_view family,
    const std::uint32_t format,
    const std::uint32_t runtime,
    const std::uint32_t systemc,
    const std::uint32_t required_format)
{
    return diagnostic::unsupported_artifact_identity(
        family,
        "format " + std::to_string(format) + ", runtime ABI "
            + std::to_string(runtime) + " and SystemC ABI "
            + std::to_string(systemc),
        "format " + std::to_string(required_format) + ", runtime ABI "
            + std::to_string(runtime_abi_version) + " and SystemC ABI "
            + std::to_string(FSIM_SYSTEMC_ABI_VERSION),
        family);
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
    if (!bytes.empty()) {
      append(std::string_view{bytes.data(), bytes.size()});
    }
  }
  void u32(const std::uint32_t value) {
    require_write(writer_.write_u32_le(value));
  }
  void u64(const std::uint64_t value) {
    require_write(writer_.write_u64_le(value));
  }
  void i64(const std::int64_t value) {
    u64(std::bit_cast<std::uint64_t>(value));
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
    u64(static_cast<std::uint64_t>(values.size()));
    for (const auto& value : values) {
      callback(value);
    }
  }
  std::string take() && { return std::move(output_); }

 private:
  static void require_write(const bool success) {
    if (!success) {
      throw std::length_error(
          "Incremental artifact metadata exceeds the output size limit.");
    }
  }
  void append(const std::string_view bytes) {
    require_write(writer_.append(bytes));
  }

  std::string output_;
  support::BoundedByteWriter<std::string> writer_{output_, output_.max_size()};
};

class Reader {
 public:
  explicit Reader(const std::string_view input)
      : input_(input),
        reader_(std::as_bytes(std::span{input.data(), input.size()})) {}
  bool raw(const std::span<const char> expected) {
    if (expected.size() > remaining()
        || input_.substr(reader_.position(), expected.size())
            != std::string_view{expected.data(), expected.size()}) {
      return false;
    }
    std::span<const std::byte> ignored;
    return reader_.take(expected.size(), ignored);
  }
  std::optional<std::uint32_t> u32() {
    std::uint32_t result{};
    if (!reader_.read_u32_le(result)) {
      return std::nullopt;
    }
    return result;
  }
  std::optional<std::uint64_t> u64() {
    std::uint64_t result{};
    if (!reader_.read_u64_le(result)) {
      return std::nullopt;
    }
    return result;
  }
  std::optional<std::int64_t> i64() {
    const auto value = u64();
    return value
        ? std::optional{std::bit_cast<std::int64_t>(*value)} : std::nullopt;
  }
  std::optional<bool> boolean() {
    if (remaining() == 0
        || static_cast<unsigned char>(input_[reader_.position()]) > 1) {
      return std::nullopt;
    }
    std::uint8_t value{};
    if (!reader_.read_u8(value)) {
      return std::nullopt;
    }
    return value != 0;
  }
  std::optional<std::string> string() {
    const auto size = u64();
    if (!size || *size > remaining()
        || *size > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    std::span<const std::byte> bytes;
    if (!reader_.take(static_cast<std::size_t>(*size), bytes)) {
      return std::nullopt;
    }
    std::string result;
    if (!bytes.empty()) {
      result.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
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
  std::size_t remaining() const noexcept { return reader_.remaining(); }

 private:
  std::string_view input_;
  support::BoundedByteReader reader_;
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

std::string object_input_digest(const IncrementalObjectMetadata& metadata)
{
    Writer writer;
    writer.string("fsim-systemc-object-input-v1");
    writer.u32(metadata.runtime_abi);
    writer.u32(metadata.systemc_abi);
    writer.string(metadata.scv_compatibility);
    writer.string(metadata.producer);
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

std::string object_digest(const IncrementalObjectMetadata& metadata)
{
    Writer writer;
    writer.string("fsim-systemc-object-v1");
    writer.string(metadata.input_digest);
    writer.string(metadata.object_checksum);
    return support::Sha256::hex(
        support::Sha256::digest(std::move(writer).take()));
}

std::string plugin_input_digest(const IncrementalPluginMetadata& metadata)
{
    Writer writer;
    writer.string("fsim-systemc-plugin-input-v1");
    writer.u32(metadata.runtime_abi);
    writer.u32(metadata.systemc_abi);
    writer.string(metadata.scv_compatibility);
    writer.string(metadata.producer);
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

std::string plugin_digest(const IncrementalPluginMetadata& metadata)
{
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
    const std::string& source)
{
    if (metadata.format != kIncrementalObjectFormatVersion
        || metadata.runtime_abi != runtime_abi_version
        || metadata.systemc_abi != FSIM_SYSTEMC_ABI_VERSION) {
        report(
            diagnostics, kSchemaCode,
            incremental_identity_diagnostic(
                ".fsimscobj", metadata.format, metadata.runtime_abi,
                metadata.systemc_abi, kIncrementalObjectFormatVersion),
            source);
    }
    if (!fsim_scv_accepts_compatibility_identity(
            metadata.scv_compatibility.c_str())) {
        report(
            diagnostics, kSchemaCode,
            diagnostic::unsupported_artifact_identity(
                ".fsimscobj SCV producer", metadata.scv_compatibility,
                fsim_scv_compatibility_identity(), ".fsimscobj"),
            source);
    }
    const auto required_producer
        = std::string { "fsim " } + std::string { version };
    if (metadata.producer != required_producer) {
        report(
            diagnostics, kSchemaCode,
            diagnostic::unsupported_artifact_identity(
                ".fsimscobj producer", metadata.producer,
                required_producer, ".fsimscobj"),
            source);
    }
    if (metadata.toolchain.empty() || metadata.target.empty()
        || !checksum(metadata.compiler_fingerprint)
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
    const std::string& source)
{
    if (metadata.format != kIncrementalPluginFormatVersion
        || metadata.runtime_abi != runtime_abi_version
        || metadata.systemc_abi != FSIM_SYSTEMC_ABI_VERSION) {
        report(
            diagnostics, kSchemaCode,
            incremental_identity_diagnostic(
                ".fsimscplugin", metadata.format, metadata.runtime_abi,
                metadata.systemc_abi, kIncrementalPluginFormatVersion),
            source);
    }
    if (!fsim_scv_accepts_compatibility_identity(
            metadata.scv_compatibility.c_str())) {
        report(
            diagnostics, kSchemaCode,
            diagnostic::unsupported_artifact_identity(
                ".fsimscplugin SCV producer", metadata.scv_compatibility,
                fsim_scv_compatibility_identity(), ".fsimscplugin"),
            source);
    }
    const auto required_producer
        = std::string { "fsim " } + std::string { version };
    if (metadata.producer != required_producer) {
        report(
            diagnostics, kSchemaCode,
            diagnostic::unsupported_artifact_identity(
                ".fsimscplugin producer", metadata.producer,
                required_producer, ".fsimscplugin"),
            source);
    }
    if (!library_name(metadata.logical_library) || metadata.toolchain.empty()
        || metadata.target.empty()
        || !checksum(metadata.compiler_fingerprint)
        || !checksum(metadata.input_digest)
        || !checksum(metadata.link_digest) || metadata.object_digests.empty()
        || !safe_path(metadata.library) || !checksum(metadata.library_checksum)
        || metadata.factories.empty()) {
        report(
            diagnostics, kValueCode,
            "SystemC plug-in metadata is missing a required identity, object, "
            "factory, or payload",
            source);
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

constexpr std::string_view kMetadataPayload = "metadata";
// The largest native payload an artifact holds.
constexpr std::uint64_t kMaximumNativeBytes = 4ULL * 1024U * 1024U * 1024U;

std::uint64_t process_id() noexcept {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(_getpid());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

constexpr std::string_view kPrivateDirectoryPrefix = "fsim-systemc-";

// The process id in `fsim-systemc-<pid>-<tick>`, or nothing for any other
// name.
std::optional<std::uint64_t> private_directory_owner(const std::string_view name) {
  if (!name.starts_with(kPrivateDirectoryPrefix)) {
    return std::nullopt;
  }
  const auto rest = name.substr(kPrivateDirectoryPrefix.size());
  const auto separator = rest.find('-');
  if (separator == std::string_view::npos || separator == 0U || separator > 10U
      || separator + 1U == rest.size()) {
    return std::nullopt;
  }
  const auto digits = [](const std::string_view text) {
    return std::ranges::all_of(
        text, [](const char value) { return value >= '0' && value <= '9'; });
  };
  if (!digits(rest.substr(0U, separator)) || !digits(rest.substr(separator + 1U))) {
    return std::nullopt;
  }
  return std::stoull(std::string{rest.substr(0U, separator)});
}

// Removes the private directories of fsim processes that ended without
// running their exit cleanup (a crash or a kill). A directory whose process
// still exists, or cannot be inspected, is kept.
void remove_abandoned_private_directories(const std::filesystem::path& temporary) noexcept {
  std::error_code code;
  auto iterator = support::native_fs::directory_iterator(temporary, code);
  const std::filesystem::directory_iterator end;
  std::vector<std::filesystem::path> abandoned;
  while (!code && iterator != end) {
    const auto status = iterator->symlink_status(code);
    if (!code && support::native_fs::is_directory(status)) {
      const auto owner =
          private_directory_owner(support::path_to_utf8(iterator->path().filename()));
      if (owner && *owner != process_id() && !compiler::detail::process_is_alive(*owner)) {
        abandoned.push_back(iterator->path());
      }
    }
    code.clear();
    iterator.increment(code);
  }
  // Another process may sweep the same directory at once, and two removals
  // of one tree can each stop part-way; a rename to a name of this process
  // claims it for one of them. A claim left by a process that dies is
  // swept like its own private directories.
  std::uint64_t claims{};
  for (const auto& directory : abandoned) {
    const auto claimed = temporary
        / (std::string{kPrivateDirectoryPrefix} + std::to_string(process_id()) + "-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + std::to_string(++claims));
    code.clear();
    support::native_fs::rename(directory, claimed, code);
    if (!code) {
      support::native_fs::remove_all(claimed, code);
    }
  }
}

class PrivateDirectories final {
 public:
  static PrivateDirectories& instance() {
    static PrivateDirectories directories;
    return directories;
  }

  PrivateDirectories(const PrivateDirectories&) = delete;
  PrivateDirectories& operator=(const PrivateDirectories&) = delete;

  ~PrivateDirectories() {
    if (root_.empty()) {
      return;
    }
    // A library still loaded on Windows keeps its file; the rest go.
    std::error_code error;
    support::native_fs::remove_all(root_, error);
  }

  std::optional<std::filesystem::path> next(std::string& error) {
    const std::scoped_lock lock{mutex_};
    std::error_code code;
    if (root_.empty()) {
      const auto temporary = std::filesystem::temp_directory_path(code);
      if (!code) {
        remove_abandoned_private_directories(temporary);
      }
      const auto name = std::string{kPrivateDirectoryPrefix} + std::to_string(process_id()) + "-"
          + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count());
      if (code || !support::native_fs::create_directory(temporary / name, code)
          || code) {
        error = "cannot create a private directory for SystemC native code"
            + (code ? ": " + code.message() : std::string{});
        return std::nullopt;
      }
      root_ = temporary / name;
    }
    const auto directory = root_ / std::to_string(sequence_++);
    if (!support::native_fs::create_directory(directory, code) || code) {
      error = "cannot create a private directory for SystemC native code"
          + (code ? ": " + code.message() : std::string{});
      return std::nullopt;
    }
    return directory;
  }

 private:
  PrivateDirectories() = default;

  std::mutex mutex_;
  std::filesystem::path root_;
  std::uint64_t sequence_{};
};

std::optional<artifact::ContainerReader> open_artifact(
    const std::filesystem::path& path,
    const std::string_view kind,
    diagnostic::Engine& diagnostics) {
  std::string error;
  auto reader = artifact::ContainerReader::open(path, kind, error);
  if (!reader) {
    report(diagnostics, kIoCode, std::move(error), support::path_to_utf8(path));
  }
  return reader;
}

// A payload checked against its container checksum and the SHA-256
// identity its metadata names.
std::optional<std::string> read_native(
    artifact::ContainerReader& reader,
    const std::filesystem::path& name,
    const std::string_view sha256,
    const std::string_view description,
    diagnostic::Engine& diagnostics) {
  std::string error;
  auto bytes = reader.read(name.generic_string(), kMaximumNativeBytes, error);
  if (!bytes) {
    report(
        diagnostics, kIoCode,
        "cannot read " + std::string{description}
            + (error.empty() ? std::string{": payload is missing"} : ": " + error),
        support::path_to_utf8(reader.path()));
    return std::nullopt;
  }
  if (support::Sha256::hex(support::Sha256::digest(*bytes)) != sha256) {
    report(
        diagnostics, kIoCode, std::string{description} + ": native payload checksum mismatch",
        support::path_to_utf8(reader.path()));
    return std::nullopt;
  }
  return bytes;
}

std::optional<std::string> read_metadata(
    artifact::ContainerReader& reader,
    const std::string_view description,
    diagnostic::Engine& diagnostics) {
  std::string error;
  auto bytes = reader.read(kMetadataPayload, 64U * 1024U * 1024U, error);
  if (!bytes) {
    report(
        diagnostics, kIoCode,
        "cannot read " + std::string{description}
            + (error.empty() ? std::string{": payload is missing"} : ": " + error),
        support::path_to_utf8(reader.path()));
  }
  return bytes;
}

bool publish(
    const std::filesystem::path& destination,
    const std::string_view kind,
    const std::string_view metadata,
    const std::filesystem::path& payload_name,
    const std::string_view payload,
    diagnostic::Engine& diagnostics) {
  if (destination.empty()) {
    report(diagnostics, kIoCode, "native artifact output must not be empty");
    return false;
  }
  std::error_code code;
  if (support::native_fs::exists(destination, code) || code) {
    report(
        diagnostics, kIoCode,
        "native artifact output already exists or cannot be inspected",
        support::path_to_utf8(destination));
    return false;
  }
  const auto parent = destination.parent_path().empty()
      ? std::filesystem::path{"."} : destination.parent_path();
  support::native_fs::create_directories(parent, code);
  if (code) {
    report(
        diagnostics, kIoCode,
        "cannot create native artifact parent: " + code.message(),
        support::path_to_utf8(parent));
    return false;
  }
  std::string error;
  auto writer = artifact::ContainerWriter::create(
      destination, kind, artifact::ContainerPages::bulk, error);
  if (!writer || !writer->put(kMetadataPayload, metadata, error)
      || !writer->put(payload_name.generic_string(), payload, error)
      || !writer->publish(error)) {
    report(
        diagnostics, kIoCode, "cannot write native artifact: " + error,
        support::path_to_utf8(destination));
    return false;
  }
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
    const IncrementalPluginMetadata& metadata)
{
    return plugin_input_digest(metadata);
}

std::string serialize_incremental_object_metadata(
    const IncrementalObjectMetadata& metadata)
{
    Writer writer;
    writer.raw(kObjectMagic);
    writer.u32(kIncrementalObjectFormatVersion);
    writer.u32(metadata.runtime_abi);
    writer.u32(metadata.systemc_abi);
    writer.string(metadata.scv_compatibility);
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
    const IncrementalPluginMetadata& metadata)
{
    Writer writer;
    writer.raw(kPluginMagic);
    writer.u32(kIncrementalPluginFormatVersion);
    writer.u32(metadata.runtime_abi);
    writer.u32(metadata.systemc_abi);
    writer.string(metadata.scv_compatibility);
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
    diagnostic::Engine& diagnostics)
{
    Reader reader { bytes };
    IncrementalObjectMetadata metadata;
    if (!reader.raw(kObjectMagic)) {
        report(diagnostics, kSchemaCode, "invalid .fsimscobj metadata magic", source_name);
        return std::nullopt;
    }
    const auto format = reader.u32();
    const auto runtime = reader.u32();
    const auto abi = reader.u32();
    if (!format || !runtime || !abi) {
        report(
            diagnostics, kSchemaCode, "truncated .fsimscobj metadata header",
            source_name);
        return std::nullopt;
    }
    metadata.format = *format;
    metadata.runtime_abi = *runtime;
    metadata.systemc_abi = *abi;
    if (metadata.format != kIncrementalObjectFormatVersion
        || metadata.runtime_abi != runtime_abi_version
        || metadata.systemc_abi != FSIM_SYSTEMC_ABI_VERSION) {
        report(
            diagnostics, kSchemaCode,
            incremental_identity_diagnostic(
                ".fsimscobj", metadata.format, metadata.runtime_abi,
                metadata.systemc_abi, kIncrementalObjectFormatVersion),
            source_name);
        return std::nullopt;
    }
    auto scv_compatibility = reader.string();
    auto producer = reader.string();
    auto toolchain = reader.string();
    auto target = reader.string();
    auto compiler = reader.string();
    auto input_digest = reader.string();
    auto digest = reader.string();
    if (!scv_compatibility || !producer || !toolchain || !target || !compiler
        || !input_digest || !digest) {
        report(diagnostics, kSchemaCode, "truncated .fsimscobj metadata", source_name);
        return std::nullopt;
    }
    metadata.scv_compatibility = std::move(*scv_compatibility);
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
        ? std::optional { std::move(metadata) }
        : std::nullopt;
}

std::optional<IncrementalPluginMetadata>
deserialize_incremental_plugin_metadata(
    const std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics)
{
    Reader reader { bytes };
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
    if (!format || !runtime || !abi) {
        report(
            diagnostics, kSchemaCode, "truncated .fsimscplugin metadata header",
            source_name);
        return std::nullopt;
    }
    metadata.format = *format;
    metadata.runtime_abi = *runtime;
    metadata.systemc_abi = *abi;
    if (metadata.format != kIncrementalPluginFormatVersion
        || metadata.runtime_abi != runtime_abi_version
        || metadata.systemc_abi != FSIM_SYSTEMC_ABI_VERSION) {
        report(
            diagnostics, kSchemaCode,
            incremental_identity_diagnostic(
                ".fsimscplugin", metadata.format, metadata.runtime_abi,
                metadata.systemc_abi, kIncrementalPluginFormatVersion),
            source_name);
        return std::nullopt;
    }
    auto scv_compatibility = reader.string();
    auto producer = reader.string();
    auto library = reader.string();
    auto toolchain = reader.string();
    auto target = reader.string();
    auto compiler = reader.string();
    auto input_digest = reader.string();
    auto digest = reader.string();
    if (!scv_compatibility || !producer || !library || !toolchain || !target
        || !compiler || !input_digest || !digest) {
        report(diagnostics, kSchemaCode, "truncated .fsimscplugin metadata", source_name);
        return std::nullopt;
    }
    metadata.scv_compatibility = std::move(*scv_compatibility);
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
            factory.parameters.push_back({ std::move(*parameter_name), *type, *has_default, *default_value });
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
        ? std::optional { std::move(metadata) }
        : std::nullopt;
}

std::optional<IncrementalObjectMetadata> load_incremental_object_metadata(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics) {
  auto reader = open_artifact(path, kIncrementalObjectKind, diagnostics);
  const auto bytes = reader
      ? read_metadata(*reader, ".fsimscobj metadata", diagnostics)
      : std::nullopt;
  auto metadata = bytes
      ? deserialize_incremental_object_metadata(
          *bytes, support::path_to_utf8(path), diagnostics)
      : std::nullopt;
  if (!metadata
      || !read_native(*reader, metadata->object, metadata->object_checksum,
          ".fsimscobj native payload", diagnostics)) {
    return std::nullopt;
  }
  return metadata;
}

std::optional<IncrementalPluginMetadata> load_incremental_plugin_metadata(
    const std::filesystem::path& path,
    diagnostic::Engine& diagnostics) {
  auto reader = open_artifact(path, kIncrementalPluginKind, diagnostics);
  const auto bytes = reader
      ? read_metadata(*reader, ".fsimscplugin metadata", diagnostics)
      : std::nullopt;
  auto metadata = bytes
      ? deserialize_incremental_plugin_metadata(
          *bytes, support::path_to_utf8(path), diagnostics)
      : std::nullopt;
  if (!metadata
      || !read_native(*reader, metadata->library, metadata->library_checksum,
          ".fsimscplugin native payload", diagnostics)) {
    return std::nullopt;
  }
  return metadata;
}

std::optional<std::string> load_incremental_object_native(
    const std::filesystem::path& path,
    const IncrementalObjectMetadata& metadata,
    diagnostic::Engine& diagnostics) {
  auto reader = open_artifact(path, kIncrementalObjectKind, diagnostics);
  return reader
      ? read_native(*reader, metadata.object, metadata.object_checksum,
          ".fsimscobj native payload", diagnostics)
      : std::nullopt;
}

std::optional<std::string> load_incremental_plugin_native(
    const std::filesystem::path& path,
    const IncrementalPluginMetadata& metadata,
    diagnostic::Engine& diagnostics) {
  auto reader = open_artifact(path, kIncrementalPluginKind, diagnostics);
  return reader
      ? read_native(*reader, metadata.library, metadata.library_checksum,
          ".fsimscplugin native payload", diagnostics)
      : std::nullopt;
}

std::optional<std::filesystem::path> private_native_directory(std::string& error) {
  return PrivateDirectories::instance().next(error);
}

bool publish_incremental_object(
    const std::filesystem::path& destination,
    const IncrementalObjectMetadata& metadata,
    const std::string_view payload,
    diagnostic::Engine& diagnostics)
{
    diagnostic::Engine validation;
    if (!validate_object(
            metadata, validation,
            std::string { kIncrementalObjectMetadataFilename })) {
        report(
            diagnostics, kIoCode,
            "invalid SystemC object supplied for publication");
        return false;
    }
    const auto bytes = serialize_incremental_object_metadata(metadata);
    if (!deserialize_incremental_object_metadata(
            bytes, std::string { kIncrementalObjectMetadataFilename }, validation)
        || support::Sha256::hex(support::Sha256::digest(payload))
            != metadata.object_checksum) {
        report(diagnostics, kIoCode, "invalid SystemC object supplied for publication");
        return false;
    }
    return publish(
        destination, kIncrementalObjectKind, bytes, metadata.object, payload,
        diagnostics);
}

bool publish_incremental_plugin(
    const std::filesystem::path& destination,
    const IncrementalPluginMetadata& metadata,
    const std::string_view payload,
    diagnostic::Engine& diagnostics)
{
    diagnostic::Engine validation;
    if (!validate_plugin(
            metadata, validation,
            std::string { kIncrementalPluginMetadataFilename })) {
        report(
            diagnostics, kIoCode,
            "invalid SystemC plug-in supplied for publication");
        return false;
    }
    const auto bytes = serialize_incremental_plugin_metadata(metadata);
    if (!deserialize_incremental_plugin_metadata(
            bytes, std::string { kIncrementalPluginMetadataFilename }, validation)
        || support::Sha256::hex(support::Sha256::digest(payload))
            != metadata.library_checksum) {
        report(diagnostics, kIoCode, "invalid SystemC plug-in supplied for publication");
        return false;
    }
    return publish(
        destination, kIncrementalPluginKind, bytes, metadata.library, payload,
        diagnostics);
}

} // namespace fsim::systemc
