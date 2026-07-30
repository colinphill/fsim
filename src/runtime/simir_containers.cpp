// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

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
        "queue method used on a dynamic array");
  }
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
  if (target.type.queue) {
    container_error(
        process.program.id, process.pc,
        "new[size] cannot resize a queue");
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
  const auto index = known_index(
      process.program.id, process.pc,
      get_register(process, operation.index),
      operation.signed_index, "container index");
  if (index >= target.elements.size()) {
    container_error(
        process.program.id, process.pc,
        "container index is out of range");
  }
  const auto& source = get_register(process, operation.source);
  if (source.width() != target.type.element_width
      || source.is_logic9()
      || (target.type.two_state
          && source.low_word().bval != 0)) {
    container_error(
        process.program.id, process.pc,
        "container element write type mismatch");
  }
  target.elements[index] = source;
  ++process.pc;
}

void Interpreter::Impl::execute_container(
    ProcessState& process,
    const DeleteContainer& operation) {
  get_container_register(
      process, operation.target).elements.clear();
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
