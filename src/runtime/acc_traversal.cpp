// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using RawNextRoutine = handle (*)(void);

struct TraversalSession {
  std::uint64_t simulation_identity{};
  std::uint64_t hierarchy_generation{};
  std::uint32_t family{};
  std::uint64_t scope{};
  std::uint64_t cursor{};
};

enum class SessionResult {
  Missing,
  Found,
  Mismatch,
};

std::mutex traversal_mutex;
std::unordered_map<handle, TraversalSession> traversal_sessions;
std::unordered_map<handle*, std::unique_ptr<handle[]>> collection_arrays;

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] const fsim_acc_handle_context_v3* context() noexcept {
  return fsim::runtime::acc_detail::current_context();
}

template <typename Function>
[[nodiscard]] bool routine_matches(const RawNextRoutine candidate,
                                   const Function expected) noexcept {
  static_assert(sizeof(candidate) == sizeof(expected));
  return std::bit_cast<std::uintptr_t>(candidate) ==
         std::bit_cast<std::uintptr_t>(expected);
}

[[nodiscard]] std::optional<std::uint32_t> traversal_family(
    const RawNextRoutine routine) noexcept {
  if (routine == nullptr ||
      fsim::runtime::validate_tf_callback_pointer(routine) !=
          fsim::runtime::TfContainmentError::None) {
    return std::nullopt;
  }
  if (routine_matches(routine, &acc_next_cell)) {
    return FSIM_ACC_TRAVERSE_CELL;
  }
  if (routine_matches(routine, &acc_next_child)) {
    return FSIM_ACC_TRAVERSE_CHILD;
  }
  if (routine_matches(routine, &acc_next_net)) {
    return FSIM_ACC_TRAVERSE_NET;
  }
  if (routine_matches(routine, &acc_next_parameter)) {
    return FSIM_ACC_TRAVERSE_PARAMETER;
  }
  if (routine_matches(routine, &acc_next_port)) {
    return FSIM_ACC_TRAVERSE_PORT;
  }
  if (routine_matches(routine, &acc_next_primitive)) {
    return FSIM_ACC_TRAVERSE_PRIMITIVE;
  }
  if (routine_matches(routine, &acc_next_scope)) {
    return FSIM_ACC_TRAVERSE_SCOPE;
  }
  if (routine_matches(routine, &acc_next_specparam)) {
    return FSIM_ACC_TRAVERSE_SPECPARAM;
  }
  if (routine_matches(routine, &acc_next_topmod)) {
    return FSIM_ACC_TRAVERSE_TOP_MODULE;
  }
  return std::nullopt;
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
  return type == accModule || type == accScope || type == accTopModule ||
         type == accModuleInstance || type == accCellInstance ||
         type == accTask || type == accFunction;
}

[[nodiscard]] bool family_type(const std::uint32_t family,
                               const PLI_INT32 type) noexcept {
  switch (family) {
    case FSIM_ACC_TRAVERSE_CELL:
      return type == accModule || type == accModuleInstance ||
             type == accCellInstance;
    case FSIM_ACC_TRAVERSE_CHILD:
      return true;
    case FSIM_ACC_TRAVERSE_NET:
      return type == accNet || type == accNetBit ||
             (type >= accWire && type <= accSupply1);
    case FSIM_ACC_TRAVERSE_PARAMETER:
      return type == accParameter || type == accIntegerParam ||
             type == accRealParam || type == accStringParam;
    case FSIM_ACC_TRAVERSE_PORT:
      return type == accPort || type == accPortBit ||
             (type >= accScalarPort && type <= accConcatPort);
    case FSIM_ACC_TRAVERSE_PRIMITIVE:
      return type == accPrimitive || type == accCombPrim ||
             type == accSeqPrim ||
             (type >= accAndGate && type <= accPulldownGate);
    case FSIM_ACC_TRAVERSE_SCOPE:
    case FSIM_ACC_TRAVERSE_TOP_MODULE:
      return scope_type(type);
    case FSIM_ACC_TRAVERSE_SPECPARAM:
      return type == accSpecparam;
    default:
      return false;
  }
}

[[nodiscard]] bool traversal_call(const std::uint32_t operation,
                                  const std::uint32_t family,
                                  const std::uint64_t scope,
                                  std::uint64_t& cursor,
                                  std::uint64_t& object,
                                  std::uint32_t& status) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  try {
    status = active->traverse(active->user_data, operation, family, scope,
                              &cursor, &object);
    return true;
  } catch (...) {
    return false;
  }
}

