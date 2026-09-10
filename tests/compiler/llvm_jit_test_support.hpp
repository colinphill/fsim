// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/compiler/object_cache.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::tests::compiler {

namespace llvm_jit_test_detail {

    using fsim::compiler::JitExecutionStatus;
    using fsim::compiler::JitGeneratedRuntimeErrorReason;
    using fsim::compiler::JitOptimizationLevel;
    using fsim::compiler::JitProcessHandle;
    using fsim::compiler::JitProcessModuleEntry;
    using fsim::compiler::JitResumeStatus;
    using fsim::compiler::LlvmJit;
    using fsim::compiler::LlvmJitError;
    using fsim::compiler::LlvmJitGeneratedRuntimeError;
    using fsim::compiler::LlvmJitOptions;
    using fsim::compiler::LlvmJitUnsupportedError;
    using fsim::runtime::Logic4;
    using fsim::runtime::Logic9;
    using fsim::runtime::PackedLogic4;
    using namespace fsim::runtime::simir;

    struct EncodedSignal {
        std::uint64_t aval { };
        std::uint64_t bval { };

        friend bool operator==(EncodedSignal, EncodedSignal) = default;
    };

    enum class ScheduledWriteKind : std::uint8_t {
        update,
        after,
        slice_update,
        slice_after,
    };

    struct ScheduledWrite {
        ScheduledWriteKind kind = ScheduledWriteKind::update;
        std::uint32_t signal { };
        EncodedSignal value;
        std::uint64_t scheduled_at { };
        std::uint64_t delay { };
        std::uint64_t due { };
        std::uint32_t offset { };
        std::uint32_t width { };

        friend bool operator==(ScheduledWrite, ScheduledWrite) = default;
    };

    struct ObservedWrite {
        std::uint64_t time { };
        std::uint32_t signal { };
        EncodedSignal value;

        friend bool operator==(ObservedWrite, ObservedWrite) = default;
    };

    struct InertialWrite {
        std::uint32_t signal { };
        EncodedSignal value;
        std::uint32_t offset { };
        std::uint32_t width { };
        std::uint64_t rise { };
        std::uint64_t fall { };
        std::uint64_t turnoff { };

        friend bool operator==(InertialWrite, InertialWrite) = default;
    };

    struct ProjectedWrite {
        std::uint32_t signal { };
        EncodedSignal value;
        std::uint32_t offset { };
        std::uint32_t width { };
        std::uint64_t delay { };
        std::uint64_t rejection { };
        std::uint32_t mode { };

        friend bool operator==(ProjectedWrite, ProjectedWrite) = default;
    };

    struct TestRuntime {
        std::array<EncodedSignal, 16> signals { };
        std::array<std::vector<std::uint64_t>, 16> wide_signal_aval;
        std::array<std::vector<std::uint64_t>, 16> wide_signal_bval;
        std::array<std::vector<std::uint64_t>, 16> wide_signal_logic9_plane2;
        std::array<std::vector<std::uint64_t>, 16> wide_signal_logic9_plane3;
        std::uint32_t packed_signal_write_mode { UINT32_MAX };
        std::uint32_t packed_signal_write_signal { UINT32_MAX };
        std::uint32_t packed_signal_write_offset { };
        std::uint32_t packed_signal_write_width { };
        std::uint64_t packed_signal_write_delay { };
        std::uint32_t packed_signal_reads { };
        std::vector<std::uint32_t> packed_signal_write_modes;
        std::vector<std::uint32_t> packed_signal_write_signals;
        std::array<std::array<std::uint64_t, 4>, 16> logic9_signals { };
        std::uint32_t assertion_count { };
        std::uint32_t failed_process { };
        std::uint32_t failed_instruction { };
        std::string assertion_message;
        bool leave_bval_untouched { };
        std::vector<std::pair<std::uint32_t, EncodedSignal>> writes;
        std::uint64_t current_time { };
        std::uint32_t vital_timing_result {
            static_cast<std::uint32_t>(Logic9::zero)
        };
        std::vector<std::pair<std::uint32_t, std::uint32_t>> vital_delay_calls;
        std::vector<ScheduledWrite> scheduled_writes;
        std::vector<std::string> output;
        std::vector<std::uint32_t> output_processes;
        std::vector<bool> output_newlines;
        std::vector<std::string> postponed_output;
        std::vector<std::uint32_t> report_instructions;
        std::vector<std::uint32_t> formatted_instructions;
        std::vector<EncodedSignal> formatted_values;
        std::vector<std::array<std::uint64_t, 4>> formatted_logic9_values;
        std::vector<std::uint32_t> time_instructions;
        std::vector<std::uint32_t> monitor_install_instructions;
        std::vector<std::uint32_t> monitor_control_instructions;
        std::vector<std::uint32_t> random_instructions;
        std::vector<InertialWrite> inertial_writes;
        std::vector<ProjectedWrite> projected_writes;
        std::vector<std::array<std::uint32_t, 3>> released_slices;
        std::array<std::string, 32> strings;
        std::array<std::string, 8> string_objects;
        std::array<std::uint64_t, 5> container_read_aval { };
        std::array<std::uint64_t, 5> container_read_bval { };
        std::array<std::uint64_t, 5> container_write_aval { };
        std::array<std::uint64_t, 5> container_write_bval { };
        std::uint32_t container_packed_reads { };
        std::uint32_t container_packed_writes { };
        std::uint32_t dynamic_part_signal_reads { };
        std::uint32_t code_coverage_checked_calls { };
        std::uint32_t code_coverage_callback_status { };
        std::vector<std::uint32_t> code_coverage_checked_counters;
    };

extern "C" std::uint32_t container_operation_stub(
        void*, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
        std::uint64_t*, std::uint64_t*);

extern "C" std::uint32_t container_read_word_stub(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t*, std::uint64_t*);

extern "C" std::uint32_t container_write_word_stub(
        void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);

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
        const std::uint32_t word_count);

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
        const std::uint32_t word_count);

