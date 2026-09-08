// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_container_profile.hpp"

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
            || state.generated_process != process
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
            if (state.process->language_standard == "2019") {
                state.context->vhdl_report(
                    instruction, message, severity,
                    string_report->source, string_report->standalone);
                return;
            }
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
    if (container_object_aliases_.at(id) != invalid_container_object) {
        throw compiler::LlvmJitError {
            "compiled process exposed an unmaterialized container register"
        };
    }
    return *container_registers_.at(id);
}

void LlvmProcessExecutor::write_container_register(
    const runtime::simir::ContainerRegisterId id,
    const runtime::simir::ContainerValue& value)
{
    if (id >= container_registers_.size() || !container_registers_[id]
        || container_registers_[id]->type != value.type) {
        throw compiler::LlvmJitError {
            "compiled process container-register write is out of range"
        };
    }
    container_registers_[id]
        = std::make_shared<runtime::simir::ContainerValue>(value);
    container_register_shared_[id] = 0U;
    container_object_aliases_[id] = invalid_container_object;
}

void LlvmProcessExecutor::write_container_register_storage(
    const runtime::simir::ContainerRegisterId id,
    std::shared_ptr<runtime::simir::ContainerValue> value)
{
    if (!value || id >= container_registers_.size()
        || !container_registers_[id]
        || container_registers_[id]->type != value->type) {
        throw compiler::LlvmJitError {
            "compiled process container-register storage is out of range"
        };
    }
    container_registers_[id] = std::move(value);
    container_register_shared_[id] = 1U;
    container_object_aliases_[id] = invalid_container_object;
}

const runtime::simir::ContainerValue&
LlvmProcessExecutor::container_register_value(
    const runtime::simir::ContainerRegisterId id,
    const runtime::simir::ProcessExecutionContext& context)
{
    auto& value = container_registers_.at(id);
    if (!value) {
        throw compiler::LlvmJitError {
            "compiled process container register has no storage"
        };
    }
    if (const auto alias = container_object_aliases_.at(id);
        alias != invalid_container_object) {
        if (container_register_shared_[id] != 0U) {
            value = std::make_shared<runtime::simir::ContainerValue>(*value);
            container_register_shared_[id] = 0U;
        }
        if (!context.copy_container_object(alias, *value)) {
            throw compiler::LlvmJitError {
                "compiled process could not materialize a container-object alias"
            };
        }
        container_object_aliases_[id] = invalid_container_object;
    }
    return *value;
}

runtime::simir::ContainerValue&
LlvmProcessExecutor::mutable_container_register_value(
    const runtime::simir::ContainerRegisterId id,
    const runtime::simir::ProcessExecutionContext& context)
{
    (void)container_register_value(id, context);
    auto& value = container_registers_[id];
    if (container_register_shared_[id] != 0U) {
        value = std::make_shared<runtime::simir::ContainerValue>(*value);
        container_register_shared_[id] = 0U;
    }
    return *value;
}

void LlvmProcessExecutor::alias_container_register(
    const runtime::simir::ContainerRegisterId destination,
    const runtime::simir::ContainerObjectId object,
    const runtime::simir::ProcessExecutionContext& context)
{
    if (!context.container_object_has_type(
            object, container_registers_.at(destination)->type)) {
        throw compiler::LlvmJitError {
            "compiled process container-object type mismatch"
        };
    }
    if (container_object_aliases_.at(destination) == invalid_container_object) {
        active_container_object_aliases_.push_back(destination);
    }
    container_object_aliases_.at(destination) = object;
}

void LlvmProcessExecutor::materialize_container_object_aliases(
    const runtime::simir::ProcessExecutionContext& context)
{
    for (const auto id : active_container_object_aliases_) {
        if (container_object_aliases_[id] != invalid_container_object) {
            (void)mutable_container_register_value(id, context);
        }
    }
    active_container_object_aliases_.clear();
}

