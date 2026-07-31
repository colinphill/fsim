// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fsim::compiler::llvm_detail {

namespace {

[[nodiscard]] std::string instruction_error(
    const runtime::simir::Process& process,
    const std::size_t instruction,
    const std::string_view message) {
  std::ostringstream result;
  result << "cannot JIT SimIR process " << process.id << " ('" << process.name
         << "'), instruction " << instruction << ": " << message;
  return result.str();
}

}  // namespace

[[noreturn]] void reject(
    const runtime::simir::Process& process,
    const std::size_t instruction,
    const std::string_view message) {
  throw LlvmJitError(instruction_error(process, instruction, message));
}

[[noreturn]] void reject_unsupported(
    const runtime::simir::Process& process,
    const std::size_t instruction,
    const std::string_view message) {
  throw LlvmJitUnsupportedError(
      instruction_error(process, instruction, message));
}

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

[[nodiscard]] std::optional<std::string>
validate_container_locator_metadata(
    const runtime::simir::LocateContainer& operation,
    const runtime::simir::ContainerType& destination,
    const runtime::simir::ContainerType& source) {
  using namespace runtime::simir;
  if (operation.operation > ContainerLocatorOperator::find_last_index) {
    return "LocateContainer has an invalid operator";
  }
  const bool predicate_locator =
      operation.operation >= ContainerLocatorOperator::find;
  const bool index_result =
      operation.operation == ContainerLocatorOperator::unique_index
      || operation.operation == ContainerLocatorOperator::find_index
      || operation.operation
          == ContainerLocatorOperator::find_first_index
      || operation.operation
          == ContainerLocatorOperator::find_last_index;
  if (!destination.queue || destination.associative
      || destination.fixed || source.associative
      || (index_result
              ? destination.element_width != 32
                    || !destination.two_state
                    || !destination.signed_elements
              : destination.element_width != source.element_width
                    || destination.two_state != source.two_state
                    || destination.signed_elements
                        != source.signed_elements)) {
    return "LocateContainer has incompatible container types";
  }
  if (predicate_locator != !operation.predicate.empty()
      || operation.predicate.size()
          > maximum_container_predicate_nodes) {
    return "LocateContainer has invalid predicate metadata";
  }
  std::vector<ContainerPredicateValueKind> value_kinds;
  value_kinds.reserve(operation.predicate.size());
  for (std::size_t index = 0;
       index < operation.predicate.size(); ++index) {
    const auto& node = operation.predicate[index];
    const auto earlier =
        [index](const std::uint32_t operand) {
          return operand < index;
        };
    const bool comparison =
        node.operation >= ContainerPredicateOperator::equal
        && node.operation
            <= ContainerPredicateOperator::greater_equal;
    if (node.operation
        > ContainerPredicateOperator::logical_not) {
      return "LocateContainer predicate has an invalid operator";
    }
    if (node.operation == ContainerPredicateOperator::item) {
      if (node.value_kind
          != ContainerPredicateValueKind::element) {
        return "LocateContainer predicate item has the wrong type";
      }
      value_kinds.push_back(node.value_kind);
    } else if (
        node.operation == ContainerPredicateOperator::index) {
      if (node.value_kind
          != ContainerPredicateValueKind::index) {
        return "LocateContainer predicate index has the wrong type";
      }
      value_kinds.push_back(node.value_kind);
    } else if (
        node.operation == ContainerPredicateOperator::constant) {
      if (node.value_kind == ContainerPredicateValueKind::logical
          || node.constant.width()
              != (node.value_kind
                          == ContainerPredicateValueKind::index
                      ? 32U
                      : source.element_width)
          || node.constant.is_logic9()
          || ((node.value_kind
                       == ContainerPredicateValueKind::index
                   || source.two_state)
              && node.constant.low_word().bval != 0)) {
        return "LocateContainer predicate constant has the wrong type";
      }
      value_kinds.push_back(node.value_kind);
    } else if (comparison) {
      if (!earlier(node.left) || !earlier(node.right)
          || node.value_kind
              != ContainerPredicateValueKind::logical
          || value_kinds[node.left]
              != value_kinds[node.right]
          || value_kinds[node.left]
              == ContainerPredicateValueKind::logical) {
        return "LocateContainer comparison operands are invalid";
      }
      value_kinds.push_back(node.value_kind);
    } else if (
        node.operation
            == ContainerPredicateOperator::logical_not) {
      if (!earlier(node.left)
          || node.value_kind
              != ContainerPredicateValueKind::logical) {
        return "LocateContainer logical operand is invalid";
      }
      value_kinds.push_back(node.value_kind);
    } else {
      if (!earlier(node.left) || !earlier(node.right)
          || node.value_kind
              != ContainerPredicateValueKind::logical) {
        return "LocateContainer logical operands are invalid";
      }
      value_kinds.push_back(node.value_kind);
    }
  }
  return std::nullopt;
}

