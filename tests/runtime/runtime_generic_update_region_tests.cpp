// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"
#include "runtime_owned_driver_demotion_test_access.hpp"
#include "../../src/runtime/simir_region_activation_reference.hpp"
#if defined(FSIM_RUNTIME_GENERIC_UPDATE_ALLOCATION_FAILURE_TESTS)
#include "runtime_fused_staging_failure_support.hpp"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#if defined(FSIM_RUNTIME_GENERIC_UPDATE_ALLOCATION_FAILURE_TESTS)
#include <new>
#endif
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* const name, const char* const value)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_); previous != nullptr) {
            previous_ = previous;
        }
#if defined(_WIN32)
        if (::_putenv_s(name_, value) != 0) {
#else
        if (::setenv(name_, value, 1) != 0) {
#endif
            throw std::runtime_error { "failed to enable generic region counters" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(name_, previous_ ? previous_->c_str() : ""));
#else
        if (previous_) {
            static_cast<void>(::setenv(name_, previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_));
        }
#endif
    }

private:
    const char* name_;
    std::optional<std::string> previous_;
};

enum class Intervention : std::uint8_t {
    none,
    stop,
    exception,
};

struct Event {
    enum class Kind : std::uint8_t { member, foreign } kind { Kind::member };
    ProcessId process { };
    SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const Event&, const Event&) = default;
};

struct GenericTaskKey {
    StableOrder stable_order { };
    std::uint64_t sequence { };
    std::uint64_t payload { };

#if defined(FSIM_RUNTIME_GENERIC_UPDATE_ALLOCATION_FAILURE_TESTS)
    friend bool operator==(const GenericTaskKey&, const GenericTaskKey&) = default;
#endif
};

struct GenericFrontierSnapshot {
    SimulationTick time { };
    std::uint64_t delta { };
    SchedulerPhase phase { SchedulerPhase::active };
    std::size_t cursor { };
    std::size_t end { };
    std::array<GenericTaskKey, 4U> tasks { };
    std::size_t task_count { };
    bool valid { };
};

[[nodiscard]] GenericFrontierSnapshot capture_generic_frontier(
    const Scheduler& scheduler) noexcept
{
    GenericFrontierSnapshot result;
    const auto frontier = scheduler.current_generic_batch_frontier();
    if (!frontier || frontier->tasks.size() > result.tasks.size()) {
        return result;
    }
    result.time = frontier->time;
    result.delta = frontier->delta;
    result.phase = frontier->phase;
    result.cursor = frontier->cursor;
    result.end = frontier->end;
    result.task_count = frontier->tasks.size();
    for (std::size_t index = 0U; index < result.task_count; ++index) {
        const auto& task = frontier->tasks[index];
        result.tasks[index] = {
            task.stable_order, task.sequence, task.payload };
    }
    result.valid = true;
    return result;
}

#if defined(FSIM_RUNTIME_GENERIC_UPDATE_ALLOCATION_FAILURE_TESTS)
[[nodiscard]] bool same_generic_task_keys(
    const GenericFrontierSnapshot& lhs,
    const GenericFrontierSnapshot& rhs) noexcept
{
    if (!lhs.valid || !rhs.valid || lhs.time != rhs.time
        || lhs.delta != rhs.delta || lhs.phase != rhs.phase
        || lhs.cursor != rhs.cursor || lhs.end != rhs.end
        || lhs.task_count != rhs.task_count) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.task_count; ++index) {
        if (lhs.tasks[index] != rhs.tasks[index]) {
            return false;
        }
    }
    // A reoffered batch gets a fresh frontier generation; the original
    // scheduler key is the stable-order/sequence/payload tuple above.
    return true;
}
#endif

struct Probe {
    Scheduler* scheduler { };
    std::vector<Event> events;
    std::array<std::size_t, 2U> resumes { };
    std::size_t backend_calls { };
    std::size_t backend_accepts { };
    GenericFrontierSnapshot failed_frontier;
    GenericFrontierSnapshot retry_frontier;
    bool capture_retry_frontier { };
};

[[nodiscard]] PackedLogic4 invert_value(const PackedLogic4& source)
{
    PackedLogic4 result(source.width(), Logic4::x);
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
        result.set(bit, logic_not(source.get(bit)));
    }
    return result;
}

[[nodiscard]] PackedLogic4 slice_value(const PackedLogic4& source,
    const std::uint32_t offset, const std::uint32_t width)
{
    PackedLogic4 result(width, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        result.set(bit, source.get(offset + bit));
    }
    return result;
}

void insert_slice(PackedLogic4& target, const PackedLogic4& source,
    const std::uint32_t offset)
{
    for (std::uint32_t bit = 0U; bit < source.width(); ++bit) {
        target.set(offset + bit, source.get(bit));
    }
}

[[nodiscard]] PackedLogic4 pattern(const std::uint32_t width,
    const std::uint32_t phase)
{
    constexpr std::array states {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    PackedLogic4 result(width, Logic4::zero);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        result.set(bit, states[(bit + phase) % states.size()]);
    }
    return result;
}

[[nodiscard]] PackedLogic4 logic9_pattern(const std::uint32_t width,
    const std::uint32_t phase)
{
    constexpr std::string_view states { "UX01ZWLH-" };
    std::string text;
    text.reserve(width);
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        text.push_back(states[(bit + phase) % states.size()]);
    }
    return PackedLogic4::from_logic9_msb_string(text);
}

class UpdateExecutor final : public ProcessExecutor {
public:
    UpdateExecutor(Probe& probe, const ProcessId process,
        const SignalId input, const SignalId output, const SignalId slice_output,
        const std::uint32_t slice_offset, const std::uint32_t slice_width,
        const InstructionIndex wait_instruction,
        ProcessExecutorProgramBinding binding,
        std::vector<std::uint32_t> register_widths)
        : probe_(probe)
        , process_(process)
        , input_(input)
        , output_(output)
        , slice_output_(slice_output)
        , slice_offset_(slice_offset)
        , slice_width_(slice_width)
        , wait_instruction_(wait_instruction)
        , binding_(std::move(binding))
        , register_widths_(std::move(register_widths))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool region_kernel_completion_has_no_persistent_registers()
        const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

    [[nodiscard]] ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        ++probe_.resumes[process_];
        probe_.events.push_back({ Event::Kind::member, process_,
            probe_.scheduler->now(), probe_.scheduler->delta() });
        require(start_instruction == 0U
                || start_instruction == wait_instruction_ + 1U,
            "generic fixture resumes only at the body or validated wait boundary");
        auto value = context.read_signal(input_);
        if (process_ == 0U) {
            context.write_update(output_, std::move(value));
        } else {
            value = invert_value(value);
            auto slice = slice_value(value, slice_offset_, slice_width_);
            context.write_update(output_, value);
            context.write_update_slice(
                slice_output_, std::move(slice), slice_offset_);
        }
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> bindings,
        const std::size_t activation_register_count,
        const void** const storage_identity) noexcept override
    {
        if (native_prepared_ || process != process_
            || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U
            || storage_identity == nullptr
            || bindings.size() != register_widths_.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < bindings.size(); ++index) {
            if (bindings[index].source_register != index
                || bindings[index].activation_register >= activation_register_count
                || !bindings[index].defined
                || bindings[index].value_kind != ValueKind::logic4
                || bindings[index].width != register_widths_[index]) {
                return false;
            }
        }
        native_prepared_ = true;
        activation_register_count_ = activation_register_count;
        *storage_identity = this;
        return true;
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> registers) noexcept override
    {
        return native_prepared_ && registers.size() == activation_register_count_;
    }

    void commit_region_completion_native() noexcept override
    {
        native_prepared_ = false;
    }

    void cancel_region_completion_native() noexcept override
    {
        native_prepared_ = false;
    }

private:
    Probe& probe_;
    ProcessId process_ { };
    SignalId input_ { };
    SignalId output_ { };
    SignalId slice_output_ { };
    std::uint32_t slice_offset_ { };
    std::uint32_t slice_width_ { };
    InstructionIndex wait_instruction_ { };
    ProcessExecutorProgramBinding binding_;
    std::vector<std::uint32_t> register_widths_;
    std::size_t activation_register_count_ { };
    bool native_prepared_ { };
};

class DisjointSliceExecutor final : public ProcessExecutor {
public:
    DisjointSliceExecutor(Probe& probe, const ProcessId process,
        const SignalId input, const SignalId output,
        const std::uint32_t offset, const std::uint32_t width,
        const InstructionIndex wait_instruction,
        ProcessExecutorProgramBinding binding,
        std::vector<std::uint32_t> register_widths)
        : probe_(probe)
        , process_(process)
        , input_(input)
        , output_(output)
        , offset_(offset)
        , width_(width)
        , wait_instruction_(wait_instruction)
        , binding_(std::move(binding))
        , register_widths_(std::move(register_widths))
    {
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool region_kernel_completion_has_no_persistent_registers()
        const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

    [[nodiscard]] ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        ++probe_.resumes[process_];
        probe_.events.push_back({ Event::Kind::member, process_,
            probe_.scheduler->now(), probe_.scheduler->delta() });
        require(start_instruction == 0U
                || start_instruction == wait_instruction_ + 1U,
            "disjoint generic writers resume only at their body or wait boundary");
        const auto input = context.read_signal(input_);
        auto slice = slice_value(input, offset_, width_);
        context.write_update_slice(output_, std::move(slice), offset_);
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> bindings,
        const std::size_t activation_register_count,
        const void** const storage_identity) noexcept override
    {
        if (native_prepared_ || process != process_
            || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U
            || storage_identity == nullptr
            || bindings.size() != register_widths_.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < bindings.size(); ++index) {
            if (bindings[index].source_register != index
                || bindings[index].activation_register >= activation_register_count
                || !bindings[index].defined
                || bindings[index].value_kind != ValueKind::logic4
                || bindings[index].width != register_widths_[index]) {
                return false;
            }
        }
        native_prepared_ = true;
        activation_register_count_ = activation_register_count;
        *storage_identity = this;
        return true;
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> registers) noexcept override
    {
        return native_prepared_ && registers.size() == activation_register_count_;
    }

