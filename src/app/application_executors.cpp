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

namespace {

struct JitProcessProfile {
    struct Entry {
        std::string name;
        std::uint64_t resumes { };
        runtime::simir::ProcessId generated_process { };
        std::size_t direct_read_slots { };
        std::size_t direct_update_slots { };
        std::size_t operations { };
        std::size_t static_waits { };
        std::chrono::nanoseconds elapsed { };
    };

    bool enabled = std::getenv("FSIM_PROFILE_JIT_PROCESSES") != nullptr;
    std::map<runtime::simir::ProcessId, Entry> entries;

    void record(
        const runtime::simir::Process& process,
        const runtime::simir::ProcessId generated_process,
        const std::size_t direct_read_slots,
        const std::size_t direct_update_slots,
        const std::chrono::nanoseconds elapsed)
    {
        if (!enabled) {
            return;
        }
        auto& entry = entries[process.id];
        if (entry.resumes == 0U) {
            entry.name = process.name;
            entry.generated_process = generated_process;
            entry.direct_read_slots = direct_read_slots;
            entry.direct_update_slots = direct_update_slots;
            entry.operations = process.operations.size();
            entry.static_waits = static_cast<std::size_t>(
                std::ranges::count_if(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_holds<
                            runtime::simir::WaitSensitivity>(operation);
                    }));
        }
        ++entry.resumes;
        entry.elapsed += elapsed;
    }

    ~JitProcessProfile()
    {
        if (!enabled) {
            return;
        }
        std::vector<std::pair<runtime::simir::ProcessId, Entry>> ranked {
            entries.begin(), entries.end()
        };
        std::ranges::sort(ranked, [](const auto& left, const auto& right) {
            return left.second.elapsed > right.second.elapsed;
        });
        const auto count
            = std::getenv("FSIM_PROFILE_JIT_PROCESSES_ALL") != nullptr
            ? ranked.size()
            : std::min<std::size_t>(ranked.size(), 32U);
        auto total_elapsed = std::chrono::nanoseconds::zero();
        std::uint64_t total_resumes = 0;
        for (const auto& [id, entry] : ranked) {
            static_cast<void>(id);
            total_elapsed += entry.elapsed;
            total_resumes += entry.resumes;
        }
        std::cerr << "FSIM-JIT-PROCESS-PROFILE-SUMMARY processes="
                  << ranked.size()
                  << " resumes=" << total_resumes
                  << " elapsed_ms="
                  << std::chrono::duration<double, std::milli>(
                         total_elapsed).count()
                  << '\n';
        for (std::size_t index = 0; index < count; ++index) {
            const auto& [id, entry] = ranked[index];
            std::cerr << "FSIM-JIT-PROCESS-PROFILE id=" << id
                      << " generated_id=" << entry.generated_process
                      << " resumes=" << entry.resumes
                      << " direct_read_slots="
                      << entry.direct_read_slots
                      << " direct_update_slots="
                      << entry.direct_update_slots
                      << " operations=" << entry.operations
                      << " static_waits=" << entry.static_waits
                      << " elapsed_ms="
                      << std::chrono::duration<double, std::milli>(
                             entry.elapsed).count()
                      << " name='" << entry.name << "'\n";
        }
    }
};

JitProcessProfile& jit_process_profile()
{
    static JitProcessProfile profile;
    return profile;
}

class JitProcessProfileScope {
public:
    explicit JitProcessProfileScope(
        const runtime::simir::Process& process,
        const runtime::simir::ProcessId generated_process,
        const std::size_t direct_read_slots,
        const std::size_t direct_update_slots)
        : process_(process)
        , generated_process_(generated_process)
        , direct_read_slots_(direct_read_slots)
        , direct_update_slots_(direct_update_slots)
        , enabled_(jit_process_profile().enabled)
    {
        if (enabled_) {
            begin_ = std::chrono::steady_clock::now();
        }
    }

    ~JitProcessProfileScope()
    {
        if (enabled_) {
            jit_process_profile().record(
                process_,
                generated_process_,
                direct_read_slots_,
                direct_update_slots_,
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - begin_));
        }
    }

private:
    const runtime::simir::Process& process_;
    runtime::simir::ProcessId generated_process_ { };
    std::size_t direct_read_slots_ { };
    std::size_t direct_update_slots_ { };
    bool enabled_ { };
    std::chrono::steady_clock::time_point begin_;
};

} // namespace

