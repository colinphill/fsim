// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_a4_signal_state.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct OwnedDriverDemotionTestAccess {
    static auto& implementation(Interpreter& interpreter)
    {
        return *interpreter.impl_;
    }

    static void add_empty_process(Interpreter& interpreter,
        const ProcessId id)
    {
        Interpreter::Impl::ProcessState process;
        process.id = id;
        implementation(interpreter).processes.push_back(std::move(process));
    }
};

} // namespace fsim::runtime::simir

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

struct PendingSnapshot {
    std::optional<ProcessId> driver;
    PackedLogic4 value;
    bool owned_composite { };
    std::optional<std::size_t> fused_cohort;
};

struct DriverSnapshot {
    ProcessId process { };
    PackedLogic4 value;
};

struct DemotionSnapshot {
    bool active { };
    bool phase_active { };
    PackedLogic4 committed;
    PackedLogic4 phase;
    PackedLogic4 current;
    PackedLogic4 stored;
    PackedLogic4 last;
    bool resolved_marked { };
    std::vector<SignalId> update_signals;
    std::vector<SignalId> resolved_signals;
    std::vector<DriverSnapshot> drivers;
    std::vector<PendingSnapshot> staged;
};

bool operator==(const PendingSnapshot& left, const PendingSnapshot& right)
{
    return left.driver == right.driver && left.value == right.value
        && left.owned_composite == right.owned_composite
        && left.fused_cohort == right.fused_cohort;
}

bool operator==(const DriverSnapshot& left, const DriverSnapshot& right)
{
    return left.process == right.process && left.value == right.value;
}

bool operator==(const DemotionSnapshot& left, const DemotionSnapshot& right)
{
    return left.active == right.active
        && left.phase_active == right.phase_active
        && left.committed == right.committed
        && left.phase == right.phase
        && left.current == right.current
        && left.stored == right.stored
        && left.last == right.last
        && left.resolved_marked == right.resolved_marked
        && left.update_signals == right.update_signals
        && left.resolved_signals == right.resolved_signals
        && left.drivers == right.drivers
        && left.staged == right.staged;
}

struct DemotionCase {
    Interpreter interpreter { };
    SignalId signal { };
};

PackedLogic4 make_driver_value(const std::size_t offset,
    const std::size_t width, const Logic4 fill, const std::size_t one_bit,
    const std::size_t x_bit)
{
    auto value = PackedLogic4 { 129U, Logic4::z };
    auto owned = PackedLogic4 { width, fill };
    owned.set(one_bit, Logic4::one);
    owned.set(x_bit, Logic4::x);
    value.insert_bits(owned, offset);
    return value;
}

