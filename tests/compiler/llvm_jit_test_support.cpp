// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler::llvm_jit_test_detail {

extern "C" std::uint32_t container_operation_stub(
        void*, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
        std::uint64_t*, std::uint64_t*)
    {
        return 1U;
    }

extern "C" std::uint32_t container_read_word_stub(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t*, std::uint64_t*)
    {
        return 1U;
    }

extern "C" std::uint32_t container_write_word_stub(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t)
    {
        return 1U;
    }

extern "C" std::uint32_t container_read_packed(
        void* opaque,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        std::uint64_t* aval,
        std::uint64_t* bval,
        const std::uint32_t word_count)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(index_aval == 0U && index_bval == 0U);
        assert(word_count == runtime.container_read_aval.size());
        std::ranges::copy(runtime.container_read_aval, aval);
        std::ranges::copy(runtime.container_read_bval, bval);
        ++runtime.container_packed_reads;
        return 0U;
    }

extern "C" std::uint32_t container_write_packed(
        void* opaque,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        const std::uint64_t* aval,
        const std::uint64_t* bval,
        const std::uint32_t word_count)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(index_aval == 0U && index_bval == 0U);
        assert(word_count == runtime.container_write_aval.size());
        std::ranges::copy_n(
            aval, word_count, runtime.container_write_aval.begin());
        std::ranges::copy_n(
            bval, word_count, runtime.container_write_bval.begin());
        ++runtime.container_packed_writes;
        return 0U;
    }

[[nodiscard]] std::array<std::uint64_t, 4> logic9_value(
        const fsim_jit_logic9_word_v1* value)
    {
        assert(value != nullptr);
        return {
            value->planes[0],
            value->planes[1],
            value->planes[2],
            value->planes[3]
        };
    }

void store_logic9_value(
        fsim_jit_logic9_word_v1* destination,
        const std::array<std::uint64_t, 4>& value)
    {
        assert(destination != nullptr);
        std::copy(value.begin(), value.end(), destination->planes);
    }

extern "C" void read_signal_logic9(
        void* opaque,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* value)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.logic9_signals.size());
        store_logic9_value(value, runtime.logic9_signals[signal]);
    }

extern "C" void write_signal_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.logic9_signals.size());
        runtime.logic9_signals[signal] = logic9_value(value);
    }

extern "C" void write_update_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value)
    {
        write_signal_logic9(opaque, signal, value);
    }

extern "C" void write_after_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t)
    {
        write_signal_logic9(opaque, signal, value);
    }

void write_signal_slice_logic9_impl(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.logic9_signals.size());
        assert(offset < 64 && width <= 64 - offset);
        const auto mask = low_mask(width) << offset;
        const auto source = logic9_value(value);
        for (std::size_t plane = 0; plane < source.size(); ++plane) {
            runtime.logic9_signals[signal][plane] = (runtime.logic9_signals[signal][plane] & ~mask)
                | ((source[plane] << offset) & mask);
        }
    }

extern "C" void write_signal_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value)
    {
        write_signal_slice_logic9_impl(
            opaque, signal, offset, width, value);
    }

extern "C" void write_update_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value)
    {
        write_signal_slice_logic9_impl(
            opaque, signal, offset, width, value);
    }

extern "C" void write_after_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t)
    {
        write_signal_slice_logic9_impl(
            opaque, signal, offset, width, value);
    }

extern "C" void signal_last_value_logic9(
        void* opaque,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* value)
    {
        read_signal_logic9(opaque, signal, value);
    }

extern "C" void write_inertial_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t)
    {
        write_signal_logic9(opaque, signal, value);
    }

extern "C" void write_inertial_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t)
    {
        write_signal_slice_logic9_impl(
            opaque, signal, offset, width, value);
    }

extern "C" void write_projected_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint32_t)
    {
        write_signal_logic9(opaque, signal, value);
    }

extern "C" void write_projected_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint32_t)
    {
        write_signal_slice_logic9_impl(
            opaque, signal, offset, width, value);
    }

extern "C" void write_projected_waveform_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t,
        const fsim_jit_logic9_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t,
        const std::uint32_t)
    {
        assert(elements != nullptr && count > 0);
        write_signal_logic9(opaque, signal, &elements[count - 1].value);
    }

