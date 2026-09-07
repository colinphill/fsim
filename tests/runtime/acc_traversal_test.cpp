// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;
using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Error = fsim::runtime::SystemVerilogVpiObjectError;
using RawNextRoutine = handle (*)(void);

struct Cursor {
  std::vector<std::uint64_t> objects;
  std::size_t index{};
};

struct Fixture {
  Registry registry{91};
  std::vector<std::uint64_t> objects;
  std::map<std::uint64_t, Cursor> cursors;
  std::uint64_t next_cursor{1};
  bool fail_next{};
};

void require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

[[nodiscard]] PLI_INT32 type_of(
    const fsim::runtime::SystemVerilogVpiObjectInfo& object) {
  if (object.name == "gate") return accPrimitive;
  switch (object.kind) {
    case Kind::Root:
    case Kind::Module:
      return accModule;
    case Kind::GenerateScope:
      return accScope;
    case Kind::Port:
      return accPort;
    case Kind::Net:
      return accNet;
    case Kind::Parameter:
      return object.name == "spec" ? accSpecparam : accParameter;
    default:
      return accReg;
  }
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& registry = static_cast<Fixture*>(user_data)->registry;
  const auto result = registry.lookup(object);
  if (!result) {
    if (result.error == Error::StaleHandle) return FSIM_ACC_VPI_OBJECT_STALE;
    if (result.error == Error::ReleasedHandle) {
      return FSIM_ACC_VPI_OBJECT_RELEASED;
    }
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  *type = type_of(*result.value);
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

[[nodiscard]] bool selected(const std::uint32_t family,
                            const fsim::runtime::SystemVerilogVpiObjectInfo& object) {
  switch (family) {
    case FSIM_ACC_TRAVERSE_CELL:
      return object.kind == Kind::Module;
    case FSIM_ACC_TRAVERSE_CHILD:
      return true;
    case FSIM_ACC_TRAVERSE_NET:
      return object.kind == Kind::Net;
    case FSIM_ACC_TRAVERSE_PARAMETER:
      return object.kind == Kind::Parameter && object.name != "spec";
    case FSIM_ACC_TRAVERSE_PORT:
      return object.kind == Kind::Port;
    case FSIM_ACC_TRAVERSE_PRIMITIVE:
      return object.name == "gate";
    case FSIM_ACC_TRAVERSE_SCOPE:
      return object.kind == Kind::Module || object.kind == Kind::GenerateScope;
    case FSIM_ACC_TRAVERSE_SPECPARAM:
      return object.kind == Kind::Parameter && object.name == "spec";
    case FSIM_ACC_TRAVERSE_TOP_MODULE:
      return object.kind == Kind::Root;
    default:
      return false;
  }
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL traverse(
    void* const user_data, const std::uint32_t operation,
    const std::uint32_t family, const std::uint64_t scope,
    std::uint64_t* const cursor, std::uint64_t* const object) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (operation == FSIM_ACC_TRAVERSE_END) {
    return fixture.cursors.erase(*cursor) == 1U
               ? FSIM_ACC_VPI_OBJECT_VALID
               : FSIM_ACC_VPI_OBJECT_INVALID;
  }
  if (operation == FSIM_ACC_TRAVERSE_NEXT) {
    if (fixture.fail_next) {
      fixture.fail_next = false;
      return FSIM_ACC_VPI_OBJECT_INVALID;
    }
    const auto found = fixture.cursors.find(*cursor);
    if (found == fixture.cursors.end()) return FSIM_ACC_VPI_OBJECT_INVALID;
    if (found->second.index == found->second.objects.size()) {
      return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    }
    *object = found->second.objects[found->second.index++];
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (operation != FSIM_ACC_TRAVERSE_BEGIN ||
      family < FSIM_ACC_TRAVERSE_CELL ||
      family > FSIM_ACC_TRAVERSE_TOP_MODULE) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  Cursor candidate;
  for (const auto candidate_handle : fixture.objects) {
    const auto view = fixture.registry.lookup(candidate_handle);
    if (!view) return FSIM_ACC_VPI_OBJECT_INVALID;
    const bool correct_parent = family == FSIM_ACC_TRAVERSE_TOP_MODULE
                                    ? view.value->parent == 0
                                    : view.value->parent == scope;
    if (correct_parent && selected(family, *view.value)) {
      candidate.objects.push_back(candidate_handle);
    }
  }
  const auto token = fixture.next_cursor++;
  fixture.cursors.emplace(token, std::move(candidate));
  *cursor = token;
  return FSIM_ACC_VPI_OBJECT_VALID;
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

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_iterate(
    void*, const fsim_acc_iterator_query_v3*, fsim_acc_iterator_result_v3*) {
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

fsim_acc_handle_context_v3 context(Fixture& fixture,
                                   const std::uint64_t root) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          13,
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
          traverse,
          unsupported_object_query,
          unsupported_read,
          unsupported_write,
          unsupported_iterate,
          unsupported_timing,
          unsupported_vcl,
          nullptr};
}

template <typename Function>
[[nodiscard]] RawNextRoutine raw_routine(const Function routine) {
  static_assert(sizeof(RawNextRoutine) == sizeof(Function));
  return std::bit_cast<RawNextRoutine>(routine);
}

handle unsupported_next(handle, handle) {
  return nullptr;
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root = fixture.registry.create(Kind::Root, 0, "top");
  const auto second_root = fixture.registry.create(Kind::Root, 0, "other");
  const auto port = fixture.registry.create(Kind::Port, root.value, "p");
  const auto first_net = fixture.registry.create(Kind::Net, root.value, "a");
  const auto cell = fixture.registry.create(Kind::Module, root.value, "u");
  const auto second_net = fixture.registry.create(Kind::Net, root.value, "b");
  const auto parameter =
      fixture.registry.create(Kind::Parameter, root.value, "parameter");
  const auto specparam =
      fixture.registry.create(Kind::Parameter, root.value, "spec");
  const auto primitive =
      fixture.registry.create(Kind::Variable, root.value, "gate");
  const auto scope =
      fixture.registry.create(Kind::GenerateScope, root.value, "g");
  require(static_cast<bool>(root),
          "traversal fixture constructs its first root");
  require(static_cast<bool>(second_root),
          "traversal fixture constructs its second root");
  require(static_cast<bool>(port), "traversal fixture constructs its port");
  require(first_net && second_net, "traversal fixture constructs its nets");
  require(static_cast<bool>(cell), "traversal fixture constructs its cell");
  require(parameter && specparam,
          "traversal fixture constructs parameter families");
  require(static_cast<bool>(primitive),
          "traversal fixture constructs its primitive");
  require(static_cast<bool>(scope),
          "traversal fixture constructs its nested scope");
  fixture.objects = {root.value,       second_root.value, port.value,
                     first_net.value,  cell.value,        second_net.value,
                     parameter.value,  specparam.value,   primitive.value,
                     scope.value};

  auto active = context(fixture, root.value);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "traversal fixture enters its exact v3 context");
  auto root_handle = fsim_acc_handle_from_vpi_v3(root.value);
  auto first_net_handle = fsim_acc_handle_from_vpi_v3(first_net.value);
  require(root_handle && first_net_handle,
          "traversal fixture maps root and recovery anchor");

  auto net0 = acc_next_net(root_handle, nullptr);
  require(acc_next_port(root_handle, net0) == nullptr &&
              acc_error_flag == 1 && fixture.cursors.empty(),
          "a previous object cannot transfer an open cursor between families");
  auto net1 = acc_next_net(root_handle, net0);
  require(fsim_acc_handle_to_vpi_v3(net0) == first_net.value &&
              fsim_acc_handle_to_vpi_v3(net1) == second_net.value &&
              acc_next_net(root_handle, net1) == nullptr &&
              acc_error_flag == 0,
          "filtered next traversal preserves canonical child creation order");
  auto recovered_net = acc_next_net(root_handle, first_net_handle);
  require(fsim_acc_handle_to_vpi_v3(recovered_net) == second_net.value &&
              acc_next_net(root_handle, recovered_net) == nullptr,
          "next traversal can recover position from a valid prior object");

  auto port_handle = acc_next_port(root_handle, nullptr);
  auto cell_handle = acc_next_cell(root_handle, nullptr);
  auto parameter_handle = acc_next_parameter(root_handle, nullptr);
  auto specparam_handle = acc_next_specparam(root_handle, nullptr);
  auto primitive_handle = acc_next_primitive(root_handle, nullptr);
  require(fsim_acc_handle_to_vpi_v3(port_handle) == port.value &&
              fsim_acc_handle_to_vpi_v3(cell_handle) == cell.value &&
              fsim_acc_handle_to_vpi_v3(parameter_handle) == parameter.value &&
              fsim_acc_handle_to_vpi_v3(specparam_handle) == specparam.value &&
              fsim_acc_handle_to_vpi_v3(primitive_handle) == primitive.value,
          "specialized families return only their selected object kinds");
  require(acc_next_port(root_handle, port_handle) == nullptr &&
              acc_next_cell(root_handle, cell_handle) == nullptr &&
              acc_next_parameter(root_handle, parameter_handle) == nullptr &&
              acc_next_specparam(root_handle, specparam_handle) == nullptr &&
              acc_next_primitive(root_handle, primitive_handle) == nullptr,
          "single-result specialized traversals close at their canonical end");
  auto scope0 = acc_next_scope(root_handle, nullptr);
  auto scope1 = acc_next_scope(root_handle, scope0);
  require(fsim_acc_handle_to_vpi_v3(scope0) == cell.value &&
              fsim_acc_handle_to_vpi_v3(scope1) == scope.value &&
              acc_next_scope(root_handle, scope1) == nullptr,
          "scope traversal includes module and nested hierarchy scopes");

  auto top0 = acc_next_topmod(nullptr);
  auto top1 = acc_next_topmod(top0);
  require(fsim_acc_handle_to_vpi_v3(top0) == root.value &&
              fsim_acc_handle_to_vpi_v3(top1) == second_root.value &&
              acc_next_topmod(top1) == nullptr && acc_error_flag == 0,
          "top-module traversal is canonical across independent roots");

  PLI_INT32 collected_count{};
  auto* collected = acc_collect(raw_routine(&acc_next_net), root_handle,
                                &collected_count);
  require(collected && collected_count == 2 && collected[0] == net0 &&
              collected[1] == net1 && collected[2] == nullptr,
          "collect returns a bounded terminated array in traversal order");
  acc_free(collected);
  require(acc_error_flag == 0,
          "free releases an exact simulator-owned collection once");
  acc_free(collected);
  require(acc_error_flag == 1,
          "free rejects stale or foreign collection pointers");
  require(acc_count(raw_routine(&acc_next_net), root_handle) == 2 &&
              acc_error_flag == 0,
          "count shares the exact specialized traversal semantics");

  require(acc_count(raw_routine(&unsupported_next), root_handle) == 0 &&
              acc_error_flag == 1,
          "collect/count dispatch rejects routines outside the standard set");
  require(acc_collect(raw_routine(&acc_next_net), root_handle,
                      reinterpret_cast<PLI_INT32*>(
                          static_cast<std::uintptr_t>(1))) == nullptr &&
              acc_error_flag == 1,
          "collect validates writable count storage before traversal");
  require(acc_next_net(first_net_handle, nullptr) == nullptr &&
              acc_error_flag == 1,
          "specialized traversal rejects nonscope owners");
  fixture.fail_next = true;
  require(acc_next_child(root_handle, nullptr) == nullptr &&
              acc_error_flag == 1,
          "callback failure closes the unpublished iterator transaction");
  require(fixture.cursors.empty(),
          "completed and failed traversals release every native cursor");

  fsim_acc_handle_context_leave_v3(&active);
  return 0;
}