DemotionCase make_demotion_case()
{
    auto result = DemotionCase { };
    result.signal = result.interpreter.add_signal(Signal {
        "demotion_target", PackedLogic4 { 129U, Logic4::z },
        ResolutionKind::sv_wire
    });

    auto& impl = OwnedDriverDemotionTestAccess::implementation(
        result.interpreter);
    constexpr ProcessId first_owner = 0U;
    constexpr ProcessId second_owner = 1U;
    OwnedDriverDemotionTestAccess::add_empty_process(
        result.interpreter, first_owner);
    OwnedDriverDemotionTestAccess::add_empty_process(
        result.interpreter, second_owner);
    impl.owned_driver_composites.resize(
        static_cast<std::size_t>(result.signal) + 1U);
    impl.owned_driver_spans.resize(2U);
    impl.driver_update_scratch.resize(
        static_cast<std::size_t>(result.signal) + 1U);
    impl.driver_update_signals.push_back(result.signal);
    impl.resolved_update_marked.resize(
        static_cast<std::size_t>(result.signal) + 1U, false);
    impl.resolved_update_marked[result.signal] = true;
    impl.resolved_update_signals.push_back(result.signal);

    const auto first_value
        = make_driver_value(0U, 64U, Logic4::zero, 3U, 61U);
    const auto second_value
        = make_driver_value(64U, 65U, Logic4::zero, 2U, 63U);
    require(impl.driver_values[result.signal].insert_if_absent(
        DriverRecord { first_owner, first_value, DriveStrength { } }),
        "the first owner driver is installed for demotion coverage");
    require(impl.driver_values[result.signal].insert_if_absent(
        DriverRecord { second_owner, second_value, DriveStrength { } }),
        "the second owner driver is installed for demotion coverage");

    auto committed = PackedLogic4 { 129U, Logic4::z };
    committed.insert_bits(first_value.extract_bits(0U, 64U), 0U);
    committed.insert_bits(second_value.extract_bits(64U, 65U), 64U);
    auto phase = committed;
    const auto next_second
        = PackedLogic4 { 65U, Logic4::one };
    phase.insert_bits(next_second, 64U);

    auto& owned = impl.owned_driver_composites[result.signal];
    owned.committed = committed;
    owned.phase = phase;
    owned.active = true;
    owned.phase_active = true;
    impl.owned_driver_spans[first_owner] = {
        result.signal, 0U, 64U
    };
    impl.owned_driver_spans[second_owner] = {
        result.signal, 64U, 65U
    };

    auto ordinary_pending = make_driver_value(
        0U, 64U, Logic4::one, 17U, 48U);
    impl.driver_update_scratch[result.signal].emplace_back(
        first_owner, std::move(ordinary_pending));
    impl.driver_update_scratch[result.signal].emplace_back(
        second_owner, PackedLogic4 { }, true);
    return result;
}

DemotionSnapshot snapshot(DemotionCase& test_case)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(
        test_case.interpreter);
    const auto& owned = impl.owned_driver_composites[test_case.signal];
    auto result = DemotionSnapshot {
        owned.active, owned.phase_active,
        owned.committed, owned.phase,
        impl.signals[test_case.signal].initial_value,
        impl.driven_values[test_case.signal],
        impl.signal_last_values[test_case.signal],
        impl.resolved_update_marked[test_case.signal] != 0U,
        impl.driver_update_signals, impl.resolved_update_signals,
        { }, { }
    };
    impl.driver_values[test_case.signal].for_each_in_process_order(
        [&](const DriverRecord& record) {
            result.drivers.push_back({ record.process, record.value });
        });
    for (const auto& pending : impl.driver_update_scratch[test_case.signal]) {
        result.staged.push_back({ pending.driver, pending.value,
            pending.owned_composite, pending.fused_cohort });
    }
    return result;
}

void demote(DemotionCase& test_case)
{
    OwnedDriverDemotionTestAccess::implementation(
        test_case.interpreter).demote_owned_driver(test_case.signal);
}

std::pair<DemotionSnapshot, std::size_t> demote_without_failure()
{
    auto test_case = make_demotion_case();
    begin_allocation_count();
    demote(test_case);
    const auto allocation_count = end_allocation_count();
    return { snapshot(test_case), allocation_count };
}

void verify_demoted_state(const DemotionSnapshot& actual,
    const DemotionSnapshot& expected)
{
    require(actual == expected,
        "retry after a failed mixed owned/ordinary demotion matches success");
    require(!actual.active && !actual.phase_active,
        "successful retry publishes the demoted ownership state");
    require(actual.drivers.size() == 2U,
        "successful retry retains both original driver identities");
    require(actual.staged.size() == 2U,
        "successful retry retains ordinary work and expands the owner marker");
    require(!actual.staged[0].owned_composite
            && actual.staged[0].driver == ProcessId { 0U }
            && actual.staged[0].value.get(17U) == Logic4::one,
        "successful retry preserves the original ordinary pending update");
    require(!actual.staged[1].owned_composite
            && actual.staged[1].driver == ProcessId { 1U }
            && actual.staged[1].value.get(64U) == Logic4::one
            && actual.staged[1].value.get(127U) == Logic4::one,
        "successful retry materializes the owned marker from its staged phase");
    require(actual.drivers[0].process == ProcessId { 0U }
            && actual.drivers[0].value.get(3U) == Logic4::one
            && actual.drivers[0].value.get(17U) == Logic4::zero,
        "successful retry restores the first original driver value");
    require(actual.drivers[1].process == ProcessId { 1U }
            && actual.drivers[1].value.get(66U) == Logic4::one
            && actual.drivers[1].value.get(127U) == Logic4::x,
        "successful retry restores the second original driver value");
}

