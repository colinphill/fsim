// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

struct AccHandleRecord {
  std::uint64_t simulation_identity{};
  std::uint64_t hierarchy_generation{};
  std::uint64_t vpi_handle{};
  PLI_INT32 acc_type{};
  bool live{};
};

using AccIdentity =
    std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;

std::mutex handle_mutex;
std::vector<std::unique_ptr<AccHandleRecord>> handle_records;
std::unordered_map<handle, AccHandleRecord*> records_by_handle;
std::map<AccIdentity, handle> handles_by_identity;
std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint32_t> live_counts;
thread_local fsim_acc_handle_context_v3* active_context{};

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] bool valid_context(
    const fsim_acc_handle_context_v3* const context) noexcept {
  return context != nullptr &&
         fsim::runtime::validate_tf_native_pointer(
             context, sizeof(*context),
             fsim::runtime::TfNativePointerAccess::Read) ==
             fsim::runtime::TfContainmentError::None &&
         context->abi_version == FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION &&
         context->struct_size >= sizeof(*context) &&
         context->simulation_identity != 0 &&
         context->hierarchy_generation != 0 &&
         context->maximum_handles != 0 &&
         context->maximum_handles <= FSIM_ACC_HANDLE_MAX_OBJECTS &&
         context->reserved == 0 && context->user_data != nullptr &&
         context->resolve != nullptr && context->calling_scope != 0 &&
         context->default_scope != 0 && context->interactive_scope != 0 &&
         context->lookup != nullptr && context->relation != nullptr &&
         context->name != nullptr && context->traverse != nullptr &&
         context->object_query != nullptr &&
         context->read != nullptr && context->write != nullptr &&
         context->iterate != nullptr && context->timing != nullptr &&
         context->vcl != nullptr &&
         fsim::runtime::validate_tf_callback_pointer(context->resolve) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->lookup) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->relation) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->name) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->traverse) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->object_query) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->read) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->write) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->iterate) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->timing) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::validate_tf_callback_pointer(context->vcl) ==
             fsim::runtime::TfContainmentError::None &&
         fsim::runtime::acc_detail::valid_tf_context_binding(context);
}

