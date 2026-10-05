// SPDX-License-Identifier: Apache-2.0
#include "../../src/runtime/simir_internal.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
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

    static const auto& implementation(const Interpreter& interpreter)
    {
        return *interpreter.impl_;
    }

    static void initialize_driver(
        Interpreter& interpreter,
        const ProcessId process,
        const SignalId signal,
        const PackedLogic4& value)
    {
        implementation(interpreter).commit_driver(
            process, signal, value, std::nullopt, false);
    }

    static void set_external_driver(
        Interpreter& interpreter,
        const SignalId signal,
        const PackedLogic4& value)
    {
        auto& impl = implementation(interpreter);
        impl.external_driver_slot(signal) = value;
        impl.commit_resolved(signal, impl.resolved_driver_value(signal));
    }

    [[nodiscard]] static bool external_driver_matches(
        const Interpreter& interpreter,
        const SignalId signal,
        const PackedLogic4& expected)
    {
        const auto& external
            = implementation(interpreter).external_driver_values.at(signal);
        return external && *external == expected;
    }

    [[nodiscard]] static bool driver_force_active(
        const Interpreter& interpreter,
        const SignalId signal,
        const ProcessId process)
    {
        const auto& impl = implementation(interpreter);
        const auto& values = impl.forced_driver_values.at(signal);
        const auto& masks = impl.forced_driver_masks.at(signal);
        if (!values || !masks) {
            return false;
        }
        const auto forced = values->find(process);
        const auto mask = masks->find(process);
        if (forced == values->end() || mask == masks->end()
            || forced->second.width() != mask->second.width()) {
            return false;
        }
        for (std::size_t bit = 0U; bit < mask->second.width(); ++bit) {
            if (mask->second.get(bit) == Logic4::one) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static bool driver_force_cleared(
        const Interpreter& interpreter,
        const SignalId signal,
        const ProcessId process)
    {
        const auto& impl = implementation(interpreter);
        const auto& values = impl.forced_driver_values.at(signal);
        const auto& masks = impl.forced_driver_masks.at(signal);
        return static_cast<bool>(values) == static_cast<bool>(masks)
            && (!values || !values->contains(process))
            && (!masks || !masks->contains(process));
    }
};

} // namespace fsim::runtime::simir

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using namespace fsim::tests::runtime::staging_failure_support;

constexpr std::size_t maximum_allocation_sites = 512U;

enum class PublicationPhase : std::uint8_t {
    unchanged,
    raw_only,
    stored_only,
    current,
    invalid,
};

struct HookCounts {
    std::array<std::size_t, 3U> signals { };
};

struct HookSummary {
    HookCounts raw;
    HookCounts stored;
    HookCounts current;
};

struct PhaseSnapshot {
    PublicationPhase phase { PublicationPhase::invalid };
    bool coherent { };
};

struct WriteReport {
    bool executor_called { };
    bool initial_state_ready { };
    bool failed { };
    bool retry_completed { };
    bool later_write_completed { };
    std::size_t allocation_count { };
    PhaseSnapshot after_attempt;
    HookSummary attempt_hooks;
    HookSummary retry_hooks;
    HookSummary later_hooks;
    bool sibling_records_preserved { };
    bool frames_clear_after_attempt { };
    bool frames_clear_after_retry { };
    bool frames_clear_after_later_write { };
    bool retry_state_complete { };
    bool later_state_complete { };
};

enum class DriverForcePhase : std::uint8_t {
    unchanged,
    stored_only,
    current,
    invalid,
};

enum class DriverStrengthMode : std::uint8_t {
    default_strength,
    opposing_explicit,
};

const char* driver_strength_mode_name(const DriverStrengthMode mode) noexcept
{
    return mode == DriverStrengthMode::default_strength
        ? "default"
        : "opposing-explicit";
}

struct DriverForceSnapshot {
    DriverForcePhase phase { DriverForcePhase::invalid };
    bool force_maps_coherent { };
    bool raw_records_preserved { };
    bool frames_clear { };
};

struct DriverForceReport {
    bool executor_called { };
    bool failed { };
    bool retry_completed { };
    bool release_completed { };
    bool later_write_completed { };
    std::size_t allocation_count { };
    DriverForceSnapshot after_attempt;
    DriverForceSnapshot after_retry;
    DriverForceSnapshot after_release;
    bool later_state_complete { };
};

PackedLogic4 aggregate_value(
    const PackedLogic4& first,
    const PackedLogic4& second)
{
    PackedLogic4 aggregate(
        first.width() + second.width(), Logic4::zero);
    aggregate.insert_bits(first, second.width());
    aggregate.insert_bits(second, 0U);
    return aggregate;
}

struct DriverFamily {
    Interpreter interpreter;
    std::size_t width;
    PackedLogic4 zero;
    PackedLogic4 ones;
    PackedLogic4 aggregate_zero;
    PackedLogic4 aggregate_ones;
    SignalId first { };
    SignalId second { };
    SignalId proxy { };
    ContainerObjectId object { };
    ProcessId sibling { };
    ProcessId writer { };
    DriveStrength sibling_strength {
        StrengthRank::weak, StrengthRank::weak
    };
    DriveStrength writer_strength {
        StrengthRank::strong, StrengthRank::strong
    };

