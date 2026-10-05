// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "runtime_fused_staging_failure_support.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

void test_checked_two_output_prefix_fallback();
void test_scheduler_atomic_batch_preflight_failures();

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

struct StaticCheckedFallbackOutcome {
    Observation observation;
    bool native_candidate_available { };
    std::uint64_t native_invocations { };
};

StaticCheckedFallbackOutcome run_static_checked_fallback_case()
{
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    const auto first_input = interpreter.add_signal(
        { "first_input", PackedLogic4(32U, Logic4::zero) });
    const auto second_input = interpreter.add_signal(
        { "second_input", PackedLogic4(33U, Logic4::zero) });
    const auto target = interpreter.add_signal(
        { "target", PackedLogic4(129U, Logic4::z),
            ResolutionKind::sv_wire });

    const auto add_owner = [&](const ProcessId id, const char* const name,
                               const SignalId input, const std::uint32_t offset,
                               const std::uint32_t width,
                               const std::uint32_t input_width) {
        Process process;
        process.id = id;
        process.name = name;
        process.register_count = width == input_width ? 1U : 2U;
        process.static_sensitivity = { { input, EdgeKind::any } };
        process.driver_regions = { { target, offset, width, false } };
        process.operations = { ReadSignal { 0U, input } };
        RegisterId source = 0U;
        if (width != input_width) {
            source = 1U;
            process.operations.emplace_back(
                Extract { source, 0U, 0U, width });
        }
        process.operations.emplace_back(
            WriteUpdateSlice { target, source, offset });
        process.operations.emplace_back(WaitSensitivity { });
        process.operations.emplace_back(Jump { 0U });
        return interpreter.add_process(std::move(process));
    };
    const auto first_owner = add_owner(
        0U, "first_owner", first_input, 0U, 32U, 32U);
    const auto first_sibling = add_owner(
        1U, "first_sibling", first_input, 32U, 32U, 32U);
    const auto second_owner_low = add_owner(
        2U, "second_owner_low", second_input, 64U, 32U, 33U);
    const auto second_owner_high = add_owner(
        3U, "second_owner_high", second_input, 96U, 33U, 33U);

    Process observer;
    observer.id = 4U;
    observer.name = "target_observer";
    observer.initialize = false;
    observer.static_sensitivity = { { target, EdgeKind::any } };
    observer.operations = {
        Display { "target" }, WaitSensitivity { }, Jump { 0U }
    };
    (void)interpreter.add_process(std::move(observer));

    const PackedLogic4 first_value(32U, Logic4::one);
    const PackedLogic4 second_value(33U, Logic4::one);
    interpreter.schedule_signal_at(first_input, first_value, 1U, 0U);
    interpreter.schedule_signal_at(second_input, second_value, 2U, 0U);

    Observation result;
    interpreter.set_output_hook(
        [&](ProcessId, const std::string_view text, bool,
            const SimulationTick time, const std::uint64_t delta) {
            if (text == "target") {
                result.publications.emplace_back(time, delta,
                    interpreter.signal_value(target).to_msb_string());
            }
        });
    interpreter.start();
    const auto candidates = interpreter.fused_static_cohort_candidates();
    const auto first_pair = std::ranges::find_if(candidates,
        [&](const auto& value) {
            return value.members
                == std::vector<ProcessId> { first_owner, first_sibling };
        });
    const bool native_candidate_available = first_pair != candidates.end();
    require(!native_candidate_available,
        "the observed multi-owner output remains on checked execution");

    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "the checked static owners reach their initial sensitivity wait");

    AllocationFailureWindow publication_window;
    publication_window.observed_signals[0U] = target;
    interpreter.scheduler().set_trace_hook(
        &publication_window, AllocationFailureWindow::trace);

    const auto first_update = interpreter.run(1U);
    require(first_update.status == RunStatus::completed
            || first_update.status == RunStatus::time_limit,
        "the first checked owner group publishes without native staging");
    const auto second_update = interpreter.run(2U);
    require(second_update.status == RunStatus::completed
            || second_update.status == RunStatus::time_limit,
        "the second checked owner group completes the output");

    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(!publication_window.publications.overflow,
        "checked fallback publication trace fits its fixed storage");
    result.publication_trace = publication_window.publications;
    result.signal = interpreter.signal_value_snapshot(target).to_msb_string();
    result.drivers = {
        interpreter.driver_value(first_owner, target).to_msb_string(),
        interpreter.driver_value(first_sibling, target).to_msb_string(),
        interpreter.driver_value(second_owner_low, target).to_msb_string(),
        interpreter.driver_value(second_owner_high, target).to_msb_string(),
    };
    const auto native_invocations
        = interpreter.fused_static_counters().invocations;
    require(native_invocations == 0U,
        "the observer-constrained fixture completes through checked members");
    return { std::move(result), native_candidate_available,
        native_invocations };
}


