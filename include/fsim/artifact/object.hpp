// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_identity.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/library/artifact.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::uint32_t kObjectFormatVersion = 10;
inline constexpr std::string_view kObjectArtifactKind = "fsim-object";
inline constexpr std::string_view kObjectMetadataFilename = "fsim-object.bin";

// Coverage facts of one indexed source, taken at compile time so that an
// elaboration with code coverage never needs the source text.
struct ObjectSourceCoverage {
  std::uint64_t content_bytes{};
  frontend::CoverageSourceControlError control_error{
      frontend::CoverageSourceControlError::None};
  std::vector<frontend::CoverageSourceExclusion> exclusions;

  friend bool operator==(
      const ObjectSourceCoverage&, const ObjectSourceCoverage&) = default;
};

// One explicitly scripted HDL compilation unit, stored as one SQLite
// container (artifact::ContainerWriter). Sources are referenced, never
// copied: each logical name is the source's path relative to the compiling
// workspace (it may climb out with ".."), and its checksum is the SHA-256 of
// the compiled content. No producer-absolute path is durable.
struct ObjectMetadata {
  std::uint32_t format{kObjectFormatVersion};
  std::uint32_t portable_schema{library::kPortableSchemaVersion};
  std::uint32_t compiled_hir_schema{library::kCompiledHirSchemaVersion};
  std::filesystem::path compiled_hir_artifact;
  std::string compiled_hir_checksum;
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
  // Parallel to `sources`.
  std::vector<ObjectSourceCoverage> source_coverage;
  std::vector<library::UnitIndexEntry> units;

  friend bool operator==(const ObjectMetadata&, const ObjectMetadata&) = default;
};

// Recomputes the path-independent digest of the explicit compilation inputs
// and complete portable unit index. Consumers reject metadata whose stored
// digest does not match this value.
// The coverage facts of a source list as one self-contained payload, for
// exported libraries, which keep them beside their location-only sources.
inline constexpr std::string_view kSourceCoverageMagic = "fsim-source-coverage-v1";

[[nodiscard]] std::string serialize_source_coverage(
    const std::vector<ObjectSourceCoverage>& coverage);

[[nodiscard]] std::optional<std::vector<ObjectSourceCoverage>> deserialize_source_coverage(
    std::string_view bytes,
    const std::string& source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::string compute_object_compilation_digest(
    const ObjectMetadata& metadata);

// Canonical little-endian metadata codec for the current format.
// Unsupported format/schema identities throw std::invalid_argument. Owning
// payload bytes are independently indexed
// and checksummed; primitive and class entries are metadata-only inventories
// validated against the compiled-HIR bundle.
[[nodiscard]] std::string serialize_object_metadata(
    const ObjectMetadata& metadata);

[[nodiscard]] std::optional<ObjectMetadata> deserialize_object_metadata(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<ObjectMetadata> load_object_metadata(
    const std::filesystem::path& object,
    diagnostic::Engine& diagnostics);

// One payload of a published object, verified against `expected_checksum`.
[[nodiscard]] std::optional<std::string> read_object_payload(
    const std::filesystem::path& object,
    const std::filesystem::path& payload,
    std::string_view expected_checksum,
    std::uint64_t maximum_bytes,
    diagnostic::Engine& diagnostics);

// Publishes the object file through a private temporary file and one
// rename, replacing an existing object of the same name.
[[nodiscard]] bool publish_object(
    const std::filesystem::path& destination,
    const ObjectMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::artifact