[[nodiscard]] runtime::simir::ProcessResumeResult LlvmProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    const runtime::simir::InstructionIndex start_instruction)
{
    JitProcessProfileScope process_profile {
        process_, generated_process_, direct_read_signals_.size(),
        direct_update_slots_.size()
    };
    const bool consuming_cohort
        = cohort_resume_mode_ == CohortResumeMode::consume;
    if (!consuming_cohort
        && frame_.program_counter != start_instruction) {
        const bool kernel_owned_callable_boundary = frame_.program_counter < process_.operations.size()
            && [&] {
                   const auto& operation = process_.operations[frame_.program_counter];
                   const auto* call = fsim::runtime::simir::operation_get_if<
                       runtime::simir::Call>(&operation);
                   const auto* return_operation = fsim::runtime::simir::operation_get_if<
                       runtime::simir::Return>(&operation);
                   return (call != nullptr && call->stack.capacity == 0)
                       || (return_operation != nullptr
                           && return_operation->stack.capacity == 0)
                       || fsim::runtime::simir::operation_holds<
                           runtime::simir::CallableFramePush>(operation)
                       || fsim::runtime::simir::operation_holds<
                           runtime::simir::CallableFramePop>(operation);
               }();
        if (!kernel_owned_callable_boundary) {
            throw compiler::LlvmJitError(
                "compiled process frame PC disagrees with the simulation kernel");
        }
        frame_.program_counter = start_instruction;
    }

    auto& callback_state = callback_state_;
    auto& runtime = runtime_;
    if (!consuming_cohort) {
        if (callback_state.executor == nullptr) {
            callback_state.executor = this;
            callback_state.process = &process_;
            callback_state.generated_process = generated_process_;
            callback_state.signal_widths = signal_widths_;
            callback_state.signal_value_kinds = signal_value_kinds_;
            callback_state.signal_remap = signal_remap_
                    && dense_signal_remap_.empty()
                ? std::span<const SignalRemap::value_type> { *signal_remap_ }
                : std::span<const SignalRemap::value_type> { };
            callback_state.dense_signal_remap = dense_signal_remap_;
            callback_state.dense_signal_remap_base = dense_signal_remap_base_;
        }
        callback_state.context = &context;
        callback_state.direct_signal_logic9_planes = {
            context.direct_signal_logic9_plane0(),
            context.direct_signal_logic9_plane1(),
            context.direct_signal_logic9_plane2(),
            context.direct_signal_logic9_plane3()
        };
        callback_state.supports_direct_word_updates
            = context.supports_direct_word_updates();
        callback_state.failure = { };
        invalidate_signal_read_cache(callback_state);
        if (runtime.abi_version == 0) {
            runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V1;
            runtime.struct_size = sizeof(runtime);
            runtime.read_signal = read_signal;
            runtime.write_signal = write_signal;
            runtime.assert_failed = assert_failed;
            runtime.write_update = write_update;
            runtime.write_after = write_after;
            runtime.write_signal_slice = write_signal_slice;
            runtime.write_update_slice = write_update_slice;
            runtime.write_after_slice = write_after_slice;
            runtime.signal_event = signal_event;
            runtime.signal_last_value = signal_last_value;
            runtime.signal_last_event = signal_last_event;
            runtime.signal_active = signal_active;
            runtime.signal_last_active = signal_last_active;
            runtime.signal_driving = signal_driving;
            runtime.signal_driving_value = signal_driving_value;
            runtime.signal_driving_value_logic9 = signal_driving_value_logic9;
            runtime.read_simulation_time = read_simulation_time;
            runtime.vital_timing_check = vital_timing_check;
            runtime.vital_delay = vital_delay;
            runtime.write_output = write_output;
            runtime.schedule_output = schedule_output;
            runtime.write_report = write_report;
            runtime.write_formatted = write_formatted;
            runtime.write_time = write_time;
            runtime.install_monitor = install_monitor;
            runtime.control_monitor = control_monitor;
            runtime.random_value = random_value;
            runtime.write_inertial = write_inertial;
            runtime.write_inertial_slice = write_inertial_slice;
            runtime.write_projected = write_projected;
            runtime.write_projected_slice = write_projected_slice;
            runtime.write_projected_waveform = write_projected_waveform;
            runtime.write_projected_waveform_slice = write_projected_waveform_slice;
            runtime.read_signal_logic9 = signal_remap_
                ? read_signal_logic9
                : read_signal_logic9_identity;
            runtime.write_signal_logic9 = write_signal_logic9;
            runtime.write_update_logic9 = write_update_logic9;
            runtime.write_after_logic9 = write_after_logic9;
            runtime.write_signal_slice_logic9 = write_signal_slice_logic9;
            runtime.write_update_slice_logic9 = write_update_slice_logic9;
            runtime.write_after_slice_logic9 = write_after_slice_logic9;
            runtime.signal_last_value_logic9 = signal_last_value_logic9;
            runtime.write_inertial_logic9 = write_inertial_logic9;
            runtime.write_inertial_slice_logic9 = write_inertial_slice_logic9;
            runtime.write_projected_logic9 = write_projected_logic9;
            runtime.write_projected_slice_logic9 = write_projected_slice_logic9;
            runtime.write_projected_waveform_logic9 = write_projected_waveform_logic9;
            runtime.write_projected_waveform_slice_logic9 = write_projected_waveform_slice_logic9;
            runtime.write_formatted_logic9 = write_formatted_logic9;
            runtime.force_signal_slice = force_signal_slice;
            runtime.force_signal_slice_logic9 = force_signal_slice_logic9;
            runtime.release_signal_slice = release_signal_slice;
            runtime.force_driver_signal_slice = force_driver_signal_slice;
            runtime.force_driver_signal_slice_logic9 = force_driver_signal_slice_logic9;
            runtime.release_driver_signal_slice = release_driver_signal_slice;
            runtime.load_string = load_string;
            runtime.copy_string = copy_string;
            runtime.read_string_object = read_string_object;
            runtime.write_string_object = write_string_object;
            runtime.concatenate_strings = concatenate_strings;
            runtime.compare_strings = compare_strings;
            runtime.string_length = string_length;
            runtime.string_index = string_index;
            runtime.string_replace_code_point = string_replace_code_point;
            runtime.write_string_output = write_string_output;
            runtime.file_open = file_open;
            runtime.file_close = file_close;
            runtime.file_write = file_write;
            runtime.file_read_line = file_read_line;
            runtime.file_end_of_file = file_end_of_file;
            runtime.file_error = file_error;
            runtime.container_operation = container_operation;
            runtime.execute_signal_operation = execute_signal_operation;
            runtime.container_read_word = container_read_word;
            runtime.container_write_word = container_write_word;
            runtime.container_read_packed = container_read_packed;
            runtime.container_write_packed = container_write_packed;
            runtime.read_signal_packed = read_signal_packed;
            runtime.write_signal_packed = write_signal_packed;
            runtime.read_signal_dynamic_part = read_signal_dynamic_part;
            const auto direct_aval = context.direct_signal_aval();
            const auto direct_bval = context.direct_signal_bval();
            const bool supports_direct_planes
                = (!direct_read_signals_.empty() || !direct_update_signals_.empty())
                && !direct_aval.empty()
                && direct_aval.size() == direct_bval.size()
                && direct_aval.size()
                    <= std::numeric_limits<std::uint32_t>::max();
            const bool supports_direct_reads = supports_direct_planes
                && !direct_read_signals_.empty()
                && direct_read_signals_.size() == layout_.direct_read_signals.size()
                && std::ranges::all_of(
                    direct_read_signals_,
                    [&](const auto signal) { return signal < direct_aval.size(); });
            runtime.direct_signal_aval = supports_direct_planes
                ? direct_aval.data()
                : nullptr;
            runtime.direct_signal_bval = supports_direct_planes
                ? direct_bval.data()
                : nullptr;
            const bool supports_direct_logic9_planes
                = callback_state.direct_signal_logic9_planes[0].size()
                    == direct_aval.size()
                && callback_state.direct_signal_logic9_planes[1].size()
                    == direct_aval.size()
                && callback_state.direct_signal_logic9_planes[2].size()
                    == direct_aval.size()
                && callback_state.direct_signal_logic9_planes[3].size()
                    == direct_aval.size();
            runtime.direct_signal_logic9_plane0
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[0].data()
                : nullptr;
            runtime.direct_signal_logic9_plane1
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[1].data()
                : nullptr;
            runtime.direct_signal_logic9_plane2
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[2].data()
                : nullptr;
            runtime.direct_signal_logic9_plane3
                = supports_direct_logic9_planes
                ? callback_state.direct_signal_logic9_planes[3].data()
                : nullptr;
            runtime.direct_read_signals = supports_direct_reads
                ? direct_read_signals_.data()
                : nullptr;
            runtime.direct_read_signal_count = supports_direct_reads
                ? static_cast<std::uint32_t>(direct_read_signals_.size())
                : 0U;
            runtime.direct_signal_count = supports_direct_planes
                ? static_cast<std::uint32_t>(direct_aval.size())
                : 0U;
            runtime.direct_signal_reserved = 0U;
            const auto direct_wide_aval = context.direct_wide_signal_aval();
            const auto direct_wide_bval = context.direct_wide_signal_bval();
            const auto direct_wide_logic9_plane2
                = context.direct_wide_signal_logic9_plane2();
            const auto direct_wide_logic9_plane3
                = context.direct_wide_signal_logic9_plane3();
            const auto direct_wide_offsets = context.direct_wide_signal_offsets();
            const bool supports_direct_wide_planes
                = !direct_wide_aval.empty()
                && direct_wide_aval.size() == direct_wide_bval.size()
                && direct_wide_aval.size()
                    <= std::numeric_limits<std::uint32_t>::max()
                && direct_wide_offsets.size()
                    <= std::numeric_limits<std::uint32_t>::max();
            const bool supports_direct_wide_logic9_planes
                = supports_direct_wide_planes
                && direct_wide_logic9_plane2.size()
                    == direct_wide_aval.size()
                && direct_wide_logic9_plane3.size()
                    == direct_wide_aval.size();
            runtime.direct_wide_signal_aval = supports_direct_wide_planes
                ? direct_wide_aval.data()
                : nullptr;
            runtime.direct_wide_signal_bval = supports_direct_wide_planes
                ? direct_wide_bval.data()
                : nullptr;
            runtime.direct_wide_signal_offsets = supports_direct_wide_planes
                ? direct_wide_offsets.data()
                : nullptr;
            runtime.direct_wide_signal_offset_count = supports_direct_wide_planes
                ? static_cast<std::uint32_t>(direct_wide_offsets.size())
                : 0U;
            runtime.direct_wide_word_count = supports_direct_wide_planes
                ? static_cast<std::uint32_t>(direct_wide_aval.size())
                : 0U;
            runtime.direct_wide_signal_logic9_plane2
                = supports_direct_wide_logic9_planes
                ? direct_wide_logic9_plane2.data()
                : nullptr;
            runtime.direct_wide_signal_logic9_plane3
                = supports_direct_wide_logic9_planes
                ? direct_wide_logic9_plane3.data()
                : nullptr;
        }
        runtime.context = &callback_state;
        runtime.flags = context.execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS
            : 0;
        runtime.direct_update_slots
            = callback_state.supports_direct_word_updates
                && !direct_update_slots_.empty()
            ? direct_update_slots_.data()
            : nullptr;
        runtime.direct_update_slot_count = static_cast<std::uint32_t>(
            direct_update_slots_.size());
        runtime.direct_update_reserved = 0U;
        runtime.direct_update_active_words
            = callback_state.supports_direct_word_updates
                && !direct_update_active_words_.empty()
            ? direct_update_active_words_.data()
            : nullptr;
        runtime.direct_update_active_word_count = static_cast<std::uint32_t>(
            direct_update_active_words_.size());
        runtime.direct_update_active_reserved = 0U;
        runtime.static_trigger_mask = context.static_trigger_mask();
        const auto writer_revision = context.signal_writer_revision();
        if (writer_revision != direct_update_writer_revision_) {
            const auto stable_owners
                = context.stable_single_writer_processes();
            const auto direct_aval = context.direct_signal_aval();
            const auto direct_bval = context.direct_signal_bval();
            const bool enabled = std::getenv(
                "FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION") == nullptr;
            for (std::size_t index = 0;
                index < direct_update_slots_.size(); ++index) {
                auto& slot = direct_update_slots_[index];
                const auto signal = direct_update_signals_[index];
                if (signal < signal_value_kinds_.size()
                    && signal_value_kinds_[signal]
                        == runtime::simir::ValueKind::logic9) {
                    slot.reserved &= ~1U;
                    continue;
                }
                const bool stable = enabled && slot.width <= 64U
                    && signal < stable_owners.size()
                    && signal < direct_aval.size()
                    && signal < direct_bval.size();
                const bool owned = stable
                    && stable_owners[signal] == process_.id;
                if (!owned) {
                    slot.reserved &= ~1U;
                    continue;
                }
                if ((slot.reserved & 1U) == 0U) {
                    slot.aval = direct_aval[signal];
                    slot.bval = direct_bval[signal];
                }
                slot.reserved |= 1U;
            }
            direct_update_writer_revision_ = writer_revision;
        }
    }

    if (cohort_resume_mode_ == CohortResumeMode::prepare) {
        cohort_resume_mode_ = CohortResumeMode::normal;
        return { };
    }

    const auto flush_updates = [&] {
        static const bool single_update_batches_enabled
            = std::getenv("FSIM_DISABLE_SINGLE_UPDATE_BATCH") == nullptr;
        flush_buffered_logic9_updates(context);
        if (single_update_batches_enabled && !consuming_cohort
            && callback_state.supports_direct_word_updates
            && !jit_process_profile().enabled
            && context.direct_update_domain() != nullptr) {
            auto batch = runtime::simir::ProcessUpdateSlotBatch {
                process_.id,
                direct_update_slot_views_,
                direct_update_active_words_
            };
            if (context.write_validated_update_slot_batches(
                    std::span { &batch, 1U })) {
                return;
            }
        }
        flush_update_words(context);
    };

    auto result = consuming_cohort
        ? cohort_resume_result_
        : fsim_jit_resume_result_v1 { };
    if (!consuming_cohort) {
        result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
        result.struct_size = sizeof(result);
    }
    compiler::JitResumeStatus status;
    try {
        status = [&]() -> compiler::JitResumeStatus {
        try {
            if (consuming_cohort) {
                cohort_resume_mode_ = CohortResumeMode::normal;
                const auto failure = std::exchange(
                    cohort_resume_failure_, { });
                if (failure) {
                    std::rethrow_exception(failure);
                }
                return static_cast<compiler::JitResumeStatus>(
                    cohort_resume_status_);
            }
            return jit_.resume_prevalidated(binding_, runtime, frame_, result);
        } catch (const compiler::LlvmJitGeneratedRuntimeError& error) {
            if (callback_state.failure) {
                std::rethrow_exception(callback_state.failure);
            }
            switch (error.reason()) {
            case compiler::JitGeneratedRuntimeErrorReason::
                unknown_branch_condition:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "branch condition is unknown or high impedance");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_operand_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer operand contains an unknown or "
                    "high-impedance value");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_overflow:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer arithmetic overflow");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_division_by_zero:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer division by zero");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_negative_exponent:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer exponent must be nonnegative");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_subtype_range:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "VHDL integer subtype range check failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                dynamic_index_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "dynamic packed index contains an unknown or "
                    "high-impedance value");
            case compiler::JitGeneratedRuntimeErrorReason::
                dynamic_index_range:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "dynamic packed index is outside the declared range");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack pointer or return target is unknown");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_overflow:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack capacity is exhausted");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_underflow:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack underflow");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_target:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "call-stack return target is invalid");
            case compiler::JitGeneratedRuntimeErrorReason::
                string_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "mutable string runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                file_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "text file runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                container_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "bounded container runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                signal_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id,
                    error.instruction(),
                    "exact-width signal runtime callback failed");
            }
            throw;
        } catch (...) {
            // A callback failure is the first language/runtime failure observed by
            // generated code and must not be masked by a later adapter status.
            if (callback_state.failure) {
                std::rethrow_exception(callback_state.failure);
            }
            throw;
        }
        }();
    } catch (...) {
        const auto failure = std::current_exception();
        flush_updates();
        if (!active_container_object_aliases_.empty()) {
            discard_container_object_aliases();
        }
        std::rethrow_exception(failure);
    }
    flush_updates();
    if (callback_state.failure) {
        std::rethrow_exception(callback_state.failure);
    }
    // ReadContainerObject installs a zero-copy alias. Any operation that needs
    // a complete value materializes it before executing; aliases still active
    // at a suspension boundary were consumed only by direct element reads and
    // are dead temporaries, so do not snapshot the whole object here.
    if (!active_container_object_aliases_.empty()) {
        discard_container_object_aliases();
    }
    if (result.instruction >= process_.operations.size()) {
        throw compiler::LlvmJitError(
            "compiled process returned an invalid boundary instruction");
    }
    if (status == compiler::JitResumeStatus::wait_sensitivity) {
        require_boundary<runtime::simir::WaitSensitivity>(
            result.instruction, "WaitSensitivity");
        if (frame_.program_counter != result.instruction + 1U) {
            throw compiler::LlvmJitError(
                "compiled process returned a non-sequential WaitSensitivity PC");
        }
        auto resumed = runtime::simir::ProcessResumeResult {
            result.instruction, frame_.program_counter
        };
        resumed.external.kind
            = runtime::simir::ExternalSuspendKind::wait_sensitivity;
        return resumed;
    }

    switch (status) {
    case compiler::JitResumeStatus::completed:
        require_boundary<runtime::simir::Halt>(
            result.instruction, "completion");
        break;
    case compiler::JitResumeStatus::assertion_failed: {
        const auto* assertion = fsim::runtime::simir::operation_get_if<runtime::simir::Assert>(
            &process_.operations[result.instruction]);
        if (assertion != nullptr) {
            throw runtime::simir::AssertionError(
                process_.id,
                result.instruction,
                assertion->message.empty()
                    ? "assertion failed"
                    : assertion->message,
                assertion->severity,
                assertion->source);
        }
        const auto* report = fsim::runtime::simir::operation_get_if<runtime::simir::Report>(
            &process_.operations[result.instruction]);
        if (report != nullptr
            && report->severity
                == runtime::simir::AssertionSeverity::failure) {
            throw runtime::simir::AssertionError(
                process_.id,
                result.instruction,
                report->message.empty()
                    ? "report failure"
                    : report->message,
                report->severity,
                report->source,
                true);
        }
        throw compiler::LlvmJitError(
            "compiled process reported an assertion at an incompatible "
            "instruction");
    }
    case compiler::JitResumeStatus::wait_for: {
        const auto* wait = fsim::runtime::simir::operation_get_if<runtime::simir::WaitFor>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
            throw compiler::LlvmJitError(
                "compiled process reported WaitFor at a non-wait instruction");
        }
        const auto expected_delay = wait->source ? 0 : wait->delay;
        if (result.delay != expected_delay) {
            throw compiler::LlvmJitError(
                "compiled process returned a WaitFor delay that disagrees "
                "with SimIR");
        }
        break;
    }
    case compiler::JitResumeStatus::wait_on: {
        const auto* wait = fsim::runtime::simir::operation_get_if<
            runtime::simir::WaitOn>(
            &process_.operations[result.instruction]);
        if (wait == nullptr) {
            throw compiler::LlvmJitError(
                "compiled process reported WaitOn at a non-wait instruction");
        }
        if (result.delay != wait->timeout.value_or(0)) {
            throw compiler::LlvmJitError(
                "compiled process returned a WaitOn timeout that disagrees "
                "with SimIR");
        }
        break;
    }
    case compiler::JitResumeStatus::wait_sensitivity:
        throw compiler::LlvmJitError(
            "compiled WaitSensitivity bypassed its fast return path");
    case compiler::JitResumeStatus::wait_forever:
        require_boundary<runtime::simir::WaitForever>(
            result.instruction, "WaitForever");
        break;
    case compiler::JitResumeStatus::yielded:
        require_boundary<runtime::simir::Yield>(
            result.instruction, "yield");
        break;
    case compiler::JitResumeStatus::fork:
        require_boundary<runtime::simir::Fork>(
            result.instruction, "fork");
        break;
    case compiler::JitResumeStatus::fork_end:
        require_boundary<runtime::simir::ForkEnd>(
            result.instruction, "fork end");
        break;
    case compiler::JitResumeStatus::wait_fork:
        require_boundary<runtime::simir::WaitFork>(
            result.instruction, "wait fork");
        break;
    case compiler::JitResumeStatus::disable_fork:
        require_boundary<runtime::simir::DisableFork>(
            result.instruction, "disable fork");
        break;
    case compiler::JitResumeStatus::debug_point:
        require_boundary<runtime::simir::DebugPoint>(
            result.instruction, "debug point");
        break;
    case compiler::JitResumeStatus::paused:
        require_boundary<runtime::simir::Pause>(
            result.instruction, "pause");
        break;
    case compiler::JitResumeStatus::stopped:
        require_boundary<runtime::simir::Stop>(
            result.instruction, "stop");
        break;
    case compiler::JitResumeStatus::simir_boundary: {
        const auto& operation = process_.operations[result.instruction];
        const auto* call = fsim::runtime::simir::operation_get_if<
            runtime::simir::Call>(&operation);
        const auto* return_operation = fsim::runtime::simir::operation_get_if<
            runtime::simir::Return>(&operation);
        const auto* read_signal = fsim::runtime::simir::operation_get_if<
            runtime::simir::ReadSignal>(&operation);
        const auto host_boundary = (read_signal != nullptr
                                       && read_signal->kind
                                           != runtime::simir::SignalReadKind::current)
            || fsim::runtime::simir::operation_holds<
                                       runtime::simir::ClassAllocate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassPropertyRead>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassPropertyWrite>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassMethodCall>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticPropertyRead>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticPropertyWrite>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ClassStaticMethodCall>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSelf>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessStatusQuery>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessCompleted>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessAwait>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessKill>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSuspend>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessResume>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessGetRandState>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSetRandState>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::ProcessSrandom>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::WaitOrder>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::EventTriggered>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::EventAlias>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::WaitRegion>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageSample>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageQuery>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::RandomDistribution>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::PlusArgSelect>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SystemCommand>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VcdControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageDatabaseControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::StochasticQueueOperation>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::PlaEvaluate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::WaitPla>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::TimeFormatControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::DisableBlock>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxCreate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxPut>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxGet>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::MailboxNum>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphoreCreate>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphoreGet>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::SemaphorePut>(operation)
            || (call != nullptr && call->stack.capacity == 0)
            || (return_operation != nullptr
                && return_operation->stack.capacity == 0)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePush>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePop>(operation);
        if (!host_boundary) {
            throw compiler::LlvmJitError(
                "compiled process reported an unsupported SimIR boundary");
        }
        break;
    }
    }
    const auto& boundary_operation = process_.operations[result.instruction];
    const auto* boundary_call = fsim::runtime::simir::operation_get_if<
        runtime::simir::Call>(&boundary_operation);
    const auto* boundary_return = fsim::runtime::simir::operation_get_if<
        runtime::simir::Return>(&boundary_operation);
    const bool callable_boundary = status
            == compiler::JitResumeStatus::simir_boundary
        && ((boundary_call != nullptr
                && boundary_call->stack.capacity == 0)
            || (boundary_return != nullptr
                && boundary_return->stack.capacity == 0)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePush>(boundary_operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CallableFramePop>(boundary_operation));
    const auto expected_program_counter = callable_boundary
        ? result.instruction
        : result.instruction + 1U;
    if (frame_.program_counter != expected_program_counter) {
        throw compiler::LlvmJitError(
            "compiled process returned a non-sequential boundary PC");
    }
    return runtime::simir::ProcessResumeResult {
        result.instruction, frame_.program_counter
    };
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_cohort(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries)
{
    if (entries.size() < 2U) {
        return 0U;
    }
    for (const auto& entry : entries) {
        if (entry.executor == nullptr
            || entry.executor->cohort_domain() != &jit_
            || entry.context == nullptr) {
            return 0U;
        }
    }

    const bool cache_matches = cohort_members_.size() == entries.size()
        && std::ranges::equal(
            entries,
            cohort_members_,
            { },
            [](const auto& entry) {
                return static_cast<LlvmProcessExecutor*>(entry.executor);
            },
            std::identity { })
        && std::ranges::equal(
            entries,
            cohort_start_instructions_,
            { },
            &runtime::simir::ProcessCohortResumeEntry::start_instruction,
            std::identity { });
    if (!cache_matches) {
        cohort_members_.clear();
        cohort_start_instructions_.clear();
        cohort_native_entries_.clear();
        cohort_update_batches_.clear();
        cohort_logic9_update_batches_.clear();
        cohort_binding_ = { };
        cohort_members_.reserve(entries.size());
        cohort_start_instructions_.reserve(entries.size());
        cohort_native_entries_.reserve(entries.size());
        cohort_update_batches_.reserve(entries.size());
        cohort_logic9_update_batches_.reserve(entries.size());
    }
    std::size_t prepared { };
    for (const auto& entry : entries) {
        auto& executor = *static_cast<LlvmProcessExecutor*>(entry.executor);
        executor.cohort_resume_mode_ = CohortResumeMode::prepare;
        try {
            static_cast<void>(executor.resume(
                *entry.context, entry.start_instruction));
        } catch (...) {
            for (const auto& reset_entry : entries) {
                auto& reset = *static_cast<LlvmProcessExecutor*>(
                    reset_entry.executor);
                reset.cohort_resume_mode_ = CohortResumeMode::normal;
            }
            return 0U;
        }
        executor.cohort_resume_result_ = { };
        executor.cohort_resume_result_.abi_version
            = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
        executor.cohort_resume_result_.struct_size
            = sizeof(executor.cohort_resume_result_);
        if (!cache_matches) {
            cohort_members_.push_back(&executor);
            cohort_start_instructions_.push_back(entry.start_instruction);
            cohort_native_entries_.push_back(
                compiler::JitProcessCohortResumeEntry {
                    executor.binding_, executor.runtime_, executor.frame_,
                    executor.cohort_resume_result_,
                    reinterpret_cast<std::uint8_t*>(entry.queued),
                    reinterpret_cast<std::uint8_t*>(
                        entry.waiting_on_static),
                    reinterpret_cast<std::uint8_t*>(entry.status)
                });
            cohort_update_batches_.push_back({
                executor.process_.id,
                executor.direct_update_slot_views_,
                executor.direct_update_active_words_
            });
            cohort_logic9_update_batches_.push_back({
                executor.process_.id,
                executor.buffered_logic9_update_views_
            });
        }
        ++prepared;
    }

    auto native_entries
        = std::span { cohort_native_entries_.data(), entries.size() };
    auto update_batches
        = std::span { cohort_update_batches_.data(), entries.size() };
    auto logic9_update_batches
        = std::span {
            cohort_logic9_update_batches_.data(), entries.size() };
    static const bool bound_cohort_enabled
        = std::getenv("FSIM_DISABLE_BOUND_COHORT") == nullptr;
    const auto executed = bound_cohort_enabled && cohort_binding_
        ? jit_.resume_cohort_prevalidated(cohort_binding_, native_entries)
        : jit_.resume_cohort_prevalidated(native_entries);
    if (bound_cohort_enabled && !cohort_binding_) {
        cohort_binding_ = jit_.bind_cohort_prevalidated(native_entries);
    }
    bool cohort_updates_flushed { };
    static const bool cohort_update_batches_enabled
        = std::getenv("FSIM_DISABLE_COHORT_UPDATE_BATCH") == nullptr;
    if (cohort_update_batches_enabled
        && executed == entries.size() && !jit_process_profile().enabled) {
        const auto* const update_domain
            = entries.front().context->direct_update_domain();
        const bool compatible = update_domain != nullptr
            && std::ranges::all_of(
                entries,
                [&](const auto& entry) {
                    return entry.context->direct_update_domain()
                        == update_domain;
                })
            && std::ranges::all_of(
                native_entries,
                [](const auto& entry) {
                    return entry.status
                            == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY
                        && !entry.failure;
                })
            && std::ranges::all_of(
                entries,
                [](const auto& entry) {
                    const auto& executor
                        = *static_cast<LlvmProcessExecutor*>(entry.executor);
                    return !executor.callback_state_.failure;
                });
        if (compatible) {
            try {
                const bool logic9_flushed
                    = entries.front().context
                          ->write_validated_logic9_update_batches(
                              logic9_update_batches);
                if (!logic9_flushed) {
                    for (std::size_t index = 0; index < executed; ++index) {
                        auto& executor = *static_cast<LlvmProcessExecutor*>(
                            entries[index].executor);
                        executor.flush_buffered_logic9_updates(
                            *entries[index].context);
                    }
                }
                cohort_updates_flushed
                    = logic9_flushed
                    && entries.front().context
                          ->write_validated_update_slot_batches(
                              update_batches);
            } catch (...) {
                entries.front().failure = std::current_exception();
                return 1U;
            }
        }
    }
    for (std::size_t index = 0; index < executed; ++index) {
        auto& entry = entries[index];
        auto& executor = *static_cast<LlvmProcessExecutor*>(entry.executor);
        const auto status = static_cast<compiler::JitResumeStatus>(
            native_entries[index].status);
        const bool callback_failed
            = static_cast<bool>(executor.callback_state_.failure);
        if (status == compiler::JitResumeStatus::wait_sensitivity
            && !callback_failed && !native_entries[index].failure
            && !jit_process_profile().enabled) {
            try {
                if (!cohort_updates_flushed) {
                    executor.flush_update_words(*entry.context);
                }
                if (!executor.active_container_object_aliases_.empty()) {
                    executor.discard_container_object_aliases();
                }
                const auto& result = executor.cohort_resume_result_;
                if (result.instruction
                    >= executor.process_.operations.size()) {
                    throw compiler::LlvmJitError(
                        "compiled cohort process returned an invalid "
                        "boundary instruction");
                }
                if (executor.frame_.program_counter
                    != result.instruction + 1U) {
                    throw compiler::LlvmJitError(
                        "compiled cohort process returned a non-sequential "
                        "WaitSensitivity PC");
                }
                entry.result = runtime::simir::ProcessResumeResult {
                    result.instruction, executor.frame_.program_counter
                };
                entry.result.external.kind
                    = runtime::simir::ExternalSuspendKind::wait_sensitivity;
                continue;
            } catch (...) {
                entry.failure = std::current_exception();
                return index + 1U;
            }
        }
        executor.cohort_resume_status_ = native_entries[index].status;
        executor.cohort_resume_failure_
            = callback_failed
            ? executor.callback_state_.failure
            : native_entries[index].failure;
        executor.cohort_resume_mode_ = CohortResumeMode::consume;
        try {
            entry.result = executor.resume(
                *entry.context, entry.start_instruction);
        } catch (...) {
            entry.failure = std::current_exception();
            return index + 1U;
        }
    }
    return executed;
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_region(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries,
    const std::span<const std::size_t> active_indices)
{
    if (entries.size() < 2U || active_indices.empty()
        || !std::ranges::is_sorted(active_indices)
        || std::ranges::adjacent_find(active_indices)
            != active_indices.end()
        || active_indices.back() >= entries.size()) {
        return 0U;
    }
    const bool cache_matches = region_entries_identity_ == entries.data()
        && region_members_.size() == entries.size();
    if (!cache_matches) {
        for (const auto& entry : entries) {
            if (entry.executor == nullptr || entry.context == nullptr
                || entry.active == nullptr
                || entry.executor->cohort_domain() != &jit_) {
                return 0U;
            }
        }
        region_members_.clear();
        region_contexts_.clear();
        region_start_instructions_.clear();
        region_native_entries_.clear();
        region_update_batches_.clear();
        region_members_.reserve(entries.size());
        region_contexts_.reserve(entries.size());
        region_start_instructions_.reserve(entries.size());
        region_native_entries_.reserve(entries.size());
        region_update_batches_.reserve(entries.size());
        try {
            for (const auto& entry : entries) {
                auto& executor = *static_cast<LlvmProcessExecutor*>(
                    entry.executor);
                executor.cohort_resume_mode_ = CohortResumeMode::prepare;
                static_cast<void>(executor.resume(
                    *entry.context, entry.start_instruction));
                executor.cohort_resume_result_ = { };
                executor.cohort_resume_result_.abi_version
                    = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
                executor.cohort_resume_result_.struct_size
                    = sizeof(executor.cohort_resume_result_);
                region_members_.push_back(&executor);
                region_contexts_.push_back(entry.context);
                region_start_instructions_.push_back(
                    entry.start_instruction);
                region_native_entries_.push_back(
                    compiler::JitProcessCohortResumeEntry {
                        executor.binding_, executor.runtime_, executor.frame_,
                        executor.cohort_resume_result_,
                        reinterpret_cast<std::uint8_t*>(entry.queued),
                        reinterpret_cast<std::uint8_t*>(
                            entry.waiting_on_static),
                        reinterpret_cast<std::uint8_t*>(entry.status),
                        entry.active });
                region_update_batches_.push_back({
                    executor.process_.id,
                    executor.direct_update_slot_views_,
                    executor.direct_update_active_words_
                });
            }
        } catch (...) {
            for (const auto& entry : entries) {
                static_cast<LlvmProcessExecutor*>(entry.executor)
                    ->cohort_resume_mode_ = CohortResumeMode::normal;
            }
            region_members_.clear();
            region_contexts_.clear();
            region_start_instructions_.clear();
            region_native_entries_.clear();
            region_update_batches_.clear();
            region_entries_identity_ = nullptr;
            return 0U;
        }
        region_entries_identity_ = entries.data();
    }

    region_active_update_batches_.clear();
    region_active_update_batches_.reserve(active_indices.size());
    for (const auto index : active_indices) {
        if (*entries[index].active == 0U
            || entries[index].executor != region_members_[index]
            || entries[index].context != region_contexts_[index]
            || entries[index].start_instruction
                != region_start_instructions_[index]) {
            return 0U;
        }
        auto& executor = *region_members_[index];
        executor.callback_state_.context = entries[index].context;
        executor.callback_state_.supports_direct_word_updates
            = entries[index].context->supports_direct_word_updates();
        executor.callback_state_.failure = { };
        invalidate_signal_read_cache(executor.callback_state_);
        executor.runtime_.context = &executor.callback_state_;
        executor.runtime_.flags
            = entries[index].context->execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS
            : 0U;
        executor.cohort_resume_result_ = { };
        executor.cohort_resume_result_.abi_version
            = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V1;
        executor.cohort_resume_result_.struct_size
            = sizeof(executor.cohort_resume_result_);
        region_native_entries_[index].failure = { };
        region_active_update_batches_.push_back(
            region_update_batches_[index]);
    }

    const auto scanned = jit_.resume_region_prevalidated(
        region_native_entries_, active_indices);
    const auto active_end = std::ranges::lower_bound(
        active_indices, scanned);
    const auto executed_active = static_cast<std::size_t>(
        std::distance(active_indices.begin(), active_end));
    const bool all_wait = std::ranges::all_of(
        active_indices.first(executed_active),
        [&](const auto index) {
            const auto& executor = *region_members_[index];
            return region_native_entries_[index].status
                    == FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY
                && !region_native_entries_[index].failure
                && !executor.callback_state_.failure;
        });
    bool updates_flushed { };
    if (all_wait && !jit_process_profile().enabled) {
        try {
            for (const auto index
                : active_indices.first(executed_active)) {
                region_members_[index]->flush_buffered_logic9_updates(
                    *entries[index].context);
            }
            updates_flushed
                = entries[active_indices.front()].context
                      ->write_validated_update_slot_batches(
                          std::span {
                              region_active_update_batches_.data(),
                              executed_active
                          });
        } catch (...) {
            entries[active_indices.front()].failure
                = std::current_exception();
            return scanned;
        }
    }

    for (const auto index : active_indices.first(executed_active)) {
        auto& entry = entries[index];
        auto& executor = *region_members_[index];
        const auto status = static_cast<compiler::JitResumeStatus>(
            region_native_entries_[index].status);
        const bool callback_failed
            = static_cast<bool>(executor.callback_state_.failure);
        if (all_wait && !jit_process_profile().enabled) {
            try {
                if (!updates_flushed) {
                    executor.flush_update_words(*entry.context);
                }
                if (!executor.active_container_object_aliases_.empty()) {
                    executor.discard_container_object_aliases();
                }
                const auto& result = executor.cohort_resume_result_;
                if (result.instruction >= executor.process_.operations.size()
                    || executor.frame_.program_counter
                        != result.instruction + 1U) {
                    throw compiler::LlvmJitError(
                        "compiled region process returned an invalid "
                        "WaitSensitivity boundary");
                }
                entry.result = runtime::simir::ProcessResumeResult {
                    result.instruction, executor.frame_.program_counter
                };
                entry.result.external.kind
                    = runtime::simir::ExternalSuspendKind::wait_sensitivity;
                continue;
            } catch (...) {
                entry.failure = std::current_exception();
                return index + 1U;
            }
        }
        executor.cohort_resume_status_
            = region_native_entries_[index].status;
        executor.cohort_resume_failure_
            = callback_failed
            ? executor.callback_state_.failure
            : region_native_entries_[index].failure;
        executor.cohort_resume_mode_ = CohortResumeMode::consume;
        try {
            entry.result = executor.resume(
                *entry.context, entry.start_instruction);
        } catch (...) {
            entry.failure = std::current_exception();
            return index + 1U;
        }
        (void)status;
    }
    return scanned;
}

[[nodiscard]] bool
LlvmProcessExecutor::cohort_manages_process_state() const noexcept
{
    static_assert(sizeof(bool) == sizeof(std::uint8_t));
    static_assert(sizeof(runtime::simir::ProcessStatus)
        == sizeof(std::uint8_t));
    static const bool enabled
        = std::getenv("FSIM_DISABLE_COHORT_PROCESS_STATE") == nullptr;
    return enabled;
}

[[nodiscard]] const void* LlvmProcessExecutor::cohort_domain() const noexcept
{
    return &jit_;
}

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::read_register(
    const runtime::simir::RegisterId id,
    const std::size_t width) const
{
    const auto& layout = layout_;
    const auto resolved_width = id < layout.register_count
            && width == runtime::simir::ProcessExecutor::native_register_width
        ? layout.register_widths[id]
        : width;
    if (id >= layout.register_count
        || layout.register_widths[id] != resolved_width) {
        const auto layout_width = id < layout.register_count
            ? std::to_string(layout.register_widths[id])
            : std::string { "<missing>" };
        throw compiler::LlvmJitError {
            "compiled process '" + process_.name
            + "' debug-register request " + std::to_string(id) + " width "
            + std::to_string(resolved_width)
            + " does not match frame layout count "
            + std::to_string(layout.register_count) + " width " + layout_width
        };
    }
    if (resolved_width == 0) {
        return PackedLogic4 { };
    }
    if (layout.tracks_register_initialization
        && register_initialized_[id] == 0) {
        throw std::logic_error {
            "compiled process debug local has not been initialized"
        };
    }
    const auto kind = process_.register_value_kinds.empty()
        ? runtime::simir::ValueKind::logic4
        : process_.register_value_kinds[id];
    const auto offset = layout.register_word_offsets[id];
    const auto words = (resolved_width + 63U) / 64U;
    if (kind == runtime::simir::ValueKind::logic9) {
        PackedLogic4 result { resolved_width, runtime::Logic4::x };
        for (std::size_t bit = 0; bit < resolved_width; ++bit) {
            const auto word = bit / 64U;
            const auto mask = std::uint64_t { 1 } << (bit % 64U);
            const auto encoded = static_cast<std::uint8_t>(
                ((register_aval_[offset + word] & mask) != 0U ? 1U : 0U)
                | ((register_bval_[offset + word] & mask) != 0U ? 2U : 0U)
                | ((register_logic9_plane2_[offset + word] & mask) != 0U
                        ? 4U
                        : 0U)
                | ((register_logic9_plane3_[offset + word] & mask) != 0U
                        ? 8U
                        : 0U));
            result.set_logic9(bit, static_cast<runtime::Logic9>(encoded));
        }
        return result;
    }
    return PackedLogic4::from_word_planes(
        resolved_width,
        std::span<const std::uint64_t> { register_aval_ }.subspan(offset, words),
        std::span<const std::uint64_t> { register_bval_ }.subspan(offset, words));
}

[[nodiscard]] PackedLogic4 LlvmProcessExecutor::snapshot_register(
    const runtime::simir::RegisterId id) const
{
    const auto& layout = layout_;
    if (id >= layout.register_count) {
        throw compiler::LlvmJitError {
            "compiled automatic-frame register is outside the frame layout"
        };
    }
    if (register_initialized_[id] == 0) {
        return PackedLogic4 {
            layout.register_widths[id], runtime::Logic4::x
        };
    }
    return read_register(
        id, runtime::simir::ProcessExecutor::native_register_width);
}

void LlvmProcessExecutor::write_register(
    const runtime::simir::RegisterId id,
    const PackedLogic4& value)
{
    const auto& layout = layout_;
    if (id >= layout.register_count
        || layout.register_widths[id] != value.width()) {
        throw compiler::LlvmJitError {
            "compiled process register write is out of range"
        };
    }
    if (value.width() == 0) {
        register_initialized_[id] = 1;
        return;
    }
    const auto kind = process_.register_value_kinds.empty()
        ? runtime::simir::ValueKind::logic4
        : process_.register_value_kinds[id];
    const auto offset = layout.register_word_offsets[id];
    if (kind == runtime::simir::ValueKind::logic9) {
        const auto words = (value.width() + 63U) / 64U;
        if (value.is_logic9()) {
            std::ranges::copy(
                value.logic9_plane_words(0), register_aval_.begin() + offset);
            std::ranges::copy(
                value.logic9_plane_words(1), register_bval_.begin() + offset);
            std::ranges::copy(
                value.logic9_plane_words(2),
                register_logic9_plane2_.begin() + offset);
            std::ranges::copy(
                value.logic9_plane_words(3),
                register_logic9_plane3_.begin() + offset);
            register_initialized_[id] = 1;
            return;
        }
        const auto word_offset = static_cast<std::ptrdiff_t>(offset);
        const auto word_count = static_cast<std::ptrdiff_t>(words);
        std::ranges::fill_n(register_aval_.begin() + word_offset, word_count, 0U);
        std::ranges::fill_n(register_bval_.begin() + word_offset, word_count, 0U);
        std::ranges::fill_n(
            register_logic9_plane2_.begin() + word_offset, word_count, 0U);
        std::ranges::fill_n(
            register_logic9_plane3_.begin() + word_offset, word_count, 0U);
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const auto encoded = static_cast<std::uint8_t>(value.get_logic9(bit));
            const auto word = bit / 64U;
            const auto mask = std::uint64_t { 1 } << (bit % 64U);
            if ((encoded & 1U) != 0U) {
                register_aval_[offset + word] |= mask;
            }
            if ((encoded & 2U) != 0U) {
                register_bval_[offset + word] |= mask;
            }
            if ((encoded & 4U) != 0U) {
                register_logic9_plane2_[offset + word] |= mask;
            }
            if ((encoded & 8U) != 0U) {
                register_logic9_plane3_[offset + word] |= mask;
            }
        }
    } else {
        std::ranges::copy(
            value.aval_words(), register_aval_.begin() + offset);
        std::ranges::copy(
            value.bval_words(), register_bval_.begin() + offset);
    }
    register_initialized_[id] = 1;
}

template <typename Boundary>
void LlvmProcessExecutor::require_boundary(
    const runtime::simir::InstructionIndex instruction,
    const std::string_view status) const
{
    if (!fsim::runtime::simir::operation_holds<Boundary>(
            process_.operations[instruction])) {
        throw compiler::LlvmJitError(
            "compiled process reported " + std::string { status }
            + " at the wrong SimIR instruction");
    }
}

void LlvmProcessExecutor::capture_failure(CallbackState& state) noexcept
{
    if (!state.failure) {
        state.failure = std::current_exception();
    }
}

void LlvmProcessExecutor::invalidate_signal_read_cache(
    CallbackState& state) noexcept
{
    state.signal_read_cache_ids.fill(
        std::numeric_limits<std::uint32_t>::max());
}

bool LlvmProcessExecutor::buffer_logic9_update(
    const runtime::simir::SignalId signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v1& value)
{
    const auto found = buffered_logic9_updates_.size() == 1U
        ? buffered_logic9_updates_.begin()
        : std::ranges::lower_bound(
            buffered_logic9_updates_, signal, { },
            &BufferedLogic9Update::signal);
    if (found == buffered_logic9_updates_.end()
        || found->signal != signal) {
        return false;
    }
    if (width == 0U || offset > found->width
        || width > found->width - offset) {
        throw std::logic_error {
            "invalid generated Logic9 buffered update"
        };
    }
    const auto source_mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    const auto target_mask = source_mask << offset;
    for (std::size_t plane = 0; plane < found->planes.size(); ++plane) {
        const auto shifted = (value.planes[plane] & source_mask) << offset;
        found->planes[plane]
            = (found->planes[plane] & ~target_mask)
            | (shifted & target_mask);
    }
    found->mask |= target_mask;
    return true;
}

void LlvmProcessExecutor::flush_buffered_logic9_updates(
    runtime::simir::ProcessExecutionContext& context)
{
    if (!buffered_logic9_update_views_.empty()
        && context.write_validated_logic9_update_batch({
            process_.id, buffered_logic9_update_views_ })) {
        return;
    }
    const auto direct_owners = context.direct_single_driver_processes();
    const bool direct_comparison_safe
        = context.direct_update_domain() != nullptr;
    for (const auto& slot : buffered_logic9_update_views_) {
        if (slot.mask == nullptr || slot.planes == nullptr) {
            continue;
        }
        auto remaining = std::exchange(*slot.mask, UINT64_C(0));
        if (remaining == 0U) {
            continue;
        }
        const auto full_mask = slot.width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << slot.width) - UINT64_C(1);
        remaining &= full_mask;
        if (direct_comparison_safe && slot.signal < direct_owners.size()
            && direct_owners[slot.signal] == process_.id) {
            const auto current = context.read_signal_logic9_word(slot.signal);
            if (current.width == slot.width) {
                std::uint64_t changed { };
                for (std::size_t plane = 0; plane < 4U; ++plane) {
                    changed |= current.planes[plane] ^ slot.planes[plane];
                }
                remaining &= changed;
                if (remaining == 0U) {
                    continue;
                }
            }
        }
        if (remaining == full_mask) {
            context.write_update(
                slot.signal,
                PackedLogic4::from_logic9_word({
                    slot.width,
                    { slot.planes[0], slot.planes[1],
                        slot.planes[2], slot.planes[3] }
                }));
            continue;
        }
        std::uint32_t bit { };
        while (bit < slot.width) {
            while (bit < slot.width
                && ((remaining >> bit) & UINT64_C(1)) == 0U) {
                ++bit;
            }
            if (bit == slot.width) {
                break;
            }
            const auto offset = bit;
            while (bit < slot.width
                && ((remaining >> bit) & UINT64_C(1)) != 0U) {
                ++bit;
            }
            const auto run_width = bit - offset;
            const auto run_mask = run_width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (UINT64_C(1) << run_width) - UINT64_C(1);
            context.write_update_slice(
                slot.signal,
                PackedLogic4::from_logic9_word({
                    run_width,
                    { (slot.planes[0] >> offset) & run_mask,
                        (slot.planes[1] >> offset) & run_mask,
                        (slot.planes[2] >> offset) & run_mask,
                        (slot.planes[3] >> offset) & run_mask }
                }),
                offset);
        }
    }
}

