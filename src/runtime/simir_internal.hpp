// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_coverage.hpp"
#include <deque>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::runtime::simir {

struct RandomDistributionResult {
    std::int32_t value { };
    std::int32_t seed { };
};

[[nodiscard]] RandomDistributionResult evaluate_random_distribution(
    RandomDistributionKind kind,
    std::int32_t seed,
    std::int32_t first,
    std::optional<std::int32_t> second);

void validate_container_value(const ContainerValue& value);

[[nodiscard]] std::string error_text(ProcessId process,
    InstructionIndex instruction,
    const std::string& message);

[[nodiscard]] std::optional<SignalId> output_signal(
    const Operation& operation);

[[nodiscard]] std::string format_output_value(
    const PackedLogic4& value,
    const OutputFormat format,
    const bool signed_decimal,
    const bool suppress_leading_zero);

[[nodiscard]] std::string make_formatted_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const OutputFormat format,
    const PackedLogic4& value,
    const bool signed_decimal,
    const bool suppress_leading_zero,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad,
    SystemVerilogScalarKind scalar_kind = SystemVerilogScalarKind::None);

[[nodiscard]] std::string make_time_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const SimulationTick tick,
    const SystemVerilogTimeFormat& time_format,
    const bool use_timeformat_width,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad);

[[nodiscard]] bool edge_matches(EdgeKind edge, Logic4 old_value,
    Logic4 new_value) noexcept;

[[nodiscard]] PackedLogic4 unary_not(const PackedLogic4& source);

[[nodiscard]] Logic4 truth_value(const PackedLogic4& source);

[[nodiscard]] PackedLogic4 logical_not(const PackedLogic4& source);