class ScopedRegionEnvironment final {
public:
    ScopedRegionEnvironment()
    {
        if (const auto* value = std::getenv("FSIM_ENABLE_SV_REGION_KERNEL")) {
            previous_ = value;
        }
        require(set("1") == 0, "A4 fixture environment is available");
    }

    ~ScopedRegionEnvironment()
    {
        if (previous_) {
            static_cast<void>(set(previous_->c_str()));
        } else {
#if defined(_WIN32)
            static_cast<void>(::_putenv_s("FSIM_ENABLE_SV_REGION_KERNEL", ""));
#else
            static_cast<void>(::unsetenv("FSIM_ENABLE_SV_REGION_KERNEL"));
#endif
        }
    }

private:
    static int set(const char* value) noexcept
    {
#if defined(_WIN32)
        const auto result = ::_putenv_s("FSIM_ENABLE_SV_REGION_KERNEL", value);
#else
        const auto result = ::setenv("FSIM_ENABLE_SV_REGION_KERNEL", value, 1);
#endif
        return result;
    }

    std::optional<std::string> previous_;
};

void check_authoritative_observation(const bool hook_first)
{
    ScopedRegionEnvironment enabled;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "authority.input",
        PackedLogic4(4U, Logic4::zero) });
    const auto internal = interpreter.add_signal({ "authority.internal",
        PackedLogic4(4U, Logic4::z), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "authority.output",
        PackedLogic4(4U, Logic4::z), ResolutionKind::sv_wire });
    for (ProcessId owner = 0U; owner < 2U; ++owner) {
        Process process;
        process.id = owner;
        process.name = "authority_member_" + std::to_string(owner);
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.register_count = 1U;
        const auto source = owner == 0U ? input : internal;
        const auto target = owner == 0U ? internal : output;
        process.static_sensitivity = { { source, EdgeKind::any } };
        process.driver_regions = { { target, 0U, 0U, true } };
        process.operations = { ReadSignal { 0U, source } };
        if (owner == 1U) {
            process.operations.push_back(UnaryNot { 0U, 0U });
        }
        process.operations.push_back(WriteUpdate { target, 0U,
            SignalUpdateDomain::systemverilog_active });
        process.operations.push_back(WaitSensitivity { });
        process.operations.push_back(Jump { 0U });
        static_cast<void>(interpreter.add_process(std::move(process)));
    }
    Process stimulus;
    stimulus.id = 2U;
    stimulus.name = "authority_stimulus";
    stimulus.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    stimulus.register_count = 1U;
    stimulus.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string("1111") },
        WriteBlocking { input, 0U }, WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4::from_msb_string("1x0z") },
        WriteBlocking { input, 0U }, WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4::from_msb_string("0010") },
        WriteBlocking { input, 0U }, Halt { },
    };
    static_cast<void>(interpreter.add_process(std::move(stimulus)));
    require(interpreter.run(1U).status == RunStatus::time_limit,
        "authority fixture pauses after a complete time slot with future work");

    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    auto* state = impl.region_authoritative_state_for_signal(internal);
    require(state != nullptr && state->values().layout().contains(output),
        "the checked runtime admits the complete unobserved component");
    // Keep the old bank alive while comparing binding addresses after demotion.
    const auto retained_bank = impl.region_authoritative_state_by_component.at(
        impl.region_authoritative_component_by_signal.at(internal));
    require(retained_bank.get() == state, "component identity is exact");
    std::array<std::span<const std::uint64_t>, 4U> internal_planes;
    std::array<std::span<const std::uint64_t>, 4U> output_planes;
    require(state->values().packed_signal_slots_bound(internal)
            && state->values().packed_signal_slots_bound(output)
            && state->values().packed_owner_slot_bound(internal, 0U)
            && state->values().packed_owner_slot_bound(output, 1U)
            && state->values().current_planes(internal, internal_planes)
            && state->values().current_planes(output, output_planes)
            && impl.signals[internal].initial_value.aval_words().data()
                == internal_planes[0U].data()
            && impl.signals[output].initial_value.aval_words().data()
                == output_planes[0U].data(),
        "runtime signal slots read the actual authoritative current planes");
    const auto current = impl.signals[internal].initial_value;
    const auto previous = impl.signal_last_values[internal];
    const auto stored = impl.driven_values[internal];
    const auto* driver = impl.driver_values[internal].find(0U);
    require(driver != nullptr, "the original owner record remains addressable");
    const auto raw = driver->value;
    require(current == PackedLogic4::from_msb_string("1x0z")
            && previous == PackedLogic4::from_msb_string("1111")
            && stored == current && raw == current,
        "copying a bound value preserves every exact role before materialization");
    const auto event = impl.signal_events[internal];
    const auto transaction = impl.signal_transactions[internal];
    const auto stamp = impl.signal_event_scheduling_stamps[internal];
    const auto pending_count = impl.pending_updates.size();
    const auto pending_values = impl.pending_update_values.size();

    std::size_t observed { };
    const auto observe = [&](const SignalId signal, const PackedLogic4& value,
                             const SimulationTick) {
        if (signal != internal) {
            return;
        }
        ++observed;
        require(interpreter.signal_value(internal) == value
                && interpreter.stored_signal_value(internal) == value
                && interpreter.driver_value(0U, internal) == value,
            "a late observer can reenter current, stored and original-driver reads");
    };
    if (hook_first) {
        interpreter.set_signal_change_hook(observe);
        require(!retained_bank->values().packed_slots_bound()
                && impl.signals[internal].initial_value.aval_words().data()
                    != internal_planes[0U].data()
                && impl.signals[output].initial_value.aval_words().data()
                    != output_planes[0U].data(),
            "late hook registration materializes its component before returning");
    }

    begin_allocation_count();
    const auto* retained_current = &interpreter.signal_value(internal);
    const auto* retained_stored = &interpreter.stored_signal_value(internal);
    const auto observation_allocations = end_allocation_count();
    require(observation_allocations == 0U,
        "fixed narrow component observation needs no allocation");
    require(!retained_bank->values().packed_slots_bound()
            && *retained_current == current && *retained_stored == stored
            && impl.signal_last_values[internal] == previous
            && interpreter.driver_value(0U, internal) == raw
            && impl.signals[internal].initial_value.aval_words().data()
                != internal_planes[0U].data()
            && impl.signals[output].initial_value.aval_words().data()
                != output_planes[0U].data(),
        "observation materializes the complete component and all four value roles");
    require(impl.signal_events[internal] == event
            && impl.signal_transactions[internal] == transaction
            && impl.signal_event_scheduling_stamps[internal].origin.process_domain
                == stamp.origin.process_domain
            && impl.signal_event_scheduling_stamps[internal].origin.phase
                == stamp.origin.phase
            && impl.signal_event_scheduling_stamps[internal].systemverilog_round
                == stamp.systemverilog_round
            && impl.pending_updates.size() == pending_count
            && impl.pending_update_values.size() == pending_values,
        "materialization neither publishes pending work nor changes event metadata");
    if (!hook_first) {
        interpreter.set_signal_change_hook(observe);
    }
    require(interpreter.run().status == RunStatus::completed && observed != 0U,
        "execution resumes through the late observer after materialization");
    require(*retained_current == PackedLogic4::from_msb_string("0010")
            && *retained_stored == *retained_current
            && current == PackedLogic4::from_msb_string("1x0z")
            && impl.signal_last_values[internal] == current,
        "retained public references update while the detached snapshot stays exact");
    interpreter.set_signal_change_hook({ });
    interpreter.force_signal(internal, PackedLogic4(4U, Logic4::zero));
    require(interpreter.run().status == RunStatus::completed
            && *retained_current == PackedLogic4(4U, Logic4::zero)
            && *retained_stored == PackedLogic4::from_msb_string("0010")
            && interpreter.driver_value(0U, internal) == *retained_stored,
        "late force keeps visible, stored and raw-owner values distinct");
    interpreter.release_signal(internal);
    require(interpreter.run().status == RunStatus::completed
            && *retained_current == *retained_stored
            && interpreter.signal_value(output)
                == PackedLogic4::from_msb_string("1101"),
        "release restores the original driver and exact dependent output");
}

