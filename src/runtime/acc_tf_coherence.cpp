// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <cstddef>
#include <cstdint>

namespace {

using fsim::runtime::TfContainmentError;
using fsim::runtime::TfNativePointerAccess;
using fsim::runtime::validate_tf_native_pointer;

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] bool readable(const void* const pointer,
                            const std::size_t size) noexcept {
  return pointer != nullptr &&
         validate_tf_native_pointer(pointer, size,
                                    TfNativePointerAccess::Read) ==
             TfContainmentError::None;
}

[[nodiscard]] const fsim_acc_tf_context_v3* active_tf() noexcept;

[[nodiscard]] const fsim_tf_value_bridge_v3* value(
    const fsim_acc_tf_context_v3& tf, const PLI_INT32 index) noexcept {
  const auto* const call = tf.call_context;
  if (index <= 0 || static_cast<std::uint32_t>(index) > tf.argument_count ||
      call->values == nullptr) {
    return nullptr;
  }
  return &call->values[index - 1];
}

[[nodiscard]] bool explicit_instance_matches(
    const fsim_acc_tf_context_v3& tf, const handle instance) noexcept {
  return instance != nullptr &&
         fsim_acc_handle_to_vpi_v3(instance) ==
             tf.call_context->instance->hierarchy_id;
}

[[nodiscard]] bool tf_instance_token_matches(
    const fsim_acc_tf_context_v3& tf, const void* const instance) noexcept {
  return instance != nullptr &&
         instance == static_cast<const void*>(tf.call_context->instance);
}

[[nodiscard]] double fetch_number(const PLI_INT32 index,
                                  const handle instance,
                                  const bool explicit_instance) noexcept {
  const auto* const tf = active_tf();
  const auto* argument = tf == nullptr ? nullptr : value(*tf, index);
  if (tf == nullptr || argument == nullptr ||
      (explicit_instance && !explicit_instance_matches(*tf, instance))) {
    publish_error(true);
    return 0.0;
  }
  if (argument->kind == FSIM_TF_VALUE_REAL) {
    publish_error(false);
    return argument->real;
  }
  if (argument->kind == FSIM_TF_VALUE_INTEGRAL &&
      argument->word_count != 0 && argument->vector_words != nullptr) {
    publish_error(false);
    return static_cast<double>(argument->vector_words[0].avalbits);
  }
  publish_error(true);
  return 0.0;
}

[[nodiscard]] PLI_INT32 fetch_integer(
    const PLI_INT32 index, const handle instance,
    const bool explicit_instance) noexcept {
  const auto* const tf = active_tf();
  const auto* argument = tf == nullptr ? nullptr : value(*tf, index);
  if (tf == nullptr || argument == nullptr ||
      (explicit_instance && !explicit_instance_matches(*tf, instance)) ||
      argument->kind != FSIM_TF_VALUE_INTEGRAL ||
      argument->word_count == 0 || argument->vector_words == nullptr) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return argument->vector_words[0].avalbits;
}

[[nodiscard]] PLI_BYTE8* fetch_string(
    const PLI_INT32 index, const handle instance,
    const bool explicit_instance) noexcept {
  const auto* const tf = active_tf();
  const auto* argument = tf == nullptr ? nullptr : value(*tf, index);
  if (tf == nullptr || argument == nullptr ||
      (explicit_instance && !explicit_instance_matches(*tf, instance))) {
    publish_error(true);
    return nullptr;
  }
  if (argument->kind == FSIM_TF_VALUE_STRING &&
      argument->string_value != nullptr) {
    publish_error(false);
    return argument->string_value;
  }
  if (argument->kind == FSIM_TF_VALUE_INTEGRAL) {
    auto* const result = tf_strgetp(index, 'd');
    publish_error(result == nullptr);
    return result;
  }
  publish_error(true);
  return nullptr;
}

[[nodiscard]] handle argument_handle(const fsim_acc_tf_context_v3& tf,
                                     const PLI_INT32 index) noexcept {
  if (index <= 0 || static_cast<std::uint32_t>(index) > tf.argument_count) {
    publish_error(true);
    return nullptr;
  }
  return fsim_acc_handle_from_vpi_v3(tf.argument_objects[index - 1]);
}

}  // namespace

