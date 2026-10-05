// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
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

enum class Mode { normal, decline, force, deposit, late_hook, invalid_second, unrelated_fork };

class ProjectedExecutor final : public FusedStaticCohortExecutor {
public:
    ProjectedExecutor(std::array<SignalId, 2> inputs,
        std::array<SignalId, 2> outputs, const Mode mode,
        const std::uint32_t width, const bool logic9)
        : inputs_(inputs)
        , outputs_(outputs)
        , mode_(mode)
        , width_(width)
        , logic9_(logic9)
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext& context) override
    {
        if (mode_ == Mode::decline) {
            return std::nullopt;
        }
        const auto words = (width_ + 63U) / 64U;
        if (logic9_ && width_ <= 64U
            && (context.signal_logic9_plane0.size() != context.signal_aval.size()
                || context.signal_logic9_plane1.size() != context.signal_aval.size()
                || context.signal_logic9_plane2.size() != context.signal_aval.size()
                || context.signal_logic9_plane3.size() != context.signal_aval.size())) {
            return std::nullopt;
        }
        if (logic9_ && width_ > 64U
            && (context.wide_signal_logic9_plane2.size()
                    != context.wide_signal_aval.size()
                || context.wide_signal_logic9_plane3.size()
                    != context.wide_signal_aval.size())) {
            return std::nullopt;
        }
        for (const auto signal : inputs_) {
            if (width_ > 64U && signal >= context.wide_signal_offsets.size()) {
                return std::nullopt;
            }
        }
        for (std::size_t index = 0U; index < writes_.size(); ++index) {
            const auto signal = inputs_[index];
            PackedLogic4 value;
            if (logic9_) {
                if (width_ <= 64U) {
                    const std::array<std::uint64_t, 1> plane0 { context.signal_logic9_plane0[signal] };
                    const std::array<std::uint64_t, 1> plane1 { context.signal_logic9_plane1[signal] };
                    const std::array<std::uint64_t, 1> plane2 { context.signal_logic9_plane2[signal] };
                    const std::array<std::uint64_t, 1> plane3 { context.signal_logic9_plane3[signal] };
                    value = PackedLogic4::from_logic9_word_planes(
                        width_, plane0, plane1, plane2, plane3);
                } else {
                    const auto offset = context.wide_signal_offsets[signal];
                    value = PackedLogic4::from_logic9_word_planes(width_,
                        context.wide_signal_aval.subspan(offset, words),
                        context.wide_signal_bval.subspan(offset, words),
                        context.wide_signal_logic9_plane2.subspan(offset, words),
                        context.wide_signal_logic9_plane3.subspan(offset, words));
                }
            } else {
                const auto offset = context.wide_signal_offsets[signal];
                value = PackedLogic4::from_word_planes(width_,
                    context.wide_signal_aval.subspan(offset, words),
                    context.wide_signal_bval.subspan(offset, words));
            }
            writes_[index] = { outputs_[index], std::move(value) };
        }
        if (mode_ == Mode::invalid_second) {
            writes_[1].value = PackedLogic4(1U, Logic4::one);
        }
        return FusedStaticCohortResume { {}, writes_ };
    }

private:
    std::array<SignalId, 2> inputs_;
    std::array<SignalId, 2> outputs_;
    Mode mode_;
    std::uint32_t width_;
    bool logic9_;
    std::array<FusedStaticProjectedWrite, 2> writes_;
};

using Trace = std::tuple<SimulationTick, std::uint64_t, std::string>;

struct Observation {
    std::vector<Trace> boundary;
    std::vector<Trace> drivers;
    std::vector<std::string> settled;
    std::string final_current;
    std::string final_stored;
    std::string final_drivers;
    FusedStaticCounters counters;
    std::uint64_t invocations_at_first_output { };
    std::uint64_t before_intervention { };
    bool rejected { };
};

