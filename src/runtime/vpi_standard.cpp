// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_bridge.h"
#include "acc_internal.hpp"

#include <vector>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

constexpr std::size_t maximum_text_size = 1U << 20U;
// The contexts entered on this thread, innermost last; it grows with the
// nesting.
thread_local std::vector<fsim_vpi_call_context_v1*> context_stack{};
thread_local std::size_t context_depth{};
// The outermost context entered on the simulation thread. A library may
// query the design from a thread of its own while the simulation thread is
// parked in one of its callbacks (cocotb's bridge); such a query runs in
// that callback's context. Routines that change the simulation (writes,
// callback registration, control) stay with the simulation thread.
std::atomic<fsim_vpi_call_context_v1*> parked_context{nullptr};

enum class Routine : std::uint32_t {
  RegisterCallback = 0,
  RemoveCallback,
  GetCallbackInfo,
  RegisterSystemTaskFunction,
  GetSystemTaskFunctionInfo,
  Handle,
  HandleByName,
  HandleByIndex,
  HandleByMultiIndex,
  HandleMulti,
  Iterate,
  Scan,
  GetProperty,
  GetProperty64,
  GetStringProperty,
  GetDelays,
  PutDelays,
  GetValue,
  PutValue,
  GetTime,
  GetVlogInfo,
  CheckError,
  FreeObject,
  ReleaseHandle,
  CompareObjects,
  GetData,
  PutData,
  GetUserData,
  PutUserData,
  Control,
  VariableControl,
  Print,
  VariablePrint,
  Flush,
  McdOpen,
  McdClose,
  McdName,
  McdPrint,
  McdVariablePrint,
  McdFlush,
};

enum class ExtensionOperation : std::uint32_t {
  ValueArray = 1,
  AssertionCallback = 2,
  LoadExtension = 3,
  CloseExtension = 4,
  LoadInit = 5,
  Load = 6,
  Unload = 7,
  Create = 8,
  GoTo = 9,
  Filter = 10,
};

struct PutValueArguments {
  p_vpi_value value{};
  p_vpi_time time{};
};

struct ValueArrayArguments {
  p_vpi_arrayvalue value{};
  PLI_INT32* indexes{};
  PLI_UINT32 count{};
};

struct AssertionCallbackArguments {
  PLI_INT32 reason{};
  vpi_assertion_callback_func* callback{};
  PLI_BYTE8* user_data{};
};

struct LoadExtensionArguments {
  PLI_BYTE8* name{};
  va_list* arguments{};
};

struct LoadInitArguments {
  vpiHandle scope{};
  PLI_INT32 level{};
};

struct CreateArguments {
  vpiHandle collection{};
  vpiHandle object{};
};

struct GoToArguments {
  p_vpi_time time{};
  PLI_INT32* return_code{};
};

struct FilterArguments {
  PLI_INT32 filter{};
  PLI_INT32 include_matches{};
};

[[nodiscard]] fsim_vpi_call_context_v1* current_context() noexcept {
  return context_depth == 0U ? nullptr : context_stack[context_depth - 1U];
}

