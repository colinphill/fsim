// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class PureWaveTestExecutor;

struct PureWaveProbe {
    struct Response {
        std::vector<std::size_t> task_ends;
        std::vector<fsim::runtime::simir::InstructionIndex> start_instructions;
        std::optional<std::size_t> completed_tasks;
        bool updates_staged { true };
    };

    std::array<std::uint8_t, 4U> domains { };
    std::vector<std::pair<fsim::runtime::simir::ProcessId,
        fsim::runtime::simir::InstructionIndex>> resumes;
    std::vector<std::size_t> resume_counts;
    std::vector<std::vector<std::size_t>> task_ends;
    std::vector<std::vector<fsim::runtime::simir::InstructionIndex>>
        start_instructions;
    std::vector<std::vector<fsim::runtime::simir::ProcessId>>
        prepared_processes;
    std::vector<std::vector<const fsim::runtime::simir::PureWavePreparedMember*>>
        prepared_pointers;
    std::vector<std::vector<std::uint64_t>> prepared_generations;
    std::vector<std::vector<std::uint64_t>> prepared_owner_epochs;
    std::vector<std::vector<const void*>> prepared_owners;
    std::vector<fsim::runtime::simir::ProcessId> wave_executor_ids;
    std::vector<bool> execution_points_enabled;
    std::vector<Response> responses;
    std::vector<bool> decline_state_unchanged;
    std::vector<fsim::runtime::simir::ProcessId> flushed_processes;
    std::vector<std::size_t> prepare_counts;
    std::vector<PureWaveTestExecutor*> executors;
    std::vector<std::uint64_t> member_generations;
    std::vector<const fsim::runtime::simir::PureWavePreparedMember*>
        prepared_addresses;
    std::vector<bool*> queued_states;
    std::vector<bool*> waiting_states;
    std::vector<fsim::runtime::simir::ProcessStatus*> statuses;
    std::vector<std::uint64_t> prepare_owner_epochs;
    std::vector<const void*> prepare_owners;
    std::optional<fsim::runtime::simir::ProcessId> unsupported_process;
    std::optional<fsim::runtime::simir::ProcessId> halt_on_first_wake;
    std::optional<fsim::runtime::simir::InstructionIndex>
        decline_on_start_instruction;
    std::size_t throw_on_wave_call { };
    std::size_t throw_on_flush_call { };
    std::size_t wave_calls { };
    std::size_t prepared_kernel_members { };
    bool post_start_failure { };
};

class PureWaveTestExecutor final : public fsim::runtime::simir::ProcessExecutor {
public:
    PureWaveTestExecutor(
        PureWaveProbe& probe,
        const fsim::runtime::simir::ProcessId process,
        const std::size_t domain,
        const std::size_t operation_count)
        : probe_ { probe }
        , process_ { process }
        , domain_ { domain }
        , shape_ { operation_count_shape(operation_count) }
    {
    }

    [[nodiscard]] fsim::runtime::simir::ProcessResumeResult resume(
        fsim::runtime::simir::ProcessExecutionContext&,
        const fsim::runtime::simir::InstructionIndex start) override
    {
        using namespace fsim::runtime::simir;

        // The production executor invalidates its prepared compiler view
        // before ordinary execution changes the process PC or frame state.
        if (prepared_member_.valid) {
            prepared_member_.valid = false;
            prepared_member_.generation
                = ++probe_.member_generations[process_];
        }
        probe_.resumes.emplace_back(process_, start);
        ++probe_.resume_counts[process_];

        const auto boundary = start == 0U
            ? static_cast<InstructionIndex>(process_)
            : start;
        ProcessResumeResult result { boundary, boundary + 1U };
        if (probe_.halt_on_first_wake == process_ && start != 0U
            && probe_.resume_counts[process_] == 2U) {
            result.external.kind = ExternalSuspendKind::halt;
        } else {
            result.external.kind = ExternalSuspendKind::wait_sensitivity;
        }
        return result;
    }