Observation run_case(const bool logic9, const std::uint32_t width,
    const Mode mode, const bool install, const bool capture_output,
    const bool capture_intermediate_state, const bool mixed_child = false)
{
    const std::string states = logic9 ? "01UXZWLH-" : "010101010";
    const auto value = [&](const std::size_t row) {
        std::string bits(width, '0');
        for (std::size_t bit = 0U; bit < bits.size(); ++bit) {
            bits[bit] = states[(row + bit) % states.size()];
        }
        return logic9 ? PackedLogic4::from_logic9_msb_string(bits)
                      : PackedLogic4::from_msb_string(bits);
    };
    const auto initial = logic9
        ? PackedLogic4::from_logic9_msb_string(std::string(width, 'U'))
        : PackedLogic4(width, Logic4::zero);
    const auto kind = logic9 ? ValueKind::logic9 : ValueKind::logic4;
    const auto resolution = logic9 ? ResolutionKind::std_logic : ResolutionKind::none;
    Interpreter interpreter;
    interpreter.set_fused_static_counters_enabled(true);
    Observation result;
    const std::array inputs {
        interpreter.add_signal({ "first_input", value(0U), ResolutionKind::none, kind }),
        interpreter.add_signal({ "second_input", value(2U), ResolutionKind::none, kind })
    };
    // Reverse numeric signal order relative to process write order.
    const auto second = interpreter.add_signal({ "second_output", initial, resolution, kind });
    const auto first = interpreter.add_signal({ "first_output", initial, resolution, kind });
    const std::array outputs { first, second };
    std::vector<ProcessId> owners;
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
        Process process;
        process.id = static_cast<ProcessId>(index);
        process.name = "projected_owner_" + std::to_string(index);
        process.register_count = 1U;
        process.register_value_kinds = { kind };
        process.static_sensitivity = { { inputs[0], EdgeKind::any }, { inputs[1], EdgeKind::any } };
        process.driver_regions = { { outputs[index], 0U, width, true } };
        process.operations = {
            ReadSignal { 0U, inputs[index] }, WriteProjected { outputs[index], 0U },
            WaitSensitivity {}, Jump { 0U }
        };
        owners.push_back(interpreter.add_process(std::move(process)));
    }
    Process observer;
    observer.id = 2U;
    observer.name = "boundary_observer";
    observer.register_count = 1U;
    observer.register_value_kinds = { kind };
    observer.static_sensitivity = { { first, EdgeKind::any }, { second, EdgeKind::any } };
    observer.operations = {
        ReadSignal { 0U, first }, Display { "boundary" }, WaitSensitivity {}, Jump { 0U }
    };
    (void)interpreter.add_process(std::move(observer));
    if (mode == Mode::unrelated_fork) {
        const auto child_initial = logic9 && !mixed_child
            ? PackedLogic4::from_logic9_msb_string("0")
            : PackedLogic4(1U, Logic4::zero);
        const auto unrelated = interpreter.add_signal({ "unrelated_child_output",
            child_initial, ResolutionKind::none,
            mixed_child ? ValueKind::logic4 : kind });
        Process parent;
        parent.id = 3U;
        parent.name = "unrelated_fork_parent";
        parent.register_count = 1U;
        parent.register_value_kinds = {
            mixed_child ? ValueKind::logic4 : kind };
        parent.driver_regions = { { unrelated, 0U, 1U, true } };
        parent.operations = {
            WaitFor { 2U }, Fork { { 3U }, ForkJoinKind::all }, Halt {},
            LoadConstant { 0U, logic9 && !mixed_child
                ? PackedLogic4::from_logic9_msb_string("1")
                : PackedLogic4(1U, Logic4::one) },
            WriteUpdate { unrelated, 0U }, ForkEnd {}
        };
        (void)interpreter.add_process(std::move(parent));
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
            [&](Scheduler&) {
                result.before_intervention = interpreter.fused_static_counters().invocations;
            });
    }
    const auto visible = [&] {
        return interpreter.signal_value(first).to_msb_string() + ":"
            + interpreter.signal_value(second).to_msb_string();
    };
    const auto raw_drivers = [&] {
        return interpreter.driver_value(owners[0], first).to_msb_string() + ":"
            + interpreter.driver_value(owners[1], second).to_msb_string();
    };
    if (capture_output) {
        interpreter.set_output_hook([&](ProcessId, std::string_view, bool,
                                        SimulationTick time, std::uint64_t delta) {
            if (result.boundary.empty()) {
                result.invocations_at_first_output
                    = interpreter.fused_static_counters().invocations;
            }
            result.boundary.emplace_back(time, delta, visible());
        });
    }
    for (SimulationTick time = 1U; time <= states.size(); ++time) {
        interpreter.schedule_signal_at(inputs[0], value(time), time, 0U);
        interpreter.schedule_signal_at(inputs[1], value(time + 2U), time, 1U);
    }
    if (mode == Mode::force || mode == Mode::deposit || mode == Mode::late_hook) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
            std::numeric_limits<StableOrder>::max(), [&](Scheduler& scheduler) {
                scheduler.schedule_next_delta(SchedulerPhase::active,
                    std::numeric_limits<StableOrder>::max(), [&](Scheduler&) {
                        result.before_intervention = interpreter.fused_static_counters().invocations;
                        if (mode == Mode::force) {
                            interpreter.force_signal(first, value(7U));
                        } else if (mode == Mode::deposit) {
                            interpreter.deposit_signal(first, value(7U));
                        } else {
                            interpreter.set_driver_change_hook(
                                [&](ProcessId owner, SignalId signal, SimulationTick time) {
                                    result.drivers.emplace_back(time, interpreter.scheduler().delta(),
                                        std::to_string(owner) + ":" + std::to_string(signal) + ":"
                                            + interpreter.driver_value(owner, signal).to_msb_string());
                                });
                        }
                    });
            });
    }
    if (mode == Mode::force) {
        interpreter.scheduler().schedule_at(3U, SchedulerPhase::active, 0U,
            [&](Scheduler&) { interpreter.release_signal(first); });
    }
    interpreter.start();
    if (install) {
        const auto candidates = interpreter.fused_static_cohort_candidates();
        const auto candidate = std::ranges::find_if(candidates,
            [&](const auto& entry) { return entry.members == owners; });
        require(candidate != candidates.end(), "generic single-owner projected cohort is admitted");
        interpreter.install_fused_static_cohort(candidate->cohort_id,
            std::make_unique<ProjectedExecutor>(inputs, outputs, mode,
                width, logic9));
    }
    for (SimulationTick time = 0U; time <= states.size(); ++time) {
        const auto before = mode == Mode::invalid_second
            ? raw_drivers() : std::string { };
        try {
            const auto run = interpreter.run(time);
            require(run.status == RunStatus::completed || run.status == RunStatus::time_limit,
                "projected differential fixture completes each timestamp");
        } catch (const std::logic_error&) {
            if (mode != Mode::invalid_second || !install) {
                throw;
            }
            require(before == raw_drivers(), "all projected outputs are validated before any driver mutation");
            result.rejected = true;
            break;
        }
        if (capture_intermediate_state) {
            result.settled.push_back(visible() + ":" + raw_drivers());
        }
        if (mode == Mode::normal && capture_intermediate_state) {
            require(interpreter.signal_value(first).to_msb_string() == value(time).to_msb_string()
                    && interpreter.signal_value(second).to_msb_string() == value(time + 2U).to_msb_string(),
                "projected fusion preserves all original input states independently of the reference engine");
        }
    }
    if (!capture_intermediate_state && !result.rejected) {
        result.settled.push_back(visible() + ":" + raw_drivers());
    }
    result.final_current = visible();
    result.final_stored
        = interpreter.stored_signal_value(first).to_msb_string() + ":"
        + interpreter.stored_signal_value(second).to_msb_string();
    result.final_drivers = raw_drivers();
    result.counters = interpreter.fused_static_counters();
    return result;
}

