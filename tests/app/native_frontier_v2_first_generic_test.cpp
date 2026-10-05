// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"

#include "../../src/app/application_internal.hpp"
#include "../../src/app/application_region_kernel_backend.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "../../src/runtime/simir_region_frontier_trusted_entry.hpp"
#include "../runtime/runtime_owned_driver_demotion_test_access.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::tests::app::frontier_v2_first {
namespace {

using namespace fsim::compiler;
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
    explicit ScopedEnvironment(const char* const name)
        : name_(name)
    {
        if (const auto* const previous = std::getenv(name_); previous != nullptr) {
            previous_ = previous;
        }
#if defined(_WIN32)
        if (::_putenv_s(name_, "") != 0) {
#else
        if (::unsetenv(name_) != 0) {
#endif
            throw std::runtime_error { "failed to clear region test policy" };
        }
    }

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
            throw std::runtime_error { "failed to set region test policy" };
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    ~ScopedEnvironment()
    {
#if defined(_WIN32)
        static_cast<void>(::_putenv_s(
            name_, previous_ ? previous_->c_str() : ""));
#else
        if (previous_) {
            static_cast<void>(::setenv(name_, previous_->c_str(), 1));
        } else {
            static_cast<void>(::unsetenv(name_));
        }
#endif
    }

private:
    const char* name_ { };
    std::optional<std::string> previous_;
};

struct ProviderCounters final {
    // Count factory calls separately from successful V1 backend returns.
    std::atomic<std::size_t> activation_creates { };
    std::atomic<std::size_t> activation_backend_creates { };
    std::atomic<std::size_t> frontier_creates { };
    std::atomic<std::size_t> checked_resumes { };
    std::atomic<std::size_t> frontier_entries { };
    std::atomic<std::size_t> frontier_delegate_calls { };
    std::atomic<std::size_t> forced_frontier_declines { };
    std::atomic<std::uint32_t> last_frontier_status { };
    std::atomic<std::uint32_t> last_frontier_cursor { };
    std::atomic<std::uint32_t> last_frontier_pending_writes { };
    std::atomic<std::uint32_t> last_frontier_staged_events { };
    std::atomic<std::uint32_t> last_frontier_ack_count { };
};

struct FrontierGate final {
    std::shared_ptr<ProviderCounters> counters;
    std::atomic<bool> decline_next_entry { };
    RegionFrontierStepEntryV2 delegate { };
};

thread_local FrontierGate* active_frontier_gate { };

class CountingProcessExecutor final
    : public ProcessExecutor
    , public RegionKernelParkedExecutor {
public:
    CountingProcessExecutor(std::unique_ptr<ProcessExecutor> executor,
        std::shared_ptr<ProviderCounters> counters)
        : executor_(std::move(executor))
        , parked_(dynamic_cast<const RegionKernelParkedExecutor*>(
              executor_.get()))
        , counters_(std::move(counters))
    {
        if (!executor_ || !counters_) {
            throw std::invalid_argument { "counting executor needs its delegate" };
        }
    }

    [[nodiscard]] std::unique_ptr<ProcessExecutor> fork_clone(
        const InstructionIndex instruction) override
    {
        return std::make_unique<CountingProcessExecutor>(
            executor_->fork_clone(instruction), counters_);
    }

    void redirect(const InstructionIndex instruction) override
    {
        executor_->redirect(instruction);
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex instruction) override
    {
        counters_->checked_resumes.fetch_add(1U, std::memory_order_relaxed);
        return executor_->resume(context, instruction);
    }

    [[nodiscard]] std::size_t resume_cohort(
        std::span<ProcessCohortResumeEntry>) override
    {
        // The LLVM V1 backend uses an exact LlvmProcessExecutor cast for this
        // route. This wrapper intentionally keeps the test on the ordinary
        // per-process callback instead of forwarding foreign wrapper objects.
        return 0U;
    }

    [[nodiscard]] std::size_t resume_ordered_cohort(
        std::span<ProcessCohortResumeEntry>) override
    {
        return 0U;
    }

    [[nodiscard]] bool cohort_manages_process_state() const noexcept override
    {
        return executor_->cohort_manages_process_state();
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
        return executor_->cohort_domain();
    }

    [[nodiscard]] const ProcessExecutorProgramBinding*
    program_access_binding() const noexcept override
    {
        return executor_->program_access_binding();
    }

    [[nodiscard]] bool region_kernel_equivalent() const noexcept override
    {
        return executor_->region_kernel_equivalent();
    }

    [[nodiscard]] bool region_kernel_completion_has_no_persistent_registers()
        const noexcept override
    {
        return executor_->region_kernel_completion_has_no_persistent_registers();
    }

    [[nodiscard]] bool region_kernel_completion_is_parked_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count) const noexcept override
    {
        return parked_ != nullptr
            && parked_->region_kernel_completion_is_parked_native(process,
                wait_instruction, jump_instruction, register_bindings,
                activation_register_count);
    }

    [[nodiscard]] std::unique_ptr<PreparedRegionCompletion>
    prepare_region_completion(const ProcessId process,
        const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::span<const PackedLogic4> activation_registers) override
    {
        return executor_->prepare_region_completion(process, wait_instruction,
            jump_instruction, register_bindings, activation_registers);
    }

    [[nodiscard]] bool prepare_region_completion_native(
        const ProcessId process, const InstructionIndex wait_instruction,
        const InstructionIndex jump_instruction,
        const std::span<const RegionRegisterBinding> register_bindings,
        const std::size_t activation_register_count,
        const void** storage_identity) noexcept override
    {
        return executor_->prepare_region_completion_native(process,
            wait_instruction, jump_instruction, register_bindings,
            activation_register_count, storage_identity);
    }

    [[nodiscard]] bool stage_region_completion_native(
        const std::span<const PackedLogic4> activation_registers) noexcept override
    {
        return executor_->stage_region_completion_native(activation_registers);
    }

    void commit_region_completion_native() noexcept override
    {
        executor_->commit_region_completion_native();
    }

    void cancel_region_completion_native() noexcept override
    {
        executor_->cancel_region_completion_native();
    }

private:
    std::unique_ptr<ProcessExecutor> executor_;
    const RegionKernelParkedExecutor* parked_ { };
    std::shared_ptr<ProviderCounters> counters_;
};

class CountingActivationProvider : public RegionKernelBackendProvider {
public:
    CountingActivationProvider(
        std::shared_ptr<RegionKernelBackendProvider> delegate,
        std::shared_ptr<ProviderCounters> counters)
        : delegate_(std::move(delegate))
        , counters_(std::move(counters))
    {
        if (!delegate_ || !counters_) {
            throw std::invalid_argument { "counting provider needs its delegate" };
        }
    }

    [[nodiscard]] std::string_view identity() const noexcept override
    {
        return delegate_->identity();
    }

    [[nodiscard]] std::unique_ptr<RegionKernelBackend> create(
        const RegionConeActivationKernel& kernel) override
    {
        counters_->activation_creates.fetch_add(1U,
            std::memory_order_relaxed);
        auto backend = delegate_->create(kernel);
        if (backend) {
            counters_->activation_backend_creates.fetch_add(1U,
                std::memory_order_relaxed);
        }
        return backend;
    }

protected:
    std::shared_ptr<RegionKernelBackendProvider> delegate_;
    std::shared_ptr<ProviderCounters> counters_;
};

class ControlledFrontierBackend final : public RegionFrontierBackend {
public:
    ControlledFrontierBackend(
        std::unique_ptr<RegionFrontierBackend> delegate,
        std::shared_ptr<FrontierGate> gate)
        : delegate_(std::move(delegate))
        , gate_(std::move(gate))
    {
        if (!delegate_ || !gate_ || active_frontier_gate != nullptr) {
            throw std::invalid_argument { "frontier test gate is not exclusive" };
        }
        gate_->delegate = delegate_->step_entry();
        active_frontier_gate = gate_.get();
    }

    ~ControlledFrontierBackend() override
    {
        if (active_frontier_gate == gate_.get()) {
            active_frontier_gate = nullptr;
        }
    }

    [[nodiscard]] RegionFrontierStepEntryV2 step_entry() const noexcept override
    {
        return &entry;
    }

    [[nodiscard]] const RegionFrontierLayoutV2& layout() const noexcept override
    {
        return delegate_->layout();
    }

private:
    [[nodiscard]] static RegionFrontierStatusV2 entry(
        RegionFrontierFrameV2* const frame) noexcept
    {
        auto* const gate = active_frontier_gate;
        if (gate == nullptr || gate->delegate == nullptr) {
            return RegionFrontierStatusV2::decline_before_mutation;
        }
        if (frame == nullptr) {
            return RegionFrontierStatusV2::decline_before_mutation;
        }
        gate->counters->frontier_entries.fetch_add(1U,
            std::memory_order_relaxed);
        auto status = RegionFrontierStatusV2::decline_before_mutation;
        if (gate->decline_next_entry.exchange(false,
                std::memory_order_acq_rel)) {
            gate->counters->forced_frontier_declines.fetch_add(1U,
                std::memory_order_relaxed);
        } else {
            gate->counters->frontier_delegate_calls.fetch_add(1U,
                std::memory_order_relaxed);
            status = gate->delegate(frame);
        }
        gate->counters->last_frontier_status.store(
            static_cast<std::uint32_t>(status), std::memory_order_relaxed);
        gate->counters->last_frontier_cursor.store(
            frame->scheduler_task_cursor, std::memory_order_relaxed);
        gate->counters->last_frontier_pending_writes.store(
            frame->pending_write_count, std::memory_order_relaxed);
        gate->counters->last_frontier_staged_events.store(
            frame->staged_event_count, std::memory_order_relaxed);
        gate->counters->last_frontier_ack_count.store(
            frame->generic_update_ack_count, std::memory_order_relaxed);
        return status;
    }

    std::unique_ptr<RegionFrontierBackend> delegate_;
    std::shared_ptr<FrontierGate> gate_;
};