[[nodiscard]] constexpr bool query_routine(const Routine routine) noexcept {
  switch (routine) {
  case Routine::GetCallbackInfo:
  case Routine::GetSystemTaskFunctionInfo:
  case Routine::Handle:
  case Routine::HandleByName:
  case Routine::HandleByIndex:
  case Routine::HandleByMultiIndex:
  case Routine::HandleMulti:
  case Routine::Iterate:
  case Routine::Scan:
  case Routine::GetProperty:
  case Routine::GetProperty64:
  case Routine::GetStringProperty:
  case Routine::GetDelays:
  case Routine::GetValue:
  case Routine::GetTime:
  case Routine::GetVlogInfo:
  case Routine::CheckError:
  case Routine::CompareObjects:
  case Routine::GetUserData:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] fsim_vpi_call_context_v1* calling_context(
    const Routine routine) noexcept {
  if (context_depth != 0U) return context_stack[context_depth - 1U];
  return query_routine(routine)
      ? parked_context.load(std::memory_order_acquire) : nullptr;
}

[[nodiscard]] bool valid_context(
    const fsim_vpi_call_context_v1* const context) noexcept {
  return context != nullptr
      && context->abi_version == FSIM_VPI_CONTEXT_ABI_VERSION
      && context->struct_size >= sizeof(fsim_vpi_call_context_v1)
      && context->user_data != nullptr && context->invoke != nullptr;
}

[[nodiscard]] fsim_vpi_handle_v1 internal_handle(
    const vpiHandle handle) noexcept {
  return static_cast<fsim_vpi_handle_v1>(
      reinterpret_cast<std::uintptr_t>(handle));
}

[[nodiscard]] vpiHandle public_handle(
    const fsim_vpi_handle_v1 handle) noexcept {
  return reinterpret_cast<vpiHandle>(static_cast<std::uintptr_t>(handle));
}

template <typename T>
[[nodiscard]] std::uint64_t pointer_argument(T* const pointer) noexcept {
  return static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(pointer));
}

// The save/restart id and the byte count travel in the argument: the id is
// an identifier, not an object handle (IEEE 1800-2023 38.9, 38.31).
[[nodiscard]] std::uint64_t save_restart_argument(
    const PLI_INT32 identifier, const PLI_INT32 size) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(size)) << 32U)
      | static_cast<std::uint32_t>(identifier);
}

[[nodiscard]] std::uint32_t bounded_text_size(
    const PLI_BYTE8* const text) noexcept {
  if (text == nullptr) return 0U;
  std::size_t size{};
  while (size <= maximum_text_size && text[size] != '\0') ++size;
  return size > maximum_text_size
      ? std::numeric_limits<std::uint32_t>::max()
      : static_cast<std::uint32_t>(size);
}

[[nodiscard]] fsim_vpi_service_request_v1 make_request(
    const fsim_vpi_service_operation_v1 operation,
    const fsim_vpi_handle_v1 handle = 0U,
    const std::uint64_t argument = 0U,
    const std::uint64_t user_data = 0U,
    const PLI_BYTE8* const text = nullptr,
    const std::uint32_t flags = 0U) noexcept {
  return {
      static_cast<std::uint32_t>(sizeof(fsim_vpi_service_request_v1)),
      static_cast<std::uint32_t>(operation),
      flags,
      bounded_text_size(text),
      handle,
      argument,
      user_data,
      text,
  };
}

[[nodiscard]] bool invoke(
    const Routine routine,
    const fsim_vpi_service_request_v1& request,
    fsim_vpi_service_result_v1& result) noexcept {
  auto* const context = calling_context(routine);
  if (!valid_context(context)
      || request.text_size == std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  result = {
      static_cast<std::uint32_t>(sizeof(fsim_vpi_service_result_v1)),
      FSIM_VPI_STATUS_INTERNAL_ERROR,
      0U,
      0U,
      0U,
      0U,
      0U,
  };
  try {
    return context->invoke(
               context->user_data, static_cast<std::uint32_t>(routine),
               &request, &result)
            == FSIM_VPI_STATUS_OK
        && result.struct_size >= sizeof(fsim_vpi_service_result_v1)
        && result.status == FSIM_VPI_STATUS_OK && result.reserved == 0U;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool invoke_extension(
    const Routine routine,
    const ExtensionOperation extension,
    fsim_vpi_service_request_v1 request,
    fsim_vpi_service_result_v1& result) noexcept {
  request.flags = static_cast<std::uint32_t>(extension);
  return invoke(routine, request, result);
}

}  // namespace

extern "C" {

vpiHandle vpi_register_cb(p_cb_data const callback) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_CALLBACK,
      callback == nullptr ? 0U : internal_handle(callback->obj),
      callback == nullptr ? 0U : static_cast<std::uint64_t>(callback->reason),
      pointer_argument(callback));
  return invoke(Routine::RegisterCallback, request, result)
      ? public_handle(result.handle) : nullptr;
}

PLI_INT32 vpi_remove_cb(const vpiHandle callback) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_CALLBACK, internal_handle(callback));
  return invoke(Routine::RemoveCallback, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

void vpi_get_cb_info(const vpiHandle callback, p_cb_data const information) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_CALLBACK, internal_handle(callback), 0U,
      pointer_argument(information));
  (void)invoke(Routine::GetCallbackInfo, request, result);
}

