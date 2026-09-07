// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_plugin.hpp"

#include "fsim/platform/dynamic_library.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace fsim::runtime {
namespace {

[[nodiscard]] const fsim_native_plugin_interface_v3* interface_at(
    const fsim_native_plugin_descriptor_v3& descriptor,
    const std::uint32_t index) noexcept {
  const auto* const bytes =
      static_cast<const std::byte*>(static_cast<const void*>(
          descriptor.interfaces));
  return reinterpret_cast<const fsim_native_plugin_interface_v3*>(
      bytes + static_cast<std::size_t>(index) * descriptor.interface_stride);
}

}  // namespace

struct TfLoadedPlugin::Impl {
  std::filesystem::path path;
  NativePluginMetadata metadata;
  TfRegistrationTableSummary registration_summary;
  std::vector<TfRegistration> registrations;
  std::shared_ptr<platform::DynamicLibrary> library;
};

TfLoadedPlugin::TfLoadedPlugin(std::shared_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

TfLoadedPlugin::TfLoadedPlugin(TfLoadedPlugin&&) noexcept = default;
TfLoadedPlugin& TfLoadedPlugin::operator=(TfLoadedPlugin&&) noexcept = default;
TfLoadedPlugin::~TfLoadedPlugin() = default;

const std::filesystem::path& TfLoadedPlugin::path() const noexcept {
  return impl_->path;
}

const NativePluginMetadata& TfLoadedPlugin::metadata() const noexcept {
  return impl_->metadata;
}

const TfRegistrationTableSummary& TfLoadedPlugin::registration_summary() const
    noexcept {
  return impl_->registration_summary;
}

const std::vector<TfRegistration>& TfLoadedPlugin::registrations() const
    noexcept {
  return impl_->registrations;
}

TfBindResult TfLoadedPlugin::bind(
    const std::uint32_t index,
    const std::span<const TfArgument> arguments,
    const TfInstanceIdentity instance,
    const TfTimeProfile time_profile,
    const TfContextProfile& context_profile) const noexcept {
  if (index >= impl_->registrations.size()) {
    return {.value = nullptr,
            .error = TfCallError::InvalidRegistration,
            .callback_value = 0,
            .control_effects = {}};
  }
  return bind_tf_call_with_owner(
      impl_->registrations[index], arguments,
      std::shared_ptr<const void>{impl_}, instance, time_profile,
      context_profile);
}

TfMiscCreateResult TfLoadedPlugin::make_misc_dispatcher() const noexcept {
  return make_tf_misc_dispatcher(
      impl_->registrations, std::shared_ptr<const void>{impl_});
}

TfRegistrationTableError validate_tf_registration_table(
    const fsim_tf_registration_table_v3& table) noexcept {
  if (table.abi_version != FSIM_TF_REGISTRATION_TABLE_ABI_VERSION) {
    return TfRegistrationTableError::AbiVersion;
  }
  if (table.struct_size < sizeof(fsim_tf_registration_table_v3)) {
    return TfRegistrationTableError::StructSize;
  }
  if (table.flags != 0 || table.reserved != 0) {
    return TfRegistrationTableError::Flags;
  }
  if (table.entry_count == 0 ||
      table.entry_count > FSIM_TF_MAX_REGISTRATIONS) {
    return TfRegistrationTableError::EntryCount;
  }
  if (table.entry_stride < sizeof(fsim_tf_registration_v3) ||
      table.entry_stride > FSIM_TF_MAX_REGISTRATION_STRIDE) {
    return TfRegistrationTableError::EntryStride;
  }
  if (table.entries == nullptr) {
    return TfRegistrationTableError::Entries;
  }
  if (table.entry_count >
          std::numeric_limits<std::size_t>::max() / table.entry_stride ||
      validate_tf_native_pointer(
          table.entries,
          static_cast<std::size_t>(table.entry_count) * table.entry_stride,
          TfNativePointerAccess::Read) != TfContainmentError::None) {
    return TfRegistrationTableError::Entries;
  }
  if (table.entry_stride % alignof(fsim_tf_registration_v3) != 0 ||
      reinterpret_cast<std::uintptr_t>(table.entries) %
              alignof(fsim_tf_registration_v3) !=
          0) {
    return TfRegistrationTableError::EntryAlignment;
  }
  return TfRegistrationTableError::None;
}

TfPluginError validate_tf_plugin_descriptor(
    const fsim_native_plugin_descriptor_v3& descriptor) noexcept {
  if (validate_native_plugin_descriptor(descriptor) !=
      NativePluginAbiError::None) {
    return TfPluginError::NativeAbi;
  }
  if ((descriptor.capabilities & FSIM_NATIVE_PLUGIN_CAPABILITY_TF) == 0) {
    return TfPluginError::MissingTfCapability;
  }
  if (descriptor.struct_size <
          FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_INTERFACES_SIZE ||
      descriptor.interface_count == 0 ||
      descriptor.interface_count > FSIM_NATIVE_PLUGIN_MAX_INTERFACES ||
      descriptor.interface_stride < sizeof(fsim_native_plugin_interface_v3) ||
      descriptor.interface_stride > FSIM_TF_MAX_REGISTRATION_STRIDE ||
      descriptor.interface_stride % alignof(fsim_native_plugin_interface_v3) !=
          0 ||
      descriptor.interfaces == nullptr ||
      reinterpret_cast<std::uintptr_t>(descriptor.interfaces) %
              alignof(fsim_native_plugin_interface_v3) !=
          0 ||
      descriptor.interface_count >
          std::numeric_limits<std::size_t>::max() /
              descriptor.interface_stride ||
      validate_tf_native_pointer(
          descriptor.interfaces,
          static_cast<std::size_t>(descriptor.interface_count) *
              descriptor.interface_stride,
          TfNativePointerAccess::Read) != TfContainmentError::None) {
    return TfPluginError::InterfaceLayout;
  }

  const fsim_native_plugin_interface_v3* tf_interface{};
  for (std::uint32_t index = 0; index < descriptor.interface_count; ++index) {
    const auto* const candidate = interface_at(descriptor, index);
    if (candidate->struct_size < sizeof(fsim_native_plugin_interface_v3) ||
        candidate->flags != 0 || candidate->descriptor == nullptr ||
        candidate->descriptor_size == 0 ||
        validate_tf_native_pointer(
            candidate->descriptor, candidate->descriptor_size,
            TfNativePointerAccess::Read) != TfContainmentError::None) {
      return TfPluginError::InterfaceLayout;
    }
    if (candidate->capability == FSIM_NATIVE_PLUGIN_CAPABILITY_TF) {
      if (tf_interface != nullptr) {
        return TfPluginError::InterfaceDuplicate;
      }
      tf_interface = candidate;
    }
  }
  if (tf_interface == nullptr) {
    return TfPluginError::MissingTfCapability;
  }
  if (tf_interface->abi_version != FSIM_TF_INTERFACE_ABI_VERSION ||
      tf_interface->descriptor_size < sizeof(fsim_tf_registration_table_v3)) {
    return TfPluginError::InterfaceAbi;
  }
  const auto* const table = static_cast<const fsim_tf_registration_table_v3*>(
      tf_interface->descriptor);
  if (validate_tf_registration_table(*table) !=
      TfRegistrationTableError::None) {
    return TfPluginError::RegistrationTable;
  }
  return TfPluginError::None;
}

TfPluginLoadResult load_tf_plugin(
    const std::filesystem::path& artifact) noexcept {
  try {
    std::string detail;
    auto library = platform::DynamicLibrary::open(artifact, detail);
    if (!library) {
      return {{}, TfPluginError::ArtifactOpen, std::move(detail)};
    }
    auto* const raw_descriptor =
        library->symbol(FSIM_NATIVE_PLUGIN_DESCRIPTOR_SYMBOL, detail);
    if (raw_descriptor == nullptr) {
      return {{}, TfPluginError::MissingDescriptor, std::move(detail)};
    }
    if (validate_tf_native_pointer(
            raw_descriptor, 1, TfNativePointerAccess::Execute) !=
        TfContainmentError::None) {
      return {{}, TfPluginError::InvalidPointer,
              "native plug-in descriptor entry is not executable"};
    }
    const auto get_descriptor =
        reinterpret_cast<fsim_native_plugin_descriptor_v3_get_fn>(
            raw_descriptor);
    const fsim_native_plugin_descriptor_v3* descriptor{};
    try {
      descriptor = get_descriptor();
    } catch (...) {
      return {{}, TfPluginError::DescriptorException,
              "native plug-in descriptor threw an exception"};
    }
    if (descriptor == nullptr) {
      return {{}, TfPluginError::NativeAbi,
              "native plug-in descriptor is absent or invalid"};
    }
    if (validate_tf_native_pointer(
            descriptor, FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_BASE_SIZE,
            TfNativePointerAccess::Read) != TfContainmentError::None) {
      return {{}, TfPluginError::InvalidPointer,
              "native plug-in descriptor address is not readable"};
    }
    if (descriptor->struct_size >=
            FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_INTERFACES_SIZE &&
        validate_tf_native_pointer(
            descriptor, sizeof(fsim_native_plugin_descriptor_v3),
            TfNativePointerAccess::Read) != TfContainmentError::None) {
      return {{}, TfPluginError::InvalidPointer,
              "native plug-in descriptor extent is not readable"};
    }
    const auto descriptor_error = validate_tf_plugin_descriptor(*descriptor);
    if (descriptor_error != TfPluginError::None) {
      return {{}, descriptor_error,
              "native plug-in TF discovery metadata is malformed"};
    }
    const auto metadata = copy_native_plugin_metadata(*descriptor);
    if (!metadata) {
      return {{}, metadata.error == NativePluginAbiError::Allocation
                      ? TfPluginError::Allocation
                      : TfPluginError::NativeAbi,
              "native plug-in metadata could not be copied"};
    }
    const fsim_native_plugin_interface_v3* tf_interface{};
    for (std::uint32_t index = 0; index < descriptor->interface_count;
         ++index) {
      const auto* const candidate = interface_at(*descriptor, index);
      if (candidate->capability == FSIM_NATIVE_PLUGIN_CAPABILITY_TF) {
        tf_interface = candidate;
        break;
      }
    }
    const auto* const table =
        static_cast<const fsim_tf_registration_table_v3*>(
            tf_interface->descriptor);
    auto registrations = validate_and_copy_tf_registrations(*table);
    if (!registrations) {
      return {{}, registrations.error == TfRegistrationError::Allocation
                      ? TfPluginError::Allocation
                      : TfPluginError::RegistrationTable,
              "TF registration descriptors are malformed"};
    }

    auto impl = std::make_shared<TfLoadedPlugin::Impl>();
    impl->path = artifact.lexically_normal();
    impl->metadata = *metadata.value;
    impl->registration_summary = {
        .entry_count = table->entry_count,
        .entry_stride = table->entry_stride,
    };
    impl->registrations = std::move(registrations.value);
    impl->library = std::shared_ptr<platform::DynamicLibrary>{
        std::move(library)};
    return {std::unique_ptr<TfLoadedPlugin>{
                new TfLoadedPlugin{std::move(impl)}},
            TfPluginError::None, {}};
  } catch (const std::bad_alloc&) {
    return {{}, TfPluginError::Allocation,
            "TF plug-in discovery exhausted memory"};
  } catch (...) {
    return {{}, TfPluginError::NativeAbi,
            "TF plug-in discovery failed unexpectedly"};
  }
}

bool tf_plugin_artifact_loaded(
    const std::filesystem::path& artifact) noexcept {
  return platform::DynamicLibrary::is_loaded(artifact);
}

}  // namespace fsim::runtime
