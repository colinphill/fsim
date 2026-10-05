// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct OwnedDriverDemotionTestAccess {
    static auto& implementation(Interpreter& interpreter)
    {
        return *interpreter.impl_;
    }

    static bool packed_a4_slots_bound(
        Interpreter& interpreter, const SignalId signal,
        const ProcessId owner)
    {
        auto* const state
            = implementation(interpreter).region_authoritative_state_for_signal(
                signal);
        return state != nullptr
            && state->values().packed_signal_slots_bound(signal)
            && state->values().packed_owner_slot_bound(signal, owner);
    }

    static std::array<PackedLogic4, 4U> packed_a4_values(
        Interpreter& interpreter, const SignalId signal,
        const ProcessId owner)
    {
        auto& impl = implementation(interpreter);
        const auto* const driver = impl.driver_values.at(signal).find(owner);
        if (driver == nullptr) {
            throw std::runtime_error {
                "Logic9 runtime has no original direct owner"
            };
        }
        return { impl.signals.at(signal).initial_value,
            impl.signal_last_values.at(signal), impl.driven_values.at(signal),
            driver->value };
    }
};

} // namespace fsim::runtime::simir

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using fsim::tests::runtime::staging_failure_support::allocation_failure_was_injected;
using fsim::tests::runtime::staging_failure_support::arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
#if defined(_WIN32)
        if (::_putenv_s(name_.c_str(), value) != 0) {
            throw std::runtime_error { "failed to set A4 test environment" };
        }
#else
        if (::setenv(name_.c_str(), value, 1) != 0) {
            throw std::runtime_error { "failed to set A4 test environment" };
        }
#endif
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(
            name_.c_str(), had_previous_ ? previous_.c_str() : ""));
#else
        if (had_previous_) {
            static_cast<void>(::setenv(name_.c_str(), previous_.c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_.c_str()));
        }
#endif
    }

private:
    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

Process whole_writer()
{
    Process process;
    process.id = 0U;
    process.name = "a4_logic9_slot_allocation_writer";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.static_sensitivity = { { 0U, EdgeKind::any } };
    process.driver_regions = { { 1U, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, 0U },
        WriteUpdate { 1U, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    return process;
}

void check_narrow_logic9_slot_copy_and_demotion_are_allocation_free()
{
    std::vector<RegionSignalDescriptor> descriptors(2U, { 9U });
    descriptors[1U].value_kind = ValueKind::logic9;
    const std::vector<Process> processes { whole_writer() };
    const std::array<const Process*, 1U> programs { &processes.front() };
    const auto graph = RegionGraph::build(programs, descriptors);
    const std::array signal_ids { 1U };
    AuthoritativeSignalPlanes values {
        SignalDriverLayout::build(graph, signal_ids)
    };

    const auto original
        = PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
    const auto replacement
        = PackedLogic4::from_logic9_msb_string("HLWZ10UX-");
    auto masked_owner = original;
    masked_owner.insert_masked_logic9_word(
        replacement.logic9_low_word(), UINT64_C(0x155));
    auto current = original;
    auto previous = original;
    auto stored = original;
    auto owner = original;
    values.seed_signal(1U, current, previous, stored);
    values.seed_owner(1U, 0U, owner);
    values.stage_packed_signal_slots(
        1U, current, previous, stored);
    values.stage_packed_owner_slot(1U, 0U, owner);
    require(values.bind_packed_slots() == 4U,
        "the measured Logic9 fixture binds all four roles");

    std::optional<PackedLogic4> copied;
    std::optional<PackedLogic4> moved;
    PackedLogic4 assigned;
    const auto replacement_word = replacement.logic9_low_word();
    begin_allocation_count();
    copied.emplace(current);
    moved.emplace(std::move(owner));
    assigned = current;
    copied->set_logic9(0U, Logic9::zero);
    current.assign_logic9_word(replacement_word);
    owner.insert_masked_logic9_word(replacement_word, UINT64_C(0x155));
    const auto unbound = values.unbind_packed_slots();
    const auto allocations = end_allocation_count();

    require(allocations == 0U,
        "narrow Logic9 copy, masked write, and demotion request no allocations");
    require(unbound == 4U && copied && moved
            && current == replacement
            && assigned == original
            && moved.value() == original
            && owner == masked_owner
            && previous == original && stored == original,
        "allocation-free demotion preserves independent snapshots and slot contents");
}

void check_runtime_late_logic9_getter_demotion_is_allocation_free()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    const auto initial_source
        = PackedLogic4::from_logic9_msb_string("UX01ZWLH-");
    const auto initial_internal
        = PackedLogic4::from_logic9_msb_string("ZZZZZZZZZ");
    Interpreter interpreter;
    const auto source = interpreter.add_signal({
        "a4.logic9_allocation_source", initial_source,
        ResolutionKind::none, ValueKind::logic9 });
    const auto internal = interpreter.add_signal({
        "a4.logic9_allocation_internal", initial_internal,
        ResolutionKind::sv_wire, ValueKind::logic9 });
    auto process = whole_writer();
    process.register_value_kinds = { ValueKind::logic9 };
    process.static_sensitivity = { { source, EdgeKind::any } };
    process.driver_regions = { { internal, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, source },
        WriteUpdate { internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(process)) == 0U,
        "runtime Logic9 getter fixture has a stable writer identity");
    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "runtime Logic9 getter fixture reaches its first suspension");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, internal, 0U),
        "runtime Logic9 getter starts with current and raw slots bound");

    begin_allocation_count();
    const auto& observed = interpreter.signal_value(internal);
    const auto allocations = end_allocation_count();
    require(allocations == 0U,
        "first late narrow Logic9 getter demotes without allocating");
    require(observed.is_logic9() && observed == initial_source
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, internal, 0U),
        "allocation-free getter returns materialized Logic9 after unbinding");
}