    void commit_region_completion_native() noexcept override
    {
        native_prepared_ = false;
    }

    void cancel_region_completion_native() noexcept override
    {
        native_prepared_ = false;
    }

private:
    Probe& probe_;
    ProcessId process_ { };
    SignalId input_ { };
    SignalId output_ { };
    std::uint32_t offset_ { };
    std::uint32_t width_ { };
    InstructionIndex wait_instruction_ { };
    ProcessExecutorProgramBinding binding_;
    std::vector<std::uint32_t> register_widths_;
    std::size_t activation_register_count_ { };
    bool native_prepared_ { };
};

class Logic9UpdateExecutor final : public ProcessExecutor {
public:
    Logic9UpdateExecutor(Probe& probe, const ProcessId process,
        const SignalId input, const SignalId parent, const SignalId whole,
        const SignalId slice, const std::uint32_t slice_offset,
        const std::uint32_t slice_width,
        const InstructionIndex wait_instruction,
        ProcessExecutorProgramBinding binding,
        std::vector<std::uint32_t> register_widths)
        : probe_(probe)
        , process_(process)
        , input_(input)
        , parent_(parent)
        , whole_(whole)
        , slice_(slice)
        , slice_offset_(slice_offset)
        , slice_width_(slice_width)
        , wait_instruction_(wait_instruction)
        , binding_(std::move(binding))
        , register_widths_(std::move(register_widths))
    {
        activation_register_indices_.resize(register_widths_.size());
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return &binding_;
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool region_kernel_completion_has_no_persistent_registers()
        const noexcept override
    {
        return true;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return true;
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return &probe_;
    }

    [[nodiscard]] ProcessResumeResult resume(ProcessExecutionContext& context,
        const InstructionIndex start_instruction) override
    {
        ++probe_.resumes[process_];
        probe_.events.push_back({ Event::Kind::member, process_,
            probe_.scheduler->now(), probe_.scheduler->delta() });
        require(start_instruction == 0U
                || start_instruction == wait_instruction_ + 1U,
            "generic Logic9 fixture resumes only at its body or wait boundary");
        auto value = context.read_signal(process_ == 0U ? input_ : parent_);
        if (process_ == 0U) {
            context.write_update(parent_, std::move(value));
        } else {
            auto slice_value = value.extract_bits(slice_offset_, slice_width_);
            context.write_update(whole_, std::move(value));
            context.write_update_slice(slice_, std::move(slice_value),
                slice_offset_);
        }
        ProcessResumeResult result {
            wait_instruction_, wait_instruction_ + 1U };
        result.external.kind = ExternalSuspendKind::validated_wait_sensitivity;
        return result;
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> bindings,
        const std::size_t activation_register_count,
        const void** const storage_identity) noexcept override
    {
        if (native_prepared_ || process != process_
            || wait_instruction != wait_instruction_
            || jump_instruction != wait_instruction_ + 1U
            || storage_identity == nullptr
            || bindings.size() != register_widths_.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < bindings.size(); ++index) {
            if (bindings[index].source_register != index
                || bindings[index].activation_register >= activation_register_count
                || !bindings[index].defined
                || bindings[index].value_kind != ValueKind::logic9
                || bindings[index].width != register_widths_[index]) {
                return false;
            }
            activation_register_indices_[index]
                = bindings[index].activation_register;
        }
        native_prepared_ = true;
        activation_register_count_ = activation_register_count;
        *storage_identity = this;
        return true;
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> registers) noexcept override
    {
        if (!native_prepared_ || registers.size() != activation_register_count_) {
            return false;
        }
        for (std::size_t index = 0U;
             index < activation_register_indices_.size(); ++index) {
            const auto activation_register
                = activation_register_indices_[index];
            if (activation_register >= registers.size()
                || !registers[activation_register].is_logic9()
                || registers[activation_register].width()
                    != register_widths_[index]) {
                return false;
            }
        }
        return true;
    }

    void commit_region_completion_native() noexcept override
    {
        native_prepared_ = false;
    }

    void cancel_region_completion_native() noexcept override
    {
        native_prepared_ = false;
    }

private:
    Probe& probe_;
    ProcessId process_ { };
    SignalId input_ { };
    SignalId parent_ { };
    SignalId whole_ { };
    SignalId slice_ { };
    std::uint32_t slice_offset_ { };
    std::uint32_t slice_width_ { };
    InstructionIndex wait_instruction_ { };
    ProcessExecutorProgramBinding binding_;
    std::vector<std::uint32_t> register_widths_;
    std::vector<std::size_t> activation_register_indices_;
    std::size_t activation_register_count_ { };
    bool native_prepared_ { };
};

class ReferenceBackend final : public RegionKernelBackend {
public:
    ReferenceBackend(Probe& probe, RegionConeActivationKernel kernel,
        const bool accept)
        : probe_(probe)
        , kernel_(std::move(kernel))
        , accept_(accept)
    {
    }

    [[nodiscard]] bool execute(
        const RegionKernelActivationImage& image) noexcept override
    {
        ++probe_.backend_calls;
        if (probe_.capture_retry_frontier) {
            probe_.retry_frontier
                = capture_generic_frontier(*probe_.scheduler);
        }
        if (!accept_) {
            return false;
        }
        try {
            registers_ = evaluate_region_activation_kernel_reference(kernel_, image);
            ++probe_.backend_accepts;
            for (const auto process : image.ready_processes) {
                probe_.events.push_back({ Event::Kind::member, process,
                    probe_.scheduler->now(), probe_.scheduler->delta() });
            }
            return true;
        } catch (...) {
            registers_.clear();
            return false;
        }
    }

    [[nodiscard]] std::span<const PackedLogic4>
    activation_registers() const noexcept override
    {
        return registers_;
    }

private:
    Probe& probe_;
    RegionConeActivationKernel kernel_;
    bool accept_ { };
    std::vector<PackedLogic4> registers_;
};

class ReferenceBackendProvider final : public RegionKernelBackendProvider {
public:
    ReferenceBackendProvider(Probe& probe, const bool accept) noexcept
        : probe_(probe)
        , accept_(accept)
    {
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return "runtime-generic-update-reference-v1";
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        if (kernel.outputs.empty()
            || !std::ranges::all_of(kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.update_kind == RegionUpdateKind::generic
                        && output.domain == SignalUpdateDomain::generic;
                })) {
            return { };
        }
        ++generic_kernels_;
        return std::make_unique<ReferenceBackend>(
            probe_, kernel, accept_);
    }

    [[nodiscard]] std::size_t generic_kernels() const noexcept
    {
        return generic_kernels_;
    }

private:
    Probe& probe_;
    bool accept_ { };
    std::size_t generic_kernels_ { };
};

struct Counts {
    std::uint64_t attempts { };
    std::uint64_t backend_runs { };
    std::uint64_t completions { };
    std::uint64_t members { };
    std::uint64_t publications { };
    std::uint64_t declines { };
    std::uint64_t failures { };
};

[[nodiscard]] Counts region_counts(Interpreter& interpreter)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    return { impl.generic_projected_region_attempts,
        impl.generic_projected_region_backend_runs,
        impl.generic_projected_region_completions,
        impl.generic_projected_region_members,
        impl.generic_projected_region_publications,
        impl.generic_projected_region_declines,
        impl.generic_projected_region_failures };
}

[[nodiscard]] Counts difference(const Counts& after, const Counts& before)
{
    return { after.attempts - before.attempts,
        after.backend_runs - before.backend_runs,
        after.completions - before.completions,
        after.members - before.members,
        after.publications - before.publications,
        after.declines - before.declines,
        after.failures - before.failures };
}

struct SignalSnapshot {
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    std::optional<PackedLogic4> raw;
    std::optional<PackedLogic4> secondary_raw;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;

    friend bool operator==(const SignalSnapshot&, const SignalSnapshot&) = default;
};

[[nodiscard]] std::vector<SignalSnapshot> snapshot(Interpreter& interpreter,
    const std::array<SignalId, 4U>& signals)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    std::vector<SignalSnapshot> result;
    result.reserve(signals.size());
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        const auto signal = signals[index];
        SignalSnapshot value { impl.signals.at(signal).initial_value,
            impl.signal_last_values.at(signal),
            impl.driven_values.at(signal),
            std::nullopt, std::nullopt,
            impl.signal_transactions.at(signal),
            impl.signal_events.at(signal) };
        if (index == 1U) {
            value.raw = impl.underlying_driver_value(0U, signal);
        } else if (index >= 2U) {
            value.raw = impl.underlying_driver_value(1U, signal);
        }
        result.push_back(std::move(value));
    }
    return result;
}

