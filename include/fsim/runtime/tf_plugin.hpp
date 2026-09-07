// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/native_plugin.hpp"
#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_misc.hpp"
#include "fsim/runtime/tf_plugin_abi.h"
#include "fsim/runtime/tf_registration.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fsim::runtime {

enum class TfPluginError {
  None,
  ArtifactOpen,
  MissingDescriptor,
  DescriptorException,
  NativeAbi,
  MissingTfCapability,
  InterfaceLayout,
  InterfaceDuplicate,
  InterfaceAbi,
  RegistrationTable,
  InvalidPointer,
  Allocation,
};

enum class TfRegistrationTableError {
  None,
  AbiVersion,
  StructSize,
  Flags,
  EntryCount,
  EntryStride,
  Entries,
  EntryAlignment,
};

struct TfRegistrationTableSummary {
  std::uint32_t entry_count{};
  std::uint32_t entry_stride{};
};

struct TfPluginLoadResult;

class TfLoadedPlugin final {
public:
  TfLoadedPlugin(TfLoadedPlugin&&) noexcept;
  TfLoadedPlugin& operator=(TfLoadedPlugin&&) noexcept;
  TfLoadedPlugin(const TfLoadedPlugin&) = delete;
  TfLoadedPlugin& operator=(const TfLoadedPlugin&) = delete;
  ~TfLoadedPlugin();

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] const NativePluginMetadata& metadata() const noexcept;
  [[nodiscard]] const TfRegistrationTableSummary& registration_summary() const
      noexcept;
  [[nodiscard]] const std::vector<TfRegistration>& registrations() const
      noexcept;
  [[nodiscard]] TfBindResult bind(
      std::uint32_t index,
      std::span<const TfArgument> arguments = {},
      TfInstanceIdentity instance = {},
      TfTimeProfile time_profile = {},
      const TfContextProfile& context_profile = {}) const noexcept;
  [[nodiscard]] TfMiscCreateResult make_misc_dispatcher() const noexcept;

private:
  struct Impl;
  explicit TfLoadedPlugin(std::shared_ptr<Impl> impl) noexcept;
  std::shared_ptr<Impl> impl_;

  friend struct TfPluginLoadResult;
  friend TfPluginLoadResult load_tf_plugin(
      const std::filesystem::path&) noexcept;
};

struct TfPluginLoadResult {
  std::unique_ptr<TfLoadedPlugin> value;
  TfPluginError error{TfPluginError::None};
  std::string detail;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfPluginError::None && value != nullptr;
  }
};

[[nodiscard]] TfRegistrationTableError validate_tf_registration_table(
    const fsim_tf_registration_table_v3& table) noexcept;

[[nodiscard]] TfPluginError validate_tf_plugin_descriptor(
    const fsim_native_plugin_descriptor_v3& descriptor) noexcept;

[[nodiscard]] TfPluginLoadResult load_tf_plugin(
    const std::filesystem::path& artifact) noexcept;

[[nodiscard]] bool tf_plugin_artifact_loaded(
    const std::filesystem::path& artifact) noexcept;

}  // namespace fsim::runtime
