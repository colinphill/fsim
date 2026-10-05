// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
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

class ScopedPrivateRegionEnvironment final {
public:
    ScopedPrivateRegionEnvironment(const char* name, const char* value)
        : name_ { name }
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error("failed to set private-region environment");
        }
    }

    ScopedPrivateRegionEnvironment(const ScopedPrivateRegionEnvironment&) = delete;
    ScopedPrivateRegionEnvironment& operator=(
        const ScopedPrivateRegionEnvironment&) = delete;

    ~ScopedPrivateRegionEnvironment()
    {
        if (had_previous_) {
            static_cast<void>(set(previous_.c_str()));
        } else {
            unset();
        }
    }

private:
    bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value) == 0;
#else
        return ::setenv(name_.c_str(), value, 1) == 0;
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
    std::string previous_;
    bool had_previous_ { };
};

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

using Trace = std::tuple<std::string, SimulationTick, std::uint64_t,
    std::string>;

enum class Intervention {
    none,
    outside_reader,
    body_reader,
    late_observer,
    late_driver_observer,
    force_pending,
    deposit_pending,
    stop_between_consumers,
    blocking_between_consumers,
    late_sparse_output_observer,
};

struct Observation {
    std::vector<Trace> trace;
    std::vector<std::string> settled;
    std::vector<std::string> intermediate_settled;
    std::vector<std::string> drivers;
    RunStatus status { };
    std::uint64_t delta { };
    FusedMaskedRegionCounters masked;
    FusedStaticCounters producers;
    std::uint64_t retired_masked_executor_calls { };
};

class RetiredMaskedExecutorSentinel final : public FusedMaskedRegionExecutor {
public:
    explicit RetiredMaskedExecutorSentinel(std::uint64_t& invocation_count)
        : invocation_count_(invocation_count)
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&,
        const std::span<const std::uint64_t>) override
    {
        ++invocation_count_;
        return std::nullopt;
    }

private:
    std::uint64_t& invocation_count_;
};

// Exercise the native slot ingress: interpreted writes remain in the generic
// PendingUpdate list until the update phase, whereas native slots can already
// have changed the owned aggregate when an active-phase observer is installed.
class BitSlotExecutor final : public ProcessExecutor {
public:
    BitSlotExecutor(const ProcessId process, const SignalId input,
        const SignalId other, const SignalId target,
        const std::uint32_t width, const std::uint32_t bit,
        ProcessExecutorProgramBinding access_binding)
        : process_(process)
        , input_(input)
        , other_(other)
        , target_(target)
        , width_(width)
        , bit_(bit)
        , access_binding_(std::move(access_binding))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
        require(start == 0U || start == 7U,
            "the slot executor starts at entry or its static wait backedge");
        const auto scalar = binary_value(BinaryOperator::bit_xor,
            context.read_signal(input_).extract_bits(bit_, 1U),
            context.read_signal(other_).extract_bits(bit_, 1U));
        auto value = PackedLogic4 { width_, Logic4::z };
        value.insert_bits(scalar, bit_);
        std::uint32_t active = 1U;
        std::array<std::uint64_t, 3U> aval { };
        std::array<std::uint64_t, 3U> bval { };
        std::array<std::uint64_t, 3U> mask { };
        const auto words = (width_ + 63U) / 64U;
        require(words <= aval.size(), "the test slot storage fits its width");
        std::copy_n(value.aval_words().begin(), words, aval.begin());
        std::copy_n(value.bval_words().begin(), words, bval.begin());
        mask[bit_ / 64U] = UINT64_C(1) << (bit_ % 64U);
        const std::array slots { ProcessUpdateSlotView {
            target_, width_, words, &active,
            aval.data(), bval.data(), mask.data()
        } };
        std::array<std::uint64_t, 1U> active_words { UINT64_C(1) };
        const std::array batches { ProcessUpdateSlotBatch {
            process_, slots, active_words
        } };
        if (!context.write_validated_update_slot_batches(batches)) {
            context.write_update_slice(target_, scalar, bit_);
        }
        ProcessResumeResult result { 6U, 7U };
        result.external.kind = ExternalSuspendKind::wait_sensitivity;
        return result;
    }

private:
    ProcessId process_;
    SignalId input_;
    SignalId other_;
    SignalId target_;
    std::uint32_t width_;
    std::uint32_t bit_;
    ProcessExecutorProgramBinding access_binding_;
};