extern "C" void write_projected_waveform_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t,
        const std::uint32_t)
    {
        assert(elements != nullptr && count > 0);
        write_signal_slice_logic9_impl(
            opaque, signal, offset, width, &elements[count - 1].value);
    }

extern "C" void write_formatted_logic9(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t,
        const fsim_jit_logic9_word_v1* value)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.formatted_logic9_values.push_back(logic9_value(value));
    }

extern "C" std::uint64_t read_signal(void* opaque,
        const std::uint32_t signal,
        std::uint64_t* bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        if (!runtime.leave_bval_untouched) {
            *bval = runtime.signals[signal].bval;
        }
        return runtime.signals[signal].aval;
    }

extern "C" std::uint32_t read_signal_packed(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t width,
        std::uint64_t* const aval,
        std::uint64_t* const bval,
        std::uint64_t* const logic9_plane2,
        std::uint64_t* const logic9_plane3)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        ++runtime.packed_signal_reads;
        assert(signal < runtime.signals.size());
        assert(width > 64U);
        assert(aval != nullptr && bval != nullptr);
        const auto words = static_cast<std::size_t>((width + 63U) / 64U);
        const auto copy_plane = [words](
                                    const std::vector<std::uint64_t>& source,
                                    std::uint64_t* const destination) {
            assert(source.size() == words);
            std::ranges::copy(source, destination);
        };
        copy_plane(runtime.wide_signal_aval[signal], aval);
        copy_plane(runtime.wide_signal_bval[signal], bval);
        const auto& plane2 = runtime.wide_signal_logic9_plane2[signal];
        const auto& plane3 = runtime.wide_signal_logic9_plane3[signal];
        if (logic9_plane2 != nullptr || logic9_plane3 != nullptr) {
            assert(logic9_plane2 != nullptr && logic9_plane3 != nullptr);
            copy_plane(plane2, logic9_plane2);
            copy_plane(plane3, logic9_plane3);
        } else {
            assert(plane2.empty() && plane3.empty());
        }
        return 0;
    }

extern "C" std::uint32_t read_signal_dynamic_part(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t source_width,
        const std::uint64_t base_aval,
        const std::uint64_t base_bval,
        const std::int64_t left,
        const std::int64_t right,
        const std::uint32_t base_offset,
        const std::uint32_t width,
        const std::uint32_t flags,
        fsim_jit_logic9_word_v1* const result)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        ++runtime.dynamic_part_signal_reads;
        const auto words = static_cast<std::size_t>((source_width + 63U) / 64U);
        assert(signal < runtime.wide_signal_aval.size());
        assert(runtime.wide_signal_aval[signal].size() == words);
        assert(runtime.wide_signal_bval[signal].size() == words);
        const auto logic9
            = runtime.wide_signal_logic9_plane2[signal].size() == words
            && runtime.wide_signal_logic9_plane3[signal].size() == words;
        const auto source = logic9
            ? PackedLogic4::from_logic9_word_planes(
                  source_width,
                  runtime.wide_signal_aval[signal],
                  runtime.wide_signal_bval[signal],
                  runtime.wide_signal_logic9_plane2[signal],
                  runtime.wide_signal_logic9_plane3[signal])
            : PackedLogic4::from_word_planes(
                  source_width,
                  runtime.wide_signal_aval[signal],
                  runtime.wide_signal_bval[signal]);
        const auto selected = runtime::simir::dynamic_part_select_value(
            source,
            PackedLogic4::from_aval_bval(32, base_aval, base_bval),
            left,
            right,
            base_offset,
            width,
            (flags & UINT32_C(1)) != 0U,
            (flags & UINT32_C(2)) != 0U,
            (flags & UINT32_C(4)) != 0U);
        if (selected.is_logic9()) {
            const auto value = selected.logic9_low_word();
            std::ranges::copy(value.planes, result->planes);
        } else {
            const auto value = selected.unchecked_low_word();
            result->planes[0] = value.aval;
            result->planes[1] = value.bval;
            result->planes[2] = 0U;
            result->planes[3] = 0U;
        }
        return 0U;
    }

