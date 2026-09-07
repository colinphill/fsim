// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_plugin.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>

namespace {

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  return user_data + reason;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL misc_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason,
    const PLI_INT32 parameter) {
  return user_data + reason + parameter;
}

void require(const bool condition, const char* const message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

}  // namespace

int main() {
  using fsim::runtime::TfPluginError;
  using fsim::runtime::TfRegistrationTableError;
  using fsim::runtime::load_tf_plugin;
  using fsim::runtime::validate_tf_plugin_descriptor;
  using fsim::runtime::validate_tf_registration_table;

  static const PLI_BYTE8 name[] = "$table_shape";
  const fsim_tf_registration_v3 entry{
      .struct_size = sizeof(fsim_tf_registration_v3),
      .kind = FSIM_TF_REGISTRATION_TASK,
      .user_data = 1,
      .flags = 0,
      .checktf = callback,
      .sizetf = nullptr,
      .calltf = callback,
      .misctf = misc_callback,
      .name_size = sizeof(name) - 1U,
      .reserved = 0,
      .name = name,
  };
  const fsim_tf_registration_table_v3 table{
      .abi_version = FSIM_TF_REGISTRATION_TABLE_ABI_VERSION,
      .struct_size = sizeof(fsim_tf_registration_table_v3),
      .flags = 0,
      .entry_count = 1,
      .entry_stride = sizeof(fsim_tf_registration_v3),
      .reserved = 0,
      .entries = &entry,
  };
  require(validate_tf_registration_table(table) ==
              TfRegistrationTableError::None,
          "complete TF registration table validates");

  const auto expect = [&](const auto mutate,
                          const TfRegistrationTableError expected,
                          const char* const message) {
    auto invalid = table;
    mutate(invalid);
    require(validate_tf_registration_table(invalid) == expected, message);
  };
  expect([](auto& value) { value.abi_version = 2; },
         TfRegistrationTableError::AbiVersion,
         "TF table rejects an older ABI");
  expect([](auto& value) { --value.struct_size; },
         TfRegistrationTableError::StructSize,
         "TF table rejects a truncated header");
  expect([](auto& value) { value.flags = 1; },
         TfRegistrationTableError::Flags,
         "TF table rejects flags");
  expect([](auto& value) { value.reserved = 1; },
         TfRegistrationTableError::Flags,
         "TF table rejects reserved state");
  expect([](auto& value) { value.entry_count = 0; },
         TfRegistrationTableError::EntryCount,
         "TF table rejects an empty inventory");
  expect([](auto& value) {
           value.entry_count = FSIM_TF_MAX_REGISTRATIONS + 1U;
         },
         TfRegistrationTableError::EntryCount,
         "TF table bounds its inventory");
  expect([](auto& value) { --value.entry_stride; },
         TfRegistrationTableError::EntryStride,
         "TF table rejects a truncated entry stride");
  expect([](auto& value) {
           value.entry_stride = FSIM_TF_MAX_REGISTRATION_STRIDE + 1U;
         },
         TfRegistrationTableError::EntryStride,
         "TF table bounds its entry stride");
  expect([](auto& value) { value.entries = nullptr; },
         TfRegistrationTableError::Entries,
         "TF table rejects a missing entry array");
  expect([](auto& value) { ++value.entry_stride; },
         TfRegistrationTableError::EntryAlignment,
         "TF table rejects a misaligned entry stride");
  expect(
      [](auto& value) {
        const auto address = reinterpret_cast<std::uintptr_t>(value.entries);
        value.entries = reinterpret_cast<const void*>(address + 1U);
      },
      TfRegistrationTableError::EntryAlignment,
      "TF table rejects a misaligned entry array");

  const fsim_native_plugin_interface_v3 interface{
      .capability = FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
      .abi_version = FSIM_TF_INTERFACE_ABI_VERSION,
      .struct_size = sizeof(fsim_native_plugin_interface_v3),
      .flags = 0,
      .descriptor_size = sizeof(fsim_tf_registration_table_v3),
      .descriptor = &table,
  };
  static constexpr char plugin_name[] = "tf-table-shape";
  static constexpr char plugin_version[] = "3";
  static constexpr char plugin_producer[] = "fsim-tests";
  const fsim_native_plugin_descriptor_v3 descriptor{
      .abi_version = FSIM_NATIVE_PLUGIN_ABI_VERSION,
      .struct_size = sizeof(fsim_native_plugin_descriptor_v3),
      .pointer_bits = sizeof(void*) * 8U,
      .flags = 0,
      .capabilities = FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
      .name_size = sizeof(plugin_name) - 1U,
      .version_size = sizeof(plugin_version) - 1U,
      .producer_size = sizeof(plugin_producer) - 1U,
      .build_id_size = 0,
      .name = plugin_name,
      .version = plugin_version,
      .producer = plugin_producer,
      .build_id = nullptr,
      .interface_count = 1,
      .interface_stride = sizeof(fsim_native_plugin_interface_v3),
      .interfaces = &interface,
  };
  require(validate_tf_plugin_descriptor(descriptor) == TfPluginError::None,
          "complete TF discovery descriptor validates");

  const auto expect_descriptor = [&](const auto mutate,
                                     const TfPluginError expected,
                                     const char* const message) {
    auto invalid = descriptor;
    mutate(invalid);
    require(validate_tf_plugin_descriptor(invalid) == expected, message);
  };
  expect_descriptor(
      [](auto& value) {
        value.struct_size = FSIM_NATIVE_PLUGIN_DESCRIPTOR_V3_BASE_SIZE;
      },
      TfPluginError::InterfaceLayout,
      "TF discovery requires the interface extension");
  expect_descriptor([](auto& value) { value.interface_count = 0; },
                    TfPluginError::InterfaceLayout,
                    "TF discovery rejects an empty interface table");
  expect_descriptor(
      [](auto& value) {
        value.interface_count = FSIM_NATIVE_PLUGIN_MAX_INTERFACES + 1U;
      },
      TfPluginError::InterfaceLayout,
      "TF discovery bounds the interface table");
  expect_descriptor([](auto& value) { --value.interface_stride; },
                    TfPluginError::InterfaceLayout,
                    "TF discovery rejects a truncated interface stride");
  expect_descriptor(
      [](auto& value) {
        value.interface_stride = FSIM_TF_MAX_REGISTRATION_STRIDE + 1U;
      },
      TfPluginError::InterfaceLayout,
      "TF discovery bounds the interface stride");
  expect_descriptor([](auto& value) { value.interfaces = nullptr; },
                    TfPluginError::InterfaceLayout,
                    "TF discovery rejects a missing interface array");

  const auto expect_interface = [&](const auto mutate,
                                    const TfPluginError expected,
                                    const char* const message) {
    auto invalid_interface = interface;
    mutate(invalid_interface);
    auto invalid_descriptor = descriptor;
    invalid_descriptor.interfaces = &invalid_interface;
    require(validate_tf_plugin_descriptor(invalid_descriptor) == expected,
            message);
  };
  expect_interface([](auto& value) { --value.struct_size; },
                   TfPluginError::InterfaceLayout,
                   "TF discovery rejects a truncated interface record");
  expect_interface([](auto& value) { value.flags = 1; },
                   TfPluginError::InterfaceLayout,
                   "TF discovery rejects interface flags");
  expect_interface([](auto& value) { value.descriptor = nullptr; },
                   TfPluginError::InterfaceLayout,
                   "TF discovery rejects a missing capability descriptor");
  expect_interface([](auto& value) { value.descriptor_size = 0; },
                   TfPluginError::InterfaceLayout,
                   "TF discovery rejects an empty capability descriptor");
  expect_interface(
      [](auto& value) { value.capability = FSIM_NATIVE_PLUGIN_CAPABILITY_ACC; },
      TfPluginError::MissingTfCapability,
      "TF discovery requires a table for its declared capability");
  expect_interface([](auto& value) { --value.abi_version; },
                   TfPluginError::InterfaceAbi,
                   "TF discovery rejects an older interface ABI");
  expect_interface([](auto& value) { --value.descriptor_size; },
                   TfPluginError::InterfaceAbi,
                   "TF discovery rejects a truncated table descriptor");

  const fsim_native_plugin_interface_v3 duplicate_interfaces[]{interface,
                                                                interface};
  auto duplicate_descriptor = descriptor;
  duplicate_descriptor.interface_count = 2;
  duplicate_descriptor.interfaces = duplicate_interfaces;
  require(validate_tf_plugin_descriptor(duplicate_descriptor) ==
              TfPluginError::InterfaceDuplicate,
          "TF discovery rejects duplicate capability tables");

  auto invalid_table = table;
  invalid_table.entry_count = 0;
  auto invalid_table_interface = interface;
  invalid_table_interface.descriptor = &invalid_table;
  auto invalid_table_descriptor = descriptor;
  invalid_table_descriptor.interfaces = &invalid_table_interface;
  require(validate_tf_plugin_descriptor(invalid_table_descriptor) ==
              TfPluginError::RegistrationTable,
          "TF discovery rejects a malformed registration table");

  auto missing_capability = descriptor;
  missing_capability.capabilities = FSIM_NATIVE_PLUGIN_CAPABILITY_ACC;
  require(validate_tf_plugin_descriptor(missing_capability) ==
              TfPluginError::MissingTfCapability,
          "TF discovery rejects plug-ins that do not declare TF");

  const auto loaded = load_tf_plugin(FSIM_TF_LINK_PROBE_PLUGIN_PATH);
  require(loaded && loaded.value->metadata().name == "fsim-tf-link-probe" &&
              loaded.value->metadata().supports(
                  FSIM_NATIVE_PLUGIN_CAPABILITY_TF) &&
              loaded.value->registration_summary().entry_count == 2 &&
              loaded.value->registration_summary().entry_stride ==
                  sizeof(entry) &&
              loaded.value->registrations().size() == 2 &&
              loaded.value->registrations().front().name ==
                  "$fsim_tf_link_probe" &&
              loaded.value->path() ==
                  std::filesystem::path{FSIM_TF_LINK_PROBE_PLUGIN_PATH}
                      .lexically_normal(),
          "TF loader discovers one bounded v3 capability table");

  const auto missing = load_tf_plugin(FSIM_TF_LINK_LIBRARY_PATH);
  require(!missing && missing.error == TfPluginError::MissingDescriptor &&
              !missing.value,
          "TF loader rejects an image without the exact v3 descriptor");
  const auto absent = load_tf_plugin(
      std::filesystem::path{FSIM_TF_LINK_PROBE_PLUGIN_PATH}.concat(".missing"));
  require(!absent && absent.error == TfPluginError::ArtifactOpen &&
              !absent.value,
          "TF loader rejects an absent image without partial state");
  return 0;
}