vpiHandle vpi_register_systf(p_vpi_systf_data const data) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_SYSTEM_TASK, 0U, 0U, pointer_argument(data));
  return invoke(Routine::RegisterSystemTaskFunction, request, result)
      ? public_handle(result.handle) : nullptr;
}

void vpi_get_systf_info(
    const vpiHandle object, p_vpi_systf_data const information) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_SYSTEM_TASK, internal_handle(object), 0U,
      pointer_argument(information));
  (void)invoke(Routine::GetSystemTaskFunctionInfo, request, result);
}

vpiHandle vpi_handle(const PLI_INT32 type, const vpiHandle reference) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(reference),
      static_cast<std::uint64_t>(type));
  return invoke(Routine::Handle, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_handle_by_name(
    PLI_BYTE8* const name, const vpiHandle scope) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(scope), 0U, 0U, name);
  return invoke(Routine::HandleByName, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_handle_by_index(
    const vpiHandle object, const PLI_INT32 index) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(index));
  return invoke(Routine::HandleByIndex, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_handle_by_multi_index(
    const vpiHandle object, const PLI_INT32 count, PLI_INT32* const indexes) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(count), pointer_argument(indexes));
  return invoke(Routine::HandleByMultiIndex, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_handle_multi(
    const PLI_INT32 type, const vpiHandle first, const vpiHandle second, ...) {
  // The only multi-reference relation is vpiInterModPath, from an output port
  // and an input port (IEEE 1800-2023 37.37, 38.22), so the service takes the
  // two handles and any further references are not read.
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(first),
      static_cast<std::uint64_t>(type), internal_handle(second));
  return invoke(Routine::HandleMulti, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_iterate(const PLI_INT32 type, const vpiHandle reference) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(reference),
      static_cast<std::uint64_t>(type));
  return invoke(Routine::Iterate, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_scan(const vpiHandle iterator) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(iterator));
  return invoke(Routine::Scan, request, result)
      ? public_handle(result.handle) : nullptr;
}

PLI_INT32 vpi_get(const PLI_INT32 property, const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(property));
  return invoke(Routine::GetProperty, request, result)
      ? static_cast<PLI_INT32>(result.value) : vpiUndefined;
}

PLI_INT64 vpi_get64(const PLI_INT32 property, const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(property));
  return invoke(Routine::GetProperty64, request, result)
      ? static_cast<PLI_INT64>(result.value) : -1;
}

PLI_BYTE8* vpi_get_str(const PLI_INT32 property, const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(property));
  return invoke(Routine::GetStringProperty, request, result)
      ? reinterpret_cast<PLI_BYTE8*>(
            static_cast<std::uintptr_t>(result.user_data))
      : nullptr;
}

void vpi_get_delays(const vpiHandle object, p_vpi_delay const delays) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, internal_handle(object), 0U,
      pointer_argument(delays));
  (void)invoke(Routine::GetDelays, request, result);
}

void vpi_put_delays(const vpiHandle object, p_vpi_delay const delays) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, internal_handle(object), 0U,
      pointer_argument(delays));
  (void)invoke(Routine::PutDelays, request, result);
}

void vpi_get_value(const vpiHandle object, p_vpi_value const value) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, internal_handle(object), 0U,
      pointer_argument(value));
  (void)invoke(Routine::GetValue, request, result);
}

vpiHandle vpi_put_value(
    const vpiHandle object, p_vpi_value const value, p_vpi_time const time,
    const PLI_INT32 flags) {
  PutValueArguments packed{value, time};
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, internal_handle(object),
      static_cast<std::uint64_t>(flags), pointer_argument(&packed));
  return invoke(Routine::PutValue, request, result)
      ? public_handle(result.handle) : nullptr;
}

