// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <compare>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace {

struct IteratorSession {
  std::uint64_t simulation_identity{};
  std::uint64_t hierarchy_generation{};
  std::uint32_t family{};
  std::uint64_t owner{};
  std::vector<PLI_INT32> types;
  std::uint64_t cursor{};
};

struct SessionKey {
  std::uint64_t simulation_identity{};
  std::uint64_t hierarchy_generation{};
  std::uintptr_t previous{};
  std::uint32_t family{};
  std::uint64_t owner{};
  std::vector<PLI_INT32> types;

  auto operator<=>(const SessionKey&) const = default;
};

enum class SessionResult { Missing, Found, Invalidated };

std::mutex iterator_mutex;
std::map<SessionKey, IteratorSession> iterator_sessions;
std::set<SessionKey> invalidated_iterator_sessions;

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] const fsim_acc_handle_context_v3* context() noexcept {
  return fsim::runtime::acc_detail::current_context();
}

[[nodiscard]] bool checked_type(const PLI_INT32* const pointer) noexcept {
  return pointer != nullptr &&
         fsim::runtime::validate_tf_native_pointer(
             pointer, sizeof(*pointer),
             fsim::runtime::TfNativePointerAccess::Read) ==
             fsim::runtime::TfContainmentError::None;
}

[[nodiscard]] std::optional<std::vector<PLI_INT32>> copy_types(
    const PLI_INT32* const types) noexcept {
  if (types == nullptr) return std::nullopt;
  std::vector<PLI_INT32> result;
  std::set<PLI_INT32> unique;
  const auto base = reinterpret_cast<std::uintptr_t>(types);
  try {
    for (std::uint32_t index = 0; index < FSIM_ACC_ITERATOR_MAXIMUM_TYPES;
         ++index) {
      if (index > (std::numeric_limits<std::uintptr_t>::max() - base) /
                      sizeof(*types)) {
        return std::nullopt;
      }
      const auto* const entry = reinterpret_cast<const PLI_INT32*>(
          base + static_cast<std::uintptr_t>(index) * sizeof(*types));
      if (!checked_type(entry)) return std::nullopt;
      if (*entry == 0) return result.empty() ? std::nullopt
                                              : std::optional{result};
      if (*entry < 0 || acc_fetch_type_str(*entry) == nullptr ||
          !unique.emplace(*entry).second) {
        return std::nullopt;
      }
      result.push_back(*entry);
    }
  } catch (...) {
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

[[nodiscard]] bool module_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accModule) ||
         fsim::runtime::acc_detail::type_matches(type, accScope);
}

[[nodiscard]] bool net_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accNet);
}

[[nodiscard]] bool reg_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accReg) ||
         type == accIntegerVar || type == accRealVar || type == accTimeVar;
}

[[nodiscard]] bool port_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accPort);
}

[[nodiscard]] bool primitive_type(const PLI_INT32 type) noexcept {
  return fsim::runtime::acc_detail::type_matches(type, accPrimitive);
}

[[nodiscard]] bool path_type(const PLI_INT32 type) noexcept {
  return type == accModPath || type == accInterModPath || type == accDataPath;
}

[[nodiscard]] bool owner_type(const std::uint32_t family,
                              const PLI_INT32 type) noexcept {
  switch (family) {
    case FSIM_ACC_ITERATOR_GENERIC: return module_type(type);
    case FSIM_ACC_ITERATOR_BIT:
      return net_type(type) || reg_type(type) || port_type(type) ||
             type == accScalar || type == accVector ||
             type == accExpandedVector || type == accUnExpandedVector;
    case FSIM_ACC_ITERATOR_CELL_LOAD: return net_type(type);
    case FSIM_ACC_ITERATOR_DRIVER:
    case FSIM_ACC_ITERATOR_LOAD: return net_type(type) || reg_type(type);
    case FSIM_ACC_ITERATOR_HICONN:
    case FSIM_ACC_ITERATOR_LOCONN: return port_type(type);
    case FSIM_ACC_ITERATOR_INPUT:
    case FSIM_ACC_ITERATOR_OUTPUT: return path_type(type);
    case FSIM_ACC_ITERATOR_MODPATH:
    case FSIM_ACC_ITERATOR_PORTOUT:
    case FSIM_ACC_ITERATOR_TCHK: return module_type(type);
    case FSIM_ACC_ITERATOR_TERMINAL: return primitive_type(type);
    default: return false;
  }
}

