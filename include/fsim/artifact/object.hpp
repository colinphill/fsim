// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_identity.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/library/artifact.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::uint32_t kObjectFormatVersion = 7;
inline constexpr std::string_view kObjectMetadataFilename = "fsim-object.bin";

// One explicitly scripted HDL compilation unit. Paths stored here are
// artifact-relative logical identities; no producer-absolute path is durable.
struct ObjectMetadata {
  std::uint32_t format{kObjectFormatVersion};
  std::uint32_t portable_schema{library::kPortableSchemaVersion};
  std::string producer;
  std::string language;
  std::string standard;
  std::string compatibility_profile { "none" };
  std::string library;
  std::string compilation_unit;
  std::string uvm_release{"none"};
  std::string compilation_digest;
  std::string trace_archive;
  CodeCoverageArtifactIdentity code_coverage;
  std::vector<std::string> defines;
  std::vector<std::filesystem::path> include_roots;
  std::vector<library::VhdlPackageDependency> vhdl_package_dependencies;
  std::vector<library::SourceIndexEntry> sources;
  std::vector<library::UnitIndexEntry> units;

  friend bool operator==(const ObjectMetadata&, const ObjectMetadata&) = default;
};

// Recomputes the path-independent digest of the explicit compilation inputs
// and complete portable unit index. Consumers reject metadata whose stored
// digest does not match this value.
[[nodiscard]] std::string compute_object_compilation_digest(
    const ObjectMetadata& metadata);

// Canonical little-endian metadata codec. Payload bytes are independently
// indexed and checksummed; they are not embedded in this record.
[[nodiscard]] std::string serialize_object_metadata(
    const ObjectMetadata& metadata);

[[nodiscard]] std::optional<ObjectMetadata> deserialize_object_metadata(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<ObjectMetadata> load_object_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

// Publishes through a sibling staging directory and one rename. The complete
// installed .fsimobj tree is read-only and existing destinations are refused.
[[nodiscard]] bool publish_object(
    const std::filesystem::path& destination,
    const ObjectMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::artifact