[[nodiscard]] std::vector<SignalSnapshot> snapshot_disjoint(
    Interpreter& interpreter, const SignalId input, const SignalId output)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    std::vector<SignalSnapshot> result;
    result.reserve(2U);
    result.push_back({ impl.signals.at(input).initial_value,
        impl.signal_last_values.at(input),
        impl.driven_values.at(input), std::nullopt,
        std::nullopt, impl.signal_transactions.at(input),
        impl.signal_events.at(input) });
    SignalSnapshot driven { impl.signals.at(output).initial_value,
        impl.signal_last_values.at(output),
        impl.driven_values.at(output),
        impl.underlying_driver_value(0U, output),
        impl.underlying_driver_value(1U, output),
        impl.signal_transactions.at(output), impl.signal_events.at(output) };
    result.push_back(std::move(driven));
    return result;
}

[[nodiscard]] PackedLogic4 changed_input(const std::uint32_t width)
{
    return pattern(width, 1U);
}

struct Outcome {
    std::vector<Event> events;
    std::vector<SignalSnapshot> stop_state;
    std::vector<SignalSnapshot> final_state;
    Counts counts;
    std::array<std::size_t, 2U> resume_delta { };
    std::size_t backend_calls { };
    std::size_t backend_accepts { };
    std::size_t generic_kernels { };
    std::size_t parent_pending_at_cut { };
    bool stopped { };
    bool threw { };
};

[[nodiscard]] Outcome run_foreign_case(const std::uint32_t width,
    const Intervention intervention, const bool install_backend,
    const bool backend_accept = true)
{
    ScopedEnvironment profile("FSIM_PROFILE_SV_WAVES", "1");
    Probe probe;
    Interpreter interpreter;
    probe.scheduler = &interpreter.scheduler();
    const auto initial_input = pattern(width, 0U);
    const auto initial_output = pattern(width, 3U);
    const auto input = interpreter.add_signal({ "generic.input", initial_input });
    const auto parent = interpreter.add_signal({ "generic.parent", initial_input });
    const auto whole = interpreter.add_signal({ "generic.whole", initial_output });
    const auto slice = interpreter.add_signal({ "generic.slice", initial_output });
    const auto slice_width = width >= 65U
        ? 2U : std::min<std::uint32_t>(3U, width);
    const auto slice_offset = width >= 65U ? 63U : width - slice_width;

    Process producer;
    producer.id = 0U;
    producer.name = "generic_update_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::generic;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.driver_regions = { { parent, 0U, width, true } };
    producer.operations = { ReadSignal { 0U, input },
        WriteUpdate { parent, 0U }, WaitSensitivity { }, Jump { 0U } };
    const auto producer_id = interpreter.add_process(std::move(producer));
    const auto& registered_producer = interpreter.process_program(producer_id);
    interpreter.set_process_executor(producer_id,
        std::make_unique<UpdateExecutor>(probe, producer_id, input, parent,
            slice, slice_offset, slice_width, 2U,
            ProcessExecutorProgramBinding {
                registered_producer, registered_producer, producer_id },
            std::vector<std::uint32_t> { width }));

    Process consumer;
    consumer.id = 1U;
    consumer.name = "generic_update_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::generic;
    consumer.register_count = 3U;
    consumer.register_value_kinds = { ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4 };
    consumer.static_sensitivity = { { parent, EdgeKind::any } };
    consumer.driver_regions = { { whole, 0U, width, true },
        { slice, slice_offset, slice_width, false } };
    consumer.operations = { ReadSignal { 0U, parent },
        UnaryNot { 1U, 0U }, Extract { 2U, 1U, slice_offset, slice_width },
        WriteUpdate { whole, 1U },
        WriteUpdateSlice { slice, 2U, slice_offset },
        WaitSensitivity { }, Jump { 0U } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));
    const auto& registered_consumer = interpreter.process_program(consumer_id);
    interpreter.set_process_executor(consumer_id,
        std::make_unique<UpdateExecutor>(probe, consumer_id, parent, whole,
            slice, slice_offset, slice_width, 5U,
            ProcessExecutorProgramBinding {
                registered_consumer, registered_consumer, consumer_id },
            std::vector<std::uint32_t> { width, width, slice_width }));

    std::shared_ptr<ReferenceBackendProvider> provider;
    if (install_backend) {
        provider = std::make_shared<ReferenceBackendProvider>(
            probe, backend_accept);
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "generic update fixture settles its initialization slot");
    const auto resume_baseline = probe.resumes;
    const auto backend_calls_baseline = probe.backend_calls;
    const auto backend_accepts_baseline = probe.backend_accepts;
    const auto count_baseline = region_counts(interpreter);
    probe.events.clear();

    interpreter.schedule_signal_at(input, changed_input(width), 1U, 0U);
    if (intervention != Intervention::none) {
        interpreter.scheduler().schedule_at(1U, SchedulerPhase::update, 1U,
            [&](Scheduler& scheduler) {
                scheduler.schedule_next_delta(SchedulerPhase::active, 1U,
                    [&](Scheduler& current) {
                        probe.events.push_back({ Event::Kind::foreign,
                            std::numeric_limits<ProcessId>::max(),
                            current.now(), current.delta() });
                        if (intervention == Intervention::stop) {
                            current.request_stop();
                        } else if (intervention == Intervention::exception) {
                            throw std::runtime_error {
                                "generic update foreign callback failure" };
                        }
                    });
            });
    }

    Outcome result;
    try {
        const auto first = interpreter.run(1U);
        result.stopped = first.status == RunStatus::stopped;
        require(result.stopped == (intervention == Intervention::stop),
            "the foreign stop witness stops only at its reserved scheduler key");
    } catch (const std::runtime_error& error) {
        require(intervention == Intervention::exception
                && std::string_view(error.what())
                    == "generic update foreign callback failure",
            "only the scheduled generic foreign task may throw");
        result.threw = true;
    }

    if (intervention == Intervention::stop
        || intervention == Intervention::exception) {
        require(intervention == Intervention::stop
                ? result.stopped : result.threw,
            "the selected foreign interruption reaches "
            "the scheduler");
        result.stop_state = snapshot(interpreter,
            { input, parent, whole, slice });
        auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
        result.parent_pending_at_cut = static_cast<std::size_t>(
            std::ranges::count_if(impl.pending_updates,
                [parent](const auto& update) { return update.signal == parent; }));
        require(result.parent_pending_at_cut == 1U,
            "the producer Update remains pending behind its original commit key");
        if (result.stopped) {
            interpreter.scheduler().clear_stop();
        }
        const auto resumed = interpreter.run(1U);
        require(resumed.status == RunStatus::completed
                || resumed.status == RunStatus::time_limit,
            "the original generic Update suffix completes after resume");
    }

    result.final_state = snapshot(interpreter, { input, parent, whole, slice });
    result.events = probe.events;
    result.backend_calls = probe.backend_calls - backend_calls_baseline;
    result.backend_accepts = probe.backend_accepts - backend_accepts_baseline;
    result.generic_kernels = provider ? provider->generic_kernels() : 0U;
    for (std::size_t index = 0U; index < result.resume_delta.size(); ++index) {
        result.resume_delta[index] = probe.resumes[index] - resume_baseline[index];
    }
    result.counts = difference(region_counts(interpreter), count_baseline);
    return result;
}

[[nodiscard]] Outcome run_late_observation_case(
    const std::uint32_t width, const bool install_backend)
{
    ScopedEnvironment profile("FSIM_PROFILE_SV_WAVES", "1");
    Probe probe;
    Interpreter interpreter;
    probe.scheduler = &interpreter.scheduler();
    const auto initial = pattern(width, 0U);
    const auto output_initial = pattern(width, 3U);
    const auto input = interpreter.add_signal({ "generic.input", initial });
    const auto parent = interpreter.add_signal({ "generic.parent", initial });
    const auto whole = interpreter.add_signal({ "generic.whole", output_initial });
    const auto slice = interpreter.add_signal({ "generic.slice", output_initial });
    const auto slice_width = width >= 65U
        ? 2U : std::min<std::uint32_t>(3U, width);
    const auto slice_offset = width >= 65U ? 63U : width - slice_width;

    Process producer;
    producer.id = 0U;
    producer.name = "generic_observed_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::generic;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.driver_regions = { { parent, 0U, width, true } };
    producer.operations = { ReadSignal { 0U, input },
        WriteUpdate { parent, 0U }, WaitSensitivity { }, Jump { 0U } };
    const auto producer_id = interpreter.add_process(std::move(producer));
    const auto& registered_producer = interpreter.process_program(producer_id);
    interpreter.set_process_executor(producer_id,
        std::make_unique<UpdateExecutor>(probe, producer_id, input, parent,
            slice, slice_offset, slice_width, 2U,
            ProcessExecutorProgramBinding {
                registered_producer, registered_producer, producer_id },
            std::vector<std::uint32_t> { width }));

    Process consumer;
    consumer.id = 1U;
    consumer.name = "generic_observed_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::generic;
    consumer.register_count = 3U;
    consumer.register_value_kinds = { ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4 };
    consumer.static_sensitivity = { { parent, EdgeKind::any } };
    consumer.driver_regions = { { whole, 0U, width, true },
        { slice, slice_offset, slice_width, false } };
    consumer.operations = { ReadSignal { 0U, parent },
        UnaryNot { 1U, 0U }, Extract { 2U, 1U, slice_offset, slice_width },
        WriteUpdate { whole, 1U },
        WriteUpdateSlice { slice, 2U, slice_offset },
        WaitSensitivity { }, Jump { 0U } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));
    const auto& registered_consumer = interpreter.process_program(consumer_id);
    interpreter.set_process_executor(consumer_id,
        std::make_unique<UpdateExecutor>(probe, consumer_id, parent, whole,
            slice, slice_offset, slice_width, 5U,
            ProcessExecutorProgramBinding {
                registered_consumer, registered_consumer, consumer_id },
            std::vector<std::uint32_t> { width, width, slice_width }));

    std::shared_ptr<ReferenceBackendProvider> provider;
    if (install_backend) {
        provider = std::make_shared<ReferenceBackendProvider>(probe, true);
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    static_cast<void>(interpreter.run(0U));
    const auto resume_baseline = probe.resumes;
    const auto before_first_wave = region_counts(interpreter);
    interpreter.schedule_signal_at(input, pattern(width, 1U), 1U, 0U);
    static_cast<void>(interpreter.run(1U));
    const auto after_first_wave = region_counts(interpreter);
    const auto native_calls_after_first = probe.backend_calls;
    const auto first_state = snapshot(interpreter, { input, parent, whole, slice });

    interpreter.prepare_signal_observation(parent);
    interpreter.schedule_signal_at(input, pattern(width, 2U), 2U, 0U);
    static_cast<void>(interpreter.run(2U));

    Outcome result;
    result.final_state = snapshot(interpreter, { input, parent, whole, slice });
    result.stop_state = first_state;
    result.backend_calls = probe.backend_calls;
    result.backend_accepts = probe.backend_accepts;
    result.generic_kernels = provider ? provider->generic_kernels() : 0U;
    result.counts = difference(region_counts(interpreter), before_first_wave);
    result.resume_delta = {
        probe.resumes[0U] - resume_baseline[0U],
        probe.resumes[1U] - resume_baseline[1U] };
    if (install_backend) {
        require(native_calls_after_first > 0U
                && difference(after_first_wave, before_first_wave).backend_runs > 0U,
            "the observation case first completes a real generic native window");
        require(probe.backend_calls == native_calls_after_first,
            "late observation demotes later windows to checked executor fallback");
        require(result.resume_delta[0U] > 0U && result.resume_delta[1U] > 0U,
            "late-observed members resume through the ordinary executor after demotion");
    }
    return result;
}

