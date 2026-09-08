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
    InstructionIndex previous_trigger_end { };
    for (const auto& region : process.static_trigger_regions) {
        if (region.begin >= region.end
            || region.end > process.operations.size()
            || region.begin < previous_trigger_end
            || (region.mask & Process::full_static_trigger_mask) != 0U
            || process.static_sensitivity.size() > 63U
            || (process.static_sensitivity.size() < 63U
                && (region.mask >> process.static_sensitivity.size()) != 0U)) {
            reject(
                process, region.begin,
                "static trigger region metadata is invalid");
        }
        previous_trigger_end = region.end;
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
#include "llvm_jit_validation_operations_prefix.tpp"
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
                    unify_registers(
                        operation.destination, operation.source, index);
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
                    unify_registers(operation.lhs, operation.rhs, index);
                    unify_registers(
                        operation.destination, operation.lhs, index);
                } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
                    if (operation.lower > operation.upper) {
                        reject(
                            process, index,
                            "IntegerCheck has an inverted range");
                    }
                    record_use(operation.source, index);
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
                } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
                    if (operation.phase < runtime::SchedulerPhase::reactive
                        || operation.phase > runtime::SchedulerPhase::postponed) {
                        reject(
                            process, index,
                            "WaitRegion target must be reactive or later");
                    }
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
                } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
                    result.uses_containers = true;
                    for (const auto signal : operation.signals) {
                        (void)referenced_signal_width(signal, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
                    if (operation.events.empty()) {
                        reject(
                            process, index,
                            "WaitOrder requires at least one event");
                    }
                    record_definition(operation.result, index);
                    constrain_width(operation.result, 1U, index);
                    for (const auto event : operation.events) {
                        if (referenced_signal_width(event, index) != 1U) {
                            reject(
                                process, index,
                                "WaitOrder events must be scalar");
                        }
                    }
                } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
                    if (referenced_signal_width(operation.event, index) != 1U) {
                        reject(
                            process, index,
                            "EventTriggered requires a scalar event");
                    }
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination, 1U, index);
                } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
                    if (referenced_signal_width(operation.target, index) != 1U
                        || (operation.has_source
                            && referenced_signal_width(operation.source, index)
                                != 1U)) {
                        reject(
                            process, index,
                            "EventAlias requires scalar named events");
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
                    || std::is_same_v<OperationType, ProcessKill>
                    || std::is_same_v<OperationType, ProcessSuspend>
                    || std::is_same_v<OperationType, ProcessResume>) {
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessGetRandState>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSetRandState>) {
                    result.uses_strings = true;
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                    validate_string_register(operation.state, index, "state");
                } else if constexpr (
                    std::is_same_v<OperationType, ProcessSrandom>) {
                    record_use(operation.source, index);
                    constrain_width(operation.source, 64U, index);
                    record_use(operation.seed, index);
                    constrain_width(operation.seed, 32U, index);
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
                } else if constexpr (
                    std::is_same_v<OperationType, VhdlEnvironmentTime>) {
                    using Kind = VhdlEnvironmentTimeKind;
                    const auto kind = operation.kind;
                    if (kind > Kind::seconds_to_time) {
                        reject(process, index,
                            "VHDL environment time operation kind is invalid");
                    }
                    const bool record_result = kind == Kind::current_local
                        || kind == Kind::current_utc
                        || kind == Kind::local_from_epoch
                        || kind == Kind::utc_from_epoch
                        || kind == Kind::local_from_utc_record
                        || kind == Kind::utc_from_local_record
                        || kind == Kind::add_seconds
                        || kind == Kind::subtract_seconds
                        || kind == Kind::reverse_subtract_seconds;
                    record_definition(operation.destination, index);
                    constrain_width(operation.destination,
                        record_result ? 515U : 64U, index);
                    const bool no_operand = kind == Kind::current_local
                        || kind == Kind::current_utc
                        || kind == Kind::current_epoch;
                    if (no_operand != !operation.first) {
                        reject(process, index,
                            "VHDL environment time operation has an invalid first operand");
                    }
                    if (operation.first) {
                        record_use(*operation.first, index);
                        const bool record_first = kind == Kind::epoch_from_local
                            || kind == Kind::local_from_utc_record
                            || kind == Kind::utc_from_local_record
                            || kind == Kind::add_seconds
                            || kind == Kind::subtract_seconds
                            || kind == Kind::reverse_subtract_seconds
                            || kind == Kind::difference_seconds;
                        constrain_width(*operation.first,
                            record_first ? 515U : 64U, index);
                    }
                    const bool needs_second = kind == Kind::add_seconds
                        || kind == Kind::subtract_seconds
                        || kind == Kind::reverse_subtract_seconds
                        || kind == Kind::difference_seconds;
                    if (needs_second != operation.second.has_value()) {
                        reject(process, index,
                            "VHDL environment time operation has an invalid second operand");
                    }
                    if (operation.second) {
                        record_use(*operation.second, index);
                        constrain_width(*operation.second,
                            kind == Kind::difference_seconds ? 515U : 64U,
                            index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentTimeToString>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    record_use(operation.record, index);
                    constrain_width(operation.record, 515U, index);
                    record_use(operation.fractional_digits, index);
                    constrain_width(operation.fractional_digits, 64U, index);
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentDirectory>) {
                    using Kind = VhdlEnvironmentDirectoryKind;
                    result.uses_strings = true;
                    result.uses_containers = result.uses_containers
                        || operation.directory.has_value();
                    if (operation.kind > Kind::delete_file) {
                        reject(process, index,
                            "VHDL environment directory operation kind is invalid");
                    }
                    const bool boolean_result = operation.kind == Kind::item_exists
                        || operation.kind == Kind::item_is_directory
                        || operation.kind == Kind::item_is_file;
                    const bool status_result = operation.kind == Kind::open
                        || operation.kind == Kind::set_working_directory
                        || operation.kind == Kind::create_directory
                        || operation.kind == Kind::delete_directory
                        || operation.kind == Kind::delete_file;
                    const bool string_result = operation.kind
                            == Kind::get_working_directory
                        || operation.kind == Kind::separator;
                    const bool directory_operand = operation.kind == Kind::open
                        || operation.kind == Kind::close;
                    const bool path_operand = operation.kind != Kind::close
                        && operation.kind != Kind::get_working_directory
                        && operation.kind != Kind::separator;
                    const bool option_allowed = operation.kind
                            == Kind::create_directory
                        || operation.kind == Kind::delete_directory;
                    if (operation.result.has_value()
                            != (boolean_result || status_result)
                        || operation.string_result.has_value() != string_result
                        || operation.directory.has_value() != directory_operand
                        || operation.path.has_value() != path_operand
                        || (operation.option && !option_allowed)) {
                        reject(process, index,
                            "VHDL environment directory operands do not match the operation kind");
                    }
                    if (operation.result) {
                        record_definition(*operation.result, index);
                        constrain_width(*operation.result,
                            boolean_result ? 1U : 3U, index);
                    }
                    if (operation.string_result) {
                        validate_string_register(
                            *operation.string_result, index, "destination");
                    }
                    if (operation.directory) {
                        validate_container_register(
                            *operation.directory, index, "directory");
                        if (*operation.directory
                            < process.container_register_types.size()) {
                            const auto& type = process.container_register_types[
                                *operation.directory];
                            if (type.element_kind
                                != ContainerElementKind::String) {
                                reject(process, index,
                                    "VHDL environment DIRECTORY must use string storage");
                            }
                        }
                    }
                    if (operation.path) {
                        validate_string_register(*operation.path, index, "path");
                    }
                    if (operation.option) {
                        record_use(*operation.option, index);
                        constrain_width(*operation.option, 1U, index);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentGetenv>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    validate_string_register(
                        operation.name, index, "name");
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentCallPath>) {
                    result.uses_strings = true;
                    validate_string_register(
                        operation.destination, index, "destination");
                    validate_string_register(
                        operation.separator, index, "separator");
                    if (operation.source_value) {
                        validate_container_register(
                            *operation.source_value, index, "source value");
                    }
                    if (operation.source_index) {
                        if (!operation.source_value) {
                            reject(process, index,
                                "VHDL call-path source index requires a source container");
                        }
                        record_use(*operation.source_index, index);
                        constrain_width(*operation.source_index, 32U, index);
                    }
                    if (operation.source.path.empty()
                        || operation.source.path.size() > maximum_string_bytes
                        || operation.source.line == 0U
                        || operation.source.column == 0U
                        || operation.scope.size() > maximum_string_bytes) {
                        reject(process, index,
                            "VHDL environment call-path source metadata is invalid");
                    }
                } else if constexpr (
                    std::is_same_v<OperationType,
                        VhdlEnvironmentGetCallPath>) {
                    result.uses_containers = true;
                    validate_container_register(
                        operation.destination, index, "destination");
                    if (operation.source.path.empty()
                        || operation.source.path.size() > maximum_string_bytes
                        || operation.source.line == 0U
                        || operation.source.column == 0U
                        || operation.scope.size() > maximum_string_bytes) {
                        reject(process, index,
                            "VHDL environment GET_CALL_PATH source metadata is invalid");
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, VhdlReflectionApi>) {
                    if (operation.kind > VhdlReflectionApiKind::file_open_kind) {
                        reject(process, index,
                            "VHDL reflection API kind is invalid");
                    }
                    const bool string_result = operation.kind
                            == VhdlReflectionApiKind::simple_name
                        || operation.kind == VhdlReflectionApiKind::image
                        || operation.kind == VhdlReflectionApiKind::unit_name
                        || operation.kind
                            == VhdlReflectionApiKind::record_element_name
                        || operation.kind
                            == VhdlReflectionApiKind::file_logical_name;
                    if (string_result
                            != operation.string_destination.has_value()
                        || string_result == operation.destination.has_value()) {
                        reject(process, index,
                            "VHDL reflection API destination shape is invalid");
                    }
                    const bool create = operation.kind
                            == VhdlReflectionApiKind::create_subtype
                        || operation.kind
                            == VhdlReflectionApiKind::create_value;
                    if (create == operation.receiver.has_value()) {
                        reject(process, index,
                            "VHDL reflection API receiver shape is invalid");
                    }
                    if (operation.kind
                            == VhdlReflectionApiKind::create_value
                        && operation.type.packed_width != 0U
                        && !operation.source) {
                        reject(process, index,
                            "VHDL reflection value source shape is invalid");
                    }
                    if (operation.kind
                            != VhdlReflectionApiKind::create_value
                        && operation.source) {
                        reject(process, index,
                            "VHDL reflection source is only valid for a value mirror");
                    }
                    if (operation.access_heap) {
                        if (operation.kind
                                != VhdlReflectionApiKind::create_value
                            || operation.type.type_class
                                != VhdlReflectionClass::access) {
                            reject(process, index,
                                "VHDL reflection access heap ownership is invalid");
                        }
                        validate_container_register(
                            *operation.access_heap, index, "access heap");
                        result.uses_containers = true;
                    }
                    if (operation.arguments.size() > 32U) {
                        reject(process, index,
                            "VHDL reflection API has too many scalar arguments");
                    }
                    if (operation.source_location.path.empty()
                        || operation.source_location.path.size()
                            > maximum_string_bytes
                        || operation.source_location.line == 0U
                        || operation.source_location.column == 0U) {
                        reject(process, index,
                            "VHDL reflection source metadata is invalid");
                    }
                    std::size_t descriptor_nodes = 0U;
                    std::size_t descriptor_bytes = 0U;
                    const auto validate_descriptor = [&]<typename Self>(
                        Self&& self, const VhdlReflectionType& type,
                        const std::size_t depth) -> void {
                        if (++descriptor_nodes > 65'536U || depth > 32U
                            || type.type_class
                                > VhdlReflectionClass::protected_type
                            || type.simple_name.empty()
                            || type.simple_name.size() > maximum_string_bytes
                            || type.ranges.size() > 32U
                            || type.names.size() > 65'536U
                            || type.scales.size() > 65'536U
                            || type.children.size() > 65'536U) {
                            reject(process, index,
                                "VHDL reflection type descriptor is invalid");
                        }
                        descriptor_bytes += type.simple_name.size();
                        for (const auto& name : type.names) {
                            descriptor_bytes += name.size();
                            if (name.size() > maximum_string_bytes
                                || descriptor_bytes
                                    > 16U * maximum_string_bytes) {
                                reject(process, index,
                                    "VHDL reflection descriptor exceeds its resource bound");
                            }
                        }
                        if (type.type_class == VhdlReflectionClass::physical
                            && (type.names.size() != type.scales.size()
                                || type.scales.empty()
                                || std::ranges::any_of(type.scales,
                                    [](const std::uint64_t scale) {
                                        return scale == 0U
                                            || scale > static_cast<std::uint64_t>(
                                                std::numeric_limits<std::int64_t>::max());
                                    }))) {
                            reject(process, index,
                                "VHDL reflection physical units are invalid");
                        }
                        for (const auto& child : type.children) {
                            self(self, child, depth + 1U);
                        }
                    };
                    if (create) {
                        validate_descriptor(
                            validate_descriptor, operation.type, 0U);
                    } else if (operation.kind
                            == VhdlReflectionApiKind::convert
                        && operation.type.type_class
                            > VhdlReflectionClass::protected_type) {
                        reject(process, index,
                            "VHDL reflection conversion class is invalid");
                    }
                    if (operation.destination) {
                        if (operation.result_width == 0U) {
                            reject(process, index,
                                "VHDL reflection packed result width is zero");
                        }
                        record_definition(*operation.destination, index);
                        constrain_width(
                            *operation.destination, operation.result_width,
                            index);
                    } else {
                        validate_string_register(
                            *operation.string_destination, index,
                            "destination");
                        result.uses_strings = true;
                    }
                    if (operation.receiver) {
                        record_use(*operation.receiver, index);
                        constrain_width(*operation.receiver, 32U, index);
                    }
                    if (operation.source) {
                        record_use(*operation.source, index);
                        constrain_width(
                            *operation.source, operation.type.packed_width,
                            index);
                    }
                    for (const auto argument : operation.arguments) {
                        record_use(argument, index);
                        constrain_width(argument, 64U, index);
                    }
                    if (operation.string_argument) {
                        validate_string_register(
                            *operation.string_argument, index, "argument");
                        result.uses_strings = true;
                    }
                } else if constexpr (std::is_same_v<OperationType, Pause>) {
                    if (operation.status) {
                        record_use(*operation.status, index);
                        constrain_width(*operation.status, 64U, index);
                    }
                } else if constexpr (std::is_same_v<OperationType, Stop>) {
                    if (operation.status) {
                        record_use(*operation.status, index);
                        constrain_width(*operation.status, 64U, index);
                    }
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
        visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
                    const auto width =
                        result.register_widths[operation.source];
                    if (width != 32U && width != 64U) {
                        reject(
                            process, index,
                            "IntegerUnary requires a 32- or 64-bit operand");
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, IntegerBinary>) {
                    const auto width = result.register_widths[operation.lhs];
                    if (width != 32U && width != 64U) {
                        reject(
                            process, index,
                            "IntegerBinary requires 32- or 64-bit operands");
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, IntegerCheck>) {
                    const auto width = result.register_widths[operation.source];
                    if (width != 32U && width != 64U) {
                        reject(
                            process, index,
                            "IntegerCheck requires a 32- or 64-bit operand");
                    }
                }
            },
            process.operations[index]);
        const auto wide_register = [&](const RegisterId id) {
            return result.register_widths[id] > 64;
        };
        const auto has_wide_register = std::ranges::any_of(instruction_uses[index], wide_register)
            || std::ranges::any_of(
                instruction_definitions[index], wide_register);
        if (!has_wide_register) {
            continue;
        }
        visit_operation(
            [&](const auto& operation) {
                using OperationType = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<OperationType, ContainerRead>) {
                    if (!operation.string_index
                        && operation.source
                            < process.container_register_types.size()) {
                        const auto& type
                            = process.container_register_types[operation.source];
                        result.uses_wide_container_operation
                            = result.uses_wide_container_operation
                            || (!type.associative
                                && (type.element_kind
                                        == ContainerElementKind::Packed
                                    || type.element_kind
                                        == ContainerElementKind::Scalar)
                                && type.element_width > 64U
                                && result.register_widths[operation.index]
                                    <= 64U);
                    }
                } else if constexpr (
                    std::is_same_v<OperationType, ContainerWrite>) {
                    if (!operation.string_index
                        && operation.target
                            < process.container_register_types.size()) {
                        const auto& type
                            = process.container_register_types[operation.target];
                        result.uses_wide_container_operation
                            = result.uses_wide_container_operation
                            || (!type.associative
                                && (type.element_kind
                                        == ContainerElementKind::Packed
                                    || type.element_kind
                                        == ContainerElementKind::Scalar)
                                && type.element_width > 64U
                                && result.register_widths[operation.index]
                                    <= 64U);
                    }
                }
            },
            process.operations[index]);
        if (!supports_wide_register_operation(
                process.operations[index], result.register_widths)) {
            std::string operation_type;
            fsim::runtime::simir::visit_operation(
                [&](const auto& operation) {
                    operation_type = typeid(operation).name();
                },
                process.operations[index]);
            record_unsupported(
                index,
                "wide register operation is outside the current LLVM vector "
                "subset (operation " + operation_type + ")");
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
                    "register " + std::to_string(used)
                        + " may be used before definition on a control-flow "
                          "path");
            }
        }
    }
    if (unsupported) {
        reject_unsupported(
            process, unsupported->first, unsupported->second);
    }
    result.instruction_uses = std::move(instruction_uses);
    result.instruction_definitions = std::move(instruction_definitions);
    return result;
}
} // namespace fsim::compiler::llvm_detail
