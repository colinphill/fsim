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
    std::vector<std::string> private_settled;
    std::vector<std::string> drivers;
    RunStatus status { };
    std::uint64_t delta { };
    FusedMaskedRegionCounters masked;
    FusedStaticCounters producers;
    std::uint64_t masked_before_output_hook { };
};

class ProducerRegionExecutor final : public FusedStaticCohortExecutor {
public:
    ProducerRegionExecutor(Interpreter& interpreter, const SignalId input,
        const SignalId other, std::vector<SignalId> outputs, const std::uint32_t width)
        : interpreter_(interpreter)
        , input_(input)
        , other_(other)
        , outputs_(std::move(outputs))
        , width_(width)
        , slots_(outputs_.size())
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&) override
    {
        const auto value = binary_value(BinaryOperator::bit_xor,
            interpreter_.signal_value(input_), interpreter_.signal_value(other_));
        const auto words = (width_ + 63U) / 64U;
        require(words <= aval_.size(), "the producer test output fits its scratch planes");
        std::copy_n(value.aval_words().begin(), words, aval_.begin());
        std::copy_n(value.bval_words().begin(), words, bval_.begin());
        mask_.fill(0U);
        for (std::uint32_t bit = 0U; bit + 1U < width_; ++bit) {
            mask_[bit / 64U] |= UINT64_C(1) << (bit % 64U);
        }
        active_ = 1U;
        for (std::size_t index = 0U; index < outputs_.size(); ++index) {
            slots_[index] = { outputs_[index], width_, words, &active_,
                aval_.data(), bval_.data(), mask_.data() };
        }
        return FusedStaticCohortResume { slots_, { } };
    }

private:
    Interpreter& interpreter_;
    SignalId input_;
    SignalId other_;
    std::vector<SignalId> outputs_;
    std::uint32_t width_;
    std::uint32_t active_ { };
    std::array<std::uint64_t, 3> aval_ { }, bval_ { }, mask_ { };
    std::vector<ProcessUpdateSlotView> slots_;
};

class ReductionRegionExecutor final : public FusedMaskedRegionExecutor {
public:
    ReductionRegionExecutor(Interpreter& interpreter,
        const std::vector<ProcessId>& members, const SignalId output)
        : interpreter_(interpreter)
        , members_(members)
        , output_(output)
    {
    }

    std::optional<FusedStaticCohortResume> resume(
        const ProcessCohortNativeContext&,
        const std::span<const std::uint64_t> activation) override
    {
        const auto width = static_cast<std::uint32_t>(interpreter_.signal_value(output_).width());
        auto combined = PackedLogic4(width, Logic4::zero);
        mask_.fill(0U);
        for (std::size_t index = 0U; index < members_.size(); ++index) {
            if ((activation[index / 64U] & (UINT64_C(1) << (index % 64U))) == 0U) {
                continue;
            }
            const auto& program = interpreter_.process_program(members_[index]);
            const auto read = operation_get<ReadSignal>(program.operations[0]);
            const auto reduction = operation_get<Reduction>(program.operations[1]);
            const auto write = operation_get<WriteUpdateSlice>(program.operations[2]);
            const auto& source = interpreter_.signal_value(read.signal);
            auto value = PackedLogic4(1U, reduction.operation == ReductionOperator::bit_and
                ? Logic4::one : Logic4::zero);
            const auto binary = reduction.operation == ReductionOperator::bit_and
                ? BinaryOperator::bit_and : reduction.operation == ReductionOperator::bit_or
                ? BinaryOperator::bit_or : BinaryOperator::bit_xor;
            for (std::size_t bit = 0U; bit < source.width(); ++bit) {
                value = binary_value(binary, value, source.extract_bits(bit, 1U));
            }
            combined.insert_bits(value, write.offset);
            mask_[write.offset / 64U] |= UINT64_C(1) << (write.offset % 64U);
        }
        const auto words = (width + 63U) / 64U;
        require(words <= aval_.size(), "the masked test output fits its scratch planes");
        std::copy_n(combined.aval_words().begin(), words, aval_.begin());
        std::copy_n(combined.bval_words().begin(), words, bval_.begin());
        active_ = 1U;
        slots_[0] = { output_, width, words, &active_,
            aval_.data(), bval_.data(), mask_.data() };
        return FusedStaticCohortResume { slots_, { } };
    }

private:
    Interpreter& interpreter_;
    std::vector<ProcessId> members_;
    SignalId output_;
    std::array<std::uint64_t, 3> aval_ { }, bval_ { }, mask_ { };
    std::uint32_t active_ { };
    std::array<ProcessUpdateSlotView, 1> slots_ { };
};

