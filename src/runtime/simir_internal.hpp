// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_coverage.hpp"
#include "fsim/runtime/output_format.hpp"
#include "simir_cohort_snapshot_pool.hpp"
#include "simir_driver_table.hpp"
#include "simir_signal_storage.hpp"
#include "simir_process_storage.hpp"
#include "simir_process_program.hpp"
#include "simir_operation_kinds.hpp"
#include "fsim/runtime/sva_engine.hpp"
#include <deque>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cctype>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <source_location>
#include <span>
#include <stdexcept>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::runtime::simir {

class ContainerRegisterAccess;

struct SignalChangeOrigin {
    ProcessSchedulingDomain process_domain {
        ProcessSchedulingDomain::generic
    };
    SchedulerPhase phase { SchedulerPhase::active };
};

struct SignalEventSchedulingStamp {
    SignalChangeOrigin origin;
    std::uint64_t systemverilog_round { };
};

struct AggregateSignalBatch {
    std::uint32_t depth { };
    std::optional<PackedLogic4> previous;
    bool changed { };
    // A staged family update preinstalls proxy event metadata before traces.
    bool event_prepared { };
    bool stored_changed { };
    bool stored_observer_notified { };
    bool transaction_changed { };
    bool notify_fanout { };
    bool driver_changed { };
    std::optional<ProcessId> driver;
    SignalChangeOrigin origin;
};

struct ContainerAliasLeafObserver {
    SignalId signal { };
    std::size_t ordinal { };
    PackedLogic4 current;
    bool current_changed { };
    bool scalar_changed { };
    bool element_changed { };
};

struct ContainerAliasWriteFrame {
    bool captured { };
    bool changed { };
    std::vector<ContainerAliasLeafObserver> leaves;
};

struct ContainerAliasWriteBatch {
    std::vector<ContainerAliasWriteFrame> frames;
};

struct ContainerReferenceLeafRefresh {
    std::size_t packed_offset { };
    PackedLogic4 value;
};

struct ContainerReferenceRefresh {
    ContainerObjectId object { };
    std::vector<ContainerReferenceLeafRefresh> leaves;
};

struct PreparedContainerReferenceRefresh {
    std::vector<ContainerReferenceRefresh> flat_aliases;
};

struct ActiveContainerReferenceRefresh {
    std::span<const SignalId> signals;
    PreparedContainerReferenceRefresh* prepared { };
    ActiveContainerReferenceRefresh* previous { };
    bool needs_refresh { true };
};

struct PreparedContainerAliasDriverLeaf {
    SignalId signal { };
    DriverTable replacement;
    bool selected { };
    bool inserted { };
    bool changed { };
};

struct ContainerAliasForceUpdate {
    std::unique_ptr<PackedLogic4> value;
    std::unique_ptr<PackedLogic4> mask;
    bool active { };
};

struct ContainerAliasDriverForceUpdate {
    std::unique_ptr<std::map<ProcessId, PackedLogic4>> values;
    std::unique_ptr<std::map<ProcessId, PackedLogic4>> masks;
    PackedLogic4 effective_current;
    bool active { };
};

struct PreparedContainerAliasDriverFamily {
    ContainerObjectId object { };
    ProcessId process { };
    SignalChangeOrigin origin;
    std::vector<PreparedContainerAliasDriverLeaf> leaves;
    std::optional<AggregateSignalBatch> suspended_aggregate_batch;
    std::exception_ptr raw_observer_failure;
    bool aggregate_batch_open { };
    bool raw_phase_begun { };
    bool installed { };
    bool raw_notifications_complete { };
    bool raw_phase_closed { };
};

enum class RandomDistributionIssue : std::uint8_t {
    none,
    exponential_mean,
    poisson_mean,
    chi_square_degrees,
    student_t_degrees,
    erlang_stages,
    resource_limit,
};

struct RandomDistributionResult {
    std::int32_t value { };
    std::int32_t seed { };
    RandomDistributionIssue issue { RandomDistributionIssue::none };
};

[[nodiscard]] RandomDistributionResult evaluate_random_distribution(
    RandomDistributionKind kind,
    std::int32_t seed,
    std::int32_t first,
    std::optional<std::int32_t> second);

[[nodiscard]] std::string_view random_distribution_issue_message(
    RandomDistributionIssue issue) noexcept;

/// One $dist_* call (IEEE 1800-2023 20.14.2, Annex N) on its operand values:
/// the result, the seed to write back (none when the seed was not read) and
/// the warning to report. Throws std::invalid_argument for an operand that
/// is not a 32-bit integer.
struct RandomDistributionStep {
    std::uint32_t value { };
    std::optional<std::uint32_t> seed;
    std::optional<std::string_view> warning;
};

[[nodiscard]] RandomDistributionStep random_distribution_step(
    const RandomDistribution& operation, const PackedLogic4& seed,
    const PackedLogic4& first, const PackedLogic4* second);

void validate_container_value(const ContainerValue& value);

[[nodiscard]] std::string error_text(ProcessId process,
    InstructionIndex instruction,
    const std::string& message);

[[nodiscard]] std::optional<SignalId> output_signal(
    const Operation& operation);

[[nodiscard]] bool edge_matches(EdgeKind edge, Logic4 old_value,
    Logic4 new_value) noexcept;

[[nodiscard]] Logic4 edge_value(const PackedLogic4& value) noexcept;

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

struct KnownDivision {
    PackedLogic4 quotient;
    PackedLogic4 remainder;
};

[[nodiscard]] KnownDivision divide_known(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor);

