// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
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
    native_decline,
    late_tie,
};

using Trace = std::tuple<std::string, SimulationTick, std::uint64_t, std::string>;

struct Observation {
    std::vector<Trace> trace;
    std::vector<std::string> settled;
    std::vector<std::string> drivers;
    std::array<std::uint64_t, 3> native_calls { }, declines { };
    FusedMaskedRegionCounters counters;
    RunStatus status { };
    std::uint64_t delta { };
};

class SliceExecutor final : public FusedMaskedRegionExecutor {
public:
    SliceExecutor(Interpreter& interpreter, std::vector<ProcessId> members,
        const SignalId output, const bool decline,
        std::uint64_t& native_calls, std::uint64_t& declines)
        : interpreter_(interpreter)
        , members_(std::move(members))
        , output_(output)
        , decline_(decline)
        , native_calls_(native_calls)
        , declines_(declines)
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&,
        const std::span<const std::uint64_t> activation) override
    {
        if (decline_) {
            ++declines_;
            return std::nullopt;
        }
        const auto width = static_cast<std::uint32_t>(
            interpreter_.signal_value(output_).width());
        auto value = PackedLogic4(width, Logic4::zero);
        mask_.fill(0U);
        for (std::size_t index = 0U; index < members_.size(); ++index) {
            if ((activation[index / 64U]
                    & (UINT64_C(1) << (index % 64U))) == 0U) {
                continue;
            }
            const auto& program = interpreter_.process_program(members_[index]);
            const auto read = operation_get<ReadSignal>(program.operations[0]);
            const auto extract = operation_get<Extract>(program.operations[1]);
            const auto write = operation_get<WriteUpdateSlice>(program.operations[2]);
            value.insert_bits(interpreter_.signal_value(read.signal)
                                  .extract_bits(extract.offset, extract.width),
                write.offset);
            for (std::uint32_t bit = write.offset;
                 bit < write.offset + extract.width; ++bit) {
                mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
            }
        }
        const auto words = (width + 63U) / 64U;
        require(words <= aval_.size(), "wide frontier test scratch fits the value");
        std::copy_n(value.aval_words().begin(), words, aval_.begin());
        std::copy_n(value.bval_words().begin(), words, bval_.begin());
        active_ = 1U;
        slots_[0] = { output_, width, words, &active_,
            aval_.data(), bval_.data(), mask_.data() };
        ++native_calls_;
        return FusedStaticCohortResume { slots_, { } };
    }

private:
    Interpreter& interpreter_;
    std::vector<ProcessId> members_;
    SignalId output_;
    bool decline_;
    std::uint64_t& native_calls_;
    std::uint64_t& declines_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 3> aval_ { }, bval_ { }, mask_ { };
    std::array<ProcessUpdateSlotView, 1> slots_ { };
};

Observation run_frontiers(const std::uint32_t width,
    const Intervention intervention, const bool fused)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    Observation observation;
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
    interpreter.set_output_hook([&](ProcessId, std::string_view, bool,
                                    SimulationTick, std::uint64_t) {
        trace("boundary");
    });
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
    if (intervention != Intervention::none && intervention != Intervention::native_decline) {
        const bool late_tie = intervention == Intervention::late_tie;
        interpreter.scheduler().schedule_at(2U,
            late_tie ? SchedulerPhase::observed : SchedulerPhase::active, 0U,
            [&](Scheduler& scheduler) {
                // Active-phase insertion precedes member 2's reservation;
                // observed-phase insertion follows the update commit and
                // therefore follows that reservation. Both use the
                // same StableOrder, so sequence decides the actual boundary.
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
                        } else if (intervention == Intervention::force_input) {
                            interpreter.force_signal(inputs[2U],
                                PackedLogic4(width, Logic4::zero));
                        } else if (intervention == Intervention::late_tie) {
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
    if (fused) {
        const auto candidates = interpreter.fused_masked_region_candidates();
        for (std::size_t group = 0U; group < outputs.size(); ++group) {
            const auto found = std::ranges::find_if(candidates, [&](const auto& candidate) {
                return candidate.members == members[group]
                    && candidate.outputs == std::vector<SignalId> { outputs[group] };
            });
            require(found != candidates.end(), "each interleaved output forms a masked region");
            std::vector<std::vector<Process::DriverRegion>> writes;
            for (const auto id : members[group]) {
                writes.push_back(interpreter.process_program(id).driver_regions);
            }
            interpreter.install_fused_masked_region(found->region_id, std::move(writes),
                std::make_unique<SliceExecutor>(interpreter, members[group], outputs[group],
                    intervention == Intervention::native_decline && group == 0U,
                    observation.native_calls[group], observation.declines[group]));
        }
    }
    for (SimulationTick time = 0U; time <= 4U; ++time) {
        RunResult result;
        try {
            result = interpreter.run(time);
        } catch (const std::runtime_error& error) {
            require(intervention == Intervention::exception && time == 2U
                    && std::string(error.what()) == "frontier outside exception",
                "only the intended outside callback may throw");
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
        observation.settled.push_back(snapshot());
    }
    for (std::size_t group = 0U; group < outputs.size(); ++group) {
        for (const auto id : members[group]) {
            observation.drivers.push_back(
                interpreter.driver_value(id, outputs[group]).to_msb_string());
        }
    }
    observation.counters = interpreter.fused_masked_region_counters();
    return observation;
}

} // namespace

void test_runtime_fused_masked_global_frontier()
{
    for (const auto width : { 3U, 65U, 129U }) {
        std::vector<Trace> early_force_trace;
        for (const auto intervention : { Intervention::none, Intervention::stop,
                 Intervention::exception, Intervention::insert_callback,
                 Intervention::force_input, Intervention::driver_hook,
                 Intervention::native_decline, Intervention::late_tie }) {
            const auto generic = run_frontiers(width, intervention, false);
            const auto fused = run_frontiers(width, intervention, true);
            if (intervention == Intervention::force_input) {
                early_force_trace = generic.trace;
            } else if (intervention == Intervention::late_tie) {
                require(!early_force_trace.empty() && generic.trace != early_force_trace,
                    "equal StableOrder callbacks on opposite sequence sides expose different deltas");
            }
            require(fused.trace == generic.trace,
                "global frontiers preserve boundary values, deltas and outside callback order");
            require(fused.settled == generic.settled && fused.drivers == generic.drivers,
                "global frontiers preserve wide committed values and original raw owners");
            require(fused.status == generic.status && fused.delta == generic.delta,
                "global frontiers preserve completion after stop or exception");
            require(fused.counters.masked_calls > 0U,
                "the interleaved fixture executes masked native regions");
            if (intervention == Intervention::native_decline) {
                require(fused.counters.prepared_fallback_tasks
                            + fused.counters.ordinary_fallback_tasks > 0U,
                    "a declined region retains its original tasks beside native regions");
                require(fused.native_calls[0U] == 0U && fused.declines[0U] > 0U
                        && fused.native_calls[1U] > 0U && fused.native_calls[2U] > 0U,
                    "both other regions complete natively beside the declined region");
            } else {
                require(std::ranges::all_of(fused.native_calls,
                            [](const auto calls) { return calls > 0U; }),
                    "every installed region enters the native route");
            }
            if (intervention == Intervention::none) {
                require(fused.counters.global_frontier_callbacks > 0U
                        && fused.counters.global_frontier_callbacks < fused.counters.masked_calls,
                    "one physical frontier must execute multiple native regions");
            }
        }
    }
}

} // namespace fsim::tests::runtime