    [[nodiscard]] const fsim::runtime::simir::PureWavePreparedMember*
    prepare_pure_wave_member(
        const fsim::runtime::simir::PureWaveResumeEntry& entry,
        fsim::runtime::simir::ProcessExecutionContext&,
        const fsim::runtime::simir::ProcessCohortNativeContext& context,
        const std::uint64_t owner_epoch) override
    {
        using namespace fsim::runtime::simir;

        ++probe_.prepare_counts[process_];
        probe_.queued_states[process_] = entry.queued;
        probe_.waiting_states[process_] = entry.waiting_on_static;
        probe_.statuses[process_] = entry.status;
        probe_.prepare_owner_epochs[process_] = owner_epoch;
        probe_.prepare_owners[process_] = context.owner;
        if (entry.process != process_ || entry.executor != this
            || probe_.unsupported_process == process_) {
            return nullptr;
        }

        prepared_member_.process = entry.process;
        prepared_member_.executor = entry.executor;
        prepared_member_.owner = context.owner;
        prepared_member_.domain = &probe_.domains[domain_];
        prepared_member_.compiler_view = nullptr;
        prepared_member_.update_batch = { };
        prepared_member_.and_lhs = 1U;
        prepared_member_.and_rhs = 2U;
        prepared_member_.resume_instruction = entry.start_instruction;
        prepared_member_.owner_epoch = owner_epoch;
        prepared_member_.generation = probe_.member_generations[process_];
        prepared_member_.shape = shape_;
        prepared_member_.valid = true;
        probe_.prepared_addresses[process_] = &prepared_member_;
        return &prepared_member_;
    }

