// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"
#include "fsim/runtime/vpi_object.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using Kind = fsim::runtime::SystemVerilogVpiObjectKind;
using Registry = fsim::runtime::SystemVerilogVpiObjectRegistry;

struct Fixture {
  Registry registry{73};
  std::map<std::uint64_t, PLI_INT32> types;
  std::map<PLI_INT32, std::uint64_t> first_by_type;
  std::uint64_t root{};
  std::uint64_t scalar_port{};
  std::uint64_t source_net{};
  std::uint64_t destination_net{};
  std::uint64_t primitive{};
  std::uint64_t terminal{};
  std::uint64_t module_path{};
  std::uint64_t data_path{};
  std::uint64_t condition{};
  std::uint64_t timing_check{};
  std::uint64_t timing_arg1{};
  std::uint64_t timing_arg2{};
  std::uint64_t notifier{};
  std::uint64_t intermodule_path{};
  bool not_found{};
  bool throw_query{};
  std::uint32_t query_calls{};
};

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error(message);
}

[[nodiscard]] std::string_view query_name(const PLI_BYTE8* const name,
                                          const std::uint32_t size) {
  if (name == nullptr) return {};
  return {name, size};
}

std::uint32_t FSIM_NATIVE_PLUGIN_CALL resolve_object(
    void* const user_data, const std::uint64_t object,
    PLI_INT32* const type) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  const auto view = fixture.registry.lookup(object);
  const auto found = fixture.types.find(object);
  if (!view || found == fixture.types.end() || type == nullptr) {
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

std::uint32_t FSIM_NATIVE_PLUGIN_CALL object_query(
    void* const user_data, const fsim_acc_object_query_v3* const query,
    std::uint64_t* const object) {
  auto& fixture = *static_cast<Fixture*>(user_data);
  ++fixture.query_calls;
  if (fixture.throw_query) throw std::runtime_error("query failure");
  if (fixture.not_found) return FSIM_ACC_VPI_OBJECT_NOT_FOUND;
  if (query == nullptr || object == nullptr ||
      query->abi_version != FSIM_ACC_OBJECT_QUERY_ABI_VERSION ||
      query->struct_size < sizeof(*query) || query->reserved != 0) {
    return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  switch (query->operation) {
    case FSIM_ACC_OBJECT_CONDITION:
      if (query->first == fixture.module_path) *object = fixture.condition;
      break;
    case FSIM_ACC_OBJECT_CONNECTION:
      if (query->first == fixture.terminal ||
          query->first == fixture.timing_arg1) {
        *object = fixture.source_net;
      } else if (query->first == fixture.timing_arg2) {
        *object = fixture.destination_net;
      }
      break;
    case FSIM_ACC_OBJECT_DATAPATH:
      if (query->first == fixture.module_path) *object = fixture.data_path;
      break;
    case FSIM_ACC_OBJECT_HICONN:
      if (query->first == fixture.scalar_port) {
        *object = fixture.source_net;
      }
      break;
    case FSIM_ACC_OBJECT_LOCONN:
      if (query->first == fixture.scalar_port) {
        *object = fixture.destination_net;
      }
      break;
    case FSIM_ACC_OBJECT_MODULE_PATH:
      if (query->owner == fixture.root &&
          ((query_name(query->first_name, query->first_name_size) == "src" &&
            query_name(query->second_name, query->second_name_size) == "dst") ||
           (query->first == fixture.source_net &&
            query->second == fixture.destination_net))) {
        *object = fixture.module_path;
      }
      break;
    case FSIM_ACC_OBJECT_NOTIFIER:
      if (query->first == fixture.timing_check) *object = fixture.notifier;
      break;
    case FSIM_ACC_OBJECT_INTERMODULE_PATH:
      if (query->first == fixture.scalar_port &&
          query->second == fixture.scalar_port) {
        *object = fixture.intermodule_path;
      }
      break;
    case FSIM_ACC_OBJECT_PATH_INPUT:
      if (query->first == fixture.module_path) *object = fixture.source_net;
      break;
    case FSIM_ACC_OBJECT_PATH_OUTPUT:
      if (query->first == fixture.module_path) {
        *object = fixture.destination_net;
      }
      break;
    case FSIM_ACC_OBJECT_PORT_INDEX:
      if (query->owner == fixture.root && query->index == 0) {
        *object = fixture.scalar_port;
      }
      break;
    case FSIM_ACC_OBJECT_TIMING_CHECK:
      if (query->owner == fixture.root && query->object_type == accSetup &&
          query->first_edge == accNoedge &&
          query->second_edge == accPosedge &&
          query_name(query->first_name, query->first_name_size) == "data" &&
          query_name(query->second_name, query->second_name_size) == "clock") {
        *object = fixture.timing_check;
      } else if (query->owner == fixture.root &&
                 query->object_type == accWidth &&
                 query->first_edge == accPosedge &&
                 query->first == fixture.source_net) {
        *object = fixture.timing_check;
      }
      break;
    case FSIM_ACC_OBJECT_TIMING_CHECK_ARG1:
      if (query->first == fixture.timing_check) *object = fixture.timing_arg1;
      break;
    case FSIM_ACC_OBJECT_TIMING_CHECK_ARG2:
      if (query->first == fixture.timing_check) *object = fixture.timing_arg2;
      break;
    case FSIM_ACC_OBJECT_TERMINAL_INDEX:
      if (query->owner == fixture.primitive && query->index == 0) {
        *object = fixture.terminal;
      }
      break;
    default:
      return FSIM_ACC_VPI_OBJECT_INVALID;
  }
  return *object == 0 ? FSIM_ACC_VPI_OBJECT_NOT_FOUND
                      : FSIM_ACC_VPI_OBJECT_VALID;
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

[[nodiscard]] fsim_acc_handle_context_v3 context(Fixture& fixture) {
  return {FSIM_ACC_HANDLE_CONTEXT_ABI_VERSION,
          sizeof(fsim_acc_handle_context_v3),
          fixture.registry.simulation_identity(),
          17,
          512,
          0,
          &fixture,
          resolve_object,
          fixture.root,
          fixture.root,
          fixture.root,
          unsupported_lookup,
          unsupported_relation,
          unsupported_name,
          unsupported_traverse,
          object_query,
          unsupported_read,
          unsupported_write,
          unsupported_iterate,
          unsupported_timing,
          unsupported_vcl,
          nullptr};
}

[[nodiscard]] std::uint64_t add_object(Fixture& fixture,
                                       const PLI_INT32 type,
                                       const std::string& name) {
  const auto created = fixture.registry.create(Kind::Variable, fixture.root,
                                               name);
  require(static_cast<bool>(created), "object fixture creates VPI identity");
  fixture.types.emplace(created.value, type);
  fixture.first_by_type.try_emplace(type, created.value);
  return created.value;
}

[[nodiscard]] handle acc(Fixture&, const std::uint64_t object) {
  auto result = fsim_acc_handle_from_vpi_v3(object);
  require(result != nullptr, "object fixture maps VPI identity into ACC");
  return result;
}

[[nodiscard]] bool maps_to(const handle object,
                           const std::uint64_t expected) {
  return fsim_acc_handle_to_vpi_v3(object) == expected;
}

}  // namespace

int main() {
  Fixture fixture;
  const auto root = fixture.registry.create(Kind::Root, 0, "top");
  require(static_cast<bool>(root), "object fixture creates root");
  fixture.root = root.value;
  fixture.types.emplace(root.value, accModule);

  constexpr std::array complete_types{
      accNet,          accReg,           accPort,
      accTerminal,     accInputTerminal, accOutputTerminal,
      accInoutTerminal, accCombPrim,     accSeqPrim,
      accAndGate,      accNandGate,      accNorGate,
      accOrGate,       accXorGate,       accXnorGate,
      accBufGate,      accNotGate,       accBufif0Gate,
      accBufif1Gate,   accNotif0Gate,    accNotif1Gate,
      accNmosGate,     accPmosGate,      accCmosGate,
      accRnmosGate,    accRpmosGate,     accRcmosGate,
      accRtranGate,    accRtranif0Gate,  accRtranif1Gate,
      accTranGate,     accTranif0Gate,   accTranif1Gate,
      accPullupGate,   accPulldownGate,  accIntegerParam,
      accRealParam,    accStringParam,   accTchk,
      accPrimitive,    accParameter,     accSpecparam,
      accModPath,      accInterModPath,  accScalarPort,
      accBitSelectPort, accPartSelectPort, accVectorPort,
      accConcatPort,   accWire,          accWand,
      accWor,          accTri,           accTriand,
      accTrior,        accTri0,          accTri1,
      accTrireg,       accSupply0,       accSupply1,
      accNamedEvent,   accIntegerVar,    accRealVar,
      accTimeVar};
  std::uint32_t ordinal{};
  for (const auto type : complete_types) {
    const auto object = add_object(
        fixture, type, "object_" + std::to_string(ordinal++));
    fixture.first_by_type.try_emplace(type, object);
  }
  fixture.scalar_port = fixture.first_by_type.at(accScalarPort);
  fixture.source_net = fixture.first_by_type.at(accNet);
  fixture.destination_net = fixture.first_by_type.at(accWire);
  fixture.primitive = fixture.first_by_type.at(accPrimitive);
  fixture.terminal = fixture.first_by_type.at(accOutputTerminal);
  fixture.module_path = fixture.first_by_type.at(accModPath);
  fixture.condition = fixture.first_by_type.at(accNamedEvent);
  fixture.timing_check = fixture.first_by_type.at(accTchk);
  fixture.notifier = fixture.first_by_type.at(accReg);
  fixture.intermodule_path = fixture.first_by_type.at(accInterModPath);
  fixture.data_path = add_object(fixture, accDataPath, "data_path");
  fixture.timing_arg1 = add_object(fixture, accTchkTerminal, "tchk_arg1");
  fixture.timing_arg2 = add_object(fixture, accTchkTerminal, "tchk_arg2");

  auto active = context(fixture);
  require(fsim_acc_handle_context_enter_v3(&active) == 1,
          "object fixture enters its exact v3 context");
  require(acc_initialize() == 1, "object fixture initializes ACC lifecycle");

  for (const auto [object, type] : fixture.types) {
    auto mapped = acc(fixture, object);
    require(acc_object_of_type(mapped, type) == 1 && acc_error_flag == 0,
            "every complete object-model subtype preserves exact identity");
  }
  require(acc_object_of_type(
              acc(fixture, fixture.first_by_type.at(accWire)), accNet) == 1 &&
              acc_object_of_type(
                  acc(fixture, fixture.first_by_type.at(accAndGate)),
                  accPrimitive) == 1 &&
              acc_object_of_type(
                  acc(fixture, fixture.first_by_type.at(accIntegerParam)),
                  accParameter) == 1 &&
              acc_object_of_type(
                  acc(fixture, fixture.first_by_type.at(accVectorPort)),
                  accPort) == 1 &&
              acc_object_of_type(
                  acc(fixture, fixture.first_by_type.at(accInputTerminal)),
                  accTerminal) == 1 &&
              acc_object_of_type(acc(fixture, fixture.root), accScope) == 1,
          "full types preserve standardized generic object membership");

  auto root_handle = acc(fixture, fixture.root);
  auto port = acc(fixture, fixture.scalar_port);
  auto source_net = acc(fixture, fixture.source_net);
  auto destination_net = acc(fixture, fixture.destination_net);
  auto primitive = acc(fixture, fixture.primitive);
  auto terminal = acc(fixture, fixture.terminal);
  auto module_path = acc(fixture, fixture.module_path);
  static_cast<void>(acc(fixture, fixture.timing_check));

  require(maps_to(acc_handle_condition(module_path), fixture.condition) &&
              maps_to(acc_handle_conn(terminal), fixture.source_net) &&
              maps_to(acc_handle_datapath(module_path), fixture.data_path) &&
              maps_to(acc_handle_hiconn(port), fixture.source_net) &&
              maps_to(acc_handle_loconn(port), fixture.destination_net),
          "unary object relations preserve exact connectivity identity");
  require(maps_to(acc_handle_modpath(root_handle, const_cast<char*>("src"),
                                      const_cast<char*>("dst")),
                  fixture.module_path) &&
              maps_to(acc_handle_path(port, port), fixture.intermodule_path) &&
              maps_to(acc_handle_pathin(module_path), fixture.source_net) &&
              maps_to(acc_handle_pathout(module_path),
                      fixture.destination_net) &&
              maps_to(acc_handle_port(root_handle, 0), fixture.scalar_port),
          "path and indexed-port queries retain endpoint ownership");
  auto setup = acc_handle_tchk(root_handle, accSetup,
                               const_cast<char*>("data"), accNoedge,
                               const_cast<char*>("clock"), accPosedge);
  require(maps_to(setup, fixture.timing_check) &&
              maps_to(acc_handle_notifier(setup), fixture.notifier) &&
              maps_to(acc_handle_tchkarg1(setup), fixture.timing_arg1) &&
              maps_to(acc_handle_tchkarg2(setup), fixture.timing_arg2) &&
              maps_to(acc_handle_conn(acc_handle_tchkarg1(setup)),
                      fixture.source_net) &&
              maps_to(acc_handle_terminal(primitive, 0), fixture.terminal),
          "timing checks and primitive terminals preserve related objects");

  require(acc_configure(accEnableArgs,
                        const_cast<char*>("acc_handle_modpath")) == 1 &&
              maps_to(acc_handle_modpath(root_handle, nullptr, nullptr,
                                          source_net, destination_net),
                      fixture.module_path),
          "enabled module-path arguments select validated connection handles");
  require(acc_configure(accEnableArgs,
                        const_cast<char*>("acc_handle_tchk")) == 1 &&
              maps_to(acc_handle_tchk(root_handle, accWidth, nullptr,
                                      accPosedge, source_net),
                      fixture.timing_check),
          "enabled timing-check arguments select a validated connection handle");

  const auto calls_before_rejection = fixture.query_calls;
  require(acc_handle_port(root_handle, -1) == nullptr && acc_error_flag == 1 &&
              acc_handle_terminal(source_net, 0) == nullptr &&
              acc_error_flag == 1 &&
              acc_handle_modpath(
                  root_handle,
                  reinterpret_cast<PLI_BYTE8*>(static_cast<std::uintptr_t>(1)),
                  const_cast<char*>("dst")) == nullptr &&
              acc_error_flag == 1 &&
              acc_handle_tchk(root_handle, accSetup,
                              const_cast<char*>("data"), 64,
                              const_cast<char*>("clock"), accPosedge) ==
                  nullptr &&
              acc_error_flag == 1 &&
              acc_handle_tchk(root_handle, accTchk,
                              const_cast<char*>("data"), accNoedge) ==
                  nullptr &&
              acc_error_flag == 1 &&
              fixture.query_calls == calls_before_rejection,
          "invalid owners indices names and edges fail before callback dispatch");

  fixture.not_found = true;
  require(acc_handle_condition(module_path) == nullptr && acc_error_flag == 0,
          "a valid query with no matching object is successful exhaustion");
  fixture.not_found = false;
  fixture.throw_query = true;
  require(acc_handle_condition(module_path) == nullptr && acc_error_flag == 1,
          "object callback exceptions are contained at the ACC boundary");
  fixture.throw_query = false;

  acc_close();
  fsim_acc_handle_context_leave_v3(&active);
  active.object_query = nullptr;
  require(fsim_acc_handle_context_enter_v3(&active) == 0 &&
              acc_error_flag == 1,
          "context entry rejects a missing object-model callback");
  return 0;
}
