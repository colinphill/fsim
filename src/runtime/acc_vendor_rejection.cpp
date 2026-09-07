// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"

#include "fsim/runtime/tf_containment.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

using namespace std::literals;

constexpr auto kStandardRoutines = std::to_array<std::string_view>({
    "acc_append_delays"sv,
    "acc_append_pulsere"sv,
    "acc_close"sv,
    "acc_collect"sv,
    "acc_compare_handles"sv,
    "acc_configure"sv,
    "acc_count"sv,
    "acc_fetch_argc"sv,
    "acc_fetch_argv"sv,
    "acc_fetch_attribute"sv,
    "acc_fetch_attribute_int"sv,
    "acc_fetch_attribute_str"sv,
    "acc_fetch_defname"sv,
    "acc_fetch_delay_mode"sv,
    "acc_fetch_delays"sv,
    "acc_fetch_direction"sv,
    "acc_fetch_edge"sv,
    "acc_fetch_fullname"sv,
    "acc_fetch_fulltype"sv,
    "acc_fetch_index"sv,
    "acc_fetch_itfarg"sv,
    "acc_fetch_itfarg_int"sv,
    "acc_fetch_itfarg_str"sv,
    "acc_fetch_location"sv,
    "acc_fetch_name"sv,
    "acc_fetch_paramtype"sv,
    "acc_fetch_paramval"sv,
    "acc_fetch_polarity"sv,
    "acc_fetch_precision"sv,
    "acc_fetch_pulsere"sv,
    "acc_fetch_range"sv,
    "acc_fetch_size"sv,
    "acc_fetch_tfarg"sv,
    "acc_fetch_tfarg_int"sv,
    "acc_fetch_tfarg_str"sv,
    "acc_fetch_timescale_info"sv,
    "acc_fetch_type"sv,
    "acc_fetch_type_str"sv,
    "acc_fetch_value"sv,
    "acc_free"sv,
    "acc_handle_by_name"sv,
    "acc_handle_condition"sv,
    "acc_handle_conn"sv,
    "acc_handle_datapath"sv,
    "acc_handle_hiconn"sv,
    "acc_handle_interactive_scope"sv,
    "acc_handle_itfarg"sv,
    "acc_handle_loconn"sv,
    "acc_handle_modpath"sv,
    "acc_handle_notifier"sv,
    "acc_handle_object"sv,
    "acc_handle_parent"sv,
    "acc_handle_path"sv,
    "acc_handle_pathin"sv,
    "acc_handle_pathout"sv,
    "acc_handle_port"sv,
    "acc_handle_scope"sv,
    "acc_handle_simulated_net"sv,
    "acc_handle_tchk"sv,
    "acc_handle_tchkarg1"sv,
    "acc_handle_tchkarg2"sv,
    "acc_handle_terminal"sv,
    "acc_handle_tfarg"sv,
    "acc_handle_tfinst"sv,
    "acc_initialize"sv,
    "acc_next"sv,
    "acc_next_bit"sv,
    "acc_next_cell"sv,
    "acc_next_cell_load"sv,
    "acc_next_child"sv,
    "acc_next_driver"sv,
    "acc_next_hiconn"sv,
    "acc_next_input"sv,
    "acc_next_load"sv,
    "acc_next_loconn"sv,
    "acc_next_modpath"sv,
    "acc_next_net"sv,
    "acc_next_output"sv,
    "acc_next_parameter"sv,
    "acc_next_port"sv,
    "acc_next_portout"sv,
    "acc_next_primitive"sv,
    "acc_next_scope"sv,
    "acc_next_specparam"sv,
    "acc_next_tchk"sv,
    "acc_next_terminal"sv,
    "acc_next_topmod"sv,
    "acc_object_in_typelist"sv,
    "acc_object_of_type"sv,
    "acc_product_type"sv,
    "acc_product_version"sv,
    "acc_release_object"sv,
    "acc_replace_delays"sv,
    "acc_replace_pulsere"sv,
    "acc_reset_buffer"sv,
    "acc_set_interactive_scope"sv,
    "acc_set_pulsere"sv,
    "acc_set_scope"sv,
    "acc_set_value"sv,
    "acc_vcl_add"sv,
    "acc_vcl_delete"sv,
    "acc_version"sv,
});

static_assert(kStandardRoutines.size() == 102U);
static_assert(std::ranges::is_sorted(kStandardRoutines));