class CountingFrontierProvider final : public CountingActivationProvider
    , public RegionFrontierBackendProvider {
public:
    CountingFrontierProvider(
        std::shared_ptr<RegionKernelBackendProvider> delegate,
        std::shared_ptr<ProviderCounters> counters,
        std::shared_ptr<FrontierGate> gate,
        const bool decline_creation)
        : CountingActivationProvider(std::move(delegate), counters)
        , gate_(std::move(gate))
        , decline_creation_(decline_creation)
    {
        if (!gate_) {
            throw std::invalid_argument { "frontier test gate is missing" };
        }
    }

    [[nodiscard]] std::unique_ptr<RegionFrontierBackend>
    create_frontier(const RegionConeActivationKernel& kernel) override
    {
        counters_->frontier_creates.fetch_add(1U,
            std::memory_order_relaxed);
        if (decline_creation_) {
            return { };
        }
        auto* const frontier_provider
            = dynamic_cast<RegionFrontierBackendProvider*>(delegate_.get());
        if (frontier_provider == nullptr) {
            return { };
        }
        auto backend = frontier_provider->create_frontier(kernel);
        if (!backend) {
            return { };
        }
        return std::make_unique<ControlledFrontierBackend>(
            std::move(backend), gate_);
    }

private:
    std::shared_ptr<FrontierGate> gate_;
    bool decline_creation_ { };
};

enum class Route : std::uint8_t {
    generic_update,
    vhdl_projected,
    systemverilog_active,
};

enum class FrontierProviderMode : std::uint8_t {
    absent,
    available,
    decline_creation,
};

struct RuntimeShape final {
    std::size_t component { };
    bool activation_program_present { };
    bool v1_backend_present { };
    bool v1_generic_workspace_present { };
    bool frontier_runtime_present { };
    bool frontier_trusted_capability_present { };
    bool alias_certificate_storage_available { };
    bool alias_certificate_buffers_empty { };
    std::uint64_t alias_checked_entries { };
    std::uint64_t alias_trusted_entries { };
    bool generic_retirement_ack_valid { };
    std::uint32_t generic_pending_writes { };
    std::uint32_t generic_staged_events { };
    std::uint32_t generic_ack_count { };
};

struct Harness final {
    Harness(const Route route, const FrontierProviderMode frontier_mode,
        const bool local_wave_enabled, const bool install_execution_hook,
        const JitOptimizationLevel optimization)
        : profile_environment("FSIM_PROFILE_SV_WAVES")
        , region_environment("FSIM_ENABLE_SV_REGION_KERNEL", "1")
        , local_wave_environment("FSIM_ENABLE_SV_LOCAL_WAVE",
              local_wave_enabled ? "1" : "0")
        , options(make_options(optimization))
        , signal_widths { 1U, 1U }
        , signal_kinds { ValueKind::logic4, ValueKind::logic4 }
        , signal_resolutions {
              ResolutionKind::none,
              route == Route::systemverilog_active
                  ? ResolutionKind::sv_wire : ResolutionKind::none }
        , jit(std::make_unique<LlvmJit>(options))
        , counters(std::make_shared<ProviderCounters>())
        , gate(std::make_shared<FrontierGate>())
        , interpreter(std::make_unique<Interpreter>())
    {
        gate->counters = counters;
        auto production_provider = ::fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(options,
                "native-frontier-v2-first-generic-regression-v1");
        if (!production_provider) {
            throw std::runtime_error { "LLVM activation provider unavailable" };
        }
        if (frontier_mode == FrontierProviderMode::absent) {
            provider = std::make_shared<CountingActivationProvider>(
                production_provider, counters);
        } else {
            provider = std::make_shared<CountingFrontierProvider>(
                production_provider, counters, gate,
                frontier_mode == FrontierProviderMode::decline_creation);
        }
        interpreter->set_region_kernel_backend_provider(provider);

        input = interpreter->add_signal({ "v2_first.input",
            PackedLogic4 { 1U, Logic4::zero } });
        output = interpreter->add_signal({ "v2_first.output",
            PackedLogic4 { 1U, route == Route::systemverilog_active
                    ? Logic4::z : Logic4::x },
            signal_resolutions[1U] });

        Process process;
        process.id = 0U;
        process.name = route == Route::generic_update
            ? "v2_first_generic_update"
            : route == Route::vhdl_projected
                ? "v2_first_vhdl_projected"
                : "v2_first_systemverilog_active";
        process.scheduling_domain = route == Route::systemverilog_active
            ? ProcessSchedulingDomain::systemverilog
            : ProcessSchedulingDomain::generic;
        if (route == Route::vhdl_projected) {
            process.language_standard = "vhdl-2008";
        }
        process.register_count = 2U;
        process.register_value_kinds = { ValueKind::logic4, ValueKind::logic4 };
        process.static_sensitivity = { { input, EdgeKind::any } };
        process.driver_regions = { { output, 0U, 1U, true } };
        process.operations.push_back(ReadSignal { 0U, input });
        process.operations.push_back(UnaryNot { 1U, 0U });
        if (route == Route::generic_update) {
            process.operations.push_back(WriteUpdate {
                output, 1U, SignalUpdateDomain::generic });
        } else if (route == Route::vhdl_projected) {
            process.operations.push_back(WriteProjected {
                output, 1U, 0U, 0U, ProjectedDelayMode::inertial });
        } else {
            process.operations.push_back(WriteUpdate {
                output, 1U, SignalUpdateDomain::systemverilog_active });
        }
        process.operations.push_back(WaitSensitivity { });
        process.operations.push_back(Jump { 0U });
        registered_process = process;
        process_id = interpreter->add_process(std::move(process));
        if (process_id != registered_process.id) {
            throw std::logic_error { "test process id changed unexpectedly" };
        }

        jit->add_process(registered_process.name, registered_process,
            signal_widths, signal_kinds);
        const auto handle = jit->lookup(registered_process.name);
        if (!handle) {
            throw std::runtime_error { "LLVM did not retain test process" };
        }
        auto executor = std::make_unique<::fsim::app::application_detail::
            LlvmProcessExecutor>(*jit, handle, registered_process,
                signal_widths, signal_kinds, signal_resolutions,
                std::shared_ptr<const ProcessSignalRemap> { }, process_id);
        if (!executor->program_access_binding()->valid()
            || !executor->region_kernel_equivalent()
            || !executor->cohort_manages_process_state()
            || !executor->region_kernel_completion_has_no_persistent_registers()) {
            throw std::runtime_error {
                "test executor does not certify the stateless whole-write body"
            };
        }
        interpreter->set_process_executor(process_id,
            std::make_unique<CountingProcessExecutor>(std::move(executor), counters));
        if (install_execution_hook) {
            interpreter->set_execution_point_hook(
                [](Scheduler&, const ExecutionPoint&) { });
        }
        interpreter->start();
        const auto startup = interpreter->run(0U);
        require(startup.status == RunStatus::completed
                || startup.status == RunStatus::time_limit,
            "test process settles its initial activation");
    }

    ~Harness()
    {
        interpreter.reset();
        provider.reset();
        counters.reset();
        gate.reset();
        jit.reset();
    }

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;

    [[nodiscard]] static LlvmJitOptions make_options(
        const JitOptimizationLevel optimization)
    {
        LlvmJitOptions result;
        result.optimization = optimization;
        result.cache_directory.clear();
        result.debug_instrumentation = false;
        result.require_direct_update_slots = false;
        result.code_coverage_identity = "disabled";
        return result;
    }

    [[nodiscard]] RuntimeShape shape() const
    {
        auto& impl = OwnedDriverDemotionTestAccess::implementation(*interpreter);
        RuntimeShape result;
        if (process_id >= impl.region_component_by_process.size()) {
            return result;
        }
        result.component = impl.region_component_by_process[process_id];
        if (result.component < impl.region_activation_programs.size()) {
            result.activation_program_present
                = impl.region_activation_programs[result.component].has_value();
        }
        if (result.component < impl.region_kernel_backends_by_component.size()) {
            const auto& entry
                = impl.region_kernel_backends_by_component[result.component];
            result.v1_backend_present = static_cast<bool>(entry);
            result.v1_generic_workspace_present
                = entry && static_cast<bool>(entry->generic_workspace);
        }
        if (result.component < impl.region_frontier_runtime_by_component.size()) {
            const auto& runtime
                = impl.region_frontier_runtime_by_component[result.component];
            result.frontier_runtime_present = runtime && runtime->backend
                && runtime->backend->executor && !runtime->invalidated;
            if (runtime) {
                const auto& frame = runtime->frame;
                const auto* const executor = runtime->backend
                    ? runtime->backend->executor.get() : nullptr;
                result.frontier_trusted_capability_present = executor != nullptr
                    && dynamic_cast<const fsim::runtime::simir::detail::
                        RegionFrontierTrustedEntryCapability*>(executor) != nullptr;
                result.alias_certificate_storage_available
                    = runtime->alias_certificate_storage_available;
                result.alias_certificate_buffers_empty
                    = runtime->alias_certificate_ranges.empty()
                    && runtime->alias_candidate_ranges.empty()
                    && runtime->alias_certificate_ranges.capacity() == 0U
                    && runtime->alias_candidate_ranges.capacity() == 0U;
                result.alias_checked_entries = runtime->alias_checked_entries;
                result.alias_trusted_entries = runtime->alias_trusted_entries;
                result.generic_pending_writes = frame.pending_write_count;
                result.generic_staged_events = frame.staged_event_count;
                result.generic_ack_count = frame.generic_update_ack_count;
                result.generic_retirement_ack_valid
                    = region_frontier_generic_retirement_ack_valid_v2(
                        frame.pending_write_count, frame.staged_event_count,
                        frame.generic_update_ack_count);
            }
        }
        return result;
    }

    void schedule_one_input()
    {
        interpreter->schedule_signal_at(input,
            PackedLogic4 { 1U, Logic4::one }, 1U, 0U);
    }

    void run_one_input()
    {
        const auto result = interpreter->run(1U);
        require(result.status == RunStatus::completed
                || result.status == RunStatus::time_limit,
            "test input activation completes");
        require(interpreter->signal_value(output).get(0U) == Logic4::zero,
            "the output reflects the whole-signal inversion");
    }

    ScopedEnvironment profile_environment;
    ScopedEnvironment region_environment;
    ScopedEnvironment local_wave_environment;
    LlvmJitOptions options;
    // Executors keep non-owning spans to this metadata; declare it before
    // both interpreter and JIT so it outlives their destruction.
    const std::vector<std::uint32_t> signal_widths;
    const std::vector<ValueKind> signal_kinds;
    const std::vector<ResolutionKind> signal_resolutions;
    // LlvmProcessExecutor retains a ProcessProgramView of this process.
    // Keep it alive until after the interpreter releases its executor.
    Process registered_process;
    std::unique_ptr<LlvmJit> jit;
    std::shared_ptr<ProviderCounters> counters;
    std::shared_ptr<FrontierGate> gate;
    std::shared_ptr<RegionKernelBackendProvider> provider;
    std::unique_ptr<Interpreter> interpreter;
    SignalId input { };
    SignalId output { };
    ProcessId process_id { };
};