void check_projected_timing_fallbacks()
{
    for (const unsigned variant : { 0U, 1U, 2U, 3U, 4U }) {
        Interpreter interpreter;
        const auto input = interpreter.add_signal({ "input", PackedLogic4(9U, Logic4::zero) });
        SignalId first_output { };
        for (ProcessId index = 0U; index < 2U; ++index) {
            const auto output = interpreter.add_signal(
                { "output_" + std::to_string(index), PackedLogic4(9U, Logic4::zero) });
            if (index == 0U) {
                first_output = output;
            }
            Process process;
            process.id = index;
            process.name = "owner_" + std::to_string(index);
            process.register_count = 1U;
            process.static_sensitivity = { { input, EdgeKind::any } };
            process.driver_regions = { { output, 0U, 9U, true } };
            WriteProjected write { output, 0U };
            if (index == 0U && variant == 0U) {
                write.delay = 1U;
                write.rejection = 1U;
            } else if (index == 0U && variant == 1U) {
                write.mode = ProjectedDelayMode::transport;
            }
            process.operations = { ReadSignal { 0U, input }, write };
            if (index == 0U && variant == 2U) {
                process.operations.push_back(write);
            } else if (index == 0U && variant == 4U) {
                process.operations.push_back(WriteUpdate { output, 0U });
            }
            process.operations.push_back(WaitSensitivity {});
            process.operations.push_back(Jump { 0U });
            (void)interpreter.add_process(std::move(process));
        }
        if (variant == 3U) {
            Process outside;
            outside.id = 2U;
            outside.name = "outside_writer_without_region_metadata";
            outside.register_count = 1U;
            outside.operations = {
                LoadConstant { 0U, PackedLogic4(9U, Logic4::one) },
                WriteUpdate { first_output, 0U }
            };
            (void)interpreter.add_process(std::move(outside));
        }
        interpreter.start();
        require(interpreter.fused_static_cohort_candidates().empty(),
            "delayed, transport, repeated, outside-owner, and mixed writes retain ordinary scheduling");
    }
}

} // namespace