struct Logic9BoundaryBatchState {
    std::size_t resumes { };
    bool batch_consumed { };
    std::uint64_t remaining_mask { };
    Logic9Word raw_value;
};

struct Logic9GroupedFallbackRound {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 owner0;
    PackedLogic4 owner1;
    PackedLogic4 expected_current;
    PackedLogic4 expected_owner0;
    PackedLogic4 expected_owner1;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    PackedLogic4 same_value_current;
    PackedLogic4 same_value_previous;
    PackedLogic4 same_value_stored;
    PackedLogic4 same_value_owner0;
    PackedLogic4 same_value_owner1;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        same_value_event;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        same_value_transaction;
    ProcessSchedulingDomain event_process_domain {
        ProcessSchedulingDomain::generic
    };
    SchedulerPhase event_phase { SchedulerPhase::active };
    std::uint64_t systemverilog_round { };
    std::uint64_t value_revision_delta { };
    std::uint64_t update_commit_delta { };
    std::uint64_t same_value_revision_delta { };
    std::uint64_t same_value_update_commit_delta { };
    std::uint64_t same_value_a4_revision_delta { };
    std::uint64_t a4_revision_delta { };
    SimulationTick final_time { };
    bool raw_current_planes_match { };
    bool slots_bound_before { };
    bool slots_bound_after { };
    bool owner0_gate_admitted { };
    bool owner1_gate_admitted { };
    bool owners_have_disjoint_scalar_regions { };
    bool grouped_workspace_prepared { };
    bool component_state_unchanged { };
    bool completed { };
    bool threw { };
    bool allocation_injected { };
    bool observed_before_update { };
    bool slots_bound_at_update { };
    bool retained_leases_preserved { };
    bool retry_completed { };
    bool retry_values_match { };
    bool same_value_round_completed { };
    bool same_value_owner_resumes { };
    bool same_value_slots_bound { };
    bool same_value_component_state_unchanged { };
    bool same_value_component_generation_unchanged { };
    bool same_value_runtime_generation_unchanged { };
    bool same_value_raw_current_planes_match { };
};

Process generic_logic9_projected_slice_writer(const ProcessId id,
    const SignalId input,
    const std::optional<SignalId> trigger,
    const SignalId output,
    const std::uint32_t offset,
    const std::uint32_t width)
{
    Process process;
    process.id = id;
    process.name = "a4_logic9_projected_slice_owner_"
        + std::to_string(id);
    process.language_standard = "2008";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.initialize = false;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic9 };
    process.static_sensitivity = { { input, EdgeKind::any } };
    if (trigger) {
        process.static_sensitivity.push_back(
            { *trigger, EdgeKind::any });
    }
    process.driver_regions = { { output, offset, width, false } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteProjectedSlice {
            output, 0U, offset, 0U, 0U,
            ProjectedDelayMode::inertial },
        WaitSensitivity { },
        Jump { 0U },
    };
    return process;
}

struct Logic9OwnerGroupExecutorState {
    std::size_t staged_writes { };
    std::optional<std::size_t> failure_index;
    bool failure_armed { };
};

class Logic9OwnerGroupExecutor final : public ProcessExecutor {
public:
    Logic9OwnerGroupExecutor(const SignalId input,
        const SignalId output,
        const std::size_t offset,
        Logic9OwnerGroupExecutorState& state,
        ProcessExecutorProgramBinding access_binding)
        : input_ { input }
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
        auto value = context.read_signal(input_);
        context.write_projected_slice(output_, std::move(value), offset_,
            0U, 0U, ProjectedDelayMode::inertial);
        ++state_->staged_writes;
        if (state_->failure_index && !state_->failure_armed
            && state_->staged_writes == 2U) {
            // Both zero-delay projected writes have entered the same generic
            // Update before the retained-role COW preflight begins.
            arm_allocation_failure(*state_->failure_index);
            state_->failure_armed = true;
        }
        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    SignalId input_ { };
    SignalId output_ { };
    std::size_t offset_ { };
    Logic9OwnerGroupExecutorState* state_ { };
    ProcessExecutorProgramBinding access_binding_;
};

class Logic9BoundaryBatchExecutor final : public ProcessExecutor {
public:
    Logic9BoundaryBatchExecutor(const SignalId input, const SignalId output,
        const bool use_invalid_codes, Logic9BoundaryBatchState& state,
        ProcessExecutorProgramBinding access_binding)
        : input_ { input }
        , output_ { output }
        , use_invalid_codes_ { use_invalid_codes }
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
        ProcessExecutionContext& context, InstructionIndex) override
    {
        ++state_->resumes;
        auto word = context.read_signal_logic9_word(input_);
        if (use_invalid_codes_ && state_->resumes > 1U) {
            set_logic9_code(word, 3U, 9U);
            set_logic9_code(word, 29U, 12U);
            set_logic9_code(word, 61U, 15U);
        }
        state_->raw_value = word;
        state_->remaining_mask = word.width == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (UINT64_C(1) << word.width) - UINT64_C(1);
        const ProcessLogic9UpdateSlotView slot {
            output_, static_cast<std::uint32_t>(word.width),
            word.planes.data(), &state_->remaining_mask
        };
        const ProcessLogic9UpdateBatch batch {
            0U, std::span { &slot, 1U }
        };
        state_->batch_consumed
            = context.write_validated_logic9_update_batch(batch);

        ProcessResumeResult result { 2U, 3U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

private:
    static void set_logic9_code(
        Logic9Word& word, const std::size_t bit, const std::uint8_t code)
    {
        const auto mask = UINT64_C(1) << bit;
        for (std::size_t plane = 0U; plane < word.planes.size(); ++plane) {
            if ((code & (1U << plane)) != 0U) {
                word.planes[plane] |= mask;
            } else {
                word.planes[plane] &= ~mask;
            }
        }
    }

    SignalId input_ { };
    SignalId output_ { };
    bool use_invalid_codes_ { };
    Logic9BoundaryBatchState* state_ { };
    ProcessExecutorProgramBinding access_binding_;
};

[[nodiscard]] PackedLogic4 logic9_pattern(
    const std::size_t width, const std::size_t phase)
{
    static constexpr std::string_view states { "UX01ZWLH-" };
    std::string text;
    text.reserve(width);
    for (std::size_t bit = 0U; bit < width; ++bit) {
        text.push_back(states[(bit + phase) % states.size()]);
    }
    return PackedLogic4::from_logic9_msb_string(text);
}

Process generic_logic9_writer(
    const SignalId input, const SignalId output)
{
    Process process;
    process.id = 0U;
    process.name = "a4_logic9_boundary_batch_writer";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic9 };
    process.static_sensitivity = { { input, EdgeKind::any } };
    process.driver_regions = { { output, 0U, 0U, true } };
    process.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { output, 0U, SignalUpdateDomain::generic },
        WaitSensitivity { }, Jump { 0U },
    };
    return process;
}