    [[nodiscard]] std::optional<fsim::runtime::simir::PureWaveCompletion>
    try_resume_prepared_pure_wave(
        const std::span<const fsim::runtime::simir::PureWavePreparedMember* const>
            entries,
        const std::span<const std::size_t> task_ends,
        fsim::runtime::simir::ProcessExecutionContext&,
        const fsim::runtime::simir::ProcessCohortNativeContext& context)
        override
    {
        using namespace fsim::runtime::simir;

        ++probe_.wave_calls;
        probe_.wave_executor_ids.push_back(process_);
        probe_.task_ends.emplace_back(task_ends.begin(), task_ends.end());
        auto& starts = probe_.start_instructions.emplace_back();
        starts.reserve(entries.size());
        auto& processes = probe_.prepared_processes.emplace_back();
        auto& pointers = probe_.prepared_pointers.emplace_back();
        auto& generations = probe_.prepared_generations.emplace_back();
        auto& owner_epochs = probe_.prepared_owner_epochs.emplace_back();
        auto& owners = probe_.prepared_owners.emplace_back();
        processes.reserve(entries.size());
        pointers.reserve(entries.size());
        generations.reserve(entries.size());
        owner_epochs.reserve(entries.size());
        owners.reserve(entries.size());
        for (const auto* const entry : entries) {
            if (entry == nullptr) {
                throw std::logic_error {
                    "prepared pure-wave member pointer is null"
                };
            }
            starts.push_back(entry->resume_instruction);
            processes.push_back(entry->process);
            pointers.push_back(entry);
            generations.push_back(entry->generation);
            owner_epochs.push_back(entry->owner_epoch);
            owners.push_back(entry->owner);
        }
        probe_.execution_points_enabled.push_back(
            context.execution_points_enabled);

        if (probe_.throw_on_wave_call == probe_.wave_calls) {
            if (!entries.empty()) {
                const auto process = entries.front()->process;
                if (probe_.queued_states[process] != nullptr) {
                    *probe_.queued_states[process] = false;
                }
                if (probe_.waiting_states[process] != nullptr) {
                    *probe_.waiting_states[process] = false;
                }
                if (probe_.statuses[process] != nullptr) {
                    *probe_.statuses[process] = ProcessStatus::running;
                }
            }
            ++probe_.prepared_kernel_members;
            probe_.post_start_failure = true;
            throw std::runtime_error { "pure-wave test failure after start" };
        }

        const bool incompatible_middle
            = probe_.decline_on_start_instruction
            && std::any_of(entries.begin(), entries.end(), [&](const auto* entry) {
                return entry->resume_instruction
                    == *probe_.decline_on_start_instruction;
            });
        const bool hook_requires_fallback
            = context.execution_points_enabled;
        if (incompatible_middle || hook_requires_fallback) {
            probe_.decline_state_unchanged.push_back(
                std::all_of(entries.begin(), entries.end(), [&](const auto* entry) {
                    const auto process = entry->process;
                    return probe_.queued_states[process] != nullptr
                        && *probe_.queued_states[process]
                        && probe_.waiting_states[process] != nullptr
                        && *probe_.waiting_states[process]
                        && probe_.statuses[process] != nullptr
                        && *probe_.statuses[process] == ProcessStatus::waiting;
                }));
            return std::nullopt;
        }

        const auto response = std::find_if(
            probe_.responses.begin(), probe_.responses.end(),
            [&](const PureWaveProbe::Response& candidate) {
                return candidate.task_ends
                        == std::vector<std::size_t>(
                            task_ends.begin(), task_ends.end())
                    && candidate.start_instructions == starts;
            });
        if (response == probe_.responses.end()
            || !response->completed_tasks) {
            probe_.decline_state_unchanged.push_back(
                std::all_of(entries.begin(), entries.end(), [&](const auto* entry) {
                    const auto process = entry->process;
                    return probe_.queued_states[process] != nullptr
                        && *probe_.queued_states[process]
                        && probe_.waiting_states[process] != nullptr
                        && *probe_.waiting_states[process]
                        && probe_.statuses[process] != nullptr
                        && *probe_.statuses[process] == ProcessStatus::waiting;
                }));
            return std::nullopt;
        }

        const auto completed_tasks = *response->completed_tasks;
        if (completed_tasks == 0U || completed_tasks > task_ends.size()) {
            throw std::logic_error {
                "pure-wave test response exceeds complete task count"
            };
        }
        const auto completed_entries = completed_tasks == 0U
            ? 0U : task_ends[completed_tasks - 1U];
        if (completed_entries > entries.size()) {
            throw std::logic_error {
                "pure-wave task boundary exceeds its entry span"
            };
        }

        for (std::size_t index = 0U; index < completed_entries; ++index) {
            const auto process = entries[index]->process;
            *probe_.queued_states[process] = false;
            *probe_.waiting_states[process] = true;
            *probe_.statuses[process] = ProcessStatus::waiting;
        }
        probe_.prepared_kernel_members += completed_entries;
        return PureWaveCompletion {
            completed_tasks, response->updates_staged
        };
    }

    void flush_pure_wave_updates(
        fsim::runtime::simir::ProcessExecutionContext&) override
    {
        probe_.flushed_processes.push_back(process_);
        if (probe_.throw_on_flush_call == probe_.flushed_processes.size()) {
            probe_.post_start_failure = true;
            throw std::runtime_error {
                "pure-wave test failure during update flush"
            };
        }
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return &probe_.domains[domain_];
    }

    void invalidate_generation()
    {
        prepared_member_.generation
            = ++probe_.member_generations[process_];
    }

    void invalidate_native_context()
    {
        prepared_member_.owner = nullptr;
        prepared_member_.owner_epoch = 0U;
    }

private:
    [[nodiscard]] static fsim::runtime::simir::PureWavePreparedShape
    operation_count_shape(const std::size_t operation_count)
    {
        using fsim::runtime::simir::PureWavePreparedShape;

        switch (operation_count) {
        case 6U:
            return PureWavePreparedShape::wide_copy6;
        case 7U:
            return PureWavePreparedShape::reduction7;
        case 10U:
            return PureWavePreparedShape::logic4_bit_and;
        case 31U:
            return PureWavePreparedShape::reducer31;
        default:
            throw std::invalid_argument {
                "pure-wave fixture operation count has no prepared shape"
            };
        }
    }

