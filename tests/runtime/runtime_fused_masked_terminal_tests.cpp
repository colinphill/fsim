// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TerminalExecutor final : public FusedMaskedRegionExecutor {
public:
    TerminalExecutor(Interpreter& interpreter,
        std::vector<ProcessId> members, const ProcessId first,
        const ProcessId second, const ProcessId terminal,
        const SignalId input, const SignalId aggregate,
        const SignalId output, const std::uint32_t width)
        : interpreter_(interpreter)
        , members_(std::move(members))
        , first_(first)
        , second_(second)
        , terminal_(terminal)
        , input_(input)
        , aggregate_(aggregate)
        , output_(output)
        , width_(width)
    {
        const auto words = (width + 63U) / 64U;
        aggregate_aval_.resize(words);
        aggregate_bval_.resize(words);
        aggregate_mask_.resize(words);
        output_aval_.resize(words);
        output_bval_.resize(words);
        output_mask_.resize(words);
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&,
        const std::span<const std::uint64_t> activation) override
    {
        const auto selected = [&](const ProcessId id) {
            const auto found = std::ranges::find(members_, id);
            require(found != members_.end(), "terminal member is in its region");
            const auto index = static_cast<std::size_t>(found - members_.begin());
            return (activation[index / 64U] & (UINT64_C(1) << (index % 64U))) != 0U;
        };
        const auto first = selected(first_);
        const auto second = selected(second_);
        const auto terminal = selected(terminal_);
        const auto& input = interpreter_.signal_value(input_);
        const auto& aggregate = interpreter_.signal_value(aggregate_);
        std::ranges::copy(input.aval_words(), aggregate_aval_.begin());
        std::ranges::copy(input.bval_words(), aggregate_bval_.begin());
        std::ranges::copy(aggregate.aval_words(), output_aval_.begin());
        std::ranges::copy(aggregate.bval_words(), output_bval_.begin());
        std::ranges::fill(aggregate_mask_, UINT64_C(0));
        std::ranges::fill(output_mask_, UINT64_C(0));
        const auto split = width_ / 2U;
        for (std::uint32_t bit = 0U; bit < width_; ++bit) {
            if ((bit < split && first) || (bit >= split && second)) {
                aggregate_mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
            }
            if (terminal) {
                output_mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
            }
        }
        aggregate_active_ = static_cast<std::uint32_t>(first || second);
        output_active_ = static_cast<std::uint32_t>(terminal);
        const auto words = static_cast<std::uint32_t>(aggregate_mask_.size());
        slots_[0] = { aggregate_, width_, words, &aggregate_active_,
            aggregate_aval_.data(), aggregate_bval_.data(),
            aggregate_mask_.data() };
        slots_[1] = { output_, width_, words, &output_active_,
            output_aval_.data(), output_bval_.data(), output_mask_.data() };
        return FusedStaticCohortResume { slots_, { } };
    }

private:
    Interpreter& interpreter_;
    std::vector<ProcessId> members_;
    ProcessId first_, second_, terminal_;
    SignalId input_, aggregate_, output_;
    std::uint32_t width_;
    std::uint32_t aggregate_active_ { }, output_active_ { };
    std::vector<std::uint64_t> aggregate_aval_, aggregate_bval_;
    std::vector<std::uint64_t> aggregate_mask_;
    std::vector<std::uint64_t> output_aval_, output_bval_, output_mask_;
    std::array<ProcessUpdateSlotView, 2U> slots_ { };
};

