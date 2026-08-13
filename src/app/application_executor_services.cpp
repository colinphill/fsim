// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/runtime/file_binary.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>

namespace fsim::app::application_detail {

SystemCProcessExecutor::SystemCProcessExecutor(
    std::shared_ptr<systemc::HierarchyRegistry> hierarchy,
    const std::uint64_t process)
    : hierarchy_(std::move(hierarchy))
    , process_(process)
{
    if (!hierarchy_) {
        throw std::invalid_argument {
            "SystemC process executor requires a hierarchy registry"
        };
    }
}

runtime::simir::ProcessResumeResult SystemCProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    runtime::simir::InstructionIndex)
{
    const auto suspension = hierarchy_->invoke_process(process_, context);
    runtime::simir::ProcessResumeResult result { 0, 1 };
    switch (suspension.kind) {
    case systemc::MethodSuspendKind::halt:
        result.external.kind = runtime::simir::ExternalSuspendKind::halt;
        break;
    case systemc::MethodSuspendKind::static_sensitivity:
        result.external.kind = runtime::simir::ExternalSuspendKind::wait_sensitivity;
        break;
    case systemc::MethodSuspendKind::wait_for:
        result.external.kind = suspension.delay_ticks == 0
            ? runtime::simir::ExternalSuspendKind::yield
            : runtime::simir::ExternalSuspendKind::wait_for;
        result.external.delay = suspension.delay_ticks;
        break;
    case systemc::MethodSuspendKind::wait_event:
        result.external.kind = runtime::simir::ExternalSuspendKind::wait_on;
        result.external.wait_all = suspension.wait_all;
        result.external.timeout = suspension.timeout_ticks;
        result.external.sensitivity.reserve(
            suspension.event_signals.size());
        for (const auto event : suspension.event_signals) {
            result.external.sensitivity.push_back(
                { event, runtime::simir::EdgeKind::any });
        }
        break;
    }
    return result;
}

void SystemCProcessExecutor::update_channel(
    const std::uint64_t channel,
    runtime::simir::ProcessExecutionContext& context)
{
    hierarchy_->invoke_primitive_channel(channel, context);
}

std::string_view report_severity_name(
    const runtime::simir::AssertionSeverity severity) noexcept
{
    switch (severity) {
    case runtime::simir::AssertionSeverity::note:
        return "note";
    case runtime::simir::AssertionSeverity::warning:
        return "warning";
    case runtime::simir::AssertionSeverity::error:
        return "error";
    case runtime::simir::AssertionSeverity::failure:
        return "failure";
    }
    return "error";
}

std::uint64_t entropy_seed()
{
    std::random_device source;
    const auto high = static_cast<std::uint64_t>(source());
    const auto low = static_cast<std::uint64_t>(source());
    return (high << 32U) ^ low;
}

#if defined(FSIM_HAS_LLVM)

