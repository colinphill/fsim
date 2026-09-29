// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsim::compiler {
using NativeProcess = fsim_jit_process_v1;
using NativeCohort = std::uint32_t(
    const fsim_jit_runtime_v1* const*,
    fsim_jit_frame_v1* const*,
    fsim_jit_resume_result_v1* const*,
    std::uint32_t*,
    std::uint8_t* const*,
    std::uint8_t* const*,
    std::uint8_t* const*,
    std::uint8_t* const*,
    std::uint32_t);
using NativeCompactCohort = void(
    const std::uint64_t*,
    const std::uint64_t*,
    std::uint32_t,
    std::uint32_t,
    fsim_jit_update_slot_v1* const*,
    std::uint64_t* const*,
    const std::uint64_t*,
    std::uint8_t* const*,
    std::uint8_t* const*,
    std::uint8_t* const*);
struct NativePureWaveKernelMember {
    const std::uint64_t* direct_signal_aval { };
    const std::uint64_t* direct_signal_bval { };
    const std::uint64_t* direct_wide_signal_aval { };
    const std::uint64_t* direct_wide_signal_bval { };
    std::uint32_t wide_signal_word_offset { };
    fsim_jit_update_slot_v1* update_slot { };
    std::uint64_t* active_word { };
    std::uint64_t active_mask { };
    std::uint8_t* queued { };
    std::uint8_t* waiting { };
    std::uint8_t* process_status { };
    std::uint32_t read_signal0 { };
    std::uint32_t read_signal1 { };
    std::uint32_t input_offset0 { };
    std::uint32_t input_offset1 { };
    std::uint32_t slice_offset { };
    std::uint32_t selector_offset { };
    std::uint32_t update_offset { };
};
using NativePureWaveAndTask = void(
    const runtime::simir::PureWavePreparedMember* const*, std::uint32_t);
using NativePureWaveSingle = void(const NativePureWaveKernelMember*);
using NativePureWaveDispatch = void(
    const runtime::simir::PureWavePreparedMember* const*,
    const std::uint32_t*,
    const std::uint8_t*, std::uint32_t);

struct NativePureWavePreparedView {
    const void* owner { };
    std::uint64_t generation { };
    const std::atomic_bool* released { };
    std::uint32_t shape { };
    std::uint32_t signal0 { };
    std::uint32_t signal1 { };
    const NativePureWaveKernelMember* kernel_member { };
};

struct LlvmJit::Impl {
    struct ProcessInfo {
        JitProcessFrameLayout frame_layout;
        std::uint32_t operation_count { };
        bool uses_native_call_stack { };
        bool requires_resume { };
        bool uses_write_update { };
        bool uses_write_after { };
        bool uses_write_inertial { };
        bool uses_write_projected { };
        bool uses_write_projected_waveform { };
        bool uses_write_blocking_slice { };
        bool uses_write_update_slice { };
        bool uses_write_after_slice { };
        bool uses_write_inertial_slice { };
        bool uses_write_projected_slice { };
        bool uses_write_projected_waveform_slice { };
        bool uses_force_signal_slice { };
        bool uses_release_signal_slice { };
        bool uses_force_driver_signal_slice { };
        bool uses_release_driver_signal_slice { };
        bool uses_debug_points { };
        bool uses_signal_event { };
        bool uses_signal_last_value { };
        bool uses_signal_last_event { };
        bool uses_simulation_time { };
        bool uses_vital_timing { };
        bool uses_vital_delay { };
        bool uses_signal_active { };
        bool uses_signal_last_active { };
        bool uses_signal_driving { };
        bool uses_signal_driving_value { };
        bool uses_output { };
        bool uses_postponed_output { };
        bool uses_report { };
        bool uses_formatted_output { };
        bool uses_time_output { };
        bool uses_monitor_install { };
        bool uses_monitor_control { };
        bool uses_random_value { };
        bool uses_strings { };
        bool uses_files { };
        bool uses_containers { };
        bool uses_wide_container_operation { };
        bool uses_exact_signal_operation { };
        bool uses_wide_signal_read { };
        bool uses_wide_signal_write { };
        bool uses_code_coverage { };
        bool uses_coverage_sample { };
        bool uses_class_property_operation { };
        bool uses_event_triggered { };
        std::vector<runtime::simir::InstructionIndex> entry_points;