constexpr auto kDiagnosticInvalid = "FSIM-ACC-NAME-001"sv;
constexpr auto kDiagnosticRoutine = "FSIM-ACC-NAME-002"sv;
constexpr auto kDiagnosticObject = "FSIM-ACC-NAME-003"sv;
constexpr auto kDiagnosticBehavior = "FSIM-ACC-NAME-004"sv;
constexpr auto kDiagnosticDispatch = "FSIM-ACC-NAME-005"sv;
constexpr auto kDiagnosticException = "FSIM-ACC-NAME-006"sv;

bool standard_object_type(const PLI_INT32 value) {
  if (value == accModule || value == accScope || value == accNet ||
      value == accReg || value == accPort ||
      value == accTerminal ||
      (value >= accInputTerminal && value <= accInoutTerminal) ||
      value == accCombPrim || value == accSeqPrim ||
      (value >= accAndGate && value <= accPulldownGate && value % 2 == 0) ||
      (value >= accIntegerParam && value <= accStringParam &&
       value % 2 == 0) ||
      (value >= accTchk && value <= accRegBit && value % 2 == 0) ||
      (value >= accParameter && value <= accModPath && value % 2 == 0) ||
      value == accInterModPath ||
      (value >= accScalarPort && value <= accConcatPort && value % 2 == 0) ||
      (value >= accWire && value <= accSupply1) ||
      (value >= accNamedEvent && value <= accTimeVar) ||
      value == accScalar || value == accVector ||
      (value >= accExpandedVector && value <= accProtected) ||
      (value >= accSetup && value <= accSkew) || value == accNochange ||
      value == accSetuphold || value == accInput || value == accOutput ||
      (value >= accInout && value <= accPositive) || value == accNegative ||
      value == accUnknown ||
      (value >= accPathTerminal && value <= accTchkTerminal &&
       value % 2 == 0) || value == accBitSelect || value == accPartSelect ||
      (value >= accTask && value <= accUserRealFunction && value % 2 == 0) ||
      value == accConstant || value == accConcat || value == accOperator ||
      value == accMinTypMax) {
    return true;
  }
  return false;
}

bool one_of(const PLI_INT32 value,
            const std::initializer_list<PLI_INT32> choices) {
  return std::ranges::find(choices, value) != choices.end();
}

bool valid_routine_name(const std::string_view name) {
  return name.starts_with("acc_") &&
         std::ranges::all_of(name, [](const char byte) {
           return byte == '_' || (byte >= 'a' && byte <= 'z') ||
                  (byte >= '0' && byte <= '9');
         });
}

bool standard_behavior(const std::uint32_t kind, const PLI_INT32 value) {
  switch (kind) {
    case FSIM_ACC_STANDARD_CONFIGURATION:
      return one_of(value, {accPathDelayCount, accPathDelimStr,
                            accDisplayErrors, accDefaultAttr0,
                            accToHiZDelay, accEnableArgs, accDisplayWarnings,
                            accDevelopmentVersion, accMapToMipd,
                            accMinTypMaxDelays});
    case FSIM_ACC_STANDARD_EDGE:
      return value >= accNoedge && value <= 63;
    case FSIM_ACC_STANDARD_DELAY_MODE:
      return value >= accDelayModeNone && value <= accDelayModeVeritime;
    case FSIM_ACC_STANDARD_UPDATE:
      return value >= accNoDelay && value <= accDeassignFlag;
    case FSIM_ACC_STANDARD_VALUE_FORMAT:
      return one_of(value, {accBinStrVal, accOctStrVal, accDecStrVal,
                            accHexStrVal, accScalarVal, accIntVal, accRealVal,
                            accStringVal, accVectorVal});
    case FSIM_ACC_STANDARD_VCL_REASON:
      return value >= logic_value_change && value <= realtime_value_change;
    case FSIM_ACC_STANDARD_TIME_TYPE:
      return value >= accTime && value <= accRealTime;
    case FSIM_ACC_STANDARD_PRODUCT_TYPE:
      return value >= accSimulator && value <= accOther;
    default:
      return false;
  }
}

void publish_result(fsim_acc_standard_query_result_v3& result,
                    const std::uint32_t status,
                    const std::string_view diagnostic,
                    const bool dispatched) {
  result = {.abi_version = FSIM_ACC_STANDARD_QUERY_ABI_VERSION,
            .struct_size = sizeof(fsim_acc_standard_query_result_v3),
            .status = status,
            .diagnostic_size =
                static_cast<std::uint32_t>(diagnostic.size()),
            .diagnostic = diagnostic.data(),
            .dispatched = dispatched ? 1U : 0U,
            .reserved = 0U};
  acc_error_flag = status == FSIM_ACC_STANDARD_SUPPORTED ? 0 : 1;
}