void LlvmProcessExecutor::flush_update_words(
    runtime::simir::ProcessExecutionContext& context)
{
    const auto direct_owners = context.direct_single_driver_processes();
    const auto direct_aval = context.direct_signal_aval();
    const auto direct_bval = context.direct_signal_bval();
    const auto direct_wide_aval = context.direct_wide_signal_aval();
    const auto direct_wide_bval = context.direct_wide_signal_bval();
    const auto direct_wide_offsets = context.direct_wide_signal_offsets();
    const auto unchanged_direct_update = [&](
        const runtime::simir::SignalId signal,
        const fsim_jit_update_slot_v1& slot) {
        if (signal >= direct_owners.size()
            || direct_owners[signal] != process_.id) {
            return false;
        }
        if (slot.width <= 64U) {
            return signal < direct_aval.size()
                && signal < direct_bval.size()
                && (((slot.aval ^ direct_aval[signal]) & slot.mask) == 0U)
                && (((slot.bval ^ direct_bval[signal]) & slot.mask) == 0U);
        }
        if (signal >= direct_wide_offsets.size()
            || direct_wide_aval.size() != direct_wide_bval.size()) {
            return false;
        }
        const auto offset = direct_wide_offsets[signal];
        if (offset > direct_wide_aval.size()
            || slot.word_count > direct_wide_aval.size() - offset) {
            return false;
        }
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto mask = slot.wide_mask[word];
            if ((((slot.wide_aval[word] ^ direct_wide_aval[offset + word])
                     & mask)
                    != 0U)
                || (((slot.wide_bval[word]
                          ^ direct_wide_bval[offset + word])
                         & mask)
                    != 0U)) {
                return false;
            }
        }
        return true;
    };
    const auto append_runs = [&](const runtime::simir::SignalId signal,
                                 const std::uint32_t word_offset,
                                 const std::uint32_t word_width,
                                 const std::uint64_t aval,
                                 const std::uint64_t bval,
                                 std::uint64_t remaining,
                                 const bool whole_signal) {
        const auto full_mask = word_width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t { 1 } << word_width) - 1U;
        remaining &= full_mask;
        if (remaining == 0U) {
            return;
        }
        if (whole_signal && remaining == full_mask) {
            pending_update_words_.push_back({
                signal,
                { word_width, aval & full_mask, bval & full_mask },
                0U,
                false
            });
            return;
        }
        std::uint32_t bit { };
        while (bit < word_width) {
            while (bit < word_width
                && ((remaining >> bit) & UINT64_C(1)) == 0U) {
                ++bit;
            }
            if (bit == word_width) {
                break;
            }
            const auto offset = bit;
            while (bit < word_width
                && ((remaining >> bit) & UINT64_C(1)) != 0U) {
                ++bit;
            }
            const auto run_width = bit - offset;
            const auto run_mask = run_width == 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (std::uint64_t { 1 } << run_width) - 1U;
            pending_update_words_.push_back({
                signal,
                { run_width,
                    (aval >> offset) & run_mask,
                    (bval >> offset) & run_mask },
                word_offset + offset,
                true
            });
        }
    };
    const auto consume_index = [&](const std::size_t index) {
        auto& slot = direct_update_slots_[index];
        if (slot.active == 0U) {
            return;
        }
        slot.active = 0U;
        const auto signal = direct_update_signals_[index];
        const auto width = slot.width;
        if (unchanged_direct_update(signal, slot)) {
            if (width <= 64U) {
                slot.mask = 0U;
            } else {
                std::fill_n(slot.wide_mask, slot.word_count, UINT64_C(0));
            }
            return;
        }
        if (width <= 64U) {
            const auto remaining = slot.mask;
            slot.mask = 0U;
            append_runs(
                signal, 0U, width, slot.aval, slot.bval, remaining, true);
            return;
        }
        for (std::uint32_t word = 0; word < slot.word_count; ++word) {
            const auto offset = word * 64U;
            const auto word_width = std::min(64U, width - offset);
            const auto remaining = slot.wide_mask[word];
            slot.wide_mask[word] = 0U;
            append_runs(
                signal,
                offset,
                word_width,
                slot.wide_aval[word],
                slot.wide_bval[word],
                remaining,
                false);
        }
    };
    if (direct_update_active_words_.empty()) {
        for (std::size_t index = 0;
             index < direct_update_slots_.size(); ++index) {
            consume_index(index);
        }
    } else {
        for (std::size_t word_index = 0;
             word_index < direct_update_active_words_.size(); ++word_index) {
            auto active = direct_update_active_words_[word_index];
            while (active != 0U) {
                const auto bit = static_cast<std::size_t>(
                    std::countr_zero(active));
                const auto index = word_index * 64U + bit;
                if (index < direct_update_slots_.size()) {
                    consume_index(index);
                }
                active &= active - UINT64_C(1);
            }
        }
        std::ranges::fill(direct_update_active_words_, UINT64_C(0));
    }
    if (!pending_update_words_.empty()) {
        try {
            context.write_validated_update_words(pending_update_words_);
        } catch (...) {
            pending_update_words_.clear();
            throw;
        }
        pending_update_words_.clear();
    }
    flush_buffered_logic9_updates(context);
}