        static constexpr std::array flags {
            &ProcessInfo::uses_native_call_stack,
            &ProcessInfo::requires_resume,
            &ProcessInfo::uses_write_update,
            &ProcessInfo::uses_write_after,
            &ProcessInfo::uses_write_inertial,
            &ProcessInfo::uses_write_projected,
            &ProcessInfo::uses_write_projected_waveform,
            &ProcessInfo::uses_write_blocking_slice,
            &ProcessInfo::uses_write_update_slice,
            &ProcessInfo::uses_write_after_slice,
            &ProcessInfo::uses_write_inertial_slice,
            &ProcessInfo::uses_write_projected_slice,
            &ProcessInfo::uses_write_projected_waveform_slice,
            &ProcessInfo::uses_force_signal_slice,
            &ProcessInfo::uses_release_signal_slice,
            &ProcessInfo::uses_force_driver_signal_slice,
            &ProcessInfo::uses_release_driver_signal_slice,
            &ProcessInfo::uses_debug_points,
            &ProcessInfo::uses_signal_event,
            &ProcessInfo::uses_signal_last_value,
            &ProcessInfo::uses_signal_last_event,
            &ProcessInfo::uses_simulation_time,
            &ProcessInfo::uses_vital_timing,
            &ProcessInfo::uses_vital_delay,
            &ProcessInfo::uses_signal_active,
            &ProcessInfo::uses_signal_last_active,
            &ProcessInfo::uses_signal_driving,
            &ProcessInfo::uses_signal_driving_value,
            &ProcessInfo::uses_output,
            &ProcessInfo::uses_postponed_output,
            &ProcessInfo::uses_report,
            &ProcessInfo::uses_formatted_output,
            &ProcessInfo::uses_time_output,
            &ProcessInfo::uses_monitor_install,
            &ProcessInfo::uses_monitor_control,
            &ProcessInfo::uses_random_value,
            &ProcessInfo::uses_strings,
            &ProcessInfo::uses_files,
            &ProcessInfo::uses_containers,
            &ProcessInfo::uses_wide_container_operation,
            &ProcessInfo::uses_exact_signal_operation,
            &ProcessInfo::uses_wide_signal_read,
            &ProcessInfo::uses_wide_signal_write,
            &ProcessInfo::uses_code_coverage,
            &ProcessInfo::uses_coverage_sample,
            &ProcessInfo::uses_class_property_operation,
            &ProcessInfo::uses_event_triggered,
        };
    };

    [[nodiscard]] static std::vector<std::byte> encode_module_metadata(
        std::span<const ProcessInfo> processes);
    [[nodiscard]] static std::optional<std::vector<ProcessInfo>>
    decode_module_metadata(
        std::span<const std::byte> metadata,
        std::span<const JitProcessModuleEntry> entries,
        std::span<const std::uint32_t> signal_widths);

    struct NativeEntry {
        NativeProcess* function { };
        ProcessInfo info;
        std::string symbol;
    };

    struct NativeCohortEntry {
        NativeCohort* function { };
        std::vector<const NativeEntry*> members;
        bool manages_process_state { };
        bool region_mode { };
    };

    struct NativeLogic4BitAndCohortEntry {
        NativeCohort* function { };
        std::vector<JitProcessCohortLogic4BitAndMember> members;
        bool manages_process_state { };
    };

    struct NativeBoundCohortEntry {
        NativeCohort* function { };
        std::uint64_t generation { };
        std::vector<const NativeEntry*> members;
        std::vector<const fsim_jit_runtime_v1*> runtimes;
        std::vector<fsim_jit_frame_v1*> frames;
        std::vector<fsim_jit_resume_result_v1*> results;
        std::vector<std::uint32_t> statuses;
        std::vector<std::uint8_t*> queued;
        std::vector<std::uint8_t*> waiting;
        std::vector<std::uint8_t*> process_statuses;
        std::vector<std::uint8_t*> active;
        bool manages_process_state { };
        bool region_mode { };
    };

