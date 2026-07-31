// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::compiler::llvm_detail {


using runtime::Logic9;
using namespace runtime::simir;


template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;


[[nodiscard]] std::string instruction_error(const Process &process,
                                            const std::size_t instruction,
                                            const std::string_view message) {
  std::ostringstream result;
  result << "cannot JIT SimIR process " << process.id << " ('" << process.name
         << "'), instruction " << instruction << ": " << message;
  return result.str();
}

[[noreturn]] void reject(const Process &process, const std::size_t instruction,
                         const std::string_view message) {
  throw LlvmJitError(instruction_error(process, instruction, message));
}

[[noreturn]] void
reject_unsupported(const Process &process, const std::size_t instruction,
                   const std::string_view message) {
  throw LlvmJitUnsupportedError(
      instruction_error(process, instruction, message));
}


[[nodiscard]] ValidatedProcess
validate_process(const Process &process,
                 const std::span<const std::uint32_t> signal_widths,
                 const std::span<const ValueKind>
                     signal_value_kinds) {
  if (process.operations.empty()) {
    throw LlvmJitError("cannot JIT an empty SimIR process");
  }
  if (process.operations.size() >
      static_cast<std::size_t>(
          std::numeric_limits<InstructionIndex>::max())) {
    throw LlvmJitUnsupportedError(
        "SimIR process has too many instructions for the resumable JIT ABI");
  }
  if (process.register_count >
      static_cast<std::size_t>(std::numeric_limits<RegisterId>::max())) {
    throw LlvmJitUnsupportedError(
        "SimIR process has too many registers for the JIT ABI");
  }
  if (!process.register_value_kinds.empty()
      && process.register_value_kinds.size()
          != process.register_count) {
    throw LlvmJitError(
        "SimIR register value-domain metadata count does not match "
        "register_count");
  }
  if (!signal_value_kinds.empty()
      && signal_value_kinds.size() != signal_widths.size()) {
    throw LlvmJitError(
        "SimIR signal value-domain metadata count does not match "
        "signal_widths");
  }

  ValidatedProcess result;
  result.uses_logic9 = std::ranges::any_of(
      process.register_value_kinds,
      [](const ValueKind kind) {
        return kind == ValueKind::logic9;
      });
  result.register_widths.resize(process.register_count);
  std::vector<RegisterId> parents(process.register_count);
  std::vector<std::size_t> root_widths(process.register_count);
  for (std::size_t index = 0; index < parents.size(); ++index) {
    parents[index] = static_cast<RegisterId>(index);
  }
  std::vector<bool> defined(process.register_count);
  std::vector<std::vector<RegisterId>> instruction_definitions(
      process.operations.size());
  std::vector<std::vector<RegisterId>> instruction_uses(
      process.operations.size());
  std::optional<std::pair<std::size_t, std::string>> unsupported;

  const auto record_unsupported =
      [&](const std::size_t instruction, const std::string_view message) {
        if (!unsupported) {
          unsupported.emplace(instruction, message);
        }
      };

  const auto referenced_signal_width =
      [&](const std::uint32_t signal,
          const std::size_t instruction) -> std::uint32_t {
    if (signal >= signal_widths.size()) {
      reject(process, instruction, "signal ID is outside signal_widths");
    }
    const auto width = signal_widths[signal];
    if (width == 0) {
      reject(process, instruction, "signal width must be greater than zero");
    }
    return width;
  };

  const auto signal_width =
      [&](const std::uint32_t signal,
          const std::size_t instruction) -> std::uint32_t {
    const auto width = referenced_signal_width(signal, instruction);
    if (width > 64) {
      record_unsupported(
          instruction,
          "the LLVM scalar subset requires signal widths in [1, 64]");
    }
    if (!signal_value_kinds.empty()
        && signal_value_kinds[signal] == ValueKind::logic9) {
      result.uses_logic9 = true;
    }
    return width;
  };

  const auto validate_register =
      [&](const RegisterId id, const std::size_t instruction,
          const std::string_view role) {
    if (id >= process.register_count) {
      reject(process, instruction,
             std::string{role} + " register ID is out of range");
    }
  };

  const auto validate_string_register =
      [&](const StringRegisterId id,
          const std::size_t instruction,
          const std::string_view role) {
        if (id >= process.string_register_count) {
          reject(
              process,
              instruction,
              std::string{role}
                  + " string register ID is out of range");
        }
      };
  const auto validate_container_register =
      [&](const ContainerRegisterId id,
          const std::size_t instruction,
          const std::string_view role) {
        if (id >= process.container_register_count
            || process.container_register_types.size()
                != process.container_register_count) {
          reject(
              process, instruction,
              std::string{role}
                  + " container register is out of range");
        }
      };

  const auto find_root = [&](const RegisterId id) {
    auto root = id;
    while (parents[root] != root) {
      root = parents[root];
    }
    auto current = id;
    while (parents[current] != current) {
      const auto next = parents[current];
      parents[current] = root;
      current = next;
    }
    return root;
  };

  const auto constrain_width =
      [&](const RegisterId id, const std::size_t width,
          const std::size_t instruction) {
    validate_register(id, instruction, "constrained");
    if (width == 0) {
      reject(process, instruction, "register width must be greater than zero");
    }
    if (width > 64) {
      record_unsupported(
          instruction,
          "the LLVM scalar subset requires register widths in [1, 64]");
    }
    const auto root = find_root(id);
    if (root_widths[root] != 0 && root_widths[root] != width) {
      reject(process, instruction,
             "register width constraints are inconsistent");
    }
    root_widths[root] = width;
  };

  const auto unify_registers =
      [&](const RegisterId lhs, const RegisterId rhs,
          const std::size_t instruction) {
    validate_register(lhs, instruction, "left");
    validate_register(rhs, instruction, "right");
    auto left_root = find_root(lhs);
    auto right_root = find_root(rhs);
    if (left_root == right_root) {
      return;
    }
    if (root_widths[left_root] != 0 && root_widths[right_root] != 0 &&
        root_widths[left_root] != root_widths[right_root]) {
      reject(process, instruction,
             "register width constraints are inconsistent");
    }
    parents[right_root] = left_root;
    if (root_widths[left_root] == 0) {
      root_widths[left_root] = root_widths[right_root];
    }
  };

  const auto record_use = [&](const RegisterId id,
                              const std::size_t instruction) {
    validate_register(id, instruction, "source");
    instruction_uses[instruction].push_back(id);
  };

  const auto record_definition = [&](const RegisterId id,
                                     const std::size_t instruction) {
    validate_register(id, instruction, "destination");
    instruction_definitions[instruction].push_back(id);
    defined[id] = true;
  };

  const auto dynamic_range_width =
      [&](const DynamicIndex& selection,
          const std::size_t instruction) -> std::uint64_t {
        if (selection.left
                < std::numeric_limits<std::int32_t>::min()
            || selection.left
                > std::numeric_limits<std::int32_t>::max()
            || selection.right
                < std::numeric_limits<std::int32_t>::min()
            || selection.right
                > std::numeric_limits<std::int32_t>::max()) {
          reject(
              process,
              instruction,
              "dynamic index bounds must fit signed 32-bit integers");
        }
        const auto left =
            static_cast<std::int64_t>(selection.left);
        const auto right =
            static_cast<std::int64_t>(selection.right);
        return static_cast<std::uint64_t>(
                   left >= right ? left - right : right - left)
            + 1U;
      };

  const auto validate_dynamic_bounds =
      [&](const DynamicIndex& selection,
          const std::uint64_t target_width,
          const std::size_t instruction) {
        const auto range_width =
            dynamic_range_width(selection, instruction);
        if (selection.base_offset > target_width
            || range_width
                > target_width - selection.base_offset) {
          reject(
              process,
              instruction,
              "dynamic index range is outside its packed target");
        }
      };

  const auto validate_dynamic_selection =
      [&](const DynamicIndex& selection,
          const std::uint64_t target_width,
          const std::size_t instruction) {
        record_use(selection.index, instruction);
        constrain_width(selection.index, 32U, instruction);
        validate_dynamic_bounds(
            selection, target_width, instruction);
      };

  const auto validate_target = [&](const InstructionIndex target,
                                   const std::size_t instruction,
                                   const std::string_view kind) {
    if (target >= process.operations.size()) {
      reject(process, instruction,
             std::string{kind} + " target is outside the operation stream");
    }
  };

  const auto validate_call_stack =
      [&](const CallStack& stack, const std::size_t instruction) {
        if (stack.capacity == 0) {
          reject(process, instruction,
                 "call-stack capacity must be greater than zero");
        }
        const auto end =
            static_cast<std::uint64_t>(stack.entries)
            + stack.capacity;
        if (end > process.register_count) {
          reject(process, instruction,
                 "call-stack register range is outside register_count");
        }
        record_use(stack.pointer, instruction);
        constrain_width(stack.pointer, 32U, instruction);
        for (std::uint32_t offset = 0;
             offset < stack.capacity; ++offset) {
          const auto entry =
              static_cast<RegisterId>(stack.entries + offset);
          record_use(entry, instruction);
          constrain_width(entry, 32U, instruction);
        }
      };

  const auto first_wait_sensitivity = std::find_if(
      process.operations.begin(), process.operations.end(),
      [](const Operation &operation) {
        return std::holds_alternative<WaitSensitivity>(operation);
      });
  const auto sensitivity_instruction =
      first_wait_sensitivity == process.operations.end()
          ? std::size_t{0}
          : static_cast<std::size_t>(
                std::distance(
                    process.operations.begin(), first_wait_sensitivity));
  for (const auto sensitivity : process.static_sensitivity) {
    const auto width = referenced_signal_width(
        sensitivity.signal, sensitivity_instruction);
    switch (sensitivity.edge) {
    case EdgeKind::any:
      break;
    case EdgeKind::posedge:
    case EdgeKind::negedge:
      if (width != 1) {
        reject(
            process, sensitivity_instruction,
            "edge sensitivity requires a scalar signal");
      }
      break;
    default:
      reject(
          process, sensitivity_instruction,
          "static sensitivity has an invalid edge kind");
    }
  }

  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    std::visit(
        Overloaded{
            [&](const LoadConstant &operation) {
              if (operation.value.width() == 0) {
                reject(process, index,
                       "LoadConstant width must be greater than zero");
              }
              if (operation.value.width() > 64) {
                record_unsupported(
                    index,
                    "LoadConstant width is outside the supported [1, 64] "
                    "range");
              }
              result.uses_logic9 =
                  result.uses_logic9
                  || operation.value.is_logic9();
              record_definition(operation.destination, index);
              constrain_width(operation.destination, operation.value.width(),
                              index);
            },
            [&](const ReadSignal &operation) {
              record_definition(operation.destination, index);
              constrain_width(operation.destination,
                              signal_width(operation.signal, index), index);
            },
            [&](const SignalEvent& operation) {
              result.uses_signal_event = true;
              (void)signal_width(operation.signal, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const SignalLastValue& operation) {
              result.uses_signal_last_value = true;
              record_definition(operation.destination, index);
              constrain_width(
                  operation.destination,
                  signal_width(operation.signal, index),
                  index);
            },
            [&](const SignalLastEvent& operation) {
              result.uses_signal_last_event = true;
              (void)signal_width(operation.signal, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 64U, index);
            },
            [&](const SignalActive& operation) {
              result.uses_signal_active = true;
              (void)signal_width(operation.signal, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const CopyRegister& operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              unify_registers(
                  operation.destination, operation.source, index);
            },
            [&](const LoadStringConstant& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.destination, index, "destination");
              if (operation.value.size() > maximum_string_bytes) {
                reject(
                    process,
                    index,
                    "LoadStringConstant exceeds the byte limit");
              }
            },
            [&](const CopyStringRegister& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.destination, index, "destination");
              validate_string_register(
                  operation.source, index, "source");
            },
            [&](const ReadStringObject& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.destination, index, "destination");
              (void)operation.object;
            },
            [&](const WriteStringObject& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.source, index, "source");
              (void)operation.object;
            },
            [&](const ConcatenateStrings& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.destination, index, "destination");
              for (const auto operand : operation.operands) {
                validate_string_register(
                    operand, index, "source");
              }
            },
            [&](const CompareStrings& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.lhs, index, "left");
              validate_string_register(
                  operation.rhs, index, "right");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const StringLength& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.source, index, "source");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const StringIndex& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.source, index, "source");
              record_use(operation.index, index);
              constrain_width(operation.index, 32U, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 8U, index);
            },
            [&](const StringReplaceByte& operation) {
              result.uses_strings = true;
              validate_string_register(
                  operation.target, index, "target");
              record_use(operation.index, index);
              record_use(operation.source, index);
              constrain_width(operation.index, 32U, index);
              constrain_width(operation.source, 8U, index);
            },
            [&](const ResizeContainer& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.target, index, "target");
              record_use(operation.size, index);
              constrain_width(operation.size, 32U, index);
            },
            [&](const CopyContainerRegister& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.destination, index, "destination");
              validate_container_register(
                  operation.source, index, "source");
            },
            [&](const ConditionalContainerSelect& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.destination, index, "destination");
              validate_container_register(
                  operation.when_true, index, "when_true");
              validate_container_register(
                  operation.when_false, index, "when_false");
              record_use(operation.condition, index);
              constrain_width(operation.condition, 1U, index);
              if (operation.destination
                      < process.container_register_types.size()
                  && operation.when_true
                      < process.container_register_types.size()
                  && operation.when_false
                      < process.container_register_types.size()
                  && (process.container_register_types[
                          operation.destination]
                          != process.container_register_types[
                              operation.when_true]
                      || process.container_register_types[
                             operation.destination]
                          != process.container_register_types[
                              operation.when_false])) {
                reject(
                    process, index,
                    "ConditionalContainerSelect profiles differ");
              }
            },
            [&](const CompareContainers& operation) {
              result.uses_containers = true;
              validate_container_register(operation.lhs, index, "lhs");
              validate_container_register(operation.rhs, index, "rhs");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 1U, index);
              if (operation.lhs < process.container_register_types.size()
                  && operation.rhs < process.container_register_types.size()
                  && process.container_register_types[operation.lhs]
                      != process.container_register_types[operation.rhs]) {
                reject(process, index, "CompareContainers profiles differ");
              }
            },
            [&](const ReadContainerObject& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.destination, index, "destination");
              (void)operation.object;
            },
            [&](const WriteContainerObject& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.source, index, "source");
              (void)operation.object;
            },
            [&](const ContainerSize& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.source, index, "source");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const ContainerReduction& operation) {
              result.uses_containers = true;
              validate_container_register(operation.source, index, "source");
              if (static_cast<std::uint8_t>(operation.operation) >
                  static_cast<std::uint8_t>(
                      ContainerReductionOperator::bit_xor)) {
                reject(process, index, "ContainerReduction has an invalid operator");
              }
              record_definition(operation.destination, index);
              if (operation.source
                  < process.container_register_types.size()) {
                if (const auto error =
                        validate_container_reduction_metadata(
                            operation,
                            process.container_register_types[
                                operation.source])) {
                  reject(process, index, *error);
                }
                constrain_width(
                    operation.destination,
                    process.container_register_types[operation.source]
                        .element_width, index);
              }
            },
            [&](const OrderContainer& operation) {
              result.uses_containers = true;
              validate_container_register(operation.target, index, "target");
              if (static_cast<std::uint8_t>(operation.operation) >
                  static_cast<std::uint8_t>(
                      ContainerOrderingOperator::descending)) {
                reject(process, index, "OrderContainer has an invalid operator");
              }
              if (operation.target < process.container_register_types.size()
                  && process.container_register_types[operation.target]
                         .associative) {
                reject(process, index,
                       "OrderContainer does not support associative arrays");
              }
              if (operation.target
                  < process.container_register_types.size()) {
                if (const auto error =
                        validate_container_ordering_metadata(
                            operation,
                            process.container_register_types[
                                operation.target])) {
                  reject(process, index, *error);
                }
              }
            },
            [&](const LocateContainer& operation) {
              result.uses_containers = true;
              validate_container_register(operation.destination, index,
                                          "destination");
              validate_container_register(operation.source, index, "source");
              if (operation.destination
                      < process.container_register_types.size()
                  && operation.source
                      < process.container_register_types.size()) {
                if (const auto error =
                        validate_container_locator_metadata(
                            operation,
                            process.container_register_types[
                                operation.destination],
                            process.container_register_types[
                                operation.source])) {
                  reject(process, index, *error);
                }
                if (const auto error =
                        validate_container_locator_transformation_metadata(
                            operation,
                            process.container_register_types[
                                operation.source])) {
                  reject(process, index, *error);
                }
              }
            },
            [&](const ContainerRead& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.source, index, "source");
              record_use(operation.index, index);
              if (operation.source
                  < process.container_register_types.size()
                  && (process.container_register_types[
                          operation.source].associative
                      || process.container_register_types[
                             operation.source].fixed)) {
                constrain_width(
                    operation.index,
                    process.container_register_types[
                            operation.source].fixed
                        ? 32U
                        : process.container_register_types[
                              operation.source].index_width,
                    index);
              }
              record_definition(operation.destination, index);
              constrain_width(
                  operation.destination,
                  process.container_register_types[
                      operation.source].element_width,
                  index);
            },
            [&](const ContainerWrite& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.target, index, "target");
              record_use(operation.index, index);
              if (operation.target
                  < process.container_register_types.size()
                  && (process.container_register_types[
                          operation.target].associative
                      || process.container_register_types[
                             operation.target].fixed)) {
                constrain_width(
                    operation.index,
                    process.container_register_types[
                            operation.target].fixed
                        ? 32U
                        : process.container_register_types[
                              operation.target].index_width,
                    index);
              }
              record_use(operation.source, index);
              constrain_width(
                  operation.source,
                  process.container_register_types[
                      operation.target].element_width,
                  index);
            },
            [&](const DeleteContainer& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.target, index, "target");
              if (operation.index) {
                record_use(*operation.index, index);
                if (operation.target
                    < process.container_register_types.size()) {
                  constrain_width(
                      *operation.index,
                      process.container_register_types[
                          operation.target].index_width,
                      index);
                }
              }
            },
            [&](const ContainerExists& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.source, index, "source");
              record_use(operation.index, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
              if (operation.source
                  < process.container_register_types.size()) {
                constrain_width(
                    operation.index,
                    process.container_register_types[
                        operation.source].index_width,
                    index);
              }
            },
            [&](const TraverseContainer& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.source, index, "source");
              record_use(operation.index, index);
              record_definition(operation.index, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
              if (operation.source
                  < process.container_register_types.size()) {
                constrain_width(
                    operation.index,
                    process.container_register_types[
                        operation.source].index_width,
                    index);
              }
            },
            [&](const LoadMemory& operation) {
              result.uses_containers = true;
              result.uses_strings = true;
              result.uses_files = true;
              validate_container_register(
                  operation.target, index, "target");
              validate_string_register(
                  operation.path, index, "path");
              if (operation.target
                      < process.container_register_types.size()
                  && !process.container_register_types[
                          operation.target].fixed) {
                reject(
                    process, index,
                    "LoadMemory target must be a fixed static array");
              }
              for (const auto source :
                   {operation.start, operation.finish}) {
                if (source) {
                  record_use(*source, index);
                  constrain_width(*source, 32U, index);
                }
              }
            },
            [&](const PushContainer& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.target, index, "target");
              record_use(operation.source, index);
              constrain_width(
                  operation.source,
                  process.container_register_types[
                      operation.target].element_width,
                  index);
            },
            [&](const PopContainer& operation) {
              result.uses_containers = true;
              validate_container_register(
                  operation.target, index, "target");
              record_definition(operation.destination, index);
              constrain_width(
                  operation.destination,
                  process.container_register_types[
                      operation.target].element_width,
                  index);
            },
            [&](const FileOpen& operation) {
              result.uses_files = true;
              result.uses_strings = true;
              validate_string_register(
                  operation.path, index, "path");
              validate_string_register(
                  operation.mode, index, "mode");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const FileClose& operation) {
              result.uses_files = true;
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
            },
            [&](const FileWriteLiteral& operation) {
              result.uses_files = true;
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
            },
            [&](const FileWriteFormatted& operation) {
              result.uses_files = true;
              if (operation.width == 0 || operation.width > 64) {
                reject(
                    process, index,
                    "FileWriteFormatted width must be in [1, 64]");
              }
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
              record_use(operation.source, index);
              constrain_width(operation.source, operation.width, index);
            },
            [&](const FileWriteString& operation) {
              result.uses_files = true;
              result.uses_strings = true;
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
              validate_string_register(
                  operation.source, index, "source");
            },
            [&](const FileReadLine& operation) {
              result.uses_files = true;
              result.uses_strings = true;
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
              validate_string_register(
                  operation.target, index, "target");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const FileEndOfFile& operation) {
              result.uses_files = true;
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const FileErrorStatus& operation) {
              result.uses_files = true;
              result.uses_strings = true;
              record_use(operation.handle, index);
              constrain_width(operation.handle, 32U, index);
              validate_string_register(
                  operation.target, index, "target");
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const StringDisplay& operation) {
              result.uses_strings = true;
              result.uses_output = result.uses_output
                  || !operation.postponed;
              result.uses_postponed_output =
                  result.uses_postponed_output
                  || operation.postponed;
              validate_string_register(
                  operation.source, index, "source");
            },
            [&](const UnaryNot &operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              unify_registers(operation.destination, operation.source, index);
            },
            [&](const LogicalNot& operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const LogicalBinary& operation) {
              switch (operation.operation) {
              case LogicalBinaryOperator::logical_and:
              case LogicalBinaryOperator::logical_or:
                break;
              default:
                reject(
                    process, index,
                    "LogicalBinary has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.lhs, index);
              record_use(operation.rhs, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const Reduction& operation) {
              switch (operation.operation) {
              case ReductionOperator::bit_and:
              case ReductionOperator::bit_or:
              case ReductionOperator::bit_xor:
              case ReductionOperator::one_hot:
              case ReductionOperator::one_hot_or_zero:
                break;
              default:
                reject(
                    process, index,
                    "Reduction has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 1U, index);
            },
            [&](const CountOnes& operation) {
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const CountBits& operation) {
              if (operation.state_mask == 0
                  || (operation.state_mask
                      & static_cast<std::uint8_t>(~0x0FU))
                      != 0) {
                reject(
                    process,
                    index,
                    "CountBits has an invalid state mask");
              }
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 32U, index);
            },
            [&](const Shift& operation) {
              switch (operation.operation) {
              case ShiftOperator::logical_left:
              case ShiftOperator::logical_right:
              case ShiftOperator::arithmetic_right:
              case ShiftOperator::arithmetic_left:
              case ShiftOperator::rotate_left:
              case ShiftOperator::rotate_right:
                break;
              default:
                reject(
                    process, index, "Shift has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.value, index);
              record_use(operation.amount, index);
              unify_registers(
                  operation.destination, operation.value, index);
            },
            [&](const Extract& operation) {
              if (operation.width == 0) {
                reject(
                    process, index,
                    "Extract width must be greater than zero");
              }
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(
                  operation.destination, operation.width, index);
            },
            [&](const DynamicExtract& operation) {
              (void)dynamic_range_width(
                  operation.selection, index);
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              record_use(operation.selection.index, index);
              constrain_width(operation.destination, 1U, index);
              constrain_width(
                  operation.selection.index, 32U, index);
            },
            [&](const Insert& operation) {
              record_definition(operation.destination, index);
              record_use(operation.target, index);
              record_use(operation.source, index);
              unify_registers(
                  operation.destination, operation.target, index);
            },
            [&](const DynamicInsert& operation) {
              (void)dynamic_range_width(
                  operation.selection, index);
              record_definition(operation.destination, index);
              record_use(operation.target, index);
              record_use(operation.source, index);
              record_use(operation.selection.index, index);
              constrain_width(operation.source, 1U, index);
              constrain_width(
                  operation.selection.index, 32U, index);
              unify_registers(
                  operation.destination, operation.target, index);
            },
            [&](const Concatenate& operation) {
              if (operation.operands.empty()) {
                reject(
                    process, index,
                    "Concatenate requires at least one operand");
              }
              if (operation.width == 0) {
                reject(
                    process, index,
                    "Concatenate width must be greater than zero");
              }
              record_definition(operation.destination, index);
              for (const auto operand : operation.operands) {
                record_use(operand, index);
              }
              constrain_width(
                  operation.destination, operation.width, index);
            },
            [&](const Binary &operation) {
              switch (operation.operation) {
              case BinaryOperator::bit_and:
              case BinaryOperator::bit_or:
              case BinaryOperator::bit_xor:
              case BinaryOperator::add_unsigned:
              case BinaryOperator::subtract_unsigned:
              case BinaryOperator::multiply_unsigned:
              case BinaryOperator::power_unsigned:
              case BinaryOperator::divide_unsigned:
              case BinaryOperator::modulo_unsigned:
              case BinaryOperator::add_signed:
              case BinaryOperator::subtract_signed:
              case BinaryOperator::multiply_signed:
              case BinaryOperator::power_signed:
              case BinaryOperator::divide_signed:
              case BinaryOperator::remainder_signed:
              case BinaryOperator::modulo_signed:
              case BinaryOperator::equal:
              case BinaryOperator::case_equal:
              case BinaryOperator::casez_equal:
              case BinaryOperator::casex_equal:
              case BinaryOperator::wildcard_equal:
              case BinaryOperator::not_equal:
              case BinaryOperator::less_unsigned:
              case BinaryOperator::less_equal_unsigned:
              case BinaryOperator::greater_unsigned:
              case BinaryOperator::greater_equal_unsigned:
              case BinaryOperator::less_signed:
              case BinaryOperator::less_equal_signed:
              case BinaryOperator::greater_signed:
              case BinaryOperator::greater_equal_signed:
                break;
              default:
                reject(process, index,
                       "Binary has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.lhs, index);
              record_use(operation.rhs, index);
              unify_registers(operation.lhs, operation.rhs, index);
              if (operation.operation == BinaryOperator::equal
                  || operation.operation == BinaryOperator::case_equal
                  || operation.operation == BinaryOperator::casez_equal
                  || operation.operation == BinaryOperator::casex_equal
                  || operation.operation == BinaryOperator::wildcard_equal
                  || operation.operation == BinaryOperator::not_equal
                  || operation.operation
                      == BinaryOperator::less_unsigned
                  || operation.operation
                      == BinaryOperator::less_equal_unsigned
                  || operation.operation
                      == BinaryOperator::greater_unsigned
                  || operation.operation
                      == BinaryOperator::greater_equal_unsigned
                  || operation.operation
                      == BinaryOperator::less_signed
                  || operation.operation
                      == BinaryOperator::less_equal_signed
                  || operation.operation
                      == BinaryOperator::greater_signed
                  || operation.operation
                      == BinaryOperator::greater_equal_signed) {
                constrain_width(operation.destination, 1U, index);
              } else {
                unify_registers(operation.destination, operation.lhs, index);
              }
            },
            [&](const IntegerUnary& operation) {
              switch (operation.operation) {
              case IntegerUnaryOperator::negate:
              case IntegerUnaryOperator::absolute:
                break;
              default:
                reject(
                    process, index,
                    "IntegerUnary has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.source, index);
              constrain_width(operation.destination, 32U, index);
              constrain_width(operation.source, 32U, index);
            },
            [&](const IntegerBinary& operation) {
              switch (operation.operation) {
              case IntegerBinaryOperator::add:
              case IntegerBinaryOperator::subtract:
              case IntegerBinaryOperator::multiply:
              case IntegerBinaryOperator::power:
              case IntegerBinaryOperator::divide:
              case IntegerBinaryOperator::remainder:
              case IntegerBinaryOperator::modulo:
                break;
              default:
                reject(
                    process, index,
                    "IntegerBinary has an invalid operator");
              }
              record_definition(operation.destination, index);
              record_use(operation.lhs, index);
              record_use(operation.rhs, index);
              constrain_width(operation.destination, 32U, index);
              constrain_width(operation.lhs, 32U, index);
              constrain_width(operation.rhs, 32U, index);
            },
            [&](const IntegerCheck& operation) {
              if (operation.lower > operation.upper) {
                reject(
                    process, index,
                    "IntegerCheck has an inverted range");
              }
              record_use(operation.source, index);
              constrain_width(operation.source, 32U, index);
            },
            [&](const ConditionalSelect& operation) {
              record_definition(operation.destination, index);
              record_use(operation.condition, index);
              record_use(operation.when_true, index);
              record_use(operation.when_false, index);
              constrain_width(operation.condition, 1U, index);
              unify_registers(
                  operation.when_true, operation.when_false, index);
              unify_registers(
                  operation.destination, operation.when_true, index);
            },
            [&](const WriteBlocking &operation) {
              record_use(operation.source, index);
              constrain_width(operation.source,
                              signal_width(operation.signal, index), index);
            },
            [&](const Assert &operation) {
              record_use(operation.condition, index);
              constrain_width(operation.condition, 1U, index);
              if (operation.severity
                  != runtime::simir::AssertionSeverity::failure) {
                result.uses_report = true;
              }
            },
            [&](const DebugPoint&) {
              result.uses_debug_points = true;
            },
            [&](const Display& operation) {
              if (operation.postponed) {
                result.uses_postponed_output = true;
              } else {
                result.uses_output = true;
              }
            },
            [&](const FormatDisplay& operation) {
              record_use(operation.source, index);
              result.uses_formatted_output = true;
            },
            [&](const TimeDisplay&) {
              result.uses_time_output = true;
            },
            [&](const MonitorInstall& operation) {
              for (const auto& value : operation.values) {
                if (value.kind == MonitorValueKind::signal) {
                  (void)signal_width(value.signal, index);
                }
              }
              result.uses_monitor_install = true;
            },
            [&](const MonitorControl&) {
              result.uses_monitor_control = true;
            },
            [&](const RandomValue& operation) {
              record_definition(operation.destination, index);
              constrain_width(operation.destination, 32U, index);
              if (operation.maximum) {
                record_use(*operation.maximum, index);
              }
              if (operation.minimum) {
                record_use(*operation.minimum, index);
              }
              if (operation.minimum && !operation.maximum) {
                reject(
                    process,
                    index,
                    "random minimum requires a maximum");
              }
              result.uses_random_value = true;
            },
            [&](const Report&) {
              result.uses_report = true;
            },
            [&](const Jump &operation) {
              validate_target(operation.target, index, "jump");
            },
            [&](const Call& operation) {
              validate_target(operation.target, index, "call");
              validate_target(
                  operation.return_target, index, "call return");
              validate_call_stack(operation.stack, index);
            },
            [&](const Return& operation) {
              validate_call_stack(operation.stack, index);
            },
            [&](const Branch &operation) {
              record_use(operation.condition, index);
              constrain_width(operation.condition, 1U, index);
              validate_target(operation.when_true, index, "branch true");
              validate_target(operation.when_false, index, "branch false");
              switch (operation.unknown_policy) {
              case UnknownBranchPolicy::error:
              case UnknownBranchPolicy::when_false:
                break;
              default:
                reject(process, index, "branch has an invalid unknown policy");
              }
            },
            [&](const Halt &) {},
            [&](const WriteUpdate &operation) {
              record_use(operation.source, index);
              constrain_width(operation.source,
                              signal_width(operation.signal, index), index);
              result.uses_write_update = true;
            },
            [&](const WriteAfter &operation) {
              record_use(operation.source, index);
              constrain_width(operation.source,
                              signal_width(operation.signal, index), index);
              result.uses_write_after = true;
            },
            [&](const WriteInertial& operation) {
              record_use(operation.source, index);
              constrain_width(
                  operation.source,
                  signal_width(operation.signal, index),
                  index);
              result.uses_write_inertial = true;
            },
            [&](const WriteProjected& operation) {
              record_use(operation.source, index);
              constrain_width(
                  operation.source,
                  signal_width(operation.signal, index),
                  index);
              switch (operation.mode) {
                case runtime::simir::ProjectedDelayMode::transport:
                case runtime::simir::ProjectedDelayMode::inertial:
                  break;
                default:
                  reject(
                      process,
                      index,
                      "projected write has an invalid delay mode");
              }
              if (operation.mode
                      == runtime::simir::ProjectedDelayMode::inertial
                  && operation.rejection > operation.delay) {
                reject(
                    process,
                    index,
                    "projected-write rejection exceeds its delay");
              }
              if (operation.mode
                      == runtime::simir::ProjectedDelayMode::transport
                  && operation.rejection != 0) {
                reject(
                    process,
                    index,
                    "transport projected write has a rejection limit");
              }
              result.uses_write_projected = true;
            },
            [&](const WriteProjectedWaveform& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              if (operation.elements.size() < 2) {
                reject(
                    process,
                    index,
                    "projected waveform requires at least two elements");
              }
              if (operation.elements.size()
                  > std::numeric_limits<std::uint32_t>::max()) {
                reject(
                    process,
                    index,
                    "projected waveform has too many elements for the "
                    "runtime ABI");
              }
              std::optional<runtime::SimulationTick> previous_delay;
              for (const auto& element : operation.elements) {
                record_use(element.source, index);
                constrain_width(
                    element.source, target_width, index);
                if (previous_delay
                    && element.delay <= *previous_delay) {
                  reject(
                      process,
                      index,
                      "projected-waveform delays must be strictly "
                      "ascending");
                }
                previous_delay = element.delay;
              }
              switch (operation.mode) {
              case runtime::simir::ProjectedDelayMode::transport:
                if (operation.rejection != 0) {
                  reject(
                      process,
                      index,
                      "transport projected waveform has a rejection limit");
                }
                break;
              case runtime::simir::ProjectedDelayMode::inertial:
                if (operation.rejection
                    > operation.elements.front().delay) {
                  reject(
                      process,
                      index,
                      "projected-waveform rejection exceeds its first "
                      "delay");
                }
                break;
              default:
                reject(
                    process,
                    index,
                    "projected waveform has an invalid delay mode");
              }
              result.uses_write_projected_waveform = true;
            },
            [&](const WriteBlockingSlice& operation) {
              record_use(operation.source, index);
              (void)signal_width(operation.signal, index);
              result.uses_write_blocking_slice = true;
            },
            [&](const WriteUpdateSlice& operation) {
              record_use(operation.source, index);
              (void)signal_width(operation.signal, index);
              result.uses_write_update_slice = true;
            },
            [&](const WriteAfterSlice& operation) {
              record_use(operation.source, index);
              (void)signal_width(operation.signal, index);
              result.uses_write_after_slice = true;
            },
            [&](const WriteInertialSlice& operation) {
              record_use(operation.source, index);
              (void)signal_width(operation.signal, index);
              result.uses_write_inertial_slice = true;
            },
            [&](const WriteProjectedSlice& operation) {
              record_use(operation.source, index);
              (void)signal_width(operation.signal, index);
              switch (operation.mode) {
                case runtime::simir::ProjectedDelayMode::transport:
                case runtime::simir::ProjectedDelayMode::inertial:
                  break;
                default:
                  reject(
                      process,
                      index,
                      "projected slice has an invalid delay mode");
              }
              if (operation.mode
                      == runtime::simir::ProjectedDelayMode::inertial
                  && operation.rejection > operation.delay) {
                reject(
                    process,
                    index,
                    "projected slice rejection exceeds its delay");
              }
              if (operation.mode
                      == runtime::simir::ProjectedDelayMode::transport
                  && operation.rejection != 0) {
                reject(
                    process,
                    index,
                    "transport projected slice has a rejection limit");
              }
              result.uses_write_projected_slice = true;
            },
            [&](const WriteProjectedWaveformSlice& operation) {
              (void)signal_width(operation.signal, index);
              if (operation.elements.size() < 2) {
                reject(
                    process,
                    index,
                    "projected slice waveform requires at least two "
                    "elements");
              }
              if (operation.elements.size()
                  > std::numeric_limits<std::uint32_t>::max()) {
                reject(
                    process,
                    index,
                    "projected slice waveform has too many elements for the "
                    "runtime ABI");
              }
              std::optional<runtime::SimulationTick> previous_delay;
              std::optional<RegisterId> first_source;
              for (const auto& element : operation.elements) {
                record_use(element.source, index);
                if (first_source) {
                  unify_registers(
                      *first_source, element.source, index);
                } else {
                  first_source = element.source;
                }
                if (previous_delay
                    && element.delay <= *previous_delay) {
                  reject(
                      process,
                      index,
                      "projected slice waveform delays must be strictly "
                      "ascending");
                }
                previous_delay = element.delay;
              }
              switch (operation.mode) {
              case runtime::simir::ProjectedDelayMode::transport:
                if (operation.rejection != 0) {
                  reject(
                      process,
                      index,
                      "transport projected slice waveform has a rejection "
                      "limit");
                }
                break;
              case runtime::simir::ProjectedDelayMode::inertial:
                if (operation.rejection
                    > operation.elements.front().delay) {
                  reject(
                      process,
                      index,
                      "projected slice waveform rejection exceeds its first "
                      "delay");
                }
                break;
              default:
                reject(
                    process,
                    index,
                    "projected slice waveform has an invalid delay mode");
              }
              result.uses_write_projected_waveform_slice = true;
            },
            [&](const WriteBlockingDynamicSlice& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              record_use(operation.source, index);
              constrain_width(operation.source, 1U, index);
              validate_dynamic_selection(
                  operation.selection, target_width, index);
              result.uses_write_blocking_slice = true;
            },
            [&](const WriteUpdateDynamicSlice& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              record_use(operation.source, index);
              constrain_width(operation.source, 1U, index);
              validate_dynamic_selection(
                  operation.selection, target_width, index);
              result.uses_write_update_slice = true;
            },
            [&](const WriteAfterDynamicSlice& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              record_use(operation.source, index);
              constrain_width(operation.source, 1U, index);
              validate_dynamic_selection(
                  operation.selection, target_width, index);
              result.uses_write_after_slice = true;
            },
            [&](const WriteInertialDynamicSlice& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              record_use(operation.source, index);
              constrain_width(operation.source, 1U, index);
              validate_dynamic_selection(
                  operation.selection, target_width, index);
              result.uses_write_inertial_slice = true;
            },
            [&](const WriteProjectedDynamicSlice& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              record_use(operation.source, index);
              constrain_width(operation.source, 1U, index);
              validate_dynamic_selection(
                  operation.selection, target_width, index);
              switch (operation.mode) {
              case runtime::simir::ProjectedDelayMode::transport:
                if (operation.rejection != 0) {
                  reject(
                      process,
                      index,
                      "transport projected dynamic slice has a rejection "
                      "limit");
                }
                break;
              case runtime::simir::ProjectedDelayMode::inertial:
                if (operation.rejection > operation.delay) {
                  reject(
                      process,
                      index,
                      "projected dynamic-slice rejection exceeds its "
                      "delay");
                }
                break;
              default:
                reject(
                    process,
                    index,
                    "projected dynamic slice has an invalid delay mode");
              }
              result.uses_write_projected_slice = true;
            },
            [&](const WriteProjectedWaveformDynamicSlice& operation) {
              const auto target_width =
                  signal_width(operation.signal, index);
              validate_dynamic_selection(
                  operation.selection, target_width, index);
              if (operation.elements.size() < 2) {
                reject(
                    process,
                    index,
                    "projected dynamic-slice waveform requires at least "
                    "two elements");
              }
              if (operation.elements.size()
                  > std::numeric_limits<std::uint32_t>::max()) {
                reject(
                    process,
                    index,
                    "projected dynamic-slice waveform has too many "
                    "elements for the runtime ABI");
              }
              std::optional<runtime::SimulationTick> previous_delay;
              for (const auto& element : operation.elements) {
                record_use(element.source, index);
                constrain_width(element.source, 1U, index);
                if (previous_delay
                    && element.delay <= *previous_delay) {
                  reject(
                      process,
                      index,
                      "projected dynamic-slice waveform delays must be "
                      "strictly ascending");
                }
                previous_delay = element.delay;
              }
              switch (operation.mode) {
              case runtime::simir::ProjectedDelayMode::transport:
                if (operation.rejection != 0) {
                  reject(
                      process,
                      index,
                      "transport projected dynamic-slice waveform has a "
                      "rejection limit");
                }
                break;
              case runtime::simir::ProjectedDelayMode::inertial:
                if (operation.rejection
                    > operation.elements.front().delay) {
                  reject(
                      process,
                      index,
                      "projected dynamic-slice waveform rejection exceeds "
                      "its first delay");
                }
                break;
              default:
                reject(
                    process,
                    index,
                    "projected dynamic-slice waveform has an invalid delay "
                    "mode");
              }
              result.uses_write_projected_waveform_slice = true;
            },
            [&](const WaitFor &) {},
            [&](const WaitOn &operation) {
              if (operation.signals.empty()
                  && !operation.timeout) {
                reject(
                    process, index,
                    "WaitOn requires at least one signal or a timeout");
              }
              if (!operation.edges.empty()
                  && operation.edges.size()
                      != operation.signals.size()) {
                reject(
                    process, index,
                    "WaitOn edge count must match its signal count");
              }
              if (!operation.timeout
                  && (operation.timeout_result
                      || operation.timeout_origin)) {
                reject(
                    process,
                    index,
                    "WaitOn timeout metadata requires a timeout");
              }
              if (operation.timeout_origin
                  && !operation.timeout_result) {
                reject(
                    process,
                    index,
                    "WaitOn timeout rearm requires a result register");
              }
              if (operation.timeout_result) {
                record_definition(
                    *operation.timeout_result, index);
                constrain_width(
                    *operation.timeout_result, 1U, index);
              }
              if (operation.timeout_origin) {
                if (*operation.timeout_origin >= index) {
                  reject(
                      process,
                      index,
                      "WaitOn timeout origin must precede its rearm");
                }
                const auto* origin = std::get_if<WaitOn>(
                    &process.operations[*operation.timeout_origin]);
                if (origin == nullptr
                    || !origin->timeout
                    || origin->timeout_origin
                    || origin->timeout
                        != operation.timeout
                    || origin->timeout_result
                        != operation.timeout_result
                    || origin->signals
                        != operation.signals
                    || origin->edges
                        != operation.edges) {
                  reject(
                      process,
                      index,
                      "WaitOn timeout rearm does not match its origin");
                }
              }
              for (std::size_t signal_index = 0;
                   signal_index < operation.signals.size();
                   ++signal_index) {
                const auto width = referenced_signal_width(
                    operation.signals[signal_index], index);
                const auto edge =
                    operation.edges.empty()
                        ? EdgeKind::any
                        : operation.edges[signal_index];
                switch (edge) {
                case EdgeKind::any:
                  break;
                case EdgeKind::posedge:
                case EdgeKind::negedge:
                  if (width != 1) {
                    reject(
                        process, index,
                        "WaitOn edge requires a scalar signal");
                  }
                  break;
                default:
                  reject(
                      process, index,
                      "WaitOn has an invalid edge kind");
                }
              }
            },
            [&](const WaitSensitivity &) {
              if (process.static_sensitivity.empty()) {
                reject(process, index,
                       "WaitSensitivity requires a static sensitivity list");
              }
            },
            [&](const WaitForever &) {},
            [&](const Yield &) {},
            [&](const Pause &) {},
            [&](const Stop &) {}},
        process.operations[index]);
  }

  for (std::size_t index = 0; index < process.register_count; ++index) {
    const auto width =
        root_widths[find_root(static_cast<RegisterId>(index))];
    if (width <= std::numeric_limits<std::uint32_t>::max()) {
      result.register_widths[index] = static_cast<std::uint32_t>(width);
    }
  }
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    for (const auto definition : instruction_definitions[index]) {
      if (result.register_widths[definition] == 0) {
        reject(process, index, "register width cannot be inferred");
      }
    }
    for (const auto used : instruction_uses[index]) {
      if (!defined[used]) {
        reject(process, index, "source register is never defined");
      }
      if (result.register_widths[used] == 0) {
        reject(process, index, "source register width cannot be inferred");
      }
    }
    if (const auto* extract =
            std::get_if<Extract>(&process.operations[index])) {
      const auto source_width =
          result.register_widths[extract->source];
      if (extract->offset > source_width
          || extract->width
              > source_width - extract->offset) {
        reject(
            process, index,
            "Extract range is outside its source register");
      }
    }
    if (const auto* extract =
            std::get_if<DynamicExtract>(
                &process.operations[index])) {
      validate_dynamic_bounds(
          extract->selection,
          result.register_widths[extract->source],
          index);
    }
    if (const auto* insert =
            std::get_if<Insert>(&process.operations[index])) {
      const auto target_width =
          result.register_widths[insert->target];
      const auto source_width =
          result.register_widths[insert->source];
      if (insert->offset > target_width
          || source_width
              > target_width - insert->offset) {
        reject(
            process, index,
            "Insert range is outside its target register");
      }
    }
    if (const auto* insert =
            std::get_if<DynamicInsert>(
                &process.operations[index])) {
      validate_dynamic_bounds(
          insert->selection,
          result.register_widths[insert->target],
          index);
    }
    if (const auto* concatenate =
            std::get_if<Concatenate>(&process.operations[index])) {
      std::uint64_t width = 0;
      for (const auto operand : concatenate->operands) {
        width += result.register_widths[operand];
      }
      if (width != concatenate->width) {
        reject(
            process, index,
            "Concatenate operand widths do not match its result width");
      }
    }
    const auto validate_slice_write =
        [&](const auto& write) {
          const auto target_width =
              signal_widths[write.signal];
          const auto source_width =
              result.register_widths[write.source];
          if (write.offset > target_width
              || source_width
                  > target_width - write.offset) {
            reject(
                process, index,
                "partial write range is outside its target signal");
          }
        };
    if (const auto* blocking_write =
            std::get_if<WriteBlockingSlice>(
                &process.operations[index])) {
      validate_slice_write(*blocking_write);
    } else if (const auto* update_write =
                   std::get_if<WriteUpdateSlice>(
                       &process.operations[index])) {
      validate_slice_write(*update_write);
    } else if (const auto* delayed_write =
                   std::get_if<WriteAfterSlice>(
                       &process.operations[index])) {
      validate_slice_write(*delayed_write);
    } else if (const auto* inertial_write =
                   std::get_if<WriteInertialSlice>(
                       &process.operations[index])) {
      validate_slice_write(*inertial_write);
    } else if (const auto* projected_write =
                   std::get_if<WriteProjectedSlice>(
                       &process.operations[index])) {
      validate_slice_write(*projected_write);
    } else if (const auto* waveform_write =
                   std::get_if<WriteProjectedWaveformSlice>(
                       &process.operations[index])) {
      const auto target_width =
          signal_widths[waveform_write->signal];
      for (const auto& element : waveform_write->elements) {
        const auto source_width =
            result.register_widths[element.source];
        if (waveform_write->offset > target_width
            || source_width
                > target_width - waveform_write->offset) {
          reject(
              process,
              index,
              "partial waveform range is outside its target signal");
        }
      }
    }
  }

  std::vector<std::vector<std::size_t>> successors(
      process.operations.size());
  std::vector<std::vector<std::size_t>> predecessors(
      process.operations.size());
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    const auto &operation = process.operations[index];
    if (std::holds_alternative<Halt>(operation) ||
        std::holds_alternative<Stop>(operation) ||
        std::holds_alternative<Return>(operation)) {
      continue;
    }
    if (const auto *jump = std::get_if<Jump>(&operation)) {
      successors[index].push_back(jump->target);
    } else if (const auto* call = std::get_if<Call>(&operation)) {
      successors[index].push_back(call->target);
      if (call->return_target != call->target) {
        successors[index].push_back(call->return_target);
      }
    } else if (const auto *branch = std::get_if<Branch>(&operation)) {
      successors[index].push_back(branch->when_true);
      if (branch->when_false != branch->when_true) {
        successors[index].push_back(branch->when_false);
      }
    } else {
      if (index + 1 >= process.operations.size()) {
        reject(process, index,
               "control flow falls outside the operation stream");
      }
      successors[index].push_back(index + 1);
    }
    for (const auto successor : successors[index]) {
      predecessors[successor].push_back(index);
    }
  }

  std::vector<bool> reachable(process.operations.size());
  std::vector<std::size_t> pending{0};
  while (!pending.empty()) {
    const auto instruction = pending.back();
    pending.pop_back();
    if (reachable[instruction]) {
      continue;
    }
    reachable[instruction] = true;
    pending.insert(pending.end(), successors[instruction].begin(),
                   successors[instruction].end());
  }
  const auto is_suspension = [](const Operation &operation) {
    return std::holds_alternative<WaitFor>(operation) ||
           std::holds_alternative<WaitOn>(operation) ||
           std::holds_alternative<WaitSensitivity>(operation) ||
           std::holds_alternative<WaitForever>(operation) ||
           std::holds_alternative<Yield>(operation) ||
           std::holds_alternative<Pause>(operation);
  };
  const auto is_cycle_safe_point =
      [&](const Operation& operation) {
        return is_suspension(operation)
            || std::holds_alternative<DebugPoint>(operation);
      };
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (reachable[index] && is_suspension(process.operations[index])) {
      result.requires_resume = true;
    }
  }

  std::vector<std::vector<std::size_t>> invocation_successors(
      process.operations.size());
  std::vector<std::vector<std::size_t>> invocation_predecessors(
      process.operations.size());
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (!reachable[index] ||
        is_cycle_safe_point(process.operations[index]) ||
        std::holds_alternative<Stop>(process.operations[index]) ||
        std::holds_alternative<Halt>(process.operations[index])) {
      continue;
    }
    invocation_successors[index] = successors[index];
    for (const auto successor : invocation_successors[index]) {
      invocation_predecessors[successor].push_back(index);
    }
  }
  std::vector<std::size_t> remaining_predecessors(
      process.operations.size());
  std::vector<std::size_t> acyclic_pending;
  std::size_t reachable_count = 0;
  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    ++reachable_count;
    remaining_predecessors[index] =
        static_cast<std::size_t>(std::count_if(
            invocation_predecessors[index].begin(),
            invocation_predecessors[index].end(),
            [&](const std::size_t predecessor) {
              return reachable[predecessor];
            }));
    if (remaining_predecessors[index] == 0) {
      acyclic_pending.push_back(index);
    }
  }
  std::size_t acyclic_count = 0;
  while (!acyclic_pending.empty()) {
    const auto instruction = acyclic_pending.back();
    acyclic_pending.pop_back();
    ++acyclic_count;
    for (const auto successor : invocation_successors[instruction]) {
      if (!reachable[successor]) {
        continue;
      }
      --remaining_predecessors[successor];
      if (remaining_predecessors[successor] == 0) {
        acyclic_pending.push_back(successor);
      }
    }
  }
  if (acyclic_count != reachable_count) {
    const auto cycle = std::find_if(
        remaining_predecessors.begin(), remaining_predecessors.end(),
        [](const std::size_t count) { return count != 0; });
    record_unsupported(
        static_cast<std::size_t>(
            std::distance(remaining_predecessors.begin(), cycle)),
        "reachable control-flow cycle has no suspension safe point");
  }

  std::vector<std::vector<bool>> definitely_defined_in(
      process.operations.size(),
      std::vector<bool>(process.register_count, true));
  std::vector<std::vector<bool>> definitely_defined_out =
      definitely_defined_in;
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
      if (!reachable[index]) {
        continue;
      }
      std::vector<bool> incoming(process.register_count, true);
      if (index == 0) {
        std::fill(incoming.begin(), incoming.end(), false);
      } else {
        bool saw_predecessor = false;
        for (const auto predecessor : predecessors[index]) {
          if (!reachable[predecessor]) {
            continue;
          }
          if (!saw_predecessor) {
            incoming = definitely_defined_out[predecessor];
            saw_predecessor = true;
          } else {
            for (std::size_t reg = 0; reg < process.register_count; ++reg) {
              incoming[reg] =
                  incoming[reg] &&
                  definitely_defined_out[predecessor][reg];
            }
          }
        }
      }
      auto outgoing = incoming;
      for (const auto definition : instruction_definitions[index]) {
        outgoing[definition] = true;
      }
      if (incoming != definitely_defined_in[index] ||
          outgoing != definitely_defined_out[index]) {
        definitely_defined_in[index] = std::move(incoming);
        definitely_defined_out[index] = std::move(outgoing);
        changed = true;
      }
    }
  }

  for (std::size_t index = 0; index < process.operations.size(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    for (const auto used : instruction_uses[index]) {
      if (!definitely_defined_in[index][used]) {
        reject(process, index,
               "register may be used before definition on a control-flow "
               "path");
      }
    }
  }
  if (unsupported) {
    reject_unsupported(
        process, unsupported->first, unsupported->second);
  }
  return result;
}

}  // namespace fsim::compiler::llvm_detail