    PureWaveProbe& probe_;
    fsim::runtime::simir::ProcessId process_ { };
    std::size_t domain_ { };
    fsim::runtime::simir::PureWavePreparedShape shape_ { };
    fsim::runtime::simir::PureWavePreparedMember prepared_member_;
};

fsim::runtime::simir::ProcessId add_waiting_process(
    fsim::runtime::simir::Interpreter& interpreter,
    PureWaveProbe& probe,
    const fsim::runtime::simir::SignalId trigger,
    const std::size_t operation_count,
    const bool reactive = false,
    const std::size_t domain = 0U)
{
    using namespace fsim::runtime::simir;

    const auto expected_id
        = static_cast<ProcessId>(probe.resume_counts.size());
    Process process;
    process.id = expected_id;
    process.name = "pure_wave_test_" + std::to_string(process.id);
    process.operations.reserve(operation_count);
    for (std::size_t index = 0U; index < operation_count; ++index) {
        process.operations.emplace_back(WaitSensitivity { });
    }
    process.static_sensitivity.push_back({ trigger, EdgeKind::any });
    process.reactive = reactive;
    const auto id = interpreter.add_process(std::move(process));
    require(id == expected_id,
        "pure-wave fixture process IDs must be sequential");
    probe.resume_counts.push_back(0U);
    probe.prepare_counts.push_back(0U);
    probe.member_generations.push_back(1U);
    probe.prepared_addresses.push_back(nullptr);
    probe.queued_states.push_back(nullptr);
    probe.waiting_states.push_back(nullptr);
    probe.statuses.push_back(nullptr);
    probe.prepare_owner_epochs.push_back(0U);
    probe.prepare_owners.push_back(nullptr);
    auto executor = std::make_unique<PureWaveTestExecutor>(
        probe, id, domain, operation_count);
    probe.executors.push_back(executor.get());
    interpreter.set_process_executor(id, std::move(executor));
    return id;
}

fsim::runtime::simir::SignalId add_trigger(
    fsim::runtime::simir::Interpreter& interpreter,
    const std::string& name)
{
    return interpreter.add_signal({
        name, fsim::runtime::PackedLogic4::from_msb_string("0")
    });
}

void schedule_edge(
    fsim::runtime::simir::Interpreter& interpreter,
    const fsim::runtime::simir::SignalId signal,
    const std::string_view value,
    const fsim::runtime::SimulationTick time)
{
    interpreter.schedule_signal_at(
        signal,
        fsim::runtime::PackedLogic4::from_msb_string(value),
        time);
}

void require_completed(fsim::runtime::simir::Interpreter& interpreter)
{
    const auto result = interpreter.run();
    require(result.status == fsim::runtime::RunStatus::completed,
        "pure-wave runtime fixture must complete");
}

} // namespace