void end_cursor(const TraversalSession& session) noexcept {
  std::uint64_t ignored{};
  auto cursor = session.cursor;
  std::uint32_t status{};
  static_cast<void>(traversal_call(
      FSIM_ACC_TRAVERSE_END, session.family, session.scope, cursor, ignored,
      status));
}

[[nodiscard]] bool begin_session(const std::uint32_t family,
                                 const std::uint64_t scope,
                                 TraversalSession& session) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  session = {active->simulation_identity, active->hierarchy_generation,
             family, scope, 0};
  std::uint64_t ignored{};
  std::uint32_t status{};
  const auto called = traversal_call(FSIM_ACC_TRAVERSE_BEGIN, family, scope,
                                     session.cursor, ignored, status);
  const auto valid = called && status == FSIM_ACC_VPI_OBJECT_VALID &&
                     session.cursor != 0;
  if (!valid && called && session.cursor != 0) end_cursor(session);
  return valid;
}

[[nodiscard]] SessionResult take_session(
    const handle previous, const std::uint32_t family,
    const std::uint64_t scope, TraversalSession& result) noexcept {
  const auto* const active = context();
  if (active == nullptr) return SessionResult::Missing;
  std::scoped_lock lock{traversal_mutex};
  const auto found = traversal_sessions.find(previous);
  if (found == traversal_sessions.end()) return SessionResult::Missing;
  result = found->second;
  const auto matches =
      result.simulation_identity == active->simulation_identity &&
      result.hierarchy_generation == active->hierarchy_generation &&
      result.family == family && result.scope == scope;
  traversal_sessions.erase(found);
  return matches ? SessionResult::Found : SessionResult::Mismatch;
}