void check_selective_authoritative_observation()
{
    ScopedRegionEnvironment enabled;
    Interpreter interpreter;
    std::array<std::array<SignalId, 3U>, 2U> signals;
    for (std::size_t component = 0U; component < signals.size(); ++component) {
        const auto prefix = "selective_" + std::to_string(component);
        signals[component][0U] = interpreter.add_signal({ prefix + ".input",
            PackedLogic4(4U, Logic4::zero) });
        signals[component][1U] = interpreter.add_signal({ prefix + ".internal",
            PackedLogic4(4U, Logic4::z), ResolutionKind::sv_wire });
        signals[component][2U] = interpreter.add_signal({ prefix + ".output",
            PackedLogic4(4U, Logic4::z), ResolutionKind::sv_wire });
    }
    for (ProcessId owner = 0U; owner < 4U; ++owner) {
        const auto component = static_cast<std::size_t>(owner / 2U);
        const auto member = static_cast<std::size_t>(owner % 2U);
        const auto source = signals[component][member];
        const auto target = signals[component][member + 1U];
        Process process;
        process.id = owner;
        process.name = "selective_member_" + std::to_string(owner);
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.register_count = 1U;
        process.static_sensitivity = { { source, EdgeKind::any } };
        process.driver_regions = { { target, 0U, 0U, true } };
        process.operations = { ReadSignal { 0U, source } };
        if (member == 1U) {
            process.operations.push_back(UnaryNot { 0U, 0U });
        }
        process.operations.push_back(WriteUpdate { target, 0U,
            SignalUpdateDomain::systemverilog_active });
        process.operations.push_back(WaitSensitivity { });
        process.operations.push_back(Jump { 0U });
        static_cast<void>(interpreter.add_process(std::move(process)));
    }
    for (ProcessId component = 0U; component < 2U; ++component) {
        Process stimulus;
        stimulus.id = 4U + component;
        stimulus.name = "selective_stimulus_" + std::to_string(component);
        stimulus.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        stimulus.register_count = 1U;
        stimulus.operations = {
            LoadConstant { 0U, PackedLogic4::from_msb_string("1111") },
            WriteBlocking { signals[component][0U], 0U }, WaitFor { 2U },
            LoadConstant { 0U, PackedLogic4::from_msb_string("1x0z") },
            WriteBlocking { signals[component][0U], 0U }, WaitFor { 1U },
            Halt { },
        };
        static_cast<void>(interpreter.add_process(std::move(stimulus)));
    }
    require(interpreter.run(1U).status == RunStatus::time_limit,
        "two independent components settle before observation");
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto observed_signal = signals[0U][1U];
    const auto unaffected_signal = signals[1U][1U];
    const auto observed_component
        = impl.region_authoritative_component_by_signal.at(observed_signal);
    const auto unaffected_component
        = impl.region_authoritative_component_by_signal.at(unaffected_signal);
    require(observed_component != unaffected_component,
        "the fixture contains independent certified components");
    const auto observed_bank
        = impl.region_authoritative_state_by_component.at(observed_component);
    const auto unaffected_bank
        = impl.region_authoritative_state_by_component.at(unaffected_component);
    require(observed_bank && unaffected_bank
            && observed_bank->values().packed_signal_slots_bound(observed_signal)
            && unaffected_bank->values().packed_signal_slots_bound(unaffected_signal),
        "both components begin with exact bound value slots");
    const auto* const unaffected_words
        = impl.signals[unaffected_signal].initial_value.aval_words().data();
    const auto unaffected_event = impl.signal_events[unaffected_signal];
    const auto unaffected_transaction = impl.signal_transactions[unaffected_signal];
    begin_allocation_count();
    const auto& visible = interpreter.signal_value(observed_signal);
    const auto allocations = end_allocation_count();
    require(allocations == 0U && visible == PackedLogic4::from_msb_string("1111"),
        "selective narrow observation materializes without allocation");
    require(!observed_bank->values().packed_slots_bound()
            && impl.region_recertification_pending
            && impl.region_authoritative_state_for_signal(observed_signal) == nullptr,
        "observation revokes and materializes its whole component immediately");
    require(unaffected_bank->values().packed_signal_slots_bound(unaffected_signal)
            && impl.region_authoritative_state_for_signal(unaffected_signal)
                == unaffected_bank.get()
            && impl.signals[unaffected_signal].initial_value.aval_words().data()
                == unaffected_words
            && impl.signal_events[unaffected_signal] == unaffected_event
            && impl.signal_transactions[unaffected_signal] == unaffected_transaction,
        "unrelated component backing and metadata remain valid while refresh is pending");
    require(interpreter.run(2U).status == RunStatus::time_limit,
        "both components continue through their next update and quiet point");
    auto* const refreshed = impl.region_authoritative_state_for_signal(unaffected_signal);
    require(visible == PackedLogic4::from_msb_string("1x0z")
            && refreshed != nullptr
            && refreshed->values().packed_signal_slots_bound(unaffected_signal)
            && impl.signals[unaffected_signal].initial_value
                == PackedLogic4::from_msb_string("1x0z")
            && impl.signals[signals[1U][2U]].initial_value
                == PackedLogic4::from_msb_string("0x1x")
            && impl.signal_last_values[unaffected_signal]
                == PackedLogic4::from_msb_string("1111")
            && impl.signal_events[unaffected_signal] != unaffected_event
            && impl.signal_transactions[unaffected_signal] != unaffected_transaction,
        "quiet-point recertification preserves exact observed and bound state");
}

} // namespace