void test_simir_pure_wave_pending_snapshot_and_whole_task_prefix()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.responses = {
        { { 1U, 2U }, { 3U, 4U }, std::size_t { 1U } }
    };
    Interpreter interpreter;
    const auto cohort_trigger = add_trigger(interpreter, "wave.cohort");
    const auto first_singleton_trigger
        = add_trigger(interpreter, "wave.singleton_first");
    const auto second_singleton_trigger
        = add_trigger(interpreter, "wave.singleton_second");
    (void)add_waiting_process(
        interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(
        interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(
        interpreter, probe, first_singleton_trigger, 7U);
    (void)add_waiting_process(
        interpreter, probe, second_singleton_trigger, 6U);

    schedule_edge(interpreter, cohort_trigger, "1", 1U);
    schedule_edge(interpreter, first_singleton_trigger, "1", 1U);
    schedule_edge(interpreter, second_singleton_trigger, "1", 1U);
    require_completed(interpreter);

    require(probe.wave_calls == 3U
            && probe.task_ends
                == std::vector<std::vector<std::size_t>> {
                    { 2U, 3U, 4U }, { 1U, 2U }, { 1U } },
        "decline and suffix retry must expose exact remaining task ends");
    require(probe.start_instructions
                == std::vector<std::vector<InstructionIndex>> {
                    { 1U, 2U, 3U, 4U }, { 3U, 4U }, { 4U } },
        "pure wave must receive each current ordered ready span");
    require(probe.prepared_processes
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U, 2U, 3U }, { 2U, 3U }, { 3U } }
            && probe.prepare_counts
                == std::vector<std::size_t> { 1U, 1U, 1U, 1U },
        "prepared capabilities must preserve mixed task order and be reused");
    bool stable_prepared_streams
        = probe.prepared_pointers.size() == probe.prepared_processes.size();
    for (std::size_t wave = 0U;
         stable_prepared_streams && wave < probe.prepared_pointers.size();
         ++wave) {
        stable_prepared_streams
            = probe.prepared_pointers[wave].size()
                == probe.prepared_processes[wave].size();
        for (std::size_t member = 0U;
             stable_prepared_streams
                && member < probe.prepared_pointers[wave].size();
             ++member) {
            const auto process = probe.prepared_processes[wave][member];
            stable_prepared_streams
                = probe.prepared_pointers[wave][member]
                == probe.prepared_addresses[process];
        }
    }
    require(stable_prepared_streams,
        "prepared capability streams must retain stable member records");
    require(probe.decline_state_unchanged
                == std::vector<bool> { true, true },
        "a pre-execution decline must leave queued waiters untouched");
    require(probe.resume_counts
                == std::vector<std::size_t> { 2U, 2U, 1U, 2U },
        "a declined snapshot must fall back once and a completed task prefix must not replay");
    require(probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U }, { 3U, 0U },
                    { 0U, 1U }, { 1U, 2U }, { 3U, 4U } },
        "generic fallback must retain original process order and resume only the suffix");
}

void test_simir_pure_wave_incompatible_middle_falls_back()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.decline_on_start_instruction = 3U;
    Interpreter interpreter;
    std::array<SignalId, 4U> triggers { };
    constexpr std::array<std::size_t, 4U> operation_counts {
        6U, 7U, 10U, 31U
    };
    for (std::size_t index = 0U; index < triggers.size(); ++index) {
        triggers[index] = add_trigger(
            interpreter, "wave.incompatible_" + std::to_string(index));
        (void)add_waiting_process(
            interpreter, probe, triggers[index], operation_counts[index]);
        schedule_edge(interpreter, triggers[index], "1", 1U);
    }
    require_completed(interpreter);

    require(probe.wave_calls == 4U
            && probe.task_ends
                == std::vector<std::vector<std::size_t>> {
                    { 1U, 2U, 3U, 4U }, { 1U, 2U, 3U },
                    { 1U, 2U }, { 1U } }
            && probe.start_instructions
                == std::vector<std::vector<InstructionIndex>> {
                    { 1U, 2U, 3U, 4U }, { 2U, 3U, 4U },
                    { 3U, 4U }, { 4U } },
        "an admitted middle entry must decline each refreshed remaining span");
    require(probe.decline_state_unchanged
                == std::vector<bool> { true, true, true, true }
            && probe.resume_counts
                == std::vector<std::size_t> { 2U, 2U, 2U, 2U }
            && probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U }, { 3U, 0U },
                    { 0U, 1U }, { 1U, 2U }, { 2U, 3U }, { 3U, 4U } },
        "middle-entry decline must fall back without skipping or replaying members");
    require(probe.prepared_kernel_members == 0U,
        "a pre-mutation decline must not execute any prepared member");
}

