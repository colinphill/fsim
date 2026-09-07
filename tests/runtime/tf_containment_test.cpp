// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_containment.hpp"
#include "fsim/runtime/tf_control.hpp"
#include "fsim/runtime/tf_plugin.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

fsim::runtime::TfBoundCall* reentrant_call{};
fsim::runtime::TfCallError nested_error{fsim::runtime::TfCallError::None};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL no_op(
    const PLI_INT32, const PLI_INT32) {
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throwing_check(
    const PLI_INT32, const PLI_INT32) {
  throw std::runtime_error{"contained checktf exception"};
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throwing_call(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_calltf) {
    (void)tf_putp(1, 9);
    throw std::runtime_error{"contained calltf exception"};
  }
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL reentrant_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_calltf && reentrant_call != nullptr) {
    nested_error = reentrant_call->invoke().error;
  }
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL invalid_api_pointers(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  auto* const bad_text = reinterpret_cast<PLI_BYTE8*>(std::uintptr_t{1});
  auto* const bad_integer =
      reinterpret_cast<PLI_INT32*>(std::uintptr_t{1});
  auto* const bad_expression =
      reinterpret_cast<p_tfexprinfo>(std::uintptr_t{1});
  auto* const bad_node =
      reinterpret_cast<p_tfnodeinfo>(std::uintptr_t{1});
  const auto* const active_instance = tf_getinstance();
  if (tf_warning(bad_text) == 0 || tf_exprinfo(1, bad_expression) != nullptr ||
      tf_nodeinfo(1, bad_node) != nullptr ||
      tf_getlongp(bad_integer, 1) != 3) {
    return 1;
  }
  tf_scale_longdelay(const_cast<PLI_BYTE8*>(active_instance), 1, 0,
                     bad_integer, bad_integer);
  tf_scale_realdelay(const_cast<PLI_BYTE8*>(active_instance), 1.0,
                     reinterpret_cast<double*>(std::uintptr_t{1}));
  tf_unscale_longdelay(const_cast<PLI_BYTE8*>(active_instance), 1, 0,
                       bad_integer, bad_integer);
  tf_unscale_realdelay(const_cast<PLI_BYTE8*>(active_instance), 1.0,
                       reinterpret_cast<double*>(std::uintptr_t{1}));
  return 0;
}

fsim::runtime::TfRegistration task(
    const char* name, const fsim_tf_routine_v3 call,
    const fsim_tf_routine_v3 check = nullptr) {
  return {.kind = fsim::runtime::TfRegistrationKind::Task,
          .user_data = 0,
          .checktf = check,
          .sizetf = nullptr,
          .calltf = call,
          .misctf = nullptr,
          .name = name};
}

fsim::runtime::TfInstanceIdentity instance() {
  return {.design_id = 5, .hierarchy_id = 8, .generation = 2};
}

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error{message};
}

}  // namespace