struct SystemVerilogFallbackObservation final {
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    PackedLogic4 raw_driver;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp event_stamp;
    std::uint64_t value_revision { };
    RunStatus run_status { RunStatus::completed };
    SimulationTick run_time { };
    std::uint64_t run_delta { };
    std::uint64_t checked_resume_delta { };
};

bool same_systemverilog_fallback_observation(
    const SystemVerilogFallbackObservation& left,
    const SystemVerilogFallbackObservation& right)
{
    return left.current == right.current
        && left.last == right.last
        && left.stored == right.stored
        && left.raw_driver == right.raw_driver
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_stamp.origin.process_domain
            == right.event_stamp.origin.process_domain
        && left.event_stamp.origin.phase == right.event_stamp.origin.phase
        && left.event_stamp.systemverilog_round
            == right.event_stamp.systemverilog_round
        && left.value_revision == right.value_revision
        && left.run_status == right.run_status
        && left.run_time == right.run_time
        && left.run_delta == right.run_delta
        && left.checked_resume_delta == right.checked_resume_delta;
}

SystemVerilogFallbackObservation run_systemverilog_input(
    Harness& test)
{
    const auto resumes_before
        = test.counters->checked_resumes.load(std::memory_order_relaxed);
    test.schedule_one_input();
    const auto run = test.interpreter->run(1U);
    require(run.status == RunStatus::completed
            || run.status == RunStatus::time_limit,
        "the SV-active checked or declined-frontier run completes");

    auto& implementation
        = OwnedDriverDemotionTestAccess::implementation(*test.interpreter);
    const auto* const raw_driver
        = implementation.driver_values.at(test.output).find(test.process_id);
    require(raw_driver != nullptr,
        "the SV-active output retains its original process owner");
    return {
        implementation.signals.at(test.output).initial_value,
        implementation.signal_last_values.at(test.output),
        implementation.driven_values.at(test.output), raw_driver->value,
        implementation.signal_events.at(test.output),
        implementation.signal_transactions.at(test.output),
        implementation.signal_event_scheduling_stamps.at(test.output),
        implementation.signal_value_revisions.at(test.output),
        run.status, run.time, run.delta,
        test.counters->checked_resumes.load(std::memory_order_relaxed)
            - resumes_before,
    };
}

void check_systemverilog_v2_fallback_without_v1(
    const FrontierProviderMode frontier_mode,
    const bool decline_first_frontier_entry,
    const JitOptimizationLevel optimization)
{
    require((frontier_mode == FrontierProviderMode::available)
                == decline_first_frontier_entry,
        "only an available SV V2 runtime can exercise a pristine entry decline");

    SystemVerilogFallbackObservation checked_reference;
    {
        Harness checked { Route::systemverilog_active,
            FrontierProviderMode::available, true, true, optimization };
        const auto checked_shape = checked.shape();
        require(!checked_shape.activation_program_present
                && !checked_shape.v1_backend_present
            && !checked_shape.frontier_runtime_present
            && checked.counters->activation_creates.load(
                       std::memory_order_relaxed) == 0U
                && checked.counters->activation_backend_creates.load(
                       std::memory_order_relaxed) == 0U
                && checked.counters->frontier_creates.load(
                       std::memory_order_relaxed) == 0U,
            "the ordinary checked reference is hook-gated with no V1 backend and no V2 runtime");
        checked_reference = run_systemverilog_input(checked);
        require(checked_reference.checked_resume_delta == 1U,
            "the SV checked reference executes exactly one original process callback");
    }

    Harness fallback { Route::systemverilog_active, frontier_mode,
        true, false, optimization };
    const auto before = fallback.shape();
    require(before.activation_program_present
            && !before.v1_backend_present
            && !before.v1_generic_workspace_present
            && before.frontier_runtime_present
                == decline_first_frontier_entry
            && fallback.counters->activation_creates.load(
                   std::memory_order_relaxed) == 1U
            && fallback.counters->activation_backend_creates.load(
                   std::memory_order_relaxed) == 0U
            && fallback.counters->frontier_creates.load(
                   std::memory_order_relaxed) == 1U,
        "the SV provider receives its one V1 request but returns no backend before the expected V2 result");

    if (decline_first_frontier_entry) {
        fallback.gate->decline_next_entry.store(true,
            std::memory_order_release);
    }
    const auto frontier_entries_before
        = fallback.counters->frontier_entries.load(std::memory_order_relaxed);
    const auto forced_declines_before
        = fallback.counters->forced_frontier_declines.load(
            std::memory_order_relaxed);
    const auto delegate_calls_before
        = fallback.counters->frontier_delegate_calls.load(
            std::memory_order_relaxed);
    const auto observed = run_systemverilog_input(fallback);
    const auto after = fallback.shape();
    require(observed.checked_resume_delta == 1U
            && fallback.counters->activation_creates.load(
                   std::memory_order_relaxed) == 1U
            && fallback.counters->activation_backend_creates.load(
                   std::memory_order_relaxed) == 0U
            && !after.v1_backend_present
            && !after.v1_generic_workspace_present,
        "the checked fallback executes once without constructing or retaining a V1 backend");

    if (decline_first_frontier_entry) {
        require(fallback.counters->frontier_entries.load(
                    std::memory_order_relaxed)
                    == frontier_entries_before + 1U
                && fallback.counters->forced_frontier_declines.load(
                       std::memory_order_relaxed)
                    == forced_declines_before + 1U
                && fallback.counters->frontier_delegate_calls.load(
                       std::memory_order_relaxed)
                    == delegate_calls_before
                && fallback.counters->last_frontier_status.load(
                       std::memory_order_relaxed)
                    == static_cast<std::uint32_t>(
                        RegionFrontierStatusV2::decline_before_mutation)
                && fallback.counters->last_frontier_cursor.load(
                       std::memory_order_relaxed) == 0U
                && fallback.counters->last_frontier_pending_writes.load(
                       std::memory_order_relaxed) == 0U
                && fallback.counters->last_frontier_staged_events.load(
                       std::memory_order_relaxed) == 0U
                && fallback.counters->last_frontier_ack_count.load(
                       std::memory_order_relaxed) == 0U,
            "a pristine SV V2 decline precedes exactly one checked process callback");
    } else {
        require(fallback.counters->frontier_entries.load(
                    std::memory_order_relaxed)
                    == frontier_entries_before
                && !after.frontier_runtime_present,
            "a refused SV V2 preparation falls directly through to the checked process callback");
    }

    const auto expected_current = PackedLogic4 { 1U, Logic4::zero };
    const auto previous_sv_value = PackedLogic4 { 1U, Logic4::one };
    require(observed.current == expected_current
            && observed.last == previous_sv_value
            && observed.stored == expected_current
            && observed.raw_driver == expected_current
            && observed.event.has_value()
            && observed.transaction.has_value()
            && observed.event->first == 1U
            && observed.transaction->first == 1U
            && observed.event_stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog
            && observed.event_stamp.origin.phase == SchedulerPhase::active
            && same_systemverilog_fallback_observation(
                observed, checked_reference),
        "V2 fallback matches the checked single-update current/LAST/stored/raw-owner values and event metadata");
}

void check_generic_v2_success(const bool local_wave_enabled,
    const JitOptimizationLevel optimization)
{
    Harness test { Route::generic_update, FrontierProviderMode::available,
        local_wave_enabled, false, optimization };
    const auto before = test.shape();
    require(before.activation_program_present
            && before.frontier_runtime_present
            && !before.v1_backend_present
            && !before.v1_generic_workspace_present
            && test.counters->activation_creates.load() == 0U
            && test.counters->frontier_creates.load() == 1U,
        "a prepared Generic V2 runtime owns this component without constructing a V1 backend or workspace");
    const auto checked_before = test.counters->checked_resumes.load();
    const auto entries_before = test.counters->frontier_entries.load();
    const auto delegate_calls_before
        = test.counters->frontier_delegate_calls.load();
    test.schedule_one_input();
    test.run_one_input();
    const auto after = test.shape();
    require(test.counters->frontier_entries.load() == entries_before + 1U
            && test.counters->frontier_delegate_calls.load()
                == delegate_calls_before + 1U
            && test.counters->last_frontier_status.load()
                == static_cast<std::uint32_t>(
                    RegionFrontierStatusV2::generic_update_batch_ready)
            && test.counters->last_frontier_cursor.load() == 1U
            && test.counters->last_frontier_pending_writes.load() == 1U
            && test.counters->last_frontier_staged_events.load() == 1U
            && test.counters->last_frontier_ack_count.load() == 0U
            && after.generic_retirement_ack_valid
            && after.generic_pending_writes == 1U
            && after.generic_staged_events == 1U
            && after.generic_ack_count == 1U
            && test.counters->checked_resumes.load() == checked_before
            && test.counters->activation_creates.load() == 0U,
        "the generated V2 entry consumes one member, stages one whole write, and the host records a full Update ACK without checked replay");
}

void check_generic_v2_decline_uses_checked_callback_once(
    const JitOptimizationLevel optimization)
{
    Harness test { Route::generic_update, FrontierProviderMode::available,
        true, false, optimization };
    const auto before = test.shape();
    require(before.frontier_runtime_present && !before.v1_backend_present
            && test.counters->activation_creates.load() == 0U,
        "the decline fixture begins with only a prepared V2 runtime");
    const auto resume_before = test.counters->checked_resumes.load();
    const auto entries_before = test.counters->frontier_entries.load();
    const auto delegate_calls_before
        = test.counters->frontier_delegate_calls.load();
    const auto declines_before
        = test.counters->forced_frontier_declines.load();
    test.gate->decline_next_entry.store(true, std::memory_order_release);
    test.schedule_one_input();
    test.run_one_input();
    const auto after = test.shape();
    require(test.counters->frontier_entries.load() == entries_before + 1U
            && test.counters->forced_frontier_declines.load()
                == declines_before + 1U
            && test.counters->last_frontier_status.load()
                == static_cast<std::uint32_t>(
                    RegionFrontierStatusV2::decline_before_mutation)
            && test.counters->last_frontier_cursor.load() == 0U
            && test.counters->last_frontier_pending_writes.load() == 0U
            && test.counters->last_frontier_staged_events.load() == 0U
            && test.counters->last_frontier_ack_count.load() == 0U
            && test.counters->frontier_delegate_calls.load()
                == delegate_calls_before
            && test.counters->checked_resumes.load() - resume_before == 1U
            && test.counters->activation_creates.load() == 0U
            && !after.v1_backend_present && after.frontier_runtime_present,
        "a pre-mutation V2 decline invokes the original checked callback exactly once without constructing V1");
}

