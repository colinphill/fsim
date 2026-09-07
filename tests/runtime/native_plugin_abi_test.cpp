// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/native_plugin.hpp"

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>

extern "C" {
std::size_t fsim_native_plugin_abi_c_descriptor_size(void);
const char* fsim_native_plugin_abi_c_descriptor_symbol(void);
fsim_native_plugin_descriptor_v3 fsim_native_plugin_abi_c_descriptor(void);
}

namespace {

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

}  // namespace

int main() {
  using fsim::runtime::NativePluginAbiError;
  using fsim::runtime::copy_native_plugin_metadata;
  using fsim::runtime::validate_native_plugin_descriptor;

  static_assert(std::is_standard_layout_v<fsim_native_plugin_descriptor_v3>);
  static_assert(std::is_trivially_copyable_v<fsim_native_plugin_descriptor_v3>);
  static_assert(std::is_standard_layout_v<fsim_native_plugin_interface_v3>);

  auto descriptor = fsim_native_plugin_abi_c_descriptor();
  require(
      sizeof(descriptor) == fsim_native_plugin_abi_c_descriptor_size(),
      "native plug-in descriptor has identical C and C++ layout");
  require(
      std::strcmp(
          fsim_native_plugin_abi_c_descriptor_symbol(),
          "fsim_native_plugin_descriptor_v3_get") == 0,
      "native plug-in ABI publishes only the exact v3 descriptor symbol");
  require(
      validate_native_plugin_descriptor(descriptor)
          == NativePluginAbiError::None,
      "complete v3 native plug-in metadata validates");

  const auto copied = copy_native_plugin_metadata(descriptor);
  require(
      copied && copied.value->name == "portable-tf"
          && copied.value->version == "3.0"
          && copied.value->producer == "independent-c-probe"
          && copied.value->build_id == "c-build-1"
          && copied.value->identity.size() == 64
          && copied.value->supports(FSIM_NATIVE_PLUGIN_CAPABILITY_TF)
          && !copied.value->supports(FSIM_NATIVE_PLUGIN_CAPABILITY_ACC),
      "native plug-in metadata is copied, identified, and capability-scoped");
  const auto repeated = copy_native_plugin_metadata(descriptor);
  require(
      repeated && repeated.value->identity == copied.value->identity,
      "native plug-in identity is deterministic");

  const auto expect = [&](const auto mutate,
                          const NativePluginAbiError expected,
                          const char* const message) {
    auto invalid = descriptor;
    mutate(invalid);
    require(validate_native_plugin_descriptor(invalid) == expected, message);
    const auto rejected = copy_native_plugin_metadata(invalid);
    require(
        !rejected && rejected.error == expected && !rejected.value,
        "invalid metadata is never partially published");
  };
  expect(
      [](auto& value) { value.abi_version = 2; },
      NativePluginAbiError::AbiVersion,
      "native plug-in ABI rejects v2 directly");
  expect(
      [](auto& value) { value.abi_version = 4; },
      NativePluginAbiError::AbiVersion,
      "native plug-in ABI rejects unknown future versions");
  expect(
      [](auto& value) {
        value.struct_size = FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_BASE_SIZE - 1U;
      },
      NativePluginAbiError::AbiSize,
      "native plug-in ABI rejects a truncated descriptor");
  expect(
      [](auto& value) {
        value.pointer_bits = value.pointer_bits == 64 ? 32 : 64;
      },
      NativePluginAbiError::AbiPointerWidth,
      "native plug-in ABI rejects a foreign pointer width");
  expect(
      [](auto& value) { value.flags = 1; },
      NativePluginAbiError::AbiFlags,
      "native plug-in ABI rejects reserved flags");
  expect(
      [](auto& value) { value.capabilities = 0; },
      NativePluginAbiError::Capabilities,
      "native plug-in ABI requires at least one known capability");
  expect(
      [](auto& value) { value.capabilities = UINT64_C(0x8000000000000000); },
      NativePluginAbiError::Capabilities,
      "native plug-in ABI rejects unknown capabilities");
  expect(
      [](auto& value) { value.name = nullptr; },
      NativePluginAbiError::Name,
      "native plug-in ABI rejects a missing name");
  expect(
      [](auto& value) { value.name_size = FSIM_NATIVE_PLUGIN_MAX_NAME_SIZE + 1U; },
      NativePluginAbiError::Name,
      "native plug-in ABI bounds its name");
  expect(
      [](auto& value) {
        static const char embedded[] = {'b', 'a', 'd', '\0', 'n'};
        value.name = embedded;
        value.name_size = sizeof(embedded);
      },
      NativePluginAbiError::Name,
      "native plug-in ABI rejects embedded terminators");
  expect(
      [](auto& value) { value.version_size = 0; },
      NativePluginAbiError::Version,
      "native plug-in ABI requires a version");
  expect(
      [](auto& value) { value.producer = nullptr; },
      NativePluginAbiError::Producer,
      "native plug-in ABI requires a producer identity");
  expect(
      [](auto& value) {
        value.build_id = "ambiguous";
        value.build_id_size = 0;
      },
      NativePluginAbiError::BuildId,
      "native plug-in ABI canonicalizes absent optional metadata");

  auto no_build_id = descriptor;
  no_build_id.build_id = nullptr;
  no_build_id.build_id_size = 0;
  const auto copied_without_build_id = copy_native_plugin_metadata(no_build_id);
  require(
      copied_without_build_id && copied_without_build_id.value->build_id.empty()
          && copied_without_build_id.value->identity != copied.value->identity,
      "optional build identity has one canonical absent representation");

  auto combined = descriptor;
  combined.capabilities = FSIM_NATIVE_PLUGIN_CAPABILITY_TF
      | FSIM_NATIVE_PLUGIN_CAPABILITY_ACC;
  const auto copied_combined = copy_native_plugin_metadata(combined);
  require(
      copied_combined
          && copied_combined.value->supports(FSIM_NATIVE_PLUGIN_CAPABILITY_TF)
          && copied_combined.value->supports(FSIM_NATIVE_PLUGIN_CAPABILITY_ACC)
          && copied_combined.value->identity != copied.value->identity,
      "common metadata represents the exact supported IEEE PLI capabilities");
  return 0;
}
