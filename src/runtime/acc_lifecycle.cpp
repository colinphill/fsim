// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>

#ifndef FSIM_PROJECT_VERSION
#define FSIM_PROJECT_VERSION "unknown"
#endif

namespace {

constexpr std::uint32_t kMaximumConfigurationValueSize = 4096;
constexpr std::array<PLI_INT32, 10> kConfigurationItems{
    accPathDelayCount,      accPathDelimStr,      accDisplayErrors,
    accDefaultAttr0,        accToHiZDelay,        accEnableArgs,
    accDisplayWarnings,     accDevelopmentVersion, accMapToMipd,
    accMinTypMaxDelays,
};

constexpr std::array<std::string_view, 3> kEnableArgRoutines{
    "acc_handle_modpath", "acc_handle_tchk", "acc_set_scope"};

struct AccLifecycleState {
  std::mutex mutex;
  bool initialized{};
  std::uint64_t generation{};
  std::uint64_t buffer_generation{};
  std::array<std::string, 20> configuration;
  // accEnableArgs names three independent switches (Table 129).
  std::array<bool, 3> enabled_arguments{};
};

AccLifecycleState state;
PLI_BYTE8 product_version[] = "fsim " FSIM_PROJECT_VERSION;
PLI_BYTE8 acc_interface_version[] = "IEEE 1364-2001 ACC";

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] bool supported_configuration_item(
    const PLI_INT32 item) noexcept {
  return std::ranges::find(kConfigurationItems, item) !=
         kConfigurationItems.end();
}

[[nodiscard]] bool bounded_configuration_value(
    const PLI_BYTE8* const value, std::uint32_t& size) noexcept {
  size = 0;
  if (value == nullptr) return false;
  std::uint32_t checked{};
  while (checked <= kMaximumConfigurationValueSize) {
    const auto remaining = kMaximumConfigurationValueSize + 1U - checked;
    const auto step = std::min(UINT32_C(64), remaining);
    if (fsim::runtime::validate_tf_native_pointer(
            value + checked, step,
            fsim::runtime::TfNativePointerAccess::Read) !=
        fsim::runtime::TfContainmentError::None) {
      return false;
    }
    for (std::uint32_t index = 0; index < step; ++index) {
      const auto byte = static_cast<unsigned char>(value[checked + index]);
      if (byte == 0) {
        size = checked + index;
        return true;
      }
      if (byte < 0x20U || byte == 0x7fU) return false;
    }
    checked += step;
  }
  return false;
}

// The default of every parameter (IEEE 1364-2001 Tables 125-134).
void reset_configuration(AccLifecycleState& lifecycle) noexcept {
  for (auto& value : lifecycle.configuration) {
    value.clear();
  }
  const auto set = [&lifecycle](const PLI_INT32 item, const char* const value) {
    lifecycle.configuration[static_cast<std::size_t>(item)] = value;
  };
  set(accPathDelayCount, "6");
  set(accPathDelimStr, "$");
  set(accDisplayErrors, "true");
  set(accDefaultAttr0, "false");
  set(accToHiZDelay, "from_user");
  set(accEnableArgs, "no_acc_handle_modpath");
  set(accDisplayWarnings, "false");
  set(accDevelopmentVersion, "IEEE 1364 PLI");
  set(accMapToMipd, "max");
  set(accMinTypMaxDelays, "false");
  lifecycle.enabled_arguments = {};
}

// Applies one accEnableArgs value: a routine name enables its optional
// arguments, the no_ form ignores them; other values change no switch.
void apply_enable_argument(AccLifecycleState& lifecycle,
                           const std::string_view value) noexcept {
  const bool disable = value.starts_with("no_");
  const auto routine = disable ? value.substr(3) : value;
  for (std::size_t index = 0; index < kEnableArgRoutines.size(); ++index) {
    if (kEnableArgRoutines[index] == routine) {
      lifecycle.enabled_arguments[index] = !disable;
    }
  }
}

// The display switches default to their documented values until the
// lifecycle is initialized; the caller holds no lock.
[[nodiscard]] bool display_enabled(const PLI_INT32 item,
                                   const bool default_value) noexcept {
  std::scoped_lock lock{state.mutex};
  if (!state.initialized) return default_value;
  return state.configuration[static_cast<std::size_t>(item)] == "true";
}

// Writes through the calling routine's output channel, the host's print path.
void show_message(const bool error, const std::string_view routine,
                  const std::string_view detail) noexcept {
  const auto* const tf = fsim::runtime::acc_detail::current_tf_context();
  if (tf == nullptr || tf->call_context == nullptr ||
      tf->call_context->control_emit == nullptr) {
    return;
  }
  try {
    const std::string text =
        std::string{routine} + "(): " + std::string{detail};
    (void)tf->call_context->control_emit(
        tf->call_context->control_user_data,
        error ? FSIM_TF_CONTROL_ERROR : FSIM_TF_CONTROL_WARNING, 1,
        0, "ACC", 3, nullptr, 0, text.data(),
        static_cast<std::uint32_t>(text.size()));
  } catch (...) {
  }
}

}  // namespace