// Exercise the native slot ingress: interpreted writes remain in the generic
// PendingUpdate list until the update phase, whereas native slots can already
// have changed the owned aggregate when an active-phase observer is installed.
class BitSlotExecutor final : public ProcessExecutor {
public:
    BitSlotExecutor(const ProcessId process, const SignalId input,
        const SignalId other, const SignalId target,
        const std::uint32_t width, const std::uint32_t bit)
        : process_(process)
        , input_(input)
        , other_(other)
        , target_(target)
        , width_(width)
        , bit_(bit)
    {
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
};

// Three independently reduced vectors feed a packed join. Additional levels
// pair the preceding reduction with an initialized constant. Each interior
// vector has disjoint owners, while the final observer is an ordinary process.
// This exercises a dependency graph rather than a named HDL primitive.
Observation run_private_chain(const std::uint32_t width,
    const std::size_t depth, const ReductionOperator reduction,
    const Intervention intervention, const bool generic, const bool fuse_producers = false)
{
    Interpreter interpreter;
    interpreter.set_fused_masked_region_counters_enabled(true);
    interpreter.set_fused_static_counters_enabled(true);
    Observation observation;
    std::vector<SignalId> private_signals;
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
            { "intermediate_" + std::to_string(private_signals.size()),
                PackedLogic4 { bits, Logic4::z },
                ResolutionKind::sv_wire });
        private_signals.push_back(signal);
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
    std::vector<ProcessId> leaf_readers;
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
        leaf_readers.push_back(add_reduction(leaves[group], join,
            static_cast<std::uint32_t>(group), reduction));
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
                interpreter.set_process_executor(id,
                    std::make_unique<BitSlotExecutor>(
                        id, input, other, leaf, width, bit));
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
    interpreter.set_output_hook(
        [&](ProcessId, const std::string_view text, bool,
            const SimulationTick time, const std::uint64_t delta) {
            const auto signal = text == "outside" ? leaves.front() : output;
            observation.trace.emplace_back(std::string { text }, time, delta,
                interpreter.signal_value(signal).to_msb_string());
        });
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
                                observation.masked_before_output_hook
                                    = interpreter.fused_masked_region_counters().masked_calls;
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
        if (fuse_producers) {
            const auto candidates = interpreter.fused_static_cohort_candidates();
            const auto found = std::ranges::find_if(candidates, [&](const auto& candidate) {
                return candidate.members == producer_ids;
            });
            require(found != candidates.end(), "the private-chain producers form a native cohort");
            interpreter.install_fused_static_cohort(found->cohort_id,
                std::make_unique<ProducerRegionExecutor>(
                    interpreter, input, other, found->outputs, width));
        }
        const auto candidates = interpreter.fused_masked_region_candidates();
        auto eligible_readers = leaf_readers;
        if (intervention == Intervention::outside_reader
            || intervention == Intervention::late_sparse_output_observer) {
            // The observer shares the last reader's sensitivity key. Keep
            // that original multi-member cohort intact on its ordinary route.
            eligible_readers.pop_back();
        }
        const auto found = std::ranges::find_if(candidates, [&](const auto& candidate) {
            return candidate.members == eligible_readers
                && candidate.outputs == std::vector<SignalId> { join };
        });
        require(found != candidates.end(), "the private-chain reductions form a real masked region");
        std::vector<std::vector<Process::DriverRegion>> writes;
        for (const auto id : found->members) {
            const auto& process = interpreter.process_program(id);
            writes.emplace_back(process.driver_regions.begin(), process.driver_regions.end());
        }
        interpreter.install_fused_masked_region(found->region_id, std::move(writes),
            std::make_unique<ReductionRegionExecutor>(interpreter, found->members, join));
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
        observation.settled.push_back(
            interpreter.signal_value(output).to_msb_string());
        for (const auto signal : private_signals) {
            observation.private_settled.push_back(
                interpreter.signal_value(signal).to_msb_string());
        }
    }
    for (const auto& [process, signal] : drivers) {
        observation.drivers.push_back(
            interpreter.driver_value(process, signal).to_msb_string());
    }
    observation.masked = interpreter.fused_masked_region_counters();
    observation.producers = interpreter.fused_static_counters();
    return observation;
}