class AliasOwnerExecutor final : public FusedMaskedRegionExecutor {
public:
    AliasOwnerExecutor(Interpreter& interpreter,
        std::vector<ProcessId> members, const ProcessId high_owner,
        const ProcessId low_owner, const SignalId backing,
        const SignalId input, const SignalId output)
        : interpreter_(interpreter)
        , members_(std::move(members))
        , high_owner_(high_owner)
        , low_owner_(low_owner)
        , backing_(backing)
        , input_(input)
        , output_(output)
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&,
        const std::span<const std::uint64_t> activation) override
    {
        const auto selected = [&](const ProcessId id) {
            const auto found = std::ranges::find(members_, id);
            require(found != members_.end(), "alias owner belongs to the region");
            const auto index = static_cast<std::size_t>(found - members_.begin());
            return (activation[index / 64U] & (UINT64_C(1) << (index % 64U))) != 0U;
        };
        const auto high = selected(high_owner_);
        const auto low = selected(low_owner_);
        auto joined = PackedLogic4(130U, Logic4::z);
        if (high) {
            joined.insert_bits(
                interpreter_.signal_value(backing_).extract_bits(65U, 65U),
                65U);
        }
        if (low) {
            joined.insert_bits(interpreter_.signal_value(input_), 0U);
        }
        std::ranges::copy(joined.aval_words(), aval_.begin());
        std::ranges::copy(joined.bval_words(), bval_.begin());
        mask_.fill(0U);
        for (std::size_t bit = 0U; bit < 130U; ++bit) {
            if ((bit < 65U && low) || (bit >= 65U && high)) {
                mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
            }
        }
        active_ = static_cast<std::uint32_t>(high || low);
        slot_ = { output_, 130U, 3U, &active_,
            aval_.data(), bval_.data(), mask_.data() };
        return FusedStaticCohortResume { std::span { &slot_, 1U }, { } };
    }

private:
    Interpreter& interpreter_;
    std::vector<ProcessId> members_;
    ProcessId high_owner_, low_owner_;
    SignalId backing_, input_, output_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 3U> aval_ { }, bval_ { }, mask_ { };
    ProcessUpdateSlotView slot_ { };
};

struct Observation {
    std::vector<std::tuple<SimulationTick, std::uint64_t, std::string>> events;
    std::vector<std::string> settled;
    std::vector<std::string> drivers;
    FusedMaskedRegionCounters counters;
};