[[nodiscard]] KnownDivision divide_known_signed(
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
    std::span<const SignalHot> signals);

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

    struct OutputCallback {
        Impl* owner { };
        std::shared_ptr<const OutputHook> callback;
        bool trusted_text_only { };

        OutputCallback& operator=(OutputHook hook)
        {
            // Revoke trust before allocating the replacement. If allocation
            // fails, the old callback remains installed but takes the safe
            // arbitrary-callback path on its next invocation.
            trusted_text_only = false;
            std::shared_ptr<const OutputHook> prepared;
            if (hook) {
                prepared = std::make_shared<OutputHook>(std::move(hook));
            }
            callback = std::move(prepared);
            return *this;
        }

        void set_trusted_text_hook(OutputHook hook)
        {
            std::shared_ptr<const OutputHook> prepared;
            if (hook) {
                prepared = std::make_shared<OutputHook>(std::move(hook));
            }
            callback = std::move(prepared);
            trusted_text_only = static_cast<bool>(callback);
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return static_cast<bool>(callback);
        }

        void operator()(ProcessId process, std::string_view text,
            bool newline, SimulationTick time, std::uint64_t delta) const;
    };

    struct ReportCallback {
        Impl* owner { };
        std::shared_ptr<const ReportHook> callback;
        bool trusted_text_only { };

        ReportCallback& operator=(ReportHook hook)
        {
            // Revoke trust before allocating, as for OutputCallback.
            trusted_text_only = false;
            std::shared_ptr<const ReportHook> prepared;
            if (hook) {
                prepared = std::make_shared<ReportHook>(std::move(hook));
            }
            callback = std::move(prepared);
            return *this;
        }

        void set_trusted_text_hook(ReportHook hook)
        {
            std::shared_ptr<const ReportHook> prepared;
            if (hook) {
                prepared = std::make_shared<ReportHook>(std::move(hook));
            }
            callback = std::move(prepared);
            trusted_text_only = static_cast<bool>(callback);
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return static_cast<bool>(callback);
        }

        void operator()(ProcessId process, std::string_view message,
            AssertionSeverity severity, const SourceLocation& source,
            SimulationTick time, std::uint64_t delta) const;
    };

    using SharedContainerValue = std::shared_ptr<ContainerValue>;
    // The reference's register file for the shared container operations
    // (simir_container_helpers.hpp).
    class ProcessContainerAccess;

    static constexpr std::size_t no_systemverilog_update_slot
        = std::numeric_limits<std::size_t>::max();

    struct SystemVerilogUpdateSlot {
        ProcessId process { };
        SignalId signal { };
        PackedLogic4 value;
        std::optional<std::size_t> offset;
        SignalChangeOrigin origin;
        std::size_t next_free { no_systemverilog_update_slot };
        std::uint64_t generation { 1U };
        bool occupied { };
        bool reserved { };
        bool retired { };
    };

    struct SystemVerilogUpdateToken {
        Impl* owner { };
        std::size_t slot { no_systemverilog_update_slot };
        std::uint64_t generation { };
    };

    static_assert(std::is_trivially_copyable_v<SystemVerilogUpdateToken>);
    static_assert(sizeof(SystemVerilogUpdateToken)
        <= ::fsim::runtime::detail::scheduler_task_payload_size);
    static_assert(std::is_nothrow_move_assignable_v<PackedLogic4>);
    static_assert(std::is_nothrow_move_constructible_v<
        SystemVerilogUpdateSlot>);

    struct ProcessFrame {
        std::vector<PackedLogic4> registers;
        std::vector<std::string> string_registers;
        std::vector<SharedContainerValue> container_registers;
        std::vector<VitalMemoryState> vital_memories;
    };

    struct ProcessDeferredExecutor {
        std::function<bool()> ready;
        std::function<std::unique_ptr<ProcessExecutor>()> take;
        DeferredProcessExecutorContract contract;
        std::unique_ptr<ProcessExecutor> prepared_executor;
        bool access_binding_rejected { };
    };

    struct ProcessCallableFrameState {
        std::uint32_t identity { };
        std::vector<RegisterId> packed_ids;
        std::vector<StringRegisterId> string_ids;
        std::vector<ContainerRegisterId> container_ids;
        std::vector<PackedLogic4> packed;
        std::vector<std::string> strings;
        std::vector<SharedContainerValue> containers;
        std::size_t storage_bytes { };
        // Non-null when this automatic activation has fork children. The
        // activation owns one live context; its owner and every child share
        // it until the owner detaches and the final child exits.
        std::shared_ptr<ProcessCallableFrameState> escaping_context;
    };

    struct ProcessForkState {
        std::set<ProcessId> live_children;
        std::map<InstructionIndex, std::set<ProcessId>> active_sites;
        // A fork child keeps the exact executor access binding accepted when
        // it was cloned. This survives parent completion and is independent
        // of any mutable alias to the executor's signal-remap vector.
        std::optional<ProcessExecutorProgramBinding>
            access_binding_attestation;
    };

    struct ProcessVitalState {
        std::map<InstructionIndex, VitalTimingState> timing;
        std::map<InstructionIndex, VitalDelayState> delay;
    };

    // A process without dynamic event or process waits needs no registration
    // tables, timeout metadata, or waiter set. This state is allocated on the
    // first mutable wait arm and kept for the process lifetime so generation
    // numbers remain monotonic across wake, cancellation, and rearm.
    struct ProcessDynamicWaitState {
        std::optional<ProcessId> waiting_process;
        std::set<ProcessId> process_waiters;
        std::vector<Sensitivity> dynamic_sensitivity;
        std::vector<bool> dynamic_triggered;
        std::vector<std::size_t> dynamic_fanout_positions;
        std::optional<ContainerObjectId> waiting_on_container;
        bool dynamic_wait_all { };
        std::vector<std::optional<SignalId>> wait_order_events;
        std::size_t wait_order_index { };
        std::optional<RegisterId> wait_order_result;
        std::optional<SimulationTick> wait_timeout_deadline;
        std::optional<RegisterId> wait_timeout_result;
        std::uint64_t wait_timeout_generation { };
        std::uint64_t dynamic_wait_generation { };
    };

    // This sidecar owns a compact per-process program view and feature state.
    // Common immutable layout/debug metadata lives in a shared template;
    // fork children receive a fresh identity and their own operation bindings.
    // The exact public Process facade is materialized only when requested.
    // wait registrations are cleared on resume while dynamic call stacks live
    // until their matching returns.
    struct ProcessColdState {
        ProcessProgramStorage local_program_storage;
        std::unique_ptr<ProcessProgramStorage> retained_program_storage;
        [[nodiscard]] ProcessProgramStorage& program_storage() noexcept
        {
            return retained_program_storage != nullptr
                ? *retained_program_storage : local_program_storage;
        }
        [[nodiscard]] const ProcessProgramStorage& program_storage() const noexcept
        {
            return retained_program_storage != nullptr
                ? *retained_program_storage : local_program_storage;
        }
        ProcessId design_process { };
        std::optional<ProcessDeferredExecutor> deferred_executor;
        SourceLocation current_source;
        std::string current_scope;
        std::uint64_t random_state { };
        // Most processes never execute VITAL operations. Keep their timing
        // and delay maps outside the ordinary per-instance state.
        std::unique_ptr<ProcessVitalState> vital_state;

        // Dynamic wait registration and process-await data is absent until a
        // process first arms a wait or becomes a ProcessAwait target.
        std::unique_ptr<ProcessDynamicWaitState> dynamic_wait_state;

        [[nodiscard]] ProcessDynamicWaitState& ensure_dynamic_wait_state()
        {
            if (!dynamic_wait_state) {
                dynamic_wait_state
                    = std::make_unique<ProcessDynamicWaitState>();
            }
            return *dynamic_wait_state;
        }

        [[nodiscard]] ProcessDynamicWaitState*
        dynamic_wait_state_if_present() noexcept
        {
            return dynamic_wait_state.get();
        }

        [[nodiscard]] const ProcessDynamicWaitState*
        dynamic_wait_state_if_present() const noexcept
        {
            return dynamic_wait_state.get();
        }

        [[nodiscard]] ProcessVitalState& ensure_vital_state()
        {
            if (!vital_state) {
                vital_state = std::make_unique<ProcessVitalState>();
            }
            return *vital_state;
        }
        std::vector<ProcessCallableFrameState> callable_frames;
        std::size_t callable_frame_storage_bytes { };
        std::vector<std::shared_ptr<ProcessCallableFrameState>>
            escaping_callable_contexts;
        // Ordinary processes pay only for the handle; callable suspension
        // allocates its owning snapshot on the checked execution path.
        std::unique_ptr<ProcessCallableFrameState> suspended_callable_context;
        std::size_t callable_context_storage_bytes { };
        // Callable register backing is live only between restore and
        // suspension. Suspended writes go to the authoritative snapshots.
        bool callable_registers_suspended { };
        // A mailbox wake result must survive stale sibling activation captures.
        std::optional<RegisterId> pending_mailbox_result;
        // The dynamic event handle this process waits on, if any.
        std::optional<std::uint64_t> pending_dynamic_event;
        std::optional<StringRegisterId> pending_mailbox_string_result;
        std::optional<ProcessId> fork_parent;
        std::optional<InstructionIndex> fork_site;
        // Spawned by a detached fork (a pending clocking drive).
        bool fork_detached { };
        std::optional<std::uint64_t> fork_group;
        // Allocate child tracking before the first fork publishes children.
        // Wait/disable operations on an ordinary process need no sidecar.
        std::unique_ptr<ProcessForkState> fork_state;
        bool waiting_for_children { };
        std::optional<std::uint64_t> waiting_fork_group;
        std::uint64_t profile_calls { };
        std::uint64_t profile_interpreter_operations { };
        std::uint64_t profile_native_resumes { };
        std::uint64_t profile_updates { };
        std::uint64_t profile_total_nanoseconds { };
        std::uint64_t profile_native_nanoseconds { };
        bool track_interpreter_operations { };
        std::uint64_t interpreter_operations { };
        std::vector<InstructionIndex> dynamic_call_stack;
        /// The static kernel's hosted tier for this process's operation
        /// list (StaticKernel::hosted_tier), with the list it was found for.
        const void* hosted_tier { };
        const OperationList* hosted_tier_list { };
        const void* hosted_tier_body { };
        std::size_t hosted_tier_size { };

        void set_program(
            Process process,
            std::shared_ptr<const ProcessProgramTemplate> common);
        void set_program(
            ProcessInstanceProgram instance,
            std::shared_ptr<const ProcessProgramTemplate> common);
        void synchronize_public_program_operations();
        void inherit_fork_program(
            const ProcessColdState& parent,
            ProcessId child_id,
            InstructionIndex fork_instruction,
            std::size_t child_index);
        [[nodiscard]] ProcessProgramView program() const noexcept;
        [[nodiscard]] const Process& public_program() const;
    };

    struct ProcessState {
        using DeferredExecutor = ProcessDeferredExecutor;
        using CallableFrameState = ProcessCallableFrameState;

        // Keep scheduler and execution working data inline. Cohort entries
        // retain pointers to queued, waiting_on_static, and status for the
        // lifetime of this address-stable process record.
        ProcessId id { };
        InstructionIndex pc { };
        std::optional<InstructionIndex> wait_timeout_origin;
        std::shared_ptr<ProcessFrame> frame;
        std::unique_ptr<ProcessExecutor> executor;
        // Changes before executor destruction, replacement, redirect, or fork
        // so cached optional executor interfaces cannot outlive their object.
        std::uint64_t executor_lifecycle_generation { 1U };
        std::uint64_t static_trigger_mask { Process::full_static_trigger_mask };
        SchedulerPhase execution_phase { SchedulerPhase::active };
        bool queued { };
        // The queued wake is a timer at a later time; a disable that
        // redirects the process abandons it by advancing timer_epoch.
        bool timer_wake { };
        bool waiting_on_static { };
        bool waiting_on_signal { };
        std::uint32_t generation { };
        std::uint32_t timer_epoch { };
        ProcessStatus status { ProcessStatus::running };
        ProcessStatus suspended_status { ProcessStatus::running };
        bool suspended { };
        bool suspended_wake { };
        bool halted { };
        bool killed { };
        bool has_callable_frame_push { };

        // The sidecar is allocated with its program at process creation and
        // remains heap-stable if this small deque record is moved.
        std::unique_ptr<ProcessColdState> cold_state
            = std::make_unique<ProcessColdState>();

        [[nodiscard]] ProcessProgramView program()
        {
            return cold().program();
        }

        [[nodiscard]] ProcessProgramView program() const
        {
            return cold().program();
        }

        [[nodiscard]] ProcessColdState& cold()
        {
            return *cold_state;
        }

        [[nodiscard]] const ProcessColdState& cold() const
        {
            return *cold_state;
        }

        [[nodiscard]] bool has_active_wait_timeout() const noexcept
        {
            const auto* const dynamic_wait
                = cold_state == nullptr
                ? nullptr
                : cold_state->dynamic_wait_state_if_present();
            return wait_timeout_origin.has_value()
                || (dynamic_wait != nullptr
                    && dynamic_wait->wait_timeout_deadline.has_value());
        }

    };

    struct ConstantDriverStartupEntry {
        ProcessId id { };
        std::uint32_t generation { };
        InstructionIndex pc { };
        SchedulerPhase execution_phase { SchedulerPhase::active };
        ProcessStatus status { ProcessStatus::running };
        bool queued { };
        bool waiting_on_static { };
        bool waiting_on_signal { };
        std::uint64_t static_trigger_mask { Process::full_static_trigger_mask };
        bool halted { };
        bool suspended { };
        bool suspended_wake { };
        bool killed { };
        ProcessStatus suspended_status { ProcessStatus::running };
        bool track_interpreter_operations { };
        std::uint64_t interpreter_operations { };
        std::uint64_t profile_calls { };
        std::uint64_t profile_interpreter_operations { };
        std::uint64_t profile_updates { };
        std::uint64_t profile_total_nanoseconds { };
        SourceLocation current_source;
        std::string current_scope;
        std::uint64_t random_state { };
        std::unique_ptr<ProcessProgramStorage> program_storage;
        std::unique_ptr<ProcessStartupWriteBank> startup_write_bank;
        SignalId signal { };
        std::uint32_t offset { };
        bool slice { };
        SignalUpdateDomain update_domain {
            SignalUpdateDomain::systemverilog_active
        };
        bool projected { };
        ProjectedDelayMode projected_mode { ProjectedDelayMode::inertial };
        SimulationTick projected_delay { };
        SimulationTick projected_rejection { };
    };

    class ProcessTable {
    public:
        enum class SlotKind : std::uint8_t { full, compact_constant };

        template <bool IsConst>
        class FullIterator {
            using Table = std::conditional_t<
                IsConst, const ProcessTable, ProcessTable>;
            using Reference = std::conditional_t<
                IsConst, const ProcessState&, ProcessState&>;
            using Pointer = std::conditional_t<
                IsConst, const ProcessState*, ProcessState*>;

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = ProcessState;
            using difference_type = std::ptrdiff_t;
            using reference = Reference;
            using pointer = Pointer;

            FullIterator() = default;
            FullIterator(Table* table, std::size_t id) : table_ { table }, id_ { id }
            {
                skip_compact();
            }
            reference operator*() const
            {
                return table_->full_state(table_->slots_[id_].index);
            }
            pointer operator->() const { return std::addressof(**this); }
            FullIterator& operator++()
            {
                ++id_;
                skip_compact();
                return *this;
            }
            FullIterator operator++(int)
            {
                auto previous = *this;
                ++*this;
                return previous;
            }
            friend bool operator==(const FullIterator&, const FullIterator&)
                = default;

        private:
            void skip_compact()
            {
                while (table_ != nullptr && id_ < table_->slots_.size()
                    && table_->slots_[id_].kind
                        == SlotKind::compact_constant) {
                    ++id_;
                }
            }

            Table* table_ { };
            std::size_t id_ { };
        };

        // Legacy ProcessState iteration is intentionally full-state-only.
        // Use ProcessId loops plus program_view() for a logical-process scan.
        using iterator = FullIterator<false>;
        using const_iterator = FullIterator<true>;

        struct Slot {
            SlotKind kind { SlotKind::full };
            ProcessSchedulingDomain scheduling_domain {
                ProcessSchedulingDomain::generic
            };
            std::size_t index { };
        };

        void reserve_initial(std::size_t capacity);
        [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }
        [[nodiscard]] bool empty() const noexcept { return slots_.empty(); }
        [[nodiscard]] iterator begin() noexcept { return { this, 0U }; }
        [[nodiscard]] iterator end() noexcept { return { this, size() }; }
        [[nodiscard]] const_iterator begin() const noexcept
        {
            return { this, 0U };
        }
        [[nodiscard]] const_iterator end() const noexcept
        {
            return { this, size() };
        }
        [[nodiscard]] bool frozen() const noexcept { return frozen_; }
        void push_back(ProcessState&& process);
        void push_compact(ConstantDriverStartupEntry&& process);
        void freeze_initial_storage();
        [[nodiscard]] ProcessState& operator[](std::size_t index);
        [[nodiscard]] const ProcessState& operator[](std::size_t index) const;
        [[nodiscard]] ProcessState& at(std::size_t index);
        [[nodiscard]] const ProcessState& at(std::size_t index) const;
        [[nodiscard]] ProcessState& promote_state(std::size_t index);
        [[nodiscard]] ProcessState* full_state_if_present(
            ProcessId id) noexcept;
        [[nodiscard]] const ProcessState* full_state_if_present(
            ProcessId id) const noexcept;
        [[nodiscard]] ProcessProgramView program_view(ProcessId id) const;
        /// Hot per-slot copy of the program's scheduling domain.
        [[nodiscard]] ProcessSchedulingDomain scheduling_domain(
            const ProcessId id) const
        {
            if (id >= slots_.size()) {
                throw std::out_of_range { "invalid SimIR process ID" };
            }
            return slots_[id].scheduling_domain;
        }
        [[nodiscard]] SignalChangeOrigin scheduling_origin(
            ProcessId id) const
        {
            if (id >= slots_.size()) {
                throw std::out_of_range { "invalid SimIR process ID" };
            }
            const auto& slot = slots_[id];
            if (slot.kind == SlotKind::full) {
                const auto& process = full_state(slot.index);
                return {
                    slot.scheduling_domain,
                    process.execution_phase
                };
            }
            const auto& compact = compact_constants_[slot.index];
            return {
                slot.scheduling_domain,
                compact.execution_phase
            };
        }
        [[nodiscard]] const Process& public_program(ProcessId id) const;
        [[nodiscard]] std::size_t operation_count(ProcessId id) const;
        [[nodiscard]] ConstantDriverStartupEntry* compact_constant(
            ProcessId id) noexcept;
        [[nodiscard]] const ConstantDriverStartupEntry* compact_constant(
            ProcessId id) const noexcept;
        [[nodiscard]] bool is_compact_constant(ProcessId id) const noexcept;
        [[nodiscard]] std::size_t compact_constant_count() const noexcept;
        [[nodiscard]] bool public_facade_materialized(
            ProcessId id) const noexcept;
        void add_profile_updates(ProcessId id, std::size_t count);
        [[nodiscard]] std::uint64_t profile_updates(ProcessId id) const;
        [[nodiscard]] std::uint64_t profile_total_nanoseconds(
            ProcessId id) const;
        [[nodiscard]] std::uint64_t profile_calls(ProcessId id) const;
        [[nodiscard]] std::uint64_t profile_interpreter_operations(
            ProcessId id) const;
        [[nodiscard]] std::uint64_t interpreter_operations(
            ProcessId id) const;
        [[nodiscard]] std::uint64_t profile_native_resumes(
            ProcessId id) const;
        [[nodiscard]] std::uint64_t profile_native_nanoseconds(
            ProcessId id) const;
        [[nodiscard]] bool has_executor(ProcessId id) const noexcept;

    private:
        friend struct OwnedDriverDemotionTestAccess;
        [[nodiscard]] ProcessState& full_state(std::size_t index);
        [[nodiscard]] const ProcessState& full_state(std::size_t index) const;
        [[nodiscard]] ProcessState& promote(std::size_t index);
        std::vector<Slot> slots_;
        // Descriptors stay address-stable after promotion because callbacks
        // can hold a reference while an explicit mutable API promotes the
        // corresponding process. The slot switches to full storage and the
        // descriptor becomes a small tombstone until Interpreter teardown.
        std::deque<ConstantDriverStartupEntry> compact_constants_;
        std::size_t active_compact_constants_ { };
        ProcessStateStorage<ProcessState> stable_full_;
        bool frozen_ { };
    };

    struct DynamicWaitRegistration {
        ProcessId process { };
        std::uint32_t process_generation { };
        std::uint64_t wait_generation { };
        EdgeKind edge = EdgeKind::any;
        std::size_t sensitivity_index { };
        bool active { true };
    };

    struct MailboxReader {
        ProcessId process { };
        RegisterId destination { };
        StringRegisterId string_destination { };
        bool peek { };
    };

    struct SampledHistoryKey {
        SignalId signal { };
        std::optional<SignalId> clock;
        SampledClockEdge edge { SampledClockEdge::any };
        std::optional<SignalId> gate;
        ProcessSchedulingDomain process_domain {
            ProcessSchedulingDomain::generic
        };

        auto operator<=>(const SampledHistoryKey&) const = default;
    };

    struct SampledHistorySlot {
        SimulationTick time { };
        ProcessSchedulingDomain process_domain {
            ProcessSchedulingDomain::generic
        };
        ProcessSchedulingDomain event_domain {
            ProcessSchedulingDomain::generic
        };
        SchedulerPhase event_phase { SchedulerPhase::active };
        std::uint64_t generic_delta { };
        std::uint64_t systemverilog_round { };

        auto operator<=>(const SampledHistorySlot&) const = default;
    };

    struct SampledHistoryState {
        // Fixed-size rings are prepared before simulation for statically
        // expanded SV clocked histories. PackedLogic4 copy assignment reuses
        // the inline planes or shared wide backing and does not allocate.
        std::vector<PackedLogic4> values;
        std::size_t first_value { };
        std::size_t value_count { };
        std::optional<SampledHistorySlot> last_slot;
    };

    struct MailboxWriter {
        ProcessId process { };
        PackedLogic4 value;
        std::string string_value;
    };

    struct MailboxState {
        MailboxElementKind element_kind { MailboxElementKind::packed };
        std::uint32_t element_width { };
        std::size_t capacity { };
        std::deque<PackedLogic4> entries;
        std::deque<std::string> string_entries;
        // The bytes of the strings in `string_entries`.
        std::size_t string_bytes { };
        std::deque<MailboxReader> readers;
        std::deque<MailboxWriter> writers;
    };

    struct SemaphoreWaiter {
        ProcessId process { };
        std::int32_t keys { };
    };

    struct SemaphoreState {
        std::int64_t keys { };
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
        std::uint32_t offset { };
        std::uint32_t width { };
    };

    struct FanoutSpan {
        std::size_t begin { };
        std::size_t count { };
    };

    struct SwitchConnection {
        SignalId source { };
        SignalId target { };
        std::optional<SignalId> control;
        std::size_t source_offset { };
        std::size_t target_offset { };
        std::size_t width { };
        bool active_high { true };
        bool resistive { };
    };

    struct SwitchComponent {
        std::vector<SignalId> signals;
        bool dirty { };
        bool has_non_switch_update { };
    };

    static constexpr std::size_t no_switch_component
        = std::numeric_limits<std::size_t>::max();

    struct StaticTransitionMatches {
        bool posedge { };
        bool negedge { };

        [[nodiscard]] bool matches(const EdgeKind edge) const noexcept
        {
            switch (edge) {
            case EdgeKind::posedge:
                return posedge;
            case EdgeKind::negedge:
                return negedge;
            case EdgeKind::any:
            case EdgeKind::transaction:
                return false;
            }
            return false;
        }
    };

    struct StaticSensitivityCohort {
        std::vector<ProcessId> members;
        std::vector<ProcessId> ready;
        CohortSnapshotPool::Token pending;
        std::uint64_t fanout_visit { };
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

        PendingUpdate(SignalId signal_value,
            std::optional<ProcessId> driver_value,
            std::optional<std::size_t> offset_value, Logic4Word word_value,
            std::optional<std::size_t> packed_value_index);

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

    struct UnresolvedUpdateOwnerProvenance {
        ProcessId process { };
        bool touched { };
        bool has_process { };
        bool ambiguous { };
    };

    struct PendingDriverCommit {
        std::optional<ProcessId> driver;
        PackedLogic4 value;

        PendingDriverCommit(std::optional<ProcessId> owner,
            PackedLogic4 pending_value)
            : driver(owner)
            , value(std::move(pending_value))
        {
        }
    };

    struct DirectSingleDriverRoute {
        ProcessId process { };
        bool active { };
    };

    struct DirectSingleDriverLogic9WordUpdate {
        static constexpr std::uint32_t unlisted = 0U;
        static constexpr std::uint32_t native = 1U;
        static constexpr std::uint32_t generic_blocked = 2U;
        std::array<std::uint64_t, 4U> planes { };
        std::uint64_t mask { };
        std::uint32_t width { };
        ProcessId process { };
        std::uint32_t active { unlisted };
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
        SignalChangeOrigin origin { };
    };

    struct InertialDriverKey {
        ProcessId process { };
        SignalId signal { };
        std::uint32_t offset { };
        std::uint32_t width { };
        ProcessSchedulingDomain process_domain {
            ProcessSchedulingDomain::generic
        };
        SchedulerPhase phase { SchedulerPhase::active };

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
        std::string logical_name;
        std::string mode;
        std::unique_ptr<std::fstream> stream;
        std::optional<std::uint8_t> pushback;
        std::string last_error;
        bool closed { };
        bool readable { };
        bool writable { };
        bool vhdl { };
    };

    explicit Impl(
        SchedulerOptions options,
        const std::uint64_t seed);
    ~Impl();

    Scheduler scheduler;
    Scheduler::DiscardHookToken scheduler_discard_hook { };
    // Native generic Update tickets hold a pointer to this stable member.
    // Initialize it once and never replace it while scheduler work may exist.
    Scheduler::Task update_commit_callback;
    std::vector<SystemVerilogUpdateSlot> systemverilog_update_slots;
    std::size_t systemverilog_update_free_head {
        no_systemverilog_update_slot
    };
    CohortSnapshotPool cohort_snapshots;
    std::vector<ProcessId> active_cohort_ready;
    std::vector<ExecutionContext> cohort_overflow_contexts;
    std::vector<ProcessCohortResumeEntry> cohort_overflow_entries;
    bool cohort_overflow_scratch_in_use { };
    std::uint64_t root_seed { 1 };
    // SignalHot and SignalCold are dense, SignalId-indexed parallel records.
    std::vector<SignalHot> signals;
    std::vector<SignalCold> signal_cold;
    std::vector<SwitchConnection> switch_connections;
    std::vector<std::vector<std::size_t>> switch_endpoint_adjacency;
    std::vector<std::vector<std::size_t>> switch_control_adjacency;
    // The unidirectional MOS switches that read each signal as their
    // source: a source driver change can change the strength they pass
    // on without changing the source's value (IEEE 1800-2017 28.13).
    std::vector<std::vector<ProcessId>> switch_source_readers;
    void wake_switch_source_readers(SignalId signal, ProcessId writer);
    std::vector<SwitchComponent> switch_components;
    std::vector<std::size_t> switch_component_by_signal;
    std::vector<std::size_t> switch_component_by_connection;
    std::vector<std::size_t> dirty_switch_components;
    bool switch_components_built { };
    std::vector<StringObject> string_objects;
    std::vector<ContainerObject> container_objects;
    std::vector<std::uint8_t> container_value_reference_exposed;
    std::vector<std::uint8_t>
        container_value_reference_exposure_in_progress;
    std::vector<ContainerObjectId> exposed_container_value_references;
    ActiveContainerReferenceRefresh*
        active_container_reference_refresh { };
    std::vector<SharedContainerValue> default_container_values;
    std::vector<std::optional<ContainerSignalAlias>>
        container_signal_aliases;
    std::vector<std::vector<std::optional<ContainerElementSignalAlias>>>
        container_element_signal_aliases;
    std::vector<ContainerAliasWriteBatch> container_alias_write_batches;
    std::vector<std::optional<ContainerAggregateSignalAlias>>
        container_aggregate_signal_aliases;
    std::vector<std::uint64_t> container_aggregate_current_revisions;
    std::vector<std::uint64_t> container_aggregate_stored_revisions;
    // Active aggregate aliases use their packed current/stored projections
    // as retained family state. Leaf sidecars remain mirrored for scheduler
    // and per-element APIs; the set is sparse and keyed by aggregate family.
    std::set<ContainerObjectId> retained_aggregate_authorities;
    std::vector<std::optional<std::uint64_t>>
        container_materialized_revisions;
    std::vector<std::vector<ContainerObjectId>>
        signal_container_aliases;
    std::vector<std::optional<std::pair<ContainerObjectId, std::size_t>>>
        signal_container_element_aliases;
    std::vector<std::optional<ContainerObjectId>>
        signal_container_aggregate_aliases;
    // Per signal, present only for an aggregate alias proxy: pointer-sized
    // when absent.
    std::vector<support::RareOptional<PackedLogic4>>
        aggregate_signal_current_projection;
    std::vector<support::RareOptional<PackedLogic4>>
        aggregate_signal_stored_projection;
    std::vector<AggregateSignalBatch> aggregate_signal_batches;
    std::vector<std::uint64_t>
        aggregate_signal_current_projection_revisions;
    std::vector<std::uint64_t>
        aggregate_signal_stored_projection_revisions;
    std::vector<support::RareOptional<std::pair<std::size_t, PackedLogic4>>>
        aggregate_signal_last_changes;
    std::vector<std::uint64_t>
        aggregate_signal_last_projection_revisions;
    std::filesystem::path file_root;
    /// Operations left to interpret (Interpreter::set_operation_budget).
    std::optional<std::uint64_t> operation_budget;
    // STD.ENV DIR_WORKINGDIR is simulator-local state. Never mutate the host
    // process working directory, which would race parallel simulation.
    std::filesystem::path vhdl_working_directory;
    std::vector<std::string> plusargs;
    SystemVerilogTimeFormat time_format;
    // The seed of `$random` without an argument; it starts at zero, as in
    // `$random(seed)` with a zero seed (IEEE 1800-2017 20.15.2, Annex N).
    std::int32_t system_random_seed { };
    std::map<FileHandle, FileState> files;
    // What a SystemVerilog file operation reads through a descriptor that
    // names no open file: nothing is readable or writable.
    FileState invalid_systemverilog_file;
    // Descriptors 0x80000000-0x80000002 are STDIN, STDOUT and STDERR (IEEE
    // 1800-2017 21.3.1).
    FileHandle next_file_handle { 3 };
    std::uint32_t next_multichannel_channel { 1 };
    std::vector<std::uint8_t> direct_signal_read_capabilities;
    std::uint64_t direct_signal_read_owner_token { };
    std::uint64_t direct_signal_read_capability_epoch { };
    bool direct_signal_read_capability_epoch_exhausted { };
    bool direct_signal_plane_layout_available { };
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
    std::vector<std::uint8_t> signal_transaction_observed;
    std::vector<std::uint32_t> signal_writer_counts;
    std::uint64_t signal_writer_revision { };
    std::vector<PackedLogic4> driven_values;
    // Signals resolved by a VHDL resolution function: their resolver
    // programs, last resolved values, and the signals whose drivers changed
    // since their resolver last ran.
    std::unordered_map<SignalId, VhdlSignalResolver> vhdl_resolvers;
    // Signals whose port-association conversions read the key signal.
    std::unordered_map<SignalId, std::vector<SignalId>>
        vhdl_conversion_dependents;
    // Resolved signals driven by formal-side conversions, by signal.
    std::unordered_map<SignalId, std::vector<VhdlConversionDriver>>
        vhdl_conversion_drivers;
    // Per signal: nonzero when its bits form one VHDL scalar.
    std::unordered_map<SignalId, std::uint32_t> vhdl_scalar_widths;
    // Drivers of guarded signals disconnected by a null transaction
    // (IEEE 1076-2008 10.5.2.2); a later value reconnects them.
    std::set<std::pair<SignalId, ProcessId>> vhdl_disconnected_drivers;
    std::unordered_map<SignalId, PackedLogic4> vhdl_resolved_values;
    std::unordered_map<SignalId, PackedLogic4> vhdl_driver_initial_values;
    mutable std::vector<SignalId> pending_vhdl_resolutions;
    mutable std::unordered_set<SignalId> pending_vhdl_resolution_set;
    bool running_vhdl_resolvers { };
    // Signals given a value before the first cycle (a VHPI start-of-
    // simulation deposit); initialization keeps that value, as it does for
    // a driven signal, until a source of the signal changes.
    std::unordered_set<SignalId> vhdl_deposited_before_start;
    std::vector<DriverTable> driver_values;
    using ForcedDriverMap = std::map<ProcessId, PackedLogic4>;
    struct ForcedDriverMapView {
        const ForcedDriverMap* values { };
        const ForcedDriverMap* masks { };
    };
    std::vector<std::unique_ptr<ForcedDriverMap>> forced_driver_values;
    std::vector<std::unique_ptr<ForcedDriverMap>> forced_driver_masks;
    std::vector<std::unique_ptr<PackedLogic4>> external_driver_values;
    std::vector<std::optional<ScheduledTaskHandle>> charge_decay_handles;
    std::vector<std::unique_ptr<PackedLogic4>> charge_values;
    std::vector<PackedLogic4> signal_last_values;
    struct SignalSelectionHistory {
        SignalSelection selection;
        PackedLogic4 last_value;
        std::optional<std::pair<SimulationTick, std::uint64_t>> last_event;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            last_transaction;
        bool transaction_pending { };
    };
    std::vector<std::vector<SignalSelectionHistory>>
        signal_selection_histories;
    std::vector<std::uint8_t>
        signal_selection_transaction_region_pending;
    std::vector<std::uint8_t>
        signal_selection_transaction_pending_listed;
    std::vector<SignalId> signal_selection_pending_transaction_signals;
    std::vector<std::uint64_t> signal_value_revisions;
    /// Signals the static kernel reads as inputs (1), and those among them
    /// whose revision changed since the kernel last scanned them (2, listed
    /// in static_kernel_dirty_inputs): the kernel refreshes only those.
    std::vector<std::uint8_t> static_kernel_input_marks;
    std::vector<SignalId> static_kernel_dirty_inputs;
    /// Called after every bump of signal_value_revisions[signal].
    void note_signal_revision(const SignalId signal)
    {
        if (signal < static_kernel_input_marks.size()
            && static_kernel_input_marks[signal] == 1U) {
            static_kernel_input_marks[signal] = 2U;
            static_kernel_dirty_inputs.push_back(signal);
        }
    }
    /// A named event's trigger is an event of the event variable whatever
    /// its value (IEEE 1800-2023 15.5.1): each one bumps its revision, so
    /// revision readers (the static kernel's inputs) see every trigger.
    void note_event_trigger(const SignalId signal)
    {
        if (++signal_value_revisions[signal] == 0U) {
            signal_value_revisions[signal] = 1U;
            std::ranges::fill(container_materialized_revisions, std::nullopt);
        }
        note_signal_revision(signal);
    }
    std::vector<PackedLogic4> sampled_values;
    std::vector<PackedLogic4> sampled_defaults;
    // SignalId-indexed dependency mask for historical ReadSignal operations.
    // When dependency discovery is uncertain, all signals are sampled.
    std::vector<std::uint8_t> sampled_value_dependency_mask;
    bool sampled_value_dependencies_unknown { };
    bool requires_sampled_values { };
    // Dependency maps are built once after all topology is registered. The
    // module-path destination map lets update staging avoid scanning every
    // path for unrelated signals.
    [[nodiscard]] bool process_signal_access_is_complete(
        ProcessId process) const noexcept;
    [[nodiscard]] std::optional<ConstantDriverStartupEntry>
    recognize_constant_driver_startup(
        ProcessId process, const ProcessProgramView& program) const;
    std::vector<std::uint8_t> native_signal_dependency_mask;
    std::vector<std::uint8_t> module_path_destination_mask;
    // Immutable after start: static dependency and alias facts used by native
    // word publication. Runtime observations, waiters, drivers, force state,
    // and writer ownership remain checked at each publication.
    std::vector<std::uint8_t>
        native_signal_publication_shape_certificate;
    bool native_signal_dependencies_unknown { };
    std::map<SampledHistoryKey, SampledHistoryState> sampled_histories;
    // Capture SV clocked history at publication, even if the reader skips
    // evaluating its sampled-value operation for that time step.
    std::vector<std::vector<SampledHistoryKey>> sampled_history_keys_by_clock;
    std::vector<std::unique_ptr<PackedLogic4>> forced_values;
    std::vector<std::unique_ptr<PackedLogic4>> forced_masks;
    // A force on a port-connected net by its own name and one through a
    // hierarchical reference to the child's side of the port share the
    // merged net's force mask; the layers keep both so releasing the outer
    // one reveals the inner (IEEE 1800-2023 10.6.2, 23.3.3). Present only
    // for signals that were forced through such a reference.
    struct PortForceLayer {
        bool port_segment { };
        std::size_t offset { };
        PackedLogic4 value;
    };
    std::unordered_map<SignalId, std::vector<PortForceLayer>>
        port_force_layers;
    // Named-event variables carry synchronization-object identities rather
    // than copied packed values. Each event starts with its own stable object.
    std::vector<std::optional<SignalId>> event_identities;
    // Event variables bound to each synchronization identity in SignalId
    // order. This mirrors event_identities across alias rebinding.
    std::vector<std::vector<SignalId>> event_identity_members;
    ProcessTable processes;
    ProcessProgramTemplatePool process_program_templates;
    // Retain each original shared operation body only until registration is
    // complete, so lazy startup banks can preserve the same body-sharing groups.
    std::unordered_map<const void*,
        std::shared_ptr<ProcessStartupWriteBodyCache>>
        startup_write_body_caches;
    // Operation kinds present in each shared body, so registration scans a
    // body once rather than once per instance. The copied list pins the body
    // until start; instances with operation overrides scan themselves.
    struct OperationBodyKinds {
        OperationList body;
        std::bitset<static_kernel_detail::operation_kind_count> kinds;
        bool sampled_read { };
        bool transaction_wait { };
    };
    std::unordered_map<const void*, OperationBodyKinds> operation_body_kinds;
    [[nodiscard]] const OperationBodyKinds* body_kinds(
        const OperationList& operations);
    // Index by the original ProcessId. Eligible constant continuous
    // assignments keep their process identity and startup literal, while
    // startup execution stages the write without materializing a register
    // frame or retaining the operation body.
    std::vector<MailboxState> mailboxes;
    struct DynamicEventState {
        std::vector<ProcessId> waiters;
        std::optional<SimulationTick> last_trigger;
    };
    std::vector<DynamicEventState> dynamic_events;
    std::vector<SemaphoreState> semaphores;
    std::unordered_map<std::int32_t, StochasticQueueState>
        stochastic_queues;
    // The concurrent-property monitor of each assertion directive, by
    // owning process and directive site (IEEE 1800-2017 16.14).
    std::map<std::pair<ProcessId, std::uint32_t>,
        std::unique_ptr<sva::Monitor>>
        sva_monitors;
    std::vector<ModulePath> module_paths;
    std::vector<ModuleTimingCheck> module_timing_checks;
    std::vector<ModuleTimingCheckState> module_timing_check_states;
    std::map<std::uint64_t, ForkGroup> fork_groups;
    std::uint64_t next_fork_group { 1 };
    // Named block and task body intervals by hierarchical name, built from
    // block_entry/block_exit debug points when a disable first needs them.
    struct NamedScopeInterval {
        ProcessId design_process { };
        InstructionIndex begin { };
        InstructionIndex end { };
    };
    std::optional<std::unordered_map<std::string,
        std::vector<NamedScopeInterval>>> named_scope_intervals;
    std::uint32_t next_process_generation { 1 };
    std::vector<Fanout> static_fanout_entries;
    std::vector<std::size_t> static_fanout_offsets;
    std::vector<std::size_t> static_fanout_category_entries;
    std::vector<std::array<FanoutSpan, 4U>> static_fanout_category_spans;
    bool static_fanout_dirty { true };
    // Exact static-sensitivity cohorts span the complete elaborated design,
    // including aliases which resolve to the same clock SignalId across
    // hierarchy. Cohorts batch scheduler dispatch while retaining each
    // member's process identity, frame, driver ownership, and program counter.
    std::vector<StaticSensitivityCohort> static_sensitivity_cohorts;

    std::vector<std::size_t> static_sensitivity_cohort_by_process;
    std::unordered_map<std::string, std::size_t>
        static_sensitivity_cohort_by_key;
    std::uint64_t static_fanout_visit_generation { };
    // Set once a complete callback observation pass has made every hidden
    // signal public; a single-signal observation or native publication clears
    // it so the next callback repeats the pass.
    bool callback_observation_complete { };
    std::vector<std::vector<DynamicWaitRegistration>> dynamic_fanout;
    // Counts exclude tombstones. Slot indices in each live process sidecar
    // make removal proportional to that process's own sensitivity list.
    std::vector<std::size_t> dynamic_fanout_active_counts;
    std::vector<std::size_t> dynamic_fanout_tombstone_counts;
    std::vector<std::vector<ProcessId>> container_dynamic_fanout;
    std::vector<EventState> event_states;
    std::vector<std::optional<std::pair<
        SimulationTick, std::uint64_t>>>
        signal_events;
    std::vector<SignalEventSchedulingStamp> signal_event_scheduling_stamps;
    std::vector<std::optional<std::pair<
        SimulationTick, std::uint64_t>>>
        signal_transactions;
    std::vector<PendingUpdate> pending_updates;
    std::vector<PackedLogic4> pending_update_values;
    // Update-phase scratch is indexed by the dense signal identity and reused
    // across deltas. Only touched entries are reset after publication, avoiding
    // per-delta map/set construction and whole-container snapshots.
    std::vector<std::optional<PackedLogic4>> unresolved_update_scratch;
    std::vector<UnresolvedUpdateOwnerProvenance>
        unresolved_update_owner_provenance;
    std::vector<SignalId> unresolved_update_owner_provenance_signals;
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
    std::vector<std::uint8_t> direct_single_driver_commit_marked;
    std::vector<SignalId> driver_update_signals;
    std::vector<SignalId> resolved_update_signals;
    std::vector<std::uint8_t> resolved_update_marked;
    struct UpdateCommitScratchRow {
        SignalId signal { };
        PackedLogic4 value;
        std::size_t next_in_word {
            std::numeric_limits<std::size_t>::max() };
    };
    struct UpdateCommitWordScratch {
        std::uint64_t dirty_mask { };
        std::size_t head {
            std::numeric_limits<std::size_t>::max() };
    };
    std::vector<UpdateCommitScratchRow> update_commit_scratch;
    std::vector<UpdateCommitWordScratch> update_commit_words;
    std::unordered_set<std::uint64_t> pending_channel_updates;
    std::unordered_map<
        InertialDriverKey,
        PendingInertialWrite,
        InertialDriverKeyHash>
        pending_inertial_writes;
    // Initial values of nets with X bits from delayed continuous
    // assignments (Interpreter::add_signal).
    std::unordered_map<SignalId, PackedLogic4> delayed_net_initial_values;
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
    ContainerElementChangeHook container_element_change_hook;
    ScalarSignalChangeHook scalar_signal_change_hook;
    ExecutionPointHook execution_point_hook;
    OutputCallback output_hook;
    ReportCallback report_hook;
    CoverageSampleHook coverage_sample_hook;
    CoverageQueryHook coverage_query_hook;
    VhdlPslApiHook vhdl_psl_api_hook;
    std::array<std::uint64_t, 4> vhdl_assert_counts { };
    std::array<bool, 4> vhdl_assert_enabled { true, true, true, true };
    std::array<std::string, 4> vhdl_assert_formats {
        "{r}", "{r}", "{r}", "{r}" };
    AssertionSeverity vhdl_read_severity { AssertionSeverity::error };
    struct VhdlReflectionMirror {
        VhdlReflectionType type;
        PackedLogic4 value { 0U };
        bool has_value { };
        std::optional<PackedLogic4> designated_value;
    };
    // Index zero is the null access value. Positive identities are stable for
    // the simulation lifetime and contain owning value snapshots.
    std::vector<VhdlReflectionMirror> vhdl_reflection_mirrors {
        VhdlReflectionMirror { }
    };
    CoverageControlHook coverage_control_hook;
    CoverageAccessHook coverage_access_hook;
    CodeCoverageCounters code_coverage_counters;
    CodeCoverageOverflowHook code_coverage_overflow_hook;
    SystemCommandHook system_command_hook;
    VcdControlHook vcd_control_hook;
    CoverageDatabaseControlHook coverage_database_control_hook;
    std::shared_ptr<const ForkSpawnFilter> fork_spawn_filter;
    ClassAllocateContextHook class_allocate_context_hook;
    std::optional<std::uint64_t> pending_class_allocation_seed;
    ClassPropertyReadHook class_property_read_hook;
    ClassPropertyWriteHook class_property_write_hook;
    ClassPropertyContainerHook class_property_container_hook;
    ClassMethodCallContextHook class_method_call_context_hook;
    ClassStaticPropertyReadHook class_static_property_read_hook;
    ClassStaticPropertyWriteHook class_static_property_write_hook;
    ClassStaticMethodCallContextHook class_static_method_call_context_hook;
    DpiFunctionCallHook dpi_function_call_hook;
    std::optional<MonitorInstall> monitor;
    std::vector<std::uint8_t> monitor_signal_watch_mask;
    bool monitor_signal_watches_unknown { };
    ProcessId monitor_process { };
    std::optional<FileHandle> monitor_file_handle;
    bool monitor_enabled { true };
    std::uint64_t monitor_generation { };
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        monitor_publication;
    std::optional<SimulationTick> monitor_systemverilog_publication;
    bool update_commit_scheduled { };
    // The update commit callback is running; it drains conversions last.
    bool update_commit_running { };
    bool has_bidirectional_switches { };
    bool switch_refreshing { };
    bool started { };
    struct PackageStaticStartupCallback {
        std::uint64_t startup_order { };
        std::vector<SignalId> writable_signal_ids;
        std::vector<StringObjectId> writable_string_object_ids;
        std::vector<ContainerObjectId> writable_container_object_ids;
        std::function<void()> callback;
    };
    std::vector<PackageStaticStartupCallback>
        package_static_startup_callbacks;
    bool package_static_startup_registration_closed { };
    bool package_static_startup_actions_entered { };
    bool start_in_progress { };
    bool package_static_initialization_active { };
    bool package_static_initialization_callback_active { };
    std::size_t package_static_initialization_callback_index {
        std::numeric_limits<std::size_t>::max()
    };
    ProcessId package_static_initialization_process {
        std::numeric_limits<ProcessId>::max()
    };
    std::vector<ProcessId> package_static_startup_signal_processes;
    bool validation_only { };
    bool stopped_by_design { };
    // A `$stop` executed by a final procedure (IEEE 1800-2017 9.2.3).
    bool paused_in_final { };
    std::optional<std::int64_t> simulator_status;
    bool finals_ran { };
    bool process_profile_enabled { };
    bool process_profile_reported { };
    bool update_profile_enabled { };
    bool update_profile_reported { };
    struct CommitSignalProfileRow {
        std::uint64_t calls { };
        std::uint64_t slice_calls { };
        std::uint64_t nanoseconds { };
    };
    bool commit_signal_profile_enabled { };
    // Interpreter::retain_state_until_exit.
    bool retain_state_until_exit { };
    // Aggregate revision at which each aliased container's element vector
    // was last extracted from its proxy (no_container_extract_revision when
    // never or invalidated).
    static constexpr std::uint64_t no_container_extract_revision
        = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint64_t> container_aggregate_extract_revisions;
    void invalidate_container_aggregate_extract(
        const ContainerObjectId id) noexcept
    {
        if (id < container_aggregate_extract_revisions.size()) {
            container_aggregate_extract_revisions[id]
                = no_container_extract_revision;
        }
    }
    struct FusedConeState {
        FusedConeRuntimeSpec spec;
        std::vector<std::uint64_t> boundary_revisions;
        bool materialized { };
    };
    std::vector<FusedConeState> fused_cones;
    std::vector<std::uint32_t> fused_cone_by_signal;
    std::vector<std::uint8_t> fusion_dormant_process;
    /// 1 for a static kernel member whose original program had a static
    /// sensitivity. Members are installed without it, but the signals they
    /// drive still settle in the time-zero active region (start()).
    std::vector<std::uint8_t> static_kernel_event_driven_process;
    bool fused_cone_materializing { };
    void materialize_fused_cone(std::size_t cone);
    // Engine v4 static kernel (simir_static_kernel.cpp). Owned signals live
    // in kernel storage and are published on observation.
    class StaticKernel;
    std::shared_ptr<StaticKernel> static_kernel;
    /// execute() calls that run SimIR operations on this evaluator
    /// (processes and programs without a native executor).
    std::uint64_t interpreted_executes { };
    /// Compact constant drivers run (no operation is interpreted).
    std::uint64_t constant_driver_executes { };
    /// FSIM_PROFILE_PHASES: interpreted_executes by program.
    std::map<ProcessId, std::uint64_t> interpreted_programs;
    void note_interpreted_execute(ProcessId id);
    std::vector<std::uint8_t> static_kernel_owned_signal;
    bool static_kernel_materializing { };
    void materialize_static_kernel_signal(SignalId signal);
    /// Publish-all designs (StaticKernelRuntimeSpec::import_host_writes):
    /// publish_value_change hands host commits to kernel-owned signals to the
    /// kernel. One flag test guards the hook; it is false otherwise.
    bool static_kernel_import_enabled { };
    /// The kernel's host process runs a stub scheduled in the Active
    /// region. When the member it replaces is an Observed or Reactive
    /// process, that member's updates keep its own region
    /// (capture_signal_change_origin).
    std::optional<ProcessId> static_kernel_region_host;
    SchedulerPhase static_kernel_region_host_phase { SchedulerPhase::active };
    /// True while StaticKernel::publish commits its own outputs under the
    /// member driver identities; those commits must not be imported back.
    bool static_kernel_publishing { };
    void import_static_kernel_write(SignalId signal, SignalChangeOrigin origin);
    // Hosted processes (StaticKernelMemberKind::hosted,
    // simir_static_kernel_hosted.cpp): 1 for a process the kernel schedules
    // and this evaluator executes. Every queue_* entry point hands such a
    // process's wake to the kernel instead of the host scheduler.
    enum class StaticKernelHostedWake : std::uint8_t {
        /// queue_current: continue in the current region (the next round).
        current,
        /// queue_active_current and a same-domain queue_next_delta: the next
        /// round of the Active region.
        active,
        /// queue_next_delta from another scheduling domain: after the
        /// current delta, as the host scheduler's next-delta queue.
        next_delta,
        /// queue_zero_delay_resume: the Inactive region (IEEE 1800-2023 4.4).
        inactive,
        /// queue_at: a timer at `time` (or the Active region if due).
        at,
    };
    std::vector<std::uint8_t> static_kernel_hosted_process;
    [[nodiscard]] bool static_kernel_hosts(const ProcessId id) const noexcept
    {
        return id < static_kernel_hosted_process.size()
            && static_kernel_hosted_process[id] != 0U;
    }
    /// The static kernel runs as `id` (its host process).
    [[nodiscard]] bool static_kernel_host(ProcessId id) const noexcept;
    void static_kernel_enqueue_hosted(ProcessId id,
        StaticKernelHostedWake wake, SimulationTick time = 0U,
        StableOrder order = 0U);
    /// A fork child of a hosted process is hosted too.
    void static_kernel_adopt_fork_child(ProcessId parent, ProcessId child);
    std::vector<CommitSignalProfileRow> commit_signal_profile_rows;
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
    std::uint64_t native_phase_profile_shape_certificate_hits { };
    std::uint64_t native_phase_profile_shape_certificate_misses { };
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
    std::uint64_t native_update_profile_unchanged_owned { };
    std::uint64_t native_update_profile_direct_word { };
    std::uint64_t native_update_profile_direct_packed { };
    std::uint64_t native_update_profile_unresolved { };
    std::uint64_t native_update_profile_resolved { };
    std::uint64_t native_update_profile_changed_owned { };
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
    bool profile_static_cohorts_enabled { };
    bool profile_processes_all_enabled { };
    bool fanout_cohort_grouping_enabled { true };
    bool static_phase_batches_enabled { true };
    bool direct_word_commit_disabled { };
    bool logic9_batch_profile_enabled { };
    std::set<std::uint32_t> program_owners;
    std::set<std::uint32_t> exited_programs;

    [[nodiscard]] SignalHot& get_signal(SignalId id);

    [[nodiscard]] const SignalHot& get_signal(SignalId id) const;

    [[nodiscard]] SignalCold& get_signal_cold(SignalId id);

    [[nodiscard]] const SignalCold& get_signal_cold(SignalId id) const;

    void materialize_direct_signal(SignalId id);
    void prepare_signal_observation(SignalId signal);
    void prepare_callback_observation();
    void expose_signal_value_reference(SignalId signal);
    void expose_container_value_reference(ContainerObjectId object);
    void prepare_container_value_observation(ContainerObjectId object);
    [[nodiscard]] ContainerReferenceRefresh
    make_container_reference_refresh(ContainerObjectId object) const;
    [[nodiscard]] PreparedContainerReferenceRefresh
    prepare_container_value_reference_refresh(
        std::span<const SignalId> signals);
    void prepare_active_container_value_reference_refresh(
        ContainerObjectId object);
    void begin_container_value_reference_refresh(
        ActiveContainerReferenceRefresh& frame,
        std::span<const SignalId> signals,
        PreparedContainerReferenceRefresh& prepared) noexcept;
    void end_container_value_reference_refresh(
        ActiveContainerReferenceRefresh& frame) noexcept;
    void synchronize_container_value_references(
        std::span<const SignalId> signals,
        PreparedContainerReferenceRefresh& prepared) noexcept;
    void synchronize_container_value_references_from_object(
        ContainerObjectId object) noexcept;
    void prepare_container_value_reference_object_update(
        ContainerObjectId object,
        const ContainerValue& replacement);
    [[nodiscard]] bool container_value_reference_depends_on_signals(
        ContainerObjectId object,
        std::span<const SignalId> signals) const noexcept;

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
    // A waveform element's delay, evaluated now when it is run-time
    // valued (IEEE 1076-2008 10.5.2.1).
    [[nodiscard]] SimulationTick projected_element_delay(
        ProcessState& process, const ProjectedWaveformElement& element);
    // The same from the delay source's value (`payload`, ignored for a
    // constant delay); the kernel's members evaluate their own registers.
    [[nodiscard]] static SimulationTick projected_element_delay(
        const ProjectedWaveformElement& element, const PackedLogic4& payload);
    // The rejection limit; the sentinel maximum takes the first element's
    // delay (the default inertial rejection, 10.5.2.1).
    [[nodiscard]] static SimulationTick projected_rejection(
        SimulationTick rejection,
        const std::vector<ProjectedWaveformValue>& elements);

    [[nodiscard]] std::string& get_string_register(
        ProcessState& process,
        StringRegisterId id);

    [[nodiscard]] StringObject& get_string_object(StringObjectId id);

    [[nodiscard]] const StringObject&
    get_string_object(StringObjectId id) const;

    // The value storage of a string object: its own, or the container
    // element it aliases.
    [[nodiscard]] std::string& string_object_storage(StringObjectId id);
    [[nodiscard]] const std::string&
    string_object_storage(StringObjectId id) const;

    [[nodiscard]] ContainerValue& get_container_register(
        ProcessState& process, ContainerRegisterId id);
    [[nodiscard]] const ContainerValue& read_container_register(
        const ProcessState& process, ContainerRegisterId id) const;
    // The bits of a one-bit queue as one value, its first bit most
    // significant (a DPI open-array actual of run-time size).
    [[nodiscard]] static PackedLogic4 bit_stream_value(
        const ContainerValue& bits)
    {
        PackedLogic4 value(bits.elements.size(), Logic4::zero);
        for (std::size_t index { }; index < bits.elements.size(); ++index) {
            value.set(bits.elements.size() - 1U - index,
                bits.elements[index].get(0U));
        }
        return value;
    }
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
        ContainerObjectId id,
        const ContainerValue& value,
        std::optional<ProcessId> driver = std::nullopt,
        SignalChangeOrigin origin = { });
    [[nodiscard]] bool can_stage_container_alias_deposit(
        ContainerObjectId object) const;
    // `registration`, when given, is the program the process stands for: a
    // fused cone's sink or the static kernel's host runs a substitute
    // program, but the drivers, history queries, transaction observations
    // and mailbox shapes registered from operations are the original's.
    /// Registers the signals a program's sampled reads need histories of
    /// (sampled_value_dependency_mask).
    void record_sampled_dependencies(const ProcessProgramView& process);
    [[nodiscard]] ProcessId add_process_program_impl(
        ProcessProgramView process,
        std::shared_ptr<const ProcessProgramTemplate> common_program,
        Process* owned_process,
        ProcessInstanceProgram* owned_instance,
        bool executable,
        const ProcessProgramView* registration = nullptr);
    [[nodiscard]] std::map<SignalId, std::vector<Process::DriverRegion>>
    expanded_driver_regions(const ProcessProgramView& process) const;
    void write_container_alias_deposit(
        ContainerObjectId object,
        const std::vector<PackedLogic4>& values,
        SignalChangeOrigin origin);
    void publish_container_alias_family(
        ContainerObjectId object,
        const std::vector<PackedLogic4>& values,
        SignalChangeOrigin origin,
        std::span<ContainerAliasForceUpdate> force_updates,
        std::span<const std::uint8_t> selected_leaves = { },
        std::span<ContainerAliasDriverForceUpdate> driver_force_updates = { });
    void update_container_alias_force(
        ContainerObjectId object,
        const PackedLogic4* value,
        std::size_t offset,
        std::size_t width);
    [[nodiscard]] bool update_container_alias_driver_force(
        ContainerObjectId object,
        ProcessId process,
        const PackedLogic4* value,
        std::size_t offset,
        std::size_t width);
    [[nodiscard]] PreparedContainerAliasDriverFamily
    prepare_container_alias_driver_family(
        ContainerObjectId object,
        ProcessId process,
        const std::vector<PackedLogic4>& values,
        SignalChangeOrigin origin,
        std::span<const std::uint8_t> selected_leaves = { });
    // Open before installing any raw driver table because aggregate-frame
    // snapshot preparation can allocate. After begin succeeds, callers must
    // install, notify, and finish the raw phase before any throwing finalize
    // work. Raw notification/finish are noexcept and drain callbacks while
    // retaining the first failure; finish also closes/restores the batch.
    // Finalize resolves live drivers, then publishes stored/current state in
    // the caller's value-publication order.
    void begin_container_alias_driver_family(
        PreparedContainerAliasDriverFamily& prepared);
    void install_container_alias_driver_family(
        PreparedContainerAliasDriverFamily& prepared) noexcept;
    void notify_container_alias_driver_family_raw(
        PreparedContainerAliasDriverFamily& prepared) noexcept;
    void finish_container_alias_driver_family_raw(
        PreparedContainerAliasDriverFamily& prepared) noexcept;
    void finalize_container_alias_driver_family(
        PreparedContainerAliasDriverFamily& prepared);
    void write_container_alias_driver_family(
        ContainerObjectId object,
        ProcessId process,
        const std::vector<PackedLogic4>& values,
        SignalChangeOrigin origin);
    void write_container_object_element_value(
        ContainerObjectId id,
        const PackedLogic4& index,
        bool signed_index,
        bool linear_index,
        const PackedLogic4& value,
        ProcessId process,
        InstructionIndex instruction,
        SignalChangeOrigin origin = { });
    // A process's write of a design object or of one of its elements (IEEE
    // 1800-2023 10.4.1, 10.4.2): a blocking one now, a nonblocking one from
    // the NBA region (the update phase outside SystemVerilog). The
    // interpreter and the static kernel's members both make it.
    void write_object(const WriteContainerObject& operation,
        ProcessSchedulingDomain process_domain, ProcessId driver,
        InstructionIndex instruction, const ContainerValue& source);
    void write_object_element(const WriteContainerObjectElement& operation,
        ProcessSchedulingDomain process_domain, ProcessId driver,
        InstructionIndex instruction, const PackedLogic4& index,
        const PackedLogic4& value, const std::optional<PackedLogic4>& base,
        std::vector<PackedLogic4> level_indices);
    void write_container_object_member_value(
        ContainerObjectId id,
        const PackedLogic4& index,
        bool signed_index,
        bool linear_index,
        std::span<const std::uint32_t> members,
        const PackedLogic4& value,
        ProcessId process,
        InstructionIndex instruction,
        SignalChangeOrigin origin = { });
    // Write the element reached through nested container levels (their
    // indices captured in `level_indices`) by rebuilding the object value.
    void write_container_object_path_value(
        ContainerObjectId id,
        std::span<const ContainerPathLevel> levels,
        std::span<const PackedLogic4> level_indices,
        const PackedLogic4& index,
        bool signed_index,
        bool linear_index,
        std::span<const std::uint32_t> members,
        const PackedLogic4& value,
        const PackedLogic4* base,
        const DynamicPartIndex* selection,
        ProcessId process,
        InstructionIndex instruction,
        SignalChangeOrigin origin = { });
    void write_container_object_dynamic_part_element_value(
        ContainerObjectId id,
        const PackedLogic4& index,
        bool signed_index,
        bool linear_index,
        const PackedLogic4& value,
        const PackedLogic4& base,
        const DynamicPartIndex& selection,
        ProcessId process,
        InstructionIndex instruction,
        SignalChangeOrigin origin = { });

    void set_file_root(std::filesystem::path root);
    [[nodiscard]] FileHandle open_file(
        ProcessId process,
        std::string_view path,
        std::string_view mode,
        bool vhdl = false);
    [[nodiscard]] FileHandle open_file_unchecked(
        ProcessId process,
        std::string_view path,
        std::string_view mode,
        bool vhdl);
    // An open file for a SystemVerilog descriptor, or nullptr when the
    // descriptor names no open file.
    [[nodiscard]] FileState* open_systemverilog_file(FileHandle handle);
    [[nodiscard]] bool soft_unreadable_file(
        ProcessId process, FileState& file);
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
    // lines: a VHDL typed file, whose units are its lines.
    [[nodiscard]] std::int32_t position_file(
        ProcessId process,
        FileHandle handle,
        FilePositionKind kind,
        std::int32_t offset,
        std::int32_t origin,
        bool lines = false);
    [[nodiscard]] std::int32_t position_vhdl_file(
        ProcessId process,
        FileHandle handle,
        FilePositionKind kind,
        std::int32_t offset,
        std::int32_t origin,
        bool lines);
    void flush_file(ProcessId process, std::optional<FileHandle> handle);
    /// The registers and blocking signal writes of the process that runs a
    /// file operation: a reference process's frame (execute_file) or a
    /// static kernel member's (its behavioral steps), so both run one
    /// implementation (run_file).
    struct FileOperationStorage {
        virtual ~FileOperationStorage() = default;
        [[nodiscard]] virtual ProcessId process() const noexcept = 0;
        [[nodiscard]] virtual PackedLogic4& packed(RegisterId id) = 0;
        [[nodiscard]] virtual std::string& text(StringRegisterId id) = 0;
        // A file handle operand (a compiled process's executor holds it).
        [[nodiscard]] virtual PackedLogic4 handle(RegisterId id) = 0;
        // A packed signal target, written as commit_driver writes it.
        virtual void commit(SignalId signal, PackedLogic4 value) = 0;
        // A container register or design memory ($fread's target), read
        // and written whole.
        [[nodiscard]] virtual ContainerValue container(ContainerRegisterId id) = 0;
        virtual void write_container(ContainerRegisterId id, ContainerValue value) = 0;
        [[nodiscard]] virtual ContainerValue object(ContainerObjectId id) = 0;
        virtual void write_object(ContainerObjectId id, const ContainerValue& value) = 0;
        // An error at the running instruction.
        [[noreturn]] virtual void fail(const std::string& message) = 0;
    };
    struct ProcessFileStorage;
    [[nodiscard]] FileHandle known_file_handle(
        ProcessState&, RegisterId);
    [[nodiscard]] FileHandle known_file_handle(
        FileOperationStorage&, RegisterId);
    // A file operation's effect on its process's storage; other errors throw
    // std::exception for the caller to report at its instruction.
    void run_file(FileOperationStorage&, const FileOpen&);
    void run_file(FileOperationStorage&, const FileClose&);
    void run_file(FileOperationStorage&, const FileWriteLiteral&);
    void run_file(FileOperationStorage&, const FileWriteFormatted&);
    void run_file(FileOperationStorage&, const FileWriteString&);
    void run_file(FileOperationStorage&, const FileReadLine&);
    void run_file(FileOperationStorage&, const FileEndOfFile&);
    void run_file(FileOperationStorage&, const FileErrorStatus&);
    void run_file(FileOperationStorage&, const FileScan&);
    void run_file(FileOperationStorage&, const FilePosition&);
    void run_file(FileOperationStorage&, const FileFlush&);
    void run_file(FileOperationStorage&, const FileBinaryRead&);
    /// A dump control (IEEE 1800-2023 21.7) with its file name and value
    /// in `event`: the VCD observer's event at `now`; $dumpvars and
    /// $dumpports begin the dump in the Postponed region.
    void control_vcd(ProcessId process, const VcdControl& operation,
        VcdControlEvent event);
    template <typename Operation>
    void run_process_file(ProcessState&, const Operation&);
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
    void execute_container(ProcessState&, const FormatContainerPattern&);
    void execute_container(ProcessState&, const ContainerElementRead&);
    void execute_container(ProcessState&, const ContainerElementWrite&);
    void execute_container(ProcessState&, const ContainerBitstream&);
    void execute_container(ProcessState&, const VhdlShaped&);
    void execute_container(ProcessState&, const ContainerAggregateRead&);
    void execute_container(ProcessState&, const ContainerAggregateWrite&);
    void execute_container(
        ProcessState&, const CopyContainerAggregateElement&);
    void execute_container(ProcessState&, const DeleteContainer&);
    void execute_container(ProcessState&, const ContainerExists&);
    void execute_container(ProcessState&, const TraverseContainer&);
    void execute_container(ProcessState&, const LoadMemory&);
    /// $readmemb/$readmemh (IEEE 1800-2023 21.4): loads the file at `path`
    /// into `target`. A file that cannot be opened, a malformed file or an
    /// address outside the range is reported and the load ends; the words
    /// loaded before an error stay.
    void read_memory_file(ProcessId process, InstructionIndex pc,
        const std::string& path, ContainerValue& target, bool hexadecimal,
        std::optional<std::int32_t> start, std::optional<std::int32_t> finish);
    /// $writememb/$writememh (IEEE 1800-2023 21.5): writes `source` to the
    /// file at `path`. Errors throw std::exception.
    void write_memory_file(ProcessId process, const std::string& path,
        const ContainerValue& source, bool hexadecimal,
        std::optional<std::int32_t> start, std::optional<std::int32_t> finish);
    void execute_container(ProcessState&, const VitalMemoryDeclare&);
    void execute_container(ProcessState&, const PushContainer&);
    void execute_container(ProcessState&, const AppendContainer&);
    void execute_container(ProcessState&, const PopContainer&);
    void execute_string(ProcessState&, const StringMethod&);
    void execute_stochastic_queue(
        ProcessState&, const StochasticQueueOperation&);
    // The storage of an assertion operation (SvaOperation, a sampled
    // read): its process's registers and errors at the running instruction.
    // A reference process and a static kernel thread provide it.
    struct AssertionStorage {
        virtual ~AssertionStorage() = default;
        // The owner of the directive's monitor: the process, or the number
        // the reference gives a fork child.
        [[nodiscard]] virtual ProcessId process() const noexcept = 0;
        [[nodiscard]] virtual ProcessId design_process() const noexcept = 0;
        [[nodiscard]] virtual ProcessSchedulingDomain domain() const noexcept = 0;
        [[nodiscard]] virtual PackedLogic4 read(RegisterId id, std::size_t width) = 0;
        virtual void write(RegisterId id, const PackedLogic4& value) = 0;
        [[noreturn]] virtual void fail(const std::string& message) = 0;
    };
    struct ProcessAssertionStorage;
    void run_sva(AssertionStorage&, const SvaOperation&);
    void run_sampled_read(AssertionStorage&, const ReadSignal&);
    void execute_sva(ProcessState&, const SvaOperation&);
    void execute_pla(ProcessState&, const PlaEvaluate&);
    void execute_random_distribution(ProcessState&, const RandomDistribution&);

    [[nodiscard]] static ValueKind register_value_kind(
        const ProcessState& process,
        const RegisterId id);

    [[nodiscard]] static PackedLogic4 coerce_value_kind(
        PackedLogic4 value,
        const ValueKind kind);

    [[nodiscard]] PackedLogic4 normalize_signal_value(
        const SignalId signal,
        PackedLogic4 value) const;

    void remove_dynamic_wait(ProcessState& process)
    {
        // Every dynamic wait sets this before recording cold registrations.
        if (!process.waiting_on_signal) {
            return;
        }
        remove_dynamic_wait_nonempty(process);
    }

    void remove_dynamic_wait_nonempty(ProcessState& process);

    void register_dynamic_wait_fanout(ProcessState& process);

    void ensure_dynamic_fanout_counts();

    void compact_dynamic_fanout(SignalId signal);

    [[nodiscard]] bool dynamic_wait_registration_is_current(
        const ProcessState& process,
        const DynamicWaitRegistration& registration,
        SignalId signal) const noexcept;

    [[nodiscard]] bool has_dynamic_waits(SignalId signal) const noexcept;

    // Whether a container object is aliased by a signal, an aggregate
    // signal, or one of its element signals.
    [[nodiscard]] bool container_has_signal_alias(
        ContainerObjectId object) const noexcept;
    // Throws unless the active package static initializer (process or
    // startup callback) may write this string or container object.
    void require_package_static_write(std::string_view kind, bool in_range,
        std::uint64_t owner, std::uint32_t object,
        std::optional<ProcessId> process, bool container) const;
    // Mirror a callable register write into the automatic callable context
    // that owns `destination`. Returns true when the write targets a
    // suspended context only, so the live register file is left untouched.
    template <typename Id, typename Value>
    bool update_callable_register_context(ProcessState& process,
        Id destination, std::vector<Id> ProcessState::CallableFrameState::*ids,
        std::vector<Value> ProcessState::CallableFrameState::*values,
        const Value& value);
    void write_process_register(
        ProcessState& process,
        const RegisterId destination,
        const PackedLogic4& value);
    void write_process_string_register(
        ProcessState& process,
        StringRegisterId destination,
        std::string_view value);

    void install_deferred_executor(ProcessState& process);
    void advance_process_executor_generation(ProcessState& process) noexcept;
    [[nodiscard]] bool deferred_executor_ready(ProcessState& process);
    void prepare_deferred_executor_callback(ProcessState& process);

    [[nodiscard]] std::int32_t control_coverage(
        const CoverageControlEvent& event) noexcept;
    [[nodiscard]] std::int32_t access_coverage(
        const CoverageAccessEvent& event) noexcept;

    void execute_sampled_read(
        ProcessState& process,
        const ReadSignal& operation);
    void build_sampled_history_clock_index();
    static void ensure_sampled_history_capacity(
        SampledHistoryState& history,
        std::size_t capacity,
        const PackedLogic4& initial_value);
    [[nodiscard]] static const PackedLogic4& sampled_history_value(
        const SampledHistoryState& history,
        std::size_t index);
    [[nodiscard]] static SampledHistorySlot make_sampled_history_slot(
        SimulationTick time,
        ProcessSchedulingDomain process_domain,
        SignalChangeOrigin origin,
        std::uint64_t generic_delta);
    [[nodiscard]] static bool same_sampled_history_slot(
        const SampledHistorySlot& previous,
        const SampledHistorySlot& current);
    static void append_sampled_history(
        SampledHistoryState& history,
        const SampledHistorySlot& slot,
        const PackedLogic4& value);

    void clear_wait_timeout(ProcessState& process)
    {
        if (!process.wait_timeout_origin) {
            return;
        }
        clear_wait_timeout_nonempty(process);
    }

    void clear_wait_timeout_nonempty(ProcessState& process);

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

    void capture_callable_values(
        ProcessState& process,
        ProcessState::CallableFrameState& context,
        const std::set<RegisterId>& excluded_packed = { },
        const std::set<StringRegisterId>& excluded_strings = { },
        const std::set<ContainerRegisterId>& excluded_containers = { });

    void restore_callable_values(
        ProcessState& process,
        const ProcessState::CallableFrameState& context,
        const std::set<RegisterId>& excluded_packed = { },
        const std::set<StringRegisterId>& excluded_strings = { },
        const std::set<ContainerRegisterId>& excluded_containers = { });

    [[nodiscard]] std::size_t callable_storage_bytes_in_use(
        const ProcessState& process,
        const ProcessState::CallableFrameState* additional_context = nullptr)
        const;
    void capture_escaping_callable_contexts(ProcessState& process);
    void restore_escaping_callable_contexts(ProcessState& process);
    void snapshot_callable_context(ProcessState& process);

    void restore_callable_context(ProcessState& process)
    {
        if (!process.has_callable_frame_push) {
            return;
        }
        const auto& cold = process.cold();
        if (cold.escaping_callable_contexts.empty()
            && !cold.suspended_callable_context) {
            process.cold().callable_registers_suspended = false;
            return;
        }
        restore_callable_context_nonempty(process);
    }

    void restore_callable_context_nonempty(ProcessState& process);

    [[nodiscard]] bool dynamic_wait_satisfied(
        ProcessState& process,
        const SignalId signal,
        const DynamicWaitRegistration& registration);

    void handle_boundary(ProcessState& process,
        InstructionIndex instruction,
        InstructionIndex next_instruction);
    void execute_scope_randomize(
        ProcessState& process,
        InstructionIndex instruction,
        const ScopeRandomize& operation);
    // std::randomize's register file (IEEE 1800-2023 18.12): the
    // reference's process frame or executor, or a kernel member's.
    struct RandomizeStorage {
        virtual ~RandomizeStorage() = default;
        [[nodiscard]] virtual PackedLogic4 packed(RegisterId id, std::size_t width) = 0;
        virtual void write_packed(RegisterId id, PackedLogic4 value) = 0;
        [[nodiscard]] virtual ContainerValue container(ContainerRegisterId id) = 0;
        virtual void write_container(ContainerRegisterId id, ContainerValue value) = 0;
        // The next draw of the running process's random stream (18.14).
        [[nodiscard]] virtual std::uint32_t next_random() = 0;
        // The running program's name, the with-clause's scope.
        [[nodiscard]] virtual std::string scope() = 0;
    };
    struct ProcessRandomizeStorage;
    void run_scope_randomize(RandomizeStorage& storage,
        InstructionIndex instruction, const ScopeRandomize& operation);
    void execute_vhdl_assert_api(
        ProcessState& process, const VhdlAssertApi& operation);
    /// The registers and reports of the process that runs a std.env
    /// operation (IEEE 1076-2019 16.5): a reference process's frame or a
    /// static kernel member's, so both run one implementation.
    struct VhdlApiStorage {
        virtual ~VhdlApiStorage() = default;
        [[nodiscard]] virtual PackedLogic4 packed(RegisterId id, std::size_t width) = 0;
        virtual void write_packed(RegisterId id, const PackedLogic4& value) = 0;
        [[nodiscard]] virtual std::string text(StringRegisterId id) = 0;
        virtual void write_text(StringRegisterId id, std::string_view value) = 0;
        [[nodiscard]] virtual ContainerValue container(ContainerRegisterId id) = 0;
        virtual void write_container(ContainerRegisterId id, ContainerValue value) = 0;
        // The running program and its call stack (return addresses), for
        // the call path (16.5.6) and the container register types.
        [[nodiscard]] virtual ProcessProgramView program() = 0;
        [[nodiscard]] virtual std::vector<InstructionIndex> call_stack() = 0;
        // A VHDL report at the running instruction (execute_vhdl_report).
        virtual void report(std::string_view message, AssertionSeverity severity,
            const SourceLocation& source, bool standalone) = 0;
        // An error at the running instruction.
        [[noreturn]] virtual void fail(const std::string& message) = 0;
    };
    struct ProcessVhdlApiStorage;
    // The application's class services (IEEE 1800-2023 8.4, 8.5, 8.9) on a
    // register file, the reference's process frame or a kernel member's: an
    // allocation for `process` (its design process and scope name), a
    // property or static property read or write. Each returns the
    // reference's message when its service is missing, and nullptr when it
    // ran.
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassAllocate&, ProcessId process, ProcessId design_process,
        std::string_view scope);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassPropertyRead&);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassPropertyWrite&);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassStaticPropertyRead&);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassStaticPropertyWrite&);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassPropertyContainerRead&);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassPropertyContainerWrite&);
    // A method call (8.6) or static method or DPI function call (8.10,
    // 35.5) for `process`, its actuals written back. A VHDL driver
    // disconnection is the caller's (try_vhdl_disconnect_call).
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassMethodCall&, ProcessId process);
    [[nodiscard]] const char* run_class_operation(ContainerRegisterAccess&,
        const ClassStaticMethodCall&, ProcessId process);
    // The assertion controls and counts (16.5.6).
    void run_vhdl_assert_api(VhdlApiStorage&, const VhdlAssertApi&);
    // The date and time functions (16.5.5); a bad operand fails.
    void run_vhdl_environment_time(VhdlApiStorage&, const VhdlEnvironmentTime&);
    // TO_STRING of a date/time record (16.5.5) and GETENV (16.5.6).
    void run_vhdl_environment_time_string(VhdlApiStorage&,
        const VhdlEnvironmentTimeToString&);
    void run_vhdl_environment_getenv(VhdlApiStorage&, const VhdlEnvironmentGetenv&);
    // The directory functions (16.5.6), GET_CALL_PATH and TO_STRING of a
    // call path (16.5.6), and the reflection API (16.6); an error fails.
    void run_vhdl_environment_directory(VhdlApiStorage&,
        const VhdlEnvironmentDirectory&);
    void run_vhdl_environment_get_call_path(VhdlApiStorage&,
        const VhdlEnvironmentGetCallPath&);
    void run_vhdl_environment_call_path(VhdlApiStorage&,
        const VhdlEnvironmentCallPath&);
    void run_vhdl_reflection_api(VhdlApiStorage&, const VhdlReflectionApi&);
    // 'DRIVING and 'DRIVING_VALUE of the process's driver of a signal (IEEE
    // 1076-2019 16.2.4): whether it is connected (a one-bit value), and its
    // current value, nothing when the process has no connected driver.
    [[nodiscard]] PackedLogic4 signal_driving(ProcessId process,
        const SignalDriving& operation);
    [[nodiscard]] std::optional<PackedLogic4> signal_driving_value(
        ProcessId process, const SignalDrivingValue& operation);
    void execute_vhdl_reflection_api(
        ProcessState& process, const VhdlReflectionApi& operation);
    void execute_vhdl_report(
        ProcessState& process,
        InstructionIndex instruction,
        std::string_view message,
        AssertionSeverity severity,
        const SourceLocation& source,
        bool standalone);
    /// A VHDL-2019 report or assertion violation (IEEE 1076-2019 10.9,
    /// 16.5.6): the std.env assertion controls, counts and message format,
    /// at `now`; a failure throws AssertionError at `instruction`. The
    /// reference evaluator and the static kernel both report through it.
    void report_vhdl(ProcessId process, std::string_view program,
        InstructionIndex instruction, std::string_view message,
        AssertionSeverity severity, const SourceLocation& source,
        bool standalone, SimulationTick now, std::uint64_t delta);
    void handle_external_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        InstructionIndex next_instruction,
        const ExternalSuspension& suspension);
    void request_channel_update(
        ProcessId process, std::uint64_t channel);
    void execute(ProcessId id);
    /// One operation of execute's loop: its semantics (true at a boundary),
    /// and the boundary handling after it (false when execute returns).
    template <typename Op>
    bool execute_operation(ProcessState& process, InstructionIndex instruction,
        const Op& op);
    bool continue_after_boundary(ProcessState& process, const Operation& operation,
        InstructionIndex instruction);
    /// Both for operation kind Op at `instruction`: the position execute's
    /// loop continues at, or operation_tier_stop when execute returns. The
    /// static kernel's operation-tier code for a hosted process calls the
    /// helper of each operation's kind (no dispatch over operations).
    template <typename Op>
    std::uint32_t hosted_step(ProcessState& process, InstructionIndex instruction);
    using HostedStep = std::uint32_t (Impl::*)(ProcessState&, InstructionIndex);
    [[nodiscard]] static const std::array<HostedStep,
        static_kernel_detail::operation_kind_count>& hosted_steps();
    /// Runs the process on the static kernel's operation-tier code (from its
    /// pc to where execute returns); false when it has none.
    bool run_hosted_tier(ProcessState& process);
    void execute_compact_constant(ConstantDriverStartupEntry& process);
    void execute_compact_startup_write(
        ConstantDriverStartupEntry& process);
    void execute_static_cohort(std::span<const ProcessId> processes);
    void execute_queued_static_cohort(std::size_t cohort);
    void prepare_systemverilog_update_pool();
    [[nodiscard]] SystemVerilogUpdateToken reserve_systemverilog_update(
        ProcessId process,
        SignalId signal,
        PackedLogic4 value,
        std::optional<std::size_t> offset,
        SignalChangeOrigin origin);
    void release_systemverilog_update(
        const SystemVerilogUpdateToken& token) noexcept;
    void clear_systemverilog_update_pool() noexcept;
    static void dispatch_systemverilog_update(
        Scheduler& scheduler,
        const SystemVerilogUpdateToken& token);
    void discard_scheduler_work() noexcept;
    [[nodiscard]] SchedulerBatchResult execute(
        Scheduler&, std::span<const std::uint64_t> cohort_ids) override;
    [[nodiscard]] bool handle_executor_resume(
        ProcessState& process, const ProcessResumeResult& boundary);

    void report_process_profile();

    void report_update_profile();

    // `initial` marks the initialization run, which a VHDL postponed
    // process makes with the other processes (IEEE 1076-2008 14.7.5.2).
    void queue_at(ProcessId id, SimulationTick time, bool initial = false);
    // Resume a process after a timed wait. A VHDL process resumes after the
    // driver updates of that time, with the processes they wake.
    void queue_timeout(ProcessState& process, SimulationTick time);

    void queue_next_delta(
        ProcessId id,
        SignalChangeOrigin origin = { });
    // Wake the readers of a changed container object without a writable
    // signal alias: its dynamic fanout, its change signal and the observer.
    void publish_container_object_change(
        ContainerObjectId object, SignalChangeOrigin origin);
    void queue_zero_delay_resume(ProcessState& process);

    void queue_static_next_delta(
        ProcessId id,
        SignalChangeOrigin origin = { });
    void queue_static_cohort_next_delta(std::size_t cohort);
    void rebuild_static_fanout();
    [[nodiscard]] std::span<const Fanout> static_fanout_for(
        SignalId signal) const noexcept;
    [[nodiscard]] std::span<const std::size_t>
    static_fanout_indices_for(SignalId signal, EdgeKind edge) const noexcept;
    [[nodiscard]] static StaticTransitionMatches decode_static_transition(
        Logic4 old_value, Logic4 new_value) noexcept;
    void notify_static_value_change(
        SignalId signal,
        StaticTransitionMatches transition,
        bool count_native_word_profile = false,
        SignalChangeOrigin origin = { });
    [[nodiscard]] std::uint64_t next_static_fanout_visit();

    void queue_current(ProcessId id);

    void queue_active_current(ProcessId id);

    /// queue_active_current at an explicit stable order.
    void queue_active_current_at(ProcessId id, StableOrder order);

    void queue_static_active_current(ProcessId id);

    void register_static_sensitivity_cohort(ProcessId id);

    [[nodiscard]] bool handle_fork_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        const Operation& operation);
    // End a disabled block for `owner`: kill its fork children spawned in
    // [begin, end) and resume it at `end` if it executes inside the block.
    void disable_process_interval(
        ProcessState& process,
        ProcessState& owner,
        InstructionIndex begin,
        InstructionIndex end,
        bool include_end);
    void disable_named_scope(
        ProcessState& process, const std::vector<std::string>& scopes);
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

    void trigger_event(
        SignalId event,
        SignalChangeOrigin origin = { });

    void set_event_identity(
        SignalId signal, std::optional<SignalId> identity);

    /// A named event's triggered state (IEEE 1800-2023 15.5.3): its
    /// synchronization object triggered in the current time step.
    [[nodiscard]] bool event_triggered_now(SignalId event) const;

    [[nodiscard]] std::uint64_t invalidate_event(
        const SignalId event);

    void stamp_signal_event(
        SignalId signal,
        std::uint64_t generic_delta,
        SignalChangeOrigin origin);
    void record_signal_event(
        SignalId signal,
        std::uint64_t generic_delta,
        SignalChangeOrigin origin);
    void capture_sampled_history_clock(
        SignalId clock,
        std::uint64_t generic_delta,
        SignalChangeOrigin origin);

    void cancel_event(const SignalId event);

    void notify_event(
        const SignalId event,
        const SimulationTick delay,
        const EventNotificationKind kind,
        const StableOrder order,
        SignalChangeOrigin origin = { });

    void notify_execution_point(
        ProcessState& process,
        const InstructionIndex instruction,
        const ExecutionPointKind kind,
        const SourceLocation& source,
        std::string_view scope = { });
    void notify_execution_point(
        ProcessId process,
        ProcessId design_process,
        ProcessProgramView program,
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
    /// $monitor, $fmonitor, $strobe and $fstrobe (IEEE 1800-2023 21.2.2,
    /// 21.2.3, 21.3.2) for `process`, with its file handle resolved.
    void install_monitor(ProcessId process, const MonitorInstall& registration,
        std::optional<FileHandle> file_handle);

    void set_monitor_enabled(const bool enabled);
    void set_time_format(
        ProcessState& process,
        const TimeFormatControl& operation);
    /// $timeformat (IEEE 1800-2023 20.4.2) from its argument values;
    /// throws std::invalid_argument for arguments outside the profile.
    void apply_time_format(const PackedLogic4& units,
        const PackedLogic4& precision, const PackedLogic4& minimum_width,
        const std::string& suffix);

    [[nodiscard]] static std::uint64_t initial_random_state(
        const std::uint64_t seed,
        const ProcessId process) noexcept;

    [[nodiscard]] static std::uint32_t next_random(
        ProcessState& process) noexcept;
    /// The next draw of a process random stream held outside a
    /// ProcessState (a kernel fork child's).
    [[nodiscard]] static std::uint32_t next_random(
        std::uint64_t& state) noexcept;

    [[nodiscard]] static std::optional<std::uint32_t>
    known_random_bound(const PackedLogic4& value);

    [[nodiscard]] PackedLogic4 random_value(
        const ProcessId process_id,
        const RandomKind kind,
        const std::optional<PackedLogic4>& maximum,
        const std::optional<PackedLogic4>& minimum);
    /// random_value on the process stream `state`.
    [[nodiscard]] PackedLogic4 random_value(
        std::uint64_t& state,
        const RandomKind kind,
        const std::optional<PackedLogic4>& maximum,
        const std::optional<PackedLogic4>& minimum);

    void publish(
        SignalId signal_id, PackedLogic4 value,
        bool notify_fanout = true);
    void publish_normalized(
        SignalId signal_id, PackedLogic4 value,
        bool notify_fanout = true,
        SignalChangeOrigin origin = { });
    void publish_normalized_word(
        SignalId signal_id, Logic4Word value,
        bool notify_fanout = true,
        SignalChangeOrigin origin = { });
    [[nodiscard]] bool can_publish_native_word(
        SignalId signal_id, ProcessId process) noexcept;
    [[nodiscard]] bool native_word_publication_phase_eligible() noexcept;
    [[nodiscard]] bool native_signal_has_runtime_dependency(
        SignalId signal_id) const noexcept;
    void advance_direct_signal_read_capability_epoch() noexcept;
    void build_native_signal_dependency_masks() noexcept;
    void build_native_signal_publication_shape_certificate() noexcept;
    void build_direct_signal_read_capabilities() noexcept;
    [[nodiscard]] bool is_aggregate_signal_proxy(
        SignalId signal_id) const noexcept;
    [[nodiscard]] bool has_container_signal_alias(
        SignalId signal_id) const noexcept;
    [[nodiscard]] bool can_publish_native_word_prevalidated(
        SignalId signal_id, ProcessId process) noexcept;
    [[nodiscard]] bool can_publish_native_logic9_word_prevalidated(
        SignalId signal_id, ProcessId process) noexcept;
    [[nodiscard]] bool can_publish_blocking_word(SignalId signal_id) noexcept;
    void publish_native_word(
        SignalId signal_id,
        Logic4Word value,
        SignalChangeOrigin origin = { });
    // Requires canonical Logic9 codes; checked batch ingress normalizes first.
    void publish_native_logic9_word(
        SignalId signal_id,
        Logic9Word value,
        SignalChangeOrigin origin = { });
    void note_signal_transaction(
        SignalId signal_id,
        bool notify_fanout,
        SignalChangeOrigin origin = { });
    // A process queries the signal's transactions ('ACTIVE, 'LAST_ACTIVE or
    // a transaction wait): its commits stamp them.
    void observe_signal_transaction(SignalId signal);
    void register_signal_selection(
        SignalId signal_id, SignalSelection selection);
    [[nodiscard]] const SignalSelectionHistory* find_signal_selection_history(
        SignalId signal_id, SignalSelection selection) const noexcept;
    // A signal's or selected range's attributes in the current simulation
    // cycle (IEEE 1076-2019 16.2.4): 'EVENT, 'ACTIVE, 'LAST_VALUE,
    // 'LAST_EVENT and 'LAST_ACTIVE. A selection must have been registered;
    // otherwise std::invalid_argument.
    [[nodiscard]] const SignalSelectionHistory* selected_attribute_history(
        SignalId signal, const std::optional<SignalSelection>& selection,
        const char* attribute) const;
    [[nodiscard]] bool signal_event_attribute(
        SignalId signal, const std::optional<SignalSelection>& selection) const;
    [[nodiscard]] bool signal_active_attribute(
        SignalId signal, const std::optional<SignalSelection>& selection) const;
    [[nodiscard]] const PackedLogic4& signal_last_value_attribute(
        SignalId signal, const std::optional<SignalSelection>& selection);
    [[nodiscard]] SimulationTick signal_last_event_attribute(
        SignalId signal, const std::optional<SignalSelection>& selection) const;
    [[nodiscard]] SimulationTick signal_last_active_attribute(
        SignalId signal, const std::optional<SignalSelection>& selection);
    void stage_selected_signal_transaction(
        SignalId signal_id, std::size_t offset, std::size_t width);
    void stage_selected_signal_transaction_mask(
        SignalId signal_id, std::span<const std::uint64_t> mask_words);
    void note_selected_signal_transaction(SignalId signal_id);
    void clear_pending_selected_signal_transactions() noexcept;
    void update_selected_signal_value_history(
        SignalId signal_id, std::uint64_t generic_delta);
    void publish_value_change(
        SignalId signal_id,
        bool notify_fanout,
        SignalChangeOrigin origin = { },
        bool state_prepared = false);
    void refresh_direct_signal_planes(SignalId signal_id);
    void publish_container_signal_aliases(
        SignalId signal_id,
        SignalChangeOrigin origin = { },
        bool stored_changed = true);
    void publish_aggregate_leaf_driver_change(
        ProcessId process, SignalId signal_id);

    [[nodiscard]] PackedLogic4 apply_force(
        SignalId signal_id, PackedLogic4 value) const;

    // origin is the forcing process's, so the fanout follows its update
    // like a blocking assignment's (IEEE 1800-2023 10.6.2, 4.4).
    void force_slice(
        SignalId signal_id, PackedLogic4 value, std::size_t offset,
        SignalChangeOrigin origin = { }, bool port_segment = false);

    void release_slice(
        SignalId signal_id, std::size_t offset, std::size_t width,
        SignalChangeOrigin origin = { }, bool port_segment = false);

    // Records a force in the port layers; true when a force by the net's own
    // name hides it, so it takes effect only once that force is released.
    [[nodiscard]] bool record_port_force_layer(
        SignalId signal_id, const PackedLogic4& value, std::size_t offset,
        bool port_segment);

    // Drops a released force from the port layers; true when the release is
    // fully handled (hidden, or replaced by the remaining inner force).
    [[nodiscard]] bool release_port_force_layer(
        SignalId signal_id, std::size_t offset, std::size_t width,
        bool port_segment, SignalChangeOrigin origin);

    [[nodiscard]] PackedLogic4 apply_driver_force(
        SignalId signal_id,
        ProcessId process,
        PackedLogic4 value,
        const ForcedDriverMapView* override_maps = nullptr) const;

    [[nodiscard]] Logic4 driver_force_logic4_at(
        SignalId signal_id, ProcessId process,
        const PackedLogic4& value, std::size_t bit) const;

    void force_driver_slice(
        ProcessId process, SignalId signal_id,
        PackedLogic4 value, std::size_t offset);

    void release_driver_slice(
        ProcessId process, SignalId signal_id,
        std::size_t offset, std::size_t width);

    // A process's force or release: immediate for SystemVerilog, scheduled
    // for the update phase for VHDL (IEEE 1076-2019 10.5.2.1, 14.7.5.3).
    void force_slice_from_process(ProcessId process, SignalId signal_id,
        PackedLogic4 value, std::size_t offset, bool driving_value,
        bool port_segment = false);
    void release_slice_from_process(ProcessId process, SignalId signal_id,
        std::size_t offset, std::size_t width, bool driving_value,
        bool port_segment = false);

    void commit(
        SignalId signal_id,
        PackedLogic4 value,
        SignalChangeOrigin origin = { });

    void initialize_package_static_signal(
        SignalId signal_id,
        PackedLogic4 value);
    void initialize_package_static_string_object(
        StringObjectId object_id,
        std::string_view value,
        std::optional<ProcessId> process = std::nullopt);
    void initialize_package_static_container_object(
        ContainerObjectId object_id,
        const ContainerValue& value,
        std::optional<ProcessId> process = std::nullopt);

    void commit_direct_single_driver(
        SignalId signal_id, PackedLogic4 value);

    void invalidate_switch_components();

    void build_switch_components();

    void mark_switch_network_dirty(
        SignalId signal_id, bool non_switch_update = false);

    void refresh_switch_network();

    void commit_resolved(
        SignalId signal_id,
        PackedLogic4 value,
        SignalChangeOrigin origin = { });

    [[nodiscard]] PackedLogic4 initial_driver_value(
        const SignalId signal_id) const;

    void require_writable_aggregate_signal(SignalId signal_id) const;
    void promote_container_alias_authority(SignalId signal_id);
    void promote_all_container_alias_authorities();
    [[nodiscard]] bool container_alias_authority_active(
        ContainerObjectId object) const noexcept;
    void begin_container_alias_write_batch(
        ContainerObjectId object,
        bool has_driver,
        bool phasewise_storage = false);
    [[nodiscard]] std::vector<ContainerAliasLeafObserver>
    take_container_alias_write_batch(
        ContainerObjectId object, bool& changed);
    void finish_container_alias_write_batch(
        ContainerObjectId object,
        std::vector<ContainerAliasLeafObserver> leaves,
        bool changed);
    void capture_container_alias_write_batch(ContainerObjectId object);
    [[nodiscard]] ContainerAliasLeafObserver*
    container_alias_leaf_observer(SignalId signal_id) noexcept;
    void begin_aggregate_signal_batch(
        SignalId signal_id, SignalChangeOrigin origin = { });
    void finish_aggregate_signal_batch(
        SignalId signal_id, bool defer_object_hook = false);
    void note_aggregate_leaf_current_change(SignalId signal_id);
    void note_aggregate_leaf_stored_change(SignalId signal_id);
    [[nodiscard]] const PackedLogic4& aggregate_signal_current_value(
        SignalId signal_id);
    [[nodiscard]] const PackedLogic4& aggregate_signal_last_value(
        SignalId signal_id);
    void publish_aggregate_leaf_value_change(
        SignalId signal_id, bool notify_fanout,
        SignalChangeOrigin origin);
    void publish_aggregate_signal_value_change(
        SignalId signal_id, bool notify_fanout,
        SignalChangeOrigin origin,
        std::exception_ptr* observer_error = nullptr,
        bool event_prepared = false);
    [[nodiscard]] const PackedLogic4& logical_signal_value(
        SignalId signal_id);
    [[nodiscard]] const PackedLogic4& logical_signal_last_value(
        SignalId signal_id);
    PackedLogic4& driver_slot(
        const ProcessId process,
        const SignalId signal_id);

    [[nodiscard]] DriverRecord* direct_single_driver_record(
        SignalId signal_id) noexcept;

    [[nodiscard]] const DriverRecord* direct_single_driver_record(
        SignalId signal_id) const noexcept;

    void refresh_direct_single_driver_route(SignalId signal_id);

    void revoke_stable_writer_shadow_for_container_alias(
        SignalId signal_id);

    [[nodiscard]] PackedLogic4 resolved_driver_value(
        const SignalId signal_id) const;
    // Whether a net's value is resolved over a bidirectional switch network.
    [[nodiscard]] bool switch_network_member(SignalId signal_id) const;
    // The strengths reaching one bit of a switch network: the strongest 0
    // and 1 components of all contributions and of the known-valued ones,
    // and whether an unknown value contributes (IEEE 1800-2017 28.12).
    struct SwitchBitStrength {
        DriveStrength total;
        DriveStrength definite;
        bool ambiguous { };

        [[nodiscard]] Logic4 value() const noexcept
        {
            if (total.zero == StrengthRank::highz
                && total.one == StrengthRank::highz) {
                return Logic4::z;
            }
            // A known value wins when it is stronger than every opposing
            // contribution, including the opposing part of unknown ones.
            if (definite.zero > total.one) {
                return Logic4::zero;
            }
            if (definite.one > total.zero) {
                return Logic4::one;
            }
            return Logic4::x;
        }
    };
    [[nodiscard]] SwitchBitStrength switch_network_bit_strength(
        SignalId signal_id, std::size_t target_bit) const;
    // `%v` text of a net: each bit's strength and value, most significant
    // bit first (IEEE 1800-2017 21.2.1.5).
    [[nodiscard]] std::string signal_strength_text(SignalId signal_id,
        std::uint32_t offset = 0U, std::uint32_t count = 0U) const;

    [[nodiscard]] PackedLogic4 resolved_local_driver_value(
        SignalId signal_id,
        const ForcedDriverMapView* override_maps = nullptr) const;
    [[nodiscard]] PackedLogic4 run_vhdl_resolver(SignalId signal_id);
    // Run a signal's conversion-driver programs and assign their results to
    // the stub drivers; true when the signal has any.
    bool run_vhdl_conversion_drivers(SignalId signal_id, bool initializing);
    // Set the driving-value mirror of an inout formal whose effective value
    // its resolver program converts from the actual: its drivers' value, or
    // its default without one. False when the mirror keeps its value.
    bool publish_vhdl_formal_driving_value(
        SignalId signal_id, bool initializing);
    // `@vhdl-disconnect:<signal>:<delay>`: a null transaction on the
    // calling process's driver. Returns false for other identities.
    bool try_vhdl_disconnect_call(
        ProcessId process, std::string_view identity);
    void schedule_vhdl_disconnect(
        ProcessId process, SignalId signal, SimulationTick delay);
    void drain_vhdl_resolutions();
    // Queue a port conversion whose source was active; the conversion runs
    // in this cycle's update (IEEE 1076-2008 14.7.3.3), so an update commit
    // is scheduled when none will drain it.
    void queue_vhdl_conversion(SignalId target);
    void initialize_vhdl_resolutions();

    [[nodiscard]] DriveStrength resolved_signal_strength(
        SignalId signal_id) const;

    [[nodiscard]] bool switch_process(ProcessId process) const;

    void reset_switch_drivers(std::size_t component);

    PackedLogic4& external_driver_slot(
        const SignalId signal_id);

    void register_driver(
        const ProcessId process,
        const SignalId signal_id,
        std::span<const Process::DriverRegion> regions,
        DriveStrength strength,
        std::shared_ptr<const std::vector<Process::DriverRegion>> scalar_regions,
        bool delayed_writer = false);

    void set_driver(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value);

    void commit_driver(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value,
        std::optional<SignalChangeOrigin> origin = std::nullopt,
        bool route_module_path = true);
    void note_unresolved_update_owner(
        SignalId signal,
        std::optional<ProcessId> process,
        bool whole_update);
    void clear_unresolved_update_owner_provenance() noexcept;
    [[nodiscard]] PackedLogic4 current_driver_value(
        const ProcessId process,
        const SignalId signal_id) const;

    [[nodiscard]] PackedLogic4 underlying_driver_value(
        ProcessId process, SignalId signal_id) const;

    void commit_slice(
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset,
        SignalChangeOrigin origin = { });

    void commit_driver_slice(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value,
        const std::size_t offset,
        std::optional<SignalChangeOrigin> origin = std::nullopt,
        bool route_module_path = true);

    [[nodiscard]] SignalChangeOrigin capture_signal_change_origin(
        ProcessId process,
        SignalUpdateDomain update_domain = SignalUpdateDomain::generic) const;

    /// `order` places the update among the region's others (by default the
    /// process's own stable order; a kernel fork child has its own).
    void schedule_systemverilog_update(
        ProcessId process,
        SignalId signal,
        PackedLogic4 value,
        std::optional<std::size_t> offset,
        SignalUpdateDomain update_domain,
        SimulationTick delay = 0,
        std::optional<StableOrder> order = std::nullopt);

    void schedule_update_commit();
    void block_native_logic9_update_before_generic(SignalId signal);

    void stage_update_unrouted(
        std::optional<ProcessId> driver,
        SignalId signal,
        PackedLogic4 value,
        std::optional<std::size_t> offset,
        SignalChangeOrigin origin = { });

    [[nodiscard]] bool route_module_path_update(
        ProcessId driver,
        SignalId signal,
        const PackedLogic4& value,
        std::optional<std::size_t> offset,
        const TransitionDelays* intrinsic_delays = nullptr,
        SimulationTick fixed_delay = 0,
        std::optional<SignalChangeOrigin> origin = std::nullopt);

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
        PackedLogic4 staged_value,
        SignalUpdateDomain domain = SignalUpdateDomain::generic,
        std::optional<StableOrder> order = std::nullopt);

    void stage_update(
        const SignalId signal_id,
        PackedLogic4 staged_value);

    void stage_update(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 staged_value,
        SignalUpdateDomain domain = SignalUpdateDomain::generic,
        std::optional<StableOrder> order = std::nullopt);

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
        const std::size_t offset,
        SignalUpdateDomain domain = SignalUpdateDomain::generic,
        std::optional<StableOrder> order = std::nullopt);

    void stage_update_words(
        ProcessId process,
        std::span<const ProcessUpdateWord> updates);
    void stage_validated_update_words(
        ProcessId process,
        std::span<const ProcessUpdateWord> updates);
    [[nodiscard]] bool stage_validated_logic9_update_batch(
        const ProcessLogic9UpdateBatch& batch);
    [[nodiscard]] bool stage_validated_logic9_update_batches(
        std::span<const ProcessLogic9UpdateBatch> batches);

    // An assignment with an intra-assignment or net delay (IEEE 1800-2023
    // 10.3.3, 10.4.1-2): the update matures `delay` later, in its domain's
    // region, or in the generic update phase.
    void schedule_after_update(ProcessId driver, SignalId signal,
        PackedLogic4 value, std::optional<std::size_t> offset,
        SignalUpdateDomain domain, SimulationTick delay);
    // A WriteDelayed's rise, fall and turn-off delays from its delay
    // expressions; `read` gives a delay source register's value.
    [[nodiscard]] static TransitionDelays write_delayed_delays(
        const WriteDelayed& operation,
        const std::function<const PackedLogic4&(RegisterId)>& read);
    // Schedules a WriteDelayed's update (inertial for a net delay, 28.16).
    void schedule_write_delayed(ProcessId driver,
        const WriteDelayed& operation, PackedLogic4 value,
        const TransitionDelays& delays);
    void schedule_inertial(
        const ProcessId process,
        const SignalId signal,
        PackedLogic4 value,
        const std::optional<std::size_t> offset,
        const TransitionDelays& delays,
        SignalUpdateDomain domain = SignalUpdateDomain::generic);

    void schedule_projected_scalar_waveform(
        const ProcessId process,
        const SignalId signal,
        const std::uint32_t offset,
        const std::vector<
            std::pair<PackedLogic4, SimulationTick>>& elements,
        const SimulationTick rejection,
        const ProjectedDelayMode mode,
        const std::vector<bool>* retained_old = nullptr);

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

/// The constraints with each calling-scope array they name replaced by its
/// elements, read from the container register it names; no value when no
/// constraint names one (IEEE 1800-2017 18.7).
[[nodiscard]] std::optional<std::vector<SystemVerilogConstraintTemplate>>
bind_inline_constraint_containers(
    std::span<const SystemVerilogConstraintTemplate> constraints,
    const std::function<ContainerValue(ContainerRegisterId)>& read);

} // namespace fsim::runtime::simir
