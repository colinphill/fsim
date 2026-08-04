// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::library {

inline constexpr std::uint32_t kFormatVersion = 1;
inline constexpr std::uint32_t kPortableSchemaVersion = 1;
inline constexpr std::string_view kMetadataFilename = "fsim-library.toml";

struct LanguageStandard {
  std::string language;
  std::string revision;

  friend bool operator==(const LanguageStandard&, const LanguageStandard&) = default;
};

struct UnitIndexEntry {
  std::string language;
  std::string kind;
  std::string name;
  std::string primary_name;
  std::string architecture;
  std::filesystem::path artifact;
  std::string checksum;

  friend bool operator==(const UnitIndexEntry&, const UnitIndexEntry&) = default;
};

struct SourceIndexEntry {
  std::string logical_name;
  // Empty when an exporter intentionally omits source text while retaining
  // locations. Otherwise this is a contained path beneath the artifact.
  std::filesystem::path artifact;
  std::string checksum;
  std::string language;

  friend bool operator==(const SourceIndexEntry&, const SourceIndexEntry&) = default;
};

// Optional host-native accelerators. Every entry has an independently
// checksummed payload and enough producer identity to make admission an exact
// comparison. A consumer that cannot prove every relevant field equal must
// ignore the entry and use the portable unit/source payloads instead.
struct NativeArtifact {
  std::string kind;
  std::filesystem::path artifact;
  std::string checksum;
  std::uint32_t runtime_abi{};
  std::uint32_t systemc_abi{};
  std::string compiler_fingerprint;
  std::string llvm_version;
  std::string target;
  std::string data_layout;
  std::string cpu;
  std::string features;
  std::string optimization;
  std::string cache_key;

  friend bool operator==(const NativeArtifact&, const NativeArtifact&) = default;
};

struct Metadata {
  std::uint32_t format{kFormatVersion};
  std::string library;
  std::string producer;
  std::uint32_t runtime_schema{};
  std::uint32_t portable_schema{kPortableSchemaVersion};
  std::vector<LanguageStandard> standards;
  std::vector<std::string> dependencies;
  std::vector<SourceIndexEntry> sources;
  std::vector<UnitIndexEntry> units;
  std::vector<NativeArtifact> native_artifacts;

  friend bool operator==(const Metadata&, const Metadata&) = default;
};

struct PortablePayload {
  std::filesystem::path path;
  std::string bytes;
};

// Emits the canonical deterministic metadata spelling used for artifact
// checksums and reproducible export. Records retain their supplied order.
[[nodiscard]] std::string serialize_metadata(const Metadata& metadata);

// Parses and validates fsim-library.toml without opening any referenced unit
// payloads. Portable artifact paths must be relative and remain within the
// mapped directory.
[[nodiscard]] std::optional<Metadata> parse_metadata(
    std::string_view source,
    std::string source_name,
    diagnostic::Engine& diagnostics);

// Opens only fsim-library.toml in directory and validates that its logical
// library matches expected_library. Unit payloads remain lazy.
[[nodiscard]] std::optional<Metadata> load_metadata(
    const std::filesystem::path& directory,
    std::string_view expected_library,
    diagnostic::Engine& diagnostics);

// Publishes a fully checksummed library through a sibling staging directory
// and a single directory rename. Existing destinations are never overwritten.
// The installed tree is made read-only after every payload and the canonical
// metadata file have been durably closed.
[[nodiscard]] bool publish(
    const std::filesystem::path& destination,
    const Metadata& metadata,
    const std::vector<PortablePayload>& payloads,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::library
