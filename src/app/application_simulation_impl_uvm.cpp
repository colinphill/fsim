// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include "fsim/support/glob_match.hpp"
#include "fsim/support/uvm_api_methods.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include <string>

namespace fsim::app {

namespace {

// The method of the library's uvm_component a standard phase runs (IEEE
// 1800.2-2020 9.8, 13.1.4); none for a phase of the user's own.
[[nodiscard]] support::UvmRole uvm_phase_method_role(
    const runtime::SystemVerilogUvmPhaseKind kind) noexcept
{
    using Kind = runtime::SystemVerilogUvmPhaseKind;
    using support::UvmRole;
    switch (kind) {
    case Kind::Build: return UvmRole::build_phase;
    case Kind::Connect: return UvmRole::connect_phase;
    case Kind::EndOfElaboration: return UvmRole::end_of_elaboration_phase;
    case Kind::StartOfSimulation: return UvmRole::start_of_simulation_phase;
    case Kind::Run: return UvmRole::run_phase;
    case Kind::Extract: return UvmRole::extract_phase;
    case Kind::Check: return UvmRole::check_phase;
    case Kind::Report: return UvmRole::report_phase;
    case Kind::Final: return UvmRole::final_phase;
    case Kind::PreReset: return UvmRole::pre_reset_phase;
    case Kind::Reset: return UvmRole::reset_phase;
    case Kind::PostReset: return UvmRole::post_reset_phase;
    case Kind::PreConfigure: return UvmRole::pre_configure_phase;
    case Kind::Configure: return UvmRole::configure_phase;
    case Kind::PostConfigure: return UvmRole::post_configure_phase;
    case Kind::PreMain: return UvmRole::pre_main_phase;
    case Kind::Main: return UvmRole::main_phase;
    case Kind::PostMain: return UvmRole::post_main_phase;
    case Kind::PreShutdown: return UvmRole::pre_shutdown_phase;
    case Kind::Shutdown: return UvmRole::shutdown_phase;
    case Kind::PostShutdown: return UvmRole::post_shutdown_phase;
    case Kind::Custom: return UvmRole::none;
    }
    return UvmRole::none;
}

// The library class with role `role` (support::UvmRole): its declaration
// identity, empty when the design does not declare it.
[[nodiscard]] std::string uvm_role_class(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const support::UvmRole role)
{
    const auto found = std::ranges::find(specializations, role,
        &semantic::sv::ClassSpecialization::uvm_role);
    return found == specializations.end() ? std::string { }
                                          : found->declaration_identity;
}

// The recorded role of the class declared as `identity`.
[[nodiscard]] support::UvmRole uvm_declared_role(
    const std::span<const semantic::sv::ClassSpecialization> specializations,
    const std::string_view identity)
{
    const auto found = std::ranges::find(specializations, identity,
        &semantic::sv::ClassSpecialization::declaration_identity);
    return found == specializations.end() ? support::UvmRole::none
                                          : found->uvm_role;
}

// The hierarchical path (`package.variable`) of the library package
// variable with role `role`; empty when the design does not declare it.
[[nodiscard]] std::string uvm_package_variable_path(
    const semantic::sv::Hir& hir, const support::UvmRole role)
{
    for (const auto& unit : hir.units()) {
        if (!support::uvm_library(unit.uvm_role))
            continue;
        for (const auto id : unit.declarations) {
            const auto* declaration = hir.find_declaration(id);
            if (declaration != nullptr && declaration->uvm_role == role)
                return unit.name + "." + declaration->name;
        }
    }
    return { };
}

} // namespace
namespace {
// A phase callback whose body has no statement and no local does nothing
// when called (IEEE 1800-2023 13.4, 13.3): the library's own
// phase_started, phase_ready_to_end and phase_ended are such functions.
// Calling it is no class code to run.
[[nodiscard]] bool empty_phase_body(
    const semantic::sv::SpecializedClassMethod& method) noexcept
{
    return method.statements.empty() && method.local_declarations.empty()
        && method.defined
        && (!method.return_type
            || method.return_type->target.spelling == "void");
}
}

void Simulation::Impl::start_uvm_api_dispatch()
{
    if (uvm_api_primed) {
        return;
    }
    // The design's own time-zero processes stay unstarted: only the dispatch
    // process runs for a request, and the run's own start starts the rest.
    interpreter->start_holding_time_zero();
    uvm_api_primed = true;
    // The first change of the notifier after the start is taken as part of
    // the start's baseline and wakes no process: wake the dispatch process
    // once with no request, so every request after it is a change.
    toggle_class_heap_change();
    (void)interpreter->run_host_activation();
}

bool Simulation::Impl::dispatch_uvm_api_construct(
    const runtime::SystemVerilogClassHandle component)
{
    start_uvm_api_dispatch();
    uvm_api_request = { support::uvm_api_construct_kind, component, 0U, false, nullptr };
    toggle_class_heap_change();
    (void)interpreter->run_host_activation();
    const bool taken = uvm_api_request.taken;
    const auto error = std::exchange(uvm_api_request.error, nullptr);
    uvm_api_request = { };
    if (error) {
        std::rethrow_exception(error);
    }
    return taken && !uvm_pending_constructions.contains(component);
}

void Simulation::Impl::dispatch_uvm_api_function(
    const runtime::SystemVerilogClassHandle component,
    const support::UvmRole method,
    const runtime::SystemVerilogClassHandle phase)
{
    const auto kind = support::uvm_api_request_kind(method);
    if (!kind) {
        throw std::logic_error {
            "UVM API method role "
            + std::to_string(static_cast<unsigned>(method))
            + " is not one the dispatch process runs" };
    }
    // The dispatch process runs in the current time step, woken by the
    // class heap notifier while the design's time-zero processes are held
    // (start_uvm_api_dispatch).
    uvm_api_request = { *kind, component, phase, false, nullptr };
    toggle_class_heap_change();
    (void)interpreter->run_host_activation();
    // Every design that declares a library class has the process.
    const bool taken = uvm_api_request.taken;
    uvm_api_request = { };
    if (!taken) {
        throw std::logic_error { "the UVM dispatch process did not take the "
            "request for the method of role "
            + std::to_string(static_cast<unsigned>(method)) };
    }
}

[[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::Impl::execute_uvm_function_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase_handle)
{
    start_uvm_api_dispatch();
    const auto phase_snapshot = uvm_phases.snapshot(phase_handle);
    (void)apply_uvm_report_settings(
        phase_snapshot.identity, interpreter->scheduler().now());
    const auto invoke_callback =
        [&](const runtime::SystemVerilogClassHandle component,
            const support::UvmRole method_name) {
            if (const auto* method
                = class_hir_execution.uvm_component_method(component, method_name)) {
                if (method->kind
                        != semantic::sv::ClassMethodKind::function
                    || method->static_method || method->formals.size() > 1U) {
                    throw std::invalid_argument {
                        "UVM function-phase callback has an unsupported HIR profile"
                    };
                }
                if (empty_phase_body(*method))
                    return;
                // The method runs as generated code in the design's UVM
                // dispatch process.
                dispatch_uvm_api_function(component, method_name,
                    uvm_phase_object(phase_handle));
                return;
            }
        };
    return uvm_phases.execute_function_phase(
        phase_handle,
        [&](const runtime::SystemVerilogClassHandle component,
            const runtime::SystemVerilogUvmPhaseHandle,
            const runtime::SystemVerilogUvmPhaseCallbackKind callback) {
            auto method_name = support::UvmRole::none;
            switch (callback) {
            case runtime::SystemVerilogUvmPhaseCallbackKind::PhaseStarted:
                method_name = support::UvmRole::phase_started;
                break;
            case runtime::SystemVerilogUvmPhaseCallbackKind::Execute:
                method_name = uvm_phase_method_role(phase_snapshot.kind);
                break;
            case runtime::SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd:
                method_name = support::UvmRole::phase_ready_to_end;
                break;
            case runtime::SystemVerilogUvmPhaseCallbackKind::PhaseEnded:
                method_name = support::UvmRole::phase_ended;
                break;
            }

            invoke_callback(component, method_name);
            uvm_sequences.dispatch_role_function(component, phase_handle, callback);
        });
}

[[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::Impl::execute_uvm_task_phase(const runtime::SystemVerilogUvmPhaseHandle phase_handle,
    const UvmTaskPhaseContinuation& continuation)
{
    start_uvm_api_dispatch();
    const auto phase_snapshot = uvm_phases.snapshot(phase_handle);
    (void)apply_uvm_report_settings(
        phase_snapshot.identity, interpreter->scheduler().now());
    const auto invoke_hook =
        [&](const runtime::SystemVerilogClassHandle component,
            const runtime::SystemVerilogUvmPhaseHandle,
            const runtime::SystemVerilogUvmPhaseCallbackKind callback) {
            const auto method_name = callback == runtime::SystemVerilogUvmPhaseCallbackKind::PhaseStarted
                ? support::UvmRole::phase_started
                : callback == runtime::SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd
                ? support::UvmRole::phase_ready_to_end
                : support::UvmRole::phase_ended;
            if (const auto* method
                = class_hir_execution.uvm_component_method(component, method_name)) {
                if (method->kind
                        != semantic::sv::ClassMethodKind::function
                    || method->static_method || method->formals.size() > 1U) {
                    throw std::invalid_argument {
                        "UVM task-phase hook has an unsupported HIR profile"
                    };
                }
                if (empty_phase_body(*method)) {
                    uvm_sequences.dispatch_role_function(
                        component, phase_handle, callback);
                    return;
                }
                dispatch_uvm_api_function(component, method_name, 0U);
                uvm_sequences.dispatch_role_function(
                    component, phase_handle, callback);
                return;
            }
            uvm_sequences.dispatch_role_function(component, phase_handle, callback);
        };
    return uvm_phases.execute_task_phase(
        phase_handle, invoke_hook,
        [&](const runtime::SystemVerilogClassHandle component,
            const runtime::SystemVerilogUvmPhaseHandle callback_phase,
            const runtime::SystemVerilogUvmPhaseProcessHandle process) {
            if (const auto* method = class_hir_execution.uvm_component_method(
                    component, uvm_phase_method_role(phase_snapshot.kind))) {
                if (method->kind != semantic::sv::ClassMethodKind::task
                    || method->static_method || method->formals.size() > 1U) {
                    throw std::invalid_argument {
                        "UVM task-phase callback has an unsupported HIR profile"
                    };
                }
                dispatch_uvm_api_function(component,
                    uvm_phase_method_role(phase_snapshot.kind), 0U);
            }
            const auto role_status = uvm_sequences.dispatch_role_task(
                component, callback_phase, process);
            const auto continuation_status = continuation ? continuation(component, callback_phase, process)
                                                          : runtime::SystemVerilogUvmTaskPhaseStatus::Completed;
            return role_status == runtime::SystemVerilogUvmTaskPhaseStatus::Suspended || continuation_status == runtime::SystemVerilogUvmTaskPhaseStatus::Suspended
                ? runtime::SystemVerilogUvmTaskPhaseStatus::Suspended
                : runtime::SystemVerilogUvmTaskPhaseStatus::Completed;
        });
}

runtime::SystemVerilogClassHandle Simulation::Impl::uvm_phase_object(
    const runtime::SystemVerilogUvmPhaseHandle& phase)
{
    auto found = std::ranges::find(uvm_phase_objects, phase);
    if (found == uvm_phase_objects.end()) {
        uvm_phase_objects.push_back(phase);
        found = std::prev(uvm_phase_objects.end());
    }
    return uvm_phase_object_tag
        + static_cast<runtime::SystemVerilogClassHandle>(
            found - uvm_phase_objects.begin())
        + 1U;
}

std::optional<runtime::PackedLogic4> Simulation::Impl::invoke_uvm_phase_method(
    const runtime::SystemVerilogClassHandle phase_object,
    const support::UvmRole method,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals)
{
    const auto index = phase_object - uvm_phase_object_tag;
    if (phase_object <= uvm_phase_object_tag
        || index > uvm_phase_objects.size()) {
        return std::nullopt;
    }
    const auto& phase = uvm_phase_objects[index - 1U];
    const auto known = [](const runtime::PackedLogic4& value) {
        return value.low_word().bval == 0U;
    };
    // raise_objection(obj, description = "", count = 1) and drop_objection
    // (IEEE 1800.2-2020 9.3.1.7); a null object objects for the root.
    if (method == support::UvmRole::phase_raise_objection || method == support::UvmRole::phase_drop_objection) {
        const auto object = !actuals.empty() && known(actuals[0])
            ? static_cast<runtime::SystemVerilogClassHandle>(
                  actuals[0].low_word().aval)
            : runtime::SystemVerilogClassHandle { };
        const auto source = uvm_objections.bind_source(object,
            object != 0 && uvm_components.contains(object)
                ? uvm_components.root_of(object)
                : uvm_run_test.root);
        const auto description = string_actuals.size() >= 2U
            ? string_actuals[1] : std::string { };
        const auto count = actuals.size() >= 3U && known(actuals[2])
            ? static_cast<std::int64_t>(
                  static_cast<std::int32_t>(actuals[2].low_word().aval))
            : std::int64_t { 1 };
        if (method == support::UvmRole::phase_raise_objection) {
            (void)uvm_objections.raise(phase, source, description, count);
            uvm_run_test.raised = true;
        } else {
            (void)uvm_objections.drop(phase, source, description, count);
            // The run phase ends when its last objection drops.
            if (!uvm_run_test.ended && uvm_run_test.root != 0
                && std::ranges::all_of(uvm_run_test.roots,
                    [&](const runtime::SystemVerilogUvmRootHandle root) {
                        return uvm_objections.all_dropped(phase, root);
                    })) {
                uvm_run_test.ended = true;
                toggle_class_heap_change();
            }
        }
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    if (method == support::UvmRole::get_name || method == support::UvmRole::get_full_name) {
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    return std::nullopt;
}

std::optional<runtime::PackedLogic4> Simulation::Impl::invoke_uvm_run_test(
    const std::string_view identity,
    const std::span<const runtime::PackedLogic4> actuals,
    const std::span<const std::string> string_actuals)
{
    using Kind = runtime::SystemVerilogUvmPhaseKind;
    // run_test makes the root with uvm_root::get() (IEEE 1800.2-2020 F.3.1.4),
    // which sets uvm_top.
    {
        const auto& specializations
            = built.compiled_systemverilog_class_specializations;
        if (const auto root_identity
            = uvm_role_class(specializations, support::UvmRole::root);
            !root_identity.empty())
            (void)uvm_singleton(root_identity);
        // The root's constructor takes the command line processor.
        if (const auto cmdline_identity = uvm_role_class(
                specializations, support::UvmRole::cmdline_processor);
            !cmdline_identity.empty())
            (void)uvm_singleton(cmdline_identity);
    }
    const auto schedule = uvm_phases.standard_schedule();
    if (!schedule) {
        throw std::invalid_argument {
            "UVM run_test has no standard phase schedule"
        };
    }
    // Every component, each parent before its children.
    const auto top_down = [&] {
        std::vector<runtime::SystemVerilogClassHandle> order;
        std::vector<runtime::SystemVerilogClassHandle> pending;
        for (const auto root : uvm_run_test.roots) {
            const auto tops = uvm_components.top_components(root);
            pending.assign(tops.rbegin(), tops.rend());
            while (!pending.empty()) {
                const auto component = pending.back();
                pending.pop_back();
                order.push_back(component);
                const auto children = uvm_components.children(component);
                pending.insert(pending.end(), children.rbegin(), children.rend());
            }
        }
        return order;
    };
    const auto handle = [](const runtime::SystemVerilogClassHandle value) {
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    };
    const auto ignore_hook = [](runtime::SystemVerilogClassHandle,
                                 runtime::SystemVerilogUvmPhaseHandle,
                                 runtime::SystemVerilogUvmPhaseCallbackKind) { };
    // Run-phase tasks still running when the objections drop are killed
    // (IEEE 1800.2-2020 9.3.1.6) before the run phase completes.
    const auto complete_run_phase = [&] {
        for (const auto& process : uvm_run_test.processes) {
            if (uvm_phases.process_snapshot(process).state
                == runtime::SystemVerilogUvmPhaseProcessState::Running) {
                uvm_phases.cancel_task_process(process);
            }
        }
        uvm_run_test.processes.clear();
        const auto run = schedule->phase(Kind::Run);
        if (uvm_phases.snapshot(run).state
            == runtime::SystemVerilogUvmPhaseState::Executing) {
            (void)uvm_phases.complete_task_phase(run, ignore_hook);
        }
    };
    if (identity == "@uvm-run-test-begin") {
        uvm_run_test = { };
        uvm_run_test.root = component_root("$simulation");
        // A named test is created next (`@uvm-run-test-construct`); without
        // a name the components the testbench constructed form the
        // hierarchy.
        uvm_run_test.roots.push_back(uvm_run_test.root);
        for (const auto& [scope, root] : uvm_roots_by_scope) {
            if (root != uvm_run_test.root) {
                uvm_run_test.roots.push_back(root);
            }
        }
        return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
    }
    // The named test, created as uvm_test_top without a parent through the
    // factory (IEEE 1800.2-2020 F.3.1.2): allocated only when the calling
    // process runs its constructor chain (the classes after the actuals).
    if (identity == "@uvm-run-test-construct") {
        if (string_actuals.size() < 2U || string_actuals[0].empty()) {
            return handle(0);
        }
        const auto& name = string_actuals[1];
        const auto resolved = uvm_factory.resolve_by_name(
            string_actuals[0], name);
        if (string_actuals.size() > 3U) {
            if (const auto object = allocate_uvm_lowered(resolved.resolved,
                    string_actuals[3], uvm_run_test.root);
                object != 0U) {
                return handle(object);
            }
        }
        return handle(uvm_registry.create_component_by_type(
            resolved.resolved, name, 0, uvm_run_test.root));
    }
    if (identity == "@uvm-run-test-function-phase") {
        // Build, connect, end_of_elaboration, start_of_simulation, then
        // extract, check, report and final; build and final run top-down,
        // the others bottom-up.
        static constexpr std::array kinds { Kind::Build, Kind::Connect,
            Kind::EndOfElaboration, Kind::StartOfSimulation, Kind::Extract,
            Kind::Check, Kind::Report, Kind::Final };
        const auto index = actuals.empty() ? std::uint64_t { }
                                           : actuals.front().low_word().aval;
        if (index >= kinds.size()) {
            throw std::invalid_argument { "UVM run_test phase index is out of range" };
        }
        const auto kind = kinds[index];
        if (kind == Kind::Extract) {
            complete_run_phase();
        }
        uvm_run_test.phase = kind;
        uvm_run_test.next = 0;
        uvm_run_test.visited.clear();
        uvm_run_test.order.clear();
        if (kind != Kind::Build) {
            uvm_run_test.order = top_down();
            if (kind != Kind::Final) {
                std::ranges::reverse(uvm_run_test.order);
            }
        }
        return handle(uvm_phase_object(schedule->phase(kind)));
    }
    if (identity == "@uvm-run-test-next") {
        if (uvm_run_test.phase == Kind::Build) {
            // A parent's build_phase creates its children, so each call
            // walks the hierarchy again for the first unvisited component.
            for (const auto component : top_down()) {
                if (std::ranges::find(uvm_run_test.visited, component)
                    == uvm_run_test.visited.end()) {
                    uvm_run_test.visited.push_back(component);
                    return handle(component);
                }
            }
            return handle({ });
        }
        return handle(uvm_run_test.next < uvm_run_test.order.size()
                ? uvm_run_test.order[uvm_run_test.next++]
                : runtime::SystemVerilogClassHandle { });
    }
    if (identity == "@uvm-run-test-components") {
        uvm_run_test.components = top_down();
        return runtime::PackedLogic4::from_aval_bval(
            32, uvm_run_test.components.size(), 0);
    }
    if (identity == "@uvm-run-test-phase") {
        // The run phase executes while the forked run_phase tasks run, so
        // they may raise and drop its objections.
        const auto run = schedule->phase(Kind::Run);
        (void)uvm_phases.execute_task_phase(run, ignore_hook,
            [&](runtime::SystemVerilogClassHandle,
                runtime::SystemVerilogUvmPhaseHandle,
                const runtime::SystemVerilogUvmPhaseProcessHandle process) {
                uvm_run_test.processes.push_back(process);
                return runtime::SystemVerilogUvmTaskPhaseStatus::Suspended;
            });
        return runtime::PackedLogic4::from_aval_bval(
            64, uvm_phase_object(run), 0);
    }
    if (identity == "@uvm-run-test-component") {
        const auto index = actuals.empty() ? std::uint64_t { }
                                           : actuals.front().low_word().aval;
        return runtime::PackedLogic4::from_aval_bval(64,
            index < uvm_run_test.components.size()
                ? uvm_run_test.components[index]
                : runtime::SystemVerilogClassHandle { },
            0);
    }
    if (identity == "@uvm-run-test-ended") {
        // A run phase in which nothing objected ends once its tasks start.
        if (!uvm_run_test.raised) {
            uvm_run_test.ended = true;
        }
        return runtime::PackedLogic4::from_aval_bval(
            1, uvm_run_test.ended ? 1U : 0U, 0);
    }
    if (identity == "@uvm-run-test-end") {
        if (output_hook) {
            output_hook(0U, uvm_reports.server().summarize(), true,
                interpreter->scheduler().now(),
                interpreter->scheduler().delta());
        }
        return runtime::PackedLogic4::from_aval_bval(1, 0, 0);
    }
    return std::nullopt;
}

Simulation::Impl::UvmMethodRole Simulation::Impl::uvm_method_role(
    const std::string_view canonical_identity)
{
    if (!uvm_method_roles) {
        auto& roles = uvm_method_roles.emplace();
        for (const auto& specialization :
            built.compiled_systemverilog_class_specializations) {
            for (const auto& method : specialization.methods) {
                if (method.owner_identity != specialization.declaration_identity)
                    continue;
                roles.emplace(method.canonical_identity,
                    UvmMethodRole { method.uvm_role, specialization.uvm_role,
                        method.virtual_slot });
                if (method.uvm_role == support::UvmRole::object_get_object_type
                    && specialization.uvm_role == support::UvmRole::object)
                    uvm_object_type_slot = method.virtual_slot;
            }
        }
    }
    const auto found = uvm_method_roles->find(canonical_identity);
    return found == uvm_method_roles->end() ? UvmMethodRole { } : found->second;
}

std::optional<std::uint32_t> Simulation::Impl::uvm_object_get_object_type_slot()
{
    static_cast<void>(uvm_method_role({ }));
    return uvm_object_type_slot;
}

runtime::SystemVerilogClassHandle Simulation::Impl::uvm_root_report_handle()
{
    // Allocated once without running the library constructor; reports through
    // it go to the root. The root's empty full name reports as its handler's
    // name, "reporter", as the library's uvm_root constructor names it.
    if (uvm_root_report_object == 0) {
        const auto report_object = uvm_role_class(
            built.compiled_systemverilog_class_specializations,
            support::UvmRole::report_object);
        uvm_root_report_object = construct_class(
            "@no-constructor:" + report_object, report_object,
            std::span<const runtime::PackedLogic4> { },
            std::span<const std::string> { },
            std::span<const std::string> { }, "$uvm-root");
        uvm_objects.set_name(uvm_root_report_object, "reporter");
    }
    return uvm_root_report_object;
}

runtime::SystemVerilogClassHandle Simulation::Impl::uvm_singleton(
    const std::string& identity)
{
    auto found = uvm_singletons.find(identity);
    if (found == uvm_singletons.end()) {
        found = uvm_singletons.emplace(identity,
            construct_class("@no-constructor:" + identity, identity,
                std::span<const runtime::PackedLogic4> { },
                std::span<const std::string> { },
                std::span<const std::string> { }, "$uvm-root")).first;
    }
    // uvm_root::m_uvm_get_root assigns uvm_top when it makes the root, and
    // uvm_cmdline_processor::get_inst assigns uvm_cmdline_proc
    // (1800.2-2020.3.1 uvm_root.svh, uvm_cmdline_processor.svh); UVM 1.2
    // initializes the variables with the getters, which store the same
    // handles.
    const auto class_role = uvm_declared_role(
        built.compiled_systemverilog_class_specializations, identity);
    const auto variable = class_role == support::UvmRole::root
        ? support::UvmRole::top
        : class_role == support::UvmRole::cmdline_processor
        ? support::UvmRole::cmdline_proc
        : support::UvmRole::none;
    const auto path = variable == support::UvmRole::none
        ? std::string { }
        : uvm_package_variable_path(built.systemverilog_hir, variable);
    if (!path.empty()) {
        auto signal = uvm_singleton_variable_signals.find(path);
        if (signal == uvm_singleton_variable_signals.end()) {
            signal = uvm_singleton_variable_signals.emplace(
                path, std::optional<runtime::simir::SignalId> { }).first;
            for (const auto& [name, id] : built.design.signal_paths()) {
                if (name == path || name.ends_with("." + path)) {
                    signal->second = id;
                    break;
                }
            }
        }
        if (signal->second) {
            const auto current = interpreter->signal_value_snapshot(
                *signal->second);
            if (current.low_word().bval != 0U
                || current.low_word().aval != found->second) {
                interpreter->deposit_signal(*signal->second,
                    runtime::PackedLogic4::from_aval_bval(
                        64, found->second, 0));
            }
        }
    }
    return found->second;
}

void Simulation::Impl::seed_uvm_coreservice(
    const std::string_view owner, const std::string_view name)
{
    // The static `inst` of the library's core service class holds its
    // default when nothing set one (IEEE 1800.2-2020 F.4.1, F.4.2): the
    // property and the classes by their recorded roles.
    const auto& specializations
        = built.compiled_systemverilog_class_specializations;
    const auto owner_class = std::ranges::find(specializations, owner,
        &semantic::sv::ClassSpecialization::declaration_identity);
    if (owner_class == specializations.end()
        || owner_class->uvm_role != support::UvmRole::coreservice
        || std::ranges::none_of(owner_class->properties,
            [&](const semantic::sv::SpecializedClassProperty& property) {
                return property.name == name && property.static_storage
                    && property.uvm_role == support::UvmRole::coreservice_inst;
            })) {
        return;
    }
    const auto standard = uvm_role_class(
        specializations, support::UvmRole::default_coreservice);
    if (standard.empty()) {
        return;
    }
    if (const auto current = packed_property_value(
            class_static_store.property(owner, name));
        current.low_word().aval != 0U || current.low_word().bval != 0U) {
        return;
    }
    const auto handle = uvm_singleton(standard);
    assign_property_value(class_static_store.property(owner, name),
        std::string { owner } + "::" + std::string { name },
        runtime::PackedLogic4::from_aval_bval(64, handle, 0));
}

bool Simulation::Impl::is_uvm_root_handle(
    const runtime::SystemVerilogClassHandle handle) const
{
    return handle != 0
        && std::ranges::any_of(uvm_singletons, [&](const auto& entry) {
               return entry.second == handle
                   && uvm_declared_role(
                          built.compiled_systemverilog_class_specializations,
                          entry.first)
                       == support::UvmRole::root;
           });
}

std::vector<runtime::SystemVerilogClassHandle>
Simulation::Impl::uvm_sorted_children(
    const runtime::SystemVerilogClassHandle component) const
{
    // uvm_component keeps its children in an associative array keyed by
    // name, so get_first_child, get_next_child and the topology walk them in
    // name order (IEEE 1800.2-2020 13.1.4).
    auto children = uvm_components.children(component);
    std::ranges::stable_sort(children, [this](const auto left, const auto right) {
        return uvm_components.snapshot(left).name
            < uvm_components.snapshot(right).name;
    });
    return children;
}

std::vector<runtime::SystemVerilogClassHandle>
Simulation::Impl::uvm_root_top_components() const
{
    std::vector<runtime::SystemVerilogClassHandle> tops;
    for (const auto root : uvm_components.roots()) {
        const auto found = uvm_components.top_components(root);
        tops.insert(tops.end(), found.begin(), found.end());
    }
    std::ranges::stable_sort(tops, [this](const auto left, const auto right) {
        return uvm_components.snapshot(left).name
            < uvm_components.snapshot(right).name;
    });
    return tops;
}

runtime::SystemVerilogClassHandle Simulation::Impl::uvm_root_object() const
{
    for (const auto& [identity, handle] : uvm_singletons) {
        if (uvm_declared_role(built.compiled_systemverilog_class_specializations,
                identity)
            == support::UvmRole::root) {
            return handle;
        }
    }
    return 0U;
}

std::vector<runtime::SystemVerilogClassHandle>
Simulation::Impl::uvm_find_all(
    const std::string_view pattern,
    const runtime::SystemVerilogClassHandle start) const
{
    // uvm_root::find_all and m_find_all_recurse: the children of a component
    // first, in name order, then the component itself when its full name
    // matches the glob (uvm_is_match); the root, named "__top__" with the
    // empty full name, closes the list when the pattern matches "".
    using Handle = runtime::SystemVerilogClassHandle;
    std::vector<Handle> matches;
    const auto walk = [&](const auto& self, const Handle component) -> void {
        for (const auto child : uvm_sorted_children(component)) {
            self(self, child);
        }
        if (support::glob_match(
                pattern, uvm_components.full_name(component), [] { })) {
            matches.push_back(component);
        }
    };
    if (start != 0U && uvm_components.contains(start)) {
        walk(walk, start);
    } else {
        for (const auto top : uvm_root_top_components()) {
            walk(walk, top);
        }
        if (support::glob_match(pattern, std::string_view { }, [] { })) {
            matches.push_back(uvm_root_object());
        }
    }
    return matches;
}

std::optional<runtime::PackedLogic4> Simulation::Impl::invoke_uvm_root_method(
    const support::UvmRole method,
    std::vector<runtime::PackedLogic4>& actuals,
    std::vector<std::string>& string_actuals,
    const std::span<const std::string> actual_names)
{
    using Handle = runtime::SystemVerilogClassHandle;
    // The root's children are the top components of every root, in the name
    // order of the library's associative array (IEEE 1800.2-2020 13.1.4).
    const auto top_components = [this] { return uvm_root_top_components(); };
    const auto handle_value = [](const Handle value) {
        return runtime::PackedLogic4::from_aval_bval(64, value, 0);
    };
    const auto bit_value = [](const bool value) {
        return runtime::PackedLogic4::from_aval_bval(1, value ? 1U : 0U, 0);
    };
    // has_child, get_first_child and get_next_child return int (13.1.4).
    const auto int_value = [](const bool value) {
        return runtime::PackedLogic4::from_aval_bval(32, value ? 1U : 0U, 0);
    };
    const bool string_result = !actual_names.empty()
        && actual_names.back() == "@result-string" && !string_actuals.empty();
    const auto name_argument = [&]() -> std::string {
        return string_actuals.empty() ? std::string { } : string_actuals.front();
    };
    const auto warn = [this](const std::string_view id, std::string message) {
        runtime::SystemVerilogUvmReportRequest request;
        request.report_object = uvm_root_report_handle();
        request.severity = runtime::SystemVerilogUvmReportSeverity::Warning;
        request.id = std::string { id };
        request.message = std::move(message);
        request.verbosity = 0;
        request.timestamp = interpreter->scheduler().now();
        (void)uvm_reports.report(request);
    };
    // The root is named "__top__" and has the empty full name (13.2).
    if (method == support::UvmRole::get_name && string_result) {
        string_actuals.back() = "__top__";
        return bit_value(false);
    }
    if (method == support::UvmRole::get_full_name && string_result) {
        string_actuals.back().clear();
        return bit_value(false);
    }
    if (method == support::UvmRole::get_type_name && string_result) {
        string_actuals.back() = "uvm_root";
        return bit_value(false);
    }
    if (method == support::UvmRole::component_get_parent) {
        return handle_value(0U);
    }
    if (method == support::UvmRole::component_get_num_children) {
        return runtime::PackedLogic4::from_aval_bval(
            32, top_components().size(), 0);
    }
    // get_child, has_child, get_first_child and get_next_child (13.1.4).
    if (method == support::UvmRole::component_get_child || method == support::UvmRole::component_has_child) {
        const auto wanted = name_argument();
        for (const auto child : top_components()) {
            if (uvm_components.snapshot(child).name == wanted) {
                return method == support::UvmRole::component_has_child ? int_value(true)
                                             : handle_value(child);
            }
        }
        return method == support::UvmRole::component_has_child ? int_value(false) : handle_value(0U);
    }
    if ((method == support::UvmRole::component_get_first_child || method == support::UvmRole::component_get_next_child)
        && !string_actuals.empty()) {
        const auto children = top_components();
        std::size_t index { };
        if (method == support::UvmRole::component_get_next_child) {
            index = children.size();
            for (std::size_t position { }; position < children.size();
                ++position) {
                if (uvm_components.snapshot(children[position]).name
                    == string_actuals.front()) {
                    index = position + 1U;
                    break;
                }
            }
        }
        if (index >= children.size()) {
            return int_value(false);
        }
        string_actuals.front() = uvm_components.snapshot(children[index]).name;
        return int_value(true);
    }
    // find(comp_match): the components whose full name matches, children
    // before their parent, the first one returned (uvm_root::find_all and
    // m_find_all_recurse).
    if (method == support::UvmRole::root_find || method == support::UvmRole::component_lookup) {
        const auto pattern = name_argument();
        std::vector<Handle> matches;
        if (method == support::UvmRole::root_find) {
            matches = uvm_find_all(pattern, 0U);
        } else {
            const auto walk = [&](const auto& self, const Handle component) -> void {
                for (const auto child : uvm_sorted_children(component)) {
                    self(self, child);
                }
                if (uvm_components.full_name(component) == pattern) {
                    matches.push_back(component);
                }
            };
            for (const auto top : top_components()) {
                walk(walk, top);
            }
        }
        if (method == support::UvmRole::component_lookup) {
            return handle_value(matches.empty() ? Handle { } : matches.front());
        }
        if (matches.size() > 1U) {
            warn("MMATCH",
                "Found " + std::to_string(matches.size())
                    + " components matching '" + pattern
                    + "'. Returning first match, "
                    + (uvm_components.contains(matches.front())
                          ? uvm_components.full_name(matches.front())
                          : std::string { }) + ".");
        }
        if (matches.empty()) {
            warn("CMPNFD",
                "Component matching '" + pattern
                    + "' was not found in the list of uvm_components");
            return handle_value(0U);
        }
        return handle_value(matches.front());
    }
    if (method == support::UvmRole::root_print_topology) {
        print_uvm_topology(actuals.empty() ? Handle { }
                                           : Handle { actuals[0].low_word().aval });
        return bit_value(false);
    }
    if (method == support::UvmRole::root_find_all || method == support::UvmRole::component_get_children) {
        // The queue formal is a `ref` argument (IEEE 1800.2-2020 13.1.5,
        // 13.2.3): the call carries it as the one encoded string actual of
        // its position (the handles in decimal; simir_class.hpp), and the
        // matches replace its contents, in the walk's order.
        const auto position = method == support::UvmRole::root_find_all ? std::size_t { 1U }
                                                   : std::size_t { 0U };
        if (string_actuals.size() <= position) {
            throw std::invalid_argument {
                std::string { method == support::UvmRole::root_find_all
                        ? "uvm_root::find_all" : "uvm_root::get_children" }
                + " is missing its queue argument"
            };
        }
        const auto start = method == support::UvmRole::root_find_all && actuals.size() > 2U
            ? Handle { actuals[2].low_word().aval } : Handle { };
        const auto found = method == support::UvmRole::root_find_all
            ? uvm_find_all(name_argument(), start)
            : top_components();
        // The library appends (push_back) to the queue it is given.
        auto encoded = runtime::simir::decode_dpi_string_array(
            string_actuals[position]);
        encoded.reserve(encoded.size() + found.size());
        for (const auto component : found) {
            encoded.push_back(std::to_string(component));
        }
        string_actuals[position] = runtime::simir::encode_dpi_string_array(encoded);
        return bit_value(false);
    }
    return std::nullopt;
}

void Simulation::Impl::print_uvm_topology(
    const runtime::SystemVerilogClassHandle printer_argument)
{
    // uvm_root::print_topology (IEEE 1800.2-2020 13.2.4.2): the printer (the
    // argument, else uvm_default_printer) prints each top component with
    // print_object, which adds one row per component and nests the children
    // one level in; emit then formats the rows. The knobs and the printer
    // kind are read from the printer object (uvm_printer_knobs, 16.2).
    struct Row {
        std::size_t level { };
        std::string name;
        std::string type;
        std::string size { "-" };
        std::string value;
    };
    struct Knobs {
        bool header { true };
        bool footer { true };
        bool full_name { false };
        bool identifier { true };
        bool type_name { true };
        bool size { true };
        std::int64_t depth { -1 };
        bool reference { true };
        std::string prefix;
        std::int64_t indent { 2 };
        bool show_root { false };
        std::string separator { "{}" };
        std::string newline { "\n" };
    };
    enum class Kind { table, tree, line } kind { Kind::table };
    Knobs knobs;
    auto printer = printer_argument;
    if (printer == 0U) {
        // uvm_default_printer, the variable of the library package.
        if (!uvm_default_printer_signal) {
            uvm_default_printer_signal.emplace();
            const auto printer_path = uvm_package_variable_path(
                built.systemverilog_hir, support::UvmRole::default_printer);
            for (const auto& [path, id] : built.design.signal_paths()) {
                if (!printer_path.empty() && path == printer_path) {
                    *uvm_default_printer_signal = id;
                    break;
                }
            }
        }
        if (*uvm_default_printer_signal) {
            const auto value = interpreter->signal_value_snapshot(
                **uvm_default_printer_signal);
            if (value.low_word().bval == 0U) {
                printer = value.low_word().aval;
            }
        }
    }
    if (printer != 0U && class_heap.contains(printer)) {
        // The classes of the printer's chain, most derived first.
        std::vector<const semantic::sv::ClassSpecialization*> chain;
        const auto chain_of = [&](const runtime::SystemVerilogClassHandle owner) {
            std::vector<const semantic::sv::ClassSpecialization*> result;
            std::set<std::string_view> followed;
            for (const auto* current = &class_specialization(
                     class_heap.object(owner).specialization_identity);
                 current != nullptr
                 && followed.insert(current->specialization_identity).second;
                 current = current->base
                     ? &class_specialization(current->base->specialization_identity)
                     : nullptr) {
                result.push_back(current);
            }
            return result;
        };
        chain = chain_of(printer);
        // Whether the printer's class is, or derives from, the library
        // class with role `role`.
        const auto derives_from = [&](const support::UvmRole role) {
            return std::ranges::any_of(chain, [&](const auto* item) {
                return item->uvm_role == role;
            });
        };
        // A tree printer's newline is "\n"; the line printer derives from it
        // with a newline of " " and no indentation (uvm_printer.svh).
        if (derives_from(support::UvmRole::line_printer)) {
            kind = Kind::line;
            knobs.newline = " ";
        } else if (derives_from(support::UvmRole::tree_printer)) {
            kind = Kind::tree;
        }
        // The library property with role `role` of the object's class
        // chain, by its recorded role (support::UvmRole); none when no class
        // of the chain declares it.
        const auto property = [&](const runtime::SystemVerilogClassHandle owner,
                                  const support::UvmRole role)
            -> const runtime::SystemVerilogClassPropertyValue* {
            if (owner == 0U || !class_heap.contains(owner))
                return nullptr;
            for (const auto* item : chain_of(owner)) {
                for (const auto& declared : item->properties) {
                    if (declared.uvm_role == role)
                        return class_heap.member(owner, declared.name);
                }
            }
            return nullptr;
        };
        if (kind != Kind::table) {
            // uvm_tree_printer::new clears size, type_name, header and footer.
            knobs.size = false;
            knobs.type_name = false;
            knobs.header = false;
            knobs.footer = false;
            if (kind == Kind::line) {
                knobs.indent = 0;
            }
            if (const auto* newline = property(
                    printer, support::UvmRole::printer_newline)) {
                knobs.newline = newline->string;
            }
        }
        if (const auto* held = property(
                printer, support::UvmRole::printer_knobs_handle)) {
            const auto owner = held->handle;
            const auto flag = [&](const support::UvmRole name, bool& target) {
                if (const auto* value = property(owner, name);
                    value != nullptr && value->packed.width() != 0U) {
                    target = value->packed.low_word().aval != 0U;
                }
            };
            const auto number = [&](const support::UvmRole name,
                                    std::int64_t& target) {
                if (const auto* value = property(owner, name);
                    value != nullptr && value->packed.width() != 0U
                    && value->packed.low_word().bval == 0U) {
                    target = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(
                            value->packed.low_word().aval));
                }
            };
            const auto text = [&](const support::UvmRole name,
                                  std::string& target) {
                if (const auto* value = property(owner, name)) {
                    target = value->string;
                }
            };
            flag(support::UvmRole::knobs_header, knobs.header);
            flag(support::UvmRole::knobs_footer, knobs.footer);
            flag(support::UvmRole::knobs_full_name, knobs.full_name);
            flag(support::UvmRole::knobs_identifier, knobs.identifier);
            flag(support::UvmRole::knobs_type_name, knobs.type_name);
            flag(support::UvmRole::knobs_size, knobs.size);
            number(support::UvmRole::knobs_depth, knobs.depth);
            flag(support::UvmRole::knobs_reference, knobs.reference);
            text(support::UvmRole::knobs_prefix, knobs.prefix);
            number(support::UvmRole::knobs_indent, knobs.indent);
            flag(support::UvmRole::knobs_show_root, knobs.show_root);
            text(support::UvmRole::knobs_separator, knobs.separator);
        }
    }
    if (knobs.separator.size() < 2U) {
        knobs.separator = "{}";
    }
    std::vector<Row> rows;
    const auto add = [&](const auto& self,
                          const runtime::SystemVerilogClassHandle component,
                          const std::size_t level) -> void {
        const auto snapshot = uvm_components.snapshot(component);
        rows.push_back({ level,
            level == 0U || knobs.full_name ? snapshot.full_name : snapshot.name,
            uvm_objects.type_name(component), "-",
            knobs.reference
                ? "@" + std::to_string(uvm_objects.instance_id(component))
                : std::string { "-" } });
        // The children are printed while depth is -1 or above this level.
        if (knobs.depth != -1 && knobs.depth <= static_cast<std::int64_t>(level)) {
            return;
        }
        for (const auto child : uvm_sorted_children(component)) {
            self(self, child, level + 1U);
        }
    };
    for (const auto top : uvm_root_top_components()) {
        add(add, top, 0U);
    }
    runtime::SystemVerilogUvmReportRequest request;
    request.report_object = uvm_root_report_handle();
    request.timestamp = interpreter->scheduler().now();
    request.verbosity = 0;
    if (rows.empty()) {
        request.severity = runtime::SystemVerilogUvmReportSeverity::Warning;
        request.id = "EMTCOMP";
        request.message = "print_topology - No UVM components to print.";
        (void)uvm_reports.report(request);
        return;
    }
    const auto spaces = [](const std::int64_t count) {
        return std::string(
            static_cast<std::size_t>(std::max<std::int64_t>(count, 0)), ' ');
    };
    std::string text;
    if (kind == Kind::table) {
        // uvm_table_printer::calculate_max_widths and emit.
        std::int64_t name_width { 4 };
        std::int64_t type_width { 4 };
        std::int64_t size_width { 4 };
        std::int64_t value_width { 5 };
        for (const auto& row : rows) {
            name_width = std::max(name_width,
                knobs.indent * static_cast<std::int64_t>(row.level)
                    + static_cast<std::int64_t>(row.name.size()));
            type_width = std::max(
                type_width, static_cast<std::int64_t>(row.type.size()));
            size_width = std::max(
                size_width, static_cast<std::int64_t>(row.size.size()));
            value_width = std::max(
                value_width, static_cast<std::int64_t>(row.value.size()));
        }
        const std::string linefeed = "\n" + knobs.prefix;
        std::string dashes;
        if (knobs.header) {
            std::string header;
            if (knobs.identifier) {
                dashes += std::string(static_cast<std::size_t>(name_width + 2), '-');
                header += "Name" + spaces(name_width - 2);
            }
            if (knobs.type_name) {
                dashes += std::string(static_cast<std::size_t>(type_width + 2), '-');
                header += "Type" + spaces(type_width - 2);
            }
            if (knobs.size) {
                dashes += std::string(static_cast<std::size_t>(size_width + 2), '-');
                header += "Size" + spaces(size_width - 2);
            }
            dashes += std::string(static_cast<std::size_t>(value_width), '-')
                + linefeed;
            header += "Value" + spaces(value_width - 5) + linefeed;
            text += dashes + header + dashes;
        }
        for (const auto& row : rows) {
            const auto level = static_cast<std::int64_t>(row.level);
            std::string line;
            if (knobs.identifier) {
                line = spaces(level * knobs.indent) + row.name
                    + spaces(name_width - static_cast<std::int64_t>(row.name.size())
                          - level * knobs.indent + 2);
            }
            if (knobs.type_name) {
                line += row.type
                    + spaces(type_width - static_cast<std::int64_t>(row.type.size()) + 2);
            }
            if (knobs.size) {
                line += row.size
                    + spaces(size_width - static_cast<std::int64_t>(row.size.size()) + 2);
            }
            text += line + row.value
                + spaces(value_width - static_cast<std::int64_t>(row.value.size()))
                + linefeed;
        }
        if (knobs.footer) {
            text += dashes;
        }
    } else {
        // uvm_tree_printer::emit (the line printer is a tree printer whose
        // newline is a space).
        const std::string linefeed = knobs.newline.empty() || knobs.newline == " "
            ? knobs.newline
            : knobs.newline + knobs.prefix;
        text = knobs.prefix;
        for (std::size_t index = 0U; index < rows.size(); ++index) {
            const auto& row = rows[index];
            const auto level = static_cast<std::int64_t>(row.level);
            if (knobs.identifier) {
                text += spaces(level * knobs.indent) + row.name;
                if (!row.name.empty() && row.name != "...") {
                    text += ": ";
                }
            }
            if (!row.value.empty() && row.value.front() == '@') {
                text += "(" + row.type + row.value + ") ";
            } else if (knobs.type_name) {
                text += "(" + row.type + ") ";
            }
            if (knobs.size) {
                text += "(" + row.size + ") ";
            }
            if (index + 1U < rows.size() && rows[index + 1U].level > row.level) {
                text += std::string(1U, knobs.separator[0]) + linefeed;
                continue;
            }
            text += row.value + " " + linefeed;
            const auto end_level = index + 1U == rows.size()
                ? std::int64_t { 0 }
                : static_cast<std::int64_t>(rows[index + 1U].level);
            for (auto closing = level - 1; closing >= end_level; --closing) {
                text += spaces(closing * knobs.indent)
                    + std::string(1U, knobs.separator[1]) + linefeed;
            }
        }
        if (knobs.newline.empty() || knobs.newline == " ") {
            text += "\n";
        }
    }
    request.severity = runtime::SystemVerilogUvmReportSeverity::Info;
    request.id = "UVMTOP";
    request.message = "UVM testbench topology:\n" + text;
    (void)uvm_reports.report(request);
}
} // namespace fsim::app