extern "C" std::uint32_t write_signal_packed(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint32_t mode,
        const std::uint64_t delay,
        const std::uint64_t* const aval,
        const std::uint64_t* const bval,
        const std::uint64_t* const logic9_plane2,
        const std::uint64_t* const logic9_plane3)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(width > 0U && aval != nullptr && bval != nullptr);
        const auto words = static_cast<std::size_t>((width + 63U) / 64U);
        runtime.wide_signal_aval[signal].assign(aval, aval + words);
        runtime.wide_signal_bval[signal].assign(bval, bval + words);
        if (logic9_plane2 != nullptr || logic9_plane3 != nullptr) {
            assert(logic9_plane2 != nullptr && logic9_plane3 != nullptr);
            runtime.wide_signal_logic9_plane2[signal].assign(
                logic9_plane2, logic9_plane2 + words);
            runtime.wide_signal_logic9_plane3[signal].assign(
                logic9_plane3, logic9_plane3 + words);
        } else {
            runtime.wide_signal_logic9_plane2[signal].clear();
            runtime.wide_signal_logic9_plane3[signal].clear();
        }
        runtime.packed_signal_write_mode = mode;
        runtime.packed_signal_write_signal = signal;
        runtime.packed_signal_write_offset = offset;
        runtime.packed_signal_write_width = width;
        runtime.packed_signal_write_delay = delay;
        runtime.packed_signal_write_modes.push_back(mode);
        runtime.packed_signal_write_signals.push_back(signal);
        return 0;
    }

extern "C" void write_signal(void* opaque, const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.signals[signal] = { aval, bval };
        runtime.writes.emplace_back(signal, runtime.signals[signal]);
    }

extern "C" void assert_failed(void* opaque, const std::uint32_t process,
        const std::uint32_t instruction,
        const char* message,
        const std::uint64_t message_size)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        ++runtime.assertion_count;
        runtime.failed_process = process;
        runtime.failed_instruction = instruction;
        runtime.assertion_message.assign(
            message, static_cast<std::size_t>(message_size));
    }

extern "C" void write_update(void* opaque, const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.scheduled_writes.push_back(
            { ScheduledWriteKind::update, signal, { aval, bval },
                runtime.current_time, 0, runtime.current_time });
    }

extern "C" void write_after(void* opaque, const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(delay <= std::numeric_limits<std::uint64_t>::max() - runtime.current_time);
        runtime.scheduled_writes.push_back(
            { ScheduledWriteKind::after, signal, { aval, bval },
                runtime.current_time, delay, runtime.current_time + delay });
    }

[[nodiscard]] std::uint64_t low_mask(const std::uint32_t width)
    {
        assert(width > 0 && width <= 64);
        return width == 64
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << width) - UINT64_C(1);
    }

extern "C" void write_signal_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(offset < 64 && width <= 64 - offset);
        const auto mask = low_mask(width) << offset;
        runtime.signals[signal].aval = (runtime.signals[signal].aval & ~mask)
            | ((aval << offset) & mask);
        runtime.signals[signal].bval = (runtime.signals[signal].bval & ~mask)
            | ((bval << offset) & mask);
        runtime.writes.emplace_back(
            signal, runtime.signals[signal]);
    }

extern "C" void release_signal_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(offset < 64 && width <= 64 - offset);
        runtime.released_slices.push_back({ signal, offset, width });
    }

extern "C" void write_update_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.scheduled_writes.push_back(
            { ScheduledWriteKind::slice_update,
                signal,
                { aval, bval },
                runtime.current_time,
                0,
                runtime.current_time,
                offset,
                width });
    }

extern "C" void write_after_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(delay
            <= std::numeric_limits<std::uint64_t>::max()
                - runtime.current_time);
        runtime.scheduled_writes.push_back(
            { ScheduledWriteKind::slice_after,
                signal,
                { aval, bval },
                runtime.current_time,
                delay,
                runtime.current_time + delay,
                offset,
                width });
    }

extern "C" std::uint32_t signal_event(
        void*, const std::uint32_t)
    {
        return 0;
    }

extern "C" std::uint64_t signal_last_value(
        void* opaque,
        const std::uint32_t signal,
        std::uint64_t* bval)
    {
        return read_signal(opaque, signal, bval);
    }

