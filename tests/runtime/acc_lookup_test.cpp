// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;
using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Error = fsim::runtime::SystemVerilogVpiObjectError;

void require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

[[nodiscard]] std::uint32_t status(const Error error) {
  switch (error) {
    case Error::None:
      return FSIM_ACC_VPI_OBJECT_VALID;
    case Error::StaleHandle:
      return FSIM_ACC_VPI_OBJECT_STALE;
    case Error::ReleasedHandle:
      return FSIM_ACC_VPI_OBJECT_RELEASED;
    case Error::NotFound:
      return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    default:
      return FSIM_ACC_VPI_OBJECT_INVALID;
  }
}

[[nodiscard]] PLI_INT32 acc_type(const Kind kind) {
  switch (kind) {
    case Kind::Root:
    case Kind::Module:
    case Kind::Interface:
    case Kind::Program:
      return accModule;
    case Kind::Port:
      return accPort;
    case Kind::Net:
      return accNet;
    case Kind::Variable:
      return accReg;
    case Kind::Parameter:
      return accParameter;
    case Kind::NamedEvent:
      return accNamedEvent;
    default:
      return accScope;
  }
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  const auto result = static_cast<Registry*>(user_data)->lookup(object);
  if (!result) return status(result.error);
  *type = acc_type(result.value->kind);
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL lookup_object(
    void* const user_data, const std::uint32_t mode,
    const std::uint64_t scope, const PLI_BYTE8* const name,
    const std::uint32_t name_size, std::uint64_t* const object) {
  auto& registry = *static_cast<Registry*>(user_data);
  const std::string_view requested{name, name_size};
  fsim::runtime::SystemVerilogVpiObjectLookupResult result;
  if (mode == FSIM_ACC_LOOKUP_ABSOLUTE) {
    result = registry.find(requested);
  } else {
    const auto base = registry.lookup(scope);
    if (!base) return status(base.error);
    if (mode == FSIM_ACC_LOOKUP_PLI_SCOPE) {
      result = registry.find(requested);
      if (result) {
        *object = result.value->handle;
        return FSIM_ACC_VPI_OBJECT_VALID;
      }
    }
    std::string full_name = base.value->full_name;
    full_name.push_back('.');
    full_name.append(requested);
    result = registry.find(full_name);
  }
  if (!result) return status(result.error);
  *object = result.value->handle;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

[[nodiscard]] bool is_scope(const Kind kind) {
  return kind == Kind::Root || kind == Kind::Module ||
         kind == Kind::Interface || kind == Kind::Program ||
         kind == Kind::GenerateScope;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL related_object(
    void* const user_data, const std::uint32_t relation,
    const std::uint64_t object, std::uint64_t* const related) {
  auto& registry = *static_cast<Registry*>(user_data);
  auto current = registry.lookup(object);
  if (!current) return status(current.error);
  if (relation == FSIM_ACC_RELATION_SIMULATED_NET) {
    if (current.value->kind != Kind::Net) {
      return FSIM_ACC_VPI_OBJECT_WRONG_TYPE;
    }
    *related = object;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (relation == FSIM_ACC_RELATION_PARENT) {
    if (current.value->parent == 0) return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
    *related = current.value->parent;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (relation != FSIM_ACC_RELATION_SCOPE) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  auto parent = current.value->parent;
  while (parent != 0) {
    current = registry.lookup(parent);
    if (!current) return status(current.error);
    if (is_scope(current.value->kind)) {
      *related = current.value->handle;
      return FSIM_ACC_VPI_OBJECT_VALID;
    }
    parent = current.value->parent;
  }
  return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL object_name(
    void* const user_data, const std::uint64_t object,
    PLI_BYTE8* const buffer, const std::uint32_t capacity,
    std::uint32_t* const size) {
  const auto result = static_cast<Registry*>(user_data)->lookup(object);
  if (!result) return status(result.error);
  if (result.value->full_name.size() >= capacity) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  std::ranges::copy(result.value->full_name, buffer);
  *size = static_cast<std::uint32_t>(result.value->full_name.size());
  buffer[*size] = '\0';
  return FSIM_ACC_VPI_OBJECT_VALID;
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

fsim_acc_handle_context_v3 context(
    Registry& registry, const std::uint64_t generation,
    const std::uint64_t calling_scope, const std::uint64_t default_scope) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          registry.simulation_identity(),
          generation,
          128,
          0,
          &registry,
          resolve_object,
          calling_scope,
          default_scope,
          default_scope,
          lookup_object,
          related_object,
          object_name,
          unsupported_traverse,
          unsupported_object_query,
          unsupported_read,
          unsupported_write,
          unsupported_iterate,
          unsupported_timing,
          unsupported_vcl,
          nullptr};
}

}  // namespace

int main() {
  Registry registry{81};
  const auto root = registry.create(Kind::Root, 0, "top");
  const auto module = registry.create(Kind::Module, root.value, "u");
  const auto sibling = registry.create(Kind::Module, root.value, "v");
  const auto net = registry.create(Kind::Net, module.value, "data");
  const auto sibling_net = registry.create(Kind::Net, sibling.value, "data");
  const auto port = registry.create(Kind::Port, module.value, "p");
  require(root && module && sibling && net && sibling_net && port,
          "lookup fixture constructs two unambiguous hierarchy branches");

  require(acc_initialize() == 1, "lookup fixture initializes ACC lifecycle");
  auto active = context(registry, 11, module.value, root.value);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "lookup fixture enters exact simulation context");

  PLI_BYTE8 absolute_name[] = "top.u.data";
  PLI_BYTE8 relative_name[] = "data";
  PLI_BYTE8 missing_name[] = "missing";
  PLI_BYTE8 port_name[] = "p";
  auto root_handle = fsim_acc_handle_from_vpi_v3(root.value);
  auto module_handle = fsim_acc_handle_from_vpi_v3(module.value);
  auto sibling_handle = fsim_acc_handle_from_vpi_v3(sibling.value);
  auto net_handle = fsim_acc_handle_from_vpi_v3(net.value);
  require(root_handle && module_handle && sibling_handle && net_handle,
          "lookup fixture maps its hierarchy scopes and net");

  require(acc_handle_by_name(absolute_name, nullptr) == net_handle &&
              acc_handle_by_name(relative_name, module_handle) == net_handle &&
              fsim_acc_handle_to_vpi_v3(
                  acc_handle_by_name(relative_name, sibling_handle)) ==
                  sibling_net.value,
          "absolute and relative lookup retain exact branch ownership");
  require(acc_handle_by_name(missing_name, module_handle) == nullptr &&
              acc_error_flag == 0 &&
              acc_handle_by_name(port_name, module_handle) == nullptr &&
              acc_error_flag == 1,
          "missing names are distinct from unsupported by-name object kinds");
  require(acc_handle_by_name(relative_name, net_handle) == nullptr &&
              acc_error_flag == 1,
          "relative lookup rejects a nonscope base handle");

  require(acc_handle_object(relative_name) == net_handle,
          "object lookup defaults to the calling PLI scope");
  auto* root_name = acc_set_scope(root_handle);
  require(root_name && std::strcmp(root_name, "top") == 0 &&
              acc_handle_object(absolute_name) == net_handle,
          "explicit scope returns its full name and permits absolute lookup");
  PLI_BYTE8 nested_name[] = "u.data";
  require(acc_handle_object(nested_name) == net_handle,
          "object lookup searches a relative hierarchy from the PLI scope");
  require(acc_set_scope(nullptr) && acc_handle_object(nested_name) == net_handle,
          "null set-scope selects the first top scope in default mode");

  PLI_BYTE8 enable_scope_name[] = "acc_set_scope";
  PLI_BYTE8 module_name[] = "top.u";
  require(acc_configure(accEnableArgs, enable_scope_name) == 1 &&
              acc_set_scope(nullptr, module_name) != nullptr &&
              acc_handle_object(relative_name) == net_handle,
          "enabled optional set-scope names select an absolute module");
  require(acc_set_scope(net_handle) == nullptr && acc_error_flag == 1 &&
              acc_handle_object(relative_name) == net_handle,
          "set-scope rejects valid nonscope objects without changing scope");

  require(acc_handle_parent(net_handle) == module_handle &&
              acc_handle_scope(net_handle) == module_handle &&
              acc_handle_parent(root_handle) == nullptr &&
              acc_error_flag == 0,
          "parent and containing-scope queries preserve hierarchy semantics");
  require(acc_handle_simulated_net(net_handle) == net_handle &&
              acc_handle_simulated_net(module_handle) == nullptr &&
              acc_error_flag == 1,
          "uncollapsed nets map to themselves and nonnets are rejected");

  require(acc_handle_interactive_scope() == root_handle &&
              acc_set_interactive_scope(module_handle, 1) == 1 &&
              acc_handle_interactive_scope() == module_handle,
          "interactive scope starts at the tool scope and changes atomically");
  require(acc_set_interactive_scope(root_handle, 2) == 0 &&
              acc_handle_interactive_scope() == module_handle,
          "invalid callback selection cannot partially change interactive scope");
  require(acc_handle_by_name(
              reinterpret_cast<PLI_BYTE8*>(static_cast<std::uintptr_t>(1)),
              nullptr) == nullptr &&
              acc_error_flag == 1,
          "unreadable lookup names fail before callback dispatch");

  fsim_acc_handle_context_leave_v3(&active);
  acc_close();
  return 0;
}