void test_fused_projected_cohorts()
{
    check_projected_timing_fallbacks();
    const auto scalar_projected = run_case(
        false, 1U, Mode::normal, true, false, false);
    const auto scalar_reference = run_case(
        false, 1U, Mode::normal, false, false, false);
    require(scalar_projected.counters.invocations > 0U
            && scalar_projected.final_current == scalar_reference.final_current
            && scalar_projected.final_stored == scalar_reference.final_stored
            && scalar_projected.final_drivers == scalar_reference.final_drivers
            && scalar_projected.settled == scalar_reference.settled
            && scalar_projected.counters.generic_update_commit_tickets > 0U,
        "generic projected scalar writes execute natively and publish owners through reserved Update tickets");
    const auto scalar_observed = run_case(
        false, 1U, Mode::normal, true, true, true);
    const auto scalar_observed_reference = run_case(
        false, 1U, Mode::normal, false, true, true);
    require(!scalar_observed.boundary.empty()
            && scalar_observed.drivers == scalar_observed_reference.drivers
            && scalar_observed.boundary == scalar_observed_reference.boundary
            && scalar_observed.settled == scalar_observed_reference.settled
            && scalar_observed.final_current
                == scalar_observed_reference.final_current
            && scalar_observed.final_stored
                == scalar_observed_reference.final_stored
            && scalar_observed.final_drivers
                == scalar_observed_reference.final_drivers
            && scalar_observed.counters.invocations == 0U
            && scalar_observed.invocations_at_first_output == 0U
            && scalar_observed.counters.generic_update_commit_tickets > 0U,
        "observed generic projected scalar writes retain per-delta parity and demote after the output callback");
    const auto mixed_fork = run_case(true, 9U, Mode::unrelated_fork,
        true, false, false, true);
    const auto mixed_reference = run_case(true, 9U, Mode::unrelated_fork,
        false, false, false, true);
    require(mixed_fork.counters.invocations == 0U
            && mixed_fork.counters.fallbacks > 0U
            && mixed_fork.settled == mixed_reference.settled
            && mixed_fork.drivers == mixed_reference.drivers,
        "a mixed-kind fork declines sparse Logic9 native planes and preserves reference results");
    for (const auto [logic9, width] : { std::pair { false, 65U },
             std::pair { true, 9U }, std::pair { true, 65U },
             std::pair { true, 129U } }) {
        for (const auto mode : { Mode::normal, Mode::decline, Mode::force,
                 Mode::deposit, Mode::late_hook, Mode::invalid_second, Mode::unrelated_fork }) {
            const auto fused = run_case(logic9, width, mode, true, false, false);
            if (mode == Mode::invalid_second) {
                require(fused.rejected && fused.counters.invocations == 0U,
                    "an invalid second output cannot partially apply a fused projected result");
                continue;
            }
            const auto reference = run_case(logic9, width, mode, false, false, false);
            require(fused.boundary == reference.boundary && fused.drivers == reference.drivers
                    && fused.settled == reference.settled,
                "projected fusion preserves owner values, update order, and boundary delta traces");
            // A startup output callback materializes the installed region.
            // Retain its exact trace separately from the native-positive run.
            const auto observed_reference = run_case(logic9, width, mode, false, true, true);
            const auto observed_fused = run_case(logic9, width, mode, true, true, true);
            require(!observed_reference.boundary.empty()
                    && observed_fused.boundary == observed_reference.boundary
                    && observed_fused.drivers == observed_reference.drivers
                    && observed_fused.settled == observed_reference.settled,
                "observed projected cohorts preserve boundary deltas, owner values and update order");
            require(observed_fused.counters.invocations
                    == observed_fused.invocations_at_first_output,
                "output observation prevents later certified projected execution");
            require(mode == Mode::decline ? fused.counters.invocations == 0U
                                         : fused.counters.invocations > 0U,
                "projected differential cases exercise the requested execution route");
            require(fused.counters.owner_stage_calls_avoided == 0U,
                "compute fusion does not claim to eliminate retained projected owner staging");
            if (mode == Mode::force || mode == Mode::deposit || mode == Mode::late_hook) {
                require(fused.before_intervention > 0U, "late interventions follow real fused activations");
            }
            if (mode == Mode::late_hook || mode == Mode::deposit) {
                require(fused.counters.invocations == fused.before_intervention,
                    "late driver observation or deposit invalidates projected fusion");
            }
            if (mode == Mode::unrelated_fork) {
                require(fused.before_intervention > 0U
                        && fused.counters.invocations > fused.before_intervention,
                    "an unrelated fork preserves projected fusion after its dynamic child starts");
                require(fused.counters.fork_events == 1U
                        && fused.counters.fork_plans_invalidated == 0U
                        && fused.counters.fork_plans_surviving_after_last > 0U,
                    "the unrelated fork preserves the projected cohort certificate");
            }
        }
    }
}

} // namespace fsim::tests::runtime