[[nodiscard]] PackedLogic4 logic9_all_z(const std::uint32_t width)
{
    return PackedLogic4::from_logic9_msb_string(
        std::string(width, 'Z'));
}

[[nodiscard]] PackedLogic4 logic9_all_u(const std::uint32_t width)
{
    return PackedLogic4::from_logic9_msb_string(
        std::string(width, 'U'));
}

template<typename Implementation>
[[nodiscard]] bool direct_logic9_current_planes_match(
    const Implementation& implementation,
    const SignalId signal,
    const PackedLogic4& value)
{
    if (!value.is_logic9() || value.width() == 0U) {
        return false;
    }
    if (value.width() <= 64U) {
        if (signal >= implementation.direct_signal_logic9_plane0.size()
            || signal >= implementation.direct_signal_logic9_plane1.size()
            || signal >= implementation.direct_signal_logic9_plane2.size()
            || signal >= implementation.direct_signal_logic9_plane3.size()) {
            return false;
        }
        const auto word = value.logic9_low_word();
        return implementation.direct_signal_logic9_plane0[signal]
                == word.planes[0U]
            && implementation.direct_signal_logic9_plane1[signal]
                == word.planes[1U]
            && implementation.direct_signal_logic9_plane2[signal]
                == word.planes[2U]
            && implementation.direct_signal_logic9_plane3[signal]
                == word.planes[3U];
    }
    if (signal >= implementation.direct_wide_signal_offsets.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(
        implementation.direct_wide_signal_offsets[signal]);
    if (offset == std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const std::array<std::span<const std::uint64_t>, 4U> expected {
        value.aval_words(), value.bval_words(),
        value.logic9_plane_words(2U), value.logic9_plane_words(3U)
    };
    const std::array<const std::vector<std::uint64_t>*, 4U> actual {
        &implementation.direct_wide_signal_aval,
        &implementation.direct_wide_signal_bval,
        &implementation.direct_wide_signal_logic9_plane2,
        &implementation.direct_wide_signal_logic9_plane3
    };
    for (std::size_t plane = 0U; plane < expected.size(); ++plane) {
        if (offset > actual[plane]->size()
            || expected[plane].size()
                > actual[plane]->size() - offset
            || !std::equal(expected[plane].begin(), expected[plane].end(),
                actual[plane]->begin()
                    + static_cast<std::ptrdiff_t>(offset))) {
            return false;
        }
    }
    return true;
}

template<typename Lease>
[[nodiscard]] bool logic9_plane_lease_matches(
    const Lease& lease, const PackedLogic4& value)
{
    if (!lease || !value.is_logic9() || !lease.is_logic9()
        || lease.width() != value.width()) {
        return false;
    }
    const std::array<std::span<const std::uint64_t>, 4U> expected {
        value.aval_words(), value.bval_words(),
        value.logic9_plane_words(2U), value.logic9_plane_words(3U)
    };
    for (std::size_t plane = 0U; plane < expected.size(); ++plane) {
        const auto actual = lease.plane_words(plane);
        if (!std::ranges::equal(actual, expected[plane])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Logic9GroupedFallbackRound
run_logic9_same_update_disjoint_owners(
    const std::uint32_t width,
    const char* disjoint_owner_policy,
    const std::optional<std::size_t> failure_index = std::nullopt,
    const bool late_observer = false,
    const bool same_value_transaction_round = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment disjoint_owner_policy_scope {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        disjoint_owner_policy };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment update_profile_enabled {
        "FSIM_PROFILE_UPDATES", "1" };

    const auto lower_width = width / 2U;
    const auto upper_width = width - lower_width;
    const auto upper_offset = lower_width;
    const auto initial_input0 = logic9_pattern(lower_width, 1U);
    const auto initial_input1 = logic9_pattern(upper_width, 2U);
    const auto next_input0 = logic9_pattern(lower_width, 4U);
    const auto next_input1 = logic9_pattern(upper_width, 6U);
    const auto declared_target = logic9_all_z(width);
    const auto initial_target = logic9_all_u(width);
    auto initial_owner0_value = logic9_all_z(width);
    initial_owner0_value.insert_bits(logic9_all_u(lower_width), 0U);
    auto initial_owner1_value = logic9_all_z(width);
    initial_owner1_value.insert_bits(
        logic9_all_u(upper_width), upper_offset);
    // Each owner has a disjoint scalar region. The inactive sibling owner is
    // excluded per bit, so a sole source's Logic9 '-' remains '-' here. Each
    // input pattern spans all nine canonical states at these widths.
    auto expected_current = logic9_all_z(width);
    expected_current.insert_bits(next_input0, 0U);
    expected_current.insert_bits(next_input1, upper_offset);
    auto expected_owner0 = logic9_all_z(width);
    expected_owner0.insert_bits(next_input0, 0U);
    auto expected_owner1 = logic9_all_z(width);
    expected_owner1.insert_bits(next_input1, upper_offset);

    Interpreter interpreter;
    const auto input0 = interpreter.add_signal({
        "a4.logic9_group_fallback.input0", initial_input0,
        ResolutionKind::none, ValueKind::logic9 });
    const auto input1 = interpreter.add_signal({
        "a4.logic9_group_fallback.input1", initial_input1,
        ResolutionKind::none, ValueKind::logic9 });
    const auto target = interpreter.add_signal({
        "a4.logic9_group_fallback.target", declared_target,
        ResolutionKind::std_logic, ValueKind::logic9 });
    std::optional<SignalId> same_value_trigger;
    if (same_value_transaction_round) {
        same_value_trigger = interpreter.add_signal({
            "a4.logic9_group_fallback.trigger",
            PackedLogic4 { 1U, Logic4::zero },
            ResolutionKind::none, ValueKind::logic4 });
    }
    Logic9OwnerGroupExecutorState executor_state;
    executor_state.failure_index = failure_index;
    const auto add_owner = [&](const ProcessId process_id,
                               const SignalId input,
                               const std::optional<SignalId> trigger,
                               const std::uint32_t offset,
                               const std::uint32_t owner_width) {
        auto process = generic_logic9_projected_slice_writer(
            process_id, input, trigger, target, offset, owner_width);
        require(interpreter.add_process(std::move(process)) == process_id,
            "both Logic9 owners retain distinct identities on one partial signal");
        const auto& registered = interpreter.process_program(process_id);
        interpreter.set_process_executor(process_id,
            std::make_unique<Logic9OwnerGroupExecutor>(input, target,
                offset, executor_state,
                ProcessExecutorProgramBinding {
                    registered, registered, process_id }));
    };
    add_owner(0U, input0, same_value_trigger, 0U, lower_width);
    add_owner(1U, input1, same_value_trigger, upper_offset, upper_width);

    interpreter.start();
    require(interpreter.run().status == RunStatus::completed,
        "Logic9 disjoint owners reach the initial generic quiet point");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto initial_component = target
            < implementation.region_authoritative_component_by_signal.size()
        ? implementation.region_authoritative_component_by_signal[target]
        : std::numeric_limits<std::size_t>::max();
    std::shared_ptr<RegionAuthoritativeComponentState>
        initial_component_owner;
    if (initial_component
            < implementation.region_authoritative_state_by_component.size()) {
        initial_component_owner
            = implementation.region_authoritative_state_by_component[
                initial_component];
    }
    const auto* const initial_component_state
        = initial_component_owner.get();
    const bool policy_enabled
        = disjoint_owner_policy != nullptr
        && std::string_view { disjoint_owner_policy } == "1";
    Logic9GroupedFallbackRound result;
    result.slots_bound_before
        = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 1U);
    const auto before_current
        = implementation.get_signal(target).initial_value;
    const auto before_revision
        = implementation.signal_value_revisions.at(target);
    const auto before_update_commits = implementation.update_profile_commits;
    const auto before_a4_revision = initial_component_state == nullptr
        ? 0U : initial_component_state->values().revision();
    const auto before_event = implementation.signal_events.at(target);
    const auto before_transaction
        = implementation.signal_transactions.at(target);
    const auto* const initial_owner0
        = implementation.driver_values.at(target).find(0U);
    const auto* const initial_owner1
        = implementation.driver_values.at(target).find(1U);
    const auto has_exact_scalar_region = [](
        const DriverRecord* const record,
        const SignalId expected_signal,
        const std::uint32_t offset,
        const std::uint32_t region_width) {
        return record != nullptr && record->scalar_regions
            && record->scalar_regions->size() == 1U
            && record->scalar_regions->front().signal == expected_signal
            && record->scalar_regions->front().offset == offset
            && record->scalar_regions->front().width == region_width
            && !record->scalar_regions->front().whole;
    };
    result.owners_have_disjoint_scalar_regions
        = has_exact_scalar_region(initial_owner0, target, 0U, lower_width)
        && has_exact_scalar_region(
            initial_owner1, target, upper_offset, upper_width);
    require(initial_owner0 != nullptr && initial_owner1 != nullptr
            && result.owners_have_disjoint_scalar_regions
            && before_current == initial_target
            && initial_owner0->value == initial_owner0_value
            && initial_owner1->value == initial_owner1_value,
        "both Logic9 owner records begin at U only in their exact disjoint scalar regions");

    if (policy_enabled) {
        require(result.slots_bound_before
                && initial_component_state != nullptr
                && initial_component_state->valid()
                && implementation.can_try_wide_disjoint_owner_commit(
                    0U, target)
                && implementation.can_try_wide_disjoint_owner_commit(
                    1U, target),
            "wide Logic9 inputs satisfy the existing per-owner A4 admission");
        const auto graph_signals = implementation.region_graph->signals();
        require(target < graph_signals.size(),
            "Logic9 group target has a dense RegionGraph node");
        const auto& signal_node = graph_signals[target];
        require(signal_node.descriptor.value_kind == ValueKind::logic9
                && signal_node.descriptor.resolution
                    == ResolutionKind::std_logic
                && signal_node.drivers
                    == RegionDriverClass::disjoint_partial
                && signal_node.writers.size() == 2U,
            "the grouped fallback witness has exactly two disjoint std_logic owners");
        const auto& owner0 = signal_node.writers[0U];
        const auto& owner1 = signal_node.writers[1U];
        require(owner0.process != owner1.process
                && owner0.offset == 0U && owner0.width == lower_width
                && owner1.offset == upper_offset
                && owner1.width == upper_width,
            "the same Logic9 signal has a complete nonoverlapping owner partition");
    }

    std::array<PackedLogic4PlaneReadLease, 5U> retained_leases;
    if (failure_index) {
        require(policy_enabled && initial_component_state != nullptr
                && result.slots_bound_before,
            "Logic9 group COW failure keeps the eligible component bound");
        const auto before_owner_roles
            = OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, target, 0U);
        retained_leases = {
            initial_component_state->values().plane_read_lease(
                target, PackedPlaneRole::current),
            initial_component_state->values().plane_read_lease(
                target, PackedPlaneRole::previous),
            initial_component_state->values().plane_read_lease(
                target, PackedPlaneRole::stored),
            initial_component_state->values().plane_read_lease(
                target, PackedPlaneRole::owner, 0U),
            initial_component_state->values().plane_read_lease(
                target, PackedPlaneRole::owner, 1U),
        };
        require(std::ranges::all_of(retained_leases,
                    [](const auto& lease) {
                        return static_cast<bool>(lease);
                    })
                && logic9_plane_lease_matches(
                    retained_leases[0U], before_owner_roles[0U])
                && logic9_plane_lease_matches(
                    retained_leases[1U], before_owner_roles[1U])
                && logic9_plane_lease_matches(
                    retained_leases[2U], before_owner_roles[2U])
                && logic9_plane_lease_matches(
                    retained_leases[3U], before_owner_roles[3U])
                && logic9_plane_lease_matches(
                    retained_leases[4U], initial_owner1_value),
            "Logic9 owner-group COW fixture retains every original plane role");
    }

    if (late_observer) {
        const auto& observed = interpreter.signal_value(target);
        result.observed_before_update = observed == before_current;
        result.slots_bound_at_update
            = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        require(result.observed_before_update
                && !result.slots_bound_at_update
                && !implementation.can_try_wide_disjoint_owner_commit(
                    0U, target)
                && !implementation.can_try_wide_disjoint_owner_commit(
                    1U, target),
            "a late public observer demotes both Logic9 owner slots before the next Update");
    } else {
        result.slots_bound_at_update = result.slots_bound_before;
    }
    interpreter.schedule_signal_at(input0, next_input0, 1U, 0U);
    interpreter.schedule_signal_at(input1, next_input1, 1U, 1U);
    RunStatus update_status = RunStatus::completed;
    try {
        update_status = interpreter.run().status;
    } catch (const std::bad_alloc&) {
        clear_allocation_failure();
        result.threw = true;
        return result;
    }
    result.allocation_injected
        = failure_index && allocation_failure_was_injected();
    clear_allocation_failure();
    result.completed = update_status == RunStatus::completed;
    if (!result.completed) {
        return result;
    }

    result.current = implementation.get_signal(target).initial_value;
    result.previous = implementation.signal_last_values.at(target);
    result.stored = implementation.driven_values.at(target);
    const auto* const final_owner0
        = implementation.driver_values.at(target).find(0U);
    const auto* const final_owner1
        = implementation.driver_values.at(target).find(1U);
    require(final_owner0 != nullptr && final_owner1 != nullptr,
        "the generic Update retains both original Logic9 owner records");
    result.owner0 = final_owner0->value;
    result.owner1 = final_owner1->value;
    result.expected_current = expected_current;
    result.expected_owner0 = expected_owner0;
    result.expected_owner1 = expected_owner1;
    result.event = implementation.signal_events.at(target);
    result.transaction = implementation.signal_transactions.at(target);
    const auto& stamp
        = implementation.signal_event_scheduling_stamps.at(target);
    result.event_process_domain = stamp.origin.process_domain;
    result.event_phase = stamp.origin.phase;
    result.systemverilog_round = stamp.systemverilog_round;
    result.value_revision_delta
        = implementation.signal_value_revisions.at(target) - before_revision;
    result.update_commit_delta
        = implementation.update_profile_commits - before_update_commits;
    const auto* const final_component_state
        = implementation.region_authoritative_state_for_signal(target);
    result.component_state_unchanged
        = final_component_state == initial_component_state;
    result.a4_revision_delta = final_component_state == nullptr
        || final_component_state != initial_component_state
        ? 0U : final_component_state->values().revision() - before_a4_revision;
    result.final_time = interpreter.scheduler().now();
    result.slots_bound_after
        = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, target, 1U);
    result.owner0_gate_admitted
        = implementation.can_try_wide_disjoint_owner_commit(0U, target);
    result.owner1_gate_admitted
        = implementation.can_try_wide_disjoint_owner_commit(1U, target);
    result.grouped_workspace_prepared
        = target < implementation.disjoint_owner_group_scratch_by_signal.size()
        && implementation.disjoint_owner_group_scratch_by_signal[target]
                .prepared;
    result.raw_current_planes_match
        = direct_logic9_current_planes_match(
            implementation, target, expected_current);
    if (failure_index) {
        result.retained_leases_preserved
            = logic9_plane_lease_matches(
                retained_leases[0U], initial_target)
            && logic9_plane_lease_matches(
                retained_leases[1U], initial_target)
            && logic9_plane_lease_matches(
                retained_leases[2U], initial_target)
            && logic9_plane_lease_matches(
                retained_leases[3U], initial_owner0_value)
            && logic9_plane_lease_matches(
                retained_leases[4U], initial_owner1_value);
    }

    if (failure_index
        && (!result.allocation_injected
            || !result.grouped_workspace_prepared)) {
        return result;
    }

    require(result.current == expected_current
            && result.previous == initial_target
            && result.stored == expected_current
            && result.owner0 == expected_owner0
            && result.owner1 == expected_owner1,
        "same-Update Logic9 fallback preserves exact current/LAST/stored "
        "and original-owner values");
    require(result.event && result.transaction
            && result.event == result.transaction
            && result.event != before_event
            && result.transaction != before_transaction
            && result.event->first == 1U
            && result.event_process_domain
                == ProcessSchedulingDomain::generic
            && result.event_phase == SchedulerPhase::active
            && result.systemverilog_round == 0U
            && result.value_revision_delta == 1U
            // The two scheduled input writes share one commit; their
            // sensitized projected writes share the following commit.
            && result.update_commit_delta == 2U
            && result.owners_have_disjoint_scalar_regions
            && result.raw_current_planes_match,
        "both owner writes coalesce into one generic Update with exact "
        "current planes and event metadata");
    if (policy_enabled) {
        if (failure_index) {
            require(result.grouped_workspace_prepared
                    && result.allocation_injected
                    && !result.slots_bound_after
                    && result.retained_leases_preserved,
                "failed Logic9 group COW preserves read leases and demotes "
                "before checked fallback");
        } else if (late_observer) {
            require(!result.slots_bound_at_update
                    && !result.slots_bound_after,
                "late-observed Logic9 owner group stays on checked publication");
        } else {
            require(result.slots_bound_after
                && result.owner0_gate_admitted
                && result.owner1_gate_admitted
                && result.a4_revision_delta == 1U
                && result.component_state_unchanged
                && result.grouped_workspace_prepared,
                "Logic9 publishes both disjoint owners through one retained A4 group mutation");
        }
        const auto roles0 = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 0U);
        const auto roles1 = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, target, 1U);
        require(roles0[0U] == expected_current
                && roles0[1U] == initial_target
                && roles0[2U] == expected_current
                && roles0[3U] == expected_owner0
                && roles1[0U] == expected_current
                && roles1[1U] == initial_target
                && roles1[2U] == expected_current
                && roles1[3U] == expected_owner1,
            "per-owner Logic9 A4 mirrors preserve all four packed facade roles");
    } else {
        require(!result.slots_bound_before && !result.slots_bound_after
                && !result.grouped_workspace_prepared,
            "explicitly disabled disjoint storage keeps ordinary checked publication");
    }

    if (same_value_transaction_round) {
        const auto event_after_change = implementation.signal_events.at(target);
        const auto transaction_after_change
            = implementation.signal_transactions.at(target);
        const auto revision_after_change
            = implementation.signal_value_revisions.at(target);
        const auto update_commits_after_change
            = implementation.update_profile_commits;
        const auto runtime_generation_after_change
            = implementation.region_runtime_generation;
        const auto a4_revision_after_change = initial_component_state == nullptr
            ? 0U : initial_component_state->values().revision();
        const auto component_generation_after_change
            = initial_component_state == nullptr
            ? 0U : initial_component_state->generation();
        interpreter.schedule_signal_at(
            *same_value_trigger, PackedLogic4 { 1U, Logic4::one }, 2U, 0U);
        result.same_value_round_completed
            = interpreter.run().status == RunStatus::completed;
        const auto* const same_value_component_state
            = implementation.region_authoritative_state_for_signal(target);
        result.same_value_component_state_unchanged
            = same_value_component_state == initial_component_state;
        result.same_value_component_generation_unchanged
            = same_value_component_state != nullptr
            && same_value_component_state->generation()
                == component_generation_after_change;
        result.same_value_runtime_generation_unchanged
            = implementation.region_runtime_generation
                == runtime_generation_after_change;
        result.same_value_a4_revision_delta
            = same_value_component_state == nullptr
            || same_value_component_state != initial_component_state
            ? 0U
            : same_value_component_state->values().revision()
                - a4_revision_after_change;
        result.same_value_slots_bound
            = OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 0U)
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, target, 1U);
        const auto* const same_value_final_owner0
            = implementation.driver_values.at(target).find(0U);
        const auto* const same_value_final_owner1
            = implementation.driver_values.at(target).find(1U);
        result.same_value_owner_resumes
            = executor_state.staged_writes == 4U;
        result.same_value_raw_current_planes_match
            = direct_logic9_current_planes_match(
                implementation, target, expected_current);
        result.same_value_current
            = implementation.get_signal(target).initial_value;
        result.same_value_previous
            = implementation.signal_last_values.at(target);
        result.same_value_stored = implementation.driven_values.at(target);
        result.same_value_event = implementation.signal_events.at(target);
        result.same_value_transaction
            = implementation.signal_transactions.at(target);
        result.same_value_revision_delta
            = implementation.signal_value_revisions.at(target)
            - revision_after_change;
        result.same_value_update_commit_delta
            = implementation.update_profile_commits
            - update_commits_after_change;
        if (same_value_final_owner0 != nullptr
            && same_value_final_owner1 != nullptr) {
            result.same_value_owner0 = same_value_final_owner0->value;
            result.same_value_owner1 = same_value_final_owner1->value;
        }
        require(result.same_value_round_completed
                && result.same_value_owner_resumes
                && result.same_value_current == expected_current
                && result.same_value_previous == initial_target
                && result.same_value_stored == expected_current
                && result.same_value_owner0 == expected_owner0
                && result.same_value_owner1 == expected_owner1
                && result.same_value_event == event_after_change
                && result.same_value_event == result.event
                && result.same_value_transaction
                && result.same_value_transaction != transaction_after_change
                && result.same_value_transaction->first == 2U
                && result.same_value_revision_delta == 0U
                && result.same_value_update_commit_delta == 2U
                && result.same_value_a4_revision_delta == 0U
                && result.same_value_component_state_unchanged
                && result.same_value_component_generation_unchanged
                && result.same_value_runtime_generation_unchanged
                && result.same_value_slots_bound
                && result.same_value_raw_current_planes_match,
            "unchanged Logic9 grouped owner writes record a transaction without changing values, LAST, event, or either revision");
    }

    if (failure_index) {
        const auto retry_input0 = logic9_pattern(lower_width, 7U);
        const auto retry_input1 = logic9_pattern(upper_width, 8U);
        auto retry_current = logic9_all_z(width);
        retry_current.insert_bits(retry_input0, 0U);
        retry_current.insert_bits(retry_input1, upper_offset);
        auto retry_owner0 = logic9_all_z(width);
        retry_owner0.insert_bits(retry_input0, 0U);
        auto retry_owner1 = logic9_all_z(width);
        retry_owner1.insert_bits(retry_input1, upper_offset);
        interpreter.schedule_signal_at(input0, retry_input0, 2U, 0U);
        interpreter.schedule_signal_at(input1, retry_input1, 2U, 1U);
        result.retry_completed
            = interpreter.run().status == RunStatus::completed;
        const auto* const retry_final_owner0
            = implementation.driver_values.at(target).find(0U);
        const auto* const retry_final_owner1
            = implementation.driver_values.at(target).find(1U);
        result.retry_values_match = result.retry_completed
            && retry_final_owner0 != nullptr && retry_final_owner1 != nullptr
            && implementation.get_signal(target).initial_value == retry_current
            && implementation.signal_last_values.at(target) == expected_current
            && implementation.driven_values.at(target) == retry_current
            && retry_final_owner0->value == retry_owner0
            && retry_final_owner1->value == retry_owner1;
    }
    return result;
}

