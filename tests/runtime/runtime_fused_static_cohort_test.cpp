// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

enum class Mode {
    normal, decline, late_hook, partial_initial, outside_reader, body_reader,
    force_pending, deposit_pending, late_driver_hook, unwritten_bit,
    multi_output_driver_hook, unrelated_fork, overlapping_fork
};

class AggregateExecutor final : public FusedStaticCohortExecutor {
public:
    AggregateExecutor(const SignalId lhs, const SignalId rhs,
        std::vector<SignalId> targets, const std::uint32_t width, const bool decline,
        const bool outside_owner)
        : lhs_(lhs)
        , rhs_(rhs)
        , targets_(std::move(targets))
        , width_(width)
        , decline_(decline)
        , outside_owner_(outside_owner)
        , slots_(targets_.size())
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext& context) override
    {
        if (decline_) {
            return std::nullopt;
        }
        const auto read = [&](const SignalId signal) {
            if (width_ <= 64U) {
                return PackedLogic4::from_aval_bval(width_,
                    context.signal_aval[signal], context.signal_bval[signal]);
            }
            const auto offset = context.wide_signal_offsets[signal];
            return PackedLogic4::from_word_planes(width_,
                context.wide_signal_aval.subspan(offset, words()),
                context.wide_signal_bval.subspan(offset, words()));
        };
        const auto lhs = read(lhs_);
        const auto rhs = read(rhs_);
        auto value = binary_value(BinaryOperator::bit_xor, lhs, rhs);
        value.set(width_ - 1U,
            binary_value(BinaryOperator::bit_and, lhs, rhs).get(width_ - 1U));
        std::ranges::copy(value.aval_words(), aval_.begin());
        std::ranges::copy(value.bval_words(), bval_.begin());
        mask_.fill(0U);
        for (std::uint32_t bit = 0U; bit < width_; ++bit) {
            if (outside_owner_ && bit == width_ - 2U) {
                continue;
            }
            mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
        }
        active_ = 1U;
        for (std::size_t index = 0U; index < targets_.size(); ++index) {
            slots_[index] = { targets_[index], width_, words(), &active_,
                aval_.data(), bval_.data(), mask_.data() };
        }
        return FusedStaticCohortResume { slots_, {} };
    }

private:
    std::uint32_t words() const { return (width_ + 63U) / 64U; }
    SignalId lhs_;
    SignalId rhs_;
    std::vector<SignalId> targets_;
    std::uint32_t width_;
    bool decline_;
    bool outside_owner_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 3> aval_ { };
    std::array<std::uint64_t, 3> bval_ { };
    std::array<std::uint64_t, 3> mask_ { };
    std::vector<ProcessUpdateSlotView> slots_;
};

using Trace = std::tuple<SimulationTick, std::uint64_t, std::string>;

struct Observation {
    std::vector<Trace> boundary;
    std::vector<Trace> hooks;
    std::vector<std::string> settled;
    std::vector<std::string> drivers;
    FusedStaticCounters counters;
    std::uint64_t invocations_before_hook { };
    bool rejected_slot { };
};