void test_owned_driver_demote_failure_atomicity()
{
    check_selective_authoritative_observation();
    check_authoritative_observation(false);
    check_authoritative_observation(true);
    const auto [expected, allocation_count] = demote_without_failure();
    require(allocation_count != 0U,
        "mixed demotion performs allocations in its preparation phase");
    auto discovered_failures = std::size_t { 0U };
    auto reached_success = false;

    for (std::size_t index = 0U; index <= allocation_count; ++index) {
        auto test_case = make_demotion_case();
        const auto before = snapshot(test_case);
        auto failed = false;
        arm_allocation_failure(index);
        try {
            demote(test_case);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        clear_allocation_failure();

        if (!failed) {
            require(index == allocation_count,
                "the first non-failing index follows every demotion allocation");
            verify_demoted_state(snapshot(test_case), expected);
            reached_success = true;
            break;
        }

        require(index < allocation_count,
            "every counted demotion allocation is a fault-injection point");
        ++discovered_failures;
        require(snapshot(test_case) == before,
            "failed mixed demotion preserves drivers, marker, ordinary pending value, and phase");
        demote(test_case);
        verify_demoted_state(snapshot(test_case), expected);
    }

    require(discovered_failures != 0U,
        "the mixed owned/ordinary demotion sweep injects a real allocation failure");
    require(reached_success,
        "the mixed demotion sweep reaches the first non-failing allocation index");
    std::cout << "mixed owned demotion allocation requests: "
              << discovered_failures << '\n';
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_owned_driver_demote_failure_atomicity();
        std::cout << "owned driver demotion failure tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