[[nodiscard]] bool same_logic9_grouped_fallback_result(
    const Logic9GroupedFallbackRound& left,
    const Logic9GroupedFallbackRound& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.owner0 == right.owner0
        && left.owner1 == right.owner1
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_process_domain == right.event_process_domain
        && left.event_phase == right.event_phase
        && left.systemverilog_round == right.systemverilog_round
        && left.value_revision_delta == right.value_revision_delta
        && left.update_commit_delta == right.update_commit_delta
        && left.final_time == right.final_time
        && left.raw_current_planes_match
        && right.raw_current_planes_match;
}

void check_logic9_same_update_multiowner_group_fallback()
{
    for (const auto width : { 9U, 64U, 65U, 129U }) {
        const auto per_owner = run_logic9_same_update_disjoint_owners(
            width, "1");
        const auto checked = run_logic9_same_update_disjoint_owners(
            width, "0");
        require(per_owner.completed && checked.completed
                && per_owner.expected_current == checked.expected_current
                && per_owner.expected_owner0 == checked.expected_owner0
                && per_owner.expected_owner1 == checked.expected_owner1
                && same_logic9_grouped_fallback_result(per_owner, checked),
            "Logic9 same-signal multiowner generic Update matches checked fallback at narrow and wide widths");
    }

    const auto same_value_round
        = run_logic9_same_update_disjoint_owners(
            9U, "1", std::nullopt, false, true);
    require(same_value_round.completed
            && same_value_round.same_value_round_completed
            && same_value_round.same_value_owner_resumes,
        "nine-state Logic9 same-value transaction round preserves the grouped publication");

    const auto checked_129
        = run_logic9_same_update_disjoint_owners(129U, "0");
    bool found_group_preflight_failure { };
    for (std::size_t failure_index = 0U; failure_index < 64U;
         ++failure_index) {
        const auto failed = run_logic9_same_update_disjoint_owners(
            129U, "1", failure_index);
        if (!failed.completed || failed.threw || !failed.allocation_injected
            || !failed.grouped_workspace_prepared) {
            continue;
        }
        require(!failed.slots_bound_after
                && failed.retained_leases_preserved
                && failed.retry_completed && failed.retry_values_match
                && same_logic9_grouped_fallback_result(failed, checked_129),
            "failed Logic9 group COW preserves leases, uses checked fallback, and retries");
        found_group_preflight_failure = true;
        break;
    }
    require(found_group_preflight_failure,
        "pinned Logic9 owner-group preflight exercises its allocation failure path");

    const auto late_observed
        = run_logic9_same_update_disjoint_owners(65U, "1", std::nullopt, true);
    const auto checked_65
        = run_logic9_same_update_disjoint_owners(65U, "0");
    require(late_observed.completed && late_observed.observed_before_update
            && !late_observed.slots_bound_at_update
            && !late_observed.slots_bound_after
            && same_logic9_grouped_fallback_result(late_observed, checked_65),
        "late public observation demotes Logic9 owner slots and preserves checked fallback");
}

