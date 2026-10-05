// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::tests::app::frontier {

enum class GenericFixtureMode : std::uint8_t {
    interpreter,
    llvm_o0,
    llvm_o2,
};

struct GenericFixturePolicy final {
    std::optional<bool> region_kernel_enabled;
    std::optional<bool> local_wave_enabled;
    std::optional<std::pair<std::size_t, runtime::simir::ResolutionKind>>
        output_resolution { };
    bool include_display_effect { };
    /// Hide optional provider capabilities while retaining the production
    /// RegionKernelBackend instance and its failure channel.
    bool v1_only_region_backend_provider { };
    /// Record the exact borrowed Generic frontier around the real LLVM entry.
    bool capture_generic_frontier { };
};

struct GenericSignalSnapshot final {
    runtime::simir::SignalId signal { };
    runtime::simir::ValueKind value_kind {
        runtime::simir::ValueKind::logic4
    };
    runtime::simir::ResolutionKind resolution {
        runtime::simir::ResolutionKind::none };
    bool direct_signal_materialization_pending { };
    runtime::PackedLogic4 current;
    runtime::PackedLogic4 last;
    runtime::PackedLogic4 stored;
    std::optional<runtime::PackedLogic4> raw_driver;
    std::optional<runtime::simir::ProcessId> raw_driver_process;
    std::optional<std::pair<runtime::SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<runtime::SimulationTick, std::uint64_t>> transaction;
    runtime::simir::ProcessSchedulingDomain event_domain {
        runtime::simir::ProcessSchedulingDomain::generic
    };
    runtime::SchedulerPhase event_phase { runtime::SchedulerPhase::active };
    std::uint64_t systemverilog_round { };
    std::uint64_t value_revision { };

    friend bool operator==(
        const GenericSignalSnapshot&, const GenericSignalSnapshot&) = default;
};

struct GenericLogic9SidecarSnapshot final {
    runtime::simir::SignalId signal { };
    bool unlisted { };
    bool generic_blocked { };
    std::uint64_t mask { };
    std::array<std::uint64_t, 4U> planes { };
    std::uint32_t list_occurrences { };

    friend bool operator==(
        const GenericLogic9SidecarSnapshot&,
        const GenericLogic9SidecarSnapshot&) = default;
};

struct GenericBoundaryPlaneSnapshot final {
    runtime::simir::SignalId signal { };
    runtime::simir::ValueKind value_kind {
        runtime::simir::ValueKind::logic4
    };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::array<std::vector<std::uint64_t>, 4U> words;
};

struct GenericFrontierSnapshot final {
    bool found { };
    runtime::simir::RegionFrontierExecutionModeV2 execution_mode {
        runtime::simir::RegionFrontierExecutionModeV2::systemverilog_active
    };
    std::uint32_t generic_update_ack_count { };
    bool mutable_plane_roles_absent { };
    std::vector<GenericBoundaryPlaneSnapshot> boundary_planes;
};

struct GenericStagedWordUpdateSnapshot final {
    runtime::simir::SignalId signal { };
    std::optional<runtime::PackedLogic4> value;
};

struct GenericFixtureSnapshot final {
    std::vector<GenericSignalSnapshot> signals;
    std::optional<bool> process_graph_pure;
    std::vector<runtime::simir::SignalId> pending_update_signals;
    std::vector<runtime::PackedLogic4> pending_update_values;
    GenericFrontierSnapshot frontier;
    std::array<std::size_t, 2U> pending_update_sizes { };
    bool update_commit_scheduled { };
    std::vector<GenericStagedWordUpdateSnapshot> unresolved_word_updates;
    std::vector<GenericStagedWordUpdateSnapshot>
        direct_single_driver_word_updates;
    std::uint64_t native_member_dispatches { };
    std::uint64_t generic_projected_region_attempts { };
    std::uint64_t generic_projected_region_backend_runs { };
    std::uint64_t generic_projected_region_completions { };
    std::uint64_t generic_projected_region_members { };
    std::uint64_t generic_projected_region_declines { };
    std::uint64_t generic_projected_region_failures { };
    std::size_t process_executor_resume_calls { };
    std::size_t region_completion_prepare_calls { };
    std::size_t region_completion_stage_calls { };
    std::size_t region_completion_commit_calls { };
    std::size_t checked_suffix_resumes { };
    std::uint32_t native_logic9_sidecar_count { };
    std::vector<GenericLogic9SidecarSnapshot> logic9_sidecars;
};

struct GenericTaskKeySnapshot final {
    runtime::StableOrder stable_order { };
    std::uint64_t sequence { };
    std::uint64_t payload { };

    friend bool operator==(
        const GenericTaskKeySnapshot&, const GenericTaskKeySnapshot&) = default;
};

inline constexpr std::size_t generic_frontier_probe_task_capacity = 64U;
inline constexpr std::size_t generic_frontier_probe_write_capacity = 16U;
inline constexpr std::size_t generic_frontier_probe_word_capacity = 16U;