    explicit DriverFamily(
        const std::size_t leaf_width,
        const DriveStrength sibling_drive = {
            StrengthRank::weak, StrengthRank::weak },
        const DriveStrength writer_drive = {
            StrengthRank::strong, StrengthRank::strong })
        : width { leaf_width }
        , zero { width, Logic4::zero }
        , ones { width, Logic4::one }
        , aggregate_zero { aggregate_value(zero, zero) }
        , aggregate_ones { aggregate_value(ones, ones) }
        , sibling_strength { sibling_drive }
        , writer_strength { writer_drive }
    {
        first = interpreter.add_signal({
            "top.driver_family[1]", zero, ResolutionKind::sv_wire });
        second = interpreter.add_signal({
            "top.driver_family[0]", zero, ResolutionKind::sv_wire });
        proxy = interpreter.add_signal({
            "top.driver_family", aggregate_zero, ResolutionKind::sv_wire });

        ContainerType type;
        type.element_kind = ContainerElementKind::Packed;
        type.element_width = static_cast<std::uint32_t>(width);
        type.fixed = true;
        type.index_left = 1;
        type.index_right = 0;
        type.dimensions = { { 1, 0 } };
        object = interpreter.add_container_object({
            "top.driver_family",
            ContainerValue { type, { zero, zero }, { } },
            std::nullopt
        });
        interpreter.add_container_element_signal_alias(
            { object, 0U, first, true, true });
        interpreter.add_container_element_signal_alias(
            { object, 1U, second, true, true });
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, true });

        sibling = add_driver_process(
            0U, "top.driver_family.sibling", sibling_strength);
        writer = add_driver_process(
            1U, "top.driver_family.writer", writer_strength);

        // ResolutionKind::sv_wire registers each raw slot as Z. Seed both
        // owners through ordinary leaf commits before the injected whole write.
        for (const auto process : std::array<ProcessId, 2U> {
                 sibling, writer }) {
            OwnedDriverDemotionTestAccess::initialize_driver(
                interpreter, process, first, zero);
            OwnedDriverDemotionTestAccess::initialize_driver(
                interpreter, process, second, zero);
        }
    }

    [[nodiscard]] ProcessId add_driver_process(
        const ProcessId id,
        std::string name,
        const DriveStrength strength)
    {
        Process process;
        process.id = id;
        process.name = std::move(name);
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.drive_strength = strength;
        process.driver_regions = {
            { first, 0U, 0U, true }, { second, 0U, 0U, true }
        };
        process.operations = { Halt { } };
        return interpreter.add_process(std::move(process));
    }

    [[nodiscard]] PhaseSnapshot snapshot() const
    {
        const bool raw_old
            = interpreter.driver_value(writer, first) == zero
            && interpreter.driver_value(writer, second) == zero
            && interpreter.driver_value(writer, proxy) == aggregate_zero;
        const bool raw_new
            = interpreter.driver_value(writer, first) == ones
            && interpreter.driver_value(writer, second) == ones
            && interpreter.driver_value(writer, proxy) == aggregate_ones;
        const bool stored_old
            = interpreter.stored_signal_value(first) == zero
            && interpreter.stored_signal_value(second) == zero
            && interpreter.stored_signal_value(proxy) == aggregate_zero;
        const bool stored_new
            = interpreter.stored_signal_value(first) == ones
            && interpreter.stored_signal_value(second) == ones
            && interpreter.stored_signal_value(proxy) == aggregate_ones;
        const bool current_old
            = interpreter.signal_value(first) == zero
            && interpreter.signal_value(second) == zero
            && interpreter.signal_value(proxy) == aggregate_zero;
        const bool current_new
            = interpreter.signal_value(first) == ones
            && interpreter.signal_value(second) == ones
            && interpreter.signal_value(proxy) == aggregate_ones;
        const auto& logical_container
            = interpreter.container_object_value(object);
        const bool container_old
            = logical_container.elements.size() == 2U
            && logical_container.elements[0] == zero
            && logical_container.elements[1] == zero;
        const bool container_new
            = logical_container.elements.size() == 2U
            && logical_container.elements[0] == ones
            && logical_container.elements[1] == ones;

        if (raw_old && stored_old && current_old && container_old) {
            return { PublicationPhase::unchanged, true };
        }
        if (raw_new && stored_old && current_old && container_old) {
            return { PublicationPhase::raw_only, true };
        }
        if (raw_new && stored_new && current_old && container_old) {
            return { PublicationPhase::stored_only, true };
        }
        if (raw_new && stored_new && current_new && container_new) {
            return { PublicationPhase::current, true };
        }
        return { PublicationPhase::invalid, false };
    }

    [[nodiscard]] bool sibling_records_unchanged() const
    {
        const auto& impl
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto* first_record = impl.driver_values.at(first).find(sibling);
        const auto* second_record = impl.driver_values.at(second).find(sibling);
        const auto* first_writer = impl.driver_values.at(first).find(writer);
        const auto* second_writer = impl.driver_values.at(second).find(writer);
        return first_record != nullptr
            && second_record != nullptr
            && first_writer != nullptr
            && second_writer != nullptr
            && first_record->value == zero
            && second_record->value == zero
            && first_record->strength == sibling_strength
            && second_record->strength == sibling_strength
            && first_writer->strength == writer_strength
            && second_writer->strength == writer_strength;
    }

    [[nodiscard]] bool frames_clear() const
    {
        const auto& impl
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        return impl.aggregate_signal_batches.at(proxy).depth == 0U
            && impl.container_alias_write_batches.at(object).frames.empty();
    }

    [[nodiscard]] bool complete_state(
        const PackedLogic4& element,
        const PackedLogic4& aggregate) const
    {
        return interpreter.driver_value(writer, first) == element
            && interpreter.driver_value(writer, second) == element
            && interpreter.driver_value(writer, proxy) == aggregate
            && interpreter.stored_signal_value(first) == element
            && interpreter.stored_signal_value(second) == element
            && interpreter.stored_signal_value(proxy) == aggregate
            && interpreter.signal_value(first) == element
            && interpreter.signal_value(second) == element
            && interpreter.signal_value(proxy) == aggregate;
    }

    [[nodiscard]] DriverForceSnapshot force_snapshot() const
    {
        const auto& impl
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        auto expected_high_force = zero;
        expected_high_force.set(0U, Logic4::one);
        auto expected_low_force = zero;
        expected_low_force.set(width - 1U, Logic4::one);
        auto expected_high_mask = PackedLogic4 { width, Logic4::zero };
        expected_high_mask.set(0U, Logic4::one);
        auto expected_low_mask = PackedLogic4 { width, Logic4::zero };
        expected_low_mask.set(width - 1U, Logic4::one);
        auto expected_high = zero;
        expected_high.set(0U, Logic4::x);
        auto expected_low = zero;
        expected_low.set(width - 1U, Logic4::x);
        const auto expected_aggregate
            = aggregate_value(expected_high, expected_low);

        const auto& high_values = impl.forced_driver_values.at(first);
        const auto& low_values = impl.forced_driver_values.at(second);
        const auto& high_masks = impl.forced_driver_masks.at(first);
        const auto& low_masks = impl.forced_driver_masks.at(second);
        const auto has_entry = [&](
            const auto& map) { return map && map->contains(writer); };
        const bool high_entry = has_entry(high_values);
        const bool low_entry = has_entry(low_values);
        const bool high_mask_entry = has_entry(high_masks);
        const bool low_mask_entry = has_entry(low_masks);
        const bool maps_absent = !high_entry && !low_entry
            && !high_mask_entry && !low_mask_entry;
        const bool maps_installed = high_entry && low_entry
            && high_mask_entry && low_mask_entry
            && high_values->size() == 1U && low_values->size() == 1U
            && high_masks->size() == 1U && low_masks->size() == 1U
            && high_values->at(writer) == expected_high_force
            && low_values->at(writer) == expected_low_force
            && high_masks->at(writer) == expected_high_mask
            && low_masks->at(writer) == expected_low_mask;
        const bool raw_records_preserved
            = interpreter.driver_value(writer, first) == zero
            && interpreter.driver_value(writer, second) == zero
            && interpreter.driver_value(writer, proxy) == aggregate_zero
            && sibling_records_unchanged();
        const bool stored_old
            = interpreter.stored_signal_value(first) == zero
            && interpreter.stored_signal_value(second) == zero
            && interpreter.stored_signal_value(proxy) == aggregate_zero;
        const bool stored_new
            = interpreter.stored_signal_value(first) == expected_high
            && interpreter.stored_signal_value(second) == expected_low
            && interpreter.stored_signal_value(proxy) == expected_aggregate;
        const bool current_old
            = interpreter.signal_value(first) == zero
            && interpreter.signal_value(second) == zero
            && interpreter.signal_value(proxy) == aggregate_zero;
        const bool current_new
            = interpreter.signal_value(first) == expected_high
            && interpreter.signal_value(second) == expected_low
            && interpreter.signal_value(proxy) == expected_aggregate;
        const auto& logical_container
            = interpreter.container_object_value(object);
        const bool container_old
            = logical_container.elements.size() == 2U
            && logical_container.elements[0U] == zero
            && logical_container.elements[1U] == zero;
        const bool container_new
            = logical_container.elements.size() == 2U
            && logical_container.elements[0U] == expected_high
            && logical_container.elements[1U] == expected_low;

        DriverForceSnapshot snapshot;
        snapshot.force_maps_coherent = maps_absent || maps_installed;
        snapshot.raw_records_preserved = raw_records_preserved;
        snapshot.frames_clear = frames_clear();
        if (maps_absent && stored_old && current_old && container_old) {
            snapshot.phase = DriverForcePhase::unchanged;
        } else if (maps_installed && stored_new && current_old
            && container_old) {
            snapshot.phase = DriverForcePhase::stored_only;
        } else if (maps_installed && stored_new && current_new
            && container_new) {
            snapshot.phase = DriverForcePhase::current;
        }
        return snapshot;
    }
};