void test_simir_pure_wave_unsupported_middle_keeps_task_prefix()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.unsupported_process = ProcessId { 2U };
    probe.responses = {
        { { 1U, 2U }, { 1U, 2U }, std::size_t { 2U } }
    };
    Interpreter interpreter;
    std::array<SignalId, 4U> triggers { };
    constexpr std::array<std::size_t, 4U> operation_counts {
        6U, 7U, 10U, 31U
    };
    for (std::size_t index = 0U; index < triggers.size(); ++index) {
        triggers[index] = add_trigger(
            interpreter, "wave.unsupported_" + std::to_string(index));
        (void)add_waiting_process(
            interpreter, probe, triggers[index], operation_counts[index]);
        schedule_edge(interpreter, triggers[index], "1", 1U);
    }
    require_completed(interpreter);

    require(probe.task_ends
                == std::vector<std::vector<std::size_t>> {
                    { 1U, 2U }, { 1U } }
            && probe.prepared_processes
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U }, { 3U } }
            && probe.start_instructions
                == std::vector<std::vector<InstructionIndex>> {
                    { 1U, 2U }, { 4U } }
            && probe.wave_executor_ids
                == std::vector<ProcessId> { 0U, 3U }
            && probe.prepare_counts
                == std::vector<std::size_t> { 1U, 1U, 2U, 1U }
            && probe.decline_state_unchanged
                == std::vector<bool> { true }
            && probe.prepared_kernel_members == 2U,
        "an unsupported middle task must keep its prefix and retry the next singleton");
    require(probe.resume_counts
                == std::vector<std::size_t> { 1U, 1U, 2U, 2U }
            && probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U }, { 3U, 0U },
                    { 2U, 3U }, { 3U, 4U } },
        "the unsupported suffix must use generic fallback exactly once");
}

void test_simir_pure_wave_uses_refreshed_ready_span()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.responses = {
        { { 1U, 2U }, { 3U, 3U }, std::size_t { 1U } }
    };
    probe.halt_on_first_wake = ProcessId { 0U };
    Interpreter interpreter;
    const auto changing_cohort_trigger
        = add_trigger(interpreter, "wave.changing_cohort");
    const auto late_singleton_trigger
        = add_trigger(interpreter, "wave.late_singleton");
    (void)add_waiting_process(
        interpreter, probe, changing_cohort_trigger, 10U);
    (void)add_waiting_process(
        interpreter, probe, changing_cohort_trigger, 10U);
    (void)add_waiting_process(
        interpreter, probe, late_singleton_trigger, 7U);

    schedule_edge(interpreter, changing_cohort_trigger, "1", 1U);
    schedule_edge(interpreter, changing_cohort_trigger, "0", 2U);
    schedule_edge(interpreter, late_singleton_trigger, "1", 2U);
    require_completed(interpreter);

    require(probe.wave_calls == 3U
            && probe.task_ends
                == std::vector<std::vector<std::size_t>> {
                    { 2U }, { 1U, 2U }, { 1U } }
            && probe.wave_executor_ids
                == std::vector<ProcessId> { 0U, 1U, 2U }
            && probe.start_instructions
                == std::vector<std::vector<InstructionIndex>> {
                    { 1U, 2U }, { 3U, 3U }, { 3U } },
        "the second event must snapshot its surviving cohort member and new singleton");
    require(probe.resume_counts
                == std::vector<std::size_t> { 2U, 2U, 2U }
            && probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U },
                    { 0U, 1U }, { 1U, 2U }, { 2U, 3U } },
        "a halted cohort member must not reappear in a later ready span");
}

