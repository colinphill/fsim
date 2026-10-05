// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] PackedLogic4 byte(const std::string_view bits)
{
    return PackedLogic4::from_msb_string(bits);
}

[[nodiscard]] ContainerType packed_array_type()
{
    ContainerType type;
    type.element_kind = ContainerElementKind::Packed;
    type.element_width = 8U;
    type.fixed = true;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    return type;
}

class WholeProxyWriter final : public ProcessExecutor {
public:
    using Write = std::function<void(ProcessExecutionContext&)>;

    WholeProxyWriter(Interpreter& interpreter, Write write)
        : interpreter_ { interpreter }
        , write_ { std::move(write) }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "whole proxy writer resumed away from its entry"
            };
        }
        write_(context);
        interpreter_.set_driver_change_hook({ });
        interpreter_.set_stored_signal_change_hook({ });
        interpreter_.set_signal_change_hook({ });
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Interpreter& interpreter_;
    Write write_;
};

struct ProxyArray {
    Interpreter interpreter;
    SignalId first { };
    SignalId second { };
    SignalId proxy { };
    ContainerObjectId object { };
    ProcessId writer { };
    ProcessId sibling { };

    ProxyArray()
    {
        const auto type = packed_array_type();
        first = interpreter.add_signal({
            "top.driver_array[1]", byte("ZZZZZZZZ"), ResolutionKind::sv_wire
        });
        second = interpreter.add_signal({
            "top.driver_array[0]", byte("ZZZZZZZZ"), ResolutionKind::sv_wire
        });
        proxy = interpreter.add_signal({
            "top.driver_array", byte("ZZZZZZZZZZZZZZZZ"),
            ResolutionKind::sv_wire
        });
        object = interpreter.add_container_object({
            "top.driver_array",
            ContainerValue { type, { byte("ZZZZZZZZ"), byte("ZZZZZZZZ") }, { } },
            std::nullopt
        });
        interpreter.add_container_element_signal_alias(
            { object, 0U, first, true, true });
        interpreter.add_container_element_signal_alias(
            { object, 1U, second, true, true });
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, true });

        Process writer_process;
        writer_process.id = 0U;
        writer_process.name = "top.driver_array.writer";
        writer_process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        writer_process.driver_regions = {
            { first, 0U, 0U, true }, { second, 0U, 0U, true }
        };
        writer_process.operations = { Halt { } };
        writer = interpreter.add_process(std::move(writer_process));

        Process sibling_process;
        sibling_process.id = 1U;
        sibling_process.name = "top.driver_array.sibling";
        sibling_process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        sibling_process.driver_regions = {
            { first, 0U, 0U, true }, { second, 0U, 0U, true }
        };
        sibling_process.operations = { Halt { } };
        sibling = interpreter.add_process(std::move(sibling_process));
    }

    void install_writer(WholeProxyWriter::Write write)
    {
        interpreter.set_process_executor(
            writer,
            std::make_unique<WholeProxyWriter>(interpreter, std::move(write)));
    }

    [[nodiscard]] std::string raw_writer_bytes() const
    {
        return interpreter.driver_value(writer, first).to_msb_string()
            + interpreter.driver_value(writer, second).to_msb_string();
    }

    [[nodiscard]] std::string current_bytes() const
    {
        return interpreter.signal_value(proxy).to_msb_string();
    }

    [[nodiscard]] std::string stored_bytes() const
    {
        return interpreter.stored_signal_value(proxy).to_msb_string();
    }
};

struct ThreeLeafProxyArray {
    Interpreter interpreter;
    std::array<SignalId, 3U> leaves { };
    SignalId proxy { };
    ContainerObjectId object { };
    ProcessId writer { };
    ProcessId sibling { };

    explicit ThreeLeafProxyArray(const bool ascending)
    {
        ContainerType type;
        type.element_kind = ContainerElementKind::Packed;
        type.element_width = 8U;
        type.fixed = true;
        type.index_left = ascending ? 0 : 2;
        type.index_right = ascending ? 2 : 0;
        type.dimensions = { { type.index_left, type.index_right } };

        std::vector<PackedLogic4> elements;
        elements.reserve(leaves.size());
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            const auto index = ascending
                ? static_cast<int>(ordinal)
                : 2 - static_cast<int>(ordinal);
            const auto name = "top.driver_array[" + std::to_string(index) + "]";
            leaves[ordinal] = interpreter.add_signal({
                name, byte("ZZZZZZZZ"), ResolutionKind::sv_wire });
            elements.emplace_back(byte("ZZZZZZZZ"));
        }
        proxy = interpreter.add_signal({
            "top.driver_array", PackedLogic4(24U, Logic4::z),
            ResolutionKind::sv_wire });
        object = interpreter.add_container_object({
            "top.driver_array",
            ContainerValue { type, std::move(elements), { } }, std::nullopt });
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            interpreter.add_container_element_signal_alias(
                { object, static_cast<std::uint32_t>(ordinal), leaves[ordinal],
                    true, true });
        }
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, true });

        Process writer_process;
        writer_process.id = 0U;
        writer_process.name = "top.driver_array.slice_writer";
        writer_process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        writer_process.driver_regions = { { proxy, 0U, 0U, true } };
        writer_process.operations = { Halt { } };
        writer = interpreter.add_process(std::move(writer_process));

        Process sibling_process;
        sibling_process.id = 1U;
        sibling_process.name = "top.driver_array.slice_sibling";
        sibling_process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        sibling_process.driver_regions = { { proxy, 0U, 0U, true } };
        sibling_process.operations = { Halt { } };
        sibling = interpreter.add_process(std::move(sibling_process));
    }

    [[nodiscard]] std::string driver_bytes() const
    {
        std::string result;
        for (const auto signal : leaves) {
            result += interpreter.driver_value(writer, signal).to_msb_string();
        }
        return result;
    }

    [[nodiscard]] std::string current_bytes() const
    {
        return interpreter.signal_value(proxy).to_msb_string();
    }

    [[nodiscard]] std::string stored_bytes() const
    {
        return interpreter.stored_signal_value(proxy).to_msb_string();
    }
};