[[nodiscard]] Outcome run_logic9_case(const bool install_backend)
{
    constexpr std::uint32_t width = 65U;
    constexpr std::uint32_t slice_offset = 63U;
    constexpr std::uint32_t slice_width = 2U;
    ScopedEnvironment profile("FSIM_PROFILE_SV_WAVES", "1");
    Probe probe;
    Interpreter interpreter;
    probe.scheduler = &interpreter.scheduler();
    const auto initial_input = logic9_pattern(width, 0U);
    const auto initial_parent = PackedLogic4::from_logic9_msb_string(
        std::string(width, '0'));
    const auto initial_output = logic9_pattern(width, 3U);
    const auto initial_slice = logic9_pattern(width, 5U);
    const auto input = interpreter.add_signal({ "generic.logic9.input",
        initial_input, ResolutionKind::none, ValueKind::logic9 });
    const auto parent = interpreter.add_signal({ "generic.logic9.parent",
        initial_parent, ResolutionKind::none, ValueKind::logic9 });
    const auto whole = interpreter.add_signal({ "generic.logic9.whole",
        initial_output, ResolutionKind::none, ValueKind::logic9 });
    const auto slice = interpreter.add_signal({ "generic.logic9.slice",
        initial_slice, ResolutionKind::none, ValueKind::logic9 });

    Process producer;
    producer.id = 0U;
    producer.name = "generic_logic9_update_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::generic;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic9 };
    producer.static_sensitivity = { { input, EdgeKind::any } };
    producer.driver_regions = { { parent, 0U, width, true } };
    producer.operations = { ReadSignal { 0U, input },
        WriteUpdate { parent, 0U }, WaitSensitivity { }, Jump { 0U } };
    const auto producer_id = interpreter.add_process(std::move(producer));
    const auto& registered_producer = interpreter.process_program(producer_id);
    interpreter.set_process_executor(producer_id,
        std::make_unique<Logic9UpdateExecutor>(probe, producer_id, input,
            parent, whole, slice, slice_offset, slice_width, 2U,
            ProcessExecutorProgramBinding {
                registered_producer, registered_producer, producer_id },
            std::vector<std::uint32_t> { width }));

    Process consumer;
    consumer.id = 1U;
    consumer.name = "generic_logic9_update_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::generic;
    consumer.register_count = 2U;
    consumer.register_value_kinds = { ValueKind::logic9, ValueKind::logic9 };
    consumer.static_sensitivity = { { parent, EdgeKind::any } };
    consumer.driver_regions = {
        { whole, 0U, width, true },
        { slice, slice_offset, slice_width, false } };
    consumer.operations = { ReadSignal { 0U, parent },
        Extract { 1U, 0U, slice_offset, slice_width },
        WriteUpdate { whole, 0U },
        WriteUpdateSlice { slice, 1U, slice_offset },
        WaitSensitivity { }, Jump { 0U } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));
    const auto& registered_consumer = interpreter.process_program(consumer_id);
    interpreter.set_process_executor(consumer_id,
        std::make_unique<Logic9UpdateExecutor>(probe, consumer_id, input,
            parent, whole, slice, slice_offset, slice_width, 4U,
            ProcessExecutorProgramBinding {
                registered_consumer, registered_consumer, consumer_id },
            std::vector<std::uint32_t> { width, slice_width }));

    std::shared_ptr<ReferenceBackendProvider> provider;
    if (install_backend) {
        provider = std::make_shared<ReferenceBackendProvider>(probe, true);
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "generic Logic9 fixture settles its initialization slot");
    const auto resume_baseline = probe.resumes;
    const auto backend_calls_baseline = probe.backend_calls;
    const auto backend_accepts_baseline = probe.backend_accepts;
    const auto count_baseline = region_counts(interpreter);
    probe.events.clear();

    const auto changed = logic9_pattern(width, 4U);
    interpreter.schedule_signal_at(input, changed, 1U, 0U);
    const auto run = interpreter.run(1U);
    require(run.status == RunStatus::completed
            || run.status == RunStatus::time_limit,
        "generic Logic9 whole and slice updates drain through Update phase");

    Outcome result;
    result.final_state = snapshot(interpreter, { input, parent, whole, slice });
    result.events = std::move(probe.events);
    result.backend_calls = probe.backend_calls - backend_calls_baseline;
    result.backend_accepts = probe.backend_accepts - backend_accepts_baseline;
    result.generic_kernels = provider ? provider->generic_kernels() : 0U;
    for (std::size_t index = 0U; index < result.resume_delta.size(); ++index) {
        result.resume_delta[index] = probe.resumes[index] - resume_baseline[index];
    }
    result.counts = difference(region_counts(interpreter), count_baseline);
    return result;
}

[[nodiscard]] Outcome run_disjoint_slice_case(
    const std::uint32_t width, const bool install_backend,
    const bool backend_accept = true,
    const bool declare_full_width_owner_regions = false)
{
    ScopedEnvironment profile("FSIM_PROFILE_SV_WAVES", "1");
    Probe probe;
    Interpreter interpreter;
    probe.scheduler = &interpreter.scheduler();
    const auto initial_input = pattern(width, 0U);
    const auto initial_output = PackedLogic4 { width, Logic4::z };
    const auto input = interpreter.add_signal({
        "generic.disjoint.input", initial_input,
        ResolutionKind::none, ValueKind::logic4 });
    const auto output = interpreter.add_signal({
        "generic.disjoint.output", initial_output,
        ResolutionKind::sv_wire, ValueKind::logic4 });
    const std::array<std::uint32_t, 2U> offsets {
        width >= 65U ? 63U : 0U,
        width == 65U ? 64U : (width >= 129U ? 127U : width - 1U),
    };
    const std::array<std::uint32_t, 2U> widths {
        width >= 129U ? 2U : 1U,
        width == 65U ? 1U : (width >= 129U ? 2U : 1U),
    };
    for (std::size_t owner_index = 0U;
         owner_index < offsets.size(); ++owner_index) {
        const auto owner = static_cast<ProcessId>(owner_index);
        Process process;
        process.id = owner;
        process.name = "generic_disjoint_slice_owner_"
            + std::to_string(owner);
        process.scheduling_domain = ProcessSchedulingDomain::generic;
        process.initialize = true;
        process.register_count = 2U;
        process.register_value_kinds = {
            ValueKind::logic4, ValueKind::logic4 };
        process.static_sensitivity = { { input, EdgeKind::any } };
        process.driver_regions = declare_full_width_owner_regions
            ? std::vector<Process::DriverRegion> { { output, 0U, width, true } }
            : std::vector<Process::DriverRegion> {
                  { output, offsets[owner_index], widths[owner_index], false } };
        process.operations = {
            ReadSignal { 0U, input },
            Extract { 1U, 0U, offsets[owner_index], widths[owner_index] },
            WriteUpdateSlice { output, 1U, offsets[owner_index] },
            WaitSensitivity { }, Jump { 0U } };
        const auto process_id = interpreter.add_process(std::move(process));
        const auto& registered = interpreter.process_program(process_id);
        interpreter.set_process_executor(process_id,
            std::make_unique<DisjointSliceExecutor>(probe, process_id,
                input, output, offsets[owner_index], widths[owner_index], 3U,
                ProcessExecutorProgramBinding {
                    registered, registered, process_id },
                std::vector<std::uint32_t> {
                    width, widths[owner_index] }));
    }

    std::shared_ptr<ReferenceBackendProvider> provider;
    if (install_backend) {
        provider = std::make_shared<ReferenceBackendProvider>(
            probe, backend_accept);
        interpreter.set_region_kernel_backend_provider(provider);
    }
    interpreter.start();
    static_cast<void>(interpreter.run(0U));
    const auto resume_baseline = probe.resumes;
    const auto count_baseline = region_counts(interpreter);
    const auto backend_calls_baseline = probe.backend_calls;
    const auto backend_accepts_baseline = probe.backend_accepts;
    probe.events.clear();
    interpreter.schedule_signal_at(input, changed_input(width), 1U, 0U);
    static_cast<void>(interpreter.run(1U));

    Outcome result;
    result.final_state = snapshot_disjoint(interpreter, input, output);
    result.events = std::move(probe.events);
    result.counts = difference(region_counts(interpreter), count_baseline);
    result.resume_delta = {
        probe.resumes[0U] - resume_baseline[0U],
        probe.resumes[1U] - resume_baseline[1U] };
    result.backend_calls = probe.backend_calls - backend_calls_baseline;
    result.backend_accepts = probe.backend_accepts - backend_accepts_baseline;
    result.generic_kernels = provider ? provider->generic_kernels() : 0U;
    return result;
}