extern "C" std::uint64_t signal_last_event(
        void*, const std::uint32_t)
    {
        return 0;
    }

extern "C" std::uint32_t signal_active(
        void*, const std::uint32_t)
    {
        return 0;
    }

extern "C" std::uint64_t signal_last_active(
        void*, const std::uint32_t)
    {
        return 0;
    }

extern "C" std::uint32_t signal_driving(
        void*, const std::uint32_t)
    {
        return 1;
    }

extern "C" std::uint64_t signal_driving_value(
        void* opaque,
        const std::uint32_t signal,
        std::uint64_t* bval)
    {
        return read_signal(opaque, signal, bval);
    }

extern "C" void signal_driving_value_logic9(
        void* opaque,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* value)
    {
        read_signal_logic9(opaque, signal, value);
    }

extern "C" std::uint64_t read_simulation_time(void* opaque)
    {
        return static_cast<TestRuntime*>(opaque)->current_time;
    }

extern "C" std::uint32_t vital_timing_check(
        void* opaque, const std::uint32_t, const std::uint32_t)
    {
        return static_cast<TestRuntime*>(opaque)->vital_timing_result;
    }

extern "C" void vital_delay(
        void* opaque,
        const std::uint32_t process,
        const std::uint32_t instruction)
    {
        static_cast<TestRuntime*>(opaque)->vital_delay_calls.emplace_back(
            process, instruction);
    }

extern "C" void write_output(
        void* opaque,
        const std::uint32_t process,
        const char* text,
        const std::uint64_t text_size,
        const std::uint32_t newline)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(text != nullptr || text_size == 0);
        assert(
            text_size
            <= static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()));
        assert(newline <= 1);
        runtime.output.emplace_back(
            text == nullptr ? "" : text,
            static_cast<std::size_t>(text_size));
        runtime.output_processes.push_back(process);
        runtime.output_newlines.push_back(newline != 0);
    }

extern "C" void schedule_output(
        void* opaque,
        const std::uint32_t,
        const char* text,
        const std::uint64_t text_size,
        const std::uint32_t newline)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(text != nullptr || text_size == 0);
        assert(
            text_size
            <= static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()));
        assert(newline <= 1);
        runtime.postponed_output.emplace_back(
            text == nullptr ? "" : text,
            static_cast<std::size_t>(text_size));
    }

extern "C" void write_report(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.report_instructions.push_back(instruction);
    }

extern "C" void write_formatted(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction,
        const std::uint32_t,
        const std::uint64_t aval,
        const std::uint64_t bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.formatted_instructions.push_back(instruction);
        runtime.formatted_values.push_back({ aval, bval });
    }

extern "C" void write_time(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.time_instructions.push_back(instruction);
    }

extern "C" void install_monitor(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.monitor_install_instructions.push_back(instruction);
    }

extern "C" void control_monitor(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.monitor_control_instructions.push_back(instruction);
    }

extern "C" std::uint64_t random_value(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t,
        std::uint64_t* result_bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.random_instructions.push_back(instruction);
        *result_bval = 0;
        return UINT64_C(0x89abcdef);
    }

extern "C" void write_inertial(
        void* opaque,
        const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t rise,
        const std::uint64_t fall,
        const std::uint64_t turnoff)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.inertial_writes.push_back(
            { signal, { aval, bval }, 0, 0, rise, fall, turnoff });
    }

extern "C" void write_inertial_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t rise,
        const std::uint64_t fall,
        const std::uint64_t turnoff)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.inertial_writes.push_back(
            { signal, { aval, bval }, offset, width, rise, fall, turnoff });
    }

extern "C" void write_projected(
        void* opaque,
        const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay,
        const std::uint64_t rejection,
        const std::uint32_t mode)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.projected_writes.push_back(
            { signal, { aval, bval }, 0, 0, delay, rejection, mode });
    }

extern "C" void write_projected_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay,
        const std::uint64_t rejection,
        const std::uint32_t mode)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        runtime.projected_writes.push_back(
            { signal, { aval, bval }, offset, width, delay, rejection, mode });
    }