struct GenericBatchTaskCapture final {
    GenericTaskKeySnapshot key;
    runtime::simir::ProcessId process_id { };
};

/// Passive observation around the real generated entry. The test provider
/// delegates to the production LLVM provider and only records the live
/// borrowed scheduler frontier before calling its genuine entry pointer.
struct GenericFrontierEntryCapture final {
    bool called { };
    bool frontier_present { };
    bool tasks_truncated { };
    std::uint64_t generation { };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    runtime::SchedulerPhase phase { runtime::SchedulerPhase::active };
    std::size_t borrowed_task_count { };
    std::size_t frontier_cursor { };
    std::size_t frontier_end { };
    std::array<GenericBatchTaskCapture,
        generic_frontier_probe_task_capacity> borrowed_tasks { };
    std::uint32_t frame_task_count { };
    std::uint32_t frame_task_cursor { };
    runtime::simir::RegionFrontierStatusV2 status {
        runtime::simir::RegionFrontierStatusV2::decline_before_mutation
    };
};

/// A bounded copy of a generated pending descriptor. The maximum fixture
/// width is 1024 bits, so sixteen words per plane cover every tested width
/// without allocation in the failure callback.
struct GenericPendingWriteProbe final {
    std::uint32_t member_index { };
    std::uint32_t member_process_id { };
    std::uint32_t signal_slot { };
    runtime::simir::SignalId signal_id { };
    std::uint32_t signal_owner_process_id { };
    std::uint32_t source_instruction { };
    runtime::simir::ValueKind value_kind {
        runtime::simir::ValueKind::logic4
    };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::uint64_t origin_time { };
    std::uint64_t origin_delta { };
    std::uint64_t origin_round { };
    std::uint64_t origin_stable_order { };
    std::uint64_t origin_sequence { };
    std::uint32_t origin_process_domain { };
    std::uint32_t origin_phase { };
    bool words_truncated { };
    std::array<std::array<std::uint64_t,
        generic_frontier_probe_word_capacity>, 4U> value_planes { };
};

/// Fixed-size, allocation-free observations for a failure callback that runs
/// while the real generic batch frontier is borrowed. It also retains the
/// original consumed-prefix keys after the callback returns, without exposing
/// mutable frame storage or creating production history.
struct GenericQueuedMemberSnapshot final {
    runtime::simir::ProcessId process_id { };
    bool process_queued { };
    bool ready { };
    bool receipt_valid { };
    GenericTaskKeySnapshot key;
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    runtime::SchedulerPhase phase { runtime::SchedulerPhase::active };
    std::uint64_t static_trigger_mask { };
    std::uint64_t process_static_trigger_mask { };
};

struct GenericFrontierProbe final {
    bool runtime_found { };
    bool runtime_invalidated { };
    bool runtime_graph_epochs_current { };
    bool frontier_present { };
    bool frontier_tasks_truncated { };
    std::uint64_t frontier_generation { };
    runtime::SimulationTick frontier_time { };
    std::uint64_t frontier_delta { };
    std::uint64_t frontier_systemverilog_round { };
    runtime::simir::ProcessSchedulingDomain frontier_process_domain {
        runtime::simir::ProcessSchedulingDomain::generic
    };
    runtime::SchedulerPhase frontier_phase { runtime::SchedulerPhase::active };
    std::size_t frontier_cursor { };
    std::size_t frontier_end { };
    std::size_t frontier_task_count { };
    std::array<GenericTaskKeySnapshot,
        generic_frontier_probe_task_capacity> frontier_tasks { };

    std::uint64_t runtime_generation { };
    runtime::SimulationTick runtime_time { };
    std::uint64_t runtime_delta { };
    std::uint64_t runtime_systemverilog_round { };
    runtime::simir::ProcessSchedulingDomain runtime_process_domain {
        runtime::simir::ProcessSchedulingDomain::generic
    };
    runtime::SchedulerPhase runtime_phase { runtime::SchedulerPhase::active };
    std::uint32_t runtime_task_count { };
    std::uint32_t runtime_task_cursor { };
    std::uint32_t runtime_signal_slot_count { };
    std::uint32_t pending_write_count { };
    std::uint32_t staged_event_count { };
    std::uint32_t generic_update_ack_count { };
    bool pending_writes_truncated { };
    std::size_t captured_pending_write_count { };
    std::array<GenericPendingWriteProbe,
        generic_frontier_probe_write_capacity> pending_writes { };
    std::uint64_t native_member_dispatches { };
    std::array<std::size_t, 2U> pending_update_sizes { };
    bool runtime_original_tasks_truncated { };
    std::size_t runtime_original_task_count { };
    std::array<GenericTaskKeySnapshot,
        generic_frontier_probe_task_capacity> runtime_original_tasks { };
    bool generic_queued_members_truncated { };
    std::size_t generic_queued_member_count { };
    std::array<GenericQueuedMemberSnapshot,
        generic_frontier_probe_task_capacity> generic_queued_members { };
};

