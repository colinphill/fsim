// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_coverage.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"
#include "fsim/runtime/simir_region_graph.hpp"
#include "simir_a4_signal_state.hpp"
#include "fsim/runtime/output_format.hpp"
#include "simir_cohort_snapshot_pool.hpp"
#include "simir_driver_table.hpp"
#include "simir_signal_storage.hpp"
#include "simir_process_storage.hpp"
#include "simir_process_program.hpp"
#include <deque>

#include <algorithm>
#include <array>
#include <atomic>
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

void validate_container_value(const ContainerValue& value);

[[nodiscard]] std::string error_text(ProcessId process,
    InstructionIndex instruction,
    const std::string& message);

[[nodiscard]] std::optional<SignalId> output_signal(
    const Operation& operation);

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

[[nodiscard]] inline RegionFrontierValueKindV2 region_frontier_value_kind_v2(
    const ValueKind kind) noexcept
{
    switch (kind) {
    case ValueKind::logic4:
        return RegionFrontierValueKindV2::logic4;
    case ValueKind::logic9:
        return RegionFrontierValueKindV2::logic9;
    }
    return static_cast<RegionFrontierValueKindV2>(UINT32_MAX);
}

[[nodiscard]] inline std::uint64_t region_frontier_tail_mask_v2(
    const std::uint32_t width) noexcept
{
    const auto remainder = width % 64U;
    return remainder == 0U
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t { 1U } << remainder) - 1U;
}

