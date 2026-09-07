// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/native_plugin_abi.h"
#include "fsim/runtime/tf_plugin_abi.h"
#include "fsim/runtime/veriuser.h"

static const char plugin_name[] = "fsim-tf-link-probe";
static const char plugin_version[] = "3";
static const char plugin_producer[] = "fsim-tests";

static PLI_INT32 FSIM_NATIVE_PLUGIN_CALL tf_probe_task_callback(
    PLI_INT32 user_data, PLI_INT32 reason) {
  (void)user_data;
  return reason == reason_checktf || reason == reason_calltf ? 0 : 1;
}

static PLI_INT32 FSIM_NATIVE_PLUGIN_CALL tf_probe_function_callback(
    PLI_INT32 user_data, PLI_INT32 reason) {
  if (reason == reason_checktf) {
    return 0;
  }
  if (reason == reason_sizetf) {
    return 17;
  }
  if (reason == reason_calltf) {
    return tf_putp(0, user_data);
  }
  return 1;
}

static PLI_INT32 FSIM_NATIVE_PLUGIN_CALL tf_probe_misc_callback(
    PLI_INT32 user_data, PLI_INT32 reason, PLI_INT32 parameter) {
  (void)user_data;
  (void)reason;
  (void)parameter;
  return 0;
}

static const PLI_BYTE8 task_name[] = "$fsim_tf_link_probe";
static const PLI_BYTE8 function_name[] = "$fsim_tf_function_probe";
static const fsim_tf_registration_v3 registrations[] = {
    {
        (uint32_t)sizeof(fsim_tf_registration_v3),
        FSIM_TF_REGISTRATION_TASK,
        7,
        0,
        tf_probe_task_callback,
        0,
        tf_probe_task_callback,
        tf_probe_misc_callback,
        (uint32_t)(sizeof(task_name) - 1u),
        0,
        task_name,
    },
    {
        (uint32_t)sizeof(fsim_tf_registration_v3),
        FSIM_TF_REGISTRATION_FUNCTION,
        23,
        0,
        tf_probe_function_callback,
        tf_probe_function_callback,
        tf_probe_function_callback,
        tf_probe_misc_callback,
        (uint32_t)(sizeof(function_name) - 1u),
        0,
        function_name,
    },
};
static const fsim_tf_registration_table_v3 registration_table = {
    FSIM_TF_REGISTRATION_TABLE_ABI_VERSION,
    (uint32_t)sizeof(fsim_tf_registration_table_v3),
    0,
    (uint32_t)(sizeof(registrations) / sizeof(registrations[0])),
    (uint32_t)sizeof(fsim_tf_registration_v3),
    0,
    registrations,
};
static const fsim_native_plugin_interface_v3 interfaces[] = {{
    FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
    FSIM_TF_INTERFACE_ABI_VERSION,
    (uint32_t)sizeof(fsim_native_plugin_interface_v3),
    0,
    (uint32_t)sizeof(registration_table),
    &registration_table,
}};
static const fsim_native_plugin_descriptor_v3 descriptor = {
    FSIM_NATIVE_PLUGIN_ABI_VERSION,
    (uint32_t)sizeof(fsim_native_plugin_descriptor_v3),
    (uint32_t)(sizeof(void*) * 8u),
    0,
    FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
    (uint32_t)(sizeof(plugin_name) - 1u),
    (uint32_t)(sizeof(plugin_version) - 1u),
    (uint32_t)(sizeof(plugin_producer) - 1u),
    0,
    plugin_name,
    plugin_version,
    plugin_producer,
    0,
    (uint32_t)(sizeof(interfaces) / sizeof(interfaces[0])),
    (uint32_t)sizeof(fsim_native_plugin_interface_v3),
    interfaces,
};

FSIM_NATIVE_PLUGIN_EXPORT const fsim_native_plugin_descriptor_v3*
    FSIM_NATIVE_PLUGIN_CALL fsim_native_plugin_descriptor_v3_get(void) {
  return &descriptor;
}

FSIM_NATIVE_PLUGIN_EXPORT PLI_INT32 FSIM_NATIVE_PLUGIN_CALL
fsim_tf_link_probe(void) {
  return tf_getp(1) == 0 && tf_igetp(1, 0) == 0 &&
         tf_getinstance() == 0 && tf_exprinfo(1, 0) == 0 &&
         tf_nodeinfo(1, 0) == 0 && tf_gettime() == 0 &&
         tf_synchronize() != 0 && tf_rosynchronize() != 0;
}
