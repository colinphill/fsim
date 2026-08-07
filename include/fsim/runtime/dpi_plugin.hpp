// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/dpi_plugin_abi.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::uint32_t systemverilog_dpi_plugin_manifest_version = 1;

enum class SystemVerilogDpiPluginPlatform {
  Posix,
  Msvc,
};

enum class SystemVerilogDpiPluginError {
  None,
  ManifestVersion,
  InvalidName,
  InvalidPath,
  InvalidSource,
  DuplicateEntry,
  InvalidSymbol,
  MissingCompiler,
  ArtifactOpen,
  MissingSymbol,
  MissingDescriptor,
  AbiVersion,
  AbiSize,
  AbiPointerWidth,
  AbiFlags,
  AbiName,
  ArtifactRead,
};

struct SystemVerilogDpiPluginManifest {
  std::uint32_t version{systemverilog_dpi_plugin_manifest_version};
  std::string name;
  std::vector<std::filesystem::path> sources;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> libraries;
  std::vector<std::string> imported_symbols;
  std::vector<std::string> exported_symbols;
};

struct SystemVerilogDpiPluginCommand {
  std::vector<std::string> arguments;
};

struct SystemVerilogDpiPluginBuildPlan {
  std::filesystem::path artifact;
  std::vector<std::filesystem::path> discovery_candidates;
  std::vector<SystemVerilogDpiPluginCommand> compile_commands;
  SystemVerilogDpiPluginCommand link_command;
};

struct SystemVerilogDpiPluginPlanResult {
  SystemVerilogDpiPluginBuildPlan value;
  SystemVerilogDpiPluginError error{SystemVerilogDpiPluginError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiPluginError::None;
  }
};

class SystemVerilogDpiPluginSymbol final {
 public:
  [[nodiscard]] void* address() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept;

  SystemVerilogDpiPluginSymbol(
      void* address, std::shared_ptr<const void> lifetime) noexcept;

 private:
  void* address_{};
  std::shared_ptr<const void> lifetime_;
};

class SystemVerilogDpiLoadedPlugin final {
 public:
  struct Impl;

  ~SystemVerilogDpiLoadedPlugin();
  SystemVerilogDpiLoadedPlugin(SystemVerilogDpiLoadedPlugin&&) noexcept;
  SystemVerilogDpiLoadedPlugin& operator=(
      SystemVerilogDpiLoadedPlugin&&) noexcept;
  SystemVerilogDpiLoadedPlugin(const SystemVerilogDpiLoadedPlugin&) = delete;
  SystemVerilogDpiLoadedPlugin& operator=(
      const SystemVerilogDpiLoadedPlugin&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] std::optional<SystemVerilogDpiPluginSymbol> imported_symbol(
      std::string_view name) const noexcept;
  [[nodiscard]] std::optional<SystemVerilogDpiPluginSymbol> exported_symbol(
      std::string_view name) const noexcept;
  [[nodiscard]] bool quarantine() noexcept;

  explicit SystemVerilogDpiLoadedPlugin(std::unique_ptr<Impl> impl) noexcept;

 private:
  std::unique_ptr<Impl> impl_;
};

struct SystemVerilogDpiPluginLoadResult {
  std::unique_ptr<SystemVerilogDpiLoadedPlugin> value;
  SystemVerilogDpiPluginError error{SystemVerilogDpiPluginError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiPluginError::None && value != nullptr;
  }
};

struct SystemVerilogDpiPluginProvenance {
  std::string manifest_digest;
  std::string artifact_digest;
  std::string cache_key;
};

struct SystemVerilogDpiPluginProvenanceResult {
  SystemVerilogDpiPluginProvenance value;
  SystemVerilogDpiPluginError error{SystemVerilogDpiPluginError::None};
  std::string message;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiPluginError::None;
  }
};

[[nodiscard]] SystemVerilogDpiPluginPlanResult
plan_systemverilog_dpi_plugin(
    const SystemVerilogDpiPluginManifest& manifest,
    const std::filesystem::path& source_root,
    const std::filesystem::path& build_root,
    const std::vector<std::filesystem::path>& discovery_roots,
    std::string compiler,
    SystemVerilogDpiPluginPlatform platform);

[[nodiscard]] SystemVerilogDpiPluginLoadResult
load_systemverilog_dpi_plugin(
    const std::filesystem::path& artifact,
    const SystemVerilogDpiPluginManifest& manifest);

[[nodiscard]] SystemVerilogDpiPluginError
validate_systemverilog_dpi_plugin_descriptor(
    const fsim_dpi_plugin_descriptor_v1& descriptor,
    const SystemVerilogDpiPluginManifest& manifest) noexcept;

[[nodiscard]] SystemVerilogDpiPluginProvenanceResult
provenance_systemverilog_dpi_plugin(
    const SystemVerilogDpiPluginManifest& manifest,
    const std::filesystem::path& artifact,
    std::string_view toolchain_identity,
    SystemVerilogDpiPluginPlatform platform);

[[nodiscard]] bool systemverilog_dpi_plugin_artifact_loaded(
    const std::filesystem::path& artifact) noexcept;

}  // namespace fsim::runtime
