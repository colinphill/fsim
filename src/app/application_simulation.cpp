// SPDX-License-Identifier: Apache-2.0
#include "application_class_execution.hpp"
#include "application_simulation_internal.hpp"
#include "application_uvm_registry.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/coverage_database_merge.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <iterator>
#include <numeric>
#include <ranges>
#include <set>
#include <thread>
#include <tuple>

namespace fsim::app {
using namespace application_detail;

Simulation::Simulation(
    BuiltProject project,
    const std::uint64_t max_deltas,
    const SimulationEngine engine,
    const SystemVerilogVpiRuntimeUpdates vpi_runtime_updates)
    : impl_(
          std::make_unique<Impl>(
              std::move(project), max_deltas, engine, vpi_runtime_updates))
{
    auto* const lifetime = impl_.get();
    impl_->hdl_vcd.remove_observer = [lifetime](const std::uint64_t token) {
        lifetime->signal_observers.erase(token);
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
}

void Simulation::release_signal(const SignalId signal)
{
    impl_->interpreter->release_signal(signal);
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
        impl_->interpreter->start();
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
#if defined(FSIM_HAS_LLVM)
    if (!impl_->jit_overlap_startup) {
        await_native_compilation();
    }
    // A bounded run that cannot amortize large-kernel lowering should finish
    // without launching it (or waiting for it during destruction). Unbounded
    // and longer runs promote the background tier while simulation advances.
    constexpr SimulationTick minimum_background_jit_run_ticks = 2'000'000U;
    if (!until || *until - now() > minimum_background_jit_run_ticks) {
        impl_->request_background_jit_compilation();
    }
    // Diagnostic gate for measuring the warm-cache cost of publishing every
    // selected recurring kernel before any interpreter activation can make
    // its frame ineligible for deferred native promotion.
    if (std::getenv("FSIM_JIT_SYNCHRONIZE_RECURRING") != nullptr) {
        impl_->request_background_jit_compilation(true);
        for (const auto& compilation : impl_->jit_compilations) {
            compilation.wait();
        }
    }
#endif
    try {
        impl_->start_vpi();
        impl_->start_systemc();
        auto result = impl_->interpreter->run(until);
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

bool Simulation::poisoned() const noexcept
{
    return impl_->lifecycle == Impl::Lifecycle::poisoned;
}

std::size_t Simulation::compiled_process_count() const noexcept
{
    return impl_->compiled_processes;
}

std::size_t Simulation::compiled_module_count() const noexcept
{
    return impl_->compiled_modules;
}

NativeCacheStatistics Simulation::native_cache_statistics(
    const bool synchronize) const noexcept
{
#if defined(FSIM_HAS_LLVM)
    if (impl_->jit) {
        if (synchronize) {
            impl_->request_background_jit_compilation(true);
            for (const auto& compilation : impl_->jit_compilations) {
                compilation.wait();
            }
        }
        const auto statistics = impl_->jit->cache_statistics();
        return {
            statistics.hits,
            statistics.misses,
            statistics.stores,
            statistics.rejected_entries,
            statistics.load_failures,
            statistics.store_failures,
            statistics.pruned_entries,
            statistics.pruned_bytes,
            statistics.prune_failures,
        };
    }
#else
    static_cast<void>(synchronize);
#endif
    return { };
}

void Simulation::await_native_compilation() const
{
#if defined(FSIM_HAS_LLVM)
    for (const auto& compilation : impl_->jit_startup_compilations) {
        static_cast<void>(compilation.get());
    }
    impl_->interpreter->materialize_ready_process_executors();
#endif
}

void Simulation::await_all_native_compilation() const
{
#if defined(FSIM_HAS_LLVM)
    impl_->request_background_jit_compilation(true);
    if (impl_->jit_materialization.valid()) {
        impl_->jit_materialization.get();
    }
    for (const auto& compilation : impl_->jit_compilations) {
        static_cast<void>(compilation.get());
    }
    impl_->interpreter->materialize_ready_process_executors();
#endif
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

const frontend::SystemVerilogCoverageState&
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

void Simulation::set_execution_point_hook(ExecutionPointHook hook)
{
#if defined(FSIM_HAS_LLVM)
    if (hook && impl_->jit && !impl_->jit_debug_instrumentation) {
        for (const auto process : impl_->jit_processes) {
            if (!impl_->interpreter->clear_process_executor(process)) {
                // A late observer still applies to interpreter-owned processes;
                // already-running native frames cannot be replaced safely.
                break;
            }
        }
        if (impl_->lifecycle == Impl::Lifecycle::ready) {
            impl_->jit_processes.clear();
        }
    }
#endif
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

void Simulation::set_vcd_control_hook(VcdControlHook hook)
{
    impl_->interpreter->set_vcd_control_hook(std::move(hook));
}

void Simulation::set_output_hook(OutputHook hook)
{
    impl_->output_hook = std::move(hook);
}

void Simulation::set_report_hook(ReportHook hook)
{
    impl_->report_hook = std::move(hook);
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