void LlvmProcessExecutor::write_report(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        if (state.context == nullptr
            || state.process == nullptr
            || state.process->id != process
            || instruction >= state.process->operations.size()) {
            throw std::logic_error {
                "invalid generated report callback"
            };
        }
        const auto* report = fsim::runtime::simir::operation_get_if<runtime::simir::Report>(
            &state.process->operations[instruction]);
        if (report != nullptr) {
            state.context->report(
                report->message, report->severity, report->source);
            return;
        }
        const auto* string_report = fsim::runtime::simir::operation_get_if<runtime::simir::StringReport>(
            &state.process->operations[instruction]);
        if (string_report != nullptr) {
            const auto encoded = state.executor->read_register(
                string_report->severity, 2);
            if (encoded.get(0) == runtime::Logic4::x
                || encoded.get(0) == runtime::Logic4::z
                || encoded.get(1) == runtime::Logic4::x
                || encoded.get(1) == runtime::Logic4::z) {
                throw runtime::simir::InterpreterError(
                    process,
                    instruction,
                    "VHDL severity expression produced an invalid value");
            }
            const auto ordinal = (encoded.get(0) == runtime::Logic4::one ? 1U : 0U)
                | (encoded.get(1) == runtime::Logic4::one ? 2U : 0U);
            const auto severity = static_cast<
                runtime::simir::AssertionSeverity>(ordinal);
            const auto message = state.executor->read_string_register(
                string_report->message);
            if (severity == runtime::simir::AssertionSeverity::failure) {
                if (string_report->standalone) {
                    state.context->report(
                        message, severity, string_report->source);
                }
                throw runtime::simir::AssertionError(
                    process,
                    instruction,
                    message.empty()
                        ? (string_report->standalone
                                  ? "report failure"
                                  : "assertion failed")
                        : message,
                    severity,
                    string_report->source,
                    string_report->standalone);
            }
            state.context->report(
                message, severity, string_report->source);
            return;
        }
        const auto* assertion = fsim::runtime::simir::operation_get_if<runtime::simir::Assert>(
            &state.process->operations[instruction]);
        if (assertion == nullptr
            || assertion->severity
                == runtime::simir::AssertionSeverity::failure) {
            throw std::logic_error {
                "generated report callback references an incompatible "
                "instruction"
            };
        }
        state.context->report(
            assertion->message.empty()
                ? std::string_view { "assertion failed" }
                : std::string_view { assertion->message },
            assertion->severity,
            assertion->source);
    } catch (...) {
        capture_failure(state);
    }
}

[[nodiscard]] std::string LlvmProcessExecutor::read_string_register(
    const runtime::simir::StringRegisterId id) const
{
    if (id >= string_registers_.size()) {
        throw compiler::LlvmJitError {
            "compiled process string-register request is out of range"
        };
    }
    return string_registers_[id];
}

void LlvmProcessExecutor::write_string_register(
    const runtime::simir::StringRegisterId id,
    const std::string_view value)
{
    if (id >= string_registers_.size()
        || value.size() > runtime::simir::maximum_string_bytes) {
        throw compiler::LlvmJitError {
            "compiled process string-register write is out of range"
        };
    }
    string_registers_[id] = value;
}

runtime::simir::ContainerValue
LlvmProcessExecutor::read_container_register(
    const runtime::simir::ContainerRegisterId id) const
{
    return container_registers_.at(id);
}

void LlvmProcessExecutor::write_container_register(
    const runtime::simir::ContainerRegisterId id,
    const runtime::simir::ContainerValue& value)
{
    if (id >= container_registers_.size()
        || container_registers_[id].type != value.type) {
        throw compiler::LlvmJitError {
            "compiled process container-register write is out of range"
        };
    }
    container_registers_[id] = value;
}

runtime::simir::FileHandle LlvmProcessExecutor::checked_file_handle(
    const std::uint64_t aval,
    const std::uint64_t bval)
{
    if (bval != 0
        || aval
            > std::numeric_limits<
                runtime::simir::FileHandle>::max()) {
        throw std::runtime_error {
            "file handle must be a known 32-bit integral value"
        };
    }
    return static_cast<runtime::simir::FileHandle>(aval);
}

void LlvmProcessExecutor::capture_file_failure(
    CallbackState& state,
    const std::uint32_t process,
    const std::uint32_t instruction) noexcept
{
    if (state.failure) {
        return;
    }
    try {
        throw;
    } catch (const runtime::simir::InterpreterError&) {
        state.failure = std::current_exception();
    } catch (const std::exception& error) {
        state.failure = std::make_exception_ptr(
            runtime::simir::InterpreterError {
                process, instruction, error.what() });
    } catch (...) {
        state.failure = std::current_exception();
    }
}

const runtime::simir::Operation&
LlvmProcessExecutor::callback_operation(
    const CallbackState& state,
    const std::uint32_t process,
    const std::uint32_t instruction)
{
    if (state.process == nullptr
        || process != state.process->id
        || instruction >= state.process->operations.size()) {
        throw compiler::LlvmJitError {
            "compiled file callback metadata is out of range"
        };
    }
    return state.process->operations[instruction];
}