struct FamilySliceTrace {
    std::array<SignalId, 4U> observed;
    std::array<std::size_t, 4U> transaction_counts { };

    static void record(
        void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<FamilySliceTrace*>(context);
        if (entry.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        for (std::size_t index = 0U;
            index < trace.observed.size();
            ++index) {
            if (entry.signal == trace.observed[index]) {
                ++trace.transaction_counts[index];
                return;
            }
        }
    }
};

void run_driver_slice_family_case(const bool ascending)
{
    ThreeLeafProxyArray fixture { ascending };
    const auto slice = PackedLogic4::from_msb_string("101001011100");
    constexpr std::size_t offset { 4U };
    PackedLogic4 expected_driver { 24U, Logic4::z };
    expected_driver.insert_bits(slice, offset);
    const auto expected_driver_text = expected_driver.to_msb_string();
    const auto forced_high_leaf = byte("10101010");
    PackedLogic4 before_current { 24U, Logic4::z };
    before_current.insert_bits(forced_high_leaf, 16U);
    const auto before_current_text = before_current.to_msb_string();
    PackedLogic4 expected_current = expected_driver;
    expected_current.insert_bits(forced_high_leaf, 16U);
    const auto expected_current_text = expected_current.to_msb_string();
    fixture.interpreter.force_signal(
        fixture.leaves[0U], forced_high_leaf);

    std::vector<std::string> raw_observations;
    std::vector<std::pair<std::string, std::string>> stored_observations;
    std::vector<std::string> current_observations;
    fixture.interpreter.set_process_executor(
        fixture.writer,
        std::make_unique<WholeProxyWriter>(
            fixture.interpreter,
            [&](ProcessExecutionContext& context) {
                fixture.interpreter.set_driver_change_hook(
                    [&](const ProcessId process,
                        const SignalId changed,
                        const SimulationTick) {
                        if (process != fixture.writer
                            || changed == fixture.proxy) {
                            return;
                        }
                        if (changed == fixture.leaves[1U]
                            || changed == fixture.leaves[2U]) {
                            raw_observations.push_back(
                                fixture.driver_bytes());
                        }
                    });
                fixture.interpreter.set_stored_signal_change_hook(
                    [&](const SignalId changed, const SimulationTick) {
                        if (changed == fixture.leaves[1U]
                            || changed == fixture.leaves[2U]
                            || changed == fixture.proxy) {
                            stored_observations.emplace_back(
                                fixture.stored_bytes(), fixture.current_bytes());
                        }
                    });
                fixture.interpreter.set_signal_change_hook(
                    [&](const SignalId changed,
                        const PackedLogic4&,
                        const SimulationTick) {
                        if (changed == fixture.leaves[1U]
                            || changed == fixture.leaves[2U]
                            || changed == fixture.proxy) {
                            current_observations.push_back(
                                fixture.current_bytes());
                        }
                    });
                context.write_blocking_slice(
                    fixture.proxy, slice, offset);
            }));

    FamilySliceTrace trace {
        { fixture.leaves[0U], fixture.leaves[1U],
            fixture.leaves[2U], fixture.proxy }
    };
    fixture.interpreter.scheduler().set_trace_hook(
        &trace, &FamilySliceTrace::record);
    const auto result = fixture.interpreter.run();
    require(result.status == RunStatus::completed,
        "the partial aggregate driver writer completes");

    Interpreter packed_reference;
    const auto reference_signal = packed_reference.add_signal({
            "top.driver_array_reference", PackedLogic4(24U, Logic4::z),
        ResolutionKind::sv_wire });
    packed_reference.force_signal_slice(
        reference_signal, forced_high_leaf, 16U);
    Process reference_process;
    reference_process.id = 0U;
    reference_process.name = "top.driver_array_reference.writer";
    reference_process.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    reference_process.driver_regions = {
        { reference_signal, 0U, 0U, true }
    };
    reference_process.operations = { Halt { } };
    const auto reference_writer
        = packed_reference.add_process(std::move(reference_process));
    packed_reference.set_process_executor(
        reference_writer,
        std::make_unique<WholeProxyWriter>(
            packed_reference,
            [&](ProcessExecutionContext& context) {
                context.write_blocking_slice(
                    reference_signal, slice, offset);
            }));
    const auto reference_result = packed_reference.run();
    require(reference_result.status == RunStatus::completed,
        "the packed reference slice writer completes");

    require(fixture.driver_bytes() == expected_driver_text
            && fixture.interpreter.driver_value(
                   fixture.writer, fixture.proxy) == expected_driver
            && fixture.stored_bytes()
                == packed_reference.stored_signal_value(reference_signal)
                    .to_msb_string()
            && fixture.current_bytes()
                == packed_reference.signal_value(reference_signal)
                    .to_msb_string()
            && fixture.current_bytes() == expected_current_text,
        "cross-leaf slice publication matches the packed signal reference");
    require(fixture.interpreter.driver_value(
                fixture.writer, fixture.leaves[0U]) == byte("ZZZZZZZZ")
            && std::ranges::all_of(
                   fixture.leaves,
                   [&](const SignalId signal) {
                       return fixture.interpreter.driver_value(
                                  fixture.sibling, signal)
                           == byte("ZZZZZZZZ");
                   }),
        "the untouched writer slot and every sibling raw driver record remain intact");
    require(raw_observations.size() == 2U
            && std::ranges::all_of(
                   raw_observations,
                   [&](const std::string& observed) {
                       return observed == expected_driver_text;
                   }),
        "every touched raw-driver observer sees the complete cross-leaf write");
    require(stored_observations.size() == 3U
            && std::ranges::all_of(
                   stored_observations,
                   [&](const auto& observed) {
                        return observed.first == expected_driver_text
                           && observed.second == before_current_text;
                   }),
        "stored observers see the complete selected family before current publication");
    require(current_observations.size() == 3U
            && std::ranges::all_of(
                   current_observations,
                   [&](const std::string& observed) {
                       return observed == expected_current_text;
                   }),
        "current observers see the complete cross-leaf family");
    require(trace.transaction_counts
            == std::array<std::size_t, 4U> { 0U, 1U, 1U, 1U },
        "only selected physical leaves and their aggregate proxy transact");
}

void test_driver_slice_family_matches_packed_reference()
{
    run_driver_slice_family_case(false);
    run_driver_slice_family_case(true);
}

void run_driver_force_slice_family_case(const bool ascending)
{
    ThreeLeafProxyArray fixture { ascending };
    Interpreter packed_reference;
    const auto reference_signal = packed_reference.add_signal({
        "top.driver_array_reference", PackedLogic4(24U, Logic4::z),
        ResolutionKind::sv_wire });
    Process reference_process;
    reference_process.id = 0U;
    reference_process.name = "top.driver_array_reference.writer";
    reference_process.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    reference_process.driver_regions = {
        { reference_signal, 0U, 0U, true }
    };
    reference_process.operations = { Halt { } };
    const auto reference_writer
        = packed_reference.add_process(std::move(reference_process));

    // Register every writer before either force is installed. This matches the
    // ordinary packed reference's owner table and keeps registration itself
    // outside the behavior under comparison.
    const auto initial = PackedLogic4::from_msb_string(
        "101010100011001111001100");
    const auto high_force = byte("01010101");
    const auto slice = byte("1111000000");
    constexpr std::size_t slice_offset { 6U };
    fixture.interpreter.force_signal(fixture.leaves[0U], high_force);
    packed_reference.force_signal_slice(
        reference_signal, high_force, 16U);

    std::vector<std::string> raw_driver_changes;
    std::vector<SignalId> stored_notifications;
    std::vector<std::pair<std::string, std::string>> stored_snapshots;
    std::vector<std::string> current_snapshots;
    std::vector<std::string> aggregate_snapshots;
    FamilySliceTrace trace {
        { fixture.leaves[0U], fixture.leaves[1U],
            fixture.leaves[2U], fixture.proxy }
    };
    fixture.interpreter.set_process_executor(
        fixture.writer,
        std::make_unique<WholeProxyWriter>(
            fixture.interpreter,
            [&](ProcessExecutionContext& context) {
                context.write_blocking(fixture.proxy, initial);
                fixture.interpreter.set_driver_change_hook(
                    [&](const ProcessId process,
                        const SignalId changed,
                        const SimulationTick) {
                        if (process == fixture.writer
                            && changed != fixture.proxy) {
                            raw_driver_changes.push_back(
                                fixture.driver_bytes());
                        }
                    });
                fixture.interpreter.set_stored_signal_change_hook(
                    [&](const SignalId changed, const SimulationTick) {
                        if (changed == fixture.proxy
                            || std::ranges::find(
                                   fixture.leaves, changed)
                                != fixture.leaves.end()) {
                            stored_notifications.push_back(changed);
                            stored_snapshots.emplace_back(
                                fixture.stored_bytes(),
                                fixture.current_bytes());
                        }
                    });
                fixture.interpreter.set_signal_change_hook(
                    [&](const SignalId,
                        const PackedLogic4&,
                        const SimulationTick) {
                        current_snapshots.push_back(
                            fixture.current_bytes());
                    });
                fixture.interpreter.scheduler().set_trace_hook(
                    &trace, &FamilySliceTrace::record);

                context.force_driver_signal_slice(
                    fixture.proxy, slice, slice_offset);
                aggregate_snapshots.push_back(
                    fixture.stored_bytes() + ":" + fixture.current_bytes());
                context.release_driver_signal_slice(
                    fixture.proxy, slice_offset, slice.width());
                aggregate_snapshots.push_back(
                    fixture.stored_bytes() + ":" + fixture.current_bytes());
            }));

    const auto result = fixture.interpreter.run();
    require(result.status == RunStatus::completed,
        "the whole-family driver-force writer completes");

    std::vector<std::string> reference_snapshots;
    packed_reference.set_process_executor(
        reference_writer,
        std::make_unique<WholeProxyWriter>(
            packed_reference,
            [&](ProcessExecutionContext& context) {
                context.write_blocking(reference_signal, initial);
                context.force_driver_signal_slice(
                    reference_signal, slice, slice_offset);
                reference_snapshots.push_back(
                    packed_reference.stored_signal_value(reference_signal)
                        .to_msb_string()
                    + ":"
                    + packed_reference.signal_value(reference_signal)
                        .to_msb_string());
                context.release_driver_signal_slice(
                    reference_signal, slice_offset, slice.width());
                reference_snapshots.push_back(
                    packed_reference.stored_signal_value(reference_signal)
                        .to_msb_string()
                    + ":"
                    + packed_reference.signal_value(reference_signal)
                        .to_msb_string());
            }));
    const auto reference_result = packed_reference.run();
    require(reference_result.status == RunStatus::completed,
        "the packed reference force/release writer completes");

    const auto describe = [](const std::vector<std::string>& values) {
        std::string result;
        for (const auto& value : values) {
            result.push_back('[');
            result += value;
            result.push_back(']');
        }
        return result;
    };
    require(aggregate_snapshots == reference_snapshots,
        "aggregate proxy force and release match ordinary packed storage: proxy="
            + describe(aggregate_snapshots)
            + " packed=" + describe(reference_snapshots));
    require(fixture.driver_bytes() == initial.to_msb_string()
            && fixture.interpreter.driver_value(
                   fixture.writer, fixture.proxy) == initial
            && std::ranges::all_of(
                   fixture.leaves,
                   [&](const SignalId signal) {
                       return fixture.interpreter.driver_value(
                                  fixture.sibling, signal)
                           == byte("ZZZZZZZZ");
                   })
            && fixture.stored_bytes()
                == packed_reference.stored_signal_value(reference_signal)
                    .to_msb_string()
            && fixture.current_bytes()
                == packed_reference.signal_value(reference_signal)
                    .to_msb_string(),
        "driver force masks do not alter the original raw driver records");
    require(raw_driver_changes.empty(),
        "driver force and release do not emit raw driver-change callbacks");
    require(stored_notifications
                == std::vector<SignalId> { fixture.proxy, fixture.proxy },
        "driver-force publication retains one proxy-only stored notification per operation");
    require(stored_snapshots.size() == 2U
            && stored_snapshots[0U].first + ":"
                    + stored_snapshots[0U].second == aggregate_snapshots[0U]
            && stored_snapshots[1U].first + ":"
                    + stored_snapshots[1U].second == aggregate_snapshots[1U],
        "proxy stored callbacks run after each complete current-family install");
    const auto force_current = reference_snapshots[0U].substr(
        reference_snapshots[0U].find(':') + 1U);
    const auto release_current = reference_snapshots[1U].substr(
        reference_snapshots[1U].find(':') + 1U);
    const bool current_callbacks_match
        = current_snapshots.size() == 6U
        && std::ranges::all_of(
            current_snapshots.begin(), current_snapshots.begin() + 3,
            [&](const std::string& observed) {
                return observed == force_current;
            })
        && std::ranges::all_of(
            current_snapshots.begin() + 3, current_snapshots.end(),
            [&](const std::string& observed) {
                return observed == release_current;
            });
    require(current_callbacks_match,
        "current observers see the force state then the release state: actual="
            + describe(current_snapshots) + " force=" + force_current
            + " release=" + release_current);
    require(trace.transaction_counts
            == std::array<std::size_t, 4U> { 0U, 2U, 2U, 2U },
        "cross-leaf driver force transacts only selected leaves and their proxy");
    require(fixture.interpreter.signal_value(fixture.leaves[0U]) == high_force,
        "the unselected high leaf keeps its independent global force");
}

void test_driver_force_slice_family_matches_packed_reference()
{
    run_driver_force_slice_family_case(false);
    run_driver_force_slice_family_case(true);
}

void test_driver_force_slice_noop_release_matches_packed_reference()
{
    struct Observation {
        std::string before;
        std::string after;
        std::array<std::size_t, 4U> transaction_counts { };
    };

    const auto run_proxy = [](const bool ascending) {
        ThreeLeafProxyArray fixture { ascending };
        const auto initial = PackedLogic4 { 24U, Logic4::zero };
        Observation observation;
        FamilySliceTrace trace {
            { fixture.leaves[0U], fixture.leaves[1U],
                fixture.leaves[2U], fixture.proxy }
        };
        fixture.interpreter.set_process_executor(
            fixture.writer,
            std::make_unique<WholeProxyWriter>(
                fixture.interpreter,
                [&](ProcessExecutionContext& context) {
                    context.write_blocking(fixture.proxy, initial);
                    context.force_driver_signal_slice(
                        fixture.proxy, byte("1"), 16U);
                    observation.before = fixture.stored_bytes() + ":"
                        + fixture.current_bytes();
                    fixture.interpreter.scheduler().set_trace_hook(
                        &trace, &FamilySliceTrace::record);

                    // Bit 17 belongs to the already-forced high leaf, but the
                    // owner's only active force mask is bit 16. The ordinary
                    // packed API still publishes a transaction for this call.
                    context.release_driver_signal_slice(
                        fixture.proxy, 17U, 1U);
                    observation.after = fixture.stored_bytes() + ":"
                        + fixture.current_bytes();
                }));
        const auto result = fixture.interpreter.run();
        require(result.status == RunStatus::completed,
            "the aggregate no-overlap release writer completes");
        observation.transaction_counts = trace.transaction_counts;
        return observation;
    };

    const auto run_packed = [] {
        Interpreter interpreter;
        const auto signal = interpreter.add_signal({
            "top.driver_force_noop_release_reference",
            PackedLogic4 { 24U, Logic4::zero }, ResolutionKind::sv_wire });
        Process process;
        process.id = 0U;
        process.name = "top.driver_force_noop_release_reference.writer";
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.driver_regions = { { signal, 0U, 0U, true } };
        process.operations = { Halt { } };
        const auto writer = interpreter.add_process(std::move(process));

        Observation observation;
        FamilySliceTrace trace { { signal, signal, signal, signal } };
        interpreter.set_process_executor(
            writer,
            std::make_unique<WholeProxyWriter>(
                interpreter,
                [&](ProcessExecutionContext& context) {
                    context.write_blocking(
                        signal, PackedLogic4 { 24U, Logic4::zero });
                    context.force_driver_signal_slice(
                        signal, byte("1"), 16U);
                    observation.before
                        = interpreter.stored_signal_value(signal).to_msb_string()
                        + ":"
                        + interpreter.signal_value(signal).to_msb_string();
                    interpreter.scheduler().set_trace_hook(
                        &trace, &FamilySliceTrace::record);
                    context.release_driver_signal_slice(signal, 17U, 1U);
                    observation.after
                        = interpreter.stored_signal_value(signal).to_msb_string()
                        + ":"
                        + interpreter.signal_value(signal).to_msb_string();
                }));
        const auto result = interpreter.run();
        require(result.status == RunStatus::completed,
            "the packed no-overlap release reference completes");
        observation.transaction_counts = trace.transaction_counts;
        return observation;
    };

    const auto packed = run_packed();
    require(packed.before == packed.after
            && packed.transaction_counts[0U] == 1U,
        "ordinary packed release transacts even when its existing force map "
        "does not overlap the released slice");
    for (const bool ascending : { false, true }) {
        const auto proxy = run_proxy(ascending);
        require(proxy.before == packed.before
                && proxy.after == packed.after
                && proxy.before == proxy.after,
            "the no-overlap proxy release preserves packed stored/current values");
        require(proxy.transaction_counts
                == std::array<std::size_t, 4U> { 1U, 0U, 0U, 1U },
            "the no-overlap proxy release transacts only its mapped leaf and proxy");
    }
}

void test_driver_force_slice_preserves_equal_strength_external_drives()
{
    const auto run = [](const bool aggregate_proxy) {
        Interpreter interpreter;
        const auto initial = byte("ZZZZZZZZ");
        SignalId target { };
        std::array<SignalId, 2U> leaves { };
        if (aggregate_proxy) {
            leaves[0U] = interpreter.add_signal({
                "top.external_force_array[1]", byte("ZZZZ"),
                ResolutionKind::sv_wire });
            leaves[1U] = interpreter.add_signal({
                "top.external_force_array[0]", byte("ZZZZ"),
                ResolutionKind::sv_wire });
            target = interpreter.add_signal({
                "top.external_force_array", initial,
                ResolutionKind::sv_wire });
            auto type = packed_array_type();
            type.element_width = 4U;
            const auto object = interpreter.add_container_object({
                "top.external_force_array",
                ContainerValue {
                    type, { byte("ZZZZ"), byte("ZZZZ") }, { }
                },
                std::nullopt });
            interpreter.add_container_element_signal_alias({
                object, 0U, leaves[0U], true, true });
            interpreter.add_container_element_signal_alias({
                object, 1U, leaves[1U], true, true });
            interpreter.add_container_aggregate_signal_alias({
                object, target, true, true });
        } else {
            target = interpreter.add_signal({
                "top.external_force_reference", initial,
                ResolutionKind::sv_wire });
        }

        std::vector<std::string> snapshots;
        class DriverForceWriter final : public ProcessExecutor {
        public:
            DriverForceWriter(
                Interpreter& interpreter,
                const SignalId target,
                std::vector<std::string>& snapshots)
                : interpreter_ { interpreter }
                , target_ { target }
                , snapshots_ { snapshots }
            {
            }

            [[nodiscard]] ProcessResumeResult resume(
                ProcessExecutionContext& context,
                const InstructionIndex start) override
            {
                if (!resumed_) {
                    require(start == 0U,
                        "external driver-force writer starts at its wait");
                    resumed_ = true;
                    ProcessResumeResult result { 0U, 1U };
                    result.external.kind
                        = ExternalSuspendKind::wait_sensitivity;
                    return result;
                }
                require(start == 1U,
                    "external driver-force writer resumes at its force");
                context.force_driver_signal_slice(
                    target_, byte("1111"), 2U);
                snapshots_.push_back(
                    interpreter_.stored_signal_value(target_).to_msb_string()
                    + ":"
                    + interpreter_.signal_value(target_).to_msb_string());
                context.release_driver_signal_slice(target_, 2U, 4U);
                snapshots_.push_back(
                    interpreter_.stored_signal_value(target_).to_msb_string()
                    + ":"
                    + interpreter_.signal_value(target_).to_msb_string());
                ProcessResumeResult result { 1U, 2U };
                result.external.kind = ExternalSuspendKind::halt;
                return result;
            }

        private:
            Interpreter& interpreter_;
            SignalId target_ { };
            std::vector<std::string>& snapshots_;
            bool resumed_ { };
        };

        Process process;
        process.id = 0U;
        process.name = aggregate_proxy
            ? "top.external_force_array.writer"
            : "top.external_force_reference.writer";
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.static_sensitivity.push_back({ target, EdgeKind::any });
        if (aggregate_proxy) {
            process.driver_regions = {
                { leaves[0U], 0U, 4U, true },
                { leaves[1U], 0U, 4U, true },
            };
        } else {
            process.driver_regions.push_back({ target, 0U, 8U, true });
        }
        process.operations = { WaitSensitivity { }, Halt { } };
        const auto writer = interpreter.add_process(std::move(process));
        interpreter.set_process_executor(
            writer,
            std::make_unique<DriverForceWriter>(
                interpreter, target, snapshots));

        // Scheduling the proxy value creates the same default-strength
        // external contribution that the packed scalar reference receives.
        interpreter.schedule_signal_at(target, byte("00000000"), 1U, 0U);
        const auto result = interpreter.run();
        require(result.status == RunStatus::completed && result.time == 1U,
            "external driver-force reference completes at its scheduled value");
        if (aggregate_proxy) {
            require(interpreter.driver_value(writer, leaves[0U])
                        == byte("ZZZZ")
                    && interpreter.driver_value(writer, leaves[1U])
                        == byte("ZZZZ"),
                "driver force preserves the original per-leaf raw records");
        } else {
            require(interpreter.driver_value(writer, target) == initial,
                "packed driver force preserves its original raw record");
        }
        return snapshots;
    };

    const auto packed = run(false);
    const auto proxy = run(true);
    require(packed == std::vector<std::string> {
                "00XXXX00:00XXXX00", "00000000:00000000" }
            && proxy == packed,
        "cross-leaf driver force keeps equal-strength external slots in the packed resolution");
}

void test_driver_hook_reentry_resolves_the_live_leaf_family()
{
    ProxyArray fixture;
    fixture.interpreter.force_signal(
        fixture.first, byte("10101010"));

    const auto outer = byte("0001001000110100");
    const auto nested = byte("1111000010100101");
    std::vector<std::string> raw_events;
    std::vector<std::string> stored_events;
    std::vector<std::string> current_events;
    bool nested_write { };

    fixture.install_writer([&](ProcessExecutionContext& context) {
        auto* context_pointer = &context;
        fixture.interpreter.set_driver_change_hook(
            [&](const ProcessId process,
                const SignalId changed,
                const SimulationTick) {
                if (process != fixture.writer) {
                    return;
                }
                const auto label = changed == fixture.first ? "high"
                    : changed == fixture.second ? "low" : "proxy";
                raw_events.emplace_back(
                    std::string { label } + ":" + fixture.raw_writer_bytes());
                if (changed == fixture.first && !nested_write) {
                    nested_write = true;
                    context_pointer->write_blocking(fixture.proxy, nested);
                }
            });
        fixture.interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                if (changed == fixture.first || changed == fixture.second
                    || changed == fixture.proxy) {
                    const auto label = changed == fixture.first ? "high"
                        : changed == fixture.second ? "low" : "proxy";
                    stored_events.emplace_back(
                        std::string { label } + ":" + fixture.stored_bytes()
                        + ":" + fixture.current_bytes());
                }
            });
        fixture.interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4&,
                const SimulationTick) {
                if (changed == fixture.first || changed == fixture.second
                    || changed == fixture.proxy) {
                    const auto label = changed == fixture.first ? "high"
                        : changed == fixture.second ? "low" : "proxy";
                    current_events.emplace_back(
                        std::string { label } + ":" + fixture.current_bytes());
                }
            });
        context.write_blocking(fixture.proxy, outer);
    });

    const auto result = fixture.interpreter.run();
    require(result.status == RunStatus::completed,
        "the reentrant whole-proxy driver process completes");
    require(raw_events == std::vector<std::string> {
            "high:0001001000110100",
            "high:1111000010100101",
            "low:1111000010100101",
            "proxy:1111000010100101",
            "low:1111000010100101",
            "proxy:1111000010100101"
        },
        "each leaf hook observes the fully installed raw family and the proxy "
        "driver hook fires once for each logical write");
    require(stored_events == std::vector<std::string> {
            "high:1111000010100101:10101010ZZZZZZZZ",
            "low:1111000010100101:10101010ZZZZZZZZ",
            "proxy:1111000010100101:10101010ZZZZZZZZ"
        },
        "the nested stored phase sees the complete stored family before current publication");
    // The aggregate current hook runs before deferred leaf current hooks.
    require(current_events == std::vector<std::string> {
            "proxy:1010101010100101",
            "low:1010101010100101"
        },
        "the nested current phase publishes the complete forced aggregate");
    require(fixture.raw_writer_bytes() == nested.to_msb_string()
            && fixture.stored_bytes() == nested.to_msb_string()
            && fixture.current_bytes() == "1010101010100101"
            && fixture.interpreter.driver_value(
                   fixture.sibling, fixture.first) == byte("ZZZZZZZZ")
            && fixture.interpreter.driver_value(
                   fixture.sibling, fixture.second) == byte("ZZZZZZZZ")
            && fixture.interpreter.driver_value(
                   fixture.writer, fixture.proxy) == nested,
        "live reentrant drivers win, sibling driver records remain intact, "
        "and the logical aggregate projects the same family");

    fixture.interpreter.release_signal(fixture.first);
    require(fixture.current_bytes() == nested.to_msb_string(),
        "releasing the element force exposes the final whole-family driver");
}

