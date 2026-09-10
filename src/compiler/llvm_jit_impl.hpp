// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include <array>
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

    struct NativeBoundCohortEntry {
        NativeCohort* function { };
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
    std::vector<std::unique_ptr<NativeBoundCohortEntry>> bound_cohorts;
    std::unordered_map<const runtime::simir::Process*,
        llvm_detail::ValidatedProcess>
        immutable_validated_processes;
    std::mutex validation_mutex;
    std::mutex lookup_mutex;
    std::mutex cohort_mutex;
    std::uint64_t next_handle = 1;
    std::uint64_t next_cohort = 1;
};

} // namespace fsim::compiler
