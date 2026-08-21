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

std::uint64_t LlvmProcessExecutor::read_signal(
    void* context,
    const std::uint32_t signal,
    std::uint64_t* bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        if (bval != nullptr) {
            *bval = 0;
        }
        return 0;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (bval == nullptr || state.context == nullptr
            || actual_signal >= state.signal_widths.size()) {
            throw std::logic_error("invalid generated read-signal callback");
        }
        const auto cache_index = static_cast<std::size_t>(actual_signal)
            % CallbackState::signal_read_cache_size;
        auto value = runtime::Logic4Word { };
        if (state.signal_read_cache_ids[cache_index] == actual_signal) {
            value = state.signal_read_cache_words[cache_index];
        } else {
            value = state.context->read_signal_word(actual_signal);
            state.signal_read_cache_ids[cache_index] = actual_signal;
            state.signal_read_cache_words[cache_index] = value;
        }
        const auto expected_width = state.signal_widths[actual_signal];
        if (value.width != expected_width
            || value.width == 0
            || value.width > 64) {
            throw std::logic_error(
                "generated read-signal callback observed an invalid width");
        }
        *bval = value.bval;
        return value.aval;
    } catch (...) {
        capture_failure(state);
        if (bval != nullptr) {
            *bval = 0;
        }
        return 0;
    }
}

std::uint32_t LlvmProcessExecutor::read_signal_packed(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t width,
    std::uint64_t* const aval,
    std::uint64_t* const bval,
    std::uint64_t* const logic9_plane2,
    std::uint64_t* const logic9_plane3) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (state.context == nullptr || actual_signal >= state.signal_widths.size()
            || state.signal_widths[actual_signal] != width || width <= 64U
            || aval == nullptr || bval == nullptr) {
            throw std::logic_error(
                "invalid generated arbitrary-width signal-read callback");
        }
        const auto word_count = static_cast<std::size_t>((width + 63U) / 64U);
        const auto signal_logic9 = !state.signal_value_kinds.empty()
            && state.signal_value_kinds[actual_signal]
                == runtime::simir::ValueKind::logic9;
        if ((logic9_plane2 == nullptr) != (logic9_plane3 == nullptr)) {
            throw std::logic_error(
                "generated arbitrary-width signal-read logic9 planes are incomplete");
        }
        const auto destination_logic9 = logic9_plane2 != nullptr;
        if (signal_logic9 != destination_logic9) {
            auto value = state.context->read_signal(actual_signal);
            value = destination_logic9
                ? value.promoted_to_logic9()
                : runtime::collapse_to_logic4(value);
            const auto plane0 = destination_logic9
                ? value.logic9_plane_words(0U) : value.aval_words();
            const auto plane1 = destination_logic9
                ? value.logic9_plane_words(1U) : value.bval_words();
            std::ranges::copy(plane0, aval);
            std::ranges::copy(plane1, bval);
            if (destination_logic9) {
                std::ranges::copy(
                    value.logic9_plane_words(2U), logic9_plane2);
                std::ranges::copy(
                    value.logic9_plane_words(3U), logic9_plane3);
            }
            return 0;
        }
        state.context->read_signal_planes(
            actual_signal,
            { aval, word_count },
            { bval, word_count },
            signal_logic9
                ? std::span<std::uint64_t> { logic9_plane2, word_count }
                : std::span<std::uint64_t> { },
            signal_logic9
                ? std::span<std::uint64_t> { logic9_plane3, word_count }
                : std::span<std::uint64_t> { });
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::read_signal_dynamic_part(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t source_width,
    const std::uint64_t base_aval,
    const std::uint64_t base_bval,
    const std::int64_t left,
    const std::int64_t right,
    const std::uint32_t base_offset,
    const std::uint32_t width,
    const std::uint32_t flags,
    fsim_jit_logic9_word_v1* const result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (state.context == nullptr || result == nullptr || width == 0U
            || width > 64U || actual_signal >= state.signal_widths.size()
            || state.signal_widths[actual_signal] != source_width
            || source_width <= 64U || (flags & ~UINT32_C(7)) != 0U) {
            throw std::logic_error(
                "invalid generated dynamic-part signal-read callback");
        }
        const auto selected = runtime::simir::dynamic_part_select_value(
            state.context->read_signal(actual_signal),
            PackedLogic4::from_aval_bval(32, base_aval, base_bval),
            left,
            right,
            base_offset,
            width,
            (flags & UINT32_C(1)) != 0U,
            (flags & UINT32_C(2)) != 0U,
            (flags & UINT32_C(4)) != 0U);
        if (selected.is_logic9()) {
            const auto word = selected.logic9_low_word();
            std::ranges::copy(word.planes, result->planes);
        } else {
            const auto word = selected.unchecked_low_word();
            result->planes[0] = word.aval;
            result->planes[1] = word.bval;
            result->planes[2] = 0U;
            result->planes[3] = 0U;
        }
        return 0;
    } catch (...) {
        capture_failure(state);
        clear_logic9_word(result);
        return 1;
    }
}