void test_stored_hook_reentry_keeps_the_outer_resolved_value()
{
    ProxyArray fixture;
    fixture.interpreter.force_signal(
        fixture.first, byte("10101010"));

    const auto outer = byte("0001001000110100");
    const auto nested = byte("1111000010100101");
    std::vector<std::string> current_events;
    std::vector<std::string> stored_events;
    bool nested_write { };

    fixture.install_writer([&](ProcessExecutionContext& context) {
        auto* context_pointer = &context;
        fixture.interpreter.set_driver_change_hook(
            [&](const ProcessId process,
                const SignalId changed,
                const SimulationTick) {
                if (process == fixture.writer) {
                    const auto label = changed == fixture.first ? "high"
                        : changed == fixture.second ? "low" : "proxy";
                    stored_events.emplace_back(
                        std::string { "driver-" } + label + ":"
                        + fixture.raw_writer_bytes());
                }
            });
        fixture.interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                if (changed != fixture.first && changed != fixture.second
                    && changed != fixture.proxy) {
                    return;
                }
                const auto label = changed == fixture.first ? "high"
                    : changed == fixture.second ? "low" : "proxy";
                stored_events.emplace_back(
                    std::string { label } + ":" + fixture.stored_bytes()
                    + ":" + fixture.current_bytes());
                if (changed == fixture.first && !nested_write) {
                    nested_write = true;
                    context_pointer->write_blocking(fixture.proxy, nested);
                }
            });
        fixture.interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4&,
                const SimulationTick) {
                if (changed == fixture.first || changed == fixture.second
                    || changed == fixture.proxy) {
                    const auto label = changed == fixture.first ? "high"
                        : changed == fixture.second ? "low" : "proxy";
                    current_events.emplace_back(
                        std::string { label } + ":" + fixture.current_bytes());
                }
            });
        context.write_blocking(fixture.proxy, outer);
    });

    const auto result = fixture.interpreter.run();
    require(result.status == RunStatus::completed,
        "stored-hook whole-proxy reentry completes");
    require(fixture.raw_writer_bytes() == nested.to_msb_string()
            && fixture.stored_bytes() == nested.to_msb_string()
            && fixture.current_bytes() == "1010101000110100"
            && fixture.interpreter.container_object_value(fixture.object)
                .elements
                == std::vector<PackedLogic4> {
                    byte("10101010"), byte("00110100")
                },
        "stored-hook reentry leaves nested raw/stored drivers while the "
        "outer captured resolution publishes current");
    require(current_events == std::vector<std::string> {
            "proxy:1010101010100101",
            "low:1010101010100101",
            "proxy:1010101000110100",
            "low:1010101000110100"
        },
        "nested current publication is followed by the outer captured current family");
    require(std::ranges::count_if(
                stored_events, [](const std::string& event) {
                    return event.starts_with("driver-");
                }) == 6
            && std::ranges::count_if(
                stored_events, [](const std::string& event) {
                    return !event.starts_with("driver-");
                }) == 6,
        "the two whole writes notify each changed leaf and aggregate exactly once");
    fixture.interpreter.release_signal(fixture.first);
    require(fixture.current_bytes() == "1111000000110100",
        "force release preserves the outer current low byte and nested stored high byte");
}