void vpi_get_value_array(
    const vpiHandle object, p_vpi_arrayvalue const value,
    PLI_INT32* const indexes, const PLI_UINT32 count) {
  ValueArrayArguments packed{value, indexes, count};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, internal_handle(object), count,
      pointer_argument(&packed));
  (void)invoke_extension(
      Routine::GetValue, ExtensionOperation::ValueArray, request, result);
}

void vpi_put_value_array(
    const vpiHandle object, p_vpi_arrayvalue const value,
    PLI_INT32* const indexes, const PLI_UINT32 count) {
  ValueArrayArguments packed{value, indexes, count};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, internal_handle(object), count,
      pointer_argument(&packed));
  (void)invoke_extension(
      Routine::PutValue, ExtensionOperation::ValueArray, request, result);
}

void vpi_get_time(const vpiHandle object, p_vpi_time const time) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_TIME, internal_handle(object), 0U,
      pointer_argument(time));
  (void)invoke(Routine::GetTime, request, result);
}

PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info const information) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_LIFECYCLE, 0U, 0U, pointer_argument(information));
  return invoke(Routine::GetVlogInfo, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_chk_error(p_vpi_error_info const information) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_LIFECYCLE, 0U, 0U, pointer_argument(information));
  return invoke(Routine::CheckError, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_free_object(const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object));
  return invoke(Routine::FreeObject, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_release_handle(const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object));
  return invoke(Routine::ReleaseHandle, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_compare_objects(
    const vpiHandle first, const vpiHandle second) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(first), 0U,
      internal_handle(second));
  return invoke(Routine::CompareObjects, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_get_data(
    const PLI_INT32 identifier, PLI_BYTE8* const data,
    const PLI_INT32 size) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, 0U, save_restart_argument(identifier, size),
      pointer_argument(data));
  return invoke(Routine::GetData, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_put_data(
    const PLI_INT32 identifier, PLI_BYTE8* const data,
    const PLI_INT32 size) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_VALUE, 0U, save_restart_argument(identifier, size),
      pointer_argument(data));
  return invoke(Routine::PutData, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

void* vpi_get_userdata(const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_USER_DATA, internal_handle(object));
  return invoke(Routine::GetUserData, request, result)
      ? reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(result.user_data))
      : nullptr;
}

PLI_INT32 vpi_put_userdata(const vpiHandle object, void* const user_data) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_USER_DATA, internal_handle(object), 0U,
      pointer_argument(user_data));
  return invoke(Routine::PutUserData, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_vcontrol(const PLI_INT32 operation, va_list arguments) {
  va_list copy;
  va_copy(copy, arguments);
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_CONTROL, 0U,
      static_cast<std::uint64_t>(operation), pointer_argument(&copy));
  const auto ok = invoke(Routine::VariableControl, request, result);
  va_end(copy);
  return ok ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_control(const PLI_INT32 operation, ...) {
  va_list arguments;
  va_start(arguments, operation);
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_CONTROL, 0U,
      static_cast<std::uint64_t>(operation), pointer_argument(&arguments));
  const auto ok = invoke(Routine::Control, request, result);
  va_end(arguments);
  if (ok && result.value != 0U && operation == vpiSetInteractiveScope) {
    // The ACC routines report the interactive scope this call set.
    va_start(arguments, operation);
    const auto scope = va_arg(arguments, vpiHandle);
    va_end(arguments);
    fsim::runtime::acc_detail::note_interactive_scope(
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(scope)));
  }
  return ok ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_vprintf(PLI_BYTE8* const format, va_list arguments) {
  va_list copy;
  va_copy(copy, arguments);
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_IO, 0U, 0U, pointer_argument(&copy), format);
  const auto ok = invoke(Routine::VariablePrint, request, result);
  va_end(copy);
  return ok ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_printf(PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  fsim_vpi_service_result_v1 service_result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_IO, 0U, 0U, pointer_argument(&arguments), format);
  const auto ok = invoke(Routine::Print, request, service_result);
  va_end(arguments);
  return ok ? static_cast<PLI_INT32>(service_result.value) : 0;
}

