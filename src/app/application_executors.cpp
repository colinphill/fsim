// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <string>
#include <vector>

namespace fsim::app::application_detail {

#if defined(FSIM_HAS_LLVM)

namespace {

std::atomic_uint64_t required_direct_read_entry_resumes { };
std::atomic<std::atomic_uint64_t*> required_direct_read_entry_counter { };

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
        const runtime::simir::ProcessProgramView& process,
        const runtime::simir::ProcessId generated_process,
        const std::size_t direct_read_slots,
        const std::size_t direct_update_slots,
        const std::chrono::nanoseconds elapsed)
    {
        if (!enabled) {
            return;
        }
        auto& entry = entries[process.id()];
        if (entry.resumes == 0U) {
            entry.name = process.name();
            entry.generated_process = generated_process;
            entry.direct_read_slots = direct_read_slots;
            entry.direct_update_slots = direct_update_slots;
            entry.operations = process.operations().size();
            entry.static_waits = static_cast<std::size_t>(
                std::ranges::count_if(
                    process.operations(), [](const auto& operation) {
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
        std::cerr << "fsim-profile: jit-process-summary processes="
                  << ranked.size()
                  << " resumes=" << total_resumes
                  << " elapsed_ms="
                  << std::chrono::duration<double, std::milli>(
                         total_elapsed).count()
                  << '\n';
        for (std::size_t index = 0; index < count; ++index) {
            const auto& [id, entry] = ranked[index];
            std::cerr << "fsim-profile: jit-process id=" << id
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
        const runtime::simir::ProcessProgramView& process,
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
    const runtime::simir::ProcessProgramView& process_;
    runtime::simir::ProcessId generated_process_ { };
    std::size_t direct_read_slots_ { };
    std::size_t direct_update_slots_ { };
    bool enabled_ { };
    std::chrono::steady_clock::time_point begin_;
};

} // namespace

std::uint64_t required_direct_read_entry_resume_count() noexcept
{
    return required_direct_read_entry_resumes.load(
        std::memory_order_relaxed);
}

void set_required_direct_read_entry_counting_for_testing(
    const bool enabled) noexcept
{
    if (enabled) {
        required_direct_read_entry_resumes.store(
            0U, std::memory_order_relaxed);
        required_direct_read_entry_counter.store(
            &required_direct_read_entry_resumes,
            std::memory_order_release);
        return;
    }
    required_direct_read_entry_counter.store(
        nullptr, std::memory_order_release);
}

std::uint64_t LlvmProcessExecutor::next_instance_generation()
{
    static std::atomic_uint64_t next { 1U };
    const auto generation = next.fetch_add(1U, std::memory_order_relaxed);
    if (generation == 0U
        || generation == std::numeric_limits<std::uint64_t>::max()) {
        throw compiler::LlvmJitError(
            "compiled process executor generation exhausted");
    }
    return generation;
}

LlvmProcessExecutor::~LlvmProcessExecutor()
{
    if (cohort_binding_) {
        try {
            (void)jit_.release_cohort_binding(cohort_binding_);
        } catch (...) {
            // Destructors must not replace an in-flight simulation failure.
        }
    }
}

[[nodiscard]] runtime::simir::ProcessResumeResult LlvmProcessExecutor::resume(
    runtime::simir::ProcessExecutionContext& context,
    const runtime::simir::InstructionIndex start_instruction)
{
    std::optional<JitProcessProfileScope> process_profile;
    if (jit_process_profile().enabled) {
        process_profile.emplace(
            process_, generated_process_, direct_read_signals_.size(),
            direct_update_slots_.size());
    }
    const bool consuming_cohort
        = cohort_resume_mode_ == CohortResumeMode::consume;
    const auto* const prior_prepared_domain = prepared_cohort_domain_;
    const auto* const current_domain = consuming_cohort
        ? prior_prepared_domain : context.direct_update_domain();
    if (!consuming_cohort) {
        prepared_cohort_domain_ = nullptr;
    }
    if (!consuming_cohort
        && frame_.program_counter != start_instruction) {
        const bool kernel_owned_callable_boundary = frame_.program_counter < operation_count_
            && [&] {
                   const auto& operation = process_.operations()[frame_.program_counter];
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
    if (callback_state.signal_callback_ids_are_actual
        != layout_.signal_callback_ids_are_actual) {
        throw compiler::LlvmJitError(
            "callback state capability differs from its compiled frame");
    }
    bool required_direct_read_entry_capable { };
    if (!consuming_cohort) {
        if (callback_state.executor == nullptr) {
            callback_state.executor = this;
            callback_state.process = process_;
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
        static const fsim_jit_services_v2 callback_services = [] {
            fsim_jit_services_v2 services { };
            services.abi_version = FSIM_JIT_SERVICES_ABI_VERSION_V2;
            services.struct_size = static_cast<std::uint32_t>(sizeof(services));
            services.read_signal = &LlvmProcessExecutor::read_signal;
            services.write_signal = &LlvmProcessExecutor::write_signal;
            services.assert_failed = &LlvmProcessExecutor::assert_failed;
            services.write_update = &LlvmProcessExecutor::write_update;
            services.write_after = &LlvmProcessExecutor::write_after;
            services.write_signal_slice = &LlvmProcessExecutor::write_signal_slice;
            services.write_update_slice = &LlvmProcessExecutor::write_update_slice;
            services.write_after_slice = &LlvmProcessExecutor::write_after_slice;
            services.signal_event = &LlvmProcessExecutor::signal_event;
            services.signal_last_value = &LlvmProcessExecutor::signal_last_value;
            services.signal_last_event = &LlvmProcessExecutor::signal_last_event;
            services.signal_active = &LlvmProcessExecutor::signal_active;
            services.signal_last_active = &LlvmProcessExecutor::signal_last_active;
            services.signal_driving = &LlvmProcessExecutor::signal_driving;
            services.signal_driving_value = &LlvmProcessExecutor::signal_driving_value;
            services.signal_driving_value_logic9
                = &LlvmProcessExecutor::signal_driving_value_logic9;
            services.read_simulation_time = &LlvmProcessExecutor::read_simulation_time;
            services.vital_timing_check = &LlvmProcessExecutor::vital_timing_check;
            services.vital_delay = &LlvmProcessExecutor::vital_delay;
            services.write_output = &LlvmProcessExecutor::write_output;
            services.schedule_output = &LlvmProcessExecutor::schedule_output;
            services.write_report = &LlvmProcessExecutor::write_report;
            services.write_formatted = &LlvmProcessExecutor::write_formatted;
            services.write_time = &LlvmProcessExecutor::write_time;
            services.install_monitor = &LlvmProcessExecutor::install_monitor;
            services.control_monitor = &LlvmProcessExecutor::control_monitor;
            services.random_value = &LlvmProcessExecutor::random_value;
            services.write_inertial = &LlvmProcessExecutor::write_inertial;
            services.write_inertial_slice = &LlvmProcessExecutor::write_inertial_slice;
            services.write_projected = &LlvmProcessExecutor::write_projected;
            services.write_projected_slice = &LlvmProcessExecutor::write_projected_slice;
            services.write_projected_waveform = &LlvmProcessExecutor::write_projected_waveform;
            services.write_projected_waveform_slice
                = &LlvmProcessExecutor::write_projected_waveform_slice;
            services.read_signal_logic9 = &LlvmProcessExecutor::read_signal_logic9;
            services.write_signal_logic9 = &LlvmProcessExecutor::write_signal_logic9;
            services.write_update_logic9 = &LlvmProcessExecutor::write_update_logic9;
            services.write_after_logic9 = &LlvmProcessExecutor::write_after_logic9;
            services.write_signal_slice_logic9 = &LlvmProcessExecutor::write_signal_slice_logic9;
            services.write_update_slice_logic9 = &LlvmProcessExecutor::write_update_slice_logic9;
            services.write_after_slice_logic9 = &LlvmProcessExecutor::write_after_slice_logic9;
            services.signal_last_value_logic9 = &LlvmProcessExecutor::signal_last_value_logic9;
            services.write_inertial_logic9 = &LlvmProcessExecutor::write_inertial_logic9;
            services.write_inertial_slice_logic9
                = &LlvmProcessExecutor::write_inertial_slice_logic9;
            services.write_projected_logic9 = &LlvmProcessExecutor::write_projected_logic9;
            services.write_projected_slice_logic9
                = &LlvmProcessExecutor::write_projected_slice_logic9;
            services.write_projected_waveform_logic9
                = &LlvmProcessExecutor::write_projected_waveform_logic9;
            services.write_projected_waveform_slice_logic9
                = &LlvmProcessExecutor::write_projected_waveform_slice_logic9;
            services.write_formatted_logic9 = &LlvmProcessExecutor::write_formatted_logic9;
            services.force_signal_slice = &LlvmProcessExecutor::force_signal_slice;
            services.force_signal_slice_logic9 = &LlvmProcessExecutor::force_signal_slice_logic9;
            services.release_signal_slice = &LlvmProcessExecutor::release_signal_slice;
            services.force_driver_signal_slice = &LlvmProcessExecutor::force_driver_signal_slice;
            services.force_driver_signal_slice_logic9
                = &LlvmProcessExecutor::force_driver_signal_slice_logic9;
            services.release_driver_signal_slice
                = &LlvmProcessExecutor::release_driver_signal_slice;
            services.load_string = &LlvmProcessExecutor::load_string;
            services.copy_string = &LlvmProcessExecutor::copy_string;
            services.read_string_object = &LlvmProcessExecutor::read_string_object;
            services.write_string_object = &LlvmProcessExecutor::write_string_object;
            services.concatenate_strings = &LlvmProcessExecutor::concatenate_strings;
            services.compare_strings = &LlvmProcessExecutor::compare_strings;
            services.string_length = &LlvmProcessExecutor::string_length;
            services.string_index = &LlvmProcessExecutor::string_index;
            services.string_replace_byte = &LlvmProcessExecutor::string_replace_byte;
            services.write_string_output = &LlvmProcessExecutor::write_string_output;
            services.file_open = &LlvmProcessExecutor::file_open;
            services.file_close = &LlvmProcessExecutor::file_close;
            services.file_write = &LlvmProcessExecutor::file_write;
            services.file_read_line = &LlvmProcessExecutor::file_read_line;
            services.file_end_of_file = &LlvmProcessExecutor::file_end_of_file;
            services.file_error = &LlvmProcessExecutor::file_error;
            services.container_operation = &LlvmProcessExecutor::container_operation;
            services.execute_signal_operation = &LlvmProcessExecutor::execute_signal_operation;
            services.container_read_word = &LlvmProcessExecutor::container_read_word;
            services.container_write_word = &LlvmProcessExecutor::container_write_word;
            services.container_read_packed = &LlvmProcessExecutor::container_read_packed;
            services.container_read_packed_index64
                = &LlvmProcessExecutor::container_read_packed_index64;
            services.container_write_packed = &LlvmProcessExecutor::container_write_packed;
            services.read_signal_packed = &LlvmProcessExecutor::read_signal_packed;
            services.write_signal_packed = &LlvmProcessExecutor::write_signal_packed;
            services.write_projected_signal_packed
                = &LlvmProcessExecutor::write_projected_signal_packed;
            services.read_signal_dynamic_part = &LlvmProcessExecutor::read_signal_dynamic_part;
            services.record_code_coverage_counter
                = &LlvmProcessExecutor::record_code_coverage_counter;
            services.sample_coverage = &LlvmProcessExecutor::sample_coverage;
            services.execute_class_property_operation
                = &LlvmProcessExecutor::execute_class_property_operation;
            services.query_event_triggered = &LlvmProcessExecutor::query_event_triggered;
            return services;
        }();
        runtime.abi_version = FSIM_JIT_RUNTIME_ABI_VERSION_V2;
        runtime.struct_size = static_cast<std::uint32_t>(sizeof(runtime));
        runtime.services = &callback_services;
        const auto direct_aval = context.direct_signal_aval();
        const auto direct_bval = context.direct_signal_bval();
        const auto direct_wide_aval = context.direct_wide_signal_aval();
        const auto direct_wide_bval = context.direct_wide_signal_bval();
        const auto direct_wide_logic9_plane2
            = context.direct_wide_signal_logic9_plane2();
        const auto direct_wide_logic9_plane3
            = context.direct_wide_signal_logic9_plane3();
        const auto direct_wide_offsets = context.direct_wide_signal_offsets();
        const bool supports_direct_planes
            = (!direct_read_signals_.empty() || !direct_update_signals_.empty())
            && !direct_aval.empty()
            && direct_aval.size() == direct_bval.size()
            && direct_aval.size()
                <= std::numeric_limits<std::uint32_t>::max();
        const bool direct_read_layout_valid
            = !direct_read_signals_.empty()
            && direct_read_signals_.size()
                == layout_.direct_read_signals.size()
            && runtime_direct_read_signals_.size()
                == direct_read_signals_.size();
        application_detail::DirectReadPlaneLayoutKey direct_read_plane_key;
        direct_read_plane_key.buffers = { {
            { direct_aval.data(), direct_aval.size() },
            { direct_bval.data(), direct_bval.size() },
            { callback_state.direct_signal_logic9_planes[0].data(),
                callback_state.direct_signal_logic9_planes[0].size() },
            { callback_state.direct_signal_logic9_planes[1].data(),
                callback_state.direct_signal_logic9_planes[1].size() },
            { callback_state.direct_signal_logic9_planes[2].data(),
                callback_state.direct_signal_logic9_planes[2].size() },
            { callback_state.direct_signal_logic9_planes[3].data(),
                callback_state.direct_signal_logic9_planes[3].size() },
            { direct_wide_aval.data(), direct_wide_aval.size() },
            { direct_wide_bval.data(), direct_wide_bval.size() },
            { direct_wide_logic9_plane2.data(),
                direct_wide_logic9_plane2.size() },
            { direct_wide_logic9_plane3.data(),
                direct_wide_logic9_plane3.size() },
            { direct_wide_offsets.data(), direct_wide_offsets.size() }
        } };
        const auto direct_read_capability_key
            = context.direct_signal_read_capability_key();
        const bool direct_read_map_is_static
            = direct_read_layout_valid
            && direct_read_map_cache_.refresh(
                direct_read_signals_,
                runtime_direct_read_signals_,
                direct_read_capability_key,
                direct_read_plane_key,
                supports_direct_planes,
                direct_aval.size(),
                [&context](const std::uint32_t signal) {
                    return context.supports_direct_signal_read(signal);
                });
        const bool supports_direct_reads = direct_read_layout_valid;
        const auto* const runtime_read_signal_map
            = direct_read_map_is_static
            ? direct_read_signals_.data()
            : runtime_direct_read_signals_.data();
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
            ? runtime_read_signal_map
            : nullptr;
        runtime.direct_read_signal_count = supports_direct_reads
            ? static_cast<std::uint32_t>(runtime_direct_read_signals_.size())
            : 0U;
        runtime.direct_signal_count = supports_direct_planes
            ? static_cast<std::uint32_t>(direct_aval.size())
            : 0U;
        runtime.direct_signal_reserved = 0U;
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
        runtime.context = &callback_state;
        runtime.code_coverage_hit_counters
            = code_coverage_hit_counters_.empty()
            ? nullptr
            : code_coverage_hit_counters_.data();
        runtime.code_coverage_hit_count = static_cast<std::uint32_t>(
            code_coverage_hit_counters_.size());
        const auto direct_code_coverage_counters
            = context.direct_code_coverage_counters();
        runtime.code_coverage_counter_values
            = direct_code_coverage_counters.empty()
            ? nullptr
            : direct_code_coverage_counters.data();
        runtime.code_coverage_counter_count
            = direct_code_coverage_counters.size()
                    <= std::numeric_limits<std::uint32_t>::max()
            ? static_cast<std::uint32_t>(
                  direct_code_coverage_counters.size())
            : 0U;
        runtime.flags = context.execution_points_enabled()
            ? FSIM_JIT_RUNTIME_FLAG_DEBUG_POINTS_V2
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
        const bool owner_changed = current_domain != prior_prepared_domain;
        if (owner_changed
            || writer_revision != direct_update_writer_revision_) {
            const auto stable_owners
                = context.stable_single_writer_processes();
            const auto refreshed_direct_aval = context.direct_signal_aval();
            const auto refreshed_direct_bval = context.direct_signal_bval();
            const bool enabled = stable_direct_update_suppression_allowed_;
            for (std::size_t index = 0;
                index < direct_update_slots_.size(); ++index) {
                auto& slot = direct_update_slots_[index];
                if (owner_changed) {
                    slot.reserved &= ~1U;
                }
                const auto signal = direct_update_signals_[index];
                if (signal < signal_value_kinds_.size()
                    && signal_value_kinds_[signal]
                        == runtime::simir::ValueKind::logic9) {
                    slot.reserved &= ~1U;
                    continue;
                }
                const bool stable = enabled && slot.width <= 64U
                    && signal < stable_owners.size()
                    && signal < refreshed_direct_aval.size()
                    && signal < refreshed_direct_bval.size()
                    && context.supports_direct_signal_read(signal);
                const bool owned = stable
                    && stable_owners[signal] == process_.id();
                if (!owned) {
                    slot.reserved &= ~1U;
                    continue;
                }
                if ((slot.reserved & 1U) == 0U) {
                    slot.aval = refreshed_direct_aval[signal];
                    slot.bval = refreshed_direct_bval[signal];
                }
                slot.reserved |= 1U;
            }
            direct_update_writer_revision_ = writer_revision;
        }
        prepared_cohort_domain_ = current_domain;
        required_direct_read_entry_capable
            = required_direct_read_binding_
            && direct_read_map_is_static
            && direct_read_capability_key.cacheable()
            && (!signal_remap_ || signal_remap_->empty())
            && runtime.flags == 0U
            && callback_state.supports_direct_word_updates
            && (layout_.direct_update_signals.empty()
                || (runtime.direct_update_slots != nullptr
                    && runtime.direct_update_active_words != nullptr))
            && runtime.direct_read_signals
                == direct_read_signals_.data()
            && runtime.direct_read_signal_count
                == direct_read_signals_.size()
            && std::ranges::equal(
                layout_.direct_read_signals,
                direct_read_signals_)
            && runtime.code_coverage_hit_counters == nullptr
            && runtime.code_coverage_hit_count == 0U
            && runtime.code_coverage_counter_values == nullptr
            && runtime.code_coverage_counter_count == 0U;
    }

    if (cohort_resume_mode_ == CohortResumeMode::prepare) {
        cohort_resume_mode_ = CohortResumeMode::normal;
        return { };
    }

    const auto flush_updates = [&] {
        flush_buffered_updates(context, !consuming_cohort);
    };

    auto& result = consuming_cohort
        ? cohort_resume_result_
        : resume_result_;
    if (!consuming_cohort) {
        result = { };
        result.abi_version = FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2;
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
            if (required_direct_read_entry_capable) {
                struct FrameLayoutRestore {
                    fsim_jit_frame_v2& frame;
                    std::uint64_t layout_id_low;
                    std::uint64_t layout_id_high;
                    ~FrameLayoutRestore()
                    {
                        frame.layout_id_low = layout_id_low;
                        frame.layout_id_high = layout_id_high;
                    }
                } restore {
                    frame_, frame_.layout_id_low, frame_.layout_id_high
                };
                frame_.layout_id_low
                    = required_direct_read_layout_id_low_;
                frame_.layout_id_high
                    = required_direct_read_layout_id_high_;
                if (!required_direct_read_lease_) {
                    const auto lease
                        = jit_.bind_required_direct_read_prevalidated(
                            required_direct_read_binding_, runtime, frame_,
                            result);
                    if (lease) {
                        required_direct_read_lease_ = *lease;
                    }
                }
                if (required_direct_read_lease_) {
                    const auto required_status
                        = jit_.resume_required_direct_read_prevalidated(
                            required_direct_read_lease_,
                            required_direct_read_binding_, runtime, frame_,
                            result);
                    if (required_status) {
                        if (auto* const counter
                            = required_direct_read_entry_counter.load(
                                std::memory_order_acquire)) {
                            counter->fetch_add(
                                1U, std::memory_order_relaxed);
                        }
                        return *required_status;
                    }
                }
                required_direct_read_lease_ = { };
            } else {
                required_direct_read_lease_ = { };
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
                    process_.id(),
                    error.instruction(),
                    "branch condition is unknown or high impedance");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_operand_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "VHDL integer operand contains an unknown or "
                    "high-impedance value");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_overflow:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "VHDL integer arithmetic overflow");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_division_by_zero:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "VHDL integer division by zero");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_negative_exponent:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "VHDL integer exponent must be nonnegative");
            case compiler::JitGeneratedRuntimeErrorReason::
                integer_subtype_range:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "VHDL integer subtype range check failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                dynamic_index_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "dynamic packed index contains an unknown or "
                    "high-impedance value");
            case compiler::JitGeneratedRuntimeErrorReason::
                dynamic_index_range:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "dynamic packed index is outside the declared range");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_unknown:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "call-stack pointer or return target is unknown");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_overflow:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "call-stack capacity is exhausted");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_underflow:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "call-stack underflow");
            case compiler::JitGeneratedRuntimeErrorReason::
                call_stack_target:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "call-stack return target is invalid");
            case compiler::JitGeneratedRuntimeErrorReason::
                string_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "mutable string runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                file_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "text file runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                container_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "bounded container runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                signal_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "exact-width signal runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                coverage_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "code coverage counter runtime callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                native_service_callback_failure:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "native SimIR service callback failed");
            case compiler::JitGeneratedRuntimeErrorReason::
                fused_activation_invalid:
                throw runtime::simir::InterpreterError(
                    process_.id(),
                    error.instruction(),
                    "fused member activation bitmap is invalid");
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
        if (has_container_registers_
            && !active_container_object_aliases_.empty()) {
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
    if (has_container_registers_
        && !active_container_object_aliases_.empty()) {
        discard_container_object_aliases();
    }
    if (result.instruction >= operation_count_) {
        throw compiler::LlvmJitError(
            "compiled process returned an invalid boundary instruction");
    }
    if (status == compiler::JitResumeStatus::wait_sensitivity) {
        if (frame_.program_counter != result.instruction + 1U) {
            throw compiler::LlvmJitError(
                "compiled process returned a non-sequential WaitSensitivity PC");
        }
        auto resumed = runtime::simir::ProcessResumeResult {
            result.instruction, frame_.program_counter
        };
        resumed.external.kind
            = validated_static_sensitivity_
            ? runtime::simir::ExternalSuspendKind::validated_wait_sensitivity
            : runtime::simir::ExternalSuspendKind::wait_sensitivity;
        return resumed;
    }

    switch (status) {
    case compiler::JitResumeStatus::completed:
        require_boundary<runtime::simir::Halt>(
            result.instruction, "completion");
        break;
    case compiler::JitResumeStatus::assertion_failed: {
        const auto* assertion = fsim::runtime::simir::operation_get_if<runtime::simir::Assert>(
            &process_.operations()[result.instruction]);
        if (assertion != nullptr) {
            throw runtime::simir::AssertionError(
                process_.id(),
                result.instruction,
                assertion->message.empty()
                    ? "assertion failed"
                    : assertion->message,
                assertion->severity,
                assertion->source);
        }
        const auto* report = fsim::runtime::simir::operation_get_if<runtime::simir::Report>(
            &process_.operations()[result.instruction]);
        if (report != nullptr
            && report->severity
                == runtime::simir::AssertionSeverity::failure) {
            throw runtime::simir::AssertionError(
                process_.id(),
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
            &process_.operations()[result.instruction]);
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
            &process_.operations()[result.instruction]);
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
        const auto& operation = process_.operations()[result.instruction];
        const auto* call = fsim::runtime::simir::operation_get_if<
            runtime::simir::Call>(&operation);
        const auto* return_operation = fsim::runtime::simir::operation_get_if<
            runtime::simir::Return>(&operation);
        const auto* read_signal = fsim::runtime::simir::operation_get_if<
            runtime::simir::ReadSignal>(&operation);
        const auto* last_value = fsim::runtime::simir::operation_get_if<
            runtime::simir::SignalLastValue>(&operation);
        const auto* driving_value = fsim::runtime::simir::operation_get_if<
            runtime::simir::SignalDrivingValue>(&operation);
        const auto* report = fsim::runtime::simir::operation_get_if<
            runtime::simir::Report>(&operation);
        const auto wide_attribute_boundary = [&](const auto* attribute) {
            return attribute != nullptr
                && attribute->signal < signal_widths_.size()
                && signal_widths_[attribute->signal] > 64U;
        };
        const auto host_boundary = (read_signal != nullptr
                                       && read_signal->kind
                                           != runtime::simir::SignalReadKind::current)
            || wide_attribute_boundary(last_value)
            || wide_attribute_boundary(driving_value)
            || (report != nullptr
                && report->severity
                    == runtime::simir::AssertionSeverity::failure)
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
                runtime::simir::ScopeRandomize>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::FormatDisplay>(operation)
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
                runtime::simir::VhdlPslApi>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlAssertApi>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageControl>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::CoverageAccess>(operation)
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
                runtime::simir::VhdlEnvironmentTime>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentTimeToString>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentDirectory>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentGetenv>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentCallPath>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlEnvironmentGetCallPath>(operation)
            || fsim::runtime::simir::operation_holds<
                runtime::simir::VhdlReflectionApi>(operation)
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
    const auto& boundary_operation = process_.operations()[result.instruction];
    const auto* boundary_call = fsim::runtime::simir::operation_get_if<
        runtime::simir::Call>(&boundary_operation);
    const auto* boundary_return = fsim::runtime::simir::operation_get_if<
        runtime::simir::Return>(&boundary_operation);
    const bool callable_boundary =
        status == compiler::JitResumeStatus::simir_boundary &&
        ((boundary_call != nullptr && boundary_call->stack.capacity == 0) ||
         (boundary_return != nullptr && boundary_return->stack.capacity == 0) ||
         fsim::runtime::simir::operation_holds<
             runtime::simir::CallableFramePush>(boundary_operation) ||
         fsim::runtime::simir::operation_holds<
             runtime::simir::CallableFramePop>(boundary_operation));
    const auto expected_program_counter =
        callable_boundary ? result.instruction : result.instruction + 1U;
    if (frame_.program_counter != expected_program_counter) {
      throw compiler::LlvmJitError(
          "compiled process returned a non-sequential boundary PC");
    }
    return runtime::simir::ProcessResumeResult{result.instruction,
                                               frame_.program_counter};
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_cohort(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries) {
  return resume_cohort_impl(entries);
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_ordered_cohort(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries) {
  return resume_cohort_impl(entries, true);
}

[[nodiscard]] std::size_t LlvmProcessExecutor::resume_cohort_impl(
    const std::span<runtime::simir::ProcessCohortResumeEntry> entries,
    const bool preserve_publication_order) {
  if (entries.size() < 2U) {
    return 0U;
  }
  for (const auto &entry : entries) {
    if (entry.executor == nullptr || entry.executor->cohort_domain() != &jit_ ||
        entry.context == nullptr) {
      return 0U;
    }
  }
  static const bool bound_cohort_enabled =
      std::getenv("FSIM_DISABLE_BOUND_COHORT") == nullptr;

  bool cache_matches =
      cohort_members_.size() == entries.size() &&
      std::ranges::equal(
          entries, cohort_members_, {},
          [](const auto &entry) {
            return static_cast<LlvmProcessExecutor *>(entry.executor);
          },
          std::identity{}) &&
      std::ranges::equal(
          entries, cohort_start_instructions_, {},
          &runtime::simir::ProcessCohortResumeEntry::start_instruction,
          std::identity{});
  if (cache_matches) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
      const auto &source = entries[index];
      const auto &executor =
          *static_cast<LlvmProcessExecutor *>(source.executor);
      const auto &cached = cohort_native_entries_[index];
      if (executor.instance_generation_ != cohort_member_generations_[index] ||
          cached.queued != reinterpret_cast<std::uint8_t *>(source.queued) ||
          cached.waiting_on_static !=
              reinterpret_cast<std::uint8_t *>(source.waiting_on_static) ||
          cached.process_status !=
              reinterpret_cast<std::uint8_t *>(source.status)) {
        cache_matches = false;
        break;
      }
    }
  }
  if (!cache_matches) {
    if (cohort_generation_ == std::numeric_limits<std::uint64_t>::max()) {
      throw compiler::LlvmJitError("compiled cohort generation exhausted");
    }
    ++cohort_generation_;
    if (cohort_binding_) {
      (void)jit_.release_cohort_binding(cohort_binding_);
    }
    cohort_members_.clear();
    cohort_member_generations_.clear();
    cohort_start_instructions_.clear();
    cohort_native_entries_.clear();
    cohort_update_batches_.clear();
    cohort_logic9_update_batches_.clear();
    cohort_binding_ = {};
    cohort_binding_generation_ = 0U;
    cohort_members_.reserve(entries.size());
    cohort_member_generations_.reserve(entries.size());
    cohort_start_instructions_.reserve(entries.size());
    cohort_native_entries_.reserve(entries.size());
    cohort_update_batches_.reserve(entries.size());
    cohort_logic9_update_batches_.reserve(entries.size());
  }
  if (cohort_binding_ && cohort_binding_generation_ != cohort_generation_) {
    throw compiler::LlvmJitError("compiled cohort binding generation is stale");
  }
  for (const auto &entry : entries) {
    auto &executor = *static_cast<LlvmProcessExecutor *>(entry.executor);
    executor.cohort_resume_mode_ = CohortResumeMode::prepare;
    try {
      static_cast<void>(
          executor.resume(*entry.context, entry.start_instruction));
    } catch (...) {
      for (const auto &reset_entry : entries) {
        auto &reset = *static_cast<LlvmProcessExecutor *>(reset_entry.executor);
        reset.cohort_resume_mode_ = CohortResumeMode::normal;
      }
      return 0U;
    }
    executor.cohort_resume_result_ = {};
    executor.cohort_resume_result_.abi_version =
        FSIM_JIT_RESUME_RESULT_ABI_VERSION_V2;
    executor.cohort_resume_result_.struct_size =
        sizeof(executor.cohort_resume_result_);
    if (!cache_matches) {
      cohort_members_.push_back(&executor);
      cohort_member_generations_.push_back(executor.instance_generation_);
      cohort_start_instructions_.push_back(entry.start_instruction);
      cohort_native_entries_.push_back(compiler::JitProcessCohortResumeEntry{
          executor.binding_, executor.runtime_, executor.frame_,
          executor.cohort_resume_result_,
          reinterpret_cast<std::uint8_t *>(entry.queued),
          reinterpret_cast<std::uint8_t *>(entry.waiting_on_static),
          reinterpret_cast<std::uint8_t *>(entry.status)});
    }
  }
  for (auto &native : cohort_native_entries_) {
    native.failure = {};
    native.status = std::numeric_limits<std::uint32_t>::max();
  }
  auto native_entries =
      std::span{cohort_native_entries_.data(), entries.size()};
  const auto executed =
      preserve_publication_order
          ? jit_.resume_ordered_cohort_prevalidated(native_entries)
      : bound_cohort_enabled && cohort_binding_
          ? jit_.resume_cohort_prevalidated(cohort_binding_, native_entries)
          : jit_.resume_cohort_prevalidated(native_entries);
  if (!preserve_publication_order && bound_cohort_enabled && !cohort_binding_) {
    cohort_binding_ = jit_.bind_cohort_prevalidated(native_entries);
    cohort_binding_generation_ = cohort_generation_;
  }
  bool cohort_updates_flushed{};
  static const bool cohort_update_batches_enabled =
      std::getenv("FSIM_DISABLE_COHORT_UPDATE_BATCH") == nullptr;
  if (!preserve_publication_order && cohort_update_batches_enabled &&
      executed == entries.size() && !jit_process_profile().enabled) {
    const auto *const update_domain =
        entries.front().context->direct_update_domain();
    bool compatible =
        update_domain != nullptr &&
        std::ranges::all_of(entries,
                            [&](const auto &entry) {
                              return entry.context->direct_update_domain() ==
                                     update_domain;
                            }) &&
        std::ranges::all_of(
            native_entries,
            [](const auto &entry) {
              return entry.status ==
                         FSIM_JIT_RESUME_STATUS_WAIT_SENSITIVITY_V2 &&
                     !entry.failure;
            }) &&
        std::ranges::all_of(entries, [](const auto &entry) {
          const auto &executor =
              *static_cast<LlvmProcessExecutor *>(entry.executor);
          return !executor.callback_state_.failure;
        });
    // Callback words are absent from direct-update slots. Batch only
    // when every member can be published entirely from those slots.
    compatible =
        compatible && std::ranges::all_of(entries, [](const auto &entry) {
          const auto &executor =
              *static_cast<LlvmProcessExecutor *>(entry.executor);
          return executor.pending_update_words_.empty();
        });
    if (compatible) {
      try {
        cohort_update_batches_.clear();
        cohort_logic9_update_batches_.clear();
        for (std::size_t index = 0; index < executed; ++index) {
          auto &executor =
              *static_cast<LlvmProcessExecutor *>(entries[index].executor);
          if (executor.has_buffered_logic9_updates()) {
            cohort_logic9_update_batches_.push_back(
                {executor.process_.id(), executor.buffered_logic9_update_views_});
          }
          if (executor.has_buffered_update_words()) {
            cohort_update_batches_.push_back(
                {executor.process_.id(), executor.direct_update_slot_views_,
                 executor.direct_update_active_words_});
          }
        }
        bool logic9_flushed =
            cohort_logic9_update_batches_.empty() ||
            entries.front().context->write_validated_logic9_update_batches(
                std::span{cohort_logic9_update_batches_.data(),
                          cohort_logic9_update_batches_.size()});
        if (!logic9_flushed) {
          for (std::size_t index = 0; index < executed; ++index) {
            auto &executor =
                *static_cast<LlvmProcessExecutor *>(entries[index].executor);
            if (executor.has_buffered_logic9_updates()) {
              executor.flush_buffered_logic9_updates(*entries[index].context);
            }
          }
          logic9_flushed = true;
        }
        const bool update_words_flushed =
            cohort_update_batches_.empty() ||
            entries.front().context->write_validated_update_slot_batches(
                std::span{cohort_update_batches_.data(),
                          cohort_update_batches_.size()});
        cohort_updates_flushed = logic9_flushed && update_words_flushed;
      } catch (...) {
        entries.front().failure = std::current_exception();
        return 1U;
      }
    }
  }
  for (std::size_t index = 0; index < executed; ++index) {
    auto &entry = entries[index];
    auto &executor = *static_cast<LlvmProcessExecutor *>(entry.executor);
    const auto status =
        static_cast<compiler::JitResumeStatus>(native_entries[index].status);
    const bool callback_failed =
        static_cast<bool>(executor.callback_state_.failure);
    if (status == compiler::JitResumeStatus::wait_sensitivity &&
        !callback_failed && !native_entries[index].failure &&
        !jit_process_profile().enabled) {
      try {
        if (!cohort_updates_flushed) {
          executor.flush_buffered_updates(*entry.context, false);
        }
        if (executor.has_container_registers_ &&
            !executor.active_container_object_aliases_.empty()) {
          executor.discard_container_object_aliases();
        }
        const auto &result = executor.cohort_resume_result_;
        if (result.instruction >= executor.operation_count_) {
          throw compiler::LlvmJitError(
              "compiled cohort process returned an invalid "
              "boundary instruction");
        }
        if (executor.frame_.program_counter != result.instruction + 1U) {
          throw compiler::LlvmJitError(
              "compiled cohort process returned a non-sequential "
              "WaitSensitivity PC");
        }
        entry.result = runtime::simir::ProcessResumeResult{
            result.instruction, executor.frame_.program_counter};
        entry.result.external.kind =
            executor.validated_static_sensitivity_
                ? runtime::simir::ExternalSuspendKind::
                      validated_wait_sensitivity
                : runtime::simir::ExternalSuspendKind::wait_sensitivity;
        continue;
      } catch (...) {
        entry.failure = std::current_exception();
        if (preserve_publication_order) {
          // All native members in executed have already committed.
          // Preserve their acceptance even if host conversion fails.
          continue;
        }
        return index + 1U;
      }
    }
    executor.cohort_resume_status_ = native_entries[index].status;
    executor.cohort_resume_failure_ = callback_failed
                                          ? executor.callback_state_.failure
                                          : native_entries[index].failure;
    executor.cohort_resume_mode_ = CohortResumeMode::consume;
    try {
      entry.result = executor.resume(*entry.context, entry.start_instruction);
    } catch (...) {
      entry.failure = std::current_exception();
      if (!preserve_publication_order) {
        return index + 1U;
      }
      // Continue consuming every already-executed member; the caller
      // reports the failure without replaying the accepted native suffix.
    }
  }
  return executed;
}

[[nodiscard]] bool
LlvmProcessExecutor::cohort_manages_process_state() const noexcept {
  static_assert(sizeof(bool) == sizeof(std::uint8_t));
  static_assert(sizeof(runtime::simir::ProcessStatus) == sizeof(std::uint8_t));
  static const bool enabled =
      std::getenv("FSIM_DISABLE_COHORT_PROCESS_STATE") == nullptr;
  return enabled;
}

[[nodiscard]] const void *LlvmProcessExecutor::cohort_domain() const noexcept {
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
            "compiled process '" + process_.name()
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
            "compiled process '" + process_.name() + "' register "
            + std::to_string(id) + " has not been initialized at instruction "
            + std::to_string(frame_.program_counter)
        };
    }
    const auto register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process_.register_value_kinds());
    const auto kind = register_value_kinds.empty()
        ? runtime::simir::ValueKind::logic4
        : register_value_kinds[id];
    const auto offset = layout.register_word_offsets[id];
    const auto words = (resolved_width + 63U) / 64U;
    if (kind == runtime::simir::ValueKind::logic9) {
        return PackedLogic4::from_logic9_word_planes(
            resolved_width,
            std::span<const std::uint64_t> { register_aval_ }
                .subspan(offset, words),
            std::span<const std::uint64_t> { register_bval_ }
                .subspan(offset, words),
            std::span<const std::uint64_t> { register_logic9_plane2_ }
                .subspan(offset, words),
            std::span<const std::uint64_t> { register_logic9_plane3_ }
                .subspan(offset, words));
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
    const auto register_value_kinds
        = runtime::simir::process_layout_detail::ProcessLayoutAccess::view(
            process_.register_value_kinds());
    const auto kind = register_value_kinds.empty()
        ? runtime::simir::ValueKind::logic4
        : register_value_kinds[id];
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
        for (std::size_t word = 0; word < words; ++word) {
            std::array<std::uint64_t, 4> planes { };
            const auto first_bit = word * 64U;
            const auto bits_in_word
                = std::min<std::size_t>(64U, value.width() - first_bit);
            for (std::size_t bit = 0; bit < bits_in_word; ++bit) {
                const auto encoded = static_cast<std::uint8_t>(
                    value.get_logic9(first_bit + bit));
                const auto mask = std::uint64_t { 1 } << bit;
                for (std::size_t plane = 0; plane < planes.size(); ++plane) {
                    if ((encoded & (1U << plane)) != 0U) {
                        planes[plane] |= mask;
                    }
                }
            }
            register_aval_[offset + word] = planes[0];
            register_bval_[offset + word] = planes[1];
            register_logic9_plane2_[offset + word] = planes[2];
            register_logic9_plane3_[offset + word] = planes[3];
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
            process_.operations()[instruction])) {
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

std::uint32_t LlvmProcessExecutor::record_code_coverage_counter(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    const std::uint32_t counter) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        const auto status = state.context->record_code_coverage_counter(
            ::fsim::runtime::CodeCoverageCounterId { counter });
        if (status
            == runtime::simir::CodeCoverageCounterRuntimeStatus::Unavailable) {
            throw runtime::simir::InterpreterError(
                process,
                instruction,
                "code coverage counter service is unavailable");
        }
        if (status
            == runtime::simir::CodeCoverageCounterRuntimeStatus::OutOfRange) {
            throw runtime::simir::InterpreterError(
                process,
                instruction,
                "code coverage counter is out of range");
        }
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

std::uint32_t LlvmProcessExecutor::sample_coverage(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v2* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || !state.process.valid() || state.generated_process != process
            || instruction >= state.process.operations().size()
            || frame != &state.executor->frame_) {
            throw std::logic_error("invalid native coverage sample callback");
        }
        const auto expanded
            = state.process.operations().expanded(instruction);
        const auto* sample = runtime::simir::operation_get_if<
            runtime::simir::CoverageSample>(&expanded);
        if (sample == nullptr) {
            throw std::logic_error(
                "native coverage sample callback targets another operation");
        }
        std::vector<PackedLogic4> actuals;
        actuals.reserve(sample->actuals.size());
        for (std::size_t index = 0; index < sample->actuals.size(); ++index) {
            actuals.push_back(state.executor->read_register(
                sample->actuals[index], sample->actual_widths[index]));
        }
        state.context->sample_coverage(*sample, actuals, instruction);
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

std::uint32_t LlvmProcessExecutor::execute_class_property_operation(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v2* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || !state.process.valid() || state.generated_process != process
            || instruction >= state.process.operations().size()
            || frame != &state.executor->frame_) {
            throw std::logic_error(
                "invalid native class-property callback");
        }
        const auto expanded
            = state.process.operations().expanded(instruction);
        const auto& stored = expanded;
        const auto read_handle = [&](const runtime::simir::RegisterId id) {
            return state.executor->read_register(
                id,
                runtime::simir::ProcessExecutor::native_register_width)
                .low_word().aval;
        };
        if (const auto* read = runtime::simir::operation_get_if<
                runtime::simir::ClassPropertyRead>(&stored)) {
            state.executor->write_register(
                read->destination,
                state.context->read_class_property(
                    read_handle(read->receiver), read->property_identity,
                    read->width, instruction));
        } else if (const auto* write = runtime::simir::operation_get_if<
                       runtime::simir::ClassPropertyWrite>(&stored)) {
            state.context->write_class_property(
                read_handle(write->receiver), write->property_identity,
                state.executor->read_register(
                    write->source,
                    state.executor->layout_.register_widths.at(
                        write->source)),
                instruction);
        } else if (const auto* static_read = runtime::simir::operation_get_if<
                       runtime::simir::ClassStaticPropertyRead>(&stored)) {
            state.executor->write_register(
                static_read->destination,
                state.context->read_class_static_property(
                    static_read->property_identity,
                    static_read->width,
                    instruction));
        } else if (const auto* static_write = runtime::simir::operation_get_if<
                       runtime::simir::ClassStaticPropertyWrite>(&stored)) {
            state.context->write_class_static_property(
                static_write->property_identity,
                state.executor->read_register(
                    static_write->source,
                    state.executor->layout_.register_widths.at(
                        static_write->source)),
                instruction);
        } else {
            throw std::logic_error(
                "native class-property callback targets another operation");
        }
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

std::uint32_t LlvmProcessExecutor::query_event_triggered(
    void* context,
    const std::uint32_t process,
    const std::uint32_t instruction,
    fsim_jit_frame_v2* frame) noexcept
{
    auto& state = *static_cast<CallbackState*>(context);
    if (state.failure) {
        return 1U;
    }
    try {
        if (state.executor == nullptr || state.context == nullptr
            || !state.process.valid() || state.generated_process != process
            || instruction >= state.process.operations().size()
            || frame != &state.executor->frame_) {
            throw std::logic_error(
                "invalid native event-trigger callback");
        }
        const auto expanded
            = state.process.operations().expanded(instruction);
        const auto* query = runtime::simir::operation_get_if<
            runtime::simir::EventTriggered>(&expanded);
        if (query == nullptr) {
            throw std::logic_error(
                "native event-trigger callback targets another operation");
        }
        const auto triggered
            = state.context->event_triggered(query->event, instruction);
        state.executor->write_register(
            query->destination,
            PackedLogic4::from_aval_bval(1U, triggered ? 1U : 0U, 0U));
        return 0U;
    } catch (...) {
        capture_failure(state);
        return 1U;
    }
}

void LlvmProcessExecutor::invalidate_signal_read_cache(
    CallbackState& state) noexcept
{
    state.signal_read_cache_ids.fill(
        std::numeric_limits<std::uint32_t>::max());
}

bool LlvmProcessExecutor::has_buffered_update_words() const noexcept
{
    if (!pending_update_words_.empty()) {
        return true;
    }
    if (!direct_update_active_words_.empty()) {
        if (std::ranges::any_of(
            direct_update_active_words_,
            [](const std::uint64_t active) { return active != 0U; })) {
            return true;
        }
    }
    return std::ranges::any_of(
        direct_update_slots_,
        [](const fsim_jit_update_slot_v2& slot) {
            return slot.active != 0U;
        });
}

bool LlvmProcessExecutor::has_buffered_logic9_updates() const noexcept
{
    return std::ranges::any_of(
        buffered_logic9_update_views_,
        [](const runtime::simir::ProcessLogic9UpdateSlotView& slot) {
            return slot.mask != nullptr && *slot.mask != 0U;
        });
}

void LlvmProcessExecutor::flush_buffered_updates(
    runtime::simir::ProcessExecutionContext& context,
    const bool allow_slot_batch)
{
    flush_buffered_logic9_updates(context);

    // Keep queued callback words on the existing word-update path.
    if (allow_slot_batch && pending_update_words_.empty()
        && has_buffered_update_words()
        && !direct_update_slot_views_.empty()
        && callback_state_.supports_direct_word_updates
        && !jit_process_profile().enabled
        && context.direct_update_domain() != nullptr) {
        auto batch = runtime::simir::ProcessUpdateSlotBatch {
            process_.id(),
            direct_update_slot_views_,
            direct_update_active_words_
        };
        if (context.write_validated_update_slot_batches(
                std::span { &batch, 1U })) {
            return;
        }
    }
    flush_update_words(context);
}

bool LlvmProcessExecutor::buffer_logic9_update(
    const runtime::simir::SignalId signal,
    const std::uint32_t offset,
    const std::uint32_t width,
    const fsim_jit_logic9_word_v2& value)
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
    const auto invalid_code_mask
        = value.planes[3]
        & (value.planes[2] | value.planes[1] | value.planes[0]);
    const std::array normalized_planes {
        value.planes[0] | invalid_code_mask,
        value.planes[1] & ~invalid_code_mask,
        value.planes[2] & ~invalid_code_mask,
        value.planes[3] & ~invalid_code_mask,
    };
    for (std::size_t plane = 0; plane < found->planes.size(); ++plane) {
        const auto shifted
            = (normalized_planes[plane] & source_mask) << offset;
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
    if (!has_buffered_logic9_updates()) {
        return;
    }
    if (context.write_validated_logic9_update_batch({
            process_.id(), buffered_logic9_update_views_ })) {
        return;
    }
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
    if (!has_buffered_update_words()) {
        return;
    }
    const auto direct_owners = context.direct_single_driver_processes();
    const auto stable_owners = context.stable_single_writer_processes();
    const auto direct_aval = context.direct_signal_aval();
    const auto direct_bval = context.direct_signal_bval();
    const auto direct_wide_aval = context.direct_wide_signal_aval();
    const auto direct_wide_bval = context.direct_wide_signal_bval();
    const auto direct_wide_offsets = context.direct_wide_signal_offsets();
    const auto unchanged_direct_update = [&](
        const runtime::simir::SignalId signal,
        const fsim_jit_update_slot_v2& slot) {
        if (!stable_direct_update_suppression_allowed_
            || !context.supports_direct_signal_read(signal)
            || signal >= direct_owners.size()
            || signal >= stable_owners.size()
            || direct_owners[signal] != process_.id()
            || stable_owners[signal] != process_.id()) {
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

std::uint32_t LlvmProcessExecutor::mapped_signal(
    const CallbackState& state, const std::uint32_t signal)
{
    if (state.signal_callback_ids_are_actual) {
        return signal;
    }
    if (!state.dense_signal_remap.empty()) {
        if (signal < state.dense_signal_remap_base) {
            return signal;
        }
        const auto offset = signal - state.dense_signal_remap_base;
        return offset < state.dense_signal_remap.size()
            ? state.dense_signal_remap[offset]
            : signal;
    }
    return state.signal_remap.empty()
        ? signal
        : mapped_signal_sparse(state, signal);
}




#endif

} // namespace fsim::app::application_detail