namespace {

[[nodiscard]] std::optional<std::string>
validate_container_element_graph(
    const std::span<const runtime::simir::ContainerPredicateNode> nodes,
    const runtime::simir::ContainerType& source,
    const std::string_view owner,
    const std::string_view graph_name) {
  using namespace runtime::simir;
  if (source.associative
      || nodes.empty()
      || nodes.size() > maximum_container_predicate_nodes) {
    return std::string{owner} + " has invalid "
        + std::string{graph_name} + " metadata";
  }
  std::vector<ContainerPredicateValueKind> value_kinds;
  value_kinds.reserve(nodes.size());
  std::size_t conditional_count{};
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    const auto earlier =
        [index](const std::uint32_t operand) {
          return operand < index;
        };
    const bool comparison =
        node.operation >= ContainerPredicateOperator::equal
        && node.operation
            <= ContainerPredicateOperator::greater_equal;
    if (node.operation > ContainerPredicateOperator::conditional) {
      return std::string{owner} + " " + std::string{graph_name}
          + " has an invalid operator";
    }
    if (node.operation != ContainerPredicateOperator::conditional
        && node.third != 0) {
      return std::string{owner} + " " + std::string{graph_name}
          + " has an unused third edge";
    }
    if (node.operation == ContainerPredicateOperator::item) {
      if (node.value_kind
          != ContainerPredicateValueKind::element) {
        return std::string{owner} + " " + std::string{graph_name}
            + " item has the wrong type";
      }
    } else if (
        node.operation == ContainerPredicateOperator::index) {
      if (node.value_kind
          != ContainerPredicateValueKind::index) {
        return std::string{owner} + " " + std::string{graph_name}
            + " index has the wrong type";
      }
    } else if (
        node.operation == ContainerPredicateOperator::constant) {
      if (node.value_kind > ContainerPredicateValueKind::logical
          || node.value_kind
              == ContainerPredicateValueKind::logical
          || node.constant.width()
              != (node.value_kind
                          == ContainerPredicateValueKind::index
                      ? 32U
                      : source.element_width)
          || node.constant.is_logic9()
          || ((node.value_kind
                       == ContainerPredicateValueKind::index
                   || source.two_state)
              && node.constant.low_word().bval != 0)) {
        return std::string{owner} + " " + std::string{graph_name}
            + " constant has the wrong type";
      }
    } else if (comparison) {
      if (!earlier(node.left) || !earlier(node.right)
          || node.value_kind
              != ContainerPredicateValueKind::logical
          || value_kinds[node.left]
              != value_kinds[node.right]
          || value_kinds[node.left]
              == ContainerPredicateValueKind::logical) {
        return std::string{owner} + " " + std::string{graph_name}
            + " comparison operands are invalid";
      }
    } else if (
        node.operation
            == ContainerPredicateOperator::logical_not) {
      if (!earlier(node.left)
          || node.value_kind
              != ContainerPredicateValueKind::logical) {
        return std::string{owner} + " " + std::string{graph_name}
            + " logical operand is invalid";
      }
    } else if (
        node.operation
            == ContainerPredicateOperator::conditional) {
      ++conditional_count;
      if (conditional_count > 1U
          || !earlier(node.left)
          || !earlier(node.right)
          || !earlier(node.third)
          || node.value_kind
              != ContainerPredicateValueKind::element
          || value_kinds[node.right]
              != ContainerPredicateValueKind::element
          || value_kinds[node.third]
              != ContainerPredicateValueKind::element) {
        return std::string{owner}
            + " conditional operands are invalid";
      }
    } else {
      if (!earlier(node.left) || !earlier(node.right)
          || node.value_kind
              != ContainerPredicateValueKind::logical) {
        return std::string{owner} + " " + std::string{graph_name}
            + " logical operands are invalid";
      }
    }
    value_kinds.push_back(node.value_kind);
  }
  if (value_kinds.back()
      != ContainerPredicateValueKind::element) {
    return std::string{owner} + " " + std::string{graph_name}
        + " root has the wrong type";
  }
  return std::nullopt;
}

}  // namespace

[[nodiscard]] std::optional<std::string>
validate_container_reduction_metadata(
    const runtime::simir::ContainerReduction& operation,
    const runtime::simir::ContainerType& source) {
  if (operation.transformation.empty()) {
    return std::nullopt;
  }
  return validate_container_element_graph(
      operation.transformation, source,
      "ContainerReduction", "transformation");
}

[[nodiscard]] std::optional<std::string>
validate_container_ordering_metadata(
    const runtime::simir::OrderContainer& operation,
    const runtime::simir::ContainerType& target) {
  using runtime::simir::ContainerOrderingOperator;
  if (operation.key.empty()) {
    return std::nullopt;
  }
  if (operation.operation != ContainerOrderingOperator::ascending
      && operation.operation
          != ContainerOrderingOperator::descending) {
    return "OrderContainer key metadata requires sort or rsort";
  }
  return validate_container_element_graph(
      operation.key, target, "OrderContainer", "key");
}

[[nodiscard]] std::optional<std::string>
validate_container_locator_transformation_metadata(
    const runtime::simir::LocateContainer& operation,
    const runtime::simir::ContainerType& source) {
  using runtime::simir::ContainerLocatorOperator;
  if (operation.transformation.empty()) {
    return std::nullopt;
  }
  if (operation.operation >= ContainerLocatorOperator::find
      || !operation.predicate.empty()) {
    return "LocateContainer transformation metadata requires "
           "min, max, unique, or unique_index";
  }
  return validate_container_element_graph(
      operation.transformation, source,
      "LocateContainer", "transformation");
}

}  // namespace fsim::compiler::llvm_detail
