// SPDX-License-Identifier: Apache-2.0
#include "application_class_execution.hpp"
#include "application_simulation_internal.hpp"
#include "application_uvm_registry.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/app/sdf_reannotation.hpp"
#include "fsim/elaboration/sdf_net_delay_split.hpp"
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
    // The method call and its completion belong to the embedding program, so
    // a run-state save refuses while it is pending (FSIM-SAVE-0004).
    impl_->interpreter->scheduler().schedule_at(
        time,
        runtime::SchedulerPhase::active,
        stable_order,
        runtime::host_closure_task("Simulation::schedule_class_method",
            [implementation = impl_.get(),
                canonical_method = std::move(canonical_method),
                this_handle,
                actuals = std::move(actuals),
                virtual_slot,
                completion = std::move(completion)](
                runtime::Scheduler&) mutable {
                const auto result = implementation->invoke_class_method(
                    canonical_method, this_handle, actuals, virtual_slot);
                if (completion)
                    completion(result, actuals);
            }));
}

std::uint32_t Simulation::mcd_open(const std::string_view name)
{
    return impl_->interpreter->mcd_open(name);
}

std::uint32_t Simulation::mcd_close(const std::uint32_t descriptor)
{
    return impl_->interpreter->mcd_close(descriptor);
}

std::optional<std::string> Simulation::mcd_name(
    const std::uint32_t descriptor) const
{
    return impl_->interpreter->mcd_name(descriptor);
}

bool Simulation::mcd_write(
    const std::uint32_t descriptor, const std::string_view text)
{
    return impl_->interpreter->mcd_write(descriptor, text);
}