void check_width64_logic9_batch_ingress(const bool use_invalid_codes)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    ScopedEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::size_t width = 64U;
    constexpr ProcessId writer_id = 0U;
    const auto initial = logic9_pattern(width, 0U);
    const auto next_input = logic9_pattern(width, 4U);
    auto expected = next_input;
    if (use_invalid_codes) {
        expected.set_logic9(3U, Logic9::x);
        expected.set_logic9(29U, Logic9::x);
        expected.set_logic9(61U, Logic9::x);
    }

    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "a4.logic9_boundary_batch_input", initial,
        ResolutionKind::none, ValueKind::logic9 });
    const auto output = interpreter.add_signal({
        "a4.logic9_boundary_batch_output", initial,
        ResolutionKind::std_logic, ValueKind::logic9 });
    require(interpreter.add_process(
                generic_logic9_writer(input, output)) == writer_id,
        "width64 Logic9 boundary keeps its generic owner identity");
    const auto& registered = interpreter.process_program(writer_id);

    Logic9BoundaryBatchState batch_state;
    interpreter.set_process_executor(writer_id,
        std::make_unique<Logic9BoundaryBatchExecutor>(
            input, output, use_invalid_codes, batch_state,
            ProcessExecutorProgramBinding {
                registered, registered, 0U }));
    interpreter.start();

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component
        = implementation.region_authoritative_component_by_signal.at(output);
    require(component
                < implementation.region_authoritative_state_by_component.size(),
        "narrow std_logic Logic9 storage remains attached to its boundary owner");
    const auto state_owner
        = implementation.region_authoritative_state_by_component.at(component);
    auto* const state = state_owner.get();
    require(state != nullptr && state->valid()
            && state->generation() == implementation.region_runtime_generation
            && state->values().requires_prewrite_unbind()
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, writer_id)
            && !implementation.process_region_kernel_eligible(writer_id)
            && implementation.process_signal_access_is_complete(writer_id),
        "narrow Logic9 boundary is stored while its generic process remains checked");

    require(interpreter.run().status == RunStatus::completed
            && batch_state.resumes == 1U && batch_state.batch_consumed
            && batch_state.remaining_mask == 0U,
        "initial generic Logic9 batch is accepted and consumed");
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, writer_id)
            && implementation.region_authoritative_state_for_signal(output)
                == state,
        "same-value initial batch leaves the narrow boundary roles bound");

    const auto before_roles
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, output, writer_id);
    const auto before_revision
        = implementation.signal_value_revisions.at(output);
    const auto before_plane_revision = state->values().revision();
    const auto before_event = implementation.signal_events.at(output);
    const auto before_transaction
        = implementation.signal_transactions.at(output);
    interpreter.schedule_signal_at(input, next_input, 1U, 0U);
    require(OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, writer_id)
            && !implementation.region_recertification_pending
            && !implementation.region_recertification_requires_snapshot
            && !implementation.region_authoritative_recertification_waiting,
        "input scheduling preserves the exact narrow boundary storage certificate");
    require(interpreter.run().status == RunStatus::completed
            && batch_state.resumes == 2U && batch_state.batch_consumed
            && batch_state.remaining_mask == 0U,
        "width64 generic Logic9 update batch is accepted without checked fallback");

    const auto expected_word = expected.logic9_low_word();
    const auto previous_word = before_roles[0U].logic9_low_word();
    const auto wide_offset
        = implementation.direct_wide_signal_offsets.at(output);
    require(batch_state.raw_value.width == width
            && (use_invalid_codes
                ? !batch_state.raw_value.has_canonical_codes()
                : batch_state.raw_value.has_canonical_codes())
            && implementation.direct_signal_logic9_plane0.at(output)
                == expected_word.planes[0]
            && implementation.direct_signal_logic9_plane1.at(output)
                == expected_word.planes[1]
            && implementation.direct_signal_logic9_plane2.at(output)
                == expected_word.planes[2]
            && implementation.direct_signal_logic9_plane3.at(output)
                == expected_word.planes[3]
            && implementation.direct_signal_last_logic9_plane0.at(output)
                == previous_word.planes[0]
            && implementation.direct_signal_last_logic9_plane1.at(output)
                == previous_word.planes[1]
            && implementation.direct_signal_last_logic9_plane2.at(output)
                == previous_word.planes[2]
            && implementation.direct_signal_last_logic9_plane3.at(output)
                == previous_word.planes[3]
            && implementation.direct_wide_signal_aval.at(wide_offset)
                == expected_word.planes[0]
            && implementation.direct_wide_signal_bval.at(wide_offset)
                == expected_word.planes[1]
            && implementation.direct_wide_signal_logic9_plane2.at(wide_offset)
                == expected_word.planes[2]
            && implementation.direct_wide_signal_logic9_plane3.at(wide_offset)
                == expected_word.planes[3],
        "Logic9 ingress normalizes only malformed raw planes before publication");

    const auto after_roles
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, output, writer_id);
    require(after_roles[0U] == expected
            && after_roles[1U] == before_roles[0U]
            && after_roles[2U] == expected
            && after_roles[3U] == expected
            && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, writer_id)
            && implementation.region_authoritative_state_for_signal(output)
                == state
            && implementation.signal_value_revisions.at(output)
                == before_revision + 1U
            && state->values().revision() == before_plane_revision + 1U
            && implementation.signal_events.at(output)
            && implementation.signal_transactions.at(output)
                == implementation.signal_events.at(output)
            && implementation.signal_events.at(output)->first == 1U
            && implementation.signal_transactions.at(output)->first == 1U
            && implementation.signal_events.at(output) != before_event
            && implementation.signal_transactions.at(output)
                != before_transaction,
        "width64 publication commits exact current/LAST/stored/owner and event roles once");

    const auto public_current = interpreter.signal_value(output);
    const auto public_owner = interpreter.driver_value(writer_id, output);
    require(public_current == expected && public_owner == expected
            && implementation.signal_last_values.at(output)
                == before_roles[0U]
            && implementation.driven_values.at(output) == expected
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, writer_id),
        "late public observation materializes Logic9 roles and demotes the facade");
}

