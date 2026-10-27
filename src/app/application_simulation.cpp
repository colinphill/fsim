// SPDX-License-Identifier: Apache-2.0
#include "application_class_execution.hpp"
#include "application_simulation_internal.hpp"
#include "application_uvm_registry.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/app/sdf_reannotation.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/coverage_database_merge.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <iostream>
#include <iterator>
#include <numeric>
#include <ranges>
#include <set>
#include <thread>
#include <tuple>
#include <type_traits>

namespace fsim::app {
using namespace application_detail;

Simulation::Simulation(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine,
    const SystemVerilogVpiRuntimeUpdates vpi_runtime_updates)
    : impl_(
          std::make_unique<Impl>(std::move(project), max_deltas,
              // `compiled` is an alias of engine v4: the per-process JIT is
              // retired (docs/simulation-engine-v4-progress.md).
              engine == SimulationEngine::compiled ? SimulationEngine::v4
                                                   : engine,
              vpi_runtime_updates))
{
    auto* const lifetime = impl_.get();
    impl_->hdl_vcd.remove_observer = [lifetime](const std::uint64_t token) {
        lifetime->remove_signal_observer(token);
    };
    impl_->hdl_vcd.current_time = [lifetime] {
        return lifetime->interpreter->scheduler().now();
    };
    application_detail::attach_hdl_vcd_control(
        *this, impl_->hdl_vcd, impl_->built.file_root);
}
Simulation::~Simulation() = default;
Simulation::Simulation(Simulation&& other) noexcept
    : impl_(std::move(other.impl_))
{
    if (impl_) {
        impl_->hdl_vcd.simulation = this;
    }
}

Simulation& Simulation::operator=(Simulation&& other) noexcept
{
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (impl_) {
            impl_->hdl_vcd.simulation = this;
        }
    }
    return *this;
}

const runtime::SystemVerilogClassPropertyValue&
Simulation::read_class_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view property) const
{
    return impl_->class_heap.property(handle, property);
}

void Simulation::deposit_class_property(
    const runtime::SystemVerilogClassHandle handle,
    const std::string_view property,
    runtime::PackedLogic4 value)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class heap is no longer mutable" };
    }
    auto& destination = impl_->class_heap.property(handle, property);
    if (destination.packed.width() == 0
        || destination.packed.width() != value.width()) {
        throw std::invalid_argument {
            "class property deposit requires an equal-width packed property"
        };
    }
    if (destination.kind == runtime::SystemVerilogClassPropertyKind::Bit2) {
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            if (value.get(bit) != runtime::Logic4::zero
                && value.get(bit) != runtime::Logic4::one) {
                throw std::invalid_argument {
                    "class property deposit would place X/Z into two-state storage"
                };
            }
        }
    }
    destination.packed = std::move(value);
    if (impl_->class_property_change_hook) {
        impl_->class_property_change_hook(
            handle, property, destination.packed,
            impl_->interpreter->scheduler().now(),
            impl_->interpreter->scheduler().delta());
    }
}

runtime::SystemVerilogClassInvocationResult
Simulation::invoke_class_method(
    const std::string_view canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue>& actuals,
    const std::optional<std::uint32_t> virtual_slot)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation class methods are no longer mutable" };
    }
    return impl_->invoke_class_method(
        canonical_method, this_handle, actuals, virtual_slot);
}

runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::execute_uvm_function_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation UVM phases are no longer mutable" };
    }
    return impl_->execute_uvm_function_phase(phase);
}

runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::execute_uvm_task_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase,
    const UvmTaskPhaseContinuation& continuation)
{
    if (impl_->lifecycle == Impl::Lifecycle::finished
        || impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error { "simulation UVM phases are no longer mutable" };
    }
    return impl_->execute_uvm_task_phase(phase, continuation);
}