extern "C" {

PLI_INT32 acc_error_flag = 0;

PLI_INT32 acc_initialize(void) {
  std::scoped_lock lock{state.mutex};
  if (state.initialized) {
    // Initialization again restores every default (IEEE 1364-2001 23.62).
    reset_configuration(state);
    publish_error(false);
    return 1;
  }
  if (state.generation == std::numeric_limits<std::uint64_t>::max()) {
    publish_error(true);
    return 0;
  }
  reset_configuration(state);
  state.initialized = true;
  ++state.generation;
  state.buffer_generation = 0;
  publish_error(false);
  return 1;
}

void acc_close(void) {
  std::scoped_lock lock{state.mutex};
  reset_configuration(state);
  state.initialized = false;
  state.buffer_generation = 0;
  publish_error(false);
}

PLI_INT32 acc_configure(const PLI_INT32 item, PLI_BYTE8* const value) {
  if (!supported_configuration_item(item)) {
    std::scoped_lock lock{state.mutex};
    publish_error(true);
    return 0;
  }
  std::uint32_t size{};
  if (!bounded_configuration_value(value, size)) {
    std::scoped_lock lock{state.mutex};
    publish_error(true);
    return 0;
  }

  try {
    std::string candidate{value, size};
    std::scoped_lock lock{state.mutex};
    if (!state.initialized) {
      publish_error(true);
      return 0;
    }
    if (item == accEnableArgs) apply_enable_argument(state, candidate);
    state.configuration[static_cast<std::size_t>(item)].swap(candidate);
    publish_error(false);
    return 1;
  } catch (const std::bad_alloc&) {
    std::scoped_lock lock{state.mutex};
    publish_error(true);
    return 0;
  } catch (...) {
    std::scoped_lock lock{state.mutex};
    publish_error(true);
    return 0;
  }
}

PLI_INT32 acc_product_type(void) {
  std::scoped_lock lock{state.mutex};
  publish_error(false);
  return accSimulator;
}

PLI_BYTE8* acc_product_version(void) {
  std::scoped_lock lock{state.mutex};
  publish_error(false);
  return product_version;
}

void acc_reset_buffer(void) {
  std::scoped_lock lock{state.mutex};
  if (!state.initialized ||
      state.buffer_generation == std::numeric_limits<std::uint64_t>::max()) {
    publish_error(true);
    return;
  }
  fsim::runtime::acc_detail::reset_read_borrowed_storage();
  fsim::runtime::acc_detail::reset_write_borrowed_storage();
  ++state.buffer_generation;
  publish_error(false);
}

PLI_BYTE8* acc_version(void) {
  std::scoped_lock lock{state.mutex};
  publish_error(false);
  return acc_interface_version;
}

const PLI_BYTE8* FSIM_NATIVE_PLUGIN_CALL fsim_acc_path_delimiter_v3(void) {
  // The returned text stays valid until the next call.
  static thread_local std::string delimiter;
  std::scoped_lock lock{state.mutex};
  const auto& configured =
      state.configuration[static_cast<std::size_t>(accPathDelimStr)];
  try {
    delimiter = state.initialized && !configured.empty() ? configured : "$";
  } catch (...) {
    return "$";
  }
  return delimiter.c_str();
}

}  // extern "C"

namespace fsim::runtime::acc_detail {

bool configuration_enabled(const PLI_INT32 item,
                           const std::string_view routine) noexcept {
  std::scoped_lock lock{state.mutex};
  if (!state.initialized || !supported_configuration_item(item) ||
      item < 0 || static_cast<std::size_t>(item) >=
                      state.configuration.size()) {
    return false;
  }
  if (item == accEnableArgs) {
    const auto found = std::ranges::find(kEnableArgRoutines, routine);
    return found != kEnableArgRoutines.end() &&
           state.enabled_arguments[static_cast<std::size_t>(
               found - kEnableArgRoutines.begin())];
  }
  return state.configuration[static_cast<std::size_t>(item)] == routine;
}

std::optional<std::string> configuration_value(
    const PLI_INT32 item) noexcept {
  std::scoped_lock lock{state.mutex};
  if (!state.initialized || !supported_configuration_item(item) || item < 0 ||
      static_cast<std::size_t>(item) >= state.configuration.size()) {
    return std::nullopt;
  }
  try {
    return state.configuration[static_cast<std::size_t>(item)];
  } catch (...) {
    return std::nullopt;
  }
}

void report_error(const bool failed, const std::string_view routine) noexcept {
  acc_error_flag = failed ? 1 : 0;
  if (!failed || !display_enabled(accDisplayErrors, true)) return;
  show_message(true, routine, "the call was rejected");
}

void report_warning(const std::string_view routine,
                    const std::string_view message) noexcept {
  if (!display_enabled(accDisplayWarnings, false)) return;
  show_message(false, routine, message);
}

}  // namespace fsim::runtime::acc_detail