[[nodiscard]] std::array<std::uint64_t, 4> logic9_value(
        const fsim_jit_logic9_word_v1* value);

void store_logic9_value(
        fsim_jit_logic9_word_v1* destination,
        const std::array<std::uint64_t, 4>& value);

extern "C" void read_signal_logic9(
        void* opaque,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* value);

extern "C" void write_signal_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value);

extern "C" void write_update_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value);

extern "C" void write_after_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t);

void write_signal_slice_logic9_impl(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value);

extern "C" void write_signal_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value);

extern "C" void write_update_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value);

extern "C" void write_after_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t);

extern "C" void signal_last_value_logic9(
        void* opaque,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* value);

extern "C" void write_inertial_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t);

extern "C" void write_inertial_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t);

extern "C" void write_projected_logic9(
        void* opaque,
        const std::uint32_t signal,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint32_t);

extern "C" void write_projected_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_word_v1* value,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint32_t);

extern "C" void write_projected_waveform_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t,
        const fsim_jit_logic9_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t,
        const std::uint32_t);

extern "C" void write_projected_waveform_slice_logic9(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_logic9_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t,
        const std::uint32_t);

extern "C" void write_formatted_logic9(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t,
        const fsim_jit_logic9_word_v1* value);

extern "C" std::uint64_t read_signal(void* opaque,
        const std::uint32_t signal,
        std::uint64_t* bval);

extern "C" std::uint32_t read_signal_packed(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t width,
        std::uint64_t* const aval,
        std::uint64_t* const bval,
        std::uint64_t* const logic9_plane2,
        std::uint64_t* const logic9_plane3);

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
        fsim_jit_logic9_word_v1* const result);

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
        const std::uint64_t* const logic9_plane3);

extern "C" void write_signal(void* opaque, const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval);

extern "C" void assert_failed(void* opaque, const std::uint32_t process,
        const std::uint32_t instruction,
        const char* message,
        const std::uint64_t message_size);

extern "C" void write_update(void* opaque, const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval);

extern "C" void write_after(void* opaque, const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay);

[[nodiscard]] std::uint64_t low_mask(const std::uint32_t width);

extern "C" void write_signal_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval);

extern "C" void release_signal_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width);

extern "C" void write_update_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval);

extern "C" void write_after_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay);

extern "C" std::uint32_t signal_event(
        void*, const std::uint32_t);

extern "C" std::uint64_t signal_last_value(
        void* opaque,
        const std::uint32_t signal,
        std::uint64_t* bval);

extern "C" std::uint64_t signal_last_event(
        void*, const std::uint32_t);

extern "C" std::uint32_t signal_active(
        void*, const std::uint32_t);

extern "C" std::uint64_t signal_last_active(
        void*, const std::uint32_t);

extern "C" std::uint32_t signal_driving(
        void*, const std::uint32_t);

extern "C" std::uint64_t signal_driving_value(
        void* opaque,
        const std::uint32_t signal,
        std::uint64_t* bval);

extern "C" void signal_driving_value_logic9(
        void* opaque,
        const std::uint32_t signal,
        fsim_jit_logic9_word_v1* value);

extern "C" std::uint64_t read_simulation_time(void* opaque);