void check_forced_narrow_logic9_boundary_keeps_checked_fallback()
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment wide_commit_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment disjoint_commit_disabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };

    const auto initial = logic9_pattern(9U, 0U);
    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "a4.logic9_forced_boundary_input", initial,
        ResolutionKind::none, ValueKind::logic9 });
    const auto output = interpreter.add_signal({
        "a4.logic9_forced_boundary_output", initial,
        ResolutionKind::std_logic, ValueKind::logic9 });
    require(interpreter.add_process(
                generic_logic9_writer(input, output)) == 0U,
        "forced fallback fixture keeps the exact generic writer");
    interpreter.force_signal(output, initial);
    interpreter.start();
    require(interpreter.signal_is_forced(output)
            && !OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                interpreter, output, 0U),
        "force state excludes narrow boundary storage and keeps ordinary semantics");
}

} // namespace

int main()
{
    try {
        check_narrow_logic9_slot_copy_and_demotion_are_allocation_free();
        check_runtime_late_logic9_getter_demotion_is_allocation_free();
        check_width64_logic9_batch_ingress(false);
        check_width64_logic9_batch_ingress(true);
        check_logic9_same_update_multiowner_group_fallback();
        check_forced_narrow_logic9_boundary_keeps_checked_fallback();
        std::cout << "Logic9 packed slot allocation test passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Logic9 packed slot allocation test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
