// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/tf_plugin_abi.h"
#include "fsim/runtime/veriuser.h"

#include <array>
#include <cstdint>

namespace {

constexpr char kPluginName[] = "fsim-tf-cpp-probe";
constexpr char kPluginVersion[] = "3";
constexpr char kPluginProducer[] = "fsim-tests-cpp";
constexpr PLI_BYTE8 kTaskName[] = "$fsim_tf_cpp_task";
constexpr PLI_BYTE8 kFunctionName[] = "$fsim_tf_cpp_function";

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL task_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason == reason_checktf) return tf_nump() == 1 ? 0 : 1;
  if (reason != reason_calltf) return 1;
  return tf_putp(1, tf_getp(1) + user_data);
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL function_callback(
    const PLI_INT32 user_data, const PLI_INT32 reason) {
  if (reason == reason_checktf) return tf_nump() == 1 ? 0 : 1;
  if (reason == reason_sizetf) return 12;
  if (reason != reason_calltf) return 1;
  return tf_putp(0, tf_getp(1) * 2 + user_data);
}

constexpr std::array<fsim_tf_registration_v3, 2> kRegistrations{{
    {
        sizeof(fsim_tf_registration_v3),
        FSIM_TF_REGISTRATION_TASK,
        3,
        0,
        task_callback,
        nullptr,
        task_callback,
        nullptr,
        sizeof(kTaskName) - 1U,
        0,
        kTaskName,
    },
    {
        sizeof(fsim_tf_registration_v3),
        FSIM_TF_REGISTRATION_FUNCTION,
        1,
        0,
        function_callback,
        function_callback,
        function_callback,
        nullptr,
        sizeof(kFunctionName) - 1U,
        0,
        kFunctionName,
    },
}};

constexpr fsim_tf_registration_table_v3 kRegistrationTable{
    FSIM_TF_REGISTRATION_TABLE_ABI_VERSION,
    sizeof(fsim_tf_registration_table_v3),
    0,
    static_cast<std::uint32_t>(kRegistrations.size()),
    sizeof(fsim_tf_registration_v3),
    0,
    kRegistrations.data(),
};

constexpr std::array<fsim_native_plugin_interface_v3, 1> kInterfaces{{{
    FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
    FSIM_TF_INTERFACE_ABI_VERSION,
    sizeof(fsim_native_plugin_interface_v3),
    0,
    sizeof(kRegistrationTable),
    &kRegistrationTable,
}}};

constexpr fsim_native_plugin_descriptor_v3 kDescriptor{
    FSIM_NATIVE_PLUGIN_ABI_VERSION,
    sizeof(fsim_native_plugin_descriptor_v3),
    sizeof(void*) * 8U,
    0,
    FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
    sizeof(kPluginName) - 1U,
    sizeof(kPluginVersion) - 1U,
    sizeof(kPluginProducer) - 1U,
    0,
    kPluginName,
    kPluginVersion,
    kPluginProducer,
    nullptr,
    static_cast<std::uint32_t>(kInterfaces.size()),
    sizeof(fsim_native_plugin_interface_v3),
    kInterfaces.data(),
};

}  // namespace

extern "C" FSIM_NATIVE_PLUGIN_EXPORT const fsim_native_plugin_descriptor_v3*
    FSIM_NATIVE_PLUGIN_CALL fsim_native_plugin_descriptor_v3_get() {
  return &kDescriptor;
}
