// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;
using Kind = fsim::runtime::SystemVerilogVpiObjectKind;

void require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const acc_type) {
  auto* const registry = static_cast<Registry*>(user_data);
  const auto result = registry->lookup(object);
  if (!result) {
    using Error = fsim::runtime::SystemVerilogVpiObjectError;
    if (result.error == Error::StaleHandle) return FSIM_ACC_VPI_OBJECT_STALE;
    if (result.error == Error::ReleasedHandle) {
      return FSIM_ACC_VPI_OBJECT_RELEASED;
    }
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  switch (result.value->kind) {
    case Kind::Root:
    case Kind::Module:
      *acc_type = accModule;
      break;
    case Kind::Port:
      *acc_type = accPort;
      break;
    case Kind::Net:
      *acc_type = accNet;
      break;
    case Kind::Variable:
      *acc_type = accReg;
      break;
    case Kind::Parameter:
      *acc_type = accParameter;
      break;
    case Kind::NamedEvent:
      *acc_type = accNamedEvent;
      break;
    default:
      *acc_type = accScope;
      break;
  }
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

fsim_acc_handle_context_v3 context(Registry& registry,
                                   const std::uint64_t generation,
                                   const std::uint64_t scope,
                                   const std::uint32_t limit = 64) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          registry.simulation_identity(),
          generation,
          limit,
          0,
          &registry,
          resolve_object,
          scope,
          scope,
          scope,
          unsupported_lookup,
          unsupported_relation,
          unsupported_name,
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
  Registry first{41};
  Registry second{42};
  const auto root = first.create(Kind::Root, 0, "top");
  const auto module = first.create(Kind::Module, root.value, "u");
  const auto net = first.create(Kind::Net, module.value, "data");
  const auto second_root = second.create(Kind::Root, 0, "other");
  const auto second_net =
      second.create(Kind::Net, second_root.value, "limited_data");
  require(root && module && net && second_root && second_net,
          "VPI fixtures publish generation-qualified hierarchy objects");

  auto first_context = context(first, 7, root.value);
  require(fsim_acc_handle_context_enter_v3(&first_context) == 1,
          "ACC handle context accepts exact v3 simulation ownership");
  require(fsim_acc_handle_context_enter_v3(&first_context) == 0 &&
              acc_error_flag == 1,
          "ACC handle context rejects re-entry");
  auto root_handle = fsim_acc_handle_from_vpi_v3(root.value);
  auto module_handle = fsim_acc_handle_from_vpi_v3(module.value);
  auto net_handle = fsim_acc_handle_from_vpi_v3(net.value);
  require(root_handle && module_handle && net_handle &&
              fsim_acc_handle_from_vpi_v3(net.value) == net_handle,
          "ACC mapping is stable for one live VPI object generation");
  require(fsim_acc_handle_to_vpi_v3(net_handle) == net.value &&
              acc_object_of_type(net_handle, accNet) == 1 &&
              acc_object_of_type(net_handle, accReg) == 0 &&
              acc_error_flag == 0,
          "ACC handles retain exact VPI identity and canonical type");
  PLI_INT32 types[] = {accModule, accNet, 0};
  require(acc_object_in_typelist(net_handle, types) == 1 &&
              acc_compare_handles(net_handle, net_handle) == 1 &&
              acc_compare_handles(net_handle, module_handle) == 0 &&
              acc_error_flag == 0,
          "ACC comparison and bounded type lists use underlying identity");
  std::array<PLI_INT32, 256> unterminated_types{};
  unterminated_types.fill(accModule);
  require(acc_object_in_typelist(net_handle, unterminated_types.data()) == 0 &&
              acc_error_flag == 1,
          "ACC type lists are rejected when their bounded terminator is absent");
  require(acc_object_in_typelist(
              net_handle,
              reinterpret_cast<PLI_INT32*>(static_cast<std::uintptr_t>(1))) ==
              0 &&
              acc_error_flag == 1,
          "malformed ACC type-list pointers are rejected before dereference");

  fsim_acc_handle_context_leave_v3(&first_context);
  require(acc_object_of_type(net_handle, accNet) == 0 && acc_error_flag == 1,
          "ACC objects cannot escape their active simulation context");
  auto second_context = context(second, 9, second_root.value);
  require(fsim_acc_handle_context_enter_v3(&second_context) == 1 &&
              acc_object_of_type(net_handle, accNet) == 0 &&
              acc_error_flag == 1,
          "ACC rejects a live handle in a different simulation");
  fsim_acc_handle_context_leave_v3(&second_context);

  require(first.release(net.value) ==
              fsim::runtime::SystemVerilogVpiObjectError::None &&
              fsim_acc_handle_context_enter_v3(&first_context) == 1 &&
              acc_object_of_type(net_handle, accNet) == 0 &&
              acc_error_flag == 1,
          "ACC observes stale or released underlying VPI generations");
  require(acc_release_object(module_handle) == 1 && acc_error_flag == 0 &&
              acc_object_of_type(module_handle, accModule) == 0 &&
              acc_error_flag == 1,
          "ACC release invalidates only its mapped wrapper");
  auto remapped_module = fsim_acc_handle_from_vpi_v3(module.value);
  require(remapped_module && remapped_module != module_handle &&
              acc_object_of_type(remapped_module, accModule) == 1,
          "remapping a live VPI object cannot revive an old ACC handle");
  require(acc_object_of_type(reinterpret_cast<handle>(1), accModule) == 0 &&
              acc_error_flag == 1,
          "malformed ACC pointer values are rejected without dereference");
  fsim_acc_handle_context_leave_v3(&first_context);

  auto limited_context = context(second, 10, second_root.value, 1);
  require(fsim_acc_handle_context_enter_v3(&limited_context) == 1 &&
              fsim_acc_handle_from_vpi_v3(second_root.value) != nullptr &&
              fsim_acc_handle_from_vpi_v3(second_net.value) == nullptr &&
              acc_error_flag == 1,
          "mapping rejects a second valid VPI object at the context limit");
  fsim_acc_handle_context_leave_v3(&limited_context);
  return 0;
}
