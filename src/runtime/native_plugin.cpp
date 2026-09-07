// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/native_plugin.hpp"

#include "fsim/runtime/tf_containment.hpp"
#include "fsim/support/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

namespace fsim::runtime {
namespace {

[[nodiscard]] bool valid_metadata_text(
    const char* const text,
    const std::uint32_t size,
    const std::uint32_t maximum,
    const bool required) noexcept {
  if (size == 0) {
    return !required && text == nullptr;
  }
  if (text == nullptr || size > maximum ||
      validate_tf_native_pointer(
          text, size, TfNativePointerAccess::Read) !=
          TfContainmentError::None
      || std::memchr(text, '\0', size) != nullptr) {
    return false;
  }
  for (std::uint32_t index = 0; index < size; ++index) {
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte < 0x20U || byte == 0x7fU) {
      return false;
    }
  }
  return true;
}

void hash_u64(support::Sha256& hasher, const std::uint64_t value) noexcept {
  std::array<std::byte, sizeof(value)> encoded{};
  for (std::size_t index = 0; index < encoded.size(); ++index) {
    encoded[index] =
        static_cast<std::byte>((value >> (index * 8U)) & UINT64_C(0xff));
  }
  hasher.update(encoded);
}

void hash_text(support::Sha256& hasher, const std::string_view text) noexcept {
  hash_u64(hasher, static_cast<std::uint64_t>(text.size()));
  hasher.update(text);
}

[[nodiscard]] std::string metadata_identity(
    const NativePluginMetadata& metadata) {
  support::Sha256 hasher;
  hasher.update("fsim-native-plugin-v3");
  hash_u64(hasher, metadata.capabilities);
  hash_text(hasher, metadata.name);
  hash_text(hasher, metadata.version);
  hash_text(hasher, metadata.producer);
  hash_text(hasher, metadata.build_id);
  return support::Sha256::hex(hasher.finish());
}

}  // namespace

NativePluginAbiError validate_native_plugin_descriptor(
    const fsim_native_plugin_descriptor_v3& descriptor) noexcept {
  if (descriptor.abi_version != FSIM_NATIVE_PLUGIN_ABI_VERSION) {
    return NativePluginAbiError::AbiVersion;
  }
  if (descriptor.struct_size < FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_BASE_SIZE) {
    return NativePluginAbiError::AbiSize;
  }
  if (descriptor.pointer_bits != sizeof(void*) * 8U) {
    return NativePluginAbiError::AbiPointerWidth;
  }
  if (descriptor.flags != 0) {
    return NativePluginAbiError::AbiFlags;
  }
  if (descriptor.capabilities == 0
      || (descriptor.capabilities & ~FSIM_NATIVE_PLUGIN_KNOWN_CAPABILITIES)
          != 0) {
    return NativePluginAbiError::Capabilities;
  }
  if (!valid_metadata_text(
          descriptor.name, descriptor.name_size,
          FSIM_NATIVE_PLUGIN_MAX_NAME_SIZE, true)) {
    return NativePluginAbiError::Name;
  }
  if (!valid_metadata_text(
          descriptor.version, descriptor.version_size,
          FSIM_NATIVE_PLUGIN_MAX_VERSION_SIZE, true)) {
    return NativePluginAbiError::Version;
  }
  if (!valid_metadata_text(
          descriptor.producer, descriptor.producer_size,
          FSIM_NATIVE_PLUGIN_MAX_PRODUCER_SIZE, true)) {
    return NativePluginAbiError::Producer;
  }
  if (!valid_metadata_text(
          descriptor.build_id, descriptor.build_id_size,
          FSIM_NATIVE_PLUGIN_MAX_BUILD_ID_SIZE, false)) {
    return NativePluginAbiError::BuildId;
  }
  return NativePluginAbiError::None;
}

NativePluginMetadataResult copy_native_plugin_metadata(
    const fsim_native_plugin_descriptor_v3& descriptor) noexcept {
  const auto validation = validate_native_plugin_descriptor(descriptor);
  if (validation != NativePluginAbiError::None) {
    return {.value = std::nullopt, .error = validation};
  }
  try {
    NativePluginMetadata metadata{
        .capabilities = descriptor.capabilities,
        .name = std::string{descriptor.name, descriptor.name_size},
        .version = std::string{descriptor.version, descriptor.version_size},
        .producer = std::string{descriptor.producer, descriptor.producer_size},
        .build_id = descriptor.build_id == nullptr
            ? std::string{}
            : std::string{descriptor.build_id, descriptor.build_id_size},
        .identity = {},
    };
    metadata.identity = metadata_identity(metadata);
    return {.value = std::move(metadata), .error = NativePluginAbiError::None};
  } catch (const std::bad_alloc&) {
    return {.value = std::nullopt, .error = NativePluginAbiError::Allocation};
  }
}

}  // namespace fsim::runtime
