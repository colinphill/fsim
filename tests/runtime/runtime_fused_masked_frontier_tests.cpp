// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
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

enum class Intervention {
    none,
    stop,
    exception,
    insert_callback,
    force_input,
    driver_hook,
    late_tie,
};

using Trace = std::tuple<std::string, SimulationTick, std::uint64_t, std::string>;

struct Observation {
    std::vector<Trace> trace;
    std::vector<std::string> settled;
    std::vector<std::string> drivers;
    FusedMaskedRegionCounters counters;
    RunStatus status { };
    std::uint64_t delta { };
    std::uint64_t retired_executor_calls { };
};

class RetiredMaskedExecutor final : public FusedMaskedRegionExecutor {
public:
    explicit RetiredMaskedExecutor(std::uint64_t& calls)
        : calls_(calls)
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&,
        std::span<const std::uint64_t>) override
    {
        ++calls_;
        throw std::runtime_error {
            "retired global masked executor must not be dispatched" };
    }

private:
    std::uint64_t& calls_;
};

Observation run_interleaved_frontier(const std::uint32_t width,
    const Intervention intervention, const bool install_candidate,
    const bool capture_output, const bool capture_intermediate_state)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    Observation observation;
    std::uint64_t retired_executor_calls { };
    std::array<SignalId, 6> inputs;
    std::array<SignalId, 3> outputs;
    std::array<std::vector<ProcessId>, 3> members;
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        inputs[index] = interpreter.add_signal({ "input_" + std::to_string(index),
            PackedLogic4(width, Logic4::zero) });
    }
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        outputs[index] = interpreter.add_signal({ "output_" + std::to_string(index),
            PackedLogic4(width, Logic4::z), ResolutionKind::sv_wire });
    }

    // Original keys interleave three independent regions: A0, B0, C0,
    // A1, B1, C1. Every member has a distinct static sensitivity cohort.
    for (ProcessId id = 0U; id < inputs.size(); ++id) {
        const auto group = id % outputs.size();
        const auto offset = id < outputs.size() ? 0U : width / 2U;
        const auto bits = id < outputs.size() ? width / 2U : width - offset;
        Process process;
        process.id = id;
        process.name = "slice_" + std::to_string(id);
        process.register_count = 2U;
        process.static_sensitivity = { { inputs[id], EdgeKind::any } };
        process.driver_regions = { { outputs[group], offset, bits, false } };
        process.operations = { ReadSignal { 0U, inputs[id] },
            Extract { 1U, 0U, offset, bits },
            WriteUpdateSlice { outputs[group], 1U, offset },
            WaitSensitivity { }, Jump { 0U } };
        members[group].push_back(interpreter.add_process(std::move(process)));
    }

    const auto snapshot = [&] {
        std::string result;
        for (const auto signal : outputs) {
            result += interpreter.signal_value(signal).to_msb_string() + ":";
        }
        return result;
    };
    const auto trace = [&](const std::string& label) {
        observation.trace.emplace_back(label, interpreter.scheduler().now(),
            interpreter.scheduler().delta(), snapshot());
    };
    Process observer;
    observer.id = static_cast<ProcessId>(inputs.size());
    observer.name = "boundary";
    for (const auto signal : outputs) {
        observer.static_sensitivity.push_back({ signal, EdgeKind::any });
    }
    observer.operations = { Display { "boundary" }, WaitSensitivity { }, Jump { 0U } };
    (void)interpreter.add_process(std::move(observer));
    if (capture_output) {
        interpreter.set_output_hook([&](ProcessId, std::string_view, bool,
                                        SimulationTick, std::uint64_t) {
            trace("boundary");
        });
    }
    require(interpreter.fused_masked_region_candidates().empty(),
        "the retired masked facade is empty before simulation start");

    for (SimulationTick time = 1U; time <= 4U; ++time) {
        for (std::size_t index = 0U; index < inputs.size(); ++index) {
            if (time == 2U && index % 2U != 0U) {
                continue;
            }
            auto value = PackedLogic4(width, Logic4::zero);
            constexpr std::array states { Logic4::one, Logic4::x,
                Logic4::z, Logic4::zero };
            for (std::uint32_t bit = 0U; bit < width; ++bit) {
                value.set(bit, states[(time + index + bit / 17U) % states.size()]);
            }
            interpreter.schedule_signal_at(inputs[index], std::move(value), time, index);
        }
    }
    if (intervention != Intervention::none) {
        const bool late_tie = intervention == Intervention::late_tie;
        interpreter.scheduler().schedule_at(2U,
            late_tie ? SchedulerPhase::observed : SchedulerPhase::active, 0U,
            [&](Scheduler& scheduler) {
                // Active-phase insertion precedes member 2's normal work;
                // observed-phase insertion follows the update commit. Both
                // use the same StableOrder, so sequence exposes the boundary.
                scheduler.schedule_next_delta(SchedulerPhase::active, 2U,
                    [&](Scheduler& current) {
                        trace("outside");
                        if (intervention == Intervention::stop) {
                            current.request_stop();
                        } else if (intervention == Intervention::exception) {
                            throw std::runtime_error("frontier outside exception");
                        } else if (intervention == Intervention::insert_callback) {
                            current.schedule(SchedulerPhase::active, 0U,
                                [&](Scheduler&) { trace("inserted"); });
                        } else if (intervention == Intervention::force_input
                            || intervention == Intervention::late_tie) {
                            interpreter.force_signal(inputs[2U],
                                PackedLogic4(width, Logic4::zero));
                        } else if (intervention == Intervention::driver_hook) {
                            interpreter.set_driver_change_hook(
                                [&](const ProcessId process, const SignalId signal,
                                    const SimulationTick time) {
                                    observation.trace.emplace_back(
                                        "driver_" + std::to_string(process), time,
                                        interpreter.scheduler().delta(),
                                        interpreter.driver_value(process, signal).to_msb_string());
                                });
                        }
                    });
            });
    }

    interpreter.start();
    if (install_candidate) {
        const auto candidates = interpreter.fused_masked_region_candidates();
        require(candidates.empty(),
            "the source-compatible retired masked facade exposes no candidates");
        bool rejected { };
        try {
            interpreter.install_fused_masked_region(0U, { },
                std::make_unique<RetiredMaskedExecutor>(
                    retired_executor_calls));
        } catch (const std::logic_error& error) {
            rejected = std::string_view { error.what() }
                == "invalid fused masked region binding";
        }
        require(rejected,
            "an unknown retired masked region keeps the checked install error");
    }

    for (SimulationTick time = 0U; time <= 4U; ++time) {
        RunResult result;
        try {
            result = interpreter.run(time);
        } catch (const std::runtime_error& error) {
            if (intervention != Intervention::exception || time != 2U
                || std::string(error.what()) != "frontier outside exception") {
                throw std::runtime_error {
                    "only the intended outside callback may throw: "
                    + std::string { error.what() }
                };
            }
            trace("caught");
            result = interpreter.run(time);
        }
        if (result.status == RunStatus::stopped) {
            require(intervention == Intervention::stop && time == 2U,
                "only the intended outside callback may stop");
            trace("stopped");
            interpreter.scheduler().clear_stop();
            result = interpreter.run(time);
        }
        observation.status = result.status;
        observation.delta = result.delta;
        if (capture_intermediate_state) {
            observation.settled.push_back(snapshot());
        }
    }
    if (!capture_intermediate_state) {
        observation.settled.push_back(snapshot());
    }
    for (std::size_t group = 0U; group < outputs.size(); ++group) {
        for (const auto id : members[group]) {
            observation.drivers.push_back(
                interpreter.driver_value(id, outputs[group]).to_msb_string());
        }
    }
    observation.counters = interpreter.fused_masked_region_counters();
    observation.retired_executor_calls = retired_executor_calls;
    return observation;
}

} // namespace