struct DriverFamilyTransactionTrace {
    std::array<SignalId, 3U> observed;
    std::array<std::size_t, 3U> counts { };

    static void record(
        void* context,
        const SchedulerTraceRecord& entry) noexcept
    {
        if (entry.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        auto& trace = *static_cast<DriverFamilyTransactionTrace*>(context);
        for (std::size_t index = 0U; index < trace.observed.size(); ++index) {
            if (entry.signal == trace.observed[index]) {
                ++trace.counts[index];
                return;
            }
        }
    }
};

class DriverFamilyOperationExecutor final : public ProcessExecutor {
public:
    using Operation = std::function<void(ProcessExecutionContext&)>;

    DriverFamilyOperationExecutor(
        Interpreter& interpreter,
        Operation operation)
        : interpreter_ { interpreter }
        , operation_ { std::move(operation) }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "driver-family operation resumed away from entry"
            };
        }
        operation_(context);
        interpreter_.set_driver_change_hook({ });
        interpreter_.set_stored_signal_change_hook({ });
        interpreter_.set_signal_change_hook({ });
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    Interpreter& interpreter_;
    Operation operation_;
};

struct ExplicitStrengthForceObservation {
    SignalId proxy_signal { };
    std::string before;
    std::string forced;
    std::string released;
    std::vector<std::string> current_snapshots;
    std::vector<std::pair<SignalId, std::string>> stored_snapshots;
    std::array<std::size_t, 3U> transaction_counts { };
    std::string owner_raw;
    std::string sibling_raw;
    bool owner_force_installed { };
    bool owner_force_cleared { };
    std::size_t raw_driver_callbacks { };
    bool external_contribution_preserved { };
};