bool Simulation::mcd_flush(const std::uint32_t descriptor)
{
    return impl_->interpreter->mcd_flush(descriptor);
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

bool Simulation::signal_is_assigned(const SignalId signal) const
{
    return impl_->interpreter->signal_is_assigned(signal);
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
                && !impl_->simulation_control_tasks
                && !impl_->signal_change_hook
                && impl_->signal_observers.empty()
                && !impl_->scalar_signal_change_hook
                && impl_->scalar_signal_observers.empty()
                && impl_->safe_point_observers.empty()
                && impl_->built.systemc_hierarchies.empty()
                && !impl_->built.systemc_hierarchy);
        // A loaded foreign library alone observes nothing: one that starts
        // observing time steps or values, or writing signals, ends the
        // kernel's own time steps then (Interpreter::open_external_observation,
        // the observation hooks of a watched object).
        auto result = impl_->interpreter->run(until);
        // A reset ends the run at a safe point; the design then starts over
        // at time zero (IEEE 1800-2023 D.8, 38.36.3).
        for (;;) {
            // A save that failed at the end of its time slot, or a restart
            // that did not take, ends the run with its FSIM-SAVE error.
            if (impl_->save_failure) {
                throw std::runtime_error { std::move(*impl_->save_failure) };
            }
            // $restart (IEEE 1800-2023 D.9) takes effect once the calling
            // task has returned: the state of the file replaces the run's,
            // and the run goes on from the saved time.
            if (impl_->restart_request) {
                if (!impl_->perform_restart()) {
                    throw std::runtime_error {
                        std::move(*impl_->save_failure) };
                }
                result = impl_->interpreter->run(until);
                continue;
            }
            if (impl_->reset_requests.empty()
                || impl_->vpi_control->state()
                    == runtime::SystemVerilogVpiControlState::Finished) {
                break;
            }
            if (!impl_->perform_reset()) {
                result = runtime::RunResult { runtime::RunStatus::stopped,
                    now(), delta(), 0, std::nullopt };
                break;
            }
            result = impl_->interpreter->run(until);
        }
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

void Simulation::add_lifecycle_hook(LifecycleHook hook)
{
    impl_->lifecycle_hooks.push_back(std::move(hook));
}

void Simulation::notify_lifecycle(
    const LifecycleEvent event, const std::string_view location)
{
    // A hook may add another; index so the vector can grow underneath.
    for (std::size_t index = 0U; index < impl_->lifecycle_hooks.size();
         ++index) {
        impl_->lifecycle_hooks[index](event, location);
    }
}

void Simulation::set_reset_source(
    std::function<std::unique_ptr<runtime::simir::Interpreter>()> source)
{
    impl_->interpreter->set_reset_source(std::move(source));
}

bool Simulation::restoring_state() const noexcept
{
    return impl_->restoring_state;
}

void Simulation::set_foreign_run_state_sections(
    runtime::RunStateSectionRegistry sections)
{
    impl_->foreign_section_registry = std::move(sections);
}

void Simulation::set_reset_design_source(
    std::function<std::optional<BuiltProject>()> reload,
    const std::uint64_t max_deltas, const SimulationEngine engine)
{
    auto* const impl = impl_.get();
    const auto effective = engine == SimulationEngine::compiled
        ? SimulationEngine::v4 : engine;
    impl->interpreter->set_reset_source(
        [impl, reload = std::move(reload), max_deltas, effective]()
            -> std::unique_ptr<runtime::simir::Interpreter> {
            auto fresh = reload();
            if (!fresh) {
                return nullptr;
            }
            return impl->make_reset_twin(
                std::move(*fresh), max_deltas, effective);
        });
}

bool Simulation::reset_supported() const noexcept
{
    return !impl_->has_systemc()
        && impl_->interpreter->can_reset_run_state();
}

std::string Simulation::reset_refusal() const
{
    return impl_->reset_refusal();
}

std::string Simulation::Impl::reset_refusal() const
{
    if (has_systemc()) {
        return "FSIM-SAVE-0018: the design has a SystemC part, which cannot "
               "be reset: the SystemC kernel has no operation that returns "
               "its modules, channels and suspended processes to their "
               "elaborated state, so the time-zero state of IEEE 1800-2023 "
               "D.8 cannot be reached";
    }
    if (const auto reason = uvm_unsaved_state(); !reason.empty()) {
        // The UVM services return to time zero (reset_uvm_state), except a
        // callback of the embedding program, which cannot be rebuilt.
        return "FSIM-SAVE-0018: " + reason + ", which a reset cannot return "
               "to its time-zero state (IEEE 1800-2023 D.8)";
    }
    if (const auto unused = uvm_unused_services(); !unused.empty()) {
        return "FSIM-SAVE-0018: the " + unused.front() + " holds state made "
               "through the embedding API, which a reset does not return to "
               "its time-zero state (IEEE 1800-2023 D.8)";
    }
    if (!foreign_structure.empty()) {
        // The twin is the design as the artifact builds it; processes,
        // drivers and sources a library added are not in it, and the library
        // cannot be asked to make them again (IEEE 1800-2023 D.8, IEEE
        // 1076-2019 20.9).
        return "FSIM-SAVE-0018: the run holds structure a foreign interface "
               "created (" + foreign_structure + "), which a reset does not "
               "rebuild from the artifact (IEEE 1800-2023 D.8, IEEE 1076-2019 "
               "20.9)";
    }
    if (!interpreter->can_reset_run_state()) {
        return "FSIM-SAVE-0018: this engine cannot reset the run (IEEE "
               "1800-2023 D.8): no reset source is installed, or a process "
               "runs under an executor that keeps its own state";
    }
    return { };
}

bool Simulation::request_reset(const ResetRequest request)
{
    if (!reset_supported()) {
        return false;
    }
    impl_->reset_requests.push_back(request);
    // The routine that asked has returned by the next safe point (IEEE
    // 1800-2023 38.4 vpiReset, IEEE 1076-2019 23.5 vhpiReset).
    request_stop();
    return true;
}

std::uint32_t Simulation::reset_count() const noexcept
{
    return impl_->reset_count;
}

std::int32_t Simulation::reset_value() const noexcept
{
    return impl_->reset_value;
}

SimulationTick Simulation::now() const noexcept
{
    return impl_->interpreter->scheduler().now();
}

std::uint64_t Simulation::delta() const noexcept
{
    return impl_->interpreter->scheduler().delta();
}

std::uint64_t Simulation::cycles() const noexcept
{
    return impl_->interpreter->scheduler().cycles();
}

bool Simulation::has_pending() const noexcept
{
    return impl_->interpreter->scheduler().has_pending();
}

std::optional<runtime::SimulationTick> Simulation::next_pending_time()
    const noexcept
{
    const auto& scheduler = impl_->interpreter->scheduler();
    if (scheduler.has_pending_activity_now()) {
        return scheduler.now();
    }
    return scheduler.next_pending_time();
}

std::vector<runtime::SimulationTick> Simulation::pending_times() const
{
    return impl_->interpreter->pending_times();
}

bool Simulation::finished() const noexcept
{
    return impl_->lifecycle == Impl::Lifecycle::finished;
}

void Simulation::end_foreign_simulation() noexcept
{
    try {
        impl_->end_vpi();
    } catch (...) {
    }
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

void Simulation::publish_kernel_outputs()
{
    impl_->interpreter->publish_static_kernel_outputs();
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

std::uint64_t Simulation::add_force_statement_hook(ForceStatementHook hook)
{
    if (!hook) {
        throw std::invalid_argument("force observer cannot be empty");
    }
    const auto token = impl_->next_activity_observer++;
    impl_->force_statement_observers.emplace(token, std::move(hook));
    if (impl_->force_statement_observers.size() == 1U) {
        impl_->interpreter->set_force_statement_hook(
            [impl = impl_.get()](const runtime::simir::ProcessId process,
                const SignalId signal, const std::size_t offset,
                const std::size_t width, const bool release) {
                for (const auto& [_, observer] :
                    impl->force_statement_observers) {
                    observer(process, signal, offset, width, release);
                }
            });
    }
    return token;
}

void Simulation::remove_force_statement_hook(
    const std::uint64_t token) noexcept
{
    if (impl_->force_statement_observers.erase(token) != 0U
        && impl_->force_statement_observers.empty()) {
        impl_->interpreter->set_force_statement_hook({});
    }
}

std::uint64_t Simulation::add_signal_transaction_hook(
    SignalTransactionHook hook)
{
    if (!hook) {
        throw std::invalid_argument("transaction observer cannot be empty");
    }
    const auto token = impl_->next_activity_observer++;
    impl_->signal_transaction_observers.emplace(token, std::move(hook));
    if (impl_->signal_transaction_observers.size() == 1U) {
        impl_->interpreter->set_signal_transaction_hook(
            [impl = impl_.get()](const SignalId signal) {
                for (const auto& [_, observer] :
                    impl->signal_transaction_observers) {
                    observer(signal);
                }
            });
    }
    return token;
}

void Simulation::remove_signal_transaction_hook(
    const std::uint64_t token) noexcept
{
    if (impl_->signal_transaction_observers.erase(token) != 0U
        && impl_->signal_transaction_observers.empty()) {
        impl_->interpreter->set_signal_transaction_hook({});
    }
}

std::uint64_t Simulation::add_driver_transaction_hook(
    DriverTransactionHook hook)
{
    if (!hook) {
        throw std::invalid_argument("driver observer cannot be empty");
    }
    const auto token = impl_->next_activity_observer++;
    impl_->driver_transaction_observers.emplace(token, std::move(hook));
    if (impl_->driver_transaction_observers.size() == 1U) {
        impl_->interpreter->set_driver_transaction_hook(
            [impl = impl_.get()](const runtime::simir::ProcessId process,
                const SignalId signal) {
                for (const auto& [_, observer] :
                    impl->driver_transaction_observers) {
                    observer(process, signal);
                }
            });
    }
    return token;
}

void Simulation::remove_driver_transaction_hook(
    const std::uint64_t token) noexcept
{
    if (impl_->driver_transaction_observers.erase(token) != 0U
        && impl_->driver_transaction_observers.empty()) {
        impl_->interpreter->set_driver_transaction_hook({});
    }
}

SdfReannotationCommitResult Simulation::commit_sdf_reannotation(
    const SdfReannotationApplication& application,
    const elaboration::ElaboratedDesign* annotated,
    const std::span<const elaboration::SdfDeviceGateDelay> device_gates)
{
    static_assert(std::is_nothrow_move_assignable_v<
        elaboration::ElaboratedDesign>);
    // The interconnect and port delays follow the reannotation when the
    // annotated design splits the same loads as the running one; the design
    // is then the annotated one, whose records name the delayed copies.
    std::optional<std::vector<
        std::pair<runtime::simir::ProcessId, runtime::simir::NetDelayTable>>>
        retiming;
    if (annotated != nullptr)
        retiming = elaboration::sdf_net_delay_retiming(
            impl_->built.design, *annotated);
    // Without a retiming the running design keeps its own net delay state (the
    // loads it split, with their copy signals and driver processes, and the
    // processes it grafted), because the interpreter runs those: only the
    // module path and timing check timing is the application's. Replacing
    // the whole design would forget the earlier splits, and the loads that
    // are delayed already would be split a second time.
    std::optional<elaboration::ElaboratedDesign> replacement;
    if (retiming) {
        replacement = elaboration::ElaboratedDesign::from_state(
            annotated->state());
        if (!replacement)
            throw std::invalid_argument(
                "SDF reannotation has invalid design state");
    }
    // A DEVICE delay of an output that no module path ends at is the delay
    // of the gates that drive it (IEEE 1497-2001 5.4.1). Every gate is
    // checked before anything is committed, so a gate the engine cannot
    // retime fails the reannotation with the run untouched.
    std::vector<std::pair<runtime::simir::ProcessId,
        runtime::simir::TransitionDelays>> gate_puts;
    SdfReannotationCommitResult refused;
    for (const auto& device : device_gates) {
        for (const auto& primitive : impl_->built.design.verilog_primitives()) {
            if (primitive.instance != device.instance)
                continue;
            const bool drives = std::ranges::any_of(primitive.terminals,
                [&](const elaboration::VerilogPrimitiveTerminalInfo& terminal) {
                    return terminal.direction
                            == frontend::VerilogPrimitiveTerminalDirection::Output
                        && terminal.signal == device.signal;
                });
            if (!drives)
                continue;
            for (const auto driver : gate_driver_processes(primitive.full_name)) {
                std::string reason;
                if (!gate_delays_accepted(driver, device.delays, reason)) {
                    refused.diagnostics.push_back(frontend::Diagnostic {
                        frontend::DiagnosticSeverity::Error,
                        "FSIM-SDF-REANNOTATION-005",
                        "SDF DEVICE delay of " + device.object
                            + " cannot retime gate " + primitive.full_name
                            + ": " + reason,
                        { }, { } });
                }
                gate_puts.emplace_back(driver, device.delays);
            }
        }
    }
    if (!refused.diagnostics.empty())
        return refused;
    auto result = fsim::app::commit_sdf_reannotation(
        application, *impl_->interpreter);
    if (result.ok()) {
        if (retiming) {
            for (const auto& [driver, table] : *retiming)
                (void)impl_->interpreter->set_net_delay_table(
                    impl_->running_process(driver), table);
        }
        // The design's net delay records are the annotation's now, so the
        // values vpi_put_delays stored for the old ones are stale (38.32).
        if (annotated != nullptr)
            ++impl_->sdf_net_delay_epoch;
        if (replacement)
            impl_->built.design = std::move(*replacement);
        else
            impl_->built.design.replace_module_timing(
                application.design().verilog_specify_paths(),
                application.design().verilog_timing_checks());
        // The gates take their DEVICE delays (checked above).
        for (const auto& [driver, delays] : gate_puts)
            (void)put_gate_driver_delays(driver, delays);
        if (!retiming && annotated != nullptr) {
            // The annotation changes which loads are delayed: a load that
            // had no delay is split now, at this safe point
            // (request_net_delay_split), and one that lost its delay takes a
            // zero table, which is exact (no un-split).
            const auto same_load = [](const elaboration::SdfNetDelayInfo& left,
                                       const elaboration::SdfNetDelayInfo& right) {
                return left.destination.object == right.destination.object
                    && left.destination.select_width
                        == right.destination.select_width
                    && left.destination.select_left
                        == right.destination.select_left;
            };
            std::vector<elaboration::SdfNetDelayInfo> fresh;
            const runtime::simir::NetDelayTable none { };
            for (const auto& record : annotated->sdf_net_delays()) {
                const auto& running = impl_->built.design.sdf_net_delays();
                if (!record.delayed) {
                    for (const auto& each : running) {
                        if (each.delayed && same_load(each, record)) {
                            for (const auto driver : each.drivers())
                                (void)impl_->interpreter->set_net_delay_table(
                                    impl_->running_process(driver), none);
                        }
                    }
                    continue;
                }
                const auto kept = std::ranges::find_if(running,
                    [&](const auto& each) {
                        return each.delayed && same_load(each, record);
                    });
                if (kept != running.end()) {
                    // A load that stays delayed takes the new delays for its
                    // later transitions when its copy driver serves this
                    // record alone (a driver shared by per-source records has
                    // one table per source group, which only a retiming of
                    // identical splits maps).
                    if (std::ranges::count(running, kept->driver,
                            &elaboration::SdfNetDelayInfo::driver) == 1) {
                        for (const auto driver : kept->drivers())
                            (void)impl_->interpreter->set_net_delay_table(
                                impl_->running_process(driver),
                                elaboration::sdf_net_delay_table(
                                    record.delays, record.reject, record.error));
                    }
                    continue;
                }
                // Later records to one load replace earlier ones (16.3).
                std::erase_if(fresh, [&](const auto& each) {
                    return same_load(each, record);
                });
                fresh.push_back(record);
            }
            for (auto& record : fresh) {
                record.delayed = false;
                record.load_signal = { };
                record.driver = { };
                record.run_drivers.clear();
                request_net_delay_split(std::move(record));
            }
            // A split the engine refuses fails the reannotation; the timing
            // committed above stays.
            if (!fresh.empty()) {
                std::vector<std::string> failures;
                impl_->commit_net_delay_splits(&failures);
                for (const auto& failure : failures) {
                    result.diagnostics.push_back(frontend::Diagnostic {
                        frontend::DiagnosticSeverity::Error,
                        "FSIM-SDF-REANNOTATION-005",
                        "SDF net delay of " + failure
                            + " (the load was not split at the safe point)",
                        { }, { } });
                }
            }
        }
    }
    return result;
}

std::uint64_t Simulation::sdf_net_delay_epoch() const noexcept
{
    return impl_->sdf_net_delay_epoch;
}

bool Simulation::at_sdf_reannotation_safe_point() const noexcept
{
    return impl_->interpreter->scheduler().at_safe_point();
}

namespace {

// The port, or the bits of it, a split request names.
std::string net_delay_split_key(const elaboration::SdfNetDelayInfo& record)
{
    const auto& terminal = record.destination;
    return terminal.object + '[' + std::to_string(terminal.select_left) + "+:"
        + std::to_string(terminal.select_width) + ']';
}

} // namespace

void Simulation::request_net_delay_split(elaboration::SdfNetDelayInfo record)
{
    auto& requests = impl_->net_delay_split_requests;
    const auto key = net_delay_split_key(record);
    if (impl_->net_delay_split_refused.contains(key))
        return;
    const auto same = std::ranges::find_if(requests, [&](const auto& each) {
        return net_delay_split_key(each) == key;
    });
    if (same != requests.end()) {
        *same = std::move(record);
        return;
    }
    requests.push_back(std::move(record));
    if (impl_->net_delay_split_observer != 0U)
        return;
    // The split waits for a safe point, which the kernel's own time steps do
    // not reach (Interpreter::open_external_observation); the observer
    // removes itself once it has made the splits.
    impl_->net_delay_split_observer = add_safe_point_hook(
        [impl = impl_.get()](runtime::Scheduler& scheduler,
            const runtime::SchedulerPhase) {
            if (scheduler.at_safe_point())
                impl->commit_net_delay_splits();
        });
    open_external_observation();
}

Simulation::GateDelayPut Simulation::put_gate_driver_delays(
    const runtime::simir::ProcessId driver,
    const runtime::simir::TransitionDelays delays)
{
    if (set_verilog_driver_delays(driver, delays)) {
        return GateDelayPut::applied;
    }
    // A gate whose output write is a net delay (split here, or delayed by an
    // SDF DEVICE entry before the run) takes the delays as its table.
    if (set_verilog_net_delay_table(driver,
            runtime::simir::NetDelayTable::from_gate_delays(delays))) {
        return GateDelayPut::applied;
    }
    if (impl_->gate_delay_split.contains(driver)) {
        return GateDelayPut::refused;
    }
    if (impl_->gate_delay_refused.contains(driver)) {
        return GateDelayPut::refused;
    }
    // The split is checked now, so that a put the engine cannot take fails
    // visibly in the caller (vpi_chk_error) instead of at the safe point.
    {
        std::string reason;
        auto program = impl_->gate_delay_program(driver, delays, reason);
        if (program) {
            (void)impl_->interpreter->split_gate_delay(
                impl_->running_process(driver), std::move(*program), reason, true);
        }
        if (!reason.empty()) {
            impl_->gate_delay_error = reason;
            impl_->gate_delay_refused.insert(driver);
            impl_->gate_delay_split_refusals.push_back(
                process_program(impl_->running_process(driver)).name + ": " + reason);
            return GateDelayPut::refused;
        }
    }
    // A later put before the safe point replaces the first one's delays.
    impl_->gate_delay_requests.insert_or_assign(driver, delays);
    if (impl_->gate_delay_observer == 0U) {
        impl_->gate_delay_observer = add_safe_point_hook(
            [impl = impl_.get()](runtime::Scheduler& scheduler,
                const runtime::SchedulerPhase) {
                if (scheduler.at_safe_point())
                    impl->commit_gate_delay_splits();
            });
        open_external_observation();
    }
    return GateDelayPut::split_requested;
}

bool Simulation::gate_delays_accepted(const runtime::simir::ProcessId driver,
    const runtime::simir::TransitionDelays delays, std::string& reason)
{
    using namespace runtime::simir;
    // As put_gate_driver_delays, changing nothing: a slotted write takes the
    // delays, a net delay write their table, and a delay-free gate the split
    // the engine accepts.
    if (impl_->gate_delay_split.contains(driver))
        return true;
    for (const auto& operation :
        process_program(impl_->running_process(driver)).operations) {
        const auto* const write = operation_get_if<WriteDelayed>(&operation);
        if ((write != nullptr && write->slotted)
            || operation_get_if<WriteNetDelay>(&operation) != nullptr)
            return true;
    }
    if (impl_->gate_delay_refused.contains(driver)) {
        reason = impl_->gate_delay_error;
        return false;
    }
    auto program = impl_->gate_delay_program(driver, delays, reason);
    if (program
        && !impl_->interpreter->split_gate_delay(
            impl_->running_process(driver), std::move(*program), reason, true)
        && reason.empty())
        reason = "the simulation engine cannot give the gate a delay slot";
    return reason.empty();
}

namespace {

// The labels the front end gives a gate's drivers: `g`, `g$one`,
// `g$output<n>`.
bool gate_driver_stem_matches(const std::string& name, const std::string_view full_name)
{
    if (!name.starts_with(full_name)) {
        return false;
    }
    const auto rest = std::string_view { name }.substr(full_name.size());
    return rest.empty() || rest == "$one" || rest.starts_with("$output");
}

} // namespace

std::vector<runtime::simir::ProcessId> Simulation::undelayed_gate_drivers(
    const std::string_view full_name) const
{
    using namespace runtime::simir;
    std::vector<ProcessId> found;
    for (ProcessId id = 0U; id < impl_->design_process_count; ++id) {
        const auto& program = process_program(impl_->running_process(id));
        if (!gate_driver_stem_matches(program.name, full_name)) {
            continue;
        }
        const bool writes = std::ranges::any_of(program.operations,
            [](const Operation& operation) {
                return operation_get_if<WriteUpdate>(&operation) != nullptr
                    || operation_get_if<WriteUpdateSlice>(&operation) != nullptr;
            });
        if (writes) {
            found.push_back(id);
        }
    }
    return found;
}

std::vector<runtime::simir::ProcessId> Simulation::gate_driver_processes(
    const std::string_view full_name) const
{
    std::vector<runtime::simir::ProcessId> found;
    for (runtime::simir::ProcessId id = 0U; id < impl_->design_process_count; ++id) {
        if (gate_driver_stem_matches(
                process_program(impl_->running_process(id)).name, full_name)) {
            found.push_back(id);
        }
    }
    return found;
}

bool Simulation::put_tranif_control_delays(
    const std::string_view full_name, const runtime::simir::TransitionDelays delays)
{
    using namespace runtime::simir;
    const std::string name { full_name };
    const auto table = NetDelayTable::from_gate_delays(delays);
    if (const auto known = impl_->tranif_controls.find(name);
        known != impl_->tranif_controls.end()) {
        return set_verilog_net_delay_table(known->second, table);
    }
    if (impl_->tranif_requests.empty() || impl_->tranif_observer == 0U) {
        impl_->tranif_observer = add_safe_point_hook(
            [impl = impl_.get()](runtime::Scheduler& scheduler,
                const runtime::SchedulerPhase) {
                if (scheduler.at_safe_point())
                    impl->commit_tranif_control_splits();
            });
        open_external_observation();
    }
    impl_->tranif_requests.insert_or_assign(name, table);
    return true;
}

void Simulation::Impl::commit_tranif_control_splits()
{
    auto requests = std::move(tranif_requests);
    tranif_requests.clear();
    safe_point_observers.erase(tranif_observer);
    tranif_observer = 0U;
    for (auto& [name, table] : requests) {
        std::string reason;
        if (!split_tranif_now(name, table, reason, nullptr)) {
            report_deferred_split_failure(name, reason);
        }
    }
}

bool Simulation::Impl::split_tranif_now(const std::string& name,
    const runtime::simir::NetDelayTable& table, std::string& reason,
    const std::vector<runtime::simir::ProcessId>* const expected_ids)
{
    auto planned = elaboration::plan_tranif_control_graft(
        built.design, name, table, design_process_count,
        [this](const runtime::simir::ProcessId process)
            -> const runtime::simir::Process& {
            return interpreter->sensitive_process_program(process);
        },
        reason);
    std::vector<runtime::simir::ProcessId> planned_ids;
    if (planned) {
        for (const auto& process : planned->graft.processes)
            planned_ids.push_back(process.id);
        if (!interpreter->graft_net_delay(planned->graft, reason)
            && reason.empty())
            reason = "the simulation engine cannot add the delayed copy "
                     "of a switch control to a running design";
    }
    if (!planned || !reason.empty()) {
        gate_delay_split_refusals.push_back(name + ": " + reason);
        return false;
    }
    std::vector<runtime::simir::ProcessId> run_ids;
    for (std::size_t index = 0U; index < planned_ids.size(); ++index) {
        run_ids.push_back(planned->graft.processes[index].id);
        if (planned->graft.processes[index].id != planned_ids[index])
            grafted_process_ids.emplace(planned_ids[index],
                planned->graft.processes[index].id);
    }
    if (expected_ids != nullptr && *expected_ids != run_ids) {
        reason = "the switch control copy of the saved run had other "
                 "process ids (the run created fork children or foreign "
                 "drivers before it)";
        return false;
    }
    tranif_controls.emplace(name, planned->driver);
    built.design = std::move(*planned->grown);
    LateSplit record;
    record.kind = 1U;
    record.name = name;
    record.table = table;
    record.run_ids = std::move(run_ids);
    late_splits.push_back(std::move(record));
    return true;
}

const std::string& Simulation::gate_delay_error() const noexcept
{
    return impl_->gate_delay_error;
}

const std::vector<std::string>& Simulation::gate_delay_split_refusals()
    const noexcept
{
    return impl_->gate_delay_split_refusals;
}

std::optional<runtime::simir::Process> Simulation::Impl::gate_delay_program(
    const runtime::simir::ProcessId driver,
    const runtime::simir::TransitionDelays delays, std::string& reason)
{
    using namespace runtime::simir;
    Process program = interpreter->sensitive_process_program(running_process(driver));
    const auto table = NetDelayTable::from_gate_delays(delays).values();
    bool rewritten { };
    for (auto& operation : program.operations) {
        if (const auto* const update = operation_get_if<WriteUpdate>(&operation)) {
            operation = WriteNetDelay { update->signal, update->source,
                std::nullopt, table, update->domain };
            rewritten = true;
        } else if (const auto* const slice
            = operation_get_if<WriteUpdateSlice>(&operation)) {
            operation = WriteNetDelay { slice->signal, slice->source,
                slice->offset, table, slice->domain };
            rewritten = true;
        }
    }
    if (!rewritten) {
        reason = "the driver has no delay-free write of its output";
        return std::nullopt;
    }
    return program;
}

void Simulation::Impl::commit_gate_delay_splits()
{
    auto requests = std::move(gate_delay_requests);
    gate_delay_requests.clear();
    safe_point_observers.erase(gate_delay_observer);
    gate_delay_observer = 0U;
    for (const auto& [driver, delays] : requests) {
        std::string reason;
        if (!split_gate_now(driver, delays, reason)) {
            report_deferred_split_failure(
                interpreter->process_program(running_process(driver)).name, reason);
        }
    }
}

bool Simulation::Impl::split_gate_now(const runtime::simir::ProcessId driver,
    const runtime::simir::TransitionDelays delays, std::string& reason)
{
    using namespace runtime::simir;
    const auto running = running_process(driver);
    auto program = gate_delay_program(driver, delays, reason);
    const std::string name = program ? program->name : std::string {};
    if (program
        && !interpreter->split_gate_delay(running, std::move(*program), reason)
        && reason.empty()) {
        reason = "the simulation engine cannot give the gate a delay slot";
    }
    if (!reason.empty()) {
        gate_delay_refused.insert(driver);
        gate_delay_split_refusals.push_back(name + ": " + reason);
        return false;
    }
    gate_delay_split.insert(driver);
    LateSplit record;
    record.kind = 0U;
    record.driver = driver;
    record.delays = delays;
    late_splits.push_back(std::move(record));
    return true;
}

bool Simulation::Impl::replay_late_splits(
    const std::vector<LateSplit>& splits, std::string& reason)
{
    // The saved run made these puts after it started; this fresh run has made
    // none. Each is applied as the safe point applied it, in order, so the
    // engine's plan (the rewritten gate programs, the control copies and the
    // ids they took) is the saved run's before the saved state replaces the
    // tables (IEEE 1800-2023 D.9, 38.32).
    struct Replaying {
        runtime::simir::Interpreter& engine;
        explicit Replaying(runtime::simir::Interpreter& value) : engine(value)
        {
            engine.set_plan_replay(true);
        }
        ~Replaying() { engine.set_plan_replay(false); }
    } replaying { *interpreter };
    for (const auto& split : splits) {
        if (split.kind == 0U) {
            if (gate_delay_split.contains(split.driver)) {
                continue;
            }
            if (!split_gate_now(split.driver, split.delays, reason)) {
                reason = "FSIM-SAVE-0010: a gate delay split of the saved "
                         "run cannot be rebuilt: " + reason;
                return false;
            }
        } else if (split.kind == 2U) {
            if (std::ranges::any_of(late_splits, [&](const LateSplit& done) {
                    return done.kind == 2U && done.name == split.name;
                })) {
                continue;
            }
            if (!split_net_delay_now(split.net_delay, reason, &split.run_ids)) {
                reason = "FSIM-SAVE-0010: a port delay split of the saved "
                         "run cannot be rebuilt: " + reason;
                return false;
            }
        } else {
            if (tranif_controls.contains(split.name)) {
                continue;
            }
            if (!split_tranif_now(split.name, split.table, reason,
                    &split.run_ids)) {
                reason = "FSIM-SAVE-0010: a switch control split of the "
                         "saved run cannot be rebuilt: " + reason;
                return false;
            }
        }
    }
    return true;
}

bool Simulation::net_delay_split_pending() const noexcept
{
    return !impl_->net_delay_split_requests.empty();
}

const std::vector<std::string>& Simulation::net_delay_split_refusals()
    const noexcept
{
    return impl_->net_delay_split_refusals;
}

void Simulation::Impl::commit_net_delay_splits(std::vector<std::string>* const failures)
{
    auto requests = std::move(net_delay_split_requests);
    net_delay_split_requests.clear();
    safe_point_observers.erase(net_delay_split_observer);
    net_delay_split_observer = 0U;
    for (auto& record : requests) {
        // A later put took the delay back to none before the split was made.
        if (std::ranges::all_of(record.delays,
                [](const runtime::SimulationTick tick) { return tick == 0U; }))
            continue;
        std::string reason;
        const auto key = net_delay_split_key(record);
        const auto label = record.identity.empty() ? key : record.identity;
        if (!split_net_delay_now(std::move(record), reason, nullptr)) {
            report_deferred_split_failure(label, reason);
            if (failures != nullptr)
                failures->push_back(key + ": " + reason);
        }
    }
}

void Simulation::Impl::report_deferred_split_failure(
    const std::string& what, const std::string& reason)
{
    // The put returned success when it was made; the split it asked for
    // failed at the safe point, so the old timing stays. That is an error of
    // the run (IEEE 1800-2023 38.32): the next vpi_chk_error reports it and
    // the run ends with a failure status.
    deferred_split_errors.push_back("vpi_put_delays on " + what
        + " took no effect at the safe point: " + reason);
}

const std::vector<std::string>& Simulation::deferred_split_errors()
    const noexcept
{
    return impl_->deferred_split_errors;
}

std::optional<std::string> Simulation::take_deferred_split_error()
{
    if (impl_->deferred_split_errors_taken
        >= impl_->deferred_split_errors.size()) {
        return std::nullopt;
    }
    return impl_->deferred_split_errors[impl_->deferred_split_errors_taken++];
}

bool Simulation::Impl::split_net_delay_now(elaboration::SdfNetDelayInfo record,
    std::string& reason,
    const std::vector<runtime::simir::ProcessId>* const expected_ids)
{
    const auto key = net_delay_split_key(record);
    const auto identity = record.identity;
    // The request as the safe point took it, for the saved plan.
    const elaboration::SdfNetDelayInfo request = record;
    auto planned = elaboration::plan_sdf_net_delay_graft(
        built.design, std::move(record), design_process_count,
        [this](const runtime::simir::ProcessId process)
            -> const runtime::simir::Process& {
            return interpreter->sensitive_process_program(process);
        },
        reason);
    std::vector<runtime::simir::ProcessId> planned_ids;
    if (planned) {
        for (const auto& process : planned->graft.processes)
            planned_ids.push_back(process.id);
        if (!interpreter->graft_net_delay(planned->graft, reason)
            && reason.empty())
            reason = "the simulation engine cannot add the delayed copy "
                     "of a port to a running design";
    }
    if (!planned || !reason.empty()) {
        net_delay_split_refused.insert(key);
        net_delay_split_refusals.push_back(
            (identity.empty() ? key : identity) + ": " + reason);
        return false;
    }
    std::vector<runtime::simir::ProcessId> run_ids;
    for (std::size_t index = 0U; index < planned_ids.size(); ++index) {
        run_ids.push_back(planned->graft.processes[index].id);
        if (planned->graft.processes[index].id != planned_ids[index])
            grafted_process_ids.emplace(planned_ids[index],
                planned->graft.processes[index].id);
    }
    if (expected_ids != nullptr && *expected_ids != run_ids) {
        reason = "the delayed copy of the saved run had other process ids "
                 "(the run created fork children or foreign drivers "
                 "before it)";
        return false;
    }
    built.design = std::move(*planned->grown);
    LateSplit split;
    split.kind = 2U;
    split.name = key;
    split.net_delay = request;
    split.run_ids = std::move(run_ids);
    late_splits.push_back(std::move(split));
    return true;
}

void Simulation::install_execution_point_hooks()
{
    if (impl_->execution_point_observers.empty()
        && !impl_->execution_point_primary) {
        impl_->interpreter->set_execution_point_hook({ });
        return;
    }
    // Observers see a point before the debugger, which may stop the run. A
    // subprogram return is a point only for the foreign interfaces (IEEE
    // 1076-2019 21.3.4.6), as are the frame, thread and constructor points
    // of the execution probes (IEEE 1800-2023 37.43, 37.44, 38.36.1); the
    // debugger and the C API never see them.
    impl_->interpreter->set_execution_point_hook(
        [impl = impl_.get()](runtime::Scheduler& scheduler,
            const runtime::simir::ExecutionPoint& point) {
            for (const auto& [_, observer] : impl->execution_point_observers) {
                observer(scheduler, point);
            }
            using Kind = runtime::simir::ExecutionPointKind;
            if (impl->execution_point_primary
                && point.kind != Kind::call_return
                && point.kind != Kind::frame_start
                && point.kind != Kind::frame_end
                && point.kind != Kind::thread_start
                && point.kind != Kind::thread_end
                && point.kind != Kind::thread_enter
                && point.kind != Kind::object_created
                && point.kind != Kind::assertion_pass
                && point.kind != Kind::assertion_fail) {
                impl->execution_point_primary(scheduler, point);
            }
        });
}

void Simulation::set_execution_point_hook(ExecutionPointHook hook)
{
    impl_->execution_point_primary = std::move(hook);
    install_execution_point_hooks();
}

std::uint64_t Simulation::add_execution_point_hook(ExecutionPointHook hook)
{
    if (!hook) {
        throw std::invalid_argument("execution-point observer cannot be empty");
    }
    const auto token = impl_->next_activity_observer++;
    impl_->execution_point_observers.emplace(token, std::move(hook));
    if (impl_->execution_point_observers.size() == 1U) {
        install_execution_point_hooks();
        // A probe observer reads the simulation time at its point, which
        // the kernel's own time steps do not keep (open_external_observation).
        open_external_observation();
    }
    return token;
}

void Simulation::remove_execution_point_hook(
    const std::uint64_t token) noexcept
{
    if (impl_->execution_point_observers.erase(token) != 0U
        && impl_->execution_point_observers.empty()) {
        install_execution_point_hooks();
    }
}

void Simulation::set_foreign_probes(const bool enabled) noexcept
{
    impl_->foreign_probes = enabled;
}

bool Simulation::foreign_probes() const noexcept
{
    return impl_->foreign_probes;
}

void Simulation::set_vhpi_end_of_step_timers(const bool enabled) noexcept
{
    impl_->vhpi_end_of_step_timers = enabled;
}

bool Simulation::vhpi_end_of_step_timers() const noexcept
{
    return impl_->vhpi_end_of_step_timers;
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

void Simulation::set_systemverilog_dpi_site_hooks(
    SystemVerilogDpiSiteHooks hooks)
{
    impl_->interpreter->set_dpi_site_hooks(std::move(hooks));
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

void Simulation::set_assertion_attempt_hook(AssertionAttemptHook hook)
{
    // The first library callback that needs attempt starts (cbAssertionStart,
    // IEEE 1800-2023 39.4.2) turns the evaluators' start reports on, at the
    // point of the call (before time 0 for a startup routine); a run that
    // never registers one never binds them.
    if (hook && impl_->interpreter) {
        impl_->interpreter->bind_assertion_start_reports();
    }
    impl_->assertion_attempt_hook = std::move(hook);
}

void Simulation::set_assertion_step_hook(AssertionStepHook hook)
{
    impl_->assertion_step_hook = std::move(hook);
}

Simulation::AssertionStepResult Simulation::set_assertion_step(
    const std::uint64_t assertion, const runtime::SimulationTick start,
    const bool enable)
{
    const auto processes = impl_->vpi_assertion_processes.find(assertion);
    if (processes == impl_->vpi_assertion_processes.end()
        || !impl_->interpreter) {
        return AssertionStepResult::no_assertion;
    }
    const std::vector<runtime::simir::ProcessId> selected {
        processes->second.begin(), processes->second.end()
    };
    return impl_->interpreter->set_sva_step(
               selected, static_cast<std::int64_t>(start), enable)
        ? AssertionStepResult::done
        : AssertionStepResult::attempt_started;
}

bool Simulation::kill_assertion_attempt(
    const std::uint64_t assertion, const runtime::SimulationTick start)
{
    const auto processes = impl_->vpi_assertion_processes.find(assertion);
    if (processes == impl_->vpi_assertion_processes.end()
        || !impl_->interpreter) {
        return false;
    }
    const std::vector<runtime::simir::ProcessId> selected {
        processes->second.begin(), processes->second.end()
    };
    return impl_->interpreter->kill_sva_attempt(
        selected, static_cast<std::int64_t>(start));
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