void check_output_values(const Outcome& result, const std::uint32_t width,
    const std::uint32_t slice_offset, const std::uint32_t slice_width,
    const PackedLogic4& slice_initial)
{
    require(result.final_state.size() == 4U,
        "generic runtime snapshot includes the complete signal set");
    const auto expected_input = changed_input(width);
    const auto expected_whole = invert_value(expected_input);
    auto expected_slice = slice_initial;
    insert_slice(expected_slice, slice_value(expected_whole,
        slice_offset, slice_width), slice_offset);
    require(result.final_state[0U].current == expected_input
            && result.final_state[1U].current == expected_input
            && result.final_state[2U].current == expected_whole
            && result.final_state[3U].current == expected_slice,
        "whole generic Update and cross-word UpdateSlice publish exact Logic4 values");
    require(result.final_state[1U].raw == expected_input
            && result.final_state[2U].raw == expected_whole
            && result.final_state[3U].raw == expected_slice,
        "generic updates retain the original parent and child driver values");
}

void check_native_case(const std::uint32_t width)
{
    const auto ordinary = run_foreign_case(width, Intervention::none, false);
    const auto native = run_foreign_case(width, Intervention::none, true);
    const auto slice_width = width >= 65U
        ? 2U : std::min<std::uint32_t>(3U, width);
    const auto slice_offset = width >= 65U ? 63U : width - slice_width;
    const auto slice_initial = pattern(width, 3U);
    check_output_values(ordinary, width, slice_offset, slice_width, slice_initial);
    check_output_values(native, width, slice_offset, slice_width, slice_initial);
    require(native.final_state == ordinary.final_state,
        "generic native Update and UpdateSlice match all current/LAST/stored/raw stamps");
    require(native.backend_accepts > 0U && native.backend_calls > 0U
            && native.generic_kernels > 0U
            && native.counts.backend_runs > 0U
            && native.counts.completions > 0U
            && native.counts.publications >= 3U,
        "the native provider and generic Update route both run and publish outputs");
    require(native.resume_delta[0U] == 0U && native.resume_delta[1U] == 0U,
        "accepted native windows produce zero ordinary executor resumes");
    require(ordinary.resume_delta[0U] > 0U && ordinary.resume_delta[1U] > 0U,
        "the provider-free control executes both original member bodies");
    require(native.events == ordinary.events,
        "native and ordinary members retain the same time/delta execution order");

    const auto declined = run_foreign_case(width, Intervention::none, true, false);
    require(declined.backend_calls > 0U && declined.backend_accepts == 0U
            && declined.counts.declines > 0U
            && declined.resume_delta[0U] > 0U
            && declined.resume_delta[1U] > 0U
            && declined.final_state == ordinary.final_state,
        "a declining optional provider runs both checked original executors exactly once");
}

void check_disjoint_slice_owners()
{
    for (const auto width : { 65U, 129U }) {
        const auto ordinary = run_disjoint_slice_case(width, false);
        const auto native = run_disjoint_slice_case(width, true);
        const auto declined = run_disjoint_slice_case(width, true, false);
        const std::array<std::uint32_t, 2U> offsets {
            63U, width == 65U ? 64U : 127U };
        const std::array<std::uint32_t, 2U> widths {
            width == 65U ? 1U : 2U, width == 65U ? 1U : 2U };
        auto expected = PackedLogic4 { width, Logic4::z };
        const auto changed = changed_input(width);
        for (std::size_t owner = 0U; owner < offsets.size(); ++owner) {
            insert_slice(expected,
                slice_value(changed, offsets[owner], widths[owner]),
                offsets[owner]);
        }
        require(ordinary.final_state == native.final_state
                && native.final_state == declined.final_state
                && native.final_state[1U].current == expected
                && native.final_state[1U].stored == expected
                && native.final_state[1U].transaction.has_value()
                && native.final_state[1U].event.has_value(),
            "resolved disjoint generic writers preserve full public current/stored values");
        require(native.final_state[1U].raw.has_value()
                && native.final_state[1U].secondary_raw.has_value()
                && declined.final_state[1U].raw.has_value()
                && declined.final_state[1U].secondary_raw.has_value(),
            "each partial publication retains its original driver record");
        for (std::uint32_t bit = offsets[0U];
             bit < offsets[0U] + widths[0U]; ++bit) {
            require(native.final_state[1U].raw->get(bit) == changed.get(bit)
                    && ordinary.final_state[1U].raw->get(bit) == changed.get(bit),
                "the first writer keeps its exact original disjoint bit range");
        }
        for (std::uint32_t bit = offsets[1U];
             bit < offsets[1U] + widths[1U]; ++bit) {
            require(native.final_state[1U].secondary_raw->get(bit)
                        == changed.get(bit)
                    && ordinary.final_state[1U].secondary_raw->get(bit)
                        == changed.get(bit),
                "the second writer keeps its exact original disjoint bit range");
        }
        require(native.backend_calls > 0U && native.backend_accepts > 0U
                && native.counts.backend_runs > 0U
                && native.counts.publications >= 2U
                && native.resume_delta[0U] == 0U
                && native.resume_delta[1U] == 0U
                && native.events == ordinary.events,
            "two disjoint generic slice owners publish natively without executor resumes");
        require(declined.backend_calls > 0U && declined.backend_accepts == 0U
                && declined.counts.declines > 0U
                && declined.resume_delta[0U] > 0U
                && declined.resume_delta[1U] > 0U
                && declined.events == ordinary.events,
            "a provider decline falls back before effects and resumes both original owners");
        require(ordinary.final_state[1U].current.width() == width,
            "disjoint generic writes retain the explicit standard resolved signal width");

        const auto conservative_geometry = run_disjoint_slice_case(
            width, true, true, true);
        require(conservative_geometry.backend_accepts == 0U
                && conservative_geometry.resume_delta[0U] > 0U
                && conservative_geometry.resume_delta[1U] > 0U
                && conservative_geometry.final_state == ordinary.final_state,
            "whole-width owner declarations for partial writes stay on the checked route");
    }
}

void check_logic9_generic_updates()
{
    constexpr std::uint32_t width = 65U;
    constexpr std::uint32_t slice_offset = 63U;
    const auto ordinary = run_logic9_case(false);
    const auto native = run_logic9_case(true);
    const auto expected = logic9_pattern(width, 4U);
    auto expected_slice = logic9_pattern(width, 5U);
    expected_slice.insert_bits(expected.extract_bits(slice_offset, 2U),
        slice_offset);
    constexpr std::string_view states { "UX01ZWLH-" };
    for (const auto state : states) {
        require(expected.to_msb_string().find(state) != std::string::npos,
            "the 65-bit runtime Logic9 source covers all nine states");
    }
    require(expected.extract_bits(slice_offset, 2U).to_msb_string() == "ZW",
        "the runtime Logic9 slice crosses the word boundary with Z/W values");
    require(native.final_state == ordinary.final_state
            && native.final_state[0U].current == expected
            && native.final_state[1U].current == expected
            && native.final_state[2U].current == expected
            && native.final_state[3U].current == expected_slice
            && native.final_state[1U].raw == expected
            && native.final_state[2U].raw == expected
            && native.final_state[3U].raw == expected_slice,
        "generic Logic9 whole and cross-word slice publications match every role");
    require(native.backend_calls > 0U && native.backend_accepts > 0U
            && native.generic_kernels > 0U
            && native.counts.backend_runs > 0U
            && native.counts.completions > 0U
            && native.counts.publications >= 3U
            && native.resume_delta[0U] == 0U
            && native.resume_delta[1U] == 0U
            && native.events == ordinary.events,
        "wide generic Logic9 updates use the accepted route without executor replay");
    require(ordinary.resume_delta[0U] > 0U && ordinary.resume_delta[1U] > 0U,
        "the Logic9 control executes both original checked member bodies");
}