void Simulation::schedule_class_method(
    const runtime::SimulationTick time,
    const runtime::StableOrder stable_order,
    std::string canonical_method,
    const runtime::SystemVerilogClassHandle this_handle,
    std::vector<runtime::SystemVerilogClassMethodValue> actuals,
    const std::optional<std::uint32_t> virtual_slot,
    ClassMethodCompletion completion)
{
    if (impl_->lifecycle != Impl::Lifecycle::ready) {
        throw std::logic_error {
            "class methods may only be scheduled on a ready simulation"
        };
    }
    impl_->interpreter->scheduler().schedule_at(
        time,
        runtime::SchedulerPhase::active,
        stable_order,
        [implementation = impl_.get(),
            canonical_method = std::move(canonical_method),
            this_handle,
            actuals = std::move(actuals),
            virtual_slot,
            completion = std::move(completion)](runtime::Scheduler&) mutable {
            const auto result = implementation->invoke_class_method(
                canonical_method, this_handle, actuals, virtual_slot);
            if (completion)
                completion(result, actuals);
        });
}

void Simulation::deposit_string_object(
    const runtime::simir::StringObjectId object,
    const std::string_view value)
{
    impl_->interpreter->deposit_string_object(object, value);
}

void Simulation::deposit_container_object(
    const runtime::simir::ContainerObjectId object,
    runtime::simir::ContainerValue value)
{
    impl_->interpreter->deposit_container_object(
        object, std::move(value));
    impl_->refresh_observation_hooks();
}

void Simulation::release_signal(const SignalId signal)
{
    impl_->interpreter->release_signal(signal);
    impl_->refresh_observation_hooks();
}

bool Simulation::signal_is_forced(const SignalId signal) const
{
    return impl_->interpreter->signal_is_forced(signal);
}

void Simulation::start()
{
    if (impl_->lifecycle != Impl::Lifecycle::ready) {
        throw std::logic_error { "simulation is not ready to start" };
    }
    try {
        impl_->start_vpi();
        impl_->start_systemc();
        impl_->refresh_observation_hooks();
        impl_->start_interpreter();
    } catch (...) {
        impl_->lifecycle = Impl::Lifecycle::poisoned;
        throw;
    }
}

runtime::RunResult Simulation::run(
    const std::optional<SimulationTick> until)
{
    if (impl_->lifecycle == Impl::Lifecycle::poisoned) {
        throw std::logic_error(
            "simulation is unavailable after a fatal runtime error");
    }
    if (impl_->lifecycle == Impl::Lifecycle::finished) {
        throw std::logic_error("simulation has finished");
    }
    if (until && *until < now()) {
        throw std::invalid_argument("run time limit is before the current time");
    }
    const bool profile_phase_split
        = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    const auto profile_run_begin = profile_phase_split
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point { };
    try {
        impl_->start_vpi();
        impl_->start_systemc();
        impl_->refresh_observation_hooks();
        impl_->start_interpreter();
        const auto interpreter_begin = profile_phase_split
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point { };
        // A closed static kernel may run its own time steps only when
        // nothing here observes the steps in between.
        runtime::simir::InterpreterProgramAccess::set_static_kernel_time_warp(
            *impl_->interpreter,
            !until && !(impl_->vhdl_psl && impl_->vhdl_psl->active())
                && !impl_->signal_change_hook
                && impl_->signal_observers.empty()
                && !impl_->scalar_signal_change_hook
                && impl_->scalar_signal_observers.empty()
                && impl_->safe_point_observers.empty()
                && impl_->vpi_signal_handles.empty()
                && !impl_->vpi_runtime_updates_enabled
                && impl_->built.systemc_hierarchies.empty()
                && !impl_->built.systemc_hierarchy
                // Foreign libraries and VCD observers may start observing
                // or depositing at any time step.
                && !impl_->built.kernel_access.foreign
                && !impl_->built.kernel_access.observe_all
                && impl_->built.kernel_access.observed.empty());
        auto result = impl_->interpreter->run(until);
        if (profile_phase_split) {
            const auto milliseconds = [](const auto duration) {
                return std::chrono::duration<double, std::milli> { duration }
                    .count();
            };
            const auto interpreter_end = std::chrono::steady_clock::now();
            std::cerr << "fsim-profile: run pre_interpreter_ms="
                      << milliseconds(interpreter_begin - profile_run_begin)
                      << " interpreter_ms="
                      << milliseconds(interpreter_end - interpreter_begin)
                      << '\n';
            // Engine v4 gate: a compiled simulation interprets nothing.
            const auto counts = runtime::simir::InterpreterProgramAccess::
                interpretation_counts(*impl_->interpreter);
            // A deoptimization continues in operation-tier code: it counts
            // as interpretation only through the operations it runs.
            std::cerr << "fsim-profile: static-kernel-run interpreted="
                      << (counts.executes + counts.kernel_bytecode_runs
                             + counts.kernel_operations)
                      << " executes=" << counts.executes
                      << " kernel_bytecode_runs=" << counts.kernel_bytecode_runs
                      << " kernel_operations=" << counts.kernel_operations
                      << " kernel_deopts=" << counts.kernel_deopts
                      << " constant_drivers=" << counts.constant_drivers
                      << " kernel_operations_by=";
            for (const auto& [cause, count] : counts.kernel_operations_by) {
                std::cerr << cause << ':' << count << ',';
            }
            std::cerr << '\n';
            for (const auto& [program, count] : counts.executed_programs) {
                auto name = program;
                std::ranges::replace(name, ' ', '_');
                std::cerr << "fsim-profile: static-kernel-executed program=" << name
                          << " count=" << count << '\n';
            }
        }
        if (impl_->vpi_control->state()
            == runtime::SystemVerilogVpiControlState::Finished) {
            const auto final_result = impl_->interpreter->finish();
            result.time = final_result.time;
            result.delta = final_result.delta;
            result.callbacks_executed += final_result.callbacks_executed;
            result.status = runtime::RunStatus::stopped;
        }
        if (result.status == runtime::RunStatus::completed
            || impl_->interpreter->stopped_by_design()
            || impl_->vpi_control->state()
                == runtime::SystemVerilogVpiControlState::Finished) {
            impl_->finish_concurrent_assertions(result.time, result.delta);
            impl_->vhdl_psl->finish(result.time, result.delta);
            impl_->save_coverage_database();
            impl_->end_vpi();
            impl_->end_systemc(result.time);
            impl_->lifecycle = Impl::Lifecycle::finished;
        }
        impl_->refresh_observation_hooks();
        return result;
    } catch (...) {
        impl_->lifecycle = Impl::Lifecycle::poisoned;
        throw;
    }
}