extern "C" std::uint32_t vital_timing_check(
        void* opaque, const std::uint32_t, const std::uint32_t);

extern "C" void vital_delay(
        void* opaque,
        const std::uint32_t process,
        const std::uint32_t instruction);

extern "C" void write_output(
        void* opaque,
        const std::uint32_t process,
        const char* text,
        const std::uint64_t text_size,
        const std::uint32_t newline);

extern "C" void schedule_output(
        void* opaque,
        const std::uint32_t,
        const char* text,
        const std::uint64_t text_size,
        const std::uint32_t newline);

extern "C" void write_report(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction);

extern "C" void write_formatted(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction,
        const std::uint32_t,
        const std::uint64_t aval,
        const std::uint64_t bval);

extern "C" void write_time(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction);

extern "C" void install_monitor(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction);

extern "C" void control_monitor(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction);

extern "C" std::uint64_t random_value(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t instruction,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t,
        const std::uint64_t,
        std::uint64_t* result_bval);

extern "C" void write_inertial(
        void* opaque,
        const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t rise,
        const std::uint64_t fall,
        const std::uint64_t turnoff);

extern "C" void write_inertial_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t rise,
        const std::uint64_t fall,
        const std::uint64_t turnoff);

extern "C" void write_projected(
        void* opaque,
        const std::uint32_t signal,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay,
        const std::uint64_t rejection,
        const std::uint32_t mode);

extern "C" void write_projected_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const std::uint64_t aval,
        const std::uint64_t bval,
        const std::uint64_t delay,
        const std::uint64_t rejection,
        const std::uint32_t mode);

extern "C" void write_projected_waveform(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t width,
        const fsim_jit_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t rejection,
        const std::uint32_t mode);

extern "C" void write_projected_waveform_slice(
        void* opaque,
        const std::uint32_t signal,
        const std::uint32_t offset,
        const std::uint32_t width,
        const fsim_jit_projected_element_v1* elements,
        const std::uint32_t count,
        const std::uint64_t rejection,
        const std::uint32_t mode);

extern "C" std::uint32_t load_string(
        void* opaque,
        const std::uint32_t destination,
        const char* bytes,
        const std::uint64_t size);

extern "C" std::uint32_t copy_string(
        void* opaque,
        const std::uint32_t destination,
        const std::uint32_t source);

extern "C" std::uint32_t read_string_object(
        void* opaque,
        const std::uint32_t destination,
        const std::uint32_t object);

extern "C" std::uint32_t write_string_object(
        void* opaque,
        const std::uint32_t object,
        const std::uint32_t source);

extern "C" std::uint32_t concatenate_strings(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t destination,
        const std::uint32_t* operands,
        const std::uint32_t count);

extern "C" std::uint32_t compare_strings(
        void* opaque,
        const std::uint32_t lhs,
        const std::uint32_t rhs,
        const std::uint32_t not_equal,
        std::uint32_t* result);

extern "C" std::uint32_t string_length(
        void* opaque,
        const std::uint32_t source,
        std::uint32_t* result);

extern "C" std::uint32_t string_index(
        void* opaque,
        const std::uint32_t,
        const std::uint32_t,
        const std::uint32_t source,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        const std::uint32_t signed_index,
        std::uint32_t* result);

extern "C" std::uint32_t string_replace_byte(
        void* opaque,
        const std::uint32_t process,
        const std::uint32_t instruction,
        const std::uint32_t target,
        const std::uint64_t index_aval,
        const std::uint64_t index_bval,
        const std::uint32_t signed_index,
        const std::uint64_t source_aval,
        const std::uint64_t source_bval);

extern "C" std::uint32_t write_string_output(
        void* opaque,
        const std::uint32_t process,
        const std::uint32_t source,
        const char* prefix,
        const std::uint64_t prefix_size,
        const char* suffix,
        const std::uint64_t suffix_size,
        const std::uint32_t newline,
        const std::uint32_t postponed);

extern "C" std::uint32_t record_code_coverage_counter(
        void* opaque,
        std::uint32_t,
        std::uint32_t,
        const std::uint32_t counter);

[[nodiscard]] fsim_jit_runtime_v1 abi(TestRuntime& runtime);

[[nodiscard]] fsim_jit_resume_result_v1 new_resume_result();

[[nodiscard]] Process make_arithmetic_process();

[[nodiscard]] EncodedSignal encode(const Logic4 value);

[[nodiscard]] EncodedSignal encode(
        const PackedLogic4& value);

[[nodiscard]] Logic4 equality(
        const Logic4 lhs,
        const Logic4 rhs);