[[nodiscard]] bool store_session(const handle current,
                                 const TraversalSession& session) noexcept {
  try {
    std::scoped_lock lock{traversal_mutex};
    return traversal_sessions.emplace(current, session).second;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool advance(TraversalSession& session,
                           std::uint64_t& object,
                           bool& at_end) noexcept {
  object = 0;
  at_end = false;
  std::uint32_t status{};
  if (!traversal_call(FSIM_ACC_TRAVERSE_NEXT, session.family, session.scope,
                      session.cursor, object, status)) {
    return false;
  }
  if (status == FSIM_ACC_VPI_OBJECT_NOT_FOUND) {
    at_end = true;
    return true;
  }
  PLI_INT32 type{};
  return status == FSIM_ACC_VPI_OBJECT_VALID && object != 0 &&
         resolve_type(object, type) && family_type(session.family, type);
}

[[nodiscard]] handle next_object(const std::uint32_t family,
                                 const handle scope,
                                 const handle previous) noexcept {
  const bool top_modules = family == FSIM_ACC_TRAVERSE_TOP_MODULE;
  std::uint64_t scope_vpi{};
  if (!top_modules) {
    scope_vpi = fsim_acc_handle_to_vpi_v3(scope);
    PLI_INT32 type{};
    if (scope_vpi == 0 || !resolve_type(scope_vpi, type) ||
        !scope_type(type)) {
      publish_error(true);
      return nullptr;
    }
  }

  std::uint64_t previous_vpi{};
  if (previous != nullptr) {
    previous_vpi = fsim_acc_handle_to_vpi_v3(previous);
    if (previous_vpi == 0) {
      publish_error(true);
      return nullptr;
    }
  }

  TraversalSession session;
  const auto session_result =
      take_session(previous, family, scope_vpi, session);
  if (session_result == SessionResult::Mismatch) {
    const auto* const active = context();
    if (active != nullptr &&
        session.simulation_identity == active->simulation_identity &&
        session.hierarchy_generation == active->hierarchy_generation) {
      end_cursor(session);
    }
    publish_error(true);
    return nullptr;
  }
  const bool recovered = session_result == SessionResult::Found;
  if (!recovered && !begin_session(family, scope_vpi, session)) {
    publish_error(true);
    return nullptr;
  }
  if (!recovered && previous_vpi != 0) {
    bool matched{};
    while (!matched) {
      std::uint64_t candidate{};
      bool at_end{};
      if (!advance(session, candidate, at_end) || at_end) {
        end_cursor(session);
        publish_error(!at_end);
        return nullptr;
      }
      matched = candidate == previous_vpi;
    }
  }

  std::uint64_t object{};
  bool at_end{};
  if (!advance(session, object, at_end)) {
    end_cursor(session);
    publish_error(true);
    return nullptr;
  }
  if (at_end) {
    end_cursor(session);
    publish_error(false);
    return nullptr;
  }
  auto result = fsim_acc_handle_from_vpi_v3(object);
  if (result == nullptr || !store_session(result, session)) {
    end_cursor(session);
    publish_error(true);
    return nullptr;
  }
  publish_error(false);
  return result;
}

[[nodiscard]] handle dispatch_next(const std::uint32_t family,
                                   const handle scope,
                                   const handle previous) noexcept {
  return next_object(family, scope, previous);
}

}  // namespace

extern "C" {

handle acc_next_cell(const handle scope, const handle cell) {
  return next_object(FSIM_ACC_TRAVERSE_CELL, scope, cell);
}

handle acc_next_child(const handle scope, const handle child) {
  return next_object(FSIM_ACC_TRAVERSE_CHILD, scope, child);
}

handle acc_next_net(const handle scope, const handle net) {
  return next_object(FSIM_ACC_TRAVERSE_NET, scope, net);
}

handle acc_next_parameter(const handle scope, const handle parameter) {
  return next_object(FSIM_ACC_TRAVERSE_PARAMETER, scope, parameter);
}

handle acc_next_port(const handle scope, const handle port) {
  return next_object(FSIM_ACC_TRAVERSE_PORT, scope, port);
}

handle acc_next_primitive(const handle scope, const handle primitive) {
  return next_object(FSIM_ACC_TRAVERSE_PRIMITIVE, scope, primitive);
}

handle acc_next_scope(const handle scope, const handle child) {
  return next_object(FSIM_ACC_TRAVERSE_SCOPE, scope, child);
}

handle acc_next_specparam(const handle scope, const handle parameter) {
  return next_object(FSIM_ACC_TRAVERSE_SPECPARAM, scope, parameter);
}

handle acc_next_topmod(const handle module) {
  return next_object(FSIM_ACC_TRAVERSE_TOP_MODULE, nullptr, module);
}

handle* acc_collect(const RawNextRoutine routine, const handle scope,
                    PLI_INT32* const count) {
  if (count == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          count, sizeof(*count), fsim::runtime::TfNativePointerAccess::Write) !=
          fsim::runtime::TfContainmentError::None) {
    publish_error(true);
    return nullptr;
  }
  *count = 0;
  const auto family = traversal_family(routine);
  if (!family) {
    publish_error(true);
    return nullptr;
  }
  std::vector<handle> objects;
  handle previous{};
  try {
    while (objects.size() < FSIM_ACC_COLLECTION_MAXIMUM_OBJECTS) {
      auto object = dispatch_next(*family, scope, previous);
      if (object == nullptr) {
        if (acc_error_flag != 0) return nullptr;
        break;
      }
      objects.push_back(object);
      previous = object;
    }
    if (objects.size() == FSIM_ACC_COLLECTION_MAXIMUM_OBJECTS) {
      auto extra = dispatch_next(*family, scope, previous);
      if (extra != nullptr || acc_error_flag != 0) {
        publish_error(true);
        return nullptr;
      }
    }
    if (objects.empty()) {
      publish_error(false);
      return nullptr;
    }
    auto owned = std::make_unique<handle[]>(objects.size() + 1U);
    for (std::size_t index = 0; index < objects.size(); ++index) {
      owned[index] = objects[index];
    }
    owned[objects.size()] = nullptr;
    auto* const result = owned.get();
    {
      std::scoped_lock lock{traversal_mutex};
      if (!collection_arrays.emplace(result, std::move(owned)).second) {
        publish_error(true);
        return nullptr;
      }
    }
    *count = static_cast<PLI_INT32>(objects.size());
    publish_error(false);
    return result;
  } catch (...) {
    publish_error(true);
    return nullptr;
  }
}

PLI_INT32 acc_count(const RawNextRoutine routine, const handle scope) {
  const auto family = traversal_family(routine);
  if (!family) {
    publish_error(true);
    return 0;
  }
  PLI_INT32 count{};
  handle previous{};
  while (count < std::numeric_limits<PLI_INT32>::max()) {
    auto object = dispatch_next(*family, scope, previous);
    if (object == nullptr) return acc_error_flag == 0 ? count : 0;
    ++count;
    previous = object;
  }
  auto extra = dispatch_next(*family, scope, previous);
  publish_error(extra != nullptr || acc_error_flag != 0);
  return acc_error_flag == 0 ? count : 0;
}

void acc_free(handle* const objects) {
  if (objects == nullptr) {
    publish_error(false);
    return;
  }
  std::scoped_lock lock{traversal_mutex};
  const auto found = collection_arrays.find(objects);
  if (found == collection_arrays.end()) {
    publish_error(true);
    return;
  }
  collection_arrays.erase(found);
  publish_error(false);
}

}  // extern "C"