void test_external_drive_identity_survives_proxy_writes()
{
    const auto run_reference = [](const bool aggregate_proxy,
                                  const bool globally_forced) {
        Interpreter interpreter;
        const auto initial = byte("ZZZZZZZZ");
        SignalId high_leaf { };
        SignalId low_leaf { };
        SignalId target { };
        ContainerObjectId object { };
        if (aggregate_proxy) {
            high_leaf = interpreter.add_signal({
                "top.external_array[1]", byte("ZZZZ"),
                ResolutionKind::sv_wire });
            low_leaf = interpreter.add_signal({
                "top.external_array[0]", byte("ZZZZ"),
                ResolutionKind::sv_wire });
            target = interpreter.add_signal({
                "top.external_array", initial, ResolutionKind::sv_wire });
            auto type = packed_array_type();
            type.element_width = 4U;
            object = interpreter.add_container_object({
                "top.external_array",
                ContainerValue { type, { byte("ZZZZ"), byte("ZZZZ") }, { } },
                std::nullopt });
            interpreter.add_container_element_signal_alias({
                object, 0U, high_leaf, true, true });
            interpreter.add_container_element_signal_alias({
                object, 1U, low_leaf, true, true });
            interpreter.add_container_aggregate_signal_alias({
                object, target, true, true });
        } else {
            target = interpreter.add_signal({
                "top.external_reference", initial, ResolutionKind::sv_wire });
        }

        std::vector<std::string> snapshots;
        class ExternalDriveWriter final : public ProcessExecutor {
        public:
            using Write = std::function<void(ProcessExecutionContext&)>;

            explicit ExternalDriveWriter(Write write)
                : write_ { std::move(write) }
            {
            }

            [[nodiscard]] ProcessResumeResult resume(
                ProcessExecutionContext& context,
                const InstructionIndex start) override
            {
                if (resumes_ == 0U) {
                    require(start == 0U,
                        "external-drive writer starts at its static wait");
                    ++resumes_;
                    ProcessResumeResult result { 0U, 1U };
                    result.external.kind
                        = ExternalSuspendKind::wait_sensitivity;
                    return result;
                }
                require(resumes_ == 1U && start == 1U,
                    "external-drive writer resumes once after the proxy update");
                ++resumes_;
                write_(context);
                ProcessResumeResult result { 1U, 2U };
                result.external.kind = ExternalSuspendKind::halt;
                return result;
            }

        private:
            Write write_;
            std::size_t resumes_ { };
        };

        Process writer_process;
        writer_process.id = 0U;
        writer_process.name = aggregate_proxy
            ? "top.external_array.writer" : "top.external_reference.writer";
        writer_process.static_sensitivity.push_back({ target, EdgeKind::any });
        if (aggregate_proxy) {
            writer_process.driver_regions = {
                { high_leaf, 0U, 4U, true },
                { low_leaf, 0U, 4U, true },
            };
        } else {
            writer_process.driver_regions.push_back({ target, 0U, 8U, true });
        }
        writer_process.operations = { WaitSensitivity { }, Halt { } };
        const auto writer = interpreter.add_process(std::move(writer_process));
        interpreter.set_process_executor(
            writer,
            std::make_unique<ExternalDriveWriter>(
                [&](ProcessExecutionContext& context) {
                    if (aggregate_proxy) {
                        context.write_blocking(high_leaf, byte("1111"));
                        snapshots.push_back(
                            interpreter.signal_value(target).to_msb_string());
                        context.write_blocking(target, byte("11111111"));
                    } else {
                        context.write_blocking_slice(
                            target, byte("1111"), 4U);
                        snapshots.push_back(
                            interpreter.signal_value(target).to_msb_string());
                        context.write_blocking(target, byte("11111111"));
                    }
                    snapshots.push_back(
                        interpreter.signal_value(target).to_msb_string());
                }));

        if (globally_forced) {
            interpreter.force_signal_slice(target, byte("1010"), 4U);
        }

        interpreter.schedule_signal_at(
            target, byte("00000000"), 1U, 0U);
        interpreter.schedule_signal_at(
            target, byte("ZZZZZZZZ"), 2U, 0U);
        const auto result = interpreter.run();
        require(result.status == RunStatus::completed && result.time == 2U,
            "external proxy and packed-reference updates complete");

        if (aggregate_proxy) {
            require(interpreter.driver_value(writer, high_leaf)
                        == byte("1111")
                    && interpreter.driver_value(writer, low_leaf)
                        == byte("1111"),
                "whole and leaf writes retain both raw process driver slots");
        } else {
            require(interpreter.driver_value(writer, target)
                        == byte("11111111"),
                "the packed reference retains its whole raw driver slot");
        }
        if (globally_forced) {
            snapshots.push_back(
                interpreter.signal_value(target).to_msb_string());
            interpreter.release_signal_slice(target, 4U, 4U);
        }
        return std::pair {
            std::move(snapshots),
            interpreter.signal_value(target).to_msb_string() };
    };

    const auto describe = [](const auto& result) {
        std::string text;
        for (const auto& snapshot : result.first) {
            text += snapshot + ",";
        }
        return text + "final=" + result.second;
    };
    const auto forced_packed = run_reference(false, true);
    const auto forced_proxy = run_reference(true, true);
    require(forced_packed.first == std::vector<std::string> {
                "10100000", "1010XXXX", "10101111" }
            && forced_packed.second == "11111111",
        "a partial global force masks current while external and process "
        "drivers retain their independent resolved contributions");
    require(forced_proxy == forced_packed,
        "global force must preserve persistent external array-driver identity: "
        "packed=" + describe(forced_packed)
            + " proxy=" + describe(forced_proxy));

    const auto packed = run_reference(false, false);
    const auto proxy = run_reference(true, false);
    require(packed.first == std::vector<std::string> {
                "XXXX0000", "XXXXXXXX" }
            && packed.second == "11111111",
        "a persistent packed external driver resolves with process writes and releases to the process value");

    require(proxy == packed,
        "an aggregate external drive remains present on each leaf through "
        "later leaf/whole process writes and its Z release: packed="
            + describe(packed) + " proxy=" + describe(proxy));
}