[[nodiscard]] std::array<std::uint64_t, 4> planes(
        const PackedLogic4& value);

template <class Function>
    inline void expect_error(Function&& function, const std::string_view fragment)
    {
        bool rejected = false;
        try {
            std::forward<Function>(function)();
        } catch (const LlvmJitError& error) {
            rejected = true;
            assert(std::string_view { error.what() }.find(fragment) != std::string_view::npos);
        }
        assert(rejected);
    }

template <class Function>
    inline void expect_unsupported(Function&& function,
        const std::string_view fragment)
    {
        bool rejected = false;
        try {
            std::forward<Function>(function)();
        } catch (const LlvmJitUnsupportedError& error) {
            rejected = true;
            assert(std::string_view { error.what() }.find(fragment) != std::string_view::npos);
        } catch (const LlvmJitError&) {
            assert(false && "capability miss was not typed as unsupported");
        }
        assert(rejected);
    }

template <class Function>
    inline void expect_fatal_error(Function&& function,
        const std::string_view fragment)
    {
        bool rejected = false;
        try {
            std::forward<Function>(function)();
        } catch (const LlvmJitGeneratedRuntimeError&) {
            assert(false && "compiler or ABI failure was typed as generated runtime");
        } catch (const LlvmJitUnsupportedError&) {
            assert(false && "malformed IR or ABI failure was typed unsupported");
        } catch (const LlvmJitError& error) {
            rejected = true;
            assert(std::string_view { error.what() }.find(fragment) != std::string_view::npos);
        }
        assert(rejected);
    }

template <class Function>
    inline void expect_generated_runtime_error(
        Function&& function, const std::uint32_t instruction,
        const JitGeneratedRuntimeErrorReason reason,
        const std::string_view fragment)
    {
        bool rejected = false;
        try {
            std::forward<Function>(function)();
        } catch (const LlvmJitGeneratedRuntimeError& error) {
            rejected = true;
            assert(error.instruction() == instruction);
            assert(error.reason() == reason);
            assert(std::string_view { error.what() }.find(fragment) != std::string_view::npos);
        } catch (const LlvmJitError&) {
            assert(false && "generated runtime failure lost its typed metadata");
        }
        assert(rejected);
    }

} // namespace llvm_jit_test_detail

using namespace llvm_jit_test_detail;

void run_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_scheduled_callbacks_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_inertial_callbacks_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_projected_callbacks_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_scheduling_differential_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_control_flow_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_native_callable_regions();
void test_checked_integer_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_resumable_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_process_cohort_resume_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_class_service_boundaries_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_signal_waits_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_scalar_truth_tables_and_64_bits();
void test_systemverilog_scalar_transport_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wide_register_frame_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wide_transient_register_frame_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_optimized_frame_initialization_elision_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wide_signal_read_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wide_signal_write_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wide_container_operations_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_fused_container_object_read_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wide_value_operations_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_constant_dynamic_part_select_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_logic4_constant_dynamic_part_select_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_affine_dynamic_extract_fusion_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_fused_dynamic_part_signal_read_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_wildcard_case_matching_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_conditional_select_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_comparisons_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_logical_binary_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_reduction_and_shift_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_signed_shift_counts_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_unsigned_arithmetic_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_signed_arithmetic_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_extract_and_concatenate_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_insert_and_partial_writes_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_dynamic_packed_indices_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_initialized_bval_slot(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_direct_signal_read_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_direct_update_accumulator_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_static_trigger_regions_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_code_coverage_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_code_coverage_cache_identity();
void test_debug_point_instrumentation();
void test_logic9_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_vital_timing_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_vital_delay_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_persistent_object_cache();
void test_vhdl_language_profile_cache_identity(
    const std::filesystem::path& cache_directory);
void test_process_control_cache_identity();
void test_static_slice_consumer_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_slice_call_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_slice_ordering_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_slice_port_cache_identity(
    const std::filesystem::path& cache_directory);
void test_static_indexed_slice_cache_identity(
    const std::filesystem::path& cache_directory);
void test_fixed_array_function_return_cache_identity(
    const std::filesystem::path& cache_directory);
void test_nonstatic_function_return_cache_identity(
    const std::filesystem::path& cache_directory);
void test_expression_selection_cache_identity(
    const std::filesystem::path& cache_directory);
void test_procedural_update_cache_identity(
    const std::filesystem::path& cache_directory);
void test_container_construction_cache_identity(
    const std::filesystem::path& cache_directory);
void test_rejections();
void test_display_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);
void test_strings_at_level(
    fsim::compiler::JitOptimizationLevel optimization,
    std::string_view symbol);

} // namespace fsim::tests::compiler