void Simulation::request_stop() noexcept
{
    impl_->interpreter->scheduler().request_stop();
}

void Simulation::clear_stop() noexcept
{
    if (impl_->lifecycle == Impl::Lifecycle::ready) {
        impl_->interpreter->scheduler().clear_stop();
    }
}

SimulationTick Simulation::now() const noexcept
{
    return impl_->interpreter->scheduler().now();
}

std::uint64_t Simulation::delta() const noexcept
{
    return impl_->interpreter->scheduler().delta();
}

bool Simulation::has_pending() const noexcept
{
    return impl_->interpreter->scheduler().has_pending();
}

bool Simulation::finished() const noexcept
{
    return impl_->lifecycle == Impl::Lifecycle::finished;
}

bool Simulation::paused_in_final() const noexcept
{
    return impl_->interpreter->paused_in_final();
}

bool Simulation::poisoned() const noexcept
{
    return impl_->lifecycle == Impl::Lifecycle::poisoned;
}

// The per-process JIT was removed in the engine-v4 migration; this API is
// kept as a stub that always reports zero.
std::size_t Simulation::compiled_process_count() const noexcept
{
    return 0U;
}

// Stub: the per-process JIT no longer exists, so no modules are compiled.
std::size_t Simulation::compiled_module_count() const noexcept
{
    return 0U;
}

// Stub: the per-process JIT native cache no longer exists; all counters are
// zero. The v4 kernel cache is reported by the planner and codegen profiles.
NativeCacheStatistics Simulation::native_cache_statistics(
    const bool) const noexcept
{
    return { };
}

// Stub: there is no background native compilation to await.
void Simulation::await_native_compilation() const
{
}

// Stub: there is no background native compilation to await.
void Simulation::await_all_native_compilation() const
{
}

std::vector<ConcurrentAssertionCoverage>
Simulation::concurrent_assertion_coverage() const
{
    auto result = impl_->concurrent_assertion_coverage;
    std::ranges::sort(
        result, { }, &ConcurrentAssertionCoverage::process);
    return result;
}

const std::vector<ConcurrentAssertionEvent>&
Simulation::concurrent_assertion_events() const noexcept
{
    return impl_->concurrent_assertion_events;
}