    struct NativeBoundLogic4BitAndCohortEntry {
        NativeCohort* function { };
        std::uint64_t generation { };
        std::vector<const NativeEntry*> native_members;
        std::vector<JitProcessCohortLogic4BitAndMember> members;
        std::vector<const fsim_jit_runtime_v1*> runtimes;
        std::vector<fsim_jit_frame_v1*> frames;
        std::vector<fsim_jit_resume_result_v1*> results;
        std::vector<std::uint32_t> statuses;
        std::vector<std::uint8_t*> queued;
        std::vector<std::uint8_t*> waiting;
        std::vector<std::uint8_t*> process_statuses;
        bool manages_process_state { };
    };

    struct NativeCompactLogic4BitAndMemberShape {
        std::uint32_t extract_lhs_offset { };
        std::uint32_t extract_rhs_offset { };
        std::uint32_t update_offset { };

        friend bool operator==(
            const NativeCompactLogic4BitAndMemberShape&,
            const NativeCompactLogic4BitAndMemberShape&) = default;
    };

    struct NativeCompactLogic4BitAndCohortEntry {
        NativeCompactCohort* function { };
        std::vector<NativeCompactLogic4BitAndMemberShape> members;
    };

    struct NativeBoundCompactLogic4BitAndCohortEntry {
        NativeCompactCohort* function { };
        std::uint64_t generation { };
        const std::uint64_t* input_aval { };
        const std::uint64_t* input_bval { };
        std::uint32_t input_lhs_signal { };
        std::uint32_t input_rhs_signal { };
        std::vector<fsim_jit_update_slot_v1*> output_slots;
        std::vector<std::uint64_t*> active_words;
        std::vector<std::uint64_t> active_masks;
        std::vector<std::uint8_t*> queued;
        std::vector<std::uint8_t*> waiting;
        std::vector<std::uint8_t*> process_statuses;
    };

    enum class PureWaveShape : std::uint8_t {
        logic4_bit_and,
        reducer31,
        reduction7,
        wide_copy6,
    };

    struct NativePureWaveMemberPlan {
        const NativeEntry* native { };
        const runtime::simir::Process* process { };
        PureWaveShape shape { };
        std::array<std::uint32_t, 4> read_slots { };
        std::array<std::uint32_t, 4> read_signals { };
        std::array<std::uint32_t, 5> registers { };
        std::array<std::uint32_t, 5> register_widths { };
        std::array<std::uint32_t, 5> register_word_offsets { };
        std::uint32_t read_count { };
        std::uint32_t update_slot { };
        std::uint32_t update_signal { };
        std::uint32_t update_offset { };
        std::uint32_t update_width { };
        std::uint32_t input_lhs_offset { };
        std::uint32_t input_rhs_offset { };
        std::uint32_t slice_offset { };
        std::uint32_t selector_offset { };
        std::uint32_t output_slice_offset { };
        std::uint32_t expected_resume_instruction { };
        std::uint32_t expected_wait_instruction { };
    };

    struct NativeBoundPureWaveMemberEntry {
        std::uint64_t generation { };
        std::atomic_bool released { };
        NativePureWaveMemberPlan plan;
        const fsim_jit_runtime_v1* runtime { };
        fsim_jit_frame_v1* frame { };
        fsim_jit_resume_result_v1* result { };
        std::uint8_t* queued { };
        std::uint8_t* waiting { };
        std::uint8_t* process_status { };
        const std::uint32_t* direct_read_signals { };
        std::uint32_t direct_read_signal_count { };
        fsim_jit_update_slot_v1* direct_update_slots { };
        std::uint32_t direct_update_slot_count { };
        std::uint64_t* direct_update_active_words { };
        std::uint32_t direct_update_active_word_count { };
        const std::uint64_t* direct_signal_aval { };
        const std::uint64_t* direct_signal_bval { };
        std::uint32_t direct_signal_count { };
        const std::uint64_t* direct_wide_signal_aval { };
        const std::uint64_t* direct_wide_signal_bval { };
        const std::uint32_t* direct_wide_signal_offsets { };
        std::uint32_t direct_wide_signal_offset_count { };
        std::uint32_t direct_wide_word_count { };
        std::uint64_t layout_id_low { };
        std::uint64_t layout_id_high { };
        std::uint32_t frame_register_count { };
        std::uint32_t expected_resume_instruction { };
        std::uint32_t expected_wait_instruction { };
        NativePureWaveKernelMember kernel_member;
        NativePureWavePreparedView prepared_view;
    };