class ScopedA4GroupEnvironment final {
public:
    ScopedA4GroupEnvironment(const char* const name, const char* const value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            previous_ = previous;
        }
        set(value);
    }

    ScopedA4GroupEnvironment(const ScopedA4GroupEnvironment&) = delete;
    ScopedA4GroupEnvironment& operator=(
        const ScopedA4GroupEnvironment&) = delete;

    ~ScopedA4GroupEnvironment()
    {
        if (previous_) {
            set(previous_->c_str());
        } else {
            unset();
        }
    }

private:
    void set(const char* const value) const noexcept
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name_.c_str(), value));
#else
        static_cast<void>(::setenv(name_.c_str(), value, 1));
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
    std::optional<std::string> previous_;
};

class A4GroupExecutor final : public FusedStaticCohortExecutor {
public:
    A4GroupExecutor(const SignalId input, const SignalId output,
        std::size_t& calls)
        : input_ { input }
        , output_ { output }
        , calls_ { calls }
    {
        mask_[0U] = std::numeric_limits<std::uint64_t>::max();
        mask_[1U] = std::numeric_limits<std::uint64_t>::max();
        mask_[2U] = 1U;
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext& context) override
    {
        ++calls_;
        constexpr std::size_t width = 129U;
        const auto offset = context.wide_signal_offsets[input_];
        const auto value = PackedLogic4::from_word_planes(width,
            context.wide_signal_aval.subspan(offset, 3U),
            context.wide_signal_bval.subspan(offset, 3U));
        std::ranges::copy(value.aval_words(), aval_.begin());
        std::ranges::copy(value.bval_words(), bval_.begin());
        active_ = 1U;
        slot_ = { output_, static_cast<std::uint32_t>(width), 3U, &active_,
            aval_.data(), bval_.data(), mask_.data() };
        return FusedStaticCohortResume { std::span { &slot_, 1U }, { } };
    }

private:
    SignalId input_;
    SignalId output_;
    std::size_t& calls_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 3U> aval_ { };
    std::array<std::uint64_t, 3U> bval_ { };
    std::array<std::uint64_t, 3U> mask_ { };
    ProcessUpdateSlotView slot_ { };
};

struct A4GroupPreflightSnapshot {
    std::string current;
    std::string previous;
    std::string stored;
    std::array<std::string, 2U> raw_owners;
    std::string a4_current;
    std::string a4_previous;
    std::string a4_stored;
    std::array<std::string, 2U> a4_owners;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    ProcessSchedulingDomain event_domain { };
    SchedulerPhase event_phase { };
    std::uint64_t event_round { };
    std::uint64_t value_revision { };

    bool operator==(const A4GroupPreflightSnapshot& other) const
    {
        // A4 revision is component-wide and input/trigger updates can advance
        // it even when this target has not published. Compare exact target
        // roles and signal revision across checked and grouped routes.
        return current == other.current
            && previous == other.previous
            && stored == other.stored
            && raw_owners == other.raw_owners
            && a4_current == other.a4_current
            && a4_previous == other.a4_previous
            && a4_stored == other.a4_stored
            && a4_owners == other.a4_owners
            && event == other.event
            && transaction == other.transaction
            && event_domain == other.event_domain
            && event_phase == other.event_phase
            && event_round == other.event_round
            && value_revision == other.value_revision;
    }
};

struct A4GroupPreflightTrial {
    bool threw { };
    bool injected { };
    bool first_completed { };
    bool later_completed { };
    bool group_layout_bound { };
    bool a4_group_route { };
    bool no_owned_composite { };
    bool preflight_fallback { };
    std::size_t calls_before { };
    std::size_t calls_after_first { };
    std::size_t calls_after_later { };
    A4GroupPreflightSnapshot before_first;
    A4GroupPreflightSnapshot after_first;
    A4GroupPreflightSnapshot after_later;
};

