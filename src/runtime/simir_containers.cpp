// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>

namespace fsim::runtime::simir {

namespace {

[[noreturn]] void container_error(
    const ProcessId process,
    const InstructionIndex instruction,
    const std::string_view message) {
  throw InterpreterError{
      process, instruction, std::string{message}};
}

[[nodiscard]] std::size_t known_index(
    const ProcessId process,
    const InstructionIndex instruction,
    const PackedLogic4& value,
    const bool signed_index,
    const std::string_view role) {
  const auto word = value.low_word();
  if (word.width == 0 || word.width > 64 || word.bval != 0) {
    container_error(
        process, instruction,
        std::string{role}
            + " must be a known integral value");
  }
  if (signed_index && word.width <= 32) {
    const auto signed_value = static_cast<std::int64_t>(
        static_cast<std::int32_t>(
            static_cast<std::uint32_t>(word.aval)));
    if (signed_value < 0) {
      container_error(
          process, instruction,
          std::string{role} + " cannot be negative");
    }
    return static_cast<std::size_t>(signed_value);
  }
  if (word.aval
      > static_cast<std::uint64_t>(
          std::numeric_limits<std::size_t>::max())) {
    container_error(
        process, instruction,
        std::string{role} + " is too large");
  }
  return static_cast<std::size_t>(word.aval);
}

void require_same_type(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& target,
    const ContainerType& source) {
  if (target != source) {
    container_error(
        process, instruction, "container value type mismatch");
  }
}

void require_queue(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value) {
  if (!value.type.queue) {
    container_error(
        process, instruction,
        value.type.associative
            ? "queue method used on an associative array"
            : "queue method used on a dynamic array");
  }
}

void require_associative(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value,
    const std::string_view operation) {
  if (!value.type.associative) {
    container_error(
        process, instruction,
        std::string{operation}
            + " requires an associative array");
  }
}

[[nodiscard]] PackedLogic4 associative_key(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value) {
  const auto word = value.low_word();
  if (word.width != type.index_width || word.bval != 0) {
    container_error(
        process, instruction,
        word.bval != 0
            ? "associative-array index must be a known integral value"
            : "associative-array index type mismatch");
  }
  return PackedLogic4::from_aval_bval(
      type.index_width, word.aval, 0);
}

[[nodiscard]] bool key_less(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right) {
  const auto lhs = left.low_word().aval;
  const auto rhs = right.low_word().aval;
  if (type.signed_indices) {
    const auto sign = UINT64_C(1) << (type.index_width - 1U);
    const auto lhs_negative = (lhs & sign) != 0;
    const auto rhs_negative = (rhs & sign) != 0;
    if (lhs_negative != rhs_negative) {
      return lhs_negative;
    }
  }
  return lhs < rhs;
}

[[nodiscard]] std::size_t lower_key(
    const ContainerValue& value,
    const PackedLogic4& key) {
  return static_cast<std::size_t>(
      std::lower_bound(
          value.keys.begin(), value.keys.end(), key,
          [&](const PackedLogic4& candidate,
              const PackedLogic4& sought) {
            return key_less(value.type, candidate, sought);
          })
      - value.keys.begin());
}

[[nodiscard]] bool key_equal(
    const PackedLogic4& left,
    const PackedLogic4& right) {
  return left.low_word().aval == right.low_word().aval;
}

}  // namespace

void validate_container_value(const ContainerValue& value) {
  if (value.type.element_width == 0
      || value.type.element_width > 64) {
    throw std::invalid_argument{
        "SimIR container element width must be in 1..64"};
  }
  if (value.elements.size() > maximum_container_elements
      || (value.type.maximum_elements
          && value.elements.size()
              > *value.type.maximum_elements)) {
    throw std::length_error{
        "SimIR container exceeds its element limit"};
  }
  if (!value.type.queue && value.type.maximum_elements) {
    throw std::invalid_argument{
        "a dynamic array cannot have a queue bound"};
  }
  if (value.type.queue && value.type.associative) {
    throw std::invalid_argument{
        "a SimIR container cannot be both queue and associative"};
  }
  if (value.type.associative) {
    if (value.type.index_width == 0
        || value.type.index_width > 64) {
      throw std::invalid_argument{
          "SimIR associative-array index width must be in 1..64"};
    }
    if (value.keys.size() != value.elements.size()) {
      throw std::invalid_argument{
          "SimIR associative-array keys and elements must be paired"};
    }
    for (std::size_t index = 0; index < value.keys.size(); ++index) {
      const auto& key = value.keys[index];
      if (key.width() != value.type.index_width
          || key.is_logic9()
          || key.low_word().bval != 0) {
        throw std::invalid_argument{
            "SimIR associative-array key does not match its type"};
      }
      if (index != 0
          && !key_less(value.type, value.keys[index - 1], key)) {
        throw std::invalid_argument{
            "SimIR associative-array keys must be unique and ordered"};
      }
    }
  } else if (!value.keys.empty()) {
    throw std::invalid_argument{
        "non-associative SimIR containers cannot contain keys"};
  }
  for (const auto& element : value.elements) {
    if (element.width() != value.type.element_width
        || element.is_logic9()
        || (value.type.two_state
            && element.low_word().bval != 0)) {
      throw std::invalid_argument{
          "SimIR container element does not match its type"};
    }
  }
}

PackedLogic4 default_container_element(
    const ContainerType& type) {
  return PackedLogic4::from_aval_bval(
      type.element_width, 0, 0);
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ResizeContainer& operation) {
  auto& target = get_container_register(process, operation.target);
  if (target.type.queue || target.type.associative) {
    container_error(
        process.program.id, process.pc,
        target.type.queue
            ? "new[size] cannot resize a queue"
            : "new[size] cannot resize an associative array");
  }
  const auto size = known_index(
      process.program.id, process.pc,
      get_register(process, operation.size),
      false, "dynamic-array size");
  if (size > maximum_container_elements) {
    container_error(
        process.program.id, process.pc,
        "dynamic-array size exceeds the 4096-element limit");
  }
  target.elements.assign(
      size, default_container_element(target.type));
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const CopyContainerRegister& operation) {
  auto& destination =
      get_container_register(process, operation.destination);
  const auto& source =
      get_container_register(process, operation.source);
  require_same_type(
      process.program.id, process.pc,
      destination.type, source.type);
  destination.elements = source.elements;
  destination.keys = source.keys;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ReadContainerObject& operation) {
  auto& destination =
      get_container_register(process, operation.destination);
  const auto& source = get_container_object(
      operation.object).initial_value;
  require_same_type(
      process.program.id, process.pc,
      destination.type, source.type);
  destination.elements = source.elements;
  destination.keys = source.keys;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const WriteContainerObject& operation) {
  auto& destination = get_container_object(
      operation.object).initial_value;
  const auto& source =
      get_container_register(process, operation.source);
  require_same_type(
      process.program.id, process.pc,
      destination.type, source.type);
  destination.elements = source.elements;
  destination.keys = source.keys;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerSize& operation) {
  const auto size = get_container_register(
      process, operation.source).elements.size();
  get_register(process, operation.destination) =
      PackedLogic4::from_aval_bval(32, size, 0);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerRead& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  if (source.type.associative) {
    const auto key = associative_key(
        process.program.id, process.pc, source.type,
        get_register(process, operation.index));
    const auto at = lower_key(source, key);
    get_register(process, operation.destination) =
        at < source.keys.size() && key_equal(source.keys[at], key)
            ? source.elements[at]
            : default_container_element(source.type);
    ++process.pc;
    return;
  }
  const auto index = known_index(
      process.program.id, process.pc,
      get_register(process, operation.index),
      operation.signed_index, "container index");
  if (index >= source.elements.size()) {
    container_error(
        process.program.id, process.pc,
        "container index is out of range");
  }
  get_register(process, operation.destination) =
      source.elements[index];
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerWrite& operation) {
  auto& target =
      get_container_register(process, operation.target);
  const auto& source = get_register(process, operation.source);
  if (source.width() != target.type.element_width
      || source.is_logic9()
      || (target.type.two_state
          && source.low_word().bval != 0)) {
    container_error(
        process.program.id, process.pc,
        "container element write type mismatch");
  }
  if (target.type.associative) {
    const auto key = associative_key(
        process.program.id, process.pc, target.type,
        get_register(process, operation.index));
    const auto at = lower_key(target, key);
    if (at < target.keys.size()
        && key_equal(target.keys[at], key)) {
      target.elements[at] = source;
    } else {
      if (target.elements.size() >= maximum_container_elements) {
        container_error(
            process.program.id, process.pc,
            "associative array exceeds the 4096-entry limit");
      }
      target.keys.insert(target.keys.begin() + at, key);
      target.elements.insert(target.elements.begin() + at, source);
    }
    ++process.pc;
    return;
  }
  const auto index = known_index(
      process.program.id, process.pc,
      get_register(process, operation.index),
      operation.signed_index, "container index");
  if (index >= target.elements.size()) {
    container_error(
        process.program.id, process.pc,
        "container index is out of range");
  }
  target.elements[index] = source;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const DeleteContainer& operation) {
  auto& target =
      get_container_register(process, operation.target);
  if (operation.index) {
    require_associative(
        process.program.id, process.pc, target, "delete(index)");
    const auto key = associative_key(
        process.program.id, process.pc, target.type,
        get_register(process, *operation.index));
    const auto at = lower_key(target, key);
    if (at < target.keys.size()
        && key_equal(target.keys[at], key)) {
      target.keys.erase(target.keys.begin() + at);
      target.elements.erase(target.elements.begin() + at);
    }
  } else {
    target.elements.clear();
    target.keys.clear();
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const ContainerExists& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  require_associative(
      process.program.id, process.pc, source, "exists(index)");
  const auto key = associative_key(
      process.program.id, process.pc, source.type,
      get_register(process, operation.index));
  const auto at = lower_key(source, key);
  const auto exists =
      at < source.keys.size() && key_equal(source.keys[at], key);
  get_register(process, operation.destination) =
      PackedLogic4::from_aval_bval(32, exists ? 1U : 0U, 0);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const TraverseContainer& operation) {
  const auto& source =
      get_container_register(process, operation.source);
  require_associative(
      process.program.id, process.pc, source,
      "first/last/next/prev");
  std::optional<std::size_t> selected;
  if (!source.keys.empty()) {
    if (operation.traversal == ContainerTraversal::first) {
      selected = 0;
    } else if (operation.traversal == ContainerTraversal::last) {
      selected = source.keys.size() - 1U;
    } else {
      const auto key = associative_key(
          process.program.id, process.pc, source.type,
          get_register(process, operation.index));
      const auto at = lower_key(source, key);
      if (operation.traversal == ContainerTraversal::next) {
        const auto next =
            at < source.keys.size()
                    && key_equal(source.keys[at], key)
                ? at + 1U
                : at;
        if (next < source.keys.size()) {
          selected = next;
        }
      } else if (at != 0) {
        selected = at - 1U;
      }
    }
  }
  if (selected) {
    get_register(process, operation.index) = source.keys[*selected];
  }
  get_register(process, operation.destination) =
      PackedLogic4::from_aval_bval(
          32, selected.has_value() ? 1U : 0U, 0);
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PushContainer& operation) {
  auto& target =
      get_container_register(process, operation.target);
  require_queue(process.program.id, process.pc, target);
  const auto& source = get_register(process, operation.source);
  if (source.width() != target.type.element_width
      || source.is_logic9()
      || (target.type.two_state
          && source.low_word().bval != 0)) {
    container_error(
        process.program.id, process.pc,
        "queue element write type mismatch");
  }
  if (operation.front) {
    target.elements.insert(target.elements.begin(), source);
  } else {
    target.elements.push_back(source);
  }
  const auto maximum = target.type.maximum_elements.value_or(
      static_cast<std::uint32_t>(
          maximum_container_elements));
  if (target.elements.size() > maximum) {
    target.elements.pop_back();
  }
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const PopContainer& operation) {
  auto& target =
      get_container_register(process, operation.target);
  require_queue(process.program.id, process.pc, target);
  if (target.elements.empty()) {
    container_error(
        process.program.id, process.pc,
        "cannot pop an empty queue");
  }
  auto value =
      operation.front
          ? target.elements.front()
          : target.elements.back();
  if (operation.front) {
    target.elements.erase(target.elements.begin());
  } else {
    target.elements.pop_back();
  }
  get_register(process, operation.destination) = std::move(value);
  ++process.pc;
}

}  // namespace fsim::runtime::simir