void check_generic_v1_fallback(const FrontierProviderMode frontier_mode)
{
    Harness test { Route::generic_update, frontier_mode,
        true, false, JitOptimizationLevel::o0 };
    const auto before = test.shape();
    require(before.activation_program_present && before.v1_backend_present
            && !before.v1_generic_workspace_present,
        "the V1 fallback is built for the same certified Generic compute component");
    const auto resume_before = test.counters->checked_resumes.load();
    test.schedule_one_input();
    test.run_one_input();
    const auto after = test.shape();
    require(test.counters->activation_creates.load() == 1U
            && test.counters->checked_resumes.load() == resume_before
            && after.v1_backend_present && after.v1_generic_workspace_present,
        "missing or declining V2 capability preserves the working V1 Generic backend route");
    if (frontier_mode == FrontierProviderMode::absent) {
        require(test.counters->frontier_creates.load() == 0U
                && !after.frontier_runtime_present,
            "an activation-only provider does not invent V2 capability");
    } else {
        require(test.counters->frontier_creates.load() == 1U
                && !after.frontier_runtime_present,
            "a provider decline during V2 construction keeps the V1 route available");
    }
}

void check_generic_hook_gate_preserves_v1_construction()
{
    Harness test { Route::generic_update, FrontierProviderMode::available,
        true, true, JitOptimizationLevel::o0 };
    const auto before = test.shape();
    require(before.activation_program_present && before.v1_backend_present
            && !before.frontier_runtime_present
            && test.counters->activation_creates.load() == 1U
            && test.counters->frontier_creates.load() == 0U,
        "an execution hook disables V2 preparation while preserving the existing V1 compute backend");
    const auto resume_before = test.counters->checked_resumes.load();
    test.schedule_one_input();
    test.run_one_input();
    require(test.counters->checked_resumes.load() - resume_before == 1U,
        "the hook-gated route retains checked callback behavior");
}

void check_systemverilog_v2_only_route()
{
    Harness test { Route::systemverilog_active,
        FrontierProviderMode::available, true, false,
        JitOptimizationLevel::o0 };
    const auto before = test.shape();
    require(before.activation_program_present && !before.v1_backend_present
            && before.frontier_runtime_present
            && !before.frontier_trusted_capability_present
            && !before.alias_certificate_storage_available
            && before.alias_certificate_buffers_empty
            && before.alias_trusted_entries == 0U
            && test.counters->activation_creates.load() == 1U
            && test.counters->activation_backend_creates.load() == 0U
            && test.counters->frontier_creates.load() == 1U,
        "the SV Active route prepares V2 without constructing the optional V1 backend");
    const auto native_before
        = test.counters->frontier_delegate_calls.load();
    const auto checked_before
        = test.counters->checked_resumes.load();
    const auto alias_checked_before = before.alias_checked_entries;
    test.schedule_one_input();
    test.run_one_input();
    const auto after = test.shape();
    require(test.counters->frontier_delegate_calls.load() > native_before
            && test.counters->checked_resumes.load() == checked_before
            && test.counters->activation_backend_creates.load() == 0U
            && after.frontier_runtime_present
            && !after.frontier_trusted_capability_present
            && !after.alias_certificate_storage_available
            && after.alias_certificate_buffers_empty
            && after.alias_checked_entries > alias_checked_before
            && after.alias_trusted_entries == 0U,
        "a wrapper without the private capability uses checked V2 and "
        "still executes the real backend");
}

void check_vhdl_projected_stays_on_v1()
{
    Harness test { Route::vhdl_projected,
        FrontierProviderMode::available, true, false,
        JitOptimizationLevel::o0 };
    const auto before = test.shape();
    require(before.activation_program_present && before.v1_backend_present
            && !before.frontier_runtime_present
            && test.counters->activation_creates.load() == 1U
            && test.counters->frontier_creates.load() == 0U,
        "a VHDL projected output retains its V1 compute backend and is not admitted to Generic V2");
    const auto resume_before = test.counters->checked_resumes.load();
    test.schedule_one_input();
    test.run_one_input();
    const auto after = test.shape();
    require(test.counters->checked_resumes.load() == resume_before
            && after.v1_generic_workspace_present,
        "VHDL projected execution remains on the existing native V1 route");
}

struct GenericCapacityActivationKey final {
    SimulationTick time { };
    std::uint64_t delta { };
    StableOrder order { };
    std::uint64_t sequence { };
    std::uint64_t systemverilog_round { };
    SchedulerPhase phase { SchedulerPhase::active };
    bool systemverilog { };
};

struct GenericCapacityTrace final {
    std::array<SchedulerTraceRecord, 129U> active_tasks { };
    std::size_t active_task_count { };
    bool overflow { };

    static void record(void* const context,
        const SchedulerTraceRecord& entry) noexcept
    {
        auto& trace = *static_cast<GenericCapacityTrace*>(context);
        if (entry.kind != SchedulerTraceKind::task_begin
            || entry.time != 1U || !entry.phase
            || *entry.phase != SchedulerPhase::active) {
            return;
        }
        if (trace.active_task_count >= trace.active_tasks.size()) {
            trace.overflow = true;
            return;
        }
        trace.active_tasks[trace.active_task_count++] = entry;
    }
};

struct GenericCapacitySignalResult final {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 stored;
    PackedLogic4 driver;
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    SignalEventSchedulingStamp event_stamp;
    std::uint64_t value_revision { };
};

struct GenericCapacityRun final {
    std::vector<GenericCapacitySignalResult> outputs;
    std::vector<GenericCapacityActivationKey> activation_keys;
    SchedulerBatchCompactionStats ticket_stats;
    std::size_t shared_frontier_body_registry_reuses { };
    bool shared_frontier_body_identity_consistent { };
    bool frontier_plan_views_are_distinct { };
    bool frontier_object_cache_statistics_are_empty { };
    std::uint64_t checked_resume_delta { };
    std::uint64_t legacy_projected_attempt_delta { };
    std::uint64_t legacy_projected_decline_delta { };
    std::uint64_t generated_member_dispatch_delta { };
    std::size_t selected_component_count { };
};

bool same_generic_capacity_signal_result(
    const GenericCapacitySignalResult& left,
    const GenericCapacitySignalResult& right)
{
    return left.current == right.current
        && left.previous == right.previous
        && left.stored == right.stored
        && left.driver == right.driver
        && left.event == right.event
        && left.transaction == right.transaction
        && left.event_stamp.origin.process_domain
            == right.event_stamp.origin.process_domain
        && left.event_stamp.origin.phase == right.event_stamp.origin.phase
        && left.event_stamp.systemverilog_round
            == right.event_stamp.systemverilog_round
        && left.value_revision == right.value_revision;
}

bool cache_statistics_are_zero(const LlvmJitCacheStatistics& statistics)
{
    return statistics.hits == 0U && statistics.misses == 0U
        && statistics.stores == 0U && statistics.rejected_entries == 0U
        && statistics.load_failures == 0U && statistics.store_failures == 0U
        && statistics.pruned_entries == 0U && statistics.pruned_bytes == 0U
        && statistics.prune_failures == 0U;
}