static_assert(std::is_trivially_copyable_v<GenericFrontierProbe>);

/// Builds one generic whole-WriteUpdate process. The LLVM modes use both the
/// production LlvmProcessExecutor and the production region-frontier provider;
/// interpreter mode is the checked semantic reference. The checked suffix
/// shares the root input by default; the sixth bool gives it a separate input.
/// Typed kind and policy follow that bool. The final bool opts into a separate
/// production-LLVM WriteUpdateSlice component used only by the partial-prefix
/// witness; it automatically receives its own input.
class GenericWholeWriteFixture final {
public:
    GenericWholeWriteFixture(GenericFixtureMode mode,
        std::uint32_t width = 1U, std::size_t whole_write_sites = 1U,
        bool include_checked_suffix = false,
        bool same_prefix_old_snapshot_chain = false,
        bool independent_checked_suffix_input = false,
        runtime::simir::ValueKind value_kind =
            runtime::simir::ValueKind::logic4,
        GenericFixturePolicy policy = { },
        bool include_llvm_cohort_slice_suffix = false,
        bool track_region_completion_calls = false,
        bool certify_parked_executor = true);
    ~GenericWholeWriteFixture();

    GenericWholeWriteFixture(const GenericWholeWriteFixture&) = delete;
    GenericWholeWriteFixture& operator=(const GenericWholeWriteFixture&) = delete;
    GenericWholeWriteFixture(GenericWholeWriteFixture&&) noexcept;
    GenericWholeWriteFixture& operator=(GenericWholeWriteFixture&&) noexcept;

    void start_and_settle();
    /// The optional key output must outlive dispatch of the scheduled callback.
    void schedule_input(runtime::PackedLogic4 value,
        runtime::SimulationTick time = 1U, runtime::StableOrder order = 0U,
        std::function<void()> before_frontier = { },
        runtime::StableOrder before_frontier_order = 0U,
        std::optional<runtime::SchedulerOrderKey>* before_frontier_key = nullptr);
    [[nodiscard]] runtime::SchedulerOrderKey
    schedule_next_delta_active_stop(runtime::StableOrder order);
    /// Prepare a late observation and stop at that exact foreign scheduler key.
    void observe_signal_and_stop(runtime::simir::SignalId signal);
    void deposit_signal(runtime::simir::SignalId signal,
        runtime::PackedLogic4 value);
    [[nodiscard]] runtime::SchedulerBatchCompactionStats
    generic_batch_compaction_stats() const noexcept;
    void set_output_hook(runtime::simir::Interpreter::OutputHook hook);

    /// Installs an ordinary Active observer after the selected generic Active
    /// prefix and stops before its deferred Update publication.
    [[nodiscard]] runtime::RunResult run_to_pre_update_cut();
    /// After deposits made between scheduler runs, append a maximum-order
    /// Active sentinel and stop after the ordinary queued batch.
    [[nodiscard]] runtime::RunResult
    run_to_pre_update_cut_after_external_deposits();
    /// Re-run the still-pending original prefix after an exception. Any cut
    /// observer installed by schedule_input() remains a one-shot queued task.
    [[nodiscard]] runtime::RunResult retry_frontier_attempt();
    /// Continue only after either pre-Update cut helper returned stopped.
    [[nodiscard]] runtime::RunResult resume_update_publication();
    [[nodiscard]] runtime::RunResult run_to_completion();

    [[nodiscard]] GenericFixtureSnapshot snapshot() const;
    [[nodiscard]] std::array<std::size_t, 2U>
    pending_update_sizes() const noexcept;
    [[nodiscard]] std::uint64_t native_member_dispatches() const noexcept;
    [[nodiscard]] GenericFrontierProbe
    probe_frontier_state() const noexcept;
    [[nodiscard]] GenericFrontierEntryCapture
    frontier_entry_capture() const noexcept;
    [[nodiscard]] std::vector<GenericTaskKeySnapshot>
    generic_frontier_task_keys() const;

    [[nodiscard]] runtime::simir::SignalId input_signal() const noexcept;
    [[nodiscard]] std::span<const runtime::simir::SignalId>
    output_signals() const noexcept;
    [[nodiscard]] runtime::simir::ProcessId process_id() const noexcept;
    [[nodiscard]] runtime::simir::ValueKind value_kind() const noexcept;
    [[nodiscard]] bool region_kernel_enabled() const noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId>
    checked_suffix_signal() const noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId>
    checked_suffix_input_signal() const noexcept;
    [[nodiscard]] std::optional<runtime::simir::SignalId>
    cohort_slice_suffix_signal() const noexcept;
    [[nodiscard]] std::optional<runtime::simir::ProcessId>
    cohort_slice_suffix_process_id() const noexcept;
    [[nodiscard]] std::size_t checked_suffix_resumes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::tests::app::frontier