std::uint32_t LlvmProcessExecutor::write_signal_packed(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint32_t mode,
    const std::uint64_t delay,
    const std::uint64_t* const aval,
    const std::uint64_t* const bval,
    const std::uint64_t* const logic9_plane2,
    const std::uint64_t* const logic9_plane3) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (state.context == nullptr || actual_signal >= state.signal_widths.size()
            || width == 0U || aval == nullptr || bval == nullptr
            || offset > state.signal_widths[actual_signal]
            || width > state.signal_widths[actual_signal] - offset) {
            throw std::logic_error(
                "invalid generated arbitrary-width signal-write callback");
        }
        const auto word_count = static_cast<std::size_t>((width + 63U) / 64U);
        const auto destination_logic9 = !state.signal_value_kinds.empty()
            && state.signal_value_kinds[actual_signal]
                == runtime::simir::ValueKind::logic9;
        if ((logic9_plane2 == nullptr) != (logic9_plane3 == nullptr)) {
            throw std::logic_error(
                "generated arbitrary-width signal-write logic9 planes are incomplete");
        }
        const auto source_logic9 = logic9_plane2 != nullptr;
        if (!source_logic9 && !destination_logic9
            && state.supports_direct_word_updates
            && (mode == FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE
                || mode == FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE)) {
            if (mode == FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE
                && (offset != 0U
                    || width != state.signal_widths[actual_signal])) {
                throw std::logic_error(
                    "packed update write does not cover the complete signal");
            }
            auto& pending = state.executor->pending_update_words_;
            pending.reserve(pending.size() + word_count);
            for (std::size_t word = 0; word < word_count; ++word) {
                const auto bit = word * 64U;
                const auto chunk_width = static_cast<std::size_t>(
                    std::min<std::uint64_t>(64U, width - bit));
                pending.push_back(runtime::simir::ProcessUpdateWord {
                    actual_signal,
                    runtime::Logic4Word {
                        chunk_width, aval[word], bval[word] },
                    static_cast<std::uint32_t>(offset + bit),
                    true });
            }
            return 0;
        }
        auto value = source_logic9
            ? PackedLogic4::from_logic9_word_planes(
                  width,
                  { aval, word_count },
                  { bval, word_count },
                  { logic9_plane2, word_count },
                  { logic9_plane3, word_count })
            : PackedLogic4::from_word_planes(
                  width, { aval, word_count }, { bval, word_count });
        if (mode == FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE
            || mode == FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE) {
            state.executor->flush_update_words(*state.context);
        }
        switch (mode) {
        case FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING:
            if (offset != 0U || width != state.signal_widths[actual_signal]) {
                throw std::logic_error(
                    "packed blocking write does not cover the complete signal");
            }
            invalidate_signal_read_cache(state);
            state.context->write_blocking(actual_signal, std::move(value));
            break;
        case FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE:
            if (offset != 0U || width != state.signal_widths[actual_signal]) {
                throw std::logic_error(
                    "packed update write does not cover the complete signal");
            }
            state.context->write_update(actual_signal, std::move(value));
            break;
        case FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER:
            if (offset != 0U || width != state.signal_widths[actual_signal]) {
                throw std::logic_error(
                    "packed delayed write does not cover the complete signal");
            }
            state.context->write_after(actual_signal, std::move(value), delay);
            break;
        case FSIM_JIT_PACKED_SIGNAL_WRITE_BLOCKING_SLICE:
            invalidate_signal_read_cache(state);
            state.context->write_blocking_slice(
                actual_signal, std::move(value), offset);
            break;
        case FSIM_JIT_PACKED_SIGNAL_WRITE_UPDATE_SLICE:
            state.context->write_update_slice(
                actual_signal, std::move(value), offset);
            break;
        case FSIM_JIT_PACKED_SIGNAL_WRITE_AFTER_SLICE:
            state.context->write_after_slice(
                actual_signal, std::move(value), offset, delay);
            break;
        default:
            throw std::logic_error(
                "generated arbitrary-width signal write has an invalid mode");
        }
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