extern "C" void write_projected_waveform(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t width,
        const fsim_jit_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t rejection,
        const std::uint32_t mode)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(elements != nullptr && count >= 2);
        for (std::uint32_t index = 0; index < count; ++index) {
            runtime.projected_writes.push_back(
                { signal,
                    { elements[index].aval, elements[index].bval },
                    0,
                    width,
                    elements[index].delay,
                    rejection,
                    mode });
        }
    }

extern "C" void write_projected_waveform_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t rejection,
        const std::uint32_t mode)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(signal < runtime.signals.size());
        assert(elements != nullptr && count >= 2);
        for (std::uint32_t index = 0; index < count; ++index) {
            runtime.projected_writes.push_back(
                { signal,
                    { elements[index].aval, elements[index].bval },
                    offset,
                    width,
                    elements[index].delay,
                    rejection,
                    mode });
        }
    }

extern "C" std::uint32_t load_string(
        void* opaque,
        const std::uint32_t destination,
        const char* bytes,
        const std::uint64_t size)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        assert(destination < runtime.strings.size());
        assert(size <= maximum_string_bytes);
        runtime.strings[destination].assign(
            bytes, static_cast<std::size_t>(size));
        return 0;
    }

extern "C" std::uint32_t copy_string(
        void* opaque,
        const std::uint32_t destination,
        const std::uint32_t source)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.strings.at(destination) = runtime.strings.at(source);
        return 0;
    }

extern "C" std::uint32_t read_string_object(
        void* opaque,
        const std::uint32_t destination,
        const std::uint32_t object)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.strings.at(destination) = runtime.string_objects.at(object);
        return 0;
    }

extern "C" std::uint32_t write_string_object(
        void* opaque,
        const std::uint32_t object,
        const std::uint32_t source)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        runtime.string_objects.at(object) = runtime.strings.at(source);
        return 0;
    }

extern "C" std::uint32_t concatenate_strings(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t destination,
        const std::uint32_t* operands,
        const std::uint32_t count)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        std::string result;
        for (std::uint32_t index = 0; index < count; ++index) {
            result += runtime.strings.at(operands[index]);
        }
        if (result.size() > maximum_string_bytes) {
            return 1;
        }
        runtime.strings.at(destination) = std::move(result);
        return 0;
    }

extern "C" std::uint32_t compare_strings(
        void* opaque,
        const std::uint32_t lhs,
        const std::uint32_t rhs,
        const std::uint32_t not_equal,
        std::uint32_t* result)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        *result = (fsim::runtime::systemverilog_string_compare(
                       runtime.strings.at(lhs), runtime.strings.at(rhs))
                      == 0)
            != (not_equal != 0);
        return 0;
    }

extern "C" std::uint32_t string_length(
        void* opaque,
        const std::uint32_t source,
        std::uint32_t* result)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        *result = static_cast<std::uint32_t>(
            fsim::runtime::systemverilog_string_length(
                runtime.strings.at(source)));
        return 0;
    }

extern "C" std::uint32_t string_index(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t source,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        const std::uint32_t signed_index,
        std::uint32_t* result)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        const auto raw = static_cast<std::uint32_t>(index_aval);
        const auto index = signed_index != 0
            ? static_cast<std::int64_t>(static_cast<std::int32_t>(raw))
            : static_cast<std::int64_t>(raw);
        if (index_bval != 0 || index < 0
            || static_cast<std::uint64_t>(index)
                >= fsim::runtime::systemverilog_string_length(
                    runtime.strings.at(source))) {
            return 1;
        }
        *result = fsim::runtime::systemverilog_string_at(
            runtime.strings.at(source), static_cast<std::size_t>(index));
        return 0;
    }

extern "C" std::uint32_t string_replace_byte(
        void* opaque,
        const std::uint32_t process,
        const std::uint32_t instruction,
        const std::uint32_t target,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        const std::uint32_t signed_index,
        const std::uint64_t source_aval,
        const std::uint64_t source_bval)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        std::uint32_t unused = 0;
        if (source_bval != 0
            || string_index(
                   opaque,
                   process,
                   instruction,
                   target,
                   index_aval,
                   index_bval,
                   signed_index,
                   &unused)
                != 0) {
            return 1;
        }
        const auto index = signed_index != 0
            ? static_cast<std::size_t>(
                  static_cast<std::int32_t>(index_aval))
            : static_cast<std::size_t>(
                  static_cast<std::uint32_t>(index_aval));
        fsim::runtime::systemverilog_string_replace(
            runtime.strings.at(target), index,
            static_cast<std::uint32_t>(source_aval), maximum_string_bytes);
        return 0;
    }