GenericCapacityRun run_generic_automatic_capacity_case(
    const std::size_t component_count,
    const std::optional<JitOptimizationLevel> optimization)
{
    if (component_count != 65U && component_count != 129U) {
        throw std::invalid_argument {
            "automatic Generic capacity case requires 65 or 129 components"
        };
    }
    ScopedEnvironment profile_environment { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment region_environment { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_environment { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };

    const bool compiled = optimization.has_value();
    const auto jit_options = compiled
        ? std::optional<LlvmJitOptions> { Harness::make_options(*optimization) }
        : std::nullopt;
    std::vector<std::uint32_t> signal_widths(component_count * 2U, 1U);
    std::vector<ValueKind> signal_kinds(
        component_count * 2U, ValueKind::logic4);
    std::vector<ResolutionKind> signal_resolutions(
        component_count * 2U, ResolutionKind::none);
    std::vector<SignalId> inputs;
    std::vector<SignalId> outputs;
    inputs.reserve(component_count);
    outputs.reserve(component_count);
    std::vector<Process> programs;
    programs.reserve(component_count);

    std::unique_ptr<LlvmJit> jit;
    std::shared_ptr<RegionKernelBackendProvider> provider;
    std::shared_ptr<ProviderCounters> process_counters
        = std::make_shared<ProviderCounters>();
    std::unique_ptr<Interpreter> interpreter
        = std::make_unique<Interpreter>();
    if (compiled) {
        jit = std::make_unique<LlvmJit>(*jit_options);
        provider = ::fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(*jit_options,
                "generic-readiness-ticket-auto-capacity-v1");
        require(static_cast<bool>(provider),
            "automatic-size case obtains the production LLVM provider");
        interpreter->set_region_kernel_backend_provider(provider);
    }

    for (std::size_t index = 0U; index < component_count; ++index) {
        const auto input = interpreter->add_signal({
            "generic_capacity.input_" + std::to_string(index),
            PackedLogic4 { 1U, Logic4::zero } });
        const auto output = interpreter->add_signal({
            "generic_capacity.output_" + std::to_string(index),
            PackedLogic4 { 1U, Logic4::x } });
        require(static_cast<std::size_t>(input) == index * 2U
                && static_cast<std::size_t>(output) == index * 2U + 1U,
            "independent Generic signals retain dense per-component IDs");
        inputs.push_back(input);
        outputs.push_back(output);

        Process process;
        process.id = static_cast<ProcessId>(index);
        process.name = "generic_capacity_process_" + std::to_string(index);
        process.scheduling_domain = ProcessSchedulingDomain::generic;
        process.register_count = 2U;
        process.register_value_kinds = {
            ValueKind::logic4, ValueKind::logic4 };
        process.static_sensitivity = { { input, EdgeKind::any } };
        process.driver_regions = { { output, 0U, 1U, true } };
        process.operations = { ReadSignal { 0U, input }, UnaryNot { 1U, 0U },
            WriteUpdate { output, 1U, SignalUpdateDomain::generic },
            WaitSensitivity { }, Jump { 0U } };
        programs.push_back(std::move(process));
        const auto registered = interpreter->add_process(programs.back());
        require(registered == static_cast<ProcessId>(index),
            "automatic-size fixture process IDs remain dense");
    }

    if (compiled) {
        for (const auto& process : programs) {
            jit->add_process(process.name, process, signal_widths, signal_kinds);
            const auto handle = jit->lookup(process.name);
            require(static_cast<bool>(handle),
                "LLVM retains every independent Generic process");
            auto executor = std::make_unique<
                ::fsim::app::application_detail::LlvmProcessExecutor>(
                    *jit, handle, process, signal_widths, signal_kinds,
                    signal_resolutions,
                    std::shared_ptr<const ProcessSignalRemap> { }, process.id);
            require(executor->program_access_binding()->valid()
                    && executor->region_kernel_equivalent()
                    && executor->cohort_manages_process_state()
                    && executor->region_kernel_completion_has_no_persistent_registers(),
                "each LLVM process certifies the stateless whole-write body");
            interpreter->set_process_executor(process.id,
                std::make_unique<CountingProcessExecutor>(
                    std::move(executor), process_counters));
        }
    }

    interpreter->start();
    const auto startup = interpreter->run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "every independent process settles at startup before the measured wave");

    GenericCapacityRun result;
    result.activation_keys.resize(component_count);
    auto& impl = OwnedDriverDemotionTestAccess::implementation(*interpreter);
    const auto ticket_stats_before
        = interpreter->scheduler().generic_batch_compaction_stats();
    const auto checked_resumes_before
        = process_counters->checked_resumes.load(std::memory_order_relaxed);
    // These counters belong to the legacy projected-region wrapper. The
    // Generic V2 ticket calls its frontier executor directly, so its accepted
    // route must leave both wrapper counters unchanged.
    const auto legacy_projected_attempts_before
        = impl.generic_projected_region_attempts;
    const auto legacy_projected_declines_before
        = impl.generic_projected_region_declines;
    const auto generated_dispatches_before
        = impl.systemverilog_wave_profile_native_frontier_member_dispatches;
    std::vector<std::uint64_t> member_dispatches_before(component_count, 0U);
    std::vector<std::size_t> component_by_process(component_count);
    const void* shared_body_owner_token { };
    std::uint64_t shared_body_address { };
    std::vector<RegionFrontierStepEntryV2> wrapper_entries;
    wrapper_entries.reserve(component_count);
    std::vector<const RegionFrontierLayoutV2*> plan_layouts;
    plan_layouts.reserve(component_count);
    std::vector<const RegionFrontierFrameV2*> runtime_frames;
    runtime_frames.reserve(component_count);
    std::size_t body_registry_reuses { };
    bool object_cache_statistics_are_empty = true;
    if (compiled) {
        require(impl.region_component_by_process.size() == component_count,
            "the runtime graph inventories every process");
        for (std::size_t index = 0U; index < component_count; ++index) {
            const auto component = impl.region_component_by_process[index];
            require(component < impl.region_frontier_runtime_by_component.size()
                    && component < impl.region_activation_programs.size()
                    && impl.region_activation_programs[component].has_value()
                    && component < impl.region_kernel_backends_by_component.size()
                    && !impl.region_kernel_backends_by_component[component],
                "each process has a prepared Generic V2 component without a V1 backend");
            const auto prior_components_end
                = component_by_process.begin()
                + static_cast<std::ptrdiff_t>(index);
            require(std::find(component_by_process.begin(),
                        prior_components_end, component)
                    == prior_components_end,
                "each independent trigger/output pair forms a distinct component");
            component_by_process[index] = component;
            const auto& runtime
                = impl.region_frontier_runtime_by_component[component];
            require(runtime && !runtime->invalidated
                    && runtime->runtime_generation == impl.region_runtime_generation
                    && runtime->execution_mode
                        == RegionFrontierExecutionModeV2::generic_deferred_update
                    && runtime->backend && runtime->backend->executor
                    && runtime->backend->executor->layout().member_count == 1U
                    && runtime->backend->executor->layout().members[0U].process_id
                        == static_cast<ProcessId>(index),
                "automatic sizing prepares one real Generic V2 member per component");
            const auto& layout = runtime->backend->executor->layout();
            require(layout.signal_slot_count == 2U && layout.signals != nullptr,
                "each one-member capacity plan has its exact two-signal layout");
            bool found_input_signal = false;
            bool found_output_signal = false;
            for (std::size_t signal_index = 0U;
                 signal_index < layout.signal_slot_count; ++signal_index) {
                const auto signal_id = layout.signals[signal_index].signal_id;
                found_input_signal = found_input_signal
                    || signal_id == inputs[index];
                found_output_signal = found_output_signal
                    || signal_id == outputs[index];
            }
            require(found_input_signal && found_output_signal,
                "each exact plan layout binds its own physical input and output IDs");
            require(std::find(plan_layouts.begin(), plan_layouts.end(), &layout)
                        == plan_layouts.end()
                    && std::find(runtime_frames.begin(), runtime_frames.end(),
                        &runtime->frame) == runtime_frames.end(),
                "shared code still has a distinct exact plan layout and frame per component");
            plan_layouts.push_back(&layout);
            runtime_frames.push_back(&runtime->frame);
            const auto backend_snapshot
                = ::fsim::app::application_detail::
                    region_frontier_backend_testing_snapshot(
                        *runtime->backend->executor);
            require(backend_snapshot.has_value()
                    && backend_snapshot->shared_body_owner_token != nullptr
                    && backend_snapshot->shared_body_address != 0U
                    && backend_snapshot->wrapper_entry != nullptr,
                "the app observes an actual native body and exact wrapper entry");
            if (shared_body_owner_token == nullptr) {
                shared_body_owner_token
                    = backend_snapshot->shared_body_owner_token;
                shared_body_address = backend_snapshot->shared_body_address;
            } else {
                require(shared_body_owner_token
                            == backend_snapshot->shared_body_owner_token
                        && shared_body_address
                            == backend_snapshot->shared_body_address,
                    "structurally equal components reuse the same live native body owner "
                    "and code address");
            }
            require(std::find(wrapper_entries.begin(), wrapper_entries.end(),
                        backend_snapshot->wrapper_entry)
                    == wrapper_entries.end()
                    && !backend_snapshot->wrapper_registry_reused,
                "each physical component keeps a distinct exact-plan wrapper, separate from body reuse");
            wrapper_entries.push_back(backend_snapshot->wrapper_entry);
            if (backend_snapshot->shared_body_registry_reused) {
                ++body_registry_reuses;
            }
            object_cache_statistics_are_empty
                = object_cache_statistics_are_empty
                    && cache_statistics_are_zero(
                        backend_snapshot->body_object_cache_statistics)
                    && cache_statistics_are_zero(
                        backend_snapshot->wrapper_object_cache_statistics);
            member_dispatches_before[index] = runtime->native_member_dispatches;
        }
    }

    if (compiled) {
        result.shared_frontier_body_registry_reuses = body_registry_reuses;
        result.shared_frontier_body_identity_consistent
            = shared_body_owner_token != nullptr && shared_body_address != 0U;
        result.frontier_plan_views_are_distinct
            = wrapper_entries.size() == component_count
                && plan_layouts.size() == component_count
                && runtime_frames.size() == component_count;
        result.frontier_object_cache_statistics_are_empty
            = object_cache_statistics_are_empty;
    }

    for (std::size_t index = 0U; index < component_count; ++index) {
        interpreter->schedule_signal_at(inputs[index],
            PackedLogic4 { 1U, Logic4::one }, 1U,
            static_cast<StableOrder>(index));
    }

    GenericCapacityTrace trace;
    if (!compiled) {
        interpreter->scheduler().set_trace_hook(&trace,
            &GenericCapacityTrace::record);
    }
    const auto measured = interpreter->run(1U);
    if (!compiled) {
        interpreter->scheduler().set_trace_hook(nullptr, nullptr);
    }
    require(measured.status == RunStatus::completed
            || measured.status == RunStatus::time_limit,
        "the simultaneous independent stimulus wave completes");

    if (compiled) {
        const auto ticket_stats_after
            = interpreter->scheduler().generic_batch_compaction_stats();
        result.ticket_stats.direct_dispatches
            = ticket_stats_after.direct_dispatches
                - ticket_stats_before.direct_dispatches;
        result.ticket_stats.direct_members
            = ticket_stats_after.direct_members
                - ticket_stats_before.direct_members;
        result.ticket_stats.generic_readiness_ticket_queue_insertions
            = ticket_stats_after.generic_readiness_ticket_queue_insertions
                - ticket_stats_before.generic_readiness_ticket_queue_insertions;
        result.ticket_stats.generic_readiness_ticket_members
            = ticket_stats_after.generic_readiness_ticket_members
                - ticket_stats_before.generic_readiness_ticket_members;
        result.ticket_stats.generic_readiness_ticket_members_elided
            = ticket_stats_after.generic_readiness_ticket_members_elided
                - ticket_stats_before.generic_readiness_ticket_members_elided;
        result.ticket_stats.generic_readiness_ticket_fallback_members
            = ticket_stats_after.generic_readiness_ticket_fallback_members
                - ticket_stats_before.generic_readiness_ticket_fallback_members;
        result.checked_resume_delta
            = process_counters->checked_resumes.load(std::memory_order_relaxed)
                - checked_resumes_before;
        result.legacy_projected_attempt_delta
            = impl.generic_projected_region_attempts
                - legacy_projected_attempts_before;
        result.legacy_projected_decline_delta
            = impl.generic_projected_region_declines
                - legacy_projected_declines_before;
        result.generated_member_dispatch_delta
            = impl.systemverilog_wave_profile_native_frontier_member_dispatches
                - generated_dispatches_before;
        result.selected_component_count = 0U;
        std::vector<std::uint64_t> sequences;
        sequences.reserve(component_count);
        for (std::size_t index = 0U; index < component_count; ++index) {
            const auto component = component_by_process[index];
            const auto& runtime
                = impl.region_frontier_runtime_by_component[component];
            const auto& original = runtime->original_scheduler_tasks[0U];
            const auto& slot = runtime->frame.slot;
            const auto& member = runtime->members[0U];
            const auto& origin = member.activation_origin;
            const auto payload
                = OwnedDriverDemotionTestAccess::Implementation::
                    generic_projected_region_payload
                | static_cast<std::uint64_t>(index);
            require(runtime->native_member_dispatches
                        == member_dispatches_before[index] + 1U
                    && runtime->frame.scheduler_task_count == 1U
                    && runtime->frame.scheduler_task_cursor == 1U
                    && runtime->frame.pending_write_count == 1U
                    && runtime->frame.staged_event_count == 1U
                    && runtime->frame.staged_events[0U].stable_order
                        == static_cast<StableOrder>(index)
                    && runtime->frame.staged_events[0U].origin.stable_order
                        == static_cast<StableOrder>(index)
                    && runtime->frame.generic_update_ack_count == 1U
                    && original.payload == payload
                    && original.stable_order == static_cast<StableOrder>(index)
                    && original.sequence != 0U
                    && slot.time == 1U
                    && slot.process_domain
                        == static_cast<std::uint32_t>(ProcessSchedulingDomain::generic)
                    && slot.phase == static_cast<std::uint32_t>(SchedulerPhase::active)
                    && slot.systemverilog_round == 0U
                    && member.process_id == index
                    && origin.time == slot.time && origin.delta == slot.delta
                    && origin.systemverilog_round == slot.systemverilog_round
                    && origin.stable_order == original.stable_order
                    && origin.sequence == original.sequence
                    && origin.process_domain == slot.process_domain
                    && origin.phase == slot.phase
                    && !impl.processes[index].queued
                    && !impl.region_readiness_queued_by_process[index].key_valid,
                "each V2 component retires one authentic Generic Active key and ACKs one whole write");
            sequences.push_back(original.sequence);
            result.activation_keys[index] = {
                slot.time, slot.delta, original.stable_order,
                original.sequence, slot.systemverilog_round,
                static_cast<SchedulerPhase>(slot.phase), false };
            ++result.selected_component_count;
        }
        std::ranges::sort(sequences);
        require(std::adjacent_find(sequences.begin(), sequences.end())
                    == sequences.end(),
            "every independent Generic component owns a distinct scheduler sequence");
        require(result.ticket_stats.direct_dispatches == component_count
                && result.ticket_stats.direct_members == component_count
                && result.ticket_stats.generic_readiness_ticket_queue_insertions
                    == component_count
                && result.ticket_stats.generic_readiness_ticket_members
                    == component_count
                && result.ticket_stats.generic_readiness_ticket_members_elided == 0U
                && result.ticket_stats.generic_readiness_ticket_fallback_members == 0U
                && result.checked_resume_delta == 0U
                && result.legacy_projected_attempt_delta == 0U
                && result.legacy_projected_decline_delta == 0U
                && result.generated_member_dispatch_delta == component_count
                && result.selected_component_count == component_count,
            "automatic runtime sizing admits one physical ticket and one native member per component without checked fallback");
    } else {
        require(!trace.overflow && trace.active_task_count == component_count,
            "the ordinary interpreter exposes every reference Active callback");
        std::vector<bool> seen(component_count, false);
        for (std::size_t index = 0U; index < trace.active_task_count; ++index) {
            const auto& entry = trace.active_tasks[index];
            require(entry.order < component_count && !seen[entry.order],
                "ordinary Active callbacks retain unique process stable orders");
            seen[entry.order] = true;
            result.activation_keys[entry.order] = {
                entry.time, entry.delta, entry.order, entry.sequence,
                entry.systemverilog_round, *entry.phase,
                entry.systemverilog };
        }
        require(std::ranges::all_of(seen, [](const bool value) {
                    return value;
                }),
            "the interpreter reference records one key for every process");
    }

    result.outputs.reserve(component_count);
    for (std::size_t index = 0U; index < component_count; ++index) {
        const auto current = interpreter->signal_value_snapshot(outputs[index]);
        const auto stored
            = interpreter->stored_signal_value_snapshot(outputs[index]);
        const auto driver
            = interpreter->driver_value(static_cast<ProcessId>(index), outputs[index]);
        const auto& signal_impl = OwnedDriverDemotionTestAccess::implementation(
            *interpreter);
        result.outputs.push_back({ current,
            signal_impl.signal_last_values.at(outputs[index]), stored, driver,
            signal_impl.signal_events.at(outputs[index]),
            signal_impl.signal_transactions.at(outputs[index]),
            signal_impl.signal_event_scheduling_stamps.at(outputs[index]),
            signal_impl.signal_value_revisions.at(outputs[index]) });
        const auto& output = result.outputs.back();
        require(output.current.get(0U) == Logic4::zero
                && output.previous.get(0U) == Logic4::one
                && output.stored.get(0U) == Logic4::zero
                && output.driver.get(0U) == Logic4::zero
                && output.event && output.event->first == 1U
                && output.transaction && output.transaction->first == 1U
                && output.event_stamp.origin.process_domain
                    == ProcessSchedulingDomain::generic
                && output.event_stamp.origin.phase == SchedulerPhase::active
                && output.event_stamp.systemverilog_round == 0U,
            "each output publishes the expected current/previous/driver value and Generic transaction");
    }
    return result;
}

GenericCapacityRun run_generic_large_connected_component_case(
    const std::size_t member_count,
    const std::optional<JitOptimizationLevel> optimization)
{
    if (member_count != 65U && member_count != 129U) {
        throw std::invalid_argument {
            "large Generic component case requires 65 or 129 members"
        };
    }
    ScopedEnvironment profile_environment { "FSIM_PROFILE_SV_WAVES", "1" };
    ScopedEnvironment region_environment { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_environment { "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };

    const bool compiled = optimization.has_value();
    const auto jit_options = compiled
        ? std::optional<LlvmJitOptions> { Harness::make_options(*optimization) }
        : std::nullopt;
    std::vector<std::uint32_t> signal_widths(member_count + 1U, 1U);
    std::vector<ValueKind> signal_kinds(
        member_count + 1U, ValueKind::logic4);
    std::vector<ResolutionKind> signal_resolutions(
        member_count + 1U, ResolutionKind::none);
    std::vector<SignalId> outputs;
    std::vector<Process> programs;
    outputs.reserve(member_count);
    programs.reserve(member_count);

    std::unique_ptr<LlvmJit> jit;
    std::shared_ptr<RegionKernelBackendProvider> provider;
    auto process_counters = std::make_shared<ProviderCounters>();
    std::unique_ptr<Interpreter> interpreter
        = std::make_unique<Interpreter>();
    if (compiled) {
        jit = std::make_unique<LlvmJit>(*jit_options);
        provider = ::fsim::app::application_detail::
            make_llvm_region_kernel_backend_provider(*jit_options,
                "generic-large-component-retained-ticket-v1");
        require(static_cast<bool>(provider),
            "large-component case obtains the production LLVM provider");
        interpreter->set_region_kernel_backend_provider(provider);
    }

    const auto trigger = interpreter->add_signal({
        "generic_large_component.trigger",
        PackedLogic4 { 1U, Logic4::zero } });
    for (std::size_t index = 0U; index < member_count; ++index) {
        const auto output = interpreter->add_signal({
            "generic_large_component.output_" + std::to_string(index),
            PackedLogic4 { 1U, Logic4::x } });
        outputs.push_back(output);

        Process process;
        process.id = static_cast<ProcessId>(index);
        process.name = "generic_large_component_process_"
            + std::to_string(index);
        process.scheduling_domain = ProcessSchedulingDomain::generic;
        process.driver_regions = { { output, 0U, 1U, true } };
        if (index == 0U) {
            process.register_count = 2U;
            process.register_value_kinds = {
                ValueKind::logic4, ValueKind::logic4 };
            process.static_sensitivity = { { trigger, EdgeKind::any } };
            process.operations = {
                ReadSignal { 0U, trigger },
                UnaryNot { 1U, 0U },
                WriteUpdate { output, 1U, SignalUpdateDomain::generic },
                WaitSensitivity { }, Jump { 0U }
            };
        } else {
            const auto predecessor = outputs[index - 1U];
            process.register_count = 3U;
            process.register_value_kinds = {
                ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
            process.static_sensitivity = {
                { trigger, EdgeKind::any },
                { predecessor, EdgeKind::any }
            };
            process.operations = {
                ReadSignal { 0U, trigger },
                ReadSignal { 1U, predecessor },
                Binary { BinaryOperator::bit_and, 2U, 0U, 1U },
                WriteUpdate { output, 2U, SignalUpdateDomain::generic },
                WaitSensitivity { }, Jump { 0U }
            };
        }
        programs.push_back(std::move(process));
        const auto registered = interpreter->add_process(programs.back());
        require(registered == static_cast<ProcessId>(index),
            "large-component Generic process IDs remain dense");
    }

    if (compiled) {
        for (const auto& process : programs) {
            jit->add_process(process.name, process, signal_widths, signal_kinds);
            const auto handle = jit->lookup(process.name);
            require(static_cast<bool>(handle),
                "LLVM retains every connected Generic process body");
            auto executor = std::make_unique<
                ::fsim::app::application_detail::LlvmProcessExecutor>(
                    *jit, handle, process, signal_widths, signal_kinds,
                    signal_resolutions,
                    std::shared_ptr<const ProcessSignalRemap> { }, process.id);
            require(executor->program_access_binding()->valid()
                    && executor->region_kernel_equivalent()
                    && executor->cohort_manages_process_state()
                    && executor->region_kernel_completion_has_no_persistent_registers(),
                "each connected LLVM member retains the parked whole-write certificate");
            interpreter->set_process_executor(process.id,
                std::make_unique<CountingProcessExecutor>(
                    std::move(executor), process_counters));
        }
    }

    interpreter->start();
    const auto startup = interpreter->run(0U);
    require(startup.status == RunStatus::completed
            || startup.status == RunStatus::time_limit,
        "large Generic component settles startup before the external trigger");

    auto& impl = OwnedDriverDemotionTestAccess::implementation(*interpreter);
    std::size_t component { };
    std::uint64_t dispatches_before { };
    const auto stats_before
        = interpreter->scheduler().generic_batch_compaction_stats();
    const auto checked_before
        = process_counters->checked_resumes.load(std::memory_order_relaxed);
    if (compiled) {
        require(impl.region_component_by_process.size() == member_count,
            "the graph inventories the connected component members");
        component = impl.region_component_by_process[0U];
        require(component < impl.region_frontier_runtime_by_component.size()
                && component < impl.region_activation_programs.size()
                && impl.region_activation_programs[component].has_value()
                && component < impl.region_kernel_backends_by_component.size()
                && !impl.region_kernel_backends_by_component[component],
            "one prepared runtime component owns the connected chain");
        for (std::size_t index = 0U; index < member_count; ++index) {
            require(impl.region_component_by_process[index] == component,
                "the dependency chain is one component rather than independent singletons");
        }
        const auto& runtime = impl.region_frontier_runtime_by_component[component];
        require(runtime && !runtime->invalidated
                && runtime->execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update
                && runtime->backend && runtime->backend->executor
                && runtime->backend->executor->layout().member_count
                    == member_count
                && runtime->generic_queued_members.size() == member_count
                && runtime->generic_queued_ready_words.size()
                    == (member_count + 63U) / 64U
                && runtime->generic_ticket_member_offsets.size()
                    == member_count
                && runtime->scheduler_tasks.size()
                    == std::remove_reference_t<
                        decltype(*runtime)>::scheduler_task_capacity
                && runtime->scheduler_tasks.size() == 64U,
            "one real Generic V2 runtime supports the full component while keeping its offered task scratch bounded");
        dispatches_before = runtime->native_member_dispatches;
    }

    interpreter->schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::one }, 1U, 0U);
    const auto measured = interpreter->run(1U);
    require(measured.status == RunStatus::completed
            || measured.status == RunStatus::time_limit,
        "large Generic component drains the trigger and deferred Update work");

    GenericCapacityRun result;
    result.outputs.reserve(member_count);
    if (compiled) {
        const auto stats_after
            = interpreter->scheduler().generic_batch_compaction_stats();
        result.ticket_stats.generic_readiness_ticket_queue_insertions
            = stats_after.generic_readiness_ticket_queue_insertions
                - stats_before.generic_readiness_ticket_queue_insertions;
        result.ticket_stats.generic_readiness_ticket_members
            = stats_after.generic_readiness_ticket_members
                - stats_before.generic_readiness_ticket_members;
        result.ticket_stats.generic_readiness_ticket_members_elided
            = stats_after.generic_readiness_ticket_members_elided
                - stats_before.generic_readiness_ticket_members_elided;
        result.ticket_stats.generic_readiness_ticket_fallback_members
            = stats_after.generic_readiness_ticket_fallback_members
                - stats_before.generic_readiness_ticket_fallback_members;
        result.ticket_stats.direct_dispatches
            = stats_after.direct_dispatches - stats_before.direct_dispatches;
        result.ticket_stats.direct_members
            = stats_after.direct_members - stats_before.direct_members;
        result.checked_resume_delta
            = process_counters->checked_resumes.load(std::memory_order_relaxed)
                - checked_before;
        const auto& runtime = impl.region_frontier_runtime_by_component[component];
        const auto& final_frame = runtime->frame;
        const auto minimum_chunk_dispatches = (member_count + 63U) / 64U;
        require(final_frame.generic_update_ack_count != 0U
                && final_frame.generic_update_ack_count
                    == final_frame.staged_event_count
                && final_frame.scheduler_frontier_generation != 0U
                && final_frame.scheduler_task_count != 0U
                && final_frame.scheduler_task_count
                    <= std::remove_reference_t<
                        decltype(*runtime)>::scheduler_task_capacity
                && final_frame.scheduler_task_cursor
                    == final_frame.scheduler_task_count
                && runtime->native_member_dispatches >= dispatches_before
                    + member_count
                && result.ticket_stats.generic_readiness_ticket_members
                    >= member_count
                && result.ticket_stats.generic_readiness_ticket_members_elided
                    >= member_count - 1U
                && result.ticket_stats.direct_members >= member_count
                && result.ticket_stats.direct_dispatches
                    >= minimum_chunk_dispatches
                && result.ticket_stats.generic_readiness_ticket_queue_insertions
                    >= 1U
                && result.ticket_stats.generic_readiness_ticket_fallback_members
                    == 0U
                && result.checked_resume_delta == 0U,
            "the large component consumes its authentic compact-ticket members natively without checked fallback");
        for (std::size_t index = 0U; index < member_count; ++index) {
            require(!impl.processes[index].queued
                    && !runtime->generic_queued_members[index].receipt.valid,
                "every large-component readiness receipt is retired after the full Generic Update drain");
        }
        result.selected_component_count = 1U;
    }

    const auto& signal_impl
        = OwnedDriverDemotionTestAccess::implementation(*interpreter);
    for (std::size_t index = 0U; index < member_count; ++index) {
        const auto current = interpreter->signal_value_snapshot(outputs[index]);
        const auto stored
            = interpreter->stored_signal_value_snapshot(outputs[index]);
        const auto driver = interpreter->driver_value(
            static_cast<ProcessId>(index), outputs[index]);
        result.outputs.push_back({ current,
            signal_impl.signal_last_values.at(outputs[index]), stored, driver,
            signal_impl.signal_events.at(outputs[index]),
            signal_impl.signal_transactions.at(outputs[index]),
            signal_impl.signal_event_scheduling_stamps.at(outputs[index]),
            signal_impl.signal_value_revisions.at(outputs[index]) });
    }
    return result;
}

void check_generic_large_connected_component_ticket_sizing(
    const std::size_t member_count)
{
    const auto reference
        = run_generic_large_connected_component_case(member_count, std::nullopt);
    for (const auto optimization : { JitOptimizationLevel::o0,
             JitOptimizationLevel::o2 }) {
        const auto compiled
            = run_generic_large_connected_component_case(member_count,
                optimization);
        require(compiled.outputs.size() == member_count
                && reference.outputs.size() == member_count
                && compiled.selected_component_count == 1U,
            "compiled and interpreter use the same single large-component fixture");
        for (std::size_t index = 0U; index < member_count; ++index) {
            require(same_generic_capacity_signal_result(
                        compiled.outputs[index], reference.outputs[index]),
                "large-component Generic O0/O2 current, last, stored, driver, event, and transaction state matches the interpreter");
        }
    }
}

void check_generic_automatic_ticket_pool_sizing(
    const std::size_t component_count)
{
    const auto reference
        = run_generic_automatic_capacity_case(component_count, std::nullopt);
    for (const auto optimization : { JitOptimizationLevel::o0,
             JitOptimizationLevel::o2 }) {
        const auto compiled
            = run_generic_automatic_capacity_case(component_count, optimization);
        require(compiled.outputs.size() == reference.outputs.size()
                && compiled.activation_keys.size()
                    == reference.activation_keys.size(),
            "compiled and interpreter capacity fixtures have the same extent");
        for (std::size_t index = 0U; index < component_count; ++index) {
            require(same_generic_capacity_signal_result(
                        compiled.outputs[index], reference.outputs[index]),
                "Generic O0/O2 current, last, stored, driver, event, and transaction state matches the interpreter");
            const auto& actual_key = compiled.activation_keys[index];
            const auto& expected_key = reference.activation_keys[index];
            require(actual_key.time == expected_key.time
                    && actual_key.delta == expected_key.delta
                    && actual_key.order == expected_key.order
                    && actual_key.sequence == expected_key.sequence
                    && actual_key.systemverilog_round
                        == expected_key.systemverilog_round
                    && actual_key.phase == expected_key.phase
                    && actual_key.systemverilog == expected_key.systemverilog
                    && actual_key.order == static_cast<StableOrder>(index)
                    && actual_key.time == 1U
                    && actual_key.phase == SchedulerPhase::active
                    && !actual_key.systemverilog,
                "Generic V2 preserves each ordinary reference callback's exact key and process order");
        }
        require(compiled.selected_component_count == component_count,
            "the automatic pool witness selects one checked one-member runtime per trigger");
        require(compiled.shared_frontier_body_identity_consistent
                && compiled.shared_frontier_body_registry_reuses
                    == component_count - 1U
                && compiled.frontier_plan_views_are_distinct
                && compiled.frontier_object_cache_statistics_are_empty,
            "the 65/129-component run shares one actual body in memory, keeps "
            "exact wrappers distinct, and records disk-cache statistics separately");
    }
}

void check_deferred_peer_checked_to_native_transition(
    const JitOptimizationLevel optimization)
{
    ScopedEnvironment profile_environment { "FSIM_PROFILE_SV_WAVES" };
    ScopedEnvironment region_environment {
        "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
    ScopedEnvironment local_wave_environment {
        "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };

    const auto options = Harness::make_options(optimization);
    const std::vector<std::uint32_t> signal_widths { 1U, 1U, 1U };
    const std::vector<ValueKind> signal_kinds {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    const std::vector<ResolutionKind> signal_resolutions {
        ResolutionKind::none, ResolutionKind::sv_wire,
        ResolutionKind::sv_wire };
    std::array<Process, 2U> programs;

    Process producer;
    producer.id = 0U;
    producer.name = "deferred_peer_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 2U;
    producer.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4 };
    producer.static_sensitivity = { { 0U, EdgeKind::any } };
    producer.driver_regions = { { 1U, 0U, 1U, true } };
    producer.operations = {
        ReadSignal { 0U, 0U },
        LoadConstant { 1U, PackedLogic4 { 1U, Logic4::zero } },
        WriteUpdate { 1U, 1U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U }
    };
    programs[0U] = producer;

    Process consumer;
    consumer.id = 1U;
    consumer.name = "deferred_peer_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    consumer.register_count = 3U;
    consumer.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    consumer.static_sensitivity = { { 0U, EdgeKind::any } };
    consumer.driver_regions = { { 2U, 0U, 1U, true } };
    consumer.operations = {
        ReadSignal { 0U, 1U }, ReadSignal { 1U, 0U },
        Binary { BinaryOperator::bit_and, 2U, 0U, 1U },
        WriteUpdate { 2U, 2U, SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { }, Jump { 0U }
    };
    programs[1U] = consumer;

    auto jit = std::make_unique<LlvmJit>(options);
    auto counters = std::make_shared<ProviderCounters>();
    auto gate = std::make_shared<FrontierGate>();
    gate->counters = counters;
    auto production_provider = ::fsim::app::application_detail::
        make_llvm_region_kernel_backend_provider(options,
            "native-frontier-v2-deferred-peer-lifecycle-v1");
    require(static_cast<bool>(production_provider),
        "the deferred-peer fixture has the production LLVM V2 provider");
    auto provider = std::make_shared<CountingFrontierProvider>(
        production_provider, counters, gate, false);
    Interpreter interpreter;
    interpreter.set_region_kernel_backend_provider(provider);
    const auto trigger = interpreter.add_signal({ "deferred_peer.trigger",
        PackedLogic4 { 1U, Logic4::zero } });
    const auto middle = interpreter.add_signal({ "deferred_peer.middle",
        PackedLogic4 { 1U, Logic4::zero }, ResolutionKind::sv_wire });
    const auto output = interpreter.add_signal({ "deferred_peer.output",
        PackedLogic4 { 1U, Logic4::zero }, ResolutionKind::sv_wire });
    require(trigger == 0U && middle == 1U && output == 2U,
        "the deferred-peer fixture keeps its dense signal layout");

    for (const auto& process : programs) {
        const auto registered = interpreter.add_process(process);
        require(registered == process.id,
            "the producer and consumer keep their stable process IDs");
        jit->add_process(process.name, process, signal_widths, signal_kinds);
    }

    const auto make_executor = [&](const ProcessId process) {
        const auto& program = programs.at(process);
        const auto handle = jit->lookup(program.name);
        require(static_cast<bool>(handle),
            "LLVM retains each deferred-peer process body");
        auto executor = std::make_unique<
            ::fsim::app::application_detail::LlvmProcessExecutor>(
                *jit, handle, program, signal_widths, signal_kinds,
                signal_resolutions,
                std::shared_ptr<const ProcessSignalRemap> { }, process);
        require(executor->program_access_binding() != nullptr
                && executor->program_access_binding()->valid()
                && executor->region_kernel_equivalent()
                && executor->cohort_manages_process_state()
                && executor->region_kernel_completion_has_no_persistent_registers(),
            "each installed executor certifies the exact parked whole-write body");
        // Keep the deferred executor concrete: install_deferred_executor
        // must restore interpreter-frame registers through ProcessExecutor's
        // writable-register interface before the first compiled activation.
        return executor;
    };

    interpreter.set_process_executor(0U,
        std::make_unique<CountingProcessExecutor>(make_executor(0U), counters));
    auto pending_consumer_executor
        = std::make_shared<std::unique_ptr<ProcessExecutor>>(
            make_executor(1U));
    const auto expected_access
        = *pending_consumer_executor->get()->program_access_binding();
    auto consumer_ready = std::make_shared<std::atomic<bool>>(false);
    auto ready_calls = std::make_shared<std::atomic<std::size_t>>(0U);
    auto take_calls = std::make_shared<std::atomic<std::size_t>>(0U);
    DeferredProcessExecutorContract contract;
    contract.expected_access = expected_access;
    contract.callbacks_observation_safe = true;
    contract.expected_region_kernel_equivalent = true;
    interpreter.set_deferred_process_executor(1U,
        [consumer_ready, ready_calls] {
            ready_calls->fetch_add(1U, std::memory_order_relaxed);
            return consumer_ready->load(std::memory_order_acquire);
        },
        [pending_consumer_executor, take_calls] {
            take_calls->fetch_add(1U, std::memory_order_relaxed);
            return std::move(*pending_consumer_executor);
        }, contract);

    interpreter.start();
    auto& impl = OwnedDriverDemotionTestAccess::implementation(interpreter);
    require(impl.region_component_by_process.size() == 2U
            && impl.region_component_by_process[0U]
                == impl.region_component_by_process[1U],
        "the offered producer and unavailable reader belong to one activation component");
    const auto component = impl.region_component_by_process[0U];
    require(component < impl.region_frontier_runtime_by_component.size(),
        "the two-member component has a retained V2 runtime");
    const auto runtime = impl.region_frontier_runtime_by_component[component];
    require(runtime && runtime->backend && runtime->backend->executor
            && runtime->backend->executor->layout().abi_version
                == kRegionFrontierAbiVersionV2
            && runtime->backend->executor->layout().member_count == 2U
            && runtime->frame_initialized
            && !runtime->scheduler_state_seeded && !runtime->invalidated,
        "a real V2 backend is prepared for both members before the first offer");
    const auto generation = runtime->runtime_generation;
    require(generation != 0U && impl.region_runtime_generation == generation
            && impl.processes[0U].executor
            && !impl.processes[1U].executor
            && impl.processes[1U].cold().deferred_executor,
        "the ready producer and not-yet-installed consumer share the certified runtime generation");
    const auto first_runtime = runtime.get();

    const auto require_pristine_runtime = [&](const char* const message) {
        require(impl.region_frontier_runtime_by_component[component].get()
                    == first_runtime
                && runtime->runtime_generation == generation
                && impl.region_runtime_generation == generation
                && !runtime->invalidated
                && !runtime->scheduler_state_seeded
                && runtime->native_member_dispatches == 0U
                && runtime->frame.scheduler_task_count == 0U
                && runtime->frame.scheduler_task_cursor == 0U
                && runtime->frame.pending_write_count == 0U
                && runtime->frame.current_pending_write == UINT32_MAX
                && runtime->frame.staged_event_count == 0U
                && runtime->frame.committed_signal_count == 0U
                && runtime->frame.generic_update_ack_count == 0U
                && runtime->boundary_callback_started_slot
                    == std::numeric_limits<std::uint32_t>::max()
                && std::ranges::all_of(runtime->ready_words,
                    [](const std::uint64_t word) { return word == 0U; })
                && std::ranges::all_of(runtime->members,
                    [](const RegionFrontierMemberV1& member) {
                        return member.flags == 0U;
                    }),
            message);
    };

    const auto run_until = [&](const SimulationTick time) {
        const auto result = interpreter.run(time);
        require(result.status == RunStatus::completed
                || result.status == RunStatus::time_limit,
            "the deferred-peer checked or native callback drains its event");
    };
    run_until(0U);
    require_pristine_runtime(
        "the startup checked path leaves the retained V2 frame pristine while the deferred peer is unavailable");
    require(impl.processes[0U].waiting_on_static
            && impl.processes[1U].waiting_on_static
            && !impl.processes[1U].executor
            && !consumer_ready->load(std::memory_order_acquire),
        "both members are parked before the deferred-peer offer is measured");
    const auto middle_roles
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, middle, 0U);
    const auto output_roles
        = OwnedDriverDemotionTestAccess::packed_a4_values(
            interpreter, output, 1U);
    const auto readiness_checks_before_offer
        = ready_calls->load(std::memory_order_relaxed);
    const auto checked_resumes_before_offer
        = counters->checked_resumes.load(std::memory_order_relaxed);

    interpreter.schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::one }, 1U, 0U);
    run_until(1U);
    require_pristine_runtime(
        "the first triggered checked activation leaves the V2 frame pristine with an uninstalled peer");
    require(ready_calls->load(std::memory_order_relaxed) != 0U
            && take_calls->load(std::memory_order_relaxed) == 0U
            && !impl.processes[1U].executor
            && counters->checked_resumes.load(std::memory_order_relaxed)
                > checked_resumes_before_offer
            && ready_calls->load(std::memory_order_relaxed)
                > readiness_checks_before_offer
            && OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, middle, 0U) == middle_roles
            && OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, output, 1U) == output_roles,
        "the unavailable peer takes checked fallback and preserves all four A4 roles");

    consumer_ready->store(true, std::memory_order_release);
    const auto checked_resumes_before_install
        = counters->checked_resumes.load(std::memory_order_relaxed);
    interpreter.schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::zero }, 2U, 0U);
    run_until(2U);
    require_pristine_runtime(
        "the second triggered checked activation preserves the retained V2 frame while installing the peer");
    require(impl.processes[1U].executor != nullptr
            && take_calls->load(std::memory_order_relaxed) == 1U
            && counters->checked_resumes.load(std::memory_order_relaxed)
                > checked_resumes_before_install
            && OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, middle, 0U) == middle_roles
            && OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, output, 1U) == output_roles,
        "the second checked fallback installs the peer only through its real scheduler activation");

    const auto checked_resumes_before_native
        = counters->checked_resumes.load(std::memory_order_relaxed);
    const auto frontier_entries_before_native
        = counters->frontier_entries.load(std::memory_order_relaxed);
    interpreter.schedule_signal_at(trigger,
        PackedLogic4 { 1U, Logic4::one }, 3U, 0U);
    run_until(3U);
    require(impl.region_frontier_runtime_by_component[component].get()
                == first_runtime
            && runtime->runtime_generation == generation
            && !runtime->invalidated && runtime->scheduler_state_seeded
            && runtime->native_member_dispatches >= 2U
            && counters->frontier_entries.load(std::memory_order_relaxed)
                > frontier_entries_before_native
            && counters->frontier_delegate_calls.load(std::memory_order_relaxed)
                != 0U
            && counters->checked_resumes.load(std::memory_order_relaxed)
                == checked_resumes_before_native,
        "the same retained V2 runtime executes both members natively after checked installation, without replay");
    require(OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, middle, 0U)
                == middle_roles
            && OwnedDriverDemotionTestAccess::packed_a4_values(
                interpreter, output, 1U)
                == output_roles,
        "the recovered V2 execution preserves the four current/LAST/stored/owner roles");
}

} // namespace
} // namespace fsim::tests::app::frontier_v2_first