Observation run_case(const std::uint32_t width, const Mode mode,
    const bool install)
{
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    Observation result;
    const auto lhs = interpreter.add_signal(
        { "lhs", PackedLogic4(width, Logic4::zero) });
    const auto rhs = interpreter.add_signal(
        { "rhs", PackedLogic4(width, Logic4::zero) });
    const bool multiple_outputs = mode == Mode::multi_output_driver_hook;
    const auto secondary = multiple_outputs
        ? interpreter.add_signal({ "second_private", PackedLogic4(width, Logic4::z),
              ResolutionKind::sv_wire })
        : SignalId { };
    const auto internal = interpreter.add_signal(
        { "private", PackedLogic4(width, Logic4::z), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal(
        { "result", PackedLogic4(1U, Logic4::z), ResolutionKind::sv_wire });
    std::vector<ProcessId> producers(multiple_outputs ? 4U : 2U);
    for (std::uint32_t index = 0U; index < producers.size(); ++index) {
        const auto member = index % 2U;
        const auto target = index < 2U ? internal : secondary;
        const auto offset = member == 0U ? 0U : width - 1U;
        const auto count = member == 0U
            ? width - (mode == Mode::overlapping_fork ? 2U : 1U) : 1U;
        const auto written = mode == Mode::unwritten_bit && index == 0U
            ? count - 1U : count;
        Process process;
        process.id = index;
        process.name = "owner_" + std::to_string(index);
        process.initialize = mode != Mode::partial_initial || index == 0U;
        process.register_count = 4U;
        process.static_sensitivity = {
            { lhs, EdgeKind::any }, { rhs, EdgeKind::any }
        };
        process.driver_regions = { { target, offset, count, false } };
        process.operations = {
            ReadSignal { 0U, lhs }, ReadSignal { 1U, rhs },
            Binary { member == 0U ? BinaryOperator::bit_xor
                                  : BinaryOperator::bit_and, 2U, 0U, 1U },
            Extract { 3U, 2U, offset, written },
            WriteUpdateSlice { target, 3U, offset },
        };
        if (member == 0U) {
            process.operations.push_back(WriteUpdateSlice { target, 3U, offset });
        }
        process.operations.push_back(WaitSensitivity { });
        process.operations.push_back(Jump { 0U });
        producers[index] = interpreter.add_process(std::move(process));
    }
    Process consumer;
    consumer.id = static_cast<ProcessId>(producers.size());
    consumer.name = "reduction";
    consumer.register_count = multiple_outputs ? 3U : 2U;
    consumer.static_sensitivity = { { internal, EdgeKind::any } };
    consumer.driver_regions = { { output, 0U, 1U, true } };
    consumer.operations = {
        ReadSignal { 0U, internal },
        Reduction { ReductionOperator::bit_xor, 1U, 0U },
        WriteUpdate { output, 1U }, WaitSensitivity { }, Jump { 0U }
    };
    if (multiple_outputs) {
        consumer.static_sensitivity.push_back({ secondary, EdgeKind::any });
        consumer.operations[0] = ReadSignal { 2U, secondary };
        consumer.operations.insert(consumer.operations.begin() + 1,
            ReadSignal { 0U, internal });
    }
    (void)interpreter.add_process(std::move(consumer));
    Process observer;
    observer.id = static_cast<ProcessId>(producers.size() + 1U);
    observer.name = "boundary_observer";
    observer.register_count = 1U;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = {
        ReadSignal { 0U, output }, Display { "boundary" },
        WaitSensitivity { }, Jump { 0U }
    };
    (void)interpreter.add_process(std::move(observer));
    const bool outside_reader = mode == Mode::outside_reader
        || mode == Mode::body_reader;
    if (outside_reader) {
        Process reader;
        reader.id = 4U;
        reader.name = "additional_reader";
        reader.register_count = 1U;
        reader.static_sensitivity = {
            { mode == Mode::body_reader ? lhs : internal, EdgeKind::any }
        };
        reader.operations = {
            ReadSignal { 0U, internal }, Display { "additional" },
            WaitSensitivity { }, Jump { 0U }
        };
        (void)interpreter.add_process(std::move(reader));
    }
    if (mode == Mode::unrelated_fork || mode == Mode::overlapping_fork) {
        const auto child_output = mode == Mode::overlapping_fork ? internal
            : interpreter.add_signal({ "unrelated_child_output", PackedLogic4(1U, Logic4::zero) });
        Process parent;
        parent.id = static_cast<ProcessId>(producers.size() + 2U);
        parent.name = "late_fork_parent";
        parent.register_count = 1U;
        if (mode == Mode::overlapping_fork) {
            parent.driver_regions = { { internal, width - 2U, 1U, false } };
            parent.operations = {
                LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
                WriteUpdateSlice { internal, 0U, width - 2U }, WaitFor { 2U },
                Fork { { 5U }, ForkJoinKind::all }, Halt {},
                LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
                WriteUpdateSlice { internal, 0U, width - 2U }, ForkEnd {}
            };
        } else {
            parent.driver_regions = { { child_output, 0U, 1U, true } };
            parent.operations = {
                WaitFor { 2U }, Fork { { 3U }, ForkJoinKind::all }, Halt {},
                LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
                WriteUpdate { child_output, 0U }, ForkEnd {}
            };
        }
        (void)interpreter.add_process(std::move(parent));
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
            [&](Scheduler&) {
                result.invocations_before_hook = interpreter.fused_static_counters().invocations;
            });
    }
    interpreter.set_output_hook(
        [&](ProcessId, std::string_view, bool, SimulationTick time,
            std::uint64_t delta) {
            result.boundary.emplace_back(time, delta,
                interpreter.signal_value(output).to_msb_string());
        });
    constexpr std::array<Logic4, 5> stimulus {
        Logic4::one, Logic4::zero, Logic4::x, Logic4::z, Logic4::one
    };
    for (SimulationTick time = 1U; time <= stimulus.size(); ++time) {
        auto left = PackedLogic4(width, stimulus[time - 1U]);
        if (time == 1U) {
            left = PackedLogic4(width, Logic4::zero);
            left.set(width - 2U, Logic4::one);
        }
        interpreter.schedule_signal_at(lhs, left, time, 0U);
        interpreter.schedule_signal_at(rhs,
            PackedLogic4(width, time == 5U ? Logic4::one : Logic4::zero), time, 1U);
    }
    if (mode == Mode::late_hook) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
            [&](Scheduler&) {
                result.invocations_before_hook
                    = interpreter.fused_static_counters().invocations;
                interpreter.set_signal_change_hook(
                    [&](SignalId signal, const PackedLogic4& value,
                        SimulationTick time) {
                        result.hooks.emplace_back(time,
                            interpreter.scheduler().delta(),
                            std::to_string(signal) + ":" + value.to_msb_string());
                    });
            });
    }
    if (mode == Mode::force_pending || mode == Mode::deposit_pending
        || mode == Mode::late_driver_hook || multiple_outputs) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
            std::numeric_limits<StableOrder>::max(),
            [&](Scheduler& scheduler) {
                scheduler.schedule_next_delta(SchedulerPhase::active,
                    std::numeric_limits<StableOrder>::max(),
                    [&](Scheduler&) {
                        result.invocations_before_hook
                            = interpreter.fused_static_counters().invocations;
                        if (mode == Mode::force_pending) {
                            interpreter.force_signal(internal,
                                PackedLogic4(width, Logic4::one));
                        } else if (mode == Mode::deposit_pending) {
                            interpreter.deposit_signal(internal,
                                PackedLogic4(width, Logic4::one));
                        } else {
                            interpreter.set_driver_change_hook(
                                [&](ProcessId process, SignalId signal,
                                    SimulationTick time) {
                                    result.hooks.emplace_back(time,
                                        interpreter.scheduler().delta(),
                                        std::to_string(process) + ":"
                                            + std::to_string(signal) + ":"
                                            + interpreter.driver_value(process, signal)
                                                  .to_msb_string());
                                });
                        }
                    });
            });
    }
    if (mode == Mode::force_pending) {
        interpreter.scheduler().schedule_at(3U, SchedulerPhase::active, 0U,
            [&](Scheduler&) { interpreter.release_signal(internal); });
    }
    interpreter.start();
    if (install) {
        const auto candidates = interpreter.fused_static_cohort_candidates();
        const auto candidate = std::ranges::find_if(candidates,
            [&](const FusedStaticCohortCandidate& item) {
                return item.members == std::vector<ProcessId>(
                    producers.begin(), producers.end());
            });
        if (outside_reader) {
            require(candidate == candidates.end()
                    || std::ranges::find(candidate->private_outputs, internal)
                        == candidate->private_outputs.end(),
                "full body reads prevent private admission even without sensitivity");
        } else {
            require(candidate != candidates.end(),
                "the full graph admits the generic disjoint producer cohort");
            require(std::ranges::find(candidate->private_outputs, internal)
                    != candidate->private_outputs.end(),
                "the full graph certifies the sole-consumer intermediate");
            interpreter.install_fused_static_cohort(candidate->cohort_id,
                std::make_unique<AggregateExecutor>(
                    lhs, rhs, candidate->private_outputs,
                    width, mode == Mode::decline, mode == Mode::overlapping_fork));
        }
    }
    for (SimulationTick time = 0U; time <= stimulus.size(); ++time) {
        const auto before = interpreter.signal_value(internal).to_msb_string();
        RunResult run;
        try {
            run = interpreter.run(time);
        } catch (const std::logic_error& error) {
            if (mode != Mode::unwritten_bit || !install) {
                throw;
            }
            require(time == 1U
                    && std::string(error.what()).find("invalid aggregate slot")
                        != std::string::npos,
                "the runtime rejects the first out-of-write-range fused slot");
            require(interpreter.signal_value(internal).to_msb_string() == before,
                "an unauthorized slot cannot alter the aggregate signal");
            result.rejected_slot = true;
            break;
        }
        require(run.status == RunStatus::completed || run.status == RunStatus::time_limit,
            "the fused differential case completes each timestamp");
        result.settled.push_back(interpreter.signal_value(output).to_msb_string());
    }
    for (std::size_t index = 0U; index < producers.size(); ++index) {
        result.drivers.push_back(interpreter.driver_value(producers[index],
            index < 2U ? internal : secondary).to_msb_string());
    }
    if (mode == Mode::overlapping_fork) {
        const auto parent = static_cast<ProcessId>(producers.size() + 2U);
        for (const auto owner : { parent, parent + 1U }) {
            result.drivers.push_back(interpreter.driver_value(owner, internal).to_msb_string());
        }
    }
    result.counters = interpreter.fused_static_counters();
    return result;
}

