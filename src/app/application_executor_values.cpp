// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <ranges>
#include <string>
#include <vector>

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

std::uint32_t LlvmProcessExecutor::load_string(
    void* context,
    const std::uint32_t destination,
    const char* bytes,
    const std::uint64_t byte_count) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (state.executor == nullptr
            || byte_count > runtime::simir::maximum_string_bytes
            || (byte_count != 0 && bytes == nullptr)) {
            throw std::logic_error { "invalid generated string-load callback" };
        }
        (void)runtime::systemverilog_string_length(
            std::string_view { bytes, static_cast<std::size_t>(byte_count) });
        state.executor->write_string_register(
            destination,
            std::string_view { bytes, static_cast<std::size_t>(byte_count) });
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::copy_string(
    void* context,
    const std::uint32_t destination,
    const std::uint32_t source) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        state.executor->write_string_register(
            destination,
            state.executor->read_string_register(source));
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::read_string_object(
    void* context,
    const std::uint32_t destination,
    const std::uint32_t object) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        state.executor->write_string_register(
            destination, state.context->read_string_object(object));
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::write_string_object(
    void* context,
    const std::uint32_t object,
    const std::uint32_t source) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        state.context->write_string_object(
            object, state.executor->read_string_register(source));
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::concatenate_strings(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t destination,
    const std::uint32_t* operands,
    const std::uint32_t operand_count) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (state.process == nullptr || process != state.generated_process
            || (operand_count != 0 && operands == nullptr)) {
            throw std::logic_error {
                "invalid generated string-concatenation callback"
            };
        }
        std::string result;
        for (std::uint32_t index = 0; index < operand_count; ++index) {
            const auto value = state.executor->read_string_register(operands[index]);
            if (value.size()
                > runtime::simir::maximum_string_bytes - result.size()) {
                throw runtime::simir::InterpreterError(
                    state.process->id,
                    instruction,
                    "string concatenation exceeds 4096-byte limit");
            }
            result += value;
        }
        state.executor->write_string_register(destination, result);
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::compare_strings(
    void* context,
    const std::uint32_t lhs,
    const std::uint32_t rhs,
    const std::uint32_t not_equal,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (result == nullptr || not_equal > 1) {
            throw std::logic_error { "invalid generated string-compare callback" };
        }
        const bool equal = runtime::systemverilog_string_compare(
                               state.executor->read_string_register(lhs),
                               state.executor->read_string_register(rhs))
            == 0;
        *result = equal != (not_equal != 0) ? 1U : 0U;
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::string_length(
    void* context,
    const std::uint32_t source,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (result == nullptr) {
            throw std::logic_error { "invalid generated string-length callback" };
        }
        *result = static_cast<std::uint32_t>(
            runtime::systemverilog_string_length(
                state.executor->read_string_register(source)));
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::string_index(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t source,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint32_t signed_index,
    std::uint32_t* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto& value = state.executor->string_registers_.at(source);
        if (state.process == nullptr || process != state.generated_process
            || result == nullptr || signed_index > 1 || index_bval != 0) {
            throw runtime::simir::InterpreterError(
                state.process->id,
                instruction,
                "string index contains X or Z");
        }
        const auto raw = static_cast<std::uint32_t>(index_aval);
        const auto index = signed_index != 0
            ? static_cast<std::int64_t>(static_cast<std::int32_t>(raw))
            : static_cast<std::int64_t>(raw);
        const auto length = runtime::systemverilog_string_length(value);
        if (index < 0
            || static_cast<std::uint64_t>(index) >= length) {
            throw runtime::simir::InterpreterError(
                state.process->id,
                instruction,
                "string index is outside the current code-point range");
        }
        *result = runtime::systemverilog_string_at(
            value, static_cast<std::size_t>(index));
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::string_replace_code_point(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t target,
    const std::uint64_t index_aval,
    const std::uint64_t index_bval,
    const std::uint32_t signed_index,
    const std::uint64_t source_aval,
    const std::uint64_t source_bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        auto& value = state.executor->string_registers_.at(target);
        if (state.process == nullptr || process != state.generated_process
            || signed_index > 1 || index_bval != 0) {
            throw runtime::simir::InterpreterError(
                state.process->id,
                instruction,
                "string index contains X or Z");
        }
        const auto raw = static_cast<std::uint32_t>(index_aval);
        const auto selected = signed_index != 0
            ? static_cast<std::int64_t>(static_cast<std::int32_t>(raw))
            : static_cast<std::int64_t>(raw);
        const auto length = runtime::systemverilog_string_length(value);
        if (selected < 0
            || static_cast<std::uint64_t>(selected) >= length) {
            throw runtime::simir::InterpreterError(
                state.process->id,
                instruction,
                "string index is outside the current code-point range");
        }
        if (source_bval != 0) {
            throw runtime::simir::InterpreterError(
                state.process->id,
                instruction,
                "string replacement code point contains X or Z");
        }
        runtime::systemverilog_string_replace(
            value,
            static_cast<std::size_t>(selected),
            static_cast<std::uint32_t>(source_aval),
            runtime::simir::maximum_string_bytes);
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::write_string_output(
    void* context,
    const std::uint32_t process,
    const std::uint32_t source,
    const char* prefix,
    const std::uint64_t prefix_size,
    const char* suffix,
    const std::uint64_t suffix_size,
    const std::uint32_t newline,
    const std::uint32_t postponed) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        if (state.process == nullptr || process != state.generated_process
            || (prefix_size != 0 && prefix == nullptr)
            || (suffix_size != 0 && suffix == nullptr)
            || newline > 1 || postponed > 1) {
            throw std::logic_error { "invalid generated string-output callback" };
        }
        std::string text { prefix, static_cast<std::size_t>(prefix_size) };
        text += state.executor->read_string_register(source);
        text.append(suffix, static_cast<std::size_t>(suffix_size));
        if (postponed != 0) {
            state.context->postpone_display(text, newline != 0);
        } else {
            state.context->display(text, newline != 0);
        }
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint64_t LlvmProcessExecutor::read_simulation_time(
    void* context) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || state.context == nullptr) {
        return 0;
    }
    return state.context->current_time();
}

std::uint32_t LlvmProcessExecutor::signal_active(
    void* context,
    const std::uint32_t signal) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || state.context == nullptr) {
        return 0;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (actual_signal >= state.signal_widths.size()) {
            return 0;
        }
        return state.context->signal_active(actual_signal) ? 1U : 0U;
    } catch (...) {
        capture_failure(state);
        return 0;
    }
}

