// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"
#include "fsim/systemc/hierarchy.hpp"
#include "fsim/systemc/scv.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kIncrementalObjectFormatVersion = 2;
inline constexpr std::uint32_t kIncrementalPluginFormatVersion = 2;
inline constexpr std::string_view kIncrementalObjectMetadataFilename =
    "fsim-systemc-object.bin";
inline constexpr std::string_view kIncrementalPluginMetadataFilename =
    "fsim-systemc-plugin.bin";

struct NativeInputIdentity {
  std::string logical_name;
  std::string checksum;

  friend bool operator==(
      const NativeInputIdentity&, const NativeInputIdentity&) = default;
};

struct IncrementalObjectMetadata {
  std::uint32_t format{kIncrementalObjectFormatVersion};
  std::uint32_t runtime_abi{};
  std::uint32_t systemc_abi{};
  std::string scv_compatibility;
  std::string producer;
  std::string toolchain;
  std::string target;
  std::string compiler_fingerprint;
  std::string input_digest;
  std::string compilation_digest;
  std::vector<std::string> defines;
  std::vector<std::string> compile_options;
  std::vector<NativeInputIdentity> inputs;
  bool defines_plugin_entry_point{};
  bool contains_macro_export{};
  std::filesystem::path object{"native/translation-unit.o"};
  std::string object_checksum;

  friend bool operator==(
      const IncrementalObjectMetadata&,
      const IncrementalObjectMetadata&) = default;
};

struct IncrementalFactoryParameter {
  std::string name;
  std::uint32_t type{};
  bool has_default{};
  std::int64_t default_value{};

  friend bool operator==(
      const IncrementalFactoryParameter&,
      const IncrementalFactoryParameter&) = default;
};

struct IncrementalFactory {
  std::string name;
  std::vector<IncrementalFactoryParameter> parameters;

  friend bool operator==(
      const IncrementalFactory&, const IncrementalFactory&) = default;
};

struct IncrementalPluginMetadata {
  std::uint32_t format{kIncrementalPluginFormatVersion};
  std::uint32_t runtime_abi{};
  std::uint32_t systemc_abi{};
  std::string scv_compatibility;
  std::string producer;
  std::string logical_library;
  std::string toolchain;
  std::string target;
  std::string compiler_fingerprint;
  std::string input_digest;
  std::string link_digest;
  std::vector<std::string> object_digests;
  std::vector<std::string> link_options;
  std::vector<std::string> libraries;
  std::vector<IncrementalFactory> factories;
  std::filesystem::path library{"native/fsim-systemc-plugin"};
  std::string library_checksum;

  friend bool operator==(
      const IncrementalPluginMetadata&,
      const IncrementalPluginMetadata&) = default;
};

struct IncrementalCompileRequest {
  std::filesystem::path source;
  std::filesystem::path output;
  project::SystemCSection settings;
  std::filesystem::path working_directory;
  std::filesystem::path scratch_directory;
};

struct IncrementalLinkRequest {
  std::vector<std::filesystem::path> objects;
  std::filesystem::path output;
  std::string logical_library{"work"};
  project::SystemCSection settings;
  std::filesystem::path working_directory;
  std::filesystem::path scratch_directory;
};

struct IncrementalPhaseResult {
  bool success{};
  bool cache_hit{};
  std::filesystem::path artifact;
  std::string input_digest;
};

[[nodiscard]] std::string compute_incremental_object_input_digest(
    const IncrementalObjectMetadata& metadata);

[[nodiscard]] std::string compute_incremental_object_digest(
    const IncrementalObjectMetadata& metadata);

[[nodiscard]] std::string compute_incremental_plugin_digest(
    const IncrementalPluginMetadata& metadata);

[[nodiscard]] std::string compute_incremental_plugin_input_digest(
    const IncrementalPluginMetadata& metadata);

[[nodiscard]] std::string serialize_incremental_object_metadata(
    const IncrementalObjectMetadata& metadata);

[[nodiscard]] std::string serialize_incremental_plugin_metadata(
    const IncrementalPluginMetadata& metadata);

[[nodiscard]] std::optional<IncrementalObjectMetadata>
deserialize_incremental_object_metadata(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<IncrementalPluginMetadata>
deserialize_incremental_plugin_metadata(
    std::string_view bytes,
    std::string source_name,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<IncrementalObjectMetadata>
load_incremental_object_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<IncrementalPluginMetadata>
load_incremental_plugin_metadata(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool compile_incremental_object(
    const IncrementalCompileRequest& request,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool link_incremental_plugin(
    const IncrementalLinkRequest& request,
    diagnostic::Engine& diagnostics);

[[nodiscard]] IncrementalPhaseResult compile_incremental_object_cached(
    IncrementalCompileRequest request,
    const std::filesystem::path& cache_directory,
    diagnostic::Engine& diagnostics);

[[nodiscard]] IncrementalPhaseResult link_incremental_plugin_cached(
    IncrementalLinkRequest request,
    const std::filesystem::path& cache_directory,
    diagnostic::Engine& diagnostics);

// Opens the checksummed native payload and verifies that the runtime factory
// inventory still exactly matches the artifact metadata.
[[nodiscard]] std::shared_ptr<HierarchyRegistry> load_incremental_plugin(
    const std::filesystem::path& directory,
    diagnostic::Engine& diagnostics,
    std::string_view expected_compiler_fingerprint = {});

}  // namespace fsim::systemc