// Three independently reduced vectors feed a packed join. Additional levels
// pair the preceding reduction with an initialized constant. Each interior
// vector has disjoint owners, while the final observer is an ordinary process.
// This exercises a dependency graph rather than a named HDL primitive.
Observation run_fused_chain(const std::uint32_t width,
    const std::size_t depth, const ReductionOperator reduction,
    const Intervention intervention, const bool generic, const bool capture_output,
    const bool check_retired_owned_route = false,
    const bool capture_intermediate_state = true)
{
    std::uint64_t retired_masked_executor_calls { };
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    interpreter.set_fused_static_counters_enabled(true);
    Observation observation;
    std::vector<SignalId> intermediate_signals;
    std::vector<std::pair<ProcessId, SignalId>> drivers;
    ProcessId next_process { };
    const auto input = interpreter.add_signal(
        { "input", PackedLogic4 { width, Logic4::zero } });
    const auto other = interpreter.add_signal(
        { "other", PackedLogic4 { width, Logic4::zero } });
    const auto output = interpreter.add_signal(
        { "answer", PackedLogic4 { 1U, Logic4::z },
            ResolutionKind::sv_wire });
    const auto add_private = [&](const std::size_t bits) {
        const auto signal = interpreter.add_signal(
            { "intermediate_" + std::to_string(intermediate_signals.size()),
                PackedLogic4 { bits, Logic4::z },
                ResolutionKind::sv_wire });
        intermediate_signals.push_back(signal);
        return signal;
    };
    const auto add_process = [&](Process process) {
        process.id = next_process++;
        process.name = "compute_" + std::to_string(process.id);
        return interpreter.add_process(std::move(process));
    };
    const auto add_constant = [&](const SignalId signal,
                                  const std::uint32_t offset,
                                  const Logic4 value) {
        Process process;
        process.register_count = 1U;
        process.driver_regions.push_back({ signal, offset, 1U, false });
        process.operations = {
            LoadConstant { 0U, PackedLogic4 { 1U, value } },
            WriteUpdateSlice { signal, 0U, offset }, Halt { }
        };
        drivers.emplace_back(add_process(std::move(process)), signal);
    };
    const auto add_reduction = [&](const SignalId source,
                                   const SignalId target,
                                   const std::uint32_t offset,
                                   const ReductionOperator operation) {
        Process process;
        process.register_count = 2U;
        process.static_sensitivity.push_back({ source, EdgeKind::any });
        process.driver_regions.push_back({ target, offset, 1U, false });
        process.operations = {
            ReadSignal { 0U, source }, Reduction { operation, 1U, 0U },
            WriteUpdateSlice { target, 1U, offset },
            WaitSensitivity { }, Jump { 0U }
        };
        const auto id = add_process(std::move(process));
        drivers.emplace_back(id, target);
        return id;
    };

    const auto join = add_private(3U);
    std::vector<SignalId> leaves;
    for (std::size_t group = 0U; group < 3U; ++group) {
        leaves.push_back(add_private(width));
    }
    // Reverse allocation order deliberately: publication is by signal ID,
    // whereas process ordering follows the opposite order for these readers.
    std::vector<ProcessId> producer_ids;
    for (std::size_t group = leaves.size(); group-- > 0U;) {
        if (intervention == Intervention::blocking_between_consumers && group == 1U) {
            Process interloper;
            interloper.register_count = 1U;
            interloper.driver_regions = { { leaves[2U], 0U, width, true } };
            interloper.operations = {
                ReadSignal { 0U, leaves[1U] }, WriteBlocking { leaves[2U], 0U },
                Display { "blocking" }, WaitOn { { leaves[1U] } }, Jump { 0U }
            };
            drivers.emplace_back(add_process(std::move(interloper)), leaves[2U]);
        }
        (void)add_reduction(leaves[group], join,
            static_cast<std::uint32_t>(group), reduction);
    }
    for (std::size_t group = leaves.size(); group-- > 0U;) {
        const auto leaf = leaves[group];
        for (std::uint32_t bit = 0U; bit + 1U < width; ++bit) {
            Process process;
            process.register_count = 5U;
            process.static_sensitivity = {
                { input, EdgeKind::any }, { other, EdgeKind::any }
            };
            process.driver_regions.push_back({ leaf, bit, 1U, false });
            process.operations = {
                ReadSignal { 0U, input }, ReadSignal { 1U, other },
                Extract { 2U, 0U, bit, 1U },
                Extract { 3U, 1U, bit, 1U },
                Binary { BinaryOperator::bit_xor, 4U, 2U, 3U },
                WriteUpdateSlice { leaf, 4U, bit },
                WaitSensitivity { }, Jump { 0U }
            };
            const auto id = add_process(std::move(process));
            producer_ids.push_back(id);
            drivers.emplace_back(id, leaf);
            if (intervention != Intervention::none) {
                const auto& registered = interpreter.process_program(id);
                interpreter.set_process_executor(id,
                    std::make_unique<BitSlotExecutor>(
                        id, input, other, leaf, width, bit,
                        ProcessExecutorProgramBinding {
                            registered, registered, id }));
            }
        }
        add_constant(leaf, width - 1U,
            reduction == ReductionOperator::bit_and
                ? Logic4::one : Logic4::zero);
    }
    auto previous = join;
    for (std::size_t level = 0U; level < depth; ++level) {
        const auto next = add_private(2U);
        add_reduction(previous, next, 0U, ReductionOperator::bit_xor);
        add_constant(next, 1U, Logic4::zero);
        previous = next;
    }
    add_reduction(previous, output, 0U, ReductionOperator::bit_xor);

    const auto add_observer = [&](const SignalId signal,
                                  const SignalId trigger,
                                  const std::string& name) {
        Process process;
        process.register_count = 1U;
        process.static_sensitivity.push_back({ trigger, EdgeKind::any });
        process.operations = { ReadSignal { 0U, signal },
            Display { name }, WaitSensitivity { }, Jump { 0U } };
        (void)add_process(std::move(process));
    };
    add_observer(output, output, "boundary");
    if (intervention == Intervention::outside_reader
        || intervention == Intervention::late_sparse_output_observer) {
        add_observer(leaves.front(), leaves.front(), "outside");
    } else if (intervention == Intervention::body_reader) {
        add_observer(leaves.front(), input, "outside");
    }
    if (capture_output) {
        interpreter.set_output_hook(
            [&](ProcessId, const std::string_view text, bool,
                const SimulationTick time, const std::uint64_t delta) {
                const auto signal = text == "outside" ? leaves.front() : output;
                observation.trace.emplace_back(std::string { text }, time, delta,
                    interpreter.signal_value(signal).to_msb_string());
            });
    }
    if (generic) {
        // The existing observation predicate prevents private admission
        // without introducing any additional scheduler callbacks.
        interpreter.set_native_signal_observation_required_hook(
            [](SignalId) { return true; });
    }
    for (SimulationTick time = 1U; time <= 5U; ++time) {
        const auto value = time == 1U ? Logic4::one
            : time == 2U ? Logic4::x
            : time == 3U ? Logic4::z : Logic4::zero;
        interpreter.schedule_signal_at(input,
            PackedLogic4 { width, value }, time, 0U);
        interpreter.schedule_signal_at(other,
            PackedLogic4 { width, time == 5U ? Logic4::one : Logic4::zero },
            time, 1U);
    }
    if (intervention == Intervention::late_observer
        || intervention == Intervention::late_driver_observer
        || intervention == Intervention::force_pending
        || intervention == Intervention::deposit_pending) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
            std::numeric_limits<StableOrder>::max(),
            [&](Scheduler& scheduler) {
                // Run after the newly awakened producers but before their
                // deferred update commit, so demotion must retain staged bits.
                scheduler.schedule_next_delta(SchedulerPhase::active,
                    std::numeric_limits<StableOrder>::max(),
                    [&](Scheduler&) {
                        if (intervention == Intervention::late_observer) {
                            interpreter.set_signal_change_hook(
                                [&](const SignalId signal, const PackedLogic4&,
                                    const SimulationTick time) {
                                    observation.trace.emplace_back(
                                        "hook_" + std::to_string(signal), time,
                                        interpreter.scheduler().delta(),
                                        interpreter.signal_value(signal)
                                            .to_msb_string());
                                });
                        } else if (intervention
                            == Intervention::late_driver_observer) {
                            interpreter.set_driver_change_hook(
                                [&](const ProcessId process,
                                    const SignalId signal,
                                    const SimulationTick time) {
                                    observation.trace.emplace_back(
                                        "driver_" + std::to_string(process),
                                        time, interpreter.scheduler().delta(),
                                        interpreter.driver_value(process, signal)
                                            .to_msb_string());
                                });
                        } else if (intervention == Intervention::force_pending) {
                            interpreter.force_signal(leaves.front(),
                                PackedLogic4 { width, Logic4::one });
                        } else {
                            interpreter.deposit_signal(leaves.front(),
                                PackedLogic4 { width, Logic4::one });
                        }
                    });
            });
    }
    if (intervention == Intervention::force_pending) {
        interpreter.scheduler().schedule_at(3U, SchedulerPhase::active,
            std::numeric_limits<StableOrder>::max(),
            [&](Scheduler&) { interpreter.release_signal(leaves.front()); });
    }
    if (intervention == Intervention::late_sparse_output_observer) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active,
            std::numeric_limits<StableOrder>::max(),
            [&](Scheduler& first) {
                first.schedule_next_delta(SchedulerPhase::active,
                    std::numeric_limits<StableOrder>::max(),
                    [&](Scheduler& second) {
                        second.schedule_next_delta(SchedulerPhase::active,
                            std::numeric_limits<StableOrder>::max(),
                            [&](Scheduler&) {
                                interpreter.set_driver_change_hook(
                                    [&](const ProcessId process, const SignalId signal,
                                        const SimulationTick time) {
                                        observation.trace.emplace_back(
                                            "pending_driver_" + std::to_string(process), time,
                                            interpreter.scheduler().delta(),
                                            interpreter.driver_value(process, signal).to_msb_string());
                                    });
                            });
                    });
            });
    }
    if (intervention == Intervention::stop_between_consumers) {
        interpreter.scheduler().schedule_at(2U, SchedulerPhase::active, 0U,
            [&](Scheduler& scheduler) {
                scheduler.schedule_next_delta(SchedulerPhase::active, 0U,
                    [&](Scheduler& next) {
                        next.schedule_next_delta(SchedulerPhase::active, 1U,
                            [&](Scheduler& frontier) {
                                observation.trace.emplace_back("stop",
                                    frontier.now(), frontier.delta(), "");
                                frontier.request_stop();
                            });
                    });
            });
    }
    interpreter.start();
    if (!generic) {
        if (check_retired_owned_route && !capture_output) {
            const auto candidates = interpreter.fused_static_cohort_candidates();
            require(std::ranges::none_of(candidates,
                        [&](const auto& candidate) {
                            return candidate.members == producer_ids;
                        }),
                "the multi-owner aggregate slot has no retired static plan");
            const auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            require(std::ranges::all_of(leaves,
                        [&](const SignalId signal) {
                            return implementation.owned_driver_active(signal);
                        }),
                "the unsupported aggregate cohort remains on legacy composite-backed checked execution");
        }
        require(interpreter.fused_masked_region_candidates().empty(),
            "the source-compatible masked facade exposes no retired plans");
        bool rejected { };
        try {
            interpreter.install_fused_masked_region(0U, { },
                std::make_unique<RetiredMaskedExecutorSentinel>(
                    retired_masked_executor_calls));
        } catch (const std::logic_error& error) {
            rejected = std::string_view { error.what() }
                == "invalid fused masked region binding";
        }
        require(rejected,
            "the retired region installer preserves its unknown-id error");
    }
    for (SimulationTick time = 0U; time <= 5U; ++time) {
        auto result = interpreter.run(time);
        if (result.status == RunStatus::stopped) {
            require(intervention == Intervention::stop_between_consumers,
                "only the interleaved stop fixture may pause execution");
            interpreter.scheduler().clear_stop();
            result = interpreter.run(time);
        }
        observation.status = result.status;
        observation.delta = result.delta;
        if (capture_intermediate_state) {
            observation.settled.push_back(
                interpreter.signal_value(output).to_msb_string());
            for (const auto signal : intermediate_signals) {
                observation.intermediate_settled.push_back(
                    interpreter.signal_value(signal).to_msb_string());
            }
        }
    }
    if (!capture_intermediate_state) {
        // Read only settled final state after scheduled execution.
        observation.settled.push_back(
            interpreter.signal_value(output).to_msb_string());
        for (const auto signal : intermediate_signals) {
            observation.intermediate_settled.push_back(
                interpreter.signal_value(signal).to_msb_string());
        }
    }
    for (const auto& [process, signal] : drivers) {
        observation.drivers.push_back(
            interpreter.driver_value(process, signal).to_msb_string());
    }
    observation.masked = interpreter.fused_masked_region_counters();
    observation.producers = interpreter.fused_static_counters();
    observation.retired_masked_executor_calls = retired_masked_executor_calls;
    return observation;
}