extern "C" std::uint32_t write_string_output(
        void* opaque,
        const std::uint32_t process,
        const std::uint32_t source,
        const char* prefix,
        const std::uint64_t prefix_size,
        const char* suffix,
        const std::uint64_t suffix_size,
        const std::uint32_t newline,
        const std::uint32_t postponed)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        std::string text { prefix, static_cast<std::size_t>(prefix_size) };
        text += runtime.strings.at(source);
        text.append(suffix, static_cast<std::size_t>(suffix_size));
        (postponed != 0 ? runtime.postponed_output : runtime.output)
            .push_back(std::move(text));
        runtime.output_processes.push_back(process);
        runtime.output_newlines.push_back(newline != 0);
        return 0;
    }

extern "C" std::uint32_t record_code_coverage_counter(
        void* opaque,
        std::uint32_t,
        std::uint32_t,
        const std::uint32_t counter)
    {
        auto& runtime = *static_cast<TestRuntime*>(opaque);
        ++runtime.code_coverage_checked_calls;
        runtime.code_coverage_checked_counters.push_back(counter);
        return runtime.code_coverage_callback_status;
    }

[[nodiscard]] fsim_jit_runtime_v1 abi(TestRuntime& runtime)
    {
        fsim_jit_runtime_v1 result { };
        result.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
        result.struct_size = static_cast<std::uint32_t>(sizeof(fsim_jit_runtime_v1));
        result.context = &runtime;
        result.read_signal = &read_signal;
        result.write_signal = &write_signal;
        result.assert_failed = &assert_failed;
        result.write_update = &write_update;
        result.write_after = &write_after;
        result.write_signal_slice = &write_signal_slice;
        result.write_update_slice = &write_update_slice;
        result.write_after_slice = &write_after_slice;
        result.signal_event = &signal_event;
        result.signal_last_value = &signal_last_value;
        result.signal_last_event = &signal_last_event;
        result.signal_active = &signal_active;
        result.signal_last_active = &signal_last_active;
        result.signal_driving = &signal_driving;
        result.signal_driving_value = &signal_driving_value;
        result.signal_driving_value_logic9 = &signal_driving_value_logic9;
        result.read_simulation_time = &read_simulation_time;
        result.vital_timing_check = &vital_timing_check;
        result.vital_delay = &vital_delay;
        result.write_output = &write_output;
        result.schedule_output = &schedule_output;
        result.write_report = &write_report;
        result.write_formatted = &write_formatted;
        result.write_time = &write_time;
        result.install_monitor = &install_monitor;
        result.control_monitor = &control_monitor;
        result.random_value = &random_value;
        result.write_inertial = &write_inertial;
        result.write_inertial_slice = &write_inertial_slice;
        result.write_projected = &write_projected;
        result.write_projected_slice = &write_projected_slice;
        result.write_projected_waveform = &write_projected_waveform;
        result.write_projected_waveform_slice = &write_projected_waveform_slice;
        result.read_signal_logic9 = &read_signal_logic9;
        result.write_signal_logic9 = &write_signal_logic9;
        result.write_update_logic9 = &write_update_logic9;
        result.write_after_logic9 = &write_after_logic9;
        result.write_signal_slice_logic9 = &write_signal_slice_logic9;
        result.write_update_slice_logic9 = &write_update_slice_logic9;
        result.write_after_slice_logic9 = &write_after_slice_logic9;
        result.signal_last_value_logic9 = &signal_last_value_logic9;
        result.write_inertial_logic9 = &write_inertial_logic9;
        result.write_inertial_slice_logic9 = &write_inertial_slice_logic9;
        result.write_projected_logic9 = &write_projected_logic9;
        result.write_projected_slice_logic9 = &write_projected_slice_logic9;
        result.write_projected_waveform_logic9 = &write_projected_waveform_logic9;
        result.write_projected_waveform_slice_logic9 = &write_projected_waveform_slice_logic9;
        result.write_formatted_logic9 = &write_formatted_logic9;
        result.force_signal_slice = &write_signal_slice;
        result.force_signal_slice_logic9 = &write_signal_slice_logic9;
        result.release_signal_slice = &release_signal_slice;
        result.load_string = &load_string;
        result.copy_string = &copy_string;
        result.read_string_object = &read_string_object;
        result.write_string_object = &write_string_object;
        result.concatenate_strings = &concatenate_strings;
        result.compare_strings = &compare_strings;
        result.string_length = &string_length;
        result.string_index = &string_index;
        result.string_replace_byte = &string_replace_byte;
        result.write_string_output = &write_string_output;
        result.force_driver_signal_slice = &write_signal_slice;
        result.force_driver_signal_slice_logic9 = &write_signal_slice_logic9;
        result.release_driver_signal_slice = &release_signal_slice;
        result.container_operation = &container_operation_stub;
        result.container_read_word = &container_read_word_stub;
        result.container_write_word = &container_write_word_stub;
        result.container_read_packed = &container_read_packed;
        result.container_write_packed = &container_write_packed;
        result.read_signal_packed = &read_signal_packed;
        result.write_signal_packed = &write_signal_packed;
        result.read_signal_dynamic_part = &read_signal_dynamic_part;
        result.record_code_coverage_counter
            = &record_code_coverage_counter;
        return result;
    }