void LlvmProcessExecutor::read_signal_logic9(
    void* context,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    clear_logic9_word(result);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        require_logic9_signal(state, actual_signal, result);
        const auto& direct = state.direct_signal_logic9_planes;
        static const bool direct_planes_enabled
            = std::getenv("FSIM_DISABLE_DIRECT_LOGIC9_READ_PLANES") == nullptr;
        if (direct_planes_enabled
            && actual_signal < direct[0].size()
            && actual_signal < direct[1].size()
            && actual_signal < direct[2].size()
            && actual_signal < direct[3].size()) {
            result->planes[0] = direct[0][actual_signal];
            result->planes[1] = direct[1][actual_signal];
            result->planes[2] = direct[2][actual_signal];
            result->planes[3] = direct[3][actual_signal];
            return;
        }
        const auto value = state.context->read_signal_logic9_word(actual_signal);
        if (value.width != state.signal_widths[actual_signal]) {
            throw std::logic_error(
                "generated Logic9 read observed an invalid width");
        }
        result->planes[0] = value.planes[0];
        result->planes[1] = value.planes[1];
        result->planes[2] = value.planes[2];
        result->planes[3] = value.planes[3];
    } catch (...) {
        capture_failure(state);
        clear_logic9_word(result);
    }
}

void LlvmProcessExecutor::read_signal_logic9_identity(
    void* context,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || result == nullptr) {
        clear_logic9_word(result);
        return;
    }
    static const bool direct_planes_enabled
        = std::getenv("FSIM_DISABLE_DIRECT_LOGIC9_READ_PLANES") == nullptr;
    if (!direct_planes_enabled) {
        read_signal_logic9(context, signal, result);
        return;
    }
    const auto& direct = state.direct_signal_logic9_planes;
    if (signal >= direct[0].size()
        || signal >= direct[1].size()
        || signal >= direct[2].size()
        || signal >= direct[3].size()) {
        read_signal_logic9(context, signal, result);
        return;
    }
    result->planes[0] = direct[0][signal];
    result->planes[1] = direct[1][signal];
    result->planes[2] = direct[2][signal];
    result->planes[3] = direct[3][signal];
}