void LlvmProcessExecutor::discard_container_object_aliases() noexcept
{
    for (const auto id : active_container_object_aliases_) {
        container_object_aliases_[id] = invalid_container_object;
    }
    active_container_object_aliases_.clear();
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
        || process != state.generated_process
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

std::uint32_t LlvmProcessExecutor::container_read_packed(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t container,
    const std::uint32_t flags,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    std::uint64_t* result_aval,
    std::uint64_t* result_bval,
    const std::uint32_t word_count) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    auto& profile = container_callback_profile();
    if (profile.enabled) {
        ++profile.read_words;
        profile.read_packed_elements += word_count > 1U ? 1U : 0U;
    }
    if (state.failure) {
        return 1;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || result_aval == nullptr
            || result_bval == nullptr) {
            throw compiler::LlvmJitError {
                "invalid generated packed-container read callback"
            };
        }
        const auto fused_object_distance = flags >> 8U;
        if (fused_object_distance != 0U) {
            if (instruction < fused_object_distance) {
                throw compiler::LlvmJitError {
                    "fused container-object read has no source operation"
                };
            }
            const auto* read_object
                = fsim::runtime::simir::operation_get_if<
                    runtime::simir::ReadContainerObject>(
                    &callback_operation(
                        state, process,
                        instruction - fused_object_distance));
            if (read_object == nullptr
                || read_object->destination != container) {
                throw compiler::LlvmJitError {
                    "fused container-object read metadata is inconsistent"
                };
            }
            state.executor->alias_container_register(
                container, read_object->object, *state.context);
        }
        const auto& source = *state.executor->container_registers_.at(container);
        const auto expected_words = static_cast<std::uint32_t>(
            (source.type.element_width + 63U) / 64U);
        if (word_count != expected_words || word_count == 0U) {
            throw compiler::LlvmJitError {
                "generated packed-container read word count mismatch"
            };
        }
        const bool linear_index = (flags & 1U) != 0;
        const bool signed_index = (flags & 2U) != 0;
        const auto publish = [&](const PackedLogic4& value) {
            if (value.width() != source.type.element_width
                || value.aval_words().size() != word_count
                || value.bval_words().size() != word_count) {
                throw compiler::LlvmJitError {
                    "generated packed-container read width mismatch"
                };
            }
            std::ranges::copy(value.aval_words(), result_aval);
            std::ranges::copy(value.bval_words(), result_bval);
        };
        const auto publish_default = [&] {
            publish(PackedLogic4(
                source.type.element_width,
                source.type.two_state
                    ? runtime::Logic4::zero
                    : runtime::Logic4::x));
        };
        if (index_bval != 0) {
            if (source.type.fixed) {
                publish_default();
                return 0;
            }
            throw runtime::simir::InterpreterError {
                process, instruction,
                "container index must be a known integral value"
            };
        }
        std::size_t selected { };
        if (source.type.fixed && !linear_index) {
            const auto sought = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(index_aval));
            const auto low = std::min(
                source.type.index_left, source.type.index_right);
            const auto high = std::max(
                source.type.index_left, source.type.index_right);
            if (sought < low || sought > high) {
                publish_default();
                return 0;
            }
            selected = static_cast<std::size_t>(
                source.type.index_left >= source.type.index_right
                    ? static_cast<std::int64_t>(source.type.index_left) - sought
                    : static_cast<std::int64_t>(sought) - source.type.index_left);
        } else {
            if (signed_index
                && static_cast<std::int32_t>(
                       static_cast<std::uint32_t>(index_aval)) < 0) {
                if (source.type.fixed) {
                    publish_default();
                    return 0;
                }
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index cannot be negative"
                };
            }
            selected = static_cast<std::size_t>(index_aval);
        }
        if (const auto alias
                = state.executor->container_object_aliases_.at(container);
            alias != invalid_container_object) {
            PackedLogic4 value;
            if (state.context->read_container_object_element(
                    alias, selected, value)) {
                if (value.width() != source.type.element_width) {
                    throw compiler::LlvmJitError {
                        "compiled process direct container-element width mismatch"
                    };
                }
                publish(value);
                return 0;
            }
            const auto& materialized
                = state.executor->container_register_value(
                    container, *state.context);
            if (selected >= materialized.elements.size()) {
                if (source.type.fixed) {
                    publish_default();
                    return 0;
                }
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index is out of range"
                };
            }
            publish(materialized.elements[selected]);
            return 0;
        }
        if (selected >= source.elements.size()) {
            if (source.type.fixed) {
                publish_default();
                return 0;
            }
            throw runtime::simir::InterpreterError {
                process, instruction,
                "container index is out of range"
            };
        }
        publish(source.elements[selected]);
        return 0;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::container_read_word(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t container,
    const std::uint32_t flags,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    std::uint64_t* result_aval,
    std::uint64_t* result_bval) noexcept
{
    return container_read_packed(
        context, process, instruction, container, flags,
        index_aval, index_bval, result_aval, result_bval, 1U);
}