PLI_INT32 vpi_flush(void) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(FSIM_VPI_SERVICE_IO);
  return invoke(Routine::Flush, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_UINT32 vpi_mcd_open(PLI_BYTE8* const name) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_IO, 0U, 0U, 0U, name);
  return invoke(Routine::McdOpen, request, result)
      ? static_cast<PLI_UINT32>(result.value) : 0U;
}

PLI_UINT32 vpi_mcd_close(const PLI_UINT32 descriptor) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(FSIM_VPI_SERVICE_IO, 0U, descriptor);
  return invoke(Routine::McdClose, request, result)
      ? static_cast<PLI_UINT32>(result.value) : 0U;
}

PLI_BYTE8* vpi_mcd_name(const PLI_UINT32 descriptor) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(FSIM_VPI_SERVICE_IO, 0U, descriptor);
  return invoke(Routine::McdName, request, result)
      ? reinterpret_cast<PLI_BYTE8*>(
            static_cast<std::uintptr_t>(result.user_data))
      : nullptr;
}

PLI_INT32 vpi_mcd_vprintf(
    const PLI_UINT32 descriptor, PLI_BYTE8* const format, va_list arguments) {
  va_list copy;
  va_copy(copy, arguments);
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_IO, 0U, descriptor, pointer_argument(&copy), format);
  const auto ok = invoke(Routine::McdVariablePrint, request, result);
  va_end(copy);
  return ok ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_mcd_printf(
    const PLI_UINT32 descriptor, PLI_BYTE8* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  fsim_vpi_service_result_v1 service_result{};
  const auto request = make_request(
      FSIM_VPI_SERVICE_IO, 0U, descriptor, pointer_argument(&arguments),
      format);
  const auto ok = invoke(Routine::McdPrint, request, service_result);
  va_end(arguments);
  return ok ? static_cast<PLI_INT32>(service_result.value) : 0;
}