void check_foreign_cut(const Intervention intervention)
{
    const auto ordinary = run_foreign_case(65U, intervention, false);
    const auto native = run_foreign_case(65U, intervention, true);
    require(native.events == ordinary.events,
        "foreign scheduler work keeps its original key between producer and consumer");
    require(native.events.size() == 3U
            && native.events[0U].kind == Event::Kind::member
            && native.events[0U].process == 0U
            && native.events[1U].kind == Event::Kind::foreign
            && native.events[2U].kind == Event::Kind::member
            && native.events[2U].process == 1U
            && native.events[0U].time == 1U
            && native.events[1U].time == 1U
            && native.events[2U].time == 1U
            && native.events[0U].delta == native.events[1U].delta
            && native.events[2U].delta > native.events[1U].delta,
        "the foreign callback is an exact same-time scheduler cut between the original keys");
    if (intervention == Intervention::stop
        || intervention == Intervention::exception) {
        require(native.stop_state == ordinary.stop_state
                && native.parent_pending_at_cut == 1U
                && ordinary.parent_pending_at_cut == 1U,
            "stop and exception retain the producer's pending generic Update suffix");
    }
    require(native.final_state == ordinary.final_state,
        "stop/resume and exception/resume preserve full update metadata and owner values");
    require(native.backend_calls == 2U && native.backend_accepts == 2U
            && native.counts.backend_runs == 2U
            && native.resume_delta[0U] == 0U
            && native.resume_delta[1U] == 0U,
        "foreign cuts preserve two accepted windows without ordinary member replay");
}

void check_late_observation()
{
    const auto ordinary = run_late_observation_case(129U, false);
    const auto native = run_late_observation_case(129U, true);
    require(native.stop_state == ordinary.stop_state
            && native.final_state == ordinary.final_state,
        "late observation preserves the first native cut and later checked result");
    require(native.backend_calls > 0U && native.backend_accepts > 0U,
        "late-observation control proves native entry before materialization");
}

#if defined(FSIM_RUNTIME_GENERIC_UPDATE_ALLOCATION_FAILURE_TESTS)

struct AllocationFailureChainSignals {
    SignalId input { };
    SignalId stable_input { };
    SignalId parent { };
    SignalId whole { };
    SignalId slice { };
};

class SchedulerScratchWarmBatch final : public SchedulerBatchTask {
public:
    SchedulerBatchResult execute(Scheduler&,
        const std::span<const std::uint64_t> payloads) override
    {
        ++executions;
        return { payloads.size(), { } };
    }

    std::size_t executions { };
    bool fallback_called { };
};

struct RuntimeFailureCut {
    struct ValueFingerprint {
        std::size_t width { };
        bool logic9 { };
        std::uint64_t bits_hash { };

        friend bool operator==(const ValueFingerprint&,
            const ValueFingerprint&) = default;
    };

    struct SignalFingerprint {
        ValueFingerprint current;
        ValueFingerprint last;
        ValueFingerprint stored;
        ValueFingerprint raw;
        ValueFingerprint secondary_raw;
        bool raw_valid { };
        bool secondary_raw_valid { };
        std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
        std::optional<std::pair<SimulationTick, std::uint64_t>> event;

        friend bool operator==(const SignalFingerprint&,
            const SignalFingerprint&) = default;
    };

    struct PendingUpdateFingerprint {
        SignalId signal { };
        ProcessId driver { };
        std::uint32_t offset { };
        std::uint32_t packed_value { };
        Logic4Word word;
        std::uint8_t flags { };

        friend bool operator==(const PendingUpdateFingerprint&,
            const PendingUpdateFingerprint&) = default;
    };

    std::array<SignalFingerprint, 5U> signals { };
    std::vector<PendingUpdateFingerprint> pending_updates;
    std::vector<ValueFingerprint> pending_values;
    std::vector<std::uint8_t> materialization_pending;
    std::array<std::uint64_t, 2U> static_masks { };
    std::array<InstructionIndex, 2U> pcs { };
    std::array<ProcessStatus, 2U> statuses { };
    std::array<bool, 2U> queued { };
    std::array<bool, 2U> waiting_static { };
    std::array<bool, 2U> waiting_signal { };
    std::array<bool, 2U> completion_boundary_validated { };
};

struct AllocationFailureRun {
    std::vector<SignalSnapshot> first_state;
    std::array<RuntimeFailureCut::SignalFingerprint, 5U>
        same_input_retry_state { };
    std::vector<SignalSnapshot> final_state;
    std::vector<Event> first_events;
    std::vector<Event> same_input_retry_events;
    std::vector<Event> final_events;
    Counts first_counts;
    Counts retry_counts;
    Counts later_counts;
    std::array<std::size_t, 2U> first_resumes { };
    std::array<std::size_t, 2U> retry_resumes { };
    std::size_t first_backend_calls { };
    std::size_t first_backend_accepts { };
    std::size_t retry_backend_calls { };
    std::size_t retry_backend_accepts { };
    std::size_t later_backend_calls { };
    std::size_t later_backend_accepts { };
    std::array<std::size_t, 2U> later_resumes { };
    std::size_t generic_kernels { };
    GenericFrontierSnapshot failed_frontier;
    GenericFrontierSnapshot retry_frontier;
    RuntimeFailureCut failure_cut_before_run;
    RuntimeFailureCut failure_cut_at_throw;
    RuntimeFailureCut failure_cut_after_throw;
    bool arm_task_ran { };
    bool workspace_was_cold_at_arm { };
    bool allocation_was_injected { };
    bool propagated_bad_alloc { };
    bool frontier_observer_called { };
    bool workspace_was_cold_after_failure { };
    bool workspace_exists_after_retry { };
    bool same_backend_entry_after_retry { };
};

[[nodiscard]] RuntimeFailureCut::ValueFingerprint fingerprint_value(
    const PackedLogic4& value) noexcept
{
    auto hash = UINT64_C(1469598103934665603);
    hash ^= static_cast<std::uint64_t>(value.width());
    hash *= UINT64_C(1099511628211);
    hash ^= value.is_logic9() ? 1U : 0U;
    hash *= UINT64_C(1099511628211);
    for (std::size_t bit = 0U; bit < value.width(); ++bit) {
        hash ^= static_cast<std::uint8_t>(value.get(bit));
        hash *= UINT64_C(1099511628211);
    }
    return { value.width(), value.is_logic9(), hash };
}

[[nodiscard]] std::array<RuntimeFailureCut::SignalFingerprint, 5U>
capture_signal_fingerprints(Interpreter& interpreter,
    const AllocationFailureChainSignals& signals) noexcept
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const std::array signal_ids { signals.input, signals.stable_input,
        signals.parent, signals.whole, signals.slice };
    std::array<RuntimeFailureCut::SignalFingerprint, 5U> result { };
    for (std::size_t index = 0U; index < signal_ids.size(); ++index) {
        const auto signal = signal_ids[index];
        auto& fingerprint = result[index];
        fingerprint.current
            = fingerprint_value(impl.signals[signal].initial_value);
        fingerprint.last
            = fingerprint_value(impl.signal_last_values[signal]);
        fingerprint.stored
            = fingerprint_value(impl.driven_values[signal]);
        fingerprint.transaction = impl.signal_transactions[signal];
        fingerprint.event = impl.signal_events[signal];
        if (index == 2U) {
            fingerprint.raw = fingerprint_value(
                impl.underlying_driver_value(0U, signal));
            fingerprint.raw_valid = true;
        } else if (index >= 3U) {
            fingerprint.raw = fingerprint_value(
                impl.underlying_driver_value(1U, signal));
            fingerprint.raw_valid = true;
        }
    }
    return result;
}