void compare_fused_chain(const std::uint32_t width,
    const std::size_t depth, const ReductionOperator reduction,
    const Intervention intervention, const bool check_retired_owned_route = false)
{
    const auto compare_observations = [](const Observation& generic,
                                           const Observation& candidate) {
        require(candidate.trace == generic.trace,
            "fused chains preserve every boundary value, delta and callback order");
        require(candidate.settled == generic.settled,
            "fused chains preserve settled four-state values at each timestamp");
        require(candidate.intermediate_settled == generic.intermediate_settled,
            "private backing values remain queryable at each timestamp");
        require(candidate.drivers == generic.drivers,
            "fused chains preserve independently observable raw driver values");
        require(candidate.status == generic.status
                && candidate.delta == generic.delta,
            "fused chains preserve completion and final delta");
        require(candidate.settled[2U] == "X" && candidate.settled[3U] == "X",
            "unknown and high impedance operands propagate through reductions");
    };

    // Keep the same observers in both runs. The compatibility run also checks
    // that no retired masked plan can be installed before checked scheduling.
    const auto generic = run_fused_chain(
        width, depth, reduction, intervention, true, false, check_retired_owned_route,
        false);
    const auto candidate = run_fused_chain(
        width, depth, reduction, intervention, false, false, check_retired_owned_route,
        false);
    require(candidate.trace == generic.trace
            && candidate.settled == generic.settled
            && candidate.intermediate_settled == generic.intermediate_settled
            && candidate.drivers == generic.drivers
            && candidate.status == generic.status
            && candidate.delta == generic.delta,
        "checked chains preserve final boundary, backing, driver and status parity");
    require(candidate.retired_masked_executor_calls == 0U,
        "the retired masked executor is never entered; writes use checked scheduling");
    if (check_retired_owned_route) {
        require(candidate.producers.invocations == 0U,
            "the retired aggregate static adapter is declined in favor of checked scheduling");
    }
    require(candidate.masked.private_candidates == 0U
            && candidate.masked.private_local_commits == 0U
            && candidate.masked.private_owned_direct_commits == 0U
            && candidate.masked.private_fanout_entries_avoided == 0U
            && candidate.masked.private_masked_notifications == 0U,
        "retired private-bridge diagnostics remain zero during scheduled handoff");
    if (intervention == Intervention::late_sparse_output_observer) {
        require(std::ranges::any_of(candidate.trace, [](const Trace& entry) {
            return std::get<0>(entry).starts_with("pending_driver_");
        }),
            "the late driver observer runs for writes after it is installed");
    }

    const auto observed_generic = run_fused_chain(
        width, depth, reduction, intervention, true, true, check_retired_owned_route);
    const auto observed_candidate = run_fused_chain(
        width, depth, reduction, intervention, false, true, check_retired_owned_route);
    compare_observations(observed_generic, observed_candidate);
    require(std::ranges::any_of(observed_candidate.trace,
                [](const Trace& entry) {
                    return std::get<0>(entry) == "boundary";
                }),
        "output-hook observation must expose a boundary callback trace");
    require(observed_candidate.retired_masked_executor_calls == 0U,
        "output-hook observation does not re-enter the retired masked executor");
}

