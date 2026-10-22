// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_bridge.h"

#include <array>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

constexpr std::size_t maximum_host_depth = 64U;
constexpr std::size_t maximum_text_size = 1U << 20U;
thread_local std::array<const fsim_vhpi_std_host_v1*, maximum_host_depth>
    host_stack{};
thread_local std::size_t host_depth{};

// The innermost host, provided it is large enough to carry the member at
// `offset` and that member is set.
[[nodiscard]] const fsim_vhpi_std_host_v1* innermost_host(
    const std::size_t offset, const std::size_t size) {
  if (host_depth == 0U) {
    return nullptr;
  }
  const auto* const host = host_stack[host_depth - 1U];
  return host->struct_size < offset + size ? nullptr : host;
}

#define FSIM_VHPI_STD_HOST(member)                                            \
  [&]() -> const fsim_vhpi_std_host_v1* {                                     \
    const auto* const candidate = innermost_host(                         \
        offsetof(fsim_vhpi_std_host_v1, member),                              \
        sizeof(fsim_vhpi_std_host_v1::member));                               \
    return candidate != nullptr && candidate->member != nullptr           \
        ? candidate                                                       \
        : nullptr;                                                        \
  }()

#define FSIM_VHPI_FORWARD(member, fallback, ...)                          \
  do {                                                                    \
    const auto* const host = FSIM_VHPI_STD_HOST(member);                      \
    if (host == nullptr) {                                                \
      return fallback;                                                    \
    }                                                                     \
    return host->member(host->user_data __VA_OPT__(, ) __VA_ARGS__);      \
  } while (false)

[[nodiscard]] std::string format_text(const char* format, va_list arguments) {
  if (format == nullptr) {
    return {};
  }
  va_list measure;
  va_copy(measure, arguments);
  const auto needed = std::vsnprintf(nullptr, 0U, format, measure);
  va_end(measure);
  if (needed <= 0) {
    return {};
  }
  const auto size = static_cast<std::size_t>(needed) < maximum_text_size
      ? static_cast<std::size_t>(needed)
      : maximum_text_size;
  std::string text(size + 1U, '\0');
  (void)std::vsnprintf(text.data(), text.size(), format, arguments);
  text.resize(size);
  return text;
}

}  // namespace