void compare_private_chain(const std::uint32_t width,
    const std::size_t depth, const ReductionOperator reduction,
    const Intervention intervention, const bool fuse_producers = false)
{
    const auto generic = run_private_chain(
        width, depth, reduction, intervention, true, fuse_producers);
    const auto candidate = run_private_chain(
        width, depth, reduction, intervention, false, fuse_producers);
    require(candidate.trace == generic.trace,
        "private chains preserve every boundary value, delta and callback order");
    require(candidate.settled == generic.settled,
        "private chains preserve settled four-state values at each timestamp");
    require(candidate.private_settled == generic.private_settled,
        "private backing values remain queryable at each timestamp");
    require(candidate.drivers == generic.drivers,
        "private chains preserve independently observable raw driver values");
    require(candidate.status == generic.status
            && candidate.delta == generic.delta,
        "private chains preserve completion and final delta");
    require(candidate.masked.masked_calls > 0U
            && candidate.masked.owner_stage_calls_avoided > 0U,
        "private-chain parity must exercise masked native staging, not only fallback");
    if (fuse_producers) {
        require(candidate.producers.invocations > 0U,
            "the private bridge must connect two actual native stages");
        require(candidate.masked.private_candidates > 0U
                && candidate.masked.private_local_commits > 0U
                && candidate.masked.private_owned_direct_commits > 0U
                && candidate.masked.private_fanout_entries_avoided > 0U
                && candidate.masked.private_masked_notifications > 0U,
            "private-bridge parity must exercise certified local publication and notification");
    }
    if (intervention == Intervention::late_sparse_output_observer) {
        require(candidate.masked_before_output_hook > 0U
                && candidate.masked.masked_calls == candidate.masked_before_output_hook,
            "late driver observation demotes the masked subset after pending output writes");
    }
    require(candidate.settled[2U] == "X" && candidate.settled[3U] == "X",
        "unknown and high impedance operands propagate through reductions");
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
    require(candidates.empty() == observed,
        "transaction observation excludes an otherwise eligible disjoint-owner region");
    (void)interpreter.run(1U);
    if (observed) {
        require(std::ranges::any_of(trace, [](const auto& row) {
            return std::get<1>(row) == 1U && std::get<3>(row) == "00";
        }), "an unchanged owner write still wakes the transaction observer");
    }
}

} // namespace

void test_private_signal_regions()
{
    check_transaction_observed_region(false);
    check_transaction_observed_region(true);
    for (const auto width : { 3U, 9U, 65U, 129U }) {
        compare_private_chain(width, 2U, ReductionOperator::bit_xor,
            Intervention::none, true);
    }
    for (const auto intervention : { Intervention::outside_reader,
             Intervention::body_reader, Intervention::late_observer,
             Intervention::late_driver_observer, Intervention::force_pending,
             Intervention::deposit_pending, Intervention::stop_between_consumers,
             Intervention::late_sparse_output_observer }) {
        compare_private_chain(65U, 2U, ReductionOperator::bit_xor,
            intervention, true);
    }
    for (const auto reduction : { ReductionOperator::bit_and,
             ReductionOperator::bit_or, ReductionOperator::bit_xor }) {
        compare_private_chain(3U, 0U, reduction, Intervention::none);
        compare_private_chain(9U, 2U, reduction, Intervention::none);
        compare_private_chain(65U, 4U, reduction, Intervention::none);
        compare_private_chain(129U, 2U, reduction, Intervention::none);
    }
    for (const auto intervention : { Intervention::outside_reader,
             Intervention::body_reader,
             Intervention::late_observer, Intervention::late_driver_observer,
             Intervention::force_pending,
             Intervention::deposit_pending,
             Intervention::stop_between_consumers }) {
        compare_private_chain(7U, 1U,
            ReductionOperator::bit_xor, intervention);
    }
    compare_private_chain(65U, 2U, ReductionOperator::bit_xor,
        Intervention::blocking_between_consumers);
    compare_private_chain(129U, 1U, ReductionOperator::bit_xor,
        Intervention::late_sparse_output_observer);
}

} // namespace fsim::tests::runtime