Observation run_terminal(const std::uint32_t width, const bool fused,
    const bool terminal_first)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    const auto input = interpreter.add_signal(
        { "input", PackedLogic4(width, Logic4::zero) });
    const auto auxiliary = interpreter.add_signal(
        { "auxiliary", PackedLogic4(1U, Logic4::zero) });
    const auto aggregate = interpreter.add_signal(
        { "aggregate", PackedLogic4(width, Logic4::z),
            ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal(
        { "output", PackedLogic4(width, Logic4::z),
            width == 129U ? ResolutionKind::sv_wire
                          : ResolutionKind::none });
    const auto add_terminal = [&] {
        Process consumer;
        consumer.id = terminal_first ? 0U : 2U;
        consumer.name = "terminal";
        consumer.register_count = 1U;
        consumer.static_sensitivity = { { aggregate, EdgeKind::any },
            { input, EdgeKind::any } };
        consumer.driver_regions = { { output, 0U,
            width == 129U ? 0U : width, true } };
        consumer.operations = { ReadSignal { 0U, aggregate },
            WriteUpdate { output, 0U }, WaitSensitivity { }, Jump { 0U } };
        return interpreter.add_process(std::move(consumer));
    };
    const auto early_terminal = terminal_first
        ? std::optional<ProcessId> { add_terminal() } : std::nullopt;
    std::array<ProcessId, 2U> owners;
    const auto split = width / 2U;
    for (std::uint32_t index = 0U; index < 2U; ++index) {
        const auto offset = index == 0U ? 0U : split;
        const auto size = index == 0U ? split : width - split;
        Process process;
        process.id = index + (terminal_first ? 1U : 0U);
        process.name = "owner_" + std::to_string(index);
        process.register_count = 2U;
        process.static_sensitivity = { { input, EdgeKind::any } };
        if (index != 0U) {
            process.static_sensitivity.push_back(
                { auxiliary, EdgeKind::any });
        }
        process.driver_regions = { { aggregate, offset, size, false } };
        process.operations = { ReadSignal { 0U, input },
            Extract { 1U, 0U, offset, size },
            WriteUpdateSlice { aggregate, 1U, offset },
            WaitSensitivity { }, Jump { 0U } };
        owners[index] = interpreter.add_process(std::move(process));
    }
    const auto terminal = terminal_first
        ? *early_terminal : add_terminal();
    Process observer;
    observer.id = 3U;
    observer.name = "observer";
    observer.initialize = false;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = { Display { "boundary" },
        WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(observer));
    Observation result;
    interpreter.set_output_hook([&](ProcessId, std::string_view,
                                    bool, SimulationTick time,
                                    std::uint64_t delta) {
        result.events.emplace_back(time, delta,
            interpreter.signal_value(output).to_msb_string());
    });
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::one), 1U, 0U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::x), 2U, 0U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::z), 2U, 2U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(width, Logic4::zero), 3U, 0U);
    interpreter.start();
    if (fused) {
        const auto candidates = interpreter.fused_masked_region_candidates();
        const auto found = std::ranges::find_if(candidates,
            [&](const auto& candidate) {
                const auto expected = terminal_first
                    ? std::vector<ProcessId> { terminal, owners[0], owners[1] }
                    : std::vector<ProcessId> { owners[0], owners[1], terminal };
                return candidate.members == expected
                    && candidate.outputs == std::vector<SignalId> {
                        aggregate, output };
            });
        require(found != candidates.end(),
            "the mixed sink has a graph-certified terminal candidate");
        std::vector<std::vector<Process::DriverRegion>> writes;
        for (const auto id : found->members) {
            const auto& program = interpreter.process_program(id);
            writes.emplace_back(program.driver_regions.begin(),
                program.driver_regions.end());
        }
        interpreter.install_fused_masked_region(found->region_id,
            std::move(writes), std::make_unique<TerminalExecutor>(
                interpreter, found->members, owners[0], owners[1], terminal,
                input, aggregate, output, width));
    }
    for (SimulationTick time = 0U; time <= 3U; ++time) {
        const auto run = interpreter.run(time);
        require(run.status == RunStatus::completed
                || run.status == RunStatus::time_limit,
            "mixed sink reaches each observation time");
        result.settled.push_back(
            interpreter.signal_value(output).to_msb_string());
    }
    for (const auto id : owners) {
        result.drivers.push_back(
            interpreter.driver_value(id, aggregate).to_msb_string());
    }
    result.drivers.push_back(
        interpreter.driver_value(terminal, output).to_msb_string());
    result.counters = interpreter.fused_masked_region_counters();
    return result;
}

