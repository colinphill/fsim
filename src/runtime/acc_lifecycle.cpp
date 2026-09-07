// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>

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

struct AccLifecycleState {
  std::mutex mutex;
  bool initialized{};
  std::uint64_t generation{};
  std::uint64_t buffer_generation{};
  std::array<std::string, 20> configuration;
};

AccLifecycleState state;
PLI_BYTE8 product_version[] = "fsim " FSIM_PROJECT_VERSION;
PLI_BYTE8 acc_interface_version[] = "IEEE 1364-2005 ACC";

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

void clear_configuration(AccLifecycleState& lifecycle) noexcept {
  for (auto& value : lifecycle.configuration) {
    value.clear();
  }
}

}  // namespace

extern "C" {

PLI_INT32 acc_error_flag = 0;

PLI_INT32 acc_initialize(void) {
  std::scoped_lock lock{state.mutex};
  if (state.initialized) {
    publish_error(false);
    return 1;
  }
  if (state.generation == std::numeric_limits<std::uint64_t>::max()) {
    publish_error(true);
    return 0;
  }
  clear_configuration(state);
  state.initialized = true;
  ++state.generation;
  state.buffer_generation = 0;
  publish_error(false);
  return 1;
}

void acc_close(void) {
  std::scoped_lock lock{state.mutex};
  clear_configuration(state);
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

}  // namespace fsim::runtime::acc_detail
