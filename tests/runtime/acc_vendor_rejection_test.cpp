// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/acc_handle_bridge.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

std::uint32_t dispatches{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL accept(
    void*, const fsim_acc_standard_query_v3*) {
  ++dispatches;
  return 1;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL reject_dispatch(
    void*, const fsim_acc_standard_query_v3*) {
  ++dispatches;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throw_dispatch(
    void*, const fsim_acc_standard_query_v3*) {
  ++dispatches;
  throw std::runtime_error{"contained vendor dispatch failure"};
}

fsim_acc_standard_query_v3 routine(const char* const name) {
  return {.abi_version = FSIM_ACC_STANDARD_QUERY_ABI_VERSION,
          .struct_size = sizeof(fsim_acc_standard_query_v3),
          .kind = FSIM_ACC_STANDARD_ROUTINE,
          .name_size = static_cast<std::uint32_t>(std::strlen(name)),
          .name = name,
          .value = 0,
          .reserved = 0};
}

fsim_acc_standard_query_v3 selector(const std::uint32_t kind,
                                    const PLI_INT32 value) {
  return {.abi_version = FSIM_ACC_STANDARD_QUERY_ABI_VERSION,
          .struct_size = sizeof(fsim_acc_standard_query_v3),
          .kind = kind,
          .name_size = 0,
          .name = nullptr,
          .value = value,
          .reserved = 0};
}

bool diagnostic_is(const fsim_acc_standard_query_result_v3& result,
                   const char* const expected) {
  return result.diagnostic != nullptr &&
         result.diagnostic_size == std::strlen(expected) &&
         std::memcmp(result.diagnostic, expected, result.diagnostic_size) == 0;
}

}  // namespace

int main() {
  fsim_acc_standard_query_result_v3 result{};
  auto known = routine("acc_initialize");
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &known, accept, nullptr, &result) ==
                  FSIM_ACC_STANDARD_SUPPORTED &&
              result.dispatched == 1U &&
              result.diagnostic == nullptr && result.diagnostic_size == 0U &&
              dispatches == 1U && acc_error_flag == 0,
          "a standardized routine enters dispatch exactly once");

  auto vendor = routine("acc_vendor_fast_handle");
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &vendor, accept, nullptr, &result) ==
                  FSIM_ACC_STANDARD_UNSUPPORTED_ROUTINE &&
              result.dispatched == 0U &&
              diagnostic_is(result, "FSIM-ACC-NAME-002") &&
              dispatches == 1U && acc_error_flag == 1,
          "an unsupported vendor routine cannot enter fallback dispatch");

  constexpr std::array standard_objects{
      accAndGate, accBit, accBitSelect, accBitSelectPort, accBufGate,
      accBufif0Gate, accBufif1Gate, accCellInstance, accCmosGate, accCombPrim,
      accConcat, accConcatPort, accConstant, accDataPath, accExpandedVector,
      accFunction, accFunctionCall, accHold, accInout, accInoutTerminal,
      accInput, accInputTerminal, accIntegerParam, accIntegerVar,
      accInterModPath, accMinTypMax, accMixedIo, accModPath, accModule,
      accModuleInstance, accNamedEvent, accNandGate, accNegative, accNet,
      accNetBit, accNmosGate, accNochange, accNorGate, accNotGate,
      accNotif0Gate, accNotif1Gate, accOperator, accOrGate, accOutput,
      accOutputTerminal, accParameter, accPartSelect, accPartSelectPort,
      accPathInput, accPathOutput, accPathTerminal, accPeriod, accPmosGate,
      accPort, accPortBit, accPositive, accPrimitive, accProtected,
      accPulldownGate, accPullupGate, accRcmosGate, accRealParam, accRealVar,
      accRecovery, accReg, accRegBit, accRnmosGate, accRpmosGate,
      accRtranGate, accRtranif0Gate, accRtranif1Gate, accScalar,
      accScalarPort, accScope, accSeqPrim, accSetup, accSetuphold, accSkew,
      accSpecparam, accStatement, accStringParam, accSupply0, accSupply1,
      accSystemFunction, accSystemRealFunction, accSystemTask, accTask,
      accTaskCall, accTchk, accTchkTerminal, accTerminal, accTimeVar,
      accTopModule, accTranGate, accTranif0Gate, accTranif1Gate, accTri,
      accTri0, accTri1, accTriand, accTrior, accTrireg, accUnExpandedVector,
      accUnknown, accUserFunction, accUserRealFunction, accUserTask, accVector,
      accVectorPort, accWand, accWidth, accWire, accWor, accXnorGate,
      accXorGate};
  static_assert(standard_objects.size() == 115U);
  for (const auto type : standard_objects) {
    const auto query = selector(FSIM_ACC_STANDARD_OBJECT_TYPE, type);
    if (fsim_acc_validate_and_dispatch_standard_v3(
            &query, nullptr, nullptr, &result) !=
        FSIM_ACC_STANDARD_SUPPORTED) {
      throw std::runtime_error{
          "every canonical ACC object constant is accepted; rejected: " +
          std::to_string(type)};
    }
  }

  const std::array supported{
      selector(FSIM_ACC_STANDARD_CONFIGURATION, accMinTypMaxDelays),
      selector(FSIM_ACC_STANDARD_EDGE, accPosedge | accNegedge),
      selector(FSIM_ACC_STANDARD_DELAY_MODE, accDelayModeVeritime),
      selector(FSIM_ACC_STANDARD_UPDATE, accReleaseFlag),
      selector(FSIM_ACC_STANDARD_VALUE_FORMAT, accVectorVal),
      selector(FSIM_ACC_STANDARD_VCL_REASON, vector_value_change),
      selector(FSIM_ACC_STANDARD_TIME_TYPE, accRealTime),
      selector(FSIM_ACC_STANDARD_PRODUCT_TYPE, accTimingAnalyzer)};
  for (const auto& query : supported) {
    require(fsim_acc_validate_and_dispatch_standard_v3(
                &query, nullptr, nullptr, &result) ==
                    FSIM_ACC_STANDARD_SUPPORTED &&
                result.dispatched == 0U && acc_error_flag == 0,
            "standard object and behavior selectors validate without dispatch");
  }

  auto object = selector(FSIM_ACC_STANDARD_OBJECT_TYPE, 9999);
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &object, accept, nullptr, &result) ==
                  FSIM_ACC_STANDARD_UNSUPPORTED_OBJECT &&
              diagnostic_is(result, "FSIM-ACC-NAME-003") &&
              dispatches == 1U,
          "an unsupported object constant has a stable diagnostic");
  auto behavior = selector(FSIM_ACC_STANDARD_VALUE_FORMAT, 9);
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &behavior, accept, nullptr, &result) ==
                  FSIM_ACC_STANDARD_UNSUPPORTED_BEHAVIOR &&
              diagnostic_is(result, "FSIM-ACC-NAME-004") &&
              dispatches == 1U,
          "an unsupported behavior selector cannot enter fallback dispatch");

  auto malformed = routine("acc_initialize");
  malformed.abi_version = 2;
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &malformed, accept, nullptr, &result) ==
                  FSIM_ACC_STANDARD_INVALID_QUERY &&
              diagnostic_is(result, "FSIM-ACC-NAME-001") &&
              dispatches == 1U,
          "a v2 query is rejected before name inspection or dispatch");
  malformed = routine("acc_Initialize");
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &malformed, accept, nullptr, &result) ==
                  FSIM_ACC_STANDARD_INVALID_QUERY &&
              diagnostic_is(result, "FSIM-ACC-NAME-001") &&
              dispatches == 1U,
          "a malformed extension spelling is rejected before dispatch");
  const auto invalid_dispatch =
      std::bit_cast<fsim_acc_standard_dispatch_v3_fn>(std::uintptr_t{1});
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &known, invalid_dispatch, nullptr, &result) ==
                  FSIM_ACC_STANDARD_INVALID_QUERY &&
              diagnostic_is(result, "FSIM-ACC-NAME-001") &&
              dispatches == 1U,
          "an invalid dispatch address is rejected before invocation");

  require(fsim_acc_validate_and_dispatch_standard_v3(
              &known, reject_dispatch, nullptr, &result) ==
                  FSIM_ACC_STANDARD_DISPATCH_REJECTED &&
              diagnostic_is(result, "FSIM-ACC-NAME-005") &&
              result.dispatched == 0U && dispatches == 2U,
          "a host rejection has a stable diagnostic and no success claim");
  require(fsim_acc_validate_and_dispatch_standard_v3(
              &known, throw_dispatch, nullptr, &result) ==
                  FSIM_ACC_STANDARD_DISPATCH_EXCEPTION &&
              diagnostic_is(result, "FSIM-ACC-NAME-006") &&
              result.dispatched == 0U && dispatches == 3U,
          "a dispatch exception is contained at the ACC boundary");
  return 0;
}