void check_transaction_observed_region(const bool observed)
{
    Interpreter interpreter;
    const auto first = interpreter.add_signal({ "first", PackedLogic4(1U, Logic4::zero) });
    const auto second = interpreter.add_signal({ "second", PackedLogic4(1U, Logic4::zero) });
    const auto output = interpreter.add_signal(
        { "joined", PackedLogic4(2U, Logic4::z), ResolutionKind::sv_wire });
    for (std::uint32_t bit = 0U; bit < 2U; ++bit) {
        Process owner;
        owner.id = bit;
        owner.name = "owner_" + std::to_string(bit);
        owner.register_count = 1U;
        owner.static_sensitivity = { { bit == 0U ? first : second, EdgeKind::any } };
        owner.driver_regions = { { output, bit, 1U, false } };
        owner.operations = { LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
            WriteUpdateSlice { output, 0U, bit }, WaitSensitivity { }, Jump { 0U } };
        (void)interpreter.add_process(std::move(owner));
    }
    if (observed) {
        Process observer;
        observer.id = 2U;
        observer.name = "transaction_observer";
        observer.initialize = false;
        observer.static_sensitivity = { { output, EdgeKind::transaction } };
        observer.operations = { Display { "transaction" }, WaitSensitivity { }, Jump { 0U } };
        (void)interpreter.add_process(std::move(observer));
    }
    std::vector<Trace> trace;
    interpreter.set_output_hook([&](ProcessId, std::string_view text, bool,
                                    const SimulationTick time, const std::uint64_t delta) {
        trace.emplace_back(text, time, delta, interpreter.signal_value(output).to_msb_string());
    });
    interpreter.schedule_signal_at(first, PackedLogic4(1U, Logic4::one), 1U, 0U);
    interpreter.start();
    const auto candidates = interpreter.fused_masked_region_candidates();
    require(candidates.empty(),
        "the retired masked planner exposes no transaction or nontransaction candidates");
    (void)interpreter.run(1U);
    if (observed) {
        require(std::ranges::any_of(trace, [](const auto& row) {
            return std::get<1>(row) == 1U && std::get<3>(row) == "00";
        }), "an unchanged owner write still wakes the transaction observer");
    }
}

struct CallbackObservationResult {
    std::vector<std::string> output_calls;
    std::vector<std::string> report_calls;
    std::string state_after_release;
    std::uint64_t retired_masked_executor_calls { };
};