[[nodiscard]] inline bool region_frontier_plane_words_canonical_v2(
    const RegionFrontierValueKindV2 kind,
    const std::uint32_t width,
    const std::uint32_t word_count,
    const std::array<std::span<const std::uint64_t>, 4U>& planes) noexcept
{
    const auto plane_count = region_frontier_required_plane_count_v2(kind);
    if (plane_count == 0U || width == 0U
        || word_count != (static_cast<std::uint64_t>(width) + 63U) / 64U) {
        return false;
    }
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        if ((plane < plane_count) != (planes[plane].size() == word_count)
            || (plane < plane_count && planes[plane].data() == nullptr)
            || (plane >= plane_count && !planes[plane].empty())) {
            return false;
        }
    }
    const auto tail_mask = region_frontier_tail_mask_v2(width);
    for (std::size_t word = 0U; word < word_count; ++word) {
        const auto mask = word + 1U == word_count
            ? tail_mask : std::numeric_limits<std::uint64_t>::max();
        for (std::size_t plane = 0U; plane < plane_count; ++plane) {
            if ((planes[plane][word] & ~mask) != 0U) {
                return false;
            }
        }
        if (kind == RegionFrontierValueKindV2::logic9
            && !region_frontier_logic9_word_is_canonical_v2(
                planes[0U][word], planes[1U][word], planes[2U][word],
                planes[3U][word])) {
            return false;
        }
    }
    return true;
}

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

        ReportCallback& operator=(ReportHook hook)
        {
            std::shared_ptr<const ReportHook> prepared;
            if (hook) {
                prepared = std::make_shared<ReportHook>(std::move(hook));
            }
            callback = std::move(prepared);
            return *this;
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

    static constexpr std::size_t no_systemverilog_update_slot
        = std::numeric_limits<std::size_t>::max();

    struct RegionPreparedOutputBatchState;

    struct SystemVerilogUpdateSlot {
        ProcessId process { };
        SignalId signal { };
        PackedLogic4 value;
        std::shared_ptr<RegionPreparedOutputBatchState>
            prepared_output_batch;
        std::size_t prepared_output_slot {
            no_systemverilog_update_slot };
        std::size_t prepared_output_dispatch_ordinal {
            no_systemverilog_update_slot };
        std::optional<std::size_t> offset;
        SignalChangeOrigin origin;
        std::size_t region_local_component {
            no_systemverilog_update_slot };
        std::uint64_t region_local_generation { };
        std::size_t next_free { no_systemverilog_update_slot };
        std::uint64_t generation { 1U };
        bool occupied { };
        bool reserved { };
        bool retired { };
        bool region_internal_output { };
        bool invalidate_local_wave_bank_on_commit { };
    };

    struct SystemVerilogUpdateToken {
        Impl* owner { };
        std::size_t slot { no_systemverilog_update_slot };
        std::uint64_t generation { };
    };

    struct RegionForwardingPrivateStageBatch;

    struct RegionForwardingPrivateOutputDispatchToken {
        RegionForwardingPrivateStageBatch* batch { };
        std::uint64_t ordinal { };
    };

    struct RegionForwardingPrivateStageOutput {
        ProcessId process { };
        SignalId signal { };
        PackedLogic4 value;
        SignalChangeOrigin origin;
        std::size_t component { no_systemverilog_update_slot };
        std::uint64_t runtime_generation { };
        bool pending { };
        bool role_deferred { };
    };

    struct RegionForwardingPrivateStageBatch final : SchedulerOrderedBatchTask {
        static constexpr std::uint64_t private_output_payload_flag
            = UINT64_C(1) << 63U;
        static constexpr std::uint64_t payload_ordinal_mask
            = ~private_output_payload_flag;

        Impl* owner { };
        std::size_t component { no_systemverilog_update_slot };
        std::uint64_t runtime_generation { };
        SimulationTick time { };
        std::vector<SystemVerilogUpdateToken> tokens;
        std::vector<RegionForwardingPrivateStageOutput> private_outputs;

        [[nodiscard]] SchedulerBatchResult execute(
            Scheduler& active_scheduler,
            std::span<const std::uint64_t> payloads) override;
        [[nodiscard]] ::fsim::runtime::detail::SchedulerTaskDescriptor
        make_fallback_descriptor(std::uint64_t payload) noexcept override;
    };

    struct RegionForwardingAppliedRoleCommitMetadata {
        std::size_t output_index { };
        SignalId signal { };
        ProcessId owner { };
        SimulationTick callback_time { };
        std::uint64_t callback_delta { };
        std::uint64_t callback_systemverilog_round { };
        std::uint64_t callback_order { };
        SignalChangeOrigin origin;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            expected_signal_event;
        SignalEventSchedulingStamp expected_event_stamp;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            expected_transaction;
        std::uint64_t expected_value_revision { };
    };

    static_assert(std::is_nothrow_move_constructible_v<
        AuthoritativeSignalPlanes::PreparedMutation>);
    static_assert(std::is_nothrow_move_constructible_v<
        RegionForwardingAppliedRoleCommitMetadata>);

    static_assert(std::is_trivially_copyable_v<SystemVerilogUpdateToken>);
    static_assert(sizeof(SystemVerilogUpdateToken)
        <= ::fsim::runtime::detail::scheduler_task_payload_size);
    static_assert(std::is_trivially_copyable_v<
        RegionForwardingPrivateOutputDispatchToken>);
    static_assert(sizeof(RegionForwardingPrivateOutputDispatchToken)
        <= ::fsim::runtime::detail::scheduler_task_payload_size);
    static_assert(std::is_nothrow_move_assignable_v<PackedLogic4>);
    static_assert(std::is_nothrow_move_constructible_v<
        SystemVerilogUpdateSlot>);

    struct RegionPreparedOutputSlotStorage {
        std::array<std::uint64_t, 2U> owner_mask { };
        std::array<std::uint64_t, 2U> old_stored { };
        std::array<std::uint64_t, 2U> old_owner { };
        std::array<std::uint64_t, 2U> next_last { };
        std::uint8_t changed { };
        std::uint8_t value_ready { };
        std::uint8_t transaction_ready { };
        AuthoritativeSignalPlanes::PreparedMutation visible_mutation;
    };

    struct RegionDirectReadyInputSource {
        SignalId signal { };
        RegisterId register_id { };
        std::uint32_t width { };
        std::size_t value_index { };
        bool internal { };
    };

    struct RegionFrontierSensitivityRange {
        std::uint32_t offset { };
        std::uint32_t width { };
    };

    struct RegionPreparedSuccessorReaderBinding {
        ProcessId process { };
        // Scheduler readiness uses activation-kernel member order.
        std::size_t readiness_member { };
        std::size_t readiness_word { };
        std::size_t queued_mask_word { };
        std::uint64_t readiness_bit { };
        // The A4 fanout mask follows certified graph-component member order.
        std::size_t authoritative_member { };
        std::size_t authoritative_readiness_word { };
        std::uint64_t authoritative_readiness_bit { };
        std::uint64_t static_trigger_mask { };
        std::size_t sensitivity_range_offset { };
        std::size_t sensitivity_range_count { };
        std::uint64_t sensitivity_range_generation { };
    };

    struct RegionPreparedSuccessorSignalMap {
        std::size_t reader_offset { };
        std::size_t reader_count { };
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        std::uint64_t expected_mask { };
    };

    /// Per-output copy of the immutable snapshot map. It stores offsets rather
    /// than pointers so stale tickets cannot dereference a replaced snapshot.
    struct RegionPreparedSuccessorSlotMap {
        SignalId signal { };
        std::size_t reader_offset { };
        std::size_t reader_count { };
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        std::uint64_t expected_mask { };
    };

    /// Private output storage stays owned by the native workspace while the
    /// ordered update tickets borrow it. Tickets retain only this allocation
    /// and an ordinal; component state is re-looked-up after checking both
    /// runtime and component generations at dispatch.
    struct RegionPreparedOutputBatchState
        : SchedulerBatchTask
        , std::enable_shared_from_this<RegionPreparedOutputBatchState> {
        explicit RegionPreparedOutputBatchState(
            const RegionConeActivationKernel& kernel)
            : slot_storage(kernel.internal_signals.size())
            , descriptors(kernel.internal_signals.size())
            , successor_masks(kernel.internal_signals.size())
            , expected_successor_masks(kernel.internal_signals.size())
            , successor_mapping_by_slot(kernel.internal_signals.size())
        {
            if (descriptors.size()
                > std::numeric_limits<std::uint32_t>::max()) {
                throw std::length_error {
                    "prepared output batch exceeds its ABI slot count"
                };
            }
            prepare_direct_ready_sources(kernel);
            current_internal_values.reserve(
                kernel.internal_signals.size());
            ordered_prefix.reserve(kernel.outputs.size());
            ticket_slot_indices.reserve(kernel.outputs.size());
            dispatch_tokens.reserve(kernel.outputs.size());
            group_batch_members.reserve(kernel.outputs.size());
            ticket_pending.resize(kernel.internal_signals.size());
            dispatch_member_pending.resize(kernel.outputs.size());
            for (auto& slot : slot_storage) {
                slot.visible_mutation.words.reserve(1U);
            }
            abi_batch.abi_version
                = kRegionPreparedOutputBatchAbiVersionV1;
            abi_batch.struct_size = sizeof(RegionPreparedOutputBatchV1);
            abi_batch.slot_count = static_cast<std::uint32_t>(
                descriptors.size());
            abi_batch.slots = descriptors.data();
            successor_abi.abi_version
                = kRegionPreparedOutputSuccessorMasksAbiVersionV1;
            successor_abi.struct_size = sizeof(successor_abi);
            successor_abi.slot_count = static_cast<std::uint32_t>(
                successor_masks.size());
            successor_abi.member_masks = successor_masks.data();
        }

        void retire_ticket(
            const std::size_t slot, const bool cancel) noexcept
        {
            if (slot >= ticket_pending.size()
                || ticket_pending[slot] == 0U) {
                return;
            }
            ticket_pending[slot] = 0U;
            if (pending_tickets != 0U) {
                --pending_tickets;
            }
            cancelled = cancelled || cancel;
            retire_if_idle();
        }

        void retire_dispatch_member(
            const std::size_t ordinal, const bool cancel) noexcept
        {
            if (ordinal >= dispatch_member_pending.size()
                || dispatch_member_pending[ordinal] == 0U) {
                return;
            }
            dispatch_member_pending[ordinal] = 0U;
            if (pending_dispatch_members != 0U) {
                --pending_dispatch_members;
            }
            cancelled = cancelled || cancel;
            retire_if_idle();
        }

        void retire_if_idle() noexcept
        {
            if (pending_tickets == 0U && pending_dispatch_members == 0U) {
                active = false;
                group_ticket_active = false;
            }
        }

        void cancel_unattached() noexcept
        {
            cancelled = true;
            retire_if_idle();
        }

        void cancel_prepared_outputs() noexcept
        {
            cancelled = true;
            std::ranges::fill(ticket_pending, 0U);
            pending_tickets = 0U;
            if (!group_ticket_active) {
                retire_if_idle();
            }
        }

        void prepare_direct_ready_sources(
            const RegionConeActivationKernel& kernel)
        {
            const auto maximum_slot_count
                = static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max());
            if (kernel.internal_signals.size() > maximum_slot_count
                || kernel.inputs.size()
                    > maximum_slot_count - kernel.internal_signals.size()) {
                return;
            }
            const auto slot_count
                = kernel.inputs.size() + kernel.internal_signals.size();
            std::size_t boundary_index { };
            try {
                direct_input_sources.reserve(slot_count);
                direct_input_slots.resize(slot_count);
                for (const auto& input : kernel.inputs) {
                    std::size_t value_index { };
                    if (input.internal) {
                        const auto internal = std::ranges::lower_bound(
                            kernel.internal_signals, input.signal);
                        if (internal == kernel.internal_signals.end()
                            || *internal != input.signal) {
                            direct_input_sources.clear();
                            direct_input_slots.clear();
                            return;
                        }
                        value_index = static_cast<std::size_t>(
                            internal - kernel.internal_signals.begin());
                    } else {
                        value_index = boundary_index++;
                    }
                    direct_input_sources.push_back({ input.signal,
                        input.value_register, input.width, value_index,
                        input.internal });
                }
                std::ranges::sort(direct_input_sources,
                    std::ranges::less { },
                    &RegionDirectReadyInputSource::register_id);

                for (std::size_t index = 0U;
                     index < kernel.internal_signals.size(); ++index) {
                    const auto signal = kernel.internal_signals[index];
                    const auto output = std::ranges::find(kernel.outputs,
                        signal, &RegionConeOutputBinding::signal);
                    const auto register_id
                        = static_cast<std::uint64_t>(
                            kernel.program.register_count) + index;
                    if (output == kernel.outputs.end()
                        || register_id
                            > std::numeric_limits<RegisterId>::max()) {
                        direct_input_sources.clear();
                        direct_input_slots.clear();
                        return;
                    }
                    direct_input_sources.push_back({ signal,
                        static_cast<RegisterId>(register_id), output->width,
                        index, true });
                }
                direct_ready_mapping_valid
                    = direct_input_sources.size() == slot_count;
            } catch (const std::bad_alloc&) {
                direct_input_sources.clear();
                direct_input_slots.clear();
                direct_ready_mapping_valid = false;
                return;
            } catch (const std::length_error&) {
                direct_input_sources.clear();
                direct_input_slots.clear();
                direct_ready_mapping_valid = false;
                return;
            }
            if (direct_ready_mapping_valid) {
                direct_ready_window.abi_version
                    = kRegionDirectReadyWindowAbiVersionV1;
                direct_ready_window.struct_size
                    = sizeof(RegionDirectReadyWindowV1);
                direct_ready_window.input_slot_count
                    = static_cast<std::uint32_t>(slot_count);
                direct_ready_window.input_slots
                    = direct_input_slots.data();
                direct_ready_member_count = kernel.members.size();
                direct_boundary_input_count = boundary_index;
                direct_internal_count = kernel.internal_signals.size();
            }
        }

        [[nodiscard]] SchedulerBatchResult execute(
            Scheduler& scheduler,
            std::span<const std::uint64_t> payloads) override;

        Impl* owner { };
        RegionPreparedOutputBatchV1 abi_batch;
        RegionPreparedOutputSuccessorMasksV1 successor_abi;
        RegionDirectReadyWindowV1 direct_ready_window;
        std::vector<RegionPreparedOutputSlotStorage> slot_storage;
        std::vector<RegionPreparedOutputSlotV1> descriptors;
        std::vector<std::uint64_t> successor_masks;
        std::vector<std::uint64_t> expected_successor_masks;
        std::vector<RegionPreparedSuccessorSlotMap>
            successor_mapping_by_slot;
        std::vector<RegionDirectReadyInputSource> direct_input_sources;
        std::vector<RegionDirectReadyInputSlotV1> direct_input_slots;
        std::size_t direct_ready_member_count { };
        std::size_t direct_boundary_input_count { };
        std::size_t direct_internal_count { };
        std::vector<PackedLogic4> current_internal_values;
        std::vector<RegionConeOutputBinding> ordered_prefix;
        std::vector<std::size_t> ticket_slot_indices;
        std::vector<std::uint8_t> ticket_pending;
        std::vector<SystemVerilogUpdateToken> dispatch_tokens;
        std::vector<std::uint8_t> dispatch_member_pending;
        std::vector<Scheduler::SystemVerilogGroupBatchMember>
            group_batch_members;
        std::size_t component { no_systemverilog_update_slot };
        std::uint64_t runtime_generation { };
        std::uint64_t component_generation { };
        std::uint64_t expected_value_revision { };
        std::size_t pending_tickets { };
        std::size_t pending_dispatch_members { };
        std::size_t sealed_tickets { };
        SchedulerBatchGroupKey dispatch_group_key;
        std::uint64_t next_dispatch_group { 1U };
        bool group_ticket_active { };
        bool direct_ready_mapping_valid { };
        bool successor_mapping_valid { };
        bool successor_masks_verified { };
        bool active { };
        bool cancelled { };
    };

    struct RegionKernelNativeWorkspace {
        explicit RegionKernelNativeWorkspace(
            const RegionConeActivationKernel& kernel)
            : activation(kernel)
            , prepared_output_batch(
                  std::make_shared<RegionPreparedOutputBatchState>(kernel))
        {
            prefix.tasks.reserve(kernel.members.size());
            boundary_inputs.reserve(kernel.inputs.size());
            internal_seeds.reserve(kernel.internal_signals.size());
            authoritative_logic4_inputs.reserve(kernel.inputs.size());
            authoritative_input_planes.reserve(kernel.inputs.size());
            reserved_update_slots.reserve(kernel.outputs.size());
            update_tokens.reserve(kernel.outputs.size());
            stable_orders.reserve(kernel.outputs.size());
            tasks.reserve(kernel.outputs.size());
            completion_storage_identities.reserve(kernel.members.size());
            readiness_mask.resize(
                (kernel.members.size() + 63U) / 64U, 0U);
        }

        RegionKernelActivationState activation;
        std::shared_ptr<RegionPreparedOutputBatchState>
            prepared_output_batch;
        RegionKernelSchedulerPrefix prefix;
        std::vector<PackedLogic4> boundary_inputs;
        std::vector<RegionKernelInternalSeed> internal_seeds;
        std::vector<RegionKernelLogic4InputPlane>
            authoritative_logic4_inputs;
        std::vector<RegionKernelInputPlane> authoritative_input_planes;
        std::vector<std::size_t> reserved_update_slots;
        std::vector<SystemVerilogUpdateToken> update_tokens;
        std::vector<StableOrder> stable_orders;
        std::vector<::fsim::runtime::detail::SchedulerTaskDescriptor> tasks;
        std::vector<const void*> completion_storage_identities;
        std::vector<std::uint64_t> readiness_mask;
    };

    struct RegionKernelGenericWorkspace {
        struct FinalDebugState {
            ProcessId process { };
            SourceLocation source;
            std::string scope;
        };

        explicit RegionKernelGenericWorkspace(
            const RegionConeActivationKernel& kernel)
            : activation(kernel)
        {
            prefix.tasks.reserve(kernel.members.size());
            boundary_inputs.reserve(kernel.inputs.size());
            internal_seeds.reserve(kernel.internal_signals.size());
            prepared_processes.reserve(kernel.members.size());
            completion_storage_identities.reserve(kernel.members.size());
            final_debug_states.reserve(kernel.members.size());
            projected_values.resize(kernel.outputs.size());
            for (auto& waveform : projected_values) {
                waveform.reserve(1U);
            }
        }

        RegionKernelActivationState activation;
        RegionKernelSchedulerPrefix prefix;
        std::vector<PackedLogic4> boundary_inputs;
        std::vector<RegionKernelInternalSeed> internal_seeds;
        std::vector<ProcessId> prepared_processes;
        std::vector<const void*> completion_storage_identities;
        std::vector<FinalDebugState> final_debug_states;
        std::vector<std::vector<ProjectedWaveformValue>> projected_values;
    };

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
        // Non-null only when a SystemVerilog-2023 function activation
        // spawned background processes. The activation owns one context;
        // every spawned process shares it until the final child exits.
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
        std::optional<ProcessId> fork_parent;
        std::optional<InstructionIndex> fork_site;
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
        // True only when the immediate executor opted in, or a deferred
        // contract's expectation was confirmed against its installed executor.
        bool region_kernel_equivalence_confirmed { };
        // This executor opt-in is cached at installation. It is paired with
        // the RegionConeKernelMember's static definite-definition proof.
        bool region_kernel_completion_has_no_persistent_registers { };
        // Set only after native preflight confirmed the executor was parked
        // at its canonical static WaitSensitivity/Jump boundary. Ordinary
        // executor resume clears it before touching that frame.
        bool region_kernel_completion_boundary_validated { };
        std::uint64_t static_trigger_mask { Process::full_static_trigger_mask };
        SchedulerPhase execution_phase { SchedulerPhase::active };
        bool queued { };
        bool waiting_on_static { };
        bool waiting_on_signal { };
        std::uint32_t generation { };
        ProcessStatus status { ProcessStatus::running };
        ProcessStatus suspended_status { ProcessStatus::running };
        bool suspended { };
        bool suspended_wake { };
        bool halted { };
        bool killed { };
        bool has_callable_frame_push { };

        // Native frontier synchronization may reuse its exact final-debug
        // result while this process and runtime generation remain unchanged.
        // The saved address is never dereferenced. Check the monotonic runtime
        // generation first so a token from a replaced snapshot is never
        // compared after its immutable backend-owned target may have expired.
        const RegionConeFinalDebugState* frontier_debug_target { };
        std::uint64_t frontier_debug_runtime_generation { };

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

        [[nodiscard]] bool frontier_debug_matches(
            const RegionConeFinalDebugState& target,
            const std::uint64_t runtime_generation) const noexcept
        {
            return frontier_debug_runtime_generation == runtime_generation
                && frontier_debug_target == &target;
        }

        void clear_frontier_debug_token() noexcept
        {
            frontier_debug_target = nullptr;
            frontier_debug_runtime_generation = 0U;
        }

        void remember_frontier_debug(
            const RegionConeFinalDebugState& target,
            const std::uint64_t runtime_generation) noexcept
        {
            frontier_debug_target = &target;
            frontier_debug_runtime_generation = runtime_generation;
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

    struct FusedStaticCohortPlan {
        struct Output {
            enum class Route : std::uint8_t {
                disjoint_owner_group_logic4,
                projected_logic4,
                projected_logic9,
            };
            struct DisjointOwner {
                ProcessId process { };
                std::vector<std::uint64_t> mask;
                PackedLogic4 staged_value;
            };
            SignalId signal { };
            std::uint32_t width { };
            std::vector<std::uint64_t> owner_masks;
            std::vector<DisjointOwner> disjoint_owners;
            ProcessId original_owner { };
            Route route { Route::disjoint_owner_group_logic4 };
        };
        FusedStaticCohortCandidate candidate;
        std::vector<InstructionIndex> resume_instructions;
        std::uint64_t owner_stage_calls_total { };
        std::vector<Output> outputs;
        std::unique_ptr<FusedStaticCohortExecutor> executor;
        bool certified { };
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
        bool owned_composite { };
        std::optional<std::size_t> fused_cohort;

        PendingDriverCommit(std::optional<ProcessId> owner,
            PackedLogic4 pending_value, bool composite = false,
            std::optional<std::size_t> cohort = std::nullopt)
            : driver(owner)
            , value(std::move(pending_value))
            , owned_composite(composite)
            , fused_cohort(cohort)
        {
        }
    };

    struct OwnedDriverSpan {
        SignalId signal { std::numeric_limits<SignalId>::max() };
        std::uint32_t offset { };
        std::uint32_t width { };
    };

    struct OwnedDriverComposite {
        PackedLogic4 committed;
        PackedLogic4 phase;
        bool active { };
        bool phase_active { };
    };

    enum class OwnedDriverStage : std::uint8_t {
        unsupported,
        unchanged,
        changed,
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
    std::vector<std::optional<PackedLogic4>>
        aggregate_signal_current_projection;
    std::vector<std::optional<PackedLogic4>>
        aggregate_signal_stored_projection;
    std::vector<AggregateSignalBatch> aggregate_signal_batches;
    std::vector<std::uint64_t>
        aggregate_signal_current_projection_revisions;
    std::vector<std::uint64_t>
        aggregate_signal_stored_projection_revisions;
    std::vector<std::optional<std::pair<std::size_t, PackedLogic4>>>
        aggregate_signal_last_changes;
    std::vector<std::uint64_t>
        aggregate_signal_last_projection_revisions;
    std::filesystem::path file_root;
    // STD.ENV DIR_WORKINGDIR is simulator-local state. Never mutate the host
    // process working directory, which would race parallel simulation.
    std::filesystem::path vhdl_working_directory;
    std::vector<std::string> plusargs;
    SystemVerilogTimeFormat time_format;
    std::map<FileHandle, FileState> files;
    FileHandle next_file_handle { 1 };
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
    std::vector<DriverTable> driver_values;
    // Certified disjoint owners use this as their raw-driver authority.
    // Their DriverRecord values are restored on cold demotion.
    std::vector<OwnedDriverComposite> owned_driver_composites;
    std::vector<OwnedDriverSpan> owned_driver_spans;
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
    std::vector<std::uint64_t> signal_value_revisions;
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
    // Immutable elaborated topology; runtime observation only narrows admission.
    std::optional<RegionGraph> region_graph;
    // The elaboration proof seeds the first runtime snapshot; later snapshots
    // reuse the structurally matching inventory or replace it after writer
    // topology changes.
    std::shared_ptr<const SignalDriverInventory>
        elaborated_signal_driver_inventory;
    std::shared_ptr<const SignalDriverInventory>
        region_signal_driver_inventory;
    bool region_recertification_pending { };
    // Only prepare_region_authoritative_write may set a component bit. Every
    // other recertification cause sets the full-snapshot latch and clears all
    // component bits so an unclassified cause always fails closed.
    bool region_recertification_requires_snapshot { };
    std::vector<std::uint8_t>
        region_value_only_recertification_by_component;
    // Exact number of set component bits above. The common clear path runs
    // once per observed signal, so it must not scan/zero the whole vector
    // when no value-only candidate is pending.
    std::size_t region_value_only_recertification_nonempty_components { };
    struct RegionKernelBackendEntry {
        RegionConeActivationKernel kernel;
        std::string provider_identity;
        std::unique_ptr<RegionKernelBackend> executor;
        std::unique_ptr<RegionKernelNativeWorkspace> native_workspace;
        std::unique_ptr<RegionKernelGenericWorkspace> generic_workspace;
        std::atomic_flag in_use = ATOMIC_FLAG_INIT;

        [[nodiscard]] bool try_enter() noexcept
        {
            return !in_use.test_and_set(std::memory_order_acquire);
        }

        void leave() noexcept
        {
            in_use.clear(std::memory_order_release);
        }
    };
    struct VhdlProjectedReadinessTask final : SchedulerOrderedBatchTask {
        struct QueuedMember {
            ProcessId process { };
            SchedulerGenericKeyReceipt receipt;
            std::uint64_t static_trigger_mask { };
        };
        struct FallbackPayload {
            VhdlProjectedReadinessTask* task { };
            std::uint64_t payload { };
        };

        Impl* owner { };
        std::size_t component { no_systemverilog_update_slot };
        std::uint64_t runtime_generation { };
        std::shared_ptr<RegionKernelBackendEntry> backend;
        std::vector<QueuedMember> members;
        // Callback-time member-to-position scratch. It is sized during
        // snapshot preparation and never caches scheduler append order.
        std::vector<std::size_t> ticket_member_offsets;
        bool invalidated { };

        [[nodiscard]] std::optional<std::size_t> member_index(
            ProcessId process) const noexcept;
        void clear_member(ProcessId process) noexcept;
        [[nodiscard]] SchedulerBatchResult execute(
            Scheduler& active_scheduler,
            std::span<const std::uint64_t> payloads) override;
        [[nodiscard]] ::fsim::runtime::detail::SchedulerTaskDescriptor
        make_fallback_descriptor(std::uint64_t payload) noexcept override;
        static void dispatch_fallback(
            Scheduler&, const FallbackPayload&);
    };
    struct RegionConeForwardingBackendEntry {
        RegionConeForwardingKernel kernel;
        std::string provider_identity;
        std::unique_ptr<RegionConeForwardingBackend> executor;
        std::atomic_flag in_use = ATOMIC_FLAG_INIT;

        [[nodiscard]] bool try_enter() noexcept
        {
            return !in_use.test_and_set(std::memory_order_acquire);
        }

        void leave() noexcept
        {
            in_use.clear(std::memory_order_release);
        }
    };
    struct RegionFrontierBackendEntry {
        RegionConeActivationKernel kernel;
        std::string provider_identity;
        std::unique_ptr<RegionFrontierBackend> executor;
    };

    enum class RegionFrontierAliasMissReason : std::uint8_t {
        unprimed,
        context,
        collector,
        task_extent_only,
        task_extent_with_other,
        pointer,
        extent,
        alias_tag,
        count,
    };

    /// Exact byte interval included in the V2 nested alias-geometry proof.
    /// Pointer and extent are captured after binding; alias_tag mirrors the
    /// generated key/role encoding for a same-slot stored/owner exception.
    struct RegionFrontierAliasRange final {
        const void* address { };
        std::size_t bytes { };
        std::uint64_t alias_tag { };

        friend bool operator==(const RegionFrontierAliasRange&,
            const RegionFrontierAliasRange&) = default;
    };

    /// The cache is local to one persistent component runtime, but its key
    /// still names every object/code generation whose immutable range recipe
    /// can affect the nested geometry decision.
    struct RegionFrontierAliasCertificateContext final {
        const Impl* owner { };
        std::size_t component { no_systemverilog_update_slot };
        const RegionFrontierBackendEntry* backend { };
        const RegionFrontierBackend* executor { };
        const RegionFrontierLayoutV2* layout { };
        const RegionFrontierFrameV2* frame { };
        RegionFrontierStepEntryV2 checked_entry { };
        RegionFrontierStepEntryV2 trusted_entry { };
        std::uint64_t runtime_generation { };
        std::uint64_t frame_runtime_generation { };
        std::uint64_t bound_runtime_generation { };
        std::uint64_t certificate_generation { };
        std::uint64_t component_generation { };
        RegionFrontierExecutionModeV2 execution_mode {
            RegionFrontierExecutionModeV2::systemverilog_active
        };

        friend bool operator==(const RegionFrontierAliasCertificateContext&,
            const RegionFrontierAliasCertificateContext&) = default;
    };

    // The component's mutable V2 frame buffers share one aligned allocation.
    // Direct runtime fixtures can still use these vectors before preparation;
    // production preparation configures the bounded arena before the first
    // vector allocation, after which an unexpected growth fails closed.
    struct RegionFrontierWorkspaceAllocationState final {
        static constexpr std::size_t allocation_alignment
            = alignof(std::max_align_t);

        RegionFrontierWorkspaceAllocationState() = default;
        RegionFrontierWorkspaceAllocationState(
            const RegionFrontierWorkspaceAllocationState&) = delete;
        RegionFrontierWorkspaceAllocationState& operator=(
            const RegionFrontierWorkspaceAllocationState&) = delete;

        [[nodiscard]] bool configure(const std::size_t byte_count)
        {
            if (storage_ != nullptr || byte_count == 0U
                || live_fallback_allocations_ != 0U) {
                return false;
            }
            storage_ = std::shared_ptr<std::byte> {
                static_cast<std::byte*>(::operator new(byte_count)),
                [](std::byte* const pointer) noexcept {
                    ::operator delete(pointer);
                }
            };
            capacity_ = byte_count;
            return true;
        }

        [[nodiscard]] void* allocate(
            const std::size_t byte_count,
            const std::size_t alignment)
        {
            if (byte_count == 0U) {
                return nullptr;
            }
            if (alignment == 0U) {
                throw std::bad_alloc { };
            }
            if (!storage_) {
                void* const result = allocate_system(byte_count, alignment);
                if (live_fallback_allocations_
                    == std::numeric_limits<std::size_t>::max()) {
                    deallocate_system(result, alignment);
                    throw std::bad_alloc { };
                }
                ++live_fallback_allocations_;
                return result;
            }
            if (alignment > allocation_alignment || used_ > capacity_) {
                throw std::bad_alloc { };
            }
            const auto base = reinterpret_cast<std::uintptr_t>(storage_.get());
            if (used_ > std::numeric_limits<std::uintptr_t>::max() - base) {
                throw std::bad_alloc { };
            }
            const auto current = base + used_;
            const auto remainder = current % alignment;
            const auto padding = remainder == 0U
                ? 0U : alignment - remainder;
            if (padding > capacity_ - used_
                || byte_count > capacity_ - used_ - padding) {
                throw std::bad_alloc { };
            }
            const auto offset = used_ + padding;
            used_ = offset + byte_count;
            return storage_.get() + offset;
        }

        void deallocate(
            void* const pointer,
            const std::size_t alignment) noexcept
        {
            if (pointer == nullptr || owns(pointer, 1U)) {
                return;
            }
            if (live_fallback_allocations_ != 0U) {
                --live_fallback_allocations_;
            }
            deallocate_system(pointer, alignment);
        }

        [[nodiscard]] bool owns(
            const void* const pointer,
            const std::size_t byte_count) const noexcept
        {
            if (!storage_ || pointer == nullptr) {
                return false;
            }
            const auto begin = reinterpret_cast<std::uintptr_t>(storage_.get());
            const auto address = reinterpret_cast<std::uintptr_t>(pointer);
            if (address < begin || address - begin > capacity_) {
                return false;
            }
            return byte_count <= capacity_ - (address - begin);
        }

        [[nodiscard]] std::size_t capacity() const noexcept
        {
            return capacity_;
        }

        [[nodiscard]] const void* begin() const noexcept
        {
            return storage_.get();
        }

        /// Pins only the backing bytes, never this allocator's bump state.
        /// Versioned A4 blocks can outlive the runtime workspace after COW
        /// snapshots or read leases retain the slab.
        [[nodiscard]] std::shared_ptr<void> storage_lifetime() const noexcept
        {
            return storage_;
        }

        [[nodiscard]] std::size_t used() const noexcept
        {
            return used_;
        }

        [[nodiscard]] static void* allocate_system(
            const std::size_t byte_count,
            const std::size_t alignment)
        {
            if (alignment > allocation_alignment) {
                return ::operator new(byte_count,
                    std::align_val_t { alignment });
            }
            return ::operator new(byte_count);
        }

        static void deallocate_system(
            void* const pointer,
            const std::size_t alignment) noexcept
        {
            if (pointer == nullptr) {
                return;
            }
            if (alignment > allocation_alignment) {
                ::operator delete(pointer,
                    std::align_val_t { alignment });
                return;
            }
            ::operator delete(pointer);
        }

    private:
        std::shared_ptr<std::byte> storage_;
        std::size_t capacity_ { };
        std::size_t used_ { };
        std::size_t live_fallback_allocations_ { };
    };

    template <typename T>
    class RegionFrontierWorkspaceAllocator {
    public:
        using value_type = T;
        using propagate_on_container_copy_assignment = std::false_type;
        using propagate_on_container_move_assignment = std::false_type;
        using propagate_on_container_swap = std::false_type;
        using is_always_equal = std::false_type;

        RegionFrontierWorkspaceAllocator() noexcept = default;

        explicit RegionFrontierWorkspaceAllocator(
            RegionFrontierWorkspaceAllocationState& state) noexcept
            : state_(&state)
        {
        }

        template <typename U>
        RegionFrontierWorkspaceAllocator(
            const RegionFrontierWorkspaceAllocator<U>& other) noexcept
            : state_(other.state())
        {
        }

        [[nodiscard]] T* allocate(const std::size_t count)
        {
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
                throw std::bad_array_new_length { };
            }
            const auto byte_count = count * sizeof(T);
            if (state_ != nullptr) {
                return static_cast<T*>(state_->allocate(
                    byte_count, alignof(T)));
            }
            return static_cast<T*>(
                RegionFrontierWorkspaceAllocationState::allocate_system(
                    byte_count, alignof(T)));
        }

        void deallocate(T* const pointer, const std::size_t) noexcept
        {
            if (state_ != nullptr) {
                state_->deallocate(pointer, alignof(T));
            } else {
                RegionFrontierWorkspaceAllocationState::deallocate_system(
                    pointer, alignof(T));
            }
        }

        [[nodiscard]] RegionFrontierWorkspaceAllocator
        select_on_container_copy_construction() const noexcept
        {
            // A detached test snapshot must not retain the runtime's arena
            // owner pointer or outlive that component workspace.
            return { };
        }

        [[nodiscard]] RegionFrontierWorkspaceAllocationState* state()
            const noexcept
        {
            return state_;
        }

        template <typename U>
        [[nodiscard]] bool operator==(
            const RegionFrontierWorkspaceAllocator<U>& other) const noexcept
        {
            return state_ == other.state();
        }

        template <typename U>
        friend class RegionFrontierWorkspaceAllocator;

    private:
        RegionFrontierWorkspaceAllocationState* state_ { };
    };

    struct RegionFrontierComponentWorkspace {
        struct GenericQueuedMember {
            SchedulerGenericKeyReceipt receipt;
            std::uint64_t static_trigger_mask { };
        };
        struct GenericParkedExecutorBinding {
            // Checked against the live ProcessState before capability use. The
            // generation is advanced before any executor lifetime mutation, so
            // even allocator address reuse cannot revive a stale cross-cast.
            ProcessExecutor* executor { };
            const RegionKernelParkedExecutor* capability { };
            std::uint64_t lifecycle_generation { };
        };

        template <typename T>
        using Vector = std::vector<T, RegionFrontierWorkspaceAllocator<T>>;

        RegionFrontierComponentWorkspace()
            : ready_words { RegionFrontierWorkspaceAllocator<std::uint64_t> {
                  workspace_allocation } }
            , members { RegionFrontierWorkspaceAllocator<
                  RegionFrontierMemberV2> { workspace_allocation } }
            , scheduler_tasks { RegionFrontierWorkspaceAllocator<
                  RegionFrontierSchedulerTaskV2> { workspace_allocation } }
            , original_scheduler_tasks { RegionFrontierWorkspaceAllocator<
                  SchedulerBatchFrontierEntry> { workspace_allocation } }
            , planes { RegionFrontierWorkspaceAllocator<
                  RegionFrontierPlaneV2> { workspace_allocation } }
            , metadata { RegionFrontierWorkspaceAllocator<
                  RegionFrontierSignalMetadataV2> { workspace_allocation } }
            , fanout_edges { RegionFrontierWorkspaceAllocator<
                  RegionFrontierFanoutEdgeV2> { workspace_allocation } }
            , port_planes { RegionFrontierWorkspaceAllocator<
                  const RegionFrontierPlaneV2*> { workspace_allocation } }
            , pending_writes { RegionFrontierWorkspaceAllocator<
                  RegionFrontierPendingWriteV2> { workspace_allocation } }
            , staged_events { RegionFrontierWorkspaceAllocator<
                  RegionFrontierStagedEventV2> { workspace_allocation } }
            , committed_signals { RegionFrontierWorkspaceAllocator<
                  RegionFrontierCommittedSignalV2> { workspace_allocation } }
            , generic_snapshot_plane_offsets { RegionFrontierWorkspaceAllocator<
                  std::size_t> { workspace_allocation } }
            , generic_snapshot_words { RegionFrontierWorkspaceAllocator<
                  std::uint64_t> { workspace_allocation } }
            , generic_member_task_indices { RegionFrontierWorkspaceAllocator<
                  std::size_t> { workspace_allocation } }
            , generic_ticket_member_offsets { RegionFrontierWorkspaceAllocator<
                  std::size_t> { workspace_allocation } }
            , generic_queued_ready_words { RegionFrontierWorkspaceAllocator<
                  std::uint64_t> { workspace_allocation } }
            , generic_queued_members { RegionFrontierWorkspaceAllocator<
                  GenericQueuedMember> { workspace_allocation } }
            , generic_parked_executors { RegionFrontierWorkspaceAllocator<
                  GenericParkedExecutorBinding> { workspace_allocation } }
            , generic_prefix_processes { RegionFrontierWorkspaceAllocator<
                  ProcessId> { workspace_allocation } }
            , generic_prefix_members { RegionFrontierWorkspaceAllocator<
                  std::uint32_t> { workspace_allocation } }
            , writable_signals { RegionFrontierWorkspaceAllocator<
                  AuthoritativeSignalPlanes::FrontierWriteBinding> {
                  workspace_allocation } }
            , pending_plane_offsets { RegionFrontierWorkspaceAllocator<
                  std::size_t> { workspace_allocation } }
            , pending_plane_words { RegionFrontierWorkspaceAllocator<
                  std::uint64_t> { workspace_allocation } }
            , compact_members { RegionFrontierWorkspaceAllocator<
                  SystemVerilogCompactBatchMember> { workspace_allocation } }
            , issued_sequences { RegionFrontierWorkspaceAllocator<
                  std::uint64_t> { workspace_allocation } }
        {
        }

        RegionFrontierWorkspaceAllocationState workspace_allocation;
        Vector<std::uint64_t> ready_words;
        Vector<RegionFrontierMemberV2> members;
        Vector<RegionFrontierSchedulerTaskV2> scheduler_tasks;
        // Exact scheduler-owned payloads paired with the generated frame's
        // local member/event payloads. The borrowed scheduler span is never
        // retained after execute() returns.
        Vector<SchedulerBatchFrontierEntry> original_scheduler_tasks;
        Vector<RegionFrontierPlaneV2> planes;
        Vector<RegionFrontierSignalMetadataV2> metadata;
        Vector<RegionFrontierFanoutEdgeV2> fanout_edges;
        Vector<const RegionFrontierPlaneV2*> port_planes;
        Vector<RegionFrontierPendingWriteV2> pending_writes;
        Vector<RegionFrontierStagedEventV2> staged_events;
        Vector<RegionFrontierCommittedSignalV2> committed_signals;
        // Generic V2 evaluates from detached per-callback Logic4/Logic9
        // snapshots. These words never retain pins into A4 signal roles
        // across a callback.
        Vector<std::size_t> generic_snapshot_plane_offsets;
        Vector<std::uint64_t> generic_snapshot_words;
        Vector<std::size_t> generic_member_task_indices;
        // Authenticated scheduler-ticket position for every runtime member.
        // This is callback scratch; it is pre-sized with the component and
        // never escapes the borrowed Generic compact-ticket callback.
        Vector<std::size_t> generic_ticket_member_offsets;
        // Scheduler-owned queued work remains authoritative across generated
        // frame retries. The generated ready_words are per-entry scratch and
        // can be mutated before the host ACK.
        Vector<std::uint64_t> generic_queued_ready_words;
        Vector<GenericQueuedMember> generic_queued_members;
        Vector<GenericParkedExecutorBinding> generic_parked_executors;
        // The Generic path proves each executor is already parked and never
        // stages or commits its register frame. This differs from legacy
        // activation copyback, where mutable completion storage aliases must
        // be rejected before any executor frame is written.
        Vector<ProcessId> generic_prefix_processes;
        Vector<std::uint32_t> generic_prefix_members;
        Vector<AuthoritativeSignalPlanes::FrontierWriteBinding>
            writable_signals;
        Vector<std::size_t> pending_plane_offsets;
        Vector<std::uint64_t> pending_plane_words;
        Vector<SystemVerilogCompactBatchMember> compact_members;
        Vector<std::uint64_t> issued_sequences;
        // Optional geometry-only certificate storage uses its own bounded
        // allocations so failure to allocate this optimization never rejects
        // an otherwise valid checked runtime. All three vectors are sized
        // during preparation and remain allocation-free during dispatch.
        std::vector<RegionFrontierAliasRange> alias_certificate_ranges;
        std::vector<RegionFrontierAliasRange> alias_candidate_ranges;
        std::vector<std::size_t> alias_sorted_indices;
    };

    struct RegionFrontierComponentRuntime final
        : SchedulerOrderedBatchTask, RegionFrontierComponentWorkspace {
        static constexpr std::size_t scheduler_task_capacity = 64U;

        using GenericQueuedMember
            = RegionFrontierComponentWorkspace::GenericQueuedMember;
        using GenericParkedExecutorBinding
            = RegionFrontierComponentWorkspace::GenericParkedExecutorBinding;

        struct FallbackToken {
            RegionFrontierComponentRuntime* runtime { };
            std::uint64_t payload { };
        };
        struct GenericFallbackToken {
            RegionFrontierComponentRuntime* runtime { };
            std::uint64_t payload { };
        };

        Impl* owner { };
        std::size_t component { no_systemverilog_update_slot };
        std::uint64_t runtime_generation { };
        RegionFrontierExecutionModeV2 execution_mode {
            RegionFrontierExecutionModeV2::systemverilog_active
        };
        // Keep the exact A4 backing selected during preparation alive while
        // scheduler tickets retain this runtime. This does not make a stale
        // generation executable; dispatch revalidates active ownership.
        std::shared_ptr<RegionAuthoritativeComponentState>
            authoritative_state;
        std::shared_ptr<RegionFrontierBackendEntry> backend;
        RegionFrontierFrameV2 frame;
        std::uint32_t stop_requested { };
        std::uint32_t boundary_callback_started_slot {
            std::numeric_limits<std::uint32_t>::max() };
        std::uint64_t native_member_dispatches { };
        RegionFrontierAliasCertificateContext alias_certificate_context;
        // Each exact scheduler task count is certified independently; no
        // smaller or larger task extent inherits another count's proof.
        std::array<std::uint8_t, scheduler_task_capacity + 1U>
            alias_certificate_task_count_valid { };
        std::size_t alias_certificate_pending_task_count { };
        std::uint64_t alias_checked_entries { };
        std::uint64_t alias_trusted_entries { };
        std::uint64_t alias_sorted_proof_attempts { };
        std::uint64_t alias_sorted_proof_successes { };
        std::uint64_t alias_sorted_proof_failures { };
        bool frame_initialized { };
        bool scheduler_state_seeded { };
        bool alias_certificate_storage_available { };
        // True only when the common inventory/context has at least one
        // confirmed exact task-count key.
        bool alias_certificate_valid { };
        bool alias_certificate_pending_confirmation { };
        bool invalidated { };
        std::atomic_flag in_use = ATOMIC_FLAG_INIT;

        [[nodiscard]] bool try_enter() noexcept
        {
            return !in_use.test_and_set(std::memory_order_acquire);
        }

        void leave() noexcept
        {
            in_use.clear(std::memory_order_release);
        }

        void invalidate(std::source_location caller
            = std::source_location::current()) noexcept;

        [[nodiscard]] bool collect_alias_certificate_ranges(
            const RegionFrontierLayoutV2& layout) noexcept;
        [[nodiscard]] bool prove_alias_geometry_sorted() noexcept;
        [[nodiscard]] RegionFrontierAliasCertificateContext
        make_alias_certificate_context(
            const RegionFrontierBackendEntry& backend_entry,
            RegionFrontierStepEntryV2 checked_entry,
            RegionFrontierStepEntryV2 trusted_entry) const noexcept;
        [[nodiscard]] bool alias_certificate_common_ranges_match(
            bool& task_extent_changed,
            std::size_t& first_other_mismatch,
            bool& task_address_changed,
            bool& task_alias_tag_changed) const noexcept;
        [[nodiscard]] bool alias_certificate_matches(
            const RegionFrontierAliasCertificateContext& context) noexcept;
        [[nodiscard]] bool stage_alias_certificate(
            const RegionFrontierAliasCertificateContext& context) noexcept;
        [[nodiscard]] bool confirm_alias_certificate(
            const RegionFrontierAliasCertificateContext& context,
            RegionFrontierStatusV2 status) noexcept;
        void clear_alias_certificate() noexcept;
        void record_alias_certificate_miss(
            RegionFrontierAliasMissReason reason,
            std::size_t tuple_index = std::numeric_limits<std::size_t>::max(),
            std::uint32_t context_mask = 0U,
            std::size_t other_tuple_index
                = std::numeric_limits<std::size_t>::max()) noexcept;

        void clear_generic_queued_member(std::size_t member) noexcept;

        [[nodiscard]] SchedulerBatchResult execute(
            Scheduler& active_scheduler,
            std::span<const std::uint64_t> payloads) override;
        [[nodiscard]] ::fsim::runtime::detail::SchedulerTaskDescriptor
        make_fallback_descriptor(std::uint64_t payload) noexcept override;
        void synchronize_committed_state(
            AuthoritativeSignalPlanes::FrontierWriteLease& lease) noexcept;
        [[nodiscard]] bool bind_frame_planes_and_metadata(
            AuthoritativeSignalPlanes::FrontierWriteLease& lease) noexcept;
        [[nodiscard]] bool commit_staged_events(
            Scheduler::SystemVerilogGroupBatchReservation& reservation,
            std::shared_ptr<void> owner_lifetime,
            const SchedulerBatchFrontier& frontier) noexcept;
        void publish_boundary_commit(
            const SchedulerBatchFrontierEntry& task,
            std::uint32_t pending_slot,
            AuthoritativeSignalPlanes::FrontierWriteLease& lease);
    };
    struct RegionConeForwardingResultBank {
        explicit RegionConeForwardingResultBank(
            const RegionConeForwardingKernel& kernel,
            const std::size_t activation_member_count)
        {
            const auto boundary_count = std::ranges::count_if(
                kernel.execution_kernel.inputs,
                [](const RegionConeKernelInput& input) {
                    return !input.internal;
                });
            boundary_signals.reserve(
                static_cast<std::size_t>(boundary_count));
            boundary_revisions.reserve(
                static_cast<std::size_t>(boundary_count));
            boundary_values.reserve(
                static_cast<std::size_t>(boundary_count));
            for (const auto& input : kernel.execution_kernel.inputs) {
                if (!input.internal) {
                    boundary_signals.push_back(input.signal);
                    boundary_revisions.push_back(0U);
                    boundary_values.emplace_back();
                }
            }
            internal_initial_revisions.resize(
                kernel.internal_signals.size(), 0U);
            for (auto& seed_bank : internal_seed_banks) {
                seed_bank.reserve(kernel.internal_signals.size());
            }
            for (const auto signal : kernel.internal_signals) {
                const auto output = std::ranges::find(
                    kernel.execution_kernel.outputs, signal,
                    &RegionConeOutputBinding::signal);
                if (output == kernel.execution_kernel.outputs.end()
                    || output->width == 0U
                    || output->value_kind != ValueKind::logic4) {
                    throw std::invalid_argument {
                        "forwarding seed bank lacks a whole Logic4 output binding"
                    };
                }
                for (auto& seed_bank : internal_seed_banks) {
                    seed_bank.push_back({ signal, output->owner,
                        PackedLogic4(output->width, Logic4::zero),
                        PackedLogic4(output->width, Logic4::zero),
                        PackedLogic4(output->width, Logic4::zero) });
                }
            }
            output_values.reserve(
                kernel.execution_kernel.outputs.size());
            prepared_role_mutations.resize(
                kernel.execution_kernel.outputs.size());
            prepared_role_mutation_ready.resize(
                kernel.execution_kernel.outputs.size(), 0U);
            for (std::size_t index = 0U;
                 index < kernel.execution_kernel.outputs.size(); ++index) {
                const auto& output
                    = kernel.execution_kernel.outputs[index];
                if (output.value_kind != ValueKind::logic4
                    || output.width == 0U) {
                    throw std::invalid_argument {
                        "forwarding result bank requires nonempty Logic4 outputs"
                    };
                }
                output_values.emplace_back(output.width, Logic4::zero);
                const auto word_count
                    = static_cast<std::size_t>(output.width / 64U)
                    + static_cast<std::size_t>(output.width % 64U != 0U);
                prepared_role_mutations[index].words.reserve(word_count);
            }
            applied_role_mutations.reserve(
                kernel.execution_kernel.outputs.size());
            applied_role_metadata.reserve(
                kernel.execution_kernel.outputs.size());
            member_active.resize(kernel.members.size(), 0U);
            member_consumed.resize(kernel.members.size(), 0U);
            seed_seen.resize(kernel.members.size(), 0U);
            topological_position.resize(kernel.members.size(),
                kernel.members.size());
            readiness_mask.resize((activation_member_count + 63U) / 64U, 0U);
            prefix.tasks.reserve(kernel.members.size());
            seed_indices.reserve(kernel.members.size());
            member_indices.reserve(kernel.members.size());
            output_indices.reserve(kernel.execution_kernel.outputs.size());
            stable_orders.reserve(kernel.execution_kernel.outputs.size());
            update_slots.reserve(kernel.execution_kernel.outputs.size());
            update_tokens.reserve(kernel.execution_kernel.outputs.size());
            tasks.reserve(kernel.execution_kernel.outputs.size());
            compact_members.reserve(kernel.execution_kernel.outputs.size());
            completion_storage_identities.reserve(kernel.members.size());
            stage_batch_group_shape
                = kernel.execution_kernel.outputs.size() > 1U
                && kernel.execution_kernel.outputs.size() <= 64U;
            if (stage_batch_group_shape) {
                stage_batch_pool.reserve(1U);
                auto batch
                    = std::make_shared<RegionForwardingPrivateStageBatch>();
                batch->tokens.reserve(
                    kernel.execution_kernel.outputs.size());
                batch->private_outputs.reserve(
                    kernel.execution_kernel.outputs.size());
                stage_batch_pool.push_back(std::move(batch));
            }
        }

        [[nodiscard]] std::shared_ptr<RegionForwardingPrivateStageBatch>
        acquire_stage_batch(Impl* const owner,
            const std::size_t component,
            const std::uint64_t stage_runtime_generation,
            const SimulationTick stage_time,
            const std::size_t token_capacity)
        {
            for (auto& batch : stage_batch_pool) {
                if (batch && batch.use_count() == 1
                    && batch->tokens.capacity() >= token_capacity
                    && batch->private_outputs.capacity() >= token_capacity) {
                    batch->tokens.clear();
                    batch->private_outputs.clear();
                    batch->owner = owner;
                    batch->component = component;
                    batch->runtime_generation = stage_runtime_generation;
                    batch->time = stage_time;
                    return batch;
                }
            }

            auto batch = std::make_shared<RegionForwardingPrivateStageBatch>();
            batch->tokens.reserve(token_capacity);
            batch->private_outputs.reserve(token_capacity);
            batch->owner = owner;
            batch->component = component;
            batch->runtime_generation = stage_runtime_generation;
            batch->time = stage_time;
            stage_batch_pool.push_back(batch);
            return batch;
        }

        void discard() noexcept
        {
            active = false;
            remaining_members = 0U;
            std::ranges::fill(member_active, 0U);
            std::ranges::fill(member_consumed, 0U);
            stage_batch.reset();
        }

        bool active { };
        std::uint64_t runtime_generation { };
        SimulationTick time { };
        std::uint64_t starting_delta { };
        std::size_t remaining_members { };
        std::vector<SignalId> boundary_signals;
        std::vector<std::uint64_t> boundary_revisions;
        std::vector<PackedLogic4> boundary_values;
        std::array<std::vector<RegionKernelInternalSeed>, 2U>
            internal_seed_banks;
        // Activation copies share the selected wide owning values. Refill only
        // the opposite bank, and advance this index only after reset succeeds.
        std::size_t activation_seed_bank_index { };
        bool activation_seed_bank_valid { };
        std::vector<std::uint64_t> internal_initial_revisions;
        std::vector<PackedLogic4> output_values;
        // Output-indexed mutations are prepared before original callback
        // tickets become visible. Applied rows move into callback order, so
        // an observer can publish the exact consumed role prefix atomically.
        std::vector<AuthoritativeSignalPlanes::PreparedMutation>
            prepared_role_mutations;
        std::vector<std::uint8_t> prepared_role_mutation_ready;
        std::vector<AuthoritativeSignalPlanes::PreparedMutation>
            applied_role_mutations;
        std::vector<RegionForwardingAppliedRoleCommitMetadata>
            applied_role_metadata;
        AuthoritativeSignalPlanes::PreparedGroupScratch role_flush_scratch;
        std::uint64_t role_callback_order { };
        bool role_journal_enabled { };
        bool private_epoch_retired { };
        std::vector<std::uint8_t> member_active;
        std::vector<std::uint8_t> member_consumed;
        std::vector<std::uint8_t> seed_seen;
        std::vector<std::size_t> topological_position;
        std::vector<std::uint64_t> readiness_mask;
        std::shared_ptr<RegionForwardingPrivateStageBatch> stage_batch;
        std::vector<std::shared_ptr<RegionForwardingPrivateStageBatch>>
            stage_batch_pool;
        bool stage_batch_group_shape { };
        std::vector<std::size_t> seed_indices;
        std::vector<std::size_t> member_indices;
        std::vector<std::size_t> output_indices;
        RegionKernelSchedulerPrefix prefix;
        std::vector<StableOrder> stable_orders;
        std::vector<std::size_t> update_slots;
        std::vector<SystemVerilogUpdateToken> update_tokens;
        std::vector<::fsim::runtime::detail::SchedulerTaskDescriptor> tasks;
        std::vector<SystemVerilogCompactBatchMember> compact_members;
        std::vector<const void*> completion_storage_identities;
    };
    /// Persistent internal-register contents for one graph component. This
    /// state is separate from the pooled backend, whose immutable kernel may
    /// be shared by different graph components.
    struct RegionLocalWaveComponentState {
        static constexpr std::size_t max_pending_private_update_tickets = 16U;
        static constexpr std::size_t no_output_index
            = std::numeric_limits<std::size_t>::max();

        struct OutputIndexKernelIdentity final {
            const RegionConeActivationKernel* kernel { };
            const SignalId* internal_signal_data { };
            std::size_t internal_signal_count { };
            const RegionConeOutputBinding* output_binding_data { };
            std::size_t output_binding_count { };

            void bind(const RegionConeActivationKernel& candidate) noexcept
            {
                kernel = &candidate;
                internal_signal_data = candidate.internal_signals.data();
                internal_signal_count = candidate.internal_signals.size();
                output_binding_data = candidate.outputs.data();
                output_binding_count = candidate.outputs.size();
            }

            [[nodiscard]] bool matches(
                const RegionConeActivationKernel& candidate) const noexcept
            {
                return kernel == &candidate
                    && internal_signal_data
                        == candidate.internal_signals.data()
                    && internal_signal_count
                        == candidate.internal_signals.size()
                    && output_binding_data == candidate.outputs.data()
                    && output_binding_count == candidate.outputs.size();
            }
        };

        RegionLocalWaveComponentState(
            const std::uint64_t runtime_generation,
            const RegionConeActivationKernel& kernel,
            const RegionConeForwardingKernel* forwarding = nullptr)
            : generation(runtime_generation)
            , activation(kernel)
        {
            activation_kernel_identity.bind(kernel);
            try {
                struct OutputSignalIndex final {
                    SignalId signal { };
                    std::size_t output_index { };
                };
                std::vector<OutputSignalIndex> sorted_outputs;
                if (kernel.outputs.size() <= sorted_outputs.max_size()
                    && kernel.internal_signals.size()
                        <= internal_output_indices.max_size()) {
                    sorted_outputs.reserve(kernel.outputs.size());
                    for (std::size_t output_index = 0U;
                         output_index < kernel.outputs.size(); ++output_index) {
                        sorted_outputs.push_back({
                            kernel.outputs[output_index].signal, output_index });
                    }
                    std::ranges::sort(sorted_outputs,
                        [](const OutputSignalIndex& left,
                           const OutputSignalIndex& right) {
                            if (left.signal != right.signal) {
                                return left.signal < right.signal;
                            }
                            return left.output_index < right.output_index;
                        });
                    internal_output_indices.reserve(
                        kernel.internal_signals.size());
                    for (const auto signal : kernel.internal_signals) {
                        const auto first = std::ranges::lower_bound(
                            sorted_outputs, signal, { },
                            &OutputSignalIndex::signal);
                        if (first == sorted_outputs.end()
                            || first->signal != signal
                            || ((first + 1) != sorted_outputs.end()
                                && (first + 1)->signal == signal)) {
                            internal_output_indices.push_back(no_output_index);
                        } else {
                            internal_output_indices.push_back(
                                first->output_index);
                        }
                    }
                    output_index_generation = runtime_generation;
                    output_index_ready = true;
                }
            } catch (const std::bad_alloc&) {
                internal_output_indices.clear();
                output_index_generation = 0U;
                output_index_ready = false;
            }
            std::size_t max_output_words { 1U };
            for (const auto& output : kernel.outputs) {
                const auto output_words
                    = static_cast<std::size_t>(output.width / 64U)
                    + static_cast<std::size_t>(output.width % 64U != 0U);
                max_output_words = std::max(max_output_words,
                    output_words);
            }
            mutation_scratch.words.reserve(max_output_words);
            if (forwarding != nullptr) {
                forwarding_results.emplace(*forwarding,
                    kernel.members.size());
            }
        }

        [[nodiscard]] InternalSystemVerilogOrderedTicketStorage*
        acquire_private_update_ticket_storage()
        {
            for (auto& storage : private_update_ticket_storage) {
                if (storage && storage->available())
                    return storage.get();
            }
            for (auto& storage : private_update_ticket_storage) {
                if (!storage) {
                    storage = std::make_unique<
                        InternalSystemVerilogOrderedTicketStorage>();
                    return storage.get();
                }
            }
            return nullptr;
        }

        std::uint64_t generation { };
        std::uint64_t output_index_generation { };
        bool output_index_ready { };
        OutputIndexKernelIdentity activation_kernel_identity;
        OutputIndexKernelIdentity frontier_kernel_identity;
        // Immutable binding indices for kernel.internal_signals. A sentinel
        // records a missing or non-unique output and therefore declines.
        std::vector<std::size_t> internal_output_indices;
        // A matching A4 revision proves the persistent internal register bank
        // still reflects current/previous/raw-owner inputs without rereading
        // those planes before the next wave.
        std::uint64_t authoritative_revision { };
        RegionKernelActivationState activation;
        std::optional<RegionConeForwardingResultBank> forwarding_results;
        // The reusable owner mutation stores one PreparedWord per output word;
        // reserve to the widest immutable kernel output before any wave.
        AuthoritativeSignalPlanes::PreparedMutation mutation_scratch;
        // Active slots remain unavailable until their original-key suffixes
        // complete or are discarded; the fixed pool is the pending-ticket cap.
        std::array<std::unique_ptr<
            InternalSystemVerilogOrderedTicketStorage>,
            max_pending_private_update_tickets> private_update_ticket_storage;
        bool seeded { };
    };
    struct RegionReadinessQueueMember {
        std::size_t component { std::numeric_limits<std::size_t>::max() };
        std::size_t member { std::numeric_limits<std::size_t>::max() };
        std::uint64_t generation { };
        RegionFrontierKeyV1 queued_key;
        std::uint64_t static_trigger_mask { };
        bool key_valid { };
    };
    struct RegionReadinessMaskDescriptor {
        std::size_t offset { };
        std::size_t word_count { };
        std::uint64_t generation { };
    };
    struct RegionGroupedFanoutMember {
        ProcessId process { };
        std::size_t readiness_member { };
        std::uint64_t static_trigger_mask { };
        std::size_t sensitivity_range_offset { };
        std::size_t sensitivity_range_count { };
        std::uint64_t sensitivity_range_generation { };
    };
    struct RegionGroupedFanoutGroup {
        std::size_t component { };
        std::size_t member_offset { };
        std::size_t member_count { };
        std::uint64_t generation { };
    };
    struct RegionGroupedFanoutSignal {
        std::size_t group_offset { };
        std::size_t group_count { };
        std::uint64_t generation { };
    };
    struct RegionRuntimeSnapshot {
        RegionGraph graph;
        std::shared_ptr<const SignalDriverInventory> signal_driver_inventory;
        std::vector<std::optional<RegionConeProgram>> programs_by_component;
        std::vector<std::size_t> component_by_process;
        std::vector<std::shared_ptr<RegionKernelBackendEntry>>
            backends_by_component;
        std::vector<std::uint64_t> backend_generation_by_component;
        std::vector<std::shared_ptr<RegionKernelBackendEntry>> backend_pool;
        std::vector<std::shared_ptr<RegionConeForwardingBackendEntry>>
            forwarding_backends_by_component;
        std::vector<std::shared_ptr<RegionConeForwardingBackendEntry>>
            forwarding_backend_pool;
        std::vector<std::shared_ptr<RegionFrontierBackendEntry>>
            frontier_backends_by_component;
        std::vector<std::shared_ptr<RegionFrontierBackendEntry>>
            frontier_backend_pool;
        std::vector<std::shared_ptr<RegionFrontierComponentRuntime>>
            frontier_runtime_by_component;
        std::vector<std::shared_ptr<VhdlProjectedReadinessTask>>
            vhdl_projected_readiness_by_component;
        std::vector<std::shared_ptr<RegionAuthoritativeComponentState>>
            authoritative_state_by_component;
        std::vector<std::shared_ptr<RegionLocalWaveComponentState>>
            local_wave_state_by_component;
        std::vector<std::uint8_t>
            value_only_recertification_by_component;
        std::vector<std::size_t> authoritative_component_by_signal;
        std::vector<std::size_t> authoritative_member_index_by_process;
        std::vector<std::uint64_t> readiness_mask_words;
        std::vector<RegionReadinessMaskDescriptor>
            readiness_mask_by_component;
        std::vector<std::size_t> readiness_member_index_by_process;
        std::vector<RegionReadinessQueueMember>
            readiness_queued_by_process;
        std::vector<RegionGroupedFanoutSignal>
            grouped_fanout_by_signal;
        std::vector<RegionGroupedFanoutGroup> grouped_fanout_groups;
        std::vector<RegionGroupedFanoutMember> grouped_fanout_members;
        std::vector<RegionFrontierSensitivityRange>
            grouped_fanout_sensitivity_ranges;
        std::vector<Scheduler::SystemVerilogGroupBatchMember>
            grouped_readiness_member_scratch;
        std::vector<SchedulerSystemVerilogKeyReceipt>
            grouped_readiness_receipt_scratch;
        std::vector<RegionReadinessQueueMember>
            grouped_readiness_queue_scratch;
        std::vector<std::uint8_t>
            grouped_readiness_member_changed_scratch;
        std::vector<ProcessId> grouped_readiness_process_scratch;
        std::vector<RegionPreparedSuccessorSignalMap>
            prepared_successor_by_signal;
        std::vector<RegionPreparedSuccessorReaderBinding>
            prepared_successor_readers;
        std::uint64_t grouped_fanout_oversized_group_fallbacks { };
        bool authoritative_state_incomplete { };
        bool profile_systemverilog_waves { };
        std::uint64_t generation { };
        bool process_signal_access_inventory_complete { true };
    };
    [[nodiscard]] RegionRuntimeSnapshot prepare_region_runtime_snapshot(
        RegionGraph graph,
        std::span<const Process* const> programs,
        bool process_access_inventory_complete,
        bool enable_region_kernel,
        bool may_compile_backends,
        std::shared_ptr<const SignalDriverInventory> signal_driver_inventory);
    [[nodiscard]] RegionRuntimeSnapshot build_region_runtime_snapshot(
        bool profile_systemverilog_waves,
        bool enable_region_kernel,
        bool may_compile_backends);
    [[nodiscard]] RegionRuntimeSnapshot prepare_current_region_runtime_snapshot();
    void publish_region_runtime_snapshot(
        RegionRuntimeSnapshot&& snapshot) noexcept;
    void request_full_region_recertification(
        std::source_location caller = std::source_location::current()) noexcept;
    void clear_value_only_region_recertification(
        std::source_location caller = std::source_location::current()) noexcept;
    void mark_value_only_region_recertification(
        std::size_t component) noexcept;
    [[nodiscard]] bool try_rebind_value_only_region_components() noexcept;
    [[nodiscard]] bool prepare_region_frontier_component(
        RegionRuntimeSnapshot& snapshot,
        std::size_t component,
        const RegionConeActivationKernel& kernel,
        std::shared_ptr<RegionFrontierBackendEntry> backend,
        RegionFrontierComponentRuntime& runtime,
        const char** rejection_reason = nullptr);
    [[nodiscard]] bool preflight_region_frontier_component_layout(
        const RegionRuntimeSnapshot& snapshot,
        std::size_t component,
        const RegionConeActivationKernel& kernel,
        const RegionFrontierLayoutV2& layout,
        const char** rejection_reason = nullptr) const;
    [[nodiscard]] bool bind_generic_frontier_read_snapshots(
        RegionFrontierComponentRuntime& runtime);
    [[nodiscard]] bool stage_generic_frontier_update_batch(
        RegionFrontierComponentRuntime& runtime,
        std::exception_ptr& failure) noexcept;
    [[nodiscard]] std::optional<SchedulerBatchResult>
    execute_generic_region_frontier_prefix(
        RegionFrontierComponentRuntime& runtime,
        const SchedulerGenericBatchFrontier& frontier,
        std::span<const std::uint64_t> payloads,
        std::size_t frontier_offset,
        std::size_t task_prefix_count,
        bool require_compact_receipts = false);
    static void dispatch_region_frontier_fallback(
        Scheduler&,
        const RegionFrontierComponentRuntime::FallbackToken&);
    static void dispatch_generic_frontier_fallback(
        Scheduler&,
        const RegionFrontierComponentRuntime::GenericFallbackToken&);
    [[nodiscard]] std::optional<SchedulerBatchResult>
    try_execute_region_frontier_prefix(
        const SchedulerBatchFrontier& frontier,
        std::span<const std::uint64_t> payloads);
    [[nodiscard]] std::optional<SchedulerBatchResult>
    try_execute_region_frontier_component(
        RegionFrontierComponentRuntime& runtime,
        const SchedulerBatchFrontier& frontier,
        std::span<const std::uint64_t> payloads,
        std::size_t task_prefix_count);
    [[nodiscard]] bool synchronize_frontier_process_states(
        RegionFrontierComponentRuntime& runtime) noexcept;
    [[nodiscard]] RegionAuthoritativeComponentState*
    region_authoritative_state_for_signal(SignalId signal) noexcept;
    [[nodiscard]] bool region_has_fanout_member(
        SignalId signal, ProcessId process) noexcept;
    [[nodiscard]] bool region_take_ready(
        SignalId signal, ProcessId process,
        std::uint64_t& trigger_mask) noexcept;
    void record_systemverilog_readiness_key(
        ProcessId process, std::size_t component, std::size_t member,
        std::uint64_t generation,
        const SchedulerSystemVerilogKeyReceipt& receipt,
        std::uint64_t trigger_mask) noexcept;
    void merge_systemverilog_readiness_mask(
        ProcessId process, std::uint64_t trigger_mask) noexcept;
    void mark_region_value_change(
        SignalId signal, const PackedLogic4& previous,
        const PackedLogic4& current) noexcept;
    void mirror_region_stored(SignalId signal) noexcept;
    void mirror_region_visible(SignalId signal) noexcept;
    void mirror_region_owner(
        SignalId signal, ProcessId process,
        const PackedLogic4& value) noexcept;
    void demote_region_authoritative_slots(
        std::size_t component, bool observation,
        std::source_location caller = std::source_location::current()) noexcept;
    void unbind_region_authoritative_slots(
        std::size_t component, bool observation) noexcept;
    void demote_all_region_authoritative_slots(bool observation,
        std::source_location caller = std::source_location::current()) noexcept;
    void prepare_region_authoritative_write(SignalId signal) noexcept;
    void prepare_region_authoritative_write(
        std::span<const SignalId> signals) noexcept;
    void prepare_region_authoritative_family_write(
        ContainerObjectId object) noexcept;
    void note_region_authoritative_mirror() noexcept;
    // Legacy native hidden-state paths do not all consult RegionGraph. They
    // therefore require a cached proof that every alternate process
    // executor is bound to its exact registered signal-access program.
    bool process_signal_access_inventory_complete { true };
    void build_region_graph();
    void try_recertify_region_graph() noexcept;
    void note_region_graph_policy_change() noexcept;
    [[nodiscard]] bool process_signal_access_is_complete(
        ProcessId process) const noexcept;
    [[nodiscard]] bool process_region_kernel_eligible(
        ProcessId process) const noexcept;
    [[nodiscard]] std::optional<ConstantDriverStartupEntry>
    recognize_constant_driver_startup(
        ProcessId process, const ProcessProgramView& program) const;
    std::vector<std::uint8_t> native_signal_dependency_mask;
    // Distinguishes module/timing dependencies from ranged static
    // sensitivities, which RegionGraph records with exact bit ranges.
    std::vector<std::uint8_t> native_signal_non_range_dependency_mask;
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
    // Index by the original ProcessId. Eligible constant continuous
    // assignments keep their process identity and startup literal, while
    // startup execution stages the write without materializing a register
    // frame or retaining the operation body.
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
    std::vector<FusedStaticCohortPlan> fused_static_cohorts;
    FusedStaticCounters fused_static_counts;
    std::size_t fused_static_certified_plan_count { };
    bool fused_static_counters_enabled { };
    bool fused_static_trace_enabled { };
    bool fused_static_bindings_open { };

    std::vector<std::size_t> static_sensitivity_cohort_by_process;
    std::unordered_map<std::string, std::size_t>
        static_sensitivity_cohort_by_key;
    std::uint64_t static_fanout_visit_generation { };
    bool systemverilog_wave_profile_enabled { };
    bool systemverilog_region_kernel_enabled { };
    bool systemverilog_local_wave_enabled { };
    bool a4_wide_single_owner_commit_enabled { };
    bool a4_wide_single_owner_versioned_storage_explicit { };
    bool a4_wide_disjoint_owner_commit_enabled { };
    bool a4_wide_disjoint_owner_versioned_storage_explicit { };
    std::vector<std::optional<RegionConeProgram>> region_activation_programs;
    std::vector<std::size_t> region_component_by_process;
    std::vector<std::shared_ptr<RegionKernelBackendEntry>>
        region_kernel_backends_by_component;
    std::vector<std::uint64_t>
        region_kernel_backend_generation_by_component;
    std::vector<std::shared_ptr<RegionKernelBackendEntry>>
        region_kernel_backend_pool;
    std::vector<std::shared_ptr<RegionConeForwardingBackendEntry>>
        region_cone_forwarding_backends_by_component;
    std::vector<std::shared_ptr<RegionConeForwardingBackendEntry>>
        region_cone_forwarding_backend_pool;
    std::vector<std::shared_ptr<RegionFrontierBackendEntry>>
        region_frontier_backends_by_component;
    std::vector<std::shared_ptr<RegionFrontierBackendEntry>>
        region_frontier_backend_pool;
    std::vector<std::shared_ptr<RegionFrontierComponentRuntime>>
        region_frontier_runtime_by_component;
    std::vector<std::shared_ptr<VhdlProjectedReadinessTask>>
        vhdl_projected_readiness_by_component;
    std::shared_ptr<RegionKernelBackendProvider>
        region_kernel_backend_provider;
    bool region_kernel_backend_provider_supports_frontier { };
    std::string region_kernel_backend_provider_identity;
    std::uint64_t region_runtime_generation { };
    // Nonzero only after every signal completed a callback-observation pass
    // for this runtime snapshot. Mutations and recertification invalidate it.
    std::uint64_t completed_callback_observation_generation { };
    bool region_forwarding_role_flush_pending_after_discard { };
    std::size_t region_forwarding_role_journal_nonempty_components { };
    std::vector<std::shared_ptr<RegionAuthoritativeComponentState>>
        region_authoritative_state_by_component;
    std::vector<std::shared_ptr<RegionLocalWaveComponentState>>
        region_local_wave_state_by_component;
    std::vector<std::size_t> region_authoritative_component_by_signal;
    std::vector<std::size_t> region_authoritative_member_index_by_process;
    std::vector<std::uint64_t> region_readiness_mask_words;
    std::vector<RegionReadinessMaskDescriptor>
        region_readiness_mask_by_component;
    std::vector<std::size_t> region_readiness_member_index_by_process;
    std::vector<RegionReadinessQueueMember>
        region_readiness_queued_by_process;
    std::vector<RegionGroupedFanoutSignal>
        region_grouped_fanout_by_signal;
    std::vector<RegionGroupedFanoutGroup> region_grouped_fanout_groups;
    std::vector<RegionGroupedFanoutMember> region_grouped_fanout_members;
    std::vector<RegionFrontierSensitivityRange>
        region_grouped_fanout_sensitivity_ranges;
    std::vector<Scheduler::SystemVerilogGroupBatchMember>
        region_grouped_readiness_member_scratch;
    std::vector<SchedulerSystemVerilogKeyReceipt>
        region_grouped_readiness_receipt_scratch;
    std::vector<RegionReadinessQueueMember>
        region_grouped_readiness_queue_scratch;
    std::vector<std::uint8_t>
        region_grouped_readiness_member_changed_scratch;
    std::vector<ProcessId> region_grouped_readiness_process_scratch;
    std::vector<RegionPreparedSuccessorSignalMap>
        region_prepared_successor_by_signal;
    std::vector<RegionPreparedSuccessorReaderBinding>
        region_prepared_successor_readers;
    std::uint64_t region_grouped_fanout_oversized_group_fallbacks { };
    bool region_authoritative_recertification_waiting { };
    std::uint64_t systemverilog_wave_profile_calls { };
    std::uint64_t systemverilog_wave_profile_offered_members { };
    std::uint64_t systemverilog_wave_profile_accepted_calls { };
    std::uint64_t systemverilog_wave_profile_accepted_members { };
    std::uint64_t systemverilog_wave_profile_declined_calls { };
    std::uint64_t systemverilog_wave_profile_failed_calls { };
    std::uint64_t systemverilog_wave_profile_region_kernel_attempts { };
    std::uint64_t systemverilog_wave_profile_region_kernel_runs { };
    std::uint64_t
        systemverilog_wave_profile_native_frontier_member_dispatches { };
    std::uint64_t systemverilog_wave_profile_native_frontier_budget_trims { };
    std::uint64_t systemverilog_wave_profile_native_frontier_alias_entry_rows { };
    bool systemverilog_wave_profile_native_frontier_alias_trusted_entry_seen { };
    std::uint64_t systemverilog_wave_profile_alias_checked_entries { };
    std::uint64_t systemverilog_wave_profile_alias_trusted_entries { };
    std::uint64_t systemverilog_wave_profile_alias_unavailable_entries { };
    std::uint64_t systemverilog_wave_profile_alias_forced_staged_entries { };
    std::uint64_t systemverilog_wave_profile_alias_confirmation_attempts { };
    std::uint64_t systemverilog_wave_profile_alias_confirmation_successes { };
    std::uint64_t systemverilog_wave_profile_alias_confirmation_failures { };
    std::uint64_t systemverilog_wave_profile_alias_miss_rows { };
    std::array<std::uint64_t,
        static_cast<std::size_t>(RegionFrontierAliasMissReason::count)>
        systemverilog_wave_profile_alias_misses { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_attempts { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_evaluations { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_member_consumptions { };
    std::uint64_t systemverilog_wave_profile_v2_selected_forwarding_prefixes { };
    std::uint64_t systemverilog_wave_profile_v2_selected_forwarding_members { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_declines { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_stage_attempts { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_stage_dispatches { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_stage_members { };
    std::uint64_t systemverilog_wave_profile_region_forwarding_stage_declines { };
    std::uint64_t
        systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors { };
    std::uint64_t
        systemverilog_wave_profile_region_forwarding_private_parent_slots_elided { };
    std::uint64_t
        systemverilog_wave_profile_region_forwarding_private_parent_dispatches { };
    std::uint64_t
        systemverilog_wave_profile_region_forwarding_private_parent_fallbacks { };
    std::uint64_t
        systemverilog_wave_profile_region_forwarding_public_update_tokens { };
    std::uint64_t systemverilog_wave_profile_region_kernel_members { };
    std::uint64_t systemverilog_wave_profile_region_kernel_publications { };
    std::uint64_t systemverilog_wave_profile_region_kernel_failures { };
    std::uint64_t systemverilog_wave_profile_region_backend_attempts { };
    std::uint64_t systemverilog_wave_profile_region_backend_runs { };
    std::uint64_t systemverilog_wave_profile_region_backend_completions { };
    std::uint64_t systemverilog_wave_profile_region_trace_declines { };
    std::uint64_t systemverilog_wave_profile_region_recertification_attempts { };
    std::uint64_t systemverilog_wave_profile_region_recertification_successes { };
    std::uint64_t systemverilog_wave_profile_region_recertification_failures { };
    std::uint64_t systemverilog_wave_profile_region_recertification_processes { };
    std::uint64_t systemverilog_wave_profile_region_recertification_signals { };
    std::uint64_t systemverilog_wave_profile_region_recertification_components { };
    std::uint64_t systemverilog_wave_profile_a4_seeded_components { };
    std::uint64_t systemverilog_wave_profile_a4_seeded_signals { };
    std::uint64_t systemverilog_wave_profile_a4_seeded_owners { };
    std::uint64_t systemverilog_wave_profile_a4_slot_bind_components { };
    std::uint64_t systemverilog_wave_profile_a4_slot_bindings { };
    std::uint64_t systemverilog_wave_profile_a4_slot_rebinds { };
    std::uint64_t systemverilog_wave_profile_a4_bound_components { };
    std::uint64_t systemverilog_wave_profile_a4_bound_slots { };
    std::uint64_t systemverilog_wave_profile_a4_materialized_components { };
    std::uint64_t systemverilog_wave_profile_a4_materialized_slots { };
    std::uint64_t systemverilog_wave_profile_a4_authoritative_slot_writes { };
    std::uint64_t
        systemverilog_wave_profile_a4_unresolved_owner_alias_commits { };
    std::uint64_t systemverilog_wave_profile_prepared_output_batches { };
    std::uint64_t
        systemverilog_wave_profile_generated_successor_mask_batches { };
    std::uint64_t systemverilog_wave_profile_direct_ready_window_attempts { };
    std::uint64_t systemverilog_wave_profile_direct_ready_window_completions { };
    std::uint64_t systemverilog_wave_profile_prepared_output_seals { };
    std::uint64_t systemverilog_wave_profile_prepared_output_fallbacks { };
    std::uint64_t
        systemverilog_wave_profile_prepared_output_group_dispatches { };
    std::uint64_t
        systemverilog_wave_profile_prepared_output_group_members { };
    std::uint64_t systemverilog_wave_profile_a4_stored_mirrors { };
    std::uint64_t systemverilog_wave_profile_a4_visible_mirrors { };
    std::uint64_t systemverilog_wave_profile_a4_owner_mirrors { };
    std::uint64_t systemverilog_wave_profile_a4_invalidations { };
    std::uint64_t systemverilog_wave_profile_a4_value_marks { };
    std::uint64_t systemverilog_wave_profile_a4_transaction_marks { };
    std::uint64_t systemverilog_wave_profile_a4_ready_consumptions { };
    std::uint64_t systemverilog_wave_profile_a4_native_input_handoffs { };
    std::uint64_t systemverilog_wave_profile_a4_native_input_completions { };
    std::uint64_t systemverilog_wave_profile_readiness_mask_images { };
    std::uint64_t systemverilog_wave_profile_a2_local_update_dispatches { };
    std::uint64_t systemverilog_wave_profile_a2_local_update_fallbacks { };
    std::uint64_t systemverilog_wave_profile_a2_local_fanout_suppressions { };
    std::uint64_t systemverilog_wave_profile_a2_ordinary_internal_updates { };
    std::uint64_t systemverilog_wave_profile_a2_internal_seed_reads { };
    std::uint64_t systemverilog_wave_profile_a2_internal_state_seeds { };
    std::uint64_t systemverilog_wave_profile_a2_internal_state_reuses { };
    std::uint64_t systemverilog_wave_profile_a3_private_update_ticket_entries { };
    std::uint64_t systemverilog_wave_profile_a3_private_update_ticket_members { };
    std::uint64_t systemverilog_wave_profile_a3_private_update_entries_elided { };
    std::uint64_t systemverilog_wave_profile_a3_private_update_fallback_members { };
    std::uint64_t systemverilog_wave_profile_a2_grouped_fanout_members { };
    std::uint64_t systemverilog_wave_profile_a2_completion_fast_batches { };
    std::uint64_t systemverilog_wave_profile_a2_completion_fast_members { };
    std::uint64_t systemverilog_wave_profile_a2_completion_preflights { };
    std::uint64_t systemverilog_wave_profile_a2_completion_register_declines { };
    std::uint64_t systemverilog_wave_profile_p3_group_fanout_groups { };
    std::uint64_t systemverilog_wave_profile_p3_group_fanout_members { };
    std::uint64_t systemverilog_wave_profile_p3_group_fanout_tickets { };
    std::uint64_t systemverilog_wave_profile_p3_group_fanout_declines { };
    std::uint64_t systemverilog_wave_profile_a3_mapped_successor_batches { };
    std::uint64_t systemverilog_wave_profile_a3_mapped_successor_readers { };
    std::uint64_t generic_projected_region_attempts { };
    std::uint64_t generic_projected_region_backend_runs { };
    std::uint64_t generic_projected_region_completions { };
    std::uint64_t generic_projected_region_members { };
    std::uint64_t generic_projected_region_publications { };
    std::uint64_t generic_projected_region_declines { };
    std::uint64_t generic_projected_region_failures { };
    static constexpr std::uint64_t systemverilog_wave_payload
        = UINT64_C(1) << 61U;
    [[nodiscard]] bool can_queue_systemverilog_wave(ProcessId) const;
    [[nodiscard]] SchedulerBatchGroupKey
    systemverilog_wave_batch_group_key(ProcessId) const noexcept;
    [[nodiscard]] bool systemverilog_wave_member_eligible(ProcessId) const;
    [[nodiscard]] bool schedule_systemverilog_grouped_fanout(
        SignalId signal,
        const PackedLogic4& previous,
        const PackedLogic4& current,
        SignalChangeOrigin origin,
        const RegionPreparedOutputBatchState* prepared_batch = nullptr,
        std::size_t prepared_slot = no_systemverilog_update_slot) noexcept;
    [[nodiscard]] bool prepare_region_prepared_output_successors(
        std::size_t component,
        const RegionConeActivationKernel& kernel,
        RegionPreparedOutputBatchState& batch) noexcept;
    [[nodiscard]] bool schedule_prepared_successor_readers(
        SignalId signal,
        const PackedLogic4& previous,
        const PackedLogic4& current,
        SignalChangeOrigin origin,
        const RegionPreparedOutputBatchState& batch,
        std::size_t slot) noexcept;
    [[nodiscard]] bool match_region_fanout_sensitivity_ranges(
        std::size_t component,
        SignalId signal,
        std::uint64_t generation,
        std::size_t range_offset,
        std::size_t range_count,
        const PackedLogic4& previous,
        const PackedLogic4& current,
        bool& changed,
        bool* has_partial_range = nullptr) const noexcept;
    [[nodiscard]] bool validate_region_prepared_output_successors(
        const RegionConeActivationKernel& kernel,
        const RegionPreparedOutputBatchState& batch) const noexcept;
    struct SystemVerilogWaveFallbackPayload {
        Impl* owner { };
        ProcessId process { };
    };
    static void dispatch_systemverilog_wave_fallback(
        Scheduler&, const SystemVerilogWaveFallbackPayload&);
    [[nodiscard]] bool region_local_wave_component_eligible(
        std::size_t component,
        const RegionConeActivationKernel& kernel);
    void invalidate_region_local_wave_signal(SignalId signal) noexcept;
    void notify_region_local_value_change(
        std::size_t component,
        SignalId signal,
        const PackedLogic4& previous,
        const PackedLogic4& current,
        SignalChangeOrigin origin,
        const RegionPreparedOutputBatchState* prepared_batch = nullptr,
        std::size_t prepared_slot = no_systemverilog_update_slot);
    bool queue_systemverilog_wave(ProcessId);
    [[nodiscard]] bool build_systemverilog_readiness_mask(
        std::size_t component, std::span<const ExecutionContext> contexts,
        std::size_t member_count,
        std::span<std::uint64_t> active_mask) const noexcept;
    void clear_systemverilog_readiness_member(
        ProcessId, const RegionFrontierKeyV1* consumed_key = nullptr,
        std::source_location caller = std::source_location::current()) noexcept;
    void clear_systemverilog_readiness_prefix(
        const SchedulerBatchFrontier&, std::size_t count,
        std::source_location caller = std::source_location::current()) noexcept;
    [[nodiscard]] SchedulerBatchResult execute_systemverilog_wave(
        std::span<const std::uint64_t>);
    [[nodiscard]] bool execute_region_forwarding_prefix(
        std::size_t component,
        const RegionConeProgram& program,
        std::span<const ExecutionContext> contexts,
        std::size_t& executed_members,
        std::exception_ptr& backend_failure);
    [[nodiscard]] SchedulerBatchResult
    execute_region_forwarding_private_stage_batch(
        RegionForwardingPrivateStageBatch& batch,
        Scheduler& active_scheduler,
        std::span<const std::uint64_t> payloads);
    static constexpr std::uint64_t generic_projected_region_payload
        = UINT64_C(1) << 60U;
    [[nodiscard]] bool generic_projected_region_member_eligible(
        ProcessId) const;
    [[nodiscard]] bool queue_generic_projected_region(ProcessId);
    void merge_generic_frontier_ready_mask(
        ProcessId, std::uint64_t trigger_mask) noexcept;
    [[nodiscard]] SchedulerBatchResult execute_generic_projected_region(
        std::span<const std::uint64_t>, std::size_t frontier_offset,
        VhdlProjectedReadinessTask* ticket_runtime = nullptr);
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
        bool disjoint_owner_group { };
    };
    struct UpdateCommitWordScratch {
        std::uint64_t dirty_mask { };
        std::size_t head {
            std::numeric_limits<std::size_t>::max() };
    };
    std::vector<UpdateCommitScratchRow> update_commit_scratch;
    std::vector<UpdateCommitWordScratch> update_commit_words;
    struct DisjointOwnerGroupScratch {
        std::vector<AuthoritativeSignalPlanes::PreparedMutation> mutations;
        PackedLogic4 resolved;
        bool prepared { };
    };
    std::vector<DisjointOwnerGroupScratch>
        disjoint_owner_group_scratch_by_signal;
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
    ClassAllocateHook class_allocate_hook;
    ClassPropertyReadHook class_property_read_hook;
    ClassPropertyWriteHook class_property_write_hook;
    ClassMethodCallHook class_method_call_hook;
    ClassStaticPropertyReadHook class_static_property_read_hook;
    ClassStaticPropertyWriteHook class_static_property_write_hook;
    ClassStaticMethodCallHook class_static_method_call_hook;
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
    bool has_bidirectional_switches { };
    bool switch_refreshing { };
    bool started { };
    bool validation_only { };
    bool stopped_by_design { };
    std::optional<std::int64_t> simulator_status;
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
        ContainerObjectId id,
        const ContainerValue& value,
        std::optional<ProcessId> driver = std::nullopt,
        SignalChangeOrigin origin = { });
    [[nodiscard]] bool can_stage_container_alias_deposit(
        ContainerObjectId object) const;
    [[nodiscard]] ProcessId add_process_program_impl(
        ProcessProgramView process,
        std::shared_ptr<const ProcessProgramTemplate> common_program,
        Process* owned_process,
        ProcessInstanceProgram* owned_instance,
        bool executable);
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

    void write_process_register(
        ProcessState& process,
        const RegisterId destination,
        const PackedLogic4& value);

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
        const ProcessState::CallableFrameState& context);

    void snapshot_callable_context(ProcessState& process);

    void restore_callable_context(ProcessState& process)
    {
        if (!process.has_callable_frame_push) {
            return;
        }
        const auto& cold = process.cold();
        if (cold.escaping_callable_contexts.empty()
            && !cold.suspended_callable_context) {
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
    void execute_vhdl_assert_api(
        ProcessState& process, const VhdlAssertApi& operation);
    void execute_vhdl_reflection_api(
        ProcessState& process, const VhdlReflectionApi& operation);
    void execute_vhdl_report(
        ProcessState& process,
        InstructionIndex instruction,
        std::string_view message,
        AssertionSeverity severity,
        const SourceLocation& source,
        bool standalone);
    void handle_external_boundary(
        ProcessState& process,
        InstructionIndex instruction,
        InstructionIndex next_instruction,
        const ExternalSuspension& suspension);
    void request_channel_update(
        ProcessId process, std::uint64_t channel);
    void execute(ProcessId id);
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
    void reserve_systemverilog_update_slots(
        std::span<std::size_t> reserved_slots);
    void cancel_systemverilog_update_slots(
        std::span<const std::size_t> reserved_slots) noexcept;
    [[nodiscard]] SystemVerilogUpdateToken commit_reserved_systemverilog_update(
        std::size_t slot,
        ProcessId process,
        SignalId signal,
        PackedLogic4 value,
        SignalChangeOrigin origin,
        std::optional<std::size_t> offset,
        bool region_internal_output = false,
        std::size_t region_local_component
            = no_systemverilog_update_slot,
        std::uint64_t region_local_generation = 0U) noexcept;
    void attach_prepared_output_ticket(
        const SystemVerilogUpdateToken& token,
        const std::shared_ptr<RegionPreparedOutputBatchState>& batch,
        std::size_t slot) noexcept;
    void attach_prepared_output_group_member(
        const SystemVerilogUpdateToken& token,
        const std::shared_ptr<RegionPreparedOutputBatchState>& batch,
        std::size_t prepared_slot,
        std::size_t dispatch_ordinal) noexcept;
    [[nodiscard]] SchedulerBatchResult execute_prepared_output_group(
        RegionPreparedOutputBatchState& batch,
        Scheduler& scheduler,
        std::span<const std::uint64_t> payloads);
    [[nodiscard]] bool prepare_region_prepared_output_batch(
        std::size_t component,
        const RegionConeActivationKernel& kernel,
        const RegionKernelActivationState& activation,
        std::span<const RegionConeOutputBinding* const> publications,
        RegionPreparedOutputBatchState& batch,
        std::size_t& prepared_publication_count) noexcept;
    [[nodiscard]] bool prepare_region_direct_ready_window(
        const RegionKernelActivationImage& image,
        std::span<const PackedLogic4> boundary_inputs,
        std::span<const std::uint64_t> readiness_mask,
        RegionPreparedOutputBatchState& batch) noexcept;
    enum class PreparedOutputTicketResult : std::uint8_t {
        declined,
        sealed,
    };
    [[nodiscard]] PreparedOutputTicketResult
    try_publish_region_prepared_output(
        RegionPreparedOutputBatchState& batch,
        std::size_t slot,
        ProcessId process,
        SignalId signal,
        PackedLogic4& value,
        SignalChangeOrigin origin,
        bool& publication_committed);
    void release_systemverilog_update(
        const SystemVerilogUpdateToken& token) noexcept;
    void clear_systemverilog_update_pool() noexcept;
    static void dispatch_systemverilog_update(
        Scheduler& scheduler,
        const SystemVerilogUpdateToken& token);
    static void dispatch_region_internal_update(
        Scheduler& scheduler,
        const SystemVerilogUpdateToken& token);
    static void dispatch_region_forwarding_private_output(
        Scheduler& scheduler,
        const RegionForwardingPrivateOutputDispatchToken& token);
    [[nodiscard]] bool publish_region_internal_value(
        std::size_t component,
        std::uint64_t generation,
        ProcessId process,
        SignalId signal,
        const PackedLogic4& value,
        SignalChangeOrigin origin,
        RegionPreparedOutputBatchState* prepared_batch = nullptr,
        std::size_t prepared_slot = no_systemverilog_update_slot,
        bool* role_deferred = nullptr,
        bool* publication_committed = nullptr,
        RegionOutputPublicationKind publication_kind
            = RegionOutputPublicationKind::update,
        bool fanout_pre_reserved = false);
    [[nodiscard]] bool try_publish_region_blocking_output(
        std::size_t component,
        ProcessId process,
        std::size_t output_index,
        const PackedLogic4& value,
        bool& publication_committed,
        std::exception_ptr& failure);
    [[nodiscard]] bool try_flush_region_forwarding_role_journal(
        std::size_t component) noexcept;
    [[nodiscard]] bool try_flush_all_region_forwarding_role_journals()
        noexcept;
    void require_region_forwarding_role_journal_flushed_for_signal(
        SignalId signal);
    void require_all_region_forwarding_role_journals_flushed();
    void retire_forwarding_epoch_after_scheduler_discard() noexcept;
    void discard_scheduler_work() noexcept;
    [[nodiscard]] SchedulerBatchResult execute(
        Scheduler&, std::span<const std::uint64_t> cohort_ids) override;
    [[nodiscard]] bool handle_executor_resume(
        ProcessState& process, const ProcessResumeResult& boundary);

    void report_process_profile();

    void report_update_profile();

    void queue_at(ProcessId id, SimulationTick time);

    void queue_next_delta(
        ProcessId id,
        SignalChangeOrigin origin = { });
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
        SignalChangeOrigin origin = { },
        bool region_value_prepared = false);
    void build_fused_static_cohort_plans();
    void invalidate_fused_static_cohorts() noexcept;
    void invalidate_fused_static_cohorts_for_fork(ProcessId child);
    [[nodiscard]] std::optional<std::size_t>
    try_execute_fused_static_cohort(
        std::span<const std::uint64_t> task_payloads,
        std::size_t& offered_tasks);
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

    void trigger_event(
        SignalId event,
        SignalChangeOrigin origin = { });

    void set_event_identity(
        SignalId signal, std::optional<SignalId> identity);

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
        SignalId signal_id,
        bool region_graph_has_exact_sensitivity_ranges = false) const noexcept;
    /// Runtime dependencies other than the legacy container-alias guard.
    /// Only the RegionGraph's exact-family proof may use this query; legacy
    /// native publication and fused callers must keep the full predicate.
    [[nodiscard]] bool native_signal_has_non_alias_runtime_dependency(
        SignalId signal_id,
        bool region_graph_has_exact_sensitivity_ranges = false) const noexcept;
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
        SignalChangeOrigin origin = { },
        // Only a caller holding a complete prepared A4 write may skip the
        // ordinary prewrite unbind and stored-plane mirror.
        bool authoritative_write_prepared = false);
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

    void force_slice(
        SignalId signal_id, PackedLogic4 value, std::size_t offset);

    void release_slice(
        SignalId signal_id, std::size_t offset, std::size_t width);

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

    void commit(
        SignalId signal_id,
        PackedLogic4 value,
        SignalChangeOrigin origin = { });

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

    void build_owned_driver_composites();
    void demote_owned_driver(SignalId signal_id);
    void demote_all_owned_drivers();
    [[nodiscard]] bool owned_driver_active(SignalId signal_id) const noexcept;
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
    [[nodiscard]] PackedLogic4 owned_driver_value(
        ProcessId process, SignalId signal_id) const;
    [[nodiscard]] OwnedDriverStage stage_owned_driver_slot(
        ProcessId process, const ProcessUpdateSlotView& slot);
    [[nodiscard]] std::optional<PreparedOwnedUpdateSlot>
    prepare_owned_update_slot(const ProcessUpdateSlotBatch& batch) const;
    [[nodiscard]] OwnedDriverStage stage_prepared_owned_update_slot(
        const PreparedOwnedUpdateSlot& prepared,
        const ProcessUpdateSlotView& slot);
    [[nodiscard]] OwnedDriverStage stage_owned_driver_slot_impl(
        ProcessId process,
        const ProcessUpdateSlotView& slot,
        const PreparedOwnedUpdateSlot* prepared);
    [[nodiscard]] bool stage_owned_driver_pending(PendingUpdate& pending);
    void commit_owned_driver(SignalId signal_id);

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

    [[nodiscard]] PackedLogic4 resolved_local_driver_value(
        SignalId signal_id,
        const ForcedDriverMapView* override_maps = nullptr) const;

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
        std::shared_ptr<const std::vector<Process::DriverRegion>> scalar_regions);

    void set_driver(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value,
        bool preserve_wide_authority = false);
    [[nodiscard]] bool try_publish_wide_owner_raw(
        ProcessId process,
        SignalId signal_id,
        const PackedLogic4& value);

    void commit_driver(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 value,
        std::optional<SignalChangeOrigin> origin = std::nullopt,
        bool route_module_path = true,
        bool allow_wide_single_owner_commit = true);
    void note_unresolved_update_owner(
        SignalId signal,
        std::optional<ProcessId> process,
        bool whole_update);
    void clear_unresolved_update_owner_provenance() noexcept;
    [[nodiscard]] bool try_commit_wide_unresolved_owner_alias(
        ProcessId process,
        SignalId signal,
        const PackedLogic4& value,
        SignalChangeOrigin origin);
    [[nodiscard]] bool try_commit_wide_single_owner(
        ProcessId process,
        SignalId signal_id,
        PackedLogic4 value,
        SignalChangeOrigin origin,
        // True only after the generic Update raw-driver phase mirrored this
        // owner's new value into the A4 owner plane.
        bool raw_owner_already_mirrored);
    [[nodiscard]] bool can_try_wide_single_owner_commit(
        ProcessId process,
        SignalId signal_id);
    [[nodiscard]] bool can_try_wide_unresolved_owner_alias(
        ProcessId process,
        SignalId signal_id,
        SignalChangeOrigin origin);
    struct WideDisjointOwnerCommitContext {
        ProcessId process { };
        SignalId signal { };
        std::size_t component { };
        RegionAuthoritativeComponentState* state { };
    };
    [[nodiscard]] bool prepare_wide_disjoint_owner_commit_context(
        ProcessId process,
        SignalId signal_id,
        WideDisjointOwnerCommitContext& context);
    [[nodiscard]] bool try_publish_wide_owner_slice_raw(
        ProcessId process,
        SignalId signal_id,
        const PackedLogic4& slice_value,
        std::size_t offset,
        const WideDisjointOwnerCommitContext& context);
    [[nodiscard]] bool try_commit_wide_disjoint_value(
        SignalId signal_id,
        PackedLogic4 value,
        SignalChangeOrigin origin,
        const WideDisjointOwnerCommitContext* context = nullptr);
    [[nodiscard]] bool can_try_wide_disjoint_signal_commit(
        SignalId signal_id,
        RegionAuthoritativeComponentState** eligible_state = nullptr);
    [[nodiscard]] bool can_try_wide_disjoint_owner_commit(
        ProcessId process,
        SignalId signal_id);

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

    [[nodiscard]] bool native_boundary_slice_matches_alias_family(
        SignalId proxy,
        std::uint32_t offset,
        std::uint32_t width,
        SignalId leaf) const noexcept;

    [[nodiscard]] SignalChangeOrigin capture_signal_change_origin(
        ProcessId process,
        SignalUpdateDomain update_domain = SignalUpdateDomain::generic) const;

    void schedule_systemverilog_update(
        ProcessId process,
        SignalId signal,
        PackedLogic4 value,
        std::optional<std::size_t> offset,
        SignalUpdateDomain update_domain,
        SimulationTick delay = 0);

    [[nodiscard]] bool can_stage_disjoint_owner_group(
        SignalId signal,
        const std::vector<PendingDriverCommit>& staged);
    [[nodiscard]] bool prepare_disjoint_owner_group_scratch(
        SignalId signal, std::size_t owner_capacity) noexcept;
    [[nodiscard]] bool disjoint_owner_group_scratch_ready(
        SignalId signal, std::size_t owner_count) const noexcept;
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
        SignalUpdateDomain domain = SignalUpdateDomain::generic);

    void stage_update(
        const SignalId signal_id,
        PackedLogic4 staged_value);

    void stage_update(
        const ProcessId process,
        const SignalId signal_id,
        PackedLogic4 staged_value,
        SignalUpdateDomain domain = SignalUpdateDomain::generic);

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
        SignalUpdateDomain domain = SignalUpdateDomain::generic);

    void stage_update_words(
        ProcessId process,
        std::span<const ProcessUpdateWord> updates);
    void stage_validated_update_words(
        ProcessId process,
        std::span<const ProcessUpdateWord> updates);
    [[nodiscard]] bool stage_validated_update_slot_batches(
        std::span<const ProcessUpdateSlotBatch> batches);
    template <typename Batches>
    [[nodiscard]] bool stage_validated_update_slot_batches_impl(
        Batches& batches,
        bool schedule_commit = true,
        bool* staged_any_out = nullptr,
        bool count_profile_call = true);
    [[nodiscard]] bool stage_validated_logic9_update_batch(
        const ProcessLogic9UpdateBatch& batch);
    [[nodiscard]] bool stage_validated_logic9_update_batches(
        std::span<const ProcessLogic9UpdateBatch> batches);

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