const runtime::SystemVerilogCoverageState&
Simulation::systemverilog_coverage() const noexcept
{
    return impl_->built.systemverilog_coverage;
}

const std::vector<runtime::VhdlPslAttemptSnapshot>&
Simulation::vhdl_psl_attempts() const noexcept
{
    return impl_->vhdl_psl->attempts();
}

std::vector<ConcurrentAssertionCoverage> Simulation::vhdl_psl_coverage() const
{
    return impl_->vhdl_psl->coverage();
}

runtime::VhdlVhpiObjectRegistry& Simulation::vhdl_vhpi_objects()
{
    if (!impl_->vhdl_vhpi_registry) {
        impl_->vhdl_vhpi_registry
            = application_detail::make_vhdl_debug_registry(impl_->built);
    }
    return *impl_->vhdl_vhpi_registry;
}

const runtime::VhdlVhpiObjectRegistry& Simulation::vhdl_vhpi_objects() const
{
    if (!impl_->vhdl_vhpi_registry) {
        impl_->vhdl_vhpi_registry
            = application_detail::make_vhdl_debug_registry(impl_->built);
    }
    return *impl_->vhdl_vhpi_registry;
}

runtime::SystemVerilogVpiObjectRegistry&
Simulation::systemverilog_vpi_objects() noexcept
{
    return *impl_->vpi_registry;
}

const runtime::SystemVerilogVpiObjectRegistry&
Simulation::systemverilog_vpi_objects() const noexcept
{
    return *impl_->vpi_registry;
}

runtime::SystemVerilogVpiTimeService&
Simulation::systemverilog_vpi_time() noexcept
{
    return *impl_->vpi_time;
}

const runtime::SystemVerilogVpiTimeService&
Simulation::systemverilog_vpi_time() const noexcept
{
    return *impl_->vpi_time;
}

runtime::SystemVerilogVpiDataReadService&
Simulation::systemverilog_vpi_data_read() noexcept
{
    return *impl_->vpi_data_read;
}

const runtime::SystemVerilogVpiDataReadService&
Simulation::systemverilog_vpi_data_read() const noexcept
{
    return *impl_->vpi_data_read;
}

runtime::SystemVerilogVpiCallbackManager&
Simulation::systemverilog_vpi_callbacks() noexcept
{
    return *impl_->vpi_callbacks;
}

const runtime::SystemVerilogVpiCallbackManager&
Simulation::systemverilog_vpi_callbacks() const noexcept
{
    return *impl_->vpi_callbacks;
}

runtime::SystemVerilogVpiValueControl&
Simulation::systemverilog_vpi_values() noexcept
{
    return *impl_->vpi_values;
}

const runtime::SystemVerilogVpiValueControl&
Simulation::systemverilog_vpi_values() const noexcept
{
    return *impl_->vpi_values;
}

runtime::SystemVerilogVpiControlService&
Simulation::systemverilog_vpi_control() noexcept
{
    return *impl_->vpi_control;
}

const runtime::SystemVerilogVpiControlService&
Simulation::systemverilog_vpi_control() const noexcept
{
    return *impl_->vpi_control;
}

runtime::SystemVerilogVpiAssertionApi&
Simulation::systemverilog_vpi_assertions() noexcept
{
    return *impl_->vpi_assertions;
}

const runtime::SystemVerilogVpiAssertionApi&
Simulation::systemverilog_vpi_assertions() const noexcept
{
    return *impl_->vpi_assertions;
}

void Simulation::set_safe_point_hook(SafePointHook hook)
{
    impl_->safe_point_hook = std::move(hook);
}

std::uint64_t Simulation::add_safe_point_hook(SafePointHook hook)
{
    if (!hook) {
        throw std::invalid_argument("safe-point observer cannot be empty");
    }
    if (impl_->next_safe_point_observer == 0) {
        throw std::overflow_error("safe-point observer token space exhausted");
    }
    const auto token = impl_->next_safe_point_observer++;
    impl_->safe_point_observers.emplace(token, std::move(hook));
    return token;
}

void Simulation::remove_safe_point_hook(const std::uint64_t token) noexcept
{
    impl_->safe_point_observers.erase(token);
}