[[nodiscard]] bool generic_result_type(
    const PLI_INT32 type, const std::vector<PLI_INT32>& types) noexcept {
  for (const auto requested : types) {
    if (fsim::runtime::acc_detail::type_matches(type, requested)) return true;
  }
  return false;
}

[[nodiscard]] bool relation_result_type(
    const std::uint32_t family, const PLI_INT32 type,
    const std::vector<PLI_INT32>& types) noexcept {
  switch (family) {
    case FSIM_ACC_ITERATOR_GENERIC: return generic_result_type(type, types);
    case FSIM_ACC_ITERATOR_BIT:
      return type == accBit || type == accPortBit || type == accNetBit ||
             type == accRegBit || type == accBitSelect;
    case FSIM_ACC_ITERATOR_CELL_LOAD:
    case FSIM_ACC_ITERATOR_DRIVER:
    case FSIM_ACC_ITERATOR_LOAD:
      return port_type(type) ||
             fsim::runtime::acc_detail::type_matches(type, accTerminal) ||
             primitive_type(type) || net_type(type) || reg_type(type);
    case FSIM_ACC_ITERATOR_HICONN:
    case FSIM_ACC_ITERATOR_LOCONN:
      return net_type(type) || reg_type(type) || port_type(type);
    case FSIM_ACC_ITERATOR_INPUT:
      return type == accInput || type == accPathInput ||
             type == accPathTerminal || type == accInputTerminal;
    case FSIM_ACC_ITERATOR_OUTPUT:
      return type == accOutput || type == accPathOutput ||
             type == accPathTerminal || type == accOutputTerminal;
    case FSIM_ACC_ITERATOR_MODPATH:
      return type == accModPath || type == accInterModPath;
    case FSIM_ACC_ITERATOR_PORTOUT: return port_type(type);
    case FSIM_ACC_ITERATOR_TCHK:
      return fsim::runtime::acc_detail::type_matches(type, accTchk);
    case FSIM_ACC_ITERATOR_TERMINAL:
      return fsim::runtime::acc_detail::type_matches(type, accTerminal);
    default: return false;
  }
}

[[nodiscard]] bool iterator_call(const IteratorSession& session,
                                 const std::uint32_t operation,
                                 fsim_acc_iterator_result_v3& result,
                                 std::uint32_t& status) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  fsim_acc_iterator_query_v3 query{};
  query.abi_version = FSIM_ACC_ITERATOR_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = operation;
  query.family = session.family;
  query.owner = session.owner;
  query.cursor = session.cursor;
  query.types = session.types.empty() ? nullptr : session.types.data();
  query.type_count = static_cast<std::uint32_t>(session.types.size());
  result = {};
  result.abi_version = FSIM_ACC_ITERATOR_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  try {
    status = active->iterate(active->user_data, &query, &result);
  } catch (...) {
    return false;
  }
  return result.abi_version == FSIM_ACC_ITERATOR_QUERY_ABI_VERSION &&
         result.struct_size >= sizeof(result) && result.reserved == 0 &&
         result.reserved2 == 0;
}

void end_cursor(const IteratorSession& session) noexcept {
  fsim_acc_iterator_result_v3 ignored{};
  std::uint32_t status{};
  static_cast<void>(iterator_call(session, FSIM_ACC_ITERATOR_END, ignored,
                                  status));
}