    struct NativeBoundPureWaveEntry {
        std::uint64_t generation { };
        std::vector<std::shared_ptr<NativeBoundPureWaveMemberEntry>> members;
        std::vector<std::size_t> task_ends;
        std::vector<PureWaveShape> task_shapes;
        std::vector<std::uint32_t> kernel_task_ends;
        std::vector<std::uint8_t> kernel_task_shapes;
        std::vector<NativePureWaveKernelMember> kernel_members;
    };

    struct NativePureWaveKernels {
        NativePureWaveAndTask* logic4_bit_and { };
        NativePureWaveSingle* reducer31 { };
        NativePureWaveSingle* reduction7 { };
        NativePureWaveSingle* wide_copy6 { };
        NativePureWaveDispatch* dispatch { };
    };

    void ensure_pure_wave_kernels();

    [[nodiscard]] static std::optional<NativePureWaveMemberPlan>
    classify_pure_wave_member(
        const JitPureWaveMember& member, const NativeEntry& native);

    LlvmJitOptions options;
    std::unique_ptr<llvm_detail::LlvmObjectCache> object_cache;
    std::unique_ptr<llvm::orc::LLJIT> jit;
    std::string target_cpu;
    std::vector<std::string> target_features;
    std::string immutable_design_identity;
    std::unordered_set<std::string> module_identities;
    std::unordered_set<std::string> symbols;
    std::unordered_set<std::string> pending_module_identities;
    std::unordered_set<std::string> pending_symbols;
    std::unordered_map<std::string, ProcessInfo> info_by_symbol;
    std::unordered_map<std::string, JitProcessHandle> handles_by_symbol;
    std::unordered_map<std::uint64_t, std::unique_ptr<NativeEntry>> functions;
    std::unordered_map<std::size_t,
        std::vector<std::unique_ptr<NativeCohortEntry>>>
        cohort_functions;
    std::unordered_map<std::size_t,
        std::vector<std::unique_ptr<NativeLogic4BitAndCohortEntry>>>
        logic4_bit_and_cohort_functions;
    std::unordered_map<std::size_t,
        std::vector<std::unique_ptr<NativeCompactLogic4BitAndCohortEntry>>>
        compact_logic4_bit_and_cohort_functions;
    std::vector<std::shared_ptr<NativeBoundCohortEntry>> bound_cohorts;
    std::vector<std::shared_ptr<NativeBoundLogic4BitAndCohortEntry>>
        bound_logic4_bit_and_cohorts;
    std::vector<std::shared_ptr<NativeBoundCompactLogic4BitAndCohortEntry>>
        bound_compact_logic4_bit_and_cohorts;
    std::unordered_map<const NativeBoundPureWaveMemberEntry*,
        std::shared_ptr<NativeBoundPureWaveMemberEntry>>
        bound_pure_wave_members;
    std::unordered_map<const NativeBoundPureWaveEntry*,
        std::shared_ptr<NativeBoundPureWaveEntry>>
        bound_pure_waves;
    NativePureWaveKernels pure_wave_kernels;
    bool pure_wave_kernels_initialized { };
    std::vector<const runtime::simir::PureWavePreparedMember*>
        pure_wave_member_scratch;
    std::vector<runtime::simir::PureWavePreparedMember>
        pure_wave_prepared_member_scratch;
    std::vector<std::uint32_t> pure_wave_task_end_scratch;
    std::vector<std::uint8_t> pure_wave_task_shape_scratch;
    std::unordered_map<const runtime::simir::Process*,
        llvm_detail::ValidatedProcess>
        immutable_validated_processes;
    std::mutex validation_mutex;
    std::mutex lookup_mutex;
    std::mutex cohort_mutex;
    std::uint64_t next_handle = 1;
    std::uint64_t next_cohort = 1;
    std::uint64_t next_bound_cohort_generation = 1;
    std::uint64_t next_pure_wave_member_generation = 1;
};

} // namespace fsim::compiler
