// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <map>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace {

using ScopeKey = std::pair<std::uint64_t, std::uint64_t>;

struct ScopeState {
  std::uint64_t pli_scope{};
  std::uint64_t interactive_scope{};
  PLI_INT32 interactive_callback{};
};

std::mutex scope_mutex;
std::map<ScopeKey, ScopeState> scope_states;
thread_local std::array<PLI_BYTE8, FSIM_ACC_NAME_MAXIMUM_BYTES + 1U>
    name_buffer{};

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] const fsim_acc_handle_context_v3* context() noexcept {
  return fsim::runtime::acc_detail::current_context();
}

[[nodiscard]] bool copy_name(const PLI_BYTE8* const name,
                             std::string& result) noexcept {
  if (name == nullptr) return false;
  std::uint32_t checked{};
  const auto base = reinterpret_cast<std::uintptr_t>(name);
  try {
    while (checked <= FSIM_ACC_NAME_MAXIMUM_BYTES) {
      const auto remaining = FSIM_ACC_NAME_MAXIMUM_BYTES + 1U - checked;
      const auto step = std::min(UINT32_C(64), remaining);
      if (checked > std::numeric_limits<std::uintptr_t>::max() - base) {
        return false;
      }
      const auto* const window = reinterpret_cast<const PLI_BYTE8*>(
          base + static_cast<std::uintptr_t>(checked));
      if (fsim::runtime::validate_tf_native_pointer(
              window, step,
              fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None) {
        return false;
      }
      for (std::uint32_t index = 0; index < step; ++index) {
        const auto byte = static_cast<unsigned char>(window[index]);
        if (byte == 0) {
          if (result.empty() && index == 0) return false;
          result.append(window, index);
          return true;
        }
        if (byte < 0x20U || byte == 0x7fU) return false;
      }
      result.append(window, step);
      checked += step;
    }
  } catch (...) {
  }
  return false;
}

[[nodiscard]] bool resolve_type(const std::uint64_t object,
                                PLI_INT32& type) noexcept {
  const auto* const active = context();
  if (active == nullptr || object == 0) return false;
  try {
    return active->resolve(active->user_data, object, &type) ==
               FSIM_ACC_VPI_OBJECT_VALID &&
           type > 0;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool scope_type(const PLI_INT32 type) noexcept {
  switch (type) {
    case accModule:
    case accScope:
    case accTopModule:
    case accModuleInstance:
    case accCellInstance:
    case accTask:
    case accFunction:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool named_by_name_type(const PLI_INT32 type) noexcept {
  if (scope_type(type)) return true;
  switch (type) {
    case accNet:
    case accReg:
    case accParameter:
    case accSpecparam:
    case accPrimitive:
    case accNamedEvent:
    case accIntegerVar:
    case accRealVar:
    case accTimeVar:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool net_type(const PLI_INT32 type) noexcept {
  return type == accNet || (type >= accWire && type <= accSupply1) ||
         type == accNetBit;
}

[[nodiscard]] bool state_snapshot(ScopeState& result) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  try {
    std::scoped_lock lock{scope_mutex};
    const ScopeKey key{active->simulation_identity,
                       active->hierarchy_generation};
    const auto [found, inserted] = scope_states.try_emplace(
        key, ScopeState{active->calling_scope, active->interactive_scope, 0});
    static_cast<void>(inserted);
    result = found->second;
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool set_pli_scope(const std::uint64_t object) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  try {
    std::scoped_lock lock{scope_mutex};
    const ScopeKey key{active->simulation_identity,
                       active->hierarchy_generation};
    auto [found, inserted] = scope_states.try_emplace(
        key, ScopeState{active->calling_scope, active->interactive_scope, 0});
    static_cast<void>(inserted);
    found->second.pli_scope = object;
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool set_interactive_scope(
    const std::uint64_t object, const PLI_INT32 callback) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  try {
    std::scoped_lock lock{scope_mutex};
    const ScopeKey key{active->simulation_identity,
                       active->hierarchy_generation};
    auto [found, inserted] = scope_states.try_emplace(
        key, ScopeState{active->calling_scope, active->interactive_scope, 0});
    static_cast<void>(inserted);
    found->second.interactive_scope = object;
    found->second.interactive_callback = callback;
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] std::uint64_t lookup_vpi(const std::uint32_t mode,
                                       const std::uint64_t scope,
                                       const std::string& name,
                                       bool& found) noexcept {
  found = false;
  const auto* const active = context();
  if (active == nullptr) return 0;
  std::uint64_t object{};
  try {
    const auto status = active->lookup(
        active->user_data, mode, scope, name.data(),
        static_cast<std::uint32_t>(name.size()), &object);
    if (status == FSIM_ACC_VPI_OBJECT_NOT_FOUND) {
      found = true;
      return 0;
    }
    if (status != FSIM_ACC_VPI_OBJECT_VALID || object == 0) return 0;
    found = true;
    return object;
  } catch (...) {
    return 0;
  }
}

[[nodiscard]] handle lookup_handle(const PLI_BYTE8* const name,
                                   const std::uint32_t mode,
                                   const std::uint64_t scope,
                                   const bool restrict_type) noexcept {
  std::string copied;
  if (!copy_name(name, copied)) {
    publish_error(true);
    return nullptr;
  }
  bool valid_lookup{};
  const auto object = lookup_vpi(mode, scope, copied, valid_lookup);
  if (!valid_lookup) {
    publish_error(true);
    return nullptr;
  }
  if (object == 0) {
    publish_error(false);
    return nullptr;
  }
  PLI_INT32 type{};
  if (!resolve_type(object, type) ||
      (restrict_type && !named_by_name_type(type))) {
    publish_error(true);
    return nullptr;
  }
  return fsim_acc_handle_from_vpi_v3(object);
}

[[nodiscard]] handle related_handle(const handle object,
                                    const std::uint32_t relation,
                                    const bool require_net) noexcept {
  const auto input = fsim_acc_handle_to_vpi_v3(object);
  PLI_INT32 input_type{};
  if (input == 0 || !resolve_type(input, input_type) ||
      (require_net && !net_type(input_type))) {
    publish_error(true);
    return nullptr;
  }
  const auto* const active = context();
  std::uint64_t related{};
  try {
    const auto status = active->relation(active->user_data, relation, input,
                                         &related);
    if (status == FSIM_ACC_VPI_OBJECT_NOT_FOUND) {
      publish_error(false);
      return nullptr;
    }
    if (status != FSIM_ACC_VPI_OBJECT_VALID || related == 0) {
      publish_error(true);
      return nullptr;
    }
  } catch (...) {
    publish_error(true);
    return nullptr;
  }
  return fsim_acc_handle_from_vpi_v3(related);
}

[[nodiscard]] PLI_BYTE8* object_name(const std::uint64_t object) noexcept {
  const auto* const active = context();
  if (active == nullptr) return nullptr;
  name_buffer.fill(0);
  std::uint32_t size{};
  try {
    const auto status = active->name(
        active->user_data, object, name_buffer.data(),
        static_cast<std::uint32_t>(name_buffer.size()), &size);
    if (status != FSIM_ACC_VPI_OBJECT_VALID ||
        size == 0 || size > FSIM_ACC_NAME_MAXIMUM_BYTES ||
        name_buffer[size] != '\0') {
      return nullptr;
    }
    return name_buffer.data();
  } catch (...) {
    return nullptr;
  }
}

}  // namespace

extern "C" {

handle acc_handle_by_name(PLI_BYTE8* const name, const handle scope) {
  if (scope == nullptr) {
    return lookup_handle(name, FSIM_ACC_LOOKUP_ABSOLUTE, 0, true);
  }
  const auto scope_vpi = fsim_acc_handle_to_vpi_v3(scope);
  PLI_INT32 type{};
  if (scope_vpi == 0 || !resolve_type(scope_vpi, type) || !scope_type(type)) {
    publish_error(true);
    return nullptr;
  }
  return lookup_handle(name, FSIM_ACC_LOOKUP_RELATIVE, scope_vpi, true);
}

handle acc_handle_object(PLI_BYTE8* const name, ...) {
  ScopeState state;
  if (!state_snapshot(state)) {
    publish_error(true);
    return nullptr;
  }
  return lookup_handle(name, FSIM_ACC_LOOKUP_PLI_SCOPE, state.pli_scope,
                       false);
}

handle acc_handle_parent(const handle object) {
  return related_handle(object, FSIM_ACC_RELATION_PARENT, false);
}

handle acc_handle_scope(const handle object) {
  return related_handle(object, FSIM_ACC_RELATION_SCOPE, false);
}

handle acc_handle_simulated_net(const handle object) {
  return related_handle(object, FSIM_ACC_RELATION_SIMULATED_NET, true);
}

handle acc_handle_interactive_scope(void) {
  ScopeState state;
  if (!state_snapshot(state)) {
    publish_error(true);
    return nullptr;
  }
  return fsim_acc_handle_from_vpi_v3(state.interactive_scope);
}

PLI_INT32 acc_set_interactive_scope(const handle scope,
                                    const PLI_INT32 callback) {
  const auto object = fsim_acc_handle_to_vpi_v3(scope);
  PLI_INT32 type{};
  if ((callback != 0 && callback != 1) || object == 0 ||
      !resolve_type(object, type) || !scope_type(type) ||
      !set_interactive_scope(object, callback)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return 1;
}

PLI_BYTE8* acc_set_scope(const handle scope, ...) {
  const auto* const active = context();
  if (active == nullptr) {
    publish_error(true);
    return nullptr;
  }
  std::uint64_t object{};
  if (scope != nullptr) {
    object = fsim_acc_handle_to_vpi_v3(scope);
  } else if (fsim::runtime::acc_detail::configuration_enabled(
                 accEnableArgs, "acc_set_scope")) {
    std::va_list arguments;
    va_start(arguments, scope);
    auto* const name = va_arg(arguments, PLI_BYTE8*);
    va_end(arguments);
    std::string copied;
    bool valid_lookup{};
    if (!copy_name(name, copied)) {
      publish_error(true);
      return nullptr;
    }
    object = lookup_vpi(FSIM_ACC_LOOKUP_ABSOLUTE, 0, copied, valid_lookup);
    if (!valid_lookup || object == 0) {
      publish_error(true);
      return nullptr;
    }
  } else {
    object = active->default_scope;
  }
  PLI_INT32 type{};
  if (object == 0 || !resolve_type(object, type) || !scope_type(type)) {
    publish_error(true);
    return nullptr;
  }
  auto* const result = object_name(object);
  if (result == nullptr || !set_pli_scope(object)) {
    publish_error(true);
    return nullptr;
  }
  publish_error(false);
  return result;
}

}  // extern "C"