void test_driver_force_uses_packed_strength_resolution()
{
    const auto sibling_strength = DriveStrength {
        StrengthRank::strong, StrengthRank::weak
    };
    const auto writer_strength = DriveStrength {
        StrengthRank::weak, StrengthRank::strong
    };
    auto external = PackedLogic4 { 16U, Logic4::z };
    external.set(6U, Logic4::zero);
    const auto external_high = external.extract_bits(8U, 8U);
    const auto external_low = external.extract_bits(0U, 8U);
    auto expected_before = PackedLogic4 { 16U, Logic4::x };
    expected_before.set(6U, Logic4::zero);
    auto expected_forced = expected_before;
    expected_forced.set(6U, Logic4::x);
    for (std::size_t bit = 7U; bit < 10U; ++bit) {
        expected_forced.set(bit, Logic4::one);
    }
    const auto writer_raw = PackedLogic4 { 16U, Logic4::zero };
    const auto sibling_raw = PackedLogic4 { 16U, Logic4::one };
    const auto force_value = PackedLogic4 { 4U, Logic4::one };

    const auto run_family = [&] {
        DriverFamily family { 8U, sibling_strength, writer_strength };
        OwnedDriverDemotionTestAccess::initialize_driver(
            family.interpreter, family.sibling, family.first, family.ones);
        OwnedDriverDemotionTestAccess::initialize_driver(
            family.interpreter, family.sibling, family.second, family.ones);
        OwnedDriverDemotionTestAccess::set_external_driver(
            family.interpreter, family.first, external_high);
        OwnedDriverDemotionTestAccess::set_external_driver(
            family.interpreter, family.second, external_low);

        ExplicitStrengthForceObservation observation;
        observation.proxy_signal = family.proxy;
        DriverFamilyTransactionTrace trace {
            { family.first, family.second, family.proxy }
        };
        family.interpreter.scheduler().set_trace_hook(
            &trace, &DriverFamilyTransactionTrace::record);
        family.interpreter.set_driver_change_hook(
            [&](const ProcessId, const SignalId, const SimulationTick) {
                ++observation.raw_driver_callbacks;
            });
        family.interpreter.set_stored_signal_change_hook(
            [&](const SignalId signal, const SimulationTick) {
                observation.stored_snapshots.emplace_back(
                    signal,
                    family.interpreter.stored_signal_value(family.proxy)
                        .to_msb_string()
                        + ":"
                        + family.interpreter.signal_value(family.proxy)
                            .to_msb_string());
            });
        family.interpreter.set_signal_change_hook(
            [&](const SignalId, const PackedLogic4&, const SimulationTick) {
                observation.current_snapshots.push_back(
                    family.interpreter.signal_value(family.proxy)
                        .to_msb_string());
            });
        family.interpreter.set_process_executor(
            family.writer,
            std::make_unique<DriverFamilyOperationExecutor>(
                family.interpreter,
                [&](ProcessExecutionContext& context) {
                    observation.before
                        = family.interpreter.stored_signal_value(family.proxy)
                            .to_msb_string()
                        + ":"
                        + family.interpreter.signal_value(family.proxy)
                            .to_msb_string();
                    context.force_driver_signal_slice(
                        family.proxy, force_value, 6U);
                    observation.owner_force_installed
                        = OwnedDriverDemotionTestAccess::driver_force_active(
                              family.interpreter, family.first, family.writer)
                        && OwnedDriverDemotionTestAccess::driver_force_active(
                            family.interpreter, family.second, family.writer);
                    observation.forced
                        = family.interpreter.stored_signal_value(family.proxy)
                            .to_msb_string()
                        + ":"
                        + family.interpreter.signal_value(family.proxy)
                            .to_msb_string();
                    context.release_driver_signal_slice(
                        family.proxy, 6U, 4U);
                    observation.owner_force_cleared
                        = OwnedDriverDemotionTestAccess::driver_force_cleared(
                              family.interpreter, family.first, family.writer)
                        && OwnedDriverDemotionTestAccess::driver_force_cleared(
                            family.interpreter, family.second, family.writer);
                    observation.released
                        = family.interpreter.stored_signal_value(family.proxy)
                            .to_msb_string()
                        + ":"
                        + family.interpreter.signal_value(family.proxy)
                            .to_msb_string();
                }));
        const auto run = family.interpreter.run();
        require(run.status == RunStatus::completed,
            "the explicit-strength aggregate force writer completes");
        observation.transaction_counts = trace.counts;
        observation.owner_raw
            = family.interpreter.driver_value(family.writer, family.first)
                .to_msb_string()
            + family.interpreter.driver_value(family.writer, family.second)
                .to_msb_string();
        observation.sibling_raw
            = family.interpreter.driver_value(family.sibling, family.first)
                .to_msb_string()
            + family.interpreter.driver_value(family.sibling, family.second)
                .to_msb_string();
        observation.external_contribution_preserved
            = OwnedDriverDemotionTestAccess::external_driver_matches(
                  family.interpreter, family.first, external_high)
            && OwnedDriverDemotionTestAccess::external_driver_matches(
                family.interpreter, family.second, external_low);
        return observation;
    };

    const auto run_packed = [&] {
        Interpreter interpreter;
        const auto signal = interpreter.add_signal({
            "top.driver_strength_reference", writer_raw,
            ResolutionKind::sv_wire });
        Process sibling_process;
        sibling_process.id = 0U;
        sibling_process.name = "top.driver_strength_reference.sibling";
        sibling_process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        sibling_process.drive_strength = sibling_strength;
        sibling_process.driver_regions = { { signal, 0U, 0U, true } };
        sibling_process.operations = { Halt { } };
        const auto sibling = interpreter.add_process(
            std::move(sibling_process));
        Process writer_process;
        writer_process.id = 1U;
        writer_process.name = "top.driver_strength_reference.writer";
        writer_process.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        writer_process.drive_strength = writer_strength;
        writer_process.driver_regions = { { signal, 0U, 0U, true } };
        writer_process.operations = { Halt { } };
        const auto writer = interpreter.add_process(
            std::move(writer_process));
        OwnedDriverDemotionTestAccess::initialize_driver(
            interpreter, sibling, signal, sibling_raw);
        OwnedDriverDemotionTestAccess::initialize_driver(
            interpreter, writer, signal, writer_raw);
        OwnedDriverDemotionTestAccess::set_external_driver(
            interpreter, signal, external);

        ExplicitStrengthForceObservation observation;
        observation.proxy_signal = signal;
        DriverFamilyTransactionTrace trace { { signal, signal, signal } };
        interpreter.scheduler().set_trace_hook(
            &trace, &DriverFamilyTransactionTrace::record);
        interpreter.set_driver_change_hook(
            [&](const ProcessId, const SignalId, const SimulationTick) {
                ++observation.raw_driver_callbacks;
            });
        interpreter.set_stored_signal_change_hook(
            [&](const SignalId changed, const SimulationTick) {
                observation.stored_snapshots.emplace_back(
                    changed,
                    interpreter.stored_signal_value(signal).to_msb_string()
                        + ":"
                        + interpreter.signal_value(signal).to_msb_string());
            });
        interpreter.set_signal_change_hook(
            [&](const SignalId, const PackedLogic4&, const SimulationTick) {
                observation.current_snapshots.push_back(
                    interpreter.signal_value(signal).to_msb_string());
            });
        interpreter.set_process_executor(
            writer,
            std::make_unique<DriverFamilyOperationExecutor>(
                interpreter,
                [&](ProcessExecutionContext& context) {
                    observation.before
                        = interpreter.stored_signal_value(signal).to_msb_string()
                        + ":"
                        + interpreter.signal_value(signal).to_msb_string();
                    context.force_driver_signal_slice(
                        signal, force_value, 6U);
                    observation.owner_force_installed
                        = OwnedDriverDemotionTestAccess::driver_force_active(
                            interpreter, signal, writer);
                    observation.forced
                        = interpreter.stored_signal_value(signal).to_msb_string()
                        + ":"
                        + interpreter.signal_value(signal).to_msb_string();
                    context.release_driver_signal_slice(signal, 6U, 4U);
                    observation.owner_force_cleared
                        = OwnedDriverDemotionTestAccess::driver_force_cleared(
                            interpreter, signal, writer);
                    observation.released
                        = interpreter.stored_signal_value(signal).to_msb_string()
                        + ":"
                        + interpreter.signal_value(signal).to_msb_string();
                }));
        const auto run = interpreter.run();
        require(run.status == RunStatus::completed,
            "the explicit-strength packed reference completes");
        observation.transaction_counts = trace.counts;
        observation.owner_raw
            = interpreter.driver_value(writer, signal).to_msb_string();
        observation.sibling_raw
            = interpreter.driver_value(sibling, signal).to_msb_string();
        observation.external_contribution_preserved
            = OwnedDriverDemotionTestAccess::external_driver_matches(
                interpreter, signal, external);
        return observation;
    };

    const auto family = run_family();
    const auto packed = run_packed();
    const auto baseline_text = expected_before.to_msb_string();
    const auto forced_text = expected_forced.to_msb_string();
    require(family.before == packed.before
            && family.before == baseline_text + ":" + baseline_text,
        "opposing drive strengths and the external slot resolve before force");
    require(family.forced == packed.forced
            && family.forced == forced_text + ":" + forced_text,
        "the staged resolver applies per-direction strengths during force");
    require(family.released == packed.released
            && family.released == baseline_text + ":" + baseline_text,
        "release restores the packed strength resolution and external slot");
    require(family.owner_raw == packed.owner_raw
            && family.owner_raw == writer_raw.to_msb_string()
            && family.sibling_raw == packed.sibling_raw
            && family.sibling_raw == sibling_raw.to_msb_string(),
        "force and release preserve both original raw driver records");
    require(family.owner_force_installed && family.owner_force_cleared
            && packed.owner_force_installed && packed.owner_force_cleared,
        "both routes install then clear the writer's force maps");
    require(family.external_contribution_preserved
            && packed.external_contribution_preserved,
        "the external contribution remains present through force and release");
    require(family.raw_driver_callbacks == 0U
            && packed.raw_driver_callbacks == 0U,
        "force and release do not emit raw driver-change callbacks");
    const bool family_current_matches
        = family.current_snapshots.size() == 6U
        && std::ranges::all_of(
            family.current_snapshots.begin(),
            family.current_snapshots.begin() + 3,
            [&](const std::string& snapshot) {
                return snapshot == forced_text;
            })
        && std::ranges::all_of(
            family.current_snapshots.begin() + 3,
            family.current_snapshots.end(),
            [&](const std::string& snapshot) {
                return snapshot == baseline_text;
            });
    const bool packed_current_matches = packed.current_snapshots
        == std::vector<std::string> { forced_text, baseline_text };
    if (!family_current_matches || !packed_current_matches) {
        const auto describe = [](const std::vector<std::string>& snapshots) {
            std::string result;
            for (const auto& snapshot : snapshots) {
                if (!result.empty()) {
                    result += ", ";
                }
                result += snapshot;
            }
            return result;
        };
        throw std::runtime_error {
            "force/release observer snapshots differ: family=["
            + describe(family.current_snapshots) + "] packed=["
            + describe(packed.current_snapshots) + "] expected=["
            + forced_text + ", " + baseline_text + "]"
        };
    }
    require(family_current_matches && packed_current_matches,
        "all value observers see complete force then release family states");
    const bool stored_callbacks_match
        = family.stored_snapshots.size() == 2U
            && family.stored_snapshots[0U].first == family.proxy_signal
            && family.stored_snapshots[1U].first == family.proxy_signal
            && family.stored_snapshots[0U].second
                == forced_text + ":" + forced_text
            && family.stored_snapshots[1U].second
                == baseline_text + ":" + baseline_text
            // Ordinary packed driver force/release changes the resolved
            // stored value without a stored hook. Aggregate aliases retain
            // their proxy-only hook after coherent family publication.
            && packed.stored_snapshots.empty();
    if (!stored_callbacks_match) {
        const auto describe = [](const auto& snapshots) {
            std::string result;
            for (const auto& snapshot : snapshots) {
                if (!result.empty()) {
                    result += ", ";
                }
                result += std::to_string(snapshot.first) + "=" + snapshot.second;
            }
            return result;
        };
        throw std::runtime_error {
            "stored force/release snapshots differ: family=["
            + describe(family.stored_snapshots) + "] packed=["
            + describe(packed.stored_snapshots) + "] expected-family=["
            + forced_text + ":" + forced_text + ", "
            + baseline_text + ":" + baseline_text + "] expected-packed=[]"
        };
    }
    require(stored_callbacks_match,
        "stored observers run after coherent force and release publication");
    require(family.transaction_counts
                == std::array<std::size_t, 3U> { 2U, 2U, 2U }
            && packed.transaction_counts
                == std::array<std::size_t, 3U> { 2U, 0U, 0U },
        "both leaves and the proxy transact once on force and release");
}