std::uint32_t LlvmProcessExecutor::container_write_packed(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t container,
    const std::uint32_t flags,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint64_t* value_aval,
    const std::uint64_t* value_bval,
    const std::uint32_t word_count) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    auto& profile = container_callback_profile();
    if (profile.enabled) {
        ++profile.write_words;
        profile.write_packed_elements += word_count > 1U ? 1U : 0U;
    }
    if (state.failure) {
        return 1;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || value_aval == nullptr || value_bval == nullptr) {
            throw compiler::LlvmJitError {
                "invalid generated packed-container write callback"
            };
        }
        auto& target = state.executor->mutable_container_register_value(
            container, *state.context);
        const auto expected_words = static_cast<std::uint32_t>(
            (target.type.element_width + 63U) / 64U);
        if (word_count != expected_words || word_count == 0U) {
            throw compiler::LlvmJitError {
                "generated packed-container write word count mismatch"
            };
        }
        const bool linear_index = (flags & 1U) != 0;
        const bool signed_index = (flags & 2U) != 0;
        if (index_bval != 0) {
            throw runtime::simir::InterpreterError {
                process, instruction,
                "container index must be a known integral value"
            };
        }
        std::size_t selected { };
        if (target.type.fixed && !linear_index) {
            const auto sought = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(index_aval));
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
            selected = static_cast<std::size_t>(
                target.type.index_left >= target.type.index_right
                    ? static_cast<std::int64_t>(target.type.index_left) - sought
                    : static_cast<std::int64_t>(sought) - target.type.index_left);
        } else {
            if (signed_index
                && static_cast<std::int32_t>(
                       static_cast<std::uint32_t>(index_aval)) < 0) {
                throw runtime::simir::InterpreterError {
                    process, instruction,
                    "container index cannot be negative"
                };
            }
            selected = static_cast<std::size_t>(index_aval);
        }
        if (selected >= target.elements.size()) {
            throw runtime::simir::InterpreterError {
                process, instruction,
                "container index is out of range"
            };
        }
        const auto aval = std::span<const std::uint64_t> {
            value_aval, word_count
        };
        const auto bval = std::span<const std::uint64_t> {
            value_bval, word_count
        };
        if (target.type.two_state
            && std::ranges::any_of(
                bval, [](const auto word) { return word != 0U; })) {
            throw runtime::simir::InterpreterError {
                process, instruction,
                "container element write type mismatch"
            };
        }
        target.elements[selected] = PackedLogic4::from_word_planes(
            target.type.element_width, aval, bval);
        return 0;
    } catch (...) {
        capture_file_failure(state, process, instruction);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::container_write_word(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t container,
    const std::uint32_t flags,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint64_t value_aval,
    const std::uint64_t value_bval) noexcept
{
    return container_write_packed(
        context, process, instruction, container, flags,
        index_aval, index_bval, &value_aval, &value_bval, 1U);
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
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_slice_word(
            state, actual_signal, offset, width, aval, bval);
        invalidate_signal_read_cache(state);
        state.context->force_signal_slice(
            actual_signal,
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
        const auto actual_signal = mapped_signal(state, signal);
        invalidate_signal_read_cache(state);
        state.context->force_signal_slice(
            actual_signal,
            checked_logic9_slice(
                state, actual_signal, offset, width, value),
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
        invalidate_signal_read_cache(state);
        state.context->release_signal_slice(
            mapped_signal(state, signal), offset, width);
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
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_slice_word(
            state, actual_signal, offset, width, aval, bval);
        invalidate_signal_read_cache(state);
        state.context->force_driver_signal_slice(
            actual_signal,
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
        const auto actual_signal = mapped_signal(state, signal);
        invalidate_signal_read_cache(state);
        state.context->force_driver_signal_slice(
            actual_signal,
            checked_logic9_slice(
                state, actual_signal, offset, width, value),
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
        invalidate_signal_read_cache(state);
        state.context->release_driver_signal_slice(
            mapped_signal(state, signal), offset, width);
    } catch (...) {
        capture_failure(state);
    }
}

compiler::JitOptimizationLevel jit_optimization(
    const project::Optimization optimization) noexcept
{
    switch (optimization) {
    case project::Optimization::o0:
        return compiler::JitOptimizationLevel::o0;
    case project::Optimization::o1:
        return compiler::JitOptimizationLevel::o1;
    case project::Optimization::o2:
    case project::Optimization::o3:
        return compiler::JitOptimizationLevel::o2;
    }
    return compiler::JitOptimizationLevel::o2;
}

#endif

} // namespace fsim::app::application_detail