[[nodiscard]] fsim_jit_resume_result_v1 new_resume_result()
    {
        return {
            FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1,
            static_cast<std::uint32_t>(sizeof(fsim_jit_resume_result_v1)),
            0,
            FSIM_JIT_INVALID_INSTRUCTION,
            0,
        };
    }

[[nodiscard]] Process make_arithmetic_process()
    {
        Process process;
        process.id = 7;
        process.name = "arithmetic";
        process.register_count = 10;
        process.operations = {
            ReadSignal { 0, 0 },
            ReadSignal { 1, 1 },
            Binary { BinaryOperator::bit_and, 2, 0, 1 },
            WriteBlocking { 2, 2 },
            Binary { BinaryOperator::bit_or, 3, 0, 1 },
            WriteBlocking { 3, 3 },
            Binary { BinaryOperator::bit_xor, 4, 0, 1 },
            WriteBlocking { 4, 4 },
            Binary { BinaryOperator::add_unsigned, 5, 0, 1 },
            WriteBlocking { 5, 5 },
            UnaryNot { 6, 0 },
            WriteBlocking { 6, 6 },
            Binary { BinaryOperator::equal, 7, 0, 1 },
            WriteBlocking { 7, 7 },
            LoadConstant { 8, PackedLogic4::from_msb_string("01000100") },
            Binary { BinaryOperator::equal, 9, 5, 8 },
            Assert {
                9,
                "unexpected sum",
                AssertionSeverity::failure,
                SourceLocation { } },
            Halt { },
        };
        return process;
    }

[[nodiscard]] EncodedSignal encode(const Logic4 value)
    {
        switch (value) {
        case Logic4::zero:
            return { 0, 0 };
        case Logic4::one:
            return { 1, 0 };
        case Logic4::x:
            return { 1, 1 };
        case Logic4::z:
            return { 0, 1 };
        }
        return { 1, 1 };
    }

[[nodiscard]] EncodedSignal encode(
        const PackedLogic4& value)
    {
        assert(value.width() > 0);
        assert(value.width() <= 64);
        return {
            value.aval_words().front(),
            value.bval_words().front()
        };
    }

[[nodiscard]] Logic4 equality(
        const Logic4 lhs,
        const Logic4 rhs)
    {
        const auto known = [](const Logic4 value) {
            return value == Logic4::zero || value == Logic4::one;
        };
        if (!known(lhs) || !known(rhs)) {
            return Logic4::x;
        }
        return lhs == rhs ? Logic4::one : Logic4::zero;
    }

[[nodiscard]] std::array<std::uint64_t, 4> planes(
        const PackedLogic4& value)
    {
        return value.logic9_low_word().planes;
    }

} // namespace fsim::tests::compiler::llvm_jit_test_detail