[[nodiscard]] bool same_signal_fingerprints(
    const std::span<const SignalSnapshot> snapshots,
    const std::span<const RuntimeFailureCut::SignalFingerprint> fingerprints)
    noexcept
{
    if (snapshots.size() != fingerprints.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < snapshots.size(); ++index) {
        const auto& snapshot = snapshots[index];
        const auto& fingerprint = fingerprints[index];
        if (fingerprint_value(snapshot.current) != fingerprint.current
            || fingerprint_value(snapshot.last) != fingerprint.last
            || fingerprint_value(snapshot.stored) != fingerprint.stored
            || snapshot.raw.has_value() != fingerprint.raw_valid
            || snapshot.secondary_raw.has_value()
                != fingerprint.secondary_raw_valid
            || (snapshot.raw
                && fingerprint_value(*snapshot.raw) != fingerprint.raw)
            || (snapshot.secondary_raw
                && fingerprint_value(*snapshot.secondary_raw)
                    != fingerprint.secondary_raw)
            || snapshot.transaction != fingerprint.transaction
            || snapshot.event != fingerprint.event) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<SignalSnapshot> snapshot_allocation_failure_chain(
    Interpreter& interpreter, const AllocationFailureChainSignals& signals)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto snapshot_signal = [&](const SignalId signal,
                                     const std::optional<ProcessId> owner) {
        SignalSnapshot value { impl.signals.at(signal).initial_value,
            impl.signal_last_values.at(signal), impl.driven_values.at(signal),
            std::nullopt, std::nullopt,
            impl.signal_transactions.at(signal), impl.signal_events.at(signal) };
        if (owner) {
            value.raw = impl.underlying_driver_value(*owner, signal);
        }
        return value;
    };
    std::vector<SignalSnapshot> result;
    result.reserve(5U);
    result.push_back(snapshot_signal(signals.input, std::nullopt));
    result.push_back(snapshot_signal(signals.stable_input, std::nullopt));
    result.push_back(snapshot_signal(signals.parent, ProcessId { 0U }));
    result.push_back(snapshot_signal(signals.whole, ProcessId { 1U }));
    result.push_back(snapshot_signal(signals.slice, ProcessId { 1U }));
    return result;
}

[[nodiscard]] RuntimeFailureCut capture_runtime_failure_cut(
    Interpreter& interpreter, const AllocationFailureChainSignals& signals)
{
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    RuntimeFailureCut result;
    result.signals = capture_signal_fingerprints(interpreter, signals);
    result.pending_updates.reserve(impl.pending_updates.size());
    for (const auto& update : impl.pending_updates) {
        result.pending_updates.push_back({ update.signal, update.driver,
            update.offset, update.packed_value, update.word, update.flags });
    }
    result.pending_values.reserve(impl.pending_update_values.size());
    for (const auto& value : impl.pending_update_values) {
        result.pending_values.push_back(fingerprint_value(value));
    }
    result.materialization_pending
        = impl.direct_signal_materialization_pending;
    for (std::size_t index = 0U; index < result.queued.size(); ++index) {
        const auto& process = impl.processes[index];
        result.static_masks[index] = process.static_trigger_mask;
        result.pcs[index] = process.pc;
        result.statuses[index] = process.status;
        result.queued[index] = process.queued;
        result.waiting_static[index] = process.waiting_on_static;
        result.waiting_signal[index] = process.waiting_on_signal;
        result.completion_boundary_validated[index]
            = process.region_kernel_completion_boundary_validated;
    }
    return result;
}

[[nodiscard]] bool same_runtime_failure_cut(
    const RuntimeFailureCut& lhs, const RuntimeFailureCut& rhs) noexcept
{
    return lhs.signals == rhs.signals
        && lhs.pending_updates == rhs.pending_updates
        && lhs.pending_values == rhs.pending_values
        && lhs.materialization_pending == rhs.materialization_pending
        && lhs.static_masks == rhs.static_masks
        && lhs.pcs == rhs.pcs
        && lhs.statuses == rhs.statuses
        && lhs.queued == rhs.queued
        && lhs.waiting_static == rhs.waiting_static
        && lhs.waiting_signal == rhs.waiting_signal
        && lhs.completion_boundary_validated
            == rhs.completion_boundary_validated;
}

[[nodiscard]] bool same_output_publication_state(
    const RuntimeFailureCut& lhs, const RuntimeFailureCut& rhs) noexcept
{
    return std::ranges::equal(
               std::span { lhs.signals }.subspan(2U),
               std::span { rhs.signals }.subspan(2U))
        && lhs.pending_updates == rhs.pending_updates
        && lhs.pending_values == rhs.pending_values
        && lhs.materialization_pending == rhs.materialization_pending;
}

struct FailureObservationContext {
    Probe* probe { };
    Interpreter* interpreter { };
    const AllocationFailureChainSignals* signals { };
    AllocationFailureRun* result { };
};

void observe_generic_preflight_failure(void* const opaque) noexcept
{
    auto& context = *static_cast<FailureObservationContext*>(opaque);
    if (context.probe == nullptr || context.interpreter == nullptr
        || context.signals == nullptr || context.result == nullptr) {
        return;
    }
    context.probe->failed_frontier
        = capture_generic_frontier(*context.probe->scheduler);
    context.result->failed_frontier = context.probe->failed_frontier;
    try {
        context.result->failure_cut_at_throw = capture_runtime_failure_cut(
            *context.interpreter, *context.signals);
        context.result->frontier_observer_called = true;
    } catch (...) {
        // The observer runs after the selected allocation is disarmed. Keep
        // the allocation failure as the test's primary exception if the
        // passive snapshot itself cannot be copied.
    }
}

[[nodiscard]] AllocationFailureChainSignals build_allocation_failure_chain(
    Interpreter& interpreter, Probe& probe)
{
    probe.scheduler = &interpreter.scheduler();
    const PackedLogic4 zero { 1U, Logic4::zero };
    const PackedLogic4 one { 1U, Logic4::one };
    AllocationFailureChainSignals signals {
        interpreter.add_signal({ "generic.allocation.input", zero }),
        interpreter.add_signal({ "generic.allocation.stable", zero }),
        interpreter.add_signal({ "generic.allocation.parent", zero }),
        interpreter.add_signal({ "generic.allocation.whole", one }),
        interpreter.add_signal({ "generic.allocation.slice", one })
    };

    Process producer;
    producer.id = 0U;
    producer.name = "generic_allocation_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::generic;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { signals.input, EdgeKind::any } };
    producer.driver_regions = { { signals.parent, 0U, 1U, true } };
    producer.operations = { ReadSignal { 0U, signals.stable_input },
        WriteUpdate { signals.parent, 0U }, WaitSensitivity { }, Jump { 0U } };
    const auto producer_id = interpreter.add_process(std::move(producer));
    const auto& producer_program = interpreter.process_program(producer_id);
    interpreter.set_process_executor(producer_id,
        std::make_unique<UpdateExecutor>(probe, producer_id,
            signals.stable_input, signals.parent, signals.slice, 0U, 1U, 2U,
            ProcessExecutorProgramBinding {
                producer_program, producer_program, producer_id },
            std::vector<std::uint32_t> { 1U }));

    Process consumer;
    consumer.id = 1U;
    consumer.name = "generic_allocation_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::generic;
    consumer.register_count = 3U;
    consumer.register_value_kinds = { ValueKind::logic4,
        ValueKind::logic4, ValueKind::logic4 };
    consumer.static_sensitivity = { { signals.parent, EdgeKind::any } };
    consumer.driver_regions = { { signals.whole, 0U, 1U, true },
        { signals.slice, 0U, 1U, false } };
    consumer.operations = { ReadSignal { 0U, signals.parent },
        UnaryNot { 1U, 0U }, Extract { 2U, 1U, 0U, 1U },
        WriteUpdate { signals.whole, 1U },
        WriteUpdateSlice { signals.slice, 2U, 0U }, WaitSensitivity { },
        Jump { 0U } };
    const auto consumer_id = interpreter.add_process(std::move(consumer));
    const auto& consumer_program = interpreter.process_program(consumer_id);
    interpreter.set_process_executor(consumer_id,
        std::make_unique<UpdateExecutor>(probe, consumer_id, signals.parent,
            signals.whole, signals.slice, 0U, 1U, 5U,
            ProcessExecutorProgramBinding {
                consumer_program, consumer_program, consumer_id },
            std::vector<std::uint32_t> { 1U, 1U, 1U }));
    return signals;
}

[[nodiscard]] AllocationFailureRun run_allocation_failure_chain(
    const bool install_backend, const bool inject_failure)
{
    Probe probe;
    Interpreter interpreter;
    const auto signals = build_allocation_failure_chain(interpreter, probe);
    std::shared_ptr<ReferenceBackendProvider> provider;
    if (install_backend) {
        provider = std::make_shared<ReferenceBackendProvider>(probe, true);
        interpreter.set_region_kernel_backend_provider(provider);
    }

    SchedulerScratchWarmBatch warm_batch;
    interpreter.start();
    interpreter.scheduler().schedule_next_delta_batchable(
        SchedulerPhase::active,
        std::numeric_limits<StableOrder>::max(), warm_batch, 0U,
        [&](Scheduler&) { warm_batch.fallback_called = true; });
    const auto startup = interpreter.run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "generic allocation fixture settles its connected-chain startup");
    require(warm_batch.executions == 1U && !warm_batch.fallback_called,
        "a no-effect startup batch prepares scheduler scratch before injection");
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    const auto component = impl.region_component_by_process.at(0U);
    const auto& certificate = impl.region_graph->certificate_inventory()
                                  .components.at(component);
    require(component == impl.region_component_by_process.at(1U)
            && certificate.members.size() == 2U
            && std::ranges::find(certificate.members, 0U)
                != certificate.members.end()
            && std::ranges::find(certificate.members, 1U)
                != certificate.members.end()
            && std::ranges::find(
                   certificate.structural_internal_signal_candidates,
                   signals.parent)
                != certificate.structural_internal_signal_candidates.end(),
        "allocation witness uses a real connected two-member graph");
    using BackendEntryPointer = std::ranges::range_value_t<
        decltype(impl.region_kernel_backends_by_component)>;
    const BackendEntryPointer backend_entry
        = component < impl.region_kernel_backends_by_component.size()
        ? impl.region_kernel_backends_by_component[component]
        : BackendEntryPointer { };
    require(!provider || provider->generic_kernels() > 0U,
        "provider compiles the connected generic component before allocation injection");
    require(!install_backend || (component
                < impl.region_kernel_backends_by_component.size()
            && backend_entry && backend_entry->executor
            && !backend_entry->generic_workspace),
        "the tested backend entry is cold before the first timed activation");

    const auto resume_baseline = probe.resumes;
    const auto backend_calls_baseline = probe.backend_calls;
    const auto backend_accepts_baseline = probe.backend_accepts;
    const auto first_count_baseline = region_counts(interpreter);
    probe.events.clear();

    AllocationFailureRun result;
    if (inject_failure) {
        interpreter.scheduler().schedule_at(1U, SchedulerPhase::update,
            std::numeric_limits<StableOrder>::max(),
            [&](Scheduler& update_scheduler) {
                update_scheduler.schedule_next_delta(SchedulerPhase::active,
                    0U, [&](Scheduler&) {
                        result.arm_task_ran = true;
                        result.workspace_was_cold_at_arm
                            = backend_entry
                            && !backend_entry->generic_workspace;
                        staging_failure_support::arm_allocation_failure(0U);
                    });
            });
    }
    interpreter.schedule_signal_at(signals.input,
        PackedLogic4 { 1U, Logic4::one }, 1U, 0U);

    std::optional<RunResult> first_run;
    FailureObservationContext failure_observation {
        &probe, &interpreter, &signals, &result };
    if (inject_failure) {
        result.failure_cut_before_run
            = capture_runtime_failure_cut(interpreter, signals);
        staging_failure_support::set_allocation_failure_observer(
            &failure_observation, &observe_generic_preflight_failure);
    }
    try {
        first_run.emplace(interpreter.run(1U));
    } catch (const std::bad_alloc&) {
        result.propagated_bad_alloc = true;
    } catch (...) {
        staging_failure_support::clear_allocation_failure();
        staging_failure_support::set_allocation_failure_observer(
            nullptr, nullptr);
        throw;
    }
    result.allocation_was_injected
        = staging_failure_support::allocation_failure_was_injected();
    staging_failure_support::clear_allocation_failure();
    staging_failure_support::set_allocation_failure_observer(nullptr, nullptr);
    if (inject_failure) {
        require(result.arm_task_ran && result.workspace_was_cold_at_arm
                && result.allocation_was_injected
                && result.propagated_bad_alloc
                && result.frontier_observer_called,
            "the prequeued next-delta arm injects into the cold generic workspace preflight");
        result.failure_cut_after_throw
            = capture_runtime_failure_cut(interpreter, signals);
    } else {
        require(first_run.has_value()
                && (first_run->status == RunStatus::completed
                    || first_run->status == RunStatus::time_limit),
            "the checked reference completes its first input transition");
    }
    if (!inject_failure) {
        result.first_state = snapshot_allocation_failure_chain(interpreter,
            signals);
    }
    result.first_events = probe.events;
    result.first_counts
        = difference(region_counts(interpreter), first_count_baseline);
    result.first_backend_calls
        = probe.backend_calls - backend_calls_baseline;
    result.first_backend_accepts
        = probe.backend_accepts - backend_accepts_baseline;
    for (std::size_t process = 0U; process < result.first_resumes.size();
         ++process) {
        result.first_resumes[process]
            = probe.resumes[process] - resume_baseline[process];
    }
    const auto entry_after_failure
        = component < impl.region_kernel_backends_by_component.size()
        ? impl.region_kernel_backends_by_component[component]
        : BackendEntryPointer { };
    result.workspace_was_cold_after_failure
        = entry_after_failure && !entry_after_failure->generic_workspace;

    if (inject_failure) {
        const auto retry_count_baseline = region_counts(interpreter);
        const auto retry_backend_calls_baseline = probe.backend_calls;
        const auto retry_backend_accepts_baseline = probe.backend_accepts;
        const auto retry_resume_baseline = probe.resumes;
        probe.capture_retry_frontier = true;
        const auto retry_run = interpreter.run(1U);
        probe.capture_retry_frontier = false;
        require(retry_run.status == RunStatus::completed
                || retry_run.status == RunStatus::time_limit,
            "the same interpreter retries the retained input without rescheduling");
        result.same_input_retry_state
            = capture_signal_fingerprints(interpreter, signals);
        result.same_input_retry_events = probe.events;
        result.retry_counts
            = difference(region_counts(interpreter), retry_count_baseline);
        result.retry_backend_calls
            = probe.backend_calls - retry_backend_calls_baseline;
        result.retry_backend_accepts
            = probe.backend_accepts - retry_backend_accepts_baseline;
        for (std::size_t process = 0U;
             process < result.retry_resumes.size(); ++process) {
            result.retry_resumes[process]
                = probe.resumes[process] - retry_resume_baseline[process];
        }
        result.retry_frontier = probe.retry_frontier;
    }

    const auto later_count_baseline = region_counts(interpreter);
    const auto later_backend_calls_baseline = probe.backend_calls;
    const auto later_backend_accepts_baseline = probe.backend_accepts;
    const auto later_resume_baseline = probe.resumes;
    interpreter.schedule_signal_at(signals.input,
        PackedLogic4 { 1U, Logic4::x }, 2U, 0U);
    const auto later_run = interpreter.run();
    require(later_run.status == RunStatus::completed
            || later_run.status == RunStatus::time_limit,
        "the later X input transition completes after same-key recovery");

    result.final_state = snapshot_allocation_failure_chain(interpreter,
        signals);
    result.final_events = probe.events;
    result.later_counts = difference(
        region_counts(interpreter), later_count_baseline);
    result.later_backend_calls
        = probe.backend_calls - later_backend_calls_baseline;
    result.later_backend_accepts
        = probe.backend_accepts - later_backend_accepts_baseline;
    for (std::size_t process = 0U; process < result.later_resumes.size();
         ++process) {
        result.later_resumes[process]
            = probe.resumes[process] - later_resume_baseline[process];
    }
    result.generic_kernels = provider ? provider->generic_kernels() : 0U;
    const auto entry_after_retry
        = component < impl.region_kernel_backends_by_component.size()
        ? impl.region_kernel_backends_by_component[component]
        : BackendEntryPointer { };
    result.workspace_exists_after_retry
        = entry_after_retry && static_cast<bool>(entry_after_retry->generic_workspace);
    result.same_backend_entry_after_retry
        = backend_entry && entry_after_retry.get() == backend_entry.get();
    return result;
}