[[nodiscard]] A4GroupPreflightTrial run_a4_group_case(
    const std::optional<std::size_t> failure_index,
    const bool install_executor)
{
    ScopedA4GroupEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedA4GroupEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedA4GroupEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "1" };
    ScopedA4GroupEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    constexpr std::uint32_t width = 129U;
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    auto first_input = PackedLogic4 { width, Logic4::zero };
    auto later_input = PackedLogic4 { width, Logic4::zero };
    first_input.set(0U, Logic4::one);
    first_input.set(63U, Logic4::x);
    first_input.set(64U, Logic4::one);
    first_input.set(128U, Logic4::x);
    later_input.set(1U, Logic4::one);
    later_input.set(62U, Logic4::x);
    later_input.set(65U, Logic4::one);
    later_input.set(127U, Logic4::x);
    const auto input = interpreter.add_signal({ "group_input",
        PackedLogic4 { width, Logic4::zero } });
    const auto trigger = interpreter.add_signal({
        "group_trigger", PackedLogic4 { 1U, Logic4::zero } });
    const auto output = interpreter.add_signal({
        "group_output", PackedLogic4 { width, Logic4::z },
        ResolutionKind::sv_wire });

    const auto add_owner = [&](const ProcessId process_id,
                               const std::uint32_t target_offset,
                               const std::uint32_t source_offset,
                               const std::uint32_t slice_width) {
        Process process;
        process.id = process_id;
        process.name = "a4_group_owner_" + std::to_string(process_id);
        process.register_count = 3U;
        process.static_sensitivity = { { trigger, EdgeKind::any } };
        process.driver_regions = {
            { output, target_offset, slice_width, false }
        };
        process.operations = {
            ReadSignal { 0U, input },
            Extract { 1U, 0U, source_offset, slice_width },
            ReadSignal { 2U, trigger },
            WriteUpdateSlice { output, 1U, target_offset,
                SignalUpdateDomain::generic },
            WaitSensitivity { }, Jump { 0U }
        };
        require(interpreter.add_process(std::move(process)) == process_id,
            "A4 preflight owners retain their certified process identities");
    };
    add_owner(0U, 0U, 0U, 64U);
    add_owner(1U, 64U, 64U, 65U);

    interpreter.start();
    const auto candidates = interpreter.fused_static_cohort_candidates();
    const auto candidate = std::ranges::find_if(candidates,
        [output](const auto& value) {
            return value.members == std::vector<ProcessId> { 0U, 1U }
                && value.outputs == std::vector<SignalId> { output };
        });
    require(candidate != candidates.end(),
        "the generic disjoint writers form a fused static cohort");
    std::size_t executor_calls { };
    if (install_executor) {
        interpreter.install_fused_static_cohort(candidate->cohort_id,
            std::make_unique<A4GroupExecutor>(input, output, executor_calls));
    }

    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "A4 preflight owners reach their initial wait");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* const state
        = implementation.region_authoritative_state_for_signal(output);
    require(state != nullptr && state->values().layout().contains(output),
        "the A4 preflight target has an authoritative owner layout");
    const auto& layout = state->values().layout();
    const auto owners = layout.owners(output);
    const bool group_layout_bound = owners.size() == 2U
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, output, 0U)
        && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
            interpreter, output, 1U);
    const bool no_owned_composite
        = !implementation.owned_driver_active(output);
    require(group_layout_bound && no_owned_composite,
        "the preflight failure fixture selects bound A4 disjoint owners, not the legacy composite adapter");
    const auto& plan = implementation.fused_static_cohorts.at(
        candidate->cohort_id);
    require(candidate->outputs.size() == 1U
            && candidate->outputs.front() == output
            && plan.outputs.size() == 1U
            && plan.outputs.front().signal == output
            && plan.outputs.front().route
                == decltype(plan.outputs.front().route)::disjoint_owner_group_logic4
            && !plan.outputs.front().disjoint_owners.empty(),
        "the fixture uses the certified disjoint-owner group plan");
    const auto& staged_value
        = plan.outputs.front().disjoint_owners.front().staged_value;
    require(staged_value.width() == width,
        "the preflight owner scratch has the full wide Logic4 shape");
    // Keep one shared owner of the plan's three-word value alive across the
    // preflight. This exercises PackedLogic4's supported COW detach without
    // mutating runtime state, targeting the pre-callback reset at
    // simir_fused_static_graph.cpp: the staged value must be writable before
    // the optional native executor is entered.
    const auto retained_preflight_value_lease = staged_value;
    require(retained_preflight_value_lease.aval_words().data()
                == staged_value.aval_words().data()
            && retained_preflight_value_lease.bval_words().data()
                == staged_value.bval_words().data(),
        "the retained preflight lease shares the plan value backing");

    const auto capture = [&]() {
        auto* const current_state
            = implementation.region_authoritative_state_for_signal(output);
        require(current_state != nullptr,
            "the checked and grouped paths retain the target A4 state");
        const auto* const owner0 = implementation.driver_values.at(output)
            .find(0U);
        const auto* const owner1 = implementation.driver_values.at(output)
            .find(1U);
        require(owner0 != nullptr && owner1 != nullptr,
            "both original raw owner rows remain present");
        const auto stamp
            = implementation.signal_event_scheduling_stamps.at(output);
        return A4GroupPreflightSnapshot {
            implementation.signals.at(output).initial_value.to_msb_string(),
            implementation.signal_last_values.at(output).to_msb_string(),
            implementation.driven_values.at(output).to_msb_string(),
            { owner0->value.to_msb_string(), owner1->value.to_msb_string() },
            current_state->values().current(output).to_msb_string(),
            current_state->values().previous(output).to_msb_string(),
            current_state->values().stored(output).to_msb_string(),
            { current_state->values().owner_value(output, 0U).to_msb_string(),
                current_state->values().owner_value(output, 1U).to_msb_string() },
            implementation.signal_events.at(output),
            implementation.signal_transactions.at(output),
            stamp.origin.process_domain,
            stamp.origin.phase,
            stamp.systemverilog_round,
            implementation.signal_value_revisions.at(output)
        };
    };

    interpreter.schedule_signal_at(input, first_input, 1U, 0U);
    interpreter.schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::one }, 1U, 0U);
    const auto counters_before = interpreter.fused_static_counters();
    A4GroupPreflightTrial result;
    result.group_layout_bound = group_layout_bound;
    result.a4_group_route = candidate->outputs.size() == 1U
        && candidate->outputs.front() == output
        && plan.outputs.size() == 1U
        && plan.outputs.front().signal == output
        && plan.outputs.front().route
            == decltype(plan.outputs.front().route)::disjoint_owner_group_logic4;
    result.no_owned_composite = no_owned_composite;
    result.calls_before = executor_calls;
    result.before_first = capture();
    if (failure_index) {
        arm_allocation_failure(*failure_index);
    }
    RunStatus first_status { RunStatus::completed };
    try {
        first_status = interpreter.run(1U).status;
        result.first_completed = first_status == RunStatus::completed
            || first_status == RunStatus::time_limit;
    } catch (const std::bad_alloc&) {
        result.threw = true;
    }
    result.injected = allocation_failure_was_injected();
    clear_allocation_failure();
    result.calls_after_first = executor_calls;
    result.after_first = capture();
    const auto counters_after = interpreter.fused_static_counters();
    result.preflight_fallback = counters_after.fallbacks
        > counters_before.fallbacks;
    if (result.threw || !result.first_completed) {
        return result;
    }

    interpreter.schedule_signal_at(input, later_input, 2U, 0U);
    interpreter.schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::zero }, 2U, 0U);
    const auto later_status = interpreter.run(2U).status;
    result.later_completed = later_status == RunStatus::completed
        || later_status == RunStatus::time_limit;
    result.calls_after_later = executor_calls;
    result.after_later = capture();
    return result;
}