[[nodiscard]] bool resolve_vpi(const std::uint64_t vpi_handle,
                               PLI_INT32& acc_type) noexcept {
  if (!valid_context(active_context) || vpi_handle == 0) return false;
  try {
    return active_context->resolve(
               active_context->user_data, vpi_handle, &acc_type) ==
           FSIM_ACC_VPI_OBJECT_VALID;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool valid_context_scope(
    const fsim_acc_handle_context_v3* const context,
    const std::uint64_t scope) noexcept {
  PLI_INT32 type{};
  try {
    if (context->resolve(context->user_data, scope, &type) !=
        FSIM_ACC_VPI_OBJECT_VALID) {
      return false;
    }
    return type == accModule || type == accScope || type == accTopModule ||
           type == accModuleInstance || type == accCellInstance ||
           type == accTask || type == accFunction;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool resolve_acc(const handle object,
                               AccHandleRecord& snapshot) noexcept {
  if (!valid_context(active_context) || object == nullptr) return false;
  {
    std::scoped_lock lock{handle_mutex};
    const auto found = records_by_handle.find(object);
    if (found == records_by_handle.end() || !found->second->live ||
        found->second->simulation_identity !=
            active_context->simulation_identity ||
        found->second->hierarchy_generation !=
            active_context->hierarchy_generation) {
      return false;
    }
    snapshot = *found->second;
  }
  PLI_INT32 current_type{};
  return resolve_vpi(snapshot.vpi_handle, current_type) &&
         current_type == snapshot.acc_type;
}

}  // namespace

namespace fsim::runtime::acc_detail {

const fsim_acc_handle_context_v3* current_context() noexcept {
  return active_context;
}

const fsim_acc_tf_context_v3* current_tf_context() noexcept {
  return active_context == nullptr ? nullptr : active_context->tf;
}

bool type_matches(const PLI_INT32 actual,
                  const PLI_INT32 requested) noexcept {
  if (actual == requested) return true;
  switch (requested) {
    case accModule:
      return actual == accTopModule || actual == accModuleInstance ||
             actual == accCellInstance;
    case accScope:
      return actual == accModule || actual == accTopModule ||
             actual == accModuleInstance || actual == accCellInstance ||
             actual == accTask || actual == accFunction;
    case accNet:
      return actual == accNetBit ||
             (actual >= accWire && actual <= accSupply1);
    case accReg: return actual == accRegBit;
    case accPort:
      return actual == accPortBit ||
             (actual >= accScalarPort && actual <= accConcatPort);
    case accTerminal:
      return actual >= accInputTerminal && actual <= accInoutTerminal;
    case accCombPrim:
      return actual >= accAndGate && actual <= accPulldownGate;
    case accPrimitive:
      return actual == accCombPrim || actual == accSeqPrim ||
             (actual >= accAndGate && actual <= accPulldownGate);
    case accParameter:
      return actual == accIntegerParam || actual == accRealParam ||
             actual == accStringParam;
    case accTchk:
      return actual == accSetup || actual == accHold || actual == accWidth ||
             actual == accPeriod || actual == accRecovery ||
             actual == accSkew || actual == accNochange ||
             actual == accSetuphold;
    default: return false;
  }
}

}  // namespace fsim::runtime::acc_detail

extern "C" {

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL fsim_acc_handle_context_enter_v3(
    fsim_acc_handle_context_v3* const context) {
  if (active_context != nullptr || !valid_context(context)) {
    publish_error(true);
    return 0;
  }
  if (!valid_context_scope(context, context->calling_scope) ||
      !valid_context_scope(context, context->default_scope) ||
      !valid_context_scope(context, context->interactive_scope)) {
    publish_error(true);
    return 0;
  }
  active_context = context;
  publish_error(false);
  return 1;
}

void FSIM_NATIVE_PLUGIN_CALL fsim_acc_handle_context_leave_v3(
    fsim_acc_handle_context_v3* const context) {
  if (active_context == context) active_context = nullptr;
}

handle FSIM_NATIVE_PLUGIN_CALL fsim_acc_handle_from_vpi_v3(
    const std::uint64_t vpi_handle) {
  PLI_INT32 acc_type{};
  if (!resolve_vpi(vpi_handle, acc_type) || acc_type <= 0) {
    publish_error(true);
    return nullptr;
  }
  const AccIdentity identity{active_context->simulation_identity,
                             active_context->hierarchy_generation,
                             vpi_handle};
  std::scoped_lock lock{handle_mutex};
  if (const auto existing = handles_by_identity.find(identity);
      existing != handles_by_identity.end()) {
    publish_error(false);
    return existing->second;
  }
  const auto count_key = std::pair{active_context->simulation_identity,
                                   active_context->hierarchy_generation};
  const auto count_found = live_counts.find(count_key);
  const auto live_count = count_found == live_counts.end()
                              ? UINT32_C(0)
                              : count_found->second;
  if (live_count >= active_context->maximum_handles ||
      handle_records.size() >= FSIM_ACC_HANDLE_MAX_OBJECTS) {
    publish_error(true);
    return nullptr;
  }
  handle result{};
  bool count_inserted{};
  bool record_inserted{};
  bool identity_inserted{};
  try {
    auto [count, inserted_count] = live_counts.try_emplace(count_key, 0);
    count_inserted = inserted_count;
    auto record = std::make_unique<AccHandleRecord>(AccHandleRecord{
        active_context->simulation_identity,
        active_context->hierarchy_generation,
        vpi_handle,
        acc_type,
        true,
    });
    auto* const record_pointer = record.get();
    result = reinterpret_cast<handle>(record_pointer);
    record_inserted = records_by_handle.emplace(result, record_pointer).second;
    if (!record_inserted) throw std::bad_alloc{};
    identity_inserted = handles_by_identity.emplace(identity, result).second;
    if (!identity_inserted) throw std::bad_alloc{};
    handle_records.push_back(std::move(record));
    ++count->second;
    publish_error(false);
    return result;
  } catch (...) {
    if (identity_inserted) handles_by_identity.erase(identity);
    if (record_inserted) records_by_handle.erase(result);
    if (count_inserted) {
      const auto count = live_counts.find(count_key);
      if (count != live_counts.end() && count->second == 0) {
        live_counts.erase(count);
      }
    }
    publish_error(true);
    return nullptr;
  }
}

std::uint64_t FSIM_NATIVE_PLUGIN_CALL fsim_acc_handle_to_vpi_v3(
    const handle object) {
  AccHandleRecord record;
  if (!resolve_acc(object, record)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return record.vpi_handle;
}

PLI_INT32 acc_compare_handles(const handle lhs, const handle rhs) {
  AccHandleRecord left;
  AccHandleRecord right;
  if (!resolve_acc(lhs, left) || !resolve_acc(rhs, right)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return left.vpi_handle == right.vpi_handle ? 1 : 0;
}

PLI_INT32 acc_object_of_type(const handle object, const PLI_INT32 type) {
  AccHandleRecord record;
  if (!resolve_acc(object, record)) {
    publish_error(true);
    return 0;
  }
  publish_error(false);
  return fsim::runtime::acc_detail::type_matches(record.acc_type, type) ? 1
                                                                       : 0;
}

PLI_INT32 acc_object_in_typelist(const handle object, PLI_INT32* const types) {
  AccHandleRecord record;
  if (!resolve_acc(object, record) || types == nullptr) {
    publish_error(true);
    return 0;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(types);
  for (std::uint32_t index = 0; index < 256; ++index) {
    if (index >
        (std::numeric_limits<std::uintptr_t>::max() - base) /
            sizeof(*types)) {
      publish_error(true);
      return 0;
    }
    const auto address = base + static_cast<std::uintptr_t>(index) *
                                    sizeof(*types);
    const auto* const entry = reinterpret_cast<const PLI_INT32*>(address);
    if (fsim::runtime::validate_tf_native_pointer(
            entry, sizeof(*entry),
            fsim::runtime::TfNativePointerAccess::Read) !=
        fsim::runtime::TfContainmentError::None) {
      publish_error(true);
      return 0;
    }
    if (*entry == 0) {
      publish_error(false);
      return 0;
    }
    if (fsim::runtime::acc_detail::type_matches(record.acc_type, *entry)) {
      publish_error(false);
      return 1;
    }
  }
  publish_error(true);
  return 0;
}

PLI_INT32 acc_release_object(const handle object) {
  AccHandleRecord snapshot;
  if (!resolve_acc(object, snapshot)) {
    publish_error(true);
    return 0;
  }
  std::scoped_lock lock{handle_mutex};
  const auto found = records_by_handle.find(object);
  if (found == records_by_handle.end() || !found->second->live) {
    publish_error(true);
    return 0;
  }
  const auto count_key =
      std::pair{snapshot.simulation_identity, snapshot.hierarchy_generation};
  const auto count = live_counts.find(count_key);
  if (count == live_counts.end() || count->second == 0) {
    publish_error(true);
    return 0;
  }
  found->second->live = false;
  handles_by_identity.erase(AccIdentity{snapshot.simulation_identity,
                                        snapshot.hierarchy_generation,
                                        snapshot.vpi_handle});
  --count->second;
  publish_error(false);
  return 1;
}

}  // extern "C"
