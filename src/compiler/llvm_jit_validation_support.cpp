// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <algorithm>
#include <cctype>

namespace fsim::compiler::llvm_detail {

[[nodiscard]] bool valid_symbol(const std::string_view symbol) noexcept {
  if (symbol.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(symbol.front());
  if (std::isalpha(first) == 0 && symbol.front() != '_') {
    return false;
  }
  return std::all_of(
      symbol.begin() + 1, symbol.end(), [](const char value) {
        const auto character = static_cast<unsigned char>(value);
        return std::isalnum(character) != 0 || value == '_';
      });
}

[[nodiscard]] std::string_view generated_runtime_error_reason(
    const JitGeneratedRuntimeErrorReason reason) noexcept {
  switch (reason) {
  case JitGeneratedRuntimeErrorReason::unknown_branch_condition:
    return "branch condition is unknown or high impedance";
  case JitGeneratedRuntimeErrorReason::integer_operand_unknown:
    return "VHDL integer operand contains an unknown or high-impedance value";
  case JitGeneratedRuntimeErrorReason::integer_overflow:
    return "VHDL integer arithmetic overflow";
  case JitGeneratedRuntimeErrorReason::integer_division_by_zero:
    return "VHDL integer division by zero";
  case JitGeneratedRuntimeErrorReason::integer_negative_exponent:
    return "VHDL integer exponent must be nonnegative";
  case JitGeneratedRuntimeErrorReason::integer_subtype_range:
    return "VHDL integer subtype range check failed";
  case JitGeneratedRuntimeErrorReason::dynamic_index_unknown:
    return "dynamic packed index contains an unknown or high-impedance value";
  case JitGeneratedRuntimeErrorReason::dynamic_index_range:
    return "dynamic packed index is outside the declared range";
  case JitGeneratedRuntimeErrorReason::call_stack_unknown:
    return "call-stack state contains an unknown or high-impedance value";
  case JitGeneratedRuntimeErrorReason::call_stack_overflow:
    return "call-stack capacity is exhausted";
  case JitGeneratedRuntimeErrorReason::call_stack_underflow:
    return "call-stack underflow";
  case JitGeneratedRuntimeErrorReason::call_stack_target:
    return "call-stack return target is invalid";
  case JitGeneratedRuntimeErrorReason::string_callback_failure:
    return "mutable string runtime callback failed";
  case JitGeneratedRuntimeErrorReason::file_callback_failure:
    return "text file runtime callback failed";
  case JitGeneratedRuntimeErrorReason::container_callback_failure:
    return "bounded container runtime callback failed";
  }
  return "unknown generated runtime error";
}

[[nodiscard]] std::string generated_runtime_error_message(
    const std::uint32_t instruction,
    const JitGeneratedRuntimeErrorReason reason) {
  return "generated SimIR process, instruction "
      + std::to_string(instruction) + ": "
      + std::string{generated_runtime_error_reason(reason)};
}

[[nodiscard]] std::optional<JitGeneratedRuntimeErrorReason>
decode_generated_runtime_error(const std::uint64_t value) noexcept {
  const auto reason =
      static_cast<JitGeneratedRuntimeErrorReason>(value);
  switch (reason) {
  case JitGeneratedRuntimeErrorReason::unknown_branch_condition:
  case JitGeneratedRuntimeErrorReason::integer_operand_unknown:
  case JitGeneratedRuntimeErrorReason::integer_overflow:
  case JitGeneratedRuntimeErrorReason::integer_division_by_zero:
  case JitGeneratedRuntimeErrorReason::integer_negative_exponent:
  case JitGeneratedRuntimeErrorReason::integer_subtype_range:
  case JitGeneratedRuntimeErrorReason::dynamic_index_unknown:
  case JitGeneratedRuntimeErrorReason::dynamic_index_range:
  case JitGeneratedRuntimeErrorReason::call_stack_unknown:
  case JitGeneratedRuntimeErrorReason::call_stack_overflow:
  case JitGeneratedRuntimeErrorReason::call_stack_underflow:
  case JitGeneratedRuntimeErrorReason::call_stack_target:
  case JitGeneratedRuntimeErrorReason::string_callback_failure:
  case JitGeneratedRuntimeErrorReason::file_callback_failure:
  case JitGeneratedRuntimeErrorReason::container_callback_failure:
    return reason;
  }
  return std::nullopt;
}

}  // namespace fsim::compiler::llvm_detail