SdfReannotationCommitResult Simulation::commit_sdf_reannotation(
    const SdfReannotationApplication& application)
{
    static_assert(std::is_nothrow_move_assignable_v<
        elaboration::ElaboratedDesign>);
    auto replacement = elaboration::ElaboratedDesign::from_state(
        application.design().state());
    if (!replacement)
        throw std::invalid_argument("SDF reannotation has invalid design state");
    auto result = fsim::app::commit_sdf_reannotation(
        application, *impl_->interpreter);
    if (result.ok())
        impl_->built.design = std::move(*replacement);
    return result;
}

bool Simulation::at_sdf_reannotation_safe_point() const noexcept
{
    return impl_->interpreter->scheduler().at_safe_point();
}

void Simulation::set_execution_point_hook(ExecutionPointHook hook)
{
    impl_->interpreter->set_execution_point_hook(std::move(hook));
}

void Simulation::set_systemverilog_plusargs(
    const std::span<const std::string> plusargs)
{
    impl_->interpreter->set_plusargs(plusargs);
}

void Simulation::set_system_command_hook(SystemCommandHook hook)
{
    impl_->interpreter->set_system_command_hook(std::move(hook));
}

void Simulation::set_systemverilog_dpi_function_hook(
    SystemVerilogDpiFunctionHook hook)
{
    impl_->dpi_function_hook = hook;
    impl_->interpreter->set_dpi_function_call_hook(std::move(hook));
}

Simulation::SystemVerilogDpiFunctionHook
Simulation::systemverilog_dpi_function_hook() const
{
    return impl_->dpi_function_hook;
}

void Simulation::set_vcd_control_hook(VcdControlHook hook)
{
    impl_->interpreter->set_vcd_control_hook(std::move(hook));
}

void Simulation::set_output_hook(OutputHook hook)
{
    if (impl_->trusted_builtin_stdout_output) {
        // Revoke the text-only contract before installing a caller-provided
        // callback. The runtime setter prepares its replacement before commit.
        impl_->interpreter->set_output_hook(impl_->interpreter_output_hook);
        impl_->trusted_builtin_stdout_output = false;
    }
    impl_->output_hook = std::move(hook);
}

void Simulation::set_builtin_stdout_output()
{
    OutputHook stdout_sink = [](
        const runtime::simir::ProcessId,
        const std::string_view text,
        const bool newline,
        const SimulationTick,
        const std::uint64_t) {
        if (!text.empty()) {
            (void)std::fwrite(text.data(), 1U, text.size(), stdout);
        }
        if (newline) {
            (void)std::fputc('\n', stdout);
        }
    };
    impl_->interpreter->set_trusted_text_output_hook(
        impl_->interpreter_output_hook);
    impl_->output_hook = std::move(stdout_sink);
    impl_->trusted_builtin_stdout_output = true;
}

void Simulation::set_report_hook(ReportHook hook)
{
    impl_->trusted_text_report_hook = false;
    impl_->report_hook = std::move(hook);
}

void Simulation::set_trusted_text_report_hook(ReportHook hook)
{
    impl_->report_hook = std::move(hook);
    impl_->trusted_text_report_hook = static_cast<bool>(impl_->report_hook);
}

void Simulation::set_concurrent_assertion_hook(
    ConcurrentAssertionHook hook)
{
    impl_->concurrent_assertion_hook = std::move(hook);
}

void Simulation::set_vhdl_psl_attempt_hook(VhdlPslAttemptHook hook)
{
    impl_->vhdl_psl_attempt_hook = std::move(hook);
}

std::uint64_t Simulation::add_uvm_activity_hook(UvmActivityHook hook)
{
    return impl_->uvm_activity.add_observer(std::move(hook));
}

void Simulation::remove_uvm_activity_hook(
    const std::uint64_t token) noexcept
{
    impl_->uvm_activity.remove_observer(token);
}

void Simulation::set_class_property_change_hook(
    ClassPropertyChangeHook hook)
{
    impl_->class_property_change_hook = std::move(hook);
}

void Simulation::set_class_static_property_change_hook(
    ClassStaticPropertyChangeHook hook)
{
    impl_->class_static_property_change_hook = std::move(hook);
}

} // namespace fsim::app