void test_multidimensional_partial_force_matches_packed_reference()
{
    const auto run = [](const bool physical_array) {
        Interpreter interpreter;
        const auto target = interpreter.add_signal({
            "top.matrix", PackedLogic4(32U, Logic4::z), ResolutionKind::sv_wire
        });
        std::array<SignalId, 4U> leaves { };
        if (physical_array) {
            auto type = packed_array_type();
            type.index_left = 3;
            type.index_right = 2;
            type.dimensions = { { 3, 2 }, { -1, 0 } };
            const auto object = interpreter.add_container_object({
                "top.matrix",
                ContainerValue { type,
                    std::vector<PackedLogic4>(4U, PackedLogic4(8U, Logic4::z)), { } },
                std::nullopt
            });
            const std::array suffixes { "[3][-1]", "[3][0]", "[2][-1]", "[2][0]" };
            for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
                leaves[ordinal] = interpreter.add_signal({
                    std::string { "top.matrix" } + suffixes[ordinal],
                    PackedLogic4(8U, Logic4::z), ResolutionKind::sv_wire
                });
                interpreter.add_container_element_signal_alias({
                    object, static_cast<std::uint32_t>(ordinal), leaves[ordinal],
                    true, true
                });
            }
            interpreter.add_container_aggregate_signal_alias(
                { object, target, true, true });
        }
        Process process;
        process.id = 0U;
        process.name = "top.matrix.writer";
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.driver_regions = { { target, 0U, 0U, true } };
        process.operations = { Halt { } };
        const auto writer = interpreter.add_process(process);
        process.id = 1U;
        process.name = "top.matrix.sibling";
        const auto sibling = interpreter.add_process(std::move(process));
        std::vector<std::array<std::string, 3U>> snapshots;
        const auto capture = [&] {
            snapshots.push_back({
                interpreter.signal_value(target).to_msb_string(),
                interpreter.stored_signal_value(target).to_msb_string(),
                interpreter.driver_value(writer, target).to_msb_string()
            });
            if (physical_array) {
                const auto current = interpreter.signal_value(target);
                const auto stored = interpreter.stored_signal_value(target);
                const auto raw = interpreter.driver_value(writer, target);
                for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
                    const auto offset = (leaves.size() - ordinal - 1U) * 8U;
                    require(interpreter.signal_value(leaves[ordinal])
                            == current.extract_bits(offset, 8U)
                        && interpreter.stored_signal_value(leaves[ordinal])
                            == stored.extract_bits(offset, 8U)
                        && interpreter.driver_value(writer, leaves[ordinal])
                            == raw.extract_bits(offset, 8U)
                        && interpreter.driver_value(sibling, leaves[ordinal])
                            == PackedLogic4(8U, Logic4::z),
                        "2D current, stored and original-owner views agree after every mutation");
                }
            }
        };
        interpreter.set_process_executor(writer,
            std::make_unique<WholeProxyWriter>(interpreter,
                [&](ProcessExecutionContext& context) {
                    context.write_blocking(target, byte("10100101001111000001001011100001"));
                    capture();
                    context.force_driver_signal_slice(target, byte("110011001100"), 6U);
                    capture();
                    context.write_blocking_slice(target, byte("01010"), 15U);
                    capture();
                    context.release_driver_signal_slice(target, 6U, 12U);
                    capture();
                    interpreter.force_signal_slice(target, byte("XZ01"), 14U);
                    capture();
                    interpreter.release_signal_slice(target, 14U, 4U);
                    capture();
                }));
        require(interpreter.run().status == RunStatus::completed,
            "2D force/release writer completes");
        return snapshots;
    };
    const auto reference = run(false);
    const auto physical = run(true);
    require(reference.size() == 6U && physical == reference,
        "2D partial driver/global force, writes and release preserve packed-reference behavior");
}

} // namespace

void test_aggregate_proxy_driver_family()
{
    test_multidimensional_partial_force_matches_packed_reference();
    test_driver_slice_family_matches_packed_reference();
    test_driver_force_slice_family_matches_packed_reference();
    test_driver_force_slice_noop_release_matches_packed_reference();
    test_driver_force_slice_preserves_equal_strength_external_drives();
    test_driver_hook_reentry_resolves_the_live_leaf_family();
    test_stored_hook_reentry_keeps_the_outer_resolved_value();
    test_external_drive_identity_survives_proxy_writes();
}

} // namespace fsim::tests::runtime