extern "C" {

// The physical units of type TIME (IEEE 1076-2019 Annex B), in femtoseconds.
const vhpiPhysT vhpiFS = {0, 1};
const vhpiPhysT vhpiPS = {0, 1000};
const vhpiPhysT vhpiNS = {0, 1000000};
const vhpiPhysT vhpiUS = {0, 1000000000};
const vhpiPhysT vhpiMS = {232, 3567587328U};
const vhpiPhysT vhpiS = {232830, 2764472320U};
const vhpiPhysT vhpiMN = {13969838, 2659581952U};
const vhpiPhysT vhpiHR = {838190317, 661127168U};

FSIM_VHPI_BRIDGE_API int FSIM_VHPI_BRIDGE_CALL
fsim_vhpi_std_host_enter_v1(const fsim_vhpi_std_host_v1* const host) {
  if (host == nullptr || host->abi_version != FSIM_VHPI_STD_HOST_ABI_VERSION
      || host->struct_size < offsetof(fsim_vhpi_std_host_v1, register_cb)
      || host_depth == maximum_host_depth) {
    return -1;
  }
  host_stack[host_depth++] = host;
  return 0;
}

FSIM_VHPI_BRIDGE_API int FSIM_VHPI_BRIDGE_CALL
fsim_vhpi_std_host_leave_v1(const fsim_vhpi_std_host_v1* const host) {
  if (host_depth == 0U || host_stack[host_depth - 1U] != host) {
    return -1;
  }
  host_stack[--host_depth] = nullptr;
  return 0;
}

int vhpi_assert(const vhpiSeverityT severity, char* const formatmsg, ...) {
  const auto* const host = FSIM_VHPI_STD_HOST(assertion);
  if (host == nullptr) {
    return 1;
  }
  va_list arguments;
  va_start(arguments, formatmsg);
  const auto text = format_text(formatmsg, arguments);
  va_end(arguments);
  return host->assertion(host->user_data, severity, text.data(), text.size());
}

vhpiHandleT vhpi_register_cb(vhpiCbDataT* const cb_data_p,
                             const int32_t flags) {
  FSIM_VHPI_FORWARD(register_cb, nullptr, cb_data_p, flags);
}

int vhpi_remove_cb(const vhpiHandleT cb_obj) {
  FSIM_VHPI_FORWARD(remove_cb, 1, cb_obj);
}

int vhpi_disable_cb(const vhpiHandleT cb_obj) {
  FSIM_VHPI_FORWARD(disable_cb, 1, cb_obj);
}

int vhpi_enable_cb(const vhpiHandleT cb_obj) {
  FSIM_VHPI_FORWARD(enable_cb, 1, cb_obj);
}

int vhpi_get_cb_info(const vhpiHandleT object, vhpiCbDataT* const cb_data_p) {
  FSIM_VHPI_FORWARD(get_cb_info, 1, object, cb_data_p);
}

vhpiHandleT vhpi_handle_by_name(const char* const name,
                                const vhpiHandleT scope) {
  FSIM_VHPI_FORWARD(handle_by_name, nullptr, name, scope);
}

vhpiHandleT vhpi_handle_by_index(const vhpiOneToManyT itRel,
                                 const vhpiHandleT parent,
                                 const int32_t indx) {
  FSIM_VHPI_FORWARD(handle_by_index, nullptr, itRel, parent, indx);
}

vhpiHandleT vhpi_handle(const vhpiOneToOneT type,
                        const vhpiHandleT referenceHandle) {
  FSIM_VHPI_FORWARD(handle, nullptr, type, referenceHandle);
}

vhpiHandleT vhpi_iterator(const vhpiOneToManyT type,
                          const vhpiHandleT referenceHandle) {
  FSIM_VHPI_FORWARD(iterator, nullptr, type, referenceHandle);
}

vhpiHandleT vhpi_scan(const vhpiHandleT iterator) {
  FSIM_VHPI_FORWARD(scan, nullptr, iterator);
}

vhpiIntT vhpi_get(const vhpiIntPropertyT property, const vhpiHandleT object) {
  FSIM_VHPI_FORWARD(get, vhpiUndefined, property, object);
}

const vhpiCharT* vhpi_get_str(const vhpiStrPropertyT property,
                              const vhpiHandleT object) {
  FSIM_VHPI_FORWARD(get_str, nullptr, property, object);
}

vhpiRealT vhpi_get_real(const vhpiRealPropertyT property,
                        const vhpiHandleT object) {
  FSIM_VHPI_FORWARD(get_real, 0.0, property, object);
}

vhpiPhysT vhpi_get_phys(const vhpiPhysPropertyT property,
                        const vhpiHandleT object) {
  FSIM_VHPI_FORWARD(get_phys, (vhpiPhysT{0, 0}), property, object);
}

int vhpi_protected_call(const vhpiHandleT varHdl, const vhpiUserFctT userFct,
                        void* const userData) {
  FSIM_VHPI_FORWARD(protected_call, 1, varHdl, userFct, userData);
}

int vhpi_get_value(const vhpiHandleT expr, vhpiValueT* const value_p) {
  FSIM_VHPI_FORWARD(get_value, -1, expr, value_p);
}

int vhpi_put_value(const vhpiHandleT object, vhpiValueT* const value_p,
                   const vhpiPutValueModeT mode) {
  FSIM_VHPI_FORWARD(put_value, 1, object, value_p, mode);
}

int vhpi_schedule_transaction(const vhpiHandleT drivHdl,
                              vhpiValueT* const value_p,
                              const uint32_t numValues,
                              vhpiTimeT* const delayp,
                              const vhpiDelayModeT delayMode,
                              vhpiTimeT* const pulseRejp) {
  FSIM_VHPI_FORWARD(schedule_transaction, 1, drivHdl, value_p, numValues,
                    delayp, delayMode, pulseRejp);
}

int vhpi_format_value(const vhpiValueT* const in_value_p,
                      vhpiValueT* const out_value_p) {
  FSIM_VHPI_FORWARD(format_value, 1, in_value_p, out_value_p);
}

void vhpi_get_time(vhpiTimeT* const time_p, long* const cycles) {
  const auto* const host = FSIM_VHPI_STD_HOST(get_time);
  if (host == nullptr) {
    return;
  }
  host->get_time(host->user_data, time_p, cycles);
}

int vhpi_get_next_time(vhpiTimeT* const time_p) {
  FSIM_VHPI_FORWARD(get_next_time, vhpiNoActivity, time_p);
}

int vhpi_control(const vhpiSimControlT command, ...) {
  // The standard commands take no further argument a caller must supply.
  FSIM_VHPI_FORWARD(control, 1, command, 0);
}

int vhpi_vprintf(const char* const format, va_list args) {
  const auto* const host = FSIM_VHPI_STD_HOST(print);
  if (host == nullptr) {
    return -1;
  }
  const auto text = format_text(format, args);
  return host->print(host->user_data, text.data(), text.size());
}

int vhpi_printf(const char* const format, ...) {
  va_list arguments;
  va_start(arguments, format);
  const auto result = vhpi_vprintf(format, arguments);
  va_end(arguments);
  return result;
}

int vhpi_is_printable(const char ch) {
  // The graphic characters of type CHARACTER (IEEE 1076-2019 16.3).
  const auto code = static_cast<unsigned char>(ch);
  return (code >= 0x20U && code <= 0x7EU) || code >= 0xA0U ? 1 : 0;
}

int vhpi_compare_handles(const vhpiHandleT handle1,
                         const vhpiHandleT handle2) {
  FSIM_VHPI_FORWARD(compare_handles, 0, handle1, handle2);
}

int vhpi_check_error(vhpiErrorInfoT* const error_info_p) {
  FSIM_VHPI_FORWARD(check_error, 0, error_info_p);
}

int vhpi_release_handle(const vhpiHandleT object) {
  FSIM_VHPI_FORWARD(release_handle, 1, object);
}

vhpiHandleT vhpi_create(const vhpiClassKindT kind, const vhpiHandleT handle1,
                        const vhpiHandleT handle2) {
  FSIM_VHPI_FORWARD(create, nullptr, kind, handle1, handle2);
}

vhpiHandleT vhpi_register_foreignf(vhpiForeignDataT* const foreignDatap) {
  FSIM_VHPI_FORWARD(register_foreignf, nullptr, foreignDatap);
}

int vhpi_get_foreignf_info(const vhpiHandleT hdl,
                           vhpiForeignDataT* const foreignDatap) {
  FSIM_VHPI_FORWARD(get_foreignf_info, 1, hdl, foreignDatap);
}

size_t vhpi_get_data(const int32_t id, void* const dataLoc,
                     const size_t numBytes) {
  FSIM_VHPI_FORWARD(get_data, 0U, id, dataLoc, numBytes);
}

size_t vhpi_put_data(const int32_t id, void* const dataLoc,
                     const size_t numBytes) {
  FSIM_VHPI_FORWARD(put_data, 0U, id, dataLoc, numBytes);
}

}  // extern "C"
