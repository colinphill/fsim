// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "runtime_fused_staging_failure_support.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using fsim::tests::runtime::staging_failure_support::arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::
    allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::end_allocation_count;
using fsim::tests::runtime::staging_failure_support::set_allocation_failure_observer;
using fsim::tests::runtime::staging_failure_support::require;

static_assert(std::is_nothrow_move_constructible_v<PackedLogic4>);
static_assert(std::is_nothrow_move_assignable_v<PackedLogic4>);
static_assert(std::is_nothrow_move_constructible_v<
    AuthoritativeSignalPlanes::FrontierWriteLease>);
static_assert(std::is_nothrow_move_assignable_v<
    AuthoritativeSignalPlanes::FrontierWriteLease>);
static_assert(!std::is_copy_constructible_v<
    AuthoritativeSignalPlanes::FrontierWriteLease>);

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (value == nullptr) {
            unset();
            return;
        }
        if (!set(value)) {
            throw std::runtime_error {
                "failed to set wide A4 test environment"
            };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
            static_cast<void>(set(previous_.c_str()));
        } else {
            unset();
        }
    }

private:
    bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value) == 0;
#else
        return ::setenv(name_.c_str(), value, 1) == 0;
#endif
    }

    void unset() const noexcept
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name_.c_str(), ""));
#else
        static_cast<void>(::unsetenv(name_.c_str()));
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

class ScheduledWideFailureExecutor final : public ProcessExecutor {
public:
    ScheduledWideFailureExecutor(const SignalId input,
        const SignalId output,
        const std::size_t failure_index,
        std::size_t& resumes,
        ProcessExecutorProgramBinding access_binding)
        : input_ { input }
        , output_ { output }
        , failure_index_ { failure_index }
        , resumes_ { &resumes }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++*resumes_;
        context.write_update_in_domain(output_, context.read_signal(input_),
            SignalUpdateDomain::systemverilog_active);
        if (!failure_armed_) {
            // The queued write is already prepared. The next allocation is
            // expected to be the A4 role-clone preflight; the test confirms
            // that it is caught by wide commit and falls back successfully.
            arm_allocation_failure(failure_index_);
            failure_armed_ = true;
        }
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    SignalId input_ { };
    SignalId output_ { };
    std::size_t failure_index_ { };
    std::size_t* resumes_ { };
    ProcessExecutorProgramBinding access_binding_;
    bool failure_armed_ { };
};

struct NativeWordA4PublicationState {
    std::size_t resumes { };
    bool capture_before_next_write { };
    bool captured { };
    bool inject_failure_after_next_write { };
    bool failure_armed { };
    std::array<PackedLogic4, 4U> retained_values;
    std::array<PackedLogic4PlaneReadLease, 4U> retained_leases;
};

class ScheduledNativeWordA4PublicationExecutor final : public ProcessExecutor {
public:
    ScheduledNativeWordA4PublicationExecutor(Interpreter& interpreter,
        const SignalId input,
        const SignalId output,
        NativeWordA4PublicationState& state,
        ProcessExecutorProgramBinding access_binding)
        : interpreter_ { &interpreter }
        , input_ { input }
        , output_ { output }
        , state_ { &state }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++state_->resumes;
        if (state_->capture_before_next_write) {
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(
                    *interpreter_);
            auto* const component_state
                = implementation.region_authoritative_state_for_signal(
                    output_);
            require(component_state != nullptr
                    && component_state->values().packed_slots_bound()
                    && component_state->values()
                        .packed_signal_slots_bound(output_)
                    && component_state->values()
                        .packed_owner_slot_bound(output_, 0U),
                "native word publication still has the bound A4 roles before commit");
            require(!implementation.region_recertification_pending
                    && !implementation.region_recertification_requires_snapshot
                    && !implementation.region_authoritative_recertification_waiting,
                "quiet-point input update leaves the A4 fast path certified immediately before commit");
            state_->retained_values
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    *interpreter_, output_, 0U);
            state_->retained_leases = {
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::current),
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::previous),
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::stored),
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::owner, 0U),
            };
            state_->captured = std::ranges::all_of(
                state_->retained_leases,
                [](const auto& lease) { return static_cast<bool>(lease); });
            state_->capture_before_next_write = false;
        }

        const ProcessUpdateWord update {
            output_, context.read_signal_word(input_), 0U, false
        };
        context.write_validated_update_words(std::span { &update, 1U });
        if (state_->inject_failure_after_next_write
            && !state_->failure_armed && state_->resumes > 1U) {
            // The ordinary Update entry is in place. Fail the retained-role
            // clone preflight so production must demote and materialize the
            // exact already-committed raw value once.
            arm_allocation_failure(0U);
            state_->failure_armed = true;
        }
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    Interpreter* interpreter_ { };
    SignalId input_ { };
    SignalId output_ { };
    NativeWordA4PublicationState* state_ { };
    ProcessExecutorProgramBinding access_binding_;
};

struct NativeLogic9A4PublicationState {
    std::size_t resumes { };
    bool capture_before_next_write { };
    bool captured { };
    bool batch_consumed { };
    bool inject_failure_after_next_write { };
    bool failure_armed { };
    std::array<PackedLogic4, 4U> retained_values;
    std::array<PackedLogic4PlaneReadLease, 4U> retained_leases;
};

class ScheduledNativeLogic9A4PublicationExecutor final
    : public ProcessExecutor {
public:
    ScheduledNativeLogic9A4PublicationExecutor(Interpreter& interpreter,
        const SignalId input,
        const SignalId output,
        const ProcessId process,
        NativeLogic9A4PublicationState& state,
        ProcessExecutorProgramBinding access_binding)
        : interpreter_ { &interpreter }
        , input_ { input }
        , output_ { output }
        , process_ { process }
        , state_ { &state }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++state_->resumes;
        if (state_->capture_before_next_write) {
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(
                    *interpreter_);
            auto* const component_state
                = implementation.region_authoritative_state_for_signal(
                    output_);
            require(component_state != nullptr
                    && component_state->values().packed_slots_bound()
                    && component_state->values()
                        .packed_signal_slots_bound(output_)
                    && component_state->values()
                        .packed_owner_slot_bound(output_, process_),
                "Logic9 publication retains bound A4 roles before commit");
            require(!implementation.region_recertification_pending
                    && !implementation.region_recertification_requires_snapshot
                    && !implementation.region_authoritative_recertification_waiting,
                "Logic9 input scheduling leaves the A4 route certified before commit");
            state_->retained_values
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    *interpreter_, output_, process_);
            state_->retained_leases = {
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::current),
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::previous),
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::stored),
                component_state->values().plane_read_lease(
                    output_, PackedPlaneRole::owner, process_),
            };
            state_->captured = std::ranges::all_of(
                state_->retained_leases,
                [](const auto& lease) { return static_cast<bool>(lease); });
            state_->capture_before_next_write = false;
        }

        auto value = context.read_signal_logic9_word(input_);
        auto mask = value.width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << value.width) - UINT64_C(1);
        const ProcessLogic9UpdateSlotView slot {
            output_, static_cast<std::uint32_t>(value.width),
            value.planes.data(), &mask
        };
        const ProcessLogic9UpdateBatch batch {
            process_, std::span { &slot, 1U }
        };
        state_->batch_consumed
            = context.write_validated_logic9_update_batch(batch);
        if (state_->batch_consumed) {
            require(mask == 0U,
                "consumed Logic9 A4 write clears its complete mask");
        } else {
            context.write_update(
                output_, PackedLogic4::from_logic9_word(value));
        }
        if (state_->inject_failure_after_next_write
            && !state_->failure_armed && state_->resumes > 1U) {
            // The Logic9 update is already staged. The next allocation must
            // be the A4 role clone preflight, not ordinary staging work.
            arm_allocation_failure(0U);
            state_->failure_armed = true;
        }
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    Interpreter* interpreter_ { };
    SignalId input_ { };
    SignalId output_ { };
    ProcessId process_ { };
    NativeLogic9A4PublicationState* state_ { };
    ProcessExecutorProgramBinding access_binding_;
};

class ScheduledVhdlProjectedFailureExecutor final : public ProcessExecutor {
public:
    ScheduledVhdlProjectedFailureExecutor(const SignalId input,
        const SignalId output,
        const std::size_t failure_index,
        std::size_t& resumes,
        ProcessExecutorProgramBinding access_binding)
        : input_ { input }
        , output_ { output }
        , failure_index_ { failure_index }
        , resumes_ { &resumes }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++*resumes_;
        context.write_projected(output_, context.read_signal(input_), 0U, 0U,
            ProjectedDelayMode::inertial);
        if (!failure_armed_ && *resumes_ == 2U) {
            // Wide zero-delay projected writes enter the ordinary generic
            // update queue synchronously. The first ordinary U-to-U write
            // warms its generic queue storage; arm the measured change after
            // its queue entry exists so failure reaches bound-role preflight.
            arm_allocation_failure(failure_index_);
            failure_armed_ = true;
        }
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    SignalId input_ { };
    SignalId output_ { };
    std::size_t failure_index_ { };
    std::size_t* resumes_ { };
    ProcessExecutorProgramBinding access_binding_;
    bool failure_armed_ { };
};

struct DisjointSliceExecutorState {
    std::array<std::size_t, 8U> resumes { };
    std::vector<ProcessId> resume_order;
    std::size_t staged_writes { };
    std::optional<std::size_t> failure_index;
    bool failure_armed { };
    bool generic_updates { };
};

bool lease_matches(const PackedLogic4PlaneReadLease& lease,
    const PackedLogic4& expected);

struct FallbackAllocationFailureProbe {
    std::size_t successful_allocations_after_preflight { };
    Interpreter* interpreter { };
    SignalId target { };
    const PackedLogic4* expected_current { };
    const PackedLogic4* expected_owner0 { };
    const PackedLogic4* expected_owner1 { };
    const PackedLogic4* initial_current { };
    const PackedLogic4* initial_owner0 { };
    const PackedLogic4* initial_owner1 { };
    const std::array<PackedLogic4PlaneReadLease, 5U>* retained_leases { };
    std::uint64_t before_revision { };
    std::optional<std::pair<SimulationTick, std::uint64_t>> before_event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> before_transaction;
    bool preflight_failure_observed { };
    bool fallback_failure_observed { };
    bool full_publication_at_fallback_failure { };
    bool unchanged_at_fallback_failure { };

    static void observe(void* const context) noexcept
    {
        auto& probe = *static_cast<FallbackAllocationFailureProbe*>(context);
        if (!probe.preflight_failure_observed) {
            probe.preflight_failure_observed = true;
            arm_allocation_failure(
                probe.successful_allocations_after_preflight);
            return;
        }
        probe.fallback_failure_observed = true;
        if (probe.interpreter == nullptr
            || probe.expected_current == nullptr
            || probe.expected_owner0 == nullptr
            || probe.expected_owner1 == nullptr
            || probe.initial_current == nullptr
            || probe.initial_owner0 == nullptr
            || probe.initial_owner1 == nullptr
            || probe.retained_leases == nullptr) {
            return;
        }
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(
                *probe.interpreter);
        const auto* const owner0
            = implementation.driver_values[probe.target].find(0U);
        const auto* const owner1
            = implementation.driver_values[probe.target].find(1U);
        const bool owners_new = owner0 != nullptr && owner1 != nullptr
            && owner0->value == *probe.expected_owner0
            && owner1->value == *probe.expected_owner1;
        const bool owners_old = owner0 != nullptr && owner1 != nullptr
            && owner0->value == *probe.initial_owner0
            && owner1->value == *probe.initial_owner1;
        const auto& current
            = implementation.signals[probe.target].initial_value;
        const auto& previous = implementation.signal_last_values[probe.target];
        const auto& stored = implementation.driven_values[probe.target];
        const bool roles_new = current == *probe.expected_current
            && previous == *probe.initial_current
            && stored == *probe.expected_current;
        const bool roles_old = current == *probe.initial_current
            && previous == *probe.initial_current
            && stored == *probe.initial_current;
        const bool metadata_new
            = implementation.signal_value_revisions[probe.target]
                    == probe.before_revision + 1U
            && implementation.signal_events[probe.target]
                != probe.before_event
            && implementation.signal_transactions[probe.target]
                != probe.before_transaction;
        const bool metadata_old
            = implementation.signal_value_revisions[probe.target]
                    == probe.before_revision
            && implementation.signal_events[probe.target]
                == probe.before_event
            && implementation.signal_transactions[probe.target]
                == probe.before_transaction;
        const bool leases_preserved
            = lease_matches((*probe.retained_leases)[0U],
                  *probe.initial_current)
            && lease_matches((*probe.retained_leases)[1U],
                  *probe.initial_current)
            && lease_matches((*probe.retained_leases)[2U],
                  *probe.initial_current)
            && lease_matches((*probe.retained_leases)[3U],
                  *probe.initial_owner0)
            && lease_matches((*probe.retained_leases)[4U],
                  *probe.initial_owner1);
        probe.full_publication_at_fallback_failure = owners_new && roles_new
            && metadata_new && leases_preserved;
        probe.unchanged_at_fallback_failure = owners_old && roles_old
            && metadata_old && leases_preserved;
    }
};

class ScheduledDisjointSliceExecutor final : public ProcessExecutor {
public:
    ScheduledDisjointSliceExecutor(const ProcessId process,
        const SignalId input,
        const SignalId output,
        const std::uint32_t offset,
        DisjointSliceExecutorState& state,
        ProcessExecutorProgramBinding access_binding)
        : process_ { process }
        , input_ { input }
        , output_ { output }
        , offset_ { offset }
        , state_ { &state }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++state_->resumes.at(process_);
        state_->resume_order.push_back(process_);
        auto value = context.read_signal(input_);
        if (state_->generic_updates) {
            context.write_update_slice(
                output_, std::move(value), offset_);
        } else {
            context.write_update_slice_in_domain(output_, std::move(value),
                offset_, SignalUpdateDomain::systemverilog_active);
        }
        ++state_->staged_writes;
        if (state_->failure_index && !state_->failure_armed
            && state_->staged_writes == 2U) {
            // Inject only after both owner updates have reached the ordinary
            // scheduler queue, so the checked fallback can preserve the
            // established raw-owner prefix and later value phase.
            arm_allocation_failure(*state_->failure_index);
            state_->failure_armed = true;
        }
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    ProcessId process_ { };
    SignalId input_ { };
    SignalId output_ { };
    std::uint32_t offset_ { };
    DisjointSliceExecutorState* state_ { };
    ProcessExecutorProgramBinding access_binding_;
};

Process whole_writer(const ProcessId id, const SignalId output)
{
    Process process;
    process.id = id;
    process.name = "wide_authoritative_slot_writer_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.static_sensitivity = { { 0U, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, 0U },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

PackedLogic4 value_for(const std::size_t width,
    const ValueKind kind,
    const std::size_t phase)
{
    static constexpr std::string_view logic4_digits { "01XZ" };
    static constexpr std::string_view logic9_digits { "UX01ZWLH-" };
    const auto digits = kind == ValueKind::logic9
        ? logic9_digits : logic4_digits;
    std::string text;
    text.reserve(width);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        text.push_back(digits[(bit + phase * 3U) % digits.size()]);
    }
    // Keep the broad repeating pattern above so every Logic9 code is exercised,
    // and encode the phase in low bits so transition pairs cannot alias when
    // the width is large enough for the pattern to repeat.
    auto phase_code = phase;
    const auto distinct_phase_bits = std::min<std::size_t>(
        width, kind == ValueKind::logic9 ? 2U : 3U);
    for (std::size_t bit = 0U; bit < distinct_phase_bits; ++bit) {
        text[width - bit - 1U] = digits[phase_code % digits.size()];
        phase_code /= digits.size();
    }
    return kind == ValueKind::logic9
        ? PackedLogic4::from_logic9_msb_string(text)
        : PackedLogic4::from_msb_string(text);
}

Process disjoint_slice_writer(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const std::uint32_t offset,
    const std::uint32_t width,
    const ValueKind kind,
    const bool generic_update = false)
{
    Process process;
    process.id = id;
    process.name = "wide_disjoint_slice_writer_" + std::to_string(id);
    process.scheduling_domain = generic_update
        ? ProcessSchedulingDomain::generic
        : ProcessSchedulingDomain::systemverilog;
    process.initialize = false;
    process.register_count = 1U;
    process.register_value_kinds = { kind };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, offset, width, false } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdateSlice { output, 0U, offset,
            generic_update ? SignalUpdateDomain::generic
                           : SignalUpdateDomain::systemverilog_active },
    };
    process.operations.emplace_back(WaitSensitivity { });
    process.operations.emplace_back(Jump { 0U });
    return process;
}

Process generic_update_slice_writer(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const std::uint32_t offset,
    const std::uint32_t driver_width,
    const ProcessSchedulingDomain scheduling_domain)
{
    Process process;
    process.id = id;
    process.name = "generic_update_slice_writer_" + std::to_string(id);
    process.scheduling_domain = scheduling_domain;
    process.initialize = false;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, offset, driver_width, false } };
    const auto update_domain
        = scheduling_domain == ProcessSchedulingDomain::generic
        ? SignalUpdateDomain::generic
        : SignalUpdateDomain::systemverilog_active;
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdateSlice { output, 0U, offset, update_domain },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process generic_triggered_update_slice_writer(const ProcessId id,
    const SignalId trigger,
    const SignalId input,
    const SignalId output,
    const std::uint32_t offset,
    const std::uint32_t driver_width)
{
    Process process;
    process.id = id;
    process.name = "generic_triggered_slice_writer_"
        + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = 2U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4 };
    process.static_sensitivity = { { trigger, EdgeKind::any } };
    process.driver_regions = { { output, offset, driver_width, false } };
    process.operations = {
        ReadSignal { 1U, trigger },
        ReadSignal { 0U, input },
        WriteUpdateSlice {
            output, 0U, offset, SignalUpdateDomain::generic },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process generic_update_copy_writer(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const std::uint32_t width)
{
    Process process;
    process.id = id;
    process.name = "generic_update_copy_writer_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, 0U, width, true } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { output, 0U, SignalUpdateDomain::generic },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

struct GenericGroupPublicationWitness {
    std::size_t reader_callbacks { };
    bool capture_next_revision { };
    bool captured_revision { };
    std::uint64_t reader_entry_revision { };
};

class GenericGroupPublicationWitnessExecutor final : public ProcessExecutor {
public:
    GenericGroupPublicationWitnessExecutor(Interpreter& interpreter,
        const SignalId tracked_signal,
        const SignalId input,
        const SignalId output,
        GenericGroupPublicationWitness& witness,
        ProcessExecutorProgramBinding access_binding)
        : interpreter_ { &interpreter }
        , tracked_signal_ { tracked_signal }
        , input_ { input }
        , output_ { output }
        , witness_ { &witness }
        , access_binding_ { std::move(access_binding) }
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return false;
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override
    {
        ++witness_->reader_callbacks;
        if (witness_->capture_next_revision) {
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(
                    *interpreter_);
            const auto* const state
                = implementation.region_authoritative_state_for_signal(
                    tracked_signal_);
            if (state == nullptr) {
                throw std::logic_error {
                    "generic group witness lost authoritative target state"
                };
            }
            // The next publication to this component comes from the
            // selected target owners; capture before the reader writes its
            // downstream signal in the same component.
            witness_->reader_entry_revision = state->values().revision();
            witness_->captured_revision = true;
            witness_->capture_next_revision = false;
        }

        auto value = context.read_signal(input_);
        context.write_update(output_, std::move(value));
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    Interpreter* interpreter_ { };
    SignalId tracked_signal_ { };
    SignalId input_ { };
    SignalId output_ { };
    GenericGroupPublicationWitness* witness_ { };
    ProcessExecutorProgramBinding access_binding_;
};

class OpaqueAccessWaitExecutor final : public ProcessExecutor {
public:
    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&, InstructionIndex) override
    {
        throw std::logic_error {
            "opaque ownership fixture unexpectedly executed"
        };
    }
};

Process disjoint_slice_reader(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const ValueKind input_kind,
    const ProcessSchedulingDomain scheduling_domain
        = ProcessSchedulingDomain::systemverilog)
{
    Process process;
    process.id = id;
    process.name = "wide_disjoint_slice_reader";
    process.scheduling_domain = scheduling_domain;
    process.initialize = false;
    process.register_count = 2U;
    process.register_value_kinds = { input_kind, ValueKind::logic4 };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 1U, true } };
    process.operations = {
        ReadSignal { 0U, input },
        Reduction { ReductionOperator::bit_or, 1U, 0U },
        WriteUpdate { output, 1U,
            scheduling_domain == ProcessSchedulingDomain::generic
                ? SignalUpdateDomain::generic
                : SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process disjoint_component_copy_writer(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const ValueKind kind,
    const ProcessSchedulingDomain scheduling_domain
        = ProcessSchedulingDomain::systemverilog)
{
    Process process;
    process.id = id;
    process.name = "wide_disjoint_component_copy_writer";
    process.scheduling_domain = scheduling_domain;
    process.initialize = false;
    process.register_count = 1U;
    process.register_value_kinds = { kind };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { output, 0U,
            scheduling_domain == ProcessSchedulingDomain::generic
                ? SignalUpdateDomain::generic
                : SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

PackedLogic4 all_z_value(const std::uint32_t width, const ValueKind kind)
{
    if (kind == ValueKind::logic9) {
        return PackedLogic4::from_logic9_msb_string(
            std::string(width, 'Z'));
    }
    return PackedLogic4 { width, Logic4::z };
}

void insert_range(PackedLogic4& destination,
    const PackedLogic4& source,
    const std::uint32_t offset,
    const ValueKind kind)
{
    require(offset <= destination.width()
            && source.width() <= destination.width() - offset,
        "disjoint fixture range fits its complete signal");
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
        const auto target_bit = static_cast<std::size_t>(offset) + bit;
        if (kind == ValueKind::logic9) {
            destination.set_logic9(target_bit, source.get_logic9(bit));
        } else {
            destination.set(target_bit, source.get(bit));
        }
    }
}

RegionGraph graph_for(const std::uint32_t width, const ValueKind kind)
{
    const std::array descriptors {
        RegionSignalDescriptor { width },
        RegionSignalDescriptor { width },
        RegionSignalDescriptor { width },
    };
    auto signals = descriptors;
    signals[0U].value_kind = kind;
    signals[1U].value_kind = kind;
    signals[2U].value_kind = kind;
    const auto first_process = whole_writer(0U, 1U);
    const auto second_process = whole_writer(1U, 2U);
    const std::array<const Process*, 2U> programs {
        &first_process, &second_process
    };
    return RegionGraph::build(programs, signals);
}

bool lease_matches(const PackedLogic4PlaneReadLease& lease,
    const PackedLogic4& expected)
{
    if (!lease || lease.width() != expected.width()
        || lease.is_logic9() != expected.is_logic9()) {
        return false;
    }
    const auto count = expected.is_logic9() ? 4U : 2U;
    for (std::size_t plane = 0U; plane < count; ++plane) {
        const auto expected_words = plane == 0U ? expected.aval_words()
            : plane == 1U ? expected.bval_words()
                          : expected.logic9_plane_words(plane);
        if (!std::ranges::equal(
                lease.plane_words(plane), expected_words)) {
            return false;
        }
    }
    return true;
}

void exercise_unresolved_stored_owner_lease(const ValueKind kind)
{
    constexpr std::uint32_t width = 129U;
    constexpr SignalId signal = 2U;
    constexpr ProcessId owner = 1U;
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 1U> component_signals { signal };
    const std::array<SignalId, 0U> no_partial_certificates { };
    const std::array<SignalId, 1U> stored_owner_certificates { signal };
    const auto layout = SignalDriverLayout::build(graph,
        component_signals, no_partial_certificates,
        stored_owner_certificates);
    AuthoritativeSignalPlanes values {
        layout, PackedSlotBindingPolicy::experimental_wide
    };

    const auto old_current = value_for(width, kind, 1U);
    const auto old_previous = value_for(width, kind, 2U);
    const auto old_stored = value_for(width, kind, 3U);
    const auto next_current = value_for(width, kind, 4U);
    const auto next_stored = value_for(width, kind, 5U);
    values.seed_signal(signal, old_current, old_previous, old_stored);
    if (kind == ValueKind::logic9) {
        const auto all_u
            = PackedLogic4::from_logic9_msb_string(std::string(width, 'U'));
        const auto all_z
            = PackedLogic4::from_logic9_msb_string(std::string(width, 'Z'));
        require(std::ranges::equal(all_u.logic9_plane_words(0U),
                    all_z.logic9_plane_words(0U))
                && std::ranges::equal(all_u.logic9_plane_words(1U),
                    all_z.logic9_plane_words(1U))
                && !std::ranges::equal(all_u.logic9_plane_words(2U),
                    all_z.logic9_plane_words(2U))
                && std::ranges::equal(all_u.logic9_plane_words(3U),
                    all_z.logic9_plane_words(3U)),
            "Logic9 alias comparison fixture differs only in raw plane two");
        AuthoritativeSignalPlanes upper_plane_probe {
            layout, PackedSlotBindingPolicy::experimental_wide
        };
        upper_plane_probe.seed_signal(signal, all_u, all_u, all_u);
        bool rejected_upper_plane_mismatch { };
        try {
            upper_plane_probe.seed_owner(signal, owner, all_z);
        } catch (const std::invalid_argument&) {
            rejected_upper_plane_mismatch = true;
        }
        require(rejected_upper_plane_mismatch,
            "a stored-owner alias compares all four Logic9 planes");
        upper_plane_probe.seed_owner(signal, owner, all_u);
    }
    values.seed_owner(signal, owner, old_stored);
    auto current = old_current;
    auto previous = old_previous;
    auto stored = old_stored;
    values.stage_packed_signal_slots(signal, current, previous, stored);

    auto attempted_owner_slot = old_stored;
    bool rejected_duplicate_binding { };
    try {
        values.stage_packed_owner_slot(signal, owner, attempted_owner_slot);
    } catch (const std::invalid_argument&) {
        rejected_duplicate_binding = true;
    }
    require(rejected_duplicate_binding && values.packed_slot_count() == 3U,
        "a stored-owner alias rejects an extra physical owner binding");

    values.stage_packed_owner_stored_alias(signal, owner);
    require(values.bind_packed_slots() == 3U
            && values.packed_slot_count() == 3U
            && values.packed_owner_slot_bound(signal, owner)
            && values.owner_value(signal, owner) == values.stored(signal),
        "the unresolved owner is a logical alias over three physical roles");
    const auto owner_lease = values.plane_read_lease(
        signal, PackedPlaneRole::owner, owner);
    require(static_cast<bool>(owner_lease)
            && lease_matches(owner_lease, old_stored),
        "the logical owner lease pins the stored plane value");

    auto mutation = values.prepare_value_change(
        signal, next_current, next_stored);
    require(values.begin_prepared_publication(mutation),
        "a pinned stored-owner alias prepares an atomic value change");
    values.publish(std::move(mutation));
    require(values.current(signal) == next_current
            && values.previous(signal) == old_current
            && values.stored(signal) == next_stored
            && values.owner_value(signal, owner) == next_stored
            && values.packed_owner_slot_bound(signal, owner)
            && values.packed_slot_count() == 3U
            && lease_matches(owner_lease, old_stored),
        "current/stored publication advances the alias and preserves its old lease");

    require(values.unbind_packed_slots() == 3U
            && !values.packed_slots_bound()
            && !values.packed_owner_slot_bound(signal, owner)
            && lease_matches(owner_lease, old_stored),
        "demotion leaves the previously acquired owner lease immutable");
}

void exercise_width(const std::uint32_t width, const ValueKind kind)
{
    const auto graph = graph_for(width, kind);
    // Include two preceding signals. Visible planes for signal 2 begin after
    // both; its owner block begins after signal 1's owner. Logic9 planes use
    // the same distinct nonzero offsets as their corresponding values.
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    auto values = std::make_unique<AuthoritativeSignalPlanes>(
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide);
    const auto old_current = value_for(width, kind, 0U);
    const auto old_previous = value_for(width, kind, 1U);
    const auto old_stored = value_for(width, kind, 2U);
    const auto old_owner = value_for(width, kind, 3U);
    const auto new_current = value_for(width, kind, 4U);
    const auto new_stored = value_for(width, kind, 5U);
    const auto new_owner = value_for(width, kind, 6U);
    require(old_current != new_current,
        "first wide publication fixture changes current");

    values->seed_signal(2U, old_current, old_previous, old_stored);
    values->seed_owner(2U, 1U, old_owner);
    auto current = old_current;
    auto previous = old_previous;
    auto stored = old_stored;
    auto owner = old_owner;
    values->stage_packed_signal_slots(2U, current, previous, stored);
    values->stage_packed_owner_slot(2U, 1U, owner);
    require(values->bind_packed_slots() == 4U,
        "wide versioned A4 slots bind all four roles");
    require(values->requires_prewrite_unbind(),
        "wide versioned bindings require the ordinary-writer detach barrier");

    begin_allocation_count();
    std::array<std::optional<PackedLogic4>, 4U> retained {
        current, previous, stored, owner
    };
    std::optional<PackedLogic4> moved_snapshot;
    moved_snapshot.emplace(std::move(current));
    std::array<PackedLogic4PlaneReadLease, 4U> leases {
        values->plane_read_lease(2U, PackedPlaneRole::current),
        values->plane_read_lease(2U, PackedPlaneRole::previous),
        values->plane_read_lease(2U, PackedPlaneRole::stored),
        values->plane_read_lease(2U, PackedPlaneRole::owner, 1U),
    };
    const auto snapshot_allocations = end_allocation_count();
    require(snapshot_allocations == 0U,
        "wide value and lease snapshots acquire existing blocks without allocation");
    require(std::ranges::all_of(leases, [](const auto& lease) {
                return static_cast<bool>(lease);
            }),
        "wide versioned A4 roles issue owning read leases");

    auto mutation = values->prepare_owner_change(
        2U, 1U, new_owner, new_current, new_stored);
    begin_allocation_count();
    values->publish(std::move(mutation));
    const auto publish_allocations = end_allocation_count();
    require(publish_allocations == 0U,
        "wide prepared publication performs no allocation after preflight");
    require(values->current(2U) == new_current
            && values->previous(2U) == old_current
            && values->stored(2U) == new_stored
            && values->owner_value(2U, 1U) == new_owner,
        "pinned publication replaces all four live role versions coherently");
    require(retained[0U] == old_current
            && retained[1U] == old_previous
            && retained[2U] == old_stored
            && retained[3U] == old_owner
            && lease_matches(leases[0U], old_current)
            && lease_matches(leases[1U], old_previous)
            && lease_matches(leases[2U], old_stored)
            && lease_matches(leases[3U], old_owner),
        "retained values and read leases keep their exact old four-plane versions");
    auto detached_copy = *retained[0U];
    if (kind == ValueKind::logic9) {
        detached_copy.set_logic9(0U,
            old_current.get_logic9(0U) == Logic9::one
                ? Logic9::zero : Logic9::one);
    } else {
        detached_copy.set(0U,
            old_current.get(0U) == Logic4::one
                ? Logic4::zero : Logic4::one);
    }
    require(detached_copy != old_current && retained[0U] == old_current
            && *moved_snapshot == old_current && current == new_current,
        "copy-on-write mutation detaches from both snapshots and live slots");

    const std::array<PackedLogic4, 5U> concurrent_versions {
        new_current,
        value_for(width, kind, 10U),
        value_for(width, kind, 11U),
        value_for(width, kind, 12U),
        value_for(width, kind, 13U),
    };
    std::atomic<bool> stop_readers { false };
    std::atomic<bool> reader_mismatch { false };
    std::atomic<bool> live_snapshot_mismatch { false };
    std::atomic<std::size_t> live_snapshot_reads { };
    const auto reader = [&] {
        while (!stop_readers.load(std::memory_order_acquire)) {
            if (retained[0U] != old_current) {
                reader_mismatch.store(true, std::memory_order_release);
                return;
            }
        }
    };
    const auto live_slot_reader = [&] {
        while (!stop_readers.load(std::memory_order_acquire)) {
            const PackedLogic4 snapshot { current };
            const bool matched = std::ranges::any_of(concurrent_versions,
                [&snapshot](const PackedLogic4& expected) {
                    return snapshot == expected;
                });
            if (!matched) {
                live_snapshot_mismatch.store(true,
                    std::memory_order_release);
                return;
            }
            live_snapshot_reads.fetch_add(1U, std::memory_order_relaxed);
        }
    };
    std::thread first_reader { reader };
    std::thread second_reader { reader };
    std::thread live_reader { live_slot_reader };
    struct ReaderJoinGuard {
        std::atomic<bool>& stop;
        std::thread& first;
        std::thread& second;
        std::thread& live;

        ~ReaderJoinGuard()
        {
            stop.store(true, std::memory_order_release);
            for (auto* thread : { &first, &second, &live }) {
                if (thread->joinable())
                    thread->join();
            }
        }
    } reader_join_guard { stop_readers, first_reader,
        second_reader, live_reader };
    while (live_snapshot_reads.load(std::memory_order_relaxed) == 0U
        && !live_snapshot_mismatch.load(std::memory_order_acquire)
        && !stop_readers.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (std::size_t phase = 10U;
        phase < 14U
            && !live_snapshot_mismatch.load(std::memory_order_acquire);
        ++phase) {
        const auto current_version = value_for(width, kind, phase);
        const auto stored_version = value_for(width, kind, phase + 1U);
        const auto owner_version = value_for(width, kind, phase + 2U);
        auto concurrent_mutation = values->prepare_owner_change(
            2U, 1U, owner_version, current_version, stored_version);
        require(values->begin_prepared_publication(concurrent_mutation),
            "concurrent wide publication prepares late-pinned roles");
        values->publish(std::move(concurrent_mutation));
    }
    stop_readers.store(true, std::memory_order_release);
    first_reader.join();
    second_reader.join();
    live_reader.join();
    require(!reader_mismatch.load(std::memory_order_acquire),
        "immutable snapshots remain stable during concurrent sidecar publications");
    require(!live_snapshot_mismatch.load(std::memory_order_acquire)
            && live_snapshot_reads.load(std::memory_order_relaxed) != 0U,
        "live slot copies capture complete versions during prepared publication");

    retained = { };
    leases = { };
    const auto current_after_pinned_sequence = value_for(width, kind, 13U);
    const auto next_current = value_for(width, kind, 7U);
    const auto next_stored = value_for(width, kind, 8U);
    const auto next_owner = value_for(width, kind, 9U);
    require(current_after_pinned_sequence != next_current,
        "unpinned wide publication fixture changes current");
    AuthoritativeSignalPlanes::PreparedMutation next_mutation;
    next_mutation.words.reserve((width + 63U) / 64U);
    const auto prepared_word_capacity = next_mutation.words.capacity();
    begin_allocation_count();
    values->prepare_owner_change_into(next_mutation, 2U, 1U,
        next_owner, next_current, next_stored);
    require(values->begin_prepared_publication(next_mutation),
        "unpinned wide roles acquire their write locks before mutation");
    values->publish(std::move(next_mutation));
    const auto unpinned_publication_allocations = end_allocation_count();
    require(unpinned_publication_allocations == 0U
            && next_mutation.words.capacity() == prepared_word_capacity,
        "fixed-topology unpinned publication reuses preallocated word storage");
    require(values->current(2U) == next_current
            && values->previous(2U) == current_after_pinned_sequence
            && values->stored(2U) == next_stored
            && values->owner_value(2U, 1U) == next_owner,
        "unpinned wide publication reuses the current four-role versions");

    std::array<PackedLogic4, 4U> survives_sidecar {
        current, previous, stored, owner
    };
    begin_allocation_count();
    const auto unbound_slots = values->unbind_packed_slots();
    const auto unbind_allocations = end_allocation_count();
    require(unbind_allocations == 0U && unbound_slots == 4U,
        "wide A4 slot demotion materializes owning snapshots");
    require(current == next_current
            && previous == current_after_pinned_sequence
            && stored == next_stored && owner == next_owner,
        "demotion preserves every wide role value");
    values->clear_dirty();
    values.reset();
    require(survives_sidecar[0U] == next_current
            && survives_sidecar[1U] == current_after_pinned_sequence
            && survives_sidecar[2U] == next_stored
            && survives_sidecar[3U] == next_owner,
        "all detached role snapshots outlive the component sidecar");
}

void exercise_snapshot_safe_rebind(const std::uint32_t width,
    const ValueKind kind)
{
    constexpr SignalId signal = 2U;
    constexpr ProcessId owner_id = 1U;
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };

    const auto old_current = value_for(width, kind, 0U);
    const auto old_previous = value_for(width, kind, 1U);
    const auto old_stored = value_for(width, kind, 2U);
    const auto old_owner = value_for(width, kind, 3U);
    const auto rebound_current = value_for(width, kind, 4U);
    const auto rebound_stored = value_for(width, kind, 5U);
    const auto rebound_owner = value_for(width, kind, 6U);
    const auto final_current = value_for(width, kind, 7U);
    const auto final_stored = value_for(width, kind, 8U);
    const auto final_owner = value_for(width, kind, 9U);

    values.seed_signal(signal, old_current, old_previous, old_stored);
    values.seed_owner(signal, owner_id, old_owner);
    auto current = old_current;
    auto previous = old_previous;
    auto stored = old_stored;
    auto owner = old_owner;
    values.stage_packed_signal_slots(signal, current, previous, stored);
    values.stage_packed_owner_slot(signal, owner_id, owner);
    require(values.bind_packed_slots() == 4U,
        "snapshot-rebind fixture binds all four wide roles");

    const std::array<PackedLogic4, 4U> retained_values {
        current, previous, stored, owner
    };
    const std::array<PackedLogic4PlaneReadLease, 4U> retained_leases {
        values.plane_read_lease(signal, PackedPlaneRole::current),
        values.plane_read_lease(signal, PackedPlaneRole::previous),
        values.plane_read_lease(signal, PackedPlaneRole::stored),
        values.plane_read_lease(signal, PackedPlaneRole::owner, owner_id),
    };
    require(std::ranges::all_of(retained_leases,
                [](const auto& lease) { return static_cast<bool>(lease); })
            && retained_values[0U] == old_current
            && retained_values[1U] == old_previous
            && retained_values[2U] == old_stored
            && retained_values[3U] == old_owner,
        "wide facade copies and read leases capture all original roles");

    {
        AuthoritativeSignalPlanes live_facade_guard {
            SignalDriverLayout::build(graph, signal_ids),
            PackedSlotBindingPolicy::experimental_wide
        };
        live_facade_guard.seed_signal(
            signal, old_current, old_previous, old_stored);
        live_facade_guard.seed_owner(signal, owner_id, old_owner);
        live_facade_guard.stage_packed_signal_slots(
            signal, current, previous, stored);
        live_facade_guard.stage_packed_owner_slot(
            signal, owner_id, owner);
        const auto guard_revision = live_facade_guard.revision();
        require(!live_facade_guard.can_bind_packed_slots()
                && !live_facade_guard.packed_slots_bound()
                && live_facade_guard.revision() == guard_revision,
            "a second sidecar still refuses facades backed by live planes");

        begin_allocation_count();
        const auto unbound_count = values.unbind_packed_slots();
        const auto unbind_allocations = end_allocation_count();
        require(unbound_count == 4U && unbind_allocations == 0U
                && !values.packed_slots_bound()
                && !values.packed_signal_slots_bound(signal)
                && !values.packed_owner_slot_bound(signal, owner_id),
            "wide role unbind detaches every facade without allocating");
        require(live_facade_guard.can_bind_packed_slots()
                && !live_facade_guard.packed_slots_bound()
                && live_facade_guard.revision() == guard_revision
                && current == old_current && previous == old_previous
                && stored == old_stored && owner == old_owner,
            "detached wide snapshots become bindable while retaining their old roles");
    }

    auto first_mutation = values.prepare_owner_change(signal, owner_id,
        rebound_owner, rebound_current, rebound_stored);
    values.publish(std::move(first_mutation));
    require(values.current(signal) == rebound_current
            && values.previous(signal) == old_current
            && values.stored(signal) == rebound_stored
            && values.owner_value(signal, owner_id) == rebound_owner
            && current == old_current && previous == old_previous
            && stored == old_stored && owner == old_owner,
        "an unbound update advances all sidecar roles without changing snapshots");

    begin_allocation_count();
    const auto can_rebind = values.can_bind_packed_slots();
    const auto rebound_count = can_rebind ? values.bind_packed_slots() : 0U;
    const auto rebind_allocations = end_allocation_count();
    require(can_rebind && rebound_count == 4U && rebind_allocations == 0U
            && values.packed_slots_bound()
            && values.packed_signal_slots_bound(signal)
            && values.packed_owner_slot_bound(signal, owner_id),
        "owning plane snapshots rebind to the updated roles without allocation");
    require(values.current(signal) == rebound_current
            && values.previous(signal) == old_current
            && values.stored(signal) == rebound_stored
            && values.owner_value(signal, owner_id) == rebound_owner
            && current == rebound_current && previous == old_current
            && stored == rebound_stored && owner == rebound_owner,
        "rebound facades expose the complete current, LAST, stored and raw-owner state");

    auto second_mutation = values.prepare_owner_change(signal, owner_id,
        final_owner, final_current, final_stored);
    values.publish(std::move(second_mutation));
    require(values.current(signal) == final_current
            && values.previous(signal) == rebound_current
            && values.stored(signal) == final_stored
            && values.owner_value(signal, owner_id) == final_owner
            && current == final_current && previous == rebound_current
            && stored == final_stored && owner == final_owner,
        "a later bound mutation updates each rebound role coherently");
    require(retained_values[0U] == old_current
            && retained_values[1U] == old_previous
            && retained_values[2U] == old_stored
            && retained_values[3U] == old_owner
            && lease_matches(retained_leases[0U], old_current)
            && lease_matches(retained_leases[1U], old_previous)
            && lease_matches(retained_leases[2U], old_stored)
            && lease_matches(retained_leases[3U], old_owner),
        "rebind and later publication preserve every detached value and lease");

    require(values.unbind_packed_slots() == 4U
            && !values.packed_slots_bound(),
        "the test detaches staged facades before their local storage expires");
}

void exercise_retained_mirror_role_publication(const std::uint32_t width,
    const ValueKind kind)
{
    constexpr auto signal = SignalId { 2U };
    constexpr auto owner_id = ProcessId { 1U };
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };

    const auto old_current = value_for(width, kind, 0U);
    const auto old_previous = value_for(width, kind, 1U);
    const auto old_stored = value_for(width, kind, 2U);
    const auto old_owner = value_for(width, kind, 3U);
    const auto next_owner = value_for(width, kind, 4U);
    const auto next_stored = value_for(width, kind, 5U);
    const auto next_previous = value_for(width, kind, 6U);
    const auto next_current = value_for(width, kind, 7U);
    require(next_owner != old_owner && next_stored != old_stored
            && next_previous != old_previous && next_current != old_current,
        "each mirror fixture role changes at least once");
    values.seed_signal(signal, old_current, old_previous, old_stored);
    values.seed_owner(signal, owner_id, old_owner);
    auto current = old_current;
    auto previous = old_previous;
    auto stored = old_stored;
    auto owner = old_owner;
    values.stage_packed_signal_slots(signal, current, previous, stored);
    values.stage_packed_owner_slot(signal, owner_id, owner);
    require(values.bind_packed_slots() == 4U,
        "mirror fixture binds all four versioned wide roles");

    const std::array<PackedLogic4, 4U> retained_values {
        current, previous, stored, owner
    };
    const std::array<PackedLogic4PlaneReadLease, 4U> retained_leases {
        values.plane_read_lease(signal, PackedPlaneRole::current),
        values.plane_read_lease(signal, PackedPlaneRole::previous),
        values.plane_read_lease(signal, PackedPlaneRole::stored),
        values.plane_read_lease(signal, PackedPlaneRole::owner, owner_id),
    };
    require(std::ranges::all_of(retained_leases,
                [](const auto& lease) { return static_cast<bool>(lease); })
            && retained_values[0U] == old_current
            && retained_values[1U] == old_previous
            && retained_values[2U] == old_stored
            && retained_values[3U] == old_owner,
        "mirror fixture retains initial copies and leases for every role");

    values.mirror_owner(signal, owner_id, next_owner);
    require(values.valid() && values.current(signal) == old_current
            && values.previous(signal) == old_previous
            && values.stored(signal) == old_stored
            && values.owner_value(signal, owner_id) == next_owner,
        "owner mirror changes only the raw owner role");

    values.mirror_stored(signal, next_stored);
    require(values.valid() && values.current(signal) == old_current
            && values.previous(signal) == old_previous
            && values.stored(signal) == next_stored
            && values.owner_value(signal, owner_id) == next_owner,
        "stored mirror preserves current, LAST, and raw owner roles");

    values.mirror_visible(signal, next_previous, next_current);
    require(values.valid() && values.current(signal) == next_current
            && values.previous(signal) == next_previous
            && values.stored(signal) == next_stored
            && values.owner_value(signal, owner_id) == next_owner,
        "visible mirror updates current and LAST while retaining stored and raw roles");
    require(retained_values[0U] == old_current
            && retained_values[1U] == old_previous
            && retained_values[2U] == old_stored
            && retained_values[3U] == old_owner
            && lease_matches(retained_leases[0U], old_current)
            && lease_matches(retained_leases[1U], old_previous)
            && lease_matches(retained_leases[2U], old_stored)
            && lease_matches(retained_leases[3U], old_owner),
        "repeated mirrors preserve all retained role copies and leases");
    require(values.unbind_packed_slots() == 4U,
        "mirror fixture detaches facades before their local storage expires");
}

void exercise_mirror_cow_failure_rollback(const std::uint32_t width,
    const ValueKind kind)
{
    constexpr auto signal = SignalId { 2U };
    constexpr auto owner_id = ProcessId { 1U };
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };
    const auto old_current = value_for(width, kind, 0U);
    const auto old_previous = value_for(width, kind, 1U);
    const auto old_stored = value_for(width, kind, 2U);
    const auto old_owner = value_for(width, kind, 3U);
    const auto next_owner = value_for(width, kind, 4U);
    require(next_owner != old_owner,
        "mirror COW failure fixture changes the pinned owner");
    values.seed_signal(signal, old_current, old_previous, old_stored);
    values.seed_owner(signal, owner_id, old_owner);
    auto current = old_current;
    auto previous = old_previous;
    auto stored = old_stored;
    auto owner = old_owner;
    values.stage_packed_signal_slots(signal, current, previous, stored);
    values.stage_packed_owner_slot(signal, owner_id, owner);
    require(values.bind_packed_slots() == 4U,
        "mirror COW failure fixture binds all wide roles");

    const std::array<PackedLogic4, 4U> retained_values {
        current, previous, stored, owner
    };
    const std::array<PackedLogic4PlaneReadLease, 4U> retained_leases {
        values.plane_read_lease(signal, PackedPlaneRole::current),
        values.plane_read_lease(signal, PackedPlaneRole::previous),
        values.plane_read_lease(signal, PackedPlaneRole::stored),
        values.plane_read_lease(signal, PackedPlaneRole::owner, owner_id),
    };
    require(std::ranges::all_of(retained_leases,
                [](const auto& lease) { return static_cast<bool>(lease); }),
        "mirror COW failure fixture pins every original role");
    const auto revision_before_failure = values.revision();

    arm_allocation_failure(0U);
    values.mirror_owner(signal, owner_id, next_owner);
    const auto injected = allocation_failure_was_injected();
    clear_allocation_failure();

    require(injected && !values.valid()
            && values.revision() == revision_before_failure
            && values.current(signal) == old_current
            && values.previous(signal) == old_previous
            && values.stored(signal) == old_stored
            && values.owner_value(signal, owner_id) == old_owner,
        "failed pinned owner COW leaves all four authoritative roles unchanged");
    require(retained_values[0U] == old_current
            && retained_values[1U] == old_previous
            && retained_values[2U] == old_stored
            && retained_values[3U] == old_owner
            && lease_matches(retained_leases[0U], old_current)
            && lease_matches(retained_leases[1U], old_previous)
            && lease_matches(retained_leases[2U], old_stored)
            && lease_matches(retained_leases[3U], old_owner),
        "failed mirror COW leaves retained copies and leases intact");
}

void exercise_retired_role_reuse(const std::uint32_t width,
    const ValueKind kind)
{
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };
    const auto first = value_for(width, kind, 0U);
    const auto second = value_for(width, kind, 1U);
    require(first != second, "retired-role fixture changes each publication");
    values.seed_signal(2U, first, first, first);
    values.seed_owner(2U, 1U, first);
    auto current = first;
    auto previous = first;
    auto stored = first;
    auto owner = first;
    values.stage_packed_signal_slots(2U, current, previous, stored);
    values.stage_packed_owner_slot(2U, 1U, owner);
    require(values.bind_packed_slots() == 4U,
        "retired-role fixture binds every runtime slot");
    const std::array<PackedLogic4, 4U> historical {
        current, previous, stored, owner
    };
    AuthoritativeSignalPlanes::PreparedMutation mutation;
    mutation.words.reserve((width + 63U) / 64U);
    for (std::size_t step = 0U; step < 10U; ++step) {
        const auto& next = step % 2U == 0U ? second : first;
        const std::array<PackedLogic4, 4U> transient {
            current, previous, stored, owner
        };
        begin_allocation_count();
        values.prepare_owner_change_into(
            mutation, 2U, 1U, next, next, next);
        const bool prepared = values.begin_prepared_publication(mutation);
        if (prepared) {
            values.publish(std::move(mutation));
        }
        const auto allocations = end_allocation_count();
        require(prepared && values.valid(),
            "retired-role publication remains certified");
        if (step >= 3U) {
            require(allocations == 0U,
                "warmed pinned publications reuse retired role blocks");
        }
        require(current == next && previous == transient[0U]
                && stored == next && owner == next,
            "reused roles preserve current, LAST, stored and original owner");
        require(transient[0U] != next && transient[2U] != next
                && transient[3U] != next,
            "transient snapshots retain their prior values after reuse");
        require(std::ranges::all_of(historical,
                    [&](const auto& value) { return value == first; }),
            "retired-block reuse never mutates retained historical snapshots");
    }

    // Keep a second historical version alive so neither retired block can be
    // reused. Unbounded observation must retain the allocating checked path.
    const std::array<PackedLogic4, 4U> second_history {
        current, previous, stored, owner
    };
    values.prepare_owner_change_into(
        mutation, 2U, 1U, second, second, second);
    require(values.begin_prepared_publication(mutation),
        "second retained history permits a prepared publication");
    values.publish(std::move(mutation));
    const std::array<PackedLogic4, 4U> before_failure {
        current, previous, stored, owner
    };
    bool caught { };
    arm_allocation_failure(0U);
    try {
        values.prepare_owner_change_into(
            mutation, 2U, 1U, first, first, first);
        static_cast<void>(values.begin_prepared_publication(mutation));
    } catch (const std::bad_alloc&) {
        caught = true;
    }
    const bool injected = allocation_failure_was_injected();
    clear_allocation_failure();
    values.cancel_prepared_publication(mutation);
    require(caught && injected && values.valid()
            && current == before_failure[0U]
            && previous == before_failure[1U]
            && stored == before_failure[2U]
            && owner == before_failure[3U],
        "exhausted retired storage preserves every role on allocation failure");
    values.prepare_owner_change_into(
        mutation, 2U, 1U, first, first, first);
    require(values.begin_prepared_publication(mutation),
        "exhausted retired storage retries through allocating preparation");
    values.publish(std::move(mutation));
    require(current == first && previous == second
            && stored == first && owner == first
            && second_history[0U] == first
            && second_history[1U] == second
            && second_history[2U] == first
            && second_history[3U] == first
            && std::ranges::all_of(historical,
                [&](const auto& value) { return value == first; }),
        "allocating retry preserves both retained historical versions");
}

void exercise_late_pin_preflight_failure_and_retry()
{
    constexpr auto width = std::uint32_t { 129U };
    constexpr auto kind = ValueKind::logic4;
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };
    const auto old_current = value_for(width, kind, 0U);
    const auto old_previous = value_for(width, kind, 1U);
    const auto old_stored = value_for(width, kind, 2U);
    const auto old_owner = value_for(width, kind, 3U);
    const auto new_current = value_for(width, kind, 4U);
    const auto new_stored = value_for(width, kind, 5U);
    const auto new_owner = value_for(width, kind, 6U);
    values.seed_signal(2U, old_current, old_previous, old_stored);
    values.seed_owner(2U, 1U, old_owner);
    auto current = old_current;
    auto previous = old_previous;
    auto stored = old_stored;
    auto owner = old_owner;
    values.stage_packed_signal_slots(2U, current, previous, stored);
    values.stage_packed_owner_slot(2U, 1U, owner);
    require(values.bind_packed_slots() == 4U,
        "late-pin fixture binds all four wide roles");

    AuthoritativeSignalPlanes::PreparedMutation mutation;
    mutation.words.reserve((width + 63U) / 64U);
    values.prepare_owner_change_into(mutation, 2U, 1U,
        new_owner, new_current, new_stored);
    const std::array<PackedLogic4PlaneReadLease, 4U> leases {
        values.plane_read_lease(2U, PackedPlaneRole::current),
        values.plane_read_lease(2U, PackedPlaneRole::previous),
        values.plane_read_lease(2U, PackedPlaneRole::stored),
        values.plane_read_lease(2U, PackedPlaneRole::owner, 1U),
    };
    require(std::ranges::all_of(leases, [](const auto& lease) {
                return static_cast<bool>(lease);
            }),
        "late leases pin every role after mutation preparation");

    bool saw_preflight_failure { };
    bool ready { };
    for (std::size_t failure_index = 0U; failure_index < 64U;
         ++failure_index) {
        arm_allocation_failure(failure_index);
        try {
            ready = values.begin_prepared_publication(mutation);
            clear_allocation_failure();
            break;
        } catch (const std::bad_alloc&) {
            clear_allocation_failure();
            saw_preflight_failure = true;
            require(values.valid()
                    && values.current(2U) == old_current
                    && values.previous(2U) == old_previous
                    && values.stored(2U) == old_stored
                    && values.owner_value(2U, 1U) == old_owner,
                "failed late-pin cloning leaves all live roles unchanged");
        }
    }
    require(saw_preflight_failure && ready,
        "late-pin role clones retry successfully before publication");
    begin_allocation_count();
    values.publish(std::move(mutation));
    const auto publication_allocations = end_allocation_count();
    require(publication_allocations == 0U
            && values.current(2U) == new_current
            && values.previous(2U) == old_current
            && values.stored(2U) == new_stored
            && values.owner_value(2U, 1U) == new_owner
            && lease_matches(leases[0U], old_current)
            && lease_matches(leases[1U], old_previous)
            && lease_matches(leases[2U], old_stored)
            && lease_matches(leases[3U], old_owner),
        "late-pin publication installs a complete new version without invalidating leases");
}

void exercise_preflight_failure_and_retry()
{
    constexpr auto width = std::uint32_t { 129U };
    constexpr auto kind = ValueKind::logic9;
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    auto values = std::make_unique<AuthoritativeSignalPlanes>(
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide);
    const auto original = value_for(width, kind, 0U);
    const auto original_previous = value_for(width, kind, 1U);
    const auto original_stored = value_for(width, kind, 2U);
    const auto next_current = value_for(width, kind, 4U);
    const auto next_stored = value_for(width, kind, 5U);
    const auto next_owner = value_for(width, kind, 6U);
    values->seed_signal(2U, original, original_previous, original_stored);
    values->seed_owner(2U, 1U, original);
    auto current = original;
    auto previous = original_previous;
    auto stored = original_stored;
    auto owner = original;
    values->stage_packed_signal_slots(2U, current, previous, stored);
    values->stage_packed_owner_slot(2U, 1U, owner);
    require(values->bind_packed_slots() == 4U,
        "failure fixture binds every wide Logic9 role");

    const std::array<PackedLogic4, 4U> retained {
        current, previous, stored, owner
    };
    const std::array<PackedLogic4PlaneReadLease, 4U> leases {
        values->plane_read_lease(2U, PackedPlaneRole::current),
        values->plane_read_lease(2U, PackedPlaneRole::previous),
        values->plane_read_lease(2U, PackedPlaneRole::stored),
        values->plane_read_lease(2U, PackedPlaneRole::owner, 1U),
    };

    bool saw_failure { };
    bool published { };
    for (std::size_t failure_index = 0U; failure_index < 64U;
         ++failure_index) {
        arm_allocation_failure(failure_index);
        try {
            auto mutation = values->prepare_owner_change(
                2U, 1U, next_owner, next_current, next_stored);
            clear_allocation_failure();
            begin_allocation_count();
            values->publish(std::move(mutation));
            const auto publish_allocations = end_allocation_count();
            require(publish_allocations == 0U,
                "pinned wide role publication allocates only in preflight");
            published = true;
            break;
        } catch (const std::bad_alloc&) {
            clear_allocation_failure();
            saw_failure = true;
            require(values->current(2U) == original
                    && values->previous(2U) == original_previous
                    && values->stored(2U) == original_stored
                    && values->owner_value(2U, 1U) == original
                    && values->valid(),
                "failed wide role preparation leaves every live role unchanged");
        }
    }
    require(saw_failure && published
            && values->current(2U) == next_current
            && values->previous(2U) == original
            && values->stored(2U) == next_stored
            && values->owner_value(2U, 1U) == next_owner
            && retained[0U] == original
            && retained[1U] == original_previous
            && retained[2U] == original_stored
            && retained[3U] == original
            && lease_matches(leases[0U], original)
            && lease_matches(leases[1U], original_previous)
            && lease_matches(leases[2U], original_stored)
            && lease_matches(leases[3U], original),
        "wide multi-role preflight retries and publishes after allocation failure");
}

[[nodiscard]] std::array<PackedLogic4, 4U> ordinary_role_values(
    Interpreter& interpreter,
    const SignalId signal,
    const ProcessId owner)
{
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto* const record = implementation.driver_values.at(signal).find(owner);
    require(record != nullptr,
        "scheduled allocation failure preserves the original raw driver record");
    return { implementation.get_signal(signal).initial_value,
        implementation.signal_last_values.at(signal),
        implementation.driven_values.at(signal), record->value };
}

[[nodiscard]] std::array<std::string, 4U> role_texts(
    const std::array<PackedLogic4, 4U>& roles)
{
    return { roles[0U].to_msb_string(), roles[1U].to_msb_string(),
        roles[2U].to_msb_string(), roles[3U].to_msb_string() };
}

void exercise_native_word_a4_direct_publication(
    const bool inject_copy_on_write_failure)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment native_phase_profile_enabled {
        "FSIM_PROFILE_NATIVE_PHASE", "1" };

    constexpr SignalId input_id = 0U;
    constexpr SignalId output_id = 1U;
    const PackedLogic4 initial_value { 1U, Logic4::z };
    const PackedLogic4 initial_input_value { 1U, Logic4::zero };
    const PackedLogic4 changed_value { 1U, Logic4::one };

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "native_a4_word.input", initial_input_value,
        ResolutionKind::none, ValueKind::logic4 });
    const auto output = interpreter.add_signal({
        "native_a4_word.output", initial_value,
        ResolutionKind::sv_wire, ValueKind::logic4 });
    require(input == input_id && output == output_id,
        "native A4 publication keeps the expected input/output identity");

    auto writer = whole_writer(0U, output);
    writer.name = "native_a4_word.writer";
    writer.scheduling_domain = ProcessSchedulingDomain::generic;
    writer.operations.replace(1U, WriteUpdate {
        output, 0U, SignalUpdateDomain::generic });
    writer.initialize = true;
    writer.static_sensitivity = { { input, EdgeKind::any } };
    require(interpreter.add_process(std::move(writer)) == 0U,
        "native A4 publication writer keeps its registered process identity");

    const auto& registered = interpreter.process_program(0U);

    NativeWordA4PublicationState executor_state;
    executor_state.inject_failure_after_next_write
        = inject_copy_on_write_failure;
    interpreter.set_process_executor(0U,
        std::make_unique<ScheduledNativeWordA4PublicationExecutor>(
            interpreter, input, output, executor_state,
            ProcessExecutorProgramBinding {
                registered, registered, 0U }));

    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component
        = implementation.region_authoritative_component_by_signal.at(output);
    require(component
                < implementation.region_authoritative_state_by_component.size(),
        "native fixture output belongs to an authoritative A4 component");
    const auto initial_state_owner
        = implementation.region_authoritative_state_by_component.at(component);
    auto* const initial_state = initial_state_owner.get();
    require(initial_state != nullptr
            && initial_state->values().requires_prewrite_unbind()
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U)
            && !implementation.signal_transaction_observed.at(output)
            && implementation.process_signal_access_is_complete(0U)
            && !implementation.process_region_kernel_eligible(0U),
        "native fixture keeps one real bound A4 owner on checked process execution");

    const auto before_first_revision
        = implementation.signal_value_revisions.at(output);
    const auto before_first_plane_revision
        = initial_state->values().revision();
    const auto before_first_event = implementation.signal_events.at(output);
    const auto before_first_transaction
        = implementation.signal_transactions.at(output);
    const auto before_first_published
        = implementation.native_phase_profile_published;
    require(interpreter.run().status == RunStatus::completed
            && executor_state.resumes == 1U,
        "initial generic update reaches one native word publication");

    const auto first_native_roles
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, output, 0U);
    const auto first_native_transaction
        = implementation.signal_transactions.at(output);
    const auto first_native_event = implementation.signal_events.at(output);
    require(implementation.native_phase_profile_published
                    == before_first_published + 1U
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U)
            && first_native_roles[0U] == initial_input_value
            && first_native_roles[1U] == initial_value
            && first_native_roles[2U] == initial_input_value
            && first_native_roles[3U] == initial_input_value
            && implementation.signal_value_revisions.at(output)
                == before_first_revision + 1U
            && initial_state->values().revision()
                == before_first_plane_revision + 1U
            && first_native_event && first_native_event != before_first_event
            && first_native_transaction
            && first_native_transaction != before_first_transaction
            && first_native_transaction == first_native_event,
        "generic native update publishes one value change into the bound A4 roles");

    const auto before_roles = first_native_roles;
    const auto before_revision
        = implementation.signal_value_revisions.at(output);
    const auto before_plane_revision = initial_state->values().revision();
    const auto before_event = implementation.signal_events.at(output);
    const auto before_transaction
        = implementation.signal_transactions.at(output);
    const auto before_direct_publications
        = implementation.native_phase_profile_published;
    executor_state.capture_before_next_write = true;
    interpreter.schedule_signal_at(input, changed_value, 1U, 0U);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U),
        "input scheduling leaves the output's versioned roles bound");
    require(!implementation.region_recertification_pending
            && !implementation.region_recertification_requires_snapshot
            && !implementation.region_authoritative_recertification_waiting,
        "input stimulus does not request a snapshot or value recertification before output commit");

    clear_allocation_failure();
    RunStatus status = RunStatus::completed;
    try {
        status = interpreter.run().status;
    } catch (const std::bad_alloc&) {
        clear_allocation_failure();
        require(false,
            "A4 COW failure is caught and materialized by native publication");
    }
    const bool allocation_was_injected_for_this_write
        = executor_state.failure_armed
            && allocation_failure_was_injected();
    clear_allocation_failure();
    require(status == RunStatus::completed
            && executor_state.resumes == 2U
            && executor_state.captured
            && implementation.native_phase_profile_published
                == before_direct_publications + 1U,
        "changed value reaches the original native single-owner word publication route");

    const auto expected_raw_current
        = Logic4Word { 1U, UINT64_C(1), UINT64_C(0) };
    const auto expected_raw_previous
        = Logic4Word { 1U, UINT64_C(0), UINT64_C(0) };
    const auto current_offset = implementation
        .direct_wide_signal_offsets.at(output);
    require(implementation.direct_signal_aval.at(output)
                    == expected_raw_current.aval
            && implementation.direct_signal_bval.at(output)
                == expected_raw_current.bval
            && implementation.direct_signal_last_aval.at(output)
                == expected_raw_previous.aval
            && implementation.direct_signal_last_bval.at(output)
                == expected_raw_previous.bval
            && implementation.direct_wide_signal_aval.at(current_offset)
                == expected_raw_current.aval
            && implementation.direct_wide_signal_bval.at(current_offset)
                == expected_raw_current.bval
            && implementation.direct_signal_materialization_pending.at(output)
                == 0U,
        "native word publication leaves direct current/LAST planes exact and materialized");

    const auto retained_roles
        = executor_state.retained_values;
    const auto retained_leases
        = executor_state.retained_leases;
    require(retained_roles == before_roles
            && lease_matches(retained_leases[0U], before_roles[0U])
            && lease_matches(retained_leases[1U], before_roles[1U])
            && lease_matches(retained_leases[2U], before_roles[2U])
            && lease_matches(retained_leases[3U], before_roles[3U]),
        "retained four-role snapshots still expose the prepublication values");

    const auto after_event = implementation.signal_events.at(output);
    const auto after_transaction
        = implementation.signal_transactions.at(output);
    const auto after_roles = allocation_was_injected_for_this_write
        ? ordinary_role_values(interpreter, output, 0U)
        : OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, output, 0U);
    require(after_roles[0U] == changed_value
            && after_roles[1U] == initial_input_value
            && after_roles[2U] == changed_value
            && after_roles[3U] == changed_value
            && implementation.signal_value_revisions.at(output)
                == before_revision + 1U
            && after_event && after_transaction
            && after_event == after_transaction
            && after_event->first == 1U
            && after_transaction->first == 1U
            && after_event != before_event
            && after_transaction != before_transaction,
        "native A4 publication keeps current/LAST/stored/owner and event transaction semantics");

    if (inject_copy_on_write_failure) {
        const auto* const current_state
            = implementation.region_authoritative_state_for_signal(output);
        const bool current_state_is_recertified
            = current_state == nullptr
            || (current_state != initial_state && current_state->valid()
                && current_state->generation()
                    == implementation.region_runtime_generation);
        require(allocation_was_injected_for_this_write
                && executor_state.failure_armed
                && !initial_state->valid()
                && !initial_state->values().packed_slots_bound()
                && !initial_state->values().packed_signal_slots_bound(output)
                && !initial_state->values().packed_owner_slot_bound(
                    output, 0U)
                && current_state_is_recertified,
            "failed COW preflight invalidates and demotes the old bound facade before fallback");
    } else {
        require(!allocation_was_injected_for_this_write
                && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, output, 0U)
                && implementation.region_authoritative_state_for_signal(
                    output) == initial_state
                && initial_state->values().revision()
                    == before_plane_revision + 1U,
            "successful native publication replaces all four A4 roles in one bound mutation");
    }
}

void exercise_observed_transaction_checked_fallback()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    const PackedLogic4 initial_value { 1U, Logic4::z };
    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "observed_transaction.input", initial_value,
        ResolutionKind::none, ValueKind::logic4 });
    const auto output = interpreter.add_signal({
        "observed_transaction.output", initial_value,
        ResolutionKind::sv_wire, ValueKind::logic4 });

    auto writer = whole_writer(0U, output);
    writer.name = "observed_transaction.writer";
    writer.initialize = true;
    writer.static_sensitivity = { { input, EdgeKind::any } };
    require(interpreter.add_process(std::move(writer)) == 0U,
        "observed transaction writer keeps its registered process identity");

    Process transaction_observer;
    transaction_observer.id = 1U;
    transaction_observer.name = "observed_transaction.observer";
    transaction_observer.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    transaction_observer.initialize = true;
    transaction_observer.static_sensitivity = {
        { output, EdgeKind::transaction } };
    transaction_observer.operations = {
        WaitSensitivity { }, Display { "transaction" }, Jump { 0U } };
    require(interpreter.add_process(std::move(transaction_observer)) == 1U,
        "observed checked control registers a transaction-only observer");

    std::size_t transaction_callbacks { };
    interpreter.set_output_hook(
        [&](ProcessId, std::string_view text, bool, SimulationTick,
            std::uint64_t) {
            if (text == "transaction") {
                ++transaction_callbacks;
            }
        });

    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component
        = implementation.region_authoritative_component_by_signal.at(output);
    require(component == std::numeric_limits<std::size_t>::max()
            && implementation.region_authoritative_state_for_signal(output)
                == nullptr
            && implementation.signal_transaction_observed.at(output),
        "the event-observed output stays outside private A4 storage");

    const auto before_roles = ordinary_role_values(interpreter, output, 0U);
    const auto before_revision
        = implementation.signal_value_revisions.at(output);
    const auto before_event = implementation.signal_events.at(output);
    const auto before_transaction
        = implementation.signal_transactions.at(output);
    require(interpreter.run().status == RunStatus::completed
            && transaction_callbacks == 1U
            && implementation.get_signal(output).initial_value == initial_value
            && ordinary_role_values(interpreter, output, 0U) == before_roles
            && implementation.signal_value_revisions.at(output)
                == before_revision
            && implementation.signal_events.at(output) == before_event
            && implementation.signal_transactions.at(output)
            && implementation.signal_transactions.at(output)
                != before_transaction,
        "same-value checked publication preserves roles and wakes the transaction observer");
}

[[nodiscard]] PackedLogic4 logic9_rotation(const std::size_t phase)
{
    static constexpr std::string_view states { "UX01ZWLH-" };
    std::string text;
    text.reserve(states.size());
    for (std::size_t bit = 0U; bit < states.size(); ++bit) {
        text.push_back(states[(bit + phase) % states.size()]);
    }
    return PackedLogic4::from_logic9_msb_string(text);
}

void exercise_native_logic9_a4_direct_publication(
    const bool inject_copy_on_write_failure)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr SignalId input_id = 0U;
    constexpr SignalId output_id = 1U;
    const auto initial_value
        = PackedLogic4::from_logic9_msb_string("UUUUUUUUU");

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "native_a4_logic9.input", initial_value,
        ResolutionKind::none, ValueKind::logic9 });
    const auto output = interpreter.add_signal({
        "native_a4_logic9.output", initial_value,
        ResolutionKind::std_logic, ValueKind::logic9 });
    require(input == input_id && output == output_id,
        "native Logic9 A4 publication keeps its signal identities");

    auto writer = whole_writer(0U, output);
    // The validated Logic9 batch drains in generic Update, so the registered
    // SimIR operation and checked fallback use the same update provenance.
    writer.scheduling_domain = ProcessSchedulingDomain::generic;
    writer.operations[1U] = WriteUpdate {
        output, 0U, SignalUpdateDomain::generic };
    writer.name = "native_a4_logic9.writer";
    writer.register_value_kinds = { ValueKind::logic9 };
    writer.initialize = true;
    writer.static_sensitivity = { { input, EdgeKind::any } };
    require(interpreter.add_process(std::move(writer)) == 0U,
        "Logic9 publication keeps the registered single-owner process");

    const auto& registered = interpreter.process_program(0U);

    NativeLogic9A4PublicationState executor_state;
    executor_state.inject_failure_after_next_write
        = inject_copy_on_write_failure;
    interpreter.set_process_executor(0U,
        std::make_unique<ScheduledNativeLogic9A4PublicationExecutor>(
            interpreter, input, output, 0U, executor_state,
            ProcessExecutorProgramBinding {
                registered, registered, 0U }));

    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component
        = implementation.region_authoritative_component_by_signal.at(output);
    require(component
                < implementation.region_authoritative_state_by_component.size(),
        "Logic9 fixture output belongs to an authoritative A4 component");
    const auto initial_state_owner
        = implementation.region_authoritative_state_by_component.at(component);
    auto* const initial_state = initial_state_owner.get();
    require(initial_state != nullptr
            && initial_state->values().requires_prewrite_unbind()
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U)
            && !implementation.signal_transaction_observed.at(output)
            && implementation.process_signal_access_is_complete(0U)
            && !implementation.process_region_kernel_eligible(0U),
        "Logic9 fixture starts with one bound A4 owner and checked process");

    const auto before_same_revision
        = implementation.signal_value_revisions.at(output);
    const auto before_same_plane_revision
        = initial_state->values().revision();
    const auto before_same_event = implementation.signal_events.at(output);
    const auto before_same_transaction
        = implementation.signal_transactions.at(output);
    require(interpreter.run().status == RunStatus::completed
            && executor_state.resumes == 1U
            && executor_state.batch_consumed,
        "same-value Logic9 batch reaches the native update commit");
    const auto same_value_roles
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, output, 0U);
    const auto after_same_transaction
        = implementation.signal_transactions.at(output);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U)
            && std::ranges::all_of(same_value_roles,
                [&](const PackedLogic4& role) { return role == initial_value; })
            && implementation.signal_value_revisions.at(output)
                == before_same_revision
            && initial_state->values().revision()
                == before_same_plane_revision
            && implementation.signal_events.at(output) == before_same_event
            && after_same_transaction
            && after_same_transaction != before_same_transaction,
        "same-value Logic9 transaction preserves all roles and creates no value event");

    const auto commit_count = inject_copy_on_write_failure ? 1U : 9U;
    for (std::size_t phase = 0U; phase < commit_count; ++phase) {
        const auto next = logic9_rotation(phase);
        const auto next_word = next.logic9_low_word();
        const auto before_roles
            = OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, output, 0U);
        const auto before_revision
            = implementation.signal_value_revisions.at(output);
        const auto before_plane_revision = initial_state->values().revision();
        const auto before_event = implementation.signal_events.at(output);
        const auto before_transaction
            = implementation.signal_transactions.at(output);

        executor_state.capture_before_next_write = true;
        interpreter.schedule_signal_at(input, next,
            static_cast<SimulationTick>(phase + 1U), 0U);
        require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, output, 0U)
                && !implementation.region_recertification_pending
                && !implementation.region_recertification_requires_snapshot
                && !implementation.region_authoritative_recertification_waiting,
            "Logic9 input update preserves the output's bound A4 certificate");
        clear_allocation_failure();
        RunStatus status = RunStatus::completed;
        try {
            status = interpreter.run().status;
        } catch (const std::bad_alloc&) {
            clear_allocation_failure();
            require(false,
                "Logic9 A4 COW failure is caught and checked publication completes");
        }
        const bool allocation_was_injected_for_this_write
            = executor_state.failure_armed
                && allocation_failure_was_injected();
        clear_allocation_failure();
        require(status == RunStatus::completed
                && executor_state.resumes == phase + 2U
                && executor_state.batch_consumed
                && executor_state.captured,
            "changed Logic9 value reaches the native direct-owner update route");

        const auto current_offset
            = implementation.direct_wide_signal_offsets.at(output);
        const auto previous_word
            = before_roles[0U].logic9_low_word();
        require(implementation.direct_signal_logic9_plane0.at(output)
                    == next_word.planes[0U]
                && implementation.direct_signal_logic9_plane1.at(output)
                    == next_word.planes[1U]
                && implementation.direct_signal_logic9_plane2.at(output)
                    == next_word.planes[2U]
                && implementation.direct_signal_logic9_plane3.at(output)
                    == next_word.planes[3U]
                && implementation.direct_signal_last_logic9_plane0.at(output)
                    == previous_word.planes[0U]
                && implementation.direct_signal_last_logic9_plane1.at(output)
                    == previous_word.planes[1U]
                && implementation.direct_signal_last_logic9_plane2.at(output)
                    == previous_word.planes[2U]
                && implementation.direct_signal_last_logic9_plane3.at(output)
                    == previous_word.planes[3U]
                && implementation.direct_wide_signal_aval.at(current_offset)
                    == next_word.planes[0U]
                && implementation.direct_wide_signal_bval.at(current_offset)
                    == next_word.planes[1U]
                && implementation.direct_wide_signal_logic9_plane2.at(current_offset)
                    == next_word.planes[2U]
                && implementation.direct_wide_signal_logic9_plane3.at(current_offset)
                    == next_word.planes[3U],
            "Logic9 current and LAST planes preserve every raw state code");

        const auto after_roles = allocation_was_injected_for_this_write
            ? ordinary_role_values(interpreter, output, 0U)
            : OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, output, 0U);
        require(after_roles[0U] == next
                && after_roles[1U] == before_roles[0U]
                && after_roles[2U] == next
                && after_roles[3U] == next
                && implementation.signal_value_revisions.at(output)
                    == before_revision + 1U
                && implementation.signal_events.at(output)
                && implementation.signal_transactions.at(output)
                    == implementation.signal_events.at(output)
                && implementation.signal_events.at(output)->first
                    == static_cast<SimulationTick>(phase + 1U)
                && implementation.signal_transactions.at(output)->first
                    == static_cast<SimulationTick>(phase + 1U)
                && implementation.signal_events.at(output) != before_event
                && implementation.signal_transactions.at(output)
                    != before_transaction,
            "Logic9 commit preserves current/LAST/stored/owner and event transaction roles");

        const auto retained_roles = executor_state.retained_values;
        const auto retained_leases = executor_state.retained_leases;
        bool retained_lease_values_match = true;
        for (std::size_t role = 0U; role < retained_leases.size(); ++role) {
            retained_lease_values_match = retained_lease_values_match
                && lease_matches(retained_leases[role], before_roles[role]);
        }
        require(retained_roles == before_roles && retained_lease_values_match,
            "retained Logic9 A4 role snapshots keep their prepublication values");

        if (inject_copy_on_write_failure) {
            const auto* const current_state
                = implementation.region_authoritative_state_for_signal(output);
            const bool current_state_is_recertified
                = current_state == nullptr
                || (current_state != initial_state && current_state->valid()
                    && current_state->generation()
                        == implementation.region_runtime_generation);
            require(allocation_was_injected_for_this_write
                    && executor_state.failure_armed
                    && !initial_state->valid()
                    && !initial_state->values().packed_slots_bound()
                    && !initial_state->values().packed_signal_slots_bound(output)
                    && !initial_state->values().packed_owner_slot_bound(
                        output, 0U)
                    && current_state_is_recertified,
                "failed Logic9 COW preflight demotes old slots before fallback");
            break;
        }

        require(!allocation_was_injected_for_this_write
                && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, output, 0U)
                && implementation.region_authoritative_state_for_signal(
                    output) == initial_state
                && initial_state->values().revision()
                    == before_plane_revision + 1U,
            "successful Logic9 publication updates all roles in one bound mutation");
    }

    if (!inject_copy_on_write_failure) {
        const auto final_value = logic9_rotation(8U);
        const auto final_roles
            = OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, output, 0U);
        require(final_roles[0U] == final_value
                && final_value.to_msb_string().find('U') != std::string::npos
                && final_value.to_msb_string().find('X') != std::string::npos
                && final_value.to_msb_string().find('0') != std::string::npos
                && final_value.to_msb_string().find('1') != std::string::npos
                && final_value.to_msb_string().find('Z') != std::string::npos
                && final_value.to_msb_string().find('W') != std::string::npos
                && final_value.to_msb_string().find('L') != std::string::npos
                && final_value.to_msb_string().find('H') != std::string::npos
                && final_value.to_msb_string().find('-') != std::string::npos,
            "native Logic9 publication covers all nine canonical states");
    }
}

struct DisjointOwnerRoundResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner0;
    PackedLogic4 owner1;
    PackedLogic4 expected_current;
    PackedLogic4 expected_previous;
    std::uint64_t revision_delta { };
    std::uint64_t sidecar_generation_delta { };
    std::uint64_t owner_mirror_delta { };
    bool bound_before { };
    bool bound_after { };
    bool generic_owner_component_precondition { };
    bool versioned_storage_ready { };
    bool allocation_injected { };
    bool completed { };
    bool threw { };
    bool graph_kept_partial_target_as_boundary { };
    bool component_structural_candidate { };
    bool executor_access_complete { };
    bool executors_remain_checked { };
    bool legacy_owned_composite_inactive { };
    bool force_release_demoted_slots { };
    bool late_observation_demoted_slots { };
    bool late_observation_coherent { };
    bool disjoint_gate_admitted_owner0 { };
    bool disjoint_gate_admitted_owner1 { };
    bool target_event_changed { };
    bool target_transaction_changed { };
    bool mixed_component_contains_checked_writer { };
    bool mixed_component_middle_is_structural_candidate { };
    bool unrelated_internal_signal_uses_checked_route { };
    bool unrelated_internal_signal_uses_authoritative_route { };
    bool unrelated_write_keeps_disjoint_slots_bound { };
    std::array<std::size_t, 2U> resumes { };
    std::vector<ProcessId> resume_order;
    bool retry_completed { };
    std::uint64_t retry_target_revision_delta { };
    bool fallback_preflight_failure_observed { };
    bool fallback_secondary_failure_observed { };
    bool fallback_secondary_failure_full_at_injection { };
    bool fallback_secondary_failure_unchanged_at_injection { };
    bool fallback_failure_left_owner_resolution_gap { };
    bool fallback_failure_reached_after_publication { };
    bool local_wave_disabled { };
    bool partial_target_not_activation_internal { };
    PackedLogic4 retry_current;
    PackedLogic4 retry_previous;
    PackedLogic4 retry_stored;
    PackedLogic4 retry_owner0;
    PackedLogic4 retry_owner1;
    PackedLogic4 expected_retry_current;
    std::array<PackedLogic4, 4U> published_roles0;
    std::array<PackedLogic4, 4U> published_roles1;
    bool published_sidecar_roles_match { };
};

[[nodiscard]] DisjointOwnerRoundResult run_disjoint_owner_round(
    const std::uint32_t width,
    const ValueKind kind,
    const char* disjoint_owner_policy,
    const bool reverse_input_deposit_order,
    const std::optional<std::size_t> failure_index = std::nullopt,
    const bool force_and_release = false,
    const char* single_owner_policy = "0",
    const bool generic_owner_updates = false,
    const std::optional<std::size_t> fallback_failure_after = std::nullopt)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_policy_scope {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", single_owner_policy };
    ScopedEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_owner_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };

    const auto lower_width = width / 2U;
    const auto upper_width = width - lower_width;
    const auto upper_offset = lower_width;
    const auto initial0 = value_for(lower_width, kind, 1U);
    const auto initial1 = value_for(upper_width, kind, 2U);
    // The one-bit lower owner must change from its initial Z in both kinds.
    const auto next0 = value_for(lower_width, kind, 20U);
    const auto next1 = value_for(upper_width, kind, 24U);
    const auto& visible_next0 = next0;
    const auto& visible_next1 = next1;
    const auto initial_target = all_z_value(width, kind);
    auto expected_current = all_z_value(width, kind);
    insert_range(expected_current, visible_next0, 0U, kind);
    insert_range(expected_current, visible_next1, upper_offset, kind);
    auto expected_owner0 = all_z_value(width, kind);
    auto expected_owner1 = all_z_value(width, kind);
    insert_range(expected_owner0, visible_next0, 0U, kind);
    insert_range(expected_owner1, visible_next1, upper_offset, kind);

    Interpreter interpreter;
    const auto input0 = interpreter.add_signal({
        "wide_disjoint.input0", initial0, ResolutionKind::none, kind });
    const auto input1 = interpreter.add_signal({
        "wide_disjoint.input1", initial1, ResolutionKind::none, kind });
    const auto target_resolution = kind == ValueKind::logic9
        ? ResolutionKind::std_logic : ResolutionKind::sv_wire;
    const auto target = interpreter.add_signal({
        "wide_disjoint.target", initial_target, target_resolution, kind });
    const auto middle = interpreter.add_signal({
        "wide_disjoint.middle", initial_target, target_resolution, kind });
    const auto sink = interpreter.add_signal({
        "wide_disjoint.sink", PackedLogic4 { 1U, Logic4::zero },
        ResolutionKind::sv_wire, ValueKind::logic4 });

    DisjointSliceExecutorState executor_state;
    executor_state.failure_index = failure_index;
    executor_state.generic_updates = generic_owner_updates;
    const auto add_writer = [&](const ProcessId process_id,
                                const SignalId input,
                                const std::uint32_t offset,
                                const std::uint32_t slice_width) {
        auto process = disjoint_slice_writer(process_id, input, target,
            offset, slice_width, kind, generic_owner_updates);
        require(interpreter.add_process(std::move(process)) == process_id,
            "disjoint partial writer preserves its process identity");
        const auto& registered = interpreter.process_program(process_id);
        interpreter.set_process_executor(process_id,
            std::make_unique<ScheduledDisjointSliceExecutor>(process_id,
                input, target, offset, executor_state,
                ProcessExecutorProgramBinding {
                    registered, registered, process_id }));
    };
    add_writer(0U, input0, 0U, lower_width);
    add_writer(1U, input1, upper_offset, upper_width);
    const auto downstream_domain = generic_owner_updates
        ? ProcessSchedulingDomain::generic
        : ProcessSchedulingDomain::systemverilog;
    auto middle_writer = disjoint_component_copy_writer(
        2U, target, middle, kind, downstream_domain);
    require(interpreter.add_process(std::move(middle_writer)) == 2U,
        "disjoint target component has an unrelated full-width writer");
    auto middle_reader = disjoint_slice_reader(
        3U, middle, sink, kind, downstream_domain);
    require(interpreter.add_process(std::move(middle_reader)) == 3U,
        "the unrelated full-width signal has an in-component reader");

    if (generic_owner_updates && disjoint_owner_policy != nullptr
        && std::string_view { disjoint_owner_policy } == "0") {
        interpreter.set_driver_change_hook(
            [](ProcessId, SignalId, SimulationTick) { });
    }
    interpreter.start();
    auto status = interpreter.run().status;
    require(status == RunStatus::completed,
        "disjoint owner fixture reaches a startup quiet point");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    bool force_release_demoted_slots { };
    if (force_and_release) {
        require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, target, 0U)
                && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, target, 1U),
            "force fixture begins with both packed owner slots bound");
        const auto forced = value_for(width, kind, 30U);
        interpreter.force_signal(target, forced);
        const bool force_demoted
            = !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        require(implementation.get_signal(target).initial_value == forced,
            "global force is applied through the ordinary checked route");
        interpreter.release_signal(target);
        const bool release_demoted
            = !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        force_release_demoted_slots = force_demoted && release_demoted;
    }
    auto* const initial_state
        = implementation.region_authoritative_state_for_signal(target);
    std::array<PackedLogic4PlaneReadLease, 5U>
        retained_group_failure_leases;
    if (failure_index && generic_owner_updates
        && kind == ValueKind::logic4) {
        require(initial_state != nullptr,
            "generic group failure fixture retains its A4 component");
        retained_group_failure_leases = {
            initial_state->values().plane_read_lease(
                target, PackedPlaneRole::current),
            initial_state->values().plane_read_lease(
                target, PackedPlaneRole::previous),
            initial_state->values().plane_read_lease(
                target, PackedPlaneRole::stored),
            initial_state->values().plane_read_lease(
                target, PackedPlaneRole::owner, 0U),
            initial_state->values().plane_read_lease(
                target, PackedPlaneRole::owner, 1U),
        };
        require(std::ranges::all_of(retained_group_failure_leases,
                    [](const auto& lease) {
                        return static_cast<bool>(lease);
                    }),
            "generic group failure fixture pins every authoritative role");
    }
    const bool middle_is_not_authoritative
        = middle >= implementation.region_authoritative_component_by_signal.size()
        || implementation.region_authoritative_component_by_signal[middle]
            == std::numeric_limits<std::size_t>::max();
    const bool bound_before = OwnedDriverDemotionTestAccess::
        packed_a4_slots_bound(interpreter, target, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 1U);
    const auto initial_current
        = implementation.get_signal(target).initial_value;
    const auto* const before_owner0
        = implementation.driver_values.at(target).find(0U);
    const auto* const before_owner1
        = implementation.driver_values.at(target).find(1U);
    require(before_owner0 != nullptr && before_owner1 != nullptr,
        "disjoint target retains both original raw owner records");
    const auto initial_owner0 = before_owner0->value;
    const auto initial_owner1 = before_owner1->value;
    const auto old_roles0 = bound_before
        ? OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 0U)
        : std::array<PackedLogic4, 4U> {
            initial_current,
            implementation.signal_last_values.at(target),
            implementation.driven_values.at(target),
            before_owner0->value };
    const auto old_roles1 = bound_before
        ? OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 1U)
        : std::array<PackedLogic4, 4U> {
            initial_current,
            implementation.signal_last_values.at(target),
            implementation.driven_values.at(target),
            before_owner1->value };
    const auto old_role_texts0 = role_texts(old_roles0);
    const auto old_role_texts1 = role_texts(old_roles1);

    DisjointOwnerRoundResult result;
    result.bound_before = bound_before;
    result.versioned_storage_ready
        = initial_state != nullptr
        && initial_state->values().requires_prewrite_unbind();
    result.force_release_demoted_slots = force_release_demoted_slots;
    result.executor_access_complete
        = implementation.process_signal_access_is_complete(0U)
        && implementation.process_signal_access_is_complete(1U);
    result.executors_remain_checked
        = !implementation.process_region_kernel_eligible(0U)
        && !implementation.process_region_kernel_eligible(1U);
    result.legacy_owned_composite_inactive
        = !implementation.owned_driver_active(target);
    if (generic_owner_updates
        && std::string_view { disjoint_owner_policy } == "0") {
        require(result.legacy_owned_composite_inactive,
            "checked generic reference has no legacy owned composite");
    }
    result.local_wave_disabled
        = !implementation.systemverilog_local_wave_enabled;
    result.unrelated_internal_signal_uses_checked_route
        = middle_is_not_authoritative
        && implementation.driver_values.at(middle).size() == 1U;
    result.unrelated_internal_signal_uses_authoritative_route
        = !middle_is_not_authoritative
        && implementation.driver_values.at(middle).size() == 1U;
    result.disjoint_gate_admitted_owner0
        = implementation.can_try_wide_disjoint_owner_commit(0U, target);
    result.disjoint_gate_admitted_owner1
        = implementation.can_try_wide_disjoint_owner_commit(1U, target);
    if (implementation.region_graph
        && !implementation.region_component_by_process.empty()) {
        const auto component = implementation.region_component_by_process[0U];
        if (component < implementation.region_activation_programs.size()
            && implementation.region_activation_programs[component]) {
            const auto& activation_internal_signals
                = implementation.region_activation_programs[component]
                    ->activation_kernel.internal_signals;
            result.partial_target_not_activation_internal
                = std::ranges::find(activation_internal_signals, target)
                    == activation_internal_signals.end();
        }
        if (component < implementation.region_graph->certificate_inventory()
                .components.size()) {
            const auto& certificate = implementation.region_graph
                ->certificate_inventory().components[component];
            result.component_structural_candidate
                = certificate.status
                    == RegionComponentCertificateStatus::structural_candidate;
            result.mixed_component_contains_checked_writer
                = std::ranges::find(certificate.members, ProcessId { 2U })
                    != certificate.members.end()
                && std::ranges::find(certificate.members, ProcessId { 3U })
                    != certificate.members.end();
            result.mixed_component_middle_is_structural_candidate
                = std::ranges::find(
                    certificate.structural_internal_signal_candidates,
                    middle)
                    != certificate.structural_internal_signal_candidates.end();
            result.graph_kept_partial_target_as_boundary
                = std::ranges::find(certificate.boundary_signals, target)
                    != certificate.boundary_signals.end();
            if (generic_owner_updates
                && implementation.region_component_by_process.size() > 3U) {
                const auto same_component =
                    implementation.region_component_by_process[1U]
                        == component
                    && implementation.region_component_by_process[2U]
                        == component
                    && implementation.region_component_by_process[3U]
                        == component;
                const std::array<ProcessId, 4U> expected_members {
                    0U, 1U, 2U, 3U };
                const bool certificate_has_all_members
                    = std::ranges::all_of(expected_members,
                        [&](const ProcessId process) {
                            return std::ranges::binary_search(
                                certificate.members, process);
                        });
                result.generic_owner_component_precondition
                    = same_component && certificate_has_all_members
                    && result.bound_before
                    && result.disjoint_gate_admitted_owner0
                    && result.disjoint_gate_admitted_owner1;
            }
        }
    }
    if (generic_owner_updates && disjoint_owner_policy != nullptr
        && std::string_view { disjoint_owner_policy } == "1") {
        require(result.generic_owner_component_precondition,
            "generic writers and readers share a certified component with "
            "both A4 owner slots bound before the Update");
    }
    const auto before_revision
        = implementation.signal_value_revisions.at(target);
    const auto before_value_generation = initial_state == nullptr
        ? std::uint64_t { 0U } : initial_state->values().revision();
    const auto before_owner_mirrors
        = implementation.systemverilog_wave_profile_a4_owner_mirrors;
    const auto before_event = implementation.signal_events.at(target);
    const auto before_transaction
        = implementation.signal_transactions.at(target);

    clear_allocation_failure();
    FallbackAllocationFailureProbe fallback_failure_probe;
    fallback_failure_probe.successful_allocations_after_preflight
        = fallback_failure_after.value_or(0U);
    fallback_failure_probe.interpreter = &interpreter;
    fallback_failure_probe.target = target;
    fallback_failure_probe.expected_current = &expected_current;
    fallback_failure_probe.expected_owner0 = &expected_owner0;
    fallback_failure_probe.expected_owner1 = &expected_owner1;
    fallback_failure_probe.initial_current = &initial_current;
    fallback_failure_probe.initial_owner0 = &initial_owner0;
    fallback_failure_probe.initial_owner1 = &initial_owner1;
    fallback_failure_probe.retained_leases = &retained_group_failure_leases;
    fallback_failure_probe.before_revision = before_revision;
    fallback_failure_probe.before_event = before_event;
    fallback_failure_probe.before_transaction = before_transaction;
    if (fallback_failure_after) {
        set_allocation_failure_observer(
            &fallback_failure_probe, &FallbackAllocationFailureProbe::observe);
    }
    if (reverse_input_deposit_order) {
        interpreter.deposit_signal(input1, next1);
        interpreter.deposit_signal(input0, next0);
    } else {
        interpreter.deposit_signal(input0, next0);
        interpreter.deposit_signal(input1, next1);
    }
    try {
        status = interpreter.run().status;
        result.completed = status == RunStatus::completed;
    } catch (const std::bad_alloc&) {
        result.threw = true;
    }
    set_allocation_failure_observer(nullptr, nullptr);
    result.allocation_injected = allocation_failure_was_injected();
    clear_allocation_failure();
    result.fallback_preflight_failure_observed
        = fallback_failure_probe.preflight_failure_observed;
    result.fallback_secondary_failure_observed
        = fallback_failure_probe.fallback_failure_observed;
    result.fallback_secondary_failure_full_at_injection
        = fallback_failure_probe.full_publication_at_fallback_failure;
    result.fallback_secondary_failure_unchanged_at_injection
        = fallback_failure_probe.unchanged_at_fallback_failure;
    if (fallback_failure_probe.fallback_failure_observed) {
        const auto* const after_failure_owner0
            = implementation.driver_values.at(target).find(0U);
        const auto* const after_failure_owner1
            = implementation.driver_values.at(target).find(1U);
        result.fallback_failure_left_owner_resolution_gap
            = after_failure_owner0 != nullptr && after_failure_owner1 != nullptr
            && after_failure_owner0->value == expected_owner0
            && after_failure_owner1->value == expected_owner1
            && implementation.signals.at(target).initial_value
                == initial_current
            && implementation.signal_last_values.at(target)
                == initial_current
            && implementation.driven_values.at(target) == initial_current;
    }
    result.resumes = {
        executor_state.resumes[0U], executor_state.resumes[1U]
    };
    result.resume_order = executor_state.resume_order;
    if (fallback_failure_after
        && fallback_failure_probe.preflight_failure_observed
        && fallback_failure_probe.fallback_failure_observed) {
        require(result.allocation_injected,
            "the selected secondary fallback allocation is injected");
        require(!result.fallback_failure_left_owner_resolution_gap,
            "a failed fallback does not leave both new owners with old signal roles");
        if (result.fallback_secondary_failure_full_at_injection) {
            require(!result.fallback_secondary_failure_unchanged_at_injection,
                "secondary failure point has one complete publication classification");
            const auto committed_revision
                = implementation.signal_value_revisions.at(target);
            const auto committed_event
                = implementation.signal_events.at(target);
            const auto committed_transaction
                = implementation.signal_transactions.at(target);
            const auto* const committed_owner0
                = implementation.driver_values.at(target).find(0U);
            const auto* const committed_owner1
                = implementation.driver_values.at(target).find(1U);
            require(committed_owner0 != nullptr
                    && committed_owner1 != nullptr
                    && implementation.get_signal(target).initial_value
                        == expected_current
                    && implementation.signal_last_values.at(target)
                        == initial_current
                    && implementation.driven_values.at(target)
                        == expected_current
                    && committed_owner0->value == expected_owner0
                    && committed_owner1->value == expected_owner1
                    && committed_revision == before_revision + 1U,
                "injected fallback failure follows complete checked publication");

            const auto resume_order_before_drain
                = executor_state.resume_order.size();
            const auto quiet_resume = interpreter.run();
            const auto* const drained_owner0
                = implementation.driver_values.at(target).find(0U);
            const auto* const drained_owner1
                = implementation.driver_values.at(target).find(1U);
            require(quiet_resume.status == RunStatus::completed
                    && executor_state.resume_order.size()
                        == resume_order_before_drain
                    && implementation.get_signal(target).initial_value
                        == expected_current
                    && implementation.signal_last_values.at(target)
                        == initial_current
                    && implementation.driven_values.at(target)
                        == expected_current
                    && implementation.signal_value_revisions.at(target)
                        == committed_revision
                    && implementation.signal_events.at(target)
                        == committed_event
                    && implementation.signal_transactions.at(target)
                        == committed_transaction
                    && drained_owner0 != nullptr && drained_owner1 != nullptr
                    && drained_owner0->value == expected_owner0
                    && drained_owner1->value == expected_owner1
                    && lease_matches(retained_group_failure_leases[0U],
                        initial_current)
                    && lease_matches(retained_group_failure_leases[1U],
                        initial_current)
                    && lease_matches(retained_group_failure_leases[2U],
                        initial_current)
                    && lease_matches(retained_group_failure_leases[3U],
                        initial_owner0)
                    && lease_matches(retained_group_failure_leases[4U],
                        initial_owner1),
                "resuming after complete publication does not replay the failed Update");

            const auto retry0 = value_for(lower_width, kind, 25U);
            const auto retry1 = value_for(upper_width, kind, 26U);
            auto expected_retry = all_z_value(width, kind);
            insert_range(expected_retry, retry0, 0U, kind);
            insert_range(expected_retry, retry1, upper_offset, kind);
            auto expected_retry_owner0 = all_z_value(width, kind);
            auto expected_retry_owner1 = all_z_value(width, kind);
            insert_range(expected_retry_owner0, retry0, 0U, kind);
            insert_range(expected_retry_owner1, retry1, upper_offset, kind);
            const auto retry_order_start = executor_state.resume_order.size();
            interpreter.deposit_signal(input0, retry0);
            interpreter.deposit_signal(input1, retry1);
            const auto retry_result = interpreter.run();
            const auto retry_revision
                = implementation.signal_value_revisions.at(target);
            const auto* const retry_owner0
                = implementation.driver_values.at(target).find(0U);
            const auto* const retry_owner1
                = implementation.driver_values.at(target).find(1U);
            require(retry_result.status == RunStatus::completed
                    && executor_state.resume_order.size()
                        == retry_order_start + 2U
                    && implementation.get_signal(target).initial_value
                        == expected_retry
                    && implementation.signal_last_values.at(target)
                        == expected_current
                    && implementation.driven_values.at(target)
                        == expected_retry
                    && retry_owner0 != nullptr && retry_owner1 != nullptr
                    && retry_owner0->value == expected_retry_owner0
                    && retry_owner1->value == expected_retry_owner1
                    && retry_revision == committed_revision + 1U
                    && lease_matches(retained_group_failure_leases[0U],
                        initial_current)
                    && lease_matches(retained_group_failure_leases[1U],
                        initial_current)
                    && lease_matches(retained_group_failure_leases[2U],
                        initial_current)
                    && lease_matches(retained_group_failure_leases[3U],
                        initial_owner0)
                    && lease_matches(retained_group_failure_leases[4U],
                        initial_owner1),
                "a later two-owner Update succeeds after the fallback failure");
            result.fallback_failure_reached_after_publication = true;
            result.retry_completed = true;
            result.retry_target_revision_delta
                = retry_revision - committed_revision;
        }
        return result;
    }
    if (!result.completed) {
        return result;
    }
    // The SystemVerilog direct route can publish owners separately. The
    // generic pending-update route stages every owner before its ordered
    // signal commit, so its LAST value is the value before this round.
    require(result.resume_order.size() == 2U,
        "disjoint owner round records both checked writer callbacks");
    auto expected_previous = initial_current;
    if (generic_owner_updates) {
        // Generic Update resolves all staged owner records in one signal row.
    } else if (result.resume_order.front() == 0U) {
        insert_range(expected_previous, visible_next0, 0U, kind);
    } else {
        require(result.resume_order.front() == 1U,
            "first disjoint writer is one of the two original owners");
        insert_range(expected_previous, visible_next1,
            upper_offset, kind);
    }
    result.expected_previous = expected_previous;

    const auto* const after_state
        = implementation.region_authoritative_state_for_signal(target);
    result.current = implementation.get_signal(target).initial_value;
    result.previous = implementation.signal_last_values.at(target);
    result.stored = implementation.driven_values.at(target);
    const auto* const after_owner0
        = implementation.driver_values.at(target).find(0U);
    const auto* const after_owner1
        = implementation.driver_values.at(target).find(1U);
    require(after_owner0 != nullptr && after_owner1 != nullptr,
        "disjoint runtime publication retains the original owner records");
    result.owner0 = after_owner0->value;
    result.owner1 = after_owner1->value;
    result.expected_current = expected_current;
    result.bound_after
        = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 1U);
    if (result.bound_after) {
        result.published_roles0
            = OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, target, 0U);
        result.published_roles1
            = OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, target, 1U);
        result.published_sidecar_roles_match
            = result.published_roles0[0U] == expected_current
            && result.published_roles0[1U] == expected_previous
            && result.published_roles0[2U] == expected_current
            && result.published_roles0[3U] == expected_owner0
            && result.published_roles1[0U] == expected_current
            && result.published_roles1[1U] == expected_previous
            && result.published_roles1[2U] == expected_current
            && result.published_roles1[3U] == expected_owner1;
    }
    result.revision_delta
        = implementation.signal_value_revisions.at(target) - before_revision;
    result.sidecar_generation_delta = after_state == nullptr
        ? 0U : after_state->values().revision()
            - before_value_generation;
    result.owner_mirror_delta
        = implementation.systemverilog_wave_profile_a4_owner_mirrors
            - before_owner_mirrors;
    result.target_event_changed
        = implementation.signal_events.at(target) != before_event;
    result.target_transaction_changed
        = implementation.signal_transactions.at(target) != before_transaction;
    result.unrelated_write_keeps_disjoint_slots_bound
        = result.bound_after
        && implementation.get_signal(middle).initial_value == expected_current;

    require(result.resumes == std::array<std::size_t, 2U> { 1U, 1U },
        "both statically bound owners execute once in the same update round");
    require(result.current == expected_current
            && result.previous == expected_previous
            && result.stored == expected_current,
        "disjoint owner update preserves exact current/LAST/stored phases");
    require(result.owner0 == expected_owner0
            && result.owner1 == expected_owner1,
        "each original driver ID retains only its declared disjoint range");
    require(old_roles0[0U] == initial_current
            && role_texts(old_roles0) == old_role_texts0
            && role_texts(old_roles1) == old_role_texts1,
        "retained snapshots preserve all original roles during disjoint publication");

    if (failure_index && result.allocation_injected) {
        const auto retry0 = value_for(lower_width, kind, 25U);
        const auto retry1 = value_for(upper_width, kind, 26U);
        const auto& visible_retry0 = retry0;
        const auto& visible_retry1 = retry1;
        auto expected_retry = all_z_value(width, kind);
        insert_range(expected_retry, visible_retry0, 0U, kind);
        insert_range(expected_retry, visible_retry1, upper_offset, kind);
        interpreter.deposit_signal(input1, retry1);
        interpreter.deposit_signal(input0, retry0);
        const auto retry_order_start = result.resume_order.size();
        result.retry_completed
            = interpreter.run().status == RunStatus::completed;
        if (result.retry_completed) {
            require(executor_state.resume_order.size()
                    == retry_order_start + 2U,
                "retry records both disjoint owner callbacks");
            auto expected_retry_previous = expected_current;
            if (!generic_owner_updates
                && executor_state.resume_order[retry_order_start] == 0U) {
                insert_range(expected_retry_previous, visible_retry0,
                    0U, kind);
            } else if (!generic_owner_updates) {
                require(executor_state.resume_order[retry_order_start] == 1U,
                    "retry begins with an original disjoint owner");
                insert_range(expected_retry_previous, visible_retry1,
                    upper_offset, kind);
            }
            result.retry_current
                = implementation.get_signal(target).initial_value;
            result.retry_previous
                = implementation.signal_last_values.at(target);
            result.retry_stored
                = implementation.driven_values.at(target);
            const auto* const retry_owner0
                = implementation.driver_values.at(target).find(0U);
            const auto* const retry_owner1
                = implementation.driver_values.at(target).find(1U);
            require(retry_owner0 != nullptr && retry_owner1 != nullptr,
                "disjoint fallback retry preserves both owner records");
            result.retry_owner0 = retry_owner0->value;
            result.retry_owner1 = retry_owner1->value;
            result.expected_retry_current = expected_retry;
            auto expected_retry_owner0 = all_z_value(width, kind);
            auto expected_retry_owner1 = all_z_value(width, kind);
            insert_range(expected_retry_owner0, retry0, 0U, kind);
            insert_range(expected_retry_owner1, retry1, upper_offset, kind);
            require(result.retry_current == expected_retry
                    && result.retry_previous == expected_retry_previous
                    && result.retry_stored == expected_retry
                    && result.retry_owner0 == expected_retry_owner0
                    && result.retry_owner1 == expected_retry_owner1,
                "a failed disjoint preflight can retry both original owner writes");
            require(role_texts(old_roles0) == old_role_texts0
                    && role_texts(old_roles1) == old_role_texts1,
                "retained four-role snapshots survive fallback and retry");
        }
    } else if (result.bound_after && !force_and_release) {
        const PackedLogic4& observed = interpreter.signal_value(target);
        result.late_observation_demoted_slots
            = !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        const auto retry0 = value_for(lower_width, kind, 25U);
        const auto retry1 = value_for(upper_width, kind, 26U);
        const auto& visible_retry0 = retry0;
        const auto& visible_retry1 = retry1;
        auto expected_retry = all_z_value(width, kind);
        insert_range(expected_retry, visible_retry0, 0U, kind);
        insert_range(expected_retry, visible_retry1, upper_offset, kind);
        interpreter.deposit_signal(input0, retry0);
        interpreter.deposit_signal(input1, retry1);
        const auto retry_order_start = result.resume_order.size();
        result.retry_completed
            = interpreter.run().status == RunStatus::completed;
        if (result.retry_completed) {
            require(executor_state.resume_order.size()
                    == retry_order_start + 2U,
                "observed retry records both disjoint owner callbacks");
            auto expected_retry_previous = expected_current;
            if (!generic_owner_updates
                && executor_state.resume_order[retry_order_start] == 0U) {
                insert_range(expected_retry_previous, visible_retry0,
                    0U, kind);
            } else if (!generic_owner_updates) {
                require(executor_state.resume_order[retry_order_start] == 1U,
                    "observed retry begins with an original owner");
                insert_range(expected_retry_previous, visible_retry1,
                    upper_offset, kind);
            }
            result.retry_current
                = implementation.get_signal(target).initial_value;
            result.retry_previous
                = implementation.signal_last_values.at(target);
            result.retry_stored
                = implementation.driven_values.at(target);
            const auto* const retry_owner0
                = implementation.driver_values.at(target).find(0U);
            const auto* const retry_owner1
                = implementation.driver_values.at(target).find(1U);
            require(retry_owner0 != nullptr && retry_owner1 != nullptr,
                "late-observation retry retains both original driver IDs");
            result.retry_owner0 = retry_owner0->value;
            result.retry_owner1 = retry_owner1->value;
            result.expected_retry_current = expected_retry;
            auto expected_retry_owner0 = all_z_value(width, kind);
            auto expected_retry_owner1 = all_z_value(width, kind);
            insert_range(expected_retry_owner0, retry0, 0U, kind);
            insert_range(expected_retry_owner1, retry1, upper_offset, kind);
            result.late_observation_coherent
                = result.late_observation_demoted_slots
                && observed == expected_retry
                && result.retry_current == expected_retry
                && result.retry_previous == expected_retry_previous
                && result.retry_stored == expected_retry
                && result.retry_owner0 == expected_retry_owner0
                && result.retry_owner1 == expected_retry_owner1;
            require(result.late_observation_coherent,
                "a retained signal reference stays coherent after demoted owner writes");
        }
    }
    return result;
}

[[nodiscard]] bool run_scheduled_wide_preflight_failure(
    const std::size_t failure_index)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };

    constexpr std::uint32_t width = 129U;
    constexpr ValueKind kind = ValueKind::logic4;
    const PackedLogic4 initial_source { width, Logic4::zero };
    const PackedLogic4 initial_target { width, Logic4::z };
    const auto first_value = value_for(width, kind, 4U);
    const auto retry_value = value_for(width, kind, 5U);
    require(initial_target != first_value && first_value != retry_value,
        "scheduled failure fixture uses two distinct target transitions");

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "wide_failure.input", initial_source,
        ResolutionKind::none, kind });
    const auto target = interpreter.add_signal({
        "wide_failure.target", initial_target,
        ResolutionKind::sv_wire, kind });
    const auto sink = interpreter.add_signal({
        "wide_failure.sink", initial_target,
        ResolutionKind::sv_wire, kind });

    Process writer;
    writer.id = 0U;
    writer.name = "wide_failure.writer";
    writer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    writer.initialize = false;
    writer.register_count = 1U;
    writer.static_sensitivity = { { input, EdgeKind::any } };
    writer.driver_regions = { { target, 0U, 0U, true } };
    writer.operations = {
        ReadSignal { 0U, input },
        WriteUpdate {
            target, 0U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(writer)) == 0U,
        "scheduled wide failure writer keeps its registered identity");
    const auto& registered = interpreter.process_program(0U);
    std::size_t resumes { };
    interpreter.set_process_executor(0U,
        std::make_unique<ScheduledWideFailureExecutor>(input, target,
            failure_index, resumes,
            ProcessExecutorProgramBinding { registered, registered, 0U }));

    Process reader;
    reader.id = 1U;
    reader.name = "wide_failure.reader";
    reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    reader.initialize = false;
    reader.register_count = 1U;
    reader.static_sensitivity = { { target, EdgeKind::any } };
    reader.driver_regions = { { sink, 0U, 0U, true } };
    reader.operations = {
        ReadSignal { 0U, target },
        WriteUpdate {
            sink, 0U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(reader)) == 1U,
        "scheduled wide failure target has an ordinary reader");

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "wide failure fixture reaches its initial quiet point");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U),
        "wide failure fixture begins with real bound current/LAST/stored/raw roles");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(implementation.process_signal_access_is_complete(0U)
            && !implementation.process_region_kernel_eligible(0U)
            && !implementation.can_queue_systemverilog_wave(0U),
        "the executor runs through checked scheduling rather than a replacement kernel");
    const auto before = OwnedDriverDemotionTestAccess::packed_a4_values(
        interpreter, target, 0U);
    const auto before_texts = role_texts(before);
    const auto before_revision
        = implementation.signal_value_revisions.at(target);
    const auto before_event = implementation.signal_events.at(target);
    const auto before_transaction
        = implementation.signal_transactions.at(target);
    require(before[0U] == initial_target,
        "wide failure fixture begins at its declared current value");

    interpreter.deposit_signal(input, first_value);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U),
        "changing only the boundary input preserves target role bindings");
    RunStatus status = RunStatus::completed;
    try {
        status = interpreter.run().status;
    } catch (const std::bad_alloc&) {
        clear_allocation_failure();
        return false;
    }
    const bool injected = allocation_failure_was_injected();
    clear_allocation_failure();
    if (!injected || status != RunStatus::completed) {
        return false;
    }
    require(!OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U),
        "the failed scheduled role clone demotes before ordinary fallback");

    const auto after_failure = ordinary_role_values(interpreter, target, 0U);
    const auto after_failure_revision
        = implementation.signal_value_revisions.at(target);
    // Scheduler tracing is deliberately absent because it declines the wide
    // path. The exact change revision and transaction/event stamps below
    // verify one normal publication epoch after the transaction bookkeeping.
    const auto failed_event = implementation.signal_events.at(target);
    const auto failed_transaction
        = implementation.signal_transactions.at(target);
    const auto after_failure_texts = role_texts(before);
    if (resumes != 1U || after_failure_revision != before_revision + 1U
        || after_failure[0U] != first_value
        || after_failure[1U] != before[0U]
        || after_failure[2U] != first_value
        || after_failure[3U] != first_value
        || implementation.get_signal(sink).initial_value != first_value
        || failed_event == before_event
        || failed_transaction == before_transaction
        || !failed_event || !failed_transaction
        || *failed_event != *failed_transaction
        || before[0U] != initial_target
        || after_failure_texts != before_texts) {
        return false;
    }

    interpreter.deposit_signal(input, retry_value);
    if (interpreter.run().status != RunStatus::completed || resumes != 2U) {
        return false;
    }
    const auto after_retry = ordinary_role_values(interpreter, target, 0U);
    const auto after_retry_texts = role_texts(before);
    const auto retry_event = implementation.signal_events.at(target);
    const auto retry_transaction
        = implementation.signal_transactions.at(target);
    // Separate quiet deposits at one time/delta may share the event stamp;
    // the value revision proves the second publication occurred.
    return implementation.signal_value_revisions.at(target)
                == before_revision + 2U
        && after_retry[0U] == retry_value
        && after_retry[1U] == first_value
        && after_retry[2U] == retry_value
        && after_retry[3U] == retry_value
        && implementation.get_signal(sink).initial_value == retry_value
        && retry_event && retry_transaction
        && *retry_event == *retry_transaction
        && before[0U] == initial_target
        && after_retry_texts == before_texts;
}

void exercise_scheduled_preflight_failure_and_retry()
{
    require(run_scheduled_wide_preflight_failure(0U),
        "a scheduled wide role-clone allocation failure demotes before fallback and retry");
}

Process vhdl_projected_writer(
    const ProcessId id, const SignalId input, const SignalId output,
    const ValueKind kind = ValueKind::logic9,
    const SimulationTick delay = 0U,
    const SimulationTick rejection = 0U,
    const ProjectedDelayMode mode = ProjectedDelayMode::inertial)
{
    Process process;
    process.id = id;
    process.name = "wide_vhdl_projected_writer_" + std::to_string(id);
    process.language_standard = "2008";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = 1U;
    process.register_value_kinds = { kind };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteProjected { output, 0U, delay, rejection, mode },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process vhdl_projected_constant_whole_writer(
    const ProcessId id, const SignalId trigger, const SignalId output,
    const PackedLogic4& value)
{
    Process process;
    process.id = id;
    process.name = "unresolved_wide_vhdl_constant_writer_"
        + std::to_string(id);
    process.language_standard = "2008";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = true;
    process.register_count = 1U;
    process.register_value_kinds = { value.is_logic9()
        ? ValueKind::logic9 : ValueKind::logic4 };
    process.static_sensitivity = { { trigger, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        LoadConstant { 0U, value },
        WriteProjected { output, 0U, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process systemverilog_active_constant_whole_writer(
    const ProcessId id, const SignalId trigger, const SignalId output,
    const PackedLogic4& value)
{
    Process process;
    process.id = id;
    process.name = "unresolved_wide_sv_active_constant_writer_"
        + std::to_string(id);
    process.language_standard = "2017";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.initialize = true;
    process.register_count = 1U;
    process.register_value_kinds = { value.is_logic9()
        ? ValueKind::logic9 : ValueKind::logic4 };
    process.static_sensitivity = { { trigger, EdgeKind::any } };
    process.driver_regions = { { output, 0U,
        static_cast<std::uint32_t>(value.width()), true } };
    process.operations = {
        LoadConstant { 0U, value },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

Process vhdl_projected_two_whole_writer(const ProcessId id,
    const SignalId input_a, const SignalId input_b, const SignalId output_a,
    const SignalId output_b, const SimulationTick delay_b = 0U)
{
    Process process;
    process.id = id;
    process.name = "wide_vhdl_two_whole_writer_" + std::to_string(id);
    process.language_standard = "2008";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = 2U;
    process.register_value_kinds = { ValueKind::logic4, ValueKind::logic4 };
    process.static_sensitivity = {
        { input_a, EdgeKind::any }, { input_b, EdgeKind::any },
    };
    process.driver_regions = {
        { output_a, 0U, 0U, true }, { output_b, 0U, 0U, true },
    };
    process.operations = {
        ReadSignal { 0U, input_a },
        ReadSignal { 1U, input_b },
        WriteProjected { output_a, 0U, 0U, 0U,
            ProjectedDelayMode::inertial },
        WriteProjected { output_b, 1U, delay_b, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

struct UnresolvedWideMultiOutputFrame {
    std::array<PackedLogic4, 2U> current;
    std::array<PackedLogic4, 2U> previous;
    std::array<PackedLogic4, 2U> stored;
    std::array<std::optional<std::pair<SimulationTick, std::uint64_t>>, 2U>
        event;
    std::array<std::optional<std::pair<SimulationTick, std::uint64_t>>, 2U>
        transaction;
    std::array<std::uint64_t, 2U> value_revision { };
    SimulationTick now { };
    std::uint64_t delta { };

    friend bool operator==(const UnresolvedWideMultiOutputFrame&,
        const UnresolvedWideMultiOutputFrame&) = default;
};

struct UnresolvedWideMultiOutputResult {
    std::vector<UnresolvedWideMultiOutputFrame> frames;
    bool shared_owner_process { };
    bool aliases_bound_at_start { };
    bool aliases_bound_after_each_update { true };
    bool aliases_point_to_stored { };
    bool no_driver_records { };
    std::size_t physical_slots { };
};

UnresolvedWideMultiOutputResult run_unresolved_wide_multioutput_owner(
    const std::uint32_t width, const char* const wide_policy,
    const SimulationTick delay_b = 0U)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", wide_policy };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    const PackedLogic4 initial_value { width, Logic4::zero };
    Interpreter interpreter;
    const auto input_a = interpreter.add_signal({ "multiowner.input_a",
        initial_value, ResolutionKind::none, ValueKind::logic4 });
    const auto input_b = interpreter.add_signal({ "multiowner.input_b",
        initial_value, ResolutionKind::none, ValueKind::logic4 });
    const auto output_a = interpreter.add_signal({ "multiowner.output_a",
        initial_value, ResolutionKind::none, ValueKind::logic4 });
    const auto output_b = interpreter.add_signal({ "multiowner.output_b",
        initial_value, ResolutionKind::none, ValueKind::logic4 });
    const auto writer = vhdl_projected_two_whole_writer(0U,
        input_a, input_b, output_a, output_b, delay_b);
    require(interpreter.add_process(writer) == 0U,
        "the multioutput VHDL writer retains one original ProcessId");
    const auto& registered_writer = interpreter.process_program(0U);
    const auto second_output_operation
        = registered_writer.operations.expanded(3U);
    const auto* const second_projected_write
        = operation_get_if<WriteProjected>(&second_output_operation);
    require(second_projected_write != nullptr
            && second_projected_write->signal == output_b
            && second_projected_write->delay == delay_b,
        "the registered second output retains its signal and delay");

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "the multioutput writer starts at its ordinary static wait");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(implementation.region_graph.has_value(),
        "the runtime multioutput witness retains its RegionGraph");
    const auto graph_signals = implementation.region_graph->signals();
    require(output_a < graph_signals.size()
            && output_b < graph_signals.size(),
        "both runtime multioutput signals remain in the RegionGraph");
    const auto& node_a = graph_signals[output_a];
    const auto& node_b = graph_signals[output_b];
    UnresolvedWideMultiOutputResult result;
    result.shared_owner_process = node_a.writers.size() == 1U
        && node_b.writers.size() == 1U
        && node_a.writers.front().process == 0U
        && node_b.writers.front().process == 0U;
    auto* const state_a
        = implementation.region_authoritative_state_for_signal(output_a);
    auto* const state_b
        = implementation.region_authoritative_state_for_signal(output_b);
    result.aliases_bound_at_start = result.shared_owner_process
        && state_a != nullptr && state_a == state_b
        && state_a->values().packed_signal_slots_bound(output_a)
        && state_a->values().packed_signal_slots_bound(output_b)
        && state_a->values().packed_owner_slot_bound(output_a, 0U)
        && state_a->values().packed_owner_slot_bound(output_b, 0U);
    result.no_driver_records
        = implementation.driver_values.at(output_a).find(0U) == nullptr
        && implementation.driver_values.at(output_b).find(0U) == nullptr;
    if (state_a != nullptr && state_a == state_b) {
        const auto& values = state_a->values();
        result.physical_slots = values.packed_slot_count();
        result.aliases_point_to_stored
            = values.owner_value(output_a, 0U) == values.stored(output_a)
            && values.owner_value(output_b, 0U) == values.stored(output_b);
    }
    const bool policy_enabled = wide_policy == nullptr
        || std::string_view { wide_policy } == "1";
    const bool expected_alias = policy_enabled && delay_b == 0U;
    require(result.aliases_bound_at_start == expected_alias
            && result.no_driver_records,
        "only pure zero-delay multioutput writers bind without synthetic drivers");
    if (result.aliases_bound_at_start) {
        require(result.physical_slots == 6U && result.aliases_point_to_stored,
            "each output contributes three physical planes and one logical alias");
    }

    const auto capture = [&] {
        UnresolvedWideMultiOutputFrame frame;
        const std::array<SignalId, 2U> outputs { output_a, output_b };
        for (std::size_t index = 0U; index < outputs.size(); ++index) {
            const auto signal = outputs[index];
            frame.current[index]
                = implementation.get_signal(signal).initial_value;
            frame.previous[index]
                = implementation.signal_last_values.at(signal);
            frame.stored[index] = implementation.driven_values.at(signal);
            frame.event[index] = implementation.signal_events.at(signal);
            frame.transaction[index]
                = implementation.signal_transactions.at(signal);
            frame.value_revision[index]
                = implementation.signal_value_revisions.at(signal);
        }
        frame.now = interpreter.scheduler().now();
        frame.delta = interpreter.scheduler().delta();
        auto* const current_state
            = implementation.region_authoritative_state_for_signal(output_a);
        result.aliases_bound_after_each_update
            = result.aliases_bound_after_each_update
            && current_state != nullptr
            && current_state
                == implementation.region_authoritative_state_for_signal(output_b)
            && current_state->values().packed_owner_slot_bound(output_a, 0U)
            && current_state->values().packed_owner_slot_bound(output_b, 0U)
            && current_state->values().owner_value(output_a, 0U)
                == current_state->values().stored(output_a)
            && current_state->values().owner_value(output_b, 0U)
                == current_state->values().stored(output_b);
        result.frames.push_back(std::move(frame));
    };

    capture();
    for (std::size_t phase = 1U; phase <= 3U; ++phase) {
        std::string digits_a(width, '0');
        std::string digits_b(width, '0');
        for (std::size_t bit = 0U; bit < width; ++bit) {
            digits_a[bit] = (bit + phase) % 3U == 0U ? '1' : '0';
            digits_b[bit] = (bit + 2U * phase) % 4U < 2U ? '1' : '0';
        }
        const auto value_a = PackedLogic4::from_msb_string(digits_a);
        const auto value_b = PackedLogic4::from_msb_string(digits_b);
        interpreter.deposit_signal(input_a, value_a);
        interpreter.deposit_signal(input_b, value_b);
        require(interpreter.run().status == RunStatus::completed,
            "both inputs settle through the same ordinary projected process");
        capture();
        const auto& frame = result.frames.back();
        require(frame.current[0U] == value_a && frame.current[1U] == value_b
                && frame.stored[0U] == value_a && frame.stored[1U] == value_b,
            "the multioutput process publishes each exact independent value");
    }
    return result;
}

void exercise_unresolved_wide_multioutput_owner()
{
    for (const auto width : { 65U, 129U }) {
        const auto admitted
            = run_unresolved_wide_multioutput_owner(width, nullptr);
        const auto checked
            = run_unresolved_wide_multioutput_owner(width, "0");
        require(admitted.shared_owner_process
                && admitted.aliases_bound_at_start
                && admitted.aliases_bound_after_each_update
                && admitted.aliases_point_to_stored
                && admitted.no_driver_records
                && admitted.physical_slots == 6U
                && !checked.aliases_bound_at_start
                && admitted.frames == checked.frames,
            "two whole outputs from one ProcessId retain independent three-role "
            "state and exact checked-path metadata parity");
    }
}

void exercise_unresolved_wide_multioutput_delayed_decline()
{
    for (const auto width : { 65U, 129U }) {
        const auto enabled
            = run_unresolved_wide_multioutput_owner(width, nullptr, 5U);
        const auto checked
            = run_unresolved_wide_multioutput_owner(width, "0", 5U);
        require(enabled.shared_owner_process
                && !enabled.aliases_bound_at_start
                && !enabled.aliases_bound_after_each_update
                && !enabled.aliases_point_to_stored
                && enabled.physical_slots == 0U
                && enabled.frames == checked.frames,
            "one delayed sibling write keeps the entire original process on "
            "the checked route with exact current/LAST/event/transaction parity");
    }
}

struct UnresolvedWideWholeOwnerResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::uint64_t value_revision { };
    bool owner_alias_bound_at_start { };
    bool owner_alias_bound_after_equal_write { };
    bool owner_alias_demoted_by_observation { };
    bool owner_alias_stays_demoted_after_force { };
    bool owner_alias_recertified_after_force { };
    bool owner_alias_has_no_physical_copy { };
    bool has_driver_record { };
    std::uint64_t unresolved_owner_alias_commits { };
};

UnresolvedWideWholeOwnerResult run_unresolved_wide_whole_owner(
    const std::uint32_t width, const char* const wide_policy,
    const ValueKind kind, const bool expose_public_reference = true,
    const bool systemverilog_active = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", wide_policy };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };

    const auto initial_target = value_for(width, kind, 0U);
    const auto projected_value = value_for(width, kind, 1U);
    const PackedLogic4 initial_trigger { 1U, Logic4::zero };
    const PackedLogic4 changed_trigger { 1U, Logic4::one };

    Interpreter interpreter;
    const auto trigger = interpreter.add_signal({ "unresolved_wide.trigger",
        initial_trigger, ResolutionKind::none, ValueKind::logic4 });
    const auto target = interpreter.add_signal({ "unresolved_wide.target",
        initial_target, ResolutionKind::none, kind });
    const auto writer = systemverilog_active
        ? systemverilog_active_constant_whole_writer(
              0U, trigger, target, projected_value)
        : vhdl_projected_constant_whole_writer(
              0U, trigger, target, projected_value);
    require(interpreter.add_process(writer) == 0U,
        "the unresolved whole writer retains its original owner ID");
    if (systemverilog_active) {
        Process reader;
        reader.id = 1U;
        reader.name = "unresolved_wide.boundary_reader";
        reader.scheduling_domain = ProcessSchedulingDomain::generic;
        reader.initialize = false;
        reader.register_count = 1U;
        reader.register_value_kinds = { kind };
        reader.static_sensitivity = { { target, EdgeKind::any } };
        DebugLocal seen;
        seen.name = "seen";
        seen.type_name = "logic";
        seen.register_id = 0U;
        seen.width = width;
        seen.value_kind = kind;
        reader.debug_locals = { std::move(seen) };
        reader.operations = {
            ReadSignal { 0U, target },
            WaitSensitivity { },
            Jump { 0U },
        };
        require(interpreter.add_process(std::move(reader)) == 1U,
            "the Active owner has a real reader outside its component");
    }

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "the unresolved whole writer reaches its initial quiet point");
    if (systemverilog_active) {
        require(interpreter.read_debug_local(1U, 0U) == projected_value,
            "the outside reader sees the original Active publication");
    }

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    if (systemverilog_active) {
        require(implementation.region_graph.has_value()
                && implementation.region_graph->processes().size() == 2U
                && implementation.region_graph->processes().front()
                    .scheduling_domain
                    == ProcessSchedulingDomain::systemverilog
                && implementation.region_graph->processes().front()
                    .update_kind
                    == RegionUpdateKind::systemverilog_active,
            "the active owner is certified in the original SV Active domain");
        const auto& certificates = implementation.region_graph
            ->certificate_inventory().components;
        const auto boundary_certificate = std::ranges::find_if(
            certificates, [target](const auto& certificate) {
                return std::ranges::find(
                    certificate.boundary_signals, target)
                    != certificate.boundary_signals.end();
            });
        require(boundary_certificate != certificates.end()
                && std::ranges::find(
                       boundary_certificate->members, ProcessId { 1U })
                    == boundary_certificate->members.end()
                && std::ranges::find(
                       boundary_certificate
                           ->structural_internal_signal_candidates,
                       target)
                    == boundary_certificate
                        ->structural_internal_signal_candidates.end(),
            "the Active owner fixture exercises a boundary alias, not the A2 internal path");
    }
    auto* const initial_state
        = implementation.region_authoritative_state_for_signal(target);
    UnresolvedWideWholeOwnerResult result;
    result.owner_alias_bound_at_start = initial_state != nullptr
        && initial_state->values().packed_signal_slots_bound(target)
        && initial_state->values().packed_owner_slot_bound(target, 0U);
    result.has_driver_record
        = implementation.driver_values.at(target).find(0U) != nullptr;
    if (initial_state != nullptr) {
        const auto& values = initial_state->values();
        // This isolated graph has one wide signal and one narrow sensitivity
        // trigger. The owner is a logical alias of stored, not a fourth
        // PackedLogic4 slot.
        result.owner_alias_has_no_physical_copy
            = values.packed_slot_count() == 3U
            && values.packed_owner_slot_bound(target, 0U)
            && values.owner_value(target, 0U) == values.stored(target);
    }
    result.unresolved_owner_alias_commits
        = implementation.systemverilog_wave_profile_a4_unresolved_owner_alias_commits;
    if (systemverilog_active) {
        const bool policy_enabled
            = wide_policy == nullptr
            || std::string_view { wide_policy } == "1";
        require(result.unresolved_owner_alias_commits
                == (policy_enabled ? 1U : 0U),
            "the first SV Active no-resolution write uses the certified "
            "owner alias only when enabled");
    }
    require(result.owner_alias_bound_at_start
            == (wide_policy == nullptr || std::string_view { wide_policy } == "1")
            && result.owner_alias_has_no_physical_copy
                == result.owner_alias_bound_at_start
            && !result.has_driver_record,
        "only the admitted no-resolution owner aliases the three physical roles");
    require(implementation.get_signal(target).initial_value == projected_value
            && implementation.signal_last_values.at(target) == initial_target
            && implementation.driven_values.at(target) == projected_value,
        "the initial whole write preserves current/LAST/stored semantics");
    const auto first_event = implementation.signal_events.at(target);
    const auto first_transaction
        = implementation.signal_transactions.at(target);
    require(first_event && first_transaction,
        "the first whole assignment retains its normal stamps");

    interpreter.scheduler().schedule_at(1U, SchedulerPhase::active, 0U,
        [&](Scheduler&) {
            interpreter.deposit_signal(trigger, changed_trigger);
        });
    require(interpreter.run().status == RunStatus::completed,
        "a later sensitivity wake commits through its ordinary language queue");
    auto& equal_write_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const equal_write_state
        = equal_write_implementation.region_authoritative_state_for_signal(
            target);
    result.owner_alias_bound_after_equal_write = equal_write_state != nullptr
        && equal_write_state->values().packed_owner_slot_bound(target, 0U);
    const auto equal_event = equal_write_implementation.signal_events.at(target);
    const auto equal_transaction
        = equal_write_implementation.signal_transactions.at(target);
    require(equal_write_implementation.get_signal(target).initial_value
                == projected_value
            && equal_write_implementation.signal_last_values.at(target)
                == initial_target
            && equal_write_implementation.driven_values.at(target)
                == projected_value
            && first_event == equal_event
            && equal_transaction && first_transaction != equal_transaction,
        "an equal same-owner transaction advances transaction metadata only");
    if (result.owner_alias_bound_after_equal_write) {
        const auto& equal_values = equal_write_state->values();
        result.owner_alias_has_no_physical_copy
            = result.owner_alias_has_no_physical_copy
            && equal_values.packed_slot_count() == 3U
            && equal_values.owner_value(target, 0U)
                == equal_values.stored(target);
        require(result.owner_alias_has_no_physical_copy,
            "the stored-owner alias follows an equal whole transaction");
    }

    if (expose_public_reference) {
        const auto observed = interpreter.signal_value(target);
        auto& observed_implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        auto* const observed_state
            = observed_implementation.region_authoritative_state_for_signal(
                target);
        result.owner_alias_demoted_by_observation
            = observed == projected_value
            && (observed_state == nullptr
                || !observed_state->values().packed_owner_slot_bound(
                    target, 0U));
        require(result.owner_alias_demoted_by_observation,
            "public reference observation demotes the unresolved owner alias");
    }

    const auto forced_value = value_for(width, kind, 2U);
    interpreter.force_signal(target, forced_value);
    auto& observed_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    if (expose_public_reference) {
        require(interpreter.signal_value_snapshot(target) == forced_value,
            "force remains visible after ordinary owner storage is demoted");
    } else {
        require(observed_implementation.get_signal(target).initial_value
                == forced_value,
            "force remains visible without creating a public observation");
    }
    auto* const forced_state
        = observed_implementation.region_authoritative_state_for_signal(target);
    result.owner_alias_stays_demoted_after_force = forced_state == nullptr
        || !forced_state->values().packed_owner_slot_bound(target, 0U);
    interpreter.release_signal(target);
    if (expose_public_reference) {
        require(interpreter.signal_value_snapshot(target) == projected_value,
            "release restores the unresolved signal's stored value");
    } else {
        require(observed_implementation.get_signal(target).initial_value
                == projected_value,
            "release restores the unresolved signal without observation");
    }

    interpreter.deposit_signal(trigger, initial_trigger);
    require(interpreter.run().status == RunStatus::completed,
        "the checked post-observation retry preserves projected publication");
    auto& final_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const final_state
        = final_implementation.region_authoritative_state_for_signal(target);
    result.owner_alias_recertified_after_force = final_state != nullptr
        && final_state->values().packed_owner_slot_bound(target, 0U);
    result.current = final_implementation.get_signal(target).initial_value;
    result.previous = final_implementation.signal_last_values.at(target);
    result.stored = final_implementation.driven_values.at(target);
    result.event = final_implementation.signal_events.at(target);
    result.transaction = final_implementation.signal_transactions.at(target);
    result.value_revision
        = final_implementation.signal_value_revisions.at(target);
    result.unresolved_owner_alias_commits
        = final_implementation.systemverilog_wave_profile_a4_unresolved_owner_alias_commits;
    result.has_driver_record
        = final_implementation.driver_values.at(target).find(0U) != nullptr;
    return result;
}

bool same_unresolved_wide_whole_owner_result(
    const UnresolvedWideWholeOwnerResult& left,
    const UnresolvedWideWholeOwnerResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.event == right.event
        && left.transaction == right.transaction
        && left.value_revision == right.value_revision
        && left.has_driver_record == right.has_driver_record;
}

void exercise_unresolved_wide_whole_owner_alias()
{
    exercise_unresolved_stored_owner_lease(ValueKind::logic4);
    exercise_unresolved_stored_owner_lease(ValueKind::logic9);
    for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
        for (const auto width : { 65U, 129U, 256U, 1024U }) {
            if (kind == ValueKind::logic9 && width > 129U) {
                continue;
            }
            const auto admitted
                = run_unresolved_wide_whole_owner(width, nullptr, kind);
            const auto checked
                = run_unresolved_wide_whole_owner(width, "0", kind);
            require(admitted.owner_alias_bound_at_start
                    && admitted.owner_alias_bound_after_equal_write
                    && admitted.owner_alias_has_no_physical_copy
                    && admitted.owner_alias_demoted_by_observation
                    && admitted.owner_alias_stays_demoted_after_force
                    && !admitted.has_driver_record
                    && !checked.owner_alias_bound_at_start
                    && !checked.owner_alias_bound_after_equal_write
                    && !checked.has_driver_record
                    && same_unresolved_wide_whole_owner_result(
                        admitted, checked),
                "unresolved wide whole owners preserve exact-kind three-role "
                "storage, Logic9 upper planes, equal transactions, force/"
                "observation demotion, and checked parity");
        }
    }
    for (const auto width : { 65U, 129U }) {
        const auto recertified
            = run_unresolved_wide_whole_owner(
                width, nullptr, ValueKind::logic9, false);
        const auto checked
            = run_unresolved_wide_whole_owner(
                width, "0", ValueKind::logic9, false);
        require(recertified.owner_alias_stays_demoted_after_force
                && recertified.owner_alias_recertified_after_force
                && !checked.owner_alias_recertified_after_force
                && same_unresolved_wide_whole_owner_result(
                    recertified, checked),
            "a quiet post-force Logic9 projected write recertifies the exact "
            "stored-owner alias without public-reference exposure");
    }
    for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
        for (const auto width : { 65U, 129U }) {
            const auto admitted = run_unresolved_wide_whole_owner(
                width, nullptr, kind, true, true);
            const auto checked = run_unresolved_wide_whole_owner(
                width, "0", kind, true, true);
            require(admitted.owner_alias_bound_at_start
                    && admitted.owner_alias_bound_after_equal_write
                    && admitted.owner_alias_has_no_physical_copy
                    && admitted.owner_alias_demoted_by_observation
                    && admitted.owner_alias_stays_demoted_after_force
                    && admitted.unresolved_owner_alias_commits == 2U
                    && !admitted.has_driver_record
                    && !checked.owner_alias_bound_at_start
                    && !checked.owner_alias_bound_after_equal_write
                    && checked.unresolved_owner_alias_commits == 0U
                    && !checked.has_driver_record
                    && same_unresolved_wide_whole_owner_result(
                        admitted, checked),
                "SV Active no-resolution whole owners alias stored with exact "
                "current/LAST/stamps, two certified updates, and checked "
                "observation/force demotion parity");
        }
    }
}

struct UnresolvedWideCancellationResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SimulationTick final_time { };
    bool remained_unchanged_at_first_maturity { };
    bool owner_alias_was_declined { };
};

UnresolvedWideCancellationResult run_unresolved_wide_owner_cancellation(
    const char* const wide_policy)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", wide_policy };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t width = 129U;
    std::string initial_digits(width, '0');
    for (std::size_t bit = 0U; bit < width; bit += 2U) {
        initial_digits[bit] = '1';
    }
    const auto initial_value = PackedLogic4::from_msb_string(initial_digits);
    const PackedLogic4 first_value { width, Logic4::one };
    const PackedLogic4 second_value { width, Logic4::zero };
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "unresolved_cancel.input",
        initial_value, ResolutionKind::none, ValueKind::logic4 });
    const auto target = interpreter.add_signal({ "unresolved_cancel.target",
        initial_value, ResolutionKind::none, ValueKind::logic4 });
    require(interpreter.add_process(vhdl_projected_writer(0U, input, target,
                ValueKind::logic4, 5U, 5U)) == 0U,
        "delayed cancellation keeps one original whole-owner process");
    interpreter.start();

    interpreter.scheduler().schedule_at(0U, SchedulerPhase::active, 0U,
        [&](Scheduler&) { interpreter.deposit_signal(input, first_value); });
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "the first inertial write remains queued for its future maturity");
    interpreter.scheduler().schedule_at(1U, SchedulerPhase::active, 0U,
        [&](Scheduler&) { interpreter.deposit_signal(input, second_value); });
    require(interpreter.run(5U).status == RunStatus::time_limit,
        "the second same-owner write replaces the first before maturity");

    auto& at_first_maturity
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    UnresolvedWideCancellationResult result;
    result.remained_unchanged_at_first_maturity
        = at_first_maturity.get_signal(target).initial_value == initial_value
        && at_first_maturity.driven_values.at(target) == initial_value;
    auto* const state
        = at_first_maturity.region_authoritative_state_for_signal(target);
    result.owner_alias_was_declined = state == nullptr
        || !state->values().packed_owner_slot_bound(target, 0U);
    require(interpreter.run(6U).status == RunStatus::completed,
        "the surviving inertial transaction matures at its original new time");
    auto& final_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    result.current = final_implementation.get_signal(target).initial_value;
    result.previous = final_implementation.signal_last_values.at(target);
    result.stored = final_implementation.driven_values.at(target);
    result.event = final_implementation.signal_events.at(target);
    result.transaction = final_implementation.signal_transactions.at(target);
    result.final_time = interpreter.scheduler().now();
    require(result.remained_unchanged_at_first_maturity
            && result.owner_alias_was_declined
            && result.current == second_value
            && result.previous == initial_value
            && result.stored == second_value
            && result.event && result.transaction
            && result.event->first == 6U
            && result.transaction->first == 6U
            && result.final_time == 6U,
        "same-owner inertial cancellation keeps the old tick silent and the "
        "replacement's ordinary stamps exact");
    return result;
}

bool same_unresolved_wide_cancellation_result(
    const UnresolvedWideCancellationResult& left,
    const UnresolvedWideCancellationResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.event == right.event
        && left.transaction == right.transaction
        && left.final_time == right.final_time
        && left.remained_unchanged_at_first_maturity
            == right.remained_unchanged_at_first_maturity
        && left.owner_alias_was_declined == right.owner_alias_was_declined;
}

void exercise_unresolved_wide_same_owner_cancellation()
{
    const auto enabled = run_unresolved_wide_owner_cancellation(nullptr);
    const auto checked = run_unresolved_wide_owner_cancellation("0");
    require(same_unresolved_wide_cancellation_result(enabled, checked),
        "delayed unresolved writes retain same-owner inertial cancellation "
        "on the checked path");
}

struct UnresolvedWideMultipleOwnerResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    bool first_owner_alias_bound { };
    bool second_owner_alias_bound { };
    bool has_driver_record { };
};

UnresolvedWideMultipleOwnerResult run_unresolved_wide_multiple_owner_fallback(
    const char* const wide_policy)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", wide_policy };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t width = 129U;
    const PackedLogic4 initial_value { width, Logic4::zero };
    const PackedLogic4 next_value { width, Logic4::one };
    Interpreter interpreter;
    const auto first_input = interpreter.add_signal({
        "unresolved_multiple.first_input", initial_value,
        ResolutionKind::none, ValueKind::logic4 });
    const auto second_input = interpreter.add_signal({
        "unresolved_multiple.second_input", initial_value,
        ResolutionKind::none, ValueKind::logic4 });
    const auto target = interpreter.add_signal({
        "unresolved_multiple.target", initial_value,
        ResolutionKind::none, ValueKind::logic4 });
    require(interpreter.add_process(vhdl_projected_writer(
                0U, first_input, target, ValueKind::logic4)) == 0U
            && interpreter.add_process(vhdl_projected_writer(
                1U, second_input, target, ValueKind::logic4)) == 1U,
        "ambiguous unresolved whole writers retain both process identities");
    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "both unresolved whole writers reach their initial wait");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto aliases_target = [&] {
        auto* const state
            = implementation.region_authoritative_state_for_signal(target);
        return state != nullptr
            && (state->values().packed_owner_slot_bound(target, 0U)
                || state->values().packed_owner_slot_bound(target, 1U));
    };
    require(!aliases_target(),
        "an ambiguous unresolved signal does not receive a sole-owner alias");

    interpreter.deposit_signal(first_input, next_value);
    require(interpreter.run().status == RunStatus::completed,
        "the first ambiguous owner uses ordinary projected publication");
    const auto first_event = implementation.signal_events.at(target);
    const auto first_transaction
        = implementation.signal_transactions.at(target);
    require(first_event && first_transaction
            && implementation.get_signal(target).initial_value == next_value
            && !aliases_target(),
        "the first process update preserves checked unresolved storage");

    interpreter.scheduler().schedule_at(1U, SchedulerPhase::active, 1U,
        [&](Scheduler&) {
            interpreter.deposit_signal(second_input, next_value);
        });
    require(interpreter.run().status == RunStatus::completed,
        "the second ambiguous owner uses ordinary projected publication");
    UnresolvedWideMultipleOwnerResult result;
    result.current = implementation.get_signal(target).initial_value;
    result.previous = implementation.signal_last_values.at(target);
    result.stored = implementation.driven_values.at(target);
    result.event = implementation.signal_events.at(target);
    result.transaction = implementation.signal_transactions.at(target);
    result.first_owner_alias_bound
        = implementation.region_authoritative_state_for_signal(target) != nullptr
        && implementation.region_authoritative_state_for_signal(target)
            ->values().packed_owner_slot_bound(target, 0U);
    result.second_owner_alias_bound
        = implementation.region_authoritative_state_for_signal(target) != nullptr
        && implementation.region_authoritative_state_for_signal(target)
            ->values().packed_owner_slot_bound(target, 1U);
    result.has_driver_record
        = implementation.driver_values.at(target).find(0U) != nullptr
        || implementation.driver_values.at(target).find(1U) != nullptr;
    require(result.current == next_value
            && result.previous == initial_value
            && result.stored == next_value
            && result.event == first_event
            && result.transaction && result.transaction != first_transaction
            && !result.first_owner_alias_bound
            && !result.second_owner_alias_bound
            && !result.has_driver_record,
        "two ambiguous unresolved writers preserve equal-transaction behavior "
        "without inventing a raw driver record");
    return result;
}

bool same_unresolved_wide_multiple_owner_result(
    const UnresolvedWideMultipleOwnerResult& left,
    const UnresolvedWideMultipleOwnerResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.event == right.event
        && left.transaction == right.transaction
        && left.first_owner_alias_bound == right.first_owner_alias_bound
        && left.second_owner_alias_bound == right.second_owner_alias_bound
        && left.has_driver_record == right.has_driver_record;
}

void exercise_unresolved_wide_multiple_owner_fallback()
{
    const auto enabled = run_unresolved_wide_multiple_owner_fallback(nullptr);
    const auto checked = run_unresolved_wide_multiple_owner_fallback("0");
    require(same_unresolved_wide_multiple_owner_result(enabled, checked),
        "ambiguous unresolved owners retain checked parity under default A4");
}

Process vhdl_projected_slice_writer(
    ProcessId id, SignalId input, SignalId output, std::uint32_t offset,
    std::uint32_t width, ValueKind kind, SimulationTick delay,
    SimulationTick rejection, ProjectedDelayMode mode,
    std::optional<SignalId> dynamic_index_signal, bool extra_waveform_write);

struct UnresolvedWideMixedOwnerResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::uint64_t value_revision { };
    bool owner_alias_bound_before_batch { };
    bool provenance_latched_ambiguous { };
    bool owner_alias_demoted_after_batch { };
    bool provenance_cleared_after_batch { };
    bool has_driver_record { };
};

UnresolvedWideMixedOwnerResult run_unresolved_wide_mixed_owner_batch(
    const char* const wide_policy)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", wide_policy };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_disabled {
        "FSIM_PROFILE_SV_WAVES", nullptr };

    constexpr std::uint32_t width = 129U;
    const PackedLogic4 initial_value { width, Logic4::zero };
    const PackedLogic4 first_value { width, Logic4::one };
    const PackedLogic4 no_owner_value { width, Logic4::zero };
    const auto final_value = value_for(width, ValueKind::logic4, 23U);
    require(final_value != initial_value && final_value != first_value,
        "mixed-owner batch has distinguishable ordered whole values");

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "unresolved_mixed.input", initial_value,
        ResolutionKind::none, ValueKind::logic4 });
    const auto target = interpreter.add_signal({
        "unresolved_mixed.target", initial_value,
        ResolutionKind::none, ValueKind::logic4 });
    require(interpreter.add_process(vhdl_projected_writer(
                0U, input, target, ValueKind::logic4)) == 0U,
        "the mixed provenance fixture retains its registered VHDL owner");
    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "the mixed provenance writer reaches its initial quiet point");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto alias_is_bound = [&] {
        auto* const state
            = implementation.region_authoritative_state_for_signal(target);
        return state != nullptr
            && state->values().packed_owner_slot_bound(target, 0U);
    };
    UnresolvedWideMixedOwnerResult result;
    result.owner_alias_bound_before_batch = alias_is_bound();

    // Exercise the coalescer's monotonic provenance rule directly before
    // staging the matching pending Update sequence. A missing owner in the
    // middle must not be repaired by a later update from the first owner.
    implementation.unresolved_update_owner_provenance.resize(
        implementation.signals.size());
    implementation.unresolved_update_owner_provenance_signals.reserve(
        implementation.signals.size());
    implementation.note_unresolved_update_owner(
        target, ProcessId { 0U }, true);
    implementation.note_unresolved_update_owner(target, std::nullopt, true);
    implementation.note_unresolved_update_owner(
        target, ProcessId { 0U }, true);
    const auto& staged_provenance
        = implementation.unresolved_update_owner_provenance.at(target);
    result.provenance_latched_ambiguous = staged_provenance.touched
        && staged_provenance.has_process
        && staged_provenance.process == ProcessId { 0U }
        && staged_provenance.ambiguous;
    require(result.provenance_latched_ambiguous,
        "owner/no-owner/same-owner provenance remains ambiguous");
    implementation.clear_unresolved_update_owner_provenance();

    const auto before_event = implementation.signal_events.at(target);
    const auto before_transaction
        = implementation.signal_transactions.at(target);
    bool inspected_after_commit = false;
    interpreter.scheduler().schedule_at(1U, SchedulerPhase::active, 0U,
        [&](Scheduler&) {
            implementation.stage_update_unrouted(
                ProcessId { 0U }, target, first_value, std::nullopt);
            implementation.stage_update_unrouted(
                std::nullopt, target, no_owner_value, std::nullopt);
            implementation.stage_update_unrouted(
                ProcessId { 0U }, target, final_value, std::nullopt);
            require(implementation.pending_updates.size() == 3U,
                "mixed provenance updates share one ordinary Update-phase batch");
            interpreter.scheduler().schedule(SchedulerPhase::update,
                std::numeric_limits<StableOrder>::max(), [&](Scheduler&) {
                    inspected_after_commit = true;
                    result.owner_alias_demoted_after_batch = !alias_is_bound();
                });
        });
    require(interpreter.run().status == RunStatus::completed,
        "the mixed owner batch commits through the checked unresolved path");

    auto& final_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    result.current = final_implementation.get_signal(target).initial_value;
    result.previous = final_implementation.signal_last_values.at(target);
    result.stored = final_implementation.driven_values.at(target);
    result.event = final_implementation.signal_events.at(target);
    result.transaction = final_implementation.signal_transactions.at(target);
    result.value_revision
        = final_implementation.signal_value_revisions.at(target);
    result.provenance_cleared_after_batch
        = !final_implementation.unresolved_update_owner_provenance.at(target)
               .touched
        && final_implementation.unresolved_update_owner_provenance_signals
               .empty();
    result.has_driver_record
        = final_implementation.driver_values.at(target).find(0U) != nullptr;
    require(inspected_after_commit && result.current == final_value
            && result.previous == initial_value
            && result.stored == final_value
            && result.event && result.transaction
            && result.event != before_event
            && result.transaction != before_transaction
            && result.value_revision != 0U
            && result.owner_alias_demoted_after_batch
            && result.provenance_cleared_after_batch
            && !result.has_driver_record,
        "mixed owner provenance publishes one exact unresolved value and demotes");
    return result;
}

bool same_unresolved_wide_mixed_owner_result(
    const UnresolvedWideMixedOwnerResult& left,
    const UnresolvedWideMixedOwnerResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.event == right.event
        && left.transaction == right.transaction
        && left.value_revision == right.value_revision
        && left.has_driver_record == right.has_driver_record;
}

void exercise_unresolved_wide_mixed_owner_provenance()
{
    const auto admitted = run_unresolved_wide_mixed_owner_batch(nullptr);
    const auto checked = run_unresolved_wide_mixed_owner_batch("0");
    require(admitted.owner_alias_bound_before_batch
            && admitted.provenance_latched_ambiguous
            && admitted.owner_alias_demoted_after_batch
            && admitted.provenance_cleared_after_batch
            && !checked.owner_alias_bound_before_batch
            && checked.provenance_latched_ambiguous
            && checked.owner_alias_demoted_after_batch
            && checked.provenance_cleared_after_batch
            && same_unresolved_wide_mixed_owner_result(admitted, checked),
        "mixed unresolved owner/no-owner updates preserve checked ordering and stamps");
}

void exercise_unresolved_wide_partial_owner_declines_alias()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t target_width = 129U;
    constexpr std::uint32_t slice_width = 64U;
    const PackedLogic4 input_value { slice_width, Logic4::zero };
    const PackedLogic4 target_value { target_width, Logic4::zero };
    for (const auto offset : { 0U, 65U }) {
        Interpreter interpreter;
        const auto input = interpreter.add_signal({
            "unresolved_partial.input", input_value,
            ResolutionKind::none, ValueKind::logic4 });
        const auto target = interpreter.add_signal({
            "unresolved_partial.target", target_value,
            ResolutionKind::none, ValueKind::logic4 });
        require(interpreter.add_process(vhdl_projected_slice_writer(
                    0U, input, target, offset, slice_width,
                    ValueKind::logic4, 0U, 0U,
                    ProjectedDelayMode::inertial, std::nullopt, false)) == 0U,
            "partial owner retains its registered projected process");
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "partial owner reaches its initial quiet point");
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        auto* const state
            = implementation.region_authoritative_state_for_signal(target);
        require(state == nullptr
                || !state->values().packed_owner_slot_bound(target, 0U),
            "a partial or offset unresolved write cannot alias the stored owner");
    }
}

Process vhdl_projected_slice_writer(const ProcessId id,
    const SignalId input,
    const SignalId output,
    const std::uint32_t offset,
    const std::uint32_t width,
    const ValueKind kind,
    const SimulationTick delay = 0U,
    const SimulationTick rejection = 0U,
    const ProjectedDelayMode mode = ProjectedDelayMode::inertial,
    const std::optional<SignalId> dynamic_index_signal = std::nullopt,
    const bool extra_waveform_write = false)
{
    Process process;
    process.id = id;
    process.name = "wide_vhdl_projected_slice_writer_"
        + std::to_string(id);
    process.language_standard = "2008";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = dynamic_index_signal ? 3U : 1U;
    process.register_value_kinds = { kind };
    process.static_sensitivity = { { input, EdgeKind::any } };
    if (dynamic_index_signal) {
        process.register_value_kinds.push_back(ValueKind::logic4);
        process.register_value_kinds.push_back(kind);
        process.static_sensitivity.push_back(
            { *dynamic_index_signal, EdgeKind::any });
    }
    process.driver_regions = { { output, offset, width, false } };
    process.operations = {
        ReadSignal { 0U, input },
    };
    if (dynamic_index_signal) {
        process.operations.push_back(
            ReadSignal { 1U, *dynamic_index_signal });
    }
    process.operations.push_back(
        WriteProjectedSlice { output, 0U, offset, delay, rejection, mode });
    if (dynamic_index_signal) {
        const DynamicIndex source_selection {
            1U, static_cast<std::int64_t>(width - 1U), 0, 0U, true };
        const DynamicIndex output_selection {
            1U, static_cast<std::int64_t>(width - 1U), 0, offset, true };
        process.operations.push_back(
            DynamicExtract { 2U, 0U, source_selection });
        process.operations.push_back(WriteProjectedDynamicSlice {
            output, 2U, output_selection, 0U, 0U,
            ProjectedDelayMode::inertial });
    }
    if (extra_waveform_write) {
        process.operations.push_back(WriteProjectedWaveformSlice {
            output, { ProjectedWaveformElement { 0U, 0U } }, offset, 0U,
            ProjectedDelayMode::inertial });
    }
    process.operations.push_back(WaitSensitivity { });
    process.operations.push_back(Jump { 0U });
    return process;
}

void exercise_vhdl_projected_wide_queue_publication(
    const char* const wide_policy)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", wide_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t width = 129U;
    constexpr ValueKind kind = ValueKind::logic9;
    const PackedLogic4 initial_input
        = PackedLogic4::from_logic9_msb_string(std::string(width, 'U'));
    const PackedLogic4 declared_output
        = PackedLogic4::from_logic9_msb_string(std::string(width, 'Z'));
    // register_driver resolves the new Logic9 owner's initial U value into
    // current, LAST and stored before the first scheduled projected write.
    const PackedLogic4 initial_output
        = PackedLogic4::from_logic9_msb_string(std::string(width, 'U'));
    const auto first_value = value_for(width, kind, 4U);
    const auto second_value = value_for(width, kind, 5U);

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "wide_vhdl.input", initial_input, ResolutionKind::none, kind });
    const auto target = interpreter.add_signal({
        "wide_vhdl.target", declared_output, ResolutionKind::std_logic, kind });
    const auto sink = interpreter.add_signal({
        "wide_vhdl.sink", declared_output, ResolutionKind::std_logic, kind });
    require(interpreter.add_process(vhdl_projected_writer(
                0U, input, target)) == 0U,
        "VHDL projected target writer retains its generic process identity");
    require(interpreter.add_process(vhdl_projected_writer(
                1U, target, sink)) == 1U,
        "VHDL projected sink reader retains its generic process identity");

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "VHDL wide queue fixture reaches its initial quiet point");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U),
        "VHDL wide target starts with bound current/LAST/stored/raw roles");
    auto* target_state = OwnedDriverDemotionTestAccess::implementation(
        interpreter).region_authoritative_state_for_signal(target);
    require(target_state != nullptr,
        "VHDL wide target has its original authoritative component");
    auto& initial_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component = initial_implementation
        .region_authoritative_component_by_signal.at(target);
    require(initial_implementation.region_authoritative_component_by_signal
                .at(sink) == component
            && initial_implementation
                    .region_authoritative_state_for_signal(sink)
                == target_state,
        "the downstream VHDL sink shares the target's authoritative component");
    const auto& certificate = initial_implementation.region_graph
        ->certificate_inventory().components.at(component);
    require(certificate.status
                == RegionComponentCertificateStatus::no_internal_state
            && certificate.structural_internal_signal_candidates.empty()
            && std::ranges::find(certificate.boundary_signals, target)
                != certificate.boundary_signals.end()
            && !initial_implementation.region_activation_programs.at(component)
            && initial_implementation.region_readiness_mask_by_component
                    .at(component).word_count == 0U,
        "wide resolved VHDL owner storage retains its public boundary and "
        "creates no native compute program or readiness descriptor");
    auto target_component_generation = target_state->generation();
    const auto before_first_target_value_revision
        = initial_implementation.signal_value_revisions.at(target);
    const auto before_first_sink_value_revision
        = initial_implementation.signal_value_revisions.at(sink);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, sink, 1U),
        "the downstream VHDL sink also starts with all four bound roles");

    const auto before_first_generation = target_state->values().revision();
    interpreter.deposit_signal(input, first_value);
    require(interpreter.run().status == RunStatus::completed,
        "zero-delay VHDL projected updates drain through the generic queue");
    auto& after_first_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const after_first_state
        = after_first_implementation.region_authoritative_state_for_signal(
            target);
    require(after_first_state != nullptr
            && after_first_state
                == after_first_implementation
                    .region_authoritative_state_for_signal(sink)
            && after_first_state->generation()
                == target_component_generation
            && after_first_state->values().revision()
                == before_first_generation + 4U
            && after_first_implementation.signal_value_revisions.at(target)
                == before_first_target_value_revision + 1U
            && after_first_implementation.signal_value_revisions.at(sink)
                == before_first_sink_value_revision + 1U,
        "both generic raw-owner/value pairs publish once in their shared "
        "bound authoritative component");
    target_state = after_first_state;
    target_component_generation = target_state->generation();
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, sink, 1U),
        "the first generic queued writes keep both wide outputs bound");

    const auto retained_after_first =
        OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 0U);

    const auto before_second_generation = target_state->values().revision();
    const auto before_second_target_value_revision
        = after_first_implementation.signal_value_revisions.at(target);
    const auto before_second_sink_value_revision
        = after_first_implementation.signal_value_revisions.at(sink);
    interpreter.deposit_signal(input, second_value);
    require(interpreter.run().status == RunStatus::completed,
        "the second VHDL projected event drains without private-wave dispatch");
    auto& after_second_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const after_second_state
        = after_second_implementation.region_authoritative_state_for_signal(
            target);
    require(after_second_state != nullptr
            && after_second_state
                == after_second_implementation
                    .region_authoritative_state_for_signal(sink)
            && after_second_state->generation()
                == target_component_generation
            && after_second_state->values().revision()
                == before_second_generation + 4U
            && after_second_implementation.signal_value_revisions.at(target)
                == before_second_target_value_revision + 1U
            && after_second_implementation.signal_value_revisions.at(sink)
                == before_second_sink_value_revision + 1U,
        "the second VHDL event publishes both raw/value pairs once in the "
        "same bound component");
    target_state = after_second_state;
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, sink, 1U),
        "the sorted generic value publications use both wide A4 commit routes");

    const auto roles = OwnedDriverDemotionTestAccess::packed_a4_values(
        interpreter, target, 0U);
    require(roles[0U] == second_value && roles[1U] == first_value
            && roles[2U] == second_value && roles[3U] == second_value,
        "VHDL generic publication keeps current/LAST/stored/raw phases exact");
    require(retained_after_first[0U] == first_value
            && retained_after_first[1U] == initial_output
            && retained_after_first[2U] == first_value
            && retained_after_first[3U] == first_value,
        "leases from the earlier generic publication retain all four old roles");
    const auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(implementation.get_signal(sink).initial_value == second_value,
        "the VHDL consumer observes the same final projected value");
}

struct VhdlProjectedDisjointRoundResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner0;
    PackedLogic4 owner1;
    PackedLogic4 expected_current;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp event_stamp;
    SimulationTick final_time { };
    std::uint64_t revision_delta { };
    bool bound_before { };
    bool bound_after_first { };
    bool versioned_storage_ready { };
    bool admitted_owner0 { };
    bool admitted_owner1 { };
    bool sidecar_roles_match { };
    bool late_observation_demoted { };
    bool late_observation_coherent { };
    bool force_started_bound { };
    bool force_demoted { };
    bool force_release_preserved_owners { };
    bool graph_kept_target_as_boundary { };
    bool no_compute_program { };
    bool writer_excluded_from_region_component { };
};

[[nodiscard]] VhdlProjectedDisjointRoundResult
run_vhdl_projected_disjoint_slice_round(
    const std::uint32_t width,
    const ValueKind kind,
    const char* disjoint_owner_policy,
    const bool late_observation,
    const bool force_release,
    const SimulationTick first_delay = 0U,
    const SimulationTick first_rejection = 0U,
    const ProjectedDelayMode first_mode = ProjectedDelayMode::inertial,
    const bool dynamic_projected_write = false,
    const bool waveform_projected_write = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_owner_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };

    const auto lower_width = width / 2U;
    const auto upper_width = width - lower_width;
    const auto initial_input0 = value_for(lower_width, kind, 2U);
    const auto initial_input1 = value_for(upper_width, kind, 3U);
    const auto first_input0 = value_for(lower_width, kind, 7U);
    const auto first_input1 = value_for(upper_width, kind, 8U);
    const auto second_input0 = value_for(lower_width, kind, 11U);
    const auto second_input1 = value_for(upper_width, kind, 12U);
    const auto initial_target = all_z_value(width, kind);
    const auto target_resolution = kind == ValueKind::logic9
        ? ResolutionKind::std_logic : ResolutionKind::sv_wire;

    Interpreter interpreter;
    const auto input0 = interpreter.add_signal({
        "wide_vhdl_disjoint.input0", initial_input0,
        ResolutionKind::none, kind });
    const auto input1 = interpreter.add_signal({
        "wide_vhdl_disjoint.input1", initial_input1,
        ResolutionKind::none, kind });
    const auto target = interpreter.add_signal({
        "wide_vhdl_disjoint.target", initial_target,
        target_resolution, kind });
    std::optional<SignalId> dynamic_index;
    if (dynamic_projected_write) {
        PackedLogic4 dynamic_index_value { 32U, Logic4::zero };
        dynamic_index_value.set(0U, Logic4::one);
        dynamic_index = interpreter.add_signal({
            "wide_vhdl_disjoint.dynamic_index", dynamic_index_value,
            ResolutionKind::none, ValueKind::logic4 });
    }
    require(interpreter.add_process(vhdl_projected_slice_writer(
                0U, input0, target, 0U, lower_width, kind,
                first_delay, first_rejection, first_mode, dynamic_index,
                waveform_projected_write)) == 0U,
        "VHDL lower-slice owner retains its generic process identity");
    require(interpreter.add_process(vhdl_projected_slice_writer(
                1U, input1, target, lower_width, upper_width, kind)) == 1U,
        "VHDL upper-slice owner retains its generic process identity");

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "VHDL disjoint slice fixture reaches its initial quiet point");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    VhdlProjectedDisjointRoundResult result;
    result.bound_before
        = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 1U);
    const auto* const initial_state
        = implementation.region_authoritative_state_for_signal(target);
    result.versioned_storage_ready = initial_state != nullptr
        && initial_state->values().requires_prewrite_unbind();
    result.admitted_owner0
        = implementation.can_try_wide_disjoint_owner_commit(0U, target);
    result.admitted_owner1
        = implementation.can_try_wide_disjoint_owner_commit(1U, target);
    if (implementation.region_graph
        && !implementation.region_component_by_process.empty()) {
        const auto component = implementation.region_component_by_process[0U];
        result.writer_excluded_from_region_component
            = component == std::numeric_limits<std::size_t>::max();
        if (component < implementation.region_activation_programs.size()) {
            result.no_compute_program
                = !implementation.region_activation_programs[component];
            const auto& certificate = implementation.region_graph
                ->certificate_inventory().components.at(component);
            result.graph_kept_target_as_boundary
                = std::ranges::find(certificate.boundary_signals, target)
                    != certificate.boundary_signals.end();
        }
    }

    const auto* const initial_record0
        = implementation.driver_values.at(target).find(0U);
    const auto* const initial_record1
        = implementation.driver_values.at(target).find(1U);
    require(initial_record0 != nullptr && initial_record1 != nullptr,
        "VHDL disjoint target keeps both registered raw drivers");
    const auto passive_owner_payload = [](
        auto& current_implementation, const SignalId signal,
        const ProcessId owner) -> PackedLogic4 {
        const auto* const record
            = current_implementation.driver_values.at(signal).find(owner);
        if (record == nullptr) {
            throw std::runtime_error {
                "VHDL disjoint target lost a registered raw driver"
            };
        }
        auto* const state
            = current_implementation.region_authoritative_state_for_signal(
                signal);
        if (state != nullptr
            && state->values().packed_owner_slot_bound(signal, owner)) {
            return state->values().owner_value(signal, owner);
        }
        if (current_implementation.owned_driver_active(signal)) {
            return current_implementation.owned_driver_value(owner, signal);
        }
        return record->value;
    };
    const auto initial_owner0
        = passive_owner_payload(implementation, target, 0U);
    const auto initial_owner1
        = passive_owner_payload(implementation, target, 1U);
    const auto baseline_revision
        = implementation.signal_value_revisions.at(target);
    const auto initial_current = implementation.get_signal(target).initial_value;
    if (force_release) {
        result.force_started_bound = result.bound_before;
        interpreter.force_signal(target, value_for(width, kind, 15U));
        result.force_demoted
            = !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        interpreter.release_signal(target);
        auto& released_implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto* const released_owner0
            = released_implementation.driver_values.at(target).find(0U);
        const auto* const released_owner1
            = released_implementation.driver_values.at(target).find(1U);
        result.force_release_preserved_owners
            = released_owner0 != nullptr && released_owner1 != nullptr
            && released_owner0->value == initial_owner0
            && released_owner1->value == initial_owner1
            && released_implementation.get_signal(target).initial_value
                == initial_current;
    }

    interpreter.deposit_signal(input0, first_input0);
    interpreter.deposit_signal(input1, first_input1);
    require(interpreter.run().status == RunStatus::completed,
        "zero-delay VHDL projected slices use the original generic scheduler");
    auto& first_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto first_current
        = first_implementation.get_signal(target).initial_value;
    const auto first_last = first_implementation.signal_last_values.at(target);
    const auto first_stored = first_implementation.driven_values.at(target);
    const auto* const first_owner0
        = first_implementation.driver_values.at(target).find(0U);
    const auto* const first_owner1
        = first_implementation.driver_values.at(target).find(1U);
    require(first_owner0 != nullptr && first_owner1 != nullptr,
        "first projected commit preserves both original owner records");
    auto first_expected_current = all_z_value(width, kind);
    insert_range(first_expected_current,
        first_input0, 0U, kind);
    insert_range(first_expected_current,
        first_input1, lower_width, kind);
    auto expected_owner0 = initial_owner0;
    auto expected_owner1 = initial_owner1;
    insert_range(expected_owner0, first_input0, 0U, kind);
    insert_range(expected_owner1, first_input1, lower_width, kind);
    result.sidecar_roles_match = first_current == first_expected_current
        && first_last == initial_current
        && first_stored == first_expected_current
        && passive_owner_payload(first_implementation, target, 0U)
            == expected_owner0
        && passive_owner_payload(first_implementation, target, 1U)
            == expected_owner1;
    if (result.bound_before && !force_release) {
        const auto roles0 = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 0U);
        const auto roles1 = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 1U);
        result.sidecar_roles_match = result.sidecar_roles_match
            && roles0[0U] == first_current
            && roles0[1U] == first_last
            && roles0[2U] == first_stored
            && roles0[3U] == expected_owner0
            && roles1[0U] == first_current
            && roles1[1U] == first_last
            && roles1[2U] == first_stored
            && roles1[3U] == expected_owner1;
    }
    result.bound_after_first
        = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 1U);
    if (late_observation) {
        const auto observed = interpreter.signal_value(target);
        auto& observed_implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto* const observed_owner0
            = observed_implementation.driver_values.at(target).find(0U);
        const auto* const observed_owner1
            = observed_implementation.driver_values.at(target).find(1U);
        result.late_observation_demoted
            = observed == first_current
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        result.late_observation_coherent
            = observed_owner0 != nullptr && observed_owner1 != nullptr
            && observed_implementation.get_signal(target).initial_value
                == first_current
            && observed_implementation.signal_last_values.at(target)
                == first_last
            && observed_implementation.driven_values.at(target)
                == first_stored
            && passive_owner_payload(observed_implementation, target, 0U)
                == expected_owner0
            && passive_owner_payload(observed_implementation, target, 1U)
                == expected_owner1;
        interpreter.deposit_signal(input0, second_input0);
        interpreter.deposit_signal(input1, second_input1);
        require(interpreter.run().status == RunStatus::completed,
            "checked VHDL retry after late observation completes");
    }

    auto& final_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    result.current = final_implementation.get_signal(target).initial_value;
    result.previous = final_implementation.signal_last_values.at(target);
    result.stored = final_implementation.driven_values.at(target);
    const auto* const final_owner0
        = final_implementation.driver_values.at(target).find(0U);
    const auto* const final_owner1
        = final_implementation.driver_values.at(target).find(1U);
    require(final_owner0 != nullptr && final_owner1 != nullptr,
        "final VHDL publication retains both original raw drivers");
    result.owner0
        = passive_owner_payload(final_implementation, target, 0U);
    result.owner1
        = passive_owner_payload(final_implementation, target, 1U);
    result.expected_current = all_z_value(width, kind);
    insert_range(result.expected_current,
        late_observation ? second_input0 : first_input0, 0U, kind);
    insert_range(result.expected_current,
        late_observation ? second_input1 : first_input1,
        lower_width, kind);
    result.event = final_implementation.signal_events.at(target);
    result.transaction = final_implementation.signal_transactions.at(target);
    result.event_stamp
        = final_implementation.signal_event_scheduling_stamps.at(target);
    result.final_time = interpreter.scheduler().now();
    result.revision_delta
        = final_implementation.signal_value_revisions.at(target)
        - baseline_revision;
    return result;
}

[[nodiscard]] bool same_vhdl_projected_disjoint_result(
    const VhdlProjectedDisjointRoundResult& left,
    const VhdlProjectedDisjointRoundResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.owner0 == right.owner0
        && left.owner1 == right.owner1
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_stamp.origin.process_domain
            == right.event_stamp.origin.process_domain
        && left.event_stamp.origin.phase == right.event_stamp.origin.phase
        && left.event_stamp.systemverilog_round
            == right.event_stamp.systemverilog_round
        && left.final_time == right.final_time
        && left.revision_delta == right.revision_delta;
}

void exercise_vhdl_projected_disjoint_slices()
{
    for (const auto width : { 65U, 129U }) {
        for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
            const auto admitted = run_vhdl_projected_disjoint_slice_round(
                width, kind, nullptr, false, false);
            const auto checked = run_vhdl_projected_disjoint_slice_round(
                width, kind, "0", false, false);
            require(admitted.bound_before && admitted.bound_after_first
                    && admitted.versioned_storage_ready
                    && admitted.admitted_owner0 && admitted.admitted_owner1
                    && admitted.graph_kept_target_as_boundary
                    && admitted.no_compute_program
                    && admitted.sidecar_roles_match
                    && !checked.bound_before && !checked.bound_after_first
                    && same_vhdl_projected_disjoint_result(admitted, checked),
                "default VHDL disjoint-slice owners preserve packed roles and "
                "original projected event metadata");
            require(admitted.current == admitted.expected_current
                    && admitted.event && admitted.transaction
                    && admitted.event == admitted.transaction
                    && admitted.event_stamp.origin.process_domain
                        == ProcessSchedulingDomain::generic
                    && admitted.event_stamp.origin.phase
                        == SchedulerPhase::active,
                "VHDL projected slices keep the generic Active origin and "
                "the same next-update event/transaction stamp");

            const auto observed = run_vhdl_projected_disjoint_slice_round(
                width, kind, nullptr, true, false);
            const auto observed_checked
                = run_vhdl_projected_disjoint_slice_round(
                    width, kind, "0", true, false);
            require(observed.bound_before && observed.bound_after_first
                    && observed.late_observation_demoted
                    && observed.late_observation_coherent
                    && !observed_checked.bound_before
                    && same_vhdl_projected_disjoint_result(
                        observed, observed_checked),
                "late target observation demotes both projected owners and "
                "the ordinary retry remains exact");

            const auto forced = run_vhdl_projected_disjoint_slice_round(
                width, kind, nullptr, false, true);
            const auto forced_checked
                = run_vhdl_projected_disjoint_slice_round(
                    width, kind, "0", false, true);
            require(forced.force_started_bound && forced.force_demoted
                    && forced.force_release_preserved_owners
                    && forced.bound_after_first
                    && same_vhdl_projected_disjoint_result(
                        forced, forced_checked),
                "force and release revoke projected owner slots before "
                "ordinary writes, then the quiet point recertifies them");
        }
    }

    const auto delayed = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, "1", false, false, 1U);
    const auto delayed_checked = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, "0", false, false, 1U);
    require(!delayed.bound_before && !delayed.bound_after_first
            && delayed.final_time == 1U
            && delayed.current == delayed.expected_current
            && same_vhdl_projected_disjoint_result(
                delayed, delayed_checked),
        "a delayed projected slice retains ordinary transaction timing "
        "and never receives disjoint owner storage");

    const auto transport = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, nullptr, false, false, 0U, 0U,
        ProjectedDelayMode::transport);
    const auto transport_checked = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, "0", false, false, 0U, 0U,
        ProjectedDelayMode::transport);
    require(!transport.bound_before && !transport.bound_after_first
            && transport.current == transport.expected_current
            && same_vhdl_projected_disjoint_result(
                transport, transport_checked),
        "transport projected slices retain queued transaction semantics "
        "without disjoint owner storage");

    const auto rejected = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, nullptr, false, false, 1U, 1U);
    const auto rejected_checked = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, "0", false, false, 1U, 1U);
    require(!rejected.bound_before && !rejected.bound_after_first
            && rejected.final_time == 1U
            && rejected.current == rejected.expected_current
            && same_vhdl_projected_disjoint_result(
                rejected, rejected_checked),
        "nonzero rejection limits retain ordinary projected timing and "
        "never receive disjoint owner storage");

    const auto dynamic = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, nullptr, false, false, 0U, 0U,
        ProjectedDelayMode::inertial, true);
    const auto dynamic_checked = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, "0", false, false, 0U, 0U,
        ProjectedDelayMode::inertial, true);
    require(!dynamic.bound_before && !dynamic.bound_after_first
            && !dynamic.admitted_owner0 && !dynamic.admitted_owner1
            && dynamic.writer_excluded_from_region_component
            && dynamic.current == dynamic.expected_current
            && same_vhdl_projected_disjoint_result(
                dynamic, dynamic_checked),
        "a runtime-indexed projected slice keeps the target on its exact "
        "generic checked-publication route");

    const auto waveform = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, nullptr, false, false, 0U, 0U,
        ProjectedDelayMode::inertial, false, true);
    const auto waveform_checked = run_vhdl_projected_disjoint_slice_round(
        129U, ValueKind::logic9, "0", false, false, 0U, 0U,
        ProjectedDelayMode::inertial, false, true);
    require(!waveform.bound_before && !waveform.bound_after_first
            && !waveform.admitted_owner0 && !waveform.admitted_owner1
            && waveform.writer_excluded_from_region_component
            && waveform.current == waveform.expected_current
            && same_vhdl_projected_disjoint_result(
                waveform, waveform_checked),
        "a projected waveform write keeps the target on its exact generic "
        "checked-publication route");
}

enum class GenericUpdateSliceAdmissionShape : std::uint8_t {
    exact,
    narrow_source,
    mixed_active,
    overlapping,
    incomplete,
    opaque_owner,
    observed,
};

struct GenericUpdateSliceAdmissionResult {
    bool owner_slots_bound_before_execution { };
    bool authoritative_component_shared { };
    bool disjoint_partial_writer_class { };
    bool ranges_cover_target_exactly { };
    bool exact_writer_ranges_and_domains { };
    bool operation_write_ranges_exact { };
    bool source_widths_match_ranges { };
    bool ownership_unknown { };
    bool access_inventory_incomplete { };
    bool target_observed { };
    bool legacy_composite_active_before { };
    bool legacy_composite_active_after { };
    bool complete_roles_match { };
    bool raw_records_match { };
    bool event_matches_transaction { };
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp event_stamp;
    std::uint64_t revision_delta { };
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner0;
    PackedLogic4 owner1;
};

[[nodiscard]] GenericUpdateSliceAdmissionResult
run_generic_update_slice_admission_case(
    const std::uint32_t width,
    const char* disjoint_owner_policy,
    const GenericUpdateSliceAdmissionShape shape,
    const bool execute_update)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_owner_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_disabled { "FSIM_PROFILE_SV_WAVES", nullptr };

    const auto lower_width = width / 2U;
    const auto upper_width = width - lower_width;
    auto offset1 = lower_width;
    auto driver_width1 = upper_width;
    if (shape == GenericUpdateSliceAdmissionShape::overlapping) {
        --offset1;
        ++driver_width1;
    } else if (shape == GenericUpdateSliceAdmissionShape::incomplete) {
        ++offset1;
        --driver_width1;
    }
    const auto source_width0
        = shape == GenericUpdateSliceAdmissionShape::narrow_source
        ? lower_width - 1U : lower_width;
    const auto target_initial = all_z_value(width, ValueKind::logic4);

    Interpreter interpreter;
    const auto input0 = interpreter.add_signal({
        "generic_update_slice.input0",
        all_z_value(source_width0, ValueKind::logic4),
        ResolutionKind::none, ValueKind::logic4 });
    const auto input1 = interpreter.add_signal({
        "generic_update_slice.input1",
        all_z_value(driver_width1, ValueKind::logic4),
        ResolutionKind::none, ValueKind::logic4 });
    const auto target = interpreter.add_signal({
        "generic_update_slice.target", target_initial,
        ResolutionKind::sv_wire, ValueKind::logic4 });
    const auto middle = interpreter.add_signal({
        "generic_update_slice.middle", all_z_value(width, ValueKind::logic4),
        ResolutionKind::sv_wire, ValueKind::logic4 });
    const auto sink = interpreter.add_signal({
        "generic_update_slice.sink", all_z_value(width, ValueKind::logic4),
        ResolutionKind::sv_wire, ValueKind::logic4 });

    const auto second_domain
        = shape == GenericUpdateSliceAdmissionShape::mixed_active
        ? ProcessSchedulingDomain::systemverilog
        : ProcessSchedulingDomain::generic;
    require(interpreter.add_process(generic_update_slice_writer(
                0U, input0, target, 0U, lower_width,
                ProcessSchedulingDomain::generic)) == 0U
            && interpreter.add_process(generic_update_slice_writer(
                1U, input1, target, offset1, driver_width1,
                second_domain)) == 1U,
        "plain generic slice owners keep their original process IDs");
    require(interpreter.add_process(generic_update_copy_writer(
                2U, target, middle, width)) == 2U
            && interpreter.add_process(generic_update_copy_writer(
                3U, middle, sink, width)) == 3U,
        "generic owner boundary connects to ordinary private component state");
    if (shape == GenericUpdateSliceAdmissionShape::opaque_owner) {
        interpreter.set_process_executor(0U,
            std::make_unique<OpaqueAccessWaitExecutor>());
    }

    const PackedLogic4* observed_reference { };
    if (shape == GenericUpdateSliceAdmissionShape::observed) {
        observed_reference = &interpreter.signal_value(target);
    }
    interpreter.start();
    static_cast<void>(observed_reference);

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    GenericUpdateSliceAdmissionResult result;
    if (!implementation.region_graph
        || target >= implementation.region_graph->signals().size()
        || implementation.region_graph->processes().size() < 2U) {
        return result;
    }
    const auto& node = implementation.region_graph->signals()[target];
    result.target_observed = node.observations != RegionObservation::none;
    result.ownership_unknown = node.writers_unknown;
    result.access_inventory_incomplete
        = !implementation.process_signal_access_inventory_complete;
    result.disjoint_partial_writer_class
        = node.drivers == RegionDriverClass::disjoint_partial;
    const auto& graph_processes
        = implementation.region_graph->processes();
    const auto graph_process0 = std::ranges::find(
        graph_processes, ProcessId { 0U }, &RegionProcessNode::process);
    const auto graph_process1 = std::ranges::find(
        graph_processes, ProcessId { 1U }, &RegionProcessNode::process);
    if (graph_process0 == graph_processes.end()
        || graph_process1 == graph_processes.end()) {
        return result;
    }
    const auto& program0 = interpreter.process_program(0U);
    const auto& program1 = interpreter.process_program(1U);
    const auto read_operation0 = program0.operations.expanded(0U);
    const auto read_operation1 = program1.operations.expanded(0U);
    const auto operation0 = program0.operations.expanded(1U);
    const auto operation1 = program1.operations.expanded(1U);
    const auto* const read0 = operation_get_if<ReadSignal>(&read_operation0);
    const auto* const read1 = operation_get_if<ReadSignal>(&read_operation1);
    const auto* const write0
        = operation_get_if<WriteUpdateSlice>(&operation0);
    const auto* const write1
        = operation_get_if<WriteUpdateSlice>(&operation1);
    const auto graph_writer0 = std::ranges::find(node.writers, 0U,
        &RegionAccess::process);
    const auto graph_writer1 = std::ranges::find(node.writers, 1U,
        &RegionAccess::process);
    const bool graph_rows_exact = graph_writer0 != node.writers.end()
        && graph_writer1 != node.writers.end()
        && graph_writer0->offset == 0U
        && graph_writer0->width == lower_width
        && graph_writer1->offset == offset1
        && graph_writer1->width == driver_width1;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> writer_ranges;
    writer_ranges.reserve(node.writers.size());
    for (const auto& writer : node.writers) {
        writer_ranges.emplace_back(writer.offset, writer.width);
    }
    std::ranges::sort(writer_ranges);
    std::uint64_t covered_end { };
    result.ranges_cover_target_exactly = !writer_ranges.empty();
    for (const auto& [offset, range_width] : writer_ranges) {
        if (offset != covered_end) {
            result.ranges_cover_target_exactly = false;
            break;
        }
        covered_end += range_width;
    }
    result.ranges_cover_target_exactly
        = result.ranges_cover_target_exactly && covered_end == width;
    const bool process_rows_exact
        = program0.scheduling_domain == ProcessSchedulingDomain::generic
        && graph_process0->scheduling_domain
            == ProcessSchedulingDomain::generic
        && graph_process0->update_kind == RegionUpdateKind::generic
        && program1.scheduling_domain == second_domain
        && graph_process1->scheduling_domain == second_domain
        && graph_process1->update_kind
            == (second_domain == ProcessSchedulingDomain::generic
                ? RegionUpdateKind::generic
                : RegionUpdateKind::systemverilog_active)
        && graph_process0->pure && graph_process1->pure
        && !program0.initialize && !program1.initialize
        && program0.operations.size() == 4U
        && program1.operations.size() == 4U
        && program0.driver_regions.size() == 1U
        && program1.driver_regions.size() == 1U
        && program0.driver_regions[0U]
            == Process::DriverRegion { target, 0U, lower_width, false }
        && program1.driver_regions[0U]
            == Process::DriverRegion {
                target, offset1, driver_width1, false };
    const bool operations_exact
        = read0 != nullptr && read0->destination == 0U
        && read0->signal == input0
        && read1 != nullptr && read1->destination == 0U
        && read1->signal == input1
        && write0 != nullptr && write0->source == 0U
        && write0->signal == target
        && write0->offset == 0U
        && write0->domain == SignalUpdateDomain::generic
        && write1 != nullptr && write1->source == 0U
        && write1->signal == target
        && write1->offset == offset1
        && write1->domain
            == (second_domain == ProcessSchedulingDomain::generic
                ? SignalUpdateDomain::generic
                : SignalUpdateDomain::systemverilog_active);
    result.exact_writer_ranges_and_domains = graph_rows_exact
        && process_rows_exact && operations_exact;
    result.exact_writer_ranges_and_domains
        = result.exact_writer_ranges_and_domains
        && node.writers.size() == 2U;
    result.operation_write_ranges_exact
        = graph_process0->operation_write_ranges_exact
        && graph_process1->operation_write_ranges_exact;
    result.source_widths_match_ranges
        = implementation.region_graph->signals()[input0]
                .descriptor.width == lower_width
        && implementation.region_graph->signals()[input1]
                .descriptor.width == driver_width1
        && program0.register_value_kinds.size() == 1U
        && program0.register_value_kinds[0U] == ValueKind::logic4
        && program1.register_value_kinds.size() == 1U
        && program1.register_value_kinds[0U] == ValueKind::logic4;
    const auto* const state
        = implementation.region_authoritative_state_for_signal(target);
    if (state != nullptr && state->values().layout().contains(target)) {
        const auto owners = state->values().layout().owners(target);
        const bool has_owner0 = std::ranges::any_of(owners,
            [](const SignalDriverOwnerLayout& owner) {
                return owner.process == 0U;
            });
        const bool has_owner1 = std::ranges::any_of(owners,
            [](const SignalDriverOwnerLayout& owner) {
                return owner.process == 1U;
            });
        result.owner_slots_bound_before_execution
            = has_owner0 && has_owner1
            && state->values().packed_signal_slots_bound(target)
            && state->values().packed_owner_slot_bound(target, 0U)
            && state->values().packed_owner_slot_bound(target, 1U);
    }
    result.authoritative_component_shared
        = implementation.region_component_by_process.size() > 1U
        && implementation.region_component_by_process[0U]
            != std::numeric_limits<std::size_t>::max()
        && implementation.region_component_by_process[0U]
            == implementation.region_component_by_process[1U];
    result.legacy_composite_active_before
        = implementation.owned_driver_active(target);
    result.legacy_composite_active_after
        = result.legacy_composite_active_before;

    if (!execute_update) {
        return result;
    }

    require(interpreter.run().status == RunStatus::completed,
        "generic slice owners reach an ordinary quiet point before stimulus");
    const auto before_current
        = implementation.get_signal(target).initial_value;
    const auto before_revision
        = implementation.signal_value_revisions.at(target);
    const auto next0 = value_for(lower_width, ValueKind::logic4, 5U);
    const auto next1 = value_for(driver_width1, ValueKind::logic4, 9U);
    auto expected = before_current;
    insert_range(expected, next0, 0U, ValueKind::logic4);
    insert_range(expected, next1, offset1, ValueKind::logic4);
    auto expected_owner0 = all_z_value(width, ValueKind::logic4);
    auto expected_owner1 = all_z_value(width, ValueKind::logic4);
    insert_range(expected_owner0, next0, 0U, ValueKind::logic4);
    insert_range(expected_owner1, next1, offset1, ValueKind::logic4);
    interpreter.deposit_signal(input0, next0);
    interpreter.deposit_signal(input1, next1);
    require(interpreter.run().status == RunStatus::completed,
        "plain generic WriteUpdateSlice owners complete their update");

    auto* const final_state
        = implementation.region_authoritative_state_for_signal(target);
    const auto* const raw_owner0
        = implementation.driver_values.at(target).find(0U);
    const auto* const raw_owner1
        = implementation.driver_values.at(target).find(1U);
    require(raw_owner0 != nullptr && raw_owner1 != nullptr,
        "generic update target retains its two original raw owners");
    const auto owner_payload = [&](const ProcessId owner) {
        const bool has_owner_layout = final_state != nullptr
            && final_state->values().layout().contains(target)
            && std::ranges::any_of(
                final_state->values().layout().owners(target),
                [&](const SignalDriverOwnerLayout& item) {
                    return item.process == owner;
                });
        if (has_owner_layout
            && final_state->values().packed_owner_slot_bound(target, owner)) {
            return final_state->values().owner_value(target, owner);
        }
        return implementation.underlying_driver_value(owner, target);
    };
    result.current = implementation.get_signal(target).initial_value;
    result.previous = implementation.signal_last_values.at(target);
    result.stored = implementation.driven_values.at(target);
    result.owner0 = owner_payload(0U);
    result.owner1 = owner_payload(1U);
    result.legacy_composite_active_after
        = implementation.owned_driver_active(target);
    result.raw_records_match = raw_owner0->value == expected_owner0
        && raw_owner1->value == expected_owner1;
    result.revision_delta
        = implementation.signal_value_revisions.at(target) - before_revision;
    result.event = implementation.signal_events.at(target);
    result.transaction = implementation.signal_transactions.at(target);
    result.event_stamp
        = implementation.signal_event_scheduling_stamps.at(target);
    result.event_matches_transaction = result.event.has_value()
        && result.event == result.transaction;
    result.complete_roles_match = result.current == expected
        && result.previous == before_current
        && result.stored == expected
        && result.owner0 == expected_owner0
        && result.owner1 == expected_owner1
        && result.revision_delta != 0U
        && result.event_matches_transaction;
    return result;
}

[[nodiscard]] bool same_generic_update_slice_result(
    const GenericUpdateSliceAdmissionResult& left,
    const GenericUpdateSliceAdmissionResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.owner0 == right.owner0
        && left.owner1 == right.owner1
        && left.event_matches_transaction
            == right.event_matches_transaction
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_stamp.origin.process_domain
            == right.event_stamp.origin.process_domain
        && left.event_stamp.origin.phase == right.event_stamp.origin.phase
        && left.event_stamp.systemverilog_round
            == right.event_stamp.systemverilog_round
        && left.revision_delta == right.revision_delta;
}

void exercise_generic_update_slice_storage_admission()
{
    for (const auto width : { 64U, 65U, 129U }) {
        const auto admitted = run_generic_update_slice_admission_case(
            width, "1", GenericUpdateSliceAdmissionShape::exact, true);
        const auto checked = run_generic_update_slice_admission_case(
            width, "0", GenericUpdateSliceAdmissionShape::exact, true);
        require(admitted.owner_slots_bound_before_execution
                && admitted.authoritative_component_shared
                && admitted.disjoint_partial_writer_class
                && admitted.ranges_cover_target_exactly
                && admitted.exact_writer_ranges_and_domains
                && admitted.operation_write_ranges_exact
                && admitted.source_widths_match_ranges
                && admitted.complete_roles_match
                && admitted.raw_records_match
                && !admitted.legacy_composite_active_before
                && !admitted.legacy_composite_active_after
                && !checked.owner_slots_bound_before_execution
                && checked.exact_writer_ranges_and_domains
                && same_generic_update_slice_result(admitted, checked),
            "exact-width generic Logic4 UpdateSlice owners bind both A4 "
            "slots before adapter execution and match checked publication");
    }

    const auto narrow_source = run_generic_update_slice_admission_case(
        65U, "1", GenericUpdateSliceAdmissionShape::narrow_source, false);
    require(narrow_source.exact_writer_ranges_and_domains
            && narrow_source.disjoint_partial_writer_class
            && narrow_source.ranges_cover_target_exactly
            && !narrow_source.operation_write_ranges_exact
            && !narrow_source.source_widths_match_ranges
            && !narrow_source.owner_slots_bound_before_execution,
        "a declared owner range wider than its UpdateSlice source declines storage");

    const auto mixed_active = run_generic_update_slice_admission_case(
        65U, "1", GenericUpdateSliceAdmissionShape::mixed_active, false);
    require(mixed_active.exact_writer_ranges_and_domains
            && mixed_active.disjoint_partial_writer_class
            && !mixed_active.owner_slots_bound_before_execution,
        "a mixed generic/SystemVerilog Active owner pair declines generic storage");

    const auto overlapping = run_generic_update_slice_admission_case(
        65U, "1", GenericUpdateSliceAdmissionShape::overlapping, false);
    require(!overlapping.owner_slots_bound_before_execution,
        "overlapping generic owner ranges retain checked DriverTable storage");
    require(!overlapping.disjoint_partial_writer_class
            || !overlapping.ranges_cover_target_exactly,
        "overlap control is recorded as a noncovering or nonsimple writer class");

    const auto incomplete = run_generic_update_slice_admission_case(
        65U, "1", GenericUpdateSliceAdmissionShape::incomplete, false);
    require(!incomplete.owner_slots_bound_before_execution,
        "a gap in generic owner ranges retains checked DriverTable storage");
    require(incomplete.disjoint_partial_writer_class
            && !incomplete.ranges_cover_target_exactly,
        "gap control remains disjoint but fails exact full-width coverage");

    const auto opaque_owner = run_generic_update_slice_admission_case(
        65U, "1", GenericUpdateSliceAdmissionShape::opaque_owner, false);
    require(opaque_owner.ownership_unknown
            || opaque_owner.access_inventory_incomplete,
        "an opaque owner is recorded by the incomplete ownership inventory");
    require(!opaque_owner.owner_slots_bound_before_execution,
        "an opaque owner invalidates storage admission");

    const auto observed = run_generic_update_slice_admission_case(
        65U, "1", GenericUpdateSliceAdmissionShape::observed, false);
    require(observed.target_observed
            && !observed.owner_slots_bound_before_execution,
        "a pre-start public target observation prevents owner-slot admission");
}

struct GenericThreeOwnerSliceFrame {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    std::array<PackedLogic4, 3U> owners;
    std::array<PackedLogic4, 3U> raw_owners;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp event_stamp;
    std::uint64_t value_revision { };
    std::uint64_t a4_revision { };
    bool all_owner_slots_bound { };
    bool legacy_composite_active { };
};

struct GenericThreeOwnerSliceResult {
    GenericThreeOwnerSliceFrame before;
    GenericThreeOwnerSliceFrame after_subset;
    GenericThreeOwnerSliceFrame after_same_value_subset;
    GenericThreeOwnerSliceFrame after_singleton;
    bool exact_graph_proof { };
    bool complete_owner_layout { };
    bool shared_component { };
    bool bound_before_callbacks { };
    std::uint64_t subset_a4_revision_delta { };
    std::uint64_t subset_target_revision_delta { };
    std::size_t subset_reader_callback_delta { };
    std::uint64_t same_value_a4_revision_delta { };
    std::uint64_t same_value_value_revision_delta { };
    std::size_t same_value_reader_callback_delta { };
    std::uint64_t singleton_target_revision_delta { };
    std::size_t singleton_reader_callback_delta { };
};

[[nodiscard]] GenericThreeOwnerSliceResult
run_generic_three_owner_slice_selection_case(
    const char* disjoint_owner_policy)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_owner_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_disabled { "FSIM_PROFILE_SV_WAVES", nullptr };

    constexpr std::uint32_t width = 64U;
    constexpr std::array<std::uint32_t, 3U> offsets { 0U, 16U, 32U };
    constexpr std::array<std::uint32_t, 3U> owner_widths {
        16U, 16U, 32U };
    constexpr std::array<std::uint32_t, 3U> seeds { 7U, 13U, 19U };
    constexpr std::array<std::uint32_t, 3U> changed_seeds { 23U, 29U, 31U };
    const bool capture_authoritative_revision
        = disjoint_owner_policy != nullptr
        && std::string_view { disjoint_owner_policy } == "1";

    Interpreter interpreter;
    std::array<SignalId, 3U> triggers { };
    std::array<SignalId, 3U> inputs { };
    std::array<PackedLogic4, 3U> input_values;
    for (std::size_t owner = 0U; owner < triggers.size(); ++owner) {
        triggers[owner] = interpreter.add_signal({
            "generic_three_owner.trigger." + std::to_string(owner),
            PackedLogic4 { 1U, Logic4::zero },
            ResolutionKind::none, ValueKind::logic4 });
        input_values[owner]
            = value_for(owner_widths[owner], ValueKind::logic4, seeds[owner]);
        inputs[owner] = interpreter.add_signal({
            "generic_three_owner.input." + std::to_string(owner),
            input_values[owner], ResolutionKind::none, ValueKind::logic4 });
    }
    const auto target = interpreter.add_signal({
        "generic_three_owner.target", all_z_value(width, ValueKind::logic4),
        ResolutionKind::sv_wire, ValueKind::logic4 });
    const auto middle = interpreter.add_signal({
        "generic_three_owner.middle", all_z_value(width, ValueKind::logic4),
        ResolutionKind::sv_wire, ValueKind::logic4 });
    const auto sink = interpreter.add_signal({
        "generic_three_owner.sink", all_z_value(width, ValueKind::logic4),
        ResolutionKind::sv_wire, ValueKind::logic4 });

    for (ProcessId owner = 0U; owner < triggers.size(); ++owner) {
        require(interpreter.add_process(
                    generic_triggered_update_slice_writer(owner,
                        triggers[owner], inputs[owner], target,
                        offsets[owner], owner_widths[owner])) == owner,
            "three original generic owners retain their process IDs");
    }
    require(interpreter.add_process(generic_update_copy_writer(
                3U, target, middle, width)) == 3U
            && interpreter.add_process(generic_update_copy_writer(
                4U, middle, sink, width)) == 4U,
        "three disjoint owners share their ordinary generic successor component");
    GenericGroupPublicationWitness witness;
    const auto& downstream_program = interpreter.process_program(3U);
    interpreter.set_process_executor(3U,
        std::make_unique<GenericGroupPublicationWitnessExecutor>(
            interpreter, target, target, middle, witness,
            ProcessExecutorProgramBinding {
                downstream_program, downstream_program, 3U }));
    if (disjoint_owner_policy != nullptr
        && std::string_view { disjoint_owner_policy } == "0") {
        interpreter.set_driver_change_hook(
            [](ProcessId, SignalId, SimulationTick) { });
    }
    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "three-owner generic fixture reaches its initial quiet point");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    GenericThreeOwnerSliceResult result;
    if (!implementation.region_graph
        || target >= implementation.region_graph->signals().size()) {
        return result;
    }
    const auto& graph_signal
        = implementation.region_graph->signals()[target];
    result.exact_graph_proof
        = graph_signal.drivers == RegionDriverClass::disjoint_partial
        && graph_signal.writers.size() == 3U
        && std::ranges::all_of(graph_signal.writers,
            [&](const RegionAccess& writer) {
                return writer.process < 3U
                    && writer.offset == offsets[writer.process]
                    && writer.width == owner_widths[writer.process];
            })
        && std::ranges::all_of(std::views::iota(0U, 3U),
            [&](const ProcessId owner) {
                return implementation.region_graph->processes()[owner].pure
                    && implementation.region_graph->processes()[owner]
                        .operation_write_ranges_exact
                    && implementation.region_graph->processes()[owner]
                        .update_kind == RegionUpdateKind::generic;
            });
    const auto* const state
        = implementation.region_authoritative_state_for_signal(target);
    result.complete_owner_layout = state != nullptr
        && state->values().layout().contains(target)
        && state->values().layout().signal(target).storage_class
            == SignalDriverStorageClass::disjoint_owner
        && state->values().layout().owners(target).size() == 3U;
    result.bound_before_callbacks = result.complete_owner_layout
        && state->values().packed_signal_slots_bound(target)
        && std::ranges::all_of(std::views::iota(0U, 3U),
            [&](const ProcessId owner) {
                return state->values().packed_owner_slot_bound(
                    target, owner);
            });
    result.shared_component
        = implementation.region_component_by_process.size() >= 5U
        && implementation.region_component_by_process[0U]
            != std::numeric_limits<std::size_t>::max()
        && std::ranges::all_of(std::views::iota(1U, 5U),
            [&](const ProcessId process) {
                return implementation.region_component_by_process[process]
                    == implementation.region_component_by_process[0U];
            });

    const auto capture = [&]() {
        GenericThreeOwnerSliceFrame frame;
        const auto* const current_state
            = implementation.region_authoritative_state_for_signal(target);
        frame.current = implementation.get_signal(target).initial_value;
        frame.previous = implementation.signal_last_values.at(target);
        frame.stored = implementation.driven_values.at(target);
        for (ProcessId owner = 0U; owner < 3U; ++owner) {
            const auto* const record
                = implementation.driver_values.at(target).find(owner);
            require(record != nullptr,
                "three-owner update retains every original DriverRecord");
            frame.raw_owners[owner] = record->value;
            frame.owners[owner]
                = current_state != nullptr
                    && current_state->values().layout().contains(target)
                    && current_state->values().packed_owner_slot_bound(
                        target, owner)
                ? current_state->values().owner_value(target, owner)
                : implementation.underlying_driver_value(owner, target);
        }
        frame.event = implementation.signal_events.at(target);
        frame.transaction = implementation.signal_transactions.at(target);
        frame.event_stamp
            = implementation.signal_event_scheduling_stamps.at(target);
        frame.value_revision
            = implementation.signal_value_revisions.at(target);
        frame.a4_revision = current_state != nullptr
            ? current_state->values().revision() : 0U;
        frame.all_owner_slots_bound = current_state != nullptr
            && current_state->values().packed_signal_slots_bound(target)
            && std::ranges::all_of(std::views::iota(0U, 3U),
                [&](const ProcessId owner) {
                    return current_state->values().packed_owner_slot_bound(
                        target, owner);
                });
        frame.legacy_composite_active
            = implementation.owned_driver_active(target);
        return frame;
    };

    result.before = capture();
    const auto before_a4_revision = result.before.a4_revision;
    const auto reader_callbacks_before_subset = witness.reader_callbacks;
    witness.capture_next_revision = capture_authoritative_revision;
    witness.captured_revision = false;
    interpreter.deposit_signal(
        triggers[0U], PackedLogic4 { 1U, Logic4::one });
    interpreter.deposit_signal(
        triggers[2U], PackedLogic4 { 1U, Logic4::one });
    require(interpreter.run().status == RunStatus::completed,
        "two selected generic owners publish in one Update round");
    result.after_subset = capture();
    result.subset_a4_revision_delta
        = result.after_subset.a4_revision - before_a4_revision;
    result.subset_reader_callback_delta
        = witness.reader_callbacks - reader_callbacks_before_subset;
    if (witness.captured_revision) {
        result.subset_target_revision_delta
            = witness.reader_entry_revision - before_a4_revision;
    }
    result.after_same_value_subset = result.after_subset;
    const auto same_value_before_a4
        = result.after_subset.a4_revision;
    const auto same_value_before_revision
        = result.after_subset.value_revision;
    const auto reader_callbacks_before_same_value = witness.reader_callbacks;
    interpreter.schedule_signal_at(
        triggers[0U], PackedLogic4 { 1U, Logic4::zero }, 2U, 0U);
    interpreter.schedule_signal_at(
        triggers[2U], PackedLogic4 { 1U, Logic4::zero }, 2U, 0U);
    require(interpreter.run().status == RunStatus::completed,
        "two same-value generic owners retain their transaction round");
    result.after_same_value_subset = capture();
    result.same_value_a4_revision_delta
        = result.after_same_value_subset.a4_revision - same_value_before_a4;
    result.same_value_value_revision_delta
        = result.after_same_value_subset.value_revision
            - same_value_before_revision;
    result.same_value_reader_callback_delta
        = witness.reader_callbacks - reader_callbacks_before_same_value;

    const auto before_singleton_revision
        = result.after_same_value_subset.a4_revision;
    const auto reader_callbacks_before_singleton = witness.reader_callbacks;
    witness.capture_next_revision = capture_authoritative_revision;
    witness.captured_revision = false;
    input_values[1U] = value_for(owner_widths[1U], ValueKind::logic4,
        changed_seeds[1U]);
    interpreter.schedule_signal_at(
        inputs[1U], input_values[1U], 3U, 0U);
    interpreter.schedule_signal_at(
        triggers[1U], PackedLogic4 { 1U, Logic4::one }, 3U, 0U);
    require(interpreter.run().status == RunStatus::completed,
        "one generic owner falls through the multi-owner group path");
    result.after_singleton = capture();
    result.singleton_reader_callback_delta
        = witness.reader_callbacks - reader_callbacks_before_singleton;
    if (witness.captured_revision) {
        result.singleton_target_revision_delta
            = witness.reader_entry_revision - before_singleton_revision;
    }
    return result;
}

[[nodiscard]] bool same_generic_three_owner_slice_frame(
    const GenericThreeOwnerSliceFrame& left,
    const GenericThreeOwnerSliceFrame& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.owners == right.owners
        && left.raw_owners == right.raw_owners
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_stamp.origin.process_domain
            == right.event_stamp.origin.process_domain
        && left.event_stamp.origin.phase == right.event_stamp.origin.phase
        && left.event_stamp.systemverilog_round
            == right.event_stamp.systemverilog_round
        && left.value_revision == right.value_revision;
}

void exercise_generic_three_owner_slice_selection()
{
    const auto admitted
        = run_generic_three_owner_slice_selection_case("1");
    const auto checked
        = run_generic_three_owner_slice_selection_case("0");
    const auto owner0 = [&]() {
        auto value = all_z_value(64U, ValueKind::logic4);
        insert_range(value, value_for(16U, ValueKind::logic4, 7U), 0U,
            ValueKind::logic4);
        return value;
    }();
    const auto owner1 = all_z_value(64U, ValueKind::logic4);
    const auto owner2 = [&]() {
        auto value = all_z_value(64U, ValueKind::logic4);
        insert_range(value, value_for(32U, ValueKind::logic4, 19U), 32U,
            ValueKind::logic4);
        return value;
    }();
    auto subset_current = all_z_value(64U, ValueKind::logic4);
    insert_range(subset_current, value_for(16U, ValueKind::logic4, 7U), 0U,
        ValueKind::logic4);
    insert_range(subset_current, value_for(32U, ValueKind::logic4, 19U), 32U,
        ValueKind::logic4);
    auto singleton_owner1 = all_z_value(64U, ValueKind::logic4);
    insert_range(singleton_owner1,
        value_for(16U, ValueKind::logic4, 29U), 16U, ValueKind::logic4);
    auto singleton_current = subset_current;
    insert_range(singleton_current,
        value_for(16U, ValueKind::logic4, 29U), 16U, ValueKind::logic4);
    require(admitted.exact_graph_proof
            && admitted.complete_owner_layout
            && admitted.shared_component
            && admitted.bound_before_callbacks
            && admitted.after_subset.all_owner_slots_bound
            && admitted.after_subset.current == subset_current
            && admitted.after_subset.owners
                == std::array { owner0, owner1, owner2 }
            && admitted.after_subset.raw_owners
                == std::array { owner0, owner1, owner2 }
            && !admitted.after_subset.legacy_composite_active
            && admitted.subset_a4_revision_delta > 0U
            && admitted.subset_reader_callback_delta == 1U
            && admitted.subset_target_revision_delta == 1U,
        "a selected two-of-three 64-bit generic UpdateSlice owner set uses bound group publication and preserves the untouched owner");
    require(admitted.after_same_value_subset.current == subset_current
            && admitted.after_same_value_subset.owners
                == admitted.after_subset.owners
            && admitted.after_same_value_subset.raw_owners
                == admitted.after_subset.raw_owners
            && admitted.after_same_value_subset.stored
                == admitted.after_subset.stored
            && admitted.after_same_value_subset.transaction
            && admitted.after_subset.transaction
            && admitted.after_same_value_subset.transaction
                != admitted.after_subset.transaction
            && admitted.after_same_value_subset.transaction->first == 2U
            && admitted.after_same_value_subset.event
                == admitted.after_subset.event
            && admitted.after_same_value_subset.event_stamp.origin.process_domain
                == admitted.after_subset.event_stamp.origin.process_domain
            && admitted.after_same_value_subset.event_stamp.origin.phase
                == admitted.after_subset.event_stamp.origin.phase
            && admitted.after_same_value_subset.event_stamp.systemverilog_round
                == admitted.after_subset.event_stamp.systemverilog_round
            && !admitted.after_same_value_subset.legacy_composite_active
            && admitted.same_value_value_revision_delta == 0U
            && admitted.same_value_a4_revision_delta == 0U
            && admitted.same_value_reader_callback_delta == 0U,
        "same-value group publication advances transaction metadata without changing event or A4 role revision");
    require(admitted.after_singleton.current == singleton_current
            && admitted.after_singleton.previous == subset_current
            && admitted.after_singleton.stored == singleton_current
            && admitted.after_singleton.owners
                == std::array { owner0, singleton_owner1, owner2 }
            && admitted.after_singleton.raw_owners
                == std::array { owner0, singleton_owner1, owner2 }
            && admitted.after_singleton.all_owner_slots_bound
            && admitted.after_singleton.a4_revision
                > admitted.after_same_value_subset.a4_revision
            && admitted.after_singleton.value_revision
                > admitted.after_same_value_subset.value_revision
            && admitted.after_singleton.event
                != admitted.after_same_value_subset.event
            && admitted.after_singleton.transaction
                != admitted.after_same_value_subset.transaction
            && admitted.singleton_reader_callback_delta == 1U
            && admitted.singleton_target_revision_delta > 0U
            && !admitted.after_singleton.legacy_composite_active,
        "a singleton generic UpdateSlice declines the group route and preserves the certified per-owner A4 fallback");
    require(same_generic_three_owner_slice_frame(
                admitted.after_subset, checked.after_subset)
            && same_generic_three_owner_slice_frame(
                admitted.after_same_value_subset,
                checked.after_same_value_subset)
            && same_generic_three_owner_slice_frame(
                admitted.after_singleton, checked.after_singleton),
        "selected-subset, same-value, and singleton generic slices match checked publication");
}

void exercise_std_logic_scalar_driver_coverage()
{
    ScopedEnvironment kernel_disabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "0" };
    constexpr auto kind = ValueKind::logic9;

    {
        Interpreter interpreter;
        const auto low = interpreter.add_signal({ "scalar_disjoint.low",
            all_z_value(9U, kind), ResolutionKind::none, kind });
        const auto high = interpreter.add_signal({ "scalar_disjoint.high",
            all_z_value(9U, kind), ResolutionKind::none, kind });
        const auto target = interpreter.add_signal({ "scalar_disjoint.target",
            all_z_value(18U, kind), ResolutionKind::std_logic, kind });
        require(interpreter.add_process(vhdl_projected_slice_writer(
                    0U, low, target, 0U, 9U, kind)) == 0U
                && interpreter.add_process(vhdl_projected_slice_writer(
                    1U, high, target, 9U, 9U, kind)) == 1U,
            "narrow disjoint scalar owners retain both static ranges");
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "narrow disjoint scalar owners reach the initial quiet point");
        interpreter.deposit_signal(low,
            PackedLogic4::from_logic9_msb_string("UX01ZWLH-"));
        interpreter.deposit_signal(high,
            PackedLogic4::from_logic9_msb_string("UX01ZWLH-"));
        require(interpreter.run().status == RunStatus::completed
                && interpreter.signal_value(target).to_msb_string()
                    == "UX01ZWLH-UX01ZWLH-",
            "two disjoint static owners preserve every std_logic symbol, "
            "including each singly driven dash");
    }

    {
        Interpreter interpreter;
        const auto dash = interpreter.add_signal({ "scalar_overlap.dash",
            all_z_value(1U, kind), ResolutionKind::none, kind });
        auto initial_z_source = PackedLogic4 { 1U, Logic4::x };
        initial_z_source.fill(Logic9::u);
        const auto z = interpreter.add_signal({ "scalar_overlap.z",
            initial_z_source, ResolutionKind::none, kind });
        const auto target = interpreter.add_signal({ "scalar_overlap.target",
            all_z_value(1U, kind), ResolutionKind::std_logic, kind });
        require(interpreter.add_process(vhdl_projected_slice_writer(
                    0U, dash, target, 0U, 1U, kind)) == 0U
                && interpreter.add_process(vhdl_projected_slice_writer(
                    1U, z, target, 0U, 1U, kind)) == 1U,
            "overlapping scalar owners retain both explicit drivers");
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "overlapping scalar owners reach the initial quiet point");
        interpreter.deposit_signal(dash,
            PackedLogic4::from_logic9_msb_string("-"));
        interpreter.deposit_signal(z,
            PackedLogic4::from_logic9_msb_string("Z"));
        require(interpreter.run().status == RunStatus::completed
                && interpreter.signal_value(target).to_msb_string() == "X",
            "an explicit Z driver overlapping dash resolves to X");
    }

    {
        Interpreter interpreter;
        const auto dash = interpreter.add_signal({ "scalar_uncovered.dash",
            all_z_value(1U, kind), ResolutionKind::none, kind });
        const auto one = interpreter.add_signal({ "scalar_uncovered.one",
            all_z_value(1U, kind), ResolutionKind::none, kind });
        auto initial_target = PackedLogic4 { 3U, Logic4::x };
        initial_target.fill(Logic9::u);
        const auto target = interpreter.add_signal({ "scalar_uncovered.target",
            initial_target, ResolutionKind::std_logic, kind });
        require(interpreter.add_process(vhdl_projected_slice_writer(
                    0U, dash, target, 0U, 1U, kind)) == 0U
                && interpreter.add_process(vhdl_projected_slice_writer(
                    1U, one, target, 1U, 1U, kind)) == 1U,
            "uncovered scalar keeps two finite driver ranges");
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "uncovered scalar owners reach the initial quiet point");
        interpreter.deposit_signal(dash,
            PackedLogic4::from_logic9_msb_string("-"));
        interpreter.deposit_signal(one,
            PackedLogic4::from_logic9_msb_string("1"));
        require(interpreter.run().status == RunStatus::completed
                && interpreter.signal_value(target).to_msb_string() == "U1-",
            "an uncovered scalar remains U while covered bits retain "
            "their independent single-driver values");
        interpreter.force_signal(target,
            PackedLogic4::from_logic9_msb_string("000"));
        require(interpreter.signal_value(target).to_msb_string() == "000",
            "force overrides covered and uncovered scalar values");
        interpreter.deposit_signal(dash,
            PackedLogic4::from_logic9_msb_string("H"));
        require(interpreter.run().status == RunStatus::completed
                && interpreter.signal_value(target).to_msb_string() == "000",
            "a driver update under force does not expose its new scalar value");
        interpreter.release_signal(target);
        require(interpreter.signal_value(target).to_msb_string() == "U1H",
            "release restores uncovered U and the committed forced-period driver update");
    }

    {
        Interpreter interpreter;
        const auto low = interpreter.add_signal({ "scalar_union.low",
            all_z_value(1U, kind), ResolutionKind::none, kind });
        const auto high = interpreter.add_signal({ "scalar_union.high",
            all_z_value(1U, kind), ResolutionKind::none, kind });
        auto initial_middle = PackedLogic4 { 1U, Logic4::x };
        initial_middle.fill(Logic9::u);
        const auto middle = interpreter.add_signal({ "scalar_union.middle",
            initial_middle, ResolutionKind::none, kind });
        const auto target = interpreter.add_signal({ "scalar_union.target",
            all_z_value(3U, kind), ResolutionKind::std_logic, kind });

        Process union_writer;
        union_writer.id = 0U;
        union_writer.name = "scalar_union_writer";
        union_writer.scheduling_domain = ProcessSchedulingDomain::generic;
        union_writer.register_count = 2U;
        union_writer.register_value_kinds = { kind, kind };
        union_writer.static_sensitivity = {
            { low, EdgeKind::any }, { high, EdgeKind::any }
        };
        union_writer.driver_regions = {
            { target, 0U, 1U, false }, { target, 2U, 1U, false }
        };
        union_writer.operations = {
            ReadSignal { 0U, low },
            WriteProjectedSlice { target, 0U, 0U, 0U, 0U,
                ProjectedDelayMode::inertial },
            ReadSignal { 1U, high },
            WriteProjectedSlice { target, 1U, 2U, 0U, 0U,
                ProjectedDelayMode::inertial },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(union_writer)) == 0U
                && interpreter.add_process(vhdl_projected_slice_writer(
                    1U, middle, target, 1U, 1U, kind)) == 1U,
            "one process retains the union of two static scalar ranges");
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "union scalar owners reach the initial quiet point");
        interpreter.deposit_signal(low,
            PackedLogic4::from_logic9_msb_string("-"));
        interpreter.deposit_signal(high,
            PackedLogic4::from_logic9_msb_string("1"));
        interpreter.deposit_signal(middle,
            PackedLogic4::from_logic9_msb_string("Z"));
        require(interpreter.run().status == RunStatus::completed
                && interpreter.signal_value(target).to_msb_string() == "1Z-",
            "one process's two disjoint ranges and a separate middle owner "
            "preserve each scalar source count");
    }
}

[[nodiscard]] bool run_vhdl_projected_wide_preflight_failure(
    const std::size_t failure_index)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled {
        "FSIM_PROFILE_SV_WAVES", "1" };

    constexpr std::uint32_t width = 129U;
    constexpr ValueKind kind = ValueKind::logic9;
    const PackedLogic4 initial_input
        = PackedLogic4::from_logic9_msb_string(std::string(width, 'U'));
    const PackedLogic4 declared_output
        = PackedLogic4::from_logic9_msb_string(std::string(width, 'Z'));
    // register_driver resolves the new Logic9 owner's initial U value into
    // current, LAST and stored before the first scheduled projected write.
    const PackedLogic4 initial_output
        = PackedLogic4::from_logic9_msb_string(std::string(width, 'U'));
    const auto first_value = value_for(width, kind, 4U);
    const auto retry_value = value_for(width, kind, 5U);

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "wide_vhdl_failure.input", initial_input,
        ResolutionKind::none, kind });
    const auto target = interpreter.add_signal({
        "wide_vhdl_failure.target", declared_output,
        ResolutionKind::std_logic, kind });
    auto writer = vhdl_projected_writer(0U, input, target);
    writer.initialize = true;
    require(interpreter.add_process(std::move(writer)) == 0U,
        "projected failure writer retains its generic program identity");
    const auto& registered = interpreter.process_program(0U);
    std::size_t resumes { };
    interpreter.set_process_executor(0U,
        std::make_unique<ScheduledVhdlProjectedFailureExecutor>(input,
            target, failure_index, resumes,
            ProcessExecutorProgramBinding { registered, registered, 0U }));

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "projected failure fixture reaches its initial quiet point");
    require(resumes == 1U,
        "the initial U-to-U projected write warms the generic commit queue");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U),
        "projected failure target begins with all four roles bound");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(implementation.process_signal_access_is_complete(0U)
            && !implementation.process_region_kernel_eligible(0U),
        "the custom projected writer uses checked generic scheduling");
    const auto before = OwnedDriverDemotionTestAccess::packed_a4_values(
        interpreter, target, 0U);
    const auto before_texts = role_texts(before);
    const auto before_revision
        = implementation.signal_value_revisions.at(target);
    const auto before_event = implementation.signal_events.at(target);
    const auto before_transaction
        = implementation.signal_transactions.at(target);
    if (before[0U] != initial_output
        || before[1U] != initial_output
        || before[2U] != initial_output
        || before[3U] != initial_output) {
        return false;
    }

    // Distinct ticks give the committed prefix and retry independent event
    // and transaction stamps while retaining the generic deposit route.
    interpreter.scheduler().schedule_at(1U, SchedulerPhase::active, 0U,
        [&](Scheduler&) { interpreter.deposit_signal(input, first_value); });
    if (!OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 0U)) {
        return false;
    }
    RunStatus status = RunStatus::completed;
    try {
        status = interpreter.run().status;
    } catch (const std::bad_alloc&) {
        clear_allocation_failure();
        return false;
    }
    const bool injected = allocation_failure_was_injected();
    clear_allocation_failure();
    if (!injected || status != RunStatus::completed || resumes != 2U) {
        return false;
    }

    const auto after_failure = ordinary_role_values(interpreter, target, 0U);
    const auto after_failure_event = implementation.signal_events.at(target);
    const auto after_failure_transaction
        = implementation.signal_transactions.at(target);
    const bool first_prefix_complete
        = implementation.signal_value_revisions.at(target)
                == before_revision + 1U
            && after_failure[0U] == first_value
            && after_failure[1U] == initial_output
            && after_failure[2U] == first_value
            && after_failure[3U] == first_value
            && after_failure_event && after_failure_transaction
            && *after_failure_event == *after_failure_transaction
            && after_failure_event != before_event
            && after_failure_transaction != before_transaction
            && before[0U] == initial_output
            && before[1U] == initial_output
            && before[2U] == initial_output
            && before[3U] == initial_output
            && role_texts(before) == before_texts;
    if (!first_prefix_complete) {
        return false;
    }

    interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
        [&](Scheduler&) { interpreter.deposit_signal(input, retry_value); });
    if (interpreter.run().status != RunStatus::completed || resumes != 3U) {
        return false;
    }
    const auto after_retry = ordinary_role_values(interpreter, target, 0U);
    const auto retry_event = implementation.signal_events.at(target);
    const auto retry_transaction
        = implementation.signal_transactions.at(target);
    return implementation.signal_value_revisions.at(target)
                == before_revision + 2U
        && after_retry[0U] == retry_value
        && after_retry[1U] == first_value
        && after_retry[2U] == retry_value
        && after_retry[3U] == retry_value
        && retry_event && retry_transaction
        && *retry_event == *retry_transaction
        && retry_event != after_failure_event
        && before[0U] == initial_output
        && before[1U] == initial_output
        && before[2U] == initial_output
        && before[3U] == initial_output
        && role_texts(before) == before_texts;
}

void exercise_update_commit_scratch_failure_and_retry()
{
    for (std::size_t failure_index = 0U; failure_index < 4U;
         ++failure_index) {
        Interpreter interpreter;
        const PackedLogic4 initial { 1U, Logic4::zero };
        const auto signal = interpreter.add_signal({
            "commit_preparation.input", initial, ResolutionKind::none });
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "commit preparation begins at a quiet point");
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);

        bool failed { };
        arm_allocation_failure(failure_index);
        try {
            implementation.schedule_update_commit();
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        const bool injected = allocation_failure_was_injected();
        clear_allocation_failure();
        require(failed && injected
                && !implementation.update_commit_scheduled,
            "scratch preparation failure does not publish a commit ticket");

        implementation.schedule_update_commit();
        const auto signal_count = implementation.signals.size();
        require(implementation.unresolved_update_scratch.size() >= signal_count
                && implementation.driver_update_scratch.size() >= signal_count
                && implementation.resolved_update_marked.size() >= signal_count
                && implementation.direct_single_driver_commit_marked.size()
                    >= signal_count
                && implementation.update_commit_words.size()
                    >= signal_count / 64U
                        + (signal_count % 64U != 0U ? 1U : 0U),
            "retry completes each scratch vector before publishing the ticket");
        require(interpreter.run().status == RunStatus::completed
                && !implementation.update_commit_scheduled
                && interpreter.signal_value(signal) == initial,
            "retried empty commit drains without changing the signal");
    }
}

void exercise_update_commit_word_order_and_reentry()
{
    constexpr auto signal_count = std::size_t { 130U };
    Interpreter interpreter;
    std::vector<SignalId> signals;
    signals.reserve(signal_count);
    for (std::size_t index = 0U; index < signal_count; ++index) {
        signals.push_back(interpreter.add_signal({
            "dirty_word." + std::to_string(index),
            PackedLogic4(1U, Logic4::x),
            index % 2U == 0U
                ? ResolutionKind::none : ResolutionKind::sv_wire }));
    }
    interpreter.start();

    struct Change {
        SignalId signal { };
        PackedLogic4 value;
        SimulationTick time { };
    };
    std::vector<Change> changes;
    changes.reserve(signal_count + 1U);
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    std::vector<std::uint64_t> initial_revisions;
    initial_revisions.reserve(signal_count);
    for (const auto signal : signals) {
        initial_revisions.push_back(
            implementation.signal_value_revisions.at(signal));
    }
    bool reentrant_batch_staged { };
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time) {
            changes.push_back({ signal, value, time });
            if (!reentrant_batch_staged && signal == signals.front()) {
                reentrant_batch_staged = true;
                implementation.stage_update(
                    signals.back(), PackedLogic4(1U, Logic4::zero));
            }
        });

    const auto expected_value = [](const std::size_t index) {
        return PackedLogic4(1U,
            index == 64U || index % 2U == 0U
                ? Logic4::zero : Logic4::one);
    };
    for (std::size_t index = signal_count; index != 0U; --index) {
        const auto slot = index - 1U;
        if (slot == 64U) {
            // Two source writes coalesce into one row at the same dirty bit.
            implementation.stage_update(
                signals[slot], PackedLogic4(1U, Logic4::one));
        }
        implementation.stage_update(signals[slot], expected_value(slot));
    }

    require(interpreter.run().status == RunStatus::completed
            && reentrant_batch_staged
            && changes.size() == signal_count + 1U,
        "reverse multiword staging and reentrant next-batch commit complete");
    for (std::size_t index = 0U; index < signal_count; ++index) {
        require(changes[index].signal == signals[index]
                && changes[index].value == expected_value(index)
                && changes[index].time == 0U,
            "dirty-word commits preserve ascending SignalId order across words");
    }
    require(changes.back().signal == signals.back()
            && changes.back().value == PackedLogic4(1U, Logic4::zero)
            && changes.back().time == 0U,
        "observer-staged write publishes only in a later commit batch");
    for (std::size_t index = 0U; index < signal_count; ++index) {
        const auto signal = signals[index];
        const auto expected_revision = initial_revisions[index]
            + (index + 1U == signal_count ? 2U : 1U);
        require(implementation.signal_value_revisions.at(signal)
                    == expected_revision
                && implementation.signal_events.at(signal)
                    == implementation.signal_transactions.at(signal)
                && implementation.signal_events.at(signal).has_value()
                && implementation.signal_event_scheduling_stamps.at(signal)
                        .origin.process_domain
                    == ProcessSchedulingDomain::generic
                && implementation.signal_event_scheduling_stamps.at(signal)
                        .origin.phase
                    == SchedulerPhase::active,
            "word-ordered batches preserve event, transaction, revision, and origin metadata");
    }
}

void exercise_update_commit_container_proxy_interleaving()
{
    Interpreter interpreter;
    const auto low = interpreter.add_signal({
        "dirty_proxy.low", PackedLogic4(1U, Logic4::x),
        ResolutionKind::none });
    const auto first = interpreter.add_signal({
        "dirty_proxy.words[1]", PackedLogic4(1U, Logic4::zero),
        ResolutionKind::sv_wire });
    const auto second = interpreter.add_signal({
        "dirty_proxy.words[0]", PackedLogic4(1U, Logic4::zero),
        ResolutionKind::sv_wire });
    const auto proxy = interpreter.add_signal({
        "dirty_proxy.words", PackedLogic4::from_msb_string("00"),
        ResolutionKind::sv_wire });
    const auto high = interpreter.add_signal({
        "dirty_proxy.high", PackedLogic4(1U, Logic4::x),
        ResolutionKind::none });
    ContainerType type;
    type.fixed = true;
    type.element_width = 1U;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    const auto object = interpreter.add_container_object({
        "dirty_proxy.words",
        ContainerValue { type,
            { PackedLogic4(1U, Logic4::zero),
                PackedLogic4(1U, Logic4::zero) }, { } },
        std::nullopt });
    interpreter.add_container_element_signal_alias(
        { object, 0U, first, true, true });
    interpreter.add_container_element_signal_alias(
        { object, 1U, second, true, true });
    interpreter.add_container_aggregate_signal_alias(
        { object, proxy, true, true });
    interpreter.start();

    std::vector<SignalId> published_order;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4&,
            const SimulationTick) {
            published_order.push_back(signal);
        });
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const std::array<SignalId, 5U> family_order {
        low, first, second, proxy, high };
    std::array<std::uint64_t, 5U> initial_revisions { };
    for (std::size_t index = 0U; index < family_order.size(); ++index) {
        initial_revisions[index]
            = implementation.signal_value_revisions.at(family_order[index]);
    }
    implementation.stage_update(
        high, PackedLogic4(1U, Logic4::one));
    implementation.stage_update(
        proxy, PackedLogic4::from_msb_string("11"));
    implementation.stage_update(
        low, PackedLogic4(1U, Logic4::one));

    // The family is ordered by its proxy SignalId. Within that publication,
    // the aggregate proxy observer runs before the deferred leaf observers.
    require(interpreter.run().status == RunStatus::completed
            && published_order
                == std::vector<SignalId> { low, proxy, first, second, high }
            && interpreter.signal_value(low) == PackedLogic4(1U, Logic4::one)
            && interpreter.signal_value(first) == PackedLogic4(1U, Logic4::one)
            && interpreter.signal_value(second) == PackedLogic4(1U, Logic4::one)
            && interpreter.signal_value(proxy)
                == PackedLogic4::from_msb_string("11")
            && interpreter.signal_value(high) == PackedLogic4(1U, Logic4::one)
            && std::ranges::all_of(
                family_order,
                [&](const SignalId signal) {
                    return implementation.signal_events.at(signal)
                            == implementation.signal_transactions.at(signal)
                        && implementation.signal_events.at(signal)
                        .has_value();
                }),
        "container family publication remains ordered by proxy SignalId");
    for (std::size_t index = 0U; index < family_order.size(); ++index) {
        require(implementation.signal_value_revisions.at(family_order[index])
                == initial_revisions[index] + 1U,
            "container interleaving preserves one revision per changed signal");
    }
}

void exercise_update_commit_word_scratch_failure_and_retry()
{
    Interpreter interpreter;
    for (std::size_t index = 0U; index < 65U; ++index) {
        (void)interpreter.add_signal({
            "dirty_word.failure." + std::to_string(index),
            PackedLogic4(1U, Logic4::zero),
            ResolutionKind::none });
    }
    interpreter.start();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto initial_revision
        = implementation.signal_value_revisions.at(SignalId { 0U });
    const auto signal_count = implementation.signals.size();
    implementation.unresolved_update_scratch.resize(signal_count);
    implementation.unresolved_update_owner_provenance.resize(signal_count);
    implementation.unresolved_update_owner_provenance_signals.reserve(
        signal_count);
    implementation.driver_update_scratch.resize(signal_count);
    implementation.resolved_update_marked.resize(signal_count);
    implementation.direct_single_driver_commit_marked.resize(signal_count);
    implementation.pending_update_values.reserve(1U);
    implementation.pending_updates.reserve(1U);
    require(implementation.update_commit_words.empty(),
        "fresh interpreter has no prepared dirty-word table");

    bool failed { };
    arm_allocation_failure(0U);
    try {
        implementation.stage_update(
            SignalId { 0U }, PackedLogic4(1U, Logic4::one));
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    const bool injected = allocation_failure_was_injected();
    clear_allocation_failure();
    require(failed && injected && !implementation.update_commit_scheduled
            && implementation.pending_updates.size() == 1U
            && implementation.pending_update_values.size() == 1U
            && implementation.signals.at(SignalId { 0U }).initial_value
                == PackedLogic4(1U, Logic4::zero),
        "dirty-word allocation failure leaves staged values uncommitted");

    implementation.schedule_update_commit();
    require(implementation.update_commit_words.size() == 2U
            && interpreter.run().status == RunStatus::completed
            && !implementation.update_commit_scheduled
            && implementation.signals.at(SignalId { 0U }).initial_value
                == PackedLogic4(1U, Logic4::one)
            && implementation.signal_value_revisions.at(SignalId { 0U })
                == initial_revision + 1U
            && implementation.signal_events.at(SignalId { 0U })
                == implementation.signal_transactions.at(SignalId { 0U }),
        "dirty-word scratch retry commits retained staged values exactly once");
}

void exercise_vhdl_projected_wide_preflight_failure_and_retry()
{
    require(run_vhdl_projected_wide_preflight_failure(0U),
        "a queued VHDL projected allocation failure preserves the committed prefix and retries");
}

struct TwoSignalFixture {
    static constexpr std::uint32_t width = 65U;
    static constexpr ValueKind kind = ValueKind::logic4;
    static constexpr std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };

    RegionGraph graph { graph_for(width, kind) };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };
    PackedLogic4 current1 { value_for(width, kind, 0U) };
    PackedLogic4 previous1 { value_for(width, kind, 1U) };
    PackedLogic4 stored1 { value_for(width, kind, 2U) };
    PackedLogic4 owner1 { value_for(width, kind, 3U) };
    PackedLogic4 current2 { value_for(width, kind, 4U) };
    PackedLogic4 previous2 { value_for(width, kind, 5U) };
    PackedLogic4 stored2 { value_for(width, kind, 6U) };
    PackedLogic4 owner2 { value_for(width, kind, 7U) };

    TwoSignalFixture()
    {
        values.seed_signal(1U, current1, previous1, stored1);
        values.seed_signal(2U, current2, previous2, stored2);
        values.seed_owner(1U, 0U, owner1);
        values.seed_owner(2U, 1U, owner2);
        values.stage_packed_signal_slots(
            1U, current1, previous1, stored1);
        values.stage_packed_signal_slots(
            2U, current2, previous2, stored2);
        values.stage_packed_owner_slot(1U, 0U, owner1);
        values.stage_packed_owner_slot(2U, 1U, owner2);
        require(values.bind_packed_slots() == 8U,
            "stale-preparation fixture binds both signal families");
    }
};

template <typename Mirror>
void check_mirror_previous_generation(const std::uint32_t width,
    const ValueKind kind,
    Mirror&& mirror)
{
    const auto graph = graph_for(width, kind);
    const std::array<SignalId, 3U> signal_ids { 0U, 1U, 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide
    };
    const auto current = value_for(width, kind, 0U);
    const auto previous = value_for(width, kind, 1U);
    const auto stored = value_for(width, kind, 2U);
    const auto owner = value_for(width, kind, 3U);
    const auto mirrored_previous = value_for(width, kind, 7U);
    values.seed_signal(2U, current, previous, stored);
    values.seed_owner(2U, 1U, owner);

    auto stale = values.prepare_owner_change(2U, 1U,
        value_for(width, kind, 4U), value_for(width, kind, 5U),
        value_for(width, kind, 6U));
    require(mirrored_previous != previous,
        "mirror-only LAST fixture changes the previous role");
    mirror(values, mirrored_previous, current, stored);
    require(values.valid() && values.current(2U) == current
            && values.previous(2U) == mirrored_previous
            && values.stored(2U) == stored
            && values.owner_value(2U, 1U) == owner,
        "mirror installs only the explicit previous role");

    values.publish(std::move(stale));
    require(!values.valid()
            && values.current(2U) == current
            && values.previous(2U) == mirrored_previous
            && values.stored(2U) == stored
            && values.owner_value(2U, 1U) == owner,
        "stale prepared publication cannot erase a mirror-only LAST update");
}

void exercise_mirror_previous_generation()
{
    check_mirror_previous_generation(65U, ValueKind::logic4,
        [](AuthoritativeSignalPlanes& values,
            const PackedLogic4& previous,
            const PackedLogic4& current,
            const PackedLogic4&) {
            values.mirror_visible(2U, previous, current);
        });
    check_mirror_previous_generation(64U, ValueKind::logic4,
        [](AuthoritativeSignalPlanes& values,
            const PackedLogic4& previous,
            const PackedLogic4& current,
            const PackedLogic4& stored) {
            values.mirror_logic4_word(2U,
                previous.unchecked_low_word(),
                current.unchecked_low_word(),
                stored.unchecked_low_word());
        });
    check_mirror_previous_generation(64U, ValueKind::logic9,
        [](AuthoritativeSignalPlanes& values,
            const PackedLogic4& previous,
            const PackedLogic4& current,
            const PackedLogic4& stored) {
            values.mirror_logic9_word(2U,
                previous.logic9_low_word(), current.logic9_low_word(),
                stored.logic9_low_word());
        });
}

void exercise_stale_distinct_signal_preparation()
{
    TwoSignalFixture fixture;
    auto stale_signal1 = fixture.values.prepare_owner_change(
        1U, 0U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 11U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 12U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 13U));
    auto interleaved_signal2 = fixture.values.prepare_owner_change(
        2U, 1U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 14U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 15U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 16U));
    fixture.values.publish(std::move(interleaved_signal2));

    const auto signal1_current = fixture.values.current(1U);
    const auto signal1_previous = fixture.values.previous(1U);
    const auto signal1_stored = fixture.values.stored(1U);
    const auto signal1_owner = fixture.values.owner_value(1U, 0U);
    const auto signal2_current = fixture.values.current(2U);
    const auto signal2_previous = fixture.values.previous(2U);
    const auto signal2_stored = fixture.values.stored(2U);
    const auto signal2_owner = fixture.values.owner_value(2U, 1U);

    fixture.values.publish(std::move(stale_signal1));
    require(!fixture.values.valid(),
        "stale distinct-signal preparation invalidates the sidecar");
    require(fixture.values.current(1U) == signal1_current
            && fixture.values.previous(1U) == signal1_previous
            && fixture.values.stored(1U) == signal1_stored
            && fixture.values.owner_value(1U, 0U) == signal1_owner
            && fixture.values.current(2U) == signal2_current
            && fixture.values.previous(2U) == signal2_previous
            && fixture.values.stored(2U) == signal2_stored
            && fixture.values.owner_value(2U, 1U) == signal2_owner,
        "stale distinct-signal publication changes no installed role");
}

void exercise_stale_same_signal_preparation()
{
    TwoSignalFixture fixture;
    auto stale = fixture.values.prepare_owner_change(
        1U, 0U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 21U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 22U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 23U));
    auto first = fixture.values.prepare_owner_change(
        1U, 0U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 24U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 25U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 26U));
    const auto first_current = value_for(
        TwoSignalFixture::width, TwoSignalFixture::kind, 25U);
    fixture.values.publish(std::move(first));
    auto second = fixture.values.prepare_owner_change(
        1U, 0U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 27U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 28U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 29U));
    const auto second_current = value_for(
        TwoSignalFixture::width, TwoSignalFixture::kind, 28U);
    fixture.values.publish(std::move(second));
    const auto latest_current = fixture.values.current(1U);
    const auto latest_previous = fixture.values.previous(1U);
    const auto latest_stored = fixture.values.stored(1U);
    const auto latest_owner = fixture.values.owner_value(1U, 0U);

    fixture.values.publish(std::move(stale));
    require(!fixture.values.valid(),
        "stale same-signal preparation invalidates the sidecar");
    require(latest_previous == first_current
            && latest_current == second_current
            && fixture.values.current(1U) == latest_current
            && fixture.values.previous(1U) == latest_previous
            && fixture.values.stored(1U) == latest_stored
            && fixture.values.owner_value(1U, 0U) == latest_owner,
        "stale same-signal publication preserves the latest current and LAST");
}

void exercise_stale_group_preparation()
{
    TwoSignalFixture fixture;
    auto stale = fixture.values.prepare_owner_change(
        1U, 0U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 31U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 32U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 33U));
    auto interleaved = fixture.values.prepare_owner_change(
        2U, 1U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 34U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 35U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 36U));
    fixture.values.publish(std::move(interleaved));
    auto fresh = fixture.values.prepare_owner_change(
        1U, 0U, value_for(TwoSignalFixture::width,
                      TwoSignalFixture::kind, 37U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 38U),
        value_for(TwoSignalFixture::width,
            TwoSignalFixture::kind, 39U));
    const auto signal1_current = fixture.values.current(1U);
    const auto signal1_previous = fixture.values.previous(1U);
    const auto signal1_stored = fixture.values.stored(1U);
    const auto signal1_owner = fixture.values.owner_value(1U, 0U);
    const auto signal2_current = fixture.values.current(2U);
    const auto signal2_previous = fixture.values.previous(2U);
    const auto signal2_stored = fixture.values.stored(2U);
    const auto signal2_owner = fixture.values.owner_value(2U, 1U);
    std::vector<AuthoritativeSignalPlanes::PreparedMutation> group;
    group.push_back(std::move(stale));
    group.push_back(std::move(fresh));
    fixture.values.publish_group(std::move(group));
    require(!fixture.values.valid(),
        "mixed-generation group invalidates the sidecar");
    require(fixture.values.current(1U) == signal1_current
            && fixture.values.previous(1U) == signal1_previous
            && fixture.values.stored(1U) == signal1_stored
            && fixture.values.owner_value(1U, 0U) == signal1_owner
            && fixture.values.current(2U) == signal2_current
            && fixture.values.previous(2U) == signal2_previous
            && fixture.values.stored(2U) == signal2_stored
            && fixture.values.owner_value(2U, 1U) == signal2_owner,
        "mixed-generation group declines every member before any role write");
}

RegionGraph disjoint_owner_group_graph_for(const std::uint32_t width)
{
    const auto lower_width = width / 2U;
    const auto upper_width = width - lower_width;
    const std::array descriptors {
        RegionSignalDescriptor { lower_width },
        RegionSignalDescriptor { upper_width },
        RegionSignalDescriptor { width },
    };
    const auto first = disjoint_slice_writer(0U, 0U, 2U,
        0U, lower_width, ValueKind::logic4, true);
    const auto second = disjoint_slice_writer(1U, 1U, 2U,
        lower_width, upper_width, ValueKind::logic4, true);
    const std::array<const Process*, 2U> programs { &first, &second };
    auto graph = RegionGraph::build(programs, descriptors);
    require(graph.signals().size() > 2U,
        "disjoint owner group graph retains the target signal");
    const auto& target = graph.signals()[2U];
    require(target.drivers == RegionDriverClass::disjoint_partial
            && !target.writers_unknown
            && !target.partial_projected_transactions,
        "disjoint owner group graph has exact ordinary partial writers");
    return graph;
}

struct DisjointOwnerGroupPlaneFixture {
    static constexpr std::uint32_t width = 65U;
    static constexpr std::uint32_t lower_width = width / 2U;
    static constexpr std::uint32_t upper_width = width - lower_width;
    static constexpr SignalId signal = 2U;
    static constexpr ProcessId owner0 = 0U;
    static constexpr ProcessId owner1 = 1U;
    static constexpr std::array<SignalId, 3U> signal_ids {
        0U, 1U, signal
    };

    RegionGraph graph { disjoint_owner_group_graph_for(width) };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide_disjoint_owners
    };
    PackedLogic4 current { width, Logic4::z };
    PackedLogic4 previous { value_for(width, ValueKind::logic4, 3U) };
    PackedLogic4 stored { width, Logic4::z };
    PackedLogic4 raw_owner0 { width, Logic4::z };
    PackedLogic4 raw_owner1 { width, Logic4::z };

    DisjointOwnerGroupPlaneFixture()
    {
        values.seed_signal(signal, current, previous, stored);
        values.seed_owner(signal, owner0, raw_owner0);
        values.seed_owner(signal, owner1, raw_owner1);
        values.stage_packed_signal_slots(
            signal, current, previous, stored);
        values.stage_packed_owner_slot(signal, owner0, raw_owner0);
        values.stage_packed_owner_slot(signal, owner1, raw_owner1);
        require(values.bind_packed_slots() == 5U,
            "owner-group fixture binds its visible and two raw owner roles");
    }
};

struct DisjointOwnerGroupValues {
    PackedLogic4 current;
    PackedLogic4 owner0;
    PackedLogic4 owner1;
};

DisjointOwnerGroupValues disjoint_owner_group_values()
{
    auto owner0 = all_z_value(
        DisjointOwnerGroupPlaneFixture::width, ValueKind::logic4);
    auto owner1 = all_z_value(
        DisjointOwnerGroupPlaneFixture::width, ValueKind::logic4);
    const PackedLogic4 lower {
        DisjointOwnerGroupPlaneFixture::lower_width, Logic4::zero
    };
    const PackedLogic4 upper {
        DisjointOwnerGroupPlaneFixture::upper_width, Logic4::one
    };
    insert_range(owner0, lower, 0U, ValueKind::logic4);
    insert_range(owner1, upper,
        DisjointOwnerGroupPlaneFixture::lower_width, ValueKind::logic4);
    auto current = all_z_value(
        DisjointOwnerGroupPlaneFixture::width, ValueKind::logic4);
    insert_range(current, lower, 0U, ValueKind::logic4);
    insert_range(current, upper,
        DisjointOwnerGroupPlaneFixture::lower_width, ValueKind::logic4);
    return { std::move(current), std::move(owner0), std::move(owner1) };
}

void prepare_disjoint_owner_group_rows(
    DisjointOwnerGroupPlaneFixture& fixture,
    std::array<AuthoritativeSignalPlanes::PreparedMutation, 2U>& rows,
    const DisjointOwnerGroupValues& next)
{
    for (auto& row : rows) {
        if (row.words.capacity() < 2U) {
            row.words.reserve(2U);
        }
    }
    fixture.values.prepare_owner_group_change_into(rows[0U],
        DisjointOwnerGroupPlaneFixture::signal,
        DisjointOwnerGroupPlaneFixture::owner0, next.owner0,
        next.current, next.current);
    fixture.values.prepare_owner_group_change_into(rows[1U],
        DisjointOwnerGroupPlaneFixture::signal,
        DisjointOwnerGroupPlaneFixture::owner1, next.owner1,
        next.current, next.current);
}

bool disjoint_owner_group_roles_match(
    const DisjointOwnerGroupPlaneFixture& fixture,
    const DisjointOwnerGroupValues& expected,
    const PackedLogic4& expected_previous)
{
    return fixture.values.valid()
        && fixture.values.current(
            DisjointOwnerGroupPlaneFixture::signal) == expected.current
        && fixture.values.previous(
            DisjointOwnerGroupPlaneFixture::signal) == expected_previous
        && fixture.values.stored(
            DisjointOwnerGroupPlaneFixture::signal) == expected.current
        && fixture.values.owner_value(
            DisjointOwnerGroupPlaneFixture::signal,
            DisjointOwnerGroupPlaneFixture::owner0) == expected.owner0
        && fixture.values.owner_value(
            DisjointOwnerGroupPlaneFixture::signal,
            DisjointOwnerGroupPlaneFixture::owner1) == expected.owner1;
}

Process vhdl_projected_disjoint_multi_writer(
    const ProcessId id,
    const SignalId first_input,
    const SignalId first_output,
    const std::uint32_t first_offset,
    const std::uint32_t first_width,
    const SignalId second_input,
    const SignalId second_output,
    const std::uint32_t second_offset,
    const std::uint32_t second_width,
    const ValueKind kind)
{
    Process process;
    process.id = id;
    process.name = "a4_multi_group_projected_writer_"
        + std::to_string(id);
    process.language_standard = "2008";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = 2U;
    process.register_value_kinds = { kind, kind };
    process.static_sensitivity = { { first_input, EdgeKind::any } };
    process.driver_regions = {
        { first_output, first_offset, first_width, false },
        { second_output, second_offset, second_width, false },
    };
    process.operations = {
        ReadSignal { 0U, first_input },
        WriteProjectedSlice {
            first_output, 0U, first_offset, 0U, 0U,
            ProjectedDelayMode::inertial },
        ReadSignal { 1U, second_input },
        WriteProjectedSlice {
            second_output, 1U, second_offset, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

void exercise_prepared_disjoint_owner_group_publication()
{
    const auto expected = disjoint_owner_group_values();
    DisjointOwnerGroupPlaneFixture fixture;
    std::array<AuthoritativeSignalPlanes::PreparedMutation, 2U> rows;
    prepare_disjoint_owner_group_rows(fixture, rows, expected);
    const auto revision_before = fixture.values.revision();
    const auto old_current = fixture.current;
    const auto old_previous = fixture.previous;
    const auto old_stored = fixture.stored;
    const auto old_owner0 = fixture.raw_owner0;
    const auto old_owner1 = fixture.raw_owner1;

    require(!fixture.values.begin_prepared_publication(rows[0U]),
        "a group-only row cannot enter single-row publication preflight");
    fixture.values.cancel_prepared_publication(rows[0U]);
    require(!fixture.values.begin_prepared_publication(rows[0U]),
        "single-row cancellation cannot remove the group-only marker");
    fixture.values.prepare_owner_change_into(rows[0U],
        DisjointOwnerGroupPlaneFixture::signal,
        DisjointOwnerGroupPlaneFixture::owner0, old_owner0,
        old_current, old_stored);
    require(!fixture.values.begin_prepared_publication(rows[0U]),
        "ordinary row reuse cannot convert a group row into a single row");
    fixture.values.cancel_prepared_owner_group_publication(rows);
    require(fixture.values.valid()
            && fixture.values.revision() == revision_before
            && fixture.current == old_current
            && fixture.previous == old_previous
            && fixture.stored == old_stored
            && fixture.raw_owner0 == old_owner0
            && fixture.raw_owner1 == old_owner1,
        "whole-group cancel releases rows without changing any bound role");

    prepare_disjoint_owner_group_rows(fixture, rows, expected);
    require(fixture.values.begin_prepared_owner_group_publication(rows),
        "both prepared direct owners pass one whole-group preflight");
    require(fixture.values.publish_prepared_owner_group(rows),
        "the complete owner group publishes through its group-only API");
    require(fixture.values.revision() == revision_before + 1U
            && fixture.values.packed_slots_bound()
            && disjoint_owner_group_roles_match(
                fixture, expected, old_current),
        "one group publication advances one generation and installs all roles");

    auto second = disjoint_owner_group_values();
    second.owner0 = all_z_value(
        DisjointOwnerGroupPlaneFixture::width, ValueKind::logic4);
    second.current = all_z_value(
        DisjointOwnerGroupPlaneFixture::width, ValueKind::logic4);
    const PackedLogic4 lower_ones {
        DisjointOwnerGroupPlaneFixture::lower_width, Logic4::one
    };
    const PackedLogic4 upper_ones {
        DisjointOwnerGroupPlaneFixture::upper_width, Logic4::one
    };
    insert_range(second.owner0, lower_ones, 0U, ValueKind::logic4);
    insert_range(second.current, lower_ones, 0U, ValueKind::logic4);
    insert_range(second.current, upper_ones,
        DisjointOwnerGroupPlaneFixture::lower_width, ValueKind::logic4);
    fixture.values.prepare_owner_change_into(rows[0U],
        DisjointOwnerGroupPlaneFixture::signal,
        DisjointOwnerGroupPlaneFixture::owner0, second.owner0,
        second.current, second.current);
    require(fixture.values.begin_prepared_publication(rows[0U]),
        "successful group publish clears markers for explicit row reuse");
    fixture.values.publish(std::move(rows[0U]));
    require(fixture.values.valid()
            && fixture.values.current(
                DisjointOwnerGroupPlaneFixture::signal) == second.current
            && fixture.values.previous(
                DisjointOwnerGroupPlaneFixture::signal) == expected.current
            && fixture.values.owner_value(
                DisjointOwnerGroupPlaneFixture::signal,
                DisjointOwnerGroupPlaneFixture::owner0) == second.owner0
            && fixture.values.owner_value(
                DisjointOwnerGroupPlaneFixture::signal,
                DisjointOwnerGroupPlaneFixture::owner1) == expected.owner1,
        "a completed group row can be reused by ordinary publication");
}

void exercise_disjoint_owner_group_cow_failure()
{
    const auto expected = disjoint_owner_group_values();
    DisjointOwnerGroupPlaneFixture fixture;
    std::array<AuthoritativeSignalPlanes::PreparedMutation, 2U> rows;
    prepare_disjoint_owner_group_rows(fixture, rows, expected);
    const auto old_current = fixture.current;
    const auto old_previous = fixture.previous;
    const auto old_stored = fixture.stored;
    const auto old_owner0 = fixture.raw_owner0;
    const auto old_owner1 = fixture.raw_owner1;
    const auto revision_before = fixture.values.revision();
    const std::array<PackedLogic4PlaneReadLease, 5U> leases {
        fixture.values.plane_read_lease(
            DisjointOwnerGroupPlaneFixture::signal,
            PackedPlaneRole::current),
        fixture.values.plane_read_lease(
            DisjointOwnerGroupPlaneFixture::signal,
            PackedPlaneRole::previous),
        fixture.values.plane_read_lease(
            DisjointOwnerGroupPlaneFixture::signal,
            PackedPlaneRole::stored),
        fixture.values.plane_read_lease(
            DisjointOwnerGroupPlaneFixture::signal,
            PackedPlaneRole::owner,
            DisjointOwnerGroupPlaneFixture::owner0),
        fixture.values.plane_read_lease(
            DisjointOwnerGroupPlaneFixture::signal,
            PackedPlaneRole::owner,
            DisjointOwnerGroupPlaneFixture::owner1),
    };
    require(std::ranges::all_of(leases,
                [](const auto& lease) { return static_cast<bool>(lease); }),
        "group COW fixture retains every original role lease");

    arm_allocation_failure(0U);
    const bool first_preflight
        = fixture.values.begin_prepared_owner_group_publication(rows);
    const bool injected = allocation_failure_was_injected();
    clear_allocation_failure();
    require(!first_preflight && injected
            && fixture.values.valid()
            && fixture.values.revision() == revision_before
            && fixture.current == old_current
            && fixture.previous == old_previous
            && fixture.stored == old_stored
            && fixture.raw_owner0 == old_owner0
            && fixture.raw_owner1 == old_owner1,
        "failed whole-group COW preflight leaves every live role unchanged");
    require(lease_matches(leases[0U], old_current)
            && lease_matches(leases[1U], old_previous)
            && lease_matches(leases[2U], old_stored)
            && lease_matches(leases[3U], old_owner0)
            && lease_matches(leases[4U], old_owner1),
        "failed whole-group COW leaves all retained role leases immutable");
    require(!fixture.values.begin_prepared_publication(rows[0U]),
        "failed group preflight still cannot be routed through one row");

    fixture.values.cancel_prepared_owner_group_publication(rows);
    prepare_disjoint_owner_group_rows(fixture, rows, expected);
    require(fixture.values.begin_prepared_owner_group_publication(rows)
            && fixture.values.publish_prepared_owner_group(rows),
        "whole-group COW retries after explicit group cancellation");
    require(disjoint_owner_group_roles_match(
                fixture, expected, old_current)
            && lease_matches(leases[0U], old_current)
            && lease_matches(leases[1U], old_previous)
            && lease_matches(leases[2U], old_stored)
            && lease_matches(leases[3U], old_owner0)
            && lease_matches(leases[4U], old_owner1),
        "successful retry installs all roles while old leases retain snapshots");
}

void exercise_disjoint_owner_group_single_row_bypass()
{
    const auto expected = disjoint_owner_group_values();
    DisjointOwnerGroupPlaneFixture fixture;
    std::array<AuthoritativeSignalPlanes::PreparedMutation, 2U> rows;
    prepare_disjoint_owner_group_rows(fixture, rows, expected);
    const auto revision_before = fixture.values.revision();
    const auto old_current = fixture.current;
    const auto old_previous = fixture.previous;
    const auto old_stored = fixture.stored;
    const auto old_owner0 = fixture.raw_owner0;
    const auto old_owner1 = fixture.raw_owner1;
    require(fixture.values.begin_prepared_owner_group_publication(rows),
        "single-row bypass fixture establishes the full group preflight");

    fixture.values.publish(std::move(rows[0U]));
    fixture.values.cancel_prepared_owner_group_publication(rows);
    require(!fixture.values.valid()
            && fixture.values.revision() == revision_before
            && fixture.current == old_current
            && fixture.previous == old_previous
            && fixture.stored == old_stored
            && fixture.raw_owner0 == old_owner0
            && fixture.raw_owner1 == old_owner1,
        "ordinary single-row publish cannot expose a preflighted group prefix");
}

struct DisjointOwnerMultiGroupSignalResult {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner0;
    PackedLogic4 owner1;
    std::uint64_t revision_delta { };
    std::uint64_t a4_generation_delta { };
    std::uint64_t event_delta { };
    std::uint64_t transaction_delta { };
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction_stamp;
    bool slots_bound_before { };
    bool slots_bound_after { };
    bool roles_match { };
};

struct DisjointOwnerMultiGroupResult {
    DisjointOwnerMultiGroupSignalResult first;
    DisjointOwnerMultiGroupSignalResult interleaved;
    DisjointOwnerMultiGroupSignalResult second;
    bool same_authoritative_component { };
    bool signal_ids_interleaved { };
    bool projected_owner_sources { };
    bool completed { };
};

DisjointOwnerMultiGroupResult run_disjoint_owner_multi_group_round(
    const char* disjoint_owner_policy)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_policy_scope {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_owner_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };

    constexpr auto first_width = std::uint32_t { 65U };
    constexpr auto first_lower_width = first_width / 2U;
    constexpr auto first_upper_width = first_width - first_lower_width;
    constexpr auto second_width = std::uint32_t { 129U };
    constexpr auto second_lower_width = second_width / 2U;
    constexpr auto second_upper_width = second_width - second_lower_width;
    constexpr auto interleaved_width = first_lower_width;
    constexpr auto kind = ValueKind::logic4;
    const auto initial_first_owner0
        = value_for(first_lower_width, kind, 1U);
    const auto initial_first_owner1
        = value_for(first_upper_width, kind, 2U);
    const auto initial_second_owner0
        = value_for(second_lower_width, kind, 3U);
    const auto initial_second_owner1
        = value_for(second_upper_width, kind, 4U);
    const auto next_first_owner0
        = value_for(first_lower_width, kind, 20U);
    const auto next_first_owner1
        = value_for(first_upper_width, kind, 24U);
    const auto next_second_owner0
        = value_for(second_lower_width, kind, 30U);
    const auto next_second_owner1
        = value_for(second_upper_width, kind, 34U);
    auto expected_first = all_z_value(first_width, kind);
    insert_range(expected_first, next_first_owner0, 0U, kind);
    insert_range(expected_first, next_first_owner1,
        first_lower_width, kind);
    auto expected_second = all_z_value(second_width, kind);
    insert_range(expected_second, next_second_owner0, 0U, kind);
    insert_range(expected_second, next_second_owner1,
        second_lower_width, kind);
    const auto initial_first_target = all_z_value(first_width, kind);
    const auto initial_second_target = all_z_value(second_width, kind);

    Interpreter interpreter;
    const auto first_owner0_input = interpreter.add_signal({
        "wide_owner_groups.first_owner0", initial_first_owner0,
        ResolutionKind::none, kind });
    const auto first_owner1_input = interpreter.add_signal({
        "wide_owner_groups.first_owner1", initial_first_owner1,
        ResolutionKind::none, kind });
    const auto target0 = interpreter.add_signal({
        "wide_owner_groups.target0", initial_first_target,
        ResolutionKind::sv_wire, kind });
    const auto interleaved = interpreter.add_signal({
        "wide_owner_groups.interleaved",
        all_z_value(interleaved_width, kind), ResolutionKind::sv_wire, kind });
    const auto target1 = interpreter.add_signal({
        "wide_owner_groups.target1", initial_second_target,
        ResolutionKind::sv_wire, kind });
    const auto second_owner0_input = interpreter.add_signal({
        "wide_owner_groups.second_owner0", initial_second_owner0,
        ResolutionKind::none, kind });
    const auto second_owner1_input = interpreter.add_signal({
        "wide_owner_groups.second_owner1", initial_second_owner1,
        ResolutionKind::none, kind });

    require(interpreter.add_process(vhdl_projected_disjoint_multi_writer(
                0U, first_owner0_input, target0, 0U,
                first_lower_width, second_owner0_input, target1, 0U,
                second_lower_width, kind)) == 0U,
        "lower owner stages both target groups from one input wake");
    require(interpreter.add_process(vhdl_projected_disjoint_multi_writer(
                1U, first_owner1_input, target0,
                first_lower_width, first_upper_width, second_owner1_input,
                target1, second_lower_width, second_upper_width, kind)) == 1U,
        "upper owner stages both target groups from one input wake");
    auto interleaved_writer = vhdl_projected_slice_writer(
        2U, first_owner0_input, interleaved, 0U, interleaved_width, kind);
    require(interpreter.add_process(std::move(interleaved_writer)) == 2U,
        "interleaved checked signal joins the owners' projected Update");

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "multi-group fixture reaches a startup quiet point");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    DisjointOwnerMultiGroupResult result;
    const auto capture_bound = [&](const SignalId signal) {
        return OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                   interpreter, signal, 0U)
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, signal, 1U);
    };
    const auto interleaved_slot_bound = [&] {
        const auto* const state
            = implementation.region_authoritative_state_for_signal(
                interleaved);
        if (state == nullptr
            || !state->values().layout().contains(interleaved)) {
            return false;
        }
        const auto owners = state->values().layout().owners(interleaved);
        if (std::ranges::none_of(owners,
                [](const SignalDriverOwnerLayout& owner) {
                    return owner.process == 2U;
                })) {
            return false;
        }
        return OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, interleaved, 2U);
    };
    result.first.slots_bound_before = capture_bound(target0);
    result.interleaved.slots_bound_before = interleaved_slot_bound();
    result.second.slots_bound_before = capture_bound(target1);
    const auto* const first_state
        = implementation.region_authoritative_state_for_signal(target0);
    const auto* const second_state
        = implementation.region_authoritative_state_for_signal(target1);
    const auto first_component
        = implementation.region_authoritative_component_by_signal.at(target0);
    const auto second_component
        = implementation.region_authoritative_component_by_signal.at(target1);
    result.signal_ids_interleaved
        = target0 < interleaved && interleaved < target1;
    result.same_authoritative_component
        = first_component != std::numeric_limits<std::size_t>::max()
        && first_component == second_component;
    result.projected_owner_sources
        = implementation.region_graph.has_value()
        && implementation.region_graph->processes().size() > 1U
        && implementation.region_graph->processes()[0U].update_kind
            == RegionUpdateKind::vhdl_projected
        && implementation.region_graph->processes()[1U].update_kind
            == RegionUpdateKind::vhdl_projected;
    const auto first_generation = first_state == nullptr
        ? 0U : first_state->values().revision();
    const auto second_generation = second_state == nullptr
        ? 0U : second_state->values().revision();
    const std::array<SignalId, 3U> targets {
        target0, interleaved, target1
    };
    const std::array<std::uint64_t, 3U> revisions {
        implementation.signal_value_revisions.at(target0),
        implementation.signal_value_revisions.at(interleaved),
        implementation.signal_value_revisions.at(target1),
    };
    const std::array<std::optional<std::pair<SimulationTick, std::uint64_t>>,
        3U> events {
        implementation.signal_events.at(target0),
        implementation.signal_events.at(interleaved),
        implementation.signal_events.at(target1),
    };
    const std::array<std::optional<std::pair<SimulationTick, std::uint64_t>>,
        3U> transactions {
        implementation.signal_transactions.at(target0),
        implementation.signal_transactions.at(interleaved),
        implementation.signal_transactions.at(target1),
    };

    interpreter.deposit_signal(first_owner0_input, next_first_owner0);
    interpreter.deposit_signal(first_owner1_input, next_first_owner1);
    interpreter.deposit_signal(second_owner0_input, next_second_owner0);
    interpreter.deposit_signal(second_owner1_input, next_second_owner1);
    result.completed = interpreter.run().status == RunStatus::completed;
    if (!result.completed) {
        return result;
    }

    std::array<DisjointOwnerMultiGroupSignalResult*, 3U> outputs {
        &result.first, &result.interleaved, &result.second
    };
    for (std::size_t index = 0U; index < targets.size(); ++index) {
        const auto signal = targets[index];
        auto& output = *outputs[index];
        output.current = implementation.get_signal(signal).initial_value;
        output.previous = implementation.signal_last_values.at(signal);
        output.stored = implementation.driven_values.at(signal);
        if (signal == interleaved) {
            const auto* const owner
                = implementation.driver_values.at(signal).find(2U);
            require(owner != nullptr,
                "interleaved checked row keeps its original driver identity");
            output.owner0 = owner->value;
        } else {
            const auto* const owner0
                = implementation.driver_values.at(signal).find(0U);
            const auto* const owner1
                = implementation.driver_values.at(signal).find(1U);
            require(owner0 != nullptr && owner1 != nullptr,
                "each Update group keeps both original driver identities");
            output.owner0 = owner0->value;
            output.owner1 = owner1->value;
        }
        output.revision_delta
            = implementation.signal_value_revisions.at(signal)
            - revisions[index];
        output.event_delta
            = implementation.signal_events.at(signal) != events[index]
            ? 1U : 0U;
        output.transaction_delta
            = implementation.signal_transactions.at(signal)
                    != transactions[index]
            ? 1U : 0U;
        output.transaction_stamp
            = implementation.signal_transactions.at(signal);
        output.slots_bound_after = signal == interleaved
            ? interleaved_slot_bound() : capture_bound(signal);
        if (signal != interleaved && output.slots_bound_after) {
            const auto roles0
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    interpreter, signal, 0U);
            const auto roles1
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    interpreter, signal, 1U);
            const auto owner0 = signal == target0
                ? [&] {
                    auto value = all_z_value(first_width, kind);
                    insert_range(value, next_first_owner0, 0U, kind);
                    return value;
                }()
                : [&] {
                    auto value = all_z_value(second_width, kind);
                    insert_range(value, next_second_owner0, 0U, kind);
                    return value;
                }();
            const auto owner1 = signal == target0
                ? [&] {
                    auto value = all_z_value(first_width, kind);
                    insert_range(value, next_first_owner1,
                        first_lower_width, kind);
                    return value;
                }()
                : [&] {
                    auto value = all_z_value(second_width, kind);
                    insert_range(value, next_second_owner1,
                        second_lower_width, kind);
                    return value;
                }();
            const auto& expected = signal == target0
                ? expected_first : expected_second;
            const auto& previous = signal == target0
                ? initial_first_target : initial_second_target;
            output.roles_match
                = roles0[0U] == expected && roles0[1U] == previous
                && roles0[2U] == expected && roles0[3U] == owner0
                && roles1[0U] == expected && roles1[1U] == previous
                && roles1[2U] == expected && roles1[3U] == owner1;
        }
    }
    const auto* const first_state_after
        = implementation.region_authoritative_state_for_signal(target0);
    const auto* const second_state_after
        = implementation.region_authoritative_state_for_signal(target1);
    result.first.a4_generation_delta = first_state_after == nullptr
        ? 0U : first_state_after->values().revision() - first_generation;
    result.second.a4_generation_delta = second_state_after == nullptr
        ? 0U : second_state_after->values().revision() - second_generation;
    return result;
}

void exercise_multiple_disjoint_owner_groups_in_one_update()
{
    const auto checked = run_disjoint_owner_multi_group_round("0");
    const auto grouped = run_disjoint_owner_multi_group_round("1");
    const auto outputs_match = [](const auto& left, const auto& right) {
        return left.current == right.current
            && left.previous == right.previous
            && left.stored == right.stored
            && left.owner0 == right.owner0
            && left.owner1 == right.owner1
            && left.revision_delta == right.revision_delta
            && left.event_delta == right.event_delta
            && left.transaction_delta == right.transaction_delta
            && left.transaction_stamp == right.transaction_stamp;
    };
    require(checked.completed && grouped.completed
            && grouped.same_authoritative_component
            && checked.signal_ids_interleaved
            && grouped.signal_ids_interleaved
            && grouped.projected_owner_sources,
        "projected owners share one component with interleaved signal IDs");
    require(grouped.first.slots_bound_before
            && grouped.second.slots_bound_before
            && grouped.first.slots_bound_after
            && grouped.second.slots_bound_after
            && grouped.first.roles_match && grouped.second.roles_match
            && grouped.first.a4_generation_delta == 2U
            && grouped.second.a4_generation_delta
                == grouped.first.a4_generation_delta,
        "both independent Logic4 owner groups publish through retained A4 planes");
    require(!checked.first.slots_bound_before
            && !checked.interleaved.slots_bound_before
            && !checked.second.slots_bound_before
            && !checked.first.slots_bound_after
            && !checked.interleaved.slots_bound_after
            && !checked.second.slots_bound_after
            && !grouped.interleaved.slots_bound_before
            && !grouped.interleaved.slots_bound_after
            && outputs_match(grouped.first, checked.first)
            && outputs_match(grouped.interleaved, checked.interleaved)
            && outputs_match(grouped.second, checked.second),
        "interleaved SignalId commits match the checked Update route exactly");
    const auto expected_interleaved
        = value_for(DisjointOwnerGroupPlaneFixture::lower_width,
            ValueKind::logic4, 20U);
    require(grouped.interleaved.current
                == expected_interleaved
            && grouped.interleaved.previous
                == all_z_value(DisjointOwnerGroupPlaneFixture::lower_width,
                    ValueKind::logic4)
            && grouped.interleaved.stored
                == expected_interleaved
            && grouped.interleaved.owner0
                == expected_interleaved
            && grouped.first.current.width() == 65U
            && grouped.second.current.width() == 129U
            && grouped.interleaved.revision_delta == 1U
            && grouped.interleaved.event_delta == 1U
            && grouped.interleaved.transaction_delta == 1U
            && grouped.first.revision_delta == 1U
            && grouped.first.event_delta == 1U
            && grouped.second.revision_delta == 1U
            && grouped.second.event_delta == 1U
            && grouped.first.transaction_stamp
            && grouped.interleaved.transaction_stamp
            && grouped.second.transaction_stamp
            && grouped.first.transaction_stamp
                == grouped.interleaved.transaction_stamp
            && grouped.second.transaction_stamp
                == grouped.interleaved.transaction_stamp,
        "ordered group commits preserve the interleaved checked event rows");
}

void exercise_disjoint_owner_runtime_routes()
{
    for (const auto width : { 2U, 64U, 65U, 129U }) {
        for (const auto kind : { ValueKind::logic4, ValueKind::logic9 }) {
            const auto checked = run_disjoint_owner_round(
                width, kind, "0", false, std::nullopt, false, "0");
            // Both policy variables are unset: this is the ordinary default
            // for any fully certified disjoint owner partition.
            const auto disjoint = run_disjoint_owner_round(
                width, kind, nullptr, false, std::nullopt, false, nullptr);
            const auto explicit_disjoint = run_disjoint_owner_round(
                width, kind, "1", false, std::nullopt, false, nullptr);
            const auto explicit_disjoint_off = run_disjoint_owner_round(
                width, kind, "0", false, std::nullopt, false, nullptr);
            const auto single_owner_off_unset_disjoint
                = run_disjoint_owner_round(
                    width, kind, nullptr, false, std::nullopt, false, "0");
            const auto single_owner_off_enabled_disjoint
                = run_disjoint_owner_round(
                    width, kind, "1", false, std::nullopt, false, "0");
            const auto reverse_touch = run_disjoint_owner_round(
                width, kind, nullptr, true, std::nullopt, false, nullptr);

            require(checked.completed && !checked.threw
                    && !checked.bound_before && !checked.bound_after
                    && checked.graph_kept_partial_target_as_boundary
                    && checked.local_wave_disabled
                    && checked.component_structural_candidate
                    && checked.executor_access_complete
                    && checked.executors_remain_checked
                    && !checked.disjoint_gate_admitted_owner0
                    && !checked.disjoint_gate_admitted_owner1,
                "explicitly disabled policies remain ordinary checked execution");
            const auto admitted_disjoint_matches = [&](const auto& result) {
                const bool expected_middle_authoritative
                    = result.mixed_component_middle_is_structural_candidate
                    || width > 64U;
                return result.completed && !result.threw
                    && result.bound_before && result.bound_after
                    && result.versioned_storage_ready
                    && result.graph_kept_partial_target_as_boundary
                    && result.component_structural_candidate
                    && result.executor_access_complete
                    && result.executors_remain_checked
                    && result.mixed_component_contains_checked_writer
                    && (kind == ValueKind::logic9
                        || result.mixed_component_middle_is_structural_candidate)
                    && result.legacy_owned_composite_inactive
                    && (result.unrelated_internal_signal_uses_authoritative_route
                        == expected_middle_authoritative)
                    && (result.unrelated_internal_signal_uses_checked_route
                        == !expected_middle_authoritative)
                    && result.unrelated_write_keeps_disjoint_slots_bound
                    && result.local_wave_disabled
                    && result.partial_target_not_activation_internal
                    && result.disjoint_gate_admitted_owner0
                    && result.disjoint_gate_admitted_owner1
                    && result.published_sidecar_roles_match;
            };
            require(admitted_disjoint_matches(disjoint),
                "unset policies bind both disjoint original owner slots by default");
            require(admitted_disjoint_matches(explicit_disjoint),
                "explicit disjoint enable preserves the diagnostic route");
            // Component counters include internal publications admitted by the
            // activation program. Target revision and all four role planes are
            // checked separately below.
            require(disjoint.sidecar_generation_delta > 0U
                    && disjoint.owner_mirror_delta >= 2U,
                "disjoint A4 advances component state and mirrors both target owners");
            require(disjoint.revision_delta == 2U
                    && disjoint.target_event_changed
                    && disjoint.target_transaction_changed,
                "disjoint A4 preserves both ordered current publications and metadata");
            const auto same_owner_values = [](const auto& left,
                                               const auto& right) {
                return left.current == right.current
                    && left.previous == right.previous
                    && left.previous == left.expected_previous
                    && right.previous == right.expected_previous
                    && left.stored == right.stored
                    && left.owner0 == right.owner0
                    && left.owner1 == right.owner1
                    && left.current == left.expected_current;
            };
            require(same_owner_values(disjoint, checked)
                    && same_owner_values(explicit_disjoint, disjoint),
                "default and explicit disjoint publications match checked phases");
            require(reverse_touch.completed
                    && reverse_touch.bound_before
                    && reverse_touch.bound_after
                    && reverse_touch.owner_mirror_delta >= 2U
                    && reverse_touch.sidecar_generation_delta > 0U
                    && reverse_touch.current == disjoint.current
                    && reverse_touch.previous == reverse_touch.expected_previous
                    && reverse_touch.stored == disjoint.stored
                    && reverse_touch.owner0 == disjoint.owner0
                    && reverse_touch.owner1 == disjoint.owner1
                    && reverse_touch.revision_delta == 2U
                    && reverse_touch.target_event_changed
                    && reverse_touch.target_transaction_changed
                    && reverse_touch.published_sidecar_roles_match
                    && disjoint.resume_order == checked.resume_order
                    && reverse_touch.resume_order == disjoint.resume_order,
                "reversing input touch order preserves stable owner order and phases");
            require(disjoint.late_observation_demoted_slots
                    && disjoint.late_observation_coherent
                    && disjoint.retry_completed,
                "late current observation demotes both slots before checked retry");
            require(single_owner_off_unset_disjoint.completed
                    && !single_owner_off_unset_disjoint.threw
                    && single_owner_off_unset_disjoint.bound_before
                    && single_owner_off_unset_disjoint.bound_after
                    && single_owner_off_unset_disjoint.versioned_storage_ready
                    && single_owner_off_unset_disjoint.unrelated_internal_signal_uses_checked_route
                    && !single_owner_off_unset_disjoint.unrelated_internal_signal_uses_authoritative_route
                    && single_owner_off_unset_disjoint.disjoint_gate_admitted_owner0
                    && single_owner_off_unset_disjoint.disjoint_gate_admitted_owner1
                    && single_owner_off_unset_disjoint.local_wave_disabled
                    && single_owner_off_unset_disjoint.partial_target_not_activation_internal
                    && same_owner_values(
                        single_owner_off_unset_disjoint, disjoint)
                    && single_owner_off_unset_disjoint.published_sidecar_roles_match
                    && single_owner_off_unset_disjoint.late_observation_demoted_slots
                    && single_owner_off_unset_disjoint.late_observation_coherent
                    && single_owner_off_unset_disjoint.retry_completed,
                "single-owner off does not disable default disjoint admission");
            require(single_owner_off_enabled_disjoint.completed
                    && !single_owner_off_enabled_disjoint.threw
                    && single_owner_off_enabled_disjoint.bound_before
                    && single_owner_off_enabled_disjoint.bound_after
                    && single_owner_off_enabled_disjoint.versioned_storage_ready
                    && single_owner_off_enabled_disjoint.unrelated_internal_signal_uses_checked_route
                    && !single_owner_off_enabled_disjoint.unrelated_internal_signal_uses_authoritative_route
                    && single_owner_off_enabled_disjoint.disjoint_gate_admitted_owner0
                    && single_owner_off_enabled_disjoint.disjoint_gate_admitted_owner1
                    && single_owner_off_enabled_disjoint.local_wave_disabled
                    && single_owner_off_enabled_disjoint.partial_target_not_activation_internal
                    && same_owner_values(
                        single_owner_off_enabled_disjoint, disjoint)
                    && single_owner_off_enabled_disjoint.published_sidecar_roles_match
                    && single_owner_off_enabled_disjoint.late_observation_demoted_slots
                    && single_owner_off_enabled_disjoint.late_observation_coherent
                    && single_owner_off_enabled_disjoint.retry_completed,
                "single-owner off preserves explicitly enabled disjoint owners");

            require(explicit_disjoint_off.completed
                    && !explicit_disjoint_off.threw
                    && !explicit_disjoint_off.bound_before
                    && !explicit_disjoint_off.bound_after
                    && !explicit_disjoint_off.versioned_storage_ready
                    && !explicit_disjoint_off.disjoint_gate_admitted_owner0
                    && !explicit_disjoint_off.disjoint_gate_admitted_owner1
                    && same_owner_values(explicit_disjoint_off, checked),
                "explicit disjoint off declines the default route");
        }
    }

    const auto generic_checked = run_disjoint_owner_round(
        65U, ValueKind::logic4, "0", false, std::nullopt,
        false, "0", true);
    const auto generic_grouped = run_disjoint_owner_round(
        65U, ValueKind::logic4, "1", false, std::nullopt,
        false, "0", true);
    const auto same_owner_values = [](const auto& left,
                                      const auto& right) {
        return left.current == right.current
            && left.previous == right.previous
            && left.stored == right.stored
            && left.owner0 == right.owner0
            && left.owner1 == right.owner1
            && left.resume_order == right.resume_order;
    };
    require(generic_checked.completed && generic_grouped.completed
            && generic_grouped.bound_before && generic_grouped.bound_after
            && generic_grouped.generic_owner_component_precondition
            && generic_grouped.disjoint_gate_admitted_owner0
            && generic_grouped.disjoint_gate_admitted_owner1
            && generic_grouped.published_sidecar_roles_match
            && generic_grouped.owner_mirror_delta == 0U
            && generic_grouped.sidecar_generation_delta == 1U
            && same_owner_values(generic_grouped, generic_checked),
        "Logic4 disjoint owners stage and publish together in one generic Update");
    bool found_group_preflight_failure { };
    std::optional<std::size_t> group_preflight_failure_index;
    for (std::size_t failure_index = 0U; failure_index < 64U;
         ++failure_index) {
        const auto failed = run_disjoint_owner_round(65U,
            ValueKind::logic4, "1", false, failure_index,
            false, "0", true);
        if (!failed.completed || failed.threw || !failed.allocation_injected) {
            continue;
        }
        require(!failed.bound_after
                && same_owner_values(failed, generic_checked)
                && failed.revision_delta == generic_checked.revision_delta
                && failed.target_event_changed
                && failed.target_transaction_changed
                && failed.retry_completed,
            "failed group COW demotes before the exact checked Update fallback");
        found_group_preflight_failure = true;
        group_preflight_failure_index = failure_index;
        break;
    }
    require(found_group_preflight_failure,
        "pinned Logic4 owner-group preflight exercises its allocation failure path");
    {
        bool found_published_fallback_failure { };
        for (std::size_t fallback_failure_after = 0U;
             fallback_failure_after < 64U; ++fallback_failure_after) {
            const auto failed = run_disjoint_owner_round(65U,
                ValueKind::logic4, "1", false, group_preflight_failure_index,
                false, "0", true, fallback_failure_after);
            if (!failed.fallback_preflight_failure_observed) {
                continue;
            }
            if (!failed.fallback_secondary_failure_observed) {
                break;
            }
            require(!failed.fallback_failure_left_owner_resolution_gap,
                "fallback failure does not leave both new owner rows with old signal roles");
            if (!failed.fallback_secondary_failure_full_at_injection) {
                continue;
            }
            require(failed.fallback_failure_reached_after_publication
                    && failed.retry_completed
                    && failed.retry_target_revision_delta == 1U,
                "post-publication fallback cut drains cleanly before the next two-owner Update");
            found_published_fallback_failure = true;
            break;
        }
        require(found_published_fallback_failure,
            "bounded allocation scan reaches a secondary failure after full grouped publication");
    }

    const auto forced = run_disjoint_owner_round(
        65U, ValueKind::logic4, nullptr, false, std::nullopt, true, nullptr);
    require(forced.force_release_demoted_slots
            && !forced.bound_before
            && !forced.disjoint_gate_admitted_owner0
            && !forced.disjoint_gate_admitted_owner1,
        "force and release revoke disjoint authority before later writes");

    const auto checked = run_disjoint_owner_round(
        129U, ValueKind::logic4, "0", false, std::nullopt, false, "0");
    bool found_scheduled_preflight_failure { };
    for (std::size_t failure_index = 0U; failure_index < 64U;
         ++failure_index) {
        const auto failed = run_disjoint_owner_round(129U,
            ValueKind::logic4, "1", false, failure_index);
        if (!failed.completed || failed.threw || !failed.allocation_injected) {
            continue;
        }
        require(failed.current == checked.current
                && failed.previous == checked.previous
                && failed.stored == checked.stored
                && failed.owner0 == checked.owner0
                && failed.owner1 == checked.owner1
                && failed.revision_delta == checked.revision_delta
                && failed.target_event_changed
                && failed.target_transaction_changed
                && failed.retry_completed,
            "wide disjoint clone failure accepts the checked raw/value prefix and retries");
        found_scheduled_preflight_failure = true;
        break;
    }
    require(found_scheduled_preflight_failure,
        "scheduled wide disjoint role-clone failure is caught before fallback");

    exercise_multiple_disjoint_owner_groups_in_one_update();
}

void exercise_narrow_single_owner_default_and_checked_policies()
{
    struct PolicyCase {
        const char* single_owner;
        bool enabled;
        bool versioned;
    };
    constexpr std::array policies {
        PolicyCase { nullptr, true, true },
        PolicyCase { "0", false, false },
    };
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment disjoint_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    // Keep local activation retention off to make this a storage-policy
    // check; explicit owner-policy off below still forces checked storage.
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_disabled {
        "FSIM_PROFILE_SV_WAVES", nullptr };

    for (const auto& policy : policies) {
        ScopedEnvironment single_owner_policy {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT",
            policy.single_owner };
        Interpreter interpreter;
        const auto input = interpreter.add_signal({
            "narrow_policy.input",
            PackedLogic4 { 1U, Logic4::zero },
            ResolutionKind::none,
            ValueKind::logic4,
        });
        const auto output = interpreter.add_signal({
            "narrow_policy.output",
            PackedLogic4 { 1U, Logic4::x },
            ResolutionKind::sv_wire,
            ValueKind::logic4,
        });
        auto writer = whole_writer(0U, output);
        writer.name = "narrow_policy.writer";
        writer.initialize = false;
        writer.static_sensitivity = { { input, EdgeKind::any } };
        require(interpreter.add_process(std::move(writer)) == 0U,
            "narrow policy process has its registered identity");
        const auto& registered = interpreter.process_program(0U);
        DisjointSliceExecutorState executor_state;
        interpreter.set_process_executor(0U,
            std::make_unique<ScheduledDisjointSliceExecutor>(
                0U, input, output, 0U, executor_state,
                ProcessExecutorProgramBinding {
                    registered, registered, 0U }));

        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "narrow policy process reaches a quiet point");
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto* const state
            = implementation.region_authoritative_state_for_signal(output);
        require(implementation.a4_wide_single_owner_commit_enabled
                    == policy.enabled
                && state != nullptr
                && state->values().packed_slots_bound()
                && state->values().requires_prewrite_unbind()
                    == policy.versioned,
            "default A2 admission versions narrow roles while explicit off "
            "retains checked narrow storage");
    }
}

void exercise_owner_only_publication_preserves_untouched_roles()
{
    constexpr auto width = std::uint32_t { 129U };
    constexpr auto lower_width = std::uint32_t { 64U };
    constexpr auto upper_width = width - lower_width;
    constexpr auto kind = ValueKind::logic4;
    std::vector<RegionSignalDescriptor> descriptors {
        RegionSignalDescriptor { lower_width },
        RegionSignalDescriptor { upper_width },
        RegionSignalDescriptor { width },
    };
    const std::vector<Process> processes {
        disjoint_slice_writer(0U, 0U, 2U, 0U, lower_width, kind),
        disjoint_slice_writer(1U, 1U, 2U, lower_width, upper_width, kind),
    };
    std::vector<const Process*> programs;
    programs.reserve(processes.size());
    for (const auto& process : processes) {
        programs.push_back(&process);
    }
    const auto graph = RegionGraph::build(programs, descriptors);
    const std::array<SignalId, 1U> signal_ids { 2U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids),
        PackedSlotBindingPolicy::experimental_wide_disjoint_owners
    };

    const auto initial_current = value_for(width, kind, 0U);
    const auto initial_previous = value_for(width, kind, 1U);
    const auto initial_stored = value_for(width, kind, 2U);
    auto initial_owner0 = all_z_value(width, kind);
    const auto initial_owner1 = all_z_value(width, kind);
    auto next_owner0 = initial_owner0;
    next_owner0.insert_bits(value_for(lower_width, kind, 5U), 0U);
    values.seed_signal(2U, initial_current, initial_previous, initial_stored);
    values.seed_owner(2U, 0U, initial_owner0);
    values.seed_owner(2U, 1U, initial_owner1);
    auto current = initial_current;
    auto previous = initial_previous;
    auto stored = initial_stored;
    auto owner0 = initial_owner0;
    auto owner1 = initial_owner1;
    values.stage_packed_signal_slots(2U, current, previous, stored);
    values.stage_packed_owner_slot(2U, 0U, owner0);
    values.stage_packed_owner_slot(2U, 1U, owner1);
    require(values.bind_packed_slots() == 5U,
        "disjoint owner fixture binds three visible and two raw roles");

    const std::array<PackedLogic4PlaneReadLease, 5U> retained {
        values.plane_read_lease(2U, PackedPlaneRole::current),
        values.plane_read_lease(2U, PackedPlaneRole::previous),
        values.plane_read_lease(2U, PackedPlaneRole::stored),
        values.plane_read_lease(2U, PackedPlaneRole::owner, 0U),
        values.plane_read_lease(2U, PackedPlaneRole::owner, 1U),
    };
    require(std::ranges::all_of(retained, [](const auto& lease) {
                return static_cast<bool>(lease);
            }),
        "owner-only mutation fixture pins every original packed role");
    const std::array<const std::uint64_t*, 3U> visible_plane_addresses {
        retained[0U].plane_words(0U).data(),
        retained[1U].plane_words(0U).data(),
        retained[2U].plane_words(0U).data(),
    };
    const auto generation_before = values.revision();

    AuthoritativeSignalPlanes::PreparedMutation mutation;
    mutation.words.reserve((width + 63U) / 64U);
    bool owner_clone_failed { };
    arm_allocation_failure(0U);
    try {
        values.prepare_owner_change_into(mutation, 2U, 0U, next_owner0,
            initial_current, initial_stored);
    } catch (const std::bad_alloc&) {
        owner_clone_failed = allocation_failure_was_injected();
    }
    clear_allocation_failure();
    require(owner_clone_failed
            && values.revision() == generation_before
            && values.current(2U) == initial_current
            && values.previous(2U) == initial_previous
            && values.stored(2U) == initial_stored
            && values.owner_value(2U, 0U) == initial_owner0
            && values.owner_value(2U, 1U) == initial_owner1,
        "failed owner-role clone preflight leaves every original role installed");

    values.prepare_owner_change_into(mutation, 2U, 0U, next_owner0,
        initial_current, initial_stored);
    require(!mutation.any_current_changed
            && !mutation.any_stored_changed
            && mutation.any_owner_changed && mutation.any_state_changed,
        "owner-only staging marks raw change without a value publication");
    require(values.begin_prepared_publication(mutation),
        "owner-only mutation preflights its pinned raw-owner role");
    values.publish(std::move(mutation));

    const std::array<PackedLogic4PlaneReadLease, 4U> visible_after {
        values.plane_read_lease(2U, PackedPlaneRole::current),
        values.plane_read_lease(2U, PackedPlaneRole::previous),
        values.plane_read_lease(2U, PackedPlaneRole::stored),
        values.plane_read_lease(2U, PackedPlaneRole::owner, 1U),
    };
    require(std::ranges::all_of(visible_after, [](const auto& lease) {
                return static_cast<bool>(lease);
            })
            && visible_after[0U].plane_words(0U).data()
                == visible_plane_addresses[0U]
            && visible_after[1U].plane_words(0U).data()
                == visible_plane_addresses[1U]
            && visible_after[2U].plane_words(0U).data()
                == visible_plane_addresses[2U],
        "raw-owner-only publication leaves current, LAST, and stored role blocks installed");
    require(values.current(2U) == initial_current
            && values.previous(2U) == initial_previous
            && values.stored(2U) == initial_stored
            && values.owner_value(2U, 0U) == next_owner0
            && values.owner_value(2U, 1U) == initial_owner1
            && current == initial_current && previous == initial_previous
            && stored == initial_stored && owner0 == next_owner0
            && owner1 == initial_owner1,
        "owner-only publication changes only the selected raw owner value");
    require(lease_matches(retained[0U], initial_current)
            && lease_matches(retained[1U], initial_previous)
            && lease_matches(retained[2U], initial_stored)
            && lease_matches(retained[3U], initial_owner0)
            && lease_matches(retained[4U], initial_owner1)
            && values.revision() == generation_before + 1U,
        "pinned role snapshots remain exact and raw mutation advances one generation");
}

void exercise_frontier_write_lease()
{
    constexpr auto width = std::uint32_t { 129U };
    constexpr auto input = SignalId { 0U };
    constexpr auto output = SignalId { 1U };
    constexpr auto owner = ProcessId { 0U };
    const auto graph = graph_for(width, ValueKind::logic4);
    const std::array<SignalId, 2U> component_signals { input, output };
    const std::array<SignalId, 0U> no_partial_certificates { };
    const std::array<SignalId, 1U> stored_owner_aliases { output };
    const auto layout = SignalDriverLayout::build(graph,
        component_signals, no_partial_certificates,
        stored_owner_aliases);
    AuthoritativeSignalPlanes values {
        layout, PackedSlotBindingPolicy::experimental_wide
    };

    const auto boundary_current = value_for(width, ValueKind::logic4, 1U);
    const auto boundary_previous = value_for(width, ValueKind::logic4, 2U);
    const auto boundary_stored = value_for(width, ValueKind::logic4, 3U);
    const auto old_current = value_for(width, ValueKind::logic4, 4U);
    const auto old_previous = value_for(width, ValueKind::logic4, 5U);
    const auto old_stored = value_for(width, ValueKind::logic4, 6U);
    const auto next_current = value_for(width, ValueKind::logic4, 7U);
    const auto next_stored = value_for(width, ValueKind::logic4, 8U);
    values.seed_signal(input, boundary_current, boundary_previous,
        boundary_stored);
    values.seed_signal(output, old_current, old_previous, old_stored);
    values.seed_owner(output, owner, old_stored);

    auto live_current = old_current;
    auto live_previous = old_previous;
    auto live_stored = old_stored;
    values.stage_packed_signal_slots(output, live_current, live_previous,
        live_stored);
    values.stage_packed_owner_stored_alias(output, owner);
    require(values.bind_packed_slots() == 3U
            && values.packed_signal_slots_bound(output)
            && values.packed_owner_slot_bound(output, owner),
        "frontier lease fixture binds one output and its stored-owner alias");

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable_signals { {
            AuthoritativeSignalPlanes::FrontierWriteBinding { output, owner }
        } };
    const auto generation = values.revision();
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    require(!values.try_acquire_frontier_write_lease(generation + 1U,
                writable_signals, lease)
            && !lease.active() && values.revision() == generation,
        "frontier lease rejects a stale expected generation without mutation");

    {
        const auto pinned_previous = values.plane_read_lease(
            output, PackedPlaneRole::previous);
        require(static_cast<bool>(pinned_previous),
            "frontier lease snapshot-pin fixture holds the previous block");
        require(!values.try_acquire_frontier_write_lease(generation,
                    writable_signals, lease)
                && !lease.active() && values.revision() == generation
                && lease_matches(pinned_previous, old_previous)
                && values.current(output) == old_current
                && values.previous(output) == old_previous
                && values.stored(output) == old_stored
                && values.owner_value(output, owner) == old_stored,
            "a later pinned role unwinds earlier lease locks "
            "without changing state");
    }

    require(values.try_acquire_frontier_write_lease(generation,
                writable_signals, lease)
            && lease.active(),
        "frontier lease retries after the snapshot pin is released");

    std::array<std::span<const std::uint64_t>, 4U> boundary_planes { };
    require(lease.current_plane_words(input, boundary_planes)
            && std::ranges::equal(boundary_planes[0U],
                boundary_current.aval_words())
            && std::ranges::equal(boundary_planes[1U],
                boundary_current.bval_words()),
        "lease exposes seeded boundary current only through a read-only view");

    std::array<std::span<std::uint64_t>, 4U> forbidden_boundary_write { };
    require(!lease.plane_words(input, PackedPlaneRole::current, owner,
                forbidden_boundary_write)
            && std::ranges::all_of(forbidden_boundary_write,
                [](const auto plane) { return plane.empty(); }),
        "lease never exposes a mutable CURRENT view for a boundary signal");

    std::array<std::span<std::uint64_t>, 4U> stored_planes { };
    std::array<std::span<std::uint64_t>, 4U> owner_planes { };
    std::array<std::span<std::uint64_t>, 4U> output_current_planes { };
    std::array<std::span<std::uint64_t>, 4U> output_previous_planes { };
    require(lease.plane_words(output, PackedPlaneRole::current, owner,
                output_current_planes)
            && lease.plane_words(output, PackedPlaneRole::previous, owner,
                output_previous_planes)
            && lease.plane_words(output, PackedPlaneRole::stored, owner,
                stored_planes)
            && lease.plane_words(output, PackedPlaneRole::owner, owner,
                owner_planes)
            && stored_planes[0U].data() == owner_planes[0U].data()
            && stored_planes[1U].data() == owner_planes[1U].data(),
        "stored-owner alias resolves to one leased writable backing");
    std::ranges::copy(old_current.aval_words(),
        output_current_planes[0U].begin());
    std::ranges::copy(old_current.bval_words(),
        output_current_planes[1U].begin());
    std::ranges::copy(old_previous.aval_words(),
        output_previous_planes[0U].begin());
    std::ranges::copy(old_previous.bval_words(),
        output_previous_planes[1U].begin());
    std::ranges::copy(old_stored.aval_words(), stored_planes[0U].begin());
    std::ranges::copy(old_stored.bval_words(), stored_planes[1U].begin());

    AuthoritativeSignalPlanes::FrontierWriteLease moved {
        std::move(lease)
    };
    AuthoritativeSignalPlanes::FrontierWriteLease reassigned;
    reassigned = std::move(moved);
    require(!lease.active() && !moved.active() && reassigned.active(),
        "lease moves transfer the exclusive role locks exactly once");
    reassigned.release();
    require(!reassigned.active() && values.revision() == generation
            && values.current(output) == old_current
            && values.previous(output) == old_previous
            && values.stored(output) == old_stored
            && values.owner_value(output, owner) == old_stored,
        "releasing an unchanged lease unlocks roles without advancing "
        "revision");

    AuthoritativeSignalPlanes::FrontierWriteLease changed;
    require(values.try_acquire_frontier_write_lease(generation,
                writable_signals, changed),
        "released lease locks can be acquired again");
    std::array<std::span<std::uint64_t>, 4U> current_planes { };
    std::array<std::span<std::uint64_t>, 4U> previous_planes { };
    require(changed.plane_words(output, PackedPlaneRole::current, owner,
                current_planes)
            && changed.plane_words(output, PackedPlaneRole::previous, owner,
                previous_planes)
            && changed.plane_words(output, PackedPlaneRole::stored, owner,
                stored_planes),
        "output lease returns the complete writable role set");
    std::ranges::copy(next_current.aval_words(), current_planes[0U].begin());
    std::ranges::copy(next_current.bval_words(), current_planes[1U].begin());
    std::ranges::copy(old_current.aval_words(), previous_planes[0U].begin());
    std::ranges::copy(old_current.bval_words(), previous_planes[1U].begin());
    std::ranges::copy(next_stored.aval_words(), stored_planes[0U].begin());
    std::ranges::copy(next_stored.bval_words(), stored_planes[1U].begin());
    changed.note_value_change(output);
    changed.note_value_change(output);
    require(values.revision() == generation,
        "frontier generation remains pinned until the changed lease releases");

    AuthoritativeSignalPlanes::FrontierWriteLease changed_owner;
    changed_owner = std::move(changed);
    changed_owner.release();
    require(values.revision() == generation + 1U
            && values.current(output) == next_current
            && values.previous(output) == old_current
            && values.stored(output) == next_stored
            && values.owner_value(output, owner) == next_stored,
        "changed lease release publishes one revision for aliased owner roles");

    AuthoritativeSignalPlanes::FrontierWriteLease final_probe;
    require(values.try_acquire_frontier_write_lease(values.revision(),
                writable_signals, final_probe)
            && final_probe.active(),
        "changed lease release leaves no role lock stranded");
}

void exercise_frontier_write_lease_narrow(const std::uint32_t width)
{
    constexpr auto input = SignalId { 0U };
    constexpr auto output = SignalId { 1U };
    constexpr auto owner = ProcessId { 0U };
    const auto graph = graph_for(width, ValueKind::logic4);
    const std::array<SignalId, 2U> component_signals { input, output };
    const auto layout = SignalDriverLayout::build(graph, component_signals);
    AuthoritativeSignalPlanes values { layout };

    const auto boundary = value_for(width, ValueKind::logic4, 1U);
    const auto old_current = value_for(width, ValueKind::logic4, 2U);
    const auto old_previous = value_for(width, ValueKind::logic4, 3U);
    const auto old_stored = value_for(width, ValueKind::logic4, 4U);
    const auto old_owner = value_for(width, ValueKind::logic4, 5U);
    const auto next_current = value_for(width, ValueKind::logic4, 6U);
    const auto next_stored = value_for(width, ValueKind::logic4, 7U);
    const auto next_owner = value_for(width, ValueKind::logic4, 8U);
    values.seed_signal(input, boundary, boundary, boundary);
    values.seed_signal(output, old_current, old_previous, old_stored);
    values.seed_owner(output, owner, old_owner);

    auto live_current = old_current;
    auto live_previous = old_previous;
    auto live_stored = old_stored;
    auto live_owner = old_owner;
    values.stage_packed_signal_slots(output, live_current, live_previous,
        live_stored);
    values.stage_packed_owner_slot(output, owner, live_owner);
    require(values.bind_packed_slots() == 4U
            && !values.requires_prewrite_unbind()
            && values.packed_signal_slots_bound(output)
            && values.packed_owner_slot_bound(output, owner),
        "default narrow A4 remains unversioned with all four roles bound");

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable_signals { {
            AuthoritativeSignalPlanes::FrontierWriteBinding { output, owner }
        } };
    const auto generation = values.revision();
    AuthoritativeSignalPlanes::FrontierWriteLease allocation_probe;
    begin_allocation_count();
    const auto acquired_without_allocation
        = values.try_acquire_frontier_write_lease(
            generation, writable_signals, allocation_probe);
    if (acquired_without_allocation) {
        allocation_probe.release();
    }
    const auto lease_allocations = end_allocation_count();
    require(acquired_without_allocation && lease_allocations == 0U
            && values.revision() == generation
            && !values.requires_prewrite_unbind(),
        "default width-one/64 planes acquire a zero-allocation lease "
        "without promotion");

    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    require(values.try_acquire_frontier_write_lease(
                generation, writable_signals, lease),
        "unversioned narrow planes admit the synchronous native lease");
    std::array<std::span<const std::uint64_t>, 4U> boundary_planes { };
    std::array<std::span<std::uint64_t>, 4U> forbidden_boundary_write { };
    std::array<std::span<std::uint64_t>, 4U> current_planes { };
    std::array<std::span<std::uint64_t>, 4U> previous_planes { };
    std::array<std::span<std::uint64_t>, 4U> stored_planes { };
    std::array<std::span<std::uint64_t>, 4U> owner_planes { };
    require(lease.current_plane_words(input, boundary_planes)
            && std::ranges::equal(boundary_planes[0U], boundary.aval_words())
            && !lease.plane_words(input, PackedPlaneRole::current, owner,
                forbidden_boundary_write)
            && std::ranges::all_of(forbidden_boundary_write,
                [](const auto plane) { return plane.empty(); })
            && lease.plane_words(output, PackedPlaneRole::current, owner,
                current_planes)
            && lease.plane_words(output, PackedPlaneRole::previous, owner,
                previous_planes)
            && lease.plane_words(output, PackedPlaneRole::stored, owner,
                stored_planes)
            && lease.plane_words(output, PackedPlaneRole::owner, owner,
                owner_planes),
        "unversioned lease exposes readonly boundary and certified output "
        "planes");
    std::ranges::copy(next_current.aval_words(), current_planes[0U].begin());
    std::ranges::copy(next_current.bval_words(), current_planes[1U].begin());
    std::ranges::copy(old_current.aval_words(), previous_planes[0U].begin());
    std::ranges::copy(old_current.bval_words(), previous_planes[1U].begin());
    std::ranges::copy(next_stored.aval_words(), stored_planes[0U].begin());
    std::ranges::copy(next_stored.bval_words(), stored_planes[1U].begin());
    std::ranges::copy(next_owner.aval_words(), owner_planes[0U].begin());
    std::ranges::copy(next_owner.bval_words(), owner_planes[1U].begin());
    lease.note_value_change(output);
    require(values.revision() == generation,
        "all narrow role changes hold the revision until lease release");
    lease.release();
    require(values.revision() == generation + 1U
            && values.current(output) == next_current
            && values.previous(output) == old_current
            && values.stored(output) == next_stored
            && values.owner_value(output, owner) == next_owner
            && !values.requires_prewrite_unbind(),
        "narrow lease commits four roles once and preserves "
        "unversioned policy");
}

void exercise_frontier_write_lease_separate_owner(
    const std::uint32_t width)
{
    constexpr auto output = SignalId { 1U };
    constexpr auto owner = ProcessId { 0U };
    const auto graph = graph_for(width, ValueKind::logic4);
    const std::array<SignalId, 1U> component_signals { output };
    const std::array<SignalId, 0U> no_partial_certificates { };
    const std::array<SignalId, 0U> no_stored_aliases { };
    const auto layout = SignalDriverLayout::build(graph,
        component_signals, no_partial_certificates, no_stored_aliases);
    AuthoritativeSignalPlanes values {
        layout, PackedSlotBindingPolicy::experimental_wide
    };

    const auto old_current = value_for(width, ValueKind::logic4, 1U);
    const auto old_previous = value_for(width, ValueKind::logic4, 2U);
    const auto old_stored = value_for(width, ValueKind::logic4, 3U);
    const auto old_owner = value_for(width, ValueKind::logic4, 4U);
    const auto next_owner = value_for(width, ValueKind::logic4, 5U);
    const auto highest_word = static_cast<std::size_t>(width / 64U);
    require(old_owner != next_owner,
        "separate owner fixture changes its raw owner value");
    require(old_owner.aval_words()[highest_word]
                != next_owner.aval_words()[highest_word]
            || old_owner.bval_words()[highest_word]
                != next_owner.bval_words()[highest_word],
        "separate owner fixture changes the highest packed word");
    values.seed_signal(output, old_current, old_previous, old_stored);
    values.seed_owner(output, owner, old_owner);
    auto live_current = old_current;
    auto live_previous = old_previous;
    auto live_stored = old_stored;
    auto live_owner = old_owner;
    values.stage_packed_signal_slots(output, live_current, live_previous,
        live_stored);
    values.stage_packed_owner_slot(output, owner, live_owner);
    require(values.bind_packed_slots() == 4U
            && values.packed_owner_slot_bound(output, owner),
        "wide separate-owner fixture binds a fourth physical A4 role");
    values.clear_dirty();
    constexpr auto output_index = std::size_t { 0U };
    require(values.dirty_signals()[output_index] == 0U,
        "owner-only lease starts with a clean signal state");

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable_signals { {
            AuthoritativeSignalPlanes::FrontierWriteBinding { output, owner }
        } };
    const auto generation = values.revision();
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    {
        const auto pinned_owner = values.plane_read_lease(
            output, PackedPlaneRole::owner, owner);
        require(static_cast<bool>(pinned_owner)
                && lease_matches(pinned_owner, old_owner),
            "separate owner snapshot pins the fourth role block");
        require(!values.try_acquire_frontier_write_lease(generation,
                    writable_signals, lease)
                && !lease.active() && values.revision() == generation
                && values.current(output) == old_current
                && values.previous(output) == old_previous
                && values.stored(output) == old_stored
                && values.owner_value(output, owner) == old_owner,
            "pinned owner declines after the first three locks and leaves "
            "roles intact");
    }
    require(values.try_acquire_frontier_write_lease(
                generation, writable_signals, lease),
        "separate owner acquisition retries after its pin is released");

    std::array<std::span<std::uint64_t>, 4U> stored_planes { };
    std::array<std::span<std::uint64_t>, 4U> owner_planes { };
    require(lease.plane_words(output, PackedPlaneRole::stored, owner,
                stored_planes)
            && lease.plane_words(output, PackedPlaneRole::owner, owner,
                owner_planes)
            && stored_planes[0U].size() == (width + 63U) / 64U
            && owner_planes[0U].size() == (width + 63U) / 64U
            && stored_planes[0U].data() != owner_planes[0U].data(),
        "unaliased owner lease returns its separate backing block");
    std::ranges::copy(next_owner.aval_words(), owner_planes[0U].begin());
    std::ranges::copy(next_owner.bval_words(), owner_planes[1U].begin());
    lease.note_value_change(output);
    lease.release();
    require(values.revision() == generation + 1U
            && values.current(output) == old_current
            && values.previous(output) == old_previous
            && values.stored(output) == old_stored
            && values.owner_value(output, owner) == next_owner
            && values.dirty_signals()[output_index] != 0U,
        "owner-only change advances one revision and preserves visible roles");
}

PackedLogic4 logic9_uniform(
    const std::uint32_t width, const Logic9 state)
{
    return PackedLogic4::from_logic9_msb_string(
        std::string(width, to_char(state)));
}

void copy_logic9_planes(const PackedLogic4& value,
    std::array<std::span<std::uint64_t>, 4U>& destination)
{
    require(value.is_logic9(), "Logic9 fixture requires four-plane values");
    const auto words = static_cast<std::size_t>(value.width() / 64U)
        + (value.width() % 64U == 0U ? 0U : 1U);
    for (std::size_t plane = 0U; plane < destination.size(); ++plane) {
        const auto source = plane < 2U
            ? (plane == 0U ? value.aval_words() : value.bval_words())
            : value.logic9_plane_words(plane);
        require(source.size() == words && destination[plane].size() == words,
            "Logic9 lease role exposes its complete plane range");
        std::ranges::copy(source, destination[plane].begin());
    }
}

bool contains_all_logic9_states(const PackedLogic4& value)
{
    if (!value.is_logic9()) {
        return false;
    }
    std::array<bool, 9U> seen { };
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        const auto state = static_cast<std::size_t>(value.get_logic9(bit));
        if (state >= seen.size()) {
            return false;
        }
        seen[state] = true;
    }
    return std::ranges::all_of(seen, [](const bool present) {
        return present;
    });
}

void exercise_frontier_write_lease_logic9(
    const std::uint32_t width, const bool aliases_stored)
{
    constexpr auto input = SignalId { 0U };
    constexpr auto output = SignalId { 2U };
    constexpr auto owner = ProcessId { 1U };
    require(!aliases_stored || width > 64U,
        "stored-owner alias is reserved for versioned wide layouts");

    const auto graph = graph_for(width, ValueKind::logic9);
    const std::array<SignalId, 3U> component_signals { 0U, 1U, 2U };
    const std::array<SignalId, 0U> no_partial_certificates { };
    const std::array<SignalId, 1U> alias_certificate { output };
    const auto layout = SignalDriverLayout::build(graph, component_signals,
        no_partial_certificates,
        aliases_stored
            ? std::span<const SignalId> { alias_certificate }
            : std::span<const SignalId> { });
    const auto policy = width > 64U
        ? PackedSlotBindingPolicy::experimental_wide
        : PackedSlotBindingPolicy::narrow_only;
    AuthoritativeSignalPlanes values { layout, policy };

    const auto boundary = value_for(width, ValueKind::logic9, 0U);
    const auto old_current = value_for(width, ValueKind::logic9, 1U);
    const auto old_previous = value_for(width, ValueKind::logic9, 2U);
    const auto old_stored = value_for(width, ValueKind::logic9, 3U);
    const auto old_owner = aliases_stored
        ? old_stored : value_for(width, ValueKind::logic9, 4U);
    const auto next_current = value_for(width, ValueKind::logic9, 5U);
    const auto next_previous = old_current;
    const auto next_stored = value_for(width, ValueKind::logic9, 6U);
    const auto next_owner = aliases_stored
        ? next_stored : value_for(width, ValueKind::logic9, 7U);
    if (width >= 9U) {
        require(contains_all_logic9_states(old_current)
                && contains_all_logic9_states(next_current)
                && contains_all_logic9_states(next_stored)
                && contains_all_logic9_states(next_owner),
            "wide lease fixtures cover all nine canonical Logic9 states");
    }

    values.seed_signal(input, boundary, boundary, boundary);
    values.seed_signal(1U, old_current, old_previous, old_stored);
    values.seed_owner(1U, 0U, old_stored);
    values.seed_signal(output, old_current, old_previous, old_stored);
    values.seed_owner(output, owner, old_owner);
    auto live_current = old_current;
    auto live_previous = old_previous;
    auto live_stored = old_stored;
    auto live_owner = old_owner;
    values.stage_packed_signal_slots(output, live_current, live_previous,
        live_stored);
    if (aliases_stored) {
        values.stage_packed_owner_stored_alias(output, owner);
    } else {
        values.stage_packed_owner_slot(output, owner, live_owner);
    }
    require(values.bind_packed_slots()
                == (aliases_stored ? 3U : 4U)
            && values.packed_signal_slots_bound(output)
            && values.packed_owner_slot_bound(output, owner)
            && values.requires_prewrite_unbind() == (width > 64U),
        "Logic9 lease fixture binds the exact owner-role topology");

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable_signals { {
            AuthoritativeSignalPlanes::FrontierWriteBinding { output, owner }
        } };
    const auto generation = values.revision();
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    require(!values.try_acquire_frontier_write_lease(generation + 1U,
                writable_signals, lease)
            && !lease.active() && values.revision() == generation
            && values.valid(),
        "Logic9 lease rejects stale generation without touching any role");

    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 2U>
        duplicate_bindings { writable_signals[0U], writable_signals[0U] };
    require(!values.try_acquire_frontier_write_lease(generation,
                duplicate_bindings, lease)
            && !lease.active() && values.revision() == generation,
        "Logic9 lease rejects duplicate writable signals before locking");

    if (width > 64U) {
        {
            const auto retained_current
                = values.plane_read_lease(output, PackedPlaneRole::current);
            const auto retained_previous
                = values.plane_read_lease(output, PackedPlaneRole::previous);
            const auto retained_stored
                = values.plane_read_lease(output, PackedPlaneRole::stored);
            const auto retained_owner
                = values.plane_read_lease(output, PackedPlaneRole::owner,
                    owner);
            require(lease_matches(retained_current, old_current)
                    && lease_matches(retained_previous, old_previous)
                    && lease_matches(retained_stored, old_stored)
                    && lease_matches(retained_owner, old_owner)
                    && !values.try_acquire_frontier_write_lease(generation,
                        writable_signals, lease)
                    && !lease.active() && values.revision() == generation
                    && lease_matches(retained_current, old_current)
                    && lease_matches(retained_owner, old_owner),
                "retained four-plane Logic9 snapshots decline without mutation");
        }

        if (!aliases_stored) {
            const auto retained_owner_only = values.plane_read_lease(
                output, PackedPlaneRole::owner, owner);
            require(static_cast<bool>(retained_owner_only)
                    && !values.try_acquire_frontier_write_lease(generation,
                        writable_signals, lease)
                    && !lease.active()
                    && values.current(output) == old_current
                    && values.previous(output) == old_previous
                    && values.stored(output) == old_stored
                    && values.owner_value(output, owner) == old_owner,
                "a pinned separate Logic9 owner unwinds earlier role locks");
        }
    }

    begin_allocation_count();
    const auto allocated = values.try_acquire_frontier_write_lease(
        generation, writable_signals, lease);
    if (allocated) {
        lease.release();
    }
    const auto allocation_count = end_allocation_count();
    require(allocated && allocation_count == 0U
            && values.revision() == generation,
        "Logic9 lease acquire/release is allocation-free after binding");

    require(values.try_acquire_frontier_write_lease(
                generation, writable_signals, lease)
            && lease.active(),
        "Logic9 lease retries after stale, duplicate, and pinned declines");
    AuthoritativeSignalPlanes::FrontierWriteLease busy;
    require(!values.try_acquire_frontier_write_lease(generation,
                writable_signals, busy)
            && !busy.active() && lease.active(),
        "Logic9 state permits only one active exclusive lease");

    std::array<std::span<const std::uint64_t>, 4U> input_planes { };
    require(lease.current_plane_words(input, input_planes)
            && input_planes[0U].size() == (width + 63U) / 64U
            && input_planes[1U].size() == (width + 63U) / 64U
            && input_planes[2U].size() == (width + 63U) / 64U
            && input_planes[3U].size() == (width + 63U) / 64U
            && std::ranges::equal(input_planes[0U], boundary.aval_words())
            && std::ranges::equal(input_planes[1U], boundary.bval_words())
            && std::ranges::equal(input_planes[2U],
                boundary.logic9_plane_words(2U))
            && std::ranges::equal(input_planes[3U],
                boundary.logic9_plane_words(3U)),
        "Logic9 input view exposes all four read-only state planes");

    std::array<std::span<std::uint64_t>, 4U> current_planes { };
    std::array<std::span<std::uint64_t>, 4U> previous_planes { };
    std::array<std::span<std::uint64_t>, 4U> stored_planes { };
    std::array<std::span<std::uint64_t>, 4U> owner_planes { };
    require(lease.plane_words(output, PackedPlaneRole::current, owner,
                current_planes)
            && lease.plane_words(output, PackedPlaneRole::previous, owner,
                previous_planes)
            && lease.plane_words(output, PackedPlaneRole::stored, owner,
                stored_planes)
            && lease.plane_words(output, PackedPlaneRole::owner, owner,
                owner_planes)
            && current_planes[2U].size() == (width + 63U) / 64U
            && current_planes[3U].size() == (width + 63U) / 64U
            && previous_planes[2U].size() == (width + 63U) / 64U
            && stored_planes[3U].size() == (width + 63U) / 64U,
        "Logic9 writes expose the complete four-plane role set");
    if (aliases_stored) {
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            require(stored_planes[plane].data() == owner_planes[plane].data(),
                "Logic9 stored-owner alias shares all four writable planes");
        }
    } else {
        require(stored_planes[2U].data() != owner_planes[2U].data()
                && stored_planes[3U].data() != owner_planes[3U].data(),
            "Logic9 nonalias owner uses its separate upper plane pair");
    }

    copy_logic9_planes(next_current, current_planes);
    copy_logic9_planes(next_previous, previous_planes);
    copy_logic9_planes(next_stored, stored_planes);
    if (!aliases_stored) {
        copy_logic9_planes(next_owner, owner_planes);
    }
    lease.note_value_change(output);
    require(values.revision() == generation,
        "Logic9 revision stays fixed while its exclusive lease is active");
    lease.release();
    const auto expected_owner = aliases_stored ? next_stored : next_owner;
    require(!lease.active() && values.valid()
            && values.revision() == generation + 1U
            && values.current(output) == next_current
            && values.previous(output) == next_previous
            && values.stored(output) == next_stored
            && values.owner_value(output, owner) == expected_owner,
        "Logic9 lease publishes all roles with exactly one revision");
    require(live_current == next_current
            && live_previous == next_previous
            && live_stored == next_stored
            && (aliases_stored || live_owner == expected_owner),
        "post-write Logic9 bound roles retain the exact four-plane values");

    if (width > 64U) {
        const auto current_snapshot
            = values.plane_read_lease(output, PackedPlaneRole::current);
        const auto previous_snapshot
            = values.plane_read_lease(output, PackedPlaneRole::previous);
        const auto stored_snapshot
            = values.plane_read_lease(output, PackedPlaneRole::stored);
        const auto owner_snapshot
            = values.plane_read_lease(output, PackedPlaneRole::owner, owner);
        require(lease_matches(current_snapshot, next_current)
                && lease_matches(previous_snapshot, next_previous)
                && lease_matches(stored_snapshot, next_stored)
                && lease_matches(owner_snapshot, expected_owner),
            "post-write wide Logic9 snapshots retain the exact four-plane values");
    }

    if (width == 1U) {
        for (std::uint8_t code = 0U; code < 9U; ++code) {
            const auto current = logic9_uniform(width,
                static_cast<Logic9>(code));
            const auto previous = logic9_uniform(width,
                static_cast<Logic9>((code + 1U) % 9U));
            const auto stored = logic9_uniform(width,
                static_cast<Logic9>((code + 2U) % 9U));
            const auto raw_owner = aliases_stored ? stored
                : logic9_uniform(width,
                    static_cast<Logic9>((code + 3U) % 9U));
            const auto before = values.revision();
            require(values.try_acquire_frontier_write_lease(before,
                        writable_signals, lease),
                "narrow Logic9 lease reacquires for every defined state");
            require(lease.plane_words(output, PackedPlaneRole::current, owner,
                        current_planes)
                    && lease.plane_words(output, PackedPlaneRole::previous,
                        owner, previous_planes)
                    && lease.plane_words(output, PackedPlaneRole::stored,
                        owner, stored_planes)
                    && lease.plane_words(output, PackedPlaneRole::owner,
                        owner, owner_planes),
                "narrow Logic9 state loop retains all four role views");
            copy_logic9_planes(current, current_planes);
            copy_logic9_planes(previous, previous_planes);
            copy_logic9_planes(stored, stored_planes);
            if (!aliases_stored) {
                copy_logic9_planes(raw_owner, owner_planes);
            }
            lease.note_value_change(output);
            lease.release();
            require(values.valid()
                    && values.revision() == before + 1U
                    && values.current(output).get_logic9(0U)
                        == static_cast<Logic9>(code)
                    && values.previous(output).get_logic9(0U)
                        == static_cast<Logic9>((code + 1U) % 9U)
                    && values.stored(output).get_logic9(0U)
                        == static_cast<Logic9>((code + 2U) % 9U)
                    && values.owner_value(output, owner).get_logic9(0U)
                        == static_cast<Logic9>(aliases_stored
                            ? (code + 2U) % 9U : (code + 3U) % 9U),
                "narrow Logic9 leases preserve each canonical state code");
        }
    }
}

RegionGraph mixed_logic4_logic9_graph_for(const std::uint32_t width)
{
    std::array descriptors {
        RegionSignalDescriptor { width },
        RegionSignalDescriptor { width },
        RegionSignalDescriptor { width },
    };
    descriptors[1U].value_kind = ValueKind::logic9;
    descriptors[2U].value_kind = ValueKind::logic9;
    const auto first = whole_writer(0U, 0U);
    const auto second = whole_writer(1U, 1U);
    const auto output = whole_writer(2U, 2U);
    const std::array<const Process*, 3U> programs {
        &first, &second, &output
    };
    return RegionGraph::build(programs, descriptors);
}

void exercise_frontier_write_lease_mixed_logic4_logic9()
{
    constexpr auto width = std::uint32_t { 129U };
    constexpr auto output = SignalId { 2U };
    constexpr auto owner = ProcessId { 2U };
    const auto graph = mixed_logic4_logic9_graph_for(width);
    const std::array<SignalId, 3U> component_signals { 0U, 1U, 2U };
    const auto layout = SignalDriverLayout::build(graph, component_signals);
    const auto& output_layout = layout.signal(output);
    const auto output_owners = layout.owners(output);
    require(output_layout.first_value_word
                != output_layout.first_logic9_word
            && output_owners.size() == 1U
            && output_owners[0U].first_value_word
                != output_owners[0U].first_logic9_word,
        "mixed kinds give signal and owner upper planes independent offsets");
    AuthoritativeSignalPlanes values {
        layout, PackedSlotBindingPolicy::experimental_wide
    };

    const auto logic4_current = value_for(width, ValueKind::logic4, 0U);
    const auto logic4_previous = value_for(width, ValueKind::logic4, 1U);
    const auto logic4_stored = value_for(width, ValueKind::logic4, 2U);
    const auto logic4_owner = value_for(width, ValueKind::logic4, 3U);
    const auto input_current = value_for(width, ValueKind::logic9, 4U);
    const auto input_previous = value_for(width, ValueKind::logic9, 5U);
    const auto input_stored = value_for(width, ValueKind::logic9, 6U);
    const auto output_current = value_for(width, ValueKind::logic9, 7U);
    const auto output_previous = value_for(width, ValueKind::logic9, 8U);
    const auto output_stored = value_for(width, ValueKind::logic9, 9U);
    const auto output_owner = value_for(width, ValueKind::logic9, 10U);
    values.seed_signal(0U, logic4_current, logic4_previous, logic4_stored);
    values.seed_owner(0U, 0U, logic4_owner);
    values.seed_signal(1U, input_current, input_previous, input_stored);
    values.seed_owner(1U, 1U, input_stored);
    values.seed_signal(output, output_current, output_previous,
        output_stored);
    values.seed_owner(output, owner, output_owner);

    auto current0 = logic4_current;
    auto previous0 = logic4_previous;
    auto stored0 = logic4_stored;
    auto owner0 = logic4_owner;
    auto current1 = input_current;
    auto previous1 = input_previous;
    auto stored1 = input_stored;
    auto owner1 = input_stored;
    auto current2 = output_current;
    auto previous2 = output_previous;
    auto stored2 = output_stored;
    auto owner2 = output_owner;
    values.stage_packed_signal_slots(0U, current0, previous0, stored0);
    values.stage_packed_owner_slot(0U, 0U, owner0);
    values.stage_packed_signal_slots(1U, current1, previous1, stored1);
    values.stage_packed_owner_slot(1U, 1U, owner1);
    values.stage_packed_signal_slots(output, current2, previous2, stored2);
    values.stage_packed_owner_slot(output, owner, owner2);
    require(values.bind_packed_slots() == 12U,
        "mixed Logic4/Logic9 fixture binds every signal and owner role");

    const auto next_current = value_for(width, ValueKind::logic9, 11U);
    const auto next_previous = output_current;
    const auto next_stored = value_for(width, ValueKind::logic9, 12U);
    const auto next_owner = value_for(width, ValueKind::logic9, 13U);
    const std::array<AuthoritativeSignalPlanes::FrontierWriteBinding, 1U>
        writable_signals { {
            AuthoritativeSignalPlanes::FrontierWriteBinding { output, owner }
        } };
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    require(values.try_acquire_frontier_write_lease(values.revision(),
                writable_signals, lease),
        "mixed-offset Logic9 lease acquires its certified output");
    std::array<std::span<const std::uint64_t>, 4U> input_planes { };
    require(lease.current_plane_words(1U, input_planes)
            && std::ranges::equal(input_planes[2U],
                input_current.logic9_plane_words(2U))
            && std::ranges::equal(input_planes[3U],
                input_current.logic9_plane_words(3U)),
        "mixed layout reads prior Logic9 upper planes at their own offset");
    std::array<std::span<std::uint64_t>, 4U> current_planes { };
    std::array<std::span<std::uint64_t>, 4U> previous_planes { };
    std::array<std::span<std::uint64_t>, 4U> stored_planes { };
    std::array<std::span<std::uint64_t>, 4U> owner_planes { };
    require(lease.plane_words(output, PackedPlaneRole::current, owner,
                current_planes)
            && lease.plane_words(output, PackedPlaneRole::previous, owner,
                previous_planes)
            && lease.plane_words(output, PackedPlaneRole::stored, owner,
                stored_planes)
            && lease.plane_words(output, PackedPlaneRole::owner, owner,
                owner_planes),
        "mixed layout leases all four target roles");
    copy_logic9_planes(next_current, current_planes);
    copy_logic9_planes(next_previous, previous_planes);
    copy_logic9_planes(next_stored, stored_planes);
    copy_logic9_planes(next_owner, owner_planes);
    lease.note_value_change(output);
    lease.release();
    require(values.valid()
            && values.current(output) == next_current
            && values.previous(output) == next_previous
            && values.stored(output) == next_stored
            && values.owner_value(output, owner) == next_owner
            && values.current(1U) == input_current
            && values.owner_value(1U, 1U) == input_stored
            && values.current(0U) == logic4_current
            && values.owner_value(0U, 0U) == logic4_owner,
        "mixed-offset Logic9 write updates only its four target roles");
}

} // namespace

int main()
{
    try {
        for (const auto width : { 65U, 129U, 1024U }) {
            exercise_snapshot_safe_rebind(width, ValueKind::logic4);
            exercise_snapshot_safe_rebind(width, ValueKind::logic9);
        }
        exercise_retained_mirror_role_publication(129U, ValueKind::logic4);
        exercise_retained_mirror_role_publication(129U, ValueKind::logic9);
        exercise_mirror_cow_failure_rollback(129U, ValueKind::logic4);
        exercise_mirror_cow_failure_rollback(129U, ValueKind::logic9);
        for (const auto width : { 65U, 128U, 129U, 256U, 1024U }) {
            exercise_width(width, ValueKind::logic4);
            exercise_width(width, ValueKind::logic9);
            exercise_retired_role_reuse(width, ValueKind::logic4);
            exercise_retired_role_reuse(width, ValueKind::logic9);
        }
        exercise_preflight_failure_and_retry();
        exercise_scheduled_preflight_failure_and_retry();
        exercise_native_word_a4_direct_publication(false);
        exercise_native_word_a4_direct_publication(true);
        exercise_native_logic9_a4_direct_publication(false);
        exercise_native_logic9_a4_direct_publication(true);
        exercise_observed_transaction_checked_fallback();
        exercise_vhdl_projected_wide_queue_publication("1");
        exercise_vhdl_projected_wide_queue_publication(nullptr);
        exercise_unresolved_wide_whole_owner_alias();
        exercise_unresolved_wide_multioutput_owner();
        exercise_unresolved_wide_multioutput_delayed_decline();
        exercise_unresolved_wide_same_owner_cancellation();
        exercise_unresolved_wide_multiple_owner_fallback();
        exercise_unresolved_wide_mixed_owner_provenance();
        exercise_unresolved_wide_partial_owner_declines_alias();
        exercise_std_logic_scalar_driver_coverage();
        exercise_vhdl_projected_disjoint_slices();
        exercise_update_commit_scratch_failure_and_retry();
        exercise_update_commit_word_order_and_reentry();
        exercise_update_commit_container_proxy_interleaving();
        exercise_update_commit_word_scratch_failure_and_retry();
        exercise_vhdl_projected_wide_preflight_failure_and_retry();
        exercise_prepared_disjoint_owner_group_publication();
        exercise_disjoint_owner_group_cow_failure();
        exercise_disjoint_owner_group_single_row_bypass();
        exercise_generic_update_slice_storage_admission();
        exercise_generic_three_owner_slice_selection();
        exercise_disjoint_owner_runtime_routes();
        exercise_narrow_single_owner_default_and_checked_policies();
        exercise_owner_only_publication_preserves_untouched_roles();
        exercise_frontier_write_lease();
        exercise_frontier_write_lease_narrow(1U);
        exercise_frontier_write_lease_narrow(64U);
        exercise_frontier_write_lease_separate_owner(65U);
        exercise_frontier_write_lease_separate_owner(129U);
        for (const auto width : { 1U, 64U, 65U, 129U, 256U, 1024U }) {
            exercise_frontier_write_lease_logic9(width, false);
            if (width > 64U) {
                exercise_frontier_write_lease_logic9(width, true);
            }
        }
        exercise_frontier_write_lease_mixed_logic4_logic9();
        exercise_late_pin_preflight_failure_and_retry();
        exercise_mirror_previous_generation();
        exercise_stale_distinct_signal_preparation();
        exercise_stale_same_signal_preparation();
        exercise_stale_group_preparation();
        std::cout << "wide A4 slot ownership tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << "wide A4 slot ownership test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