Observation run_normalized_owner(const bool fused)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    const auto backing = interpreter.add_signal(
        { "container_backing", PackedLogic4(130U, Logic4::z) });
    const auto input = interpreter.add_signal(
        { "input", PackedLogic4(65U, Logic4::zero) });
    const auto output = interpreter.add_signal(
        { "aggregate", PackedLogic4(130U, Logic4::z),
            ResolutionKind::sv_wire });
    ContainerType type;
    type.fixed = true;
    type.element_width = 65U;
    type.index_left = 1;
    type.index_right = 0;
    type.dimensions = { { 1, 0 } };
    const auto object = interpreter.add_container_object(
        { "array", default_container_value(type), std::nullopt });
    interpreter.add_container_signal_alias({ object, backing, true, true });
    Process high;
    high.id = 0U;
    high.name = "array_reader";
    high.register_count = 2U;
    high.container_register_count = 1U;
    high.container_register_types = { type };
    high.static_sensitivity = { { backing, EdgeKind::any } };
    high.driver_regions = { { output, 65U, 65U, false } };
    high.operations = {
        LoadConstant { 0U, PackedLogic4::from_aval_bval(32U, 1U, 0U) },
        ReadContainerObject { 0U, object },
        ContainerRead { 1U, 0U, 0U, true, false, false },
        WriteUpdateSlice { output, 1U, 65U },
        WaitSensitivity { }, Jump { 0U }
    };
    const auto high_id = interpreter.add_process(std::move(high));
    Process low;
    low.id = 1U;
    low.name = "direct_reader";
    low.register_count = 1U;
    low.static_sensitivity = { { input, EdgeKind::any } };
    low.driver_regions = { { output, 0U, 65U, false } };
    low.operations = { ReadSignal { 0U, input },
        WriteUpdateSlice { output, 0U, 0U },
        WaitSensitivity { }, Jump { 0U } };
    const auto low_id = interpreter.add_process(std::move(low));
    Process observer;
    observer.id = 2U;
    observer.name = "observer";
    observer.initialize = false;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = { Display { "boundary" },
        WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(observer));
    Observation result;
    interpreter.set_output_hook([&](ProcessId, std::string_view,
                                    bool, SimulationTick time,
                                    std::uint64_t delta) {
        result.events.emplace_back(time, delta,
            interpreter.signal_value(output).to_msb_string());
    });
    interpreter.schedule_signal_at(backing,
        PackedLogic4(130U, Logic4::one), 1U, 0U);
    interpreter.schedule_signal_at(input,
        PackedLogic4(65U, Logic4::one), 1U, 1U);
    interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
        std::numeric_limits<StableOrder>::max(), [&](Scheduler&) {
            auto changed = interpreter.container_object_value(object);
            changed.elements.front() = PackedLogic4(65U, Logic4::x);
            interpreter.deposit_container_object(object, std::move(changed));
        });
    interpreter.schedule_signal_at(input,
        PackedLogic4(65U, Logic4::z), 3U, 0U);
    interpreter.start();
    if (fused) {
        const auto& original = interpreter.process_program(high_id);
        const auto& normalized = interpreter.fused_masked_member_program(high_id);
        require(operation_holds<ReadContainerObject>(original.operations[1U])
                && operation_holds<ReadSignal>(normalized.operations[1U]),
            "the original array read remains intact while the private copy normalizes");
        const auto candidates = interpreter.fused_masked_region_candidates();
        const auto found = std::ranges::find_if(candidates,
            [&](const auto& candidate) {
                return candidate.members == std::vector<ProcessId> {
                        high_id, low_id }
                    && candidate.outputs == std::vector<SignalId> { output };
            });
        require(found != candidates.end(),
            "a normalized owned slice participates in the generic masked region");
        interpreter.install_fused_masked_region(found->region_id,
            { { { output, 65U, 65U, false } },
              { { output, 0U, 65U, false } } },
            std::make_unique<AliasOwnerExecutor>(interpreter,
                found->members, high_id, low_id, backing, input, output));
    }
    for (SimulationTick time = 0U; time <= 3U; ++time) {
        const auto run = interpreter.run(time);
        require(run.status == RunStatus::completed
                || run.status == RunStatus::time_limit,
            "the normalized-owner test reaches each observation time");
        result.settled.push_back(
            interpreter.signal_value(output).to_msb_string());
    }
    for (const auto id : { high_id, low_id }) {
        result.drivers.push_back(
            interpreter.driver_value(id, output).to_msb_string());
    }
    result.counters = interpreter.fused_masked_region_counters();
    return result;
}

} // namespace

void test_fused_masked_terminal_regions()
{
    for (const auto width : { 65U, 129U }) {
        for (const auto terminal_first : { false, true }) {
            const auto reference = run_terminal(width, false, terminal_first);
            const auto candidate = run_terminal(width, true, terminal_first);
            require(candidate.events == reference.events
                    && candidate.settled == reference.settled
                    && candidate.drivers == reference.drivers,
                "mixed owned/normal sinks preserve exact boundary deltas and drivers");
            require(candidate.counters.terminal_candidates > 0U
                    && candidate.counters.terminal_regions_bound > 0U
                    && candidate.counters.terminal_activations > 0U
                    && candidate.counters.terminal_joint_activations > 0U,
                "the mixed terminal path must run with its producers");
        }
    }
    const auto reference = run_normalized_owner(false);
    const auto candidate = run_normalized_owner(true);
    require(candidate.events == reference.events
            && candidate.settled == reference.settled
            && candidate.drivers == reference.drivers,
        "late alias cache mutation preserves fallback values and raw drivers");
    require(candidate.counters.masked_calls > 0U
            && candidate.counters.demotions > 0U,
        "a normalized owned member demotes only after genuine fused execution");
}

} // namespace fsim::tests::runtime
