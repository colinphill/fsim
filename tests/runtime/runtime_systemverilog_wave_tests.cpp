// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"
#include "../../src/runtime/simir_region_activation_reference.hpp"
#if defined(FSIM_RUNTIME_PREPARED_OUTPUT_FAILURE_TESTS)
#include "runtime_fused_staging_failure_support.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {
using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(bool value, const char* message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void discard_scheduler_trace(
    void*, const SchedulerTraceRecord&) noexcept
{
}

[[nodiscard]] PackedLogic4 invert_value(const PackedLogic4& source)
{
    PackedLogic4 result(source.width(), Logic4::x);
    if (source.is_logic9()) {
        result.fill(Logic9::u);
    }
    for (std::size_t index = 0; index < source.width(); ++index) {
        if (source.is_logic9()) {
            result.set_logic9(index, logic_not(source.get_logic9(index)));
        } else {
            result.set(index, logic_not(source.get(index)));
        }
    }
    return result;
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        if (!set(value)) {
            throw std::runtime_error("failed to set profile environment");
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
        if (had_previous_) {
            (void)set(previous_.c_str());
        } else {
            unset();
        }
    }

private:
    bool set(const char* value) const noexcept
    {
#if defined(_WIN32)
        return ::_putenv_s(name_.c_str(), value == nullptr ? "" : value) == 0;
#else
        return (value == nullptr
            ? ::unsetenv(name_.c_str())
            : ::setenv(name_.c_str(), value, 1)) == 0;
#endif
    }

    void unset() const noexcept
    {
#if defined(_WIN32)
        (void)::_putenv_s(name_.c_str(), "");
#else
        (void)::unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ { };
};

class ScopedCerrCapture final {
public:
    explicit ScopedCerrCapture(std::ostringstream& output)
        : previous_(std::cerr.rdbuf(output.rdbuf()))
    {
    }

    ScopedCerrCapture(const ScopedCerrCapture&) = delete;
    ScopedCerrCapture& operator=(const ScopedCerrCapture&) = delete;

    ~ScopedCerrCapture()
    {
        std::cerr.rdbuf(previous_);
    }

private:
    std::streambuf* previous_;
};

enum class Mode { normal, outside_writer, stop, failure, post_failure, decline, observe, coverage };
struct Probe {
    Interpreter& interpreter;
    Mode mode;
    std::string visits;
    std::vector<std::vector<ProcessId>> batches;
    bool interrupted { };
};

class OrderedExecutor final : public ProcessExecutor {
public:
    OrderedExecutor(Probe& probe, ProcessId id, SignalId input,
        SignalId output, ProcessExecutorProgramBinding access_binding)
        : probe_(probe)
        , id_(id)
        , input_(input)
        , output_(output)
        , access_binding_(std::move(access_binding))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    ProcessResumeResult resume(ProcessExecutionContext& context,
        InstructionIndex) override
    {
        if (context.current_time() != 0U) {
            probe_.visits += static_cast<char>('a' + id_);
        }
        context.write_update_in_domain(output_, context.read_signal(input_),
            SignalUpdateDomain::systemverilog_active);
        ProcessResumeResult result { 2U, 3U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    std::size_t resume_ordered_cohort(
        std::span<ProcessCohortResumeEntry> entries) override
    {
        auto& batch = probe_.batches.emplace_back();
        for (auto& entry : entries) {
            batch.push_back(static_cast<OrderedExecutor*>(entry.executor)->id_);
        }
        if (probe_.mode == Mode::decline && !probe_.interrupted) {
            probe_.interrupted = true;
            return 0U;
        }
        std::size_t completed { };
        for (auto& entry : entries) {
            *entry.queued = false;
            *entry.waiting_on_static = false;
            *entry.status = ProcessStatus::running;
            entry.result = entry.executor->resume(*entry.context, entry.start_instruction);
            *entry.waiting_on_static = true;
            *entry.status = ProcessStatus::waiting;
            ++completed;
            if (!probe_.interrupted && (probe_.mode == Mode::stop
                    || probe_.mode == Mode::failure)) {
                probe_.interrupted = true;
                if (probe_.mode == Mode::stop) {
                    probe_.interpreter.scheduler().request_stop();
                } else {
                    entry.failure = std::make_exception_ptr(
                        std::runtime_error("ordered wave injected failure"));
                }
                return completed;
            }
        }
        if (probe_.mode == Mode::post_failure && !probe_.interrupted) {
            probe_.interrupted = true;
            entries.front().failure = std::make_exception_ptr(
                std::runtime_error("ordered wave injected failure"));
        }
        return completed;
    }

    bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

private:
    Probe& probe_;
    ProcessId id_;
    SignalId input_;
    SignalId output_;
    ProcessExecutorProgramBinding access_binding_;
};

struct RegionKernelProbe {
    std::array<std::size_t, 4U> resumes { };
    std::array<std::size_t, 4U> initial_starts { };
    std::array<std::size_t, 4U> wait_starts { };
    std::array<std::size_t, 4U> pc_mismatches { };
    std::size_t completion_prepares { };
    std::size_t completion_stages { };
    std::size_t completion_commits { };
    std::size_t completion_cancels { };
    std::vector<std::string>* route_events { };
    bool route_process_ids { };
    std::function<void()> after_root_sample;
    std::function<void(ProcessId)> after_secondary_sample;
    std::function<void(ProcessId, const PackedLogic4&)> after_process_sample;
    std::vector<std::pair<ProcessId, PackedLogic4>>* sampled_input_values { };
};

struct RegionObservationTrace {
    Interpreter* interpreter { };
    SignalId signal { };
    PackedLogic4 observed_value;
    bool observed { };
    bool failed { };

    static void receive(void* context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& state = *static_cast<RegionObservationTrace*>(context);
        if (state.observed || record.kind != SchedulerTraceKind::batch_begin
            || record.phase != SchedulerPhase::active
            || record.order != 1U || record.count != 2U) {
            return;
        }
        try {
            state.observed_value = state.interpreter->signal_value(state.signal);
            state.observed = true;
        } catch (...) {
            state.failed = true;
        }
    }
};

class RegionKernelExecutor final : public ProcessExecutor {
public:
    RegionKernelExecutor(RegionKernelProbe& probe, const ProcessId id,
        const SignalId input, const SignalId output,
        const InstructionIndex wait_instruction,
        ProcessExecutorProgramBinding access_binding,
        const bool equivalent, const std::size_t probe_slot,
        const bool supports_native_completion = false,
        const bool no_persistent_registers = false,
        const std::size_t expected_register_count = 1U,
        const std::optional<SignalId> second_output = std::nullopt,
        const std::optional<SignalId> secondary_input = std::nullopt,
        const bool all_registers_defined = false,
        const std::optional<bool> invert_result = std::nullopt,
        const bool xor_secondary = false,
        std::vector<std::pair<std::uint8_t, std::uint8_t>>*
            sampled_secondary_inputs = nullptr,
        const std::size_t expected_register_width = 1U,
        const std::optional<SignalId> tertiary_input = std::nullopt,
        const bool xor_tertiary = false,
        const std::optional<std::uint32_t> output_slice_offset = std::nullopt)
        : probe_(probe)
        , id_(id)
        , input_(input)
        , output_(output)
        , wait_instruction_(wait_instruction)
        , access_binding_(std::move(access_binding))
        , equivalent_(equivalent)
        , probe_slot_(probe_slot)
        , supports_native_completion_(supports_native_completion)
        , no_persistent_registers_(no_persistent_registers)
        , expected_register_count_(expected_register_count)
        , second_output_(second_output)
        , secondary_input_(secondary_input)
        , all_registers_defined_(all_registers_defined)
        , invert_result_(invert_result)
        , xor_secondary_(xor_secondary)
        , sampled_secondary_inputs_(sampled_secondary_inputs)
        , expected_register_width_(expected_register_width)
        , tertiary_input_(tertiary_input)
        , xor_tertiary_(xor_tertiary)
        , output_slice_offset_(output_slice_offset)
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return equivalent_;
    }

    [[nodiscard]] bool
    region_kernel_completion_has_no_persistent_registers() const noexcept
        override
    {
        return no_persistent_registers_;
    }

    class Completion final : public PreparedRegionCompletion {
    public:
        explicit Completion(const void* identity) noexcept
            : identity_(identity)
        {
        }

        [[nodiscard]] const void* storage_identity() const noexcept override
        {
            return identity_;
        }

        void commit() noexcept override { }

    private:
        const void* identity_ { };
    };

    [[nodiscard]] std::unique_ptr<PreparedRegionCompletion>
    prepare_region_completion(
        const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        std::span<const RegionRegisterBinding>,
        std::span<const PackedLogic4>) override
    {
        if (!equivalent_ || process != id_
            || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U) {
            return { };
        }
        return std::make_unique<Completion>(this);
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count,
        const void** const storage_identity) noexcept override
    {
        ++probe_.completion_prepares;
        if (!supports_native_completion_ || !equivalent_
            || native_completion_prepared_ || storage_identity == nullptr
            || process != id_ || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U
            || register_bindings.size() != expected_register_count_) {
            return false;
        }
        for (std::size_t index = 0U; index < register_bindings.size(); ++index) {
            const auto& binding = register_bindings[index];
            if (binding.source_register != index
                || binding.activation_register >= activation_register_count
                || binding.value_kind != ValueKind::logic4
                || (all_registers_defined_
                    ? !binding.defined
                        || binding.width != expected_register_width_
                    : (index == 0U
                        ? !binding.defined || binding.width != 1U
                        : binding.defined || binding.width != 0U))) {
                return false;
            }
        }
        native_activation_register_count_ = activation_register_count;
        native_completion_prepared_ = true;
        *storage_identity = this;
        return true;
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> activation_registers)
        noexcept override
    {
        ++probe_.completion_stages;
        return native_completion_prepared_
            && activation_registers.size()
                == native_activation_register_count_;
    }

    void commit_region_completion_native() noexcept override
    {
        ++probe_.completion_commits;
        native_completion_prepared_ = false;
    }

    void cancel_region_completion_native() noexcept override
    {
        if (native_completion_prepared_) {
            ++probe_.completion_cancels;
        }
        native_completion_prepared_ = false;
    }

    void redirect(InstructionIndex) override
    {
        // This fixture keeps no executor-owned program counter. The resume
        // argument below checks the interpreter's redirected start PC.
    }

    ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        if (probe_.route_events) {
            probe_.route_events->push_back("member"
                + std::to_string(probe_.route_process_ids
                        ? id_ : probe_slot_));
        }
        ++probe_.resumes[probe_slot_];
        if (start_instruction == 0U) {
            ++probe_.initial_starts[probe_slot_];
        } else if (start_instruction == wait_instruction_ + 1U) {
            ++probe_.wait_starts[probe_slot_];
        } else {
            ++probe_.pc_mismatches[probe_slot_];
        }
        auto value = context.read_signal(input_);
        if (probe_.sampled_input_values != nullptr) {
            probe_.sampled_input_values->emplace_back(id_, value);
        }
        if (probe_.after_process_sample) {
            probe_.after_process_sample(id_, value);
        }
        if (id_ == 1U && probe_.after_root_sample) {
            auto after_root_sample = std::move(probe_.after_root_sample);
            after_root_sample();
        }
        if (secondary_input_) {
            const auto secondary = context.read_signal(*secondary_input_);
            if (sampled_secondary_inputs_ != nullptr) {
                const auto primary_word = value.unchecked_low_word();
                const auto secondary_word = secondary.unchecked_low_word();
                sampled_secondary_inputs_->emplace_back(
                    static_cast<std::uint8_t>(primary_word.aval & 1U),
                    static_cast<std::uint8_t>(secondary_word.aval & 1U));
            }
            if (probe_.after_secondary_sample) {
                probe_.after_secondary_sample(id_);
            }
            if (xor_secondary_) {
                value = binary_value(
                    BinaryOperator::bit_xor, value, secondary);
            } else {
                const auto zero = PackedLogic4(1U, Logic4::zero);
                const auto masked = binary_value(
                    BinaryOperator::bit_and, secondary, zero);
                value = binary_value(
                    BinaryOperator::bit_or, value, masked);
            }
        }
        if (tertiary_input_) {
            const auto tertiary = context.read_signal(*tertiary_input_);
            if (xor_tertiary_) {
                value = binary_value(
                    BinaryOperator::bit_xor, value, tertiary);
            }
        }
        if (invert_result_.value_or(probe_slot_ == 1U)) {
            value = invert_value(value);
        }
        if (second_output_) {
            auto second_value = value;
            context.write_update_in_domain(output_, std::move(value),
                SignalUpdateDomain::systemverilog_active);
            context.write_update_in_domain(*second_output_,
                std::move(second_value),
                SignalUpdateDomain::systemverilog_active);
        } else {
            if (output_slice_offset_) {
                context.write_update_slice_in_domain(output_,
                    std::move(value), *output_slice_offset_,
                    SignalUpdateDomain::systemverilog_active);
            } else {
                context.write_update_in_domain(output_, std::move(value),
                    SignalUpdateDomain::systemverilog_active);
            }
        }
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind
            = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    std::size_t resume_ordered_cohort(
        std::span<ProcessCohortResumeEntry> entries) override
    {
        std::size_t completed { };
        for (auto& entry : entries) {
            *entry.queued = false;
            *entry.waiting_on_static = false;
            *entry.status = ProcessStatus::running;
            entry.result = entry.executor->resume(
                *entry.context, entry.start_instruction);
            ++completed;
        }
        return completed;
    }

    bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

private:
    RegionKernelProbe& probe_;
    ProcessId id_;
    SignalId input_;
    SignalId output_;
    InstructionIndex wait_instruction_;
    ProcessExecutorProgramBinding access_binding_;
    bool equivalent_;
    std::size_t probe_slot_;
    bool supports_native_completion_ { };
    bool no_persistent_registers_ { };
    std::size_t expected_register_count_ { 1U };
    std::optional<SignalId> second_output_;
    std::optional<SignalId> secondary_input_;
    bool all_registers_defined_ { };
    std::optional<bool> invert_result_;
    bool xor_secondary_ { };
    std::vector<std::pair<std::uint8_t, std::uint8_t>>*
        sampled_secondary_inputs_ { };
    std::size_t expected_register_width_ { 1U };
    std::optional<SignalId> tertiary_input_;
    bool xor_tertiary_ { };
    std::optional<std::uint32_t> output_slice_offset_;
    bool native_completion_prepared_ { };
    std::size_t native_activation_register_count_ { };
};

enum class PreparedInterruptionExecutorRole : std::uint8_t {
    first_writer,
    reader,
    second_writer,
};

class PreparedInterruptionExecutor final : public ProcessExecutor {
public:
    PreparedInterruptionExecutor(const ProcessId process,
        const PreparedInterruptionExecutorRole role,
        const SignalId first_input,
        const SignalId second_input,
        const SignalId output,
        const InstructionIndex wait_instruction,
        const std::size_t register_count,
        ProcessExecutorProgramBinding access_binding,
        const void* cohort_domain,
        std::vector<PackedLogic4>* reader_observed_values)
        : process_(process)
        , role_(role)
        , first_input_(first_input)
        , second_input_(second_input)
        , output_(output)
        , wait_instruction_(wait_instruction)
        , register_count_(register_count)
        , access_binding_(std::move(access_binding))
        , cohort_domain_(cohort_domain)
        , reader_observed_values_(reader_observed_values)
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &access_binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return true;
    }

    [[nodiscard]] std::unique_ptr<PreparedRegionCompletion>
    prepare_region_completion(const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        std::span<const RegionRegisterBinding>,
        std::span<const PackedLogic4>) override
    {
        if (process != process_ || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U) {
            return { };
        }
        return std::make_unique<RegionKernelExecutor::Completion>(this);
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count,
        const void** const storage_identity) noexcept override
    {
        if (process != process_ || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U
            || register_bindings.size() != register_count_
            || activation_register_count < register_count_
            || storage_identity == nullptr
            || native_completion_prepared_) {
            return false;
        }
        std::array<bool, 4U> sources { };
        if (register_count_ > sources.size()) {
            return false;
        }
        for (std::size_t index = 0U;
             index < register_bindings.size(); ++index) {
            const auto& binding = register_bindings[index];
            if (!binding.defined || binding.width != 1U
                || binding.value_kind != ValueKind::logic4
                || binding.source_register >= register_count_
                || binding.activation_register >= activation_register_count
                || binding.source_register >= sources.size()
                || sources[binding.source_register]) {
                return false;
            }
            for (std::size_t previous = 0U; previous < index; ++previous) {
                if (register_bindings[previous].activation_register
                    == binding.activation_register) {
                    return false;
                }
            }
            sources[binding.source_register] = true;
        }
        for (std::size_t source = 0U; source < register_count_; ++source) {
            if (!sources[source]) {
                return false;
            }
        }
        native_activation_register_count_ = activation_register_count;
        native_completion_prepared_ = true;
        *storage_identity = this;
        return true;
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> activation_registers)
        noexcept override
    {
        return native_completion_prepared_
            && activation_registers.size()
                == native_activation_register_count_;
    }

    void commit_region_completion_native() noexcept override
    {
        native_completion_prepared_ = false;
    }

    void cancel_region_completion_native() noexcept override
    {
        native_completion_prepared_ = false;
    }

    ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex) override
    {
        switch (role_) {
        case PreparedInterruptionExecutorRole::first_writer: {
            auto value = context.read_signal(first_input_);
            context.write_update_in_domain(output_, std::move(value),
                SignalUpdateDomain::systemverilog_active);
            break;
        }
        case PreparedInterruptionExecutorRole::reader:
            static_cast<void>(context.read_signal(first_input_));
            if (reader_observed_values_ != nullptr) {
                reader_observed_values_->push_back(
                    context.read_signal(second_input_));
            } else {
                static_cast<void>(context.read_signal(second_input_));
            }
            break;
        case PreparedInterruptionExecutorRole::second_writer:
            static_cast<void>(context.read_signal(first_input_));
            context.write_update_in_domain(output_,
                context.read_signal(second_input_),
                SignalUpdateDomain::systemverilog_active);
            break;
        }
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    std::size_t resume_ordered_cohort(
        const std::span<ProcessCohortResumeEntry> entries) override
    {
        std::size_t completed { };
        for (auto& entry : entries) {
            *entry.queued = false;
            *entry.waiting_on_static = false;
            *entry.status = ProcessStatus::running;
            try {
                entry.result = entry.executor->resume(
                    *entry.context, entry.start_instruction);
            } catch (...) {
                entry.failure = std::current_exception();
                return completed + 1U;
            }
            ++completed;
        }
        return completed;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return cohort_domain_;
    }

private:
    ProcessId process_ { };
    PreparedInterruptionExecutorRole role_ { };
    SignalId first_input_ { };
    SignalId second_input_ { };
    SignalId output_ { };
    InstructionIndex wait_instruction_ { };
    std::size_t register_count_ { };
    ProcessExecutorProgramBinding access_binding_;
    const void* cohort_domain_ { };
    std::vector<PackedLogic4>* reader_observed_values_ { };
    bool native_completion_prepared_ { };
    std::size_t native_activation_register_count_ { };
};

struct ExpectedLogic4Input {
    SignalId signal { };
    RegisterId register_id { };
    std::uint32_t width { };
};

struct RegionBackendPrefixWitness {
    std::uint64_t frontier_generation { };
    std::size_t frontier_cursor { };
    std::size_t frontier_end { };
    SimulationTick time { };
    std::uint64_t delta { };
    SchedulerPhase phase { SchedulerPhase::active };
    std::uint64_t systemverilog_round { };
    ProcessSchedulingDomain process_domain {
        ProcessSchedulingDomain::systemverilog
    };
    std::array<RegionKernelSchedulerPrefixTask, 64U> tasks { };
    std::size_t task_count { };

    [[nodiscard]] bool capture(
        const RegionKernelSchedulerPrefix& prefix) noexcept
    {
        if (prefix.tasks.empty() || prefix.tasks.size() > tasks.size()) {
            return false;
        }
        frontier_generation = prefix.frontier_generation;
        frontier_cursor = prefix.frontier_cursor;
        frontier_end = prefix.frontier_end;
        time = prefix.time;
        delta = prefix.delta;
        phase = prefix.phase;
        systemverilog_round = prefix.systemverilog_round;
        process_domain = prefix.process_domain;
        task_count = prefix.tasks.size();
        std::ranges::copy(prefix.tasks, tasks.begin());
        return true;
    }

    [[nodiscard]] bool matches(
        const RegionKernelSchedulerPrefix& prefix) const noexcept
    {
        // The scheduler may mint a new frontier generation on retry. The
        // offered task identities and complete source keys remain stable.
        return task_count == prefix.tasks.size()
            && time == prefix.time
            && delta == prefix.delta
            && phase == prefix.phase
            && systemverilog_round == prefix.systemverilog_round
            && process_domain == prefix.process_domain
            && std::ranges::equal(
                std::span<const RegionKernelSchedulerPrefixTask> {
                    tasks.data(), task_count },
                prefix.tasks);
    }
};

struct RegionBackendProbe {
    enum class FailurePoint : std::uint8_t {
        none,
        generalized_input_planes,
        legacy_input_planes,
    };

    std::vector<SignalId> internal_signals;
    std::size_t factories { };
    std::size_t attempts { };
    std::size_t plane_input_dispatches { };
    std::size_t legacy_input_dispatches { };
    std::size_t failure_channel_polls { };
    RegionBackendPrefixWitness failed_prefix;
    RegionBackendPrefixWitness retried_prefix;
    bool plane_input_views_valid { true };
    bool legacy_input_views_valid { true };
    bool readiness_views_valid { true };
    bool failed_prefix_captured { };
    bool retried_prefix_captured { };
    bool retry_prefix_matches_failed_prefix { };
    FailurePoint failure_point { FailurePoint::none };
    std::exception_ptr armed_failure;
    std::exception_ptr pending_failure;
};

class DecliningRegionBackend final
    : public RegionKernelBackend
    , public RegionKernelInputPlaneBackend
    , public RegionKernelLogic4InputBackend
    , public RegionKernelFailureBackend {
public:
    DecliningRegionBackend(RegionBackendProbe& probe,
        std::vector<ExpectedLogic4Input> expected_logic4_inputs,
        std::vector<std::pair<ProcessId, RegisterId>> readiness_registers)
        : probe_(probe)
        , expected_logic4_inputs_(std::move(expected_logic4_inputs))
        , readiness_registers_(std::move(readiness_registers))
    {
    }

    [[nodiscard]] bool execute(
        const RegionKernelActivationImage&) noexcept override
    {
        ++probe_.attempts;
        return false;
    }

    [[nodiscard]] bool execute_with_input_planes(
        const RegionKernelActivationImage& image,
        const std::span<const RegionKernelInputPlane> planes)
        noexcept override
    {
        ++probe_.plane_input_dispatches;
        if (probe_.failed_prefix_captured
            && !probe_.retried_prefix_captured) {
            probe_.retried_prefix_captured
                = probe_.retried_prefix.capture(image.scheduler_prefix);
            probe_.retry_prefix_matches_failed_prefix
                = probe_.retried_prefix_captured
                && probe_.failed_prefix.matches(image.scheduler_prefix);
        }
        bool valid = planes.size() == expected_logic4_inputs_.size()
            && !planes.empty();
        for (std::size_t index = 0U;
             valid && index < expected_logic4_inputs_.size(); ++index) {
            const auto& expected = expected_logic4_inputs_[index];
            const auto& plane = planes[index];
            const auto input = std::ranges::find(image.register_inputs,
                expected.register_id,
                &RegionKernelRegisterInput::register_id);
            valid = plane.signal == expected.signal
                && plane.register_id == expected.register_id
                && plane.width == expected.width
                && plane.value_kind == ValueKind::logic4
                && plane.planes[0U].size() == expected.width / 64U
                    + (expected.width % 64U == 0U ? 0U : 1U)
                && plane.planes[1U].size() == plane.planes[0U].size()
                && plane.planes[2U].empty()
                && plane.planes[3U].empty()
                && input != image.register_inputs.end()
                && input->value.width() == expected.width
                && !input->value.is_logic9()
                && std::ranges::equal(plane.planes[0U],
                    input->value.aval_words())
                && std::ranges::equal(plane.planes[1U],
                    input->value.bval_words());
        }
        probe_.plane_input_views_valid
            = probe_.plane_input_views_valid && valid;
        for (const auto& [process, register_id] : readiness_registers_) {
            const auto readiness = std::ranges::find(image.register_inputs,
                register_id, &RegionKernelRegisterInput::register_id);
            const auto active = std::ranges::find(image.ready_processes,
                process) != image.ready_processes.end();
            probe_.readiness_views_valid
                = probe_.readiness_views_valid
                && readiness != image.register_inputs.end()
                && readiness->value.width() == 1U
                && !readiness->value.is_logic9()
                && readiness->value.get(0U)
                    == (active ? Logic4::one : Logic4::zero);
        }
        if (valid && probe_.failure_point
                == RegionBackendProbe::FailurePoint::generalized_input_planes) {
            probe_.failure_point = RegionBackendProbe::FailurePoint::none;
            probe_.failed_prefix_captured
                = probe_.failed_prefix.capture(image.scheduler_prefix);
            probe_.pending_failure = std::move(probe_.armed_failure);
        }
        return false;
    }

    [[nodiscard]] bool execute_with_logic4_input_planes(
        const RegionKernelActivationImage& image,
        const std::span<const RegionKernelLogic4InputPlane> planes)
        noexcept override
    {
        ++probe_.legacy_input_dispatches;
        bool valid = planes.size() == expected_logic4_inputs_.size()
            && !planes.empty();
        for (std::size_t index = 0U;
             valid && index < expected_logic4_inputs_.size(); ++index) {
            const auto& expected = expected_logic4_inputs_[index];
            const auto& plane = planes[index];
            const auto input = std::ranges::find(image.register_inputs,
                expected.register_id,
                &RegionKernelRegisterInput::register_id);
            valid = plane.signal == expected.signal
                && plane.register_id == expected.register_id
                && plane.width == expected.width
                && plane.aval.size() == 1U
                && plane.bval.size() == 1U
                && input != image.register_inputs.end()
                && input->value.width() == expected.width
                && !input->value.is_logic9()
                && std::ranges::equal(plane.aval,
                    input->value.aval_words())
                && std::ranges::equal(plane.bval,
                    input->value.bval_words());
        }
        probe_.legacy_input_views_valid
            = probe_.legacy_input_views_valid && valid;
        if (valid && probe_.failure_point
                == RegionBackendProbe::FailurePoint::legacy_input_planes) {
            probe_.failure_point = RegionBackendProbe::FailurePoint::none;
            probe_.failed_prefix_captured
                = probe_.failed_prefix.capture(image.scheduler_prefix);
            probe_.pending_failure = std::move(probe_.armed_failure);
        }
        return false;
    }

    [[nodiscard]] std::exception_ptr take_failure() noexcept override
    {
        ++probe_.failure_channel_polls;
        return std::exchange(probe_.pending_failure,
            std::exception_ptr { });
    }

    [[nodiscard]] std::span<const PackedLogic4>
    activation_registers() const noexcept override
    {
        return { };
    }

private:
    RegionBackendProbe& probe_;
    std::vector<ExpectedLogic4Input> expected_logic4_inputs_;
    std::vector<std::pair<ProcessId, RegisterId>> readiness_registers_;
};

class RecordingRegionBackendProvider final
    : public RegionKernelBackendProvider {
public:
    explicit RecordingRegionBackendProvider(
        RegionBackendProbe& probe,
        const bool throw_on_create = false) noexcept
        : probe_(probe)
        , throw_on_create_(throw_on_create)
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return "runtime-region-backend-test-v1";
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        ++probe_.factories;
        probe_.internal_signals = kernel.internal_signals;
        if (throw_on_create_) {
            throw std::runtime_error {
                "test-only optional region backend construction failure"
            };
        }
        std::vector<ExpectedLogic4Input> expected_logic4_inputs;
        std::vector<std::pair<ProcessId, RegisterId>> readiness_registers;
        for (const auto& input : kernel.inputs) {
            if (input.internal && input.value_kind == ValueKind::logic4) {
                expected_logic4_inputs.push_back({ input.signal,
                    input.value_register, input.width });
            }
        }
        for (const auto& member : kernel.members) {
            readiness_registers.push_back({ member.process,
                member.readiness_register });
        }
        std::ranges::sort(expected_logic4_inputs, std::ranges::less { },
            &ExpectedLogic4Input::signal);
        return std::make_unique<DecliningRegionBackend>(probe_,
            std::move(expected_logic4_inputs),
            std::move(readiness_registers));
    }

private:
    RegionBackendProbe& probe_;
    bool throw_on_create_ { };
};

struct LocalWaveBackendProbe {
    enum class FailurePoint : std::uint8_t {
        none,
        prepared_output,
        direct_ready,
    };

    std::vector<SignalId> internal_signals;
    std::size_t factories { };
    std::size_t executions { };
    std::size_t plane_dispatches { };
    std::size_t internal_register_rewrites { };
    bool decline_first_activation { };
    bool first_activation_declined { };
    bool decline_next_activation { };
    bool on_demand_activation_declined { };
    bool checked_fallback_decline_pending { };
    bool all_internal_reads_rewritten { true };
    bool plane_views_valid { true };
    bool execute_prepared_output_prefix { };
    bool execute_prepared_output_successor_masks { };
    bool execute_direct_ready_window { };
    bool execute_direct_ready_window_successor_masks { };
    bool decline_first_direct_ready_window { };
    bool direct_ready_window_declined { };
    bool direct_ready_fallback_pending { };
    bool direct_ready_fallback_image_matches { };
    std::size_t direct_ready_prepared_calls_at_decline { };
    std::size_t direct_ready_slots_at_decline { };
    std::size_t direct_ready_window_calls { };
    std::size_t direct_ready_window_completions { };
    std::size_t direct_ready_fallback_comparisons { };
    std::size_t direct_ready_signal_input_count_at_decline { };
    std::vector<RegionKernelRegisterInput> direct_ready_input_snapshot;
    FailurePoint failure_point { FailurePoint::none };
    std::exception_ptr armed_failure;
    std::exception_ptr pending_failure;
    std::size_t failure_channel_polls { };
    RegionBackendPrefixWitness failed_backend_prefix;
    RegionBackendPrefixWitness retried_backend_prefix;
    bool failed_backend_prefix_captured { };
    bool retried_backend_prefix_captured { };
    bool retry_backend_prefix_matches_failure { };
    std::size_t prepared_output_calls { };
    std::size_t prepared_output_slots_written { };
    std::size_t successor_mask_calls { };
    std::array<std::uint64_t, 8U> last_successor_masks { };
    std::array<std::uint8_t, 8U> last_successor_changed { };
    std::size_t last_successor_mask_count { };
    std::vector<SignalId> prepared_output_signals;
    std::function<void()> after_prepared_output_execution;
    std::vector<PackedLogic4> internal_input_values;
    std::vector<PackedLogic4> boundary_input_values;
    std::optional<SignalId> reader_observed_signal;
    ProcessId reader_process { };
    std::vector<PackedLogic4> reader_observed_values;
};

class AcceptingRegionBackend
    : public RegionKernelBackend
    , public RegionKernelInputPlaneBackend
    , public RegionKernelFailureBackend {
public:
    AcceptingRegionBackend(
        LocalWaveBackendProbe& probe,
        const RegionConeActivationKernel& kernel)
        : probe_(probe)
        , kernel_(kernel)
    {
    }

    [[nodiscard]] bool execute(
        const RegionKernelActivationImage& image) noexcept override
    {
        if (probe_.checked_fallback_decline_pending) {
            probe_.checked_fallback_decline_pending = false;
            probe_.on_demand_activation_declined = true;
            return false;
        }
        if (probe_.direct_ready_fallback_pending) {
            bool matches = image.register_inputs.size()
                == probe_.direct_ready_input_snapshot.size();
            for (const auto& expected : probe_.direct_ready_input_snapshot) {
                const auto actual = std::ranges::find(image.register_inputs,
                    expected.register_id,
                    &RegionKernelRegisterInput::register_id);
                matches = matches
                    && actual != image.register_inputs.end()
                    && actual->value == expected.value;
            }
            probe_.direct_ready_fallback_pending = false;
            probe_.direct_ready_fallback_image_matches = matches;
            ++probe_.direct_ready_fallback_comparisons;
            if (!matches) {
                return false;
            }
        }
        if (decline_current_activation_) {
            decline_current_activation_ = false;
            return false;
        }
        ++probe_.executions;
        try {
            registers_ = evaluate_region_activation_kernel_reference(
                kernel_, image);
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::exception_ptr take_failure() noexcept override
    {
        ++probe_.failure_channel_polls;
        return std::exchange(probe_.pending_failure, std::exception_ptr { });
    }

    [[nodiscard]] bool execute_with_input_planes(
        const RegionKernelActivationImage& image,
        const std::span<const RegionKernelInputPlane> planes)
        noexcept override
    {
        ++probe_.plane_dispatches;
        bool valid = true;
        std::size_t expected_count { };
        for (const auto& input : kernel_.inputs) {
            if (!input.internal) {
                const auto image_input = std::ranges::find(image.register_inputs,
                    input.value_register,
                    &RegionKernelRegisterInput::register_id);
                if (image_input == image.register_inputs.end()) {
                    valid = false;
                    continue;
                }
                try {
                    probe_.boundary_input_values.push_back(image_input->value);
                } catch (...) {
                    valid = false;
                }
                continue;
            }
            ++expected_count;
            const auto plane = std::ranges::find(planes, input.signal,
                &RegionKernelInputPlane::signal);
            const auto image_input = std::ranges::find(image.register_inputs,
                input.value_register,
                &RegionKernelRegisterInput::register_id);
            if (plane == planes.end() || image_input == image.register_inputs.end()
                || plane->register_id != input.value_register
                || plane->width != input.width
                || plane->value_kind != input.value_kind
                || plane->planes[0U].size() != 1U
                || plane->planes[1U].size() != 1U
                || !plane->planes[2U].empty()
                || !plane->planes[3U].empty()
                || image_input->value.width() != input.width
                || image_input->value.is_logic9()
                || plane->planes[0U].front()
                    != image_input->value.unchecked_low_word().aval
                || plane->planes[1U].front()
                    != image_input->value.unchecked_low_word().bval) {
                valid = false;
                continue;
            }
            try {
                probe_.internal_input_values.push_back(image_input->value);
            } catch (...) {
                valid = false;
            }
        }
        valid = valid && expected_count != 0U
            && planes.size() == expected_count;
        probe_.plane_views_valid = probe_.plane_views_valid && valid;
        if (valid && probe_.decline_next_activation) {
            probe_.decline_next_activation = false;
            probe_.checked_fallback_decline_pending = true;
            return false;
        }
        if (valid && probe_.decline_first_activation
            && !probe_.first_activation_declined) {
            probe_.first_activation_declined = true;
            decline_current_activation_ = true;
            return false;
        }
        return valid && execute(image);
    }

    [[nodiscard]] bool execute_prepared_output_prefix(
        const RegionKernelActivationImage& image,
        const std::span<const PackedLogic4> current_internal_values,
        const std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs) noexcept
    {
        if (!probe_.execute_prepared_output_prefix) {
            return false;
        }
        ++probe_.prepared_output_calls;
        if (record_backend_failure_or_retry(
                image, LocalWaveBackendProbe::FailurePoint::prepared_output)) {
            return false;
        }
        if (outputs.abi_version
                != kRegionPreparedOutputBatchAbiVersionV1
            || outputs.struct_size != sizeof(RegionPreparedOutputBatchV1)
            || outputs.slots == nullptr
            || outputs.slot_count != kernel_.internal_signals.size()
            || current_internal_values.size()
                != kernel_.internal_signals.size()
            || ordered_prefix.empty()
            || !execute(image)) {
            return false;
        }

        const std::span<const RegionPreparedOutputSlotV1> slots {
            outputs.slots,
            static_cast<std::size_t>(outputs.slot_count) };
        for (const auto& binding : ordered_prefix) {
            if (binding.value_register >= registers_.size()) {
                return false;
            }
            const auto descriptor = std::ranges::find_if(
                slots,
                [&binding](const RegionPreparedOutputSlotV1& slot) {
                    return slot.signal_id == binding.signal
                        && slot.owner_id == binding.owner
                        && slot.selected == 1U;
                });
            if (descriptor == slots.end()
                || descriptor->struct_size
                    != sizeof(RegionPreparedOutputSlotV1)
                || descriptor->value_kind
                    != RegionPreparedOutputValueKindV1::logic4
                || descriptor->width != binding.width
                || descriptor->word_count != 1U
                || descriptor->owner_mask == nullptr
                || descriptor->old_current_aval == nullptr
                || descriptor->old_current_bval == nullptr
                || descriptor->old_owner_aval == nullptr
                || descriptor->old_owner_bval == nullptr
                || descriptor->next_current_aval == nullptr
                || descriptor->next_current_bval == nullptr
                || descriptor->next_last_aval == nullptr
                || descriptor->next_last_bval == nullptr
                || descriptor->next_stored_aval == nullptr
                || descriptor->next_stored_bval == nullptr
                || descriptor->next_owner_aval == nullptr
                || descriptor->next_owner_bval == nullptr
                || descriptor->changed == nullptr
                || descriptor->value_ready == nullptr
                || descriptor->transaction_ready == nullptr) {
                return false;
            }
        }

        try {
            for (const auto& input : kernel_.inputs) {
                const auto image_input = std::ranges::find(
                    image.register_inputs, input.value_register,
                    &RegionKernelRegisterInput::register_id);
                if (image_input == image.register_inputs.end()) {
                    return false;
                }
                if (input.internal) {
                    const auto internal = std::ranges::find(
                        kernel_.internal_signals, input.signal);
                    if (internal == kernel_.internal_signals.end()) {
                        return false;
                    }
                    const auto internal_index = static_cast<std::size_t>(
                        internal - kernel_.internal_signals.begin());
                    if (internal_index >= current_internal_values.size()
                        || current_internal_values[internal_index]
                            != image_input->value) {
                        return false;
                    }
                    probe_.internal_input_values.push_back(
                        image_input->value);
                    if (probe_.reader_observed_signal == input.signal
                        && std::ranges::find(image.ready_processes,
                               probe_.reader_process)
                            != image.ready_processes.end()) {
                        probe_.reader_observed_values.push_back(
                            image_input->value);
                    }
                } else {
                    probe_.boundary_input_values.push_back(
                        image_input->value);
                }
            }
            for (const auto& binding : ordered_prefix) {
                const auto descriptor = std::ranges::find_if(
                    slots,
                    [&binding](const RegionPreparedOutputSlotV1& slot) {
                        return slot.signal_id == binding.signal
                            && slot.owner_id == binding.owner
                            && slot.selected == 1U;
                    });
                const auto& value = registers_[binding.value_register];
                if (value.width() != binding.width || value.is_logic9()) {
                    return false;
                }
                const auto word = value.unchecked_low_word();
                const bool changed
                    = *descriptor->old_current_aval != word.aval
                    || *descriptor->old_current_bval != word.bval;
                *descriptor->next_current_aval = word.aval;
                *descriptor->next_current_bval = word.bval;
                if (changed) {
                    *descriptor->next_last_aval
                        = *descriptor->old_current_aval;
                    *descriptor->next_last_bval
                        = *descriptor->old_current_bval;
                }
                *descriptor->next_stored_aval = word.aval;
                *descriptor->next_stored_bval = word.bval;
                *descriptor->next_owner_aval = word.aval;
                *descriptor->next_owner_bval = word.bval;
                *descriptor->changed = changed ? 1U : 0U;
                *descriptor->value_ready = changed ? 1U : 0U;
                *descriptor->transaction_ready = 1U;
                probe_.prepared_output_signals.push_back(binding.signal);
                ++probe_.prepared_output_slots_written;
            }
            auto after_execution
                = std::move(probe_.after_prepared_output_execution);
            if (after_execution) {
                after_execution();
            }
        } catch (...) {
            return false;
        }
        return true;
    }

    [[nodiscard]] std::span<const PackedLogic4>
    activation_registers() const noexcept override
    {
        return registers_;
    }

protected:
    LocalWaveBackendProbe& probe_;
    RegionConeActivationKernel kernel_;
    std::vector<PackedLogic4> registers_;

protected:
    [[nodiscard]] bool record_backend_failure_or_retry(
        const RegionKernelActivationImage& image,
        const LocalWaveBackendProbe::FailurePoint point) noexcept
    {
        if (probe_.failure_point == point) {
            probe_.failure_point = LocalWaveBackendProbe::FailurePoint::none;
            probe_.failed_backend_prefix_captured
                = probe_.failed_backend_prefix.capture(
                    image.scheduler_prefix);
            probe_.pending_failure = std::move(probe_.armed_failure);
            return true;
        }
        if (probe_.failed_backend_prefix_captured
            && !probe_.retried_backend_prefix_captured) {
            probe_.retried_backend_prefix_captured
                = probe_.retried_backend_prefix.capture(
                    image.scheduler_prefix);
            probe_.retry_backend_prefix_matches_failure
                = probe_.retried_backend_prefix_captured
                && probe_.failed_backend_prefix.matches(
                    image.scheduler_prefix);
        }
        return false;
    }

private:
    bool decline_current_activation_ { };
};

class PreparedAcceptingRegionBackend
    : public AcceptingRegionBackend
    , public RegionKernelDirectReadyWindowBackend {
public:
    using AcceptingRegionBackend::AcceptingRegionBackend;

    [[nodiscard]] bool supports_direct_ready_window() const noexcept override
    {
        return probe_.execute_direct_ready_window;
    }

    [[nodiscard]] bool execute_direct_ready_window_prepared(
        const RegionKernelActivationImage& image,
        const RegionDirectReadyWindowV1& input_window,
        const std::span<const PackedLogic4> current_internal_values,
        const std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs) noexcept override
    {
        ++probe_.direct_ready_window_calls;
        std::vector<RegionKernelRegisterInput> direct_inputs;
        if (!decode_direct_inputs(image, input_window, direct_inputs)) {
            return false;
        }
        if (record_backend_failure_or_retry(
                image, LocalWaveBackendProbe::FailurePoint::direct_ready)) {
            return false;
        }
        if (probe_.decline_first_direct_ready_window
            && !probe_.direct_ready_window_declined) {
            probe_.direct_ready_window_declined = true;
            probe_.direct_ready_fallback_pending = true;
            probe_.direct_ready_input_snapshot = std::move(direct_inputs);
            probe_.direct_ready_signal_input_count_at_decline
                = kernel_.inputs.size();
            probe_.direct_ready_prepared_calls_at_decline
                = probe_.prepared_output_calls;
            probe_.direct_ready_slots_at_decline
                = probe_.prepared_output_slots_written;
            return false;
        }

        try {
            auto materialized = image;
            materialized.register_inputs = std::move(direct_inputs);
            if (!execute_prepared_output_prefix(materialized,
                    current_internal_values, ordered_prefix, outputs)) {
                return false;
            }
        } catch (...) {
            return false;
        }
        ++probe_.direct_ready_window_completions;
        return true;
    }

    [[nodiscard]] bool execute_internal_output_prefix_prepared(
        const RegionKernelActivationImage& image,
        const std::span<const PackedLogic4> current_internal_values,
        const std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs) noexcept override
    {
        return execute_prepared_output_prefix(image,
            current_internal_values, ordered_prefix, outputs);
    }

private:
    [[nodiscard]] bool decode_direct_inputs(
        const RegionKernelActivationImage& image,
        const RegionDirectReadyWindowV1& window,
        std::vector<RegionKernelRegisterInput>& decoded) const noexcept
    {
        const auto maximum_slots
            = static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max());
        const auto expected_readiness_words
            = kernel_.members.size() / 64U
            + (kernel_.members.size() % 64U != 0U ? 1U : 0U);
        if (image.register_inputs.size() != 0U || image.generation == 0U
            || window.abi_version
                != kRegionDirectReadyWindowAbiVersionV1
            || window.struct_size != sizeof(RegionDirectReadyWindowV1)
            || window.activation_generation != image.generation
            || window.frontier_generation
                != image.scheduler_prefix.frontier_generation
            || window.frontier_generation == 0U
            || window.member_count != kernel_.members.size()
            || window.readiness_word_count != expected_readiness_words
            || window.readiness_mask == nullptr
            || window.input_slots == nullptr
            || kernel_.inputs.size() > maximum_slots
            || kernel_.internal_signals.size()
                > maximum_slots - kernel_.inputs.size()
            || window.input_slot_count
                != kernel_.inputs.size() + kernel_.internal_signals.size()) {
            return false;
        }

        const auto readiness_remainder = kernel_.members.size() % 64U;
        if (readiness_remainder != 0U
            && (window.readiness_mask[expected_readiness_words - 1U]
                & (~UINT64_C(0) << readiness_remainder)) != 0U) {
            return false;
        }

        const std::span<const RegionDirectReadyInputSlotV1> slots {
            window.input_slots,
            static_cast<std::size_t>(window.input_slot_count) };
        try {
            decoded.clear();
            decoded.reserve(kernel_.inputs.size());
            for (const auto& input : kernel_.inputs) {
                if (input.width == 0U || input.width > 64U
                    || input.value_kind != ValueKind::logic4) {
                    return false;
                }
                const auto slot = std::ranges::find_if(slots,
                    [&input](const RegionDirectReadyInputSlotV1& candidate) {
                        return candidate.signal_id == input.signal
                            && candidate.register_id == input.value_register;
                    });
                if (slot == slots.end()
                    || slot->struct_size
                        != sizeof(RegionDirectReadyInputSlotV1)
                    || slot->width != input.width
                    || slot->word_count != 1U || slot->reserved != 0U
                    || slot->aval == nullptr || slot->bval == nullptr) {
                    return false;
                }
                decoded.push_back({ input.value_register,
                    PackedLogic4::from_word_planes(input.width,
                        std::span<const std::uint64_t> { slot->aval, 1U },
                        std::span<const std::uint64_t> { slot->bval, 1U }) });
            }
            for (std::size_t index = 0U;
                 index < kernel_.members.size(); ++index) {
                const bool ready
                    = (window.readiness_mask[index / 64U]
                        & (UINT64_C(1) << (index % 64U))) != 0U;
                const bool active = std::ranges::binary_search(
                    image.active_member_indices, index);
                if (ready != active) {
                    return false;
                }
                decoded.push_back({ kernel_.members[index].readiness_register,
                    PackedLogic4(1U,
                        ready ? Logic4::one : Logic4::zero) });
            }
            std::ranges::sort(decoded, std::ranges::less { },
                &RegionKernelRegisterInput::register_id);
            if (std::ranges::adjacent_find(decoded,
                    [](const RegionKernelRegisterInput& left,
                        const RegionKernelRegisterInput& right) {
                        return left.register_id == right.register_id;
                    }) != decoded.end()) {
                return false;
            }
        } catch (...) {
            decoded.clear();
            return false;
        }
        return decoded.size() == kernel_.inputs.size()
            + kernel_.members.size();
    }
};

class SuccessorMaskPreparedRegionBackend final
    : public PreparedAcceptingRegionBackend
    , public RegionKernelPreparedOutputSuccessorMaskBackend {
public:
    using PreparedAcceptingRegionBackend::PreparedAcceptingRegionBackend;

    [[nodiscard]] bool supports_direct_ready_window_successor_masks()
        const noexcept override
    {
        return probe_.execute_direct_ready_window_successor_masks;
    }

    [[nodiscard]] bool
    execute_direct_ready_window_prepared_with_successor_masks(
        const RegionKernelActivationImage& image,
        const RegionDirectReadyWindowV1& input_window,
        const std::span<const PackedLogic4> current_internal_values,
        const std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs,
        RegionPreparedOutputSuccessorMasksV1& successors) noexcept override
    {
        std::array<std::uint64_t, 8U> masks { };
        if (!prepare_successor_masks(outputs, successors, masks)
            || !PreparedAcceptingRegionBackend::
                execute_direct_ready_window_prepared(image, input_window,
                    current_internal_values, ordered_prefix, outputs)) {
            return false;
        }
        publish_successor_masks(outputs, successors, masks);
        return true;
    }

    [[nodiscard]] bool execute_internal_output_prefix_prepared_with_successor_masks(
        const RegionKernelActivationImage& image,
        const std::span<const PackedLogic4> current_internal_values,
        const std::span<const RegionConeOutputBinding> ordered_prefix,
        RegionPreparedOutputBatchV1& outputs,
        RegionPreparedOutputSuccessorMasksV1& successors) noexcept override
    {
        std::array<std::uint64_t, 8U> masks { };
        if (!prepare_successor_masks(outputs, successors, masks)) {
            return false;
        }
        if (!execute_prepared_output_prefix(image, current_internal_values,
                ordered_prefix, outputs)) {
            return false;
        }
        publish_successor_masks(outputs, successors, masks);
        return true;
    }

private:
    [[nodiscard]] bool prepare_successor_masks(
        const RegionPreparedOutputBatchV1& outputs,
        const RegionPreparedOutputSuccessorMasksV1& successors,
        std::array<std::uint64_t, 8U>& masks) const noexcept
    {
        if (successors.abi_version
                != kRegionPreparedOutputSuccessorMasksAbiVersionV1
            || successors.struct_size != sizeof(successors)
            || successors.reserved != 0U
            || successors.member_masks == nullptr
            || outputs.slots == nullptr
            || successors.slot_count != kernel_.internal_signals.size()
            || outputs.slot_count != successors.slot_count
            || successors.slot_count > probe_.last_successor_masks.size()) {
            return false;
        }
        for (std::size_t slot = 0U; slot < successors.slot_count; ++slot) {
            const auto signal = kernel_.internal_signals[slot];
            if (outputs.slots[slot].signal_id != signal) {
                return false;
            }
            std::size_t ordinal { };
            for (const auto& member : kernel_.members) {
                const bool reads_whole_any = std::ranges::any_of(
                    member.sensitivities, [signal](const auto& sensitivity) {
                        return sensitivity.signal == signal
                            && sensitivity.edge == EdgeKind::any
                            && sensitivity.offset == 0U
                            && sensitivity.width == 0U;
                    });
                if (!reads_whole_any) {
                    continue;
                }
                if (ordinal >= 64U) {
                    return false;
                }
                masks[slot] |= UINT64_C(1) << ordinal;
                ++ordinal;
            }
        }
        return true;
    }

    void publish_successor_masks(
        const RegionPreparedOutputBatchV1& outputs,
        RegionPreparedOutputSuccessorMasksV1& successors,
        const std::array<std::uint64_t, 8U>& masks) noexcept
    {
        for (std::size_t slot = 0U; slot < successors.slot_count; ++slot) {
            const auto& output = outputs.slots[slot];
            const bool changed = output.selected != 0U
                && output.changed != nullptr && *output.changed != 0U;
            successors.member_masks[slot] = output.selected != 0U
                    && changed
                ? masks[slot] : 0U;
            probe_.last_successor_masks[slot]
                = successors.member_masks[slot];
            probe_.last_successor_changed[slot] = changed ? 1U : 0U;
        }
        probe_.last_successor_mask_count = successors.slot_count;
        ++probe_.successor_mask_calls;
    }
};

class AcceptingRegionBackendProvider final
    : public RegionKernelBackendProvider {
public:
    explicit AcceptingRegionBackendProvider(
        LocalWaveBackendProbe& probe) noexcept
        : probe_(probe)
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return "runtime-local-wave-register-test-v1";
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        ++probe_.factories;
        probe_.internal_signals = kernel.internal_signals;
        for (const auto& input : kernel.inputs) {
            if (!input.internal) {
                continue;
            }
            bool rewritten = false;
            for (std::size_t index = 0U;
                 index < kernel.program.operations.size(); ++index) {
                const auto operation
                    = kernel.program.operations.expanded(index);
                const auto* const copy = operation_get_if<CopyRegister>(
                    &operation);
                if (copy != nullptr && copy->source == input.value_register) {
                    rewritten = true;
                    ++probe_.internal_register_rewrites;
                    break;
                }
            }
            probe_.all_internal_reads_rewritten
                = probe_.all_internal_reads_rewritten && rewritten;
        }
        if (probe_.execute_prepared_output_prefix) {
            if (probe_.execute_prepared_output_successor_masks) {
                return std::make_unique<SuccessorMaskPreparedRegionBackend>(
                    probe_, kernel);
            }
            return std::make_unique<PreparedAcceptingRegionBackend>(
                probe_, kernel);
        }
        return std::make_unique<AcceptingRegionBackend>(probe_, kernel);
    }

private:
    LocalWaveBackendProbe& probe_;
};

void check_multidimensional_alias_region_admission()
{
    ScopedEnvironment kernel_enabled { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    for (const bool three_dimensions : { false, true }) {
        Interpreter interpreter;
        RegionBackendProbe probe;
        const auto input = interpreter.add_signal({
            "array_region.input", PackedLogic4(1U, Logic4::one) });
        const auto proxy = interpreter.add_signal({
            "array_region.array", PackedLogic4(4U, Logic4::z),
            ResolutionKind::sv_wire });
        std::array<SignalId, 4U> leaves;
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            leaves[ordinal] = interpreter.add_signal({
                "array_region.leaf" + std::to_string(ordinal),
                PackedLogic4(1U, Logic4::z), ResolutionKind::sv_wire });
        }
        const auto output = interpreter.add_signal({
            "array_region.output", PackedLogic4(1U, Logic4::x),
            ResolutionKind::sv_wire });
        ContainerType type;
        type.element_kind = ContainerElementKind::Packed;
        type.element_width = 1U;
        type.fixed = true;
        type.index_left = 2;
        type.index_right = 1;
        type.dimensions = { { 2, 1 }, { -1, 0 } };
        if (three_dimensions) {
            type.dimensions.push_back({ 7, 7 });
        }
        const auto object = interpreter.add_container_object({
            "array_region.array", ContainerValue { type,
                std::vector<PackedLogic4>(4U, PackedLogic4(1U, Logic4::z)),
                { } }, std::nullopt });
        for (std::size_t ordinal = 0U; ordinal < leaves.size(); ++ordinal) {
            interpreter.add_container_element_signal_alias(
                { object, static_cast<std::uint32_t>(ordinal),
                    leaves[ordinal], true, true });
        }
        interpreter.add_container_aggregate_signal_alias(
            { object, proxy, true, true });
        for (std::size_t member = 0U; member <= leaves.size(); ++member) {
            const auto source = member == 0U ? input : leaves[member - 1U];
            const auto target = member == leaves.size() ? output : leaves[member];
            Process process;
            process.id = static_cast<ProcessId>(member);
            process.name = "array_region_member" + std::to_string(member);
            process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
            process.register_count = 1U;
            process.static_sensitivity = { { source, EdgeKind::any } };
            process.driver_regions = { { target, 0U, 0U, true } };
            process.operations = { ReadSignal { 0U, source },
                WriteUpdate { target, 0U, SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U } };
            static_cast<void>(interpreter.add_process(std::move(process)));
        }
        interpreter.set_region_kernel_backend_provider(
            std::make_shared<RecordingRegionBackendProvider>(probe));
        interpreter.start();
        // Direct physical alias leaves are public boundaries. The final
        // output is first read after certification, so it remains internal.
        const std::array<SignalId, 1U> expected_internals { output };
        require(probe.factories == 1U
                && std::ranges::equal(probe.internal_signals,
                    expected_internals),
            "multidimensional alias leaves are boundaries while the unobserved output remains internal");
        require(interpreter.run().status == RunStatus::completed
                && interpreter.signal_value(output) == PackedLogic4(1U, Logic4::one)
                && interpreter.signal_value(proxy) == PackedLogic4(4U, Logic4::one),
            "admitted multidimensional families retain ordinary publication and proxy values");
        const auto& retained = interpreter.container_object_value(object);
        require(retained.type.dimensions == type.dimensions
                && retained.elements.size() == leaves.size()
                && std::ranges::all_of(retained.elements,
                    [](const PackedLogic4& value) {
                        return value == PackedLogic4(1U, Logic4::one);
                    }),
            "region admission preserves declared dimensions and exact public element values");
    }
}

std::uint64_t region_profile_metric(
    const std::string& profile, const std::string_view metric)
{
    const auto key = std::string { metric } + '=';
    const auto start = profile.find(key);
    require(start != std::string::npos,
        "region profile includes the requested route counter");
    const auto value_start = start + key.size();
    const auto end = profile.find(' ', value_start);
    return static_cast<std::uint64_t>(std::stoull(profile.substr(
        value_start, end == std::string::npos
            ? std::string::npos : end - value_start)));
}

void check_region_kernel_capability_route(
    const bool expected_equivalent, const bool actual_equivalent,
    const bool configure_coverage = false,
    const bool install_coverage_hook = false,
    const bool install_all_coverage_hooks = false,
    const bool install_scheduler_trace = false,
    const bool observe_during_batch_trace = false,
    const bool observe_after_ticket_creation = false,
    const bool single_member_activation = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    RegionKernelProbe probe;
    RegionObservationTrace observation_trace;
    std::vector<std::string> route_events;
    if (observe_after_ticket_creation)
        probe.route_events = &route_events;
    PackedLogic4 foreign_observed_value;
    bool foreign_observation_seen { };
    bool foreign_observation_failed { };
    bool observer_enqueued_after_trigger { };
    bool observer_saw_compacted_ticket { };
    bool observer_saw_readiness_ticket { };
    bool observer_saw_current_epoch { };
    bool observer_invalidated_component { };
    SchedulerBatchCompactionStats compaction_at_observer { };
    std::size_t coverage_hook_calls { };
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        const auto input = interpreter.add_signal({ "region.input",
            PackedLogic4(1U, Logic4::one) });
        const auto internal = interpreter.add_signal({ "region.internal",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto output = interpreter.add_signal({ "region.output",
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });
        const auto trigger = interpreter.add_signal({ "region.trigger",
            PackedLogic4(1U, Logic4::zero) });

        Process order_spacer;
        order_spacer.id = 0U;
        order_spacer.name = "region_order_spacer";
        order_spacer.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        order_spacer.initialize = false;
        order_spacer.operations = { Halt { } };
        require(interpreter.add_process(std::move(order_spacer)) == 0U,
            "an inert process reserves a stable order before the component");

        Process producer;
        producer.id = 1U;
        producer.name = "region_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.initialize = false;
        producer.register_count = 1U;
        producer.static_sensitivity = { { trigger, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, input },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == 1U,
            "region producer process has its stable ID");

        Process consumer;
        consumer.id = 2U;
        consumer.name = "region_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.initialize = false;
        consumer.register_count = 1U;
        consumer.static_sensitivity = single_member_activation
            ? std::vector<Sensitivity> {
                  { internal, EdgeKind::any } }
            : std::vector<Sensitivity> {
                  { trigger, EdgeKind::any },
                  { internal, EdgeKind::any } };
        consumer.driver_regions = { { output, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, internal }, UnaryNot { 0U, 0U },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(consumer)) == 2U,
            "region consumer process has its stable ID");

        for (std::size_t member_index = 0U; member_index < 2U;
             ++member_index) {
            const auto process_id = static_cast<ProcessId>(member_index + 1U);
            const auto& registered = interpreter.process_program(process_id);
            const auto binding = ProcessExecutorProgramBinding {
                registered, registered, process_id };
            const auto wait_instruction = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            DeferredProcessExecutorContract contract;
            contract.expected_access = binding;
            contract.callbacks_observation_safe = true;
            contract.expected_region_kernel_equivalent = expected_equivalent;
            interpreter.set_deferred_process_executor(process_id,
                [] { return true; },
                [&probe, process_id, member_index, binding,
                    actual_equivalent, wait_instruction, input, internal,
                    output] {
                    const auto source
                        = member_index == 0U ? input : internal;
                    const auto destination
                        = member_index == 0U ? internal : output;
                    return std::make_unique<RegionKernelExecutor>(probe,
                        process_id, source, destination, wait_instruction,
                        binding, actual_equivalent, member_index);
                }, std::move(contract));
        }
        interpreter.materialize_ready_process_executors();

        if (install_scheduler_trace) {
            if (observe_during_batch_trace) {
                observation_trace.interpreter = &interpreter;
                observation_trace.signal = internal;
                interpreter.scheduler().set_trace_hook(
                    &observation_trace, &RegionObservationTrace::receive);
            } else {
                interpreter.scheduler().set_trace_hook(
                    nullptr, &discard_scheduler_trace);
            }
        }

        Process clock;
        clock.id = 3U;
        clock.name = "region_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        if (observe_after_ticket_creation || single_member_activation) {
            clock.operations = {
                WaitFor { 1U },
                LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
                WriteBlocking { trigger, 0U }, Halt { },
            };
        } else {
            clock.operations = { WaitFor { 1U },
                LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
                WriteBlocking { trigger, 0U }, WaitFor { 1U },
                LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
                WriteBlocking { trigger, 0U }, Halt { } };
        }
        require(interpreter.add_process(std::move(clock)) == 3U,
            "region fixture clock is independent of the compute component");

        if (configure_coverage) {
            interpreter.set_code_coverage_counters({ 0U });
        }
        if (install_coverage_hook || install_all_coverage_hooks) {
            interpreter.set_coverage_query_hook(
                [&coverage_hook_calls](CoverageQueryKind) {
                    ++coverage_hook_calls;
                    return PackedLogic4(1U, Logic4::zero);
                });
        }
        if (install_all_coverage_hooks) {
            interpreter.set_coverage_sample_hook(
                [&coverage_hook_calls](std::string_view,
                    std::span<const PackedLogic4>,
                    std::span<const std::uint8_t>,
                    std::span<const frontend::SystemVerilogScalarKind>,
                    CoverageSampleTrigger) {
                    ++coverage_hook_calls;
                });
            interpreter.set_coverage_control_hook(
                [&coverage_hook_calls](const CoverageControlEvent&) {
                    ++coverage_hook_calls;
                    return std::int32_t { };
                });
            interpreter.set_coverage_access_hook(
                [&coverage_hook_calls](const CoverageAccessEvent&) {
                    ++coverage_hook_calls;
                    return std::int32_t { };
                });
            interpreter.set_code_coverage_overflow_hook(
                [&coverage_hook_calls](
                    ::fsim::runtime::CodeCoverageCounterId) {
                    ++coverage_hook_calls;
                });
            interpreter.set_coverage_database_control_hook(
                [&coverage_hook_calls](const CoverageDatabaseControlEvent&) {
                    ++coverage_hook_calls;
                });
        }

        interpreter.start();
        if (observe_after_ticket_creation) {
            // The frozen Active batch runs clock order 3 before this order 4
            // callback. Clock publication queues both component members for
            // the next Active batch; this callback inserts observer order 0
            // before that batch is prepared and compacted.
            interpreter.scheduler().schedule_systemverilog_at(
                1U, SchedulerPhase::active, 4U,
                [&interpreter, internal, &foreign_observed_value,
                    &foreign_observation_seen, &foreign_observation_failed,
                    &observer_enqueued_after_trigger,
                    &observer_saw_compacted_ticket,
                    &observer_saw_readiness_ticket,
                    &observer_saw_current_epoch,
                    &observer_invalidated_component,
                    &compaction_at_observer, &route_events](Scheduler& current) {
                    current.schedule_systemverilog(
                        SchedulerPhase::active, 0U,
                        [&interpreter, internal, &foreign_observed_value,
                            &foreign_observation_seen,
                            &foreign_observation_failed,
                            &observer_saw_compacted_ticket,
                            &observer_saw_readiness_ticket,
                            &observer_saw_current_epoch,
                            &observer_invalidated_component,
                            &compaction_at_observer,
                            &route_events](Scheduler& observer_scheduler) {
                            try {
                                compaction_at_observer
                                    = observer_scheduler
                                          .systemverilog_batch_compaction_stats();
                                if (observer_scheduler.now() != 1U
                                    || observer_scheduler.current_phase()
                                        != SchedulerPhase::active) {
                                    foreign_observation_failed = true;
                                    return;
                                }
                                observer_saw_compacted_ticket
                                    = compaction_at_observer.tickets >= 1U
                                    && compaction_at_observer.members >= 2U
                                    && compaction_at_observer.entries_elided
                                        >= 1U;
                                observer_saw_readiness_ticket
                                    = compaction_at_observer
                                              .readiness_ticket_queue_insertions
                                            == 1U
                                    && compaction_at_observer
                                               .readiness_ticket_members
                                        == 2U
                                    && compaction_at_observer
                                               .readiness_ticket_members_elided
                                        == 1U;
                                auto& implementation
                                    = OwnedDriverDemotionTestAccess::implementation(
                                        interpreter);
                                if (!implementation.region_graph
                                    || implementation.region_component_by_process
                                            .size() <= 1U) {
                                    foreign_observation_failed = true;
                                    return;
                                }
                                const auto component
                                    = implementation
                                          .region_component_by_process[1U];
                                observer_saw_current_epoch
                                    = implementation.region_graph
                                          ->component_epochs_current(component);
                                route_events.push_back("observe");
                                foreign_observed_value
                                    = interpreter.signal_value(internal);
                                observer_invalidated_component
                                    = !implementation.region_graph
                                           ->component_epochs_current(component);
                                foreign_observation_seen = true;
                            } catch (...) {
                                foreign_observation_failed = true;
                            }
                        });
                    observer_enqueued_after_trigger
                        = current.now() == 1U
                        && current.current_phase() == SchedulerPhase::active;
                });
        }
        require(interpreter.run().status == RunStatus::completed,
            "region replacement/fallback route completes the fixture");
        if (install_all_coverage_hooks) {
            const auto counters = interpreter.code_coverage_counters();
            require(coverage_hook_calls == 0U
                    && counters.size() == 1U && counters.front() == 0U,
                "installed unused coverage hooks and counters remain untouched");
        }
        require(interpreter.signal_value(internal)
                    == PackedLogic4(1U, Logic4::one)
                && interpreter.signal_value(output)
                    == PackedLogic4(1U, Logic4::zero),
            "replacement preserves committed-input and original-owner values");
        if (observe_during_batch_trace) {
            // Registering the sole sv_wire owner seeds its undriven current
            // value to Z before the first queued producer activation.
            require(observation_trace.observed && !observation_trace.failed
                    && observation_trace.observed_value
                        == PackedLogic4(1U, Logic4::z),
                "a trace observer can safely read a queued component input before fallback");
        }
        if (observe_after_ticket_creation) {
            require(foreign_observation_seen && !foreign_observation_failed
                    && foreign_observed_value
                        == PackedLogic4(1U, Logic4::z),
                "an earlier foreign process observes before component fallback");
        }
    }

    const auto output = captured.str();
    const auto profile_start = output.find(
        "fsim-profile: sv-ordered-wave-summary ");
    require(profile_start != std::string::npos,
        "runtime route emits the focused wave profile");
    const auto profile_end = output.find('\n', profile_start);
    const auto profile = output.substr(profile_start,
        profile_end == std::string::npos
            ? std::string::npos : profile_end - profile_start);
    const auto expected_runs = expected_equivalent && actual_equivalent
            && !install_scheduler_trace && !observe_after_ticket_creation
            && !single_member_activation
        ? 1U : 0U;
    const auto expected_members = expected_runs == 0U ? 0U : 2U;
    require(region_profile_metric(profile, "region_kernel_runs")
                == expected_runs
            && region_profile_metric(profile, "region_kernel_members")
                == expected_members
            && (!(configure_coverage || install_coverage_hook
                    || install_all_coverage_hooks)
                || region_profile_metric(profile, "accepted_members") > 0U)
            && (!install_scheduler_trace
                || (region_profile_metric(profile, "region_trace_declines") > 0U
                    && region_profile_metric(profile, "region_backend_attempts")
                        == 0U)),
        "only a deferred executor with expected and actual opt-in uses the kernel");
    if (expected_runs != 0U) {
        require(region_profile_metric(profile, "component_batch_tickets")
                    >= 1U
                && region_profile_metric(profile, "component_batch_members")
                    >= expected_members
                && region_profile_metric(
                    profile, "component_batch_entries_elided") >= 1U
                && region_profile_metric(
                    profile, "component_batch_direct_dispatches") >= 1U
                && region_profile_metric(
                    profile, "component_batch_direct_members")
                    >= expected_members,
            "the interpreter kernel route uses a compact component queue ticket");
        require(region_profile_metric(profile,
                    "region_backend_attempts") == 0U
                && region_profile_metric(profile,
                    "component_readiness_mask_images") == 0U,
            "the checked C++ activation keeps ticket ordering without a native mask image");
        require(region_profile_metric(profile,
                    "component_readiness_queue_insertions") > 0U
                && region_profile_metric(profile,
                    "component_readiness_members")
                    > region_profile_metric(profile,
                        "component_readiness_queue_insertions")
                && region_profile_metric(profile,
                    "component_readiness_members_elided") > 0U,
            "the checked route groups ready members behind fewer queue entries");
    }
    if (single_member_activation) {
        require(region_profile_metric(profile,
                    "component_readiness_queue_insertions") == 2U
                && region_profile_metric(profile,
                    "component_readiness_members") == 2U
                && region_profile_metric(profile,
                    "component_readiness_members_elided") == 0U
                && region_profile_metric(profile,
                    "component_batch_tickets") == 2U
                && region_profile_metric(profile,
                    "region_backend_attempts") == 0U
                && region_profile_metric(profile,
                    "a2_ordinary_internal_updates") == 2U,
            "checked singleton tickets retain their original ordinary callbacks");
    }
    if (observe_during_batch_trace) {
        require(region_profile_metric(profile, "component_batch_tickets")
                    >= 1U
                && region_profile_metric(profile, "component_batch_members")
                    >= 2U
                && region_profile_metric(
                    profile, "component_batch_entries_elided") >= 1U,
            "the late-observation fallback begins from an actual component ticket");
    }
    if (observe_after_ticket_creation) {
        require(observer_enqueued_after_trigger
                && observer_saw_compacted_ticket
                && observer_saw_readiness_ticket
                && observer_saw_current_epoch
                && observer_invalidated_component
                && compaction_at_observer.tickets >= 1U
                && compaction_at_observer.members >= 2U
                && compaction_at_observer.entries_elided >= 1U
                && compaction_at_observer
                       .readiness_ticket_queue_insertions == 1U
                && compaction_at_observer.readiness_ticket_members == 2U
                && compaction_at_observer
                       .readiness_ticket_members_elided == 1U
                && route_events == std::vector<std::string> {
                    "observe", "member0", "member1", "member1" },
            "the order-zero observer sees a compacted ticket before exact-order fallback");
        require(region_profile_metric(profile, "component_batch_tickets")
                    >= 1U
                && region_profile_metric(profile, "component_batch_members")
                    >= 2U
                && region_profile_metric(
                    profile, "component_batch_entries_elided") >= 1U
                && region_profile_metric(
                    profile, "component_batch_direct_dispatches") >= 1U
                && region_profile_metric(
                    profile, "component_batch_direct_members") >= 2U
                && region_profile_metric(profile, "region_kernel_runs") == 0U,
            "the stale component ticket is checked and declines native execution");
        const auto a4_profile_start = output.find(
            "fsim-profile: a4-state-summary ");
        require(a4_profile_start != std::string::npos,
            "late observation reports the A4 state transition");
        const auto a4_profile_end = output.find('\n', a4_profile_start);
        const auto a4_profile = output.substr(a4_profile_start,
            a4_profile_end == std::string::npos
                ? std::string::npos : a4_profile_end - a4_profile_start);
        require(region_profile_metric(a4_profile, "materialized_components")
                    >= 1U,
            "the foreign signal read materializes the ticket's component backing");
    }
    const auto expected_wait_starts = single_member_activation
        ? std::array<std::size_t, 4U> { 0U, 0U, 0U, 0U }
        : observe_after_ticket_creation
        // Once the foreign read invalidates the ticket, producer fallback
        // publishes internal and wakes consumer for one additional wait PC.
        ? std::array<std::size_t, 4U> { 0U, 1U, 0U, 0U }
        : expected_runs == 1U
            ? std::array<std::size_t, 4U> { 0U, 1U, 0U, 0U }
            : std::array<std::size_t, 4U> { 1U, 2U, 0U, 0U };
    require(probe.pc_mismatches == std::array<std::size_t, 4U> { }
            && probe.initial_starts
                == std::array<std::size_t, 4U> { 1U, 1U, 0U, 0U }
            && probe.wait_starts == expected_wait_starts,
        "replacement skips only the offered members and preserves the LLVM wait PC");
}

struct GroupedFanoutSignalState {
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    PackedLogic4 driver;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    ProcessSchedulingDomain event_domain {
        ProcessSchedulingDomain::generic
    };
    SchedulerPhase event_phase { SchedulerPhase::active };
    std::uint64_t event_round { };

    bool operator==(const GroupedFanoutSignalState&) const = default;
};

struct LocalWavePartialPublicationRun {
    GroupedFanoutSignalState output;
    std::uint64_t kernel_runs { };
    std::uint64_t kernel_publications { };
    std::uint64_t local_update_dispatches { };
};

struct GroupedFanoutRun {
    std::array<GroupedFanoutSignalState, 4U> outputs;
    std::array<GroupedFanoutSignalState, 4U> shared_signals;
    SchedulerBatchCompactionStats scheduler_stats;
    RunResult result;
    std::string profile;
    std::vector<std::string> reader_resume_order;
};

GroupedFanoutRun run_grouped_fanout_case(
    const bool grouped, const bool disable_fanout_grouping = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", grouped ? "1" : "0" };
    ScopedEnvironment fanout_grouping {
        "FSIM_DISABLE_FANOUT_COHORT_GROUPING",
        disable_fanout_grouping ? "1" : nullptr };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    GroupedFanoutRun result;
    RegionKernelProbe executor_probe;
    executor_probe.route_events = &result.reader_resume_order;
    executor_probe.route_process_ids = true;
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        const auto internal_a = interpreter.add_signal({ "p3.internal_a",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto internal_b = interpreter.add_signal({ "p3.internal_b",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto gate_a = interpreter.add_signal({ "p3.foreign_gate_a",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto gate_b = interpreter.add_signal({ "p3.foreign_gate_b",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto foreign_trigger = interpreter.add_signal({
            "p3.foreign_trigger", PackedLogic4(1U, Logic4::zero) });
        const auto trigger_a = interpreter.add_signal({ "p3.trigger_a",
            PackedLogic4(1U, Logic4::zero) });
        const auto trigger_b = interpreter.add_signal({ "p3.trigger_b",
            PackedLogic4(1U, Logic4::zero) });
        std::array<SignalId, 4U> outputs { };
        for (std::size_t index = 0U; index < outputs.size(); ++index) {
            outputs[index] = interpreter.add_signal({
                "p3.output_" + std::to_string(index),
                PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });
        }

        Process source_a;
        source_a.id = 0U;
        source_a.name = "p3_source_a";
        source_a.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        source_a.initialize = false;
        source_a.register_count = 1U;
        source_a.static_sensitivity = { { trigger_a, EdgeKind::any } };
        source_a.driver_regions = { { internal_a, 0U, 0U, true },
            { foreign_trigger, 0U, 0U, true } };
        source_a.operations = {
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteUpdate { internal_a, 0U,
                SignalUpdateDomain::systemverilog_active },
            WriteUpdate { foreign_trigger, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(source_a)) == 0U,
            "grouped fanout source A retains its original process key");

        Process source_b;
        source_b.id = 1U;
        source_b.name = "p3_source_b";
        source_b.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        source_b.initialize = false;
        source_b.register_count = 1U;
        source_b.static_sensitivity = { { trigger_b, EdgeKind::any } };
        source_b.driver_regions = { { internal_b, 0U, 0U, true } };
        source_b.operations = {
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteUpdate { internal_b, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(source_b)) == 1U,
            "grouped fanout source B retains its original process key");

        const std::array<ProcessId, 4U> reader_ids { 2U, 3U, 5U, 6U };
        const std::array<SignalId, 4U> sensitivity_signals {
            internal_a, internal_b, internal_a, internal_b };
        const std::array<SignalId, 4U> gate_signals {
            gate_a, gate_b, gate_a, gate_b };
        const auto add_reader = [&](const std::size_t index) {
            Process reader;
            reader.id = reader_ids[index];
            reader.name = "p3_reader_" + std::to_string(index);
            reader.scheduling_domain
                = ProcessSchedulingDomain::systemverilog;
            reader.initialize = false;
            reader.register_count = 1U;
            reader.static_sensitivity = {
                { sensitivity_signals[index], EdgeKind::any } };
            reader.driver_regions = { { outputs[index], 0U, 0U, true } };
            reader.operations = {
                ReadSignal { 0U, gate_signals[index] },
                WriteUpdate { outputs[index], 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
            require(interpreter.add_process(std::move(reader))
                    == reader_ids[index],
                "certified reader keeps its original dense scheduler key");
        };
        add_reader(0U);
        add_reader(1U);

        Process foreign_writer;
        foreign_writer.id = 4U;
        foreign_writer.name = "p3_foreign_writer";
        foreign_writer.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        foreign_writer.initialize = false;
        foreign_writer.register_count = 1U;
        foreign_writer.static_sensitivity = {
            { foreign_trigger, EdgeKind::any } };
        foreign_writer.driver_regions = { { gate_a, 0U, 0U, true },
            { gate_b, 0U, 0U, true } };
        foreign_writer.operations = {
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            // Keep this foreign process on checked dispatch with a meaningful
            // checked precondition while preserving its original key between
            // the reader groups. Assert is deliberately outside the pure
            // region operation subset and reads no shared signal.
            Assert { 0U, "foreign writer precondition",
                AssertionSeverity::failure, { } },
            WriteBlocking { gate_a, 0U },
            WriteBlocking { gate_b, 0U },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(foreign_writer)) == 4U,
            "foreign blocking writer is keyed between the two ticket groups");

        add_reader(2U);
        add_reader(3U);

        Process clock;
        clock.id = 7U;
        clock.name = "p3_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.driver_regions = { { trigger_a, 0U, 0U, true },
            { trigger_b, 0U, 0U, true } };
        clock.operations = {
            WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { trigger_a, 0U },
            WriteBlocking { trigger_b, 0U },
            Halt { },
        };
        require(interpreter.add_process(std::move(clock)) == 7U,
            "grouped fanout clock remains outside both certified components");

        {
            for (std::size_t index = 0U; index < reader_ids.size(); ++index) {
                const auto process_id = reader_ids[index];
                const auto& registered
                    = interpreter.process_program(process_id);
                const auto binding = ProcessExecutorProgramBinding {
                    registered, registered, process_id };
                const auto wait_instruction = static_cast<InstructionIndex>(
                    registered.operations.size() - 2U);
                DeferredProcessExecutorContract contract;
                contract.expected_access = binding;
                contract.callbacks_observation_safe = true;
                contract.expected_region_kernel_equivalent = true;
                interpreter.set_deferred_process_executor(process_id,
                    [] { return true; },
                    [&executor_probe, process_id, binding, wait_instruction,
                        gate = gate_signals[index], output = outputs[index]] {
                        return std::make_unique<RegionKernelExecutor>(
                            executor_probe, process_id, gate, output,
                            wait_instruction, binding, true, 0U);
                    }, std::move(contract));
            }
            interpreter.materialize_ready_process_executors();
        }

        interpreter.start();
        result.result = interpreter.run();
        require(result.result.status == RunStatus::completed,
            "grouped fanout fixture drains both ordered readiness groups");
        result.scheduler_stats
            = interpreter.scheduler().systemverilog_batch_compaction_stats();

        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto capture_signal = [&](const SignalId signal,
                                        const std::optional<ProcessId> owner) {
            GroupedFanoutSignalState state;
            state.current = interpreter.signal_value_snapshot(signal);
            state.last = implementation.signal_last_values.at(signal);
            state.stored = interpreter.stored_signal_value_snapshot(signal);
            if (owner) {
                state.driver = interpreter.driver_value(*owner, signal);
            }
            state.event = implementation.signal_events.at(signal);
            state.transaction = implementation.signal_transactions.at(signal);
            const auto& stamp
                = implementation.signal_event_scheduling_stamps.at(signal);
            state.event_domain = stamp.origin.process_domain;
            state.event_phase = stamp.origin.phase;
            state.event_round = stamp.systemverilog_round;
            return state;
        };
        for (std::size_t index = 0U; index < outputs.size(); ++index) {
            result.outputs[index]
                = capture_signal(outputs[index], reader_ids[index]);
        }
        result.shared_signals = { capture_signal(internal_a, 0U),
            capture_signal(internal_b, 1U), capture_signal(gate_a, 4U),
            capture_signal(gate_b, 4U) };
        const auto no_component = std::numeric_limits<std::size_t>::max();
        const auto& components = implementation.region_component_by_process;
        if (grouped) {
            require(components.at(4U) == no_component,
                "the checked foreign writer stays outside certified "
                "reader components");
            require(components.at(2U) != no_component
                    && components.at(3U) != no_component
                    && components.at(2U) == components.at(5U)
                    && components.at(3U) == components.at(6U)
                    && components.at(2U) != components.at(3U),
                "the interleaved reader pairs remain distinct certified components");
        } else {
            require(components.at(2U) == no_component
                    && components.at(3U) == no_component
                    && components.at(5U) == no_component
                    && components.at(6U) == no_component,
                "the disabled region route keeps every reader ordinary");
        }
    }
    result.profile = captured.str();
    return result;
}

struct LargeGroupedFanoutRun {
    RunResult result;
    SchedulerBatchCompactionStats scheduler_stats;
    std::vector<GroupedFanoutSignalState> outputs;
    std::vector<std::string> reader_resume_order;
    std::size_t grouped_member_count { };
    std::uint64_t readiness_queue_tickets { };
    std::uint64_t readiness_logical_members { };
    std::uint64_t readiness_fallback_members { };
};

LargeGroupedFanoutRun run_large_grouped_fanout_case(
    const bool grouped, const std::size_t reader_count)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", grouped ? "1" : "0" };
    ScopedEnvironment fanout_grouping {
        "FSIM_DISABLE_FANOUT_COHORT_GROUPING", nullptr };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", nullptr };

    LargeGroupedFanoutRun result;
    result.outputs.resize(reader_count);
    RegionKernelProbe executor_probe;
    executor_probe.route_events = &result.reader_resume_order;
    executor_probe.route_process_ids = true;
    Interpreter interpreter;
    const auto internal = interpreter.add_signal({ "a3.large_internal",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto gate = interpreter.add_signal({ "a3.large_gate",
        PackedLogic4(1U, Logic4::zero) });
    const auto trigger = interpreter.add_signal({ "a3.large_trigger",
        PackedLogic4(1U, Logic4::zero) });
    std::vector<SignalId> outputs;
    outputs.reserve(reader_count);
    for (std::size_t index = 0U; index < reader_count; ++index) {
        outputs.push_back(interpreter.add_signal({
            "a3.large_output_" + std::to_string(index),
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire }));
    }

    Process source;
    source.id = 0U;
    source.name = "a3_large_source";
    source.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    source.initialize = false;
    source.register_count = 1U;
    source.static_sensitivity = { { trigger, EdgeKind::any } };
    source.driver_regions = { { internal, 0U, 0U, true } };
    source.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteUpdate { internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(source)) == 0U,
        "large fanout source keeps its original process key");

    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto process_id = static_cast<ProcessId>(index + 1U);
        Process reader;
        reader.id = process_id;
        reader.name = "a3_large_reader_" + std::to_string(index);
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        reader.initialize = false;
        reader.register_count = 1U;
        reader.static_sensitivity = { { internal, EdgeKind::any } };
        reader.driver_regions = { { outputs[index], 0U, 0U, true } };
        reader.operations = {
            ReadSignal { 0U, gate },
            WriteUpdate { outputs[index], 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(reader)) == process_id,
            "large fanout reader retains its stable process key");
    }

    const auto clock_id = static_cast<ProcessId>(reader_count + 1U);
    Process clock;
    clock.id = clock_id;
    clock.name = "a3_large_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { trigger, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { trigger, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "large fanout clock remains outside the reader component");

    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto process_id = static_cast<ProcessId>(index + 1U);
        const auto& registered = interpreter.process_program(process_id);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process_id };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process_id,
            [] { return true; },
            [&executor_probe, process_id, gate, output = outputs[index],
                wait_instruction, binding] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process_id, gate, output,
                    wait_instruction, binding, true, 0U);
            }, std::move(contract));
    }
    interpreter.materialize_ready_process_executors();
    interpreter.start();
    result.result = interpreter.run();
    require(result.result.status == RunStatus::completed,
        "large grouped fanout drains to completion");
    result.scheduler_stats
        = interpreter.scheduler().systemverilog_batch_compaction_stats();
    result.readiness_queue_tickets
        = result.scheduler_stats.readiness_ticket_queue_insertions;
    result.readiness_logical_members
        = result.scheduler_stats.readiness_ticket_members;
    result.readiness_fallback_members
        = result.scheduler_stats.readiness_ticket_fallback_members;

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    if (grouped) {
        const auto& descriptor
            = implementation.region_grouped_fanout_by_signal.at(internal);
        require(descriptor.generation == implementation.region_runtime_generation
                && descriptor.group_count == 1U
                && descriptor.group_offset
                    < implementation.region_grouped_fanout_groups.size(),
            "the large elaborated reader group remains admitted");
        const auto& group = implementation.region_grouped_fanout_groups.at(
            descriptor.group_offset);
        result.grouped_member_count = group.member_count;
    } else {
        const auto& descriptors
            = implementation.region_grouped_fanout_by_signal;
        require(descriptors.empty()
                || (internal < descriptors.size()
                    && descriptors[internal].group_count == 0U),
            "the disabled region route has no grouped descriptor");
    }
    for (std::size_t index = 0U; index < reader_count; ++index) {
        auto& state = result.outputs[index];
        state.current = interpreter.signal_value_snapshot(outputs[index]);
        state.last = implementation.signal_last_values.at(outputs[index]);
        state.stored = interpreter.stored_signal_value_snapshot(outputs[index]);
        state.driver = interpreter.driver_value(
            static_cast<ProcessId>(index + 1U), outputs[index]);
        state.event = implementation.signal_events.at(outputs[index]);
        state.transaction
            = implementation.signal_transactions.at(outputs[index]);
        const auto& stamp
            = implementation.signal_event_scheduling_stamps.at(outputs[index]);
        state.event_domain = stamp.origin.process_domain;
        state.event_phase = stamp.origin.phase;
        state.event_round = stamp.systemverilog_round;
    }
    return result;
}

void test_large_grouped_fanout_exceeds_sixty_four_members()
{
    constexpr std::array<std::size_t, 2U> reader_counts {
        65U,
        129U,
    };
    for (const auto reader_count : reader_counts) {
        const auto ordinary = run_large_grouped_fanout_case(
            false, reader_count);
        const auto grouped = run_large_grouped_fanout_case(
            true, reader_count);
        require(grouped.grouped_member_count == reader_count
                && grouped.readiness_queue_tickets == 1U
                && grouped.readiness_logical_members == reader_count
                && grouped.readiness_fallback_members == 0U,
            "large grouped fanout retains one ticket for every eligible reader");
        require(grouped.reader_resume_order.size() == reader_count
                && ordinary.reader_resume_order
                    == grouped.reader_resume_order,
            "large grouped fanout resumes every reader exactly once in process order");
        for (std::size_t index = 0U; index < reader_count; ++index) {
            require(grouped.reader_resume_order[index]
                        == "member" + std::to_string(index + 1U),
                "large grouped fanout preserves each reader scheduler key");
        }
        require(ordinary.result.status == grouped.result.status
                && ordinary.result.time == grouped.result.time
                && ordinary.result.delta == grouped.result.delta
                && ordinary.outputs == grouped.outputs,
            "65/129-member grouped route matches ordinary value and event metadata");
        for (const auto& output : grouped.outputs) {
            require(output.current == PackedLogic4(1U, Logic4::zero)
                    && output.driver == PackedLogic4(1U, Logic4::zero),
                "each grouped reader publishes its own expected output value");
        }
    }
}

struct AutomaticReadinessPoolRun {
    RunResult result;
    SchedulerBatchCompactionStats scheduler_stats;
    std::vector<GroupedFanoutSignalState> outputs;
    std::vector<GroupedFanoutSignalState> triggers;
    std::vector<std::string> reader_resume_order;
    std::size_t distinct_component_count { };
    std::size_t grouped_signal_count { };
};

AutomaticReadinessPoolRun run_automatic_readiness_pool_case(
    const bool grouped, const std::size_t reader_count)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", grouped ? "1" : "0" };
    ScopedEnvironment fanout_grouping {
        "FSIM_DISABLE_FANOUT_COHORT_GROUPING", nullptr };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", nullptr };

    AutomaticReadinessPoolRun result;
    result.outputs.resize(reader_count);
    result.triggers.resize(reader_count);
    RegionKernelProbe executor_probe;
    executor_probe.route_events = &result.reader_resume_order;
    executor_probe.route_process_ids = true;

    Interpreter interpreter;
    std::vector<SignalId> source_signals;
    std::vector<SignalId> trigger_signals;
    std::vector<SignalId> output_signals;
    source_signals.reserve(reader_count);
    trigger_signals.reserve(reader_count);
    output_signals.reserve(reader_count);
    for (std::size_t index = 0U; index < reader_count; ++index) {
        trigger_signals.push_back(interpreter.add_signal({
            "a3.pool_trigger_" + std::to_string(index),
            PackedLogic4(1U, Logic4::zero) }));
        const auto source_value = index % 2U == 0U
            ? Logic4::zero : Logic4::one;
        source_signals.push_back(interpreter.add_signal({
            "a3.pool_source_" + std::to_string(index),
            PackedLogic4(1U, source_value) }));
        output_signals.push_back(interpreter.add_signal({
            "a3.pool_output_" + std::to_string(index),
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire }));
    }

    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto process_id = static_cast<ProcessId>(index);
        Process reader;
        reader.id = process_id;
        reader.name = "a3_pool_reader_" + std::to_string(index);
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        reader.initialize = false;
        reader.register_count = 1U;
        reader.static_sensitivity = {
            { trigger_signals[index], EdgeKind::any } };
        reader.driver_regions = { { output_signals[index], 0U, 0U, true } };
        reader.operations = {
            ReadSignal { 0U, source_signals[index] },
            WriteUpdate { output_signals[index], 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(reader)) == process_id,
            "each automatic-pool reader retains a dense process id");
    }

    const auto clock_id = static_cast<ProcessId>(reader_count);
    Process clock;
    clock.id = clock_id;
    clock.name = "a3_pool_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions.reserve(reader_count);
    clock.operations.reserve(reader_count + 3U);
    clock.operations.push_back(WaitFor { 1U });
    clock.operations.push_back(
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) });
    for (const auto signal : trigger_signals) {
        clock.driver_regions.push_back({ signal, 0U, 0U, true });
        clock.operations.push_back(WriteBlocking { signal, 0U });
    }
    clock.operations.push_back(Halt { });
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "one clock callback wakes every independent component");

    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto process_id = static_cast<ProcessId>(index);
        const auto& registered = interpreter.process_program(process_id);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process_id };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process_id,
            [] { return true; },
            [&executor_probe, process_id, input = source_signals[index],
                output = output_signals[index], wait_instruction, binding] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process_id, input, output,
                    wait_instruction, binding, true, 0U);
            }, std::move(contract));
    }
    interpreter.materialize_ready_process_executors();
    interpreter.start();
    result.result = interpreter.run();
    require(result.result.status == RunStatus::completed,
        "all independent component callbacks drain after one clock wake");
    result.scheduler_stats
        = interpreter.scheduler().systemverilog_batch_compaction_stats();
    require(executor_probe.resumes[0U] == reader_count
            && executor_probe.pc_mismatches[0U] == 0U
            && result.reader_resume_order.size() == reader_count,
        "every checked reader executor resumes exactly once");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    std::vector<std::size_t> components;
    components.reserve(reader_count);
    const auto no_component = std::numeric_limits<std::size_t>::max();
    require(implementation.region_component_by_process.size()
                >= reader_count,
        "runtime component inventory covers every reader");
    if (grouped) {
        for (std::size_t index = 0U; index < reader_count; ++index) {
            const auto process_id = static_cast<ProcessId>(index);
            const auto component
                = implementation.region_component_by_process.at(process_id);
            require(component != no_component
                    && component
                        < implementation.region_activation_programs.size()
                    && implementation.region_activation_programs[component]
                    && implementation.region_activation_programs[component]
                               ->activation_kernel.members.size() == 1U
                    && implementation.region_activation_programs[component]
                               ->activation_kernel.members.front().process
                        == process_id,
                "each reader has its own certified singleton component");
            components.push_back(component);

            const auto& descriptor
                = implementation.region_grouped_fanout_by_signal.at(
                    trigger_signals[index]);
            require(descriptor.generation
                        == implementation.region_runtime_generation
                    && descriptor.group_count == 1U
                    && descriptor.group_offset
                        < implementation.region_grouped_fanout_groups.size(),
                "each private trigger has one certified grouped-fanout group");
            const auto& group
                = implementation.region_grouped_fanout_groups.at(
                    descriptor.group_offset);
            require(group.component == component && group.member_count == 1U
                    && group.member_offset
                        < implementation.region_grouped_fanout_members.size(),
                "each trigger group targets only its own component member");
            const auto& member
                = implementation.region_grouped_fanout_members.at(
                    group.member_offset);
            require(member.process == process_id
                    && member.readiness_member
                        == implementation.region_readiness_member_index_by_process
                               .at(process_id),
                "each ticket member retains its graph-certified readiness index");
            ++result.grouped_signal_count;
        }
        std::ranges::sort(components);
        const auto distinct = std::ranges::unique(components);
        components.erase(distinct.begin(), distinct.end());
        result.distinct_component_count = components.size();
    } else {
        require(std::ranges::all_of(
                    std::span { implementation.region_component_by_process }
                        .first(reader_count),
                    [](const std::size_t component) {
                        return component == no_component;
                    }),
            "the checked reference does not build grouped components");
    }

    const auto capture_signal = [&](const SignalId signal,
                                    const std::optional<ProcessId> owner) {
        GroupedFanoutSignalState state;
        state.current = interpreter.signal_value_snapshot(signal);
        state.last = implementation.signal_last_values.at(signal);
        state.stored = interpreter.stored_signal_value_snapshot(signal);
        if (owner) {
            state.driver = interpreter.driver_value(*owner, signal);
        }
        state.event = implementation.signal_events.at(signal);
        state.transaction = implementation.signal_transactions.at(signal);
        const auto& stamp
            = implementation.signal_event_scheduling_stamps.at(signal);
        state.event_domain = stamp.origin.process_domain;
        state.event_phase = stamp.origin.phase;
        state.event_round = stamp.systemverilog_round;
        return state;
    };
    for (std::size_t index = 0U; index < reader_count; ++index) {
        result.outputs[index] = capture_signal(output_signals[index],
            static_cast<ProcessId>(index));
        result.triggers[index] = capture_signal(trigger_signals[index],
            clock_id);
    }
    return result;
}

void test_automatic_readiness_pool_sizing_across_components()
{
    constexpr std::array<std::size_t, 2U> component_counts { 65U, 129U };
    for (const auto component_count : component_counts) {
        const auto ordinary = run_automatic_readiness_pool_case(
            false, component_count);
        const auto grouped = run_automatic_readiness_pool_case(
            true, component_count);
        require(grouped.distinct_component_count == component_count
                && grouped.grouped_signal_count == component_count,
            "runtime grouping covers every independent ready component");
        require(grouped.scheduler_stats.readiness_ticket_queue_insertions
                    == component_count
                && grouped.scheduler_stats.readiness_ticket_members
                    == component_count
                && grouped.scheduler_stats.readiness_ticket_members_elided
                    == 0U
                && grouped.scheduler_stats.readiness_ticket_fallback_members
                    == 0U
                && grouped.scheduler_stats.direct_dispatches == component_count
                && grouped.scheduler_stats.direct_members == component_count,
            "runtime topology sizing admits one physical ticket per component");
        require(ordinary.scheduler_stats.readiness_ticket_queue_insertions == 0U
                && ordinary.scheduler_stats.readiness_ticket_members == 0U
                && ordinary.scheduler_stats.readiness_ticket_fallback_members
                    == 0U,
            "the checked reference uses ordinary callbacks without readiness tickets");
        require(ordinary.result.status == grouped.result.status
                && ordinary.result.time == grouped.result.time
                && ordinary.result.delta == grouped.result.delta
                && ordinary.reader_resume_order
                    == grouped.reader_resume_order
                && ordinary.outputs == grouped.outputs
                && ordinary.triggers == grouped.triggers,
            "automatic ticket sizing preserves callback order and full signal roles");
        require(grouped.reader_resume_order.size() == component_count,
            "each independent group consumes one reader callback");
        for (std::size_t index = 0U; index < component_count; ++index) {
            require(grouped.reader_resume_order[index]
                        == "member" + std::to_string(index),
                "independent group callbacks retain their original process order");
            const auto expected = index % 2U == 0U
                ? PackedLogic4(1U, Logic4::zero)
                : PackedLogic4(1U, Logic4::one);
            require(grouped.outputs[index].current == expected
                    && grouped.outputs[index].driver == expected
                    && grouped.outputs[index].event.has_value()
                    && grouped.outputs[index].event->first == 1U
                    && grouped.outputs[index].transaction.has_value()
                    && grouped.outputs[index].transaction->first == 1U
                    && grouped.outputs[index].event_domain
                        == ProcessSchedulingDomain::systemverilog
                    && grouped.outputs[index].event_phase
                        == SchedulerPhase::active,
                "each reader publishes its matching scalar whole-signal result");
            require(grouped.triggers[index].current
                        == PackedLogic4(1U, Logic4::one)
                    && grouped.triggers[index].driver
                        == PackedLogic4(1U, Logic4::one)
                    && grouped.triggers[index].event.has_value()
                    && grouped.triggers[index].event->first == 1U
                    && grouped.triggers[index].transaction.has_value()
                    && grouped.triggers[index].transaction->first == 1U
                    && grouped.triggers[index].event_phase
                        == SchedulerPhase::active,
                "one clock callback publishes every independent trigger");
        }
        require(std::ranges::all_of(grouped.triggers,
                    [&](const GroupedFanoutSignalState& trigger) {
                        return trigger.event_round
                            == grouped.triggers.front().event_round;
                    })
                && std::ranges::all_of(grouped.outputs,
                    [&](const GroupedFanoutSignalState& output) {
                        return output.event_round
                            == grouped.outputs.front().event_round;
                    }),
            "all component wakes and outputs retain the same Active round");
    }
}

// This SimIR/runtime fixture uses deterministic checked executors; it proves
// scheduler readiness selection, not parsed-HDL or LLVM execution.
struct LargeSelectiveFanoutRun {
    RunResult first_cut_result;
    RunResult final_result;
    SchedulerBatchCompactionStats first_cut_stats;
    SchedulerBatchCompactionStats final_stats;
    std::vector<GroupedFanoutSignalState> baseline_outputs;
    std::vector<GroupedFanoutSignalState> first_cut_outputs;
    std::vector<GroupedFanoutSignalState> final_outputs;
    std::vector<std::string> first_cut_resume_order;
    std::vector<std::string> final_resume_order;
    std::vector<ProcessId> first_group_processes;
    std::vector<ProcessId> second_group_processes;
    std::vector<std::size_t> first_group_readiness_members;
    std::vector<std::size_t> second_group_readiness_members;
    std::size_t first_group_descriptor_offset { };
    std::size_t second_group_descriptor_offset { };
    std::vector<std::uint64_t> sampled_readiness_mask;
    std::size_t component_member_count { };
    std::size_t readiness_word_count { };
    std::uint64_t readiness_tickets_at_first_cut { };
    std::uint64_t readiness_members_at_first_cut { };
    std::uint64_t readiness_tickets_at_end { };
    std::uint64_t readiness_members_at_end { };
    bool readiness_mask_sampled { };
};

LargeSelectiveFanoutRun run_large_selective_fanout_case(
    const bool grouped)
{
    constexpr std::size_t first_group_count = 65U;
    constexpr std::size_t second_group_count = 64U;
    constexpr std::size_t reader_count
        = first_group_count + second_group_count;
    constexpr ProcessId first_reader_id = 0U;
    constexpr ProcessId last_reader_id
        = static_cast<ProcessId>(reader_count - 1U);
    constexpr ProcessId clock_id = last_reader_id + 1U;

    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", grouped ? "1" : "0" };
    ScopedEnvironment fanout_grouping {
        "FSIM_DISABLE_FANOUT_COHORT_GROUPING", nullptr };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", nullptr };

    LargeSelectiveFanoutRun result;
    result.baseline_outputs.resize(reader_count);
    result.first_cut_outputs.resize(reader_count);
    result.final_outputs.resize(reader_count);
    std::vector<std::uint64_t> expected_readiness_mask;
    bool readiness_mask_capture_armed { };

    RegionKernelProbe executor_probe;
    std::vector<std::string> resume_order;
    executor_probe.route_events = &resume_order;
    executor_probe.route_process_ids = true;
    Interpreter interpreter;
    const auto anchor = interpreter.add_signal({ "a3.selective_anchor",
        PackedLogic4(1U, Logic4::zero) });
    const auto first_trigger = interpreter.add_signal({
        "a3.selective_first_trigger", PackedLogic4(1U, Logic4::zero) });
    const auto second_trigger = interpreter.add_signal({
        "a3.selective_second_trigger", PackedLogic4(1U, Logic4::zero) });
    std::vector<SignalId> outputs;
    outputs.reserve(reader_count);
    for (std::size_t index = 0U; index < reader_count; ++index) {
        outputs.push_back(interpreter.add_signal({
            "a3.selective_output_" + std::to_string(index),
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire }));
    }

    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto process_id = static_cast<ProcessId>(index);
        const auto trigger = index < first_group_count
            ? first_trigger : second_trigger;
        Process reader;
        reader.id = process_id;
        reader.name = "a3_selective_reader_" + std::to_string(index);
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        reader.initialize = false;
        reader.register_count = 1U;
        reader.static_sensitivity = { { trigger, EdgeKind::any } };
        reader.driver_regions = { { outputs[index], 0U, 0U, true } };
        reader.operations = {
            ReadSignal { 0U, anchor },
            WriteUpdate { outputs[index], 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(reader)) == process_id,
            "selective fanout reader retains its dense scheduler process id");
    }

    Process clock;
    clock.id = clock_id;
    clock.name = "a3_selective_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { first_trigger, 0U, 0U, true },
        { second_trigger, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { first_trigger, 0U },
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { second_trigger, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "selective fanout clock remains outside the reader component");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    executor_probe.after_process_sample
        = [&](const ProcessId process, const PackedLogic4&) {
              if (!grouped || !readiness_mask_capture_armed
                  || result.readiness_mask_sampled
                  || process != first_reader_id) {
                  return;
              }
              const auto component
                  = implementation.region_component_by_process.at(process);
              const auto& descriptor
                  = implementation.region_readiness_mask_by_component.at(
                      component);
              if (descriptor.word_count
                      != result.sampled_readiness_mask.size()
                  || descriptor.offset
                      > implementation.region_readiness_mask_words.size()
                  || descriptor.word_count
                      > implementation.region_readiness_mask_words.size()
                          - descriptor.offset) {
                  return;
              }
              std::copy_n(
                  implementation.region_readiness_mask_words.begin()
                      + static_cast<std::ptrdiff_t>(descriptor.offset),
                  descriptor.word_count,
                  result.sampled_readiness_mask.begin());
              result.readiness_mask_sampled = true;
          };

    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto process_id = static_cast<ProcessId>(index);
        const auto& registered = interpreter.process_program(process_id);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process_id };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process_id,
            [] { return true; },
            [&executor_probe, process_id, anchor, output = outputs[index],
                wait_instruction, binding] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process_id, anchor, output,
                    wait_instruction, binding, true, 0U);
            }, std::move(contract));
    }
    interpreter.materialize_ready_process_executors();
    interpreter.start();
    const auto warmup = interpreter.run(0U);
    require(warmup.status == RunStatus::time_limit,
        "the selective fanout clock is waiting at time one after warmup");

    std::size_t component = std::numeric_limits<std::size_t>::max();
    if (grouped) {
        component = implementation.region_component_by_process.at(
            first_reader_id);
        require(component != std::numeric_limits<std::size_t>::max(),
            "all selective readers have an admitted activation component");
        const auto& program
            = implementation.region_activation_programs.at(component);
        require(program.has_value(),
            "the selective readers retain their activation program");
        const auto& kernel = program->activation_kernel;
        result.component_member_count = kernel.members.size();
        // All readers touch the shared read-only anchor; each also contributes
        // a single-whole output, which supplies structural SV internal state.
        require(result.component_member_count == reader_count,
            "the shared anchor connects exactly one 129-reader SV component");
        for (ProcessId process = first_reader_id;
             process <= last_reader_id; ++process) {
            require(implementation.region_component_by_process.at(process)
                    == component,
                "every reader maps to the same certified activation component");
        }
        const auto& readiness
            = implementation.region_readiness_mask_by_component.at(component);
        result.readiness_word_count = readiness.word_count;
        require(readiness.generation == implementation.region_runtime_generation
                && readiness.word_count >= 2U,
            "the shared activation component has multiple readiness words");

        const auto collect_group = [&](const SignalId signal,
                                       const std::size_t expected_count,
                                       const ProcessId first_process,
                                       std::vector<ProcessId>& process_ids,
                                       std::vector<std::size_t>& readiness_ids) {
            const auto& descriptor
                = implementation.region_grouped_fanout_by_signal.at(signal);
            require(descriptor.generation
                        == implementation.region_runtime_generation
                    && descriptor.group_count == 1U
                    && descriptor.group_offset
                        < implementation.region_grouped_fanout_groups.size(),
                "each clock phase has one certified signal-component fanout group");
            const auto& group
                = implementation.region_grouped_fanout_groups.at(
                    descriptor.group_offset);
            require(group.component == component
                    && group.member_count == expected_count
                    && group.member_offset
                        <= implementation.region_grouped_fanout_members.size()
                    && group.member_count
                        <= implementation.region_grouped_fanout_members.size()
                            - group.member_offset,
                "both selective signal groups map to the same full component");
            if (signal == first_trigger) {
                result.first_group_descriptor_offset
                    = descriptor.group_offset;
            } else {
                result.second_group_descriptor_offset
                    = descriptor.group_offset;
            }
            process_ids.reserve(expected_count);
            readiness_ids.reserve(expected_count);
            std::vector<std::uint64_t> group_words(readiness.word_count, 0U);
            for (std::size_t offset = 0U; offset < group.member_count; ++offset) {
                const auto& member
                    = implementation.region_grouped_fanout_members.at(
                        group.member_offset + offset);
                const auto expected_process = static_cast<ProcessId>(
                    first_process + static_cast<ProcessId>(offset));
                require(member.process == expected_process
                        && member.process
                            < implementation.region_readiness_member_index_by_process.size()
                        && implementation.region_component_by_process.at(
                            member.process) == component
                        && implementation.region_readiness_member_index_by_process.at(
                            member.process) == member.readiness_member
                        && std::ranges::find(readiness_ids,
                            member.readiness_member) == readiness_ids.end()
                        && member.readiness_member
                            < result.component_member_count
                        && member.readiness_member / 64U
                            < readiness.word_count,
                    "signal group members preserve graph-authenticated readiness indices");
                process_ids.push_back(member.process);
                readiness_ids.push_back(member.readiness_member);
                group_words[member.readiness_member / 64U]
                    |= UINT64_C(1) << (member.readiness_member % 64U);
            }
            if (signal == first_trigger) {
                expected_readiness_mask = std::move(group_words);
            }
        };
        collect_group(first_trigger, first_group_count, first_reader_id,
            result.first_group_processes,
            result.first_group_readiness_members);
        collect_group(second_trigger, second_group_count,
            first_reader_id + static_cast<ProcessId>(first_group_count),
            result.second_group_processes,
            result.second_group_readiness_members);
        for (const auto member : result.second_group_readiness_members) {
            require(std::ranges::find(result.first_group_readiness_members,
                        member)
                    == result.first_group_readiness_members.end(),
                "the two disjoint signal groups retain unique component readiness members");
        }
        const auto first_group_word_count = static_cast<std::size_t>(
            std::ranges::count_if(expected_readiness_mask,
                [](const std::uint64_t word) { return word != 0U; }));
        require(first_group_word_count >= 2,
            "the first 65-member wake set spans multiple readiness words");
        result.sampled_readiness_mask.resize(result.readiness_word_count);
        readiness_mask_capture_armed = true;
    }

    const auto capture_output = [&](const std::size_t index) {
        const auto signal = outputs[index];
        const auto owner = static_cast<ProcessId>(index);
        GroupedFanoutSignalState state;
        if (grouped) {
            const auto output_component
                = implementation.region_component_by_process.at(owner);
            require(output_component
                        < implementation.region_authoritative_state_by_component.size(),
                "each grouped output maps to an authoritative component");
            const auto* const authoritative
                = implementation.region_authoritative_state_by_component.at(
                    output_component).get();
            require(authoritative != nullptr && authoritative->valid()
                    && authoritative->generation()
                        == implementation.region_runtime_generation
                    && authoritative->values().valid(),
                "passive output snapshot reads the live authoritative roles");
            state.current = authoritative->values().current(signal);
            state.last = authoritative->values().previous(signal);
            state.stored = authoritative->values().stored(signal);
            state.driver = authoritative->values().owner_value(signal, owner);
        } else {
            state.current = interpreter.signal_value_snapshot(signal);
            state.last = implementation.signal_last_values.at(signal);
            state.stored = interpreter.stored_signal_value_snapshot(signal);
            state.driver = interpreter.driver_value(owner, signal);
        }
        state.event = implementation.signal_events.at(signal);
        state.transaction = implementation.signal_transactions.at(signal);
        const auto& stamp
            = implementation.signal_event_scheduling_stamps.at(signal);
        state.event_domain = stamp.origin.process_domain;
        state.event_phase = stamp.origin.phase;
        state.event_round = stamp.systemverilog_round;
        return state;
    };

    for (std::size_t index = 0U; index < reader_count; ++index) {
        result.baseline_outputs[index] = capture_output(index);
    }

    result.first_cut_result = interpreter.run(1U);
    require(result.first_cut_result.status == RunStatus::time_limit
            && result.first_cut_result.time == 1U,
        "the first selective wake reaches the time-one cut while time two remains queued");
    result.first_cut_resume_order = resume_order;
    result.first_cut_stats
        = interpreter.scheduler().systemverilog_batch_compaction_stats();
    result.readiness_tickets_at_first_cut
        = result.first_cut_stats.readiness_ticket_queue_insertions;
    result.readiness_members_at_first_cut
        = result.first_cut_stats.readiness_ticket_members;
    for (std::size_t index = 0U; index < reader_count; ++index) {
        result.first_cut_outputs[index] = capture_output(index);
    }
    if (grouped) {
        require(result.readiness_mask_sampled
                && result.sampled_readiness_mask == expected_readiness_mask,
            "the first grouped dispatch sees exactly the active 65-member readiness mask");
        for (const auto process : result.second_group_processes) {
            const auto& state = implementation.get_process(process);
            require(state.waiting_on_static && !state.queued,
                "the later cohort remains waiting and has no queued readiness key at time one");
        }
        const auto& readiness
            = implementation.region_readiness_mask_by_component.at(component);
        require(readiness.offset
                    <= implementation.region_readiness_mask_words.size()
                && result.readiness_word_count
                    <= implementation.region_readiness_mask_words.size()
                        - readiness.offset,
            "the passive readiness snapshot stays within the live component mask");
        require(std::ranges::all_of(
                    std::span<const std::uint64_t> {
                        implementation.region_readiness_mask_words }
                        .subspan(readiness.offset,
                            result.readiness_word_count),
                    [](const std::uint64_t word) { return word == 0U; }),
            "the first wake consumes only its readiness bits before the time-one cut");
    }

    result.final_result = interpreter.run();
    require(result.final_result.status == RunStatus::completed,
        "the second selective wake drains the remaining 64-member cohort");
    result.final_resume_order = resume_order;
    result.final_stats
        = interpreter.scheduler().systemverilog_batch_compaction_stats();
    result.readiness_tickets_at_end
        = result.final_stats.readiness_ticket_queue_insertions;
    result.readiness_members_at_end
        = result.final_stats.readiness_ticket_members;
    for (std::size_t index = 0U; index < reader_count; ++index) {
        result.final_outputs[index] = capture_output(index);
    }
    return result;
}

void test_large_grouped_fanout_selective_wakeup_crosses_readiness_words()
{
    constexpr std::size_t first_group_count = 65U;
    constexpr std::size_t reader_count = first_group_count + 64U;
    const auto ordinary = run_large_selective_fanout_case(false);
    const auto grouped = run_large_selective_fanout_case(true);

    require(grouped.component_member_count == reader_count
            && grouped.readiness_word_count >= 2U
            && grouped.first_group_descriptor_offset
                != grouped.second_group_descriptor_offset
            && grouped.first_group_processes.size() == first_group_count
            && grouped.second_group_processes.size() == 64U
            && grouped.first_group_readiness_members.size()
                == first_group_count
            && grouped.second_group_readiness_members.size() == 64U,
        "distinct fanout groups map disjoint readiness sets into one exact multiword component");
    require(grouped.readiness_tickets_at_first_cut == 1U
            && grouped.readiness_members_at_first_cut == first_group_count
            && grouped.readiness_tickets_at_end == 2U
            && grouped.readiness_members_at_end == reader_count
            && ordinary.readiness_tickets_at_first_cut == 0U
            && ordinary.readiness_members_at_first_cut == 0U
            && ordinary.readiness_tickets_at_end == 0U
            && ordinary.readiness_members_at_end == 0U,
        "each time cut queues only its own compact readiness group");

    std::vector<std::string> expected_first_order;
    expected_first_order.reserve(first_group_count);
    std::vector<std::string> expected_final_order;
    expected_final_order.reserve(reader_count);
    for (std::size_t index = 0U; index < reader_count; ++index) {
        const auto key = "member" + std::to_string(index);
        expected_final_order.push_back(key);
        if (index < first_group_count) {
            expected_first_order.push_back(key);
        }
    }
    require(grouped.first_cut_resume_order == expected_first_order
            && ordinary.first_cut_resume_order == expected_first_order
            && grouped.final_resume_order == expected_final_order
            && ordinary.final_resume_order == expected_final_order,
        "only the first selected cohort resumes at time one and every reader "
        "resumes once by time two");
    require(grouped.first_cut_result.status == ordinary.first_cut_result.status
            && grouped.first_cut_result.time == ordinary.first_cut_result.time
            && grouped.first_cut_result.delta == ordinary.first_cut_result.delta
            && grouped.final_result.status == ordinary.final_result.status
            && grouped.final_result.time == 2U
            && ordinary.final_result.time == 2U
            && grouped.final_result.time == ordinary.final_result.time
            && grouped.final_result.delta == ordinary.final_result.delta
            && grouped.first_cut_outputs == ordinary.first_cut_outputs
            && grouped.final_outputs == ordinary.final_outputs,
        "selective grouped wakeups preserve ordinary current/LAST/stored/raw/event state");
    for (std::size_t index = first_group_count;
         index < reader_count; ++index) {
        require(grouped.first_cut_outputs[index]
                    == grouped.baseline_outputs[index],
            "the inactive later cohort keeps every output role and event "
            "field unchanged at time one");
    }
    for (std::size_t index = 0U; index < first_group_count; ++index) {
        require(grouped.first_cut_outputs[index].current
                    == PackedLogic4(1U, Logic4::zero)
                && grouped.first_cut_outputs[index].driver
                    == PackedLogic4(1U, Logic4::zero),
            "the selected 65-member cohort publishes the common anchor at time one");
    }
    for (const auto& output : grouped.final_outputs) {
        require(output.current == PackedLogic4(1U, Logic4::zero)
                && output.driver == PackedLogic4(1U, Logic4::zero),
            "each selected reader publishes the shared anchor value exactly once");
    }
}

void test_grouped_fanout_interleaved_component_route()
{
    const auto ordinary = run_grouped_fanout_case(false);
    const auto grouped = run_grouped_fanout_case(true);
    const auto disabled = run_grouped_fanout_case(true, true);
    require(ordinary.result.status == grouped.result.status
            && ordinary.result.time == grouped.result.time
            && ordinary.result.delta == grouped.result.delta
            && ordinary.outputs == grouped.outputs
            && ordinary.shared_signals == grouped.shared_signals
            && ordinary.result.status == disabled.result.status
            && ordinary.result.time == disabled.result.time
            && ordinary.result.delta == disabled.result.delta
            && ordinary.outputs == disabled.outputs
            && ordinary.shared_signals == disabled.shared_signals,
        "grouped fanout preserves current, LAST, stored, raw-driver and event metadata against ordinary dispatch");
    require(ordinary.reader_resume_order == std::vector<std::string> {
                "member2", "member3", "member5", "member6" }
            && grouped.reader_resume_order == ordinary.reader_resume_order
            && disabled.reader_resume_order == ordinary.reader_resume_order,
        "interleaved checked singleton prefixes retain their original reader callbacks in key order");
    // Registering the foreign writer initializes each resolved wire driver
    // to Z. The early readers must see that undriven value before key 4
    // publishes its blocking assignments; the later readers must see one.
    require(grouped.outputs[0U].current == PackedLogic4(1U, Logic4::z)
            && grouped.outputs[1U].current == PackedLogic4(1U, Logic4::z)
            && grouped.outputs[2U].current == PackedLogic4(1U, Logic4::one)
            && grouped.outputs[3U].current == PackedLogic4(1U, Logic4::one),
        "the foreign blocking writer runs between the early and late members of both groups");
    require(region_profile_metric(grouped.profile,
                "p3_group_fanout_groups") >= 2U
            && region_profile_metric(grouped.profile,
                "p3_group_fanout_tickets") >= 2U
            && region_profile_metric(grouped.profile,
                "p3_group_fanout_members") >= 4U
            && grouped.scheduler_stats.tickets >= 2U
            && grouped.scheduler_stats.members >= 4U,
        "both certified signal-component fanouts create real ordered tickets");
    require(region_profile_metric(disabled.profile,
                "p3_group_fanout_groups") == 0U
            && region_profile_metric(disabled.profile,
                "p3_group_fanout_tickets") == 0U
            && region_profile_metric(disabled.profile,
                "p3_group_fanout_members") == 0U,
        "the fanout grouping off switch bypasses the P3 route");
}

void check_region_backend_pool_refresh(
    const bool provider_throws = false,
    const bool install_scheduler_trace = false,
    const RegionBackendProbe::FailurePoint failure_point
        = RegionBackendProbe::FailurePoint::none)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    RegionKernelProbe executor_probe;
    RegionBackendProbe backend_probe;
    std::size_t warm_attempts { };
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        const auto input = interpreter.add_signal({ "pool.input",
            PackedLogic4(1U, Logic4::one) });
        const auto internal = interpreter.add_signal({ "pool.internal",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto output = interpreter.add_signal({ "pool.output",
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });
        const auto trigger = interpreter.add_signal({ "pool.trigger",
            PackedLogic4(1U, Logic4::zero) });
        Process producer;
        producer.id = 0U;
        producer.name = "pool_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.initialize = false;
        producer.register_count = 1U;
        producer.static_sensitivity = { { trigger, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, input },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == 0U,
            "persistent backend producer keeps its stable ID");

        Process consumer;
        consumer.id = 1U;
        consumer.name = "pool_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.initialize = false;
        consumer.register_count = 1U;
        consumer.static_sensitivity = {
            { trigger, EdgeKind::any }, { internal, EdgeKind::any },
        };
        consumer.driver_regions = { { output, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, internal }, UnaryNot { 0U, 0U },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(consumer)) == 1U,
            "persistent backend consumer keeps its stable ID");

        for (const auto process_id : { ProcessId { 0U }, ProcessId { 1U } }) {
            const auto& registered = interpreter.process_program(process_id);
            const auto binding = ProcessExecutorProgramBinding {
                registered, registered, process_id };
            const auto wait_instruction = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            DeferredProcessExecutorContract contract;
            contract.expected_access = binding;
            contract.callbacks_observation_safe = true;
            contract.expected_region_kernel_equivalent = true;
            interpreter.set_deferred_process_executor(process_id,
                [] { return true; },
                [&executor_probe, process_id, binding,
                    wait_instruction, input, internal, output] {
                    const auto source
                        = process_id == 0U ? input : internal;
                    const auto destination
                        = process_id == 0U ? internal : output;
                    return std::make_unique<RegionKernelExecutor>(
                        executor_probe, process_id, source, destination,
                        wait_instruction, binding, true,
                        static_cast<std::size_t>(process_id));
                }, std::move(contract));
        }
        interpreter.materialize_ready_process_executors();

        Process clock;
        clock.id = 2U;
        clock.name = "pool_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.operations = { WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
            WriteBlocking { trigger, 0U }, WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { trigger, 0U }, WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
            WriteBlocking { trigger, 0U }, Halt { } };
        require(interpreter.add_process(std::move(clock)) == 2U,
            "persistent backend fixture clock has its stable ID");
        interpreter.set_region_kernel_backend_provider(
            std::make_shared<RecordingRegionBackendProvider>(
                backend_probe, provider_throws));
        interpreter.schedule_signal_at(
            trigger, PackedLogic4(1U, Logic4::one), 0U, 0U);
        if (install_scheduler_trace) {
            interpreter.scheduler().set_trace_hook(
                nullptr, &discard_scheduler_trace);
        }

        interpreter.start();
        require(interpreter.scheduler().trace_hook_installed()
                == install_scheduler_trace,
            "the trace-decline witness installs tracing only when requested");
        require(backend_probe.factories == 1U,
            "startup consults the provider once for the exact region mapping");
        const auto startup = interpreter.run(0U);
        require(startup.status == RunStatus::time_limit,
            "backend members reach their canonical wait PCs before the first trigger");
        warm_attempts = backend_probe.attempts;
        if (provider_throws) {
            require(interpreter.run().status == RunStatus::completed,
                "optional backend creation failure retains checked execution");
            require(backend_probe.attempts == 0U,
                "a throwing provider does not leave a partial backend installed");
        } else if (install_scheduler_trace) {
            require(interpreter.run().status == RunStatus::completed,
                "the traced activation completes through the checked path");
            require(backend_probe.attempts == 0U
                    && backend_probe.plane_input_dispatches == 0U,
                "an installed trace observer blocks native plane dispatch");
        } else {
            if (failure_point
                != RegionBackendProbe::FailurePoint::none) {
                backend_probe.failure_point = failure_point;
                backend_probe.armed_failure = std::make_exception_ptr(
                    std::runtime_error(
                        "test-only region backend entry failure"));
            }
            const auto before_failure_plane_dispatches
                = backend_probe.plane_input_dispatches;
            const auto before_failure_legacy_dispatches
                = backend_probe.legacy_input_dispatches;
            const auto before_failure_attempts = backend_probe.attempts;
            const auto before_failure_takes
                = backend_probe.failure_channel_polls;
            auto& before_failure_impl
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            const auto before_failure_internal_roles
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    interpreter, internal, 0U);
            const auto before_failure_output_roles
                = OwnedDriverDemotionTestAccess::packed_a4_values(
                    interpreter, output, 1U);
            const auto before_failure_internal_transaction
                = before_failure_impl.signal_transactions[internal];
            const auto before_failure_output_transaction
                = before_failure_impl.signal_transactions[output];
            const auto before_failure_internal_event
                = before_failure_impl.signal_events[internal];
            const auto before_failure_output_event
                = before_failure_impl.signal_events[output];
            const auto before_failure_internal_stamp
                = before_failure_impl.signal_event_scheduling_stamps[internal];
            const auto before_failure_output_stamp
                = before_failure_impl.signal_event_scheduling_stamps[output];
            const auto before_failure_executor_resumes
                = executor_probe.resumes;
            const auto before_failure_completion_stages
                = executor_probe.completion_stages;
            const auto before_failure_completion_commits
                = executor_probe.completion_commits;
            bool failure_was_rethrown { };
            std::optional<RunResult> first;
            try {
                first = interpreter.run(1U);
            } catch (const std::runtime_error& error) {
                failure_was_rethrown
                    = std::string_view(error.what())
                    == "test-only region backend entry failure";
                if (!failure_was_rethrown) {
                    throw;
                }
            }
            if (failure_point
                != RegionBackendProbe::FailurePoint::none) {
                require(failure_was_rethrown && !first.has_value(),
                    "the backend exception is propagated instead of "
                    "being converted to a decline");
                require(backend_probe.plane_input_dispatches
                            == before_failure_plane_dispatches + 1U
                        && backend_probe.legacy_input_dispatches
                            == before_failure_legacy_dispatches
                                + (failure_point
                                        == RegionBackendProbe::FailurePoint::legacy_input_planes
                                    ? 1U : 0U)
                        && backend_probe.attempts == before_failure_attempts
                        && backend_probe.failure_channel_polls
                            == before_failure_takes
                                + (failure_point
                                        == RegionBackendProbe::FailurePoint::legacy_input_planes
                                    ? 2U : 1U),
                    "a failed plane entry is polled once and suppresses all later backend entries");
                require(OwnedDriverDemotionTestAccess::packed_a4_values(
                            interpreter, internal, 0U)
                            == before_failure_internal_roles
                        && OwnedDriverDemotionTestAccess::packed_a4_values(
                            interpreter, output, 1U)
                            == before_failure_output_roles
                        && before_failure_impl.signal_transactions[internal]
                            == before_failure_internal_transaction
                        && before_failure_impl.signal_transactions[output]
                            == before_failure_output_transaction
                        && before_failure_impl.signal_events[internal]
                            == before_failure_internal_event
                        && before_failure_impl.signal_events[output]
                            == before_failure_output_event
                        && before_failure_impl.signal_event_scheduling_stamps[internal]
                                .origin.process_domain
                            == before_failure_internal_stamp.origin.process_domain
                        && before_failure_impl.signal_event_scheduling_stamps[internal]
                                .origin.phase
                            == before_failure_internal_stamp.origin.phase
                        && before_failure_impl.signal_event_scheduling_stamps[internal]
                                .systemverilog_round
                            == before_failure_internal_stamp.systemverilog_round
                        && before_failure_impl.signal_event_scheduling_stamps[output]
                                .origin.process_domain
                            == before_failure_output_stamp.origin.process_domain
                        && before_failure_impl.signal_event_scheduling_stamps[output]
                                .origin.phase
                            == before_failure_output_stamp.origin.phase
                        && before_failure_impl.signal_event_scheduling_stamps[output]
                                .systemverilog_round
                            == before_failure_output_stamp.systemverilog_round
                        && executor_probe.resumes
                            == before_failure_executor_resumes
                        && executor_probe.completion_stages
                            == before_failure_completion_stages
                        && executor_probe.completion_commits
                            == before_failure_completion_commits,
                    "the failed native attempt leaves signal roles, metadata, "
                    "and checked executors untouched");
                require(backend_probe.failed_prefix_captured
                        && !backend_probe.pending_failure
                        && !backend_probe.armed_failure,
                    "polling consumes the one-shot backend exception");
                const auto retry = interpreter.run(1U);
                require(retry.status == RunStatus::time_limit,
                    "the original scheduler prefix resumes after the one-shot backend failure");
                require(backend_probe.attempts == before_failure_attempts + 1U
                        && backend_probe.retried_prefix_captured
                        && backend_probe.retry_prefix_matches_failed_prefix
                        && backend_probe.failure_channel_polls
                            == before_failure_takes
                                + (failure_point
                                        == RegionBackendProbe::FailurePoint::legacy_input_planes
                                    ? 5U : 4U)
                        && !backend_probe.pending_failure,
                    "retry reaches the ordinary image decline with an empty failure channel");
                require(before_failure_impl.logical_signal_value(internal)
                            == PackedLogic4(1U, Logic4::one)
                        && before_failure_impl.logical_signal_value(output)
                            == PackedLogic4(1U, Logic4::zero),
                    "the checked component retry publishes each original member once");
            } else {
                require(first.has_value()
                        && first->status == RunStatus::time_limit,
                    "ordinary native decline retains checked execution");
            }
            if (failure_point
                == RegionBackendProbe::FailurePoint::none) {
                require(backend_probe.attempts == warm_attempts + 1U,
                    "the first warmed activation consults the pooled backend");
            }
            require(backend_probe.plane_input_dispatches != 0U
                    && backend_probe.legacy_input_dispatches != 0U
                    && backend_probe.plane_input_views_valid
                    && backend_probe.legacy_input_views_valid
                    && backend_probe.readiness_views_valid,
                "a valid current-generation component plane reaches the backend");
            const auto plane_dispatches_before_observation
                = backend_probe.plane_input_dispatches;
            interpreter.prepare_signal_observation(internal);
            require(backend_probe.factories == 1U,
                "quiet refresh rebinds from the persistent pool without calling the provider");
            const auto second = interpreter.run(2U);
            require(second.status == RunStatus::time_limit
                    && backend_probe.attempts == warm_attempts + 1U
                    && backend_probe.plane_input_dispatches
                        == plane_dispatches_before_observation,
                "the observed activation uses checked inputs while the component is stale");
            require(interpreter.run().status == RunStatus::completed,
                "same-image C++ fallback completes after quiet refresh");
            require(backend_probe.attempts == warm_attempts + 2U,
                "the exact pooled backend is reused for the later activation");
            require(backend_probe.plane_input_dispatches
                    > plane_dispatches_before_observation
                    && backend_probe.plane_input_views_valid
                    && backend_probe.readiness_views_valid,
                "quiet recertification restores validated plane dispatch");
        }
        require(interpreter.signal_value(internal)
                    == PackedLogic4(1U, Logic4::one)
                && interpreter.signal_value(output)
                    == PackedLogic4(1U, Logic4::zero),
            "optional backend construction/decline preserves checked results");
    }
    const auto output = captured.str();
    const auto profile_start = output.find(
        "fsim-profile: sv-ordered-wave-summary ");
    require(profile_start != std::string::npos,
        "persistent backend test emits its focused route counters");
    const auto profile_end = output.find('\n', profile_start);
    const auto profile = output.substr(profile_start,
        profile_end == std::string::npos
            ? std::string::npos : profile_end - profile_start);
    require(region_profile_metric(profile, "region_backend_attempts")
                == ((provider_throws || install_scheduler_trace)
                        ? 0U : warm_attempts + 2U
                            + (failure_point
                                    == RegionBackendProbe::FailurePoint::none
                                ? 0U : 1U))
            && region_profile_metric(profile, "region_backend_runs") == 0U
            && region_profile_metric(profile, "region_backend_completions") == 0U,
        "optional backend attempts are counted separately from checked completions");
    if (!provider_throws && !install_scheduler_trace) {
        require(region_profile_metric(profile, "region_recert_successes") != 0U,
            "the observed region is recertified before pooled backend reuse");
        require(region_profile_metric(profile, "a4_native_input_handoffs")
                    == backend_probe.plane_input_dispatches
                        + backend_probe.legacy_input_dispatches,
            "the profile records every borrowed-plane backend handoff");
    }
    if (install_scheduler_trace) {
        require(region_profile_metric(profile, "region_trace_declines") != 0U
                && region_profile_metric(profile, "a4_native_input_handoffs") == 0U,
            "trace observation declines the native plane route before entry");
    }
    if (provider_throws) {
        require(region_profile_metric(profile, "region_kernel_runs") != 0U,
            "provider construction failure uses the checked activation evaluator");
    }
}

void check_local_wave_runtime(
    const bool observe_after_first,
    const bool decline_first_activation = false,
    const bool use_default = false,
    const bool stateless_completion = false,
    const bool retain_unused_register = false,
    const bool partial_branch = false,
    const bool decline_after_success = false,
    const bool prepared_output_prefix = false,
    const bool decline_direct_ready_first = false,
    const std::optional<std::uint32_t> partial_output_offset = std::nullopt,
    LocalWavePartialPublicationRun* const partial_output_run = nullptr)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", use_default ? nullptr : "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    LocalWaveBackendProbe backend_probe;
    require(!partial_output_offset
            || (!partial_branch && !prepared_output_prefix),
        "partial boundary output witness uses the direct activation path");
    require(partial_output_run == nullptr || partial_output_offset.has_value(),
        "partial boundary capture is used only for a sliced output");
    backend_probe.decline_first_activation = decline_first_activation;
    backend_probe.execute_prepared_output_prefix = prepared_output_prefix;
    backend_probe.execute_direct_ready_window = decline_direct_ready_first;
    backend_probe.decline_first_direct_ready_window
        = decline_direct_ready_first;
    RegionKernelProbe executor_probe;
    std::uint64_t startup_ordinary_internal_updates = 0U;
    SignalId internal_profile_signal { };
    std::optional<SignalId> internal_second_profile_signal;
    SignalId output_profile_signal { };
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        const auto input = interpreter.add_signal({ "local.input",
            PackedLogic4(1U, Logic4::z) });
        const auto internal = interpreter.add_signal({ "local.internal",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto internal_second = prepared_output_prefix
            ? std::optional<SignalId> { interpreter.add_signal({
                  "local.internal_second",
                  PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire }) }
            : std::nullopt;
        internal_profile_signal = internal;
        internal_second_profile_signal = internal_second;
        backend_probe.reader_observed_signal = internal_second;
        backend_probe.reader_process = 1U;
        const auto output_width = partial_output_offset ? 65U : 1U;
        const auto output = interpreter.add_signal({ "local.output",
            PackedLogic4(output_width, Logic4::x), ResolutionKind::sv_wire });
        output_profile_signal = output;
        const auto trigger = interpreter.add_signal({ "local.trigger",
            PackedLogic4(1U, Logic4::zero) });

        Process producer;
        producer.id = 0U;
        producer.name = "local_wave_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.initialize = true;
        producer.register_count
            = (retain_unused_register || partial_branch) ? 2U : 1U;
        producer.static_sensitivity = { { trigger, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        if (partial_branch) {
            producer.operations = {
                LoadConstant { 1U, PackedLogic4(1U, Logic4::one) },
                Branch { 1U, 2U, 4U },
                ReadSignal { 0U, input },
                WriteUpdate { internal, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        } else if (internal_second) {
            producer.driver_regions.push_back(
                { *internal_second, 0U, 0U, true });
            producer.operations = {
                ReadSignal { 0U, input },
                WriteUpdate { internal, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WriteUpdate { *internal_second, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        } else {
            producer.operations = {
                ReadSignal { 0U, input },
                WriteUpdate { internal, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        }
        require(interpreter.add_process(std::move(producer)) == 0U,
            "local-wave producer keeps its original process identity");

        Process consumer;
        consumer.id = 1U;
        consumer.name = "local_wave_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.initialize = true;
        consumer.register_count = prepared_output_prefix
            ? 3U : retain_unused_register ? 2U : 1U;
        consumer.static_sensitivity = {
            { trigger, EdgeKind::any }, { internal, EdgeKind::any },
        };
        if (internal_second) {
            consumer.static_sensitivity.push_back(
                { *internal_second, EdgeKind::any });
        }
        if (partial_output_offset) {
            consumer.driver_regions = { { output,
                *partial_output_offset, 1U, false } };
        } else {
            consumer.driver_regions = { { output, 0U, 0U, true } };
        }
        if (prepared_output_prefix && internal_second) {
            consumer.operations = {
                ReadSignal { 0U, internal },
                ReadSignal { 1U, *internal_second },
                LoadConstant { 2U, PackedLogic4(1U, Logic4::zero) },
                Binary { BinaryOperator::bit_and, 2U, 1U, 2U },
                Binary { BinaryOperator::bit_or, 0U, 0U, 2U },
                UnaryNot { 0U, 0U },
                WriteUpdate { output, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        } else if (partial_output_offset) {
            consumer.operations = {
                ReadSignal { 0U, internal }, UnaryNot { 0U, 0U },
                WriteUpdateSlice { output, 0U, *partial_output_offset,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        } else {
            consumer.operations = {
                ReadSignal { 0U, internal }, UnaryNot { 0U, 0U },
                WriteUpdate { output, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { }, Jump { 0U },
            };
        }
        require(interpreter.add_process(std::move(consumer)) == 1U,
            "local-wave consumer keeps its original process identity");

        for (const auto process_id : { ProcessId { 0U }, ProcessId { 1U } }) {
            const auto& registered = interpreter.process_program(process_id);
            const auto binding = ProcessExecutorProgramBinding {
                registered, registered, process_id };
            const auto wait_instruction = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            DeferredProcessExecutorContract contract;
            contract.expected_access = binding;
            contract.callbacks_observation_safe = true;
            contract.expected_region_kernel_equivalent = true;
            interpreter.set_deferred_process_executor(process_id,
                [] { return true; },
                [&executor_probe, process_id, binding, wait_instruction,
                    input, internal, internal_second, output,
                    stateless_completion, retain_unused_register,
                    partial_branch, prepared_output_prefix,
                    partial_output_offset] {
                    const auto source
                        = process_id == 0U ? input : internal;
                    const auto destination
                        = process_id == 0U ? internal : output;
                    return std::make_unique<RegionKernelExecutor>(
                        executor_probe, process_id, source, destination,
                        wait_instruction, binding, true,
                        static_cast<std::size_t>(process_id), true,
                        stateless_completion,
                        (retain_unused_register
                            || (partial_branch && process_id == 0U))
                            ? 2U
                            : process_id == 1U && prepared_output_prefix
                            ? 3U : 1U,
                        process_id == 0U ? internal_second : std::nullopt,
                        process_id == 1U && prepared_output_prefix
                            ? internal_second : std::nullopt,
                        process_id == 1U && prepared_output_prefix,
                        std::nullopt, false, nullptr, 1U, std::nullopt, false,
                        process_id == 1U
                            ? partial_output_offset : std::nullopt);
                }, std::move(contract));
        }
        interpreter.materialize_ready_process_executors();

        Process clock;
        clock.id = 2U;
        clock.name = "local_wave_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.driver_regions = { { input, 0U, 0U, true },
            { trigger, 0U, 0U, true } };
        clock.operations = { WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { input, 0U },
            WriteBlocking { trigger, 0U }, WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
            WriteBlocking { input, 0U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
            WriteBlocking { trigger, 0U }, WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { trigger, 0U }, WaitFor { 1U }, Halt { } };
        require(interpreter.add_process(std::move(clock)) == 2U,
            "local-wave clock remains outside the component");
        interpreter.set_region_kernel_backend_provider(
            std::make_shared<AcceptingRegionBackendProvider>(backend_probe));

        interpreter.start();
        require(interpreter.run(0U).status == RunStatus::time_limit,
            "local-wave members reach their static wait before the measured clock edge");
        startup_ordinary_internal_updates
            = OwnedDriverDemotionTestAccess::implementation(interpreter)
                  .systemverilog_wave_profile_a2_ordinary_internal_updates;
        if (partial_output_offset) {
            const auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            const auto component
                = implementation.region_component_by_process.at(1U);
            require(component
                        < implementation.region_activation_programs.size()
                    && implementation.region_activation_programs[component],
                "the partial output belongs to a prepared local-wave component");
            const auto& program
                = *implementation.region_activation_programs[component];
            const auto output_binding = std::ranges::find(
                program.boundary_outputs, output,
                &RegionConeOutputBinding::signal);
            require(output_binding != program.boundary_outputs.end()
                    && output_binding->owner == 1U
                    && output_binding->offset == *partial_output_offset
                    && output_binding->width == 1U
                    && output_binding->signal_width == output_width
                    && output_binding->source_instruction == 2U
                    && output_binding->domain
                        == SignalUpdateDomain::systemverilog_active
                    && output_binding->update_kind
                        == RegionUpdateKind::systemverilog_active
                    && std::ranges::find(program.internal_materializations,
                           output, &RegionConeOutputBinding::signal)
                        == program.internal_materializations.end()
                    && std::ranges::find(
                           program.activation_kernel.internal_signals,
                           output)
                        == program.activation_kernel.internal_signals.end(),
                "the local-wave binding retains the original partial boundary coordinates and full signal width");
            const auto registered = interpreter.process_program(1U);
            const auto source_operation
                = registered.operations.expanded(
                    output_binding->source_instruction);
            const auto* const source_slice
                = operation_get_if<WriteUpdateSlice>(&source_operation);
            require(source_slice != nullptr
                    && source_slice->signal == output
                    && source_slice->source == 0U
                    && source_slice->offset == *partial_output_offset
                    && source_slice->domain
                        == SignalUpdateDomain::systemverilog_active,
                "the activation keeps the exact original sliced write instruction");
        }
        if (partial_branch) {
            require(interpreter.run().status == RunStatus::completed,
                "a path-conditional member completes through ordinary execution");
            const auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            const bool producer_has_native_kernel = std::ranges::any_of(
                implementation.region_activation_programs,
                [](const auto& program) {
                    return program && std::ranges::any_of(
                        program->activation_kernel.members,
                        [](const auto& member) { return member.process == 0U; });
                });
            require(!producer_has_native_kernel
                    && !implementation.processes[0U]
                            .region_kernel_completion_boundary_validated,
                "the path-conditional producer has no native activation or "
                "cached stateless completion boundary");
            require(interpreter.signal_value(internal)
                        == PackedLogic4(1U, Logic4::zero)
                    && interpreter.signal_value(output)
                        == PackedLogic4(1U, Logic4::one),
                "the partial-definition branch keeps its ordinary signal results");
        } else if (decline_after_success) {
            require(interpreter.run(1U).status == RunStatus::time_limit,
                "the initial native route reaches its first quiet point");
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            require(implementation.processes[0U]
                            .region_kernel_completion_boundary_validated
                    && implementation.processes[1U]
                            .region_kernel_completion_boundary_validated,
                "successful native completion caches both process boundaries");
            const auto first_preflights
                = implementation.systemverilog_wave_profile_a2_completion_preflights;
            backend_probe.decline_next_activation = true;
            require(interpreter.run(2U).status == RunStatus::time_limit,
                "the declined native activation completes via ordinary resume");
            const bool both_boundaries_cached
                = implementation.processes[0U]
                        .region_kernel_completion_boundary_validated
                && implementation.processes[1U]
                        .region_kernel_completion_boundary_validated;
            const bool fallback_was_revalidated
                = implementation.systemverilog_wave_profile_a2_completion_preflights
                    > first_preflights;
            require(backend_probe.on_demand_activation_declined
                    && (!both_boundaries_cached || fallback_was_revalidated),
                "ordinary cohort fallback invalidates cached boundaries before re-promotion");
            require(interpreter.run(3U).status == RunStatus::time_limit,
                "a later activation reaches the native re-promotion point");
            require(implementation.systemverilog_wave_profile_a2_completion_preflights
                        > first_preflights
                    && implementation.processes[0U]
                            .region_kernel_completion_boundary_validated
                    && implementation.processes[1U]
                            .region_kernel_completion_boundary_validated,
                "fallback frames are revalidated before stateless completion resumes");
            require(interpreter.run().status == RunStatus::completed,
                "the fallback and re-promoted process pair completes cleanly");
        } else if (observe_after_first) {
            require(interpreter.run(1U).status == RunStatus::time_limit,
                "the first local-wave activation reaches its quiet time");
            const auto component
                = OwnedDriverDemotionTestAccess::implementation(interpreter)
                      .region_authoritative_component_by_signal.at(internal);
            interpreter.prepare_signal_observation(internal);
            const auto& stale = OwnedDriverDemotionTestAccess::implementation(
                interpreter);
            require(!stale.region_graph->component_epochs_current(component)
                    && !stale.region_authoritative_state_by_component
                            .at(component)->values().packed_slots_bound(),
                "late observation revokes the previous local plane certificate");
            require(interpreter.run().status == RunStatus::completed,
                "late observation reaches a quiet recertification point");
            const auto& refreshed = OwnedDriverDemotionTestAccess::implementation(
                interpreter);
            require(refreshed.systemverilog_wave_profile_region_recertification_successes
                        != 0U
                    && backend_probe.plane_dispatches != 0U,
                "a later native handoff uses a refreshed local plane certificate");
        } else {
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            std::uint64_t local_generation_before_first_wave { };
            std::uint64_t authoritative_generation_before_first_wave { };
            std::uint64_t runtime_generation_before_first_wave { };
            std::uint64_t recertification_attempts_before_first_wave { };
            std::uint64_t recertification_successes_before_first_wave { };
            std::uint64_t slot_bind_components_before_first_wave { };
            std::uint64_t slot_bindings_before_first_wave { };
            std::size_t component_before_first_wave { };
            std::weak_ptr<const void> initial_local_state_identity;
            std::weak_ptr<const void> initial_authoritative_state_identity;
            {
                const auto component
                    = implementation.region_authoritative_component_by_signal
                          .at(internal);
                require(component
                            < implementation.region_local_wave_state_by_component
                                  .size()
                        && component
                            < implementation.region_authoritative_state_by_component
                                  .size(),
                    "the initial signal has component-owned local and A4 state");
                const auto initial_local_state
                    = implementation.region_local_wave_state_by_component
                          .at(component);
                const auto initial_authoritative_state
                    = implementation.region_authoritative_state_by_component
                          .at(component);
                require(initial_local_state && initial_authoritative_state,
                    "the local register bank is component-owned beside A4 state");
                component_before_first_wave = component;
                local_generation_before_first_wave
                    = initial_local_state->generation;
                authoritative_generation_before_first_wave
                    = initial_authoritative_state->generation();
                runtime_generation_before_first_wave
                    = implementation.region_runtime_generation;
                recertification_attempts_before_first_wave
                    = implementation
                          .systemverilog_wave_profile_region_recertification_attempts;
                recertification_successes_before_first_wave
                    = implementation
                          .systemverilog_wave_profile_region_recertification_successes;
                slot_bind_components_before_first_wave
                    = implementation.systemverilog_wave_profile_a4_slot_bind_components;
                slot_bindings_before_first_wave
                    = implementation.systemverilog_wave_profile_a4_slot_bindings;
                initial_local_state_identity = initial_local_state;
                initial_authoritative_state_identity = initial_authoritative_state;
            }
            std::size_t inspected_state_count { };
            const auto inspect_internal_state = [&](const SignalId signal,
                const PackedLogic4& expected_current,
                const PackedLogic4& expected_previous) {
                const auto component
                    = implementation.region_authoritative_component_by_signal
                          .at(signal);
                require(component
                            < implementation.region_local_wave_state_by_component
                                  .size()
                        && component
                            < implementation.region_authoritative_state_by_component
                                  .size(),
                    "the inspected signal retains its component state slots");
                // Quiet-point recertification can replace either component
                // vector while run() is in progress. Copy the current shared
                // owners for each inspection instead of retaining references
                // to vector elements across a run boundary.
                const auto local_state
                    = implementation.region_local_wave_state_by_component
                          .at(component);
                const auto authoritative_state
                    = implementation.region_authoritative_state_by_component
                          .at(component);
                require(local_state && authoritative_state,
                    "the local register bank is component-owned beside A4 state");
                const auto& values = authoritative_state->values();
                if (!local_state->seeded) {
                    const auto initial_local_state
                        = initial_local_state_identity.lock();
                    const auto initial_authoritative_state
                        = initial_authoritative_state_identity.lock();
                    require(decline_first_activation
                            && inspected_state_count == 0U
                            && component == component_before_first_wave
                            && initial_local_state
                            && initial_local_state.get()
                                == static_cast<const void*>(local_state.get())
                            && initial_authoritative_state
                            && initial_authoritative_state.get()
                                == static_cast<const void*>(
                                    authoritative_state.get())
                            && implementation.region_runtime_generation
                                == runtime_generation_before_first_wave
                            && local_state->generation
                                == local_generation_before_first_wave
                            && authoritative_state->generation()
                                == authoritative_generation_before_first_wave
                            && local_state->generation
                                == implementation.region_runtime_generation
                            && authoritative_state->generation()
                                == implementation.region_runtime_generation
                            && authoritative_state->generation()
                                == local_state->generation
                            && authoritative_state->valid()
                            && implementation.region_graph
                            && implementation.region_graph
                                ->component_epochs_current(component)
                            && values.requires_prewrite_unbind()
                            && values.packed_slots_bound()
                            && values.packed_signal_slots_bound(signal)
                            && values.packed_owner_slot_bound(
                                signal, ProcessId { 0U })
                            && local_state->authoritative_revision
                                == values.revision()
                            && implementation
                                    .systemverilog_wave_profile_region_recertification_attempts
                                > recertification_attempts_before_first_wave
                            && implementation
                                    .systemverilog_wave_profile_region_recertification_successes
                                > recertification_successes_before_first_wave
                            && implementation
                                    .systemverilog_wave_profile_a4_slot_bind_components
                                > slot_bind_components_before_first_wave
                            && implementation
                                    .systemverilog_wave_profile_a4_slot_bindings
                                > slot_bindings_before_first_wave
                            && values.current(signal) == expected_current
                            && values.previous(signal) == expected_previous
                            && values.stored(signal) == expected_current
                            && values.owner_value(signal, ProcessId { 0U })
                                == expected_current,
                    "a declined first activation retains exact roles in the "
                        "same A4/local states after quiet value-only rebind");
                    ++inspected_state_count;
                    return;
                }
                require(local_state->seeded,
                    "a committed private publication keeps the bank seeded");
                const auto& local
                    = local_state->activation.internal_state(signal);
                require(local.current == expected_current
                        && local.previous == expected_previous
                        && local.raw_driver == expected_current
                        && values.current(signal) == local.current
                        && values.previous(signal) == local.previous
                        && values.stored(signal) == local.current
                        && values.owner_value(signal, local.owner)
                            == local.raw_driver,
                    "persistent registers match A4 current, LAST, and raw owner state");
                ++inspected_state_count;
            };

            require(interpreter.run(1U).status == RunStatus::time_limit,
                "the first local wave reaches its quiet time");
            inspect_internal_state(internal, PackedLogic4(1U, Logic4::one),
                PackedLogic4(1U, Logic4::z));
            if (decline_direct_ready_first) {
                require(backend_probe.direct_ready_window_declined
                        && backend_probe.direct_ready_window_calls != 0U
                        && backend_probe.direct_ready_signal_input_count_at_decline
                            != 0U
                        && backend_probe.direct_ready_fallback_comparisons == 1U
                        && backend_probe.direct_ready_fallback_image_matches
                        && !backend_probe.direct_ready_fallback_pending
                        && backend_probe.direct_ready_prepared_calls_at_decline
                            == 0U
                        && backend_probe.direct_ready_slots_at_decline == 0U,
                    "declined direct-ready execution preserves the captured "
                    "input image and leaves outputs unstaged");
            }
            if (internal_second) {
                inspect_internal_state(*internal_second,
                    PackedLogic4(1U, Logic4::one),
                    PackedLogic4(1U, Logic4::z));
            }
            require(interpreter.run(2U).status == RunStatus::time_limit,
                "a changed boundary input reaches a later local wave");
            inspect_internal_state(internal, PackedLogic4(1U, Logic4::zero),
                PackedLogic4(1U, Logic4::one));
            if (internal_second) {
                inspect_internal_state(*internal_second,
                    PackedLogic4(1U, Logic4::zero),
                    PackedLogic4(1U, Logic4::one));
            }
            const auto previous_at_equal_update
                = implementation.signal_last_values[internal];
            const auto event_at_equal_update
                = implementation.signal_events[internal];
            const auto event_stamp_at_equal_update
                = implementation.signal_event_scheduling_stamps[internal];
            const auto transaction_before_equal_update
                = implementation.signal_transactions[internal];
            const auto prepared_calls_before_equal_update
                = backend_probe.prepared_output_calls;
            const auto prepared_slots_before_equal_update
                = backend_probe.prepared_output_slots_written;
            require(interpreter.run(3U).status == RunStatus::time_limit,
                "an equal internal publication reaches a following wave");
            inspect_internal_state(internal,
                PackedLogic4(1U, Logic4::zero),
                PackedLogic4(1U, Logic4::one));
            if (internal_second) {
                inspect_internal_state(*internal_second,
                    PackedLogic4(1U, Logic4::zero),
                    PackedLogic4(1U, Logic4::one));
            }
            require(implementation.signal_last_values[internal]
                        == previous_at_equal_update
                    && implementation.signal_events[internal]
                        == event_at_equal_update
                    && implementation.signal_event_scheduling_stamps[internal]
                            .origin.process_domain
                        == event_stamp_at_equal_update.origin.process_domain
                    && implementation.signal_event_scheduling_stamps[internal]
                            .origin.phase
                        == event_stamp_at_equal_update.origin.phase
                    && implementation.signal_event_scheduling_stamps[internal]
                            .systemverilog_round
                        == event_stamp_at_equal_update.systemverilog_round
                    && implementation.signal_transactions[internal]
                        != transaction_before_equal_update,
                "an equal commit advances transaction time but preserves LAST and event metadata");
            if (prepared_output_prefix) {
                require(backend_probe.prepared_output_calls
                            == prepared_calls_before_equal_update + 1U
                        && backend_probe.prepared_output_slots_written
                            == prepared_slots_before_equal_update + 3U,
                    "the equal-value activation fills both producer outputs and the consumer output");
            }
            require(interpreter.run().status == RunStatus::completed,
                "local-wave register reuse completes the ordinary SV chain");
        }
        auto expected_output = partial_output_offset
            ? PackedLogic4 { output_width, Logic4::z }
            : PackedLogic4 { output_width, Logic4::one };
        if (partial_output_offset) {
            expected_output.set(*partial_output_offset, Logic4::one);
            const auto output_current
                = interpreter.signal_value_snapshot(output);
            require(output_current == expected_output,
                "local-wave publication changes only the declared output slice");
            if (partial_output_run != nullptr) {
                auto& implementation
                    = OwnedDriverDemotionTestAccess::implementation(interpreter);
                auto& captured_output = partial_output_run->output;
                captured_output.current = output_current;
                captured_output.last
                    = implementation.signal_last_values.at(output);
                captured_output.stored
                    = interpreter.stored_signal_value_snapshot(output);
                captured_output.driver = interpreter.driver_value(1U, output);
                captured_output.event = implementation.signal_events.at(output);
                captured_output.transaction
                    = implementation.signal_transactions.at(output);
                const auto& stamp
                    = implementation.signal_event_scheduling_stamps.at(output);
                captured_output.event_domain = stamp.origin.process_domain;
                captured_output.event_phase = stamp.origin.phase;
                captured_output.event_round = stamp.systemverilog_round;
                require(captured_output.current == expected_output
                        && captured_output.stored == expected_output
                        && captured_output.driver == expected_output
                        && captured_output.event.has_value()
                        && captured_output.transaction.has_value(),
                    "partial local-wave commit preserves full-width raw owner and stored values");
                partial_output_run->kernel_runs
                    = implementation.systemverilog_wave_profile_region_kernel_runs;
                partial_output_run->kernel_publications
                    = implementation.systemverilog_wave_profile_region_kernel_publications;
                partial_output_run->local_update_dispatches
                    = implementation.systemverilog_wave_profile_a2_local_update_dispatches;
            }
        }
        require(interpreter.signal_value(internal)
                    == PackedLogic4(1U, Logic4::zero)
                && (!internal_second
                    || interpreter.signal_value(*internal_second)
                        == PackedLogic4(1U, Logic4::zero))
                && interpreter.signal_value(output)
                    == expected_output,
            "local internal publication preserves exact checked values");
    }

    const auto output = captured.str();
    const auto profile_start = output.find(
        "fsim-profile: sv-ordered-wave-summary ");
    require(profile_start != std::string::npos,
        "local-wave runtime emits its focused route counters");
    const auto profile_end = output.find('\n', profile_start);
    const auto profile = output.substr(profile_start,
        profile_end == std::string::npos
            ? std::string::npos : profile_end - profile_start);
    if (partial_output_offset && !decline_first_activation) {
        require(partial_output_run != nullptr
                && partial_output_run->kernel_runs != 0U
                && partial_output_run->kernel_publications != 0U
                && partial_output_run->local_update_dispatches != 0U
                && region_profile_metric(profile, "region_kernel_runs")
                    == partial_output_run->kernel_runs
                && region_profile_metric(profile, "region_kernel_publications")
                    == partial_output_run->kernel_publications,
            "the direct slice was published by the accepted local-wave kernel route");
    }
    if (partial_output_offset && decline_first_activation) {
        require(backend_probe.first_activation_declined
                && partial_output_run != nullptr
                && partial_output_run->kernel_runs
                    == backend_probe.executions + 1U,
            "the declined first backend activation runs once through the interpreter evaluator");
    }
    if (partial_branch) {
        // The independent straight-line consumer may still have a native
        // kernel. Only the producer's retained-register path must remain on
        // ordinary completion, as checked before public observation above.
        require(executor_probe.resumes[0U] > 1U,
            "a branch with a path that retains a register stays on ordinary completion");
    } else {
        require(backend_probe.factories == 1U
                && backend_probe.all_internal_reads_rewritten
                && backend_probe.internal_register_rewrites != 0U
                && backend_probe.executions != 0U
                && (prepared_output_prefix
                        ? backend_probe.prepared_output_calls != 0U
                        : backend_probe.plane_dispatches != 0U)
                && backend_probe.plane_views_valid
                && !backend_probe.boundary_input_values.empty(),
            "native activation consumes generated internal-register reads and exact planes");
    }
    if (!partial_branch) {
        const auto first_boundary_one = std::ranges::find(
            backend_probe.boundary_input_values,
            PackedLogic4(1U, Logic4::one));
        const auto first_boundary_zero = std::ranges::find(
            backend_probe.boundary_input_values,
            PackedLogic4(1U, Logic4::zero));
        require(first_boundary_one != backend_probe.boundary_input_values.end()
                && first_boundary_zero
                    != backend_probe.boundary_input_values.end()
                && first_boundary_zero > first_boundary_one,
            "boundary inputs are refreshed after the clocked source changes");
    }
    if (!partial_branch && !observe_after_first && !decline_first_activation
        && !decline_after_success && !decline_direct_ready_first) {
        require(region_profile_metric(profile, "a2_local_update_dispatches") > 0U
                && region_profile_metric(profile, "a2_local_update_fallbacks") == 0U
                && region_profile_metric(profile, "a2_ordinary_internal_updates")
                    == startup_ordinary_internal_updates
                && region_profile_metric(profile, "a2_internal_seed_reads")
                    == backend_probe.internal_signals.size()
                && region_profile_metric(profile, "a2_internal_state_seeds") == 1U
                && region_profile_metric(profile, "a2_internal_state_reuses") > 0U
                && region_profile_metric(profile, "a2_local_fanout_suppressions") > 0U
                && region_profile_metric(profile, "a2_grouped_fanout_members") > 0U,
            "local wave updates retained internal registers and grouped fanout without ordinary commits");
        if (!prepared_output_prefix) {
            require(region_profile_metric(profile,
                       "a3_private_update_ticket_entries") > 0U
                && region_profile_metric(profile,
                       "a3_private_update_ticket_members")
                    > region_profile_metric(profile,
                        "a3_private_update_ticket_entries")
                && region_profile_metric(profile,
                       "a3_private_update_entries_elided")
                    == region_profile_metric(profile,
                        "a3_private_update_ticket_members")
                        - region_profile_metric(profile,
                            "a3_private_update_ticket_entries")
                && region_profile_metric(profile,
                       "a3_private_update_fallback_members") == 0U,
                "ordinary private updates keep their grouped ticket route");
        }
        if (prepared_output_prefix) {
            const auto batches
                = region_profile_metric(profile, "prepared_output_batches");
            const auto seals
                = region_profile_metric(profile, "prepared_output_seals");
            const auto first_seals = std::ranges::count(
                backend_probe.prepared_output_signals,
                internal_profile_signal);
            const auto second_seals = internal_second_profile_signal
                ? std::ranges::count(
                    backend_probe.prepared_output_signals,
                    *internal_second_profile_signal)
                : 0;
            const auto consumer_seals = std::ranges::count(
                backend_probe.prepared_output_signals,
                output_profile_signal);
            require(internal_second_profile_signal.has_value()
                    && backend_probe.internal_signals.size() == 3U
                    && std::ranges::find(backend_probe.internal_signals,
                           output_profile_signal)
                        != backend_probe.internal_signals.end()
                    && batches == 5U
                    && first_seals == 3
                    && second_seals == 3
                    && consumer_seals == 5
                    && seals == static_cast<std::uint64_t>(
                        first_seals + second_seals + consumer_seals)
                    && region_profile_metric(profile,
                           "prepared_output_fallbacks") == 0U
                    && backend_probe.prepared_output_calls == batches
                    && backend_probe.plane_dispatches == 0U
                    && backend_probe.prepared_output_slots_written == seals
                    && region_profile_metric(profile,
                           "prepared_output_group_dispatches") != 0U
                    && region_profile_metric(profile,
                           "prepared_output_group_members")
                        > region_profile_metric(profile,
                            "prepared_output_group_dispatches")
                    && std::ranges::any_of(
                        backend_probe.reader_observed_values,
                        [](const PackedLogic4& value) {
                            return value == PackedLogic4(1U, Logic4::one);
                        }),
                "prepared activations seal each selected producer pair and every consumer output natively");
        }
        require(!backend_probe.internal_input_values.empty()
                && backend_probe.internal_input_values.front()
                    != PackedLogic4(1U, Logic4::one)
                && std::ranges::find(backend_probe.internal_input_values,
                    PackedLogic4(1U, Logic4::one))
                    != backend_probe.internal_input_values.end(),
            "the internal register changes only after its private committed publication");
    } else if (!partial_branch
        && (decline_first_activation || decline_after_success)) {
        require((decline_first_activation
                    ? backend_probe.first_activation_declined
                    : backend_probe.on_demand_activation_declined)
                && region_profile_metric(profile,
                       "a2_ordinary_internal_updates") > 0U
                && region_profile_metric(profile,
                       "a2_internal_seed_reads")
                    >= 2U * backend_probe.internal_signals.size()
                && region_profile_metric(profile,
                       "a2_internal_state_seeds") >= 2U
                && region_profile_metric(profile,
                       "a2_internal_state_reuses") > 0U
                && region_profile_metric(profile,
                       "a2_local_update_dispatches") > 0U,
            "ordinary fallback invalidates the local bank before a later reseed");
        const auto first_committed_one = std::ranges::find(
            backend_probe.internal_input_values,
            PackedLogic4(1U, Logic4::one));
        require(!backend_probe.internal_input_values.empty()
                && backend_probe.internal_input_values.front()
                    != PackedLogic4(1U, Logic4::one)
                && first_committed_one
                    != backend_probe.internal_input_values.end()
                && first_committed_one
                    != backend_probe.internal_input_values.begin(),
            "the retried native wave reads the value installed by ordinary fallback");
    } else if (!partial_branch && decline_direct_ready_first) {
        const auto attempts
            = region_profile_metric(profile, "direct_ready_window_attempts");
        const auto completions
            = region_profile_metric(profile, "direct_ready_window_completions");
        require(backend_probe.direct_ready_window_declined
                && backend_probe.direct_ready_fallback_comparisons == 1U
                && backend_probe.direct_ready_fallback_image_matches
                && !backend_probe.direct_ready_fallback_pending
                && backend_probe.direct_ready_window_calls == attempts
                && backend_probe.direct_ready_window_completions == completions
                && completions > 0U
                && attempts > completions
                && attempts - completions == 1U
                && backend_probe.prepared_output_calls >= completions
                && region_profile_metric(profile,
                       "a2_ordinary_internal_updates")
                    == startup_ordinary_internal_updates,
            "one direct-ready decline uses its exact captured image for "
            "checked fallback before native recovery");
    } else if (!partial_branch) {
        require(region_profile_metric(profile, "a2_ordinary_internal_updates") > 0U,
            "late observation routes subsequent internal commits through checked publication");
    }

    if (stateless_completion && !retain_unused_register && !partial_branch) {
        const auto fast_members = region_profile_metric(profile,
            "a2_completion_fast_members");
        const auto preflights = region_profile_metric(profile,
            "a2_completion_preflights");
        require(region_profile_metric(profile,
                    "a2_completion_fast_batches") > 0U
                && backend_probe.executions > 1U
                && (decline_after_success
                        ? fast_members >= preflights
                        : fast_members > preflights)
                && preflights > 0U
                && executor_probe.completion_prepares == preflights
                && executor_probe.completion_prepares
                    == executor_probe.completion_cancels
                && executor_probe.completion_stages == 0U
                && executor_probe.completion_commits == 0U,
            "certified stateless members skip per-member stage and commit after a boundary preflight");
    }
    if (retain_unused_register) {
        require(region_profile_metric(profile,
                    "a2_completion_fast_batches") == 0U
                && region_profile_metric(profile,
                    "a2_completion_register_declines") > 0U
                && executor_probe.completion_stages > 0U
                && executor_probe.completion_commits > 0U,
            "a retained unassigned register keeps ordinary completion staging");
    }
}

enum class PreparedOutputInterruption : std::uint8_t {
    range_sensitivity,
    observation,
    force,
    deposit,
    stop_resume,
    cancel,
    allocation_failure_retry,
    captured_backend_failure_retry,
};

struct PreparedOutputInterruptionProbe {
    Interpreter* interpreter { };
    PreparedOutputInterruption mode { };
    SignalId first_output { };
    SignalId second_output { };
    ProcessId first_owner { };
    ProcessId second_owner { };
    std::size_t seals_before { };
    std::size_t fallbacks_before { };
    std::uint64_t mapped_successor_batches_before { };
    std::uint64_t mapped_successor_readers_before { };
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        second_transaction_before;
    std::array<Logic4, 4U> first_roles_before { };
    std::array<Logic4, 4U> second_roles_before { };
    std::array<Logic4, 4U> second_roles_after_ticket { };
    std::array<PackedLogic4, 4U> retained_second_roles;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        second_transaction_after_ticket;
    OwnedDriverDemotionTestAccess::PreparedOutputBatchState* batch { };
    std::size_t pending_tickets_at_cut { };
    std::size_t seals_at_cut { };
    bool versioned_roles_available { };
    bool interposition_queued { };
    bool interposition_ran { };
    bool cut_was_between_tickets { };
    bool post_ticket_callback_queued { };
    bool post_ticket_callback_ran { };
    bool allocation_armed { };
    LocalWaveBackendProbe* backend_probe { };
    bool reader_queued_at_cut { };
    std::size_t reader_samples_at_cut { };
    std::optional<PackedLogic4> reader_value_at_cut;
};

[[nodiscard]] Logic4 word_value(const PackedLogic4& value)
{
    return value.get(0U);
}

[[nodiscard]] std::array<Logic4, 4U> read_output_roles(
    OwnedDriverDemotionTestAccess::Implementation& implementation,
    const SignalId signal,
    const ProcessId owner)
{
    const auto* const driver = implementation.driver_values.at(signal).find(owner);
    if (driver == nullptr) {
        throw std::runtime_error {
            "prepared output owner lost its original driver record"
        };
    }
    return { word_value(implementation.signals.at(signal).initial_value),
        word_value(implementation.signal_last_values.at(signal)),
        word_value(implementation.driven_values.at(signal)),
        word_value(driver->value) };
}

[[nodiscard]] std::array<PackedLogic4, 4U> read_full_output_roles(
    OwnedDriverDemotionTestAccess::Implementation& implementation,
    const SignalId signal,
    const ProcessId owner)
{
    const auto component
        = signal < implementation.region_authoritative_component_by_signal.size()
            ? implementation.region_authoritative_component_by_signal[signal]
            : std::numeric_limits<std::size_t>::max();
    if (component < implementation.region_authoritative_state_by_component.size()
        && implementation.region_authoritative_state_by_component[component]
        && implementation.region_authoritative_state_by_component[component]
               ->values().packed_signal_slots_bound(signal)) {
        const auto& values
            = implementation.region_authoritative_state_by_component[component]
                  ->values();
        return { values.current(signal), values.previous(signal),
            values.stored(signal), values.owner_value(signal, owner) };
    }
    return { implementation.logical_signal_value(signal),
        implementation.logical_signal_last_value(signal),
        implementation.driven_values.at(signal),
        implementation.underlying_driver_value(owner, signal) };
}

void check_prepared_output_interruption(
    const PreparedOutputInterruption mode,
    const bool offer_direct_ready = false,
    const bool offer_direct_ready_successor_masks = false)
{
    require(!offer_direct_ready_successor_masks || offer_direct_ready,
        "combined direct-ready masks require the direct-ready V1 capability");
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment wide_slots_enabled {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1" };
    ScopedEnvironment profile_enabled {
        "FSIM_PROFILE_SV_WAVES", "1" };

    std::ostringstream captured;
    PreparedOutputInterruptionProbe probe;
    probe.mode = mode;
    LocalWaveBackendProbe backend_probe;
    backend_probe.execute_prepared_output_prefix = true;
    backend_probe.execute_prepared_output_successor_masks
        = mode != PreparedOutputInterruption::allocation_failure_retry
            && mode != PreparedOutputInterruption::range_sensitivity
            && mode
                != PreparedOutputInterruption::captured_backend_failure_retry;
    backend_probe.execute_direct_ready_window = offer_direct_ready;
    backend_probe.execute_direct_ready_window_successor_masks
        = offer_direct_ready_successor_masks;
    {
        ScopedCerrCapture capture { captured };
        Interpreter interpreter;
        probe.interpreter = &interpreter;
        probe.backend_probe = &backend_probe;
        const auto input = interpreter.add_signal({
            "prepared_interrupt.input", PackedLogic4(1U, Logic4::zero) });
        const auto first = interpreter.add_signal({
            "prepared_interrupt.first", PackedLogic4(1U, Logic4::zero),
            ResolutionKind::sv_wire });
        const auto second = interpreter.add_signal({
            "prepared_interrupt.second", PackedLogic4(1U, Logic4::zero),
            ResolutionKind::sv_wire });
        const auto trigger = interpreter.add_signal({
            "prepared_interrupt.trigger", PackedLogic4(1U, Logic4::zero) });
        probe.first_output = first;
        probe.second_output = second;
        backend_probe.reader_observed_signal = second;
        backend_probe.reader_process = 2U;
        probe.first_owner = 1U;
        probe.second_owner = 3U;

        Process order_spacer;
        order_spacer.id = 0U;
        order_spacer.name = "prepared_interrupt_order_spacer";
        order_spacer.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        order_spacer.initialize = false;
        order_spacer.operations = { Halt { } };
        require(interpreter.add_process(std::move(order_spacer)) == 0U,
            "prepared output fixture reserves the leading stable order");

        Process first_writer;
        first_writer.id = 1U;
        first_writer.name = "prepared_interrupt_first_writer";
        first_writer.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        first_writer.initialize = true;
        first_writer.register_count = 1U;
        first_writer.static_sensitivity = { { trigger, EdgeKind::any } };
        first_writer.driver_regions = { { first, 0U, 0U, true } };
        first_writer.operations = {
            ReadSignal { 0U, input },
            WriteUpdate { first, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(first_writer)) == 1U,
            "the first prepared writer owns stable order one");

        Process reader;
        reader.id = 2U;
        reader.name = "prepared_interrupt_internal_reader";
        reader.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        reader.initialize = true;
        reader.register_count = 2U;
        reader.static_sensitivity = { { trigger, EdgeKind::any } };
        reader.static_sensitivity.push_back(
            mode == PreparedOutputInterruption::range_sensitivity
                ? Sensitivity { first, EdgeKind::any, 0U, 1U }
                : Sensitivity { first, EdgeKind::any });
        reader.operations = {
            ReadSignal { 0U, first }, ReadSignal { 1U, second },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(reader)) == 2U,
            "an in-cone reader makes both outputs eligible internal values");

        Process second_writer;
        second_writer.id = 3U;
        second_writer.name = "prepared_interrupt_second_writer";
        second_writer.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        second_writer.initialize = true;
        second_writer.register_count = 2U;
        second_writer.static_sensitivity = { { trigger, EdgeKind::any } };
        second_writer.driver_regions = { { second, 0U, 0U, true } };
        second_writer.operations = {
            ReadSignal { 0U, first }, ReadSignal { 1U, input },
            WriteUpdate { second, 1U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(second_writer)) == 3U,
            "the second prepared writer owns stable order three");

        const auto install_executor = [&](const ProcessId process,
                                          const PreparedInterruptionExecutorRole role,
                                          const SignalId first_input,
                                          const SignalId second_input,
                                          const SignalId output,
                                          const std::size_t register_count) {
            const auto& registered = interpreter.process_program(process);
            const auto binding = ProcessExecutorProgramBinding {
                registered, registered, process };
            const auto wait = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            interpreter.set_process_executor(process,
                std::make_unique<PreparedInterruptionExecutor>(process,
                    role, first_input, second_input, output, wait,
                    register_count, binding, &backend_probe,
                    &backend_probe.reader_observed_values));
        };
        install_executor(1U,
            PreparedInterruptionExecutorRole::first_writer,
            input, second, first, 1U);
        install_executor(2U,
            PreparedInterruptionExecutorRole::reader,
            first, second, second, 2U);
        install_executor(3U,
            PreparedInterruptionExecutorRole::second_writer,
            first, input, second, 2U);

        Process clock;
        clock.id = 4U;
        clock.name = "prepared_interrupt_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.driver_regions = {
            { input, 0U, 0U, true }, { trigger, 0U, 0U, true },
        };
        clock.operations = {
            WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { input, 0U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { trigger, 0U },
        };
        if (mode == PreparedOutputInterruption::allocation_failure_retry) {
            clock.operations.emplace_back(WaitFor { 1U });
            clock.operations.emplace_back(
                LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) });
            clock.operations.emplace_back(WriteBlocking { input, 0U });
            clock.operations.emplace_back(
                LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) });
            clock.operations.emplace_back(WriteBlocking { trigger, 0U });
        }
        clock.operations.emplace_back(Halt { });
        require(interpreter.add_process(std::move(clock)) == 4U,
            "the clock creates one event, or a second retry event");

        interpreter.set_region_kernel_backend_provider(
            std::make_shared<AcceptingRegionBackendProvider>(backend_probe));
        backend_probe.after_prepared_output_execution = [&probe] {
            auto& interpreter = *probe.interpreter;
            auto& implementation
                = OwnedDriverDemotionTestAccess::implementation(interpreter);
            const auto first_component
                = implementation.region_authoritative_component_by_signal
                      .at(probe.first_output);
            const auto second_component
                = implementation.region_authoritative_component_by_signal
                      .at(probe.second_output);
            if (first_component != second_component
                || first_component
                    >= implementation.region_kernel_backends_by_component.size()) {
                return;
            }
            auto* const backend
                = implementation.region_kernel_backends_by_component
                      .at(first_component).get();
            if (backend == nullptr || !backend->native_workspace
                || !backend->native_workspace->prepared_output_batch) {
                return;
            }
            probe.batch = backend->native_workspace
                              ->prepared_output_batch.get();
            auto* const state
                = implementation.region_authoritative_state_by_component
                      .at(first_component).get();
            probe.versioned_roles_available = state != nullptr
                && state->values().requires_prewrite_unbind()
                && state->values().packed_signal_slots_bound(
                    probe.first_output)
                && state->values().packed_signal_slots_bound(
                    probe.second_output);
            if (probe.interposition_queued) {
                return;
            }
            probe.interposition_queued = true;
            interpreter.scheduler().schedule_systemverilog(
                SchedulerPhase::active, 2U,
                [&probe](Scheduler& scheduler) {
                    auto& active_interpreter = *probe.interpreter;
                    auto& active_implementation
                        = OwnedDriverDemotionTestAccess::implementation(
                            active_interpreter);
                    const auto* const first_driver
                        = active_implementation.driver_values.at(
                            probe.first_output).find(probe.first_owner);
                    const auto* const second_driver
                        = active_implementation.driver_values.at(
                            probe.second_output).find(probe.second_owner);
                    probe.seals_at_cut
                        = active_implementation
                              .systemverilog_wave_profile_prepared_output_seals;
                    probe.reader_queued_at_cut
                        = active_implementation.processes.at(2U).queued;
                    probe.reader_samples_at_cut
                        = probe.backend_probe == nullptr ? 0U
                        : probe.backend_probe->reader_observed_values.size();
                    if (probe.backend_probe != nullptr
                        && !probe.backend_probe->reader_observed_values.empty()) {
                        probe.reader_value_at_cut
                            = probe.backend_probe->reader_observed_values.back();
                    }
                    probe.pending_tickets_at_cut = probe.batch == nullptr
                        ? 0U : probe.batch->pending_tickets;
                    probe.cut_was_between_tickets
                        = probe.batch != nullptr && probe.batch->active
                        && !probe.batch->cancelled
                        && probe.batch->sealed_tickets == 1U
                        && probe.pending_tickets_at_cut == 1U
                        && probe.seals_at_cut == probe.seals_before + 1U
                        && first_driver != nullptr && second_driver != nullptr
                        && read_output_roles(active_implementation,
                               probe.first_output, probe.first_owner)
                            == std::array<Logic4, 4U> {
                                Logic4::one,
                                probe.first_roles_before[0U],
                                Logic4::one,
                                Logic4::one }
                        && read_output_roles(active_implementation,
                               probe.second_output, probe.second_owner)
                            == probe.second_roles_before
                        && active_implementation.signal_transactions.at(
                               probe.second_output)
                            == probe.second_transaction_before;
                    probe.interposition_ran = true;

                    switch (probe.mode) {
                    case PreparedOutputInterruption::range_sensitivity:
                        break;
                    case PreparedOutputInterruption::observation:
                        active_interpreter.prepare_signal_observation(
                            probe.second_output);
                        break;
                    case PreparedOutputInterruption::force:
                        active_interpreter.force_signal(probe.second_output,
                            PackedLogic4(1U, Logic4::one));
                        break;
                    case PreparedOutputInterruption::deposit:
                        active_interpreter.deposit_signal(probe.second_output,
                            PackedLogic4(1U, Logic4::one));
                        break;
                    case PreparedOutputInterruption::stop_resume:
                    case PreparedOutputInterruption::cancel:
                        scheduler.request_stop();
                        break;
                    case PreparedOutputInterruption::allocation_failure_retry:
#if defined(FSIM_RUNTIME_PREPARED_OUTPUT_FAILURE_TESTS)
                        probe.retained_second_roles
                            = OwnedDriverDemotionTestAccess::packed_a4_values(
                                active_interpreter, probe.second_output,
                                probe.second_owner);
                        staging_failure_support::arm_allocation_failure(0U);
                        probe.allocation_armed = true;
#endif
                        break;
                    case PreparedOutputInterruption::captured_backend_failure_retry:
                        break;
                    }
                });
            if (probe.mode == PreparedOutputInterruption::observation
                || probe.mode == PreparedOutputInterruption::force
                || probe.mode == PreparedOutputInterruption::deposit
                || probe.mode
                    == PreparedOutputInterruption::allocation_failure_retry) {
                probe.post_ticket_callback_queued = true;
                interpreter.scheduler().schedule_systemverilog(
                    SchedulerPhase::active, 4U,
                    [&probe](Scheduler& after_ticket_scheduler) {
                        auto& after_ticket_interpreter = *probe.interpreter;
                        auto& after_ticket_implementation
                            = OwnedDriverDemotionTestAccess::implementation(
                                after_ticket_interpreter);
                        probe.second_roles_after_ticket = read_output_roles(
                            after_ticket_implementation,
                            probe.second_output, probe.second_owner);
                        probe.second_transaction_after_ticket
                            = after_ticket_implementation.signal_transactions.at(
                                probe.second_output);
                        probe.post_ticket_callback_ran = true;
                        if (probe.mode
                            == PreparedOutputInterruption::allocation_failure_retry) {
                            after_ticket_scheduler.request_stop();
                        }
                    });
            }
        };

        interpreter.start();
        require(interpreter.run(0U).status == RunStatus::time_limit,
            "prepared interruption fixture reaches its future clock event");
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto component
            = implementation.region_authoritative_component_by_signal.at(first);
        require(component
                    == implementation.region_authoritative_component_by_signal.at(second)
                && component
                    < implementation.region_kernel_backends_by_component.size(),
            "both prepared outputs belong to the same retained component");
        const auto* const entry
            = implementation.region_kernel_backends_by_component.at(component).get();
        require(entry != nullptr && entry->native_workspace
                && entry->kernel.internal_signals.size() == 2U
                && std::ranges::find(entry->kernel.internal_signals, first)
                    != entry->kernel.internal_signals.end()
                && std::ranges::find(entry->kernel.internal_signals, second)
                    != entry->kernel.internal_signals.end()
                && std::ranges::count(entry->kernel.outputs, first,
                       &RegionConeOutputBinding::signal) == 1
                && std::ranges::count(entry->kernel.outputs, second,
                       &RegionConeOutputBinding::signal) == 1
                && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, first, probe.first_owner)
                && OwnedDriverDemotionTestAccess::packed_a4_slots_bound(
                    interpreter, second, probe.second_owner),
            "the real scheduled fixture binds both original output slots");
        const auto check_prepared_reader_map =
            [&](const SignalId signal) {
                const auto& mapping
                    = implementation.region_prepared_successor_by_signal.at(
                        signal);
                require(mapping.generation
                            == implementation.region_runtime_generation
                        && mapping.component == component
                        && mapping.reader_count == 1U
                        && mapping.expected_mask == UINT64_C(1)
                        && mapping.reader_offset
                            < implementation.region_prepared_successor_readers.size(),
                    "the prepared map captures one certified whole-any reader");
                const auto& mapped
                    = implementation.region_prepared_successor_readers.at(
                        mapping.reader_offset);
                const auto& readiness
                    = implementation.region_readiness_mask_by_component.at(
                        component);
                require(mapped.process == 2U
                        && mapped.readiness_member
                            == implementation.region_readiness_member_index_by_process.at(
                                mapped.process)
                        && mapped.readiness_word
                            == mapped.readiness_member / 64U
                        && mapped.authoritative_member
                            == implementation.region_authoritative_member_index_by_process.at(
                                mapped.process)
                        && mapped.authoritative_readiness_word
                            == mapped.authoritative_member / 64U
                        && mapped.authoritative_readiness_bit
                            == (UINT64_C(1)
                                << (mapped.authoritative_member % 64U))
                        && mapped.queued_mask_word
                            == readiness.offset + mapped.readiness_word
                        && mapped.readiness_bit
                            == (UINT64_C(1)
                                << (mapped.readiness_member % 64U))
                        && mapped.static_trigger_mask != 0U,
                    "the prepared map binds sparse process IDs to exact readiness bits");
            };
        if (mode != PreparedOutputInterruption::range_sensitivity) {
            check_prepared_reader_map(first);
            const auto& second_mapping
                = implementation.region_prepared_successor_by_signal.at(second);
            require(second_mapping.reader_count == 0U,
                "a read operand without static sensitivity has no prepared wakeup map");
        }
        const auto expected_successor_slot_count
            = entry->kernel.internal_signals.size();
        require(expected_successor_slot_count
                    <= backend_probe.last_successor_masks.size()
                && expected_successor_slot_count
                    <= 8U,
            "the expected successor map fits the passive test snapshot");
        std::array<std::uint64_t, 8U> expected_successor_masks { };
        for (std::size_t slot = 0U;
             slot < expected_successor_slot_count; ++slot) {
            const auto signal = entry->kernel.internal_signals[slot];
            std::size_t ordinal { };
            for (const auto& member : entry->kernel.members) {
                const bool reads_whole_any = std::ranges::any_of(
                    member.sensitivities,
                    [signal](const auto& sensitivity) {
                        return sensitivity.signal == signal
                            && sensitivity.edge == EdgeKind::any
                            && sensitivity.offset == 0U
                            && sensitivity.width == 0U;
                    });
                if (!reads_whole_any) {
                    continue;
                }
                require(ordinal < 64U,
                    "the saved successor witness fits its V1 mask word");
                expected_successor_masks[slot]
                    |= UINT64_C(1) << ordinal;
                ++ordinal;
            }
        }
        probe.first_roles_before = read_output_roles(
            implementation, first, probe.first_owner);
        probe.second_roles_before = read_output_roles(
            implementation, second, probe.second_owner);
        probe.second_transaction_before
            = implementation.signal_transactions.at(second);
        probe.seals_before
            = implementation.systemverilog_wave_profile_prepared_output_seals;
        probe.fallbacks_before
            = implementation.systemverilog_wave_profile_prepared_output_fallbacks;
        probe.mapped_successor_batches_before
            = implementation.systemverilog_wave_profile_a3_mapped_successor_batches;
        probe.mapped_successor_readers_before
            = implementation.systemverilog_wave_profile_a3_mapped_successor_readers;

        std::optional<RunResult> first_result;
        if (mode
            == PreparedOutputInterruption::captured_backend_failure_retry) {
            backend_probe.failure_point = offer_direct_ready
                ? LocalWaveBackendProbe::FailurePoint::direct_ready
                : LocalWaveBackendProbe::FailurePoint::prepared_output;
            backend_probe.armed_failure = std::make_exception_ptr(
                std::runtime_error {
                    "test-only prepared backend entry failure"
                });

            const auto before_failure_executions = backend_probe.executions;
            const auto before_failure_prepared_calls
                = backend_probe.prepared_output_calls;
            const auto before_failure_direct_ready_calls
                = backend_probe.direct_ready_window_calls;
            const auto before_failure_direct_ready_completions
                = backend_probe.direct_ready_window_completions;
            const auto before_failure_channel_polls
                = backend_probe.failure_channel_polls;
            const auto before_failure_kernel_failures
                = implementation.systemverilog_wave_profile_region_kernel_failures;
            const auto before_failure_forwarding_attempts
                = implementation.systemverilog_wave_profile_region_forwarding_attempts;
            const auto before_failure_forwarding_declines
                = implementation.systemverilog_wave_profile_region_forwarding_declines;
            const auto before_failure_first_transaction
                = implementation.signal_transactions.at(first);
            const auto before_failure_second_transaction
                = implementation.signal_transactions.at(second);
            const auto before_failure_first_event
                = implementation.signal_events.at(first);
            const auto before_failure_second_event
                = implementation.signal_events.at(second);

            bool failure_was_rethrown { };
            try {
                static_cast<void>(interpreter.run(1U));
            } catch (const std::runtime_error& error) {
                failure_was_rethrown = std::string_view(error.what())
                    == "test-only prepared backend entry failure";
                if (!failure_was_rethrown) {
                    throw;
                }
            }
            require(failure_was_rethrown,
                "a captured prepared-route exception is rethrown to the caller");
            require(implementation.systemverilog_wave_profile_region_kernel_failures
                        == before_failure_kernel_failures + 1U
                    && implementation.systemverilog_wave_profile_region_forwarding_declines
                        - before_failure_forwarding_declines
                        == implementation.systemverilog_wave_profile_region_forwarding_attempts
                            - before_failure_forwarding_attempts,
                "a captured native failure counts once without adding a forwarding decline");
            require(backend_probe.executions == before_failure_executions
                    && implementation.systemverilog_wave_profile_prepared_output_fallbacks
                        == probe.fallbacks_before
                    && backend_probe.failure_channel_polls
                        == before_failure_channel_polls + 1U
                    && !backend_probe.pending_failure
                    && !backend_probe.armed_failure,
                "the captured error suppresses image fallback and consumes its one-shot channel");
            if (offer_direct_ready) {
                require(backend_probe.direct_ready_window_calls
                            == before_failure_direct_ready_calls + 1U
                        && backend_probe.direct_ready_window_completions
                            == before_failure_direct_ready_completions
                        && backend_probe.prepared_output_calls
                            == before_failure_prepared_calls,
                    "direct-ready error returns before prepared or image fallback");
            } else {
                require(backend_probe.prepared_output_calls
                            == before_failure_prepared_calls + 1U
                        && backend_probe.direct_ready_window_calls
                            == before_failure_direct_ready_calls,
                    "prepared-output error returns before a later backend route");
            }
            require(read_output_roles(implementation, first,
                        probe.first_owner) == probe.first_roles_before
                    && read_output_roles(implementation, second,
                        probe.second_owner) == probe.second_roles_before
                    && implementation.signal_transactions.at(first)
                        == before_failure_first_transaction
                    && implementation.signal_transactions.at(second)
                        == before_failure_second_transaction
                    && implementation.signal_events.at(first)
                        == before_failure_first_event
                    && implementation.signal_events.at(second)
                        == before_failure_second_event
                    && probe.batch == nullptr
                    && !probe.interposition_queued,
                "the failed prepared attempt publishes no output ticket or signal state");
            first_result = interpreter.run(1U);
            require(backend_probe.failed_backend_prefix_captured
                    && backend_probe.retried_backend_prefix_captured
                    && backend_probe.retry_backend_prefix_matches_failure
                    && !backend_probe.pending_failure
                    && implementation.systemverilog_wave_profile_region_kernel_failures
                        == before_failure_kernel_failures + 1U,
                "retry reoffers the exact original scheduler prefix");
            if (offer_direct_ready) {
                require(backend_probe.direct_ready_window_calls
                            > before_failure_direct_ready_calls + 1U
                        && backend_probe.direct_ready_window_calls
                            == backend_probe.direct_ready_window_completions + 1U
                        && backend_probe.executions
                            > before_failure_executions,
                    "the direct-ready retry executes after exactly one captured decline");
            } else {
                require(backend_probe.prepared_output_calls
                            > before_failure_prepared_calls + 1U
                        && backend_probe.executions
                            > before_failure_executions,
                    "the prepared-output retry executes after exactly one captured decline");
            }
        } else {
            first_result = interpreter.run(1U);
        }
        if (mode == PreparedOutputInterruption::stop_resume
            || mode == PreparedOutputInterruption::cancel
            || mode == PreparedOutputInterruption::allocation_failure_retry) {
            require(first_result->status == RunStatus::stopped,
                "the interruption stops after the expected ticket boundary");
        } else {
            require(first_result->status == RunStatus::time_limit
                    || first_result->status == RunStatus::completed,
                "the first prepared publication round reaches its boundary");
        }
        if (mode == PreparedOutputInterruption::stop_resume) {
            require(probe.batch != nullptr
                    && probe.batch->pending_tickets == 1U,
                "the second prepared ticket remains owned after stop");
            interpreter.scheduler().clear_stop();
            require(interpreter.run().status == RunStatus::completed,
                "resuming the scheduler retires the retained prepared ticket");
            require(probe.reader_queued_at_cut
                    && probe.reader_samples_at_cut != 0U
                    && probe.reader_value_at_cut
                    && *probe.reader_value_at_cut
                        == PackedLogic4(1U, Logic4::zero)
                    && std::ranges::find(
                           backend_probe.reader_observed_values,
                           PackedLogic4(1U, Logic4::one))
                        != backend_probe.reader_observed_values.end(),
                "a reader raised by output one waits for the frozen group, then reads output two");
        } else if (mode == PreparedOutputInterruption::cancel) {
            const auto finished = interpreter.finish();
            require(finished.status == RunStatus::stopped,
                "finish cancels the remaining prepared ticket");
        } else if (mode == PreparedOutputInterruption::allocation_failure_retry) {
#if defined(FSIM_RUNTIME_PREPARED_OUTPUT_FAILURE_TESTS)
            const bool injected
                = staging_failure_support::allocation_failure_was_injected();
            staging_failure_support::clear_allocation_failure();
            require(probe.allocation_armed && injected,
                "the pinned second ticket reaches an actual replacement allocation");
            require(probe.post_ticket_callback_ran
                    && probe.seals_at_cut == probe.seals_before + 1U
                    && implementation
                               .systemverilog_wave_profile_prepared_output_seals
                        == probe.seals_before + 1U
                    && implementation
                               .systemverilog_wave_profile_prepared_output_fallbacks
                        == probe.fallbacks_before + 1U
                    && probe.second_roles_after_ticket
                        == std::array<Logic4, 4U> {
                            Logic4::one, Logic4::zero,
                            Logic4::one, Logic4::one }
                    && probe.second_transaction_after_ticket
                        != probe.second_transaction_before,
                "allocation failure falls back once with exact roles and transaction");
            interpreter.scheduler().clear_stop();
            const auto retry_boundary = interpreter.run(2U);
            require(retry_boundary.status == RunStatus::time_limit
                    || retry_boundary.status == RunStatus::completed,
                "the second clock edge reaches the retry boundary");
            require(interpreter.run().status == RunStatus::completed,
                "the checked fallback and later native retry complete once");
#endif
        } else {
            require(interpreter.run().status == RunStatus::completed,
                "the checked fallback finishes the one-edge fixture");
        }

        require(probe.interposition_queued && probe.interposition_ran
                && probe.cut_was_between_tickets
                && probe.versioned_roles_available,
            "the witness interrupts a real two-ticket prepared batch after ticket one");

        if (backend_probe.execute_prepared_output_successor_masks) {
            const auto generated_successor_batches
                = implementation
                      .systemverilog_wave_profile_generated_successor_mask_batches;
            if (offer_direct_ready) {
                if (offer_direct_ready_successor_masks) {
                    require(backend_probe.successor_mask_calls != 0U
                            && generated_successor_batches
                                == backend_probe.successor_mask_calls
                            && backend_probe.direct_ready_window_calls != 0U
                            && backend_probe.direct_ready_window_completions
                                == backend_probe.successor_mask_calls
                            && backend_probe.last_successor_mask_count
                                <= backend_probe.last_successor_masks.size()
                            && backend_probe.last_successor_mask_count
                                <= backend_probe.last_successor_changed.size(),
                        "opted-in combined direct-ready activations complete "
                        "their generated successor-mask sidecars");
                    require(backend_probe.last_successor_mask_count
                                == expected_successor_slot_count,
                        "the combined route covers the saved output map");
                } else {
                    require(backend_probe.successor_mask_calls == 0U
                            && generated_successor_batches == 0U
                            && backend_probe.direct_ready_window_calls != 0U
                            && backend_probe.direct_ready_window_completions
                                == backend_probe.direct_ready_window_calls,
                        "V1 direct-ready remains preferred when combined "
                        "successor support is absent");
                }
            } else {
                require(backend_probe.successor_mask_calls != 0U
                        && generated_successor_batches
                            == backend_probe.successor_mask_calls
                        && backend_probe.last_successor_mask_count
                            == expected_successor_slot_count
                        && backend_probe.last_successor_mask_count
                            <= backend_probe.last_successor_masks.size()
                        && backend_probe.last_successor_mask_count
                            <= backend_probe.last_successor_changed.size(),
                    "the runtime consumed each completed native successor-mask batch");
            }
            for (std::size_t slot = 0U;
                 slot < backend_probe.last_successor_mask_count; ++slot) {
                std::uint64_t expected_mask
                    = expected_successor_masks[slot];
                if (backend_probe.last_successor_changed[slot] == 0U) {
                    expected_mask = 0U;
                }
                require(backend_probe.last_successor_masks[slot]
                            == expected_mask,
                    "runtime binds each generated mask to the exact whole-any reader set");
            }
        }

        const auto seals_after
            = implementation.systemverilog_wave_profile_prepared_output_seals;
        const auto fallbacks_after
            = implementation.systemverilog_wave_profile_prepared_output_fallbacks;
        const auto dispatches_after
            = implementation.systemverilog_wave_profile_a2_local_update_dispatches;
        if (mode == PreparedOutputInterruption::observation
            || mode == PreparedOutputInterruption::force
            || mode == PreparedOutputInterruption::deposit) {
            require(seals_after == probe.seals_before + 1U
                    && fallbacks_after == probe.fallbacks_before + 1U
                    && dispatches_after == seals_after
                    && probe.post_ticket_callback_queued
                    && probe.post_ticket_callback_ran
                    && probe.second_roles_after_ticket
                        == std::array<Logic4, 4U> {
                            Logic4::one, Logic4::zero,
                            Logic4::one, Logic4::one }
                    && probe.second_transaction_after_ticket
                        != probe.second_transaction_before,
                "observation and mutation invalidate exactly the later ticket");
        } else if (mode == PreparedOutputInterruption::stop_resume) {
            require(seals_after == probe.seals_before + 2U
                    && fallbacks_after == probe.fallbacks_before
                    && dispatches_after == seals_after
                    && !probe.batch->active
                    && probe.batch->pending_tickets == 0U,
                "stop/resume seals both original tickets and retires their batch");
        } else if (mode == PreparedOutputInterruption::cancel) {
            require(seals_after == probe.seals_before + 1U
                    && fallbacks_after == probe.fallbacks_before
                    && dispatches_after == seals_after
                    && probe.batch->cancelled && !probe.batch->active
                    && probe.batch->pending_tickets == 0U,
                "finish cancellation retires the unsealed ticket without fallback");
            if (!offer_direct_ready && backend_probe
                    .execute_prepared_output_successor_masks) {
                require(implementation
                            .systemverilog_wave_profile_a3_mapped_successor_batches
                            == probe.mapped_successor_batches_before + 1U
                        && implementation
                               .systemverilog_wave_profile_a3_mapped_successor_readers
                            == probe.mapped_successor_readers_before + 1U,
                    "publication consumes one precomputed reader map without rediscovery");
            }
        } else if (mode == PreparedOutputInterruption::range_sensitivity) {
            require(backend_probe.successor_mask_calls == 0U
                    && implementation
                               .systemverilog_wave_profile_generated_successor_mask_batches
                        == 0U
                    && seals_after == probe.seals_before + 2U
                    && fallbacks_after == probe.fallbacks_before
                    && dispatches_after == seals_after
                    && std::ranges::find(
                           backend_probe.reader_observed_values,
                           PackedLogic4(1U, Logic4::one))
                        != backend_probe.reader_observed_values.end(),
                "range-sensitive readers retain ordinary fanout and ticket ordering");
        } else if (mode
            == PreparedOutputInterruption::captured_backend_failure_retry) {
            require(fallbacks_after == probe.fallbacks_before
                    && dispatches_after == seals_after,
                "a captured prepared error retries without publishing fallback work");
        } else {
#if defined(FSIM_RUNTIME_PREPARED_OUTPUT_FAILURE_TESTS)
            require(seals_after >= probe.seals_before + 3U
                    && fallbacks_after == probe.fallbacks_before + 1U
                    && dispatches_after == seals_after,
                "allocation failure falls back once and a later batch retries natively");
            for (std::size_t index = 0U;
                 index < probe.retained_second_roles.size(); ++index) {
                require(word_value(probe.retained_second_roles[index])
                        == probe.second_roles_before[index],
                    "old four-role snapshots survive failed replacement");
            }
#endif
        }

        if (mode == PreparedOutputInterruption::force) {
            const auto* const owner
                = implementation.driver_values.at(second).find(
                    probe.second_owner);
            require(owner != nullptr
                    && word_value(implementation.signals.at(second)
                            .initial_value) == Logic4::one
                    && word_value(owner->value) == Logic4::one,
                "force remains visible while fallback installs the raw owner");
            interpreter.release_signal(second);
        }
        if (mode == PreparedOutputInterruption::cancel) {
            require(read_output_roles(implementation, second,
                        probe.second_owner) == probe.second_roles_before,
                "cancelled publication preserves every second-output role");
        } else {
            const auto expected_first
                = mode == PreparedOutputInterruption::allocation_failure_retry
                    ? Logic4::zero : Logic4::one;
            const auto expected_second
                = mode == PreparedOutputInterruption::allocation_failure_retry
                    ? Logic4::zero : Logic4::one;
            require(interpreter.signal_value(first)
                        == PackedLogic4(1U, expected_first)
                    && interpreter.signal_value(second)
                        == PackedLogic4(1U, expected_second),
                "first seal and later checked/retried value are exact");
        }
    }
#if defined(FSIM_RUNTIME_PREPARED_OUTPUT_FAILURE_TESTS)
    staging_failure_support::clear_allocation_failure();
#endif
}

void check_region_backend_exact_mapping_comparison()
{
    RegionConeActivationKernel original;
    original.program.name = "region_backend_compare";
    original.program.register_count = 1U;
    original.program.register_value_kinds = { ValueKind::logic4 };
    original.program.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    original.program.operations = {
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) }, Halt { },
    };
    original.inputs.push_back({ 3U, 0U, 1U, ValueKind::logic4, false });
    original.constant_inputs.push_back({ 2U, 3U, 0U, 1U,
        ValueKind::logic4, SignalUpdateDomain::systemverilog_active,
        PackedLogic4(1U, Logic4::one) });
    RegionConeOutputBinding output;
    output.owner = 1U;
    output.signal = 5U;
    output.width = 1U;
    original.outputs.push_back(output);
    auto identical = original;
    require(same_region_kernel_mapping(original, identical),
        "exact immutable operation and mapping copies can share a backend");

    auto different_publication_kind = original;
    different_publication_kind.outputs.front().publication_kind
        = RegionOutputPublicationKind::blocking_immediate;
    require(!same_region_kernel_mapping(
                original, different_publication_kind),
        "blocking and deferred publication effects cannot share a backend");

    auto different_input = original;
    different_input.inputs.front().signal = 4U;
    require(!same_region_kernel_mapping(original, different_input),
        "a boundary signal remap cannot reuse an instance-bound backend");

    auto different_constant = original;
    different_constant.program.operations[0U] = LoadConstant {
        0U, PackedLogic4(1U, Logic4::zero) };
    require(!same_region_kernel_mapping(original, different_constant),
        "changed operation values cannot reuse a compiled backend");

    auto different_startup_value = original;
    different_startup_value.constant_inputs.front().value.set(
        0U, Logic4::zero);
    require(!same_region_kernel_mapping(original, different_startup_value),
        "a changed startup fact cannot reuse a constant-specialized backend");

    auto unsupported = original;
    unsupported.program.operations[0U] = ReadSignal { 0U, 3U };
    require(!same_region_kernel_mapping(original, unsupported),
        "unknown-to-the-comparator operations fail closed");

    auto replaced_operation = original;
    replaced_operation.program.operations.replace(0U,
        UnaryNot { 0U, 0U });
    require(original.program.operations.shares_body_with(
                replaced_operation.program.operations),
        "a generic operation replacement retains the shared immutable body");
    require(!same_region_kernel_mapping(original, replaced_operation),
        "a supported per-instance operation replacement changes the mapping");
    auto identical_replacement = replaced_operation;
    require(same_region_kernel_mapping(
                replaced_operation, identical_replacement),
        "equal supported operation overlays retain valid mapping equality");

    auto unsupported_overlay = original;
    unsupported_overlay.program.operations.replace(0U,
        ReadSignal { 0U, 3U });
    auto identical_unsupported_overlay = unsupported_overlay;
    require(unsupported_overlay.program.operations.shares_body_with(
                identical_unsupported_overlay.program.operations),
        "identical unsupported overlays can still share the body");
    require(!same_region_kernel_mapping(
                unsupported_overlay, identical_unsupported_overlay),
        "shared-body identity cannot admit an unsupported operation variant");

    const auto make_debug_process = [](const char* path,
                                        const std::uint32_t line,
                                        const char* scope) {
        Process process;
        process.id = 17U;
        process.name = "region_backend_debug_compare";
        process.register_count = 1U;
        process.register_value_kinds = { ValueKind::logic4 };
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.operations = {
            DebugPoint { DebugPointKind::statement,
                SourceLocation { path, line, 4U }, scope },
            Halt { },
        };
        return process;
    };
    const auto make_debug_kernel = [](Process process) {
        RegionConeActivationKernel kernel;
        kernel.program = std::move(process);
        return kernel;
    };
    constexpr auto canonical_source = "canonical_debug.sv";
    constexpr auto canonical_scope = "top.left";
    auto canonical_debug = make_debug_process(
        canonical_source, 29U, canonical_scope);
    auto equal_debug = make_debug_process(
        canonical_source, 29U, canonical_scope);
    require(share_process_operations(
                canonical_debug, equal_debug, std::span<const Signal> { }),
        "equal debug provenance can share one immutable operation body");
    require(canonical_debug.operations.shares_body_with(
                equal_debug.operations)
            && same_region_kernel_mapping(
                make_debug_kernel(canonical_debug),
                make_debug_kernel(equal_debug)),
        "equal effective debug overlays preserve mapping equality");

    auto source_representative = make_debug_process(
        canonical_source, 29U, canonical_scope);
    auto source_override = make_debug_process(
        "instance_debug.sv", 29U, canonical_scope);
    require(share_process_operations(source_representative,
                source_override, std::span<const Signal> { }),
        "different debug source is represented as an instance overlay");
    require(source_representative.operations.shares_body_with(
                source_override.operations),
        "debug source differences do not duplicate the shared body");
    require(!same_region_kernel_mapping(
                make_debug_kernel(source_representative),
                make_debug_kernel(source_override)),
        "effective per-instance debug source participates in mapping equality");

    auto scope_representative = make_debug_process(
        canonical_source, 29U, canonical_scope);
    auto scope_override = make_debug_process(
        canonical_source, 29U, "top.right");
    require(share_process_operations(scope_representative,
                scope_override, std::span<const Signal> { }),
        "different debug scope is represented as an instance overlay");
    require(scope_representative.operations.shares_body_with(
                scope_override.operations),
        "debug scope differences do not duplicate the shared body");
    require(!same_region_kernel_mapping(
                make_debug_kernel(scope_representative),
                make_debug_kernel(scope_override)),
        "effective per-instance debug scope participates in mapping equality");
}

void check_wave(std::size_t width, Mode mode, Logic4 initial = Logic4::zero)
{
    Interpreter interpreter;
    Probe probe { interpreter, mode, { }, { }, false };
    const auto trigger = interpreter.add_signal({ "trigger", PackedLogic4(1U, Logic4::zero) });
    const auto input = interpreter.add_signal({ "input", PackedLogic4(width, initial) });
    std::array<SignalId, 4U> outputs;
    for (ProcessId id = 0U; id < outputs.size(); ++id) {
        outputs[id] = interpreter.add_signal({ "out" + std::to_string(id),
            PackedLogic4(width, Logic4::z), ResolutionKind::sv_wire });
        Process process;
        process.id = id;
        process.name = "member" + std::to_string(id);
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.register_count = 1U;
        process.static_sensitivity = { { trigger, EdgeKind::any } };
        process.driver_regions = { { outputs[id], 0U, static_cast<std::uint32_t>(width) } };
        process.operations = { ReadSignal { 0U, input },
            WriteUpdate { outputs[id], 0U, SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U } };
        require(interpreter.add_process(std::move(process)) == id, "ordered member identity");
        const auto& registered = interpreter.process_program(id);
        interpreter.set_process_executor(id,
            std::make_unique<OrderedExecutor>(probe, id, input, outputs[id],
                ProcessExecutorProgramBinding {
                    registered, registered, id }));
    }
    Process clock;
    clock.id = 4U;
    clock.name = "clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.operations = { WaitFor { 1U }, LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { trigger, 0U }, Halt { } };
    static_cast<void>(interpreter.add_process(std::move(clock)));
    if (mode == Mode::coverage) {
        interpreter.set_code_coverage_counters({ 0U });
        require(interpreter.code_coverage_counters_configured(),
            "coverage control fixture must configure counters");
    }
    interpreter.set_signal_change_hook([&](SignalId signal, const PackedLogic4&, SimulationTick time) {
        if (signal != trigger || time != 1U) {
            return;
        }
        if (mode == Mode::outside_writer) {
            interpreter.scheduler().schedule_systemverilog(SchedulerPhase::active, 2U,
                [&](Scheduler&) {
                    probe.visits += 'x';
                    interpreter.deposit_signal(input, PackedLogic4(width, Logic4::one));
                });
        } else if (mode == Mode::observe) {
            interpreter.prepare_signal_observation(outputs[1]);
        }
    });
    bool failed { };
    try {
        const auto result = interpreter.run();
        require(result.status == (mode == Mode::stop ? RunStatus::stopped : RunStatus::completed),
            "ordered wave stop state");
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()) == "ordered wave injected failure";
        if (!failed) {
            throw;
        }
    }
    require(failed == (mode == Mode::failure || mode == Mode::post_failure), "ordered wave failure identity");
    if (mode == Mode::stop || mode == Mode::failure || mode == Mode::post_failure) {
        require(probe.visits == (mode == Mode::post_failure ? "abcd" : "a"),
            "the accepted native prefix remains committed before interruption");
        interpreter.scheduler().clear_stop();
        require(interpreter.run().status == RunStatus::completed, "ordered wave suffix resumes");
    }
    require(probe.visits == (mode == Mode::outside_writer ? "abxcd" : "abcd"),
        "SV wave preserves process order, outside writer and exactly-once resume");
    require(!probe.batches.empty(),
        "configured coverage retains the original ordered executor route");
    if (mode == Mode::outside_writer) {
        require(probe.batches == std::vector<std::vector<ProcessId>> { { 0U, 1U }, { 2U, 3U } },
            "SV wave must split at an outside writer");
    }
    if (mode == Mode::observe) {
        require(probe.batches == std::vector<std::vector<ProcessId>> { { 2U, 3U } },
            "late observation demotes only affected members");
    }
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        const auto expected = mode == Mode::outside_writer && index >= 2U ? Logic4::one : initial;
        require(interpreter.signal_value(outputs[index]) == PackedLogic4(width, expected),
            "ordered wave original-driver publication values");
    }
}

void check_structural_census_profile()
{
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::ostringstream captured;
    {
        ScopedCerrCapture capture { captured };
        {
            Interpreter interpreter;
            const auto trigger = interpreter.add_signal(
                { "trigger", PackedLogic4(1U, Logic4::zero) });
            const auto input = interpreter.add_signal(
                { "input", PackedLogic4(1U, Logic4::zero) });
            const auto output = interpreter.add_signal(
                { "output", PackedLogic4(1U, Logic4::zero) });
            Process process;
            process.id = 0U;
            process.name = "certificate_profile";
            process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
            process.register_count = 1U;
            process.static_sensitivity = { { trigger, EdgeKind::any } };
            process.driver_regions = { { output, 0U, 0U, true } };
            process.operations = {
                ReadSignal { 0U, input },
                WriteUpdate { output, 0U,
                    SignalUpdateDomain::systemverilog_active },
                WaitSensitivity { },
                Jump { 0U },
            };
            process.initialize = false;
            require(interpreter.add_process(std::move(process)) == 0U,
                "census profile fixture has one dense process identity");
            interpreter.start();
        }
    }

    const auto output = captured.str();
    const auto start = output.find(
        "fsim-profile: sv-region-structural-census ");
    require(start != std::string::npos,
        "SV wave profiling prints the structural certificate census");
    const auto end = output.find('\n', start);
    require(end != std::string::npos,
        "structural census profile terminates as one line");
    const auto line = output.substr(start, end - start);
    require(line.find(
                "components=1 structural_candidate_components=1 "
                "no_internal_state_components=0 "
                "incomplete_access_inventory_components=0 "
                "current_epoch_components=1 stale_epoch_components=0 "
                "current_epoch_structural_candidate_components=1 "
                "stale_epoch_structural_candidate_components=0 "
                "component_member_processes=1 "
                "structural_candidate_member_processes=1 "
                "boundary_signals=2 candidate_internal_signals=1 "
                "candidate_internal_none_resolution_signals=1 "
                "candidate_internal_sv_wire_requires_runtime_single_driver_proof_signals=0 "
                "candidate_internal_other_resolution_signals=0 "
                "graph_access_inventory_complete=1 "
                "opaque_operation_incidences=0 opaque_operation_kinds=none "
                "opaque_operation_kinds_omitted=0 "
                "opaque_operation_count_semantics=instruction_occurrences "
                "access_inventory_scope=whole_graph "
                "inventory_scope=build_snapshot "
                "runtime_execution_proof=not_evaluated "
                "reason_count_semantics=overlapping_incidences")
            != std::string::npos,
        "profile distinguishes structural NONE candidates from wire proof needs");
    require(line.find("process_exclusion_not_pure=0") != std::string::npos
            && line.find("boundary_no_internal_writer=2") != std::string::npos
            && line.find("boundary_access_inventory_incomplete=0")
                != std::string::npos,
        "profile gives named overlapping reason counts and complete inventory state");
}

struct ForwardingBoundaryRootInputSample {
    RegionKernelActivationOrigin origin;
    PackedLogic4 input;
    std::uint64_t input_revision { };
};

struct ForwardingRuntimeProbe {
    std::size_t activation_factories { };
    std::size_t activation_calls { };
    std::size_t forwarding_factories { };
    std::size_t forwarding_calls { };
    std::size_t failure_channel_polls { };
    bool origin_was_exact_member_seed_prefix { true };
    bool fail_next_forwarding { };
    bool ordinary_forwarding_decline_observed { };
    bool ordinary_decline_channel_empty { };
    bool failed_prefix_captured { };
    bool retried_prefix_captured { };
    bool retry_prefix_matches_failed_prefix { };
    RegionBackendPrefixWitness failed_prefix;
    RegionBackendPrefixWitness retried_prefix;
    std::exception_ptr armed_failure;
    std::exception_ptr pending_failure;
    std::vector<std::vector<ProcessId>> member_seed_prefixes;
    std::vector<RegionBackendPrefixWitness> forwarding_prefixes;
    bool capture_forwarding_prefixes { };
    ProcessId root_forwarding_sample_process {
        std::numeric_limits<ProcessId>::max() };
    SignalId root_forwarding_sample_signal {
        std::numeric_limits<SignalId>::max() };
    std::vector<ForwardingBoundaryRootInputSample>*
        root_forwarding_input_samples { };
    const std::vector<std::uint64_t>* signal_value_revisions { };
    bool root_forwarding_sample_capture_failed { };
    std::function<void()> after_forwarding_execution;
    std::function<void(const RegionKernelSchedulerPrefix&)>
        on_forwarding_execution;
    std::function<void(const RegionKernelSchedulerPrefix&,
        std::span<const PackedLogic4>)> on_forwarding_execution_with_inputs;
    std::function<void(std::span<const RegionConeKernelInput>,
        const RegionKernelActivationImage&)> on_activation_decline;
    bool activation_capture_failed { };
};

class DecliningForwardingActivationBackend final
    : public RegionKernelBackend {
public:
    explicit DecliningForwardingActivationBackend(
        ForwardingRuntimeProbe& probe,
        std::vector<RegionConeKernelInput> inputs) noexcept
        : probe_(probe)
        , inputs_(std::move(inputs))
    {
    }

    [[nodiscard]] bool execute(
        const RegionKernelActivationImage& image) noexcept override
    {
        ++probe_.activation_calls;
        if (probe_.on_activation_decline) {
            try {
                probe_.on_activation_decline(inputs_, image);
            } catch (...) {
                probe_.activation_capture_failed = true;
            }
        }
        return false;
    }

    [[nodiscard]] std::span<const PackedLogic4>
    activation_registers() const noexcept override
    {
        return { };
    }

private:
    ForwardingRuntimeProbe& probe_;
    std::vector<RegionConeKernelInput> inputs_;
};

class ReferenceForwardingBackend final
    : public RegionConeForwardingBackend
    , public RegionConeForwardingFailureBackend {
public:
    ReferenceForwardingBackend(
        ForwardingRuntimeProbe& probe,
        const RegionConeForwardingKernel& kernel)
        : probe_(probe)
        , kernel_(kernel)
    {
    }

    [[nodiscard]] bool execute_forwarding(
        const RegionKernelSchedulerPrefix& origin,
        const std::span<const PackedLogic4> boundary_inputs,
        const std::span<PackedLogic4> output_values) noexcept override
    {
        ++probe_.forwarding_calls;
        if (probe_.failed_prefix_captured
            && !probe_.retried_prefix_captured) {
            probe_.retried_prefix_captured
                = probe_.retried_prefix.capture(origin);
            probe_.retry_prefix_matches_failed_prefix
                = probe_.retried_prefix_captured
                && probe_.failed_prefix.matches(origin);
        }
        if (probe_.fail_next_forwarding) {
            probe_.fail_next_forwarding = false;
            probe_.ordinary_forwarding_decline_observed = true;
            probe_.failed_prefix_captured
                = probe_.failed_prefix.capture(origin);
            probe_.pending_failure = std::move(probe_.armed_failure);
            return false;
        }
        try {
            if (origin.tasks.empty()
                || origin.tasks.size() > kernel_.members.size()
                || boundary_inputs.size()
                    != kernel_.execution_kernel.inputs.size()
                || output_values.size()
                    != kernel_.execution_kernel.outputs.size()) {
                probe_.origin_was_exact_member_seed_prefix = false;
                return false;
            }
            if (probe_.capture_forwarding_prefixes) {
                RegionBackendPrefixWitness prefix_witness;
                if (!prefix_witness.capture(origin)) {
                    probe_.origin_was_exact_member_seed_prefix = false;
                    return false;
                }
                probe_.forwarding_prefixes.push_back(prefix_witness);
            }
            std::vector<ProcessId> seed_processes;
            seed_processes.reserve(origin.tasks.size());
            for (std::size_t task_index = 0U;
                 task_index < origin.tasks.size(); ++task_index) {
                const auto process = origin.tasks[task_index].member.process;
                const auto member = std::ranges::find(kernel_.members, process,
                    &RegionConeForwardingMember::process);
                if (member == kernel_.members.end()
                    || std::ranges::any_of(
                        origin.tasks.begin(),
                        origin.tasks.begin()
                            + static_cast<std::ptrdiff_t>(task_index),
                        [process](
                            const RegionKernelSchedulerPrefixTask& prior) {
                            return prior.member.process == process;
                        })) {
                    probe_.origin_was_exact_member_seed_prefix = false;
                    return false;
                }
                seed_processes.push_back(process);
            }
            probe_.member_seed_prefixes.push_back(std::move(seed_processes));
            if (probe_.on_forwarding_execution_with_inputs) {
                probe_.on_forwarding_execution_with_inputs(
                    origin, boundary_inputs);
            }
            if (probe_.root_forwarding_input_samples != nullptr) {
                const auto root_task = std::ranges::find_if(origin.tasks,
                    [&](const RegionKernelSchedulerPrefixTask& task) {
                        return task.member.process
                            == probe_.root_forwarding_sample_process;
                    });
                if (root_task != origin.tasks.end()) {
                    const auto root_input = std::ranges::find_if(
                        kernel_.execution_kernel.inputs,
                        [&](const RegionConeKernelInput& input) {
                            return input.signal
                                    == probe_.root_forwarding_sample_signal
                                && !input.internal;
                        });
                    if (root_input
                            == kernel_.execution_kernel.inputs.end()
                        || boundary_inputs.size()
                            != kernel_.execution_kernel.inputs.size()) {
                        probe_.root_forwarding_sample_capture_failed = true;
                        return false;
                    }
                    const auto input_index = static_cast<std::size_t>(
                        root_input
                            - kernel_.execution_kernel.inputs.begin());
                    if (probe_.signal_value_revisions == nullptr
                        || probe_.root_forwarding_sample_signal
                            >= probe_.signal_value_revisions->size()) {
                        probe_.root_forwarding_sample_capture_failed = true;
                        return false;
                    }
                    probe_.root_forwarding_input_samples->push_back({
                        root_task->member.origin,
                        boundary_inputs[input_index],
                        (*probe_.signal_value_revisions)[
                            probe_.root_forwarding_sample_signal] });
                }
            }
            RegionKernelActivationImage image;
            image.generation = origin.frontier_generation;
            image.scheduler_prefix = origin;
            for (std::size_t index = 0U;
                 index < kernel_.execution_kernel.inputs.size(); ++index) {
                const auto& input = kernel_.execution_kernel.inputs[index];
                if (input.internal || input.value_kind != ValueKind::logic4
                    || boundary_inputs[index].width() != input.width
                    || boundary_inputs[index].is_logic9()) {
                    return false;
                }
                image.register_inputs.push_back({ input.value_register,
                    boundary_inputs[index] });
            }
            for (const auto& member : kernel_.execution_kernel.members) {
                image.ready_processes.push_back(member.process);
                image.register_inputs.push_back({ member.readiness_register,
                    PackedLogic4(1U, Logic4::one) });
            }
            std::ranges::sort(image.register_inputs,
                std::ranges::less { },
                &RegionKernelRegisterInput::register_id);
            const auto registers
                = evaluate_region_activation_kernel_reference(
                    kernel_.execution_kernel, image);
            std::vector<PackedLogic4> staged_outputs;
            staged_outputs.reserve(kernel_.execution_kernel.outputs.size());
            for (std::size_t index = 0U;
                 index < kernel_.execution_kernel.outputs.size(); ++index) {
                const auto& binding
                    = kernel_.execution_kernel.outputs[index];
                if (binding.value_register >= registers.size()
                    || registers[binding.value_register].width()
                        != binding.width
                    || registers[binding.value_register].is_logic9()
                    || output_values[index].width() != binding.width
                    || output_values[index].is_logic9()
                    || binding.width == 0U
                    || binding.value_kind != ValueKind::logic4) {
                    return false;
                }
                staged_outputs.push_back(registers[binding.value_register]);
            }
            for (std::size_t index = 0U;
                 index < output_values.size(); ++index) {
                output_values[index] = std::move(staged_outputs[index]);
            }
            auto after_execution = std::move(probe_.after_forwarding_execution);
            if (after_execution) {
                after_execution();
            }
            if (probe_.on_forwarding_execution) {
                probe_.on_forwarding_execution(origin);
            }
        } catch (...) {
            return false;
        }
        return true;
    }

    [[nodiscard]] std::exception_ptr take_failure() noexcept override
    {
        ++probe_.failure_channel_polls;
        auto failure = std::exchange(probe_.pending_failure,
            std::exception_ptr { });
        if (probe_.ordinary_forwarding_decline_observed && !failure) {
            probe_.ordinary_decline_channel_empty = true;
        }
        return failure;
    }

private:
    ForwardingRuntimeProbe& probe_;
    RegionConeForwardingKernel kernel_;
};

class ForwardingRuntimeBackendProvider final
    : public RegionKernelBackendProvider
    , public RegionConeForwardingBackendProvider {
public:
    explicit ForwardingRuntimeBackendProvider(
        ForwardingRuntimeProbe& probe,
        const bool forwarding_enabled = true,
        const bool capture_activation_declines = false) noexcept
        : probe_(probe)
        , forwarding_enabled_(forwarding_enabled)
        , capture_activation_declines_(capture_activation_declines)
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return "runtime-forwarding-chain-test-v1";
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        ++probe_.activation_factories;
        if (capture_activation_declines_) {
            return std::make_unique<DecliningForwardingActivationBackend>(
                probe_, kernel.inputs);
        }
        return { };
    }

    [[nodiscard]] std::unique_ptr<RegionConeForwardingBackend>
    create_forwarding(const RegionConeForwardingKernel& kernel) override
    {
        ++probe_.forwarding_factories;
        if (!forwarding_enabled_) {
            return { };
        }
        return std::make_unique<ReferenceForwardingBackend>(probe_, kernel);
    }

private:
    ForwardingRuntimeProbe& probe_;
    bool forwarding_enabled_ { true };
    bool capture_activation_declines_ { };
};

struct ForwardingStageCutObservation {
    Interpreter* interpreter { };
    SignalId first_output { };
    SignalId second_output { };
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        second_transaction_before;
    std::uint64_t scheduled_round { };
    std::uint64_t last_poll_round { };
    std::uint64_t observer_round { };
    std::size_t polls { };
    bool observer_queued { };
    bool observer_ran { };
    bool observer_saw_expected_cut { };
    bool stop_after_observer { };
    bool throw_after_observer { };
    bool stop_requested { };
    bool exception_thrown { };
    bool timed_out { };
    PackedLogic4 first_at_cut;
    PackedLogic4 second_at_cut;
};

void run_forwarding_stage_cut_poll(
    const std::shared_ptr<ForwardingStageCutObservation>& observation,
    Scheduler& scheduler)
{
    ++observation->polls;
    observation->last_poll_round = scheduler.systemverilog_round();
    if (observation->polls > 8U) {
        observation->timed_out = true;
        return;
    }

    const auto& implementation = OwnedDriverDemotionTestAccess::implementation(
        *observation->interpreter);
    const auto has_pending_owner_update = [&](const ProcessId owner,
                                               const SignalId signal) {
        return std::ranges::any_of(implementation.systemverilog_update_slots,
            [&](const auto& slot) {
                return slot.occupied && slot.process == owner
                    && slot.signal == signal;
            });
    };
    if (!has_pending_owner_update(0U, observation->first_output)
        || !has_pending_owner_update(2U, observation->second_output)) {
        const auto scheduled_round = scheduler.systemverilog_round();
        scheduler.schedule_systemverilog(SchedulerPhase::active, 3U,
            [observation, scheduled_round](Scheduler& next_scheduler) {
                if (next_scheduler.systemverilog_round() <= scheduled_round) {
                    observation->timed_out = true;
                    return;
                }
                run_forwarding_stage_cut_poll(observation, next_scheduler);
            });
        return;
    }

    observation->observer_queued = true;
    const auto scheduled_round = scheduler.systemverilog_round();
    scheduler.schedule_systemverilog(SchedulerPhase::active, 1U,
        [observation, scheduled_round](Scheduler& observer_scheduler) {
            observation->observer_round
                = observer_scheduler.systemverilog_round();
            auto& active_implementation
                = OwnedDriverDemotionTestAccess::implementation(
                    *observation->interpreter);
            observation->first_at_cut = active_implementation.logical_signal_value(
                observation->first_output);
            observation->second_at_cut = active_implementation.logical_signal_value(
                observation->second_output);
            observation->observer_saw_expected_cut
                = observer_scheduler.systemverilog_round() > scheduled_round
                && active_implementation.signal_transactions.at(
                    observation->second_output)
                    == observation->second_transaction_before
                && observation->first_at_cut
                    == PackedLogic4(1U, Logic4::zero)
                && observation->second_at_cut
                    == PackedLogic4(1U, Logic4::zero);
            observation->observer_ran = true;
            if (observation->throw_after_observer) {
                observation->exception_thrown = true;
                throw std::runtime_error {
                    "forwarding stage cut injected exception"
                };
            }
            if (observation->stop_after_observer) {
                observation->stop_requested = true;
                observer_scheduler.request_stop();
            }
        });
}

enum class ForwardingStageCutKind {
    none,
    foreign_reader,
    stop,
    exception,
};

void check_region_forwarding_private_stage_batch()
{
    struct RunResult {
        std::size_t forwarding_calls { };
        bool resumed_after_stop { };
        bool resumed_after_exception { };
        bool queued_stage_batch_retained { };
        bool overlap_used_distinct_stage_batch { };
        bool queued_tokens_preserved_during_overlap { };
        bool drained_stage_batch_reused { };
        std::uint64_t stage_attempts { };
        std::uint64_t stage_dispatches { };
        std::uint64_t stage_members { };
        std::uint64_t stage_declines { };
        std::uint64_t stage_fallback_descriptors { };
        std::uint64_t private_parent_slots_elided { };
        std::uint64_t private_parent_dispatches { };
        std::uint64_t private_parent_fallbacks { };
        SchedulerBatchCompactionStats scheduler_delta;
        PackedLogic4 root_value;
        PackedLogic4 root_last_before;
        PackedLogic4 root_last_after;
        PackedLogic4 first_value;
        PackedLogic4 second_value;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            root_transaction_before;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            root_transaction_after;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            root_event_before;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            root_event_after;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            second_transaction_before;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            second_transaction_after;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            second_event_before;
        std::optional<std::pair<SimulationTick, std::uint64_t>>
            second_event_after;
        PackedLogic4 second_last_before;
        PackedLogic4 second_last_after;
        ForwardingStageCutObservation cut;
    };

    const auto run_case = [](const bool forwarding_enabled,
                              const ForwardingStageCutKind cut_kind) {
        ScopedEnvironment kernel_enabled {
            "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave_enabled {
            "FSIM_ENABLE_SV_LOCAL_WAVE", forwarding_enabled ? "1" : "0" };
        ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
        std::ostringstream profile_output;
        ScopedCerrCapture profile_capture { profile_output };
        RunResult result;
        ForwardingRuntimeProbe forwarding_probe;
        RegionKernelProbe executor_probe;
        Interpreter interpreter;
        const auto input = interpreter.add_signal({ "forward_stage.input",
            PackedLogic4(1U, Logic4::zero) });
        const auto internal = interpreter.add_signal({ "forward_stage.internal",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto first_output = interpreter.add_signal({
            "forward_stage.first", PackedLogic4(1U, Logic4::x),
            ResolutionKind::sv_wire });
        const auto second_output = interpreter.add_signal({
            "forward_stage.second", PackedLogic4(1U, Logic4::zero),
            ResolutionKind::sv_wire });

        Process first_child;
        first_child.id = 0U;
        first_child.name = "forward_stage_first_child";
        first_child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        first_child.initialize = true;
        first_child.register_count = 1U;
        first_child.static_sensitivity = { { internal, EdgeKind::any } };
        first_child.driver_regions = { { first_output, 0U, 0U, true } };
        first_child.operations = {
            ReadSignal { 0U, internal },
            UnaryNot { 0U, 0U },
            WriteUpdate { first_output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(first_child)) == 0U,
            "first forwarding child has the earlier original update key");

        Process root;
        root.id = 1U;
        root.name = "forward_stage_root";
        root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        root.initialize = true;
        root.register_count = 1U;
        root.static_sensitivity = { { input, EdgeKind::any } };
        root.driver_regions = { { internal, 0U, 0U, true } };
        root.operations = {
            ReadSignal { 0U, input },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(root)) == 1U,
            "forwarding root separates the two child update keys");

        Process second_child;
        second_child.id = 2U;
        second_child.name = "forward_stage_second_child";
        second_child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        second_child.initialize = true;
        second_child.register_count = 2U;
        second_child.static_sensitivity = { { internal, EdgeKind::any } };
        second_child.driver_regions = { { second_output, 0U, 0U, true } };
        second_child.operations = {
            ReadSignal { 0U, internal },
            ReadSignal { 1U, input },
            Binary { BinaryOperator::bit_xor, 0U, 0U, 1U },
            WriteUpdate { second_output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(second_child)) == 2U,
            "second forwarding child keeps its later original update key");

        const auto add_executor = [&](const ProcessId process,
                                      const SignalId source,
                                      const SignalId destination,
                                      const std::size_t probe_slot,
                                      const bool invert,
                                      const std::optional<SignalId> secondary_input,
                                      const bool xor_secondary,
                                      const std::size_t register_count) {
            const auto& registered = interpreter.process_program(process);
            const ProcessExecutorProgramBinding binding {
                registered, registered, process };
            const auto wait_instruction = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            DeferredProcessExecutorContract contract;
            contract.expected_access = binding;
            contract.callbacks_observation_safe = true;
            contract.expected_region_kernel_equivalent = true;
            interpreter.set_deferred_process_executor(process,
                [] { return true; },
                [&executor_probe, process, source, destination, probe_slot,
                    invert, secondary_input, xor_secondary, register_count,
                    binding, wait_instruction] {
                    return std::make_unique<RegionKernelExecutor>(
                        executor_probe, process, source, destination,
                        wait_instruction, binding, true, probe_slot,
                        true, true, register_count, std::nullopt,
                        secondary_input, true, invert, xor_secondary);
                }, std::move(contract));
        };
        add_executor(0U, internal, first_output, 0U, true,
            std::nullopt, false, 1U);
        add_executor(1U, input, internal, 1U, false,
            std::nullopt, false, 1U);
        add_executor(2U, internal, second_output, 2U, false,
            input, true, 2U);
        interpreter.materialize_ready_process_executors();

        Process clock;
        clock.id = 3U;
        clock.name = "forward_stage_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.driver_regions = { { input, 0U, 0U, true } };
        clock.operations = {
            WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { input, 0U },
            Halt { },
        };
        require(interpreter.add_process(std::move(clock)) == 3U,
            "forwarding stage clock stays outside the certified forest");

        result.cut.interpreter = &interpreter;
        result.cut.first_output = first_output;
        result.cut.second_output = second_output;
        auto cut_observation
            = std::make_shared<ForwardingStageCutObservation>();
        cut_observation->interpreter = &interpreter;
        cut_observation->first_output = first_output;
        cut_observation->second_output = second_output;
        cut_observation->stop_after_observer
            = cut_kind == ForwardingStageCutKind::stop;
        cut_observation->throw_after_observer
            = cut_kind == ForwardingStageCutKind::exception;
        if (forwarding_enabled) {
            interpreter.set_region_kernel_backend_provider(
                std::make_shared<ForwardingRuntimeBackendProvider>(
                    forwarding_probe));
        }

        interpreter.start();
        require(interpreter.run(0U).status == RunStatus::time_limit,
            "private-stage members reach their static waits");
        auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        auto forwarding_component
            = std::numeric_limits<std::size_t>::max();
        if (forwarding_enabled) {
            forwarding_component
                = implementation.region_component_by_process.at(1U);
            require(forwarding_component
                        < implementation.region_activation_programs.size()
                    && implementation.region_activation_programs[forwarding_component]
                    && implementation.region_activation_programs[forwarding_component]
                            ->forwarding_kernel.has_value(),
                "the root and both single-output children have a forwarding forest");
            const auto& forwarding
                = *implementation.region_activation_programs[forwarding_component]
                       ->forwarding_kernel;
            require(forwarding.members.size() == 3U
                    && forwarding.execution_kernel.outputs.size() == 3U
                    && std::ranges::all_of(forwarding.members,
                        [](const RegionConeForwardingMember& member) {
                            return member.output_count == 1U;
                        }),
                "each certified member owns one original update ticket");
        }

        const auto transaction_before
            = implementation.signal_transactions.at(second_output);
        cut_observation->second_transaction_before = transaction_before;
        const auto event_before = implementation.signal_events.at(second_output);
        const auto last_before = implementation.signal_last_values.at(second_output);
        result.root_last_before = implementation.signal_last_values.at(internal);
        result.root_transaction_before
            = implementation.signal_transactions.at(internal);
        result.root_event_before = implementation.signal_events.at(internal);
        const auto stage_attempts_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_attempts;
        const auto stage_dispatches_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_dispatches;
        const auto stage_members_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_members;
        const auto stage_declines_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_declines;
        const auto stage_fallback_descriptors_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors;
        const auto private_parent_slots_before
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided;
        const auto private_parent_dispatches_before
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
        const auto private_parent_fallbacks_before
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_fallbacks;
        const auto compaction_before
            = interpreter.scheduler().systemverilog_batch_compaction_stats();
        if (forwarding_enabled
            && cut_kind != ForwardingStageCutKind::none) {
            // Arm only after startup has settled: the cut belongs to the
            // timed forwarding wave, not an initial nonroot seed.
            forwarding_probe.after_forwarding_execution
                = [&interpreter, cut_observation] {
                    auto& implementation
                        = OwnedDriverDemotionTestAccess::implementation(
                            interpreter);
                    cut_observation->second_transaction_before
                        = implementation.signal_transactions.at(
                            cut_observation->second_output);
                    cut_observation->scheduled_round
                        = interpreter.scheduler().systemverilog_round();
                    interpreter.scheduler().schedule_systemverilog(
                        SchedulerPhase::active, 3U,
                        [cut_observation](Scheduler& scheduler) {
                            if (scheduler.systemverilog_round()
                                <= cut_observation->scheduled_round) {
                                cut_observation->timed_out = true;
                                return;
                            }
                            run_forwarding_stage_cut_poll(cut_observation,
                                scheduler);
                        });
                };
        }

        if (cut_kind == ForwardingStageCutKind::stop) {
            result.resumed_after_stop
                = interpreter.run().status == RunStatus::stopped
                && cut_observation->stop_requested
                && cut_observation->observer_saw_expected_cut;
            if (result.resumed_after_stop) {
                auto& forwarding_bank
                    = *implementation.region_local_wave_state_by_component
                            .at(forwarding_component)
                            ->forwarding_results;
                auto& stage_pool = forwarding_bank.stage_batch_pool;
                if (stage_pool.size() == 1U && stage_pool.front()) {
                    auto* const queued_batch = stage_pool.front().get();
                    const auto retained_capacity
                        = queued_batch->tokens.capacity();
                    std::vector<std::pair<std::size_t, std::uint64_t>>
                        retained_tokens;
                    retained_tokens.reserve(queued_batch->tokens.size());
                    for (const auto& token : queued_batch->tokens) {
                        retained_tokens.emplace_back(token.slot,
                            token.generation);
                    }
                    forwarding_bank.stage_batch.reset();
                    result.queued_stage_batch_retained
                        = stage_pool.front().use_count() > 1;
                    auto overlapping_batch
                        = forwarding_bank.acquire_stage_batch(&implementation,
                            forwarding_component,
                            implementation.region_runtime_generation,
                            interpreter.scheduler().now(),
                            retained_capacity);
                    result.overlap_used_distinct_stage_batch
                        = overlapping_batch.get() != queued_batch
                        && stage_pool.size() == 2U;
                    result.queued_tokens_preserved_during_overlap
                        = queued_batch->tokens.size()
                            == retained_tokens.size()
                        && queued_batch->tokens.capacity()
                            == retained_capacity;
                    for (std::size_t index = 0U;
                         result.queued_tokens_preserved_during_overlap
                            && index < retained_tokens.size();
                         ++index) {
                        const auto& token = queued_batch->tokens[index];
                        result.queued_tokens_preserved_during_overlap
                            = token.owner == &implementation
                            && token.slot == retained_tokens[index].first
                            && token.generation
                                == retained_tokens[index].second;
                    }
                    overlapping_batch.reset();
                }
                interpreter.scheduler().clear_stop();
                result.resumed_after_stop
                    = interpreter.run().status == RunStatus::completed;
                if (result.resumed_after_stop
                    && result.queued_stage_batch_retained) {
                    auto* const completed_batch = stage_pool.front().get();
                    const auto capacity = completed_batch->tokens.capacity();
                    auto reused_batch = forwarding_bank.acquire_stage_batch(
                        &implementation, forwarding_component,
                        implementation.region_runtime_generation,
                        interpreter.scheduler().now(), capacity);
                    result.drained_stage_batch_reused
                        = reused_batch.get() == completed_batch
                        && stage_pool.size() == 2U
                        && completed_batch->tokens.empty()
                        && completed_batch->tokens.capacity() == capacity;
                    reused_batch.reset();
                }
            }
        } else if (cut_kind == ForwardingStageCutKind::exception) {
            try {
                (void)interpreter.run();
            } catch (const std::runtime_error& error) {
                result.resumed_after_exception
                    = std::string_view(error.what())
                        == "forwarding stage cut injected exception"
                    && cut_observation->exception_thrown
                    && cut_observation->observer_saw_expected_cut;
            }
            if (result.resumed_after_exception) {
                result.resumed_after_exception
                    = interpreter.run().status == RunStatus::completed;
            }
        } else {
            require(interpreter.run().status == RunStatus::completed,
                "the private-stage fixture drains every original owner update");
        }
        const auto compaction_after
            = interpreter.scheduler().systemverilog_batch_compaction_stats();
        result.scheduler_delta.tickets
            = compaction_after.tickets - compaction_before.tickets;
        result.scheduler_delta.members
            = compaction_after.members - compaction_before.members;
        result.scheduler_delta.entries_elided
            = compaction_after.entries_elided - compaction_before.entries_elided;
        result.stage_attempts
            = implementation.systemverilog_wave_profile_region_forwarding_stage_attempts
            - stage_attempts_before;
        result.stage_dispatches
            = implementation.systemverilog_wave_profile_region_forwarding_stage_dispatches
            - stage_dispatches_before;
        result.stage_members
            = implementation.systemverilog_wave_profile_region_forwarding_stage_members
            - stage_members_before;
        result.stage_declines
            = implementation.systemverilog_wave_profile_region_forwarding_stage_declines
            - stage_declines_before;
        result.stage_fallback_descriptors
            = implementation.systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors
            - stage_fallback_descriptors_before;
        result.private_parent_slots_elided
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
            - private_parent_slots_before;
        result.private_parent_dispatches
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches
            - private_parent_dispatches_before;
        result.private_parent_fallbacks
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_fallbacks
            - private_parent_fallbacks_before;
        result.forwarding_calls = forwarding_probe.forwarding_calls;
        result.root_value = implementation.logical_signal_value(internal);
        result.root_last_after = implementation.signal_last_values.at(internal);
        result.root_transaction_after
            = implementation.signal_transactions.at(internal);
        result.root_event_after = implementation.signal_events.at(internal);
        result.first_value = implementation.logical_signal_value(first_output);
        result.second_value = implementation.logical_signal_value(second_output);
        result.second_transaction_before = transaction_before;
        result.second_transaction_after
            = implementation.signal_transactions.at(second_output);
        result.second_event_before = event_before;
        result.second_event_after = implementation.signal_events.at(second_output);
        result.second_last_before = last_before;
        result.second_last_after = implementation.signal_last_values.at(second_output);
        result.cut = *cut_observation;
        result.cut.interpreter = nullptr;
        return result;
    };

    const auto checked = run_case(false, ForwardingStageCutKind::none);
    const auto compacted = run_case(true, ForwardingStageCutKind::none);
    const auto interrupted = run_case(true,
        ForwardingStageCutKind::foreign_reader);
    const auto stopped = run_case(true, ForwardingStageCutKind::stop);
    const auto exceptional = run_case(true,
        ForwardingStageCutKind::exception);
    require(compacted.stage_dispatches == 3U
            && compacted.stage_members == 3U
            && compacted.forwarding_calls > 0U
            && compacted.private_parent_slots_elided == 1U
            && compacted.private_parent_dispatches == 1U
            && compacted.private_parent_fallbacks == 0U
            && compacted.stage_declines == 0U
            && compacted.stage_fallback_descriptors == 0U
            && compacted.scheduler_delta.tickets >= 1U
            && compacted.scheduler_delta.members >= 2U
            && compacted.scheduler_delta.entries_elided >= 1U,
        "one singleton parent fans out through its private original-key commit while each output key permits lower-key work to interpose");
    require(checked.forwarding_calls == 0U
            && checked.stage_dispatches == 0U
            && checked.stage_members == 0U,
        "the local-wave-disabled reference takes the ordinary callback route");
    require(compacted.root_value == checked.root_value
            && compacted.root_last_before == checked.root_last_before
            && compacted.root_last_after == checked.root_last_after
            && compacted.root_transaction_before
                == checked.root_transaction_before
            && compacted.root_transaction_after
                == checked.root_transaction_after
            && compacted.root_event_before == checked.root_event_before
            && compacted.root_event_after == checked.root_event_after
            && compacted.first_value == checked.first_value
            && compacted.second_value == checked.second_value
            && compacted.root_value == PackedLogic4(1U, Logic4::one)
            && compacted.first_value == PackedLogic4(1U, Logic4::zero)
            && compacted.second_value == PackedLogic4(1U, Logic4::zero),
        "private-stage compaction preserves the ordinary three-process result");
    require(compacted.second_transaction_after
                != compacted.second_transaction_before
            && compacted.second_event_after == compacted.second_event_before
            && compacted.second_last_after == compacted.second_last_before,
        "the equal-value second owner update advances transaction metadata without an event or LAST change");
    require(interrupted.stage_dispatches == 3U
            && interrupted.stage_members == 3U
            && interrupted.forwarding_calls > 0U
            && interrupted.stage_declines == 0U
            && interrupted.scheduler_delta.entries_elided >= 2U
            && interrupted.cut.observer_queued
            && interrupted.cut.observer_ran
            && interrupted.cut.observer_saw_expected_cut
            && !interrupted.cut.timed_out
            && interrupted.cut.observer_round
                > interrupted.cut.scheduled_round
            && interrupted.cut.last_poll_round
                > interrupted.cut.scheduled_round
            && interrupted.cut.polls >= 2U,
        "a callback-created Active continuation runs in a later round and cuts the grouped ticket at its original foreign key");
    require(interrupted.root_value == checked.root_value
            && interrupted.first_value == checked.first_value
            && interrupted.second_value == checked.second_value
            && interrupted.second_transaction_after
                != interrupted.second_transaction_before,
        "the foreign-key cut preserves both original output commits and their final values");
    require(stopped.resumed_after_stop
            && stopped.stage_dispatches == 3U
            && stopped.stage_members == 3U
            && stopped.queued_stage_batch_retained
            && stopped.overlap_used_distinct_stage_batch
            && stopped.queued_tokens_preserved_during_overlap
            && stopped.drained_stage_batch_reused
            && stopped.stage_declines == 0U
            && stopped.cut.observer_ran
            && stopped.cut.observer_saw_expected_cut
            && !stopped.cut.timed_out
            && stopped.cut.last_poll_round
                > stopped.cut.scheduled_round
            && stopped.second_transaction_after
                != stopped.second_transaction_before
            && stopped.first_value == checked.first_value
            && stopped.second_value == checked.second_value,
        "stop at a foreign key preserves the untouched output-ticket suffix for resume");
    require(exceptional.resumed_after_exception
            && exceptional.stage_dispatches == 3U
            && exceptional.stage_members == 3U
            && exceptional.stage_declines == 0U
            && exceptional.cut.observer_ran
            && exceptional.cut.observer_saw_expected_cut
            && exceptional.cut.exception_thrown
            && !exceptional.cut.timed_out
            && exceptional.cut.last_poll_round
                > exceptional.cut.scheduled_round
            && exceptional.second_transaction_after
                != exceptional.second_transaction_before
            && exceptional.first_value == checked.first_value
            && exceptional.second_value == checked.second_value,
        "an exception at a foreign key leaves the exact original ticket suffix for retry");
}

enum class PrivateParentOutputCut {
    none,
    equal_key_foreign,
    late_observation,
    late_observation_then_throw,
    stop_before_parent,
    throw_before_parent,
    throw_after_parent,
};

struct PrivateParentOutputCutObservation {
    Interpreter* interpreter { };
    SignalId internal { };
    PackedLogic4 current_before;
    PackedLogic4 last_before;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        transaction_before;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event_before;
    PackedLogic4 observed;
    bool ran { };
    bool saw_expected_cut { };
    bool observer_read { };
    bool stop_requested { };
    bool exception_thrown { };
    bool first_parent_published_with_second_pending { };
    std::function<std::size_t()> private_records_consumed_probe;
    std::size_t pending_private_records_at_cut { };
    bool private_output_order_correct { };
};

struct PrivateParentOutputResult {
    bool completed { };
    bool interrupted_before_parent { };
    bool resumed { };
    bool private_record_pending_while_stopped { };
    bool private_batch_pinned_after_bank_release { };
    bool private_record_consumed_once_after_resume { };
    bool private_output_order_correct { };
    bool lower_key_child_saw_parent_output_suffix { };
    bool private_fallback_record_consumed_after_throw { };
    bool caught_exception { };
    bool observation_fell_back { };
    bool postpublication_exception_saw_parent { };
    std::uint64_t slot_elisions { };
    std::uint64_t private_dispatches { };
    std::uint64_t private_fallbacks { };
    std::uint64_t fallback_descriptors { };
    std::uint64_t public_update_tokens { };
    std::size_t forwarding_calls { };
    PackedLogic4 internal_value;
    PackedLogic4 internal_last;
    PackedLogic4 middle_value;
    PackedLogic4 middle_last;
    PackedLogic4 child_value;
    PackedLogic4 child_last;
    PackedLogic4 additional_parent_value;
    PackedLogic4 additional_parent_last;
    PackedLogic4 additional_child_value;
    PackedLogic4 additional_child_last;
    std::array<PackedLogic4, 4U> internal_roles;
    std::array<PackedLogic4, 4U> additional_parent_roles;
    std::array<PackedLogic4, 4U> child_roles;
    std::array<PackedLogic4, 4U> additional_child_roles;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        transaction_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        middle_transaction_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        middle_event_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        child_transaction_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        child_event_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        additional_parent_transaction_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        additional_parent_event_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        additional_child_transaction_after;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        additional_child_event_after;
    PrivateParentOutputCutObservation cut;
};

PrivateParentOutputResult run_private_parent_output_case(
    const bool forwarding_enabled,
    const PrivateParentOutputCut cut_kind,
    const bool three_member_chain = false,
    const bool multioutput_parent = false,
    const SimulationTick second_output_delay = 0U,
    const std::uint32_t value_width = 1U,
    const bool second_output_boundary = false)
{
    require(!multioutput_parent || !three_member_chain,
        "the multi-output fixture keeps a single parent level");
    require(!second_output_boundary || multioutput_parent,
        "a second-output boundary requires the multi-output parent");
    require(value_width != 0U,
        "the private-parent fixture keeps a nonempty value width");
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    ForwardingRuntimeProbe forwarding_probe;
    RegionKernelProbe executor_probe;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "private_parent.input",
        PackedLogic4(value_width, Logic4::zero) });
    const auto internal = interpreter.add_signal({ "private_parent.internal",
        PackedLogic4(value_width, Logic4::zero), ResolutionKind::sv_wire });
    std::optional<SignalId> additional_parent_output;
    if (multioutput_parent) {
        additional_parent_output = interpreter.add_signal({
            "private_parent.additional_parent_output",
            PackedLogic4(value_width, Logic4::zero), ResolutionKind::sv_wire });
    }
    std::optional<SignalId> middle_internal;
    if (three_member_chain) {
        middle_internal = interpreter.add_signal({
            "private_parent.middle_internal",
            PackedLogic4(value_width, Logic4::zero), ResolutionKind::sv_wire });
    }
    std::optional<SignalId> additional_child_output;
    if (multioutput_parent && !second_output_boundary) {
        additional_child_output = interpreter.add_signal({
            "private_parent.additional_child_output",
            PackedLogic4(value_width, Logic4::zero), ResolutionKind::sv_wire });
    }
    const auto output = interpreter.add_signal({ "private_parent.output",
        PackedLogic4(value_width,
            three_member_chain ? Logic4::one : Logic4::zero),
        ResolutionKind::sv_wire });

    Process child;
    child.id = 0U;
    child.name = "private_parent_child";
    child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    child.initialize = true;
    child.register_count
        = multioutput_parent && !second_output_boundary ? 2U : 1U;
    const auto child_input = middle_internal.value_or(internal);
    child.static_sensitivity = { { child_input, EdgeKind::any } };
    if (additional_parent_output && !second_output_boundary) {
        child.static_sensitivity.push_back(
            { *additional_parent_output, EdgeKind::any });
    }
    child.driver_regions = { { output, 0U, 0U, true } };
    child.operations.push_back(ReadSignal { 0U, child_input });
    if (multioutput_parent && !second_output_boundary) {
        child.operations.push_back(
            ReadSignal { 1U, *additional_parent_output });
        child.operations.push_back(
            Binary { BinaryOperator::bit_xor, 0U, 0U, 1U });
    } else if (three_member_chain) {
        child.operations.push_back(UnaryNot { 0U, 0U });
    } else {
        child.operations.push_back(CopyRegister { 0U, 0U });
    }
    child.operations.push_back(WriteUpdate { output, 0U,
        SignalUpdateDomain::systemverilog_active });
    child.operations.push_back(WaitSensitivity { });
    child.operations.push_back(Jump { 0U });
    require(interpreter.add_process(std::move(child)) == 0U,
        "private-parent fixture puts the boundary child at key zero");

    Process parent;
    parent.id = 1U;
    parent.name = "private_parent_root";
    parent.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    parent.initialize = true;
    parent.register_count = 1U;
    parent.static_sensitivity = { { input, EdgeKind::any } };
    parent.driver_regions = { { internal, 0U, 0U, true } };
    if (additional_parent_output) {
        parent.driver_regions.push_back(
            { *additional_parent_output, 0U, 0U, true });
    }
    parent.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { internal, 0U,
            SignalUpdateDomain::systemverilog_active },
    };
    if (additional_parent_output) {
        if (second_output_delay == 0U) {
            parent.operations.push_back(WriteUpdate {
                *additional_parent_output, 0U,
                SignalUpdateDomain::systemverilog_active });
        } else {
            parent.operations.push_back(WriteProjected {
                *additional_parent_output, 0U, second_output_delay, 0U,
                ProjectedDelayMode::inertial });
        }
    }
    parent.operations.push_back(WaitSensitivity { });
    parent.operations.push_back(Jump { 0U });
    require(interpreter.add_process(std::move(parent)) == 1U,
        "the private parent retains its original key one");
    if (second_output_delay != 0U) {
        const auto& registered_parent = interpreter.process_program(1U);
        const auto operation = registered_parent.operations.expanded(2U);
        const auto* const projected
            = operation_get_if<WriteProjected>(&operation);
        require(projected != nullptr
                && projected->signal == *additional_parent_output
                && projected->delay == second_output_delay
                && projected->rejection == 0U
                && projected->mode == ProjectedDelayMode::inertial,
            "the negative parent keeps an exact delayed projected second output");
    }

    const auto add_executor = [&](const ProcessId process,
                                  const SignalId source,
                                  const SignalId destination,
                                  const std::size_t probe_slot,
                                  const bool invert_result,
                                  const std::optional<SignalId> second_output
                                      = std::nullopt,
                                  const std::optional<SignalId> secondary_input
                                      = std::nullopt,
                                  const bool xor_secondary = false,
                                  const std::size_t expected_register_count
                                      = 1U) {
        const auto& registered = interpreter.process_program(process);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process,
            [] { return true; },
                [&executor_probe, process, source, destination, probe_slot,
                binding, wait_instruction, invert_result, second_output,
                secondary_input, xor_secondary, expected_register_count,
                value_width] {
                    return std::make_unique<RegionKernelExecutor>(
                        executor_probe, process, source, destination,
                        wait_instruction, binding, true, probe_slot, true,
                        true, expected_register_count, second_output,
                        secondary_input, true, invert_result,
                        xor_secondary, nullptr, value_width);
                }, std::move(contract));
    };
    std::vector<std::pair<PackedLogic4, PackedLogic4>>
        lower_key_child_samples;
    if (multioutput_parent) {
        executor_probe.after_process_sample
            = [&interpreter, additional_parent_output,
                  &lower_key_child_samples](
                  const ProcessId process, const PackedLogic4& sampled) {
                  if (process != 0U || !additional_parent_output) {
                      return;
                  }
                  auto& active_implementation
                      = OwnedDriverDemotionTestAccess::implementation(
                          interpreter);
                  lower_key_child_samples.emplace_back(sampled,
                      active_implementation.logical_signal_value(
                          *additional_parent_output));
              };
    }
    if (multioutput_parent && !second_output_boundary) {
        add_executor(0U, child_input, output, 0U, false, std::nullopt,
            additional_parent_output, true, 2U);
    } else {
        add_executor(0U, child_input, output, 0U, three_member_chain);
    }
    if (second_output_delay == 0U) {
        add_executor(1U, input, internal, 1U, false,
            additional_parent_output);
    }
    if (three_member_chain) {
        Process middle;
        middle.id = 2U;
        middle.name = "private_parent_middle";
        middle.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        middle.initialize = true;
        middle.register_count = 1U;
        middle.static_sensitivity = { { internal, EdgeKind::any } };
        middle.driver_regions = { { *middle_internal, 0U, 0U, true } };
        middle.operations = {
            ReadSignal { 0U, internal }, CopyRegister { 0U, 0U },
            WriteUpdate { *middle_internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(middle)) == 2U,
            "the non-topological middle process keeps its original key two");
        add_executor(2U, internal, *middle_internal, 2U, false);
    }
    if (multioutput_parent && !second_output_boundary) {
        Process additional_child;
        additional_child.id = 2U;
        additional_child.name = "private_parent_additional_child";
        additional_child.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        additional_child.initialize = true;
        additional_child.register_count = 1U;
        additional_child.static_sensitivity = {
            { *additional_parent_output, EdgeKind::any } };
        additional_child.driver_regions = {
            { *additional_child_output, 0U, 0U, true } };
        additional_child.operations = {
            ReadSignal { 0U, *additional_parent_output },
            CopyRegister { 0U, 0U },
            WriteUpdate { *additional_child_output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(additional_child)) == 2U,
            "the second parent output has a child at its own original process key");
        add_executor(2U, *additional_parent_output,
            *additional_child_output, 2U, false);
    }
    interpreter.materialize_ready_process_executors();

    Process clock;
    const bool has_third_process
        = three_member_chain || (multioutput_parent && !second_output_boundary);
    const ProcessId clock_id = has_third_process ? 3U : 2U;
    clock.id = clock_id;
    clock.name = "private_parent_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(value_width, Logic4::one) },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == clock_id,
        "the private-parent stimulus remains outside the two-member component");

    Process boundary_observer;
    const ProcessId boundary_observer_id = clock_id + 1U;
    boundary_observer.id = boundary_observer_id;
    boundary_observer.name = "private_parent_boundary_observer";
    boundary_observer.scheduling_domain = ProcessSchedulingDomain::generic;
    boundary_observer.register_count = 1U;
    boundary_observer.static_sensitivity = { { output, EdgeKind::any } };
    boundary_observer.operations = {
        ReadSignal { 0U, output }, WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(boundary_observer))
            == boundary_observer_id,
        "an ordinary reader makes the child result a real graph boundary");
    if (additional_child_output) {
        Process additional_boundary_observer;
        const auto additional_observer_id = boundary_observer_id + 1U;
        additional_boundary_observer.id = additional_observer_id;
        additional_boundary_observer.name
            = "private_parent_additional_boundary_observer";
        additional_boundary_observer.scheduling_domain
            = ProcessSchedulingDomain::generic;
        additional_boundary_observer.register_count = 1U;
        additional_boundary_observer.static_sensitivity = {
            { *additional_child_output, EdgeKind::any } };
        additional_boundary_observer.operations = {
            ReadSignal { 0U, *additional_child_output },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(
                    std::move(additional_boundary_observer))
                == additional_observer_id,
            "the second child output has an ordinary boundary observer");
    } else if (second_output_boundary) {
        Process additional_parent_boundary_observer;
        const auto additional_observer_id = boundary_observer_id + 1U;
        additional_parent_boundary_observer.id = additional_observer_id;
        additional_parent_boundary_observer.name
            = "private_parent_additional_parent_boundary_observer";
        additional_parent_boundary_observer.scheduling_domain
            = ProcessSchedulingDomain::generic;
        additional_parent_boundary_observer.register_count = 1U;
        additional_parent_boundary_observer.static_sensitivity = {
            { *additional_parent_output, EdgeKind::any } };
        additional_parent_boundary_observer.operations = {
            ReadSignal { 0U, *additional_parent_output },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(
                    std::move(additional_parent_boundary_observer))
                == additional_observer_id,
            "the second parent output remains a checked component boundary");
    }

    interpreter.set_region_kernel_backend_provider(
        std::make_shared<ForwardingRuntimeBackendProvider>(
            forwarding_probe, forwarding_enabled));

    interpreter.start();
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "the private parent, children, and boundaries reach their static waits");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component = implementation.region_component_by_process.at(1U);
    if (forwarding_enabled && second_output_delay == 0U) {
        require(component < implementation.region_activation_programs.size()
                && implementation.region_activation_programs[component]
                && implementation.region_activation_programs[component]
                        ->forwarding_kernel.has_value(),
            "the two-member parent-to-boundary-child shape has a forwarding proof");
        const auto& forwarding
            = *implementation.region_activation_programs[component]
                   ->forwarding_kernel;
        const auto expected_members
            = three_member_chain
                    || (multioutput_parent && !second_output_boundary)
            ? 3U : 2U;
        const auto expected_outputs
            = 2U + (three_member_chain ? 1U : 0U)
            + (multioutput_parent ? 1U : 0U)
            + (additional_child_output ? 1U : 0U);
        const auto expected_internal_signals
            = three_member_chain
                    || (multioutput_parent && !second_output_boundary)
            ? 2U : 1U;
        require(forwarding.members.size() == expected_members
                && forwarding.execution_kernel.outputs.size()
                    == expected_outputs
                && forwarding.internal_signals.size()
                    == expected_internal_signals,
                three_member_chain
                ? "the ProcessId order differs from the three-member forwarding topology"
                : "the private-parent fixture retains its expected internal and boundary signal inventory");
        if (three_member_chain) {
            const auto root_member = std::ranges::find(
                forwarding.members, ProcessId { 1U },
                &RegionConeForwardingMember::process);
            const auto middle_member = std::ranges::find(
                forwarding.members, ProcessId { 2U },
                &RegionConeForwardingMember::process);
            const auto leaf_member = std::ranges::find(
                forwarding.members, ProcessId { 0U },
                &RegionConeForwardingMember::process);
            require(root_member != forwarding.members.end()
                    && middle_member != forwarding.members.end()
                    && leaf_member != forwarding.members.end()
                    && forwarding.topological_member_indices.size() == 3U
                    && forwarding.topological_member_indices[0U]
                        == static_cast<std::size_t>(
                            root_member - forwarding.members.begin())
                    && forwarding.topological_member_indices[1U]
                        == static_cast<std::size_t>(
                            middle_member - forwarding.members.begin())
                    && forwarding.topological_member_indices[2U]
                        == static_cast<std::size_t>(
                            leaf_member - forwarding.members.begin())
                    && middle_member->dependency_count == 1U
                    && forwarding.dependencies[
                           middle_member->dependency_begin]
                           .writer_member_index
                        == static_cast<std::size_t>(
                            root_member - forwarding.members.begin())
                    && leaf_member->dependency_count == 1U
                    && forwarding.dependencies[
                           leaf_member->dependency_begin]
                           .writer_member_index
                        == static_cast<std::size_t>(
                            middle_member - forwarding.members.begin()),
                "the three-member chain is root 1 to middle 2 to leaf 0");
            require(!std::ranges::binary_search(
                        forwarding.internal_signals, output)
                    && boundary_observer_id
                        < implementation.region_component_by_process.size()
                    && implementation.region_component_by_process[
                           boundary_observer_id]
                        == std::numeric_limits<std::size_t>::max(),
                "the generic read-only observer keeps the leaf output at a real component boundary");
        }
        if (multioutput_parent && !second_output_boundary) {
            const auto root_member = std::ranges::find(
                forwarding.members, ProcessId { 1U },
                &RegionConeForwardingMember::process);
            const auto first_child_member = std::ranges::find(
                forwarding.members, ProcessId { 0U },
                &RegionConeForwardingMember::process);
            const auto second_child_member = std::ranges::find(
                forwarding.members, ProcessId { 2U },
                &RegionConeForwardingMember::process);
            const auto first_parent_output = std::ranges::find_if(
                forwarding.execution_kernel.outputs,
                [&](const RegionConeOutputBinding& binding) {
                    return binding.owner == 1U
                        && binding.signal == internal;
                });
            const auto second_parent_output = std::ranges::find_if(
                forwarding.execution_kernel.outputs,
                [&](const RegionConeOutputBinding& binding) {
                    return binding.owner == 1U
                        && binding.signal == *additional_parent_output;
                });
            require(root_member != forwarding.members.end()
                    && root_member->output_count == 2U
                    && first_child_member != forwarding.members.end()
                    && second_child_member != forwarding.members.end()
                    && first_child_member->depth == root_member->depth + 1U
                    && second_child_member->depth == root_member->depth + 1U
                    && first_child_member->dependency_count == 2U
                    && second_child_member->dependency_count == 1U
                    && first_child_member->dependency_begin + 1U
                        < forwarding.dependencies.size()
                    && forwarding.dependencies[
                           first_child_member->dependency_begin]
                           .writer_member_index
                        == static_cast<std::size_t>(
                            root_member - forwarding.members.begin())
                    && forwarding.dependencies[
                           first_child_member->dependency_begin + 1U]
                           .writer_member_index
                        == static_cast<std::size_t>(
                            root_member - forwarding.members.begin())
                    && ((forwarding.dependencies[
                             first_child_member->dependency_begin].signal
                             == internal
                            && forwarding.dependencies[
                                   first_child_member->dependency_begin + 1U]
                                   .signal == *additional_parent_output)
                        || (forwarding.dependencies[
                                first_child_member->dependency_begin].signal
                                == *additional_parent_output
                            && forwarding.dependencies[
                                   first_child_member->dependency_begin + 1U]
                                   .signal == internal))
                    && forwarding.dependencies[
                           second_child_member->dependency_begin]
                           .writer_member_index
                        == static_cast<std::size_t>(
                            root_member - forwarding.members.begin())
                    && forwarding.dependencies[
                           second_child_member->dependency_begin].signal
                        == *additional_parent_output
                    && first_parent_output
                        != forwarding.execution_kernel.outputs.end()
                    && second_parent_output
                        != forwarding.execution_kernel.outputs.end()
                    && first_parent_output->source_instruction
                        < second_parent_output->source_instruction
                    && std::ranges::binary_search(
                        forwarding.internal_signals, internal)
                    && std::ranges::binary_search(
                        forwarding.internal_signals,
                        *additional_parent_output)
                    && implementation.region_component_by_process.at(
                           boundary_observer_id)
                        == std::numeric_limits<std::size_t>::max()
                    && implementation.region_component_by_process.at(
                           boundary_observer_id + 1U)
                        == std::numeric_limits<std::size_t>::max(),
                "both original parent update keys own internal signals with direct depth-one readers and stable source order");
        } else if (second_output_boundary) {
            const auto root_member = std::ranges::find(
                forwarding.members, ProcessId { 1U },
                &RegionConeForwardingMember::process);
            const auto child_member = std::ranges::find(
                forwarding.members, ProcessId { 0U },
                &RegionConeForwardingMember::process);
            const auto first_parent_output = std::ranges::find_if(
                forwarding.execution_kernel.outputs,
                [&](const RegionConeOutputBinding& binding) {
                    return binding.owner == 1U
                        && binding.signal == internal;
                });
            const auto second_parent_output = std::ranges::find_if(
                forwarding.execution_kernel.outputs,
                [&](const RegionConeOutputBinding& binding) {
                    return binding.owner == 1U
                        && binding.signal == *additional_parent_output;
                });
            require(root_member != forwarding.members.end()
                    && root_member->output_count == 2U
                    && child_member != forwarding.members.end()
                    && child_member->depth == root_member->depth + 1U
                    && child_member->dependency_count == 1U
                    && forwarding.dependencies[
                           child_member->dependency_begin].signal == internal
                    && first_parent_output
                        != forwarding.execution_kernel.outputs.end()
                    && second_parent_output
                        != forwarding.execution_kernel.outputs.end()
                    && first_parent_output->source_instruction
                        < second_parent_output->source_instruction
                    && std::ranges::binary_search(
                        forwarding.internal_signals, internal)
                    && !std::ranges::binary_search(
                        forwarding.internal_signals,
                        *additional_parent_output)
                    && implementation.region_component_by_process.at(
                           boundary_observer_id + 1U)
                        == std::numeric_limits<std::size_t>::max(),
                "a mixed-boundary parent keeps its public second output outside the private parent shape");
        }
    }

    auto cut_observation
        = std::make_shared<PrivateParentOutputCutObservation>();
    cut_observation->interpreter = &interpreter;
    cut_observation->internal = internal;
    cut_observation->current_before
        = implementation.logical_signal_value(internal);
    cut_observation->last_before
        = implementation.signal_last_values.at(internal);
    cut_observation->transaction_before
        = implementation.signal_transactions.at(internal);
    cut_observation->event_before
        = implementation.signal_events.at(internal);
    const auto slot_elisions_before
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided;
    const auto private_dispatches_before
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
    const auto private_fallbacks_before
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_fallbacks;
    const auto public_update_tokens_before
        = implementation.systemverilog_wave_profile_region_forwarding_public_update_tokens;
    const auto fallback_descriptors_before
        = implementation.systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors;

    if (cut_kind != PrivateParentOutputCut::none) {
        const auto capture_private_records
            = [&interpreter, cut_observation, component, internal,
                  additional_parent_output, multioutput_parent] {
                  auto& active_implementation
                      = OwnedDriverDemotionTestAccess::implementation(
                          interpreter);
                  if (component >= active_implementation
                          .region_local_wave_state_by_component.size()) {
                      return;
                  }
                  const auto& local_state
                      = active_implementation
                            .region_local_wave_state_by_component[component];
                  if (!local_state || !local_state->forwarding_results
                      || !local_state->forwarding_results->stage_batch) {
                      return;
                  }
                  const auto retained_batch
                      = local_state->forwarding_results->stage_batch;
                  cut_observation->pending_private_records_at_cut
                      = static_cast<std::size_t>(std::ranges::count_if(
                          retained_batch->private_outputs,
                          [component](const auto& record) {
                              return record.pending && record.process == 1U
                                  && record.component == component;
                          }));
                  const auto expected_size = multioutput_parent ? 2U : 1U;
                  const auto matches = [component](const auto& record,
                                            const SignalId signal) {
                      return record.process == 1U && record.signal == signal
                          && record.component == component
                          && record.runtime_generation != 0U
                          && record.origin.process_domain
                              == ProcessSchedulingDomain::systemverilog
                          && record.origin.phase == SchedulerPhase::active;
                  };
                  const auto& records = retained_batch->private_outputs;
                  cut_observation->private_output_order_correct
                      = records.size() == expected_size
                      && matches(records[0U], internal)
                      && (!multioutput_parent
                          || (additional_parent_output
                              && matches(records[1U],
                                  *additional_parent_output)));
              };
        const auto queue_foreign_cut
            = [&interpreter, cut_observation, cut_kind, internal, component,
                  additional_parent_output, multioutput_parent,
                  capture_private_records, value_width] {
                if (cut_kind
                    == PrivateParentOutputCut::late_observation_then_throw) {
                    interpreter.scheduler().schedule_systemverilog(
                        SchedulerPhase::active, 1U,
                        [cut_observation, internal, component,
                            additional_parent_output, multioutput_parent,
                            capture_private_records,
                            value_width](Scheduler&) {
                            capture_private_records();
                            auto& active_implementation
                                = OwnedDriverDemotionTestAccess::implementation(
                                    *cut_observation->interpreter);
                            cut_observation->ran = true;
                            cut_observation->saw_expected_cut
                                = active_implementation.logical_signal_value(
                                    internal)
                                    == cut_observation->current_before
                                && active_implementation.signal_last_values.at(
                                       internal)
                                    == cut_observation->last_before
                                && active_implementation.signal_transactions.at(
                                       internal)
                                    == cut_observation->transaction_before
                                && active_implementation.signal_events.at(
                                       internal)
                                    == cut_observation->event_before;
                            if (component < active_implementation
                                    .region_local_wave_state_by_component.size()) {
                                const auto& local_state
                                    = active_implementation
                                          .region_local_wave_state_by_component[
                                              component];
                                if (local_state
                                    && local_state->forwarding_results
                                    && local_state->forwarding_results
                                           ->stage_batch) {
                                    const auto retained_batch
                                        = local_state->forwarding_results
                                              ->stage_batch;
                                    cut_observation
                                        ->private_records_consumed_probe
                                        = [retained_batch, component] {
                                            return static_cast<std::size_t>(
                                                std::ranges::count_if(
                                                    retained_batch->private_outputs,
                                                    [component](const auto& record) {
                                                        return !record.pending
                                                            && record.process
                                                                == 1U
                                                            && record.component
                                                                == component;
                                                    }));
                                        };
                                }
                            }
                            cut_observation->interpreter->set_signal_change_hook(
                                [cut_observation, internal,
                                    additional_parent_output,
                                    multioutput_parent, value_width](
                                    const SignalId changed_signal,
                                    const PackedLogic4& changed_value,
                                    const SimulationTick) {
                                    if (changed_signal != internal) {
                                        return;
                                    }
                                    auto& publishing_implementation
                                        = OwnedDriverDemotionTestAccess::implementation(
                                            *cut_observation->interpreter);
                                    cut_observation->saw_expected_cut
                                        = changed_value
                                                == PackedLogic4(value_width,
                                                    Logic4::one)
                                        && publishing_implementation
                                                .signal_transactions.at(internal)
                                            != cut_observation->transaction_before
                                        && publishing_implementation
                                                .signal_last_values.at(internal)
                                            == cut_observation->last_before
                                        && publishing_implementation
                                                .signal_events.at(internal)
                                            != cut_observation->event_before;
                                    if (multioutput_parent
                                        && additional_parent_output) {
                                        cut_observation
                                            ->first_parent_published_with_second_pending
                                            = changed_value
                                                == PackedLogic4(value_width,
                                                    Logic4::one)
                                            && publishing_implementation
                                                   .logical_signal_value(
                                                       *additional_parent_output)
                                                == PackedLogic4(value_width,
                                                    Logic4::zero)
                                            && cut_observation
                                                   ->private_records_consumed_probe
                                            && cut_observation
                                                   ->private_records_consumed_probe()
                                                == 1U;
                                    }
                                    cut_observation->exception_thrown = true;
                                    throw std::runtime_error {
                                        "private-parent fallback hook injected exception"
                                    };
                                });
                            cut_observation->observed
                                = cut_observation->interpreter->signal_value(
                                    internal);
                            cut_observation->observer_read = true;
                        });
                    return;
                }

                const bool after_parent
                    = cut_kind == PrivateParentOutputCut::throw_after_parent;
                const auto stable_key = after_parent ? 2U : 1U;
                interpreter.scheduler().schedule_systemverilog(
                    SchedulerPhase::active, stable_key,
                    [cut_observation, cut_kind, internal,
                        capture_private_records, value_width](
                        Scheduler& scheduler) {
                        capture_private_records();
                        auto& active_implementation
                            = OwnedDriverDemotionTestAccess::implementation(
                                *cut_observation->interpreter);
                        cut_observation->ran = true;
                        const auto current
                            = active_implementation.logical_signal_value(internal);
                        const auto transaction
                            = active_implementation.signal_transactions.at(internal);
                        if (cut_kind
                                == PrivateParentOutputCut::throw_after_parent) {
                            cut_observation->saw_expected_cut
                                = current
                                    == PackedLogic4(value_width, Logic4::one)
                                && transaction
                                    != cut_observation->transaction_before;
                            cut_observation->exception_thrown = true;
                            throw std::runtime_error {
                                "private-parent postpublication cut injected exception"
                            };
                        }

                        cut_observation->saw_expected_cut
                            = current == cut_observation->current_before
                            && active_implementation.signal_last_values.at(internal)
                                == cut_observation->last_before
                            && transaction
                                == cut_observation->transaction_before
                            && active_implementation.signal_events.at(internal)
                                == cut_observation->event_before;
                        if (cut_kind
                            == PrivateParentOutputCut::late_observation) {
                            cut_observation->observed
                                = cut_observation->interpreter->signal_value(
                                    internal);
                            cut_observation->observer_read = true;
                        } else if (cut_kind
                            == PrivateParentOutputCut::stop_before_parent) {
                            cut_observation->stop_requested = true;
                            scheduler.request_stop();
                        } else if (cut_kind
                            == PrivateParentOutputCut::throw_before_parent) {
                            cut_observation->exception_thrown = true;
                            throw std::runtime_error {
                                "private-parent prepublication cut injected exception"
                            };
                        }
                    });
            };
        if (forwarding_enabled) {
            forwarding_probe.after_forwarding_execution = queue_foreign_cut;
        } else {
            executor_probe.after_root_sample = queue_foreign_cut;
        }
    }

    PrivateParentOutputResult result;
    result.cut = *cut_observation;
    try {
        const auto first = interpreter.run();
        result.interrupted_before_parent
            = first.status == RunStatus::stopped
            && cut_observation->stop_requested;
        if (result.interrupted_before_parent) {
            result.interrupted_before_parent
                = implementation.signal_transactions.at(internal)
                    == cut_observation->transaction_before;
            if (result.interrupted_before_parent && forwarding_enabled) {
                auto& forwarding_bank
                    = *implementation.region_local_wave_state_by_component
                            .at(component)->forwarding_results;
                auto& stage_pool = forwarding_bank.stage_batch_pool;
                if (stage_pool.size() == 1U && stage_pool.front()) {
                    auto* const queued_batch = stage_pool.front().get();
                    const auto is_parent_output
                        = [internal, additional_parent_output, component](
                              const auto& private_output) {
                              return private_output.process == 1U
                                  && private_output.component == component
                                  && (private_output.signal == internal
                                      || (additional_parent_output
                                          && private_output.signal
                                              == *additional_parent_output));
                          };
                    const auto expected_private_records
                        = multioutput_parent ? 2U : 1U;
                    const auto pending_private_records
                        = static_cast<std::size_t>(std::ranges::count_if(
                            queued_batch->private_outputs,
                            [&](const auto& private_output) {
                                return private_output.pending
                                    && is_parent_output(private_output);
                            }));
                    forwarding_bank.stage_batch.reset();
                    result.private_batch_pinned_after_bank_release
                        = stage_pool.front().use_count() > 1;
                    const auto retained_batch = stage_pool.front();
                    result.private_record_pending_while_stopped
                        = pending_private_records
                            == expected_private_records
                        && implementation
                                .systemverilog_wave_profile_region_forwarding_private_parent_dispatches
                            == private_dispatches_before;

                    interpreter.scheduler().clear_stop();
                    result.resumed
                        = interpreter.run().status == RunStatus::completed;
                    const auto consumed_private_records
                        = static_cast<std::size_t>(std::ranges::count_if(
                            retained_batch->private_outputs,
                            [&](const auto& private_output) {
                                return !private_output.pending
                                    && is_parent_output(private_output);
                            }));
                    // Releasing the active bank leaves the queued parent
                    // pinned, while later chain members use their ordinary
                    // update slots when the bank is reacquired.
                    result.private_record_consumed_once_after_resume
                        = result.resumed
                        && consumed_private_records
                            == expected_private_records
                        && implementation
                                .systemverilog_wave_profile_region_forwarding_private_parent_dispatches
                            == private_dispatches_before
                                + expected_private_records;
                } else {
                    interpreter.scheduler().clear_stop();
                    result.resumed
                        = interpreter.run().status == RunStatus::completed;
                }
            } else if (result.interrupted_before_parent) {
                interpreter.scheduler().clear_stop();
                result.resumed
                    = interpreter.run().status == RunStatus::completed;
            }
        } else {
            result.completed = first.status == RunStatus::completed;
        }
    } catch (const std::runtime_error& error) {
        const auto message = std::string_view(error.what());
        const bool after_parent_publication
            = message
                == "private-parent postpublication cut injected exception"
            || message
                == "private-parent fallback hook injected exception";
        result.caught_exception
            = (message
                    == "private-parent prepublication cut injected exception"
                && cut_observation->exception_thrown
                && implementation.signal_transactions.at(internal)
                    == cut_observation->transaction_before)
            || (after_parent_publication
                && cut_observation->exception_thrown
                && implementation.logical_signal_value(internal)
                    == PackedLogic4(value_width, Logic4::one)
                && implementation.signal_transactions.at(internal)
                    != cut_observation->transaction_before);
        result.postpublication_exception_saw_parent
            = after_parent_publication
            && cut_observation->saw_expected_cut;
        result.resumed
            = result.caught_exception
            && interpreter.run().status == RunStatus::completed;
    }

    result.slot_elisions
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
        - slot_elisions_before;
    result.private_dispatches
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches
        - private_dispatches_before;
    result.private_fallbacks
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_fallbacks
        - private_fallbacks_before;
    result.public_update_tokens
        = implementation.systemverilog_wave_profile_region_forwarding_public_update_tokens
        - public_update_tokens_before;
    result.fallback_descriptors
        = implementation.systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors
        - fallback_descriptors_before;
    result.forwarding_calls = forwarding_probe.forwarding_calls;
    result.internal_value = implementation.logical_signal_value(internal);
    result.internal_last = implementation.signal_last_values.at(internal);
    result.internal_roles = read_full_output_roles(implementation, internal, 1U);
    if (middle_internal) {
        result.middle_value
            = implementation.logical_signal_value(*middle_internal);
        result.middle_last
            = implementation.signal_last_values.at(*middle_internal);
        result.middle_transaction_after
            = implementation.signal_transactions.at(*middle_internal);
        result.middle_event_after
            = implementation.signal_events.at(*middle_internal);
    }
    result.child_value = implementation.logical_signal_value(output);
    result.child_last = implementation.signal_last_values.at(output);
    result.child_transaction_after
        = implementation.signal_transactions.at(output);
    result.child_event_after = implementation.signal_events.at(output);
    result.child_roles = read_full_output_roles(implementation, output, 0U);
    if (additional_parent_output) {
        result.additional_parent_value
            = implementation.logical_signal_value(*additional_parent_output);
        result.additional_parent_last
            = implementation.signal_last_values.at(*additional_parent_output);
        result.additional_parent_transaction_after
            = implementation.signal_transactions.at(*additional_parent_output);
        result.additional_parent_event_after
            = implementation.signal_events.at(*additional_parent_output);
        result.additional_parent_roles = read_full_output_roles(
            implementation, *additional_parent_output, 1U);
    }
    if (additional_child_output) {
        result.additional_child_value
            = implementation.logical_signal_value(*additional_child_output);
        result.additional_child_last
            = implementation.signal_last_values.at(*additional_child_output);
        result.additional_child_transaction_after
            = implementation.signal_transactions.at(*additional_child_output);
        result.additional_child_event_after
            = implementation.signal_events.at(*additional_child_output);
        result.additional_child_roles = read_full_output_roles(
            implementation, *additional_child_output, 2U);
    }
    result.lower_key_child_saw_parent_output_suffix
        = std::ranges::any_of(lower_key_child_samples,
            [value_width](const auto& sample) {
                return sample.first == PackedLogic4(value_width, Logic4::one)
                    && sample.second == PackedLogic4(value_width, Logic4::zero);
            });
    result.private_output_order_correct
        = cut_observation->private_output_order_correct;
    result.transaction_after = implementation.signal_transactions.at(internal);
    result.event_after = implementation.signal_events.at(internal);
    result.observation_fell_back
        = result.private_fallbacks != 0U
        && cut_observation->observer_read
        && cut_observation->observed == cut_observation->current_before;
    result.private_fallback_record_consumed_after_throw
        = result.resumed
        && static_cast<bool>(cut_observation->private_records_consumed_probe)
        && cut_observation->private_records_consumed_probe()
            == (multioutput_parent ? 2U : 1U)
        && result.private_dispatches == (multioutput_parent ? 2U : 1U)
        && result.private_fallbacks != 0U;
    cut_observation->private_records_consumed_probe = { };
    result.cut = *cut_observation;
    result.cut.interpreter = nullptr;
    result.cut.private_records_consumed_probe = { };
    return result;
}

void check_region_forwarding_private_parent_update_elision()
{
    const auto checked = run_private_parent_output_case(false,
        PrivateParentOutputCut::equal_key_foreign);
    const auto native = run_private_parent_output_case(true,
        PrivateParentOutputCut::equal_key_foreign);
    const auto checked_late_observer = run_private_parent_output_case(false,
        PrivateParentOutputCut::late_observation);
    const auto late_observer = run_private_parent_output_case(true,
        PrivateParentOutputCut::late_observation);
    const auto checked_late_observer_throw = run_private_parent_output_case(
        false, PrivateParentOutputCut::late_observation_then_throw);
    const auto late_observer_throw = run_private_parent_output_case(true,
        PrivateParentOutputCut::late_observation_then_throw);
    const auto checked_stopped = run_private_parent_output_case(false,
        PrivateParentOutputCut::stop_before_parent);
    const auto stopped = run_private_parent_output_case(true,
        PrivateParentOutputCut::stop_before_parent);
    const auto checked_prepublication_throw = run_private_parent_output_case(
        false, PrivateParentOutputCut::throw_before_parent);
    const auto prepublication_throw = run_private_parent_output_case(true,
        PrivateParentOutputCut::throw_before_parent);
    const auto checked_postpublication_throw = run_private_parent_output_case(
        false, PrivateParentOutputCut::throw_after_parent);
    const auto postpublication_throw = run_private_parent_output_case(true,
        PrivateParentOutputCut::throw_after_parent);
    const auto checked_chain = run_private_parent_output_case(false,
        PrivateParentOutputCut::equal_key_foreign, true);
    const auto native_chain = run_private_parent_output_case(true,
        PrivateParentOutputCut::equal_key_foreign, true);
    const auto checked_chain_late = run_private_parent_output_case(false,
        PrivateParentOutputCut::late_observation, true);
    const auto chain_late = run_private_parent_output_case(true,
        PrivateParentOutputCut::late_observation, true);
    const auto checked_chain_stopped = run_private_parent_output_case(false,
        PrivateParentOutputCut::stop_before_parent, true);
    const auto chain_stopped = run_private_parent_output_case(true,
        PrivateParentOutputCut::stop_before_parent, true);
    const auto checked_chain_exception = run_private_parent_output_case(false,
        PrivateParentOutputCut::throw_after_parent, true);
    const auto chain_exception = run_private_parent_output_case(true,
        PrivateParentOutputCut::throw_after_parent, true);
    const auto checked_multioutput = run_private_parent_output_case(false,
        PrivateParentOutputCut::equal_key_foreign, false, true);
    const auto multioutput = run_private_parent_output_case(true,
        PrivateParentOutputCut::equal_key_foreign, false, true);
    const auto checked_multioutput_late = run_private_parent_output_case(false,
        PrivateParentOutputCut::late_observation, false, true);
    const auto multioutput_late = run_private_parent_output_case(true,
        PrivateParentOutputCut::late_observation, false, true);
    const auto checked_multioutput_late_throw
        = run_private_parent_output_case(false,
            PrivateParentOutputCut::late_observation_then_throw,
            false, true);
    const auto multioutput_late_throw
        = run_private_parent_output_case(true,
            PrivateParentOutputCut::late_observation_then_throw,
            false, true);
    const auto checked_multioutput_stopped = run_private_parent_output_case(
        false, PrivateParentOutputCut::stop_before_parent, false, true);
    const auto multioutput_stopped = run_private_parent_output_case(
        true, PrivateParentOutputCut::stop_before_parent, false, true);
    const auto checked_multioutput_delayed = run_private_parent_output_case(
        false, PrivateParentOutputCut::none, false, true, 1U);
    const auto multioutput_delayed = run_private_parent_output_case(
        true, PrivateParentOutputCut::none, false, true, 1U);
    const auto checked_multioutput_boundary = run_private_parent_output_case(
        false, PrivateParentOutputCut::none, false, true, 0U, 1U, true);
    const auto multioutput_boundary = run_private_parent_output_case(
        true, PrivateParentOutputCut::none, false, true, 0U, 1U, true);

    const auto require_chain_parity = [](const PrivateParentOutputResult& left,
                                          const PrivateParentOutputResult& right,
                                          const char* const description) {
        require(left.internal_value == right.internal_value
                && left.internal_last == right.internal_last
                && left.middle_value == right.middle_value
                && left.middle_last == right.middle_last
                && left.child_value == right.child_value
                && left.child_last == right.child_last
                && left.transaction_after == right.transaction_after
                && left.event_after == right.event_after
                && left.middle_transaction_after
                    == right.middle_transaction_after
                && left.middle_event_after == right.middle_event_after
                && left.child_transaction_after
                    == right.child_transaction_after
                && left.child_event_after == right.child_event_after,
            description);
    };
    const auto require_multioutput_parity
        = [](const PrivateParentOutputResult& left,
              const PrivateParentOutputResult& right,
              const char* const description) {
              require(left.internal_value == right.internal_value
                      && left.internal_last == right.internal_last
                      && left.additional_parent_value
                          == right.additional_parent_value
                      && left.additional_parent_last
                          == right.additional_parent_last
                      && left.child_value == right.child_value
                      && left.child_last == right.child_last
                      && left.additional_child_value
                          == right.additional_child_value
                      && left.additional_child_last
                          == right.additional_child_last
                      && left.internal_roles == right.internal_roles
                      && left.additional_parent_roles
                          == right.additional_parent_roles
                      && left.child_roles == right.child_roles
                      && left.additional_child_roles
                          == right.additional_child_roles
                      && left.transaction_after == right.transaction_after
                      && left.event_after == right.event_after
                      && left.additional_parent_transaction_after
                          == right.additional_parent_transaction_after
                      && left.additional_parent_event_after
                          == right.additional_parent_event_after
                      && left.child_transaction_after
                          == right.child_transaction_after
                      && left.child_event_after == right.child_event_after
                      && left.additional_child_transaction_after
                          == right.additional_child_transaction_after
                      && left.additional_child_event_after
                          == right.additional_child_event_after,
                  description);
          };

    require(native.completed && native.forwarding_calls > 0U
            && native.slot_elisions == 1U
            && native.private_dispatches == 1U
            && native.private_fallbacks == 0U
            && native.public_update_tokens == 1U
            && native.cut.ran && native.cut.saw_expected_cut,
        "a certified parent output uses one private stage record at its equal-key ordering cut and constructs no parent update token");
    require(native.internal_value == checked.internal_value
            && native.internal_last == checked.internal_last
            && native.child_value == checked.child_value
            && native.transaction_after != native.cut.transaction_before
            && native.transaction_after == checked.transaction_after
            && native.event_after == checked.event_after
            && native.event_after != native.cut.event_before
            && native.internal_value == PackedLogic4(1U, Logic4::one)
            && native.internal_last == PackedLogic4(1U, Logic4::zero)
            && native.child_value == PackedLogic4(1U, Logic4::one),
        "private parent publication preserves current, LAST, transaction, event, and the child boundary result");
    require(checked.forwarding_calls == 0U
            && checked.slot_elisions == 0U
            && checked.private_dispatches == 0U
            && native.internal_value == checked.internal_value
            && native.internal_last == checked.internal_last
            && native.transaction_after == checked.transaction_after
            && native.event_after == checked.event_after,
        "the ordinary and private routes preserve identical transaction and event stamps under the same foreign key");
    require(late_observer.completed
            && late_observer.slot_elisions == 1U
            && late_observer.private_dispatches == 1U
            && late_observer.observation_fell_back
            && late_observer.internal_value == checked_late_observer.internal_value
            && late_observer.internal_last == checked_late_observer.internal_last
            && late_observer.child_value == checked_late_observer.child_value
            && late_observer.transaction_after
                == checked_late_observer.transaction_after
            && late_observer.event_after == checked_late_observer.event_after,
        "a late public observation rejects private publication and commits the retained parent value through the ordinary driver path once");
    require(late_observer_throw.caught_exception
            && late_observer_throw.resumed
            && late_observer_throw.observation_fell_back
            && late_observer_throw.private_dispatches == 1U
            && late_observer_throw.private_fallbacks == 1U
            // The accepted batch commits its checked fallback inline; a
            // scheduler descriptor is needed only when the whole batch declines.
            && late_observer_throw.fallback_descriptors == 0U
            && late_observer_throw.private_fallback_record_consumed_after_throw
            && late_observer_throw.internal_value
                == checked_late_observer_throw.internal_value
            && late_observer_throw.internal_last
                == checked_late_observer_throw.internal_last
            && late_observer_throw.child_value
                == checked_late_observer_throw.child_value
            && late_observer_throw.transaction_after
                == checked_late_observer_throw.transaction_after
            && late_observer_throw.event_after
                == checked_late_observer_throw.event_after,
        "a later exception cannot replay an already-consumed late-observation fallback record");
    require(stopped.interrupted_before_parent && stopped.resumed
            && checked_stopped.interrupted_before_parent
            && checked_stopped.resumed
            && stopped.private_record_pending_while_stopped
            && stopped.private_batch_pinned_after_bank_release
            && stopped.private_record_consumed_once_after_resume
            && stopped.slot_elisions == 1U
            && stopped.private_dispatches == 1U
            && stopped.private_fallbacks == 0U
            && stopped.cut.saw_expected_cut
            && stopped.internal_value == checked_stopped.internal_value
            && stopped.internal_last == checked_stopped.internal_last
            && stopped.child_value == checked_stopped.child_value
            && stopped.transaction_after == checked_stopped.transaction_after
            && stopped.event_after == checked_stopped.event_after,
        "stop before the retained parent key preserves its private record for exact resume");
    require(prepublication_throw.caught_exception
            && prepublication_throw.resumed
            && checked_prepublication_throw.caught_exception
            && checked_prepublication_throw.resumed
            && prepublication_throw.slot_elisions == 1U
            && prepublication_throw.private_dispatches == 1U
            && prepublication_throw.private_fallbacks == 0U
            && prepublication_throw.cut.saw_expected_cut
            && prepublication_throw.internal_value
                == checked_prepublication_throw.internal_value
            && prepublication_throw.internal_last
                == checked_prepublication_throw.internal_last
            && prepublication_throw.child_value
                == checked_prepublication_throw.child_value
            && prepublication_throw.transaction_after
                == checked_prepublication_throw.transaction_after
            && prepublication_throw.event_after
                == checked_prepublication_throw.event_after,
        "a prepublication exception leaves the original private output key pending for one retry");
    require(postpublication_throw.caught_exception
            && postpublication_throw.resumed
            && checked_postpublication_throw.caught_exception
            && checked_postpublication_throw.resumed
            && postpublication_throw.postpublication_exception_saw_parent
            && postpublication_throw.slot_elisions == 1U
            && postpublication_throw.private_dispatches == 1U
            && postpublication_throw.private_fallbacks == 0U
            && postpublication_throw.internal_value
                == checked_postpublication_throw.internal_value
            && postpublication_throw.internal_last
                == checked_postpublication_throw.internal_last
            && postpublication_throw.child_value
                == checked_postpublication_throw.child_value
            && postpublication_throw.transaction_after
                == checked_postpublication_throw.transaction_after
            && postpublication_throw.event_after
                == checked_postpublication_throw.event_after,
        "an exception after the original parent publication cannot replay that consumed private output");
    require(native_chain.completed && native_chain.forwarding_calls > 0U
            && native_chain.slot_elisions == 2U
            && native_chain.private_dispatches == 2U
            && native_chain.private_fallbacks == 0U
            && native_chain.public_update_tokens == 1U
            && native_chain.cut.ran && native_chain.cut.saw_expected_cut
            && native_chain.internal_value == PackedLogic4(1U, Logic4::one)
            && native_chain.internal_last == PackedLogic4(1U, Logic4::zero)
            && native_chain.middle_value == PackedLogic4(1U, Logic4::one)
            && native_chain.middle_last == PackedLogic4(1U, Logic4::zero)
            && native_chain.child_value == PackedLogic4(1U, Logic4::zero),
        "a non-topological three-member chain privately stages both single-output internal parents and keeps the boundary update keyed");
    require_chain_parity(native_chain, checked_chain,
        "the three-member private chain preserves checked current, LAST, transaction, and event metadata");
    require(chain_late.completed && chain_late.slot_elisions == 1U
            && chain_late.private_dispatches >= 1U
            && chain_late.private_fallbacks == 1U
            && chain_late.observation_fell_back,
        "late observation invalidates the retained private chain result and uses the checked owner path");
    require_chain_parity(chain_late, checked_chain_late,
        "late-observation fallback preserves all chain role metadata");
    require(chain_stopped.interrupted_before_parent && chain_stopped.resumed
            && chain_stopped.private_record_pending_while_stopped
            && chain_stopped.private_batch_pinned_after_bank_release
            && chain_stopped.private_record_consumed_once_after_resume
            && chain_stopped.slot_elisions == 1U
            && chain_stopped.private_dispatches == 1U
            && chain_stopped.private_fallbacks == 0U,
        "stop and resume consumes the pinned parent once and continues the chain through ordinary update slots");
    require_chain_parity(chain_stopped, checked_chain_stopped,
        "stop and resume preserves current, LAST, transactions, and events for the full chain");
    require(chain_exception.caught_exception && chain_exception.resumed
            && chain_exception.slot_elisions == 2U
            && chain_exception.private_dispatches == 2U
            && chain_exception.private_fallbacks == 0U,
        "an exception after the first chain publication resumes only the original pending suffix");
    require_chain_parity(chain_exception, checked_chain_exception,
        "exception-prefix recovery preserves the checked chain metadata");
    require(multioutput.completed && multioutput.forwarding_calls > 0U
            && multioutput.slot_elisions == 2U
            && multioutput.private_dispatches == 2U
            && multioutput.private_fallbacks == 0U
            && multioutput.public_update_tokens == 2U
            && multioutput.cut.pending_private_records_at_cut == 2U
            && multioutput.cut.saw_expected_cut
            && multioutput.private_output_order_correct
            && !multioutput.lower_key_child_saw_parent_output_suffix
            && !checked_multioutput.lower_key_child_saw_parent_output_suffix
            && multioutput.internal_value == PackedLogic4(1U, Logic4::one)
            && multioutput.additional_parent_value
                == PackedLogic4(1U, Logic4::one)
            && multioutput.child_value == PackedLogic4(1U, Logic4::zero)
            && multioutput.child_last == PackedLogic4(1U, Logic4::x)
            && multioutput.additional_child_value
                == PackedLogic4(1U, Logic4::one),
        "a two-output parent publishes both private records in the frozen Active round before the child enters the next round");
    require_multioutput_parity(multioutput, checked_multioutput,
        "multi-output private publication preserves current, LAST, stored, raw, transaction, event, and both child outputs");
    for (const std::uint32_t width : { 65U, 129U }) {
        const auto checked_wide_multioutput
            = run_private_parent_output_case(false,
                PrivateParentOutputCut::equal_key_foreign,
                false, true, 0U, width);
        const auto wide_multioutput
            = run_private_parent_output_case(true,
                PrivateParentOutputCut::equal_key_foreign,
                false, true, 0U, width);
        require(wide_multioutput.completed
                && wide_multioutput.forwarding_calls > 0U
                && wide_multioutput.slot_elisions == 2U
                && wide_multioutput.private_dispatches == 2U
                && wide_multioutput.private_fallbacks == 0U
                && wide_multioutput.public_update_tokens == 2U
                && wide_multioutput.cut.pending_private_records_at_cut == 2U
                && wide_multioutput.cut.saw_expected_cut
                && wide_multioutput.private_output_order_correct
                && !wide_multioutput.lower_key_child_saw_parent_output_suffix
                && !checked_wide_multioutput.lower_key_child_saw_parent_output_suffix
                && wide_multioutput.internal_value
                    == PackedLogic4(width, Logic4::one)
                && wide_multioutput.additional_parent_value
                    == PackedLogic4(width, Logic4::one)
                && wide_multioutput.child_value
                    == PackedLogic4(width, Logic4::zero)
                && wide_multioutput.child_last
                    == PackedLogic4(width, Logic4::x)
                && wide_multioutput.additional_child_value
                    == PackedLogic4(width, Logic4::one),
            "wide multi-output parents preserve frozen-round publication across word boundaries");
        require_multioutput_parity(wide_multioutput,
            checked_wide_multioutput,
            "wide multi-output private publication preserves full-plane values and metadata");
    }
    require(multioutput_stopped.interrupted_before_parent
            && multioutput_stopped.resumed
            && multioutput_stopped.private_record_pending_while_stopped
            && multioutput_stopped.private_batch_pinned_after_bank_release
            && multioutput_stopped.private_record_consumed_once_after_resume
            && multioutput_stopped.slot_elisions == 2U
            && multioutput_stopped.private_dispatches == 2U
            && multioutput_stopped.private_fallbacks == 0U,
        "stop and resume retains both original parent keys and consumes each private output once");
    require_multioutput_parity(multioutput_stopped,
        checked_multioutput_stopped,
        "multi-output stop and resume preserves every signal role and stamp");
    require(multioutput_late.completed
            && multioutput_late.slot_elisions == 2U
            && multioutput_late.private_dispatches == 2U
            && multioutput_late.private_fallbacks != 0U
            && multioutput_late.observation_fell_back,
        "a late observation invalidates the multi-output private suffix and uses ordinary publication");
    require_multioutput_parity(multioutput_late, checked_multioutput_late,
        "multi-output observation fallback preserves every signal role and stamp");
    require(multioutput_late_throw.caught_exception
            && multioutput_late_throw.resumed
            && multioutput_late_throw.private_output_order_correct
            && multioutput_late_throw.cut
                   .first_parent_published_with_second_pending
            && multioutput_late_throw.private_fallback_record_consumed_after_throw
            && multioutput_late_throw.private_dispatches == 2U,
        "an exception after the first private parent key preserves and resumes the second output suffix");
    require_multioutput_parity(multioutput_late_throw,
        checked_multioutput_late_throw,
        "multi-output exception recovery preserves every signal role and stamp");
    require(multioutput_delayed.completed
            && multioutput_delayed.slot_elisions == 0U
            && multioutput_delayed.private_dispatches == 0U
            && multioutput_delayed.private_fallbacks == 0U
            && multioutput_delayed.additional_parent_transaction_after
                .has_value(),
        "a delayed second projected output keeps the whole parent on checked fallback");
    require_multioutput_parity(multioutput_delayed,
        checked_multioutput_delayed,
        "the delayed mixed-output fallback preserves its pending waveform and all signal roles");
    require(multioutput_boundary.completed
            && multioutput_boundary.forwarding_calls > 0U
            && multioutput_boundary.slot_elisions == 0U
            && multioutput_boundary.private_dispatches == 0U
            && multioutput_boundary.private_fallbacks == 0U
            && multioutput_boundary.public_update_tokens == 3U
            && multioutput_boundary.child_value
                == PackedLogic4(1U, Logic4::one)
            && multioutput_boundary.child_last
                == PackedLogic4(1U, Logic4::zero)
            && multioutput_boundary.additional_parent_value
                == PackedLogic4(1U, Logic4::one),
        "a mixed-boundary parent keeps its second output public and preserves the single-input child result");
    require_multioutput_parity(multioutput_boundary,
        checked_multioutput_boundary,
        "mixed-boundary fallback preserves exact public output metadata");
}

void check_region_forwarding_runtime(const bool fail_first_forwarding = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    ForwardingRuntimeProbe forwarding_probe;
    RegionKernelProbe executor_probe;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "forward.input",
        PackedLogic4(1U, Logic4::zero) });
    const auto first_internal = interpreter.add_signal({ "forward.a",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto second_internal = interpreter.add_signal({ "forward.b",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "forward.output",
        PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });
    const std::string long_debug_source
        = "forward_source_" + std::string(160U, 's') + ".sv";
    const std::string long_debug_scope
        = "forward.scope." + std::string(180U, 'h');

    Process first_child;
    first_child.id = 0U;
    first_child.name = "forward_child";
    first_child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    first_child.initialize = true;
    first_child.register_count = 1U;
    first_child.static_sensitivity = {
        { first_internal, EdgeKind::any },
    };
    first_child.driver_regions = {
        { second_internal, 0U, 0U, true },
    };
    first_child.operations = {
        ReadSignal { 0U, first_internal },
        UnaryNot { 0U, 0U },
        WriteUpdate { second_internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(first_child)) == 0U,
        "forwarding chain places the first child before its parent ProcessId");

    Process root;
    root.id = 1U;
    root.name = "forward_root";
    root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    root.initialize = true;
    root.register_count = 1U;
    root.static_sensitivity = { { input, EdgeKind::any } };
    root.driver_regions = { { first_internal, 0U, 0U, true } };
    root.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { first_internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        DebugPoint { DebugPointKind::statement,
            SourceLocation { long_debug_source, 19U, 7U },
            long_debug_scope },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(root)) == 1U,
        "forwarding root keeps its original ProcessId");

    Process grandchild;
    grandchild.id = 2U;
    grandchild.name = "forward_grandchild";
    grandchild.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    grandchild.initialize = true;
    grandchild.register_count = 1U;
    grandchild.static_sensitivity = {
        { second_internal, EdgeKind::any },
    };
    grandchild.driver_regions = { { output, 0U, 0U, true } };
    grandchild.operations = {
        ReadSignal { 0U, second_internal },
        CopyRegister { 0U, 0U },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(grandchild)) == 2U,
        "forwarding chain keeps its final ProcessId");

    const auto add_executor = [&](const ProcessId process,
                                  const SignalId source,
                                  const SignalId destination,
                                  const std::size_t probe_slot) {
        const auto& registered = interpreter.process_program(process);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process,
            [] { return true; },
            [&executor_probe, process, source, destination,
                probe_slot, binding, wait_instruction] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process, source, destination,
                    wait_instruction, binding, true, probe_slot, true,
                    true, 1U, std::nullopt, std::nullopt, true);
            }, std::move(contract));
    };
    add_executor(0U, first_internal, second_internal, 1U);
    add_executor(1U, input, first_internal, 0U);
    add_executor(2U, second_internal, output, 2U);
    interpreter.materialize_ready_process_executors();

    Process clock;
    clock.id = 3U;
    clock.name = "forward_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { input, 0U },
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::zero) },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == 3U,
        "forwarding clock remains outside the certified component");
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<ForwardingRuntimeBackendProvider>(forwarding_probe,
            true, fail_first_forwarding));

    interpreter.start();
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "forwarding root reaches its initial static wait");
    const auto startup_resumes = executor_probe.resumes;
    const auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto startup_forwarding_calls = forwarding_probe.forwarding_calls;
    const auto startup_activation_calls = forwarding_probe.activation_calls;
    const auto startup_region_kernel_failures
        = implementation.systemverilog_wave_profile_region_kernel_failures;
    const auto startup_forwarding_declines
        = implementation.systemverilog_wave_profile_region_forwarding_declines;
    const auto startup_forwarding_evaluations
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations;
    const auto startup_forwarding_members
        = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions;
    const auto component
        = implementation.region_component_by_process.at(1U);
    require(component < implementation.region_activation_programs.size()
            && implementation.region_activation_programs[component]
            && implementation.region_activation_programs[component]
                    ->forwarding_kernel.has_value(),
        "the three-process component has a certified forwarding program");
    const auto& forwarding
        = *implementation.region_activation_programs[component]
               ->forwarding_kernel;
    require(forwarding.topological_member_indices.size() == 3U
            && forwarding.members[forwarding.topological_member_indices[0U]]
                    .process == 1U
            && forwarding.members[forwarding.topological_member_indices[1U]]
                    .process == 0U
        && forwarding.members[forwarding.topological_member_indices[2U]]
                    .process == 2U,
        "forwarding topology is independent of ProcessId sort order");
    const auto forwarding_root_member = std::ranges::find(
        forwarding.execution_kernel.members, ProcessId { 1U },
        &RegionConeKernelMember::process);
    require(forwarding_root_member
                != forwarding.execution_kernel.members.end()
            && forwarding_root_member->final_debug_state
            && forwarding_root_member->final_debug_state->source.path.str()
                == long_debug_source
            && forwarding_root_member->final_debug_state->source.line == 19U
            && forwarding_root_member->final_debug_state->source.column == 7U
            && forwarding_root_member->final_debug_state->scope
                == long_debug_scope,
        "both activation kernels retain the original source and long scope");
    const auto local_state
        = implementation.region_local_wave_state_by_component[component];
    const auto authoritative_state
        = implementation.region_authoritative_state_by_component[component];
    require(local_state && authoritative_state,
        "forwarding result bank is paired with current A4 component state");
    require(local_state->forwarding_results
            && local_state->forwarding_results->stage_batch_group_shape
            && local_state->forwarding_results->stage_batch_pool.size() == 1U,
        "certified fixed-topology forwarding preallocates one private-stage batch");
    const auto& forwarding_stage_pool
        = local_state->forwarding_results->stage_batch_pool;
    const auto* const forwarding_stage_storage
        = forwarding_stage_pool.front().get();
    const auto forwarding_stage_token_capacity
        = forwarding_stage_storage->tokens.capacity();
    require(forwarding_stage_token_capacity
                >= forwarding.execution_kernel.outputs.size(),
        "the persistent private-stage token storage matches the output topology");

    if (fail_first_forwarding) {
        forwarding_probe.fail_next_forwarding = true;
        forwarding_probe.armed_failure = std::make_exception_ptr(
            std::runtime_error("test-only forwarding entry failure"));
        struct SignalSnapshot {
            std::array<PackedLogic4, 4U> roles;
            std::optional<std::pair<SimulationTick, std::uint64_t>>
                transaction;
            std::optional<std::pair<SimulationTick, std::uint64_t>> event;
            SignalEventSchedulingStamp stamp;
        };
        const auto capture_signal = [&](const SignalId signal,
                                        const ProcessId owner) {
            return SignalSnapshot {
                OwnedDriverDemotionTestAccess::packed_a4_values(
                    interpreter, signal, owner),
                implementation.signal_transactions[signal],
                implementation.signal_events[signal],
                implementation.signal_event_scheduling_stamps[signal],
            };
        };
        const auto signal_unchanged = [&](const SignalId signal,
                                          const ProcessId owner,
                                          const SignalSnapshot& before) {
            const auto& after_stamp
                = implementation.signal_event_scheduling_stamps[signal];
            return OwnedDriverDemotionTestAccess::packed_a4_values(
                       interpreter, signal, owner) == before.roles
                && implementation.signal_transactions[signal]
                    == before.transaction
                && implementation.signal_events[signal] == before.event
                && after_stamp.origin.process_domain
                    == before.stamp.origin.process_domain
                && after_stamp.origin.phase == before.stamp.origin.phase
                && after_stamp.systemverilog_round
                    == before.stamp.systemverilog_round;
        };
        const auto before_first
            = capture_signal(first_internal, 1U);
        const auto before_second
            = capture_signal(second_internal, 0U);
        const auto before_output = capture_signal(output, 2U);
        const auto before_resumes = executor_probe.resumes;
        bool failure_was_rethrown { };
        try {
            (void)interpreter.run();
        } catch (const std::runtime_error& error) {
            failure_was_rethrown = std::string_view(error.what())
                == "test-only forwarding entry failure";
            if (!failure_was_rethrown) {
                throw;
            }
        }
        require(failure_was_rethrown
                && forwarding_probe.failure_channel_polls == 1U
                && !forwarding_probe.pending_failure
                && !forwarding_probe.armed_failure,
            "the forwarding error channel is consumed once and propagated");
        require(implementation.systemverilog_wave_profile_region_kernel_failures
                    == startup_region_kernel_failures + 1U
                && implementation.systemverilog_wave_profile_region_forwarding_declines
                    == startup_forwarding_declines,
            "a captured forwarding failure counts once as a region failure, not as a decline");
        require(signal_unchanged(first_internal, 1U, before_first)
                && signal_unchanged(second_internal, 0U, before_second)
                && signal_unchanged(output, 2U, before_output)
                && executor_probe.resumes == before_resumes
                && forwarding_probe.activation_calls
                    == startup_activation_calls,
            "forwarding failure leaves output planes untouched and suppresses activation fallback");
        require(forwarding_probe.failed_prefix_captured,
            "the failing forwarding attempt records its original scheduler prefix");
    }

    require(interpreter.run().status == RunStatus::completed,
        "forwarding chain completes through original scheduler callbacks");
    require(forwarding_probe.forwarding_factories == 1U
            && forwarding_probe.forwarding_calls
                - startup_forwarding_calls
                == (fail_first_forwarding ? 3U : 2U)
            && forwarding_probe.activation_factories == 1U
            && forwarding_probe.activation_calls
                == startup_activation_calls
            && forwarding_probe.origin_was_exact_member_seed_prefix,
        "the two timed forwarding waves run without an ordinary activation backend");
    if (fail_first_forwarding) {
        require(forwarding_probe.retried_prefix_captured
                && forwarding_probe.retry_prefix_matches_failed_prefix
                && forwarding_probe.failure_channel_polls == 1U
                && !forwarding_probe.pending_failure
                && implementation.systemverilog_wave_profile_region_kernel_failures
                    == startup_region_kernel_failures + 1U
                && implementation.systemverilog_wave_profile_region_forwarding_declines
                    == startup_forwarding_declines,
            "forwarding retry receives the exact unconsumed scheduler keys with no stale failure");
    }
    require(executor_probe.resumes == startup_resumes,
        "root, unchanged intermediate, and grandchild callbacks consume the precomputed bank");
    require(implementation.systemverilog_wave_profile_region_forwarding_attempts
                >= 2U
            && implementation.systemverilog_wave_profile_region_forwarding_evaluations
                - startup_forwarding_evaluations == 2U
            && implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
                - startup_forwarding_members == 6U,
        "the runtime profile records both timed forwarding evaluations and six original member consumptions");
    require(forwarding_stage_pool.size() == 1U
            && forwarding_stage_pool.front().get()
                == forwarding_stage_storage
            && forwarding_stage_storage->tokens.capacity()
                == forwarding_stage_token_capacity
            && forwarding_stage_storage->tokens.size()
                <= forwarding_stage_token_capacity
            && forwarding_stage_pool.front().use_count() == 1,
        "two actual forwarding waves reuse the same drained stage batch and token capacity");
    const auto& forwarded_root = implementation.processes.at(1U);
    require(forwarded_root.cold().current_scope == long_debug_scope
            && forwarded_root.cold().current_scope.capacity()
                >= long_debug_scope.size()
            && forwarded_root.cold().current_source.path.str()
                == long_debug_source
            && forwarded_root.cold().current_source.line == 19U
            && forwarded_root.cold().current_source.column == 7U,
        "a long source scope is installed at the consumed member callback");
    require(authoritative_state->valid()
            && authoritative_state->values().packed_slots_bound()
            && local_state->authoritative_revision
                == authoritative_state->values().revision(),
        "internal forwarding output tickets retain synchronized A4 slots");
    require(interpreter.signal_value(first_internal)
                == PackedLogic4(1U, Logic4::zero)
            && interpreter.signal_value(second_internal)
                == PackedLogic4(1U, Logic4::one)
            && interpreter.signal_value(output)
                == PackedLogic4(1U, Logic4::one),
        "a no-change intermediate publication leaves its child inactive, then the later changed chain publishes exact values");
}

enum class ForwardingJoinOrder : std::uint8_t {
    topological,
    reverse_roots,
};

struct ForwardingJoinSignalState {
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    PackedLogic4 raw_owner;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;

    bool operator==(const ForwardingJoinSignalState&) const = default;
};

struct ForwardingJournalSignalMetadataSnapshot {
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalChangeOrigin event_origin;
    std::uint64_t event_round { };
    std::uint64_t value_revision { };
};

struct ForwardingJournalRowSnapshot {
    std::size_t output_index { };
    SignalId signal { };
    ProcessId owner { };
    SimulationTick callback_time { };
    std::uint64_t callback_delta { };
    std::uint64_t callback_round { };
    std::uint64_t callback_order { };
    SignalChangeOrigin origin;
    ForwardingJournalSignalMetadataSnapshot expected_signal_metadata;
};

struct ForwardingPrewriteFallbackObservation {
    bool safe_point_observed_private_rows { };
    bool safe_point_found_no_frontier_runtime { };
    bool safe_point_bank_active { };
    bool boundary_update_scheduled_after_root_publication { };
    bool boundary_update_preceded_sink_key { };
    bool sink_receipt_matches_before_boundary_update { };
    bool sink_receipt_matches_after_boundary_update { };
    bool activation_called { };
    bool activation_has_no_frontier_runtime { };
    bool activation_prefix_captured { };
    bool activation_is_single_join_member { };
    bool sink_receipt_retained_at_fallback { };
    bool activation_inputs_match_materialized_values { };
    bool activation_boundary_value_captured { };
    PackedLogic4 activation_boundary_value;
    bool checked_control_ready_before_boundary_update { };
    bool boundary_value_before_commit_captured { };
    PackedLogic4 boundary_value_before_commit;
    bool prewrite_roles_materialized_at_commit { };
    bool prewrite_journal_empty_after_commit { };
    bool prewrite_boundary_is_kernel_input { };
    bool prewrite_signal_metadata_preserved_at_commit { };
    bool ordinary_forwarding_decline_observed { };
    bool ordinary_decline_channel_empty { };
    bool ordinary_decline_prefix_matches_sink_key { };
    bool role_journal_was_flushed_before_activation { };
    bool all_roles_materialized_before_activation { };
    bool signal_metadata_preserved_before_activation { };
    bool callback_keys_match_metadata { };
    std::size_t private_rows_at_safe_point { };
    std::size_t accepted_root_prefix_calls { };
    std::size_t activation_calls_after_stimulus { };
    std::array<std::size_t, 3U> checked_process_resumes { };
    std::uint64_t authoritative_revision_before { };
    std::uint64_t authoritative_revision_at_activation { };
    std::uint64_t authoritative_revision_at_prewrite_commit { };
    std::array<ForwardingJoinSignalState, 2U> roles_before;
    std::array<ForwardingJoinSignalState, 2U> roles_at_activation;
    std::array<ForwardingJoinSignalState, 2U> roles_after_prewrite_commit;
    std::array<ForwardingJournalSignalMetadataSnapshot, 2U>
        signal_metadata_before;
    std::array<ForwardingJournalSignalMetadataSnapshot, 2U>
        signal_metadata_at_activation;
    std::array<ForwardingJournalSignalMetadataSnapshot, 2U>
        signal_metadata_after_prewrite_commit;
    std::vector<ForwardingJournalRowSnapshot> rows;
    std::array<std::optional<RegionFrontierKeyV1>, 2U> original_root_keys;
    std::optional<RegionFrontierKeyV1> original_sink_key;
    bool accepted_root_prefix_captured { };
    bool accepted_root_prefix_is_exact { };
    RegionBackendPrefixWitness accepted_root_prefix;
    RegionBackendPrefixWitness activation_prefix;
    std::array<PackedLogic4, 2U> activation_internal_values;
    std::array<bool, 2U> activation_internal_values_captured { };
    std::array<std::uint64_t, 2U> initial_internal_value_revisions { };
};

struct ForwardingJoinOutcome {
    bool completed { };
    bool member_seed_prefix_was_exact { true };
    std::size_t forwarding_calls { };
    std::uint64_t forwarding_evaluations { };
    std::uint64_t forwarding_declines { };
    std::uint64_t forwarding_member_consumptions { };
    bool join_continuation_receipt_captured { };
    bool join_continuation_bank_ready { };
    bool join_continuation_receipt_retired { };
    bool join_continuation_consumed_once { };
    bool join_continuation_receipt_retained_after_foreign_change { };
    bool join_continuation_checked_once { };
    RegionFrontierKeyV1 join_continuation_key;
    std::size_t join_continuation_component { };
    std::size_t join_continuation_member { };
    std::uint64_t join_continuation_generation { };
    std::uint64_t join_consumptions_at_receipt { };
    std::uint64_t join_consumptions_after_retirement { };
    std::size_t join_checked_resumes { };
    std::array<ForwardingJoinSignalState, 3U> initial_state;
    std::array<ForwardingJoinSignalState, 3U> final_state;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> join_samples;
    std::vector<std::vector<ProcessId>> member_seed_prefixes;
    std::vector<RegionBackendPrefixWitness> forwarding_prefixes;
    ForwardingPrewriteFallbackObservation prewrite_fallback;
    bool sensitivity_boundary_change_queued { };
    bool sensitivity_boundary_change_ran { };
    bool sensitivity_boundary_change_after_native_evaluation { };
    bool sensitivity_join_receipt_present_before_foreign_change { };
    bool sensitivity_join_receipt_present_after_foreign_change { };
    bool sensitivity_join_receipt_same_across_foreign_change { };
    RegionFrontierKeyV1 sensitivity_join_receipt_before_foreign_change;
    RegionFrontierKeyV1 sensitivity_join_receipt_after_foreign_change;
    std::size_t sensitivity_join_receipt_component_before_foreign_change { };
    std::size_t sensitivity_join_receipt_member_before_foreign_change { };
    std::uint64_t sensitivity_join_receipt_generation_before_foreign_change { };
    std::size_t sensitivity_join_receipt_component_after_foreign_change { };
    std::size_t sensitivity_join_receipt_member_after_foreign_change { };
    std::uint64_t sensitivity_join_receipt_generation_after_foreign_change { };
    bool sensitivity_certificate_current_before { };
    bool sensitivity_certificate_current_after { };
    bool sensitivity_bank_active_before { };
    bool sensitivity_bank_active_after { };
    bool sensitivity_bank_contains_boundary { };
    bool sensitivity_bank_boundary_stale_after { };
    std::uint64_t sensitivity_boundary_revision_before { };
    std::uint64_t sensitivity_boundary_revision_after { };
    PackedLogic4 sensitivity_boundary_value;
    bool sensitivity_child_entry_captured { };
    bool sensitivity_child_after_foreign_change { };
    bool sensitivity_child_started_fresh { };
    bool sensitivity_child_has_no_applied_rows { };
    bool sensitivity_child_receipt_matches_origin { };
    bool sensitivity_child_runtime_generations_match { };
    bool sensitivity_child_boundary_matches_live { };
    bool sensitivity_child_input_matches_live { };
    std::uint64_t sensitivity_child_boundary_revision { };
    std::uint64_t sensitivity_child_live_revision { };
    PackedLogic4 sensitivity_child_boundary_value;
    PackedLogic4 sensitivity_child_live_value;
    PackedLogic4 sensitivity_child_backend_input;
};

auto forwarding_join_signal_state(auto& implementation,
    const SignalId signal, const bool require_authoritative = false)
    -> ForwardingJoinSignalState
{
    const auto component
        = signal < implementation.region_authoritative_component_by_signal.size()
            ? implementation.region_authoritative_component_by_signal[signal]
            : std::numeric_limits<std::size_t>::max();
    if (!implementation.region_graph
        || signal >= implementation.region_graph->signals().size()) {
        throw std::runtime_error { "forwarding join signal is absent from the graph" };
    }
    const auto& graph_signal
        = implementation.region_graph->signals()[signal];
    if (graph_signal.writers.size() != 1U) {
        throw std::runtime_error {
            "forwarding join signal does not have one original owner"
        };
    }
    const auto owner = graph_signal.writers.front().process;
    if (component < implementation.region_authoritative_state_by_component.size()
        && implementation.region_authoritative_state_by_component[component]
        && implementation.region_authoritative_state_by_component[component]
               ->values().packed_signal_slots_bound(signal)) {
        const auto& values
            = implementation.region_authoritative_state_by_component[component]
                  ->values();
        return { values.current(signal),
            values.previous(signal),
            values.stored(signal),
            values.owner_value(signal, owner),
            implementation.signal_transactions.at(signal),
            implementation.signal_events.at(signal) };
    }
    if (require_authoritative) {
        throw std::runtime_error {
            "forwarding join internal signal has unbound A4 roles"
        };
    }

    const auto& current = implementation.logical_signal_value(signal);
    const auto& previous = implementation.logical_signal_last_value(signal);
    const auto& stored = implementation.driven_values.at(signal);
    const auto raw_owner = implementation.underlying_driver_value(owner, signal);
    return { current, previous, stored, raw_owner,
        implementation.signal_transactions.at(signal),
        implementation.signal_events.at(signal) };
}

ForwardingJoinOutcome run_forwarding_join_case(
    const bool forwarding_enabled, const ForwardingJoinOrder order,
    const bool external_only_seed = false,
    const bool invalidate_sensitivity_only_boundary = false,
    const bool capture_forwarding_prefixes = false,
    const bool exercise_prewrite_fallback = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    std::optional<ScopedEnvironment> a4_single_owner_storage;
    std::optional<ScopedEnvironment> a4_disjoint_owner_storage;
    if (exercise_prewrite_fallback) {
        a4_single_owner_storage.emplace(
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "1");
        a4_disjoint_owner_storage.emplace(
            "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "1");
    }
    ForwardingRuntimeProbe forwarding_probe;
    forwarding_probe.capture_forwarding_prefixes
        = capture_forwarding_prefixes;
    RegionKernelProbe executor_probe;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> join_samples;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "forward_join.input",
        PackedLogic4(1U, Logic4::zero) });
    const auto first_internal = interpreter.add_signal({ "forward_join.a",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto second_internal = interpreter.add_signal({ "forward_join.b",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "forward_join.output",
        PackedLogic4(1U, Logic4::one), ResolutionKind::sv_wire });
    std::optional<SignalId> external_trigger;
    if (external_only_seed || invalidate_sensitivity_only_boundary
        || exercise_prewrite_fallback) {
        external_trigger = interpreter.add_signal({ "forward_join.guard",
            PackedLogic4(1U, Logic4::zero) });
    }

    ProcessId first_root_id { };
    ProcessId second_root_id { };
    ProcessId join_id { };
    switch (order) {
    case ForwardingJoinOrder::topological:
        first_root_id = 0U;
        second_root_id = 1U;
        join_id = 2U;
        break;
    case ForwardingJoinOrder::reverse_roots:
        second_root_id = 0U;
        first_root_id = 1U;
        join_id = 2U;
        break;
    }

    const auto make_root = [&](const ProcessId process,
                               const char* const name,
                               const SignalId destination,
                               const bool invert) {
        Process root;
        root.id = process;
        root.name = name;
        root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        root.initialize = true;
        root.register_count = 1U;
        root.static_sensitivity = { { input, EdgeKind::any } };
        root.driver_regions = { { destination, 0U, 0U, true } };
        root.operations = {
            ReadSignal { 0U, input },
        };
        if (invert) {
            root.operations.emplace_back(UnaryNot { 0U, 0U });
        } else {
            root.operations.emplace_back(CopyRegister { 0U, 0U });
        }
        root.operations.emplace_back(WriteUpdate { destination, 0U,
            SignalUpdateDomain::systemverilog_active });
        root.operations.emplace_back(WaitSensitivity { });
        root.operations.emplace_back(Jump { 0U });
        return root;
    };

    auto first_root = make_root(first_root_id, "forward_join_root_a",
        first_internal, false);
    auto second_root = make_root(second_root_id, "forward_join_root_b",
        second_internal, true);

    Process join;
    join.id = join_id;
    join.name = "forward_join_consumer";
    join.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    join.initialize = true;
    join.register_count = exercise_prewrite_fallback ? 3U : 2U;
    join.static_sensitivity = {
        { first_internal, EdgeKind::any },
        { second_internal, EdgeKind::any },
    };
    if (external_trigger) {
        join.static_sensitivity.push_back(
            { *external_trigger, EdgeKind::any });
    }
    join.driver_regions = { { output, 0U, 0U, true } };
    join.operations = {
        ReadSignal { 0U, first_internal },
        ReadSignal { 1U, second_internal },
    };
    if (exercise_prewrite_fallback) {
        require(external_trigger.has_value(),
            "the prewrite fallback reads its changed boundary signal");
        join.operations.emplace_back(
            ReadSignal { 2U, *external_trigger });
    }
    join.operations.emplace_back(
        Binary { BinaryOperator::bit_xor, 0U, 0U, 1U });
    if (exercise_prewrite_fallback) {
        join.operations.emplace_back(
            Binary { BinaryOperator::bit_xor, 0U, 0U, 2U });
    }
    join.operations.emplace_back(WriteUpdate { output, 0U,
        SignalUpdateDomain::systemverilog_active });
    join.operations.emplace_back(WaitSensitivity { });
    join.operations.emplace_back(Jump { 0U });
    std::vector<Process> component_processes;
    component_processes.reserve(3U);
    component_processes.push_back(std::move(first_root));
    component_processes.push_back(std::move(second_root));
    component_processes.push_back(std::move(join));
    std::ranges::sort(component_processes, std::ranges::less { },
        &Process::id);
    for (std::size_t index = 0U; index < component_processes.size(); ++index) {
        require(interpreter.add_process(std::move(component_processes[index]))
                == index,
            "join members are registered in dense ProcessId order");
    }

    const auto install_root_executor = [&](const ProcessId process,
                                           const SignalId destination,
                                           const std::size_t probe_slot,
                                           const bool invert) {
        const auto& registered = interpreter.process_program(process);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process,
            [] { return true; },
            [&executor_probe, process, input, destination, probe_slot,
                invert, binding, wait_instruction] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process, input, destination,
                    wait_instruction, binding, true, probe_slot, true, true,
                    1U, std::nullopt, std::nullopt, true, invert);
            }, std::move(contract));
    };
    install_root_executor(first_root_id, first_internal, 0U, false);
    install_root_executor(second_root_id, second_internal, 1U, true);

    const auto& registered_join = interpreter.process_program(join_id);
    const ProcessExecutorProgramBinding join_binding {
        registered_join, registered_join, join_id };
    const auto join_wait = static_cast<InstructionIndex>(
        registered_join.operations.size() - 2U);
    DeferredProcessExecutorContract join_contract;
    join_contract.expected_access = join_binding;
    join_contract.callbacks_observation_safe = true;
    join_contract.expected_region_kernel_equivalent = true;
    interpreter.set_deferred_process_executor(join_id,
        [] { return true; },
        [&executor_probe, join_id, first_internal, second_internal, output,
            join_wait, join_binding, &join_samples, external_trigger,
            exercise_prewrite_fallback] {
            return std::make_unique<RegionKernelExecutor>(
                executor_probe, join_id, first_internal, output, join_wait,
                join_binding, true, 2U, true, true,
                exercise_prewrite_fallback ? 3U : 2U, std::nullopt,
                second_internal, true, false, true, &join_samples, 1U,
                exercise_prewrite_fallback ? external_trigger : std::nullopt,
                exercise_prewrite_fallback);
        }, std::move(join_contract));

    interpreter.materialize_ready_process_executors();

    Process clock;
    clock.id = 3U;
    clock.name = "forward_join_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == 3U,
        "join stimulus stays outside the certified component");

    Process observer;
    observer.id = 4U;
    observer.name = "forward_join_output_reader";
    observer.scheduling_domain = ProcessSchedulingDomain::generic;
    observer.register_count = 1U;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = {
        ReadSignal { 0U, output },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(observer)) == 4U,
        "a generic read-only process makes the join output a boundary");

    if (external_trigger && !exercise_prewrite_fallback) {
        Process external_driver;
        external_driver.id = 5U;
        external_driver.name = "forward_join_external_trigger";
        external_driver.scheduling_domain
            = ProcessSchedulingDomain::systemverilog;
        external_driver.register_count = 1U;
        external_driver.driver_regions = {
            { *external_trigger, 0U, 0U, true }
        };
        external_driver.operations = {
            WaitFor { 2U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { *external_trigger, 0U },
            Halt { },
        };
        require(interpreter.add_process(std::move(external_driver)) == 5U,
            "the sensitivity-only trigger is driven outside the forwarding component");
    }

    const bool capture_checked_activation_declines
        = (invalidate_sensitivity_only_boundary && !forwarding_enabled)
        || exercise_prewrite_fallback;
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<ForwardingRuntimeBackendProvider>(
            forwarding_probe, forwarding_enabled,
            capture_checked_activation_declines));

    interpreter.start();
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "both roots and the join settle at their startup waits");
    const auto executor_resumes_before_stimulus = executor_probe.resumes;
    forwarding_probe.member_seed_prefixes.clear();
    forwarding_probe.forwarding_prefixes.clear();
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component
        = implementation.region_component_by_process.at(join_id);
    if (forwarding_enabled) {
        require(component < implementation.region_activation_programs.size()
                && implementation.region_activation_programs[component]
                && implementation.region_activation_programs[component]
                        ->forwarding_kernel.has_value(),
            "two independent roots and their join receive a forwarding certificate");
        const auto& forwarding
            = *implementation.region_activation_programs[component]
                   ->forwarding_kernel;
        require(forwarding.members.size() == 3U
                && forwarding.execution_kernel.outputs.size() == 3U
                && forwarding.topological_member_indices.size() == 3U,
            "the join component preserves all three member and output bindings");
        std::array<std::size_t, 2U> expected_roots {
            std::numeric_limits<std::size_t>::max(),
            std::numeric_limits<std::size_t>::max() };
        std::size_t root_count { };
        std::size_t join_member_index
            = std::numeric_limits<std::size_t>::max();
        for (std::size_t member_index = 0U;
             member_index < forwarding.members.size(); ++member_index) {
            const auto& member = forwarding.members[member_index];
            if (member.dependency_count == 0U) {
                require(root_count < expected_roots.size(),
                    "the provider exposes exactly two root members");
                expected_roots[root_count++] = member_index;
            }
            if (member.process == join_id) {
                join_member_index = member_index;
            }
        }
        require(root_count == expected_roots.size()
                && join_member_index < forwarding.members.size(),
            "the join has two independent roots and one dependent member");
        const auto& join_member = forwarding.members[join_member_index];
        require(join_member.depth == 1U
                && join_member.dependency_count == 2U
                && join_member.read_count == 2U,
            "the join records both sensitivity predecessors and both body reads");
        const auto dependencies
            = std::span<const RegionConeForwardingDependency> {
                  forwarding.dependencies }
                  .subspan(join_member.dependency_begin,
                      join_member.dependency_count);
        const auto reads
            = std::span<const RegionConeForwardingRead> {
                  forwarding.internal_reads }
                  .subspan(join_member.read_begin, join_member.read_count);
        require(std::ranges::any_of(dependencies,
                    [&](const auto& dependency) {
                        return dependency.signal == first_internal
                            && forwarding.members[
                                   dependency.writer_member_index].process
                                == first_root_id;
                    })
                && std::ranges::any_of(dependencies,
                    [&](const auto& dependency) {
                        return dependency.signal == second_internal
                            && forwarding.members[
                                   dependency.writer_member_index].process
                                == second_root_id;
                    })
                && std::ranges::any_of(reads,
                    [&](const auto& read) {
                        return read.signal == first_internal
                            && forwarding.members[
                                   read.writer_member_index].process
                                == first_root_id;
                    })
                && std::ranges::any_of(reads,
                    [&](const auto& read) {
                        return read.signal == second_internal
                            && forwarding.members[
                                   read.writer_member_index].process
                                == second_root_id;
                    }),
            "the join dependency/read provenance maps to distinct root owners");
    }

    ForwardingJoinOutcome outcome;
    ForwardingPrewriteFallbackObservation prewrite_fallback;
    std::array<ForwardingJoinSignalState, 3U> initial_state {
        forwarding_join_signal_state(implementation, first_internal),
        forwarding_join_signal_state(implementation, second_internal),
        forwarding_join_signal_state(implementation, output) };
    require(initial_state[0].current == PackedLogic4(1U, Logic4::zero)
            && initial_state[1].current == PackedLogic4(1U, Logic4::one)
            && initial_state[2].current == PackedLogic4(1U, Logic4::one)
            && std::ranges::all_of(initial_state,
                [](const ForwardingJoinSignalState& state) {
                    return state.stored == state.current
                        && state.raw_owner == state.current;
                }),
        "join startup establishes A=0, B=1, and A XOR B=1");
    outcome.initial_state = initial_state;
    if (exercise_prewrite_fallback) {
        prewrite_fallback.initial_internal_value_revisions = {
            implementation.signal_value_revisions.at(first_internal),
            implementation.signal_value_revisions.at(second_internal) };
    }
    join_samples.clear();

    const auto evaluations_before
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations;
    const auto declines_before
        = implementation.systemverilog_wave_profile_region_forwarding_declines;
    const auto members_before
        = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions;
    const auto calls_before = forwarding_probe.forwarding_calls;
    const auto activation_calls_before_stimulus
        = forwarding_probe.activation_calls;
    const auto join_resumes_before = executor_probe.resumes[2U];

    const auto same_region_frontier_key = [](
        const RegionFrontierKeyV1& left,
        const RegionFrontierKeyV1& right) {
        return left.time == right.time
            && left.delta == right.delta
            && left.systemverilog_round == right.systemverilog_round
            && left.stable_order == right.stable_order
            && left.sequence == right.sequence
            && left.process_domain == right.process_domain
            && left.phase == right.phase;
    };
    const auto capture_sensitivity_join_receipt = [&, join_id, component](
        auto& current_implementation,
        auto& current_scheduler,
        const bool after_foreign_change) {
        if (join_id >= current_implementation.processes.size()
            || join_id
                >= current_implementation
                       .region_readiness_queued_by_process.size()
            || join_id
                >= current_implementation
                       .region_readiness_member_index_by_process.size()) {
            return false;
        }

        const auto& process = current_implementation.processes[join_id];
        const auto& queued
            = current_implementation.region_readiness_queued_by_process[
                join_id];
        if (!process.queued || !queued.key_valid
            || queued.component != component
            || queued.member
                != current_implementation
                       .region_readiness_member_index_by_process[join_id]
            || queued.generation
                != current_implementation.region_runtime_generation) {
            return false;
        }

        const auto& key = queued.queued_key;
        if (key.time != current_scheduler.now()
            || key.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || key.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || key.stable_order != join_id
            || key.systemverilog_round == 0U) {
            return false;
        }

        if (after_foreign_change) {
            outcome.sensitivity_join_receipt_present_after_foreign_change
                = true;
            outcome.sensitivity_join_receipt_after_foreign_change = key;
            outcome.sensitivity_join_receipt_component_after_foreign_change
                = queued.component;
            outcome.sensitivity_join_receipt_member_after_foreign_change
                = queued.member;
            outcome.sensitivity_join_receipt_generation_after_foreign_change
                = queued.generation;
        } else {
            outcome.sensitivity_join_receipt_present_before_foreign_change
                = true;
            outcome.sensitivity_join_receipt_before_foreign_change = key;
            outcome.sensitivity_join_receipt_component_before_foreign_change
                = queued.component;
            outcome.sensitivity_join_receipt_member_before_foreign_change
                = queued.member;
            outcome.sensitivity_join_receipt_generation_before_foreign_change
                = queued.generation;
        }
        return true;
    };

    Scheduler::SafePointHookToken join_continuation_hook_token { };
    if (forwarding_enabled && capture_forwarding_prefixes
        && !exercise_prewrite_fallback) {
        join_continuation_hook_token
            = interpreter.scheduler().add_safe_point_hook(
                [&, component, join_id, same_region_frontier_key](
                    Scheduler& safe_scheduler,
                    const SchedulerPhase phase) {
                    if (phase != SchedulerPhase::active
                        || join_id >= implementation.processes.size()
                        || join_id
                            >= implementation
                                   .region_readiness_queued_by_process.size()
                        || join_id
                            >= implementation
                                   .region_readiness_member_index_by_process
                                   .size()) {
                        return;
                    }

                    const auto& process
                        = implementation.processes[join_id];
                    const auto& queued
                        = implementation
                              .region_readiness_queued_by_process[join_id];
                    if (!outcome.join_continuation_receipt_captured) {
                        if (!process.queued || !queued.key_valid
                            || queued.component != component
                            || queued.member
                                != implementation
                                       .region_readiness_member_index_by_process[
                                           join_id]
                            || queued.generation
                                != implementation.region_runtime_generation
                            || component
                                >= implementation
                                       .region_local_wave_state_by_component
                                       .size()) {
                            return;
                        }

                        const auto& local
                            = implementation
                                  .region_local_wave_state_by_component[
                                      component];
                        if (!local || !local->forwarding_results
                            || !local->seeded) {
                            return;
                        }
                        const auto& bank = *local->forwarding_results;
                        const auto& forwarding
                            = *implementation
                                   .region_activation_programs.at(component)
                                   ->forwarding_kernel;
                        const auto member = std::ranges::find(
                            forwarding.members, join_id,
                            &RegionConeForwardingMember::process);
                        if (member == forwarding.members.end()) {
                            return;
                        }
                        const auto member_index = static_cast<std::size_t>(
                            member - forwarding.members.begin());
                        if (bank.member_active.size()
                                != forwarding.members.size()
                            || bank.member_consumed.size()
                                != forwarding.members.size()
                            || member_index >= bank.member_active.size()
                            || member_index >= bank.member_consumed.size()
                            || bank.runtime_generation != local->generation
                            || local->generation
                                != implementation.region_runtime_generation
                            || !bank.active
                            || bank.remaining_members != 1U
                            || bank.member_active[member_index] == 0U
                            || bank.member_consumed[member_index] != 0U
                            || queued.member != member_index) {
                            return;
                        }
                        for (std::size_t index = 0U;
                             index < forwarding.members.size(); ++index) {
                            if (index != member_index
                                && bank.member_consumed[index] == 0U) {
                                return;
                            }
                        }

                        const auto& key = queued.queued_key;
                        if (key.time != safe_scheduler.now()
                            || key.process_domain
                                != static_cast<std::uint32_t>(
                                    ProcessSchedulingDomain::systemverilog)
                            || key.phase != static_cast<std::uint32_t>(
                                SchedulerPhase::active)
                            || key.stable_order != join_id
                            || key.systemverilog_round == 0U) {
                            return;
                        }
                        outcome.join_continuation_key = key;
                        outcome.join_continuation_component = queued.component;
                        outcome.join_continuation_member = queued.member;
                        outcome.join_continuation_generation = queued.generation;
                        outcome.join_continuation_receipt_captured = true;
                        outcome.join_continuation_bank_ready = true;
                        if (invalidate_sensitivity_only_boundary
                            && outcome
                                   .sensitivity_join_receipt_present_after_foreign_change) {
                            outcome.join_continuation_receipt_retained_after_foreign_change
                                = same_region_frontier_key(key,
                                    outcome.sensitivity_join_receipt_after_foreign_change)
                                && queued.component
                                    == outcome
                                           .sensitivity_join_receipt_component_after_foreign_change
                                && queued.member
                                    == outcome.sensitivity_join_receipt_member_after_foreign_change
                                && queued.generation
                                    == outcome
                                           .sensitivity_join_receipt_generation_after_foreign_change;
                        }
                        outcome.join_consumptions_at_receipt
                            = implementation
                                  .systemverilog_wave_profile_region_forwarding_member_consumptions
                            - members_before;
                        return;
                    }

                    if (outcome.join_continuation_receipt_retired) {
                        return;
                    }
                    if (invalidate_sensitivity_only_boundary
                        && outcome.sensitivity_boundary_change_ran
                        && process.queued && queued.key_valid) {
                        const auto& saved = outcome.join_continuation_key;
                        const auto& current = queued.queued_key;
                        outcome.join_continuation_receipt_retained_after_foreign_change
                            = current.time == saved.time
                            && current.delta == saved.delta
                            && current.systemverilog_round
                                == saved.systemverilog_round
                            && current.stable_order == saved.stable_order
                            && current.sequence == saved.sequence
                            && current.process_domain == saved.process_domain
                            && current.phase == saved.phase
                            && queued.component
                                == outcome.join_continuation_component
                            && queued.member == outcome.join_continuation_member
                            && queued.generation
                                == outcome.join_continuation_generation;
                        return;
                    }
                    if (process.queued || queued.key_valid) {
                        return;
                    }
                    outcome.join_continuation_receipt_retired = true;
                    if (invalidate_sensitivity_only_boundary) {
                        outcome.join_continuation_checked_once
                            = executor_probe.resumes[join_id]
                                - join_resumes_before == 1U;
                        return;
                    }
                    outcome.join_consumptions_after_retirement
                        = implementation
                              .systemverilog_wave_profile_region_forwarding_member_consumptions
                        - members_before;
                    outcome.join_continuation_consumed_once
                        = outcome.join_consumptions_after_retirement
                            == outcome.join_consumptions_at_receipt + 1U;
                });
    }

    if (invalidate_sensitivity_only_boundary) {
        const auto queue_boundary_change = [&,
                                               capture_sensitivity_join_receipt,
                                               same_region_frontier_key] {
            if (outcome.sensitivity_boundary_change_queued) {
                return;
            }
            outcome.sensitivity_boundary_change_queued = true;
            outcome.sensitivity_boundary_change_after_native_evaluation
                = forwarding_enabled;
            // Topological roots are processes 0 and 1; the join is process
            // 2. Queue foreign Active work at stable key 0 from the captured
            // two-root cut, before runtime dispatches those roots. It enters
            // the pending Active round before their later output publications
            // can queue the join at key 2, so it precedes the original join key.
            interpreter.scheduler().schedule_systemverilog(
                SchedulerPhase::active, 0U,
                [&outcome, &interpreter, join_id,
                    trigger = *external_trigger,
                    capture_sensitivity_join_receipt,
                    same_region_frontier_key](
                    Scheduler&) {
                    auto& implementation
                        = OwnedDriverDemotionTestAccess::implementation(
                            interpreter);
                    const auto component
                        = implementation.region_component_by_process.at(join_id);
                    const auto& local
                        = implementation.region_local_wave_state_by_component.at(
                            component);
                    outcome.sensitivity_certificate_current_before
                        = implementation.region_graph
                        && implementation.region_graph->component_epochs_current(
                            component);
                    outcome.sensitivity_bank_active_before
                        = local && local->forwarding_results
                        && local->forwarding_results->active;
                    outcome.sensitivity_boundary_revision_before
                        = implementation.signal_value_revisions.at(trigger);
                    capture_sensitivity_join_receipt(
                        implementation, interpreter.scheduler(), false);
                    implementation.commit(trigger,
                        PackedLogic4(1U, Logic4::one),
                        { ProcessSchedulingDomain::systemverilog,
                            SchedulerPhase::active });
                    outcome.sensitivity_boundary_revision_after
                        = implementation.signal_value_revisions.at(trigger);
                    outcome.sensitivity_boundary_change_ran
                        = outcome.sensitivity_boundary_revision_after
                            != outcome.sensitivity_boundary_revision_before;
                    capture_sensitivity_join_receipt(
                        implementation, interpreter.scheduler(), true);
                    if (outcome.sensitivity_join_receipt_present_before_foreign_change
                        && outcome.sensitivity_join_receipt_present_after_foreign_change) {
                        outcome.sensitivity_join_receipt_same_across_foreign_change
                            = same_region_frontier_key(
                                outcome.sensitivity_join_receipt_before_foreign_change,
                                outcome.sensitivity_join_receipt_after_foreign_change)
                            && outcome.sensitivity_join_receipt_component_before_foreign_change
                                == outcome.sensitivity_join_receipt_component_after_foreign_change
                            && outcome.sensitivity_join_receipt_member_before_foreign_change
                                == outcome.sensitivity_join_receipt_member_after_foreign_change
                            && outcome.sensitivity_join_receipt_generation_before_foreign_change
                                == outcome.sensitivity_join_receipt_generation_after_foreign_change;
                    }
                    if (outcome.join_continuation_receipt_captured
                        && outcome.sensitivity_join_receipt_present_after_foreign_change) {
                        outcome.join_continuation_receipt_retained_after_foreign_change
                            = same_region_frontier_key(
                                outcome.join_continuation_key,
                                outcome.sensitivity_join_receipt_after_foreign_change)
                            && outcome.join_continuation_component
                                == outcome.sensitivity_join_receipt_component_after_foreign_change
                            && outcome.join_continuation_member
                                == outcome.sensitivity_join_receipt_member_after_foreign_change
                            && outcome.join_continuation_generation
                                == outcome.sensitivity_join_receipt_generation_after_foreign_change;
                    }
                    outcome.sensitivity_certificate_current_after
                        = implementation.region_graph
                        && implementation.region_graph->component_epochs_current(
                            component);
                    outcome.sensitivity_bank_active_after
                        = local && local->forwarding_results
                        && local->forwarding_results->active;
                    if (local && local->forwarding_results) {
                        const auto& bank = *local->forwarding_results;
                        const auto boundary = std::ranges::find(
                            bank.boundary_signals, trigger);
                        outcome.sensitivity_bank_contains_boundary
                            = boundary != bank.boundary_signals.end();
                        if (boundary != bank.boundary_signals.end()) {
                            const auto index = static_cast<std::size_t>(
                                boundary - bank.boundary_signals.begin());
                            outcome.sensitivity_bank_boundary_stale_after
                                = bank.boundary_revisions.at(index)
                                != implementation.signal_value_revisions.at(
                                    trigger);
                        }
                    }
                });
        };
        if (forwarding_enabled) {
            forwarding_probe.on_forwarding_execution
                = [&, queue_boundary_change](
                      const RegionKernelSchedulerPrefix& prefix) {
                      const auto has_seed = [&](const ProcessId process) {
                          return std::ranges::any_of(prefix.tasks,
                              [process](const auto& task) {
                                  return task.member.process == process;
                              });
                      };
                      if (has_seed(first_root_id)
                          && has_seed(second_root_id)) {
                          queue_boundary_change();
                      }
                  };
            forwarding_probe.on_forwarding_execution_with_inputs
                = [&, trigger = *external_trigger](
                      const RegionKernelSchedulerPrefix& prefix,
                      const std::span<const PackedLogic4> boundary_inputs) {
                      if (prefix.tasks.size() != 1U
                          || prefix.tasks.front().member.process != join_id) {
                          return;
                      }

                      auto& current_implementation
                          = OwnedDriverDemotionTestAccess::implementation(
                              interpreter);
                      outcome.sensitivity_child_entry_captured = true;
                      outcome.sensitivity_child_after_foreign_change
                          = outcome.sensitivity_boundary_change_ran;
                      const auto current_frontier
                          = current_implementation.scheduler
                                .current_batch_frontier();

                      const auto& child
                          = prefix.tasks.front();
                      const auto& origin = child.member.origin;
                      const auto& queued
                          = current_implementation
                                .region_readiness_queued_by_process.at(join_id);
                      outcome.sensitivity_child_receipt_matches_origin
                          = queued.key_valid
                          && queued.component == component
                          && queued.member
                              == current_implementation
                                  .region_readiness_member_index_by_process
                                      .at(join_id)
                          && queued.generation
                              == current_implementation.region_runtime_generation
                          && prefix.frontier_generation != 0U
                          && current_frontier
                          && prefix.frontier_generation
                              == current_frontier->generation
                          && prefix.frontier_cursor
                              == current_frontier->cursor
                          && prefix.frontier_end == current_frontier->end
                          && prefix.time == current_frontier->time
                          && prefix.delta == current_frontier->delta
                          && prefix.phase == current_frontier->phase
                          && prefix.systemverilog_round
                              == current_frontier->systemverilog_round
                          && child.task_ordinal == prefix.frontier_cursor
                          && prefix.frontier_end > prefix.frontier_cursor
                          && origin.process_domain
                              == ProcessSchedulingDomain::systemverilog
                          && origin.phase == SchedulerPhase::active
                          && origin.time == prefix.time
                          && origin.delta == prefix.delta
                          && origin.systemverilog_round
                              == prefix.systemverilog_round
                          && origin.process_domain == prefix.process_domain
                          && queued.queued_key.time == origin.time
                          && queued.queued_key.delta == origin.delta
                          && queued.queued_key.systemverilog_round
                              == origin.systemverilog_round
                          && queued.queued_key.stable_order
                              == origin.stable_order
                          && queued.queued_key.sequence == origin.sequence
                          && queued.queued_key.process_domain
                              == static_cast<std::uint32_t>(
                                  origin.process_domain)
                          && queued.queued_key.phase
                              == static_cast<std::uint32_t>(origin.phase);

                      const auto& forwarding
                          = *current_implementation
                                 .region_activation_programs.at(component)
                                 ->forwarding_kernel;
                      const auto input = std::ranges::find(
                          forwarding.execution_kernel.inputs, trigger,
                          &RegionConeKernelInput::signal);
                      const auto& local
                          = current_implementation
                                .region_local_wave_state_by_component.at(
                                    component);
                      if (input == forwarding.execution_kernel.inputs.end()
                          || input->internal || !local
                          || !local->forwarding_results) {
                          return;
                      }
                      const auto input_index = static_cast<std::size_t>(
                          input
                              - forwarding.execution_kernel.inputs.begin());
                      if (input_index >= boundary_inputs.size()) {
                          return;
                      }
                      const auto& bank = *local->forwarding_results;
                      // During a fresh start, the helper seeds local
                      // activation immediately before invoking the backend;
                      // the forwarding bank becomes active only on success.
                      outcome.sensitivity_child_started_fresh
                          = !bank.active && local->seeded;
                      outcome.sensitivity_child_runtime_generations_match
                          = queued.generation == local->generation
                          && bank.runtime_generation == local->generation
                          && local->generation
                              == current_implementation.region_runtime_generation;
                      outcome.sensitivity_child_has_no_applied_rows
                          = bank.applied_role_mutations.empty()
                          && bank.applied_role_metadata.empty();
                      const auto bank_boundary = std::ranges::find(
                          bank.boundary_signals, trigger);
                      if (bank_boundary == bank.boundary_signals.end()) {
                          return;
                      }
                      const auto boundary_index = static_cast<std::size_t>(
                          bank_boundary - bank.boundary_signals.begin());
                      if (boundary_index >= bank.boundary_revisions.size()
                          || boundary_index >= bank.boundary_values.size()) {
                          return;
                      }

                      outcome.sensitivity_child_boundary_revision
                          = bank.boundary_revisions[boundary_index];
                      outcome.sensitivity_child_live_revision
                          = current_implementation.signal_value_revisions.at(
                              trigger);
                      outcome.sensitivity_child_boundary_value
                          = bank.boundary_values[boundary_index];
                      outcome.sensitivity_child_live_value
                          = current_implementation.logical_signal_value(trigger);
                      outcome.sensitivity_child_backend_input
                          = boundary_inputs[input_index];
                      outcome.sensitivity_child_boundary_matches_live
                          = outcome.sensitivity_child_boundary_revision
                                  == outcome.sensitivity_child_live_revision
                          && outcome.sensitivity_child_boundary_value
                              == outcome.sensitivity_child_live_value;
                      outcome.sensitivity_child_input_matches_live
                          = outcome.sensitivity_child_backend_input
                              == outcome.sensitivity_child_live_value;
                  };
        } else {
            forwarding_probe.on_activation_decline
                = [&, queue_boundary_change](
                      const std::span<const RegionConeKernelInput>,
                      const RegionKernelActivationImage& image) {
                      const bool first_root_ready
                          = std::ranges::find(image.ready_processes,
                                first_root_id)
                              != image.ready_processes.end();
                      const bool second_root_ready
                          = std::ranges::find(image.ready_processes,
                                second_root_id)
                              != image.ready_processes.end();
                      if (first_root_ready && second_root_ready) {
                          queue_boundary_change();
                      }
                  };
        }
    }

    Scheduler::SafePointHookToken prewrite_hook_token { };
    if (exercise_prewrite_fallback) {
        const auto capture_signal_metadata =
            [&](const SignalId signal) {
                const auto& stamp
                    = implementation.signal_event_scheduling_stamps.at(signal);
                return ForwardingJournalSignalMetadataSnapshot {
                    implementation.signal_events.at(signal),
                    implementation.signal_transactions.at(signal),
                    stamp.origin,
                    stamp.systemverilog_round,
                    implementation.signal_value_revisions.at(signal) };
            };
        const auto same_signal_metadata = [](
            const ForwardingJournalSignalMetadataSnapshot& left,
            const ForwardingJournalSignalMetadataSnapshot& right) {
            return left.event == right.event
                && left.transaction == right.transaction
                && left.event_origin.process_domain
                    == right.event_origin.process_domain
                && left.event_origin.phase == right.event_origin.phase
                && left.event_round == right.event_round
                && left.value_revision == right.value_revision;
        };
        const auto key_matches_origin = [](
            const RegionFrontierKeyV1& key,
            const RegionKernelActivationOrigin& origin) {
            return key.time == origin.time
                && key.delta == origin.delta
                && key.systemverilog_round == origin.systemverilog_round
                && key.stable_order == origin.stable_order
                && key.sequence == origin.sequence
                && key.process_domain
                    == static_cast<std::uint32_t>(origin.process_domain)
                && key.phase == static_cast<std::uint32_t>(origin.phase);
        };
        forwarding_probe.on_forwarding_execution
            = [&, key_matches_origin](
                  const RegionKernelSchedulerPrefix& prefix) {
                  ++prewrite_fallback.accepted_root_prefix_calls;
                  if (prewrite_fallback.accepted_root_prefix_captured) {
                      return;
                  }
                  prewrite_fallback.accepted_root_prefix_captured
                      = prewrite_fallback.accepted_root_prefix.capture(
                          prefix);
                  bool exact = prewrite_fallback
                      .accepted_root_prefix_captured
                      && prefix.tasks.size() == 2U
                      && prefix.frontier_generation != 0U
                      && prefix.frontier_cursor <= prefix.frontier_end
                      && prefix.frontier_end - prefix.frontier_cursor == 2U
                      && prefix.process_domain
                          == ProcessSchedulingDomain::systemverilog
                      && prefix.phase == SchedulerPhase::active;
                  for (std::size_t index = 0U;
                       index < prefix.tasks.size(); ++index) {
                      const auto& task = prefix.tasks[index];
                      const auto process = index == 0U
                          ? first_root_id : second_root_id;
                      const auto& origin = task.member.origin;
                      const RegionFrontierKeyV1 saved_key {
                          origin.time,
                          origin.delta,
                          origin.systemverilog_round,
                          origin.stable_order,
                          origin.sequence,
                          static_cast<std::uint32_t>(
                              origin.process_domain),
                          static_cast<std::uint32_t>(origin.phase) };
                      if (index < prewrite_fallback.original_root_keys.size()) {
                          prewrite_fallback.original_root_keys[index]
                              = saved_key;
                      } else {
                          exact = false;
                      }
                      exact = exact
                          && key_matches_origin(saved_key, origin)
                          && task.member.process == process
                          && task.task_ordinal
                              == prefix.frontier_cursor + index
                          && origin.process_domain
                              == ProcessSchedulingDomain::systemverilog
                          && origin.phase == SchedulerPhase::active
                          && origin.time == prefix.time
                          && origin.delta == prefix.delta
                          && origin.systemverilog_round
                              == prefix.systemverilog_round
                          && origin.stable_order == process;
                      if (index != 0U) {
                          exact = exact
                              && prefix.tasks[index - 1U].member.origin.sequence
                                  != origin.sequence;
                      }
                  }
                  prewrite_fallback.accepted_root_prefix_is_exact = exact;
              };
        forwarding_probe.on_activation_decline
            = [&, capture_signal_metadata, same_signal_metadata,
                  key_matches_origin](
                  const std::span<const RegionConeKernelInput> inputs,
                  const RegionKernelActivationImage& image) {
                  prewrite_fallback.activation_called = true;
                  prewrite_fallback.activation_prefix_captured
                      = prewrite_fallback.activation_prefix.capture(
                          image.scheduler_prefix);
                  prewrite_fallback.activation_is_single_join_member
                      = image.scheduler_prefix.tasks.size() == 1U
                      && image.scheduler_prefix.tasks.front().member.process
                          == join_id;
                  prewrite_fallback.activation_has_no_frontier_runtime
                      = component
                              >= implementation
                                    .region_frontier_runtime_by_component.size()
                      || !implementation.region_frontier_runtime_by_component[
                          component];

                  const auto key_matches_saved_sink =
                      [&](const RegionKernelActivationOrigin& origin) {
                          return prewrite_fallback.original_sink_key
                              && key_matches_origin(
                                  *prewrite_fallback.original_sink_key,
                                  origin);
                      };
                  const auto queued_sink_matches = [&] {
                      if (join_id
                              >= implementation.region_readiness_queued_by_process
                                     .size()
                          || join_id
                              >= implementation
                                     .region_readiness_member_index_by_process
                                     .size()) {
                          return false;
                      }
                      const auto& queued
                          = implementation.region_readiness_queued_by_process[
                              join_id];
                      return queued.key_valid && queued.component == component
                          && queued.member
                              == implementation
                                     .region_readiness_member_index_by_process[
                                         join_id]
                          && queued.generation
                              == implementation.region_runtime_generation
                          && image.scheduler_prefix.tasks.size() == 1U
                          && key_matches_saved_sink(
                              image.scheduler_prefix.tasks.front()
                                  .member.origin)
                          && key_matches_saved_sink(
                              RegionKernelActivationOrigin {
                                  static_cast<ProcessSchedulingDomain>(
                                      queued.queued_key.process_domain),
                                  static_cast<SchedulerPhase>(
                                      queued.queued_key.phase),
                                  queued.queued_key.time,
                                  queued.queued_key.delta,
                                  static_cast<StableOrder>(
                                      queued.queued_key.stable_order),
                                  queued.queued_key.sequence,
                                  queued.queued_key.systemverilog_round });
                  };
                  prewrite_fallback.sink_receipt_retained_at_fallback
                      = prewrite_fallback.activation_prefix_captured
                      && queued_sink_matches();

                  const auto local
                      = component
                              < implementation
                                    .region_local_wave_state_by_component.size()
                      ? implementation.region_local_wave_state_by_component[
                            component].get()
                      : nullptr;
                  const auto authoritative
                      = component
                              < implementation
                                    .region_authoritative_state_by_component.size()
                      ? implementation.region_authoritative_state_by_component[
                            component].get()
                      : nullptr;
                  const auto bank
                      = local == nullptr || !local->forwarding_results
                      ? nullptr : &*local->forwarding_results;
                  prewrite_fallback.role_journal_was_flushed_before_activation
                      = bank != nullptr
                      && bank->applied_role_mutations.empty()
                      && bank->applied_role_metadata.empty()
                      && !bank->role_journal_enabled
                      && !bank->active
                      && implementation
                             .region_forwarding_role_journal_nonempty_components
                          == 0U;
                  if (authoritative != nullptr && authoritative->valid()
                      && authoritative->values().packed_signal_slots_bound(
                          first_internal)
                      && authoritative->values().packed_signal_slots_bound(
                          second_internal)) {
                      prewrite_fallback.authoritative_revision_at_activation
                          = authoritative->values().revision();
                      prewrite_fallback.roles_at_activation = {
                          forwarding_join_signal_state(implementation,
                              first_internal),
                          forwarding_join_signal_state(implementation,
                              second_internal) };
                      prewrite_fallback.all_roles_materialized_before_activation
                          = prewrite_fallback.roles_at_activation[0].current
                                  == PackedLogic4(1U, Logic4::one)
                          && prewrite_fallback.roles_at_activation[0].last
                              == PackedLogic4(1U, Logic4::zero)
                          && prewrite_fallback.roles_at_activation[0].stored
                              == PackedLogic4(1U, Logic4::one)
                          && prewrite_fallback.roles_at_activation[0].raw_owner
                              == PackedLogic4(1U, Logic4::one)
                          && prewrite_fallback.roles_at_activation[1].current
                              == PackedLogic4(1U, Logic4::zero)
                          && prewrite_fallback.roles_at_activation[1].last
                              == PackedLogic4(1U, Logic4::one)
                          && prewrite_fallback.roles_at_activation[1].stored
                              == PackedLogic4(1U, Logic4::zero)
                          && prewrite_fallback.roles_at_activation[1].raw_owner
                              == PackedLogic4(1U, Logic4::zero);
                  }
                  prewrite_fallback.signal_metadata_at_activation = {
                      capture_signal_metadata(first_internal),
                      capture_signal_metadata(second_internal) };
                  prewrite_fallback.signal_metadata_preserved_before_activation
                      = std::ranges::equal(
                          prewrite_fallback.signal_metadata_before,
                          prewrite_fallback.signal_metadata_at_activation,
                          same_signal_metadata);

                  std::array<PackedLogic4, 2U> expected_inputs {
                      PackedLogic4(1U, Logic4::one),
                      PackedLogic4(1U, Logic4::zero) };
                  for (const auto& input : inputs) {
                      const auto register_input = std::ranges::find(
                          image.register_inputs, input.value_register,
                          &RegionKernelRegisterInput::register_id);
                      if (register_input == image.register_inputs.end()
                          || input.value_kind != ValueKind::logic4
                          || input.width != 1U) {
                          continue;
                      }
                      if (input.internal
                          && input.signal == first_internal) {
                          prewrite_fallback.activation_internal_values[0U]
                              = register_input->value;
                          prewrite_fallback
                              .activation_internal_values_captured[0U] = true;
                      } else if (input.internal
                          && input.signal == second_internal) {
                          prewrite_fallback.activation_internal_values[1U]
                              = register_input->value;
                          prewrite_fallback
                              .activation_internal_values_captured[1U] = true;
                      } else if (external_trigger
                          && input.signal == *external_trigger
                          && !input.internal) {
                          prewrite_fallback.activation_boundary_value
                              = register_input->value;
                          prewrite_fallback.activation_boundary_value_captured
                              = true;
                      }
                  }
                  prewrite_fallback.activation_inputs_match_materialized_values
                      = prewrite_fallback
                            .activation_internal_values_captured[0U]
                      && prewrite_fallback
                            .activation_internal_values_captured[1U]
                      && prewrite_fallback.activation_internal_values[0U]
                          == expected_inputs[0U]
                      && prewrite_fallback.activation_internal_values[1U]
                          == expected_inputs[1U]
                      && prewrite_fallback.activation_boundary_value_captured
                      && external_trigger
                      && prewrite_fallback.activation_boundary_value
                          == implementation.logical_signal_value(
                              *external_trigger)
                      && prewrite_fallback.activation_boundary_value
                          == PackedLogic4(1U, Logic4::one)
                      && prewrite_fallback.roles_at_activation[0U].current
                          == expected_inputs[0U]
                      && prewrite_fallback.roles_at_activation[1U].current
                          == expected_inputs[1U];
              };

        prewrite_hook_token = interpreter.scheduler().add_safe_point_hook(
            [&, capture_signal_metadata, same_signal_metadata,
                key_matches_origin](Scheduler&, const SchedulerPhase) {
                if (prewrite_fallback.boundary_update_scheduled_after_root_publication) {
                    return;
                }
                if (join_id
                        < implementation.region_readiness_queued_by_process.size()) {
                    const auto& queued
                        = implementation.region_readiness_queued_by_process[
                            join_id];
                    if (join_id < implementation.processes.size()
                        && implementation.processes[join_id].queued
                        && queued.key_valid && queued.component == component
                        && join_id
                            < implementation
                                  .region_readiness_member_index_by_process.size()
                        && queued.member
                            == implementation
                                   .region_readiness_member_index_by_process[
                                       join_id]
                        && queued.generation
                            == implementation.region_runtime_generation
                        && queued.queued_key.time == 1U
                        && queued.queued_key.process_domain
                            == static_cast<std::uint32_t>(
                                ProcessSchedulingDomain::systemverilog)
                        && queued.queued_key.phase
                            == static_cast<std::uint32_t>(
                                SchedulerPhase::active)
                        && queued.queued_key.stable_order == join_id
                        && queued.queued_key.systemverilog_round != 0U
                        && !prewrite_fallback.original_sink_key) {
                        prewrite_fallback.original_sink_key
                            = queued.queued_key;
                    }
                }

                if (forwarding_enabled) {
                    if (component
                            >= implementation.region_local_wave_state_by_component
                                   .size()) {
                        return;
                    }
                    const auto& local
                        = implementation.region_local_wave_state_by_component[
                            component];
                    if (!local || !local->forwarding_results) {
                        return;
                    }
                    const auto& bank = *local->forwarding_results;
                    if (!bank.role_journal_enabled || bank.private_epoch_retired
                        || bank.applied_role_mutations.empty()
                        || bank.applied_role_mutations.size() != 2U
                        || bank.applied_role_mutations.size()
                            != bank.applied_role_metadata.size()) {
                        return;
                    }
                    const auto authoritative
                        = component
                                < implementation
                                      .region_authoritative_state_by_component.size()
                        ? implementation.region_authoritative_state_by_component[
                              component].get()
                        : nullptr;
                    if (component
                            >= implementation.region_activation_programs.size()
                        || !implementation.region_activation_programs[component]) {
                        return;
                    }
                    if (authoritative == nullptr || !authoritative->valid()
                        || !authoritative->values().packed_signal_slots_bound(
                            first_internal)
                        || !authoritative->values().packed_signal_slots_bound(
                            second_internal)) {
                        return;
                    }
                    prewrite_fallback.safe_point_found_no_frontier_runtime
                        = component
                                >= implementation
                                      .region_frontier_runtime_by_component.size()
                        || !implementation.region_frontier_runtime_by_component[
                            component];
                    prewrite_fallback.authoritative_revision_before
                        = authoritative->values().revision();
                    prewrite_fallback.roles_before = {
                        forwarding_join_signal_state(implementation,
                            first_internal),
                        forwarding_join_signal_state(implementation,
                            second_internal) };
                    prewrite_fallback.signal_metadata_before = {
                        capture_signal_metadata(first_internal),
                        capture_signal_metadata(second_internal) };
                    prewrite_fallback.callback_keys_match_metadata = true;
                    prewrite_fallback.rows.clear();
                    for (std::size_t index = 0U;
                         index < bank.applied_role_metadata.size(); ++index) {
                        const auto& metadata = bank.applied_role_metadata[index];
                        if (metadata.signal != first_internal
                            && metadata.signal != second_internal) {
                            prewrite_fallback.callback_keys_match_metadata
                                = false;
                        }
                        const auto signal_index
                            = metadata.signal == first_internal ? 0U : 1U;
                        const auto& live_metadata
                            = prewrite_fallback.signal_metadata_before[
                                signal_index];
                        const auto output_match
                            = metadata.output_index
                                    < implementation.region_activation_programs[
                                          component]
                                          ->activation_kernel.outputs.size()
                            ? &implementation.region_activation_programs[component]
                                   ->activation_kernel.outputs[metadata.output_index]
                            : nullptr;
                        const ForwardingJournalRowSnapshot row {
                            metadata.output_index,
                            metadata.signal,
                            metadata.owner,
                            metadata.callback_time,
                            metadata.callback_delta,
                            metadata.callback_systemverilog_round,
                            metadata.callback_order,
                            metadata.origin,
                            { metadata.expected_signal_event,
                                metadata.expected_transaction,
                                metadata.expected_event_stamp.origin,
                                metadata.expected_event_stamp.systemverilog_round,
                                metadata.expected_value_revision } };
                        prewrite_fallback.rows.push_back(row);
                        prewrite_fallback.callback_keys_match_metadata
                            = prewrite_fallback.callback_keys_match_metadata
                            && output_match != nullptr
                            && output_match->signal == metadata.signal
                            && output_match->owner == metadata.owner
                            && same_signal_metadata(
                                row.expected_signal_metadata, live_metadata)
                            && metadata.origin.process_domain
                                == ProcessSchedulingDomain::systemverilog
                            && metadata.origin.phase == SchedulerPhase::active
                            && row.expected_signal_metadata.event.has_value()
                            && metadata.callback_time
                                == row.expected_signal_metadata.event->first
                            && metadata.callback_systemverilog_round
                                == metadata.expected_event_stamp.systemverilog_round
                            && (index == 0U
                                || bank.applied_role_metadata[index - 1U]
                                        .callback_order
                                    < metadata.callback_order);
                    }
                    prewrite_fallback.private_rows_at_safe_point
                        = bank.applied_role_mutations.size();
                    prewrite_fallback.safe_point_bank_active = bank.active;
                    prewrite_fallback.safe_point_observed_private_rows = true;
                } else {
                    const auto sink_receipt_is_current = [&] {
                        if (join_id >= implementation.processes.size()
                            || join_id
                                >= implementation
                                       .region_readiness_queued_by_process.size()
                            || join_id
                                >= implementation
                                       .region_readiness_member_index_by_process
                                       .size()
                            || !prewrite_fallback.original_sink_key) {
                            return false;
                        }
                        const auto& process
                            = implementation.processes[join_id];
                        const auto& queued
                            = implementation
                                  .region_readiness_queued_by_process[join_id];
                        return process.queued && queued.key_valid
                            && queued.component == component
                            && queued.member
                                == implementation
                                       .region_readiness_member_index_by_process[
                                           join_id]
                            && queued.generation
                                == implementation.region_runtime_generation
                            && same_region_frontier_key(
                                queued.queued_key,
                                *prewrite_fallback.original_sink_key);
                    };

                    const std::array<ForwardingJoinSignalState, 2U>
                        checked_root_roles {
                            forwarding_join_signal_state(
                                implementation, first_internal),
                            forwarding_join_signal_state(
                                implementation, second_internal) };
                    const std::array<ForwardingJournalSignalMetadataSnapshot,
                        2U> checked_root_metadata {
                            capture_signal_metadata(first_internal),
                            capture_signal_metadata(second_internal) };
                    const auto root_publication_is_committed = [&](
                        const std::size_t index,
                        const PackedLogic4& current,
                        const PackedLogic4& previous,
                        const PackedLogic4& stored,
                        const PackedLogic4& raw_owner) {
                        const auto& roles = checked_root_roles[index];
                        const auto& metadata
                            = checked_root_metadata[index];
                        return roles.current == current
                            && roles.last == previous
                            && roles.stored == stored
                            && roles.raw_owner == raw_owner
                            && roles.transaction && roles.event
                            && roles.transaction->first == 1U
                            && roles.event->first == 1U
                            && roles.transaction == roles.event
                            && metadata.transaction == roles.transaction
                            && metadata.event == roles.event
                            && metadata.event_origin.process_domain
                                == ProcessSchedulingDomain::systemverilog
                            && metadata.event_origin.phase
                                == SchedulerPhase::active
                            && metadata.event_round != 0U
                            && metadata.value_revision
                                > prewrite_fallback
                                      .initial_internal_value_revisions[index];
                    };
                    if (!prewrite_fallback.original_sink_key
                        || !sink_receipt_is_current()
                        || !root_publication_is_committed(
                            0U, PackedLogic4(1U, Logic4::one),
                            PackedLogic4(1U, Logic4::zero),
                            PackedLogic4(1U, Logic4::one),
                            PackedLogic4(1U, Logic4::one))
                        || !root_publication_is_committed(
                            1U, PackedLogic4(1U, Logic4::zero),
                            PackedLogic4(1U, Logic4::one),
                            PackedLogic4(1U, Logic4::zero),
                            PackedLogic4(1U, Logic4::zero))) {
                        return;
                    }
                    prewrite_fallback.checked_control_ready_before_boundary_update
                        = true;
                    prewrite_fallback.safe_point_found_no_frontier_runtime
                        = component
                                >= implementation
                                      .region_frontier_runtime_by_component.size()
                        || !implementation.region_frontier_runtime_by_component[
                            component];
                    const auto authoritative
                        = component
                                < implementation
                                      .region_authoritative_state_by_component.size()
                        ? implementation.region_authoritative_state_by_component[
                              component].get()
                        : nullptr;
                    if (authoritative != nullptr && authoritative->valid()
                        && authoritative->values().packed_signal_slots_bound(
                            first_internal)
                        && authoritative->values().packed_signal_slots_bound(
                            second_internal)) {
                        prewrite_fallback.authoritative_revision_before
                            = authoritative->values().revision();
                    }
                    prewrite_fallback.roles_before = checked_root_roles;
                    prewrite_fallback.signal_metadata_before
                        = checked_root_metadata;
                }
                if (!external_trigger
                    || !prewrite_fallback.original_sink_key) {
                    return;
                }

                prewrite_fallback.boundary_update_scheduled_after_root_publication
                    = true;
                outcome.sensitivity_boundary_change_queued = true;
                outcome.sensitivity_boundary_change_after_native_evaluation
                    = forwarding_enabled;
                if (forwarding_enabled) {
                    forwarding_probe.fail_next_forwarding = true;
                }
                const auto saved_sink_key
                    = *prewrite_fallback.original_sink_key;
                interpreter.scheduler().schedule_systemverilog_next_delta(
                    SchedulerPhase::active, 0U,
                    [&, key_matches_origin, saved_sink_key,
                        trigger = *external_trigger](
                        Scheduler& safe_scheduler) {
                        auto& current_implementation
                            = OwnedDriverDemotionTestAccess::implementation(
                                interpreter);
                        const auto queued_sink_key_matches = [&] {
                            if (join_id
                                    >= current_implementation
                                           .region_readiness_queued_by_process
                                           .size()
                                || join_id >= current_implementation.processes
                                               .size()) {
                                return false;
                            }
                            const auto& queued
                                = current_implementation
                                      .region_readiness_queued_by_process[
                                          join_id];
                            if (!current_implementation.processes[join_id].queued
                                || !queued.key_valid
                                || queued.component != component
                                || join_id
                                    >= current_implementation
                                          .region_readiness_member_index_by_process
                                          .size()
                                || queued.member
                                    != current_implementation
                                           .region_readiness_member_index_by_process[
                                               join_id]
                                || queued.generation
                                    != current_implementation
                                           .region_runtime_generation) {
                                return false;
                            }
                            const RegionKernelActivationOrigin origin {
                                static_cast<ProcessSchedulingDomain>(
                                    queued.queued_key.process_domain),
                                static_cast<SchedulerPhase>(
                                    queued.queued_key.phase),
                                queued.queued_key.time,
                                queued.queued_key.delta,
                                static_cast<StableOrder>(
                                    queued.queued_key.stable_order),
                                queued.queued_key.sequence,
                                queued.queued_key.systemverilog_round };
                            return key_matches_origin(saved_sink_key, origin);
                        };
                        prewrite_fallback.sink_receipt_matches_before_boundary_update
                            = queued_sink_key_matches();
                        prewrite_fallback.boundary_update_preceded_sink_key
                            = safe_scheduler.now() == saved_sink_key.time
                            && safe_scheduler.delta() == saved_sink_key.delta
                            && safe_scheduler.systemverilog_round()
                                == saved_sink_key.systemverilog_round
                            && saved_sink_key.stable_order > 0U;
                        outcome.sensitivity_boundary_revision_before
                            = current_implementation.signal_value_revisions.at(
                                trigger);
                        prewrite_fallback.boundary_value_before_commit
                            = current_implementation.logical_signal_value(
                                trigger);
                        prewrite_fallback.boundary_value_before_commit_captured
                            = true;
                        outcome.sensitivity_certificate_current_before
                            = current_implementation.region_graph
                            && current_implementation.region_graph
                                   ->component_epochs_current(component);
                        std::size_t boundary_index { };
                        if (forwarding_enabled) {
                            const auto before_local
                                = component
                                        < current_implementation
                                              .region_local_wave_state_by_component
                                              .size()
                                ? current_implementation
                                      .region_local_wave_state_by_component[
                                          component]
                                      .get()
                                : nullptr;
                            outcome.sensitivity_bank_active_before
                                = before_local != nullptr
                                && before_local->forwarding_results
                                && before_local->forwarding_results->active;
                            if (before_local == nullptr
                                || !before_local->forwarding_results) {
                                return;
                            }
                            const auto& before_bank
                                = *before_local->forwarding_results;
                            const auto boundary = std::ranges::find(
                                before_bank.boundary_signals, trigger);
                            outcome.sensitivity_bank_contains_boundary
                                = boundary != before_bank.boundary_signals.end();
                            if (!outcome.sensitivity_bank_contains_boundary) {
                                return;
                            }
                            boundary_index = static_cast<std::size_t>(
                                boundary - before_bank.boundary_signals.begin());
                            if (boundary_index
                                    >= before_bank.boundary_revisions.size()
                                || boundary_index
                                    >= before_bank.boundary_values.size()
                                || before_bank.boundary_revisions[boundary_index]
                                    != current_implementation
                                           .signal_value_revisions.at(trigger)
                                || before_bank.boundary_values[boundary_index]
                                    != current_implementation.logical_signal_value(
                                        trigger)) {
                                return;
                            }
                            const auto& forwarding
                                = *current_implementation
                                       .region_activation_programs.at(component)
                                       ->forwarding_kernel;
                            const auto boundary_input = std::ranges::find(
                                forwarding.execution_kernel.inputs, trigger,
                                &RegionConeKernelInput::signal);
                            prewrite_fallback.prewrite_boundary_is_kernel_input
                                = boundary_input
                                    != forwarding.execution_kernel.inputs.end()
                                && !boundary_input->internal;
                        }

                        current_implementation.commit(trigger,
                            PackedLogic4(1U, Logic4::one),
                            { ProcessSchedulingDomain::systemverilog,
                                SchedulerPhase::active });
                        outcome.sensitivity_boundary_revision_after
                            = current_implementation.signal_value_revisions.at(
                                trigger);
                        outcome.sensitivity_certificate_current_after
                            = current_implementation.region_graph
                            && current_implementation.region_graph
                                   ->component_epochs_current(component);
                        outcome.sensitivity_boundary_value
                            = current_implementation.logical_signal_value(
                                trigger);
                        prewrite_fallback.roles_after_prewrite_commit = {
                            forwarding_join_signal_state(current_implementation,
                                first_internal),
                            forwarding_join_signal_state(current_implementation,
                                second_internal) };
                        prewrite_fallback.signal_metadata_after_prewrite_commit = {
                            capture_signal_metadata(first_internal),
                            capture_signal_metadata(second_internal) };
                        const auto authoritative_after_prewrite
                            = component
                                    < current_implementation
                                          .region_authoritative_state_by_component
                                          .size()
                            ? current_implementation
                                  .region_authoritative_state_by_component[
                                      component]
                                  .get()
                            : nullptr;
                        if (authoritative_after_prewrite != nullptr
                            && authoritative_after_prewrite->valid()) {
                            prewrite_fallback.authoritative_revision_at_prewrite_commit
                                = authoritative_after_prewrite->values().revision();
                        }
                        const auto after_local
                            = component
                                    < current_implementation
                                          .region_local_wave_state_by_component
                                          .size()
                            ? current_implementation
                                  .region_local_wave_state_by_component[
                                      component]
                                  .get()
                            : nullptr;
                        const auto after_bank
                            = after_local == nullptr
                                || !after_local->forwarding_results
                            ? nullptr : &*after_local->forwarding_results;
                        prewrite_fallback.prewrite_journal_empty_after_commit
                            = after_bank != nullptr
                            && after_bank->applied_role_mutations.empty()
                            && after_bank->applied_role_metadata.empty()
                            && !after_bank->role_journal_enabled
                            && after_bank->private_epoch_retired;
                        const std::array<ForwardingJoinSignalState, 2U>
                            expected_prewrite_roles {
                                ForwardingJoinSignalState {
                                    PackedLogic4(1U, Logic4::one),
                                    PackedLogic4(1U, Logic4::zero),
                                    PackedLogic4(1U, Logic4::one),
                                    PackedLogic4(1U, Logic4::one),
                                    { }, { } },
                                ForwardingJoinSignalState {
                                    PackedLogic4(1U, Logic4::zero),
                                    PackedLogic4(1U, Logic4::one),
                                    PackedLogic4(1U, Logic4::zero),
                                    PackedLogic4(1U, Logic4::zero),
                                    { }, { } } };
                        const auto same_public_roles = [](
                            const ForwardingJoinSignalState& left,
                            const ForwardingJoinSignalState& right) {
                            return left.current == right.current
                                && left.last == right.last
                                && left.stored == right.stored
                                && left.raw_owner == right.raw_owner;
                        };
                        prewrite_fallback.prewrite_roles_materialized_at_commit
                            = same_public_roles(
                                  prewrite_fallback.roles_after_prewrite_commit[0U],
                                  expected_prewrite_roles[0U])
                            && same_public_roles(
                                  prewrite_fallback.roles_after_prewrite_commit[1U],
                                  expected_prewrite_roles[1U]);
                        prewrite_fallback.prewrite_signal_metadata_preserved_at_commit
                            = std::ranges::equal(
                                prewrite_fallback.signal_metadata_before,
                                prewrite_fallback
                                    .signal_metadata_after_prewrite_commit,
                                same_signal_metadata);
                        prewrite_fallback.sink_receipt_matches_after_boundary_update
                            = queued_sink_key_matches();
                        outcome.sensitivity_boundary_change_ran
                            = outcome.sensitivity_boundary_revision_after
                                != outcome.sensitivity_boundary_revision_before;
                    });
            });
    }

    auto run_result = interpreter.run();
    if (prewrite_hook_token != 0U) {
        interpreter.scheduler().remove_safe_point_hook(prewrite_hook_token);
    }
    if (join_continuation_hook_token != 0U) {
        interpreter.scheduler().remove_safe_point_hook(
            join_continuation_hook_token);
    }
    outcome.completed = run_result.status == RunStatus::completed;
    outcome.member_seed_prefix_was_exact
        = forwarding_probe.origin_was_exact_member_seed_prefix;
    outcome.forwarding_calls
        = forwarding_probe.forwarding_calls - calls_before;
    outcome.forwarding_evaluations
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations
            - evaluations_before;
    outcome.forwarding_declines
        = implementation.systemverilog_wave_profile_region_forwarding_declines
            - declines_before;
    outcome.forwarding_member_consumptions
        = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
            - members_before;
    if (exercise_prewrite_fallback && forwarding_enabled) {
        const auto& failed = forwarding_probe.failed_prefix;
        if (forwarding_probe.failed_prefix_captured
            && failed.task_count == 1U
            && failed.frontier_generation != 0U
            && failed.frontier_cursor < failed.frontier_end
            && failed.frontier_end - failed.frontier_cursor == 1U
            && failed.process_domain
                == ProcessSchedulingDomain::systemverilog
            && failed.phase == SchedulerPhase::active) {
            const auto& task = failed.tasks[0U];
            const auto& origin = task.member.origin;
            const RegionFrontierKeyV1 failed_key {
                origin.time,
                origin.delta,
                origin.systemverilog_round,
                origin.stable_order,
                origin.sequence,
                static_cast<std::uint32_t>(origin.process_domain),
                static_cast<std::uint32_t>(origin.phase) };
            prewrite_fallback.ordinary_decline_prefix_matches_sink_key
                = task.member.process == join_id
                && task.task_ordinal == failed.frontier_cursor
                && prewrite_fallback.original_sink_key.has_value()
                && failed.time
                    == prewrite_fallback.original_sink_key->time
                && failed.delta
                    == prewrite_fallback.original_sink_key->delta
                && failed.systemverilog_round
                    == prewrite_fallback.original_sink_key->systemverilog_round
                && failed_key.time
                    == prewrite_fallback.original_sink_key->time
                && failed_key.delta
                    == prewrite_fallback.original_sink_key->delta
                && failed_key.systemverilog_round
                    == prewrite_fallback.original_sink_key->systemverilog_round
                && failed_key.stable_order
                    == prewrite_fallback.original_sink_key->stable_order
                && failed_key.sequence
                    == prewrite_fallback.original_sink_key->sequence
                && failed_key.process_domain
                    == prewrite_fallback.original_sink_key->process_domain
                && failed_key.phase
                    == prewrite_fallback.original_sink_key->phase;
        }
        prewrite_fallback.ordinary_forwarding_decline_observed
            = forwarding_probe.ordinary_forwarding_decline_observed;
        prewrite_fallback.ordinary_decline_channel_empty
            = forwarding_probe.ordinary_decline_channel_empty
            && !forwarding_probe.pending_failure;
    }
    outcome.join_checked_resumes
        = executor_probe.resumes[2U] - join_resumes_before;
    // A completed run can intentionally retain accepted private role rows.
    // Observe one component signal before reading the A4-backed final roles.
    static_cast<void>(interpreter.signal_value_snapshot(first_internal));
    outcome.final_state = {
        forwarding_join_signal_state(implementation, first_internal),
        forwarding_join_signal_state(implementation, second_internal),
        forwarding_join_signal_state(implementation, output) };
    outcome.join_samples = std::move(join_samples);
    outcome.member_seed_prefixes
        = std::move(forwarding_probe.member_seed_prefixes);
    outcome.forwarding_prefixes
        = std::move(forwarding_probe.forwarding_prefixes);
    if (exercise_prewrite_fallback) {
        for (std::size_t process = 0U;
             process < prewrite_fallback.checked_process_resumes.size();
             ++process) {
            prewrite_fallback.checked_process_resumes[process]
                = executor_probe.resumes[process]
                - executor_resumes_before_stimulus[process];
        }
        prewrite_fallback.activation_calls_after_stimulus
            = forwarding_probe.activation_calls
            - activation_calls_before_stimulus;
        outcome.prewrite_fallback = std::move(prewrite_fallback);
    }
    outcome.sensitivity_boundary_value = external_trigger
        ? implementation.logical_signal_value(*external_trigger)
        : PackedLogic4 { };
    return outcome;
}

void check_region_forwarding_multi_parent_join()
{
    const auto checked = run_forwarding_join_case(false,
        ForwardingJoinOrder::topological);
    const auto native = run_forwarding_join_case(true,
        ForwardingJoinOrder::topological, false, false, true);
    const auto checked_reverse = run_forwarding_join_case(false,
        ForwardingJoinOrder::reverse_roots);
    const auto native_reverse = run_forwarding_join_case(true,
        ForwardingJoinOrder::reverse_roots, false, false, true);
    const auto require_parity = [](const ForwardingJoinOutcome& reference,
                                    const ForwardingJoinOutcome& actual,
                                    const char* const message) {
        require(reference.completed && actual.completed
                && reference.final_state == actual.final_state,
            message);
    };
    require_parity(checked, native,
        "a complete two-root forwarding join matches checked roles and samples");
    require_parity(checked_reverse, native_reverse,
        "a reversed root ProcessId order preserves checked join behavior");
    const auto member_prefixes_are_exact = [](
        const ForwardingJoinOutcome& outcome,
        const std::span<const std::vector<ProcessId>> expected_prefixes) {
        std::size_t expected_task_count { };
        for (const auto& prefix : expected_prefixes) {
            expected_task_count += prefix.size();
        }
        if (outcome.forwarding_calls != expected_prefixes.size()
            || outcome.forwarding_evaluations != expected_prefixes.size()
            || outcome.forwarding_prefixes.size() != expected_prefixes.size()
            || outcome.member_seed_prefixes.size() != expected_prefixes.size()
            || !outcome.member_seed_prefix_was_exact) {
            return false;
        }
        std::vector<std::uint64_t> captured_sequences;
        captured_sequences.reserve(expected_task_count);
        for (std::size_t prefix_index = 0U;
             prefix_index < expected_prefixes.size(); ++prefix_index) {
            const auto& expected = expected_prefixes[prefix_index];
            const auto& prefix = outcome.forwarding_prefixes[prefix_index];
            if (outcome.member_seed_prefixes[prefix_index] != expected
                || prefix.task_count != expected.size()
                || prefix.frontier_generation == 0U
                || prefix.systemverilog_round == 0U
                || prefix.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || prefix.phase != SchedulerPhase::active
                || prefix.task_count > prefix.tasks.size()
                || prefix.frontier_cursor > prefix.frontier_end
                || prefix.task_count
                    > prefix.frontier_end - prefix.frontier_cursor) {
                return false;
            }
            for (std::size_t task_index = 0U;
                 task_index < prefix.task_count; ++task_index) {
                const auto& task = prefix.tasks[task_index];
                const auto& origin = task.member.origin;
                if (std::ranges::find(captured_sequences, origin.sequence)
                        != captured_sequences.end()) {
                    return false;
                }
                captured_sequences.push_back(origin.sequence);
                if (task.task_ordinal
                        != prefix.frontier_cursor + task_index
                    || task.member.process != expected[task_index]
                    || origin.process_domain
                        != ProcessSchedulingDomain::systemverilog
                    || origin.phase != SchedulerPhase::active
                    || origin.time != prefix.time
                    || origin.delta != prefix.delta
                    || origin.systemverilog_round
                        != prefix.systemverilog_round
                    || origin.stable_order != expected[task_index]) {
                    return false;
                }
            }
        }
        return captured_sequences.size() == expected_task_count;
    };
    const std::array<std::vector<ProcessId>, 1U> root_seed_prefix {
        std::vector<ProcessId> { 0U, 1U } };
    const auto join_continuation_is_exact = [](
        const ForwardingJoinOutcome& outcome) {
        const auto& key = outcome.join_continuation_key;
        return outcome.join_continuation_receipt_captured
            && outcome.join_continuation_bank_ready
            && outcome.join_continuation_receipt_retired
            && outcome.join_continuation_consumed_once
            && outcome.join_consumptions_at_receipt == 2U
            && outcome.join_consumptions_after_retirement == 3U
            && outcome.join_checked_resumes == 0U
            && key.time == 1U
            && key.systemverilog_round != 0U
            && key.stable_order == 2U
            && key.process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && key.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active);
    };
    const auto external_child_key_is_distinct = [&join_continuation_is_exact](
        const ForwardingJoinOutcome& outcome) {
        if (!join_continuation_is_exact(outcome)
            || outcome.forwarding_prefixes.size() != 2U) {
            return false;
        }
        const auto& prefix = outcome.forwarding_prefixes.back();
        if (prefix.task_count != 1U) {
            return false;
        }
        const auto& task = prefix.tasks.front();
        const auto& key = outcome.join_continuation_key;
        const auto& origin = task.member.origin;
        return task.member.process == 2U
            && origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && origin.phase == SchedulerPhase::active
            && origin.time == 2U
            && origin.time != key.time
            && origin.delta == key.delta
            && origin.stable_order == key.stable_order
            && origin.sequence != key.sequence
            && origin.systemverilog_round != 0U;
    };
    require(checked.forwarding_calls == 0U
            && native.forwarding_calls == 1U
            && native.forwarding_evaluations == 1U
            && native.forwarding_declines == 0U
            && native.forwarding_member_consumptions == 3U
            && member_prefixes_are_exact(native, root_seed_prefix)
            && join_continuation_is_exact(native),
        "one authenticated two-root evaluation consumes the cached join through its original child receipt");
    require(checked_reverse.forwarding_calls == 0U
            && native_reverse.forwarding_calls == 1U
            && native_reverse.forwarding_evaluations == 1U
            && native_reverse.forwarding_declines == 0U
            && native_reverse.forwarding_member_consumptions == 3U
            && member_prefixes_are_exact(native_reverse, root_seed_prefix)
            && join_continuation_is_exact(native_reverse),
        "reversing root ProcessIds preserves the authenticated seed and original cached-join receipt");
    require(native.final_state[0U].current == PackedLogic4(1U, Logic4::one)
            && native.final_state[1U].current == PackedLogic4(1U, Logic4::zero)
            && native.final_state[2U].current == PackedLogic4(1U, Logic4::one)
            && !checked.join_samples.empty()
            && checked.join_samples.back()
                == std::pair<std::uint8_t, std::uint8_t> { 1U, 0U },
        "the checked join reads both settled predecessors while authenticated native prefixes consume all three members");
    require(std::ranges::all_of(native.final_state,
                [](const ForwardingJoinSignalState& state) {
                    return state.stored == state.current
                        && state.raw_owner == state.current;
                }),
        "the native join leaves all current, stored, and owner roles coherent");

    const auto checked_external_seed = run_forwarding_join_case(false,
        ForwardingJoinOrder::topological, true);
    const auto native_external_seed = run_forwarding_join_case(true,
        ForwardingJoinOrder::topological, true, false, true);
    require(checked_external_seed.completed && native_external_seed.completed
            && checked_external_seed.final_state
                == native_external_seed.final_state,
        "a sensitivity-only external wake preserves checked and native role metadata");
    const std::array<std::vector<ProcessId>, 2U>
        external_backend_seed_prefixes {
        std::vector<ProcessId> { 0U, 1U },
        std::vector<ProcessId> { 2U } };
    require(checked_external_seed.forwarding_calls == 0U
            && native_external_seed.forwarding_calls == 2U
            && native_external_seed.forwarding_evaluations == 2U
            && native_external_seed.forwarding_declines == 0U
            && native_external_seed.forwarding_member_consumptions == 4U
            && member_prefixes_are_exact(
                native_external_seed, external_backend_seed_prefixes)
            && join_continuation_is_exact(native_external_seed)
            && external_child_key_is_distinct(native_external_seed),
        "the sensitivity-only event executes the quiet child once at each of its two distinct original scheduler keys");
    require(native_external_seed.final_state[0U].current
                == PackedLogic4(1U, Logic4::one)
            && native_external_seed.final_state[1U].current
                == PackedLogic4(1U, Logic4::zero)
            && native_external_seed.final_state[2U].transaction
                != native.final_state[2U].transaction
            && native_external_seed.final_state[2U].event
                == native.final_state[2U].event,
        "the external-only child publication retains equal-value transaction semantics without parent changes");

    const auto checked_stale_sensitivity = run_forwarding_join_case(false,
        ForwardingJoinOrder::topological, false, true);
    const auto native_stale_sensitivity = run_forwarding_join_case(true,
        ForwardingJoinOrder::topological, false, true, true);
    require(checked_stale_sensitivity.completed
            && native_stale_sensitivity.completed
            && checked_stale_sensitivity.final_state
                == native_stale_sensitivity.final_state
            && checked_stale_sensitivity.join_samples.size() == 1U
            && checked_stale_sensitivity.join_samples.front()
                == std::pair<std::uint8_t, std::uint8_t> { 1U, 0U }
            && native_stale_sensitivity.join_samples
                == checked_stale_sensitivity.join_samples
            && checked_stale_sensitivity.sensitivity_boundary_change_queued
            && native_stale_sensitivity.sensitivity_boundary_change_queued
            && checked_stale_sensitivity.sensitivity_boundary_change_ran
            && native_stale_sensitivity.sensitivity_boundary_change_ran
            && checked_stale_sensitivity.sensitivity_certificate_current_before
            && checked_stale_sensitivity.sensitivity_certificate_current_after
            && native_stale_sensitivity.sensitivity_certificate_current_before
            && native_stale_sensitivity.sensitivity_certificate_current_after
            && native_stale_sensitivity.sensitivity_bank_active_before
            && native_stale_sensitivity.sensitivity_bank_active_after
            && native_stale_sensitivity.sensitivity_bank_contains_boundary
            && native_stale_sensitivity.sensitivity_bank_boundary_stale_after
            && !checked_stale_sensitivity
                    .sensitivity_boundary_change_after_native_evaluation
            && native_stale_sensitivity
                .sensitivity_boundary_change_after_native_evaluation
            && native_stale_sensitivity.sensitivity_boundary_revision_after
                != native_stale_sensitivity.sensitivity_boundary_revision_before
            && checked_stale_sensitivity.sensitivity_boundary_value
                == PackedLogic4(1U, Logic4::one)
            && native_stale_sensitivity.sensitivity_boundary_value
                == PackedLogic4(1U, Logic4::one),
        "a same-time foreign Active task changes the sensitivity-only boundary from the captured two-root cut");
    require(checked_stale_sensitivity.forwarding_calls == 0U
            && native_stale_sensitivity.forwarding_calls == 1U
            && native_stale_sensitivity.forwarding_evaluations == 1U
            && native_stale_sensitivity.forwarding_declines == 1U
            && native_stale_sensitivity.forwarding_member_consumptions == 2U
            && native_stale_sensitivity.member_seed_prefixes.size() == 1U
            && member_prefixes_are_exact(native_stale_sensitivity,
                root_seed_prefix)
            && native_stale_sensitivity.forwarding_prefixes.size() == 1U
            && !native_stale_sensitivity.sensitivity_child_entry_captured
            && native_stale_sensitivity.join_continuation_receipt_captured
            && native_stale_sensitivity.join_continuation_bank_ready
            && native_stale_sensitivity
                .sensitivity_join_receipt_present_after_foreign_change
            && (!native_stale_sensitivity
                    .sensitivity_join_receipt_present_before_foreign_change
                || native_stale_sensitivity
                       .sensitivity_join_receipt_same_across_foreign_change)
            && native_stale_sensitivity
                .join_continuation_receipt_retained_after_foreign_change
            && native_stale_sensitivity.join_continuation_receipt_retired
            && native_stale_sensitivity.join_continuation_checked_once
            && native_stale_sensitivity.join_checked_resumes == 1U
            && native_stale_sensitivity.member_seed_prefix_was_exact,
        "the stale root bank flushes before the child runs once through its "
        "original checked scheduler key");

}

void check_region_forwarding_prewrite_journal_decline_fallback()
{
    const auto checked = run_forwarding_join_case(false,
        ForwardingJoinOrder::topological, false, false, false, true);
    const auto fallback = run_forwarding_join_case(true,
        ForwardingJoinOrder::topological, false, false, true, true);
    const auto& observation = fallback.prewrite_fallback;
    const auto same_public_roles = [](
                                        const ForwardingJoinSignalState& left,
                                        const ForwardingJoinSignalState& right) {
        return left.current == right.current
            && left.last == right.last
            && left.stored == right.stored
            && left.raw_owner == right.raw_owner;
    };
    const auto root_key_matches = [&](
        const std::optional<RegionFrontierKeyV1>& key,
        const std::size_t task_index,
        const ProcessId process) {
        if (!key
            || task_index >= observation.accepted_root_prefix.task_count) {
            return false;
        }
        const auto& task
            = observation.accepted_root_prefix.tasks[task_index];
        const auto& origin = task.member.origin;
        return task.member.process == process
            && key->time == origin.time
            && key->delta == origin.delta
            && key->systemverilog_round == origin.systemverilog_round
            && key->stable_order == origin.stable_order
            && key->sequence == origin.sequence
            && key->process_domain
                == static_cast<std::uint32_t>(origin.process_domain)
            && key->phase == static_cast<std::uint32_t>(origin.phase)
            && key->stable_order == process
            && key->process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && key->phase
                == static_cast<std::uint32_t>(SchedulerPhase::active);
    };

    require(checked.completed && fallback.completed
            && checked.final_state == fallback.final_state
            && checked.prewrite_fallback
                   .checked_control_ready_before_boundary_update
            && checked.prewrite_fallback.safe_point_found_no_frontier_runtime
            && checked.sensitivity_boundary_change_queued
            && checked.sensitivity_boundary_change_ran
            && !checked.sensitivity_boundary_change_after_native_evaluation
            && checked.sensitivity_boundary_revision_after
                > checked.sensitivity_boundary_revision_before
            && checked.sensitivity_boundary_value
                == PackedLogic4(1U, Logic4::one)
            && checked.final_state[2U].current
                == PackedLogic4(1U, Logic4::zero)
            && checked.join_samples.size() == 1U
            && checked.join_samples.front()
                == std::pair<std::uint8_t, std::uint8_t> { 1U, 0U }
            && fallback.join_samples == checked.join_samples
            && observation.activation_internal_values_captured[0U]
            && observation.activation_internal_values_captured[1U]
            && observation.activation_internal_values[0U]
                == PackedLogic4(1U,
                    checked.join_samples.front().first == 0U
                        ? Logic4::zero : Logic4::one)
            && observation.activation_internal_values[1U]
                == PackedLogic4(1U,
                    checked.join_samples.front().second == 0U
                        ? Logic4::zero : Logic4::one)
            && observation.activation_boundary_value_captured
            && observation.activation_boundary_value
                == PackedLogic4(1U, Logic4::one),
        "the declined activation receives the same checked internal values and changed guard used by the control");
    require(fallback.forwarding_calls == 2U
            && fallback.forwarding_evaluations == 1U
            && fallback.forwarding_declines == 1U
            && fallback.forwarding_member_consumptions == 2U
            && fallback.member_seed_prefix_was_exact
            && fallback.member_seed_prefixes
                == std::vector<std::vector<ProcessId>> {
                    { 0U, 1U } }
            && fallback.forwarding_prefixes.size() == 1U
            && observation.ordinary_forwarding_decline_observed
            && observation.ordinary_decline_channel_empty
            && observation.ordinary_decline_prefix_matches_sink_key
            && observation.accepted_root_prefix_calls == 1U
            && observation.accepted_root_prefix_captured
            && observation.accepted_root_prefix_is_exact
            && observation.accepted_root_prefix.task_count == 2U
            && root_key_matches(observation.original_root_keys[0U], 0U, 0U)
            && root_key_matches(observation.original_root_keys[1U], 1U, 1U)
            && observation.original_root_keys[0U]->sequence
                != observation.original_root_keys[1U]->sequence
            && observation.checked_process_resumes
                == std::array<std::size_t, 3U> { 0U, 0U, 1U }
            && observation.activation_calls_after_stimulus == 1U,
        "the exact root prefix is accepted once and its child-only forwarding retry declines normally before one checked resume");
    require(observation.safe_point_observed_private_rows
            && observation.safe_point_found_no_frontier_runtime
            && observation.safe_point_bank_active
            && observation.boundary_update_scheduled_after_root_publication
            && observation.private_rows_at_safe_point == 2U
            && observation.rows.size() == 2U
            && observation.original_sink_key.has_value()
            && observation.original_sink_key->time == 1U
            && observation.original_sink_key->stable_order == 2U
            && observation.original_sink_key->systemverilog_round != 0U
            && observation.original_sink_key->sequence
                != observation.original_root_keys[0U]->sequence
            && observation.original_sink_key->sequence
                != observation.original_root_keys[1U]->sequence
            && observation.original_sink_key->process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && observation.original_sink_key->phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && observation.callback_keys_match_metadata
            && observation.rows[0U].signal != observation.rows[1U].signal
            && observation.rows[0U].callback_order
                < observation.rows[1U].callback_order
            && observation.roles_before[0U].current
                == PackedLogic4(1U, Logic4::zero)
            && observation.roles_before[1U].current
                == PackedLogic4(1U, Logic4::one)
            && same_public_roles(observation.roles_before[0U],
                fallback.initial_state[0U])
            && same_public_roles(observation.roles_before[1U],
                fallback.initial_state[1U]),
        "a passive cut records two authentic private rows and unchanged public A4 roles");
    require(fallback.sensitivity_boundary_change_queued
            && fallback.sensitivity_boundary_change_ran
            && fallback.sensitivity_boundary_change_after_native_evaluation
            && fallback.sensitivity_certificate_current_before
            && fallback.sensitivity_certificate_current_after
            && fallback.sensitivity_bank_active_before
            && fallback.sensitivity_bank_contains_boundary
            && fallback.sensitivity_boundary_revision_after
                > fallback.sensitivity_boundary_revision_before
            && observation.boundary_value_before_commit_captured
            && observation.boundary_value_before_commit
                == PackedLogic4(1U, Logic4::zero)
            && fallback.sensitivity_boundary_value
                == PackedLogic4(1U, Logic4::one)
            && observation.boundary_update_preceded_sink_key
            && observation.sink_receipt_matches_before_boundary_update
            && observation.sink_receipt_matches_after_boundary_update
            && observation.prewrite_boundary_is_kernel_input
            && observation.prewrite_journal_empty_after_commit
            && observation.prewrite_roles_materialized_at_commit
            && observation.authoritative_revision_at_prewrite_commit
                > observation.authoritative_revision_before
            && observation.prewrite_signal_metadata_preserved_at_commit,
        "a real foreign guard write materializes private rows before changing the forwarding input and retains the child's original key");
    require(observation.activation_called
            && observation.activation_has_no_frontier_runtime
            && observation.activation_prefix_captured
            && observation.activation_is_single_join_member
            && observation.sink_receipt_retained_at_fallback,
        "the legacy activation fallback receives the child's retained scheduler-authored key with no V2 runtime");
    require(observation.role_journal_was_flushed_before_activation
            && observation.authoritative_revision_at_activation
                >= observation.authoritative_revision_at_prewrite_commit
            && observation.all_roles_materialized_before_activation
            && observation.activation_inputs_match_materialized_values
            && observation.signal_metadata_preserved_before_activation,
        "the ordinary decline reaches legacy activation with materialized roles and unchanged internal event metadata");
}

struct ForwardingUnequalDepthOutcome {
    bool completed { };
    bool member_seed_prefix_was_exact { true };
    bool initial_a4_roles_bound { };
    bool initial_a4_requires_prewrite_unbind { };
    std::array<bool, 2U> join_a4_roles_bound { };
    bool late_observation_demoted_roles { };
    bool boundary_revision_captured { };
    bool partial_join_declined_before_checked_resume { };
    bool activation_cut_has_original_join_key { };
    bool activation_cut_captured { };
    bool stopped_at_first_join_callback { };
    bool middle_output_pending_at_stop { };
    bool join_output_pending_at_stop { };
    std::size_t activation_join_attempts { };
    std::vector<RegionKernelSchedulerPrefix> activation_join_prefixes;
    std::vector<RegionKernelReadyMember> activation_join_requests;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> activation_join_inputs;
    std::size_t forwarding_calls { };
    std::uint64_t forwarding_evaluations { };
    std::uint64_t forwarding_declines { };
    std::uint64_t forwarding_member_consumptions { };
    std::vector<std::vector<ProcessId>> forwarding_seed_prefixes;
    std::vector<RegionKernelSchedulerPrefix> forwarded_join_prefixes;
    std::vector<std::pair<PackedLogic4, PackedLogic4>> forwarded_join_inputs;
    std::uint64_t boundary_revision { };
    std::array<ForwardingJoinSignalState, 4U> initial_state;
    std::array<ForwardingJoinSignalState, 4U> partial_join_state;
    std::array<ForwardingJoinSignalState, 4U> stopped_state;
    std::array<ForwardingJoinSignalState, 4U> settled_join_state;
    std::array<ForwardingJoinSignalState, 4U> final_state;
    std::array<std::uint64_t, 2U> join_boundary_revisions { };
    std::array<bool, 2U> join_boundary_revision_unchanged { };
    std::vector<std::pair<std::uint8_t, std::uint8_t>> join_samples;
    std::vector<std::pair<PackedLogic4, PackedLogic4>> activation_full_join_inputs;
    std::vector<std::pair<PackedLogic4, PackedLogic4>> full_join_samples;
};

ForwardingUnequalDepthOutcome run_forwarding_unequal_depth_join_case(
    const bool forwarding_enabled,
    const std::size_t width = 1U,
    const bool observe_at_first_join = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled {
        "FSIM_PROFILE_SV_WAVES", "1" };

    ForwardingRuntimeProbe forwarding_probe;
    RegionKernelProbe executor_probe;
    bool first_join_stop_requested { };
    std::vector<std::pair<std::uint8_t, std::uint8_t>> join_samples;
    std::vector<std::pair<PackedLogic4, PackedLogic4>> activation_full_join_inputs;
    std::vector<std::pair<PackedLogic4, PackedLogic4>> full_join_samples;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "forward_join_depth.input",
        PackedLogic4(width, Logic4::zero) });
    const auto a_stage = interpreter.add_signal({ "forward_join_depth.a_stage",
        PackedLogic4(width, Logic4::zero), ResolutionKind::sv_wire });
    const auto a_value = interpreter.add_signal({ "forward_join_depth.a_value",
        PackedLogic4(width, Logic4::zero), ResolutionKind::sv_wire });
    const auto b_value = interpreter.add_signal({ "forward_join_depth.b_value",
        PackedLogic4(width, width == 1U ? Logic4::one : Logic4::zero),
        ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "forward_join_depth.output",
        PackedLogic4(width, width == 1U ? Logic4::one : Logic4::zero),
        ResolutionKind::sv_wire });

    constexpr ProcessId join_id = 0U;
    constexpr ProcessId b_root_id = 1U;
    constexpr ProcessId middle_id = 2U;
    constexpr ProcessId a_root_id = 3U;

    const auto make_root = [&](const ProcessId process,
                               const char* const name,
                               const SignalId destination,
                               const bool invert) {
        Process root;
        root.id = process;
        root.name = name;
        root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        root.initialize = true;
        root.register_count = 1U;
        root.static_sensitivity = { { input, EdgeKind::any } };
        root.driver_regions = { { destination, 0U, 0U, true } };
        root.operations = { ReadSignal { 0U, input } };
        if (invert) {
            root.operations.emplace_back(UnaryNot { 0U, 0U });
        } else {
            root.operations.emplace_back(CopyRegister { 0U, 0U });
        }
        root.operations.emplace_back(WriteUpdate { destination, 0U,
            SignalUpdateDomain::systemverilog_active });
        root.operations.emplace_back(WaitSensitivity { });
        root.operations.emplace_back(Jump { 0U });
        return root;
    };

    auto a_root = make_root(a_root_id, "forward_join_depth_root_a",
        a_stage, false);
    auto b_root = make_root(b_root_id, "forward_join_depth_root_b",
        b_value, width == 1U);

    Process middle;
    middle.id = middle_id;
    middle.name = "forward_join_depth_middle";
    middle.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    middle.initialize = true;
    middle.register_count = 1U;
    middle.static_sensitivity = { { a_stage, EdgeKind::any } };
    middle.driver_regions = { { a_value, 0U, 0U, true } };
    middle.operations = {
        ReadSignal { 0U, a_stage },
        CopyRegister { 0U, 0U },
        WriteUpdate { a_value, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    Process join;
    join.id = join_id;
    join.name = "forward_join_depth_consumer";
    join.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    join.initialize = true;
    join.register_count = 2U;
    join.static_sensitivity = {
        { a_value, EdgeKind::any },
        { b_value, EdgeKind::any },
    };
    join.driver_regions = { { output, 0U, 0U, true } };
    join.operations = {
        ReadSignal { 0U, a_value },
        ReadSignal { 1U, b_value },
        Binary { BinaryOperator::bit_xor, 0U, 0U, 1U },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };

    std::vector<Process> members;
    members.reserve(4U);
    members.push_back(std::move(a_root));
    members.push_back(std::move(b_root));
    members.push_back(std::move(middle));
    members.push_back(std::move(join));
    std::ranges::sort(members, std::ranges::less { }, &Process::id);
    for (std::size_t index = 0U; index < members.size(); ++index) {
        require(interpreter.add_process(std::move(members[index])) == index,
            "unequal-depth join members use dense ProcessId order");
    }

    const auto install_executor = [&](const ProcessId process,
                                      const SignalId primary_input,
                                      const SignalId destination,
                                      const std::size_t probe_slot,
                                      const bool invert,
                                      const std::optional<SignalId> secondary,
                                      const bool xor_secondary,
                                      std::vector<std::pair<std::uint8_t,
                                          std::uint8_t>>* const samples) {
        const auto& registered = interpreter.process_program(process);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        const auto register_count = secondary ? 2U : 1U;
        interpreter.set_deferred_process_executor(process,
            [] { return true; },
            [&executor_probe, process, primary_input, destination, probe_slot,
                invert, binding, wait_instruction, secondary, xor_secondary,
                samples, register_count, width] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process, primary_input, destination,
                    wait_instruction, binding, true, probe_slot, true, true,
                    register_count, std::nullopt, secondary, true, invert,
                    xor_secondary, samples, width);
            }, std::move(contract));
    };
    install_executor(a_root_id, input, a_stage, 3U, false,
        std::nullopt, false, nullptr);
    install_executor(b_root_id, input, b_value, 1U, width == 1U,
        std::nullopt, false, nullptr);
    install_executor(middle_id, a_stage, a_value, 2U, false,
        std::nullopt, false, nullptr);
    install_executor(join_id, a_value, output, 0U, false,
        b_value, true, &join_samples);
    interpreter.materialize_ready_process_executors();

    Process clock;
    clock.id = 4U;
    clock.name = "forward_join_depth_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { input, 0U, 0U, true } };
    PackedLogic4 changed_input(width, Logic4::zero);
    if (width == 1U) {
        changed_input = PackedLogic4(width, Logic4::one);
    } else {
        changed_input.set(width - 1U, Logic4::one);
    }
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, changed_input },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == 4U,
        "the unequal-depth fixture has one boundary-input stimulus");

    Process observer;
    observer.id = 5U;
    observer.name = "forward_join_depth_output_reader";
    observer.scheduling_domain = ProcessSchedulingDomain::generic;
    observer.register_count = 1U;
    observer.static_sensitivity = { { output, EdgeKind::any } };
    observer.operations = {
        ReadSignal { 0U, output },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(observer)) == 5U,
        "the generic reader makes only the join output a public boundary");

    interpreter.set_region_kernel_backend_provider(
        std::make_shared<ForwardingRuntimeBackendProvider>(
            forwarding_probe, forwarding_enabled, true));
    interpreter.start();
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "the four-member unequal-depth component reaches its initialized waits");
    forwarding_probe.member_seed_prefixes.clear();
    forwarding_probe.forwarding_prefixes.clear();

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component
        = implementation.region_component_by_process.at(join_id);
    ForwardingUnequalDepthOutcome outcome;
    const auto* const initial_authoritative_state
        = component
                < implementation.region_authoritative_state_by_component.size()
        ? implementation.region_authoritative_state_by_component[component].get()
        : nullptr;
    outcome.initial_a4_roles_bound = initial_authoritative_state != nullptr
        && initial_authoritative_state->values().packed_slots_bound();
    outcome.initial_a4_requires_prewrite_unbind
        = initial_authoritative_state != nullptr
        && initial_authoritative_state->values().requires_prewrite_unbind();
    if (forwarding_enabled && width > 64U) {
        require(component
                    < implementation.region_authoritative_state_by_component.size()
                && implementation.region_authoritative_state_by_component[component],
            "wide forwarding internals seed one authoritative role component");
        const auto& values
            = implementation.region_authoritative_state_by_component[component]
                  ->values();
        for (const auto signal : { a_stage, a_value, b_value }) {
            const auto& writers = implementation.region_graph->signals()[signal]
                                      .writers;
            require(writers.size() == 1U
                    && values.packed_signal_slots_bound(signal)
                    && values.packed_owner_slot_bound(
                        signal, writers.front().process),
                "each wide private parent has bound current, LAST, stored, and raw-owner roles");
        }
        require(!values.packed_signal_slots_bound(output),
            "the observed wide boundary output does not inherit internal-only A2 storage authority");
    }
    outcome.initial_state = {
        forwarding_join_signal_state(implementation, a_stage, width > 64U),
        forwarding_join_signal_state(implementation, a_value, width > 64U),
        forwarding_join_signal_state(implementation, b_value, width > 64U),
        forwarding_join_signal_state(implementation, output) };
    const auto initial_b = PackedLogic4(width,
        width == 1U ? Logic4::one : Logic4::zero);
    require(outcome.initial_state[0U].current
                == PackedLogic4(width, Logic4::zero)
            && outcome.initial_state[1U].current
                == PackedLogic4(width, Logic4::zero)
            && outcome.initial_state[2U].current == initial_b
            && outcome.initial_state[3U].current == initial_b,
        "startup settles the declared unequal-depth join values");
    for (const auto& state : outcome.initial_state) {
        require(state.stored == state.current
                && state.raw_owner == state.current,
            "each unequal-depth startup role begins coherent");
    }
    join_samples.clear();

    const auto evaluations_before
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations;
    const auto declines_before
        = implementation.systemverilog_wave_profile_region_forwarding_declines;
    const auto members_before
        = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions;
    const auto calls_before = forwarding_probe.forwarding_calls;
    if (forwarding_enabled) {
        require(component < implementation.region_activation_programs.size()
                && implementation.region_activation_programs[component]
                && implementation.region_activation_programs[component]
                       ->forwarding_kernel.has_value(),
            "the unequal-depth runtime graph retains a forwarding certificate");
        const auto& forwarding
            = *implementation.region_activation_programs[component]
                   ->forwarding_kernel;
        require(forwarding.members.size() == 4U
                && forwarding.execution_kernel.outputs.size() == 4U
                && forwarding.internal_signals.size() == 3U,
            "the unequal-depth component contains four members and three internal values");
        const auto member_index_for = [&](const ProcessId process) {
            const auto member = std::ranges::find(forwarding.members,
                process, &RegionConeForwardingMember::process);
            if (member == forwarding.members.end()) {
                throw std::runtime_error {
                    "unequal-depth member is absent from forwarding map"
                };
            }
            return static_cast<std::size_t>(member - forwarding.members.begin());
        };
        const auto member_for = [&](const ProcessId process)
            -> const RegionConeForwardingMember& {
            return forwarding.members[member_index_for(process)];
        };
        const auto topo_position = [&](const ProcessId process) {
            const auto member_index = member_index_for(process);
            const auto position = std::ranges::find(
                forwarding.topological_member_indices, member_index);
            if (position == forwarding.topological_member_indices.end()) {
                throw std::runtime_error {
                    "unequal-depth process is absent from topological order"
                };
            }
            return static_cast<std::size_t>(
                position - forwarding.topological_member_indices.begin());
        };
        const auto& join_member = member_for(join_id);
        const auto& middle_member = member_for(middle_id);
        const auto& b_root_member = member_for(b_root_id);
        const auto& a_root_member = member_for(a_root_id);
        require(join_member.depth == 2U
                && middle_member.depth == 1U
                && b_root_member.depth == 0U
                && a_root_member.depth == 0U
                && topo_position(b_root_id) < topo_position(middle_id)
                && topo_position(a_root_id) < topo_position(middle_id)
                && topo_position(middle_id) < topo_position(join_id)
                && join_member.dependency_count == 2U
                && join_member.read_count == 2U,
            "the join is depth two after a middle member and an independent root");
        const auto join_dependencies
            = std::span<const RegionConeForwardingDependency> {
                  forwarding.dependencies }
                  .subspan(join_member.dependency_begin,
                      join_member.dependency_count);
        require(std::ranges::any_of(join_dependencies,
                    [&](const auto& dependency) {
                        return dependency.signal == a_value
                            && dependency.writer_member_index
                                == member_index_for(middle_id);
                    })
                && std::ranges::any_of(join_dependencies,
                    [&](const auto& dependency) {
                        return dependency.signal == b_value
                            && dependency.writer_member_index
                                == member_index_for(b_root_id);
                    }),
            "the join names one middle parent and a distinct direct-root parent");
    }

    const auto capture_boundary_revision = [&] {
        if (outcome.boundary_revision_captured) {
            return;
        }
        outcome.boundary_revision
            = implementation.signal_value_revisions.at(input);
        outcome.boundary_revision_captured = true;
    };
    if (forwarding_enabled) {
        forwarding_probe.after_forwarding_execution
            = capture_boundary_revision;
    } else {
        executor_probe.after_root_sample = capture_boundary_revision;
    }
    const auto record_join_cut = [&] {
        const auto sample_index = join_samples.size() - 1U;
        if (sample_index >= outcome.join_boundary_revisions.size()) {
            return;
        }
        const auto* const authoritative_state
            = component
                    < implementation.region_authoritative_state_by_component.size()
            ? implementation.region_authoritative_state_by_component[component]
                  .get()
            : nullptr;
        outcome.join_a4_roles_bound[sample_index]
            = authoritative_state != nullptr
            && authoritative_state->values().packed_slots_bound();
        outcome.join_boundary_revisions[sample_index]
            = implementation.signal_value_revisions.at(input);
        outcome.join_boundary_revision_unchanged[sample_index]
            = outcome.boundary_revision_captured
            && outcome.join_boundary_revisions[sample_index]
                == outcome.boundary_revision;
        const bool require_authoritative_roles = width > 64U
            && (!observe_at_first_join || join_samples.size() == 1U);
        const std::array<ForwardingJoinSignalState, 4U> sampled_state {
            forwarding_join_signal_state(
                implementation, a_stage, require_authoritative_roles),
            forwarding_join_signal_state(
                implementation, a_value, require_authoritative_roles),
            forwarding_join_signal_state(
                implementation, b_value, require_authoritative_roles),
            forwarding_join_signal_state(implementation, output) };
        if (sample_index == 0U) {
            outcome.partial_join_state = sampled_state;
            const auto* const local = component
                    < implementation.region_local_wave_state_by_component.size()
                ? implementation.region_local_wave_state_by_component[component].get()
                : nullptr;
            outcome.partial_join_declined_before_checked_resume
                = forwarding_enabled
                && implementation.systemverilog_wave_profile_region_forwarding_declines
                    > declines_before
                && local && local->forwarding_results
                && !local->forwarding_results->active;
        } else if (sample_index == 1U) {
            outcome.settled_join_state = sampled_state;
        }
    };
    forwarding_probe.on_forwarding_execution
        = [&](const RegionKernelSchedulerPrefix& prefix) {
              const auto join_seed = std::ranges::find_if(
                  prefix.tasks, [](const auto& task) {
                      return task.member.process == join_id;
                  });
              if (join_seed == prefix.tasks.end()) {
                  return;
              }
              outcome.forwarded_join_prefixes.push_back(prefix);
              const std::pair<PackedLogic4, PackedLogic4> inputs {
                  implementation.logical_signal_value(a_value),
                  implementation.logical_signal_value(b_value) };
              outcome.forwarded_join_inputs.push_back(inputs);
              const auto low_bit = [](const PackedLogic4& value) {
                  return static_cast<std::uint8_t>(
                      value.unchecked_low_word().aval & 1U);
              };
              join_samples.emplace_back(
                  low_bit(inputs.first), low_bit(inputs.second));
              full_join_samples.push_back(inputs);
              record_join_cut();
          };
    forwarding_probe.on_activation_decline
        = [&](const std::span<const RegionConeKernelInput> inputs,
              const RegionKernelActivationImage& image) {
              if (!forwarding_enabled
                  && !outcome.boundary_revision_captured
                  && std::ranges::find(image.ready_processes, b_root_id)
                      != image.ready_processes.end()
                  && std::ranges::find(image.ready_processes, a_root_id)
                      != image.ready_processes.end()) {
                  const auto boundary_binding = std::ranges::find(inputs,
                      input, &RegionConeKernelInput::signal);
                  if (boundary_binding == inputs.end()) {
                      throw std::runtime_error {
                          "the checked root image lacks its boundary input"
                      };
                  }
                  const auto boundary_value = std::ranges::find(
                      image.register_inputs,
                      boundary_binding->value_register,
                      &RegionKernelRegisterInput::register_id);
                  if (boundary_value == image.register_inputs.end()
                      || boundary_value->value != changed_input) {
                      throw std::runtime_error {
                          "the checked root image must read the new boundary value"
                      };
                  }
                  capture_boundary_revision();
              }
              if (std::ranges::find(image.ready_processes, join_id)
                      == image.ready_processes.end()) {
                  return;
              }
              ++outcome.activation_join_attempts;
              const auto join_task = std::ranges::find_if(
                  image.scheduler_prefix.tasks,
                  [&](const auto& task) {
                      return task.member.process == join_id;
                  });
              const auto request = std::ranges::find(image.requests,
                  join_id, &RegionKernelReadyMember::process);
              const bool key_is_original
                  = join_task != image.scheduler_prefix.tasks.end()
                  && request != image.requests.end()
                  && join_task->member == *request;
              const auto read_full_input = [&](const SignalId signal) {
                  const auto binding = std::ranges::find(inputs,
                      signal, &RegionConeKernelInput::signal);
                  if (binding == inputs.end()) {
                      throw std::runtime_error {
                          "the checked join image lacks an internal input binding"
                      };
                  }
                  const auto value = std::ranges::find(image.register_inputs,
                      binding->value_register,
                      &RegionKernelRegisterInput::register_id);
                  if (value == image.register_inputs.end()
                      || value->value.width() != width
                      || value->value.is_logic9()) {
                      throw std::runtime_error {
                          "the checked join image lacks an exact full-width input"
                      };
                  }
                  return value->value;
              };
              const auto full_sample = std::pair {
                  read_full_input(a_value), read_full_input(b_value) };
              const auto low_bit = [](const PackedLogic4& value) {
                  return static_cast<std::uint8_t>(
                      value.unchecked_low_word().aval & 1U);
              };
              const auto sample = std::pair {
                  low_bit(full_sample.first), low_bit(full_sample.second) };
              outcome.activation_join_prefixes.push_back(
                  image.scheduler_prefix);
              if (request != image.requests.end()) {
                  outcome.activation_join_requests.push_back(*request);
              }
              outcome.activation_join_inputs.push_back(sample);
              activation_full_join_inputs.push_back(full_sample);
              if (outcome.activation_cut_captured) {
                  return;
              }
              outcome.activation_cut_has_original_join_key = key_is_original;
              join_samples.push_back(sample);
              // This is the checked partial cut. The checked route samples
              // the settled callback in its executor; the forwarding route
              // samples its later nonroot seed in the provider below.
              full_join_samples.push_back(full_sample);
              outcome.activation_cut_captured = true;
              record_join_cut();
              if (observe_at_first_join && !first_join_stop_requested) {
                  first_join_stop_requested = true;
                  interpreter.scheduler().request_stop();
              }
          };
    executor_probe.after_secondary_sample = [&](const ProcessId process) {
        require(process == join_id,
            "the sampled two-input operation belongs to the join callback");
        require(!join_samples.empty(),
            "the executor records a join sample before its callback");
        full_join_samples.emplace_back(
            implementation.logical_signal_value(a_value),
            implementation.logical_signal_value(b_value));
        record_join_cut();
    };

    auto run_result = interpreter.run();
    if (observe_at_first_join && first_join_stop_requested) {
        outcome.stopped_at_first_join_callback
            = run_result.status == RunStatus::stopped;
        if (outcome.stopped_at_first_join_callback) {
            outcome.stopped_state = {
                forwarding_join_signal_state(implementation, a_stage, width > 64U),
                forwarding_join_signal_state(implementation, a_value, width > 64U),
                forwarding_join_signal_state(implementation, b_value, width > 64U),
                forwarding_join_signal_state(implementation, output) };
            const auto pending_update = [&](const ProcessId owner,
                                            const SignalId signal) {
                return std::ranges::any_of(
                    implementation.systemverilog_update_slots,
                    [&](const auto& slot) {
                        return slot.occupied && slot.process == owner
                            && slot.signal == signal;
                    });
            };
            outcome.middle_output_pending_at_stop
                = pending_update(middle_id, a_value);
            outcome.join_output_pending_at_stop
                = pending_update(join_id, output);

            // The checked join and middle members finish inside this wave
            // callback. Stop is honored before their original-key output
            // publications, which remain queued for the resume below.
            interpreter.prepare_signal_observation(a_value);
            const auto* const observed_state
                = component
                    < implementation.region_authoritative_state_by_component.size()
                ? implementation.region_authoritative_state_by_component[component]
                      .get()
                : nullptr;
            outcome.late_observation_demoted_roles
                = observed_state == nullptr
                || (!observed_state->values().packed_signal_slots_bound(a_value)
                    && !observed_state->values().packed_owner_slot_bound(
                        a_value, middle_id));
            interpreter.scheduler().clear_stop();
            run_result = interpreter.run();
        }
    }
    outcome.completed = run_result.status == RunStatus::completed;
    outcome.member_seed_prefix_was_exact
        = forwarding_probe.origin_was_exact_member_seed_prefix;
    outcome.forwarding_calls
        = forwarding_probe.forwarding_calls - calls_before;
    outcome.forwarding_evaluations
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations
            - evaluations_before;
    outcome.forwarding_declines
        = implementation.systemverilog_wave_profile_region_forwarding_declines
            - declines_before;
    outcome.forwarding_member_consumptions
        = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
            - members_before;
    outcome.forwarding_seed_prefixes
        = std::move(forwarding_probe.member_seed_prefixes);
    if (width > 64U && !observe_at_first_join) {
        // Retain the wide private-state binding assertion before the public
        // observation materializes and demotes these A4 role slots.
        static_cast<void>(forwarding_join_signal_state(
            implementation, a_stage, true));
        static_cast<void>(forwarding_join_signal_state(
            implementation, a_value, true));
        static_cast<void>(forwarding_join_signal_state(
            implementation, b_value, true));
    }
    // Keep the mid-run hidden-state cut above private; this completed outcome
    // must materialize any later accepted rows before its final comparison.
    static_cast<void>(interpreter.signal_value_snapshot(a_stage));
    outcome.final_state = {
        forwarding_join_signal_state(implementation, a_stage),
        forwarding_join_signal_state(implementation, a_value),
        forwarding_join_signal_state(implementation, b_value),
        forwarding_join_signal_state(implementation, output) };
    outcome.join_samples = std::move(join_samples);
    outcome.activation_full_join_inputs
        = std::move(activation_full_join_inputs);
    outcome.full_join_samples = std::move(full_join_samples);
    require(!forwarding_probe.activation_capture_failed,
        "the checked activation image has complete join input and key metadata");
    return outcome;
}

void check_region_forwarding_unequal_depth_join()
{
    // Keep the historical narrow role-store route explicit: narrow_only
    // slots do not need versioned copy-on-write unbinding between callbacks.
    ScopedEnvironment unversioned_narrow_slots {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", "0" };
    ScopedEnvironment unversioned_disjoint_slots {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", "0" };
    const auto checked = run_forwarding_unequal_depth_join_case(false);
    const auto native = run_forwarding_unequal_depth_join_case(true);
    const std::vector<std::pair<std::uint8_t, std::uint8_t>> expected_samples {
        { 0U, 0U }, { 1U, 0U } };
    const auto check_checked_activation_cuts = [&](const auto& result) {
        if (result.activation_join_attempts != 2U
            || result.activation_join_prefixes.size() != 2U
            || result.activation_join_requests.size() != 2U
            || result.activation_join_inputs != expected_samples) {
            return false;
        }
        const auto& partial = result.activation_join_prefixes[0U];
        const auto& settled = result.activation_join_prefixes[1U];
        const auto& first_request = result.activation_join_requests[0U];
        const auto& second_request = result.activation_join_requests[1U];
        return partial.tasks.size() == 2U
            && settled.tasks.size() == 1U
            && std::ranges::any_of(partial.tasks,
                [&](const auto& task) {
                    return task.member.process == 0U;
                })
            && std::ranges::any_of(partial.tasks,
                [&](const auto& task) {
                    return task.member.process == 2U;
                })
            && settled.tasks.front().member == second_request
            && first_request.origin.time == second_request.origin.time
            && first_request.origin.delta == second_request.origin.delta
            && first_request.origin.sequence
                < second_request.origin.sequence
            && partial.frontier_generation
                < settled.frontier_generation;
    };
    const auto check_native_partial_activation = [&](const auto& result) {
        if (result.activation_join_attempts != 1U
            || result.activation_join_prefixes.size() != 1U
            || result.activation_join_requests.size() != 1U
            || result.activation_join_inputs
                != std::vector<std::pair<std::uint8_t, std::uint8_t>> {
                    expected_samples.front() }) {
            return false;
        }
        const auto& partial = result.activation_join_prefixes.front();
        const auto& request = result.activation_join_requests.front();
        const auto join_task = std::ranges::find_if(
            partial.tasks, [](const auto& task) {
                return task.member.process == 0U;
            });
        return partial.tasks.size() == 2U
            && join_task != partial.tasks.end()
            && join_task->member == request
            && std::ranges::any_of(partial.tasks,
                [](const auto& task) {
                    return task.member.process == 2U;
                });
    };
    require(checked.completed && native.completed
            && checked.final_state == native.final_state,
        "the checked and forwarding unequal-depth cuts finish with identical roles and stamps");
    require(checked.join_samples == expected_samples
            && native.join_samples == expected_samples
            && checked.activation_cut_captured
            && native.activation_cut_captured
            && checked.activation_cut_has_original_join_key
            && native.activation_cut_has_original_join_key
            && check_checked_activation_cuts(checked)
            && check_native_partial_activation(native),
        "the partial join uses its checked original key and the later native seed sees the settled cut");
    require(native.partial_join_declined_before_checked_resume
            && native.forwarding_calls == 2U
            && native.forwarding_evaluations == 2U
            && native.forwarding_declines >= 1U
            && native.forwarding_member_consumptions == 3U
            && native.member_seed_prefix_was_exact
            && native.forwarding_seed_prefixes.size() == 2U
            && native.forwarded_join_prefixes.size() == 1U
            && native.forwarded_join_inputs
                == std::vector<std::pair<PackedLogic4, PackedLogic4>> {
                    { PackedLogic4(1U, Logic4::one),
                        PackedLogic4(1U, Logic4::zero) } }
            && native.activation_full_join_inputs
                == std::vector<std::pair<PackedLogic4, PackedLogic4>> {
                    { PackedLogic4(1U, Logic4::zero),
                        PackedLogic4(1U, Logic4::zero) } },
        "the first bank declines at the unsettled join, then a fresh nonroot seed consumes the settled join");
    auto initial_seed_processes = native.forwarding_seed_prefixes.front();
    std::ranges::sort(initial_seed_processes);
    const auto& partial_join_origin
        = native.activation_join_requests.front().origin;
    const auto& forwarded_join_origin
        = native.forwarded_join_prefixes.front().tasks.front().member.origin;
    require(initial_seed_processes == std::vector<ProcessId> { 1U, 3U }
            && native.forwarding_seed_prefixes.back()
                == std::vector<ProcessId> { 0U }
            && native.forwarded_join_prefixes.front().tasks.size() == 1U
            && native.forwarded_join_prefixes.front().tasks.front()
                   .member.process == 0U
            && forwarded_join_origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && forwarded_join_origin.phase == SchedulerPhase::active
            && forwarded_join_origin.time == partial_join_origin.time
            && forwarded_join_origin.delta == partial_join_origin.delta
            && forwarded_join_origin.stable_order
                == partial_join_origin.stable_order
            && forwarded_join_origin.sequence > partial_join_origin.sequence,
        "provider origins retain the initial roots and later join at its original ordered key");
    require(native.join_boundary_revision_unchanged[0U]
            && native.join_boundary_revision_unchanged[1U]
            && checked.join_boundary_revision_unchanged[0U]
            && checked.join_boundary_revision_unchanged[1U]
            && native.join_boundary_revisions[0U]
                == native.join_boundary_revisions[1U]
            && checked.join_boundary_revisions[0U]
                == checked.join_boundary_revisions[1U],
        "both join callbacks retain the captured boundary-input revision while internal parents settle");
    require(checked.partial_join_state == native.partial_join_state
            && checked.settled_join_state == native.settled_join_state,
        "both routes publish the same authoritative roles and stamps at each join callback");
    const auto& intermediate = native.partial_join_state;
    require(intermediate[0U].current == PackedLogic4(1U, Logic4::one)
            && intermediate[1U].current == PackedLogic4(1U, Logic4::zero)
            && intermediate[2U].current == PackedLogic4(1U, Logic4::zero)
            && intermediate[3U].current == PackedLogic4(1U, Logic4::one)
            && intermediate[0U].last
                == native.initial_state[0U].current
            && intermediate[1U] == native.initial_state[1U]
            && intermediate[2U].last
                == native.initial_state[2U].current
            && intermediate[3U] == native.initial_state[3U],
        "the first join callback sees A-stage new, A old, B new, and the previous output");
    require(native.settled_join_state[1U].current
                == PackedLogic4(1U, Logic4::one)
            && native.settled_join_state[2U].current
                == PackedLogic4(1U, Logic4::zero)
            && native.settled_join_state[3U].current
                == PackedLogic4(1U, Logic4::zero)
            && native.settled_join_state[1U].last
                == native.partial_join_state[1U].current
            && native.final_state[3U].current
                == PackedLogic4(1U, Logic4::one)
            && native.final_state[3U].last
                == PackedLogic4(1U, Logic4::zero)
            && std::ranges::all_of(native.final_state,
                [](const ForwardingJoinSignalState& state) {
                    return state.stored == state.current
                        && state.raw_owner == state.current;
                }),
        "the later join preserves the output glitch and all four roles remain coherent");
}

void check_region_forwarding_versioned_narrow_join_demotes_after_checked_cut()
{
    // An unset policy is the default: certified narrow A4 roles use the same
    // versioned storage as wide roles when the active component qualifies.
    ScopedEnvironment default_narrow_slots {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
    ScopedEnvironment default_disjoint_slots {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", nullptr };
    const auto checked = run_forwarding_unequal_depth_join_case(false);
    const auto native = run_forwarding_unequal_depth_join_case(true);
    const std::vector<std::pair<std::uint8_t, std::uint8_t>> expected_samples {
        { 0U, 0U }, { 1U, 0U } };
    const std::vector<std::pair<PackedLogic4, PackedLogic4>> expected_inputs {
        { PackedLogic4(1U, Logic4::zero), PackedLogic4(1U, Logic4::zero) },
        { PackedLogic4(1U, Logic4::one), PackedLogic4(1U, Logic4::zero) } };
    const auto has_exact_checked_join_cuts = [&](const auto& result) {
        if (result.activation_join_attempts != 2U
            || result.activation_join_prefixes.size() != 2U
            || result.activation_join_requests.size() != 2U
            || result.activation_join_inputs != expected_samples
            || result.activation_full_join_inputs != expected_inputs) {
            return false;
        }
        const auto& partial = result.activation_join_prefixes[0U];
        const auto& settled = result.activation_join_prefixes[1U];
        const auto& first_request = result.activation_join_requests[0U];
        const auto& second_request = result.activation_join_requests[1U];
        return partial.tasks.size() == 2U
            && settled.tasks.size() == 1U
            && settled.tasks.front().member == second_request
            && std::ranges::any_of(partial.tasks,
                [&](const auto& task) {
                    return task.member.process == 0U
                        && task.member == first_request;
                })
            && std::ranges::any_of(partial.tasks,
                [](const auto& task) {
                    return task.member.process == 2U;
                })
            && first_request.origin.time == second_request.origin.time
            && first_request.origin.delta == second_request.origin.delta
            && first_request.origin.sequence < second_request.origin.sequence
            && partial.frontier_generation < settled.frontier_generation;
    };
    require(checked.completed && native.completed
            && checked.final_state == native.final_state
            && checked.join_samples == expected_samples
            && native.join_samples == expected_samples
            && checked.partial_join_state == native.partial_join_state
            && checked.settled_join_state == native.settled_join_state,
        "default versioned narrow roles retain checked/native value and metadata parity");
    require(native.initial_a4_roles_bound
            && native.initial_a4_requires_prewrite_unbind
            && native.join_a4_roles_bound
                == std::array<bool, 2U> { true, false },
        "the default native A4 roles stay bound at the partial join then unbind for the checked middle commit");
    require(checked.activation_cut_captured
            && native.activation_cut_captured
            && checked.activation_cut_has_original_join_key
            && native.activation_cut_has_original_join_key
            && has_exact_checked_join_cuts(checked)
            && has_exact_checked_join_cuts(native),
        "both partial and settled checked join probes retain their exact original scheduler keys");
    require(native.partial_join_declined_before_checked_resume
            && native.forwarding_calls == 1U
            && native.forwarding_evaluations == 1U
            && native.forwarding_declines >= 1U
            && native.forwarding_member_consumptions == 2U
            && native.member_seed_prefix_was_exact
            && native.forwarding_seed_prefixes.size() == 1U
            && native.forwarded_join_prefixes.empty()
            && native.forwarded_join_inputs.empty(),
        "after the versioned partial commit unbinds A4, the settled join stays on checked execution");
    require(native.join_boundary_revision_unchanged[0U]
            && native.join_boundary_revision_unchanged[1U]
            && checked.join_boundary_revision_unchanged[0U]
            && checked.join_boundary_revision_unchanged[1U]
            && checked.final_state[3U].current == PackedLogic4(1U, Logic4::one)
            && checked.final_state[3U].last == PackedLogic4(1U, Logic4::zero)
            && std::ranges::all_of(native.final_state,
                [](const ForwardingJoinSignalState& state) {
                    return state.stored == state.current
                        && state.raw_owner == state.current;
                }),
        "checked fallback preserves the original boundary revision and final current/LAST/owner roles");
}

void check_region_forwarding_wide_logic4_join()
{
    for (const std::size_t width : { 65U, 129U }) {
        const auto checked
            = run_forwarding_unequal_depth_join_case(false, width);
        const auto native
            = run_forwarding_unequal_depth_join_case(true, width);
        PackedLogic4 high_word_value(width, Logic4::zero);
        high_word_value.set(width - 1U, Logic4::one);
        const PackedLogic4 zero(width, Logic4::zero);
        const std::vector<std::pair<PackedLogic4, PackedLogic4>> expected_inputs {
            { zero, high_word_value }, { high_word_value, high_word_value } };

        require(checked.completed && native.completed
                && checked.final_state == native.final_state,
            "wide forwarding joins match checked final values and metadata");
        require(native.forwarding_calls == 2U
                && native.forwarding_evaluations == 2U
                && native.forwarding_declines >= 1U
                && native.forwarding_member_consumptions == 3U
                && native.member_seed_prefix_was_exact
                && native.forwarding_seed_prefixes.size() == 2U
                && native.forwarded_join_prefixes.size() == 1U
                && native.forwarded_join_inputs
                    == std::vector<std::pair<PackedLogic4, PackedLogic4>> {
                        { high_word_value, high_word_value } }
                && native.activation_full_join_inputs
                    == std::vector<std::pair<PackedLogic4, PackedLogic4>> {
                        { zero, high_word_value } },
            "wide forwarding declines the partial join and reseeds it at its settled original key");
        auto initial_seed_processes = native.forwarding_seed_prefixes.front();
        std::ranges::sort(initial_seed_processes);
        const auto& partial_join_origin
            = native.activation_join_requests.front().origin;
        const auto& forwarded_join_origin
            = native.forwarded_join_prefixes.front().tasks.front().member.origin;
        require(initial_seed_processes == std::vector<ProcessId> { 1U, 3U }
                && native.forwarding_seed_prefixes.back()
                    == std::vector<ProcessId> { 0U }
                && native.forwarded_join_prefixes.front().tasks.size() == 1U
                && native.forwarded_join_prefixes.front().tasks.front()
                       .member.process == 0U
                && forwarded_join_origin.process_domain
                    == ProcessSchedulingDomain::systemverilog
                && forwarded_join_origin.phase == SchedulerPhase::active
                && forwarded_join_origin.time == partial_join_origin.time
                && forwarded_join_origin.delta == partial_join_origin.delta
                && forwarded_join_origin.stable_order
                    == partial_join_origin.stable_order
                && forwarded_join_origin.sequence > partial_join_origin.sequence,
            "wide provider origins keep both roots and the later original-key join seed");
        require(checked.full_join_samples == expected_inputs
                && native.full_join_samples == expected_inputs,
            "wide checked and forwarding paths capture the partial and settled full-width join values");
        require(checked.activation_full_join_inputs == expected_inputs
                && native.activation_full_join_inputs
                    == std::vector<std::pair<PackedLogic4, PackedLogic4>> {
                        expected_inputs.front() },
            "checked activation records both join cuts while forwarding falls back only at the partial cut");
        require(native.activation_join_attempts == 1U
                && native.activation_join_prefixes.size() == 1U
                && native.activation_join_requests.size() == 1U,
            "wide forwarding captures one checked join cut before its fresh nonroot seed");
        require(checked.activation_join_attempts == 2U
                && native.activation_join_prefixes.size() == 1U
                && std::ranges::any_of(
                    native.activation_join_prefixes.front().tasks,
                    [](const auto& task) {
                        return task.member.process == 0U;
                    })
                && std::ranges::any_of(
                    native.activation_join_prefixes.front().tasks,
                    [](const auto& task) {
                        return task.member.process == 2U;
                    }),
            "wide activation fallback preserves the exact partial join and middle prefix");
        require(native.join_boundary_revision_unchanged[0U]
                && native.join_boundary_revision_unchanged[1U]
                && native.join_boundary_revisions[0U]
                    == native.join_boundary_revisions[1U],
            "wide join fallback retains the original boundary revision across both callback cuts");

        const auto& partial = native.partial_join_state;
        const auto& settled = native.settled_join_state;
        require(partial[0U].current == high_word_value
                && partial[0U].last == zero
                && partial[1U].current == zero
                && partial[1U] == native.initial_state[1U]
                && partial[2U].current == high_word_value
                && partial[2U].last == zero
                && partial[3U].current == zero
                && partial[3U] == native.initial_state[3U],
            "wide partial cut publishes high-word root roles before the first join output update");
        require(settled[0U].current == high_word_value
                && settled[1U].current == high_word_value
                && settled[2U].current == high_word_value
                && settled[3U].current == high_word_value
                && settled[3U].last == zero,
            "wide settled cut retains the first join publication in LAST before the final update");
        require(std::ranges::all_of(native.final_state,
                    [](const ForwardingJoinSignalState& state) {
                        return state.stored == state.current
                            && state.raw_owner == state.current;
                    }),
            "wide forwarding fallback leaves complete stored and original-owner planes coherent");

        const auto checked_observed
            = run_forwarding_unequal_depth_join_case(false, width, true);
        const auto native_observed
            = run_forwarding_unequal_depth_join_case(true, width, true);
        const std::vector<std::pair<PackedLogic4, PackedLogic4>> settled_sample {
            { high_word_value, high_word_value } };
        require(checked_observed.completed && native_observed.completed
                && checked_observed.final_state == native_observed.final_state
                && checked_observed.partial_join_state
                    == native_observed.partial_join_state
                && checked_observed.stopped_at_first_join_callback
                && native_observed.stopped_at_first_join_callback
                && checked_observed.middle_output_pending_at_stop
                && native_observed.middle_output_pending_at_stop
                && checked_observed.join_output_pending_at_stop
                && native_observed.join_output_pending_at_stop
                && checked_observed.stopped_state
                    == native_observed.stopped_state
                && checked_observed.stopped_state
                    == checked_observed.partial_join_state
                && native_observed.stopped_state
                    == native_observed.partial_join_state
                && checked_observed.settled_join_state
                    == native_observed.settled_join_state,
            "stop retains the checked join/middle outputs at their original keys before late observation");
        require(checked_observed.late_observation_demoted_roles
                && native_observed.late_observation_demoted_roles
                && checked_observed.full_join_samples == expected_inputs
                && native_observed.full_join_samples == expected_inputs
                && checked_observed.activation_full_join_inputs.size() >= 1U
                && native_observed.activation_full_join_inputs.size() >= 1U
                && checked_observed.activation_full_join_inputs.front()
                    == expected_inputs.front()
                && native_observed.activation_full_join_inputs.front()
                    == expected_inputs.front()
                && checked_observed.full_join_samples.back()
                    == settled_sample.front()
                && native_observed.full_join_samples.back()
                    == settled_sample.front(),
            "observation retains the first activation cut and later checked settled sample");
        require(native_observed.forwarding_calls == 1U
                && native_observed.forwarding_evaluations == 1U
                && native_observed.forwarding_declines >= 1U
                && native_observed.forwarding_member_consumptions == 2U,
            "late observation follows native wide root evaluation and leaves the pending member on checked publication");
        require(std::ranges::all_of(native_observed.final_state,
                    [](const ForwardingJoinSignalState& state) {
                        return state.stored == state.current
                            && state.raw_owner == state.current;
                    })
                && native_observed.final_state[0U].current == high_word_value
                && native_observed.final_state[1U].current == high_word_value
                && native_observed.final_state[2U].current == high_word_value
                && native_observed.final_state[3U].current == zero
                && native_observed.final_state[3U].last == high_word_value,
            "demoted wide roles retain high-word current, LAST, stored, and raw values through pending checked publication");
    }
}

struct ForwardingBoundaryQueuedRootKey {
    RegionFrontierKeyV1 key;
    std::size_t observations { };
    bool captured { };
    bool replaced { };
    bool consumed { };
    bool trace_hook_absent { };
};

struct ForwardingBoundaryCheckedRootInputSample {
    ProcessId process { };
    PackedLogic4 input;
    std::uint64_t input_revision { };
    SimulationTick time { };
    std::uint64_t delta { };
    std::optional<SchedulerPhase> phase;
    std::uint64_t systemverilog_round { };
};

struct ForwardingBoundaryCut {
    SignalId input { };
    SignalId root_output { };
    SimulationTick time { };
    PackedLogic4 input_before;
    PackedLogic4 input_after;
    PackedLogic4 first_before;
    PackedLogic4 first_at_start;
    std::uint64_t input_revision_before_deposit { };
    std::uint64_t input_revision_after_deposit { };
    std::uint64_t cached_boundary_revision_before_deposit { };
    bool cached_boundary_revision_captured { };
    bool a4_slots_bound_before_deposit { };
    bool a4_slots_bound_after_deposit { };
    ForwardingBoundaryQueuedRootKey changed_root_receipt;
    Scheduler::SafePointHookToken receipt_hook_token { };
    bool queued { };
    bool ran { };
    bool root_output_pending { };
    bool root_output_public_pending { };
    bool root_output_private_pending { };
    bool root_sampled_new_input { };
};

struct ForwardingBoundaryOutcome {
    PackedLogic4 input;
    PackedLogic4 first_internal;
    PackedLogic4 second_internal;
    PackedLogic4 output;
    std::array<std::size_t, 3U> fallback_resumes { };
    std::size_t forwarding_calls { };
    std::uint64_t forwarding_evaluations { };
    std::uint64_t forwarding_declines { };
    std::uint64_t private_parent_slots_elided { };
    std::uint64_t private_parent_dispatches { };
    std::uint64_t public_update_tokens { };
    SimulationTick final_time { };
    std::vector<ForwardingBoundaryRootInputSample>
        root_forwarding_input_samples;
    std::vector<ForwardingBoundaryCheckedRootInputSample>
        checked_root_input_samples;
    bool root_forwarding_sample_capture_failed { };
    ForwardingBoundaryCut cut;
};

ForwardingBoundaryOutcome run_forwarding_boundary_case(
    const bool forwarding_enabled,
    const bool unversioned_narrow_storage = false)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment a4_single_owner_storage {
        "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT",
        unversioned_narrow_storage ? "0" : nullptr };
    ScopedEnvironment a4_disjoint_owner_storage {
        "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT",
        unversioned_narrow_storage ? "0" : nullptr };
    ForwardingRuntimeProbe forwarding_probe;
    RegionKernelProbe executor_probe;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "forward_cut.input",
        PackedLogic4(1U, Logic4::zero) });
    const auto first_internal = interpreter.add_signal({ "forward_cut.first",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto second_internal = interpreter.add_signal({ "forward_cut.second",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "forward_cut.output",
        PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });

    Process child;
    child.id = 0U;
    child.name = "forward_cut_child";
    child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    child.initialize = true;
    child.register_count = 1U;
    child.static_sensitivity = { { first_internal, EdgeKind::any } };
    child.driver_regions = { { second_internal, 0U, 0U, true } };
    child.operations = {
        ReadSignal { 0U, first_internal }, UnaryNot { 0U, 0U },
        WriteUpdate { second_internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(child)) == 0U,
        "the stale-boundary fixture places its child before the root ID");

    Process root;
    root.id = 1U;
    root.name = "forward_cut_root";
    root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    root.initialize = true;
    root.register_count = 1U;
    root.static_sensitivity = { { input, EdgeKind::any } };
    root.driver_regions = { { first_internal, 0U, 0U, true } };
    root.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { first_internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(root)) == 1U,
        "the stale-boundary fixture keeps the original root owner ID");

    Process grandchild;
    grandchild.id = 2U;
    grandchild.name = "forward_cut_grandchild";
    grandchild.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    grandchild.initialize = true;
    grandchild.register_count = 1U;
    grandchild.static_sensitivity = { { second_internal, EdgeKind::any } };
    grandchild.driver_regions = { { output, 0U, 0U, true } };
    grandchild.operations = {
        ReadSignal { 0U, second_internal },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(grandchild)) == 2U,
        "the stale-boundary fixture keeps the final process ID stable");

    const auto add_executor = [&](const ProcessId process,
                                  const SignalId source,
                                  const SignalId destination,
                                  const std::size_t probe_slot,
                                  const bool invert) {
        const auto& registered = interpreter.process_program(process);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process,
            [] { return true; },
            [&executor_probe, process, source, destination, probe_slot,
                invert, binding, wait_instruction] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process, source, destination,
                    wait_instruction, binding, true, probe_slot,
                    true, true, 1U, std::nullopt, std::nullopt,
                    true, invert);
            }, std::move(contract));
    };
    add_executor(0U, first_internal, second_internal, 1U, true);
    add_executor(1U, input, first_internal, 0U, false);
    add_executor(2U, second_internal, output, 2U, false);
    interpreter.materialize_ready_process_executors();

    Process clock;
    clock.id = 3U;
    clock.name = "forward_cut_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = {
        WaitFor { 1U },
        LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
        WriteBlocking { input, 0U },
        Halt { },
    };
    require(interpreter.add_process(std::move(clock)) == 3U,
        "the clock supplies the initial real boundary change");
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<ForwardingRuntimeBackendProvider>(
            forwarding_probe, forwarding_enabled));

    interpreter.start();
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "the stale-boundary fixture reaches its initialized static waits");
    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    ForwardingBoundaryOutcome outcome;
    outcome.cut.input = input;
    outcome.cut.root_output = first_internal;
    outcome.cut.first_at_start = implementation.logical_signal_value(
        first_internal);
    std::vector<std::pair<ProcessId, PackedLogic4>> sampled_inputs;
    std::vector<ForwardingBoundaryRootInputSample>
        root_forwarding_input_samples;
    std::vector<ForwardingBoundaryCheckedRootInputSample>
        checked_root_input_samples;
    checked_root_input_samples.reserve(4U);
    forwarding_probe.signal_value_revisions
        = &implementation.signal_value_revisions;
    executor_probe.after_process_sample
        = [&](const ProcessId process, const PackedLogic4& value) {
              if (process == 1U) {
                  auto& active_scheduler = interpreter.scheduler();
                  checked_root_input_samples.push_back({ process, value,
                      implementation.signal_value_revisions[input],
                      active_scheduler.now(), active_scheduler.delta(),
                      active_scheduler.current_phase(),
                      active_scheduler.systemverilog_round() });
              }
          };
    forwarding_probe.root_forwarding_sample_process = 1U;
    forwarding_probe.root_forwarding_sample_signal = input;
    forwarding_probe.root_forwarding_input_samples
        = &root_forwarding_input_samples;
    executor_probe.sampled_input_values = &sampled_inputs;
    const auto startup_resumes = executor_probe.resumes;
    const auto evaluations_before
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations;
    const auto declines_before
        = implementation.systemverilog_wave_profile_region_forwarding_declines;
    const auto private_slots_before
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided;
    const auto private_dispatches_before
        = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
    const auto public_tokens_before
        = implementation.systemverilog_wave_profile_region_forwarding_public_update_tokens;
    const auto calls_before = forwarding_probe.forwarding_calls;

    const auto queue_boundary_change = [&] {
        if (outcome.cut.queued) {
            return;
        }
        outcome.cut.queued = true;
        interpreter.scheduler().schedule_systemverilog(
            SchedulerPhase::active, 0U,
            [&outcome, &interpreter](Scheduler& scheduler) {
                outcome.cut.time = scheduler.now();
                auto& active_implementation
                    = OwnedDriverDemotionTestAccess::implementation(
                        interpreter);
                outcome.cut.input_before
                    = active_implementation.logical_signal_value(
                        outcome.cut.input);
                outcome.cut.input_revision_before_deposit
                    = active_implementation.signal_value_revisions[
                        outcome.cut.input];
                const auto component
                    = outcome.cut.root_output
                            < active_implementation
                                  .region_authoritative_component_by_signal.size()
                        ? active_implementation
                              .region_authoritative_component_by_signal[
                                  outcome.cut.root_output]
                        : std::numeric_limits<std::size_t>::max();
                if (component
                    < active_implementation
                          .region_authoritative_state_by_component.size()) {
                    const auto* const state
                        = active_implementation
                              .region_authoritative_state_by_component[
                                  component]
                              .get();
                    outcome.cut.a4_slots_bound_before_deposit
                        = state != nullptr
                        && state->values().packed_slots_bound();
                    if (component
                        < active_implementation
                              .region_local_wave_state_by_component.size()) {
                        const auto& local_state
                            = active_implementation
                                  .region_local_wave_state_by_component[
                                      component];
                        if (local_state
                            && local_state->forwarding_results) {
                            const auto& bank
                                = *local_state->forwarding_results;
                            for (std::size_t index = 0U;
                                 index < bank.boundary_signals.size()
                                     && index
                                         < bank.boundary_revisions.size();
                                 ++index) {
                                if (bank.boundary_signals[index]
                                    == outcome.cut.input) {
                                    outcome.cut.cached_boundary_revision_before_deposit
                                        = bank.boundary_revisions[index];
                                    outcome.cut.cached_boundary_revision_captured
                                        = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                outcome.cut.first_before
                    = active_implementation.logical_signal_value(
                        outcome.cut.root_output);
                outcome.cut.root_output_public_pending = std::ranges::any_of(
                    active_implementation.systemverilog_update_slots,
                    [&](const auto& slot) {
                        return slot.occupied && slot.process == 1U
                            && slot.signal == outcome.cut.root_output;
                    });
                outcome.cut.root_output_private_pending = std::ranges::any_of(
                    active_implementation.region_local_wave_state_by_component,
                    [&](const auto& local_state) {
                        if (!local_state || !local_state->forwarding_results) {
                            return false;
                        }
                        return std::ranges::any_of(
                            local_state->forwarding_results->stage_batch_pool,
                            [&](const auto& stage_batch) {
                                return stage_batch
                                    && std::ranges::any_of(
                                        stage_batch->private_outputs,
                                        [&](const auto& private_output) {
                                            return private_output.pending
                                                && private_output.process == 1U
                                                && private_output.signal
                                                    == outcome.cut.root_output;
                                        });
                            });
                    });
                outcome.cut.root_output_pending
                    = outcome.cut.root_output_public_pending
                    || outcome.cut.root_output_private_pending;
                interpreter.deposit_signal(outcome.cut.input,
                    PackedLogic4(1U, Logic4::zero));
                outcome.cut.input_after
                    = active_implementation.logical_signal_value(
                        outcome.cut.input);
                outcome.cut.input_revision_after_deposit
                    = active_implementation.signal_value_revisions[
                        outcome.cut.input];
                if (component
                    < active_implementation
                          .region_authoritative_state_by_component.size()) {
                    const auto* const state
                        = active_implementation
                              .region_authoritative_state_by_component[
                                  component]
                              .get();
                    outcome.cut.a4_slots_bound_after_deposit
                        = state != nullptr
                        && state->values().packed_slots_bound();
                }
                outcome.cut.receipt_hook_token
                    = scheduler.add_safe_point_hook(
                        [&outcome, &interpreter, component] (
                            Scheduler& safe_scheduler,
                            const SchedulerPhase phase) {
                            constexpr ProcessId root_process { 1U };
                            auto& active_implementation
                                = OwnedDriverDemotionTestAccess::implementation(
                                    interpreter);
                            if (phase != SchedulerPhase::active
                                || root_process
                                    >= active_implementation.processes.size()
                                || root_process
                                    >= active_implementation
                                           .region_readiness_queued_by_process
                                           .size()
                                || root_process
                                    >= active_implementation
                                           .region_readiness_member_index_by_process
                                           .size()) {
                                return;
                            }
                            const auto& process
                                = active_implementation.processes[root_process];
                            const auto& queued
                                = active_implementation
                                      .region_readiness_queued_by_process[
                                          root_process];
                            const auto member
                                = active_implementation
                                      .region_readiness_member_index_by_process[
                                          root_process];
                            if (!process.queued || !queued.key_valid
                                || queued.component != component
                                || queued.member != member
                                || queued.generation
                                    != active_implementation
                                           .region_runtime_generation) {
                                return;
                            }
                            if (outcome.cut.input
                                    >= active_implementation
                                           .signal_value_revisions.size()
                                || active_implementation
                                           .signal_value_revisions[
                                               outcome.cut.input]
                                    != outcome.cut
                                           .input_revision_after_deposit) {
                                return;
                            }
                            const auto& key = queued.queued_key;
                            if (key.time != safe_scheduler.now()
                                || key.process_domain
                                    != static_cast<std::uint32_t>(
                                        ProcessSchedulingDomain::systemverilog)
                                || key.phase != static_cast<std::uint32_t>(
                                    SchedulerPhase::active)
                                || key.stable_order != root_process
                                || key.systemverilog_round == 0U) {
                                return;
                            }
                            auto& receipt
                                = outcome.cut.changed_root_receipt;
                            if (!receipt.captured) {
                                receipt.key = key;
                                receipt.captured = true;
                            } else if (receipt.key.time != key.time
                                || receipt.key.delta != key.delta
                                || receipt.key.systemverilog_round
                                    != key.systemverilog_round
                                || receipt.key.stable_order
                                    != key.stable_order
                                || receipt.key.sequence != key.sequence
                                || receipt.key.process_domain
                                    != key.process_domain
                                || receipt.key.phase != key.phase) {
                                receipt.replaced = true;
                            }
                            const bool trace_hook_absent
                                = !safe_scheduler.trace_hook_installed();
                            ++receipt.observations;
                            if (receipt.observations == 1U) {
                                receipt.trace_hook_absent = trace_hook_absent;
                            } else {
                                receipt.trace_hook_absent
                                    = receipt.trace_hook_absent
                                    && trace_hook_absent;
                            }
                        });
                outcome.cut.ran = true;
            });
    };
    executor_probe.after_root_sample = queue_boundary_change;
    forwarding_probe.after_forwarding_execution = queue_boundary_change;

    require(interpreter.run().status == RunStatus::completed,
        "the stale-boundary fixture drains the original and fallback callbacks");
    if (outcome.cut.receipt_hook_token != 0U) {
        interpreter.scheduler().remove_safe_point_hook(
            outcome.cut.receipt_hook_token);
    }
    if (1U < implementation.region_readiness_queued_by_process.size()
        && 1U < implementation.processes.size()) {
        const auto& queued
            = implementation.region_readiness_queued_by_process[1U];
        outcome.cut.changed_root_receipt.consumed
            = outcome.cut.changed_root_receipt.captured
            && !implementation.processes[1U].queued
            && !queued.key_valid;
    }
    outcome.final_time = interpreter.scheduler().now();
    outcome.input = implementation.logical_signal_value(input);
    outcome.first_internal
        = implementation.logical_signal_value(first_internal);
    outcome.second_internal
        = implementation.logical_signal_value(second_internal);
    outcome.output = implementation.logical_signal_value(output);
    for (std::size_t index = 0U;
         index < outcome.fallback_resumes.size(); ++index) {
        outcome.fallback_resumes[index]
            = executor_probe.resumes[index] - startup_resumes[index];
    }
    outcome.forwarding_calls
        = forwarding_probe.forwarding_calls - calls_before;
    outcome.forwarding_evaluations
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations
            - evaluations_before;
    outcome.forwarding_declines
        = implementation.systemverilog_wave_profile_region_forwarding_declines
        - declines_before;
    outcome.private_parent_slots_elided
        = implementation
              .systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
        - private_slots_before;
    outcome.private_parent_dispatches
        = implementation
              .systemverilog_wave_profile_region_forwarding_private_parent_dispatches
        - private_dispatches_before;
    outcome.public_update_tokens
        = implementation
              .systemverilog_wave_profile_region_forwarding_public_update_tokens
        - public_tokens_before;
    outcome.root_forwarding_input_samples
        = std::move(root_forwarding_input_samples);
    outcome.checked_root_input_samples
        = std::move(checked_root_input_samples);
    outcome.root_forwarding_sample_capture_failed
        = forwarding_probe.root_forwarding_sample_capture_failed;
    outcome.cut.root_sampled_new_input = std::ranges::any_of(
        sampled_inputs,
        [](const auto& sample) {
            return sample.first == 1U
                && sample.second == PackedLogic4(1U, Logic4::zero);
        });
    return outcome;
}

void check_region_forwarding_stale_boundary()
{
    const auto checked = run_forwarding_boundary_case(false);
    const auto versioned_forwarded = run_forwarding_boundary_case(true);
    // Both switches must be off to retain the pre-versioned narrow-only A4
    // path for the compatibility witness.
    const auto unversioned_checked
        = run_forwarding_boundary_case(false, true);
    const auto unversioned_forwarded
        = run_forwarding_boundary_case(true, true);
    const auto assert_boundary_cut = [](
        const ForwardingBoundaryOutcome& result) {
        require(result.cut.queued && result.cut.ran
                && result.cut.time > 0U
                && result.cut.time == result.final_time
                && result.cut.root_output_pending
                && result.cut.input_before
                    == PackedLogic4(1U, Logic4::one)
                && result.cut.input_after
                    == PackedLogic4(1U, Logic4::zero)
                && result.cut.first_before == result.cut.first_at_start
                && result.cut.input_revision_after_deposit
                    != result.cut.input_revision_before_deposit,
            "a same-time foreign Active task changes the boundary revision while the original root update is pending");
    };
    assert_boundary_cut(checked);
    assert_boundary_cut(versioned_forwarded);
    assert_boundary_cut(unversioned_checked);
    assert_boundary_cut(unversioned_forwarded);
    require(checked.cut.root_sampled_new_input
            && unversioned_checked.cut.root_sampled_new_input,
        "checked root execution samples the foreign boundary update");

    const auto initial_root_sample = [](const auto& result) {
        return std::ranges::find_if(
            result.root_forwarding_input_samples,
            [](const ForwardingBoundaryRootInputSample& sample) {
                return sample.input == PackedLogic4(1U, Logic4::one);
            });
    };
    const auto changed_root_sample = [](const auto& result) {
        return std::ranges::find_if(
            result.root_forwarding_input_samples,
            [](const ForwardingBoundaryRootInputSample& sample) {
                return sample.input == PackedLogic4(1U, Logic4::zero);
            });
    };
    const auto changed_checked_root_sample = [](const auto& result) {
        return std::ranges::find_if(
            result.checked_root_input_samples,
            [&](const ForwardingBoundaryCheckedRootInputSample& sample) {
                return sample.input == PackedLogic4(1U, Logic4::zero)
                    && sample.input_revision
                        == result.cut.input_revision_after_deposit;
            });
    };
    const auto later_scheduler_origin = [](
        const RegionKernelActivationOrigin& candidate,
        const RegionKernelActivationOrigin& previous) {
        if (candidate.time != previous.time) {
            return candidate.time > previous.time;
        }
        if (candidate.delta != previous.delta) {
            return candidate.delta > previous.delta;
        }
        if (candidate.systemverilog_round
            != previous.systemverilog_round) {
            return candidate.systemverilog_round
                > previous.systemverilog_round;
        }
        if (candidate.stable_order != previous.stable_order) {
            return candidate.stable_order > previous.stable_order;
        }
        return candidate.sequence > previous.sequence;
    };
    const auto queued_origin = [](
        const RegionFrontierKeyV1& key) {
        return RegionKernelActivationOrigin {
            static_cast<ProcessSchedulingDomain>(key.process_domain),
            static_cast<SchedulerPhase>(key.phase), key.time, key.delta,
            static_cast<StableOrder>(key.stable_order), key.sequence,
            key.systemverilog_round };
    };

    const auto versioned_initial = initial_root_sample(versioned_forwarded);
    const auto versioned_changed = changed_root_sample(versioned_forwarded);
    const auto versioned_checked = changed_checked_root_sample(
        versioned_forwarded);
    const auto& versioned_receipt
        = versioned_forwarded.cut.changed_root_receipt;
    require(!versioned_forwarded.root_forwarding_sample_capture_failed
            && versioned_initial
                != versioned_forwarded.root_forwarding_input_samples.end()
            && versioned_changed
                == versioned_forwarded.root_forwarding_input_samples.end()
            && versioned_checked
                != versioned_forwarded.checked_root_input_samples.end()
            && std::ranges::count_if(
                versioned_forwarded.checked_root_input_samples,
                [&](const ForwardingBoundaryCheckedRootInputSample& sample) {
                    return sample.input == PackedLogic4(1U, Logic4::zero)
                        && sample.input_revision
                            == versioned_forwarded.cut.input_revision_after_deposit;
                }) == 1U
            && versioned_forwarded.cut.cached_boundary_revision_captured
            && versioned_forwarded.cut.input_revision_before_deposit
                == versioned_forwarded.cut.cached_boundary_revision_before_deposit
            && versioned_forwarded.cut.a4_slots_bound_before_deposit
            && versioned_forwarded.cut.a4_slots_bound_after_deposit
            && versioned_receipt.captured
            && versioned_receipt.observations > 0U
            && !versioned_receipt.replaced
            && versioned_receipt.consumed
            && versioned_receipt.trace_hook_absent
            && versioned_receipt.key.time
                == versioned_forwarded.cut.time
            && versioned_receipt.key.process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && versioned_receipt.key.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && versioned_receipt.key.stable_order == 1U
            && versioned_receipt.key.systemverilog_round != 0U
            && versioned_receipt.key.sequence
                != versioned_initial->origin.sequence
            && later_scheduler_origin(
                queued_origin(versioned_receipt.key),
                versioned_initial->origin)
            && versioned_checked->process == 1U
            && versioned_checked->time == versioned_receipt.key.time
            && versioned_checked->delta == versioned_receipt.key.delta
            && versioned_checked->phase == SchedulerPhase::active
            && versioned_checked->systemverilog_round
                == versioned_receipt.key.systemverilog_round
            && versioned_initial->input_revision
                == versioned_forwarded.cut.input_revision_before_deposit
            && versioned_checked->input_revision
                == versioned_forwarded.cut.input_revision_after_deposit,
        "a stale boundary revision keeps versioned A4 roles bound and sends the changed input through one checked root activation on its authentic queued key");

    const auto unversioned_initial
        = initial_root_sample(unversioned_forwarded);
    const auto unversioned_changed
        = changed_root_sample(unversioned_forwarded);
    require(!unversioned_forwarded.root_forwarding_sample_capture_failed
            && unversioned_initial
                != unversioned_forwarded.root_forwarding_input_samples.end()
            && unversioned_changed
                != unversioned_forwarded.root_forwarding_input_samples.end()
            && unversioned_forwarded.cut.cached_boundary_revision_captured
            && unversioned_forwarded.cut.input_revision_before_deposit
                == unversioned_forwarded.cut.cached_boundary_revision_before_deposit
            && unversioned_forwarded.cut.a4_slots_bound_before_deposit
            && unversioned_forwarded.cut.a4_slots_bound_after_deposit
            && unversioned_initial->origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && unversioned_changed->origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && unversioned_initial->origin.phase == SchedulerPhase::active
            && unversioned_changed->origin.phase == SchedulerPhase::active
            && unversioned_initial->origin.time
                == unversioned_forwarded.cut.time
            && unversioned_changed->origin.time
                == unversioned_forwarded.cut.time
            && unversioned_initial->origin.stable_order == 1U
            && unversioned_changed->origin.stable_order == 1U
            && unversioned_changed->origin.sequence
                != unversioned_initial->origin.sequence
            && later_scheduler_origin(unversioned_changed->origin,
                unversioned_initial->origin)
            && unversioned_initial->input_revision
                == unversioned_forwarded.cut.input_revision_before_deposit
            && unversioned_changed->input_revision
                == unversioned_forwarded.cut.input_revision_after_deposit,
        "the explicit unversioned narrow policy re-forwards the changed boundary at a new original key");

    const auto assert_pending_route = [](
        const ForwardingBoundaryOutcome& ordinary,
        const ForwardingBoundaryOutcome& forwarded) {
        require(ordinary.cut.root_output_public_pending
                && !ordinary.cut.root_output_private_pending
                && forwarded.cut.root_output_private_pending
                && !forwarded.cut.root_output_public_pending
                && forwarded.private_parent_slots_elided > 0U
                && forwarded.private_parent_dispatches > 0U,
            "the same stale-boundary cut sees a checked root token and its forwarded private-stage replacement");
        require(forwarded.forwarding_calls >= 1U
                && forwarded.forwarding_evaluations >= 1U
                && forwarded.forwarding_declines >= 1U
                && forwarded.fallback_resumes[1U] != 0U,
            "forwarding rejects stale member state and resumes the affected child through its checked executor");
        require(forwarded.input == ordinary.input
                && forwarded.first_internal == ordinary.first_internal
                && forwarded.second_internal == ordinary.second_internal
                && forwarded.output == ordinary.output,
            "stale-boundary fallback publishes the same final values as ordinary execution");
    };
    assert_pending_route(checked, versioned_forwarded);
    assert_pending_route(unversioned_checked, unversioned_forwarded);
}

void check_region_forwarding_ranged_dependency(
    const bool cohort_grouping_enabled,
    const std::size_t width = 2U)
{
    ScopedEnvironment kernel_enabled {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_enabled {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
    ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
    ForwardingRuntimeProbe forwarding_probe;
    RegionKernelProbe executor_probe;
    Interpreter interpreter;
    const auto input = interpreter.add_signal({ "forward_range.input",
        PackedLogic4(width, Logic4::zero) });
    const auto internal = interpreter.add_signal({ "forward_range.internal",
        PackedLogic4(width, Logic4::zero), ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "forward_range.output",
        PackedLogic4(width, Logic4::zero), ResolutionKind::sv_wire });
    const std::uint32_t sensitivity_offset = width > 2U ? 63U : 0U;
    const std::uint32_t sensitivity_width = width > 2U ? 2U : 1U;

    Process child;
    child.id = 0U;
    child.name = "forward_range_child";
    child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    child.initialize = true;
    child.register_count = 1U;
    child.static_sensitivity = {
        { internal, EdgeKind::any, sensitivity_offset, sensitivity_width },
    };
    child.driver_regions = { { output, 0U, 0U, true } };
    child.operations = {
        ReadSignal { 0U, internal },
        WriteUpdate { output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(child)) == 0U,
        "ranged forwarding child precedes its parent ProcessId");

    Process root;
    root.id = 1U;
    root.name = "forward_range_root";
    root.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    root.initialize = true;
    root.register_count = 1U;
    root.static_sensitivity = { { input, EdgeKind::any } };
    root.driver_regions = { { internal, 0U, 0U, true } };
    root.operations = {
        ReadSignal { 0U, input },
        WriteUpdate { internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U },
    };
    require(interpreter.add_process(std::move(root)) == 1U,
        "ranged forwarding parent has stable ownership");

    const auto add_executor = [&](const ProcessId process,
                                  const SignalId source,
                                  const SignalId destination,
                                  const std::size_t probe_slot) {
        const auto& registered = interpreter.process_program(process);
        const ProcessExecutorProgramBinding binding {
            registered, registered, process };
        const auto wait_instruction = static_cast<InstructionIndex>(
            registered.operations.size() - 2U);
        DeferredProcessExecutorContract contract;
        contract.expected_access = binding;
        contract.callbacks_observation_safe = true;
        contract.expected_region_kernel_equivalent = true;
        interpreter.set_deferred_process_executor(process,
            [] { return true; },
            [&executor_probe, process, source, destination, probe_slot,
                binding, wait_instruction, width] {
                return std::make_unique<RegionKernelExecutor>(
                    executor_probe, process, source, destination,
                    wait_instruction, binding, true, probe_slot, true,
                    true, 1U, std::nullopt, std::nullopt, true, false,
                    false, nullptr, width);
            }, std::move(contract));
    };
    add_executor(0U, internal, output, 0U);
    add_executor(1U, input, internal, 1U);
    interpreter.materialize_ready_process_executors();

    PackedLogic4 high_bit_value(width, Logic4::zero);
    const std::size_t unrelated_bit
        = width == 65U ? 62U : (width > 65U ? width - 1U : 1U);
    high_bit_value.set(unrelated_bit, Logic4::one);
    PackedLogic4 first_watched_transition = high_bit_value;
    first_watched_transition.set(sensitivity_offset, Logic4::one);
    PackedLogic4 second_watched_transition = first_watched_transition;
    if (sensitivity_width > 1U) {
        second_watched_transition.set(sensitivity_offset + 1U, Logic4::one);
    }
    Process clock;
    clock.id = 2U;
    clock.name = "forward_range_clock";
    clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    clock.register_count = 1U;
    clock.driver_regions = { { input, 0U, 0U, true } };
    clock.operations = { WaitFor { 1U },
        LoadConstant { 0U, high_bit_value }, WriteBlocking { input, 0U },
        WaitFor { 1U },
        LoadConstant { 0U, first_watched_transition },
        WriteBlocking { input, 0U } };
    if (sensitivity_width > 1U) {
        clock.operations.emplace_back(WaitFor { 1U });
        clock.operations.emplace_back(
            LoadConstant { 0U, second_watched_transition });
        clock.operations.emplace_back(WriteBlocking { input, 0U });
    }
    clock.operations.emplace_back(Halt { });
    require(interpreter.add_process(std::move(clock)) == 2U,
        "ranged forwarding clock stays outside the cone");
    interpreter.set_region_kernel_backend_provider(
        std::make_shared<ForwardingRuntimeBackendProvider>(forwarding_probe));

    interpreter.start();
    require(interpreter.run(0U).status == RunStatus::time_limit,
        "ranged forwarding processes reach their initial waits");
    const auto startup_resumes = executor_probe.resumes;
    const auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto startup_forwarding_calls = forwarding_probe.forwarding_calls;
    const auto startup_forwarding_evaluations
        = implementation.systemverilog_wave_profile_region_forwarding_evaluations;
    const auto startup_forwarding_members
        = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions;
    const auto component = implementation.region_component_by_process.at(1U);
    require(implementation.native_signal_has_runtime_dependency(internal)
            && !implementation.native_signal_has_runtime_dependency(
                internal, true),
        "legacy native publication stays conservative while RegionGraph uses its exact range");
    auto& mutable_implementation
        = OwnedDriverDemotionTestAccess::implementation(interpreter);
    mutable_implementation.fanout_cohort_grouping_enabled
        = cohort_grouping_enabled;
    const auto private_slots_before_high_bit
        = mutable_implementation
              .systemverilog_wave_profile_region_forwarding_private_parent_slots_elided;
    const auto private_dispatches_before_high_bit
        = mutable_implementation
              .systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
    const auto public_tokens_before_high_bit
        = mutable_implementation
              .systemverilog_wave_profile_region_forwarding_public_update_tokens;
    const bool saved_unknown
        = mutable_implementation.native_signal_dependencies_unknown;
    mutable_implementation.native_signal_dependencies_unknown = true;
    const bool unknown_mask_is_conservative
        = mutable_implementation.native_signal_has_runtime_dependency(
            internal, true);
    mutable_implementation.native_signal_dependencies_unknown = saved_unknown;
    require(unknown_mask_is_conservative
            && !mutable_implementation.native_signal_dependencies_unknown
            && !mutable_implementation.native_signal_has_runtime_dependency(
                internal, true),
        "unknown dependency masks conservatively reject, then restored range metadata remains usable");
    require(component < implementation.region_activation_programs.size()
            && implementation.region_activation_programs[component]
            && implementation.region_activation_programs[component]
                    ->forwarding_kernel.has_value(),
        "ranged sensitivity has a certified forwarding kernel");
    const auto& forwarding
        = *implementation.region_activation_programs[component]
               ->forwarding_kernel;
    const auto child_member = std::ranges::find(forwarding.members,
        ProcessId { 0U }, &RegionConeForwardingMember::process);
    require(child_member != forwarding.members.end()
            && child_member->dependency_count == 1U
            && child_member->dependency_begin < forwarding.dependencies.size(),
        "ranged child retains its exact sensitivity dependency");
    const auto& dependency
        = forwarding.dependencies[child_member->dependency_begin];
    require(dependency.signal == internal
            && dependency.offset == sensitivity_offset
            && dependency.width == sensitivity_width,
        "forwarding dependency preserves the exact sensitivity range");

    require(interpreter.run(1U).status == RunStatus::time_limit,
        "ranged forwarding reaches the unrelated high-bit transition");
    require(forwarding_probe.forwarding_calls - startup_forwarding_calls == 1U
            && implementation.systemverilog_wave_profile_region_forwarding_evaluations
                - startup_forwarding_evaluations == 1U
            && implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
                - startup_forwarding_members == 1U
            && executor_probe.resumes == startup_resumes,
        "an unrelated high-bit change forwards only the root and leaves the child inactive");
    require(mutable_implementation
                    .systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
                - private_slots_before_high_bit == 1U
            && mutable_implementation
                    .systemverilog_wave_profile_region_forwarding_private_parent_dispatches
                - private_dispatches_before_high_bit == 1U
            && mutable_implementation
                    .systemverilog_wave_profile_region_forwarding_public_update_tokens
                - public_tokens_before_high_bit == 0U,
        "the ranged root replaces its update slot with one private-stage record");
    require(mutable_implementation.logical_signal_value(internal)
                == high_bit_value
            && mutable_implementation.logical_signal_value(output)
                == PackedLogic4(width, Logic4::zero),
        "an irrelevant bit change does not update the ranged child output");

    if (sensitivity_width > 1U) {
        require(interpreter.run(2U).status == RunStatus::time_limit,
            "the first cross-word range bit advances independently");
        require(forwarding_probe.forwarding_calls - startup_forwarding_calls == 2U
                && implementation.systemverilog_wave_profile_region_forwarding_evaluations
                    - startup_forwarding_evaluations == 2U
                && implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
                    - startup_forwarding_members == 3U
                && mutable_implementation.logical_signal_value(internal)
                    == first_watched_transition
                && mutable_implementation.logical_signal_value(output)
                    == first_watched_transition,
            "the low bit of the cross-word range wakes the child with the full value");
    }

    require(interpreter.run().status == RunStatus::completed,
        "ranged forwarding completes the watched low-bit transition");
    const auto expected_calls = sensitivity_width > 1U ? 3U : 2U;
    const auto expected_members = sensitivity_width > 1U ? 5U : 3U;
    require(forwarding_probe.forwarding_calls - startup_forwarding_calls
                == expected_calls
            && implementation.systemverilog_wave_profile_region_forwarding_evaluations
                - startup_forwarding_evaluations == expected_calls
            && implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
                - startup_forwarding_members == expected_members
            && executor_probe.resumes == startup_resumes,
        "a watched low-bit change forwards both root and child without source resumes");
    require(interpreter.signal_value(internal) == second_watched_transition
            && interpreter.signal_value(output) == second_watched_transition,
        "each watched range transition activates the child with the complete signal value");
    require(mutable_implementation
                    .systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
                - private_slots_before_high_bit == expected_calls
            && mutable_implementation
                    .systemverilog_wave_profile_region_forwarding_private_parent_dispatches
                - private_dispatches_before_high_bit == expected_calls
            && mutable_implementation
                    .systemverilog_wave_profile_region_forwarding_public_update_tokens
                - public_tokens_before_high_bit == expected_calls - 1U,
        "ranged roots retain private original keys while only watched child transitions create public tokens");
}

void check_region_forwarding_module_path_dependency_masks()
{
    const auto add_module_path = [](Interpreter& interpreter,
                                    const SignalId source,
                                    const SignalId destination,
                                    std::string identity) {
        ModulePath path;
        path.id = 0U;
        path.identity = std::move(identity);
        path.sources = { { source, 0U, 1U } };
        path.destinations = { { destination, 0U, 1U } };
        path.delays = { 1U };
        (void)interpreter.add_module_path(std::move(path));
    };

    Interpreter ranged_and_module;
    const auto ranged_signal = ranged_and_module.add_signal({
        "forward_range_and_path.source", PackedLogic4(2U, Logic4::zero) });
    const auto ranged_destination = ranged_and_module.add_signal({
        "forward_range_and_path.destination",
        PackedLogic4(2U, Logic4::zero) });
    Process ranged_process;
    ranged_process.id = 0U;
    ranged_process.name = "forward_range_and_path_process";
    ranged_process.static_sensitivity = {
        { ranged_signal, EdgeKind::any, 0U, 1U },
    };
    ranged_process.operations = { Halt { } };
    require(ranged_and_module.add_process(std::move(ranged_process)) == 0U,
        "the mixed dependency fixture registers its ranged process");
    add_module_path(ranged_and_module, ranged_signal,
        ranged_destination, "forward_range_and_path.module_path");
    auto& mixed_impl
        = OwnedDriverDemotionTestAccess::implementation(ranged_and_module);
    mixed_impl.build_native_signal_dependency_masks();
    require(mixed_impl.native_signal_dependency_mask.at(ranged_signal) != 0U
            && mixed_impl.native_signal_non_range_dependency_mask.at(
                ranged_signal) != 0U
            && mixed_impl.native_signal_has_runtime_dependency(
                ranged_signal, true),
        "a module-path dependency on the ranged signal remains non-range and conservative for RegionGraph");

    Interpreter module_only;
    const auto module_signal = module_only.add_signal({
        "forward_module_only.source", PackedLogic4(2U, Logic4::zero) });
    const auto module_destination = module_only.add_signal({
        "forward_module_only.destination",
        PackedLogic4(2U, Logic4::zero) });
    add_module_path(module_only, module_signal, module_destination,
        "forward_module_only.module_path");
    auto& module_impl
        = OwnedDriverDemotionTestAccess::implementation(module_only);
    module_impl.build_native_signal_dependency_masks();
    require(module_impl.native_signal_dependency_mask.at(module_signal)
                != 0U
            && module_impl.native_signal_non_range_dependency_mask.at(
                module_signal) != 0U
            && module_impl.native_signal_has_runtime_dependency(
                module_signal)
            && module_impl.native_signal_has_runtime_dependency(
                module_signal, true),
        "a module-only dependency remains conservative for legacy and RegionGraph queries");
}

void check_region_forwarding_multi_output_cut()
{
    struct RunResult {
        std::vector<std::pair<std::uint8_t, std::uint8_t>> sampled_inputs;
        std::array<std::size_t, 4U> startup_resumes { };
        std::array<std::size_t, 4U> final_resumes { };
        PackedLogic4 first;
        PackedLogic4 second;
        PackedLogic4 output;
        ForwardingRuntimeProbe forwarding_probe;
        std::uint64_t forwarding_evaluations { };
        std::uint64_t forwarded_members { };
        std::uint64_t private_stage_dispatches { };
        std::uint64_t private_stage_fallback_descriptors { };
        std::uint64_t private_parent_slots_elided { };
        std::uint64_t private_parent_dispatches { };
        std::uint64_t public_update_tokens { };
        bool private_stage_shape_is_available { };
        bool private_stage_pool_is_available { };
    };
    const auto run_case = [](const bool forwarding_enabled) {
        ScopedEnvironment kernel_enabled {
            "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave_enabled {
            "FSIM_ENABLE_SV_LOCAL_WAVE", forwarding_enabled ? "1" : "0" };
        ScopedEnvironment profile_enabled { "FSIM_PROFILE_SV_WAVES", "1" };
        RunResult result;
        RegionKernelProbe executor_probe;
        std::vector<std::pair<std::uint8_t, std::uint8_t>> sampled_inputs;
        Interpreter interpreter;
        const auto input = interpreter.add_signal({ "forward_cut.input",
            PackedLogic4(1U, Logic4::zero) });
        const auto first_internal = interpreter.add_signal({ "forward_cut.a",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto second_internal = interpreter.add_signal({ "forward_cut.b",
            PackedLogic4(1U, Logic4::zero), ResolutionKind::sv_wire });
        const auto output = interpreter.add_signal({ "forward_cut.output",
            PackedLogic4(1U, Logic4::x), ResolutionKind::sv_wire });

        Process child;
        child.id = 0U;
        child.name = "forward_cut_child";
        child.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        child.initialize = true;
        child.register_count = 2U;
        child.static_sensitivity = {
            { first_internal, EdgeKind::any },
            { second_internal, EdgeKind::any },
        };
        child.driver_regions = { { output, 0U, 0U, true } };
        child.operations = {
            ReadSignal { 0U, first_internal },
            ReadSignal { 1U, second_internal },
            Binary { BinaryOperator::bit_xor, 0U, 0U, 1U },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(child)) == 0U,
            "multi-output child precedes its parent in ProcessId order");

        Process parent;
        parent.id = 1U;
        parent.name = "forward_cut_parent";
        parent.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        parent.initialize = true;
        parent.register_count = 2U;
        parent.static_sensitivity = { { input, EdgeKind::any } };
        parent.driver_regions = {
            { first_internal, 0U, 0U, true },
            { second_internal, 0U, 0U, true },
        };
        parent.operations = {
            ReadSignal { 0U, input },
            CopyRegister { 1U, 0U },
            WriteUpdate { first_internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WriteUpdate { second_internal, 1U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { }, Jump { 0U },
        };
        require(interpreter.add_process(std::move(parent)) == 1U,
            "multi-output parent keeps two source-ordered update tickets");

        const auto add_executor = [&](const ProcessId process,
                                      const SignalId source,
                                      const SignalId destination,
                                      const std::size_t probe_slot,
                                      const std::size_t register_count,
                                      const std::optional<SignalId> second_destination,
                                      const std::optional<SignalId> secondary,
                                      const bool xor_secondary) {
            const auto& registered = interpreter.process_program(process);
            const ProcessExecutorProgramBinding binding {
                registered, registered, process };
            const auto wait_instruction = static_cast<InstructionIndex>(
                registered.operations.size() - 2U);
            DeferredProcessExecutorContract contract;
            contract.expected_access = binding;
            contract.callbacks_observation_safe = true;
            contract.expected_region_kernel_equivalent = true;
            interpreter.set_deferred_process_executor(process,
                [] { return true; },
                [&executor_probe, &sampled_inputs, process, source, destination,
                    probe_slot, register_count, second_destination, secondary,
                    xor_secondary, binding, wait_instruction] {
                    return std::make_unique<RegionKernelExecutor>(
                        executor_probe, process, source, destination,
                        wait_instruction, binding, true, probe_slot, true,
                        true, register_count, second_destination, secondary,
                        true, false, xor_secondary,
                        secondary ? &sampled_inputs : nullptr);
                }, std::move(contract));
        };
        add_executor(0U, first_internal, output, 0U, 2U,
            std::nullopt, second_internal, true);
        add_executor(1U, input, first_internal, 1U, 2U,
            second_internal, std::nullopt, false);
        interpreter.materialize_ready_process_executors();

        Process clock;
        clock.id = 2U;
        clock.name = "forward_cut_clock";
        clock.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        clock.register_count = 1U;
        clock.driver_regions = { { input, 0U, 0U, true } };
        clock.operations = {
            WaitFor { 1U },
            LoadConstant { 0U, PackedLogic4(1U, Logic4::one) },
            WriteBlocking { input, 0U },
            Halt { },
        };
        require(interpreter.add_process(std::move(clock)) == 2U,
            "multi-output test clock stays outside the certified component");
        if (forwarding_enabled) {
            interpreter.set_region_kernel_backend_provider(
                std::make_shared<ForwardingRuntimeBackendProvider>(
                    result.forwarding_probe));
        }

        interpreter.start();
        require(interpreter.run(0U).status == RunStatus::time_limit,
            "multi-output members reach their initial static waits");
        result.startup_resumes = executor_probe.resumes;
        sampled_inputs.clear();
        const auto& implementation
            = OwnedDriverDemotionTestAccess::implementation(interpreter);
        const auto evaluations_before
            = implementation.systemverilog_wave_profile_region_forwarding_evaluations;
        const auto members_before
            = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions;
        const auto stages_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_dispatches;
        const auto fallbacks_before
            = implementation.systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors;
        const auto elisions_before
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided;
        const auto private_dispatches_before
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
        const auto public_tokens_before
            = implementation.systemverilog_wave_profile_region_forwarding_public_update_tokens;
        if (forwarding_enabled) {
            const auto component
                = implementation.region_component_by_process.at(1U);
            require(component < implementation.region_activation_programs.size()
                    && implementation.region_activation_programs[component]
                    && implementation.region_activation_programs[component]
                            ->forwarding_kernel.has_value(),
                "multi-output component has a certified forwarding program");
            const auto& forwarding
                = *implementation.region_activation_programs[component]
                       ->forwarding_kernel;
            const auto root = std::ranges::find(forwarding.members,
                ProcessId { 1U }, &RegionConeForwardingMember::process);
            require(root != forwarding.members.end()
                    && root->output_count == 2U,
                "forwarding parent owns both internal output tickets");
            const auto& local_state
                = implementation.region_local_wave_state_by_component.at(
                    component);
            require(local_state && local_state->forwarding_results,
                "the multi-output forwarding route has a runtime result bank");
            result.private_stage_shape_is_available
                = local_state->forwarding_results->stage_batch_group_shape;
            result.private_stage_pool_is_available
                = !local_state->forwarding_results->stage_batch_pool.empty();
        }

        require(interpreter.run().status == RunStatus::completed,
            "multi-output execution preserves original callback ordering");
        result.sampled_inputs = std::move(sampled_inputs);
        result.final_resumes = executor_probe.resumes;
        result.first = interpreter.signal_value(first_internal);
        result.second = interpreter.signal_value(second_internal);
        result.output = interpreter.signal_value(output);
        result.forwarding_evaluations
            = implementation.systemverilog_wave_profile_region_forwarding_evaluations
                - evaluations_before;
        result.forwarded_members
            = implementation.systemverilog_wave_profile_region_forwarding_member_consumptions
                - members_before;
        result.private_stage_dispatches
            = implementation.systemverilog_wave_profile_region_forwarding_stage_dispatches
                - stages_before;
        result.private_stage_fallback_descriptors
            = implementation.systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors
                - fallbacks_before;
        result.private_parent_slots_elided
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
                - elisions_before;
        result.private_parent_dispatches
            = implementation.systemverilog_wave_profile_region_forwarding_private_parent_dispatches
                - private_dispatches_before;
        result.public_update_tokens
            = implementation.systemverilog_wave_profile_region_forwarding_public_update_tokens
                - public_tokens_before;
        require(!forwarding_enabled
                || (result.forwarding_probe.activation_factories == 1U
                    && result.forwarding_probe.activation_calls == 0U
                    && result.forwarding_probe.forwarding_calls != 0U),
            "the multi-output positive route uses a forwarding-only provider");
        return result;
    };

    const auto checked = run_case(false);
    const auto forwarded = run_case(true);
    const auto complete_cut = std::pair<std::uint8_t, std::uint8_t> { 1U, 1U };
    require(std::ranges::find(checked.sampled_inputs, complete_cut)
                != checked.sampled_inputs.end()
            && std::ranges::all_of(checked.sampled_inputs,
                [&](const auto& sample) { return sample == complete_cut; }),
        "the ordinary reference shows the child runs after the parent update batch settles");
    require(forwarded.sampled_inputs.empty(),
        "the forwarding result consumes the child only after both parent output tickets settle");
    require(forwarded.final_resumes[1U] == forwarded.startup_resumes[1U]
            && forwarded.final_resumes[0U] == forwarded.startup_resumes[0U]
            && forwarded.forwarding_evaluations == 1U
            && forwarded.forwarded_members == 2U
            && forwarded.private_stage_shape_is_available
            && forwarded.private_stage_pool_is_available
            && forwarded.private_stage_dispatches == 2U
            && forwarded.private_stage_fallback_descriptors == 0U
            && forwarded.private_parent_slots_elided == 2U
            && forwarded.private_parent_dispatches == 2U
            && forwarded.public_update_tokens == 1U,
        "both private parent outputs use the preallocated compact ticket one original key at a time");
    require(forwarded.first == checked.first
            && forwarded.second == checked.second
            && forwarded.output == checked.output
            && forwarded.first == PackedLogic4(1U, Logic4::one)
            && forwarded.second == PackedLogic4(1U, Logic4::one)
            && forwarded.output == PackedLogic4(1U, Logic4::zero),
        "forwarded and checked runs publish the same final multi-output result");
}
} // namespace

void test_systemverilog_wave()
{
    check_region_forwarding_runtime();
    check_region_forwarding_runtime(true);
    check_region_forwarding_multi_parent_join();
    check_region_forwarding_prewrite_journal_decline_fallback();
    check_region_forwarding_unequal_depth_join();
    check_region_forwarding_versioned_narrow_join_demotes_after_checked_cut();
    check_region_forwarding_wide_logic4_join();
    check_region_forwarding_private_parent_update_elision();
    check_region_forwarding_stale_boundary();
    check_region_forwarding_ranged_dependency(true);
    check_region_forwarding_ranged_dependency(false);
    check_region_forwarding_ranged_dependency(true, 65U);
    check_region_forwarding_ranged_dependency(false, 129U);
    check_region_forwarding_module_path_dependency_masks();
    check_region_forwarding_multi_output_cut();
    check_region_forwarding_private_stage_batch();
    for (const auto width : { 1U, 65U, 129U, 256U, 1024U }) {
        for (const auto mode : { Mode::normal, Mode::outside_writer, Mode::stop,
                 Mode::failure, Mode::post_failure, Mode::decline, Mode::observe }) {
            check_wave(width, mode);
        }
        check_wave(width, Mode::normal, Logic4::x);
        check_wave(width, Mode::normal, Logic4::z);
    }
    check_wave(1U, Mode::coverage);
    check_structural_census_profile();
    test_large_grouped_fanout_exceeds_sixty_four_members();
    test_automatic_readiness_pool_sizing_across_components();
    test_large_grouped_fanout_selective_wakeup_crosses_readiness_words();
    test_grouped_fanout_interleaved_component_route();
    check_region_kernel_capability_route(true, false);
    check_region_kernel_capability_route(true, true);
    check_region_kernel_capability_route(true, true, true);
    check_region_kernel_capability_route(true, true, false, true);
    check_region_kernel_capability_route(true, true, true, false, true);
    check_region_kernel_capability_route(true, true, false, false, false, true);
    check_region_kernel_capability_route(
        true, true, false, false, false, true, true);
    check_region_kernel_capability_route(
        true, true, false, false, false, false, false, true);
    check_region_kernel_capability_route(
        true, true, false, false, false, false, false, false, true);
    check_region_backend_pool_refresh();
    check_region_backend_pool_refresh(true);
    check_region_backend_pool_refresh(false, true);
    check_region_backend_pool_refresh(false, false,
        RegionBackendProbe::FailurePoint::generalized_input_planes);
    check_region_backend_pool_refresh(false, false,
        RegionBackendProbe::FailurePoint::legacy_input_planes);
    check_local_wave_runtime(false);
    check_local_wave_runtime(true);
    check_local_wave_runtime(false, true);
    check_local_wave_runtime(false, false, true);
    check_local_wave_runtime(true, false, true);
    check_local_wave_runtime(false, true, true);
    check_local_wave_runtime(false, false, false, true);
    check_local_wave_runtime(false, false, false, true, true);
    check_local_wave_runtime(false, false, false, true, false, true);
    check_local_wave_runtime(false, false, false, true, false, false, true);
    check_local_wave_runtime(false, false, false, false, false, false, false, true);
    check_local_wave_runtime(
        false, false, false, false, false, false, false, true, true);
    for (const auto offset : { std::uint32_t { 0U },
             std::uint32_t { 64U } }) {
        LocalWavePartialPublicationRun local_wave;
        LocalWavePartialPublicationRun interpreter_activation_fallback;
        check_local_wave_runtime(false, false, false, false, false, false,
            false, false, false, offset, &local_wave);
        check_local_wave_runtime(false, true, false, false, false, false,
            false, false, false, offset, &interpreter_activation_fallback);
        require(local_wave.output == interpreter_activation_fallback.output
                && local_wave.output.current.width() == 65U
                && local_wave.output.driver.width() == 65U
                && local_wave.output.event.has_value()
                && local_wave.output.transaction.has_value()
                && local_wave.kernel_runs != 0U
                && local_wave.kernel_publications != 0U
                && local_wave.local_update_dispatches != 0U,
            "offset-zero and nonzero partial local-wave publications match interpreter-fallback full-signal state and metadata");
    }
    check_prepared_output_interruption(
        PreparedOutputInterruption::observation);
    check_prepared_output_interruption(
        PreparedOutputInterruption::range_sensitivity);
    check_prepared_output_interruption(
        PreparedOutputInterruption::force);
    check_prepared_output_interruption(
        PreparedOutputInterruption::deposit);
    check_prepared_output_interruption(
        PreparedOutputInterruption::stop_resume);
    check_prepared_output_interruption(
        PreparedOutputInterruption::cancel);
    check_prepared_output_interruption(
        PreparedOutputInterruption::cancel, true);
    check_prepared_output_interruption(
        PreparedOutputInterruption::cancel, true, true);
    check_prepared_output_interruption(
        PreparedOutputInterruption::captured_backend_failure_retry);
    check_prepared_output_interruption(
        PreparedOutputInterruption::captured_backend_failure_retry, true);
    check_multidimensional_alias_region_admission();
    check_region_backend_exact_mapping_comparison();
}

#if defined(FSIM_RUNTIME_PREPARED_OUTPUT_FAILURE_TESTS)
void test_prepared_output_allocation_failure_retry()
{
    check_prepared_output_interruption(
        PreparedOutputInterruption::allocation_failure_retry);
}
#endif
} // namespace fsim::tests::runtime