CallbackObservationResult run_callback_observation_case(
    const bool install_region)
{
    std::uint64_t retired_masked_executor_calls { };
    Interpreter initial;
    CallbackObservationResult observation;
    const auto first_input = initial.add_signal({
        "callback_first_input", PackedLogic4 { 1U, Logic4::zero }
    });
    const auto second_input = initial.add_signal({
        "callback_second_input", PackedLogic4 { 1U, Logic4::zero }
    });
    const auto target = initial.add_signal({
        "callback_target", PackedLogic4 { 2U, Logic4::z },
        ResolutionKind::sv_wire
    });

    const std::array inputs { first_input, second_input };
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        const auto bit = static_cast<std::uint32_t>(index);
        Process owner;
        owner.id = static_cast<ProcessId>(index);
        owner.name = "callback_owner_" + std::to_string(bit);
        owner.register_count = 2U;
        owner.static_sensitivity = { { inputs[bit], EdgeKind::any } };
        owner.driver_regions = { { target, bit, 1U, false } };
        owner.operations = {
            ReadSignal { 0U, inputs[bit] },
            Reduction { ReductionOperator::bit_xor, 1U, 0U },
            WriteUpdateSlice { target, 1U, bit },
            WaitSensitivity { }, Jump { 0U }
        };
        (void)initial.add_process(std::move(owner));
    }

    Process output_process;
    output_process.id = 2U;
    output_process.name = "callback_output_process";
    output_process.operations = {
        WaitFor { 2U }, Display { "first" },
        WaitFor { 2U }, Display { "second" }, Halt { }
    };
    (void)initial.add_process(std::move(output_process));

    Process report_process;
    report_process.id = 3U;
    report_process.name = "callback_report_process";
    report_process.register_count = 1U;
    report_process.operations = {
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::zero } },
        WaitFor { 3U },
        Assert { 0U, "callback report", AssertionSeverity::warning, { } },
        Halt { }
    };
    (void)initial.add_process(std::move(report_process));

    initial.schedule_signal_at(
        first_input, PackedLogic4 { 1U, Logic4::one }, 1U, 0U);
    initial.schedule_signal_at(
        second_input, PackedLogic4 { 1U, Logic4::one }, 1U, 1U);
    initial.schedule_signal_at(
        first_input, PackedLogic4 { 1U, Logic4::zero }, 3U, 0U);
    initial.schedule_signal_at(
        second_input, PackedLogic4 { 1U, Logic4::zero }, 3U, 1U);

    Interpreter* active = &initial;
    Interpreter::OutputHook replacement = [&](ProcessId,
        const std::string_view text, bool, SimulationTick, std::uint64_t) {
        observation.output_calls.push_back(
            std::string { text } + ":"
            + active->signal_value(target).to_msb_string() + ":"
            + active->stored_signal_value(target).to_msb_string() + ":"
            + active->driver_value(0U, target).to_msb_string() + ":"
            + active->driver_value(1U, target).to_msb_string());
        active->release_signal(target);
    };
    initial.set_output_hook(
        [&](ProcessId, const std::string_view text, bool,
            SimulationTick, std::uint64_t) {
            observation.output_calls.push_back(
                std::string { text } + ":"
                + active->signal_value(target).to_msb_string() + ":"
                + active->stored_signal_value(target).to_msb_string() + ":"
                + active->driver_value(0U, target).to_msb_string() + ":"
                + active->driver_value(1U, target).to_msb_string());
            active->set_output_hook(replacement);
            active->force_signal(target,
                PackedLogic4 { 2U, Logic4::x });
        });
    initial.set_report_hook(
        [&](ProcessId, const std::string_view, AssertionSeverity,
            const SourceLocation&, SimulationTick, std::uint64_t) {
            observation.report_calls.push_back(
                active->signal_value(target).to_msb_string() + ":"
                + active->stored_signal_value(target).to_msb_string() + ":"
                + active->driver_value(0U, target).to_msb_string() + ":"
                + active->driver_value(1U, target).to_msb_string());
        });

    // The callbacks retain Impl ownership while the public facade moves.
    Interpreter interpreter { std::move(initial) };
    active = &interpreter;
    interpreter.start();
    if (install_region) {
        require(interpreter.fused_masked_region_candidates().empty(),
            "the callback fixture uses checked scheduling after route retirement");
        bool rejected { };
        try {
            interpreter.install_fused_masked_region(0U, { },
                std::make_unique<RetiredMaskedExecutorSentinel>(
                    retired_masked_executor_calls));
        } catch (const std::logic_error& error) {
            rejected = std::string_view { error.what() }
                == "invalid fused masked region binding";
        }
        require(rejected,
            "callback users cannot bind a nonexistent retired masked plan");
    }

    (void)interpreter.run(2U);
    (void)interpreter.run(4U);
    observation.state_after_release
        = interpreter.signal_value(target).to_msb_string();
    observation.retired_masked_executor_calls = retired_masked_executor_calls;
    return observation;
}

void check_callback_observation_barrier()
{
    const auto generic = run_callback_observation_case(false);
    const auto candidate = run_callback_observation_case(true);
    require(generic.output_calls == candidate.output_calls
            && candidate.output_calls
                == std::vector<std::string> {
                    "first:11:11:Z1:1Z", "second:XX:00:Z0:0Z" }
            && generic.report_calls == candidate.report_calls
            && candidate.report_calls.size() == 1U
            && candidate.report_calls[0U].rfind("XX:", 0U) == 0U
            && generic.state_after_release == "00"
            && candidate.state_after_release == generic.state_after_release,
        "output and report callbacks observe current, stored and raw drivers "
        "across a force-held later owner update and release");
    require(candidate.retired_masked_executor_calls == 0U,
        "callback force and release use checked scheduling, not the retired executor");
}

struct TrustedTextOutputProbe {
    std::vector<std::string> trusted_text;
    std::string ordinary_text;
    PackedLogic4 ordinary_current;
    PackedLogic4 ordinary_last;
    PackedLogic4 ordinary_stored;
    PackedLogic4 ordinary_owner;
    std::optional<std::pair<SimulationTick, std::uint64_t>> ordinary_event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> ordinary_transaction;
    SignalEventSchedulingStamp ordinary_event_stamp;
    std::uint64_t ordinary_value_revision { };
    bool ordinary_barrier_completed { };
    bool ordinary_component_epoch_revoked { };
    bool ordinary_recertification_pending { };
    bool ordinary_snapshot_recertification_pending { };
};