void test_a4_group_preflight_failure_falls_back_before_executor()
{
    const auto checked = run_a4_group_case(std::nullopt, false);
    require(checked.first_completed && checked.later_completed,
        "checked A4 reference completes both activations");
    require(checked.after_first.current != checked.before_first.current,
        "the first checked activation changes the target value");

    bool found_preflight_failure { };
    for (std::size_t failure_index = 0U; failure_index < 128U;
         ++failure_index) {
        const auto trial
            = run_a4_group_case(failure_index, true);
        if (!trial.injected) {
            break;
        }
        if (trial.threw || !trial.first_completed
            || trial.calls_after_first != trial.calls_before
            || !trial.preflight_fallback) {
            continue;
        }
        require(trial.a4_group_route && trial.group_layout_bound
                && trial.no_owned_composite,
            "the injected decline was on the certified A4 group route");
        require(trial.after_first == checked.after_first,
            "A4 preflight decline executes the checked owner writes and publication");
        require(trial.later_completed
                && trial.calls_after_later == trial.calls_before + 1U,
            "a later activation reaches the native executor after the preflight decline");
        require(trial.after_later == checked.after_later,
            "later native activation preserves checked visible, raw-owner, and A4 role state");
        found_preflight_failure = true;
        break;
    }
    require(found_preflight_failure,
        "bounded failpoint sweep reaches a caught A4 group preflight allocation before the callback");
}

