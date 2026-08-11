// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_string.hpp"
#include "llvm_jit_internal.hpp"
#include "llvm_jit_validation_class.hpp"
#include "llvm_jit_validation_signal.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>
namespace fsim::compiler::llvm_detail {
using runtime::Logic9;
using namespace runtime::simir;
[[nodiscard]] ValidatedProcess validate_process(
    const Process& process, const std::span<const std::uint32_t> signal_widths,
    const std::span<const ValueKind> signal_value_kinds)
{
    validate_process_shape(process, signal_widths, signal_value_kinds);
    ValidatedProcess result;
    result.uses_logic9 = std::ranges::any_of(
        process.register_value_kinds,
        [](const ValueKind kind) { return kind == ValueKind::logic9; });
    result.register_widths.resize(process.register_count);
    std::vector<RegisterId> parents(process.register_count);
    std::vector<std::size_t> root_widths(process.register_count);
    for (std::size_t index = 0; index < parents.size(); ++index)
        parents[index] = static_cast<RegisterId>(index);
    std::vector<bool> defined(process.register_count);
    std::vector<std::vector<RegisterId>> instruction_definitions(process.operations.size());
    std::vector<std::vector<RegisterId>> instruction_uses(process.operations.size());
    std::optional<std::pair<std::size_t, std::string>> unsupported;
    const auto record_unsupported =
        [&](const std::size_t instruction, const std::string_view message) {
            if (!unsupported)
                unsupported.emplace(instruction, message);
        };
    const auto referenced_signal_width =
        [&](const std::uint32_t signal,
            const std::size_t instruction) -> std::uint32_t {
        if (signal >= signal_widths.size())
            reject(process, instruction, "signal ID is outside signal_widths");
        const auto width = signal_widths[signal];
        if (width == 0)
            reject(process, instruction, "signal width must be greater than zero");
        return width;
    };
    const auto signal_width =
        [&](const std::uint32_t signal,
            const std::size_t instruction) -> std::uint32_t {
        const auto width = referenced_signal_width(signal, instruction);
        if (width > 64)
            record_unsupported(
                instruction,
                "the LLVM scalar subset requires signal widths in [1, 64]");
        if (!signal_value_kinds.empty()
            && signal_value_kinds[signal] == ValueKind::logic9)
            result.uses_logic9 = true;
        return width;
    };
    const auto exact_signal_width =
        [&](const std::uint32_t signal,
            const std::size_t instruction) -> std::uint32_t {
        const auto width = referenced_signal_width(signal, instruction);
        if (!signal_value_kinds.empty()
            && signal_value_kinds[signal] == ValueKind::logic9) {
            result.uses_logic9 = true;
            if (width > 64) {
                record_unsupported(
                    instruction,
                    "wide exact-Logic9 signals are not yet supported");
            }
        }
        return width;
    };
    const auto validate_register =
        [&](const RegisterId id, const std::size_t instruction,
            const std::string_view role) {
            if (id >= process.register_count) {
                reject(process, instruction,
                    std::string { role } + " register ID is out of range");
            }
        };
    const auto validate_string_register =
        [&](const StringRegisterId id,
            const std::size_t instruction,
            const std::string_view role) {
            if (id >= process.string_register_count) {
                reject(process, instruction,
                    std::string { role } + " string register ID is out of range");
            }
        };
    const auto validate_container_register =
        [&](const ContainerRegisterId id,
            const std::size_t instruction,
            const std::string_view role) {
            if (id >= process.container_register_count
                || process.container_register_types.size()
                    != process.container_register_count) {
                reject(process, instruction,
                    std::string { role } + " container register is out of range");
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
            if (root_widths[left_root] != 0 && root_widths[right_root] != 0 && root_widths[left_root] != root_widths[right_root]) {
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
    const auto validate_dynamic_selection =
        [&](const DynamicIndex& selection,
            const std::uint64_t target_width,
            const std::size_t instruction) {
            record_use(selection.index, instruction);
            constrain_width(selection.index, 32U, instruction);
            if (const auto error = validate_dynamic_index_bounds(selection, target_width)) {
                reject(process, instruction, *error);
            }
        };
    const auto validate_dynamic_part_selection =
        [&](const DynamicPartIndex& selection,
            const std::uint64_t target_width,
            const std::size_t instruction) {
            record_use(selection.base, instruction);
            constrain_width(selection.base, 32U, instruction);
            if (const auto error = validate_dynamic_part_index_bounds(
                    selection, target_width)) {
                reject(process, instruction, *error);
            }
        };
    const auto validate_target = [&](const InstructionIndex target,
                                     const std::size_t instruction,
                                     const std::string_view kind) {
        if (target >= process.operations.size()) {
            reject(process, instruction,
                std::string { kind } + " target is outside the operation stream");
        }
    };
    const auto validate_call_stack =
        [&](const CallStack& stack, const std::size_t instruction) {
            if (stack.capacity == 0) {
                if (stack.pointer != 0 || stack.entries != 0) {
                    reject(process, instruction,
                        "dynamic call stack must not name fixed registers");
                }
                return;
            }
            const auto end = static_cast<std::uint64_t>(stack.entries)
                + stack.capacity;
            if (end > process.register_count) {
                reject(process, instruction,
                    "call-stack register range is outside register_count");
            }
            record_use(stack.pointer, instruction);
            constrain_width(stack.pointer, 32U, instruction);
            for (std::uint32_t offset = 0;
                offset < stack.capacity; ++offset) {
                const auto entry = static_cast<RegisterId>(stack.entries + offset);
                record_use(entry, instruction);
                constrain_width(entry, 32U, instruction);
            }
        };
    const auto first_wait_sensitivity = std::find_if(
        process.operations.begin(), process.operations.end(),
        [](const Operation& operation) {
            return fsim::runtime::simir::operation_holds<WaitSensitivity>(operation);
        });
    const auto sensitivity_instruction = first_wait_sensitivity == process.operations.end()
        ? std::size_t { 0 }
        : static_cast<std::size_t>(
              std::distance(
                  process.operations.begin(), first_wait_sensitivity));
    for (const auto sensitivity : process.static_sensitivity) {
        const auto width = referenced_signal_width(
            sensitivity.signal, sensitivity_instruction);
        switch (sensitivity.edge) {
        case EdgeKind::any:
        case EdgeKind::transaction:
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
        fsim::runtime::simir::visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if (validate_output_signal_operation(
                        operation, process, index, result,
                        exact_signal_width, referenced_signal_width,
                        signal_width, record_use, constrain_width,
                        record_unsupported, validate_target,
                        validate_call_stack, record_definition,
                        unify_registers, validate_dynamic_selection,
                        validate_dynamic_part_selection)) {
                    return;
                }
                if constexpr (std::is_same_v<OperationType, LoadConstant>) {
                    if (operation.value.width() == 0) {
                        reject(process, index,
                            "LoadConstant width must be greater than zero");
                    }
                    if (operation.value.width() > 64
                        && operation.value.is_logic9()) {
                        record_unsupported(
                            index, "wide exact-Logic9 constants are not yet supported");
                    }
                    result.uses_logic9 = result.uses_logic9
                        || operation.value.is_logic9();
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, operation.value.width(),
                        index);
                } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
                    const auto width = exact_signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, width, index);
                    result.uses_exact_signal_operation = result.uses_exact_signal_operation || width > 64;
                } else if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                    result.uses_signal_event = true;
                    (void)signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
                    result.uses_signal_last_value = true;
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        signal_width(operation.signal, index),
                        index);
                } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                    result.uses_signal_last_event = true;
                    (void)signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ReadSimulationTime>) {
                    result.uses_simulation_time = true;
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, VitalTimingCheck>) {
                    result.uses_vital_timing = true;
                    const auto test_width = signal_width(
                        operation.test_signal, index);
                    if (operation.test_offset >= test_width) {
                        reject(
                            process, index,
                            "VitalTimingCheck test offset is outside the signal");
                    }
                    if (operation.reference_signal) {
                        const auto reference_width = signal_width(
                            *operation.reference_signal, index);
                        if (operation.reference_offset >= reference_width) {
                            reject(
                                process, index,
                                "VitalTimingCheck reference offset is outside the signal");
                        }
                    }
                    if (operation.trigger_signal
                        && signal_width(*operation.trigger_signal, index) != 1U) {
                        reject(
                            process, index,
                            "VitalTimingCheck trigger signal must be scalar");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
                    result.uses_vital_delay = true;
                    if (signal_width(operation.output, index) != 1U) {
                        reject(process, index, "VitalDelay output signal must be scalar");
                    }
                    record_use(operation.source, index);
                    constrain_width(operation.source, 1U, index);
                    if (operation.shape == VitalDelayShape::delay01z) {
                        record_use(operation.output_map, index);
                        constrain_width(operation.output_map, 9U, index);
                    }
                    for (const auto delay : operation.default_delays) {
                        record_use(delay, index);
                        constrain_width(delay, 64U, index);
                    }
                    for (const auto& path : operation.paths) {
                        record_use(path.input_change_time, index);
                        constrain_width(path.input_change_time, 64U, index);
                        record_use(path.condition, index);
                        constrain_width(path.condition, 1U, index);
                        for (const auto delay : path.delays) {
                            record_use(delay, index);
                            constrain_width(delay, 64U, index);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, SignalActive>) {
                    result.uses_signal_active = true;
                    (void)signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalLastActive>) {
                    result.uses_signal_last_active = true;
                    (void)signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDriving>) {
                    result.uses_signal_driving = true;
                    (void)signal_width(operation.signal, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SignalDrivingValue>) {
                    result.uses_signal_driving_value = true;
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        signal_width(operation.signal, index),
                        index);
                } else if constexpr (
                    operation_group_contains_v<OperationType, ClassOperationGroup>) {
                    if constexpr (
                        std::is_same_v<OperationType, ClassMethodCall>
                        || std::is_same_v<OperationType, ClassStaticMethodCall>) {
                        result.uses_strings = result.uses_strings
                            || std::ranges::find(operation.actual_kinds, 1U)
                                != operation.actual_kinds.end();
                    }
                    validate_class_operation(
                        process, index, operation, record_use, record_definition,
                        constrain_width, validate_string_register);
                } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    unify_registers(
                        operation.destination, operation.source, index);
                } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    if (operation.value.size() > maximum_string_bytes)
                        reject(process, index, "LoadStringConstant exceeds the byte limit");
                    if (!runtime::systemverilog_string_is_valid(operation.value))
                        reject(process, index, "LoadStringConstant is not strict UTF-8");
                } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    validate_string_register(operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    (void)operation.object;
                } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
                    result.uses_strings = true;
                    validate_string_register(operation.source, index, "source");
                    (void)operation.object;
                } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    for (const auto operand : operation.operands)
                        validate_string_register(operand, index, "source");
                } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
                    result.uses_strings = true;
                    validate_string_register(operation.lhs, index, "left");
                    validate_string_register(operation.rhs, index, "right");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, StringLength>) {
                    result.uses_strings = true;
                    validate_string_register(operation.source, index, "source");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
                    result.uses_strings = true;
                    validate_string_register(operation.source, index, "source");
                    record_use(operation.index, index);
                    constrain_width(operation.index, 32U, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, StringReplaceCodePoint>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.target, index, "target");
                    record_use(operation.index, index);
                    record_use(operation.source, index);
                    constrain_width(operation.index, 32U, index);
                    constrain_width(operation.source, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
                    result.uses_strings = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_string_method_metadata(
                            operation, process, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        if (value.width != 0)
                            constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType,
                                         SystemVerilogScalarBinary>) {
                    result.uses_containers = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_scalar_binary_metadata(operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, ResizeContainer>) {
                    result.uses_containers = true;
                    validate_container_register(operation.target, index, "target");
                    if (operation.initializer)
                        validate_container_register(*operation.initializer, index, "initializer");
                    record_use(operation.size, index);
                    constrain_width(operation.size, 32U, index);
                    if (operation.allow_queue
                        && operation.target
                            < process.container_register_types.size()
                        && (!process.container_register_types[operation.target].queue
                            || operation.initializer)) {
                        reject(
                            process, index,
                            "internal queue resize requires a queue target "
                            "without an initializer");
                    }
                } else if constexpr (std::is_same_v<OperationType, CopyContainerRegister>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.destination, index, "destination");
                    validate_container_register(
                        operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, ConditionalContainerSelect>) {
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
                        && (process.container_register_types[operation.destination]
                                != process.container_register_types[operation.when_true]
                            || process.container_register_types[operation.destination]
                                != process.container_register_types[operation.when_false])) {
                        reject(
                            process, index,
                            "ConditionalContainerSelect profiles differ");
                    }
                } else if constexpr (std::is_same_v<OperationType, CompareContainers>) {
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
                } else if constexpr (std::is_same_v<OperationType, ReadContainerObject>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.destination, index, "destination");
                    (void)operation.object;
                } else if constexpr (std::is_same_v<OperationType, WriteContainerObject>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    (void)operation.object;
                    if (operation.transaction_signal) {
                        (void)referenced_signal_width(
                            *operation.transaction_signal, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, ContainerSize>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, ContainerReduction>) {
                    result.uses_containers = true;
                    validate_container_register(operation.source, index, "source");
                    if (static_cast<std::uint8_t>(operation.operation) > static_cast<std::uint8_t>(
                            ContainerReductionOperator::bit_xor)) {
                        reject(process, index, "ContainerReduction has an invalid operator");
                    }
                    record_definition(operation.destination, index);
                    if (operation.source
                        < process.container_register_types.size()) {
                        if (const auto error = validate_container_reduction_metadata(
                                operation,
                                process.container_register_types[operation.source])) {
                            reject(process, index, *error);
                        }
                        constrain_width(
                            operation.destination,
                            process.container_register_types[operation.source]
                                .element_width,
                            index);
                    }
                } else if constexpr (std::is_same_v<OperationType, OrderContainer>) {
                    result.uses_containers = true;
                    validate_container_register(operation.target, index, "target");
                    if (static_cast<std::uint8_t>(operation.operation) > static_cast<std::uint8_t>(
                            ContainerOrderingOperator::shuffle)) {
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
                        if (const auto error = validate_container_ordering_metadata(
                                operation,
                                process.container_register_types[operation.target])) {
                            reject(process, index, *error);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, LocateContainer>) {
                    result.uses_containers = true;
                    validate_container_register(operation.destination, index,
                        "destination");
                    validate_container_register(operation.source, index, "source");
                    if (operation.destination
                            < process.container_register_types.size()
                        && operation.source
                            < process.container_register_types.size()) {
                        if (const auto error = validate_container_locator_metadata(
                                operation,
                                process.container_register_types[operation.destination],
                                process.container_register_types[operation.source])) {
                            reject(process, index, *error);
                        }
                        if (const auto error = validate_container_locator_transformation_metadata(
                                operation,
                                process.container_register_types[operation.source])) {
                            reject(process, index, *error);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, ContainerRead>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    if (operation.source
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.source];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container read index kind does not match its profile");
                        }
                        if (!operation.string_index && type.associative) {
                            constrain_width(
                                operation.index, type.index_width, index);
                        }
                    }
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        process.container_register_types[operation.source].element_width,
                        index);
                } else if constexpr (std::is_same_v<OperationType, ContainerWrite>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    if (operation.target
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.target];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container write index kind does not match its profile");
                        }
                        if (!operation.string_index && type.associative) {
                            constrain_width(
                                operation.index, type.index_width, index);
                        }
                    }
                    record_use(operation.source, index);
                    constrain_width(
                        operation.source,
                        process.container_register_types[operation.target].element_width,
                        index);
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerStringRead>
                    || std::is_same_v<
                        OperationType, ContainerStringWrite>) {
                    result.uses_containers = true;
                    result.uses_strings = true;
                    const auto container = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerStringRead>) {
                            return operation.source;
                        } else {
                            return operation.target;
                        }
                    }();
                    validate_container_register(
                        container, index,
                        std::is_same_v<OperationType, ContainerStringRead>
                            ? "source"
                            : "target");
                    if (operation.string_index) {
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    if (container < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[container];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "string element index kind does not match its profile");
                        }
                        if (!operation.string_index
                            && (type.fixed || type.associative)) {
                            constrain_width(
                                operation.index,
                                type.fixed ? 32U : type.index_width,
                                index);
                        }
                        if (type.element_kind != ContainerElementKind::String) {
                            reject(
                                process, index,
                                "string element operation requires a string container");
                        }
                    }
                    if constexpr (std::is_same_v<
                                      OperationType,
                                      ContainerStringRead>) {
                        validate_string_register(
                            operation.destination, index, "destination");
                    } else {
                        validate_string_register(
                            operation.source, index, "source");
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerElementRead>
                    || std::is_same_v<
                        OperationType, ContainerElementWrite>) {
                    result.uses_containers = true;
                    const auto container = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerElementRead>) {
                            return operation.source;
                        } else {
                            return operation.target;
                        }
                    }();
                    const auto element = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerElementRead>) {
                            return operation.destination;
                        } else {
                            return operation.source;
                        }
                    }();
                    validate_container_register(container, index, "outer");
                    validate_container_register(element, index, "nested");
                    record_use(operation.index, index);
                    if (container < process.container_register_types.size()
                        && element < process.container_register_types.size()) {
                        const auto& outer = process.container_register_types[container];
                        if (outer.fixed || outer.associative) {
                            constrain_width(
                                operation.index,
                                outer.fixed ? 32U : outer.index_width,
                                index);
                        }
                        if (outer.element_kind != ContainerElementKind::Container
                            || outer.element_types.size() != 1
                            || outer.element_types.front()
                                != process.container_register_types[element]) {
                            reject(
                                process, index,
                                "nested container element profiles differ");
                        }
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerAggregateRead>
                    || std::is_same_v<
                        OperationType, ContainerAggregateWrite>) {
                    result.uses_containers = true;
                    const auto container = [&] {
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerAggregateRead>) {
                            return operation.source;
                        } else {
                            return operation.target;
                        }
                    }();
                    validate_container_register(
                        container, index,
                        std::is_same_v<
                            OperationType, ContainerAggregateRead>
                            ? "source"
                            : "target");
                    record_use(operation.index, index);
                    if (container < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[container];
                        if (type.fixed || type.associative) {
                            constrain_width(
                                operation.index,
                                type.fixed ? 32U : type.index_width,
                                index);
                        }
                        const ContainerType* selected = &type;
                        for (const auto member : operation.members) {
                            if (selected->element_kind
                                    != ContainerElementKind::Aggregate
                                || member >= selected->element_types.size()) {
                                reject(
                                    process, index,
                                    "aggregate member operation path is invalid");
                            }
                            selected = &selected->element_types[member];
                        }
                        if (operation.members.empty()
                            || !selected->fixed
                            || selected->element_width == 0) {
                            reject(
                                process, index,
                                "aggregate member operation requires a packed or "
                                "scalar leaf");
                        }
                        if constexpr (std::is_same_v<
                                          OperationType,
                                          ContainerAggregateRead>) {
                            record_definition(operation.destination, index);
                            constrain_width(
                                operation.destination,
                                selected->element_width, index);
                        } else {
                            record_use(operation.source, index);
                            constrain_width(
                                operation.source,
                                selected->element_width, index);
                        }
                    }
                } else if constexpr (std::is_same_v<
                                         OperationType, CopyContainerAggregateElement>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    validate_container_register(
                        operation.source, index, "source");
                    record_use(operation.target_index, index);
                    record_use(operation.source_index, index);
                    if (operation.target
                            < process.container_register_types.size()
                        && operation.source
                            < process.container_register_types.size()) {
                        const auto& target_type = process.container_register_types[operation.target];
                        const auto& source_type = process.container_register_types[operation.source];
                        if (target_type.fixed || target_type.associative) {
                            constrain_width(
                                operation.target_index,
                                target_type.fixed ? 32U : target_type.index_width,
                                index);
                        }
                        if (source_type.fixed || source_type.associative) {
                            constrain_width(
                                operation.source_index,
                                source_type.fixed ? 32U : source_type.index_width,
                                index);
                        }
                        if (target_type != source_type
                            || target_type.element_kind
                                != ContainerElementKind::Aggregate) {
                            reject(
                                process, index,
                                "aggregate element copy profiles differ");
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, DeleteContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    if (operation.index) {
                        if (operation.string_index) {
                            result.uses_strings = true;
                            validate_string_register(
                                *operation.index, index, "index");
                        } else {
                            record_use(*operation.index, index);
                        }
                        if (operation.target
                            < process.container_register_types.size()) {
                            const auto& type = process.container_register_types[operation.target];
                            if (type.string_indices != operation.string_index) {
                                reject(process, index,
                                    "container delete index kind does not match its profile");
                            }
                            if (!operation.string_index) {
                                constrain_width(
                                    *operation.index, type.index_width, index);
                            }
                        }
                    } else if (operation.string_index) {
                        reject(process, index,
                            "container delete string index is missing");
                    }
                } else if constexpr (std::is_same_v<OperationType, ContainerExists>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    if (operation.source
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.source];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container exists index kind does not match its profile");
                        }
                        if (!operation.string_index) {
                            constrain_width(operation.index, type.index_width, index);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, TraverseContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.source, index, "source");
                    if (operation.string_index) {
                        result.uses_strings = true;
                        validate_string_register(
                            operation.index, index, "index");
                    } else {
                        record_use(operation.index, index);
                        record_definition(operation.index, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    if (operation.source
                        < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.source];
                        if (type.string_indices != operation.string_index) {
                            reject(process, index,
                                "container traversal index kind does not match its profile");
                        }
                        if (!operation.string_index) {
                            constrain_width(operation.index, type.index_width, index);
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, LoadMemory>) {
                    result.uses_containers = true;
                    result.uses_strings = true;
                    result.uses_files = true;
                    validate_container_register(
                        operation.target, index, "target");
                    validate_string_register(
                        operation.path, index, "path");
                    if (operation.target < process.container_register_types.size()) {
                        const auto& type = process.container_register_types[operation.target];
                        if (!type.fixed)
                            reject(process, index,
                                "LoadMemory target must be a fixed static array");
                    }
                    for (const auto source :
                        { operation.start, operation.finish }) {
                        if (source) {
                            record_use(*source, index);
                            constrain_width(*source, 32U, index);
                        }
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, VitalMemoryDeclare>) {
                    result.uses_containers = true;
                    result.uses_strings = true;
                    result.uses_files = result.uses_files
                        || !operation.embedded_load;
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    validate_string_register(
                        operation.load_file, index, "load-file");
                    if (operation.word_count == 0U
                        || operation.word_width == 0U
                        || operation.subword_width == 0U
                        || operation.subword_width > operation.word_width) {
                        reject(
                            process, index,
                            "VitalMemoryDeclare geometry is invalid");
                    }
                } else if constexpr (std::is_same_v<OperationType, PushContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    record_use(operation.source, index);
                    constrain_width(
                        operation.source,
                        process.container_register_types[operation.target].element_width,
                        index);
                    if (operation.index) {
                        record_use(*operation.index, index);
                        constrain_width(*operation.index, 32U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, PopContainer>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.target, index, "target");
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        process.container_register_types[operation.target].element_width,
                        index);
                } else if constexpr (std::is_same_v<OperationType, FileOpen>) {
                    result.uses_files = true;
                    result.uses_strings = true;
                    validate_string_register(
                        operation.path, index, "path");
                    validate_string_register(
                        operation.mode, index, "mode");
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                    if (operation.status) {
                        record_definition(*operation.status, index);
                        constrain_width(*operation.status, 2U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FileClose>) {
                    result.uses_files = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    if (operation.clear_handle) {
                        record_definition(operation.handle, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FileWriteLiteral>) {
                    result.uses_files = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileWriteFormatted>) {
                    result.uses_files = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_file_write_metadata(operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FileWriteString>) {
                    result.uses_files = true;
                    result.uses_strings = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    validate_string_register(
                        operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, FileReadLine>) {
                    result.uses_files = true;
                    if (operation.kind > FileReadKind::unget)
                        reject(process, index, "FileReadLine kind is invalid");
                    if (operation.target_kind > FileTextTargetKind::packed_signal)
                        reject(process, index, "FileReadLine target kind is invalid");
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    if (operation.kind == FileReadKind::line) {
                        if (operation.target_kind
                            == FileTextTargetKind::string_register) {
                            result.uses_strings = true;
                            validate_string_register(operation.target, index, "target");
                        } else if (operation.target_width == 0) {
                            reject(process, index, "FileReadLine packed target width is zero");
                        } else if (operation.target_kind
                            == FileTextTargetKind::packed_register) {
                            record_definition(operation.target, index);
                            constrain_width(
                                operation.target, operation.target_width, index);
                        } else if (operation.target >= signal_widths.size()
                            || signal_widths[operation.target]
                                != operation.target_width) {
                            reject(process, index, "FileReadLine packed signal is invalid");
                        }
                    } else if (operation.kind == FileReadKind::unget) {
                        record_use(operation.source, index);
                        constrain_width(operation.source, 32U, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileEndOfFile>) {
                    result.uses_files = true;
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination,
                        operation.lookahead ? 1U : 32U,
                        index);
                } else if constexpr (std::is_same_v<OperationType, FileErrorStatus>) {
                    result.uses_files = true;
                    if (operation.target_kind > FileTextTargetKind::packed_signal)
                        reject(process, index, "FileErrorStatus target kind is invalid");
                    record_use(operation.handle, index);
                    constrain_width(operation.handle, 32U, index);
                    if (operation.target_kind
                        == FileTextTargetKind::string_register) {
                        result.uses_strings = true;
                        validate_string_register(operation.target, index, "target");
                    } else if (operation.target_width == 0) {
                        reject(process, index, "FileErrorStatus packed target width is zero");
                    } else if (operation.target_kind
                        == FileTextTargetKind::packed_register) {
                        record_definition(operation.target, index);
                        constrain_width(
                            operation.target, operation.target_width, index);
                    } else if (operation.target >= signal_widths.size()
                        || signal_widths[operation.target]
                            != operation.target_width) {
                        reject(process, index, "FileErrorStatus packed signal is invalid");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileScan>) {
                    if (!operation.string_source)
                        result.uses_files = true;
                    result.uses_strings = true;
                    if (operation.consume_string_source
                        && !operation.string_source) {
                        reject(
                            process, index,
                            "FileScan can consume only a string source");
                    }
                    if (!operation.string_source) {
                        record_use(operation.handle, index);
                        constrain_width(operation.handle, 32U, index);
                    }
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_file_scan_metadata(
                            operation, process, signal_widths,
                            signal_value_kinds, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        record_definition(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                    if (operation.success) {
                        record_definition(*operation.success, index);
                        constrain_width(*operation.success, 1U, index);
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, FileBinaryRead>) {
                    result.uses_files = true;
                    result.uses_containers |= operation.target_kind
                        >= FileBinaryTargetKind::container_register;
                    std::vector<PackedRegisterValidation> registers;
                    const auto error = validate_file_binary_metadata(
                        operation, process, signal_widths, registers);
                    if (error)
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, FilePosition>) {
                    result.uses_files = true;
                    std::vector<PackedRegisterValidation> registers;
                    if (const auto error = validate_file_position_metadata(
                            operation, registers))
                        reject(process, index, *error);
                    for (const auto& value : registers) {
                        if (value.definition)
                            record_definition(value.id, index);
                        else
                            record_use(value.id, index);
                        constrain_width(value.id, value.width, index);
                    }
                } // Start a second chain to avoid MSVC's nested-block limit.
                if constexpr (std::is_same_v<OperationType, FileFlush>) {
                    result.uses_files = true;
                    if (!operation.all) {
                        record_use(operation.handle, index);
                        constrain_width(operation.handle, 32U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, StringDisplay>) {
                    result.uses_strings = true;
                    result.uses_output = result.uses_output
                        || !operation.postponed;
                    result.uses_postponed_output = result.uses_postponed_output
                        || operation.postponed;
                    validate_string_register(
                        operation.source, index, "source");
                } else if constexpr (std::is_same_v<OperationType, StringReport>) {
                    result.uses_strings = true;
                    result.uses_report = true;
                    validate_string_register(
                        operation.message, index, "message");
                    record_use(operation.severity, index);
                    constrain_width(operation.severity, 2U, index);
                } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    unify_registers(operation.destination, operation.source, index);
                } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
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
                } else if constexpr (std::is_same_v<OperationType, Reduction>) {
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
                } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, CountBits>) {
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
                } else if constexpr (std::is_same_v<OperationType, Shift>) {
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
                } else if constexpr (std::is_same_v<OperationType, Extract>) {
                    if (operation.width == 0) {
                        reject(
                            process, index,
                            "Extract width must be greater than zero");
                    }
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    constrain_width(
                        operation.destination, operation.width, index);
                } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
                    if (const auto error = validate_dynamic_index_metadata(operation.selection)) {
                        reject(process, index, *error);
                    }
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    record_use(operation.selection.index, index);
                    constrain_width(operation.destination, 1U, index);
                    constrain_width(
                        operation.selection.index, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
                    if (const auto error = validate_dynamic_part_select_metadata(operation)) {
                        reject(process, index, *error);
                    }
                    record_definition(operation.destination, index);
                    record_use(operation.source, index);
                    record_use(operation.base, index);
                    constrain_width(
                        operation.destination, operation.width, index);
                    constrain_width(operation.base, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, Insert>) {
                    record_definition(operation.destination, index);
                    record_use(operation.target, index);
                    record_use(operation.source, index);
                    unify_registers(
                        operation.destination, operation.target, index);
                } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
                    if (const auto error = validate_dynamic_index_metadata(operation.selection)) {
                        reject(process, index, *error);
                    }
                    record_definition(operation.destination, index);
                    record_use(operation.target, index);
                    record_use(operation.source, index);
                    record_use(operation.selection.index, index);
                    constrain_width(operation.source, 1U, index);
                    constrain_width(
                        operation.selection.index, 32U, index);
                    unify_registers(
                        operation.destination, operation.target, index);
                } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
                    if (const auto error = validate_dynamic_part_index_metadata(
                            operation.selection)) {
                        reject(process, index, *error);
                    }
                    record_definition(operation.destination, index);
                    record_use(operation.target, index);
                    record_use(operation.source, index);
                    record_use(operation.selection.base, index);
                    constrain_width(
                        operation.source, operation.selection.width, index);
                    constrain_width(operation.selection.base, 32U, index);
                    unify_registers(
                        operation.destination, operation.target, index);
                } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
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
                } else if constexpr (std::is_same_v<OperationType, Binary>) {
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
                    case BinaryOperator::vhdl_match_equal:
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
                        || operation.operation
                            == BinaryOperator::vhdl_match_equal
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
                } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
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
                } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
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
                } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
                    if (operation.lower > operation.upper) {
                        reject(
                            process, index,
                            "IntegerCheck has an inverted range");
                    }
                    record_use(operation.source, index);
                    constrain_width(operation.source, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
                    record_definition(operation.destination, index);
                    record_use(operation.condition, index);
                    record_use(operation.when_true, index);
                    record_use(operation.when_false, index);
                    constrain_width(operation.condition, 1U, index);
                    unify_registers(
                        operation.when_true, operation.when_false, index);
                    unify_registers(
                        operation.destination, operation.when_true, index);
                } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
                    if (operation.rounding_quantum == 0) {
                        reject(
                            process, index,
                            "WaitFor rounding quantum must be greater than zero");
                    }
                    if (operation.source) {
                        if (operation.source_width == 0
                            || operation.source_width > 64) {
                            reject(
                                process, index,
                                "dynamic WaitFor source width must be in [1, 64]");
                        }
                        switch (operation.source_kind) {
                        case runtime::SystemVerilogScalarKind::None:
                            break;
                        case runtime::SystemVerilogScalarKind::ShortReal:
                            if (operation.source_width != 32) {
                                reject(
                                    process, index,
                                    "dynamic shortreal WaitFor source must be 32 bits");
                            }
                            break;
                        case runtime::SystemVerilogScalarKind::Real:
                        case runtime::SystemVerilogScalarKind::Realtime:
                        case runtime::SystemVerilogScalarKind::Time:
                            if (operation.source_width != 64) {
                                reject(
                                    process, index,
                                    "dynamic scalar WaitFor source must be 64 bits");
                            }
                            break;
                        default:
                            reject(
                                process, index,
                                "dynamic WaitFor has an invalid scalar kind");
                        }
                        record_use(*operation.source, index);
                        constrain_width(
                            *operation.source, operation.source_width, index);
                    } else if (operation.source_width != 0
                        || operation.source_kind
                            != runtime::SystemVerilogScalarKind::None
                        || operation.source_signed
                        || operation.rounding_quantum != 1) {
                        reject(
                            process, index,
                            "static WaitFor has dynamic-delay metadata");
                    }
                } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
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
                        const auto* origin = fsim::runtime::simir::operation_get_if<WaitOn>(
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
                        const auto edge = operation.edges.empty()
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
                } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
                    if (process.static_sensitivity.empty()) {
                        reject(process, index,
                            "WaitSensitivity requires a static sensitivity list");
                    }
                } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
                } else if constexpr (std::is_same_v<OperationType, Yield>) {
                } else if constexpr (std::is_same_v<OperationType, Fork>) {
                    validate_fork_operation(process, index, operation);
                } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
                } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
                } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
                    if (operation.site
                        && (*operation.site >= process.operations.size()
                            || !fsim::runtime::simir::operation_holds<Fork>(
                                process.operations[*operation.site]))) {
                        reject(
                            process, index,
                            "DisableFork named site does not reference Fork");
                    }
                } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
                    if (operation.begin >= operation.end
                        || operation.end > process.operations.size()) {
                        reject(
                            process, index,
                            "DisableBlock has an invalid lexical interval");
                    }
                } else if constexpr (std::is_same_v<OperationType, ProcessSelf>) {
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessStatusQuery>) {
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessCompleted>) {
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessAwait>
                    || std::is_same_v<OperationType, ProcessKill>) {
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxCreate>) {
                    if (operation.element_width == 0) {
                        reject(process, index, "MailboxCreate requires a positive element width");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                    record_use(operation.capacity, index);
                    constrain_width(operation.capacity, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxPut>) {
                    if (operation.element_width == 0) {
                        reject(process, index, "MailboxPut requires a positive element width");
                    }
                    record_use(operation.receiver, index);
                    constrain_width(operation.receiver, 64U, index);
                    record_use(operation.source, index);
                    constrain_width(operation.source, operation.element_width, index);
                    if (operation.result) {
                        record_definition(*operation.result, index);
                        constrain_width(*operation.result, 32U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxGet>) {
                    if (operation.element_width == 0) {
                        reject(process, index, "MailboxGet requires a positive element width");
                    }
                    record_use(operation.receiver, index);
                    constrain_width(operation.receiver, 64U, index);
                    record_definition(operation.destination, index);
                    constrain_width(
                        operation.destination, operation.element_width, index);
                    if (operation.result) {
                        record_definition(*operation.result, index);
                        constrain_width(*operation.result, 32U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, MailboxNum>) {
                    record_use(operation.receiver, index);
                    constrain_width(operation.receiver, 64U, index);
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SemaphoreCreate>) {
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 64U, index);
                    record_use(operation.keys, index);
                    constrain_width(operation.keys, 32U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, SemaphoreGet>) {
                    record_use(operation.receiver, index);
                    constrain_width(operation.receiver, 64U, index);
                    record_use(operation.keys, index);
                    constrain_width(operation.keys, 32U, index);
                    if (operation.result) {
                        record_definition(*operation.result, index);
                        constrain_width(*operation.result, 32U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, SemaphorePut>) {
                    record_use(operation.receiver, index);
                    constrain_width(operation.receiver, 64U, index);
                    record_use(operation.keys, index);
                    constrain_width(operation.keys, 32U, index);
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                }
            },
            process.operations[index]);
    }
    for (std::size_t index = 0; index < process.register_count; ++index) {
        const auto width = root_widths[find_root(static_cast<RegisterId>(index))];
        if (width <= std::numeric_limits<std::uint32_t>::max()) {
            result.register_widths[index] = static_cast<std::uint32_t>(width);
        }
    }
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        const auto wide_register = [&](const RegisterId id) {
            return result.register_widths[id] > 64;
        };
        const auto has_wide_register = std::ranges::any_of(instruction_uses[index], wide_register)
            || std::ranges::any_of(
                instruction_definitions[index], wide_register);
        if (!has_wide_register) {
            continue;
        }
        if (!supports_wide_register_operation(
                process.operations[index], result.register_widths)) {
            record_unsupported(
                index,
                "wide register operation is outside the current LLVM vector subset");
        }
    }
    if (const auto error = validate_selection_operation_bounds(
            process, result.register_widths, signal_widths)) {
        reject(process, error->instruction, error->message);
    }
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        for (const auto definition : instruction_definitions[index]) {
            if (result.register_widths[definition] == 0) {
                reject(process, index,
                    "register width cannot be inferred for register "
                        + std::to_string(definition));
            }
        }
        for (const auto used : instruction_uses[index]) {
            if (!defined[used]) {
                reject(process, index, "source register is never defined");
            }
            if (result.register_widths[used] == 0) {
                reject(process, index,
                    "source register width cannot be inferred for register "
                        + std::to_string(used));
            }
        }
        if (const auto* concatenate = fsim::runtime::simir::operation_get_if<Concatenate>(&process.operations[index])) {
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
                const auto target_width = signal_widths[write.signal];
                const auto source_width = result.register_widths[write.source];
                if (write.offset > target_width
                    || source_width
                        > target_width - write.offset) {
                    reject(
                        process, index,
                        "partial write range is outside its target signal");
                }
            };
        if (const auto* blocking_write = fsim::runtime::simir::operation_get_if<WriteBlockingSlice>(
                &process.operations[index])) {
            validate_slice_write(*blocking_write);
        } else if (const auto* update_write = fsim::runtime::simir::operation_get_if<WriteUpdateSlice>(
                       &process.operations[index])) {
            validate_slice_write(*update_write);
        } else if (const auto* delayed_write = fsim::runtime::simir::operation_get_if<WriteAfterSlice>(
                       &process.operations[index])) {
            validate_slice_write(*delayed_write);
        } else if (const auto* inertial_write = fsim::runtime::simir::operation_get_if<WriteInertialSlice>(
                       &process.operations[index])) {
            validate_slice_write(*inertial_write);
        } else if (const auto* projected_write = fsim::runtime::simir::operation_get_if<WriteProjectedSlice>(
                       &process.operations[index])) {
            validate_slice_write(*projected_write);
        } else if (const auto* waveform_write = fsim::runtime::simir::operation_get_if<WriteProjectedWaveformSlice>(
                       &process.operations[index])) {
            const auto target_width = signal_widths[waveform_write->signal];
            for (const auto& element : waveform_write->elements) {
                const auto source_width = result.register_widths[element.source];
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
        const auto& operation = process.operations[index];
        if (fsim::runtime::simir::operation_holds<Halt>(operation) || fsim::runtime::simir::operation_holds<Stop>(operation) || fsim::runtime::simir::operation_holds<Return>(operation) || fsim::runtime::simir::operation_holds<ForkEnd>(operation)) {
            continue;
        }
        if (const auto* fork = fsim::runtime::simir::operation_get_if<Fork>(&operation)) {
            successors[index].insert(
                successors[index].end(),
                fork->branches.begin(), fork->branches.end());
            successors[index].push_back(index + 1);
        } else if (const auto* jump = fsim::runtime::simir::operation_get_if<Jump>(&operation)) {
            successors[index].push_back(jump->target);
        } else if (const auto* call = fsim::runtime::simir::operation_get_if<Call>(&operation)) {
            successors[index].push_back(call->target);
            if (call->return_target != call->target) {
                successors[index].push_back(call->return_target);
            }
        } else if (const auto* branch = fsim::runtime::simir::operation_get_if<Branch>(&operation)) {
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
    std::vector<std::size_t> pending { 0 };
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
    const auto is_cycle_safe_point =
        [&](const Operation& operation) {
            return is_resume_boundary(operation)
                || fsim::runtime::simir::operation_holds<DebugPoint>(operation);
        };
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (reachable[index]
            && is_resume_boundary(process.operations[index])) {
            result.requires_resume = true;
        }
    }
    std::vector<std::vector<std::size_t>> invocation_successors(
        process.operations.size());
    std::vector<std::vector<std::size_t>> invocation_predecessors(
        process.operations.size());
    for (std::size_t index = 0; index < process.operations.size(); ++index) {
        if (!reachable[index] || is_cycle_safe_point(process.operations[index]) || fsim::runtime::simir::operation_holds<Stop>(process.operations[index]) || fsim::runtime::simir::operation_holds<Halt>(process.operations[index])) {
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
        remaining_predecessors[index] = static_cast<std::size_t>(std::count_if(
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
    std::vector<std::vector<bool>> definitely_defined_out = definitely_defined_in;
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
                            incoming[reg] = incoming[reg] && definitely_defined_out[predecessor][reg];
                        }
                    }
                }
            }
            auto outgoing = incoming;
            for (const auto definition : instruction_definitions[index]) {
                outgoing[definition] = true;
            }
            if (incoming != definitely_defined_in[index] || outgoing != definitely_defined_out[index]) {
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
} // namespace fsim::compiler::llvm_detail