void test_simir_pure_wave_reprepares_stale_member_context_and_generation()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.responses = {
        { { 2U }, { 1U, 2U }, std::size_t { 1U } },
        { { 2U }, { 1U, 2U }, std::size_t { 1U } },
    };
    Interpreter interpreter;
    const auto trigger = add_trigger(interpreter, "wave.reprepare");
    (void)add_waiting_process(interpreter, probe, trigger, 10U);
    (void)add_waiting_process(interpreter, probe, trigger, 10U);

    // The active callback runs before the time-2 signal update. It simulates
    // an executor invalidating one generation and one cached owner signature
    // between two otherwise identical ready spans.
    interpreter.scheduler().schedule_at(
        2U, fsim::runtime::SchedulerPhase::active, 0U,
        [&probe](fsim::runtime::Scheduler&) {
            probe.executors[0U]->invalidate_generation();
            probe.executors[1U]->invalidate_native_context();
        });
    schedule_edge(interpreter, trigger, "1", 1U);
    schedule_edge(interpreter, trigger, "0", 2U);
    require_completed(interpreter);

    require(probe.wave_calls == 2U
            && probe.prepare_counts == std::vector<std::size_t> { 2U, 2U }
            && probe.prepared_processes
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U }, { 0U, 1U } },
        "stale member generation and owner signature must be prepared again");
    require(probe.prepared_generations
                == std::vector<std::vector<std::uint64_t>> {
                    { 1U, 1U }, { 2U, 1U } }
            && probe.prepared_owner_epochs.size() == 2U
            && probe.prepared_owners.size() == 2U
            && probe.prepared_owners[0U][0U]
                == probe.prepare_owners[0U]
            && probe.prepared_owners[1U][1U]
                == probe.prepare_owners[1U]
            && probe.prepared_owner_epochs[0U][0U]
                == probe.prepare_owner_epochs[0U]
            && probe.prepared_owner_epochs[1U][1U]
                == probe.prepare_owner_epochs[1U],
        "reprepared records must carry the current native owner and epoch");
    require(probe.prepared_pointers[0U][0U]
                == probe.prepared_pointers[1U][0U]
            && probe.prepared_pointers[0U][1U]
                == probe.prepared_pointers[1U][1U]
            && probe.resume_counts
                == std::vector<std::size_t> { 1U, 1U },
        "repreparation must refresh stable executor-owned records without replay");
}

void test_simir_pure_wave_respects_hooks_and_scheduler_phases()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    Interpreter interpreter;
    const auto active_trigger = add_trigger(interpreter, "wave.active");
    const auto reactive_trigger = add_trigger(interpreter, "wave.reactive");
    (void)add_waiting_process(interpreter, probe, active_trigger, 7U);
    (void)add_waiting_process(interpreter, probe, active_trigger, 6U);
    (void)add_waiting_process(
        interpreter, probe, reactive_trigger, 10U, true);
    (void)add_waiting_process(
        interpreter, probe, reactive_trigger, 31U, true);

    std::vector<std::pair<fsim::runtime::SchedulerPhase, ProcessId>> suspend_order;
    interpreter.set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const ExecutionPoint& point) {
            if (scheduler.now() == 1U
                && point.kind == ExecutionPointKind::process_suspend) {
                suspend_order.emplace_back(
                    scheduler.current_phase().value_or(
                        fsim::runtime::SchedulerPhase::active),
                    point.process);
            }
        });
    schedule_edge(interpreter, active_trigger, "1", 1U);
    schedule_edge(interpreter, reactive_trigger, "1", 1U);
    require_completed(interpreter);

    require(suspend_order
                == std::vector<std::pair<fsim::runtime::SchedulerPhase, ProcessId>> {
                    { fsim::runtime::SchedulerPhase::active, 0U },
                    { fsim::runtime::SchedulerPhase::active, 1U },
                    { fsim::runtime::SchedulerPhase::reactive, 2U },
                    { fsim::runtime::SchedulerPhase::reactive, 3U } },
        "hooked suspension points must preserve active-before-reactive order");
    require(std::all_of(
                probe.execution_points_enabled.begin(),
                probe.execution_points_enabled.end(),
                [](const bool enabled) { return enabled; })
            && probe.resume_counts
                == std::vector<std::size_t> { 2U, 2U, 2U, 2U },
        "execution-point hooks must make each pure-wave attempt decline to ordered fallback");
}

