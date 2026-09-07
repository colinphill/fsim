// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <ranges>
#include <string>

namespace {

using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;
using Kind = fsim::runtime::SystemVerilogVpiObjectKind;

struct Fixture {
  Registry registry{59};
  std::map<std::uint64_t, PLI_INT32> types;
  std::uint64_t path{};
  std::uint64_t condition{};
  std::array<double, 2> delays{0.25, 1.5};
  std::array<fsim_acc_logic_word_v3, 1> words{};
  std::string name;
  std::string full_name;
};

void require(const bool condition, const char* const message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::uint32_t status(const fsim::runtime::SystemVerilogVpiObjectError error) {
  using Error = fsim::runtime::SystemVerilogVpiObjectError;
  if (error == Error::ReleasedHandle) return FSIM_ACC_VPI_OBJECT_RELEASED;
  if (error == Error::StaleHandle) return FSIM_ACC_VPI_OBJECT_STALE;
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  const auto found = fixture.registry.lookup(object);
  if (!found) return status(found.error);
  const auto mapped = fixture.types.find(object);
  if (mapped == fixture.types.end()) return FSIM_ACC_VPI_OBJECT_WRONG_TYPE;
  *type = mapped->second;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL lookup_object(
    void* const user_data, const std::uint32_t, const std::uint64_t,
    const PLI_BYTE8* const name, const std::uint32_t size,
    std::uint64_t* const object) {
  const auto found = static_cast<Fixture*>(user_data)->registry.find(
      std::string_view{name, size});
  if (!found) return status(found.error);
  *object = found.value->handle;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL related_object(
    void* const user_data, const std::uint32_t relation,
    const std::uint64_t object, std::uint64_t* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  const auto found = fixture.registry.lookup(object);
  if (!found) return status(found.error);
  if ((relation != FSIM_ACC_RELATION_PARENT &&
       relation != FSIM_ACC_RELATION_SCOPE) ||
      found.value->parent == 0) {
    return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  }
  *result = found.value->parent;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL object_name(
    void* const user_data, const std::uint64_t object,
    PLI_BYTE8* const buffer, const std::uint32_t capacity,
    std::uint32_t* const size) {
  const auto found = static_cast<Fixture*>(user_data)->registry.lookup(object);
  if (!found) return status(found.error);
  if (found.value->full_name.size() >= capacity) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  std::ranges::copy(found.value->full_name, buffer);
  *size = static_cast<std::uint32_t>(found.value->full_name.size());
  buffer[*size] = 0;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL object_query(
    void* const user_data, const fsim_acc_object_query_v3* const query,
    std::uint64_t* const result) {
  const auto& fixture = *static_cast<Fixture*>(user_data);
  if (query->abi_version != FSIM_ACC_OBJECT_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->operation != FSIM_ACC_OBJECT_CONDITION ||
      query->first != fixture.path) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  *result = fixture.condition;
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL read_object(
    void* const user_data, const fsim_acc_read_query_v3* const query,
    fsim_acc_read_result_v3* const result) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  if (query->abi_version != FSIM_ACC_READ_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->operation != FSIM_ACC_READ_OBJECT) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  const auto found = fixture.registry.lookup(query->object);
  if (!found) return status(found.error);
  const auto mapped = fixture.types.find(query->object);
  if (mapped == fixture.types.end()) return FSIM_ACC_VPI_OBJECT_WRONG_TYPE;

  fixture.name = found.value->name;
  fixture.full_name = found.value->full_name;
  result->abi_version = FSIM_ACC_READ_QUERY_ABI_VERSION;
  result->struct_size = sizeof(*result);
  result->type = mapped->second;
  result->full_type = mapped->second;
  result->name = fixture.name.data();
  result->name_size = static_cast<std::uint32_t>(fixture.name.size());
  result->full_name = fixture.full_name.data();
  result->full_name_size =
      static_cast<std::uint32_t>(fixture.full_name.size());
  if (query->object == fixture.path) {
    result->timing_capabilities = FSIM_ACC_TIMING_CAP_DELAYS;
    result->timing_delay_count = 2;
    return FSIM_ACC_VPI_OBJECT_VALID;
  }
  if (!found.value->type.has_value()) return FSIM_ACC_VPI_OBJECT_INVALID;
  const auto direct = fixture.registry.read_value(
      query->object, fsim::runtime::SystemVerilogVpiValueFormat::Integer);
  if (!direct) return FSIM_ACC_VPI_OBJECT_INVALID;
  result->width = static_cast<PLI_INT32>(found.value->type->width);
  result->has_range = 1;
  result->msb = result->width - 1;
  result->lsb = 0;
  result->value_kind = FSIM_ACC_READ_VALUE_LOGIC4;
  result->logic_word_count = 1;
  fixture.words[0] = {static_cast<std::uint32_t>(direct.integer), 0};
  result->logic_words = fixture.words.data();
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL timing_object(
    void* const user_data, const fsim_acc_timing_query_v3* const query,
    fsim_acc_timing_result_v3* const result) {
  const auto& fixture = *static_cast<Fixture*>(user_data);
  if (query->abi_version != FSIM_ACC_TIMING_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0 ||
      query->operation != FSIM_ACC_TIMING_FETCH_DELAYS ||
      query->object != fixture.path || query->delay_count != 2) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  result->abi_version = FSIM_ACC_TIMING_QUERY_ABI_VERSION;
  result->struct_size = sizeof(*result);
  result->value_count = static_cast<std::uint32_t>(fixture.delays.size());
  result->values = fixture.delays.data();
  return FSIM_ACC_VPI_OBJECT_VALID;
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_traverse(
    void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint64_t*,
    std::uint64_t*) {
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
std::uint32_t FSIM_NATIVE_PLUGIN_CALL unsupported_vcl(
    void*, const fsim_acc_vcl_query_v3*) {
  return FSIM_ACC_VPI_OBJECT_INVALID;
}

fsim_acc_handle_context_v3 context(Fixture& fixture,
                                   const std::uint64_t module) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          17,
          128,
          0,
          &fixture,
          resolve_object,
          module,
          module,
          module,
          lookup_object,
          related_object,
          object_name,
          unsupported_traverse,
          object_query,
          read_object,
          unsupported_write,
          unsupported_iterate,
          timing_object,
          unsupported_vcl,
          nullptr};
}

fsim::runtime::SystemVerilogVpiObjectResult add_expression(
    Fixture& fixture, const Kind kind, const std::uint64_t parent,
    const char* const name, const PLI_INT32 acc_type,
    const char* const bits) {
  fsim::runtime::SystemVerilogVpiTypeInfo type;
  type.category = fsim::runtime::SystemVerilogVpiValueCategory::Logic4;
  type.width = 8;
  type.is_constant = kind == Kind::Constant;
  const auto object = fixture.registry.create(
      {.kind = kind,
       .parent = parent,
       .name = name,
       .source = std::nullopt,
       .type = type});
  if (object) {
    fixture.types.emplace(object.value, acc_type);
    const auto value = fsim::runtime::SystemVerilogVpiStoredValue{
        fsim::runtime::PackedLogic4::from_msb_string(bits), std::nullopt};
    require(fixture.registry.bind_value(object.value, value) ==
                fsim::runtime::SystemVerilogVpiValueError::None,
            "VPI expression stores one shared value");
  }
  return object;
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root = fixture.registry.create(Kind::Root, 0, "top");
  const auto module = fixture.registry.create(Kind::Module, root.value, "dut");
  require(root && module, "VPI coherence fixture creates hierarchy");
  fixture.types.emplace(root.value, accTopModule);
  fixture.types.emplace(module.value, accModuleInstance);
  const auto constant = add_expression(
      fixture, Kind::Constant, module.value, "literal", accConstant,
      "00101010");
  const auto concat = add_expression(
      fixture, Kind::Concatenation, module.value, "concat", accConcat,
      "00110011");
  const auto operation = add_expression(
      fixture, Kind::Operation, module.value, "sum", accOperator,
      "01010101");
  const auto min_typ_max = add_expression(
      fixture, Kind::MinTypMax, module.value, "delay_expr", accMinTypMax,
      "01100110");
  const auto path = fixture.registry.create(Kind::Process, module.value, "path");
  require(constant && concat && operation && min_typ_max && path,
          "VPI registry publishes every remaining ACC expression kind");
  fixture.path = path.value;
  fixture.condition = operation.value;
  fixture.types.emplace(path.value, accModPath);

  require(acc_initialize() == 1, "ACC coherence fixture initializes");
  require(acc_configure(accPathDelayCount, const_cast<char*>("2")) == 1,
          "ACC coherence fixture selects exact path arity");
  auto active = context(fixture, module.value);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "ACC coherence context enters one VPI simulation");

  const std::array objects{constant.value, concat.value, operation.value,
                           min_typ_max.value};
  const std::array types{accConstant, accConcat, accOperator, accMinTypMax};
  std::array<handle, 4> handles{};
  for (std::size_t index = 0; index < objects.size(); ++index) {
    handles[index] = fsim_acc_handle_from_vpi_v3(objects[index]);
    const auto direct = fixture.registry.lookup(objects[index]);
    require(handles[index] != nullptr && direct &&
                fsim_acc_vpi_same_object_v3(handles[index], objects[index]) ==
                    1 &&
                acc_object_of_type(handles[index], types[index]) == 1 &&
                direct.value->parent == module.value,
            "ACC expression handle retains exact live VPI identity and type");
  }

  s_acc_value value{};
  value.format = accIntVal;
  require(acc_fetch_value(handles[0], const_cast<char*>("%%"), &value) ==
                  nullptr &&
              value.value.integer == 42,
          "ACC value reads the same storage as the VPI registry");
  const auto direct_value = fixture.registry.read_value(
      constant.value, fsim::runtime::SystemVerilogVpiValueFormat::Integer);
  require(direct_value && direct_value.integer == 42,
          "direct VPI value agrees with the ACC view");

  const auto parent = acc_handle_parent(handles[2]);
  require(fsim_acc_vpi_same_object_v3(parent, module.value) == 1 &&
              std::strcmp(acc_fetch_fullname(handles[2]), "top.dut.sum") == 0,
          "ACC hierarchy resolves the same VPI parent and full name");
  const auto path_handle = fsim_acc_handle_from_vpi_v3(path.value);
  const auto condition = acc_handle_condition(path_handle);
  require(fsim_acc_vpi_same_object_v3(condition, operation.value) == 1,
          "ACC connectivity returns the exact VPI expression identity");
  double first{};
  double second{};
  require(acc_fetch_delays(path_handle, &first, &second) == 1 &&
              first == fixture.delays[0] && second == fixture.delays[1],
          "ACC timing reads the same VPI-keyed path record");

  require(fsim_acc_vpi_same_object_v3(handles[1], constant.value) == 0 &&
              acc_error_flag == 1,
          "different live VPI identities cannot compare equal");
  require(fixture.registry.release(constant.value) ==
                  fsim::runtime::SystemVerilogVpiObjectError::None &&
              fsim_acc_vpi_same_object_v3(handles[0], constant.value) == 0 &&
              acc_error_flag == 1 &&
              fixture.registry.lookup(constant.value).error ==
                  fsim::runtime::SystemVerilogVpiObjectError::ReleasedHandle,
          "ACC and VPI reject the same released generation");

  fsim_acc_handle_context_leave_v3(&active);
  acc_close();
  return 0;
}
