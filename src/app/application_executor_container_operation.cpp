// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_container_profile.hpp"

#include "fsim/runtime/file_binary.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/string_methods.hpp"

#include <algorithm>

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

std::uint32_t LlvmProcessExecutor::container_operation(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t input0_aval,
    const std::uint64_t input0_bval,
    const std::uint64_t input1_aval,
    const std::uint64_t input1_bval,
    std::uint64_t* result_aval,
    std::uint64_t* result_bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || result_aval == nullptr || result_bval == nullptr) {
            throw compiler::LlvmJitError {
                "invalid generated container callback"
            };
        }
        *result_aval = 0;
        *result_bval = 0;
        const auto& operation = callback_operation(state, process, instruction);
        if (!fsim::runtime::simir::operation_holds<
                runtime::simir::ReadContainerObject>(operation)
            && !fsim::runtime::simir::operation_holds<
                runtime::simir::WriteContainerObjectElement>(operation)) {
            state.executor->materialize_container_object_aliases(*state.context);
        }
        auto& profile = container_callback_profile();
        if (profile.enabled) {
            ++profile.generic;
            if (fsim::runtime::simir::operation_holds<
                    runtime::simir::CopyContainerRegister>(operation)) {
                ++profile.copy_registers;
            } else if (fsim::runtime::simir::operation_holds<
                           runtime::simir::ReadContainerObject>(operation)) {
                ++profile.read_objects;
            } else if (fsim::runtime::simir::operation_holds<
                           runtime::simir::WriteContainerObject>(operation)) {
                ++profile.write_objects;
            }
        }
        const auto fast_offset = [&](const runtime::simir::ContainerValue& value,
                                     const bool linear_index,
                                     const bool signed_index) {
            if (input0_bval != 0) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index must be a known integral value"
                };
            }
            if (value.type.fixed && !linear_index) {
                const auto sought = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(input0_aval));
                const auto low = std::min(
                    value.type.index_left, value.type.index_right);
                const auto high = std::max(
                    value.type.index_left, value.type.index_right);
                if (sought < low || sought > high) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "static-array index is out of range"
                    };
                }
                return static_cast<std::size_t>(
                    value.type.index_left >= value.type.index_right
                        ? static_cast<std::int64_t>(value.type.index_left)
                            - sought
                        : static_cast<std::int64_t>(sought)
                            - value.type.index_left);
            }
            if (signed_index
                && static_cast<std::int32_t>(
                       static_cast<std::uint32_t>(input0_aval)) < 0) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index cannot be negative"
                };
            }
            return static_cast<std::size_t>(input0_aval);
        };
        const auto fast_packed_type = [](const auto& value) {
            return !value.type.associative
                && (value.type.element_kind
                        == runtime::simir::ContainerElementKind::Packed
                    || value.type.element_kind
                        == runtime::simir::ContainerElementKind::Scalar)
                && value.type.element_width <= 64U;
        };
        if (const auto* read = fsim::runtime::simir::operation_get_if<
                runtime::simir::ContainerRead>(&operation)) {
            const auto& source = *state.executor->container_registers_.at(
                read->source);
            if (!read->string_index && fast_packed_type(source)
                && state.executor->layout_.register_widths.at(read->index)
                    <= 64U) {
                const auto publish = [&](const PackedLogic4& value) {
                    const auto word = value.low_word();
                    *result_aval = word.aval;
                    *result_bval = word.bval;
                };
                std::optional<std::size_t> selected;
                if (source.type.fixed) {
                    try {
                        selected = fast_offset(
                            source, read->linear_index, true);
                    } catch (const runtime::simir::InterpreterError&) {
                        publish(PackedLogic4(
                            source.type.element_width,
                            source.type.two_state
                                ? runtime::Logic4::zero
                                : runtime::Logic4::x));
                        return 0;
                    }
                } else {
                    selected = fast_offset(
                        source, read->linear_index, read->signed_index);
                }
                if (*selected >= source.elements.size()) {
                    if (source.type.fixed) {
                        publish(PackedLogic4(
                            source.type.element_width,
                            source.type.two_state
                                ? runtime::Logic4::zero
                                : runtime::Logic4::x));
                        return 0;
                    }
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "container index is out of range"
                    };
                }
                publish(source.elements[*selected]);
                return 0;
            }
        } else if (const auto* write = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerWrite>(&operation)) {
            auto& target = state.executor->mutable_container_register_value(
                write->target, *state.context);
            if (!write->string_index && fast_packed_type(target)
                && state.executor->layout_.register_widths.at(write->index)
                    <= 64U
                && state.executor->layout_.register_widths.at(write->source)
                    <= 64U) {
                const auto selected = fast_offset(
                    target, write->linear_index,
                    target.type.fixed || write->signed_index);
                if (selected >= target.elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "container index is out of range"
                    };
                }
                if (target.type.two_state && input1_bval != 0) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "container element write type mismatch"
                    };
                }
                target.elements[selected] = PackedLogic4::from_aval_bval(
                    target.type.element_width, input1_aval, input1_bval);
                return 0;
            }
        }
        if (const auto* declaration = fsim::runtime::simir::operation_get_if<
                runtime::simir::VitalMemoryDeclare>(&operation)) {
            auto memory = runtime::simir::make_vital_memory(
                declaration->word_count, declaration->word_width,
                declaration->subword_width);
            const auto& path = state.executor->string_registers_.at(
                declaration->load_file);
            if (declaration->embedded_load) {
                runtime::simir::load_vital_memory_text(
                    memory, declaration->embedded_load_text,
                    declaration->binary);
            } else if (!path.empty()) {
                const auto handle = state.context->open_file(path, "r");
                std::string text;
                try {
                    while (!state.context->file_end_of_file(handle)) {
                        std::uint32_t count { };
                        auto line = state.context->read_file_line(handle, count);
                        if (text.size() + line.size()
                            > runtime::simir::maximum_memory_file_bytes) {
                            throw std::length_error {
                                "VITAL memory load file exceeds the 1 MiB input budget"
                            };
                        }
                        text += line;
                    }
                    state.context->close_file(handle);
                } catch (...) {
                    try {
                        state.context->close_file(handle);
                    } catch (...) {
                    }
                    throw;
                }
                runtime::simir::load_vital_memory_text(
                    memory, text, declaration->binary);
            }
            auto& memories = state.executor->storage_->vital_memories;
            if (memories.size() >= std::numeric_limits<std::uint32_t>::max()) {
                throw compiler::LlvmJitError {
                    "compiled VITAL memory handle space is exhausted"
                };
            }
            memories.push_back(std::move(memory));
            *result_aval = memories.size();
            return 0;
        } else if (const auto* method = fsim::runtime::simir::operation_get_if<runtime::simir::StringMethod>(&operation)) {
            auto& source = state.executor->string_registers_.at(method->source);
            if (method->operation
                >= runtime::simir::StringMethodOperator::format_packed) {
                const auto packed = method->operation
                        == runtime::simir::StringMethodOperator::format_packed
                    ? state.executor->read_register(
                          method->first,
                          runtime::simir::ProcessExecutor::native_register_width)
                    : runtime::PackedLogic4 { 1, runtime::Logic4::zero };
                runtime::simir::execute_string_format(
                    *method, source, packed,
                    method->operation
                            == runtime::simir::StringMethodOperator::format_string
                        ? std::string_view {
                              state.executor->string_registers_.at(method->argument) }
                        : std::string_view { },
                    state.context->current_time(), state.context->systemverilog_time_format());
                return 0;
            }
            const auto signed32 = [](
                                      const std::uint64_t aval,
                                      const std::uint64_t bval) -> std::optional<std::int32_t> {
                return bval == 0
                    ? std::optional<std::int32_t> {
                          static_cast<std::int32_t>(
                              static_cast<std::uint32_t>(aval))
                      }
                    : std::nullopt;
            };
            const bool compare = method->operation
                    == runtime::simir::StringMethodOperator::compare
                || method->operation
                    == runtime::simir::StringMethodOperator::icompare;
            const auto result = runtime::simir::execute_string_method(
                method->operation, source,
                compare
                    ? std::string_view { state.executor->string_registers_.at(
                          method->argument) }
                    : std::string_view { },
                signed32(input0_aval, input0_bval),
                signed32(input1_aval, input1_bval),
                method->operation
                            == runtime::simir::StringMethodOperator::realtoa
                        && input0_bval == 0
                    ? std::optional<std::uint64_t> { input0_aval }
                    : std::nullopt);
            if (result.integer) {
                *result_aval = *result.integer;
            }
            if (result.scalar_bits) {
                *result_aval = *result.scalar_bits;
            }
            if (result.string) {
                state.executor->write_string_register(
                    method->string_destination, *result.string);
            }
            return 0;
        }
        if (const auto* file = fsim::runtime::simir::operation_get_if<runtime::simir::FileReadLine>(&operation);
            file != nullptr
            && file->kind != runtime::simir::FileReadKind::line) {
            const auto handle = checked_file_handle(input0_aval, input0_bval);
            std::int32_t result { };
            if (file->kind == runtime::simir::FileReadKind::character) {
                result = state.context->read_file_character(handle);
            } else {
                if (input1_bval == 0) {
                    const auto requested = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(input1_aval));
                    result = state.context->unread_file_character(
                                 handle, requested)
                            == requested
                        ? 0
                        : -1;
                } else {
                    result = -1;
                }
            }
            *result_aval = static_cast<std::uint32_t>(result);
            return 0;
        }
        if (const auto* position = fsim::runtime::simir::operation_get_if<runtime::simir::FilePosition>(&operation)) {
            const auto known = [](const std::uint64_t aval,
                                   const std::uint64_t bval) {
                if (bval != 0) {
                    throw compiler::LlvmJitError {
                        "compiled file position operand is unknown"
                    };
                }
                return static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(aval));
            };
            const auto handle = state.executor
                                    ->read_register(position->handle, 32)
                                    .low_word();
            const auto result = state.context->position_file(
                checked_file_handle(handle.aval, handle.bval),
                position->kind,
                position->kind == runtime::simir::FilePositionKind::seek
                    ? known(input0_aval, input0_bval)
                    : 0,
                position->kind == runtime::simir::FilePositionKind::seek
                    ? known(input1_aval, input1_bval)
                    : 0);
            *result_aval = static_cast<std::uint32_t>(result);
            return 0;
        }
        if (const auto* flush = fsim::runtime::simir::operation_get_if<runtime::simir::FileFlush>(&operation)) {
            std::optional<runtime::simir::FileHandle> handle;
            if (!flush->all) {
                const auto word = state.executor
                                      ->read_register(flush->handle, 32)
                                      .low_word();
                handle = checked_file_handle(word.aval, word.bval);
            }
            state.context->flush_file(handle);
            return 0;
        }
        if (const auto* scan = fsim::runtime::simir::operation_get_if<runtime::simir::FileScan>(&operation)) {
            runtime::simir::InputScanResult scanned;
            if (scan->string_source) {
                auto& source = state.executor->string_registers_.at(scan->source);
                scanned = runtime::simir::scan_formatted_string(
                    *scan, source);
                if (scan->consume_string_source) {
                    source.erase(0, std::min(source.size(), scanned.consumed));
                }
            } else {
                const auto handle = checked_file_handle(input0_aval, input0_bval);
                const std::function<std::int32_t()> read = [&] {
                    return state.context->read_file_character(handle);
                };
                const std::function<void(std::int32_t)> unread = [&](const auto value) {
                    if (state.context->unread_file_character(handle, value) != value) {
                        throw compiler::LlvmJitError {
                            "compiled file scan could not preserve lookahead"
                        };
                    }
                };
                scanned = runtime::simir::scan_formatted_input(*scan, read, unread);
            }
            const auto expected = static_cast<std::int32_t>(
                std::ranges::count_if(
                    scan->conversions,
                    [](const runtime::simir::InputScanConversion& conversion) {
                        return !conversion.suppress;
                    }));
            const bool complete = scanned.assignments == expected;
            if (scan->success) {
                state.executor->write_register(
                    *scan->success,
                    runtime::PackedLogic4::from_aval_bval(
                        1, complete ? 1U : 0U, 0));
            }
            if (scan->require_assignments && !complete) {
                throw compiler::LlvmJitError {
                    "VHDL file read did not convert the requested element"
                };
            }
            for (std::size_t index = 0; index < scanned.values.size(); ++index) {
                if (!scanned.values[index])
                    continue;
                const auto& target = scan->conversions[index].target;
                auto& value = *scanned.values[index];
                switch (target.kind) {
                case runtime::simir::InputScanTargetKind::packed_register:
                    state.executor->write_register(target.id, value.packed);
                    break;
                case runtime::simir::InputScanTargetKind::packed_signal:
                    state.context->write_blocking(target.id, std::move(value.packed));
                    break;
                case runtime::simir::InputScanTargetKind::string_register:
                    state.executor->write_string_register(target.id, value.text);
                    break;
                case runtime::simir::InputScanTargetKind::string_object:
                    state.context->write_string_object(target.id, value.text);
                    break;
                }
            }
            *result_aval = static_cast<std::uint32_t>(scanned.assignments);
            return 0;
        }
        if (const auto* binary = fsim::runtime::simir::operation_get_if<runtime::simir::FileBinaryRead>(&operation)) {
            const auto known_integer = [&](const runtime::simir::RegisterId id) {
                const auto value = state.executor->read_register(id, 32).low_word();
                if (value.bval != 0) {
                    throw compiler::LlvmJitError {
                        "compiled $fread bound is not a known integer"
                    };
                }
                return static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(value.aval));
            };
            std::optional<runtime::simir::ContainerValue> container;
            if (binary->target_kind
                == runtime::simir::FileBinaryTargetKind::container_register) {
                container = *state.executor->container_registers_.at(
                    binary->target);
            } else if (binary->target_kind
                == runtime::simir::FileBinaryTargetKind::container_object) {
                container = state.context->read_container_object(binary->target);
            }
            const auto handle = checked_file_handle(input0_aval, input0_bval);
            const std::function<std::int32_t()> read = [&] {
                return state.context->read_file_character(handle);
            };
            auto value = runtime::simir::read_binary_file(
                *binary, std::move(container),
                binary->has_start
                    ? std::optional { known_integer(binary->start) }
                    : std::nullopt,
                binary->has_count
                    ? std::optional { known_integer(binary->count) }
                    : std::nullopt,
                read);
            switch (binary->target_kind) {
            case runtime::simir::FileBinaryTargetKind::packed_register:
                state.executor->write_register(binary->target, value.packed);
                break;
            case runtime::simir::FileBinaryTargetKind::packed_signal:
                state.context->write_blocking(binary->target, std::move(value.packed));
                break;
            case runtime::simir::FileBinaryTargetKind::container_register:
                state.executor->write_container_register(
                    binary->target, *value.container);
                break;
            case runtime::simir::FileBinaryTargetKind::container_object:
                state.context->write_container_object(binary->target, *value.container);
                break;
            }
            *result_aval = value.bytes;
            return 0;
        }
        const auto read_container = [&](const auto id)
            -> const runtime::simir::ContainerValue& {
            return state.executor->container_register_value(
                id, *state.context);
        };
        const auto mutable_container = [&](const auto id)
            -> runtime::simir::ContainerValue& {
            return state.executor->mutable_container_register_value(
                id, *state.context);
        };
        const auto index =
            [&](const std::uint64_t aval,
                const std::uint64_t bval,
                const bool signed_index,
                const std::string_view role) {
                if (bval != 0
                    || (signed_index
                        && static_cast<std::int32_t>(
                               static_cast<std::uint32_t>(aval))
                            < 0)) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        std::string { role }
                            + (bval != 0
                                    ? " must be a known integral value"
                                    : " cannot be negative")
                    };
                }
                return static_cast<std::size_t>(aval);
            };
        const auto fixed_offset =
            [&](const runtime::simir::ContainerValue& target,
                const std::uint64_t aval,
                const std::uint64_t bval) {
                if (bval != 0) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "static-array index must be a known 32-bit integral value"
                    };
                }
                const auto sought = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(aval));
                const auto low = std::min(
                    target.type.index_left, target.type.index_right);
                const auto high = std::max(
                    target.type.index_left, target.type.index_right);
                if (sought < low || sought > high) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "static-array index is out of range"
                    };
                }
                return static_cast<std::size_t>(
                    target.type.index_left >= target.type.index_right
                        ? static_cast<std::int64_t>(
                              target.type.index_left)
                            - sought
                        : static_cast<std::int64_t>(sought)
                            - target.type.index_left);
            };
        const auto packed_fixed_offset =
            [&](const runtime::simir::ContainerValue& target,
                const runtime::simir::RegisterId index_register,
                const std::uint64_t aval,
                const std::uint64_t bval) {
                const auto& layout = state.executor->layout_;
                if (layout.register_widths.at(index_register) <= 64) {
                    return fixed_offset(target, aval, bval);
                }
                const auto value = state.executor->read_register(
                    index_register,
                    layout.register_widths[index_register]);
                const auto converted = value.known_signed_value();
                if (!converted
                    || *converted < std::numeric_limits<std::int32_t>::min()
                    || *converted > std::numeric_limits<std::int32_t>::max()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "static-array index must be a known 32-bit integral value"
                    };
                }
                const auto sought = static_cast<std::int32_t>(*converted);
                const auto low = std::min(
                    target.type.index_left, target.type.index_right);
                const auto high = std::max(
                    target.type.index_left, target.type.index_right);
                if (sought < low || sought > high) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "static-array index is out of range"
                    };
                }
                return static_cast<std::size_t>(
                    target.type.index_left >= target.type.index_right
                        ? static_cast<std::int64_t>(target.type.index_left) - sought
                        : static_cast<std::int64_t>(sought) - target.type.index_left);
            };
        const auto require_same =
            [&](const runtime::simir::ContainerValue& left,
                const runtime::simir::ContainerValue& right) {
                if (left.type != right.type) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "container value type mismatch"
                    };
                }
            };
        const auto element =
            [&](const runtime::simir::ContainerValue& target,
                const std::uint64_t aval,
                const std::uint64_t bval) {
                if (target.type.two_state && bval != 0) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "container element write type mismatch"
                    };
                }
                return PackedLogic4::from_aval_bval(
                    target.type.element_width, aval, bval);
            };
        const auto key =
            [&](const runtime::simir::ContainerValue& target,
                const std::uint64_t aval,
                const std::uint64_t bval) {
                if (!target.type.associative) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "associative-array method used on another container"
                    };
                }
                if (target.type.string_indices) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "string-indexed associative array requires a string index"
                    };
                }
                if (bval != 0) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "associative-array index must be a known integral value"
                    };
                }
                return PackedLogic4::from_aval_bval(
                    target.type.index_width, aval, 0);
            };
        const auto key_less =
            [](const runtime::simir::ContainerValue& target,
                const PackedLogic4& left,
                const PackedLogic4& right) {
                const auto lhs = left.low_word().aval;
                const auto rhs = right.low_word().aval;
                if (target.type.signed_indices) {
                    const auto sign = UINT64_C(1)
                        << (target.type.index_width - 1U);
                    const auto lhs_negative = (lhs & sign) != 0;
                    const auto rhs_negative = (rhs & sign) != 0;
                    if (lhs_negative != rhs_negative) {
                        return lhs_negative;
                    }
                }
                return lhs < rhs;
            };
        const auto lower_key =
            [&](const runtime::simir::ContainerValue& target,
                const PackedLogic4& sought) {
                return static_cast<std::size_t>(
                    std::lower_bound(
                        target.keys.begin(), target.keys.end(), sought,
                        [&](const PackedLogic4& candidate,
                            const PackedLogic4& value) {
                            return key_less(target, candidate, value);
                        })
                    - target.keys.begin());
            };
        const auto key_equal =
            [](const PackedLogic4& left,
                const PackedLogic4& right) {
                return left.low_word().aval
                    == right.low_word().aval;
            };
        const auto string_key =
            [&](const runtime::simir::ContainerValue& target,
                const runtime::simir::StringRegisterId index_register)
            -> const std::string& {
            if (!target.type.associative
                || !target.type.string_indices) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "integral-indexed associative array requires an integral index"
                };
            }
            const auto& value = state.executor->string_registers_.at(index_register);
            if (value.size() > runtime::simir::maximum_string_bytes) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "associative-array string index exceeds the byte limit"
                };
            }
            return value;
        };
        const auto lower_string_key =
            [](const runtime::simir::ContainerValue& target,
                const std::string_view sought) {
                return static_cast<std::size_t>(
                    std::lower_bound(
                        target.string_keys.begin(), target.string_keys.end(),
                        sought)
                    - target.string_keys.begin());
            };
        const auto aggregate_element_type =
            [](const runtime::simir::ContainerType& type) {
                auto result = type;
                result.queue = false;
                result.associative = false;
                result.fixed = false;
                result.aggregate_value = true;
                result.maximum_elements.reset();
                result.dimensions.clear();
                result.index_left = 0;
                result.index_right = 0;
                return result;
            };
        if (const auto* scalar = fsim::runtime::simir::operation_get_if<
                runtime::simir::SystemVerilogScalarBinary>(&operation)) {
            const auto width = [](const auto kind) {
                return kind == runtime::SystemVerilogScalarKind::ShortReal
                    ? 32U
                    : 64U;
            };
            const auto value = runtime::systemverilog_scalar_binary_payload(
                scalar->operation,
                PackedLogic4::from_aval_bval(
                    width(scalar->lhs_kind), input0_aval, input0_bval),
                scalar->lhs_kind,
                PackedLogic4::from_aval_bval(
                    width(scalar->rhs_kind), input1_aval, input1_bval),
                scalar->rhs_kind,
                scalar->result_kind);
            if (!value) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "SystemVerilog scalar binary operation failed"
                };
            }
            const auto word = value.value.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* math = fsim::runtime::simir::operation_get_if<
                       runtime::simir::SystemVerilogMath>(&operation)) {
            if (math->function
                >= runtime::SystemVerilogMathFunction::Time) {
                const auto function
                    = math->function
                        == runtime::SystemVerilogMathFunction::Time
                    ? runtime::SystemVerilogTimeFunction::Time
                    : math->function
                        == runtime::SystemVerilogMathFunction::Stime
                    ? runtime::SystemVerilogTimeFunction::Stime
                    : runtime::SystemVerilogTimeFunction::Realtime;
                const auto time_format
                    = state.context->systemverilog_time_format();
                const auto value = runtime::systemverilog_time_function(
                    function,
                    state.context->current_time(),
                    { math->time_unit_femtoseconds,
                        math->time_precision_femtoseconds,
                        time_format.resolution_femtoseconds });
                if (!value) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "SystemVerilog time query failed"
                    };
                }
                const auto encoded = math->function
                        == runtime::SystemVerilogMathFunction::Realtime
                    ? runtime::encode_systemverilog_scalar_payload(value.value)
                    : runtime::systemverilog_scalar_to_packed(
                          value.value,
                          math->function
                                  == runtime::SystemVerilogMathFunction::Stime
                              ? 32U
                              : 64U,
                          false);
                if (!encoded) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "SystemVerilog time-query payload encoding failed"
                    };
                }
                const auto word = encoded.value.low_word();
                *result_aval = word.aval;
                *result_bval = word.bval;
                return 0;
            }
            const auto first = state.executor->read_register(
                math->first, math->first_width);
            std::optional<runtime::PackedLogic4> second;
            if (math->second_width != 0) {
                second = state.executor->read_register(
                    math->second, math->second_width);
            }
            const auto value = runtime::systemverilog_math_payload(
                math->function,
                first,
                math->first_kind,
                math->first_signed,
                second ? &*second : nullptr,
                math->second_kind,
                math->second_signed);
            if (!value) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "SystemVerilog math function failed"
                };
            }
            const auto word = value.value.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* resize = fsim::runtime::simir::operation_get_if<runtime::simir::ResizeContainer>(
                       &operation)) {
            auto& target = mutable_container(resize->target);
            if ((target.type.queue && !resize->allow_queue)
                || target.type.associative || target.type.fixed) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    target.type.queue
                        ? "new[size] cannot resize a queue"
                        : target.type.associative
                        ? "new[size] cannot resize an associative array"
                        : "new[size] cannot resize a static array"
                };
            }
            const auto size = index(
                input0_aval, input0_bval, false,
                "dynamic-array size");
            const runtime::simir::ContainerValue* initializer { };
            if (resize->initializer) {
                const auto& source = read_container(*resize->initializer);
                require_same(target, source);
                initializer = &source;
            }
            runtime::simir::resize_container_value(
                target, size, initializer);
        } else if (const auto* copy = fsim::runtime::simir::operation_get_if<
                       runtime::simir::CopyContainerRegister>(
                       &operation)) {
            auto& target = mutable_container(copy->destination);
            const auto& source = read_container(copy->source);
            require_same(target, source);
            target = source;
        } else if (const auto* conditional = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ConditionalContainerSelect>(
                       &operation)) {
            auto& target = mutable_container(conditional->destination);
            const auto& when_true = read_container(conditional->when_true);
            const auto& when_false = read_container(conditional->when_false);
            runtime::simir::select_container_value(
                target,
                PackedLogic4::from_aval_bval(
                    1, input0_aval, input0_bval),
                when_true,
                when_false);
        } else if (const auto* comparison = fsim::runtime::simir::operation_get_if<runtime::simir::CompareContainers>(
                       &operation)) {
            const auto result = runtime::simir::compare_container_values(
                read_container(comparison->lhs),
                read_container(comparison->rhs),
                comparison->case_equal);
            const auto word = result.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* read_object = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ReadContainerObject>(
                       &operation)) {
            state.executor->alias_container_register(
                read_object->destination, read_object->object, *state.context);
        } else if (const auto* write_object = fsim::runtime::simir::operation_get_if<
                       runtime::simir::WriteContainerObject>(
                       &operation)) {
            state.context->write_container_object(
                write_object->object,
                read_container(write_object->source));
            if (write_object->transaction_signal) {
                state.context->write_update_word(
                    *write_object->transaction_signal,
                    runtime::Logic4Word { 1, 0, 0 });
            }
        } else if (const auto* write_element = fsim::runtime::simir::operation_get_if<
                       runtime::simir::WriteContainerObjectElement>(
                       &operation)) {
            const auto packed_input = [&](const runtime::simir::RegisterId id,
                                          const std::uint64_t aval,
                                          const std::uint64_t bval) {
                const auto width = state.executor->layout_.register_widths.at(id);
                return width <= 64U
                    ? PackedLogic4::from_aval_bval(width, aval, bval)
                    : state.executor->read_register(id, width);
            };
            state.context->write_container_object_element(
                write_element->object,
                packed_input(
                    write_element->index, input0_aval, input0_bval),
                write_element->signed_index,
                write_element->linear_index,
                packed_input(
                    write_element->source, input1_aval, input1_bval),
                state.generated_process,
                instruction,
                write_element->nonblocking);
            if (write_element->transaction_signal) {
                state.context->write_update_word(
                    *write_element->transaction_signal,
                    runtime::Logic4Word { 1, 0, 0 });
            }
        } else if (const auto* size = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerSize>(
                       &operation)) {
            *result_aval = runtime::simir::container_value_size(
                read_container(size->source));
        } else if (const auto* reduction = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerReduction>(
                       &operation)) {
            const auto result = runtime::simir::reduce_container_value(
                read_container(reduction->source),
                reduction->operation,
                reduction->transformation);
            const auto word = result.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* ordering = fsim::runtime::simir::operation_get_if<runtime::simir::OrderContainer>(
                       &operation)) {
            runtime::simir::order_container_value(
                mutable_container(ordering->target),
                ordering->operation, ordering->key,
                [&]() {
                    const auto word = state.context->random_value(
                                                       runtime::simir::RandomKind::urandom,
                                                       std::nullopt, std::nullopt)
                                          .low_word();
                    if (word.width != 32U || word.bval != 0) {
                        throw compiler::LlvmJitError {
                            "generated container shuffle received invalid entropy"
                        };
                    }
                    return static_cast<std::uint32_t>(word.aval);
                });
        } else if (const auto* locator = fsim::runtime::simir::operation_get_if<runtime::simir::LocateContainer>(
                       &operation)) {
            runtime::simir::locate_container_values(
                mutable_container(locator->destination),
                read_container(locator->source),
                locator->operation,
                locator->predicate,
                locator->transformation);
        } else if (const auto* read = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerRead>(
                       &operation)) {
            const auto& source = read_container(read->source);
            const auto publish = [&](const PackedLogic4& value) {
                if (value.width() > 64) {
                    state.executor->write_register(read->destination, value);
                } else {
                    const auto word = value.low_word();
                    *result_aval = word.aval;
                    *result_bval = word.bval;
                }
            };
            if (source.type.associative) {
                if (read->string_index) {
                    const auto& sought = string_key(source, read->index);
                    const auto at = lower_string_key(source, sought);
                    publish(
                        at < source.string_keys.size()
                                && source.string_keys[at] == sought
                            ? source.elements[at]
                            : PackedLogic4(
                                  source.type.element_width,
                                  runtime::Logic4::zero));
                    return 0;
                }
                const auto sought = key(source, input0_aval, input0_bval);
                const auto at = lower_key(source, sought);
                publish(
                    at < source.keys.size()
                            && key_equal(source.keys[at], sought)
                        ? source.elements[at]
                        : PackedLogic4(
                              source.type.element_width,
                              runtime::Logic4::zero));
                return 0;
            }
            std::optional<std::size_t> selected_offset;
            if (source.type.fixed) {
                try {
                    selected_offset = read->linear_index
                        ? index(
                              input0_aval, input0_bval, true,
                              "multidimensional linear index")
                        : packed_fixed_offset(
                              source, read->index,
                              input0_aval, input0_bval);
                } catch (const runtime::simir::InterpreterError&) {
                    publish(PackedLogic4(
                        source.type.element_width,
                        source.type.two_state
                            ? runtime::Logic4::zero
                            : runtime::Logic4::x));
                    return 0;
                }
            } else {
                selected_offset = index(
                    input0_aval, input0_bval, read->signed_index,
                    "container index");
            }
            const auto at = *selected_offset;
            if (at >= source.elements.size()) {
                if (source.type.fixed) {
                    publish(PackedLogic4(
                        source.type.element_width,
                        source.type.two_state
                            ? runtime::Logic4::zero
                            : runtime::Logic4::x));
                    return 0;
                }
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index is out of range"
                };
            }
            publish(source.elements[at]);
        } else if (const auto* write = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerWrite>(
                       &operation)) {
            auto& target = mutable_container(write->target);
            const auto source_element = [&] {
                return target.type.element_width > 64
                    ? state.executor->read_register(
                          write->source, target.type.element_width)
                    : element(target, input1_aval, input1_bval);
            };
            if (target.type.associative) {
                if (write->string_index) {
                    const auto& sought = string_key(target, write->index);
                    const auto source = source_element();
                    const auto at = lower_string_key(target, sought);
                    if (at < target.string_keys.size()
                        && target.string_keys[at] == sought) {
                        target.elements[at] = source;
                    } else {
                        if (target.elements.size()
                            >= runtime::simir::maximum_container_elements(
                                target.type)) {
                            throw runtime::simir::InterpreterError {
                                process, instruction,
                                "associative array exceeds the per-container "
                                "owning-storage budget"
                            };
                        }
                        target.string_keys.insert(
                            target.string_keys.begin()
                                + static_cast<std::ptrdiff_t>(at),
                            sought);
                        target.elements.insert(
                            target.elements.begin()
                                + static_cast<std::ptrdiff_t>(at),
                            source);
                    }
                    if (runtime::simir::container_value_storage_bytes(target)
                        > runtime::simir::maximum_container_storage_bytes) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative array exceeds its recursive owning-storage budget"
                        };
                    }
                    return 0;
                }
                const auto sought = key(target, input0_aval, input0_bval);
                const auto source = source_element();
                const auto at = lower_key(target, sought);
                if (at < target.keys.size()
                    && key_equal(target.keys[at], sought)) {
                    target.elements[at] = source;
                } else {
                    if (target.elements.size()
                        >= runtime::simir::maximum_container_elements(target.type)) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative array exceeds the per-container "
                            "owning-storage budget"
                        };
                    }
                    target.keys.insert(
                        target.keys.begin() + static_cast<std::ptrdiff_t>(at),
                        sought);
                    target.elements.insert(
                        target.elements.begin() + static_cast<std::ptrdiff_t>(at),
                        source);
                }
                return 0;
            }
            const auto at = target.type.fixed
                ? write->linear_index
                    ? index(
                          input0_aval, input0_bval, true,
                          "multidimensional linear index")
                    : packed_fixed_offset(
                          target, write->index, input0_aval, input0_bval)
                : index(
                      input0_aval, input0_bval, write->signed_index,
                      "container index");
            if (at >= target.elements.size()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index is out of range"
                };
            }
            target.elements[at] = source_element();
        } else if (const auto* string_read = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerStringRead>(&operation)) {
            const auto& source = read_container(string_read->source);
            if (!string_read->members.empty()) {
                if (source.type.element_kind
                        != runtime::simir::ContainerElementKind::Aggregate
                    || source.type.associative || string_read->string_index) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate string member read requires an indexed aggregate container"
                    };
                }
                const auto at = source.type.fixed
                    ? string_read->linear_index
                        ? index(
                              input0_aval, input0_bval, true,
                              "multidimensional linear index")
                        : fixed_offset(source, input0_aval, input0_bval)
                    : index(
                          input0_aval, input0_bval,
                          string_read->signed_index, "container index");
                if (at >= source.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate container index is out of range"
                    };
                }
                const auto* selected = &source.nested_elements[at];
                for (const auto member : string_read->members) {
                    if (selected->type.element_kind
                            != runtime::simir::ContainerElementKind::Aggregate
                        || member >= selected->nested_elements.size()) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "aggregate string member read path is invalid"
                        };
                    }
                    selected = &selected->nested_elements[member];
                }
                if (selected->type.element_kind
                        != runtime::simir::ContainerElementKind::String
                    || !selected->type.fixed
                    || selected->string_elements.size() != 1U) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate string member read requires a scalar string leaf"
                    };
                }
                state.executor->write_string_register(
                    string_read->destination,
                    selected->string_elements.front());
                return 0;
            }
            if (source.type.element_kind
                != runtime::simir::ContainerElementKind::String) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "string element read requires a string container"
                };
            }
            if (source.type.associative) {
                if (string_read->string_index) {
                    const auto& sought = string_key(source, string_read->index);
                    const auto at = lower_string_key(source, sought);
                    state.executor->write_string_register(
                        string_read->destination,
                        at < source.string_keys.size()
                                && source.string_keys[at] == sought
                            ? source.string_elements[at]
                            : std::string { });
                    return 0;
                }
                const auto sought = key(source, input0_aval, input0_bval);
                const auto at = lower_key(source, sought);
                state.executor->write_string_register(
                    string_read->destination,
                    at < source.keys.size()
                            && key_equal(source.keys[at], sought)
                        ? source.string_elements[at]
                        : std::string { });
                return 0;
            }
            const auto at = source.type.fixed
                ? string_read->linear_index
                    ? index(
                          input0_aval, input0_bval, true,
                          "multidimensional linear index")
                    : fixed_offset(source, input0_aval, input0_bval)
                : index(
                      input0_aval, input0_bval, string_read->signed_index,
                      "container index");
            if (at >= source.string_elements.size()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "string container index is out of range"
                };
            }
            state.executor->write_string_register(
                string_read->destination, source.string_elements[at]);
        } else if (const auto* string_write = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerStringWrite>(&operation)) {
            auto& target = mutable_container(string_write->target);
            if (target.type.element_kind
                != runtime::simir::ContainerElementKind::String) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "string element write requires a string container"
                };
            }
            const auto source = state.executor->string_registers_.at(string_write->source);
            if (target.type.associative) {
                if (string_write->string_index) {
                    const auto& sought = string_key(target, string_write->index);
                    const auto at = lower_string_key(target, sought);
                    if (at < target.string_keys.size()
                        && target.string_keys[at] == sought) {
                        target.string_elements[at] = source;
                    } else {
                        if (target.string_keys.size()
                            >= runtime::simir::maximum_container_elements(
                                target.type)) {
                            throw runtime::simir::InterpreterError {
                                process, instruction,
                                "associative string array exceeds its owning-storage budget"
                            };
                        }
                        target.string_keys.insert(
                            target.string_keys.begin()
                                + static_cast<std::ptrdiff_t>(at),
                            sought);
                        target.string_elements.insert(
                            target.string_elements.begin()
                                + static_cast<std::ptrdiff_t>(at),
                            source);
                    }
                    if (runtime::simir::container_value_storage_bytes(target)
                        > runtime::simir::maximum_container_storage_bytes) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative string array exceeds its recursive owning-storage budget"
                        };
                    }
                    return 0;
                }
                const auto sought = key(target, input0_aval, input0_bval);
                const auto at = lower_key(target, sought);
                if (at < target.keys.size()
                    && key_equal(target.keys[at], sought)) {
                    target.string_elements[at] = source;
                } else {
                    if (target.keys.size()
                        >= runtime::simir::maximum_container_elements(target.type)) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative string array exceeds its owning-storage budget"
                        };
                    }
                    target.keys.insert(
                        target.keys.begin() + static_cast<std::ptrdiff_t>(at),
                        sought);
                    target.string_elements.insert(
                        target.string_elements.begin()
                            + static_cast<std::ptrdiff_t>(at),
                        source);
                }
                return 0;
            }
            const auto at = target.type.fixed
                ? string_write->linear_index
                    ? index(
                          input0_aval, input0_bval, true,
                          "multidimensional linear index")
                    : fixed_offset(target, input0_aval, input0_bval)
                : index(
                      input0_aval, input0_bval, string_write->signed_index,
                      "container index");
            if (at >= target.string_elements.size()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "string container index is out of range"
                };
            }
            target.string_elements[at] = source;
        } else if (const auto* element_read = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerElementRead>(&operation)) {
            const auto& source = read_container(element_read->source);
            if (source.type.element_kind
                    != runtime::simir::ContainerElementKind::Container
                || source.type.element_types.size() != 1) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "nested element read requires a nested container"
                };
            }
            const runtime::simir::ContainerValue* selected = nullptr;
            std::optional<runtime::simir::ContainerValue> missing;
            if (source.type.associative) {
                const auto sought = key(source, input0_aval, input0_bval);
                const auto at = lower_key(source, sought);
                if (at < source.keys.size()
                    && key_equal(source.keys[at], sought)) {
                    selected = &source.nested_elements[at];
                } else {
                    missing = runtime::simir::default_container_value(
                        source.type.element_types.front());
                    selected = &*missing;
                }
            } else {
                const auto at = source.type.fixed
                    ? fixed_offset(source, input0_aval, input0_bval)
                    : index(
                          input0_aval, input0_bval,
                          element_read->signed_index, "container index");
                if (at >= source.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "nested container index is out of range"
                    };
                }
                selected = &source.nested_elements[at];
            }
            auto& destination = mutable_container(element_read->destination);
            if (destination.type != selected->type) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "nested element read profile mismatch"
                };
            }
            destination = *selected;
        } else if (const auto* element_write = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerElementWrite>(&operation)) {
            auto& target = mutable_container(element_write->target);
            const auto& source = read_container(element_write->source);
            if (target.type.element_kind
                    != runtime::simir::ContainerElementKind::Container
                || target.type.element_types.size() != 1
                || target.type.element_types.front() != source.type) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "nested element write profile mismatch"
                };
            }
            std::size_t at { };
            if (target.type.associative) {
                const auto sought = key(target, input0_aval, input0_bval);
                at = lower_key(target, sought);
                if (at >= target.keys.size()
                    || !key_equal(target.keys[at], sought)) {
                    if (target.keys.size()
                        >= runtime::simir::maximum_container_elements(target.type)) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative nested container exceeds its owning-storage "
                            "budget"
                        };
                    }
                    target.keys.insert(
                        target.keys.begin() + static_cast<std::ptrdiff_t>(at),
                        sought);
                    target.nested_elements.insert(
                        target.nested_elements.begin()
                            + static_cast<std::ptrdiff_t>(at),
                        runtime::simir::default_container_value(
                            target.type.element_types.front()));
                }
            } else {
                at = target.type.fixed
                    ? fixed_offset(target, input0_aval, input0_bval)
                    : index(
                          input0_aval, input0_bval,
                          element_write->signed_index, "container index");
                if (at >= target.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "nested container index is out of range"
                    };
                }
            }
            target.nested_elements[at] = source;
        } else if (const auto* aggregate_read = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerAggregateRead>(&operation)) {
            const auto& source = read_container(aggregate_read->source);
            if (source.type.element_kind
                    != runtime::simir::ContainerElementKind::Aggregate
                || aggregate_read->members.empty()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "aggregate member read requires an unpacked aggregate "
                    "container"
                };
            }
            std::optional<runtime::simir::ContainerValue> missing;
            const runtime::simir::ContainerValue* selected = nullptr;
            if (source.type.associative) {
                const auto sought = key(source, input0_aval, input0_bval);
                const auto at = lower_key(source, sought);
                if (at < source.keys.size()
                    && key_equal(source.keys[at], sought)) {
                    selected = &source.nested_elements[at];
                } else {
                    missing = runtime::simir::default_container_value(
                        aggregate_element_type(source.type));
                    selected = &*missing;
                }
            } else {
                const auto at = source.type.fixed
                    ? aggregate_read->linear_index
                        ? index(
                              input0_aval, input0_bval, true,
                              "multidimensional linear index")
                        : fixed_offset(source, input0_aval, input0_bval)
                    : index(
                          input0_aval, input0_bval,
                          aggregate_read->signed_index,
                          "container index");
                if (at >= source.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate container index is out of range"
                    };
                }
                selected = &source.nested_elements[at];
            }
            for (const auto member : aggregate_read->members) {
                if (selected->type.element_kind
                        != runtime::simir::ContainerElementKind::Aggregate
                    || member >= selected->nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate member read path is invalid"
                    };
                }
                selected = &selected->nested_elements[member];
            }
            if (!selected->type.fixed
                || selected->elements.size() != 1U) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "aggregate member read requires a packed or scalar leaf"
                };
            }
            const auto word = selected->elements.front().low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* aggregate_write = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerAggregateWrite>(&operation)) {
            auto& target = mutable_container(aggregate_write->target);
            if (target.type.element_kind
                    != runtime::simir::ContainerElementKind::Aggregate
                || aggregate_write->members.empty()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "aggregate member write requires an unpacked aggregate "
                    "container"
                };
            }
            std::size_t at { };
            if (target.type.associative) {
                const auto sought = key(target, input0_aval, input0_bval);
                at = lower_key(target, sought);
                if (at >= target.keys.size()
                    || !key_equal(target.keys[at], sought)) {
                    if (target.keys.size()
                        >= runtime::simir::maximum_container_elements(target.type)) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative aggregate exceeds its owning-storage budget"
                        };
                    }
                    target.keys.insert(
                        target.keys.begin() + static_cast<std::ptrdiff_t>(at),
                        sought);
                    target.nested_elements.insert(
                        target.nested_elements.begin()
                            + static_cast<std::ptrdiff_t>(at),
                        runtime::simir::default_container_value(
                            aggregate_element_type(target.type)));
                }
            } else {
                at = target.type.fixed
                    ? aggregate_write->linear_index
                        ? index(
                              input0_aval, input0_bval, true,
                              "multidimensional linear index")
                        : fixed_offset(target, input0_aval, input0_bval)
                    : index(
                          input0_aval, input0_bval,
                          aggregate_write->signed_index,
                          "container index");
                if (at >= target.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate container index is out of range"
                    };
                }
            }
            auto* selected = &target.nested_elements[at];
            runtime::simir::ContainerValue* direct_union = nullptr;
            for (std::size_t path_index = 0;
                path_index < aggregate_write->members.size(); ++path_index) {
                const auto member = aggregate_write->members[path_index];
                if (selected->type.element_kind
                        != runtime::simir::ContainerElementKind::Aggregate
                    || member >= selected->nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate member write path is invalid"
                    };
                }
                if (selected->type.union_aggregate
                    && path_index + 1U
                        == aggregate_write->members.size()) {
                    direct_union = selected;
                }
                selected = &selected->nested_elements[member];
            }
            const auto value = runtime::PackedLogic4::from_aval_bval(
                selected->type.element_width, input1_aval, input1_bval);
            if (!selected->type.fixed
                || selected->elements.size() != 1U
                || (selected->type.two_state && input1_bval != 0)) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "aggregate member write leaf type mismatch"
                };
            }
            selected->elements.front() = value;
            if (direct_union != nullptr) {
                for (auto& sibling : direct_union->nested_elements) {
                    if (sibling.type == selected->type && sibling.type.fixed
                        && sibling.elements.size() == 1U) {
                        sibling.elements.front() = value;
                    }
                }
            }
        } else if (const auto* aggregate_copy = fsim::runtime::simir::operation_get_if<
                       runtime::simir::CopyContainerAggregateElement>(
                       &operation)) {
            auto& target = mutable_container(aggregate_copy->target);
            const auto& source = read_container(aggregate_copy->source);
            if (target.type != source.type
                || target.type.element_kind
                    != runtime::simir::ContainerElementKind::Aggregate) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "aggregate element copy requires identical container profiles"
                };
            }
            std::optional<runtime::simir::ContainerValue> missing;
            const runtime::simir::ContainerValue* source_element = nullptr;
            if (source.type.associative) {
                const auto sought = key(source, input1_aval, input1_bval);
                const auto source_at = lower_key(source, sought);
                if (source_at < source.keys.size()
                    && key_equal(source.keys[source_at], sought)) {
                    source_element = &source.nested_elements[source_at];
                } else {
                    missing = runtime::simir::default_container_value(
                        aggregate_element_type(source.type));
                    source_element = &*missing;
                }
            } else {
                const auto source_at = source.type.fixed
                    ? fixed_offset(source, input1_aval, input1_bval)
                    : index(
                          input1_aval, input1_bval,
                          aggregate_copy->source_signed_index,
                          "source container index");
                if (source_at >= source.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate element copy source index is out of range"
                    };
                }
                source_element = &source.nested_elements[source_at];
            }
            const auto snapshot = *source_element;
            std::size_t target_at { };
            if (target.type.associative) {
                const auto sought = key(target, input0_aval, input0_bval);
                target_at = lower_key(target, sought);
                if (target_at >= target.keys.size()
                    || !key_equal(target.keys[target_at], sought)) {
                    if (target.keys.size()
                        >= runtime::simir::maximum_container_elements(target.type)) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "associative aggregate exceeds its owning-storage budget"
                        };
                    }
                    target.keys.insert(
                        target.keys.begin()
                            + static_cast<std::ptrdiff_t>(target_at),
                        sought);
                    target.nested_elements.insert(
                        target.nested_elements.begin()
                            + static_cast<std::ptrdiff_t>(target_at),
                        runtime::simir::default_container_value(
                            aggregate_element_type(target.type)));
                }
            } else {
                target_at = target.type.fixed
                    ? fixed_offset(target, input0_aval, input0_bval)
                    : index(
                          input0_aval, input0_bval,
                          aggregate_copy->target_signed_index,
                          "target container index");
                if (target_at >= target.nested_elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "aggregate element copy target index is out of range"
                    };
                }
            }
            if (target.nested_elements[target_at].type != snapshot.type) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "aggregate element copy profile mismatch"
                };
            }
            target.nested_elements[target_at] = snapshot;
        } else if (const auto* erase = fsim::runtime::simir::operation_get_if<runtime::simir::DeleteContainer>(
                       &operation)) {
            auto& target = mutable_container(erase->target);
            const bool aggregate = target.type.element_kind
                == runtime::simir::ContainerElementKind::Aggregate;
            const bool string_element = target.type.element_kind
                == runtime::simir::ContainerElementKind::String;
            if (erase->index) {
                if (target.type.queue) {
                    const auto at = index(
                        input0_aval, input0_bval, true,
                        "queue delete index");
                    const auto element_count = runtime::simir::container_value_size(target);
                    if (at >= element_count) {
                        throw runtime::simir::InterpreterError {
                            process, instruction,
                            "queue delete index is out of range"
                        };
                    }
                    if (aggregate) {
                        target.nested_elements.erase(
                            target.nested_elements.begin()
                                + static_cast<std::ptrdiff_t>(at));
                    } else if (string_element) {
                        target.string_elements.erase(
                            target.string_elements.begin()
                                + static_cast<std::ptrdiff_t>(at));
                    } else {
                        target.elements.erase(
                            target.elements.begin()
                                + static_cast<std::ptrdiff_t>(at));
                    }
                } else {
                    if (erase->string_index) {
                        const auto& sought = string_key(target, *erase->index);
                        const auto at = lower_string_key(target, sought);
                        if (at < target.string_keys.size()
                            && target.string_keys[at] == sought) {
                            target.string_keys.erase(
                                target.string_keys.begin()
                                    + static_cast<std::ptrdiff_t>(at));
                            if (aggregate) {
                                target.nested_elements.erase(
                                    target.nested_elements.begin()
                                        + static_cast<std::ptrdiff_t>(at));
                            } else if (string_element) {
                                target.string_elements.erase(
                                    target.string_elements.begin()
                                        + static_cast<std::ptrdiff_t>(at));
                            } else {
                                target.elements.erase(
                                    target.elements.begin()
                                        + static_cast<std::ptrdiff_t>(at));
                            }
                        }
                        return 0;
                    }
                    const auto sought = key(target, input0_aval, input0_bval);
                    const auto at = lower_key(target, sought);
                    if (at < target.keys.size()
                        && key_equal(target.keys[at], sought)) {
                        target.keys.erase(
                            target.keys.begin()
                                + static_cast<std::ptrdiff_t>(at));
                        if (aggregate) {
                            target.nested_elements.erase(
                                target.nested_elements.begin()
                                    + static_cast<std::ptrdiff_t>(at));
                        } else {
                            target.elements.erase(
                                target.elements.begin()
                                    + static_cast<std::ptrdiff_t>(at));
                        }
                    }
                }
            } else {
                if (target.type.fixed) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "delete() cannot clear a static array"
                    };
                }
                if (aggregate) {
                    target.nested_elements.clear();
                } else if (string_element) {
                    target.string_elements.clear();
                } else {
                    target.elements.clear();
                }
                target.keys.clear();
                target.string_keys.clear();
            }
        } else if (const auto* load = fsim::runtime::simir::operation_get_if<runtime::simir::LoadMemory>(
                       &operation)) {
            const auto known_optional =
                [&](const std::optional<runtime::simir::RegisterId> source,
                    const std::uint64_t aval,
                    const std::uint64_t bval,
                    const std::string_view role)
                -> std::optional<std::int32_t> {
                if (!source) {
                    return std::nullopt;
                }
                if (bval != 0) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        std::string { role }
                            + " must be a known 32-bit integral value"
                    };
                }
                return static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(aval));
            };
            const auto start = known_optional(
                load->start, input0_aval, input0_bval,
                load->write ? "write-memory start" : "read-memory start");
            const auto finish = known_optional(
                load->finish, input1_aval, input1_bval,
                load->write ? "write-memory finish" : "read-memory finish");
            if (load->write) {
                const auto text = runtime::simir::write_memory_text(
                    read_container(load->target), load->hexadecimal, start,
                    finish);
                const auto handle = state.context->open_file(
                    state.executor->string_registers_.at(load->path), "w");
                try {
                    state.context->write_file(handle, text, false);
                    state.context->close_file(handle);
                } catch (...) {
                    try {
                        state.context->close_file(handle);
                    } catch (...) {
                    }
                    throw;
                }
                return 0;
            }
            const auto handle = state.context->open_file(
                state.executor->string_registers_.at(load->path), "r");
            std::string text;
            try {
                while (!state.context->file_end_of_file(handle)) {
                    std::uint32_t count { };
                    auto line = state.context->read_file_line(handle, count);
                    if (text.size() + line.size()
                        > runtime::simir::maximum_memory_file_bytes) {
                        throw std::length_error {
                            "read-memory file exceeds the 1 MiB limit"
                        };
                    }
                    text += line;
                }
                state.context->close_file(handle);
            } catch (...) {
                try {
                    state.context->close_file(handle);
                } catch (...) {
                }
                throw;
            }
            runtime::simir::load_memory_text(
                mutable_container(load->target), text, load->hexadecimal,
                start, finish);
        } else if (const auto* exists = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerExists>(
                       &operation)) {
            const auto& source = read_container(exists->source);
            if (exists->string_index) {
                const auto& sought = string_key(source, exists->index);
                const auto at = lower_string_key(source, sought);
                *result_aval = at < source.string_keys.size()
                        && source.string_keys[at] == sought
                    ? 1U
                    : 0U;
                return 0;
            }
            const auto sought = key(source, input0_aval, input0_bval);
            const auto at = lower_key(source, sought);
            *result_aval = at < source.keys.size()
                    && key_equal(source.keys[at], sought)
                ? 1U
                : 0U;
        } else if (const auto* traverse = fsim::runtime::simir::operation_get_if<runtime::simir::TraverseContainer>(
                       &operation)) {
            const auto& source = read_container(traverse->source);
            if (!source.type.associative) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "first/last/next/prev require an associative array"
                };
            }
            if (traverse->string_index) {
                std::optional<std::size_t> selected;
                if (!source.string_keys.empty()) {
                    if (traverse->traversal
                        == runtime::simir::ContainerTraversal::first) {
                        selected = 0;
                    } else if (traverse->traversal
                        == runtime::simir::ContainerTraversal::last) {
                        selected = source.string_keys.size() - 1U;
                    } else {
                        const auto& sought = string_key(source, traverse->index);
                        const auto at = lower_string_key(source, sought);
                        if (traverse->traversal
                            == runtime::simir::ContainerTraversal::next) {
                            const auto next = at < source.string_keys.size()
                                    && source.string_keys[at] == sought
                                ? at + 1U
                                : at;
                            if (next < source.string_keys.size())
                                selected = next;
                        } else if (at != 0) {
                            selected = at - 1U;
                        }
                    }
                }
                if (selected) {
                    state.executor->write_string_register(
                        traverse->index, source.string_keys[*selected]);
                }
                *result_aval = selected ? 1U : 0U;
                return 0;
            }
            std::optional<std::size_t> selected;
            if (!source.keys.empty()) {
                if (traverse->traversal
                    == runtime::simir::ContainerTraversal::first) {
                    selected = 0;
                } else if (
                    traverse->traversal
                    == runtime::simir::ContainerTraversal::last) {
                    selected = source.keys.size() - 1U;
                } else {
                    const auto sought = key(source, input0_aval, input0_bval);
                    const auto at = lower_key(source, sought);
                    if (traverse->traversal
                        == runtime::simir::ContainerTraversal::next) {
                        const auto next = at < source.keys.size()
                                && key_equal(source.keys[at], sought)
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
            if (input1_aval == 0) {
                if (selected) {
                    const auto word = source.keys[*selected].low_word();
                    *result_aval = word.aval;
                    *result_bval = word.bval;
                } else {
                    *result_aval = input0_aval;
                    *result_bval = input0_bval;
                }
            } else {
                *result_aval = selected ? 1U : 0U;
            }
        } else if (const auto* push = fsim::runtime::simir::operation_get_if<runtime::simir::PushContainer>(
                       &operation)) {
            auto& target = mutable_container(push->target);
            if (!target.type.queue) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    target.type.associative
                        ? "queue method used on an associative array"
                        : "queue method used on a dynamic array"
                };
            }
            const auto source = element(target, input0_aval, input0_bval);
            if (push->index) {
                const auto at = index(
                    input1_aval, input1_bval, true,
                    "queue insert index");
                if (at > target.elements.size()) {
                    throw runtime::simir::InterpreterError {
                        process, instruction,
                        "queue insert index is out of range"
                    };
                }
                target.elements.insert(
                    target.elements.begin() + static_cast<std::ptrdiff_t>(at),
                    source);
            } else if (push->front) {
                target.elements.insert(target.elements.begin(), source);
            } else {
                target.elements.push_back(source);
            }
            const auto storage_limit = runtime::simir::maximum_container_elements(target.type);
            if (target.elements.size() > storage_limit
                && (!target.type.maximum_elements
                    || *target.type.maximum_elements > storage_limit)) {
                target.elements.pop_back();
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "queue exceeds the per-container owning-storage budget"
                };
            }
            if (target.type.maximum_elements
                && target.elements.size() > *target.type.maximum_elements) {
                target.elements.pop_back();
            }
        } else if (const auto* pop = fsim::runtime::simir::operation_get_if<runtime::simir::PopContainer>(
                       &operation)) {
            auto& target = mutable_container(pop->target);
            if (!target.type.queue || target.elements.empty()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    target.type.queue
                        ? "cannot pop an empty queue"
                        : (target.type.associative
                                  ? "queue method used on an associative array"
                                  : "queue method used on a dynamic array")
                };
            }
            const auto source = pop->front
                ? target.elements.front()
                : target.elements.back();
            const auto word = source.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
            if (pop->front) {
                target.elements.erase(target.elements.begin());
            } else {
                target.elements.pop_back();
            }
        } else {
            const auto alternative
                = runtime::simir::operation_alternative_index(operation);
            throw compiler::LlvmJitError {
                "compiled container callback has the wrong operation at process "
                + std::to_string(process) + ", instruction "
                + std::to_string(instruction) + " (group "
                + std::to_string(
                    runtime::simir::operation_group_index(operation))
                + ", alternative "
                + std::to_string(alternative) + ")"
            };
        }
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

#endif

} // namespace fsim::app::application_detail