int main()
{
    try {
        using namespace fsim::tests::app::frontier_v2_first;
        check_generic_v2_success(true, JitOptimizationLevel::o0);
        check_generic_v2_decline_uses_checked_callback_once(
            JitOptimizationLevel::o0);
        check_generic_v2_success(true, JitOptimizationLevel::o2);
        check_generic_v2_decline_uses_checked_callback_once(
            JitOptimizationLevel::o2);
        check_generic_v2_success(false, JitOptimizationLevel::o0);
        check_generic_v1_fallback(FrontierProviderMode::absent);
        check_generic_v1_fallback(FrontierProviderMode::decline_creation);
        check_generic_hook_gate_preserves_v1_construction();
        check_systemverilog_v2_only_route();
        for (const auto optimization : {
                 JitOptimizationLevel::o0, JitOptimizationLevel::o2 }) {
            check_systemverilog_v2_fallback_without_v1(
                FrontierProviderMode::decline_creation, false, optimization);
            check_systemverilog_v2_fallback_without_v1(
                FrontierProviderMode::available, true, optimization);
        }
        check_vhdl_projected_stays_on_v1();
        check_generic_automatic_ticket_pool_sizing(65U);
        check_generic_automatic_ticket_pool_sizing(129U);
        check_generic_large_connected_component_ticket_sizing(65U);
        check_generic_large_connected_component_ticket_sizing(129U);
        check_deferred_peer_checked_to_native_transition(JitOptimizationLevel::o0);
        check_deferred_peer_checked_to_native_transition(JitOptimizationLevel::o2);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V2-first Generic construction test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
