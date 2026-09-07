// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

struct Cursor {
  std::uint32_t family{};
  std::uint64_t owner{};
  std::vector<PLI_INT32> types;
  std::size_t index{};
};

struct Fixture {
  Registry registry{109};
  std::map<std::uint64_t, PLI_INT32> types;
  std::map<std::pair<std::uint32_t, std::uint64_t>,
           std::vector<std::uint64_t>> relations;
  std::map<std::uint64_t, Cursor> cursors;
  std::uint64_t next_cursor{1};
  std::uint32_t calls{};
  std::uint32_t end_calls{};
  bool throw_iterate{};
  bool malformed_result{};
};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] bool matches(const PLI_INT32 actual,
                           const PLI_INT32 requested) {
  if (actual == requested) return true;
  if (requested == accNet) {
    return actual == accNetBit || (actual >= accWire && actual <= accSupply1);
  }
  if (requested == accReg) return actual == accRegBit;
  if (requested == accPort) {
    return actual == accPortBit ||
           (actual >= accScalarPort && actual <= accConcatPort);
  }
  if (requested == accParameter) {
    return actual == accIntegerParam || actual == accRealParam ||
           actual == accStringParam;
  }
  return false;
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

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_read(
    void*, const fsim_acc_read_query_v3*, fsim_acc_read_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_write(
    void*, const fsim_acc_write_query_v3*, fsim_acc_write_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_timing(
    void*, const fsim_acc_timing_query_v3*, fsim_acc_timing_result_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_vcl(
    void*, const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

[[nodiscard]] std::vector<PLI_INT32> query_types(
    const fsim_acc_iterator_query_v3& query) {
  if (query.type_count == 0) return {};
  return {query.types, query.types + query.type_count};
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL iterate(
    void* const user_data, const fsim_acc_iterator_query_v3* const query,
    fsim_acc_iterator_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  ++fixture.calls;
  if (fixture.throw_iterate) throw std::runtime_error("iterate failure");
  if (query == nullptr || result == nullptr ||
      query->abi_version != FSIM_ACC_ITERATOR_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->family < FSIM_ACC_ITERATOR_GENERIC ||
      query->family > FSIM_ACC_ITERATOR_TERMINAL ||
      query->type_count > FSIM_ACC_ITERATOR_MAXIMUM_TYPES ||
      (query->family == FSIM_ACC_ITERATOR_GENERIC) !=
          (query->type_count != 0) ||
      result->abi_version != FSIM_ACC_ITERATOR_QUERY_ABI_VERSION ||
      result->struct_size < sizeof(*result)) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation == FSIM_ACC_ITERATOR_BEGIN) {
    const auto cursor = fixture.next_cursor++;
    fixture.cursors.emplace(
        cursor, Cursor{query->family, query->owner, query_types(*query), 0});
    result->cursor = cursor;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  const auto found = fixture.cursors.find(query->cursor);
  if (found == fixture.cursors.end() ||
      found->second.family != query->family ||
      found->second.owner != query->owner ||
      found->second.types != query_types(*query)) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (query->operation == FSIM_ACC_ITERATOR_END) {
    fixture.cursors.erase(found);
    ++fixture.end_calls;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (query->operation != FSIM_ACC_ITERATOR_NEXT) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  result->cursor = query->cursor;
  const auto relation = fixture.relations.find({query->family, query->owner});
  if (relation == fixture.relations.end()) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  while (found->second.index < relation->second.size()) {
    const auto candidate = relation->second[found->second.index++];
    if (query->family == FSIM_ACC_ITERATOR_GENERIC) {
      const auto actual = fixture.types.at(candidate);
      bool accepted{};
      for (const auto requested : found->second.types) {
        accepted = accepted || matches(actual, requested);
      }
      if (!accepted) continue;
    }
    result->object = candidate;
    if (fixture.malformed_result) result->reserved = 1;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

[[nodiscard]] fsim_acc_handle_context_v3 context(
    Fixture& fixture, const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          29,
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
          unsupported_read,
          unsupported_write,
          iterate,
          unsupported_timing,
          unsupported_vcl,
          nullptr};
}

[[nodiscard]] std::uint64_t add(Fixture& fixture, const std::uint64_t parent,
                                const std::string& name,
                                const PLI_INT32 type) {
  const auto object = fixture.registry.create(Kind::Variable, parent, name);
  require(static_cast<bool>(object), "iterator fixture creates VPI identity");
  fixture.types.emplace(object.value, type);
  return object.value;
}

[[nodiscard]] handle mapped(const std::uint64_t object) {
  auto result = fsim_acc_handle_from_vpi_v3(object);
  require(result != nullptr, "iterator fixture maps VPI identity");
  return result;
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root_object = fixture.registry.create(Kind::Root, 0, "top");
  require(static_cast<bool>(root_object), "iterator fixture creates root");
  const auto root = root_object.value;
  fixture.types.emplace(root, accTopModule);
  const auto net = add(fixture, root, "net", accWire);
  const auto reg = add(fixture, root, "reg", accReg);
  const auto parameter = add(fixture, root, "P", accIntegerParam);
  const auto port = add(fixture, root, "port", accPort);
  const auto path = add(fixture, root, "path", accModPath);
  const auto primitive = add(fixture, root, "gate", accAndGate);
  const auto bit0 = add(fixture, reg, "bit0", accRegBit);
  const auto bit1 = add(fixture, reg, "bit1", accRegBit);
  const auto input_terminal =
      add(fixture, primitive, "input", accInputTerminal);
  const auto output_terminal =
      add(fixture, primitive, "output", accOutputTerminal);
  const auto path_input = add(fixture, path, "path_input", accPathInput);
  const auto path_output = add(fixture, path, "path_output", accPathOutput);
  const auto timing_check = add(fixture, root, "setup", accSetup);

  fixture.relations[{FSIM_ACC_ITERATOR_GENERIC, root}] = {net, reg, parameter};
  fixture.relations[{FSIM_ACC_ITERATOR_BIT, reg}] = {bit0, bit1};
  fixture.relations[{FSIM_ACC_ITERATOR_CELL_LOAD, net}] = {primitive};
  fixture.relations[{FSIM_ACC_ITERATOR_DRIVER, net}] = {output_terminal};
  fixture.relations[{FSIM_ACC_ITERATOR_HICONN, port}] = {net};
  fixture.relations[{FSIM_ACC_ITERATOR_INPUT, path}] = {path_input};
  fixture.relations[{FSIM_ACC_ITERATOR_LOAD, net}] = {input_terminal};
  fixture.relations[{FSIM_ACC_ITERATOR_LOCONN, port}] = {net};
  fixture.relations[{FSIM_ACC_ITERATOR_MODPATH, root}] = {path};
  fixture.relations[{FSIM_ACC_ITERATOR_OUTPUT, path}] = {path_output};
  fixture.relations[{FSIM_ACC_ITERATOR_PORTOUT, root}] = {port};
  fixture.relations[{FSIM_ACC_ITERATOR_TCHK, root}] = {timing_check};
  fixture.relations[{FSIM_ACC_ITERATOR_TERMINAL, primitive}] = {
      input_terminal, output_terminal};

  auto active = context(fixture, root);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "iterator fixture enters its exact v3 context");
  auto root_handle = mapped(root);
  auto net_handle = mapped(net);
  auto reg_handle = mapped(reg);
  auto port_handle = mapped(port);
  auto path_handle = mapped(path);
  auto primitive_handle = mapped(primitive);

  std::array<PLI_INT32, 3> selected_types{accNet, accParameter, 0};
  auto first = acc_next(selected_types.data(), root_handle, nullptr);
  auto second = acc_next(selected_types.data(), root_handle, first);
  require(fsim_acc_handle_to_vpi_v3(first) == net &&
              fsim_acc_handle_to_vpi_v3(second) == parameter &&
              acc_next(selected_types.data(), root_handle, second) == nullptr &&
              acc_error_flag == 0,
          "generic iterator filters canonical children by exact type list");

  auto bit_first = acc_next_bit(reg_handle, nullptr);
  auto bit_second = acc_next_bit(reg_handle, bit_first);
  require(fsim_acc_handle_to_vpi_v3(bit_first) == bit0 &&
              fsim_acc_handle_to_vpi_v3(bit_second) == bit1 &&
              acc_next_bit(reg_handle, bit_second) == nullptr &&
              acc_error_flag == 0,
          "bit iterator preserves canonical order and normal exhaustion");
  require(fsim_acc_handle_to_vpi_v3(acc_next_bit(reg_handle, bit_first)) ==
              bit1,
          "iterator recovers position from a valid prior relation object");
  require(acc_next_bit(reg_handle, bit_second) == nullptr &&
              acc_error_flag == 0,
          "recovered iterator closes cleanly at end");

  auto high = acc_next_hiconn(port_handle, nullptr);
  auto low = acc_next_loconn(port_handle, nullptr);
  require(fsim_acc_handle_to_vpi_v3(high) == net &&
              fsim_acc_handle_to_vpi_v3(low) == net &&
              acc_next_hiconn(port_handle, high) == nullptr &&
              acc_next_loconn(port_handle, low) == nullptr,
          "same result identity can own independent relation-family cursors");

  struct RelationCase {
    handle (*next)(handle, handle);
    handle owner;
    std::uint64_t expected;
  };
  const std::array relations{
      RelationCase{acc_next_cell_load, net_handle, primitive},
      RelationCase{acc_next_driver, net_handle, output_terminal},
      RelationCase{acc_next_input, path_handle, path_input},
      RelationCase{acc_next_load, net_handle, input_terminal},
      RelationCase{acc_next_modpath, root_handle, path},
      RelationCase{acc_next_output, path_handle, path_output},
      RelationCase{acc_next_portout, root_handle, port},
      RelationCase{acc_next_tchk, root_handle, timing_check},
      RelationCase{acc_next_terminal, primitive_handle, input_terminal}};
  for (const auto& relation : relations) {
    auto object = relation.next(relation.owner, nullptr);
    require(fsim_acc_handle_to_vpi_v3(object) == relation.expected,
            "specialized iterator returns its exact relation subtype");
    while (object != nullptr) object = relation.next(relation.owner, object);
    require(acc_error_flag == 0,
            "specialized iterator reports normal exhaustion");
  }

  const auto calls_before = fixture.calls;
  std::array<PLI_INT32, 3> duplicate_types{accNet, accNet, 0};
  require(acc_next(duplicate_types.data(), root_handle, nullptr) == nullptr &&
              acc_error_flag == 1 && fixture.calls == calls_before,
          "duplicate generic types fail before callback dispatch");
  require(acc_next(reinterpret_cast<PLI_INT32*>(1), root_handle, nullptr) ==
                  nullptr &&
              acc_error_flag == 1 && fixture.calls == calls_before,
          "unreadable generic type lists fail before callback dispatch");
  std::array<PLI_INT32, 2> unknown_type{999999, 0};
  require(acc_next(unknown_type.data(), root_handle, nullptr) == nullptr &&
              acc_error_flag == 1 && fixture.calls == calls_before,
          "nonstandard generic types fail before callback dispatch");
  require(acc_next_bit(root_handle, nullptr) == nullptr &&
              acc_error_flag == 1 && fixture.calls == calls_before,
          "invalid relation owners fail before callback dispatch");
  require(acc_next_bit(reg_handle, net_handle) == nullptr &&
              acc_error_flag == 1,
          "a valid handle outside the relation fails recovery");

  fixture.relations[{FSIM_ACC_ITERATOR_BIT, reg}] = {parameter};
  fixture.malformed_result = true;
  require(acc_next_bit(reg_handle, nullptr) == nullptr &&
              acc_error_flag == 1 && fixture.cursors.empty(),
          "malformed result records close their native cursor");
  fixture.malformed_result = false;
  fixture.relations[{FSIM_ACC_ITERATOR_BIT, reg}] = {bit0, bit1};
  fixture.throw_iterate = true;
  require(acc_next_bit(reg_handle, nullptr) == nullptr &&
              acc_error_flag == 1,
          "iterator callback exceptions are contained at the ACC boundary");
  fixture.throw_iterate = false;
  require(fixture.cursors.empty() && fixture.end_calls >= 15,
          "completed and failed indexed iterators release every native cursor");

  fsim_acc_handle_context_leave_v3(&active);
  active.iterate = nullptr;
  require(fsim_acc_handle_context_enter_v3(&active) == 0 &&
              acc_error_flag == 1,
          "context entry rejects a missing iterator callback");
  return 0;
}