namespace fsim::runtime::acc_detail {

bool valid_tf_context_binding(
    const fsim_acc_handle_context_v3* const context) noexcept {
  if (context == nullptr || context->tf == nullptr) return true;
  if (!readable(context->tf, sizeof(*context->tf))) return false;
  const auto* const tf = context->tf;
  const auto* const call = fsim_tf_current_call_context_v3();
  if (tf->abi_version != FSIM_ACC_TF_CONTEXT_ABI_VERSION ||
      tf->struct_size < sizeof(*tf) || tf->reserved != 0 ||
      tf->reserved2 != 0 || tf->call_context == nullptr ||
      tf->call_context != call || call->instance == nullptr ||
      call->instance->design_id != context->simulation_identity ||
      call->instance->generation != context->hierarchy_generation ||
      tf->argument_count != call->argument_count ||
      tf->argument_count > FSIM_ACC_TF_MAXIMUM_ARGUMENTS ||
      (tf->argument_count != 0 &&
       (!readable(tf->argument_objects,
                  static_cast<std::size_t>(tf->argument_count) *
                      sizeof(*tf->argument_objects)) ||
        call->arguments == nullptr)) ||
      tf->argc > FSIM_ACC_TF_MAXIMUM_ARGV ||
      (tf->argc == 0 &&
       (tf->argv != nullptr || tf->argv_sizes != nullptr)) ||
      (tf->argc != 0 &&
       (!readable(tf->argv, static_cast<std::size_t>(tf->argc) *
                                sizeof(*tf->argv)) ||
        !readable(tf->argv_sizes,
                  static_cast<std::size_t>(tf->argc) *
                      sizeof(*tf->argv_sizes))))) {
    return false;
  }
  for (std::uint32_t index = 0; index < tf->argument_count; ++index) {
    if (tf->argument_objects[index] == 0) return false;
  }
  for (std::uint32_t index = 0; index < tf->argc; ++index) {
    const auto size = tf->argv_sizes[index];
    if (size == 0 || size > FSIM_ACC_TF_MAXIMUM_ARGV_BYTES ||
        !readable(tf->argv[index], size) || tf->argv[index][size - 1U] != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace fsim::runtime::acc_detail

namespace {

const fsim_acc_tf_context_v3* active_tf() noexcept {
  const auto* const context = fsim::runtime::acc_detail::current_context();
  return context != nullptr && context->tf != nullptr &&
                 fsim::runtime::acc_detail::valid_tf_context_binding(context)
             ? context->tf
             : nullptr;
}

}  // namespace

extern "C" {

PLI_INT32 acc_fetch_argc(void) {
  const auto* const tf = active_tf();
  publish_error(tf == nullptr);
  return tf == nullptr ? 0 : static_cast<PLI_INT32>(tf->argc);
}

PLI_BYTE8** acc_fetch_argv(void) {
  const auto* const tf = active_tf();
  publish_error(tf == nullptr);
  return tf == nullptr ? nullptr : const_cast<PLI_BYTE8**>(tf->argv);
}

double acc_fetch_tfarg(const PLI_INT32 index) {
  return fetch_number(index, nullptr, false);
}

PLI_INT32 acc_fetch_tfarg_int(const PLI_INT32 index) {
  return fetch_integer(index, nullptr, false);
}

PLI_BYTE8* acc_fetch_tfarg_str(const PLI_INT32 index) {
  return fetch_string(index, nullptr, false);
}

double acc_fetch_itfarg(const PLI_INT32 index, const handle instance) {
  return fetch_number(index, instance, true);
}

PLI_INT32 acc_fetch_itfarg_int(const PLI_INT32 index,
                               const handle instance) {
  return fetch_integer(index, instance, true);
}

PLI_BYTE8* acc_fetch_itfarg_str(const PLI_INT32 index,
                                const handle instance) {
  return fetch_string(index, instance, true);
}

handle acc_handle_tfarg(const PLI_INT32 index) {
  const auto* const tf = active_tf();
  if (tf == nullptr) {
    publish_error(true);
    return nullptr;
  }
  return argument_handle(*tf, index);
}

handle acc_handle_itfarg(const PLI_INT32 index, void* const instance) {
  const auto* const tf = active_tf();
  if (tf == nullptr || !tf_instance_token_matches(*tf, instance)) {
    publish_error(true);
    return nullptr;
  }
  return argument_handle(*tf, index);
}

handle acc_handle_tfinst(void) {
  const auto* const tf = active_tf();
  if (tf == nullptr) {
    publish_error(true);
    return nullptr;
  }
  return fsim_acc_handle_from_vpi_v3(
      tf->call_context->instance->hierarchy_id);
}

}  // extern "C"