void check_trusted_text_output_barrier_contract()
{
    ScopedPrivateRegionEnvironment region_kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };

    TrustedTextOutputProbe probe;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({
        "trusted_text.input", PackedLogic4 { 1U, Logic4::zero } });
    const auto trigger = interpreter.add_signal({
        "trusted_text.trigger", PackedLogic4 { 1U, Logic4::zero } });
    const auto intermediate = interpreter.add_signal({
        "trusted_text.intermediate", PackedLogic4 { 1U, Logic4::zero },
        ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({
        "trusted_text.output", PackedLogic4 { 1U, Logic4::zero },
        ResolutionKind::sv_wire });

    const auto add_region_member = [&](const ProcessId id,
                                       const SignalId source,
                                       const SignalId destination,
                                       const bool reads_intermediate) {
        Process process;
        process.id = id;
        process.name = "trusted_text_region_member_" + std::to_string(id);
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.initialize = true;
        process.register_count = 1U;
        process.static_sensitivity = { { trigger, EdgeKind::any } };
        if (reads_intermediate) {
            process.static_sensitivity.push_back(
                { intermediate, EdgeKind::any });
        }
        process.driver_regions = { { destination, 0U, 0U, true } };
        process.operations = {
            ReadSignal { 0U, source },
            WriteUpdate { destination, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(process)) == id,
            "trusted-text candidate members have stable process IDs");
    };
    add_region_member(0U, input, intermediate, false);
    add_region_member(1U, intermediate, output, true);

    const auto add_display = [&](const ProcessId id,
                                 const SimulationTick time,
                                 const char* text) {
        Process process;
        process.id = id;
        process.name = "trusted_text_display_" + std::to_string(id);
        process.operations = { WaitFor { time }, Display { text }, Halt { } };
        require(interpreter.add_process(std::move(process)) == id,
            "trusted-text display has a stable nonmember process ID");
    };
    add_display(2U, 2U, "trusted text");
    add_display(3U, 4U, "ordinary replacement");

    Process clock;
    clock.id = 4U;
    clock.name = "trusted_text_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::one } },
        WriteBlocking { input, 0U },
        WriteBlocking { trigger, 0U },
        WaitFor { 2U },
        LoadConstant { 0U, PackedLogic4 { 1U, Logic4::zero } },
        WriteBlocking { input, 0U },
        WriteBlocking { trigger, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == 4U,
        "trusted-text input changes use the ordinary SV scheduling path");

    interpreter.set_trusted_text_output_hook(
        [&probe](const ProcessId process, const std::string_view text,
            const bool, const SimulationTick, const std::uint64_t) {
            require(process == 2U,
                "the trusted callback receives the first display only");
            probe.trusted_text.emplace_back(text);
        });

    interpreter.start();
    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::time_limit,
        "trusted-text candidate members settle at their startup waits");

    const auto& initial = OwnedDriverDemotionTestAccess::implementation(
        interpreter);
    require(initial.region_graph.has_value()
            && initial.region_component_by_process.size() > 1U
            && initial.region_component_by_process[0U]
                == initial.region_component_by_process[1U],
        "the output writers belong to one certified region component");
    const auto component = initial.region_component_by_process[0U];
    require(component < initial.region_activation_programs.size()
            && initial.region_activation_programs[component].has_value()
            && initial.region_graph->component_epochs_current(component),
        "the candidate component has a current activation certificate");
    const auto first_input_slot = interpreter.run(1U);
    require(first_input_slot.status == RunStatus::time_limit,
        "the first scheduled input update reaches the output slot");
    const auto& before_trusted_output
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(before_trusted_output.region_graph
                ->component_epochs_current(component),
        "the component is current after the input update settles");
    const auto output_slot_generation
        = before_trusted_output.region_runtime_generation;
    const auto output_slot_capability_epoch
        = before_trusted_output.region_graph->capability_epoch(0U);

    (void)interpreter.run(2U);
    require(probe.trusted_text == std::vector<std::string> { "trusted text" },
        "the trusted output hook receives the exact formatted text");

    const auto& trusted_return = OwnedDriverDemotionTestAccess::implementation(
        interpreter);
    require(trusted_return.region_runtime_generation
                == output_slot_generation
            && trusted_return.region_graph->component_epochs_current(component)
            && trusted_return.region_graph->capability_epoch(0U)
                == output_slot_capability_epoch
            && trusted_return.completed_callback_observation_generation
                != trusted_return.region_runtime_generation,
        "formatted-text output leaves the current graph certificate alone and skips implicit callback observation");

    interpreter.prepare_output_callback_observation();
    const auto& explicit_barrier = OwnedDriverDemotionTestAccess::implementation(
        interpreter);
    const auto* const first_owner
        = explicit_barrier.driver_values.at(output).find(1U);
    require(explicit_barrier.completed_callback_observation_generation
                == explicit_barrier.region_runtime_generation
            && !explicit_barrier.region_graph->component_epochs_current(component)
            && explicit_barrier.region_recertification_pending
            && explicit_barrier.region_recertification_requires_snapshot
            && explicit_barrier.signals.at(output).initial_value
                == PackedLogic4 { 1U, Logic4::one }
            && explicit_barrier.signal_last_values.at(output)
                == PackedLogic4 { 1U, Logic4::zero }
            && explicit_barrier.driven_values.at(output)
                == PackedLogic4 { 1U, Logic4::one }
            && first_owner != nullptr
            && first_owner->value == PackedLogic4 { 1U, Logic4::one }
            && explicit_barrier.signal_events.at(output)
                == explicit_barrier.signal_transactions.at(output)
            && explicit_barrier.signal_events.at(output).has_value()
            && explicit_barrier.signal_events.at(output)->first == 1U,
        "the explicit barrier materializes exact signal, owner and event roles while revoking the component epoch");

    interpreter.set_output_hook(
        [&probe, &interpreter, component, output](const ProcessId process,
            const std::string_view text, const bool,
            const SimulationTick, const std::uint64_t) {
            require(process == 3U,
                "the ordinary replacement receives the second display");
            probe.ordinary_text = text;
            const auto& ordinary
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            const auto* const owner
                = ordinary.driver_values.at(output).find(1U);
            probe.ordinary_barrier_completed
                = ordinary.completed_callback_observation_generation
                    == ordinary.region_runtime_generation;
            probe.ordinary_component_epoch_revoked
                = !ordinary.region_graph->component_epochs_current(component);
            probe.ordinary_recertification_pending
                = ordinary.region_recertification_pending;
            probe.ordinary_snapshot_recertification_pending
                = ordinary.region_recertification_requires_snapshot;
            probe.ordinary_current
                = ordinary.signals.at(output).initial_value;
            probe.ordinary_last = ordinary.signal_last_values.at(output);
            probe.ordinary_stored = ordinary.driven_values.at(output);
            probe.ordinary_owner = owner != nullptr
                ? owner->value : PackedLogic4 { };
            probe.ordinary_event = ordinary.signal_events.at(output);
            probe.ordinary_transaction
                = ordinary.signal_transactions.at(output);
            probe.ordinary_event_stamp
                = ordinary.signal_event_scheduling_stamps.at(output);
            probe.ordinary_value_revision
                = ordinary.signal_value_revisions.at(output);
        });

    (void)interpreter.run(4U);
    require(probe.ordinary_text == "ordinary replacement"
            && probe.ordinary_barrier_completed
            && probe.ordinary_component_epoch_revoked
            && probe.ordinary_recertification_pending
            && probe.ordinary_snapshot_recertification_pending
            && probe.ordinary_current == PackedLogic4 { 1U, Logic4::zero }
            && probe.ordinary_last == PackedLogic4 { 1U, Logic4::one }
            && probe.ordinary_stored == PackedLogic4 { 1U, Logic4::zero }
            && probe.ordinary_owner == PackedLogic4 { 1U, Logic4::zero }
            && probe.ordinary_event == probe.ordinary_transaction
            && probe.ordinary_event.has_value()
            && probe.ordinary_event->first == 3U
            && probe.ordinary_event_stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && probe.ordinary_event_stamp.origin.phase == SchedulerPhase::active
            && probe.ordinary_value_revision != 0U,
        "ordinary callback replacement restores full observation before entry after later scheduled traffic");
}