int main() {
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfContainmentError;
  using fsim::runtime::TfNativePointerAccess;
  using fsim::runtime::TfPluginError;
  using fsim::runtime::TfRegistrationError;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::load_tf_plugin;
  using fsim::runtime::validate_tf_callback_pointer;
  using fsim::runtime::validate_tf_native_pointer;
  using fsim::runtime::validate_tf_plugin_descriptor;
  using fsim::runtime::validate_tf_registration_table;
  using fsim::runtime::validate_and_copy_tf_registrations;

  std::uint32_t writable{};
  require(validate_tf_native_pointer(
              &writable, sizeof(writable), TfNativePointerAccess::Read) ==
              TfContainmentError::None &&
              validate_tf_native_pointer(
                  &writable, sizeof(writable),
                  TfNativePointerAccess::Write) ==
                  TfContainmentError::None &&
              validate_tf_callback_pointer(no_op) ==
                  TfContainmentError::None &&
              validate_tf_native_pointer(
                  nullptr, 1, TfNativePointerAccess::Read) ==
                  TfContainmentError::Null &&
              validate_tf_native_pointer(
                  reinterpret_cast<const void*>(std::uintptr_t{1}), 1,
                  TfNativePointerAccess::Read) != TfContainmentError::None &&
              validate_tf_native_pointer(
                  reinterpret_cast<const void*>(
                      std::numeric_limits<std::uintptr_t>::max() - 1U),
                  8, TfNativePointerAccess::Read) ==
                  TfContainmentError::Overflow,
          "native pointer inspection distinguishes mapped access and failures");

  static const char name[] = "containment";
  static const char version[] = "3";
  static const char producer[] = "fsim-tests";
  fsim_native_plugin_descriptor_v3 descriptor{
      FSIM_NATIVE_PLUGIN_ABI_VERSION,
      sizeof(fsim_native_plugin_descriptor_v3),
      static_cast<std::uint32_t>(sizeof(void*) * 8U),
      0,
      FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
      sizeof(name) - 1U,
      sizeof(version) - 1U,
      sizeof(producer) - 1U,
      0,
      name,
      version,
      producer,
      nullptr,
      1,
      sizeof(fsim_native_plugin_interface_v3),
      reinterpret_cast<const fsim_native_plugin_interface_v3*>(
          std::uintptr_t{1})};
  require(validate_tf_plugin_descriptor(descriptor) ==
              TfPluginError::InterfaceLayout,
          "unmapped interface tables are rejected without dereference");

  const fsim_native_plugin_interface_v3 bad_interface{
      FSIM_NATIVE_PLUGIN_CAPABILITY_TF,
      FSIM_TF_INTERFACE_ABI_VERSION,
      sizeof(fsim_native_plugin_interface_v3),
      0,
      sizeof(fsim_tf_registration_table_v3),
      reinterpret_cast<const void*>(std::uintptr_t{1})};
  descriptor.interfaces = &bad_interface;
  require(validate_tf_plugin_descriptor(descriptor) ==
              TfPluginError::InterfaceLayout,
          "unmapped interface descriptors are rejected before inspection");

  fsim_tf_registration_table_v3 table{
      FSIM_TF_REGISTRATION_TABLE_ABI_VERSION,
      sizeof(fsim_tf_registration_table_v3),
      0,
      1,
      sizeof(fsim_tf_registration_v3),
      0,
      reinterpret_cast<const void*>(std::uintptr_t{1})};
  require(validate_tf_registration_table(table) ==
              fsim::runtime::TfRegistrationTableError::Entries,
          "unmapped registration arrays are rejected without dereference");

  auto invalid_callback = std::bit_cast<fsim_tf_routine_v3>(
      std::uintptr_t{1});
  static const PLI_BYTE8 registration_name[] = "$invalid";
  fsim_tf_registration_v3 registration{
      sizeof(fsim_tf_registration_v3),
      FSIM_TF_REGISTRATION_TASK,
      0,
      0,
      nullptr,
      nullptr,
      invalid_callback,
      nullptr,
      sizeof(registration_name) - 1U,
      0,
      registration_name};
  table.entries = &registration;
  require(validate_and_copy_tf_registrations(table).error ==
              TfRegistrationError::Pointer,
          "non-executable callback addresses are rejected transactionally");
  registration.calltf = no_op;
  registration.name = reinterpret_cast<const PLI_BYTE8*>(
      std::uintptr_t{1});
  require(validate_and_copy_tf_registrations(table).error ==
              TfRegistrationError::Pointer,
          "unmapped registration names are rejected transactionally");
  require(bind_tf_call(task("$bad", invalid_callback)).error ==
              TfCallError::InvalidPointer,
          "direct call binding rejects a non-executable callback address");
  require(fsim::runtime::capture_tf_control_effect_v3(
              reinterpret_cast<void*>(std::uintptr_t{1}),
              FSIM_TF_CONTROL_OUTPUT, 1, 0, nullptr, 0, nullptr, 0, nullptr,
              0) != 0,
          "control capture rejects an unmapped bridge owner");

  const std::array arguments{TfArgument{
      .kind = TfArgumentKind::ReadWrite,
      .width = 8,
      .is_signed = false,
      .lhs_select = -1,
      .rhs_select = -1,
      .expression = "state"}};
  const std::array values{TfArgumentValue{
      .kind = TfValueKind::Integral,
      .vector_words = {{.avalbits = 3, .bvalbits = 0}},
      .real = 0.0,
      .string = {}}};
  require(bind_tf_call(
              task("$throwing_check", no_op, throwing_check),
              arguments, instance())
              .error == TfCallError::CallbackException,
          "checktf exceptions do not escape binding");
  auto throwing = bind_tf_call(
      task("$throwing_call", throwing_call), arguments, instance());
  const auto thrown = throwing.value->invoke(values);
  require(thrown.error == TfCallError::CallbackException &&
              thrown.argument_updates.empty() &&
              thrown.control_effects.empty() &&
              thrown.delay_requests.empty() &&
              thrown.synchronization_requests.empty(),
          "calltf exceptions publish no partial simulator effects");

  auto reentrant = bind_tf_call(task("$reentrant", reentrant_callback), {},
                                instance());
  reentrant_call = reentrant.value.get();
  require(reentrant_call->invoke() &&
              nested_error == TfCallError::ContextBusy,
          "recursive callback entry fails inside the active host boundary");

  auto invalid_api = bind_tf_call(
      task("$invalid_api", invalid_api_pointers), arguments, instance());
  require(invalid_api.value->invoke(values).callback_value == 0,
          "public TF APIs reject invalid plug-in pointers without a fault");
  require(fsim_tf_call_context_enter_v3(
              reinterpret_cast<fsim_tf_call_context_v3*>(
                  std::uintptr_t{1})) != 0,
          "call-context entry rejects an unmapped bridge pointer");

  auto loaded = load_tf_plugin(FSIM_TF_LINK_PROBE_PLUGIN_PATH);
  require(static_cast<bool>(loaded), "C TF probe plug-in loads");
  auto retained = loaded.value->bind(0, {}, instance());
  loaded.value.reset();
  require(static_cast<bool>(retained.value->invoke()),
          "bound call retains its image after the loader handle is released");
  return 0;
}