std::uint32_t LlvmProcessExecutor::file_open(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    const runtime::simir::FileOpen* operation { };
    try {
        if (result == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-open result pointer is null"
            };
        }
        operation = fsim::runtime::simir::operation_get_if<runtime::simir::FileOpen>(
            &callback_operation(state, process, instruction));
        if (operation == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-open callback has the wrong operation"
            };
        }
        if (operation->vhdl) {
            const auto current = state.executor->read_register(
                                                   operation->destination, 32)
                                     .low_word();
            if (current.bval != 0) {
                throw compiler::LlvmJitError {
                    "VHDL file object handle is unknown"
                };
            }
            if (current.aval != 0) {
                if (!operation->status) {
                    throw runtime::simir::InterpreterError(
                        process, instruction,
                        "VHDL file object is already open");
                }
                *result = static_cast<std::uint32_t>(current.aval);
                return 1;
            }
        }
        *result = state.context->open_file(
            state.executor->string_registers_.at(operation->path),
            state.executor->string_registers_.at(operation->mode));
        return 0;
    } catch (const std::exception& error) {
        if (operation != nullptr && operation->status) {
            *result = 0;
            const auto message = std::string_view { error.what() };
            return message.find("unsupported") != std::string_view::npos
                    && message.find("mode") != std::string_view::npos
                ? 3U
                : 2U;
        }
        capture_file_failure(state, process, instruction);
        return 1;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::file_close(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::FileClose>(
            &callback_operation(state, process, instruction));
        if (operation == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-close callback has the wrong operation"
            };
        }
        const auto handle = checked_file_handle(handle_aval, handle_bval);
        if (handle != 0) {
            state.context->close_file(handle);
        } else if (!operation->ignore_zero) {
            if (operation->clear_handle) {
                throw runtime::simir::InterpreterError(
                    process, instruction, "VHDL file object is not open");
            }
            state.context->close_file(handle);
        }
        return 0;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::file_write(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto handle = checked_file_handle(handle_aval, handle_bval);
        const auto& operation = callback_operation(state, process, instruction);
        if (const auto* literal = fsim::runtime::simir::operation_get_if<runtime::simir::FileWriteLiteral>(
                &operation)) {
            state.context->write_file(
                handle, literal->text, literal->newline);
            return 0;
        }
        if (const auto* formatted = fsim::runtime::simir::operation_get_if<runtime::simir::FileWriteFormatted>(
                &operation)) {
            const auto value = state.executor->read_register(
                formatted->source, formatted->width);
            state.context->write_file_formatted(
                handle,
                formatted->prefix,
                formatted->suffix,
                formatted->format,
                value,
                formatted->signed_decimal,
                formatted->suppress_leading_zero,
                formatted->minimum_width,
                formatted->left_justify,
                formatted->zero_pad,
                formatted->scalar_kind);
            if (formatted->newline) {
                state.context->write_file(handle, { }, true);
            }
            return 0;
        }
        if (const auto* string = fsim::runtime::simir::operation_get_if<runtime::simir::FileWriteString>(
                &operation)) {
            state.context->write_file(
                handle,
                string->prefix
                    + state.executor->string_registers_.at(string->source)
                    + string->suffix,
                string->newline);
            if (string->clear_source) {
                state.executor->write_string_register(string->source, { });
            }
            return 0;
        }
        throw compiler::LlvmJitError {
            "compiled file-write callback has the wrong operation"
        };
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::file_read_line(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (result == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-read result pointer is null"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::FileReadLine>(
            &callback_operation(state, process, instruction));
        if (operation == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-read callback has the wrong operation"
            };
        }
        auto line = state.context->read_file_line(
            checked_file_handle(handle_aval, handle_bval), *result);
        if (operation->vhdl_textio) {
            if (*result == 0) {
                throw compiler::LlvmJitError {
                    "VHDL readline reached end of file"
                };
            }
            if (!line.empty() && line.back() == '\n')
                line.pop_back();
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
        }
        if (*result != 0) {
            switch (operation->target_kind) {
            case runtime::simir::FileTextTargetKind::string_register:
                state.executor->write_string_register(operation->target, line);
                break;
            case runtime::simir::FileTextTargetKind::packed_register:
                state.executor->write_register(
                    operation->target,
                    runtime::simir::pack_file_text(
                        line, operation->target_width));
                break;
            case runtime::simir::FileTextTargetKind::packed_signal:
                state.context->write_blocking(
                    operation->target,
                    runtime::simir::pack_file_text(
                        line, operation->target_width));
                break;
            }
        }
        return 0;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::file_end_of_file(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::FileEndOfFile>(
            &callback_operation(state, process, instruction));
        if (result == nullptr || operation == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-eof callback metadata is invalid"
            };
        }
        const auto handle = checked_file_handle(handle_aval, handle_bval);
        if (operation->lookahead) {
            const auto character = state.context->read_file_character(handle);
            *result = character < 0 ? 1U : 0U;
            if (character >= 0
                && state.context->unread_file_character(handle, character)
                    != character) {
                throw compiler::LlvmJitError {
                    "compiled VHDL endfile could not preserve lookahead"
                };
            }
        } else {
            *result = state.context->file_end_of_file(handle) ? 1U : 0U;
        }
        return 0;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::file_error(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t handle_aval,
    const std::uint64_t handle_bval,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (result == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-error result pointer is null"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::FileErrorStatus>(
            &callback_operation(state, process, instruction));
        if (operation == nullptr) {
            throw compiler::LlvmJitError {
                "compiled file-error callback has the wrong operation"
            };
        }
        bool has_error { };
        auto message = state.context->file_error(
            checked_file_handle(handle_aval, handle_bval),
            has_error);
        switch (operation->target_kind) {
        case runtime::simir::FileTextTargetKind::string_register:
            state.executor->write_string_register(operation->target, message);
            break;
        case runtime::simir::FileTextTargetKind::packed_register:
            state.executor->write_register(
                operation->target,
                runtime::simir::pack_file_text(
                    message, operation->target_width));
            break;
        case runtime::simir::FileTextTargetKind::packed_signal:
            state.context->write_blocking(
                operation->target,
                runtime::simir::pack_file_text(
                    message, operation->target_width));
            break;
        }
        *result = has_error ? 1U : 0U;
        return 0;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

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
                result = input1_bval == 0
                    ? state.context->unread_file_character(
                          handle,
                          static_cast<std::int32_t>(
                              static_cast<std::uint32_t>(input1_aval)))
                    : -1;
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
                container = state.executor->container_registers_.at(binary->target);
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
        auto& registers = state.executor->container_registers_;
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
                const auto layout = state.executor->jit_.frame_layout(
                    state.executor->handle_);
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
            if (value.size() > runtime::simir::maximum_string_bytes
                || !runtime::systemverilog_string_is_valid(value)) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "associative-array string index must be bounded strict UTF-8"
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
            auto& target = registers.at(resize->target);
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
                const auto& source = registers.at(*resize->initializer);
                require_same(target, source);
                initializer = &source;
            }
            runtime::simir::resize_container_value(
                target, size, initializer);
        } else if (const auto* copy = fsim::runtime::simir::operation_get_if<
                       runtime::simir::CopyContainerRegister>(
                       &operation)) {
            auto& target = registers.at(copy->destination);
            const auto& source = registers.at(copy->source);
            require_same(target, source);
            target = source;
        } else if (const auto* conditional = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ConditionalContainerSelect>(
                       &operation)) {
            auto& target = registers.at(conditional->destination);
            const auto& when_true = registers.at(conditional->when_true);
            const auto& when_false = registers.at(conditional->when_false);
            runtime::simir::select_container_value(
                target,
                PackedLogic4::from_aval_bval(
                    1, input0_aval, input0_bval),
                when_true,
                when_false);
        } else if (const auto* comparison = fsim::runtime::simir::operation_get_if<runtime::simir::CompareContainers>(
                       &operation)) {
            const auto result = runtime::simir::compare_container_values(
                registers.at(comparison->lhs),
                registers.at(comparison->rhs),
                comparison->case_equal);
            const auto word = result.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* read_object = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ReadContainerObject>(
                       &operation)) {
            auto& target = registers.at(read_object->destination);
            const auto source = state.context->read_container_object(read_object->object);
            require_same(target, source);
            target = source;
        } else if (const auto* write_object = fsim::runtime::simir::operation_get_if<
                       runtime::simir::WriteContainerObject>(
                       &operation)) {
            state.context->write_container_object(
                write_object->object,
                registers.at(write_object->source));
            if (write_object->transaction_signal) {
                state.context->write_update_word(
                    *write_object->transaction_signal,
                    runtime::Logic4Word { });
            }
        } else if (const auto* size = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerSize>(
                       &operation)) {
            *result_aval = runtime::simir::container_value_size(
                registers.at(size->source));
        } else if (const auto* reduction = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerReduction>(
                       &operation)) {
            const auto result = runtime::simir::reduce_container_value(
                registers.at(reduction->source),
                reduction->operation,
                reduction->transformation);
            const auto word = result.low_word();
            *result_aval = word.aval;
            *result_bval = word.bval;
        } else if (const auto* ordering = fsim::runtime::simir::operation_get_if<runtime::simir::OrderContainer>(
                       &operation)) {
            runtime::simir::order_container_value(
                registers.at(ordering->target),
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
                registers.at(locator->destination),
                registers.at(locator->source),
                locator->operation,
                locator->predicate,
                locator->transformation);
        } else if (const auto* read = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerRead>(
                       &operation)) {
            const auto& source = registers.at(read->source);
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
            const auto at = source.type.fixed
                ? read->linear_index
                    ? index(
                          input0_aval, input0_bval, true,
                          "multidimensional linear index")
                    : packed_fixed_offset(
                          source, read->index, input0_aval, input0_bval)
                : index(
                      input0_aval, input0_bval, read->signed_index,
                      "container index");
            if (at >= source.elements.size()) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index is out of range"
                };
            }
            publish(source.elements[at]);
        } else if (const auto* write = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerWrite>(
                       &operation)) {
            auto& target = registers.at(write->target);
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
                            target.string_keys.begin() + at, sought);
                        target.elements.insert(
                            target.elements.begin() + at, source);
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
                    target.keys.insert(target.keys.begin() + at, sought);
                    target.elements.insert(
                        target.elements.begin() + at, source);
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
            const auto& source = registers.at(string_read->source);
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
            auto& target = registers.at(string_write->target);
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
                            target.string_keys.begin() + at, sought);
                        target.string_elements.insert(
                            target.string_elements.begin() + at, source);
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
                    target.keys.insert(target.keys.begin() + at, sought);
                    target.string_elements.insert(
                        target.string_elements.begin() + at, source);
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
            const auto& source = registers.at(element_read->source);
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
            auto& destination = registers.at(element_read->destination);
            if (destination.type != selected->type) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "nested element read profile mismatch"
                };
            }
            destination = *selected;
        } else if (const auto* element_write = fsim::runtime::simir::operation_get_if<
                       runtime::simir::ContainerElementWrite>(&operation)) {
            auto& target = registers.at(element_write->target);
            const auto& source = registers.at(element_write->source);
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
                    target.keys.insert(target.keys.begin() + at, sought);
                    target.nested_elements.insert(
                        target.nested_elements.begin() + at,
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
            const auto& source = registers.at(aggregate_read->source);
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
            auto& target = registers.at(aggregate_write->target);
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
                    target.keys.insert(target.keys.begin() + at, sought);
                    target.nested_elements.insert(
                        target.nested_elements.begin() + at,
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
            auto& target = registers.at(aggregate_copy->target);
            const auto& source = registers.at(aggregate_copy->source);
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
                    target.keys.insert(target.keys.begin() + target_at, sought);
                    target.nested_elements.insert(
                        target.nested_elements.begin() + target_at,
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
            auto& target = registers.at(erase->target);
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
                            target.nested_elements.begin() + at);
                    } else if (string_element) {
                        target.string_elements.erase(
                            target.string_elements.begin() + at);
                    } else {
                        target.elements.erase(target.elements.begin() + at);
                    }
                } else {
                    if (erase->string_index) {
                        const auto& sought = string_key(target, *erase->index);
                        const auto at = lower_string_key(target, sought);
                        if (at < target.string_keys.size()
                            && target.string_keys[at] == sought) {
                            target.string_keys.erase(
                                target.string_keys.begin() + at);
                            if (aggregate) {
                                target.nested_elements.erase(
                                    target.nested_elements.begin() + at);
                            } else if (string_element) {
                                target.string_elements.erase(
                                    target.string_elements.begin() + at);
                            } else {
                                target.elements.erase(target.elements.begin() + at);
                            }
                        }
                        return 0;
                    }
                    const auto sought = key(target, input0_aval, input0_bval);
                    const auto at = lower_key(target, sought);
                    if (at < target.keys.size()
                        && key_equal(target.keys[at], sought)) {
                        target.keys.erase(target.keys.begin() + at);
                        if (aggregate) {
                            target.nested_elements.erase(
                                target.nested_elements.begin() + at);
                        } else {
                            target.elements.erase(target.elements.begin() + at);
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
                    registers.at(load->target), load->hexadecimal, start, finish);
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
                registers.at(load->target), text, load->hexadecimal,
                start, finish);
        } else if (const auto* exists = fsim::runtime::simir::operation_get_if<runtime::simir::ContainerExists>(
                       &operation)) {
            const auto& source = registers.at(exists->source);
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
            const auto& source = registers.at(traverse->source);
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
            auto& target = registers.at(push->target);
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
                target.elements.insert(target.elements.begin() + at, source);
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
            auto& target = registers.at(pop->target);
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
            const auto alternative = std::visit(
                [](const auto& group) { return group.storage.index(); },
                operation.storage);
            throw compiler::LlvmJitError {
                "compiled container callback has the wrong operation at process "
                + std::to_string(process) + ", instruction "
                + std::to_string(instruction) + " (group "
                + std::to_string(operation.storage.index()) + ", alternative "
                + std::to_string(alternative) + ")"
            };
        }
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

void LlvmProcessExecutor::force_signal_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto value = checked_slice_word(
            state, signal, offset, width, aval, bval);
        state.context->force_signal_slice(
            signal,
            runtime::PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::force_signal_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        state.context->force_signal_slice(
            signal,
            checked_logic9_slice(state, signal, offset, width, value),
            offset);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::release_signal_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        state.context->release_signal_slice(signal, offset, width);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::force_driver_signal_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto value = checked_slice_word(
            state, signal, offset, width, aval, bval);
        state.context->force_driver_signal_slice(
            signal,
            runtime::PackedLogic4::from_aval_bval(
                value.width, value.aval, value.bval),
            offset);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::force_driver_signal_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        state.context->force_driver_signal_slice(
            signal,
            checked_logic9_slice(state, signal, offset, width, value),
            offset);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::release_driver_signal_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        state.context->release_driver_signal_slice(signal, offset, width);
    } catch (...) {
        capture_failure(state);
    }
}

compiler::JitOptimizationLevel jit_optimization(
    const project::Optimization optimization) noexcept
{
    return optimization == project::Optimization::o0
        ? compiler::JitOptimizationLevel::o0
        : compiler::JitOptimizationLevel::o2;
}

#endif

} // namespace fsim::app::application_detail