std::uint32_t LlvmProcessExecutor::mapped_signal_sparse(
    const CallbackState& state, const std::uint32_t signal)
{
    const auto found = std::ranges::lower_bound(
        state.signal_remap, signal, { }, &SignalRemap::value_type::first);
    if (found == state.signal_remap.end() || found->first != signal) {
        return signal;
    }
    return found->second;
}

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
        const auto logic9 = !state.signal_value_kinds.empty()
            && state.signal_value_kinds[actual_signal]
                == runtime::simir::ValueKind::logic9;
        if (logic9 != (logic9_plane2 != nullptr && logic9_plane3 != nullptr)
            || (!logic9
                && (logic9_plane2 != nullptr || logic9_plane3 != nullptr))) {
            throw std::logic_error(
                "generated arbitrary-width signal-read planes disagree with the signal kind");
        }
        state.context->read_signal_planes(
            actual_signal,
            { aval, word_count },
            { bval, word_count },
            logic9 ? std::span<std::uint64_t> { logic9_plane2, word_count }
                   : std::span<std::uint64_t> { },
            logic9 ? std::span<std::uint64_t> { logic9_plane3, word_count }
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
        const auto logic9 = !state.signal_value_kinds.empty()
            && state.signal_value_kinds[actual_signal]
                == runtime::simir::ValueKind::logic9;
        if (logic9 != (logic9_plane2 != nullptr && logic9_plane3 != nullptr)
            || (!logic9
                && (logic9_plane2 != nullptr || logic9_plane3 != nullptr))) {
            throw std::logic_error(
                "generated arbitrary-width signal-write planes disagree with the signal kind");
        }
        if (!logic9 && state.supports_direct_word_updates
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
        auto value = logic9
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
    const auto& direct = state.direct_signal_logic9_planes;
    if (signal >= direct[0].size()
        || signal >= direct[1].size()
        || signal >= direct[2].size()
        || signal >= direct[3].size()) {
        clear_logic9_word(result);
        try {
            throw std::out_of_range(
                "generated Logic9 read signal is outside direct storage");
        } catch (...) {
            capture_failure(state);
        }
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
