// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

struct Cursor {
  std::uint64_t owner{};
  bool delivered{};
};

struct Fixture {
  Registry registry{139};
  std::map<std::uint64_t, PLI_INT32> types;
  std::map<std::uint64_t, std::string> names;
  std::map<std::uint64_t, Cursor> cursors;
  std::uint64_t net{};
  std::uint64_t next_cursor{1};
  std::uint64_t link{};
  std::uint32_t iterator_begin{};
  std::uint32_t iterator_end{};
  std::uint32_t consumer_calls{};
};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
}

PLI_INT32 consumer(p_vc_record record) {
  auto& fixture = *reinterpret_cast<Fixture*>(record->user_data);
  ++fixture.consumer_calls;
  return 0;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  const auto found = fixture.types.find(object);
  if (!fixture.registry.lookup(object) || found == fixture.types.end() ||
      type == nullptr) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  *type = found->second;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_lookup(
    void*, std::uint32_t, std::uint64_t, const PLI_BYTE8*, std::uint32_t,
    std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_relation(
    void*, std::uint32_t, std::uint64_t, std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_name(
    void*, std::uint64_t, PLI_BYTE8*, std::uint32_t, std::uint32_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_traverse(
    void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t*,
    std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_object_query(
    void*, const fsim_acc_object_query_v3*, std::uint64_t*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL read_object(
    void* const user_data, const fsim_acc_read_query_v3* const query,
    fsim_acc_read_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (query == nullptr || result == nullptr ||
      query->operation != FSIM_ACC_READ_OBJECT) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto type = fixture.types.find(query->object);
  const auto name = fixture.names.find(query->object);
  if (type == fixture.types.end() || name == fixture.names.end()) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  result->type = type->second == accWire ? accNet : type->second;
  result->full_type = type->second;
  result->width = 1;
  result->name = name->second.data();
  result->name_size = static_cast<std::uint32_t>(name->second.size());
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_write(
    void*, const fsim_acc_write_query_v3*, fsim_acc_write_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL iterate(
    void* const user_data, const fsim_acc_iterator_query_v3* const query,
    fsim_acc_iterator_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (query == nullptr || result == nullptr ||
      query->family != FSIM_ACC_ITERATOR_GENERIC) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation == FSIM_ACC_ITERATOR_BEGIN) {
    ++fixture.iterator_begin;
    const auto cursor = fixture.next_cursor++;
    fixture.cursors.emplace(cursor, Cursor{query->owner, false});
    result->cursor = cursor;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto found = fixture.cursors.find(query->cursor);
  if (found == fixture.cursors.end() || found->second.owner != query->owner) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation == FSIM_ACC_ITERATOR_END) {
    fixture.cursors.erase(found);
    ++fixture.iterator_end;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (query->operation != FSIM_ACC_ITERATOR_NEXT) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  result->cursor = query->cursor;
  if (found->second.delivered) return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  found->second.delivered = true;
  result->object = fixture.net;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_timing(
    void*, const fsim_acc_timing_query_v3*, fsim_acc_timing_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL vcl_control(
    void* const user_data, const fsim_acc_vcl_query_v3* const query) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (query == nullptr) return FSIM_ACC_VPI_OBJECT_INVALID;
  if (query->operation == FSIM_ACC_VCL_REGISTER) {
    fixture.link = query->link_identity;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (query->operation == FSIM_ACC_VCL_UNREGISTER) {
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(Fixture& fixture,
                                                  const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          43,
          256,
          0,
          &fixture,
          resolve_object,
          root,
          root,
          root,
          unsupported_lookup,
          unsupported_relation,
          unsupported_name,
          unsupported_traverse,
          unsupported_object_query,
          read_object,
          unsupported_write,
          iterate,
          unsupported_timing,
          vcl_control,
          nullptr};
}

[[nodiscard]] fsim_acc_safe_point_v3 safe_point(
    const std::uint64_t identity) {
  return {FSIM_ACC_SAFE_POINT_ABI_VERSION,
          sizeof(fsim_acc_safe_point_v3), 0, 0, identity};
}

[[nodiscard]] fsim_acc_vcl_event_v3 value_event(
    const std::uint64_t link, const std::uint64_t sequence) {
  fsim_acc_vcl_event_v3 result{};
  result.abi_version = FSIM_ACC_VCL_QUERY_ABI_VERSION;
  result.struct_size = sizeof(result);
  result.reason = logic_value_change;
  result.link_identity = link;
  result.sequence_identity = sequence;
  result.logic_value = acc1;
  return result;
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root_object = fixture.registry.create(Kind::Root, 0, "top");
  require(static_cast<bool>(root_object), "lifetime fixture creates root");
  const auto net_object =
      fixture.registry.create(Kind::Variable, root_object.value, "wire");
  require(static_cast<bool>(net_object), "lifetime fixture creates net");
  fixture.net = net_object.value;
  fixture.types.emplace(root_object.value, accTopModule);
  fixture.types.emplace(net_object.value, accWire);
  fixture.names.emplace(root_object.value, "top");
  fixture.names.emplace(net_object.value, "wire");

  require(acc_initialize() == 1, "ACC lifecycle initializes");
  auto active = context(fixture, root_object.value);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "lifetime context enters");
  const auto root = fsim_acc_handle_from_vpi_v3(root_object.value);
  const auto net = fsim_acc_handle_from_vpi_v3(net_object.value);
  require(root != nullptr && net != nullptr, "lifetime handles map");

  auto first_point = safe_point(1);
  require(fsim_acc_safe_point_advance_v3(&active, &first_point) == 1,
          "first safe point publishes");
  PLI_INT32 types[]{accNet, 0};
  const auto first = acc_next(types, root, nullptr);
  require(first == net && fixture.iterator_begin == 1 &&
              fixture.iterator_end == 0,
          "iterator remains open within one safe point");
  auto* const borrowed = acc_fetch_name(net);
  require(borrowed != nullptr && std::string{borrowed} == "wire",
          "borrowed string is visible within one safe point");
  acc_vcl_add(net, consumer, reinterpret_cast<PLI_BYTE8*>(&fixture),
              vcl_verilog_logic);
  require(acc_error_flag == 0 && fixture.link != 0,
          "callback link registers before safe-point advance");

  auto second_point = safe_point(2);
  require(fsim_acc_safe_point_advance_v3(&active, &second_point) == 1 &&
              fixture.iterator_end == 1 && fixture.cursors.empty(),
          "safe-point advance closes retained iterator cursors");
  require(fsim_acc_handle_to_vpi_v3(net) == net_object.value,
          "generation-qualified handles survive safe-point advance");
  auto event = value_event(fixture.link, 1);
  require(fsim_acc_vcl_dispatch_v3(&active, &event) == 1 &&
              fixture.consumer_calls == 1,
          "callback links survive safe-point advance");
  require(acc_next(types, root, first) == nullptr && acc_error_flag == 1 &&
              fixture.iterator_begin == 1,
          "an iterator cannot resume across its safe-point boundary");
  const auto fresh = acc_next(types, root, nullptr);
  require(fresh == net && fixture.iterator_begin == 2,
          "a fresh iterator starts after safe-point advance");
  require(acc_next(types, root, fresh) == nullptr && acc_error_flag == 0 &&
              fixture.iterator_end == 2,
          "fresh iterator exhausts and closes normally");
  auto* const fresh_name = acc_fetch_name(net);
  require(fresh_name != nullptr && std::string{fresh_name} == "wire",
          "borrowed storage can be reacquired after a safe point");
  acc_reset_buffer();
  require(acc_error_flag == 0 && acc_fetch_name(net) != nullptr,
          "explicit buffer reset invalidates and permits reacquisition");

  require(fsim_acc_safe_point_advance_v3(&active, &second_point) == 0 &&
              acc_error_flag == 1,
          "safe-point identities must increase monotonically");
  auto malformed = safe_point(3);
  malformed.reserved = 1;
  require(fsim_acc_safe_point_advance_v3(&active, &malformed) == 0,
          "malformed safe-point records are rejected");

  fsim_acc_handle_context_leave_v3(&active);
  active.hierarchy_generation = 44;
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "next hierarchy generation enters");
  require(fsim_acc_handle_to_vpi_v3(net) == 0 && acc_error_flag == 1,
          "old handles reject a new hierarchy generation");
  event.sequence_identity = 2;
  require(fsim_acc_vcl_dispatch_v3(&active, &event) == 0 &&
              fixture.consumer_calls == 1,
          "old callback links reject a new hierarchy generation");
  fsim_acc_handle_context_leave_v3(&active);
  acc_close();
  return 0;
}