[[nodiscard]] bool begin_session(const std::uint32_t family,
                                 const std::uint64_t owner,
                                 std::vector<PLI_INT32> types,
                                 IteratorSession& session) noexcept {
  const auto* const active = context();
  if (active == nullptr) return false;
  session = {active->simulation_identity, active->hierarchy_generation,
             family, owner, std::move(types), 0};
  fsim_acc_iterator_result_v3 result{};
  std::uint32_t status{};
  const auto called =
      iterator_call(session, FSIM_ACC_ITERATOR_BEGIN, result, status);
  if (!called || status != FSIM_ACC_VPI_OBJECT_VALID || result.cursor == 0) {
    if (called && result.cursor != 0) {
      session.cursor = result.cursor;
      end_cursor(session);
    }
    return false;
  }
  session.cursor = result.cursor;
  return true;
}

[[nodiscard]] SessionKey session_key(
    const handle previous, const std::uint32_t family,
    const std::uint64_t owner, const std::vector<PLI_INT32>& types) noexcept {
  const auto* const active = context();
  return {active == nullptr ? 0 : active->simulation_identity,
          active == nullptr ? 0 : active->hierarchy_generation,
          reinterpret_cast<std::uintptr_t>(previous), family, owner, types};
}

[[nodiscard]] SessionResult take_session(
    const handle previous, const std::uint32_t family,
    const std::uint64_t owner, const std::vector<PLI_INT32>& types,
    IteratorSession& result) noexcept {
  std::scoped_lock lock{iterator_mutex};
  const auto key = session_key(previous, family, owner, types);
  if (invalidated_iterator_sessions.contains(key)) {
    return SessionResult::Invalidated;
  }
  const auto found = iterator_sessions.find(key);
  if (found == iterator_sessions.end()) return SessionResult::Missing;
  result = std::move(found->second);
  iterator_sessions.erase(found);
  return SessionResult::Found;
}