std::uint32_t reject(fsim_acc_standard_query_result_v3& result,
                     const std::uint32_t status,
                     const std::string_view diagnostic) {
  publish_result(result, status, diagnostic, false);
  return status;
}

}  // namespace

extern "C" uint32_t FSIM_NATIVE_PLUGIN_CALL
fsim_acc_validate_and_dispatch_standard_v3(
    const fsim_acc_standard_query_v3* const query,
    const fsim_acc_standard_dispatch_v3_fn dispatch, void* const user_data,
    fsim_acc_standard_query_result_v3* const result) {
  if (result == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          result, sizeof(*result),
          fsim::runtime::TfNativePointerAccess::Write) !=
          fsim::runtime::TfContainmentError::None) {
    acc_error_flag = 1;
    return FSIM_ACC_STANDARD_INVALID_QUERY;
  }
  if (query == nullptr ||
      fsim::runtime::validate_tf_native_pointer(
          query, sizeof(*query), fsim::runtime::TfNativePointerAccess::Read) !=
          fsim::runtime::TfContainmentError::None ||
      query->abi_version != FSIM_ACC_STANDARD_QUERY_ABI_VERSION ||
      query->struct_size != sizeof(*query) || query->reserved != 0U) {
    return reject(*result, FSIM_ACC_STANDARD_INVALID_QUERY,
                  kDiagnosticInvalid);
  }

  bool supported{};
  std::uint32_t unsupported_status{};
  std::string_view unsupported_diagnostic;
  if (query->kind == FSIM_ACC_STANDARD_ROUTINE) {
    if (query->name == nullptr || query->name_size == 0U ||
        query->name_size > FSIM_ACC_STANDARD_NAME_MAXIMUM_BYTES ||
        query->value != 0 ||
        fsim::runtime::validate_tf_native_pointer(
            query->name, static_cast<std::size_t>(query->name_size) + 1U,
            fsim::runtime::TfNativePointerAccess::Read) !=
            fsim::runtime::TfContainmentError::None ||
        query->name[query->name_size] != '\0') {
      return reject(*result, FSIM_ACC_STANDARD_INVALID_QUERY,
                    kDiagnosticInvalid);
    }
    const std::string_view name{query->name, query->name_size};
    if (!valid_routine_name(name)) {
      return reject(*result, FSIM_ACC_STANDARD_INVALID_QUERY,
                    kDiagnosticInvalid);
    }
    supported = std::ranges::binary_search(kStandardRoutines, name);
    unsupported_status = FSIM_ACC_STANDARD_UNSUPPORTED_ROUTINE;
    unsupported_diagnostic = kDiagnosticRoutine;
  } else {
    if (query->name != nullptr || query->name_size != 0U) {
      return reject(*result, FSIM_ACC_STANDARD_INVALID_QUERY,
                    kDiagnosticInvalid);
    }
    if (query->kind == FSIM_ACC_STANDARD_OBJECT_TYPE) {
      supported = standard_object_type(query->value);
      unsupported_status = FSIM_ACC_STANDARD_UNSUPPORTED_OBJECT;
      unsupported_diagnostic = kDiagnosticObject;
    } else if (query->kind >= FSIM_ACC_STANDARD_CONFIGURATION &&
               query->kind <= FSIM_ACC_STANDARD_PRODUCT_TYPE) {
      supported = standard_behavior(query->kind, query->value);
      unsupported_status = FSIM_ACC_STANDARD_UNSUPPORTED_BEHAVIOR;
      unsupported_diagnostic = kDiagnosticBehavior;
    } else {
      return reject(*result, FSIM_ACC_STANDARD_INVALID_QUERY,
                    kDiagnosticInvalid);
    }
  }
  if (!supported) {
    return reject(*result, unsupported_status, unsupported_diagnostic);
  }
  if (dispatch != nullptr) {
    if (fsim::runtime::validate_tf_callback_pointer(dispatch) !=
        fsim::runtime::TfContainmentError::None) {
      return reject(*result, FSIM_ACC_STANDARD_INVALID_QUERY,
                    kDiagnosticInvalid);
    }
    try {
      if (dispatch(user_data, query) == 0) {
        return reject(*result, FSIM_ACC_STANDARD_DISPATCH_REJECTED,
                      kDiagnosticDispatch);
      }
    } catch (...) {
      return reject(*result, FSIM_ACC_STANDARD_DISPATCH_EXCEPTION,
                    kDiagnosticException);
    }
  }
  publish_result(*result, FSIM_ACC_STANDARD_SUPPORTED, {}, dispatch != nullptr);
  return FSIM_ACC_STANDARD_SUPPORTED;
}