void LlvmProcessExecutor::write_output(
    void* context,
    const std::uint32_t,
    const char* text,
    const std::uint64_t text_size,
    const std::uint32_t newline) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        if (state.context == nullptr
            || (text == nullptr && text_size != 0)
            || newline > 1
            || text_size
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
            throw std::logic_error(
                "invalid generated language-output callback");
        }
        state.context->display(
            std::string_view {
                text == nullptr ? "" : text,
                static_cast<std::size_t>(text_size) },
            newline != 0);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::schedule_output(
    void* context,
    const std::uint32_t,
    const char* text,
    const std::uint64_t text_size,
    const std::uint32_t newline) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        if (state.context == nullptr
            || (text == nullptr && text_size != 0)
            || newline > 1
            || text_size
                > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
            throw std::logic_error {
                "invalid generated postponed-output callback"
            };
        }
        state.context->postpone_display(
            std::string_view {
                text == nullptr ? "" : text,
                static_cast<std::size_t>(text_size) },
            newline != 0);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_formatted(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        if (state.context == nullptr
            || state.process == nullptr
            || state.generated_process != process
            || instruction >= state.process->operations.size()
            || width == 0
            || width > 64) {
            throw std::logic_error {
                "invalid generated formatted-output callback"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::FormatDisplay>(
            &state.process->operations[instruction]);
        if (operation == nullptr) {
            throw std::logic_error {
                "generated formatted-output callback references a "
                "different operation"
            };
        }
        const auto value = PackedLogic4::from_aval_bval(
            width, aval, bval);
        state.context->display_formatted(
            operation->prefix,
            operation->suffix,
            operation->format,
            value,
            operation->newline,
            operation->postponed,
            operation->signed_decimal,
            operation->suppress_leading_zero,
            operation->minimum_width,
            operation->left_justify,
            operation->zero_pad,
            operation->scalar_kind);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_formatted_logic9(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        if (state.context == nullptr
            || state.process == nullptr
            || state.generated_process != process
            || instruction >= state.process->operations.size()
            || width == 0 || width > 64
            || value == nullptr) {
            throw std::logic_error {
                "invalid generated Logic9 formatted-output callback"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::FormatDisplay>(
            &state.process->operations[instruction]);
        if (operation == nullptr) {
            throw std::logic_error {
                "generated Logic9 formatted-output callback references a "
                "different operation"
            };
        }
        const auto packed = PackedLogic4::from_logic9_word(
            { width,
                { value->planes[0],
                    value->planes[1],
                    value->planes[2],
                    value->planes[3] } });
        state.context->display_formatted(
            operation->prefix,
            operation->suffix,
            operation->format,
            packed,
            operation->newline,
            operation->postponed,
            operation->signed_decimal,
            operation->suppress_leading_zero,
            operation->minimum_width,
            operation->left_justify,
            operation->zero_pad,
            operation->scalar_kind);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_time(
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
                "invalid generated time-output callback"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::TimeDisplay>(
            &state.process->operations[instruction]);
        if (operation == nullptr) {
            throw std::logic_error {
                "generated time-output callback references a "
                "different operation"
            };
        }
        state.context->display_time(
            operation->prefix,
            operation->suffix,
            operation->newline,
            operation->postponed,
            operation->minimum_width,
            operation->left_justify,
            operation->zero_pad,
            operation->use_timeformat_width);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::install_monitor(
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
                "invalid generated monitor-install callback"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::MonitorInstall>(
            &state.process->operations[instruction]);
        if (operation == nullptr) {
            throw std::logic_error {
                "generated monitor-install callback references a "
                "different operation"
            };
        }
        state.context->install_monitor(*operation);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::control_monitor(
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
                "invalid generated monitor-control callback"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::MonitorControl>(
            &state.process->operations[instruction]);
        if (operation == nullptr) {
            throw std::logic_error {
                "generated monitor-control callback references a "
                "different operation"
            };
        }
        state.context->set_monitor_enabled(operation->enabled);
    } catch (...) {
        capture_failure(state);
    }
}

std::uint64_t LlvmProcessExecutor::random_value(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint64_t maximum_aval,
    const std::uint64_t maximum_bval,
    const std::uint64_t minimum_aval,
    const std::uint64_t minimum_bval,
    std::uint64_t* result_bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || result_bval == nullptr) {
        return 0;
    }
    try {
        if (state.context == nullptr
            || state.process == nullptr
            || state.generated_process != process
            || instruction >= state.process->operations.size()) {
            throw std::logic_error {
                "invalid generated random-value callback"
            };
        }
        const auto* operation = fsim::runtime::simir::operation_get_if<runtime::simir::RandomValue>(
            &state.process->operations[instruction]);
        if (operation == nullptr) {
            throw std::logic_error {
                "generated random-value callback references a "
                "different operation"
            };
        }
        const auto maximum = operation->maximum
            ? std::optional<PackedLogic4> {
                  PackedLogic4::from_aval_bval(
                      32, maximum_aval, maximum_bval)
              }
            : std::nullopt;
        const auto minimum = operation->minimum
            ? std::optional<PackedLogic4> {
                  PackedLogic4::from_aval_bval(
                      32, minimum_aval, minimum_bval)
              }
            : std::nullopt;
        const auto result = state.context->random_value(
            operation->kind, maximum, minimum);
        const auto encoded = result.low_word();
        *result_bval = encoded.bval;
        return encoded.aval;
    } catch (...) {
        capture_failure(state);
        *result_bval = std::numeric_limits<std::uint64_t>::max();
        return std::numeric_limits<std::uint64_t>::max();
    }
}

#endif

} // namespace fsim::app::application_detail