[[nodiscard]] bool store_session(const handle current,
                                 const IteratorSession& session) noexcept {
  try {
    std::scoped_lock lock{iterator_mutex};
    const auto key =
        session_key(current, session.family, session.owner, session.types);
    invalidated_iterator_sessions.erase(key);
    return iterator_sessions.emplace(key, session).second;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool advance(IteratorSession& session, std::uint64_t& object,
                           bool& at_end) noexcept {
  fsim_acc_iterator_result_v3 result{};
  std::uint32_t status{};
  if (!iterator_call(session, FSIM_ACC_ITERATOR_NEXT, result, status)) {
    return false;
  }
  if (status == FSIM_ACC_VPI_OBJECT_NOT_FOUND) {
    object = 0;
    at_end = true;
    return true;
  }
  if (status != FSIM_ACC_VPI_OBJECT_VALID || result.object == 0 ||
      result.cursor != session.cursor) {
    return false;
  }
  PLI_INT32 type{};
  if (!resolve_type(result.object, type) ||
      !relation_result_type(session.family, type, session.types)) {
    return false;
  }
  object = result.object;
  at_end = false;
  return true;
}

[[nodiscard]] handle next_relation(const std::uint32_t family,
                                   const handle owner,
                                   const handle previous,
                                   std::vector<PLI_INT32> types) noexcept {
  const auto owner_vpi = fsim_acc_handle_to_vpi_v3(owner);
  PLI_INT32 owner_kind{};
  if (owner_vpi == 0 || !resolve_type(owner_vpi, owner_kind) ||
      !owner_type(family, owner_kind)) {
    publish_error(true);
    return nullptr;
  }
  std::uint64_t previous_vpi{};
  if (previous != nullptr) {
    previous_vpi = fsim_acc_handle_to_vpi_v3(previous);
    if (previous_vpi == 0) {
      publish_error(true);
      return nullptr;
    }
  }

  IteratorSession session;
  const auto session_result =
      take_session(previous, family, owner_vpi, types, session);
  if (session_result == SessionResult::Invalidated) {
    publish_error(true);
    return nullptr;
  }
  const auto recovered = session_result == SessionResult::Found;
  if (!recovered &&
      !begin_session(family, owner_vpi, std::move(types), session)) {
    publish_error(true);
    return nullptr;
  }
  if (!recovered && previous_vpi != 0) {
    for (;;) {
      std::uint64_t candidate{};
      bool at_end{};
      if (!advance(session, candidate, at_end) || at_end) {
        end_cursor(session);
        publish_error(true);
        return nullptr;
      }
      if (candidate == previous_vpi) break;
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

}  // namespace

namespace fsim::runtime::acc_detail {

bool invalidate_iterator_safe_point(
    const fsim_acc_handle_context_v3* const active) noexcept {
  if (active == nullptr || active != current_context()) return false;
  std::vector<IteratorSession> expired;
  try {
    std::scoped_lock lock{iterator_mutex};
    std::size_t matching{};
    for (const auto& [key, unused] : iterator_sessions) {
      static_cast<void>(unused);
      if (key.simulation_identity == active->simulation_identity &&
          key.hierarchy_generation == active->hierarchy_generation) {
        ++matching;
      }
    }
    if (matching > FSIM_ACC_HANDLE_MAX_OBJECTS -
                       invalidated_iterator_sessions.size()) {
      return false;
    }
    for (auto iterator = iterator_sessions.begin();
         iterator != iterator_sessions.end();) {
      if (iterator->first.simulation_identity == active->simulation_identity &&
          iterator->first.hierarchy_generation ==
              active->hierarchy_generation) {
        invalidated_iterator_sessions.insert(iterator->first);
        expired.push_back(std::move(iterator->second));
        iterator = iterator_sessions.erase(iterator);
      } else {
        ++iterator;
      }
    }
  } catch (...) {
    return false;
  }
  for (const auto& session : expired) end_cursor(session);
  return true;
}

}  // namespace fsim::runtime::acc_detail

extern "C" {

handle acc_next(PLI_INT32* const types, const handle scope,
                const handle object) {
  auto copied = copy_types(types);
  if (!copied) {
    publish_error(true);
    return nullptr;
  }
  return next_relation(FSIM_ACC_ITERATOR_GENERIC, scope, object,
                       std::move(*copied));
}

handle acc_next_bit(const handle vector, const handle bit) {
  return next_relation(FSIM_ACC_ITERATOR_BIT, vector, bit, {});
}

handle acc_next_cell_load(const handle net, const handle load) {
  return next_relation(FSIM_ACC_ITERATOR_CELL_LOAD, net, load, {});
}

handle acc_next_driver(const handle net, const handle driver) {
  return next_relation(FSIM_ACC_ITERATOR_DRIVER, net, driver, {});
}

handle acc_next_hiconn(const handle port, const handle connection) {
  return next_relation(FSIM_ACC_ITERATOR_HICONN, port, connection, {});
}

handle acc_next_input(const handle path, const handle input) {
  return next_relation(FSIM_ACC_ITERATOR_INPUT, path, input, {});
}

handle acc_next_load(const handle net, const handle load) {
  return next_relation(FSIM_ACC_ITERATOR_LOAD, net, load, {});
}

handle acc_next_loconn(const handle port, const handle connection) {
  return next_relation(FSIM_ACC_ITERATOR_LOCONN, port, connection, {});
}

handle acc_next_modpath(const handle module, const handle path) {
  return next_relation(FSIM_ACC_ITERATOR_MODPATH, module, path, {});
}

handle acc_next_output(const handle path, const handle output) {
  return next_relation(FSIM_ACC_ITERATOR_OUTPUT, path, output, {});
}

handle acc_next_portout(const handle module, const handle port) {
  return next_relation(FSIM_ACC_ITERATOR_PORTOUT, module, port, {});
}

handle acc_next_tchk(const handle module, const handle timing_check) {
  return next_relation(FSIM_ACC_ITERATOR_TCHK, module, timing_check, {});
}

handle acc_next_terminal(const handle primitive, const handle terminal) {
  return next_relation(FSIM_ACC_ITERATOR_TERMINAL, primitive, terminal, {});
}

}  // extern "C"