void check_generic_update_allocation_failure_retries_exactly()
{
    ScopedEnvironment profile("FSIM_PROFILE_SV_WAVES", "1");
    const auto checked = run_allocation_failure_chain(false, false);
    const auto retried = run_allocation_failure_chain(true, true);

    require(retried.propagated_bad_alloc && retried.allocation_was_injected
            && retried.frontier_observer_called,
        "the injected cold-workspace bad_alloc escapes through the scheduler error channel");
    require(same_runtime_failure_cut(retried.failure_cut_at_throw,
                retried.failure_cut_after_throw)
            && same_output_publication_state(
                retried.failure_cut_before_run,
                retried.failure_cut_at_throw),
        "failed preflight preserves output roles, stamps, pending vectors, and process state");
    require(retried.failed_frontier.valid
            && retried.failed_frontier.time == 1U
            && retried.failed_frontier.phase == SchedulerPhase::active
            && retried.failed_frontier.cursor == 0U
            && retried.failed_frontier.end == 1U
            && retried.failed_frontier.task_count == 1U
            && retried.failure_cut_after_throw.queued[0U]
            && !retried.failure_cut_after_throw.queued[1U],
        "the failed native attempt retains the original queued producer task");
    require(retried.first_counts.attempts == 1U
            && retried.first_counts.failures == 1U
            && retried.first_counts.declines == 0U
            && retried.first_counts.backend_runs == 0U
            && retried.first_counts.completions == 0U
            && retried.first_counts.members == 0U
            && retried.first_counts.publications == 0U,
        "the injected cold-workspace allocation is one failure without a checked decline");
    require(retried.first_backend_calls == 0U
            && retried.first_backend_accepts == 0U
            && retried.first_resumes[0U] == 0U
            && retried.first_resumes[1U] == 0U
            && retried.first_events.empty()
            && retried.workspace_was_cold_after_failure,
        "failed preflight executes no backend or ordinary process body and publishes nothing");
    require(same_generic_task_keys(retried.failed_frontier,
                retried.retry_frontier),
        "scheduler retry reoffers the exact original stable-order/sequence/payload key");
    require(same_signal_fingerprints(checked.first_state,
                retried.same_input_retry_state)
            && retried.same_input_retry_events == checked.first_events,
        "retrying without rescheduling applies the retained input exactly once with checked semantics");
    require(retried.retry_backend_calls == 1U
            && retried.retry_backend_accepts == 1U
            && retried.generic_kernels > 0U
            && retried.retry_counts.attempts == 1U
            && retried.retry_counts.backend_runs == 1U
            && retried.retry_counts.completions == 1U
            && retried.retry_counts.members == 1U
            && retried.retry_counts.publications == 1U
            && retried.retry_counts.declines == 0U
            && retried.retry_counts.failures == 0U
            && retried.retry_resumes[0U] == 0U
            && retried.retry_resumes[1U] == 0U
            && retried.workspace_exists_after_retry
            && retried.same_backend_entry_after_retry,
        "the same cold-entry route succeeds natively on same-key retry for one selected chain member");
    require(retried.later_counts.attempts == 1U
            && retried.later_counts.backend_runs == 1U
            && retried.later_counts.completions == 1U
            && retried.later_counts.members == 1U
            && retried.later_counts.publications == 1U
            && retried.later_counts.declines == 0U
            && retried.later_counts.failures == 0U
            && retried.later_backend_calls == 1U
            && retried.later_backend_accepts == 1U
            && retried.later_resumes[0U] == 0U
            && retried.later_resumes[1U] == 0U,
        "the subsequent X stimulus also reaches the warmed native route without ordinary resumes");
    require(retried.final_state == checked.final_state
            && retried.final_events == checked.final_events,
        "the later X transition preserves complete checked order and emits no duplicate owner transaction");
}

#endif

} // namespace

void test_generic_update_region_runtime()
{
    for (const auto width : { 3U, 9U, 65U, 129U }) {
        check_native_case(width);
    }
    check_foreign_cut(Intervention::stop);
    check_foreign_cut(Intervention::exception);
    check_late_observation();
    check_disjoint_slice_owners();
    check_logic9_generic_updates();
}

#if defined(FSIM_RUNTIME_GENERIC_UPDATE_ALLOCATION_FAILURE_TESTS)
void test_generic_update_region_allocation_failure()
{
    check_generic_update_allocation_failure_retries_exactly();
}
#endif

} // namespace fsim::tests::runtime