void check_case(const std::uint32_t width, const Mode mode)
{
    const auto fused = run_case(width, mode, true);
    if (mode == Mode::unwritten_bit) {
        require(fused.rejected_slot && fused.counters.invocations == 0U
                && fused.counters.aggregate_signals_staged == 0U,
            "ownership alone does not authorize writes outside the body write set");
        return;
    }
    const auto reference = run_case(width, mode, false);
    if (mode == Mode::normal) {
        require((reference.settled
                    == std::vector<std::string> { "0", "1", "0", "X", "X", "1" }),
            "the stimulus exercises known transitions and four-state propagation");
    }
    require(fused.boundary == reference.boundary,
        "fused execution preserves every boundary delta and value");
    require(fused.settled == reference.settled && fused.hooks == reference.hooks,
        "fused execution preserves settled values and late hook order");
    require(fused.drivers == reference.drivers,
        "aggregate writes retain each original owner's raw driver value");
    if (mode == Mode::outside_reader || mode == Mode::body_reader) {
        return;
    }
    if (mode == Mode::decline) {
        require(fused.counters.invocations == 0U && fused.counters.fallbacks > 0U,
            "declined kernels use the original process execution path");
        return;
    }
    require(fused.counters.invocations > 0U,
        "the test must execute the fused path rather than merely its fallback");
    const auto outputs = mode == Mode::multi_output_driver_hook ? 2U : 1U;
    require(fused.counters.represented_members == 2U * outputs * fused.counters.invocations,
        "each fused invocation replaces every original member execution");
    require(fused.counters.aggregate_signals_staged == outputs * fused.counters.invocations,
        "each fused invocation stages only one aggregate per signal");
    require(fused.counters.owner_stage_calls_avoided == 2U * outputs * fused.counters.invocations,
        "one aggregate per signal replaces the original owner staging calls");
    if (mode == Mode::late_hook) {
        require(fused.invocations_before_hook > 0U
                && fused.counters.invocations == fused.invocations_before_hook,
            "late observation invalidates the installed fused route");
    }
    if (mode == Mode::unrelated_fork) {
        require(fused.invocations_before_hook > 0U
                && fused.counters.invocations > fused.invocations_before_hook,
            "an unrelated dynamic child preserves future graph-kernel activations");
        require(fused.counters.fork_events == 1U
                && fused.counters.fork_plans_invalidated == 0U
                && fused.counters.fork_plans_surviving_after_last > 0U,
            "the unrelated fork leaves the certified cohort live");
    }
    if (mode == Mode::overlapping_fork) {
        require(fused.invocations_before_hook > 0U
                && fused.counters.invocations == fused.invocations_before_hook,
            "a dynamic child that changes certified output ownership invalidates the graph kernel");
        require(fused.counters.fork_events == 1U
                && fused.counters.fork_plans_invalidated > 0U,
            "the overlapping child retires its affected cohort certificate");
    }
    if (mode == Mode::force_pending || mode == Mode::deposit_pending
        || mode == Mode::late_driver_hook || mode == Mode::multi_output_driver_hook) {
        require(fused.invocations_before_hook >= 2U,
            "the pending-write intervention follows actual fused activations");
    }
}

} // namespace

void test_fused_static_cohorts()
{
    for (const auto width : { 3U, 9U, 65U, 129U }) {
        check_case(width, Mode::normal);
        check_case(width, Mode::decline);
        check_case(width, Mode::late_hook);
        check_case(width, Mode::partial_initial);
        check_case(width, Mode::outside_reader);
        check_case(width, Mode::body_reader);
        check_case(width, Mode::force_pending);
        check_case(width, Mode::deposit_pending);
        check_case(width, Mode::late_driver_hook);
        check_case(width, Mode::unwritten_bit);
        check_case(width, Mode::multi_output_driver_hook);
        check_case(width, Mode::unrelated_fork);
        check_case(width, Mode::overlapping_fork);
    }
}

} // namespace fsim::tests::runtime