void LlvmProcessExecutor::write_signal(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_write_word(
            state, actual_signal, aval, bval);
        invalidate_signal_read_cache(state);
        state.context->write_blocking_word(
            actual_signal, value);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_signal_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        invalidate_signal_read_cache(state);
        state.context->write_blocking(
            actual_signal,
            checked_logic9_value(state, actual_signal, value));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_update(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_write_word(
            state, actual_signal, aval, bval);
        state.executor->pending_update_words_.push_back(
            { actual_signal, value, 0U, false });
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_update_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        require_logic9_signal(state, actual_signal, value);
        if (!state.executor->buffer_logic9_update(
                actual_signal, 0U,
                state.signal_widths[actual_signal], *value)) {
            state.executor->flush_update_words(*state.context);
            state.context->write_update(
                actual_signal,
                checked_logic9_value(state, actual_signal, value));
        }
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_after(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_write_word(
            state, actual_signal, aval, bval);
        state.context->write_after_word(
            actual_signal, value, delay);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_after_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        state.context->write_after(
            actual_signal,
            checked_logic9_value(state, actual_signal, value),
            delay);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_inertial(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_write_word(
            state, actual_signal, aval, bval);
        state.context->write_inertial_word(
            actual_signal,
            value,
            { rise_delay, fall_delay, turnoff_delay });
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_inertial_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        state.context->write_inertial(
            actual_signal,
            checked_logic9_value(state, actual_signal, value),
            { rise_delay, fall_delay, turnoff_delay });
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected(
    void* context,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_write_word(
            state, actual_signal, aval, bval);
        state.context->write_projected_word(
            actual_signal,
            value,
            delay,
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_logic9(
    void* context,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (value != nullptr && delay == 0U && rejection == 0U
            && projected_delay_mode(mode)
                == runtime::simir::ProjectedDelayMode::inertial
            && actual_signal < state.signal_widths.size()
            && state.executor->buffer_logic9_update(
                actual_signal, 0U, state.signal_widths[actual_signal],
                *value)) {
            return;
        }
        state.context->write_projected(
            actual_signal,
            checked_logic9_value(state, actual_signal, value),
            delay,
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_signal_slice(
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
        state.context->write_blocking_slice_word(
            actual_signal, value, offset);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_signal_slice_logic9(
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
        state.context->write_blocking_slice(
            actual_signal,
            checked_logic9_slice(
                state, actual_signal, offset, width, value),
            offset);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_update_slice(
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
        state.executor->pending_update_words_.push_back(
            { actual_signal, value, offset, true });
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_update_slice_logic9(
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
        require_logic9_signal(state, actual_signal, value);
        if (!state.executor->buffer_logic9_update(
                actual_signal, offset, width, *value)) {
            state.executor->flush_update_words(*state.context);
            state.context->write_update_slice(
                actual_signal,
                checked_logic9_slice(
                    state, actual_signal, offset, width, value),
                offset);
        }
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_after_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_slice_word(
            state, actual_signal, offset, width, aval, bval);
        state.context->write_after_slice_word(
            actual_signal, value, offset, delay);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_after_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        state.context->write_after_slice(
            actual_signal,
            checked_logic9_slice(
                state, actual_signal, offset, width, value),
            offset,
            delay);
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_inertial_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_slice_word(
            state, actual_signal, offset, width, aval, bval);
        state.context->write_inertial_slice_word(
            actual_signal,
            value,
            offset,
            { rise_delay, fall_delay, turnoff_delay });
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_inertial_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t rise_delay,
    const std::uint64_t fall_delay,
    const std::uint64_t turnoff_delay) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        state.context->write_inertial_slice(
            actual_signal,
            checked_logic9_slice(
                state, actual_signal, offset, width, value),
            offset,
            { rise_delay, fall_delay, turnoff_delay });
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        const auto value = checked_slice_word(
            state, actual_signal, offset, width, aval, bval);
        state.context->write_projected_slice_word(
            actual_signal,
            value,
            offset,
            delay,
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value,
    const std::uint64_t delay,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (value != nullptr && delay == 0U && rejection == 0U
            && projected_delay_mode(mode)
                == runtime::simir::ProjectedDelayMode::inertial
            && state.executor->buffer_logic9_update(
                actual_signal, offset, width, *value)) {
            return;
        }
        state.context->write_projected_slice(
            actual_signal,
            checked_logic9_slice(
                state, actual_signal, offset, width, value),
            offset,
            delay,
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_waveform(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t width,
    const fsim_jit_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (elements == nullptr || count == 0
            || actual_signal >= state.signal_widths.size()
            || width != state.signal_widths[actual_signal]) {
            throw std::logic_error(
                "invalid generated projected-waveform callback");
        }
        std::vector<runtime::simir::ProjectedWaveformValue> values;
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto word = checked_write_word(
                state,
                actual_signal,
                elements[index].aval,
                elements[index].bval);
            values.push_back({ PackedLogic4::from_aval_bval(
                                   word.width, word.aval, word.bval),
                elements[index].delay });
        }
        state.context->write_projected_waveform(
            actual_signal,
            std::move(values),
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_waveform_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t width,
    const fsim_jit_logic9_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (elements == nullptr || count == 0
            || actual_signal >= state.signal_widths.size()
            || width != state.signal_widths[actual_signal]) {
            throw std::logic_error(
                "invalid generated Logic9 projected-waveform callback");
        }
        std::vector<runtime::simir::ProjectedWaveformValue> values;
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            values.push_back({ checked_logic9_value(
                                   state, actual_signal,
                                   &elements[index].value),
                elements[index].delay });
        }
        state.context->write_projected_waveform(
            actual_signal,
            std::move(values),
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_waveform_slice(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (elements == nullptr || count == 0) {
            throw std::logic_error(
                "invalid generated projected-slice-waveform callback");
        }
        std::vector<runtime::simir::ProjectedWaveformValue> values;
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto word = checked_slice_word(
                state,
                actual_signal,
                offset,
                width,
                elements[index].aval,
                elements[index].bval);
            values.push_back({ PackedLogic4::from_aval_bval(
                                   word.width, word.aval, word.bval),
                elements[index].delay });
        }
        state.context->write_projected_waveform_slice(
            actual_signal,
            std::move(values),
            offset,
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

void LlvmProcessExecutor::write_projected_waveform_slice_logic9(
    void* context,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_projected_element_v1* elements,
    const std::uint32_t count,
    const std::uint64_t rejection,
    const std::uint32_t mode) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (elements == nullptr || count == 0) {
            throw std::logic_error(
                "invalid generated Logic9 projected-slice-waveform "
                "callback");
        }
        std::vector<runtime::simir::ProjectedWaveformValue> values;
        values.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            values.push_back({ checked_logic9_slice(
                                   state,
                                   actual_signal,
                                   offset,
                                   width,
                                   &elements[index].value),
                elements[index].delay });
        }
        state.context->write_projected_waveform_slice(
            actual_signal,
            std::move(values),
            offset,
            rejection,
            projected_delay_mode(mode));
    } catch (...) {
        capture_failure(state);
    }
}

[[nodiscard]] runtime::simir::ProjectedDelayMode
LlvmProcessExecutor::projected_delay_mode(const std::uint32_t mode)
{
    if (mode == FSIM_JIT_PROJECTED_TRANSPORT) {
        return runtime::simir::ProjectedDelayMode::transport;
    }
    if (mode == FSIM_JIT_PROJECTED_INERTIAL) {
        return runtime::simir::ProjectedDelayMode::inertial;
    }
    throw compiler::LlvmJitError(
        "generated process requested an invalid projected delay mode");
}

[[nodiscard]] runtime::Logic4Word LlvmProcessExecutor::checked_write_word(
    const CallbackState& state,
    const std::uint32_t signal,
    const std::uint64_t aval,
    const std::uint64_t bval)
{
    if (state.context == nullptr
        || signal >= state.signal_widths.size()) {
        throw std::logic_error("invalid generated write-signal callback");
    }
    const auto width = state.signal_widths[signal];
    if (width == 0 || width > 64) {
        throw std::logic_error(
            "generated write-signal callback received an invalid width");
    }
    return { width, aval, bval };
}

void LlvmProcessExecutor::clear_logic9_word(
    fsim_jit_logic9_word_v1* value) noexcept
{
    if (value == nullptr) {
        return;
    }
    value->planes[0] = 0;
    value->planes[1] = 0;
    value->planes[2] = 0;
    value->planes[3] = 0;
}

void LlvmProcessExecutor::require_logic9_signal(
    const CallbackState& state,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value)
{
    if (value == nullptr
        || state.context == nullptr
        || signal >= state.signal_widths.size()
        || signal >= state.signal_value_kinds.size()
        || state.signal_value_kinds[signal]
            != runtime::simir::ValueKind::logic9
        || state.signal_widths[signal] == 0
        || state.signal_widths[signal] > 64) {
        throw std::logic_error(
            "invalid generated Logic9 signal callback: signal="
            + std::to_string(signal)
            + " widths=" + std::to_string(state.signal_widths.size())
            + " kinds=" + std::to_string(state.signal_value_kinds.size())
            + " width="
            + (signal < state.signal_widths.size()
                    ? std::to_string(state.signal_widths[signal])
                    : "out-of-range")
            + " kind="
            + (signal < state.signal_value_kinds.size()
                    ? std::to_string(static_cast<unsigned>(
                        state.signal_value_kinds[signal]))
                    : "out-of-range")
            + " value=" + (value == nullptr ? "null" : "present")
            + " context=" + (state.context == nullptr ? "null" : "present"));
    }
}

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::checked_logic9_value(
    const CallbackState& state,
    const std::uint32_t signal,
    const fsim_jit_logic9_word_v1* value)
{
    require_logic9_signal(state, signal, value);
    return PackedLogic4::from_logic9_word(
        { state.signal_widths[signal],
            { value->planes[0],
                value->planes[1],
                value->planes[2],
                value->planes[3] } });
}

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::checked_logic9_slice(
    const CallbackState& state,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1* value)
{
    if (value == nullptr
        || state.context == nullptr
        || signal >= state.signal_widths.size()
        || signal >= state.signal_value_kinds.size()
        || state.signal_value_kinds[signal]
            != runtime::simir::ValueKind::logic9
        || state.signal_widths[signal] == 0) {
        throw std::logic_error(
            "invalid generated Logic9 partial-write callback");
    }
    const auto target_width = state.signal_widths[signal];
    if (width == 0 || width > 64
        || offset > target_width
        || width > target_width - offset) {
        throw std::logic_error(
            "invalid generated Logic9 partial-write callback");
    }
    return PackedLogic4::from_logic9_word(
        { width,
            { value->planes[0],
                value->planes[1],
                value->planes[2],
                value->planes[3] } });
}

[[nodiscard]] runtime::Logic4Word LlvmProcessExecutor::checked_slice_word(
    const CallbackState& state,
    const std::uint32_t signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const std::uint64_t aval,
    const std::uint64_t bval)
{
    if (state.context == nullptr
        || signal >= state.signal_widths.size()
        || width == 0 || width > 64) {
        throw std::logic_error(
            "invalid generated partial-write callback");
    }
    const auto target_width = state.signal_widths[signal];
    if (offset > target_width
        || width > target_width - offset) {
        throw std::logic_error(
            "generated partial-write range is outside its target");
    }
    return { width, aval, bval };
}

void LlvmProcessExecutor::assert_failed(
    void* context,
    std::uint32_t,
    std::uint32_t,
    const char*,
    std::uint64_t) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return;
    }
    // The generated status and the immutable SimIR assertion carry all data
    // needed after the C ABI returns. No C++ allocation or exception is
    // permitted in this thunk.
}

std::uint32_t LlvmProcessExecutor::signal_event(
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
        return state.context->signal_event(actual_signal) ? 1U : 0U;
    } catch (...) {
        capture_failure(state);
        return 0;
    }
}

std::uint64_t LlvmProcessExecutor::signal_last_value(
    void* context,
    const std::uint32_t signal,
    std::uint64_t* bval) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        if (bval != nullptr) {
            *bval = 0;
        }
        return 0;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (bval == nullptr || state.context == nullptr
            || actual_signal >= state.signal_widths.size()) {
            throw std::logic_error(
                "invalid generated signal-last-value callback");
        }
        const auto value = state.context->signal_last_value_word(actual_signal);
        if (value.width != state.signal_widths[actual_signal]
            || value.width == 0 || value.width > 64) {
            throw std::logic_error(
                "generated signal-last-value callback observed an invalid width");
        }
        *bval = value.bval;
        return value.aval;
    } catch (...) {
        capture_failure(state);
        if (bval != nullptr) {
            *bval = 0;
        }
        return 0;
    }
}

void LlvmProcessExecutor::signal_last_value_logic9(
    void* context,
    const std::uint32_t signal,
    fsim_jit_logic9_word_v1* result) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    clear_logic9_word(result);
    if (state.failure) {
        return;
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        require_logic9_signal(state, actual_signal, result);
        const auto value
            = state.context->signal_last_value_logic9_word(actual_signal);
        if (value.width != state.signal_widths[actual_signal]) {
            throw std::logic_error(
                "generated Logic9 last-value read observed an invalid width");
        }
        result->planes[0] = value.planes[0];
        result->planes[1] = value.planes[1];
        result->planes[2] = value.planes[2];
        result->planes[3] = value.planes[3];
    } catch (...) {
        capture_failure(state);
        clear_logic9_word(result);
    }
}

std::uint64_t LlvmProcessExecutor::signal_last_event(
    void* context,
    const std::uint32_t signal) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure || state.context == nullptr) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    try {
        const auto actual_signal = mapped_signal(state, signal);
        if (actual_signal >= state.signal_widths.size()) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return state.context->signal_last_event(actual_signal);
    } catch (...) {
        capture_failure(state);
        return std::numeric_limits<std::uint64_t>::max();
    }
}

std::uint32_t LlvmProcessExecutor::execute_signal_operation(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v1* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1;
    }
    try {
        invalidate_signal_read_cache(state);
        if (state.executor == nullptr || state.context == nullptr
            || state.process == nullptr || state.generated_process != process
            || instruction >= state.process->operations.size()
            || frame != &state.executor->frame_) {
            throw std::logic_error(
                "invalid generated exact-width signal callback");
        }
        state.executor->flush_update_words(*state.context);
        const auto& stored = state.process->operations[instruction];
        const auto dynamic_offset = [&](const runtime::simir::DynamicIndex& selection) {
            return runtime::simir::dynamic_index_offset(
                state.executor->read_register(selection.index, 32),
                selection);
        };
        if (const auto* read = runtime::simir::operation_get_if<runtime::simir::ReadSignal>(
                &stored)) {
            state.executor->write_register(
                read->destination,
                state.context->read_signal(read->signal));
        } else if (const auto* blocking = runtime::simir::operation_get_if<
                       runtime::simir::WriteBlocking>(&stored)) {
            state.context->write_blocking(
                blocking->signal,
                state.executor->read_register(
                    blocking->source,
                    runtime::simir::ProcessExecutor::native_register_width));
        } else if (const auto* blocking_slice = runtime::simir::operation_get_if<
                       runtime::simir::WriteBlockingSlice>(&stored)) {
            state.context->write_blocking_slice(
                blocking_slice->signal,
                state.executor->read_register(
                    blocking_slice->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                blocking_slice->offset);
        } else if (const auto* dynamic_blocking = runtime::simir::operation_get_if<
                       runtime::simir::WriteBlockingDynamicSlice>(&stored)) {
            state.context->write_blocking_slice(
                dynamic_blocking->signal,
                state.executor->read_register(dynamic_blocking->source, 1),
                dynamic_offset(dynamic_blocking->selection));
        } else if (const auto* dynamic_blocking_part = runtime::simir::operation_get_if<
                       runtime::simir::WriteBlockingDynamicPartSlice>(&stored)) {
            auto selected_write = runtime::simir::dynamic_part_write_value(
                state.executor->read_register(
                    dynamic_blocking_part->source,
                    dynamic_blocking_part->selection.width),
                state.executor->read_register(
                    dynamic_blocking_part->selection.base, 32),
                dynamic_blocking_part->selection);
            if (selected_write) {
                state.context->write_blocking_slice(
                    dynamic_blocking_part->signal,
                    std::move(selected_write->value),
                    selected_write->offset);
            }
        } else if (const auto* update = runtime::simir::operation_get_if<
                       runtime::simir::WriteUpdate>(&stored)) {
            state.context->write_update(
                update->signal,
                state.executor->read_register(
                    update->source,
                    runtime::simir::ProcessExecutor::native_register_width));
        } else if (const auto* update_slice = runtime::simir::operation_get_if<
                       runtime::simir::WriteUpdateSlice>(&stored)) {
            state.context->write_update_slice(
                update_slice->signal,
                state.executor->read_register(
                    update_slice->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                update_slice->offset);
        } else if (const auto* dynamic_update_bit = runtime::simir::operation_get_if<
                       runtime::simir::WriteUpdateDynamicSlice>(&stored)) {
            state.context->write_update_slice(
                dynamic_update_bit->signal,
                state.executor->read_register(dynamic_update_bit->source, 1),
                dynamic_offset(dynamic_update_bit->selection));
        } else if (const auto* dynamic_update = runtime::simir::operation_get_if<
                       runtime::simir::WriteUpdateDynamicPartSlice>(
                       &stored)) {
            auto selected_write = runtime::simir::dynamic_part_write_value(
                state.executor->read_register(
                    dynamic_update->source,
                    dynamic_update->selection.width),
                state.executor->read_register(
                    dynamic_update->selection.base, 32),
                dynamic_update->selection);
            if (selected_write) {
                state.context->write_update_slice(
                    dynamic_update->signal,
                    std::move(selected_write->value),
                    selected_write->offset);
            }
        } else if (const auto* write = runtime::simir::operation_get_if<
                       runtime::simir::WriteInertial>(&stored)) {
            state.context->write_inertial(
                write->signal,
                state.executor->read_register(
                    write->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                write->delays);
        } else if (const auto* after = runtime::simir::operation_get_if<
                       runtime::simir::WriteAfter>(&stored)) {
            state.context->write_after(
                after->signal,
                state.executor->read_register(
                    after->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                after->delay);
        } else if (const auto* after_slice = runtime::simir::operation_get_if<
                       runtime::simir::WriteAfterSlice>(&stored)) {
            state.context->write_after_slice(
                after_slice->signal,
                state.executor->read_register(
                    after_slice->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                after_slice->offset,
                after_slice->delay);
        } else if (const auto* dynamic_after = runtime::simir::operation_get_if<
                       runtime::simir::WriteAfterDynamicSlice>(&stored)) {
            state.context->write_after_slice(
                dynamic_after->signal,
                state.executor->read_register(dynamic_after->source, 1),
                dynamic_offset(dynamic_after->selection),
                dynamic_after->delay);
        } else if (const auto* dynamic_after_part = runtime::simir::operation_get_if<
                       runtime::simir::WriteAfterDynamicPartSlice>(&stored)) {
            auto selected_write = runtime::simir::dynamic_part_write_value(
                state.executor->read_register(
                    dynamic_after_part->source,
                    dynamic_after_part->selection.width),
                state.executor->read_register(
                    dynamic_after_part->selection.base, 32),
                dynamic_after_part->selection);
            if (selected_write) {
                state.context->write_after_slice(
                    dynamic_after_part->signal,
                    std::move(selected_write->value),
                    selected_write->offset,
                    dynamic_after_part->delay);
            }
        } else if (const auto* slice = runtime::simir::operation_get_if<
                       runtime::simir::WriteInertialSlice>(&stored)) {
            state.context->write_inertial_slice(
                slice->signal,
                state.executor->read_register(
                    slice->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                slice->offset,
                slice->delays);
        } else if (const auto* dynamic_part = runtime::simir::operation_get_if<
                       runtime::simir::WriteInertialDynamicPartSlice>(
                       &stored)) {
            auto selected_write = runtime::simir::dynamic_part_write_value(
                state.executor->read_register(
                    dynamic_part->source, dynamic_part->selection.width),
                state.executor->read_register(dynamic_part->selection.base, 32),
                dynamic_part->selection);
            if (selected_write) {
                state.context->write_inertial_slice(
                    dynamic_part->signal,
                    std::move(selected_write->value),
                    selected_write->offset,
                    dynamic_part->delays);
            }
        } else if (const auto* dynamic_inertial = runtime::simir::operation_get_if<
                       runtime::simir::WriteInertialDynamicSlice>(&stored)) {
            state.context->write_inertial_slice(
                dynamic_inertial->signal,
                state.executor->read_register(dynamic_inertial->source, 1),
                dynamic_offset(dynamic_inertial->selection),
                dynamic_inertial->delays);
        } else if (const auto* projected = runtime::simir::operation_get_if<
                       runtime::simir::WriteProjected>(&stored)) {
            state.context->write_projected(
                projected->signal,
                state.executor->read_register(
                    projected->source,
                    runtime::simir::ProcessExecutor::native_register_width),
                projected->delay,
                projected->rejection,
                projected->mode);
        } else if (const auto* force = runtime::simir::operation_get_if<
                       runtime::simir::ForceSignalSlice>(&stored)) {
            const auto offset = force->selection
                ? dynamic_offset(*force->selection)
                : force->offset;
            auto value = state.executor->read_register(
                force->source,
                runtime::simir::ProcessExecutor::native_register_width);
            if (force->driving_value) {
                state.context->force_driver_signal_slice(
                    force->signal, std::move(value), offset);
            } else {
                state.context->force_signal_slice(
                    force->signal, std::move(value), offset);
            }
        } else if (const auto* release = runtime::simir::operation_get_if<
                       runtime::simir::ReleaseSignalSlice>(&stored)) {
            const auto offset = release->selection
                ? dynamic_offset(*release->selection)
                : release->offset;
            if (release->driving_value) {
                state.context->release_driver_signal_slice(
                    release->signal, offset, release->width);
            } else {
                state.context->release_signal_slice(
                    release->signal, offset, release->width);
            }
        } else {
            throw std::logic_error(
                "generated exact-width signal callback references a different "
                "operation");
        }
        return 0;
    } catch (...) {
        capture_failure(state);
        return 1;
    }
}

#endif

} // namespace fsim::app::application_detail