struct WriteHooks {
    SignalId first { };
    SignalId second { };
    SignalId proxy { };
    ProcessId writer { };
    HookSummary counts;

    static void record(HookCounts& counts, const SignalId signal,
        const SignalId first, const SignalId second, const SignalId proxy) noexcept
    {
        if (signal == first) {
            ++counts.signals[0U];
        } else if (signal == second) {
            ++counts.signals[1U];
        } else if (signal == proxy) {
            ++counts.signals[2U];
        }
    }

    void install(Interpreter& interpreter)
    {
        interpreter.set_driver_change_hook(
            [this](const ProcessId process, const SignalId signal,
                const SimulationTick) {
                if (process == writer) {
                    record(counts.raw, signal, first, second, proxy);
                }
            });
        interpreter.set_stored_signal_change_hook(
            [this](const SignalId signal, const SimulationTick) {
                record(counts.stored, signal, first, second, proxy);
            });
        interpreter.set_signal_change_hook(
            [this](const SignalId signal, const PackedLogic4&,
                const SimulationTick) {
                record(counts.current, signal, first, second, proxy);
            });
    }

    void clear(Interpreter& interpreter) noexcept
    {
        interpreter.set_driver_change_hook({ });
        interpreter.set_stored_signal_change_hook({ });
        interpreter.set_signal_change_hook({ });
    }

    void reset() noexcept
    {
        counts = { };
    }
};

class ScopedWriteHooks {
public:
    ScopedWriteHooks(Interpreter& interpreter, WriteHooks& hooks) noexcept
        : interpreter_ { interpreter }
        , hooks_ { hooks }
    {
    }

    ~ScopedWriteHooks()
    {
        hooks_.clear(interpreter_);
    }

    ScopedWriteHooks(const ScopedWriteHooks&) = delete;
    ScopedWriteHooks& operator=(const ScopedWriteHooks&) = delete;

private:
    Interpreter& interpreter_;
    WriteHooks& hooks_;
};