void check_executor_access_provenance_gates_hidden_plans()
{
    class AccessExecutor final : public ProcessExecutor {
    public:
        explicit AccessExecutor(
            std::optional<ProcessExecutorProgramBinding> access_binding)
            : access_binding_(std::move(access_binding))
        {
        }

        [[nodiscard]] const ProcessExecutorProgramBinding*
        program_access_binding() const noexcept override
        {
            return access_binding_ ? &*access_binding_ : nullptr;
        }

        [[nodiscard]] ProcessResumeResult resume(
            ProcessExecutionContext&,
            const InstructionIndex start) override
        {
            ProcessResumeResult result { start, start + 1U };
            result.external.kind = ExternalSuspendKind::halt;
            return result;
        }

    private:
        std::optional<ProcessExecutorProgramBinding> access_binding_;
    };

    enum class ExecutorMode { none, opaque, bound };
    struct CandidateCounts {
        std::size_t static_candidates { };
        std::size_t masked_candidates { };
        bool exact_graph_proof { };
        bool disjoint_a4_slots { };
        bool exact_static_cohort { };
    };
    const auto candidate_counts = [](const ExecutorMode mode) {
        ScopedPrivateRegionEnvironment region_kernel {
            "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedPrivateRegionEnvironment single_owner_disabled {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
        ScopedPrivateRegionEnvironment disjoint_owner_enabled {
            "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "1" };
        ScopedPrivateRegionEnvironment local_wave_disabled {
            "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
        Interpreter interpreter;
        const auto trigger = interpreter.add_signal({
            "trigger", PackedLogic4 { 1U, Logic4::zero }
        });
        constexpr std::uint32_t output_width = 65U;
        const auto input = interpreter.add_signal({
            "slice_input", PackedLogic4 { output_width, Logic4::zero }
        });
        const auto output = interpreter.add_signal({
            "output", PackedLogic4 { output_width, Logic4::z },
            ResolutionKind::sv_wire
        });
        for (ProcessId id = 0U; id < 2U; ++id) {
            const auto offset = id == 0U ? 0U : 64U;
            const auto width = id == 0U ? 64U : 1U;
            Process process;
            process.id = id;
            process.name = "owner_" + std::to_string(id);
            process.scheduling_domain = ProcessSchedulingDomain::generic;
            process.register_count = 3U;
            process.initialize = false;
            process.static_sensitivity = { { trigger, EdgeKind::any } };
            process.driver_regions = {
                { output, offset, width, false },
            };
            process.operations = {
                ReadSignal { 0U, input },
                ReadSignal { 2U, trigger },
                Extract { 1U, 0U, offset, width },
                WriteUpdateSlice { output, 1U, offset,
                    SignalUpdateDomain::generic },
                WaitSensitivity { },
                Jump { 0U },
            };
            require(interpreter.add_process(std::move(process)) == id,
                "access-provenance owners have dense process IDs");
        }

        const std::array masked_inputs {
            interpreter.add_signal({
                "masked_first_input", PackedLogic4 { 1U, Logic4::zero } }),
            interpreter.add_signal({
                "masked_second_input", PackedLogic4 { 1U, Logic4::zero } })
        };
        const auto masked_output = interpreter.add_signal({
            "masked_output", PackedLogic4 { 2U, Logic4::z },
            ResolutionKind::sv_wire
        });
        for (std::size_t index = 0U; index < masked_inputs.size(); ++index) {
            const auto id = static_cast<ProcessId>(index + 2U);
            const auto bit = static_cast<std::uint32_t>(index);
            Process process;
            process.id = id;
            process.name = "masked_owner_" + std::to_string(index);
            process.scheduling_domain = ProcessSchedulingDomain::generic;
            process.register_count = 1U;
            process.initialize = false;
            process.static_sensitivity = {
                { masked_inputs[index], EdgeKind::any }
            };
            process.driver_regions = {
                { masked_output, bit, 1U, false }
            };
            process.operations = {
                LoadConstant { 0U, PackedLogic4 { 1U, Logic4::one } },
                WriteUpdateSlice { masked_output, 0U, bit,
                    SignalUpdateDomain::generic },
                WaitSensitivity { }, Jump { 0U },
            };
            require(interpreter.add_process(std::move(process)) == id,
                "masked provenance owners have dense process IDs");
        }

        if (mode != ExecutorMode::none) {
            Process process;
            process.id = 4U;
            process.name = "executor_without_signal_effects";
            process.initialize = false;
            process.operations = { Halt { } };
            const auto id = interpreter.add_process(std::move(process));
            std::optional<ProcessExecutorProgramBinding> binding;
            if (mode == ExecutorMode::bound) {
                const auto& registered = interpreter.process_program(id);
                binding.emplace(registered, registered, id);
            }
            interpreter.set_process_executor(id,
                std::make_unique<AccessExecutor>(std::move(binding)));
        }

        interpreter.start();
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto* const graph = implementation.region_graph.has_value()
            ? &*implementation.region_graph : nullptr;
        const bool exact_graph_proof = graph != nullptr
            && graph->processes().size() > 1U
            && graph->signals().size() > output
            && graph->processes()[0U].pure
            && graph->processes()[1U].pure
            && graph->processes()[0U].operation_write_ranges_exact
            && graph->processes()[1U].operation_write_ranges_exact
            && graph->processes()[0U].update_kind
                == RegionUpdateKind::generic
            && graph->processes()[1U].update_kind
                == RegionUpdateKind::generic
            && std::ranges::any_of(graph->signals()[output].writers,
                [&](const RegionAccess& writer) {
                    return writer.process == 0U && writer.offset == 0U
                        && writer.width == 64U;
                })
            && std::ranges::any_of(graph->signals()[output].writers,
                [&](const RegionAccess& writer) {
                    return writer.process == 1U && writer.offset == 64U
                        && writer.width == 1U;
                });
        auto* const authoritative
            = implementation.region_authoritative_state_for_signal(output);
        const bool disjoint_a4_slots = authoritative != nullptr
            && authoritative->values().packed_slots_bound()
            && authoritative->values().packed_signal_slots_bound(output)
            && authoritative->values().layout().contains(output)
            && authoritative->values().layout().signal(output).storage_class
                == SignalDriverStorageClass::disjoint_owner
            && authoritative->values().layout().owners(output).size() == 2U
            && authoritative->values().packed_owner_slot_bound(output, 0U)
            && authoritative->values().packed_owner_slot_bound(output, 1U)
            && !implementation.owned_driver_active(output);
        const auto static_candidates
            = interpreter.fused_static_cohort_candidates();
        const bool exact_static_cohort
            = std::ranges::any_of(static_candidates,
                [](const FusedStaticCohortCandidate& candidate) {
                    return candidate.members
                        == std::vector<ProcessId> { 0U, 1U };
                });
        return CandidateCounts {
            static_candidates.size(),
            interpreter.fused_masked_region_candidates().size(),
            exact_graph_proof,
            disjoint_a4_slots,
            exact_static_cohort
        };
    };

    const auto ordinary = candidate_counts(ExecutorMode::none);
    const auto opaque = candidate_counts(ExecutorMode::opaque);
    const auto bound = candidate_counts(ExecutorMode::bound);
    require(ordinary.static_candidates != 0U
            && ordinary.masked_candidates == 0U
            && ordinary.exact_graph_proof
            && ordinary.disjoint_a4_slots
            && ordinary.exact_static_cohort
            && opaque.static_candidates == 0U
            && opaque.masked_candidates == 0U
            && !opaque.exact_static_cohort
            && bound.static_candidates == ordinary.static_candidates
            && bound.masked_candidates == ordinary.masked_candidates
            && bound.exact_graph_proof
            && bound.disjoint_a4_slots
            && bound.exact_static_cohort,
        "only an exact executor signal-access binding permits hidden-state static cohorts");

    Process parent;
    parent.id = 7U;
    parent.name = "bound_parent";
    parent.operations = {
        ReadSignal { 0U, 12U },
        Halt { },
    };
    const ProcessExecutorProgramBinding binding { parent, parent, parent.id };
    auto child = parent;
    child.id = 8U;
    child.name = "bound_parent.$fork[0].child[0]";
    require(binding.matches_registered_program(parent.id, parent)
            && !binding.matches_registered_program(child.id, child)
            && binding.matches_forked_program(child.id, child),
        "a fork clone preserves its exact inherited access body across process IDs");
    child.operations.replace(0U, ReadSignal { 0U, 13U });
    require(!binding.matches_forked_program(child.id, child),
        "a fork clone with a changed signal-access body is opaque");
}

} // namespace

void test_private_signal_regions()
{
    check_trusted_text_output_barrier_contract();
    check_executor_access_provenance_gates_hidden_plans();
    check_callback_observation_barrier();
    check_transaction_observed_region(false);
    check_transaction_observed_region(true);
    for (const auto width : { 3U, 9U, 65U, 129U }) {
        compare_fused_chain(width, 2U, ReductionOperator::bit_xor,
            Intervention::none, width == 3U);
    }
    for (const auto intervention : { Intervention::outside_reader,
             Intervention::body_reader, Intervention::late_observer,
             Intervention::late_driver_observer, Intervention::force_pending,
             Intervention::deposit_pending, Intervention::stop_between_consumers,
             Intervention::late_sparse_output_observer }) {
        compare_fused_chain(65U, 2U, ReductionOperator::bit_xor,
            intervention);
    }
    for (const auto reduction : { ReductionOperator::bit_and,
             ReductionOperator::bit_or, ReductionOperator::bit_xor }) {
        compare_fused_chain(3U, 0U, reduction, Intervention::none);
        compare_fused_chain(9U, 2U, reduction, Intervention::none);
        compare_fused_chain(65U, 4U, reduction, Intervention::none);
        compare_fused_chain(129U, 2U, reduction, Intervention::none);
    }
    for (const auto intervention : { Intervention::outside_reader,
             Intervention::body_reader,
             Intervention::late_observer, Intervention::late_driver_observer,
             Intervention::force_pending,
             Intervention::deposit_pending,
             Intervention::stop_between_consumers }) {
        compare_fused_chain(7U, 1U,
            ReductionOperator::bit_xor, intervention);
    }
    compare_fused_chain(65U, 2U, ReductionOperator::bit_xor,
        Intervention::blocking_between_consumers);
    compare_fused_chain(129U, 1U, ReductionOperator::bit_xor,
        Intervention::late_sparse_output_observer);
}

} // namespace fsim::tests::runtime