[[nodiscard]] PackedLogic4 logical_binary(
    const LogicalBinaryOperator operation,
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 reduce_value(
    const ReductionOperator operation,
    const PackedLogic4& source);

[[nodiscard]] PackedLogic4 count_ones_value(
    const PackedLogic4& source);

[[nodiscard]] PackedLogic4 count_bits_value(
    const PackedLogic4& source,
    const std::uint8_t state_mask);

[[nodiscard]] constexpr ShiftOperator reverse_shift(
    const ShiftOperator operation) noexcept;

[[nodiscard]] PackedLogic4 shift_value(
    ShiftOperator operation,
    const PackedLogic4& value,
    const PackedLogic4& amount_value,
    const bool signed_amount);

[[nodiscard]] PackedLogic4 extract_value(
    const PackedLogic4& source,
    const std::size_t offset,
    const std::size_t width);

[[nodiscard]] PackedLogic4 insert_value(
    PackedLogic4 target,
    const PackedLogic4& source,
    const std::size_t offset);

[[nodiscard]] std::uint32_t dynamic_index_offset(
    const PackedLogic4& index,
    const DynamicIndex& selection);

[[nodiscard]] PackedLogic4 dynamic_part_insert_value(
    PackedLogic4 target,
    const PackedLogic4& source,
    const PackedLogic4& base,
    const DynamicPartIndex& selection);

[[nodiscard]] PackedLogic4 concatenate_values(
    const std::vector<PackedLogic4>& operands,
    const std::size_t expected_width);

[[nodiscard]] bool has_unknown(const PackedLogic4& value);

[[nodiscard]] bool is_zero(const PackedLogic4& value);

[[nodiscard]] bool is_one(const PackedLogic4& value);

[[nodiscard]] bool is_all_ones(const PackedLogic4& value);

[[nodiscard]] int compare_known_unsigned(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 subtract_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 add_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 negate_known(
    const PackedLogic4& value);

[[nodiscard]] PackedLogic4 multiply_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] PackedLogic4 power_known(
    const PackedLogic4& base,
    const PackedLogic4& exponent,
    const bool signed_exponent);

[[nodiscard]] PackedLogic4 divide_known(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor,
    const bool return_remainder);

struct SignedDivision {
    PackedLogic4 quotient;
    PackedLogic4 remainder;
};

[[nodiscard]] SignedDivision divide_known_signed(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor);

[[nodiscard]] PackedLogic4 binary_value(BinaryOperator operation,
    const PackedLogic4& lhs,
    const PackedLogic4& rhs);

[[nodiscard]] std::int64_t checked_integer_operand(
    const PackedLogic4& value);

[[nodiscard]] PackedLogic4 packed_integer(
    std::int64_t value, std::size_t width);

[[nodiscard]] PackedLogic4 integer_unary_value(
    const IntegerUnaryOperator operation,
    const PackedLogic4& source);

[[nodiscard]] PackedLogic4 integer_binary_value(
    const IntegerBinaryOperator operation,
    const PackedLogic4& lhs_value,
    const PackedLogic4& rhs_value);

void check_integer_range(
    const PackedLogic4& source,
    std::int64_t lower,
    std::int64_t upper);

[[nodiscard]] PackedLogic4 conditional_value(
    const PackedLogic4& condition,
    const PackedLogic4& when_true,
    const PackedLogic4& when_false);

void validate_module_path_expression(
    const ModulePathExpression& expression,
    std::span<const Signal> signals);

[[nodiscard]] Logic9 evaluate_vital_timing_check(
    const VitalTimingCheck& operation,
    VitalTimingState& state,
    SimulationTick now,
    Logic9 test,
    bool test_event,
    Logic9 reference,
    bool reference_event,
    bool trigger_event);

void validate_container_value(const ContainerValue& value);
[[nodiscard]] PackedLogic4 default_container_element(
    const ContainerType& type);

struct Interpreter::Impl : SchedulerBatchTask {
    struct ExecutionContext;

    using SharedContainerValue = std::shared_ptr<ContainerValue>;

    struct ProcessFrame {
        std::vector<PackedLogic4> registers;
        std::vector<std::string> string_registers;
        std::vector<SharedContainerValue> container_registers;
        std::vector<VitalMemoryState> vital_memories;
    };

    struct ProcessState {
        struct DeferredExecutor {
            std::function<bool()> ready;
            std::function<std::unique_ptr<ProcessExecutor>()> take;
        };

        struct CallableFrameState {
            std::uint32_t identity { };
            std::vector<RegisterId> packed_ids;
            std::vector<StringRegisterId> string_ids;
            std::vector<ContainerRegisterId> container_ids;
            std::vector<PackedLogic4> packed;
            std::vector<std::string> strings;
            std::vector<SharedContainerValue> containers;
            std::size_t storage_bytes { };
        };

        Process program;
        ProcessId design_process { };
        InstructionIndex pc { };
        std::shared_ptr<ProcessFrame> frame;
        std::unique_ptr<ProcessExecutor> executor;
        std::optional<DeferredExecutor> deferred_executor;
        std::vector<Sensitivity> dynamic_sensitivity;
        std::vector<bool> dynamic_triggered;
        std::uint64_t static_trigger_mask { Process::full_static_trigger_mask };
        SourceLocation current_source;
        std::string current_scope;
        bool queued { };
        bool waiting_on_static { };
        bool waiting_on_signal { };
        std::optional<ContainerObjectId> waiting_on_container;
        bool dynamic_wait_all { };
        std::vector<std::optional<SignalId>> wait_order_events;
        std::size_t wait_order_index { };
        std::optional<RegisterId> wait_order_result;
        std::optional<InstructionIndex> wait_timeout_origin;
        std::optional<SimulationTick> wait_timeout_deadline;
        std::optional<RegisterId> wait_timeout_result;
        std::uint64_t wait_timeout_generation { };
        std::uint64_t random_state { };
        std::map<InstructionIndex, VitalTimingState> vital_timing_states;
        std::map<InstructionIndex, VitalDelayState> vital_delay_states;
        std::vector<InstructionIndex> dynamic_call_stack;
        std::vector<CallableFrameState> callable_frames;
        std::size_t callable_frame_storage_bytes { };
        std::optional<CallableFrameState> suspended_callable_context;
        std::size_t callable_context_storage_bytes { };
        std::uint32_t generation { };
        ProcessStatus status { ProcessStatus::running };
        ProcessStatus suspended_status { ProcessStatus::running };
        bool suspended { };
        bool suspended_wake { };
        bool halted { };
        bool killed { };
        std::optional<ProcessId> fork_parent;
        std::optional<InstructionIndex> fork_site;
        std::optional<std::uint64_t> fork_group;
        std::set<ProcessId> live_children;
        std::map<InstructionIndex, std::set<ProcessId>> active_fork_sites;
        bool waiting_for_children { };
        std::optional<std::uint64_t> waiting_fork_group;
        std::optional<ProcessId> waiting_process;
        std::set<ProcessId> process_waiters;
        std::uint64_t profile_calls { };
        std::uint64_t profile_interpreter_operations { };
        std::uint64_t profile_native_resumes { };
        std::uint64_t profile_updates { };
        std::uint64_t profile_total_nanoseconds { };
        std::uint64_t profile_native_nanoseconds { };
        bool track_interpreter_operations { };
        std::uint64_t interpreter_operations { };
    };

    struct MailboxReader {
        ProcessId process { };
        RegisterId destination { };
        bool peek { };
    };

    struct SampledHistoryKey {
        SignalId signal { };
        std::optional<SignalId> clock;
        SampledClockEdge edge { SampledClockEdge::any };
        std::optional<SignalId> gate;

        auto operator<=>(const SampledHistoryKey&) const = default;
    };

    struct SampledHistoryState {
        std::deque<PackedLogic4> values;
        std::optional<std::pair<SimulationTick, std::uint64_t>> last_slot;
    };

    struct MailboxWriter {
        ProcessId process { };
        PackedLogic4 value;
    };

    struct MailboxState {
        std::uint32_t element_width { };
        std::size_t capacity { };
        std::deque<PackedLogic4> entries;
        std::deque<MailboxReader> readers;
        std::deque<MailboxWriter> writers;
    };

    struct SemaphoreWaiter {
        ProcessId process { };
        std::uint32_t keys { };
    };

    struct SemaphoreState {
        std::uint32_t keys { };
        std::deque<SemaphoreWaiter> waiters;
    };

    struct StochasticQueueEntry {
        std::int32_t job_id { };
        std::int32_t information_id { };
        SimulationTick arrival { };
    };

    struct StochasticQueueState {
        bool lifo { };
        std::uint32_t maximum_length { };
        std::deque<StochasticQueueEntry> entries;
        std::uint64_t arrivals { };
        std::optional<SimulationTick> last_arrival;
        SimulationTick total_interarrival { };
        std::uint64_t maximum_occupancy { };
        std::optional<SimulationTick> shortest_wait;
        SimulationTick total_removed_wait { };
        std::uint64_t removals { };
    };

    struct ForkGroup {
        ProcessId parent { };
        InstructionIndex site { };
        ForkJoinKind join { ForkJoinKind::all };
        std::set<ProcessId> children;
        bool parent_resumed { };
    };

    struct Fanout {
        ProcessId process { };
        EdgeKind edge = EdgeKind::any;
        std::uint64_t static_trigger_mask { Process::full_static_trigger_mask };
    };

    struct StaticSensitivityCohort {
        std::vector<ProcessId> members;
        std::vector<ProcessId> ready;
        std::uint64_t fanout_visit { };
    };

    struct NativeStaticRegion {
        std::vector<ProcessId> members;
        std::vector<std::unique_ptr<ProcessExecutionContext>> contexts;
        std::vector<ProcessCohortResumeEntry> entries;
        std::vector<std::uint8_t> active;
        std::vector<std::size_t> ready_offsets;
        bool prepared { };
    };

    struct PendingUpdate {
        enum Flag : std::uint8_t {
            has_driver = 1U << 0U,
            has_offset = 1U << 1U,
            has_packed_value = 1U << 2U,
        };

        SignalId signal { };
        ProcessId driver { };
        std::uint32_t offset { };
        std::uint32_t packed_value { };
        Logic4Word word;
        std::uint8_t flags { };

        PendingUpdate(
            SignalId signal_value,
            std::optional<ProcessId> driver_value,
            std::optional<std::size_t> offset_value,
            Logic4Word word_value,
            std::optional<std::size_t> packed_value_index)
            : signal(signal_value)
            , driver(driver_value.value_or(0U))
            , offset(static_cast<std::uint32_t>(offset_value.value_or(0U)))
            , packed_value(static_cast<std::uint32_t>(
                  packed_value_index.value_or(0U)))
            , word(word_value)
            , flags(static_cast<std::uint8_t>(
                  (driver_value
                          ? static_cast<std::uint8_t>(has_driver)
                          : std::uint8_t { })
                  | (offset_value
                          ? static_cast<std::uint8_t>(has_offset)
                          : std::uint8_t { })
                  | (packed_value_index
                          ? static_cast<std::uint8_t>(has_packed_value)
                          : std::uint8_t { })))
        {
            if ((offset_value
                    && *offset_value
                        > std::numeric_limits<std::uint32_t>::max())
                || (packed_value_index
                    && *packed_value_index
                        > std::numeric_limits<std::uint32_t>::max())) {
                throw std::length_error(
                    "pending update metadata exceeds its compact representation");
            }
        }

        [[nodiscard]] bool driver_present() const noexcept
        {
            return (flags & has_driver) != 0U;
        }
        [[nodiscard]] bool offset_present() const noexcept
        {
            return (flags & has_offset) != 0U;
        }
        [[nodiscard]] bool packed_value_present() const noexcept
        {
            return (flags & has_packed_value) != 0U;
        }
    };

    static_assert(sizeof(PendingUpdate) <= 48U);

    struct PendingDriverCommit {
        std::optional<ProcessId> driver;
        PackedLogic4 value;
    };

    struct DirectSingleDriverRoute {
        ProcessId process { };
        PackedLogic4* value { };
    };

    struct DirectSingleDriverLogic9WordUpdate {
        std::array<std::uint64_t, 4U> planes { };
        std::uint64_t mask { };
        std::uint32_t width { };
        ProcessId process { };
        std::uint32_t active { };
    };

    enum class PendingEventKind : std::uint8_t {
        none,
        delta,
        timed,
    };

    struct EventState {
        PendingEventKind kind { PendingEventKind::none };
        SimulationTick due { };
        std::uint64_t generation { };
    };

    struct InertialDriverKey {
        ProcessId process { };
        SignalId signal { };
        std::uint32_t offset { };
        std::uint32_t width { };

        friend bool operator==(
            const InertialDriverKey&,
            const InertialDriverKey&) = default;
    };

    struct InertialDriverKeyHash {
        [[nodiscard]] std::size_t operator()(
            const InertialDriverKey& key) const noexcept;
    };

    struct PendingInertialWrite {
        ScheduledTaskHandle handle;
        PackedLogic4 source_value;
    };

    struct PendingModulePathWrite {
        ScheduledTaskHandle handle;
        PackedLogic4 source_value;
        SimulationTick detected_at { };
        SimulationTick target_time { };
        SimulationTick reject_limit { };
        SimulationTick error_limit { };
        std::optional<SimulationTick> retain_delay;
        ModulePathPulseStyle pulse_style { ModulePathPulseStyle::onevent };
        bool show_cancelled { };
    };

    struct ModuleTimingCheckState {
        std::optional<SimulationTick> last_reference;
        std::optional<SimulationTick> last_data;
        std::optional<SimulationTick> last_terminal_change;
        ScheduledTaskHandle deadline;
        bool active { };
    };

    struct ProjectedDriverKey {
        ProcessId process { };
        SignalId signal { };
        std::uint32_t offset { };

        friend bool operator==(
            const ProjectedDriverKey&,
            const ProjectedDriverKey&) = default;
    };

    struct ProjectedDriverKeyHash {
        [[nodiscard]] std::size_t operator()(
            const ProjectedDriverKey& key) const noexcept;
    };

    struct ProjectedTransaction {
        std::uint64_t id { };
        SimulationTick time { };
        Logic9 value { Logic9::x };
        ScheduledTaskHandle handle;
    };

    struct ProjectedDriverState {
        std::vector<ProjectedTransaction> transactions;
    };

    struct FileState {
        ProcessId owner { };
        std::filesystem::path path;
        std::string mode;
        std::unique_ptr<std::fstream> stream;
        std::optional<std::uint8_t> pushback;
        std::string last_error;
        bool closed { };
        bool readable { };
        bool writable { };
    };

    explicit Impl(
        SchedulerOptions options,
        const std::uint64_t seed);

    Scheduler scheduler;
    std::uint64_t root_seed { 1 };
    std::vector<Signal> signals;
    std::vector<StringObject> string_objects;
    std::vector<ContainerObject> container_objects;
    std::vector<SharedContainerValue> default_container_values;
    std::vector<std::optional<ContainerSignalAlias>>
        container_signal_aliases;
    std::vector<std::optional<std::uint64_t>>
        container_materialized_revisions;
    std::vector<std::vector<ContainerObjectId>>
        signal_container_aliases;
    std::filesystem::path file_root;
    std::vector<std::string> plusargs;
    SystemVerilogTimeFormat time_format;
    std::map<FileHandle, FileState> files;
    FileHandle next_file_handle { 1 };
    std::uint32_t next_multichannel_channel { 1 };
    std::vector<std::uint64_t> direct_signal_aval;
    std::vector<std::uint64_t> direct_signal_bval;
    std::vector<std::uint64_t> direct_signal_logic9_plane0;
    std::vector<std::uint64_t> direct_signal_logic9_plane1;
    std::vector<std::uint64_t> direct_signal_logic9_plane2;
    std::vector<std::uint64_t> direct_signal_logic9_plane3;
    // The dense planes are authoritative between native phase boundaries for
    // eligible one-word signals. Packed mirrors are materialized lazily only
    // when a generic observer crosses back into the interpreter/runtime API.
    std::vector<std::uint64_t> direct_signal_last_aval;
    std::vector<std::uint64_t> direct_signal_last_bval;
    std::vector<std::uint64_t> direct_signal_last_logic9_plane0;
    std::vector<std::uint64_t> direct_signal_last_logic9_plane1;
    std::vector<std::uint64_t> direct_signal_last_logic9_plane2;
    std::vector<std::uint64_t> direct_signal_last_logic9_plane3;
    std::vector<std::uint8_t> direct_signal_materialization_pending;
    std::vector<std::uint64_t> direct_wide_signal_aval;
    std::vector<std::uint64_t> direct_wide_signal_bval;
    std::vector<std::uint64_t> direct_wide_signal_logic9_plane2;
    std::vector<std::uint64_t> direct_wide_signal_logic9_plane3;
    std::vector<std::uint32_t> direct_wide_signal_offsets;
    std::vector<ProcessId> direct_single_driver_processes;
    std::vector<ProcessId> stable_single_writer_processes;
    std::vector<bool> signal_transaction_observed;
    std::vector<std::uint32_t> signal_writer_counts;
    std::uint64_t signal_writer_revision { };
    std::vector<PackedLogic4> driven_values;
    std::vector<std::map<ProcessId, PackedLogic4>> driver_values;
    std::vector<std::map<ProcessId, DriveStrength>> driver_strengths;
    using ForcedDriverMap = std::map<ProcessId, PackedLogic4>;
    std::vector<std::unique_ptr<ForcedDriverMap>> forced_driver_values;
    std::vector<std::unique_ptr<ForcedDriverMap>> forced_driver_masks;
    std::vector<std::unique_ptr<PackedLogic4>> external_driver_values;
    std::vector<std::optional<ScheduledTaskHandle>> charge_decay_handles;
    std::vector<std::unique_ptr<PackedLogic4>> charge_values;
    std::vector<PackedLogic4> signal_last_values;
    std::vector<std::uint64_t> signal_value_revisions;
    std::vector<PackedLogic4> sampled_values;
    std::vector<PackedLogic4> sampled_defaults;
    bool requires_sampled_values { };
    std::map<SampledHistoryKey, SampledHistoryState> sampled_histories;
    std::vector<std::unique_ptr<PackedLogic4>> forced_values;
    std::vector<std::unique_ptr<PackedLogic4>> forced_masks;
    // Named-event variables carry synchronization-object identities rather
    // than copied packed values. Each event starts with its own stable object.
    std::vector<std::optional<SignalId>> event_identities;
    std::deque<ProcessState> processes;
    std::vector<MailboxState> mailboxes;
    std::vector<SemaphoreState> semaphores;
    std::unordered_map<std::int32_t, StochasticQueueState>
        stochastic_queues;
    std::vector<ModulePath> module_paths;
    std::vector<ModuleTimingCheck> module_timing_checks;
    std::vector<ModuleTimingCheckState> module_timing_check_states;
    std::map<std::uint64_t, ForkGroup> fork_groups;
    std::uint64_t next_fork_group { 1 };
    std::uint32_t next_process_generation { 1 };
    std::vector<std::vector<Fanout>> static_fanout;
    // Exact static-sensitivity cohorts span the complete elaborated design,
    // including aliases which resolve to the same clock SignalId across
    // hierarchy. Cohorts batch scheduler dispatch while retaining each
    // member's process identity, frame, driver ownership, and program counter.
    std::vector<StaticSensitivityCohort> static_sensitivity_cohorts;
    std::vector<std::size_t> static_sensitivity_cohort_by_process;
    std::unordered_map<std::string, std::size_t>
        static_sensitivity_cohort_by_key;
    std::uint64_t static_fanout_visit_generation { };
    std::vector<NativeStaticRegion> native_static_regions;
    std::vector<std::size_t> native_static_region_by_process;
    std::vector<std::size_t> native_static_region_offset_by_process;
    bool native_static_regions_built { };
    std::uint64_t native_static_region_attempts { };
    std::uint64_t native_static_region_calls { };
    std::uint64_t native_static_region_ready { };
    std::uint64_t native_static_region_consumed { };
    std::array<std::uint64_t, 5U> native_static_region_declines { };
    static constexpr std::uint64_t native_static_region_payload
        = UINT64_C(1) << 63U;
    std::vector<std::vector<Fanout>> dynamic_fanout;
    std::vector<std::vector<ProcessId>> container_dynamic_fanout;
    std::vector<EventState> event_states;
    std::vector<std::optional<std::pair<
        SimulationTick, std::uint64_t>>>
        signal_events;
    std::vector<std::optional<std::pair<
        SimulationTick, std::uint64_t>>>
        signal_transactions;
    std::vector<PendingUpdate> pending_updates;
    std::vector<PackedLogic4> pending_update_values;
    // Update-phase scratch is indexed by the dense signal identity and reused
    // across deltas. Only touched entries are reset after publication, avoiding
    // per-delta map/set construction and whole-container snapshots.
    std::vector<std::optional<PackedLogic4>> unresolved_update_scratch;
    std::vector<std::vector<PendingDriverCommit>> driver_update_scratch;
    std::vector<DirectSingleDriverRoute> direct_single_driver_routes;
    std::vector<ProcessNativeWordUpdate>
        direct_single_driver_word_scratch;
    std::vector<DirectSingleDriverLogic9WordUpdate>
        direct_single_driver_logic9_word_scratch;
    std::vector<SignalId> unresolved_update_signals;
    // Ordinary single-driver Verilog nets do not need a temporary
    // per-driver collection or a resolution pass. Native word updates stage
    // their final value here, while retaining the driver slot for VPI/debug
    // observation at the update boundary.
    std::vector<SignalId> direct_single_driver_update_signals;
    std::vector<SignalId> native_word_update_signals;
    std::uint32_t native_word_update_count { };
    std::vector<SignalId> native_logic9_word_update_signals;
    std::uint32_t native_logic9_word_update_count { };
    std::vector<bool> direct_single_driver_commit_marked;
    std::vector<SignalId> driver_update_signals;
    std::vector<SignalId> resolved_update_signals;
    std::vector<bool> resolved_update_marked;
    std::vector<std::pair<SignalId, PackedLogic4>> update_commit_scratch;
    std::unordered_set<std::uint64_t> pending_channel_updates;
    std::unordered_map<
        InertialDriverKey,
        PendingInertialWrite,
        InertialDriverKeyHash>
        pending_inertial_writes;
    std::unordered_map<
        InertialDriverKey,
        PendingModulePathWrite,
        InertialDriverKeyHash>
        pending_module_path_writes;
    std::unordered_map<
        ProjectedDriverKey,
        ProjectedDriverState,
        ProjectedDriverKeyHash>
        projected_drivers;
    std::uint64_t next_projected_transaction_id { 1 };
    SignalChangeHook signal_change_hook;
    NativeSignalObservationRequiredHook
        native_signal_observation_required_hook;
    NativeSignalObservationAnyHook native_signal_observation_any_hook;
    StoredSignalChangeHook stored_signal_change_hook;
    DriverChangeHook driver_change_hook;
    EventTriggerHook event_trigger_hook;
    ContainerObjectChangeHook container_object_change_hook;
    ScalarSignalChangeHook scalar_signal_change_hook;
    ExecutionPointHook execution_point_hook;
    OutputHook output_hook;
    ReportHook report_hook;
    CoverageSampleHook coverage_sample_hook;
    CoverageQueryHook coverage_query_hook;
    CoverageControlHook coverage_control_hook;
    CoverageAccessHook coverage_access_hook;
    CodeCoverageCounters code_coverage_counters;
    CodeCoverageOverflowHook code_coverage_overflow_hook;
    SystemCommandHook system_command_hook;
    VcdControlHook vcd_control_hook;
    CoverageDatabaseControlHook coverage_database_control_hook;
    ForkSpawnFilter fork_spawn_filter;
    ClassAllocateHook class_allocate_hook;
    ClassPropertyReadHook class_property_read_hook;
    ClassPropertyWriteHook class_property_write_hook;
    ClassMethodCallHook class_method_call_hook;
    ClassStaticPropertyReadHook class_static_property_read_hook;
    ClassStaticPropertyWriteHook class_static_property_write_hook;
    ClassStaticMethodCallHook class_static_method_call_hook;
    std::optional<MonitorInstall> monitor;
    ProcessId monitor_process { };
    std::optional<FileHandle> monitor_file_handle;
    bool monitor_enabled { true };
    std::uint64_t monitor_generation { };
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        monitor_publication;
    bool update_commit_scheduled { };
    bool has_bidirectional_switches { };
    bool switch_refreshing { };
    bool started { };
    bool validation_only { };
    bool stopped_by_design { };
    bool finals_ran { };
    bool process_profile_enabled { };
    bool process_profile_reported { };
    bool update_profile_enabled { };
    bool update_profile_reported { };
    std::uint64_t update_profile_commits { };
    std::uint64_t update_profile_updates { };
    std::uint64_t update_profile_whole { };
    std::uint64_t update_profile_slices { };
    std::uint64_t update_profile_unresolved { };
    std::uint64_t update_profile_resolved { };
    std::uint64_t update_profile_resolved_single_driver { };
    std::uint64_t update_profile_bits { };
    bool native_phase_profile_enabled { };
    std::uint64_t native_phase_profile_attempts { };
    std::uint64_t native_phase_profile_published { };
    std::uint64_t native_phase_profile_rejected_structure { };
    std::uint64_t native_phase_profile_rejected_observer { };
    std::uint64_t native_phase_profile_rejected_route { };
    std::uint64_t native_phase_profile_rejected_semantics { };
    std::uint64_t native_phase_profile_rejected_runtime { };
    std::uint64_t native_phase_profile_single_resumes { };
    std::uint64_t native_phase_profile_cohort_resumes { };
    std::uint64_t native_phase_profile_cohort_members { };
    bool native_process_count_profile_enabled { };
    std::vector<std::uint64_t> native_process_resume_counts;
    std::vector<std::uint64_t> native_process_single_resume_counts;
    std::vector<std::uint64_t> native_process_single_static_wait_counts;
    std::vector<std::uint64_t> native_process_cohort_resume_counts;
    std::vector<std::uint64_t> native_process_cohort_static_wait_counts;
    std::vector<std::uint64_t> native_process_word_fanout_ready_counts;
    std::vector<std::unordered_map<SignalId, std::uint64_t>>
        native_process_static_trigger_counts;
    std::vector<ProcessId> native_process_single_wave_processes;
    std::vector<std::size_t> native_process_single_wave_offsets;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        native_process_single_wave_identity;
    std::array<std::uint64_t, 6> native_process_single_boundary_counts { };
    std::array<std::uint64_t, 6> native_process_cohort_boundary_counts { };
    std::array<std::uint64_t, 9> native_process_simir_boundary_groups { };
    std::array<std::uint64_t, 32> native_process_scheduling_boundaries { };
    std::uint64_t native_process_word_changes { };
    std::uint64_t native_process_word_fanout_matches { };
    std::uint64_t native_process_word_fanout_ready { };
    bool native_update_profile_enabled { };
    std::uint64_t native_update_profile_calls { };
    std::uint64_t native_update_profile_fallbacks { };
    std::uint64_t native_update_profile_batches { };
    std::uint64_t native_update_profile_slots { };
    std::uint64_t native_update_profile_inactive { };
    std::uint64_t native_update_profile_untouched { };
    std::uint64_t native_update_profile_unchanged { };
    std::uint64_t native_update_profile_unchanged_direct_word { };
    std::uint64_t native_update_profile_unchanged_direct_packed { };
    std::uint64_t native_update_profile_unchanged_unresolved { };
    std::uint64_t native_update_profile_unchanged_resolved { };
    std::uint64_t native_update_profile_direct_word { };
    std::uint64_t native_update_profile_direct_packed { };
    std::uint64_t native_update_profile_unresolved { };
    std::uint64_t native_update_profile_resolved { };
    std::uint64_t native_update_profile_schedule_requests { };
    std::uint64_t native_update_profile_schedule_coalesced { };
    std::uint64_t native_update_profile_commits { };
    std::uint64_t native_update_profile_commit_word_signals { };
    std::uint64_t native_update_profile_commit_value_signals { };
    std::uint64_t native_update_profile_word_calls { };
    std::uint64_t native_update_profile_words { };
    std::uint64_t native_update_profile_word_fallbacks { };
    std::uint64_t native_update_profile_word_scalar { };
    std::uint64_t native_update_profile_word_unchanged { };
    std::uint64_t native_update_profile_word_direct { };
    std::uint64_t native_update_profile_word_unresolved { };
    std::uint64_t native_update_profile_word_resolved { };
    bool jit_skip_callable_frames { };
    std::set<std::uint32_t> program_owners;
    std::set<std::uint32_t> exited_programs;

    [[nodiscard]] Signal& get_signal(SignalId id);

    [[nodiscard]] const Signal& get_signal(SignalId id) const;

    void materialize_direct_signal(SignalId id);

    [[nodiscard]] ProcessState& get_process(ProcessId id);

    [[nodiscard]] ProcessFrame& ensure_process_frame(ProcessState& process);

    [[nodiscard]] static bool can_install_deferred_executor(
        const ProcessState& process) noexcept;

    [[nodiscard]] Logic9 execute_vital_timing_check(
        ProcessId process,
        InstructionIndex instruction,
        const VitalTimingCheck& operation);

    void execute_vital_delay(
        ProcessId process,
        InstructionIndex instruction,
        const VitalDelay& operation,
        const VitalDelayRuntimeValues& values);
    void execute_vital_delay_operation(
        ProcessId process_id,
        ProcessState& process,
        InstructionIndex instruction,
        const VitalDelay& operation);

    [[nodiscard]] PackedLogic4& get_register(ProcessState& process,
        RegisterId id);

    [[nodiscard]] std::string& get_string_register(
        ProcessState& process,
        StringRegisterId id);

    [[nodiscard]] StringObject& get_string_object(StringObjectId id);

    [[nodiscard]] const StringObject&
    get_string_object(StringObjectId id) const;

    [[nodiscard]] ContainerValue& get_container_register(
        ProcessState& process, ContainerRegisterId id);
    [[nodiscard]] const ContainerValue& read_container_register(
        const ProcessState& process, ContainerRegisterId id) const;
    [[nodiscard]] SharedContainerValue container_register_storage(
        const ProcessState& process, ContainerRegisterId id) const;
    void set_container_register_storage(
        ProcessState& process,
        ContainerRegisterId id,
        SharedContainerValue value);
    [[nodiscard]] SharedContainerValue default_container_register(
        const ContainerType& type);
    [[nodiscard]] ContainerObject& get_container_object(
        ContainerObjectId id);
    [[nodiscard]] const ContainerObject& get_container_object(
        ContainerObjectId id) const;
    [[nodiscard]] const ContainerValue&
    read_container_object_value(ContainerObjectId id);
    [[nodiscard]] bool read_container_object_element(
        ContainerObjectId id,
        std::size_t ordinal,
        PackedLogic4& result);
    void write_container_object_value(
        ContainerObjectId id, const ContainerValue& value);
    void write_container_object_element_value(
        ContainerObjectId id,
        const PackedLogic4& index,
        bool signed_index,
        bool linear_index,
        const PackedLogic4& value,
        ProcessId process,
        InstructionIndex instruction);

    void set_file_root(std::filesystem::path root);
    [[nodiscard]] FileHandle open_file(
        ProcessId process,
        std::string_view path,
        std::string_view mode);
    [[nodiscard]] FileState& checked_file(
        ProcessId process, FileHandle handle);
    void close_file(ProcessId process, FileHandle handle);
    void write_file(
        ProcessId process,
        FileHandle handle,
        std::string_view text,
        bool newline);
    [[nodiscard]] std::string read_file_line(
        ProcessId process,
        FileHandle handle,
        std::uint32_t& count);
    [[nodiscard]] std::int32_t read_file_character(
        ProcessId process, FileHandle handle);
    [[nodiscard]] std::int32_t unread_file_character(
        ProcessId process, FileHandle handle, std::int32_t character);
    [[nodiscard]] bool file_end_of_file(
        ProcessId process, FileHandle handle);
    [[nodiscard]] std::string file_error(
        ProcessId process, FileHandle handle, bool& has_error);
    [[nodiscard]] std::int32_t position_file(
        ProcessId process,
        FileHandle handle,
        FilePositionKind kind,
        std::int32_t offset,
        std::int32_t origin);
    void flush_file(ProcessId process, std::optional<FileHandle> handle);
    [[nodiscard]] FileHandle known_file_handle(
        ProcessState&, RegisterId);
    void execute_file(ProcessState&, const FileOpen&);
    void execute_file(ProcessState&, const FileClose&);
    void execute_file(ProcessState&, const FileWriteLiteral&);
    void execute_file(ProcessState&, const FileWriteFormatted&);
    void execute_file(ProcessState&, const FileWriteString&);
    void execute_file(ProcessState&, const FileReadLine&);
    void execute_file(ProcessState&, const FileEndOfFile&);
    void execute_file(ProcessState&, const FileErrorStatus&);
    void execute_file(ProcessState&, const FileScan&);
    void execute_file(ProcessState&, const FileBinaryRead&);
    void execute_file(ProcessState&, const FilePosition&);
    void execute_file(ProcessState&, const FileFlush&);

    void execute_container(ProcessState&, const ResizeContainer&);
    void execute_container(ProcessState&, const CopyContainerRegister&);
    void execute_container(ProcessState&, const ConditionalContainerSelect&);
    void execute_container(ProcessState&, const CompareContainers&);
    void execute_container(ProcessState&, const ReadContainerObject&);
    void execute_container(ProcessState&, const WriteContainerObject&);
    void execute_container(ProcessState&, const ContainerSize&);
    void execute_container(ProcessState&, const ContainerReduction&);
    void execute_container(ProcessState&, const OrderContainer&);
    void execute_container(ProcessState&, const LocateContainer&);
    void execute_container(ProcessState&, const ContainerRead&);
    void execute_container(ProcessState&, const ContainerWrite&);
    void execute_container(ProcessState&, const WriteContainerObjectElement&);
    void execute_container(ProcessState&, const ContainerStringRead&);
    void execute_container(ProcessState&, const ContainerStringWrite&);
    void execute_container(ProcessState&, const ContainerElementRead&);
    void execute_container(ProcessState&, const ContainerElementWrite&);
    void execute_container(ProcessState&, const ContainerAggregateRead&);
    void execute_container(ProcessState&, const ContainerAggregateWrite&);
    void execute_container(
        ProcessState&, const CopyContainerAggregateElement&);
    void execute_container(ProcessState&, const DeleteContainer&);
    void execute_container(ProcessState&, const ContainerExists&);
    void execute_container(ProcessState&, const TraverseContainer&);
    void execute_container(ProcessState&, const LoadMemory&);
    void execute_container(ProcessState&, const VitalMemoryDeclare&);
    void execute_container(ProcessState&, const PushContainer&);
    void execute_container(ProcessState&, const PopContainer&);
    void execute_string(ProcessState&, const StringMethod&);
    void execute_stochastic_queue(
        ProcessState&, const StochasticQueueOperation&);
    void execute_pla(ProcessState&, const PlaEvaluate&);

    [[nodiscard]] static ValueKind register_value_kind(
        const ProcessState& process,
        const RegisterId id);

    [[nodiscard]] static PackedLogic4 coerce_value_kind(
        PackedLogic4 value,
        const ValueKind kind);

    [[nodiscard]] PackedLogic4 normalize_signal_value(
        const SignalId signal,
        PackedLogic4 value) const;

    void remove_dynamic_wait(ProcessState& process);

    void write_process_register(
        ProcessState& process,
        const RegisterId destination,
        const PackedLogic4& value);

    void install_deferred_executor(ProcessState& process);

    [[nodiscard]] std::int32_t control_coverage(
        const CoverageControlEvent& event) noexcept;
    [[nodiscard]] std::int32_t access_coverage(
        const CoverageAccessEvent& event) noexcept;

    void execute_sampled_read(
        ProcessState& process,
        const ReadSignal& operation);

    void clear_wait_timeout(ProcessState& process);

    void set_wait_timeout_result(
        ProcessState& process,
        const bool timed_out);

    void begin_wait_timeout(
        ProcessState& process,
        const InstructionIndex origin,
        const SimulationTick delay,
        const std::optional<RegisterId> result);

    void rearm_wait_timeout(
        ProcessState& process,
        const InstructionIndex instruction,
        const InstructionIndex origin,
        const std::optional<RegisterId> result);

    void mark_dynamic_event_resume(
        ProcessState& process);

    void execute_dynamic_call(
        ProcessState& process,
        const Call& operation);

    void execute_dynamic_return(
        ProcessState& process,
        const Return& operation);

    void push_callable_frame(
        ProcessState& process,
        const CallableFramePush& operation);

    void pop_callable_frame(
        ProcessState& process,
        const CallableFramePop& operation);

    void snapshot_callable_context(ProcessState& process);

    void restore_callable_context(ProcessState& process);

    [[nodiscard]] bool dynamic_wait_satisfied(
        ProcessState& process,
        const SignalId signal,
        const EdgeKind edge);

    void handle_boundary(ProcessState& process,
        InstructionIndex instruction,
        InstructionIndex next_instruction);
    void handle_external_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        InstructionIndex next_instruction,
        const ExternalSuspension& suspension);
    void request_channel_update(
        ProcessId process, std::uint64_t channel);
    void execute(ProcessId id);
    void execute_static_cohort(std::span<const ProcessId> processes);
    [[nodiscard]] SchedulerBatchResult execute(
        Scheduler&, std::span<const std::uint64_t> cohort_ids) override;
    [[nodiscard]] bool handle_executor_resume(
        ProcessState& process, const ProcessResumeResult& boundary);

    void report_process_profile();

    void report_update_profile();

    void queue_at(ProcessId id, SimulationTick time);

    void queue_next_delta(ProcessId id);

    void queue_static_next_delta(ProcessId id);
    void queue_static_cohort_next_delta(std::size_t cohort);
    void build_native_static_regions();
    [[nodiscard]] std::size_t execute_native_static_region(
        std::size_t region, std::span<const ProcessId> ready);
    [[nodiscard]] std::uint64_t next_static_fanout_visit();

    void queue_current(ProcessId id);

    void queue_active_current(ProcessId id);

    void queue_static_active_current(ProcessId id);

    void register_static_sensitivity_cohort(ProcessId id);

    [[nodiscard]] bool handle_fork_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        const Operation& operation);
    void spawn_fork(
        ProcessState& parent,
        InstructionIndex instruction,
        const Fork& operation);
    void complete_fork_child(
        ProcessState& child,
        ProcessStatus status = ProcessStatus::finished);
    void cancel_fork_descendants(ProcessState& parent);
    void kill_dynamic_processes(
        std::span<const ProcessId> design_processes);
    void exit_program(ProcessState& process);
    void complete_program_process(ProcessState& process);
    [[nodiscard]] std::uint64_t process_handle(
        const ProcessState& process) const;
    [[nodiscard]] ProcessState& process_from_handle(
        ProcessState& caller,
        RegisterId source);
    [[nodiscard]] bool handle_process_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        const Operation& operation);
    [[nodiscard]] bool handle_synchronization_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        const Operation& operation);
    void complete_process(
        ProcessState& process,
        ProcessStatus status);

    void trigger_event(const SignalId event);

    [[nodiscard]] std::uint64_t invalidate_event(
        const SignalId event);

    void cancel_event(const SignalId event);

    void notify_event(
        const SignalId event,
        const SimulationTick delay,
        const EventNotificationKind kind,
        const StableOrder order);

    void notify_execution_point(
        ProcessState& process,
        const InstructionIndex instruction,
        const ExecutionPointKind kind,
        const SourceLocation& source,
        std::string_view scope = { });

    [[nodiscard]] bool monitor_watches(
        const SignalId signal) const;

    [[nodiscard]] std::string render_monitor(
        const MonitorInstall& registration) const;

    void schedule_monitor_publication();

    void install_monitor(
        const ProcessId process,
        const MonitorInstall& registration);

    void set_monitor_enabled(const bool enabled);
    void set_time_format(
        ProcessState& process,
        const TimeFormatControl& operation);

    [[nodiscard]] static std::uint64_t initial_random_state(
        const std::uint64_t seed,
        const ProcessId process) noexcept;

    [[nodiscard]] static std::uint32_t next_random(
        ProcessState& process) noexcept;

    [[nodiscard]] static std::optional<std::uint32_t>
    known_random_bound(const PackedLogic4& value);

    [[nodiscard]] PackedLogic4 random_value(
        const ProcessId process_id,
        const RandomKind kind,
        const std::optional<PackedLogic4>& maximum,
        const std::optional<PackedLogic4>& minimum);

    void publish(
        SignalId signal_id, PackedLogic4 value,
        bool notify_fanout = true);
    void publish_normalized(
        SignalId signal_id, PackedLogic4 value,
        bool notify_fanout = true);
    void publish_normalized_word(
        SignalId signal_id, Logic4Word value,
        bool notify_fanout = true);
    [[nodiscard]] bool can_publish_native_word(
        SignalId signal_id, ProcessId process) noexcept;
    [[nodiscard]] bool native_word_publication_phase_eligible() noexcept;
    [[nodiscard]] bool can_publish_native_word_prevalidated(
        SignalId signal_id, ProcessId process) noexcept;
    [[nodiscard]] bool can_publish_native_logic9_word(
        SignalId signal_id, ProcessId process) noexcept;
    [[nodiscard]] bool can_publish_blocking_word(SignalId signal_id) noexcept;
    void publish_native_word(SignalId signal_id, Logic4Word value);
    void publish_native_logic9_word(SignalId signal_id, Logic9Word value);
    void note_signal_transaction(SignalId signal_id, bool notify_fanout);
    void publish_value_change(SignalId signal_id, bool notify_fanout);
    void refresh_direct_signal_planes(SignalId signal_id);
    void publish_container_signal_aliases(SignalId signal_id);

    [[nodiscard]] PackedLogic4 apply_force(
        SignalId signal_id, PackedLogic4 value) const;

    void force_slice(
        SignalId signal_id, PackedLogic4 value, std::size_t offset);

    void release_slice(
        SignalId signal_id, std::size_t offset, std::size_t width);

    [[nodiscard]] PackedLogic4 apply_driver_force(
        SignalId signal_id, ProcessId process, PackedLogic4 value) const;

    [[nodiscard]] Logic4 driver_force_logic4_at(
        SignalId signal_id, ProcessId process,
        const PackedLogic4& value, std::size_t bit) const;

    void force_driver_slice(
        ProcessId process, SignalId signal_id,
        PackedLogic4 value, std::size_t offset);

    void release_driver_slice(
        ProcessId process, SignalId signal_id,
        std::size_t offset, std::size_t width);

    void commit(SignalId signal_id, PackedLogic4 value);

    void commit_direct_single_driver(
        SignalId signal_id, PackedLogic4 value);

    void refresh_switch_network();

    void commit_resolved(SignalId signal_id, PackedLogic4 value);

    [[nodiscard]] PackedLogic4 initial_driver_value(
        const SignalId signal_id) const;

    PackedLogic4& driver_slot(
        const ProcessId process,
        const SignalId signal_id);

    void refresh_direct_single_driver_route(SignalId signal_id);

    [[nodiscard]] PackedLogic4 resolved_driver_value(
        const SignalId signal_id) const;

    [[nodiscard]] PackedLogic4 resolved_local_driver_value(
        SignalId signal_id) const;

    [[nodiscard]] DriveStrength resolved_signal_strength(
        SignalId signal_id) const;

    [[nodiscard]] bool switch_process(ProcessId process) const;

    void reset_switch_drivers();

    PackedLogic4& external_driver_slot(
        const SignalId signal_id);

    void register_driver(
        const ProcessId process,
        const SignalId signal_id,
        std::span<const Process::DriverRegion> regions,
        DriveStrength strength);

    void set_driver(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value);

    void commit_driver(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value);

    [[nodiscard]] PackedLogic4 current_driver_value(
        const ProcessId process,
        const SignalId signal_id) const;

    [[nodiscard]] PackedLogic4 underlying_driver_value(
        ProcessId process, SignalId signal_id) const;

    void commit_slice(
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset);

    void commit_driver_slice(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset);

    void schedule_update_commit();

    void stage_update_unrouted(
        std::optional<ProcessId> driver,
        SignalId signal,
        PackedLogic4 value,
        std::optional<std::size_t> offset);

    [[nodiscard]] bool route_module_path_update(
        ProcessId driver,
        SignalId signal,
        const PackedLogic4& value,
        std::optional<std::size_t> offset,
        const TransitionDelays* intrinsic_delays = nullptr,
        SimulationTick fixed_delay = 0);

    [[nodiscard]] PackedLogic4 evaluate_module_path_expression(
        const ModulePathExpression& expression) const;

    void evaluate_module_timing_checks(
        SignalId changed,
        const PackedLogic4& before,
        const PackedLogic4& after);
    void report_module_timing_violation(std::size_t check_index);

    void stage_update(
        const std::optional<ProcessId> driver,
        SignalId signal_id,
        PackedLogic4 staged_value);

    void stage_update(
        const SignalId signal_id,
        PackedLogic4 staged_value);

    void stage_update(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 staged_value);

    void stage_update_slice(
        const std::optional<ProcessId> driver,
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset);

    void stage_update_slice(
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset);

    void stage_update_slice(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset);

    void stage_update_words(
        ProcessId process,
        std::span<const ProcessUpdateWord> updates);
    void stage_validated_update_words(
        ProcessId process,
        std::span<const ProcessUpdateWord> updates);
    [[nodiscard]] bool stage_validated_update_slot_batches(
        std::span<const ProcessUpdateSlotBatch> batches);
    [[nodiscard]] bool stage_validated_logic9_update_batch(
        const ProcessLogic9UpdateBatch& batch);
    [[nodiscard]] bool stage_validated_logic9_update_batches(
        std::span<const ProcessLogic9UpdateBatch> batches);

    void schedule_inertial(
        const ProcessId process,
        const SignalId signal,
        PackedLogic4 value,
        const std::optional<std::size_t> offset,
        const TransitionDelays& delays);

    void schedule_projected_scalar_waveform(
        const ProcessId process,
        const SignalId signal,
        const std::uint32_t offset,
        const std::vector<
            std::pair<PackedLogic4, SimulationTick>>& elements,
        const SimulationTick rejection,
        const ProjectedDelayMode mode);

    void schedule_projected_waveform(
        const ProcessId process,
        const SignalId signal,
        const std::vector<ProjectedWaveformValue>& elements,
        const std::optional<std::size_t> offset,
        const SimulationTick rejection,
        const ProjectedDelayMode mode);

    void schedule_projected(
        const ProcessId process,
        const SignalId signal,
        const PackedLogic4& value,
        const std::optional<std::size_t> offset,
        const SimulationTick delay,
        const SimulationTick rejection,
        const ProjectedDelayMode mode);

    [[noreturn]] void fail(const ProcessState& process,
        const std::string& message) const;
};

} // namespace fsim::runtime::simir