PLI_INT32 vpi_mcd_flush(const PLI_UINT32 descriptor) {
  fsim_vpi_service_result_v1 result{};
  const auto request = make_request(FSIM_VPI_SERVICE_IO, 0U, descriptor);
  return invoke(Routine::McdFlush, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

vpiHandle vpi_register_assertion_cb(
    const vpiHandle assertion, const PLI_INT32 reason,
    vpi_assertion_callback_func* const callback, PLI_BYTE8* const user_data) {
  AssertionCallbackArguments packed{reason, callback, user_data};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_CALLBACK, internal_handle(assertion),
      static_cast<std::uint64_t>(reason), pointer_argument(&packed));
  return invoke_extension(
             Routine::RegisterCallback, ExtensionOperation::AssertionCallback,
             request, result)
      ? public_handle(result.handle) : nullptr;
}

PLI_INT32 vpi_load_extension(
    PLI_BYTE8* const extension_name, PLI_BYTE8* const name,
    const PLI_INT32 mode, ...) {
  va_list arguments;
  va_start(arguments, mode);
  LoadExtensionArguments packed{name, &arguments};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_LIFECYCLE, 0U, static_cast<std::uint64_t>(mode),
      pointer_argument(&packed), extension_name);
  const auto ok = invoke_extension(
      Routine::GetVlogInfo, ExtensionOperation::LoadExtension,
      request, result);
  va_end(arguments);
  return ok ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_close(
    const PLI_INT32 tool, const PLI_INT32 property, PLI_BYTE8* const name) {
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_CONTROL, 0U, static_cast<std::uint64_t>(tool),
      static_cast<std::uint64_t>(property), name);
  return invoke_extension(
             Routine::Control, ExtensionOperation::CloseExtension,
             request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_load_init(
    const vpiHandle collection, const vpiHandle scope,
    const PLI_INT32 level) {
  LoadInitArguments packed{scope, level};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(collection),
      static_cast<std::uint64_t>(level), pointer_argument(&packed));
  return invoke_extension(
             Routine::Handle, ExtensionOperation::LoadInit, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_load(const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object));
  return invoke_extension(
             Routine::FreeObject, ExtensionOperation::Load, request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

PLI_INT32 vpi_unload(const vpiHandle object) {
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object));
  return invoke_extension(
             Routine::ReleaseHandle, ExtensionOperation::Unload,
             request, result)
      ? static_cast<PLI_INT32>(result.value) : 0;
}

vpiHandle vpi_create(
    const PLI_INT32 property, const vpiHandle collection,
    const vpiHandle object) {
  CreateArguments packed{collection, object};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(collection),
      static_cast<std::uint64_t>(property), pointer_argument(&packed));
  return invoke_extension(
             Routine::Handle, ExtensionOperation::Create, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_goto(
    const PLI_INT32 property, const vpiHandle object, p_vpi_time const time,
    PLI_INT32* const return_code) {
  GoToArguments packed{time, return_code};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(property), pointer_argument(&packed));
  return invoke_extension(
             Routine::Handle, ExtensionOperation::GoTo, request, result)
      ? public_handle(result.handle) : nullptr;
}

vpiHandle vpi_filter(
    const vpiHandle object, const PLI_INT32 filter,
    const PLI_INT32 include_matches) {
  FilterArguments packed{filter, include_matches};
  fsim_vpi_service_result_v1 result{};
  auto request = make_request(
      FSIM_VPI_SERVICE_HIERARCHY, internal_handle(object),
      static_cast<std::uint64_t>(filter), pointer_argument(&packed));
  return invoke_extension(
             Routine::Handle, ExtensionOperation::Filter, request, result)
      ? public_handle(result.handle) : nullptr;
}

FSIM_VPI_BRIDGE_API int FSIM_VPI_BRIDGE_CALL
fsim_vpi_call_context_enter_v1(fsim_vpi_call_context_v1* const context) {
  if (!valid_context(context)) {
    return -1;
  }
  if (context_depth == context_stack.size()) {
    try {
      context_stack.push_back(nullptr);
    } catch (...) {
      return -1;
    }
  }
  if (context_depth == 0U) {
    fsim_vpi_call_context_v1* expected = nullptr;
    (void)parked_context.compare_exchange_strong(
        expected, context, std::memory_order_acq_rel);
  }
  context_stack[context_depth++] = context;
  return 0;
}

FSIM_VPI_BRIDGE_API int FSIM_VPI_BRIDGE_CALL
fsim_vpi_call_context_leave_v1(fsim_vpi_call_context_v1* const context) {
  if (context_depth == 0U || context_stack[context_depth - 1U] != context) {
    return -1;
  }
  context_stack[--context_depth] = nullptr;
  if (context_depth == 0U) {
    auto* expected = context;
    (void)parked_context.compare_exchange_strong(
        expected, nullptr, std::memory_order_acq_rel);
  }
  return 0;
}

FSIM_VPI_BRIDGE_API const fsim_vpi_call_context_v1* FSIM_VPI_BRIDGE_CALL
fsim_vpi_current_call_context_v1(void) {
  return current_context();
}

}  // extern "C"

namespace fsim::runtime::acc_detail {

vpiHandle register_vcl_callback(p_cb_data const callback) {
  fsim_vpi_service_result_v1 result{};
  // Extension operation 3: an ACC link's callback.
  const auto request = make_request(
      FSIM_VPI_SERVICE_CALLBACK, internal_handle(callback->obj),
      static_cast<std::uint64_t>(callback->reason), pointer_argument(callback),
      nullptr, 3U);
  return invoke(Routine::RegisterCallback, request, result)
      ? public_handle(result.handle) : nullptr;
}

bool deliver_vcl_changes(const vpiHandle callback) {
  fsim_vpi_service_result_v1 result{};
  // A nonzero argument delivers the pending changes; the callback stays.
  const auto request = make_request(
      FSIM_VPI_SERVICE_CALLBACK, internal_handle(callback), 1U);
  return invoke(Routine::RemoveCallback, request, result);
}

}  // namespace fsim::runtime::acc_detail