class DriverFamilyFailureWriter final : public ProcessExecutor {
public:
    DriverFamilyFailureWriter(
        DriverFamily& family,
        WriteReport& report,
        const bool count_allocations,
        const std::size_t failure_index)
        : family_ { family }
        , report_ { report }
        , count_allocations_ { count_allocations }
        , failure_index_ { failure_index }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "driver-family failure writer resumed away from entry"
            };
        }
        report_.executor_called = true;
        report_.initial_state_ready
            = family_.complete_state(
                  family_.zero, family_.aggregate_zero)
            && family_.sibling_records_unchanged()
            && family_.frames_clear();
        WriteHooks hooks {
            family_.first, family_.second, family_.proxy, family_.writer, { }
        };
        hooks.install(family_.interpreter);
        ScopedWriteHooks clear_hooks { family_.interpreter, hooks };

        if (count_allocations_) {
            begin_allocation_count();
        } else {
            arm_allocation_failure(failure_index_);
        }
        try {
            context.write_blocking(family_.proxy, family_.aggregate_ones);
        } catch (const std::bad_alloc&) {
            report_.failed = true;
        } catch (...) {
            clear_allocation_failure();
            if (count_allocations_) {
                (void)end_allocation_count();
            }
            throw;
        }
        clear_allocation_failure();
        if (count_allocations_) {
            report_.allocation_count = end_allocation_count();
        }

        report_.after_attempt = family_.snapshot();
        report_.attempt_hooks = hooks.counts;
        report_.sibling_records_preserved
            = family_.sibling_records_unchanged();
        report_.frames_clear_after_attempt = family_.frames_clear();

        if (report_.failed) {
            hooks.reset();
            context.write_blocking(family_.proxy, family_.aggregate_ones);
            report_.retry_completed = true;
            report_.retry_state_complete = family_.complete_state(
                family_.ones, family_.aggregate_ones);
            report_.retry_hooks = hooks.counts;
            report_.frames_clear_after_retry = family_.frames_clear();
        }

        hooks.reset();
        context.write_blocking(family_.proxy, family_.aggregate_zero);
        report_.later_write_completed = true;
        report_.later_state_complete = family_.complete_state(
            family_.zero, family_.aggregate_zero);
        report_.later_hooks = hooks.counts;
        report_.frames_clear_after_later_write = family_.frames_clear();
        report_.sibling_records_preserved
            = report_.sibling_records_preserved
            && family_.sibling_records_unchanged();

        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    DriverFamily& family_;
    WriteReport& report_;
    bool count_allocations_ { };
    std::size_t failure_index_ { };
};

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

void require_leaf_hooks(const HookCounts& hooks, const bool changed,
    const char* const message)
{
    const auto expected = changed ? 1U : 0U;
    require(hooks.signals[0U] == expected
            && hooks.signals[1U] == expected
            && hooks.signals[2U] <= 1U,
        message);
}

void require_attempt_phase(const WriteReport& report)
{
    require(report.after_attempt.coherent,
        "an injected write leaves a coherent unchanged/raw/stored/current family");
    const bool raw_changed
        = report.after_attempt.phase != PublicationPhase::unchanged;
    const bool stored_changed
        = report.after_attempt.phase == PublicationPhase::stored_only
        || report.after_attempt.phase == PublicationPhase::current;
    require_leaf_hooks(report.attempt_hooks.raw, raw_changed,
        "raw notifications cover both leaves before family finalization");
    require_leaf_hooks(report.attempt_hooks.stored, stored_changed,
        "stored notifications cover both leaves only after a family store");
    require(report.attempt_hooks.current.signals[0U] <= 1U
            && report.attempt_hooks.current.signals[1U] <= 1U
            && report.attempt_hooks.current.signals[2U] <= 1U,
        "current observers receive at most one callback per family member");
}

WriteReport run_case(
    const std::size_t width,
    const bool count_allocations,
    const std::size_t failure_index)
{
    DriverFamily family(width);
    WriteReport report;
    family.interpreter.set_process_executor(
        family.writer,
        std::make_unique<DriverFamilyFailureWriter>(
            family, report, count_allocations, failure_index));
    const auto result = family.interpreter.run();
    require(result.status == RunStatus::completed,
        "the driver-family checked executor completes after retrying failures");
    require(report.executor_called,
        "the checked executor reaches the whole-proxy blocking write");
    require(report.initial_state_ready,
        "both owners are registered before the injected family write");
    require(family.sibling_records_unchanged(),
        "both preexisting owners retain their per-leaf values and strengths");
    require(report.sibling_records_preserved,
        "the injected attempt never changes the preexisting owner's slots or strengths");
    require(report.frames_clear_after_attempt,
        "the whole-family attempt leaves no open aggregate or container frame");
    require(report.later_write_completed && report.later_state_complete
            && report.frames_clear_after_later_write,
        "a later whole-proxy write succeeds after the injected attempt");
    if (report.failed) {
        require_attempt_phase(report);
        require(report.retry_completed && report.retry_state_complete
                && report.frames_clear_after_retry,
            "a failed whole-family write retries with no stale aggregate frame");
    } else {
        require(report.after_attempt.coherent
                && report.after_attempt.phase == PublicationPhase::current,
            "the terminal allocation index completes the full family publication");
    }
    return report;
}

void sweep_driver_family_write(const std::size_t width)
{
    const auto measured = run_case(width, true, 0U);
    const auto allocation_count = measured.allocation_count;
    require(allocation_count != 0U,
        "the checked whole-proxy write exercises real allocation sites");
    require(allocation_count <= maximum_allocation_sites,
        "the injected raw-driver sweep stays within its explicit site cap");

    std::array<std::size_t, 4U> phase_counts { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count;
        ++failure_index) {
        const auto report = run_case(width, false, failure_index);
        require(report.failed == (failure_index < allocation_count),
            "each measured allocation cut fails once and the terminal cut succeeds");
        if (!report.failed) {
            continue;
        }
        ++phase_counts[static_cast<std::size_t>(report.after_attempt.phase)];
    }

    require(phase_counts[static_cast<std::size_t>(PublicationPhase::unchanged)]
                != 0U
            && phase_counts[static_cast<std::size_t>(PublicationPhase::raw_only)]
                != 0U,
        "the sweep reaches both pre-install and post-raw-install failures");
    std::cout << "aggregate proxy driver-family allocation width=" << width
              << " sites=" << allocation_count
              << " failures=" << allocation_count
              << " unchanged="
              << phase_counts[static_cast<std::size_t>(PublicationPhase::unchanged)]
              << " raw_only="
              << phase_counts[static_cast<std::size_t>(PublicationPhase::raw_only)]
              << " stored_only="
              << phase_counts[static_cast<std::size_t>(PublicationPhase::stored_only)]
              << " current="
              << phase_counts[static_cast<std::size_t>(PublicationPhase::current)]
              << '\n';
}

