// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/native_plugin_abi.h"

#include <cstdint>
#include <optional>
#include <string>

namespace fsim::runtime {

enum class NativePluginAbiError {
  None,
  AbiVersion,
  AbiSize,
  AbiPointerWidth,
  AbiFlags,
  Capabilities,
  Name,
  Version,
  Producer,
  BuildId,
  Allocation,
};

struct NativePluginMetadata {
  std::uint64_t capabilities{};
  std::string name;
  std::string version;
  std::string producer;
  std::string build_id;
  std::string identity;

  [[nodiscard]] bool supports(const std::uint64_t capability) const noexcept {
    return capability != 0 && (capabilities & capability) == capability;
  }
};

struct NativePluginMetadataResult {
  std::optional<NativePluginMetadata> value;
  NativePluginAbiError error{NativePluginAbiError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == NativePluginAbiError::None && value.has_value();
  }
};

[[nodiscard]] NativePluginAbiError validate_native_plugin_descriptor(
    const fsim_native_plugin_descriptor_v3& descriptor) noexcept;

[[nodiscard]] NativePluginMetadataResult copy_native_plugin_metadata(
    const fsim_native_plugin_descriptor_v3& descriptor) noexcept;

}  // namespace fsim::runtime