void test_runtime_fused_masked_fallback_interleaving()
{
    for (const auto width : { 3U, 65U, 129U }) {
        std::vector<Trace> early_force_trace;
        for (const auto intervention : { Intervention::none, Intervention::stop,
                 Intervention::exception, Intervention::insert_callback,
                 Intervention::force_input, Intervention::driver_hook,
                 Intervention::late_tie }) {
            const auto generic = run_interleaved_frontier(
                width, intervention, false, false, false);
            const auto retained_candidate = run_interleaved_frontier(
                width, intervention, true, false, false);
            const auto observed_generic = run_interleaved_frontier(
                width, intervention, false, true, true);
            const auto observed_candidate = run_interleaved_frontier(
                width, intervention, true, true, true);
            if (intervention == Intervention::force_input) {
                early_force_trace = observed_generic.trace;
            } else if (intervention == Intervention::late_tie) {
                require(!early_force_trace.empty()
                        && observed_generic.trace != early_force_trace,
                    "equal StableOrder callbacks on opposite sequence sides expose different deltas");
            }
            require(retained_candidate.trace == generic.trace,
                "retired candidates preserve boundary values, deltas and outside callback order");
            require(retained_candidate.settled == generic.settled
                    && retained_candidate.drivers == generic.drivers,
                "checked fallback preserves wide committed values and original raw owners");
            require(retained_candidate.status == generic.status
                    && retained_candidate.delta == generic.delta,
                "checked fallback preserves completion after stop or exception");
            require(retained_candidate.retired_executor_calls == 0U
                    && retained_candidate.counters.candidates == 0U
                    && retained_candidate.counters.masked_calls == 0U
                    && retained_candidate.counters.global_frontier_callbacks == 0U,
                "retired masked counters stay zero and never enter an executor");
            require(observed_candidate.trace == observed_generic.trace,
                "observed checked fallback preserves boundary values, deltas and callback order");
            require(observed_candidate.settled == observed_generic.settled
                    && observed_candidate.drivers == observed_generic.drivers,
                "observed checked fallback preserves committed values and raw owners");
            require(observed_candidate.status == observed_generic.status
                    && observed_candidate.delta == observed_generic.delta,
                "observed checked fallback preserves completion after stop or exception");
            require(observed_candidate.retired_executor_calls == 0U
                    && observed_candidate.counters.masked_calls == 0U
                    && observed_candidate.counters.global_frontier_callbacks == 0U,
                "output observation does not enter the retired executor");
            require(std::ranges::any_of(observed_generic.trace,
                        [](const Trace& entry) {
                            return std::get<0>(entry) == "boundary";
                        }),
                "the observed fallback run exposes a boundary callback");
        }
    }
}

} // namespace fsim::tests::runtime