class DriverForceAllocationExecutor final : public ProcessExecutor {
public:
    DriverForceAllocationExecutor(
        DriverFamily& family,
        DriverForceReport& report,
        const bool count_allocations,
        const std::size_t failure_index)
        : family_ { family }
        , report_ { report }
        , count_allocations_ { count_allocations }
        , failure_index_ { failure_index }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "driver-force allocation writer resumed away from entry"
            };
        }
        report_.executor_called = true;
        auto force_value = PackedLogic4 { 2U, Logic4::one };
        auto retry_force_value = PackedLogic4 { 2U, Logic4::one };
        if (count_allocations_) {
            begin_allocation_count();
        } else {
            arm_allocation_failure(failure_index_);
        }
        try {
            context.force_driver_signal_slice(
                family_.proxy, std::move(force_value), family_.width - 1U);
        } catch (const std::bad_alloc&) {
            report_.failed = true;
        } catch (...) {
            clear_allocation_failure();
            if (count_allocations_) {
                (void)end_allocation_count();
            }
            throw;
        }
        clear_allocation_failure();
        if (count_allocations_) {
            report_.allocation_count = end_allocation_count();
        }

        report_.after_attempt = family_.force_snapshot();
        if (report_.failed) {
            context.force_driver_signal_slice(
                family_.proxy, std::move(retry_force_value),
                family_.width - 1U);
            report_.retry_completed = true;
            report_.after_retry = family_.force_snapshot();
        } else {
            report_.after_retry = report_.after_attempt;
        }

        context.release_driver_signal_slice(
            family_.proxy, family_.width - 1U, 2U);
        report_.release_completed = true;
        report_.after_release = family_.force_snapshot();
        context.write_blocking(family_.proxy, family_.aggregate_zero);
        report_.later_write_completed = true;
        report_.later_state_complete = family_.complete_state(
            family_.zero, family_.aggregate_zero);

        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    DriverFamily& family_;
    DriverForceReport& report_;
    bool count_allocations_ { };
    std::size_t failure_index_ { };
};

DriverForceReport run_driver_force_case(
    const std::size_t width,
    const bool count_allocations,
    const std::size_t failure_index,
    const DriverStrengthMode strength_mode)
{
    const auto sibling_strength
        = strength_mode == DriverStrengthMode::default_strength
        ? DriveStrength { }
        : DriveStrength {
            StrengthRank::strong, StrengthRank::weak
        };
    const auto writer_strength
        = strength_mode == DriverStrengthMode::default_strength
        ? DriveStrength { }
        : DriveStrength {
            StrengthRank::weak, StrengthRank::strong
        };
    // Both raw slots are zero. In explicit mode the sibling drives a strong
    // zero while the forced writer contributes a strong one, so the selected
    // resolved bits become X as required by the existing force snapshot.
    DriverFamily family {
        width, sibling_strength, writer_strength
    };
    DriverForceReport report;
    family.interpreter.set_process_executor(
        family.writer,
        std::make_unique<DriverForceAllocationExecutor>(
            family, report, count_allocations, failure_index));
    const auto result = family.interpreter.run();
    require(result.status == RunStatus::completed,
        "the driver-force allocation executor completes after retrying failures");
    require(report.executor_called,
        "the driver-force allocation executor reaches its cross-leaf slice");
    require(report.after_attempt.phase != DriverForcePhase::invalid
            && report.after_attempt.force_maps_coherent
            && report.after_attempt.raw_records_preserved
            && report.after_attempt.frames_clear,
        "allocation failure cannot expose mixed force maps or partial family values");
    require(report.after_retry.phase == DriverForcePhase::current
            && report.after_retry.force_maps_coherent
            && report.after_retry.raw_records_preserved
            && report.after_retry.frames_clear,
        "retry installs the complete cross-leaf driver-force state");
    require(report.release_completed
            && report.after_release.phase == DriverForcePhase::unchanged
            && report.after_release.force_maps_coherent
            && report.after_release.raw_records_preserved
            && report.after_release.frames_clear,
        "release clears the complete selected family without changing raw drivers");
    require(report.later_write_completed && report.later_state_complete,
        "an ordinary aggregate write succeeds after driver-force retry and release");
    if (count_allocations) {
        require(!report.failed
                && report.after_attempt.phase == DriverForcePhase::current,
            "the measured driver-force operation completes without injection");
    } else if (report.failed) {
        require(report.after_attempt.phase == DriverForcePhase::unchanged
                || report.after_attempt.phase == DriverForcePhase::current,
            "driver-force allocation failures leave the family unchanged or fully published");
    } else {
        require(report.after_attempt.phase == DriverForcePhase::current,
            "the terminal driver-force allocation cut publishes the whole family");
    }
    return report;
}

void sweep_driver_family_force(
    const std::size_t width,
    const DriverStrengthMode strength_mode)
{
    const auto measured = run_driver_force_case(
        width, true, 0U, strength_mode);
    const auto allocation_count = measured.allocation_count;
    require(allocation_count != 0U,
        "the cross-leaf driver-force operation exercises real allocation sites");
    require(allocation_count <= maximum_allocation_sites,
        "the driver-force allocation sweep stays within its explicit site cap");

    std::size_t failures { };
    std::array<std::size_t, 2U> failure_phases { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count; ++failure_index) {
        const auto report
            = run_driver_force_case(
                width, false, failure_index, strength_mode);
        require(report.failed == (failure_index < allocation_count),
            "each measured driver-force allocation cut fails once and the terminal cut succeeds");
        if (report.failed) {
            ++failures;
            const auto phase = report.after_attempt.phase
                == DriverForcePhase::unchanged
                ? 0U
                : 1U;
            ++failure_phases[phase];
        }
    }
    require(failures == allocation_count,
        "the force-family sweep fires every measured allocation fault");
    std::cout << "aggregate proxy driver-force allocation width=" << width
              << " strength=" << driver_strength_mode_name(strength_mode)
              << " sites=" << allocation_count
              << " failures=" << failures
              << " unchanged=" << failure_phases[0U]
              << " complete=" << failure_phases[1U] << '\n';
}

struct DriverForceReleaseReport {
    bool executor_called { };
    bool failed { };
    bool retry_completed { };
    std::size_t allocation_count { };
    DriverForceSnapshot before_release;
    DriverForceSnapshot after_attempt;
    DriverForceSnapshot after_retry;
    bool later_write_completed { };
    bool later_state_complete { };
};

