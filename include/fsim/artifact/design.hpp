// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_identity.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/version.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::artifact {

inline constexpr std::uint32_t kDesignFormatVersion = 12;
inline constexpr std::string_view kDesignMetadataFilename = "fsim-design.bin";

struct DesignRoot {
  std::string alias;
  std::string target;
  std::string selected_identity;
  friend bool operator==(const DesignRoot&, const DesignRoot&) = default;
};

struct DesignBinding {
  std::string instance;
  std::optional<std::string> target;
  std::optional<std::string> resolver;
  friend bool operator==(const DesignBinding&, const DesignBinding&) = default;
};

struct DesignObjectInput {
  std::string metadata_digest;
  std::string compilation_digest;
  std::string language;
  std::string standard;
  std::string compatibility_profile { "none" };
  std::string library;
  CodeCoverageArtifactIdentity code_coverage;
  std::vector<library::VhdlPackageDependency> vhdl_package_dependencies;
  std::vector<std::string> unit_checksums;
  friend bool operator==(
      const DesignObjectInput&, const DesignObjectInput&) = default;
};

struct DesignVhdlUnitProvenance {
  std::uint32_t unit{};
  std::string standard;
  std::string predefined_environment;
  std::string compatibility_profile;
  std::vector<library::VhdlPackageDependency> package_dependencies;
  friend bool operator==(
      const DesignVhdlUnitProvenance&,
      const DesignVhdlUnitProvenance&) = default;
};

struct DesignVerilogUnitProvenance {
  std::uint32_t unit{};
  std::string language;
  std::string standard;
  std::string compatibility_profile;
  friend bool operator==(
      const DesignVerilogUnitProvenance&,
      const DesignVerilogUnitProvenance&) = default;
};

struct DesignSdfAnnotation {
  std::uint32_t schema{1};
  std::string revision;
  std::string revision_adapter;
  bool has_timescale{};
  std::string timescale;
  std::string selection_policy;
  std::string scope_identity;
  std::string source_digest;
  std::string design_digest;
  std::string ir_identity;
  std::string resolution_identity;
  std::string mapping_identity;
  std::vector<std::string> selected_root_identities;
  std::vector<std::string> semantic_unit_identities;
  std::vector<std::string> semantic_object_identities;
  std::string cache_key;

  friend bool operator==(
      const DesignSdfAnnotation&, const DesignSdfAnnotation&) = default;
};

struct DesignPayload {
  std::string kind;
  std::filesystem::path artifact;
  std::string checksum;
  friend bool operator==(const DesignPayload&, const DesignPayload&) = default;
};

/// A payload produced directly in the artifact staging tree. The checksum is
/// established before publication and verified again from the generated file,
/// so large payloads need not be retained in memory.
struct GeneratedDesignPayload {
  std::filesystem::path path;
  std::string checksum;
  std::function<bool(
      const std::filesystem::path&, diagnostic::Engine&)> write;
};

struct DesignSystemCPlugin {
  std::string logical_library;
  std::string input_digest;
  std::string link_digest;
  std::string compiler_fingerprint;
  std::string scv_compatibility;
  std::filesystem::path directory;
  std::string metadata_checksum;
  std::string library_checksum;
  std::vector<std::string> factories;
  friend bool operator==(
      const DesignSystemCPlugin&, const DesignSystemCPlugin&) = default;
};

// Standalone elaboration artifact metadata. Producer paths are deliberately
// absent; ordered object content identities and selected canonical units are
// sufficient provenance for relocation and exact-compatible reuse.
struct DesignMetadata {
  std::uint32_t format{kDesignFormatVersion};
  std::uint32_t runtime_abi{runtime_abi_version};
  std::string producer;
  std::string design_digest;
  std::string time_resolution;
  std::string delay_mode;
  std::string optimization;
  std::string cache_key;
  std::string trace_archive;
  CodeCoverageArtifactIdentity code_coverage;
  std::string uvm_release{"none"};
  std::string uvm_source_identity;
  std::uint64_t seed{1};
  bool entropy_seed{};
  std::vector<std::string> search_libraries;
  std::vector<DesignRoot> roots;
  std::vector<DesignBinding> bindings;
  std::vector<DesignObjectInput> objects;
  std::vector<DesignVhdlUnitProvenance> vhdl_unit_provenance;
  std::vector<DesignVerilogUnitProvenance> verilog_unit_provenance;
  std::vector<DesignSdfAnnotation> sdf_annotations;
  std::vector<DesignSystemCPlugin> systemc_plugins;
  std::vector<DesignPayload> payloads;
  std::vector<std::string> specialization_cache_keys;
  std::uint64_t unit_count{};
  std::uint64_t semantic_source_count{};
  std::uint64_t specialization_count{};
  std::uint64_t signal_count{};
  std::uint64_t process_count{};

  friend bool operator==(const DesignMetadata&, const DesignMetadata&) = default;
};

[[nodiscard]] std::string compute_design_digest(
    const DesignMetadata& metadata);
[[nodiscard]] std::string serialize_design_metadata(
    const DesignMetadata& metadata);
[[nodiscard]] std::optional<DesignMetadata> deserialize_design_metadata(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<DesignMetadata> load_design_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool publish_design(
    const std::filesystem::path& destination,
    const DesignMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    diagnostic::Engine& diagnostics);
[[nodiscard]] bool publish_design(
    const std::filesystem::path& destination,
    const DesignMetadata& metadata,
    const std::vector<library::PortablePayload>& payloads,
    std::span<const GeneratedDesignPayload> generated_payloads,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::artifact
