// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/design.hpp"

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

void write_digest_fields(Writer& writer, const DesignMetadata& metadata) {
  writer.string(
      metadata.format == 1 ? "fsim-design-provenance-v1"
                           : "fsim-design-provenance-v2");
  writer.u32(metadata.runtime_abi);
  writer.string(metadata.time_resolution);
  writer.string(metadata.delay_mode);
  writer.string(metadata.optimization);
  writer.string(metadata.cache_key);
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
    writer.string(object.library);
    writer.sequence(object.unit_checksums, [&](const auto& checksum) {
      writer.string(checksum);
    });
  });
  if (metadata.format >= 2) {
    writer.sequence(metadata.systemc_plugins, [&](const auto& plugin) {
      writer.string(plugin.logical_library);
      writer.string(plugin.input_digest);
      writer.string(plugin.link_digest);
      writer.string(plugin.compiler_fingerprint);
      writer.string(plugin.metadata_checksum);
      writer.string(plugin.library_checksum);
      writer.sequence(plugin.factories, [&](const auto& factory) {
        writer.string(factory);
      });
    });
  }
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
  if ((metadata.format != 1 && metadata.format != kDesignFormatVersion)
      || metadata.runtime_abi != runtime_abi_version) {
    report(
        diagnostics, kSchemaCode,
        "unsupported .fsimdesign format or runtime ABI", source);
  }
  if (metadata.producer.empty() || metadata.time_resolution.empty()
      || (metadata.delay_mode != "min" && metadata.delay_mode != "typ"
          && metadata.delay_mode != "max")
      || (metadata.optimization != "O0" && metadata.optimization != "O2")
      || !checksum_spelling(metadata.cache_key)
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
    if (!checksum_spelling(object.metadata_digest)
        || !checksum_spelling(object.compilation_digest)
        || (object.language != "vhdl" && object.language != "verilog"
            && object.language != "systemverilog")
        || object.standard.empty() || !safe_name(object.library)
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
  std::set<std::string> systemc_libraries;
  std::set<std::string> systemc_directories;
  if (metadata.format == 1 && !metadata.systemc_plugins.empty()) {
    report(
        diagnostics, kValueCode,
        "format-1 .fsimdesign metadata cannot index SystemC plug-ins",
        source);
  }
  for (const auto& plugin : metadata.systemc_plugins) {
    const auto directory = support::path_to_utf8(plugin.directory);
    if (!safe_name(plugin.logical_library)
        || !systemc_libraries.insert(plugin.logical_library).second
        || !checksum_spelling(plugin.input_digest)
        || !checksum_spelling(plugin.link_digest)
        || !checksum_spelling(plugin.compiler_fingerprint)
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
  writer.u32(metadata.format);
  writer.u32(metadata.runtime_abi);
  writer.string(metadata.producer);
  writer.string(metadata.design_digest);
  writer.string(metadata.time_resolution);
  writer.string(metadata.delay_mode);
  writer.string(metadata.optimization);
  writer.string(metadata.cache_key);
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
    writer.string(object.library);
    writer.sequence(object.unit_checksums, [&](const auto& checksum) {
      writer.string(checksum);
    });
  });
  if (metadata.format >= 2) {
    writer.sequence(metadata.systemc_plugins, [&](const auto& plugin) {
      writer.string(plugin.logical_library);
      writer.string(plugin.input_digest);
      writer.string(plugin.link_digest);
      writer.string(plugin.compiler_fingerprint);
      writer.path(plugin.directory);
      writer.string(plugin.metadata_checksum);
      writer.string(plugin.library_checksum);
      writer.sequence(plugin.factories, [&](const auto& factory) {
        writer.string(factory);
      });
    });
  }
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
  if (metadata.format != 1 && metadata.format != kDesignFormatVersion) {
    report(
        diagnostics, kSchemaCode,
        "unsupported .fsimdesign format or runtime ABI", source_name);
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
  const auto seed = [&]() -> std::optional<std::uint64_t> {
    if (!read_string(metadata.producer)
        || !read_string(metadata.design_digest)
        || !read_string(metadata.time_resolution)
        || !read_string(metadata.delay_mode)
        || !read_string(metadata.optimization)
        || !read_string(metadata.cache_key)) {
      return std::nullopt;
    }
    return reader.u64();
  }();
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
        if (value) metadata.search_libraries.push_back(std::move(*value));
        return value.has_value();
      })
      || !read_sequence([&] {
           DesignRoot root;
           if (!read_string(root.alias) || !read_string(root.target)
               || !read_string(root.selected_identity)) return false;
           metadata.roots.push_back(std::move(root));
           return true;
         })
      || !read_sequence([&] {
           DesignBinding binding;
           if (!read_string(binding.instance)) return false;
           auto target = reader.optional_string();
           auto resolver = reader.optional_string();
           if (!target || !resolver) return false;
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
               || !read_string(object.library)) return false;
           if (!read_sequence([&] {
                 auto checksum = reader.string();
                 if (checksum) object.unit_checksums.push_back(
                     std::move(*checksum));
                 return checksum.has_value();
               })) return false;
           metadata.objects.push_back(std::move(object));
           return true;
         })
      || (metadata.format >= 2 && !read_sequence([&] {
           DesignSystemCPlugin plugin;
           if (!read_string(plugin.logical_library)
               || !read_string(plugin.input_digest)
               || !read_string(plugin.link_digest)
               || !read_string(plugin.compiler_fingerprint)) return false;
           auto directory = reader.path();
           if (!directory || !read_string(plugin.metadata_checksum)
               || !read_string(plugin.library_checksum)) return false;
           plugin.directory = std::move(*directory);
           if (!read_sequence([&] {
                 auto factory = reader.string();
                 if (factory) plugin.factories.push_back(std::move(*factory));
                 return factory.has_value();
               })) return false;
           metadata.systemc_plugins.push_back(std::move(plugin));
           return true;
         }))
      || !read_sequence([&] {
           DesignPayload payload;
           if (!read_string(payload.kind)) return false;
           auto path = reader.path();
           if (!path || !read_string(payload.checksum)) return false;
           payload.artifact = std::move(*path);
           metadata.payloads.push_back(std::move(payload));
           return true;
         })
      || !read_sequence([&] {
           auto key = reader.string();
           if (key) metadata.specialization_cache_keys.push_back(
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
  diagnostic::Engine validation;
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

}  // namespace fsim::artifact