void test_simir_pure_wave_exception_is_not_replayed()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.throw_on_wave_call = 1U;
    Interpreter interpreter;
    const auto cohort_trigger = add_trigger(interpreter, "wave.failure_cohort");
    const auto singleton_trigger = add_trigger(interpreter, "wave.failure_singleton");
    (void)add_waiting_process(interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(interpreter, probe, singleton_trigger, 7U);
    schedule_edge(interpreter, cohort_trigger, "1", 1U);
    schedule_edge(interpreter, singleton_trigger, "1", 1U);

    bool failed { };
    try {
        (void)interpreter.run();
    } catch (const std::exception& error) {
        failed = std::string_view { error.what() }
            .find("pure-wave test failure after start")
            != std::string_view::npos;
    }
    require(failed && probe.post_start_failure
            && probe.wave_calls == 1U
            && !interpreter.scheduler().running(),
        "a post-start pure-wave exception must fail and clean up the scheduler");
    require(probe.resume_counts
                == std::vector<std::size_t> { 1U, 1U, 1U }
            && probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U } },
        "a started pure wave that throws must never replay through generic resume");
}

void test_simir_pure_wave_lazy_update_flush_preserves_member_order()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.responses = {
        { { 2U, 3U }, { 1U, 2U, 3U }, std::size_t { 2U }, false }
    };
    Interpreter interpreter;
    const auto cohort_trigger = add_trigger(interpreter, "wave.flush_cohort");
    const auto singleton_trigger
        = add_trigger(interpreter, "wave.flush_singleton");
    (void)add_waiting_process(interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(interpreter, probe, singleton_trigger, 7U);
    schedule_edge(interpreter, cohort_trigger, "1", 1U);
    schedule_edge(interpreter, singleton_trigger, "1", 1U);
    require_completed(interpreter);

    require(probe.flushed_processes
                == std::vector<ProcessId> { 0U, 1U, 2U },
        "a declined batch stage must lazily flush completed members in order");
    require(probe.resume_counts
                == std::vector<std::size_t> { 1U, 1U, 1U }
            && probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U },
                },
        "lazy update flush must complete the wave without generic replay");
}

void test_simir_pure_wave_lazy_update_flush_failure_is_not_replayed()
{
    using namespace fsim::runtime::simir;

    PureWaveProbe probe;
    probe.responses = {
        { { 2U, 3U }, { 1U, 2U, 3U }, std::size_t { 2U }, false }
    };
    probe.throw_on_flush_call = 2U;
    Interpreter interpreter;
    const auto cohort_trigger
        = add_trigger(interpreter, "wave.flush_failure_cohort");
    const auto singleton_trigger
        = add_trigger(interpreter, "wave.flush_failure_singleton");
    (void)add_waiting_process(interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(interpreter, probe, cohort_trigger, 10U);
    (void)add_waiting_process(interpreter, probe, singleton_trigger, 7U);
    schedule_edge(interpreter, cohort_trigger, "1", 1U);
    schedule_edge(interpreter, singleton_trigger, "1", 1U);

    bool failed { };
    try {
        (void)interpreter.run();
    } catch (const std::exception& error) {
        failed = std::string_view { error.what() }
            .find("pure-wave test failure during update flush")
            != std::string_view::npos;
    }
    require(failed && probe.post_start_failure
            && probe.wave_calls == 1U
            && !interpreter.scheduler().running(),
        "a lazy flush failure after wave execution must fail the scheduler");
    require(probe.flushed_processes
                == std::vector<ProcessId> { 0U, 1U }
            && probe.resume_counts
                == std::vector<std::size_t> { 1U, 1U, 1U }
            && probe.resumes
                == std::vector<std::pair<ProcessId, InstructionIndex>> {
                    { 0U, 0U }, { 1U, 0U }, { 2U, 0U },
                },
        "a failed lazy flush must not replay any started wave member");
}

} // namespace fsim::tests::runtime