class DriverForceReleaseAllocationExecutor final : public ProcessExecutor {
public:
    DriverForceReleaseAllocationExecutor(
        DriverFamily& family,
        DriverForceReleaseReport& report,
        const bool count_allocations,
        const std::size_t failure_index)
        : family_ { family }
        , report_ { report }
        , count_allocations_ { count_allocations }
        , failure_index_ { failure_index }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        if (start != 0U) {
            throw std::logic_error {
                "driver-force release writer resumed away from entry"
            };
        }
        report_.executor_called = true;
        auto force_value = PackedLogic4 { 2U, Logic4::one };
        context.force_driver_signal_slice(
            family_.proxy, std::move(force_value), family_.width - 1U);
        report_.before_release = family_.force_snapshot();
        require(report_.before_release.phase == DriverForcePhase::current
                && report_.before_release.force_maps_coherent
                && report_.before_release.raw_records_preserved
                && report_.before_release.frames_clear,
            "the release allocation scope starts with a complete cross-leaf force");

        if (count_allocations_) {
            begin_allocation_count();
        } else {
            arm_allocation_failure(failure_index_);
        }
        try {
            context.release_driver_signal_slice(
                family_.proxy, family_.width - 1U, 2U);
        } catch (const std::bad_alloc&) {
            report_.failed = true;
        } catch (...) {
            clear_allocation_failure();
            if (count_allocations_) {
                (void)end_allocation_count();
            }
            throw;
        }
        clear_allocation_failure();
        if (count_allocations_) {
            report_.allocation_count = end_allocation_count();
        }
        report_.after_attempt = family_.force_snapshot();

        if (report_.failed) {
            context.release_driver_signal_slice(
                family_.proxy, family_.width - 1U, 2U);
            report_.retry_completed = true;
            report_.after_retry = family_.force_snapshot();
        } else {
            report_.after_retry = report_.after_attempt;
        }
        context.write_blocking(family_.proxy, family_.aggregate_zero);
        report_.later_write_completed = true;
        report_.later_state_complete = family_.complete_state(
            family_.zero, family_.aggregate_zero);

        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::halt;
        return result;
    }

private:
    DriverFamily& family_;
    DriverForceReleaseReport& report_;
    bool count_allocations_ { };
    std::size_t failure_index_ { };
};

DriverForceReleaseReport run_driver_force_release_case(
    const std::size_t width,
    const bool count_allocations,
    const std::size_t failure_index,
    const DriverStrengthMode strength_mode)
{
    const auto sibling_strength
        = strength_mode == DriverStrengthMode::default_strength
        ? DriveStrength { }
        : DriveStrength {
            StrengthRank::strong, StrengthRank::weak
        };
    const auto writer_strength
        = strength_mode == DriverStrengthMode::default_strength
        ? DriveStrength { }
        : DriveStrength {
            StrengthRank::weak, StrengthRank::strong
        };
    // The same opposing-strength resolution applies to the release setup;
    // removing the force restores the stronger sibling zero.
    DriverFamily family {
        width, sibling_strength, writer_strength
    };
    DriverForceReleaseReport report;
    family.interpreter.set_process_executor(
        family.writer,
        std::make_unique<DriverForceReleaseAllocationExecutor>(
            family, report, count_allocations, failure_index));
    const auto result = family.interpreter.run();
    require(result.status == RunStatus::completed
            && report.executor_called,
        "the checked force-release executor completes");
    require(report.before_release.phase == DriverForcePhase::current
            && report.after_attempt.force_maps_coherent
            && report.after_attempt.raw_records_preserved
            && report.after_attempt.frames_clear,
        "release allocation starts from and preserves coherent family state");
    require(report.after_attempt.phase == DriverForcePhase::current
            || report.after_attempt.phase == DriverForcePhase::unchanged,
        "an injected release allocation leaves the complete force or released state");
    const auto final_release = report.failed
        ? report.after_retry
        : report.after_attempt;
    require(final_release.phase == DriverForcePhase::unchanged
            && final_release.force_maps_coherent
            && final_release.raw_records_preserved
            && final_release.frames_clear,
        "release retry clears every selected owner map and projection");
    require(report.later_write_completed && report.later_state_complete,
        "an ordinary family write succeeds after release failure and retry");
    if (count_allocations) {
        require(!report.failed
                && report.after_attempt.phase == DriverForcePhase::unchanged,
            "the measured force-release operation completes without injection");
    }
    return report;
}

void sweep_driver_family_force_release(
    const std::size_t width,
    const DriverStrengthMode strength_mode)
{
    const auto measured = run_driver_force_release_case(
        width, true, 0U, strength_mode);
    const auto allocation_count = measured.allocation_count;
    require(allocation_count != 0U
            && allocation_count <= maximum_allocation_sites,
        "the driver-force release sweep has measured sites within its fixed cap");
    std::array<std::size_t, 2U> failure_phases { };
    for (std::size_t failure_index = 0U;
        failure_index <= allocation_count; ++failure_index) {
        const auto report
            = run_driver_force_release_case(
                width, false, failure_index, strength_mode);
        require(report.failed == (failure_index < allocation_count),
            "each measured driver-force release allocation fails once before the terminal cut");
        if (report.failed) {
            const auto phase = report.after_attempt.phase
                == DriverForcePhase::current
                ? 0U
                : 1U;
            ++failure_phases[phase];
        }
    }
    std::cout << "aggregate proxy driver-force release allocation width="
              << width
              << " strength=" << driver_strength_mode_name(strength_mode)
              << " sites=" << allocation_count
              << " failures=" << allocation_count
              << " force_intact=" << failure_phases[0U]
              << " released=" << failure_phases[1U] << '\n';
}

} // namespace

void test_aggregate_proxy_driver_family_allocation_failures()
{
    test_driver_force_uses_packed_strength_resolution();
    for (const auto width : std::array<std::size_t, 2U> { 65U, 129U }) {
        sweep_driver_family_write(width);
        for (const auto mode : std::array<DriverStrengthMode, 2U> {
                 DriverStrengthMode::default_strength,
                 DriverStrengthMode::opposing_explicit }) {
            sweep_driver_family_force(width, mode);
            sweep_driver_family_force_release(width, mode);
        }
    }
}

} // namespace fsim::tests::runtime

int main()
{
    try {
        fsim::tests::runtime::test_aggregate_proxy_driver_family_allocation_failures();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aggregate proxy driver-family allocation failure test failed: "
                  << error.what() << '\n';
        fsim::tests::runtime::staging_failure_support::
            clear_allocation_failure();
        return 1;
    }
}
