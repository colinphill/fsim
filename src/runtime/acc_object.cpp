// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_user.h"

#include "acc_internal.hpp"
#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using TypePredicate = bool (*)(PLI_INT32);

void publish_error(const bool failed) noexcept {
  acc_error_flag = failed ? 1 : 0;
}

[[nodiscard]] const fsim_acc_handle_context_v3* context() noexcept {
  return fsim::runtime::acc_detail::current_context();
}

[[nodiscard]] bool copy_name(const PLI_BYTE8* const name,
                             std::string& result) noexcept {
  if (name == nullptr) return false;
  const auto base = reinterpret_cast<std::uintptr_t>(name);
  std::uint32_t checked{};
  try {
    while (checked <= FSIM_ACC_NAME_MAXIMUM_BYTES) {
      const auto remaining = FSIM_ACC_NAME_MAXIMUM_BYTES + 1U - checked;
      const auto step = std::min(UINT32_C(64), remaining);
      if (checked > std::numeric_limits<std::uintptr_t>::max() - base) {
        return false;
      }
      const auto* const window = reinterpret_cast<const PLI_BYTE8*>(
          base + static_cast<std::uintptr_t>(checked));
      if (fsim::runtime::validate_tf_native_pointer(
              window, step,
              fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None) {
        return false;
      }
      for (std::uint32_t index = 0; index < step; ++index) {
        const auto byte = static_cast<unsigned char>(window[index]);
        if (byte == 0) {
          if (result.empty() && index == 0) return false;
          result.append(window, index);
          return true;
        }
        if (byte < 0x20U || byte == 0x7fU) return false;
      }
      result.append(window, step);
      checked += step;
    }
  } catch (...) {
  }
  return false;
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

[[nodiscard]] bool resolve_handle(const handle object, std::uint64_t& vpi,
                                  PLI_INT32& type) noexcept {
  vpi = fsim_acc_handle_to_vpi_v3(object);
  return vpi != 0 && resolve_type(vpi, type);
}

[[nodiscard]] bool module_type(const PLI_INT32 type) noexcept {
  return type == accModule || type == accTopModule ||
         type == accModuleInstance || type == accCellInstance;
}

[[nodiscard]] bool net_type(const PLI_INT32 type) noexcept {
  return type == accNet || type == accNetBit ||
         (type >= accWire && type <= accSupply1);
}

[[nodiscard]] bool port_type(const PLI_INT32 type) noexcept {
  return type == accPort || type == accPortBit ||
         (type >= accScalarPort && type <= accConcatPort);
}

[[nodiscard]] bool scalar_port_reference(const PLI_INT32 type) noexcept {
  return type == accPortBit || type == accScalarPort ||
         type == accBitSelectPort;
}

[[nodiscard]] bool primitive_type(const PLI_INT32 type) noexcept {
  return type == accPrimitive || type == accCombPrim || type == accSeqPrim ||
         (type >= accAndGate && type <= accPulldownGate);
}

[[nodiscard]] bool terminal_type(const PLI_INT32 type) noexcept {
  return type == accTerminal ||
         (type >= accInputTerminal && type <= accInoutTerminal) ||
         type == accPathTerminal || type == accPathInput ||
         type == accPathOutput || type == accTchkTerminal;
}

[[nodiscard]] bool timing_check_type(const PLI_INT32 type) noexcept {
  return type == accTchk || type == accSetup || type == accHold ||
         type == accWidth || type == accPeriod || type == accRecovery ||
         type == accSkew || type == accNochange || type == accSetuphold;
}

[[nodiscard]] bool timing_check_selector(const PLI_INT32 type) noexcept {
  return type == accSetup || type == accHold || type == accWidth ||
         type == accPeriod || type == accRecovery || type == accSkew ||
         type == accNochange || type == accSetuphold;
}

[[nodiscard]] bool expression_type(const PLI_INT32 type) noexcept {
  return net_type(type) || type == accReg || type == accRegBit ||
         type == accIntegerVar || type == accRealVar || type == accTimeVar ||
         type == accNamedEvent || type == accBitSelect ||
         type == accPartSelect || type == accConstant || type == accConcat ||
         type == accOperator || type == accMinTypMax;
}

[[nodiscard]] bool module_path_type(const PLI_INT32 type) noexcept {
  return type == accModPath;
}

[[nodiscard]] bool data_path_type(const PLI_INT32 type) noexcept {
  return type == accDataPath;
}

[[nodiscard]] bool intermodule_path_type(const PLI_INT32 type) noexcept {
  return type == accInterModPath;
}

[[nodiscard]] bool register_type(const PLI_INT32 type) noexcept {
  return type == accReg || type == accRegBit || type == accIntegerVar ||
         type == accTimeVar;
}

[[nodiscard]] bool valid_edge(const PLI_INT32 edge) noexcept {
  return edge >= 0 && (edge & ~PLI_INT32{63}) == 0;
}

[[nodiscard]] handle query_handle(fsim_acc_object_query_v3& query,
                                  const TypePredicate output_type) noexcept {
  const auto* const active = context();
  if (active == nullptr) {
    publish_error(true);
    return nullptr;
  }
  std::uint64_t result{};
  std::uint32_t status{};
  try {
    status = active->object_query(active->user_data, &query, &result);
  } catch (...) {
    publish_error(true);
    return nullptr;
  }
  if (status == FSIM_ACC_VPI_OBJECT_NOT_FOUND) {
    publish_error(false);
    return nullptr;
  }
  PLI_INT32 type{};
  if (status != FSIM_ACC_VPI_OBJECT_VALID || result == 0 ||
      !resolve_type(result, type) || !output_type(type)) {
    publish_error(true);
    return nullptr;
  }
  return fsim_acc_handle_from_vpi_v3(result);
}

[[nodiscard]] fsim_acc_object_query_v3 base_query(
    const std::uint32_t operation) noexcept {
  fsim_acc_object_query_v3 query{};
  query.abi_version = FSIM_ACC_OBJECT_QUERY_ABI_VERSION;
  query.struct_size = sizeof(query);
  query.operation = operation;
  return query;
}

[[nodiscard]] handle unary_query(const handle object,
                                 const std::uint32_t operation,
                                 const TypePredicate input_type,
                                 const TypePredicate output_type) noexcept {
  std::uint64_t input{};
  PLI_INT32 type{};
  if (!resolve_handle(object, input, type) || !input_type(type)) {
    publish_error(true);
    return nullptr;
  }
  auto query = base_query(operation);
  query.first = input;
  return query_handle(query, output_type);
}

[[nodiscard]] bool set_query_name(const PLI_BYTE8* const input,
                                  std::string& owned,
                                  const PLI_BYTE8*& output,
                                  std::uint32_t& size) noexcept {
  if (!copy_name(input, owned)) return false;
  output = owned.data();
  size = static_cast<std::uint32_t>(owned.size());
  return true;
}

}  // namespace

extern "C" {

handle acc_handle_condition(const handle object) {
  return unary_query(object, FSIM_ACC_OBJECT_CONDITION,
                     [](const PLI_INT32 type) {
                       return module_path_type(type) ||
                              data_path_type(type) ||
                              type == accTchkTerminal;
                     },
                     expression_type);
}

handle acc_handle_conn(const handle terminal) {
  return unary_query(terminal, FSIM_ACC_OBJECT_CONNECTION, terminal_type,
                     net_type);
}

handle acc_handle_datapath(const handle path) {
  return unary_query(path, FSIM_ACC_OBJECT_DATAPATH, module_path_type,
                     data_path_type);
}

handle acc_handle_hiconn(const handle port) {
  return unary_query(port, FSIM_ACC_OBJECT_HICONN, scalar_port_reference,
                     net_type);
}

handle acc_handle_loconn(const handle port) {
  return unary_query(port, FSIM_ACC_OBJECT_LOCONN, scalar_port_reference,
                     net_type);
}

handle acc_handle_modpath(const handle module, PLI_BYTE8* const input,
                          PLI_BYTE8* const output, ...) {
  std::uint64_t owner{};
  PLI_INT32 owner_type{};
  if (!resolve_handle(module, owner, owner_type) || !module_type(owner_type)) {
    publish_error(true);
    return nullptr;
  }
  auto query = base_query(FSIM_ACC_OBJECT_MODULE_PATH);
  query.owner = owner;
  std::string input_name;
  std::string output_name;
  const bool extended = fsim::runtime::acc_detail::configuration_enabled(
      accEnableArgs, "acc_handle_modpath");
  if (input != nullptr &&
      !set_query_name(input, input_name, query.first_name,
                      query.first_name_size)) {
    publish_error(true);
    return nullptr;
  }
  if (output != nullptr &&
      !set_query_name(output, output_name, query.second_name,
                      query.second_name_size)) {
    publish_error(true);
    return nullptr;
  }
  if (!extended && (input == nullptr || output == nullptr)) {
    publish_error(true);
    return nullptr;
  }
  if (extended && (input == nullptr || output == nullptr)) {
    std::va_list arguments;
    va_start(arguments, output);
    const auto input_handle = va_arg(arguments, handle);
    const auto output_handle = va_arg(arguments, handle);
    va_end(arguments);
    PLI_INT32 type{};
    if ((input == nullptr &&
         (!resolve_handle(input_handle, query.first, type) || !net_type(type))) ||
        (output == nullptr &&
         (!resolve_handle(output_handle, query.second, type) ||
          !net_type(type)))) {
      publish_error(true);
      return nullptr;
    }
  }
  return query_handle(query, module_path_type);
}

handle acc_handle_notifier(const handle timing_check) {
  return unary_query(timing_check, FSIM_ACC_OBJECT_NOTIFIER,
                     timing_check_type, register_type);
}

handle acc_handle_path(const handle source, const handle destination) {
  auto query = base_query(FSIM_ACC_OBJECT_INTERMODULE_PATH);
  PLI_INT32 type{};
  if (!resolve_handle(source, query.first, type) || !port_type(type) ||
      !resolve_handle(destination, query.second, type) || !port_type(type)) {
    publish_error(true);
    return nullptr;
  }
  return query_handle(query, intermodule_path_type);
}

handle acc_handle_pathin(const handle path) {
  return unary_query(path, FSIM_ACC_OBJECT_PATH_INPUT, module_path_type,
                     net_type);
}

handle acc_handle_pathout(const handle path) {
  return unary_query(path, FSIM_ACC_OBJECT_PATH_OUTPUT, module_path_type,
                     net_type);
}

handle acc_handle_port(const handle module, const PLI_INT32 index, ...) {
  auto query = base_query(FSIM_ACC_OBJECT_PORT_INDEX);
  PLI_INT32 type{};
  if (index < 0 || !resolve_handle(module, query.owner, type) ||
      !module_type(type)) {
    publish_error(true);
    return nullptr;
  }
  query.index = index;
  return query_handle(query, port_type);
}

handle acc_handle_tchk(const handle module, const PLI_INT32 type,
                       PLI_BYTE8* const argument, const PLI_INT32 edge, ...) {
  auto query = base_query(FSIM_ACC_OBJECT_TIMING_CHECK);
  PLI_INT32 owner_type{};
  if (!resolve_handle(module, query.owner, owner_type) ||
      !module_type(owner_type) || !timing_check_selector(type) ||
      !valid_edge(edge)) {
    publish_error(true);
    return nullptr;
  }
  query.object_type = type;
  query.first_edge = edge;
  std::string first_name;
  std::string second_name;
  if (argument != nullptr &&
      !set_query_name(argument, first_name, query.first_name,
                      query.first_name_size)) {
    publish_error(true);
    return nullptr;
  }
  const bool two_arguments = type != accWidth && type != accPeriod;
  const bool extended = fsim::runtime::acc_detail::configuration_enabled(
      accEnableArgs, "acc_handle_tchk");
  std::va_list arguments;
  va_start(arguments, edge);
  PLI_BYTE8* second_argument{};
  if (two_arguments) {
    second_argument = va_arg(arguments, PLI_BYTE8*);
    query.second_edge = va_arg(arguments, PLI_INT32);
    if (!valid_edge(query.second_edge) ||
        (second_argument != nullptr &&
         !set_query_name(second_argument, second_name, query.second_name,
                         query.second_name_size))) {
      va_end(arguments);
      publish_error(true);
      return nullptr;
    }
  }
  if ((!extended && argument == nullptr) ||
      (!extended && two_arguments && second_argument == nullptr)) {
    va_end(arguments);
    publish_error(true);
    return nullptr;
  }
  if (extended &&
      (argument == nullptr || (two_arguments && second_argument == nullptr))) {
    const auto first_handle = va_arg(arguments, handle);
    handle second_handle{};
    if (two_arguments) second_handle = va_arg(arguments, handle);
    PLI_INT32 connection_type{};
    if ((argument == nullptr &&
         (!resolve_handle(first_handle, query.first, connection_type) ||
          !net_type(connection_type))) ||
        (two_arguments && second_argument == nullptr &&
         (!resolve_handle(second_handle, query.second, connection_type) ||
          !net_type(connection_type)))) {
      va_end(arguments);
      publish_error(true);
      return nullptr;
    }
  }
  va_end(arguments);
  return query_handle(query, timing_check_type);
}

handle acc_handle_tchkarg1(const handle timing_check) {
  return unary_query(timing_check, FSIM_ACC_OBJECT_TIMING_CHECK_ARG1,
                     timing_check_type,
                     [](const PLI_INT32 type) {
                       return type == accTchkTerminal;
                     });
}

handle acc_handle_tchkarg2(const handle timing_check) {
  return unary_query(timing_check, FSIM_ACC_OBJECT_TIMING_CHECK_ARG2,
                     timing_check_type,
                     [](const PLI_INT32 type) {
                       return type == accTchkTerminal;
                     });
}

handle acc_handle_terminal(const handle primitive, const PLI_INT32 index) {
  auto query = base_query(FSIM_ACC_OBJECT_TERMINAL_INDEX);
  PLI_INT32 type{};
  if (index < 0 || !resolve_handle(primitive, query.owner, type) ||
      !primitive_type(type)) {
    publish_error(true);
    return nullptr;
  }
  query.index = index;
  return query_handle(query, terminal_type);
}

}  // extern "C"