void test_static_checked_fallback_traces()
{
    ScopedA4GroupEnvironment region_kernel {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedA4GroupEnvironment single_owner_disabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedA4GroupEnvironment disjoint_owner_enabled {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "1" };
    ScopedA4GroupEnvironment local_wave_disabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    auto expected_final = PackedLogic4(129U, Logic4::one);
    auto first_update = PackedLogic4(129U, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < 64U; ++bit) {
        first_update.set(bit, Logic4::one);
    }

    const auto expected_owner = [](const std::uint32_t offset,
                                   const std::uint32_t width) {
        auto value = PackedLogic4(129U, Logic4::z);
        for (std::uint32_t bit = offset; bit < offset + width; ++bit) {
            value.set(bit, Logic4::one);
        }
        return value.to_msb_string();
    };
    const std::vector<std::string> expected_drivers {
        expected_owner(0U, 32U),
        expected_owner(32U, 32U),
        expected_owner(64U, 32U),
        expected_owner(96U, 33U)
    };

    const auto a4_checked = run_static_checked_fallback_case();
    StaticCheckedFallbackOutcome composite_checked;
    {
        ScopedA4GroupEnvironment disjoint_owner_disabled {
            "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
        composite_checked = run_static_checked_fallback_case();
    }
    const auto& a4 = a4_checked.observation;
    const auto& composite = composite_checked.observation;
    require(!a4_checked.native_candidate_available
            && !composite_checked.native_candidate_available
            && a4_checked.native_invocations == 0U
            && composite_checked.native_invocations == 0U,
        "observer and trace barriers keep policy-enabled and policy-disabled runs on checked execution");
    require(a4.signal == expected_final.to_msb_string()
            && a4.drivers == expected_drivers
            && composite.signal == a4.signal
            && composite.drivers == a4.drivers,
        "checked fallback preserves final value and every original owner slice");
    require(a4.publications == composite.publications
            && !a4.publications.empty()
            && a4.publication_trace == composite.publication_trace,
        "checked observer fallback preserves order and transaction/change trace under both A4 policies");
    const auto has_publication_at = [&](const auto& observation,
                                        const SimulationTick time,
                                        const std::string& value) {
        return std::ranges::any_of(observation.publications,
            [&](const auto& publication) {
                return std::get<0>(publication) == time
                    && std::get<2>(publication) == value;
            });
    };
    require(has_publication_at(a4, 1U, first_update.to_msb_string())
            && has_publication_at(a4, 2U, expected_final.to_msb_string()),
        "the observer sees the staged low and completed full-width owner updates");
    const auto has_event = [&](const auto& observation,
                               const SchedulerTraceKind kind,
                               const SimulationTick time) {
        for (std::size_t index = 0U;
             index < observation.publication_trace.count; ++index) {
            const auto& event = observation.publication_trace.records[index];
            if (event.kind == kind && event.time == time) {
                return true;
            }
        }
        return false;
    };
    for (const auto time : { SimulationTick { 1U }, SimulationTick { 2U } }) {
        require(has_event(a4, SchedulerTraceKind::signal_transaction, time)
                && has_event(a4, SchedulerTraceKind::signal_change, time),
            "each checked owner update retains its transaction and value-change records");
    }
}


} // namespace

void test_fused_staging_allocation_failures()
{
    test_scheduler_atomic_batch_preflight_failures();
    test_static_checked_fallback_traces();
    test_a4_group_preflight_failure_falls_back_before_executor();
    test_checked_two_output_prefix_fallback();
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_fused_staging_allocation_failures();
        std::cout << "fused staging allocation failure tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        fsim::tests::runtime::staging_failure_support::clear_allocation_failure();
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
