// SPDX-License-Identifier: Apache-2.0
#include "application_simulation_internal.hpp"

#include <algorithm>
#include <array>

namespace fsim::app {
[[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
Simulation::Impl::execute_uvm_function_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase_handle)
{
    const auto phase_snapshot = uvm_phases.snapshot(phase_handle);
    (void)apply_uvm_report_settings(
        phase_snapshot.identity, interpreter->scheduler().now());
    const auto invoke_callback =
        [&](const runtime::SystemVerilogClassHandle component,
            const std::string_view method_name) {
            if (const auto* method
                = class_hir_execution.method_named(component, method_name)) {
                if (method->kind
                        != semantic::sv::ClassMethodKind::function
                    || method->static_method || method->formals.size() > 1U) {
                    throw std::invalid_argument {
                        "UVM function-phase callback has an unsupported HIR profile"
                    };
                }
                std::vector<runtime::PackedLogic4> actuals;
                if (method->formals.size() == 1U) {
                    actuals.push_back(runtime::PackedLogic4::from_aval_bval(
                        64U, uvm_phase_object(phase_handle), 0U));
                }
                std::vector<std::string> string_actuals(actuals.size());
                const auto before = packed_class_snapshot();
                const auto static_before = packed_static_snapshot();
                (void)class_hir_execution.invoke_function(
                    *method, component, actuals, string_actuals, { }, { });
                notify_class_changes(before);
                notify_static_changes(static_before);
                return;
            }
        };
    return uvm_phases.execute_function_phase(
        phase_handle,
        [&](const runtime::SystemVerilogClassHandle component,
            const runtime::SystemVerilogUvmPhaseHandle,
            const runtime::SystemVerilogUvmPhaseCallbackKind callback) {
            std::string method_name;
            switch (callback) {
            case runtime::SystemVerilogUvmPhaseCallbackKind::PhaseStarted:
                method_name = "phase_started";
                break;
            case runtime::SystemVerilogUvmPhaseCallbackKind::Execute:
                method_name = phase_snapshot.identity + "_phase";
                break;
            case runtime::SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd:
                method_name = "phase_ready_to_end";
                break;
            case runtime::SystemVerilogUvmPhaseCallbackKind::PhaseEnded:
                method_name = "phase_ended";
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
    const auto phase_snapshot = uvm_phases.snapshot(phase_handle);
    (void)apply_uvm_report_settings(
        phase_snapshot.identity, interpreter->scheduler().now());
    const auto invoke_hook =
        [&](const runtime::SystemVerilogClassHandle component,
            const runtime::SystemVerilogUvmPhaseHandle,
            const runtime::SystemVerilogUvmPhaseCallbackKind callback) {
            const auto method_name = callback == runtime::SystemVerilogUvmPhaseCallbackKind::PhaseStarted
                ? std::string_view { "phase_started" }
                : callback == runtime::SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd
                ? std::string_view { "phase_ready_to_end" }
                : std::string_view { "phase_ended" };
            if (const auto* method
                = class_hir_execution.method_named(component, method_name)) {
                if (method->kind
                        != semantic::sv::ClassMethodKind::function
                    || method->static_method || method->formals.size() > 1U) {
                    throw std::invalid_argument {
                        "UVM task-phase hook has an unsupported HIR profile"
                    };
                }
                std::vector<runtime::PackedLogic4> actuals;
                if (method->formals.size() == 1U)
                    actuals.push_back(runtime::PackedLogic4(64U));
                std::vector<std::string> string_actuals(actuals.size());
                const auto before = packed_class_snapshot();
                const auto static_before = packed_static_snapshot();
                (void)class_hir_execution.invoke_function(
                    *method, component, actuals, string_actuals, { }, { });
                notify_class_changes(before);
                notify_static_changes(static_before);
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
            if (const auto* method = class_hir_execution.method_named(
                    component, phase_snapshot.identity + "_phase")) {
                if (method->kind != semantic::sv::ClassMethodKind::task
                    || method->static_method || method->formals.size() > 1U) {
                    throw std::invalid_argument {
                        "UVM task-phase callback has an unsupported HIR profile"
                    };
                }
                std::vector<runtime::PackedLogic4> actuals;
                if (method->formals.size() == 1U)
                    actuals.push_back(runtime::PackedLogic4(64U));
                const auto before = packed_class_snapshot();
                const auto static_before = packed_static_snapshot();
                class_hir_execution.invoke_task(*method, component, actuals);
                notify_class_changes(before);
                notify_static_changes(static_before);
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
    const std::string_view method,
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
    if (method == "raise_objection" || method == "drop_objection") {
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
        if (method == "raise_objection") {
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
    if (method == "get_name" || method == "get_full_name") {
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
        const auto name = string_actuals.empty()
            ? std::string { } : string_actuals.front();
        // A named test is created as uvm_test_top; without a name the
        // components the testbench constructed form the hierarchy.
        if (!name.empty()) {
            (void)uvm_factory.create_component_by_name(
                name, "", "uvm_test_top", 0, uvm_run_test.root);
        }
        uvm_run_test.roots.push_back(uvm_run_test.root);
        for (const auto& [scope, root] : uvm_roots_by_scope) {
            if (root != uvm_run_test.root) {
                uvm_run_test.roots.push_back(root);
            }
        }
        return runtime::PackedLogic4::from_aval_bval(32, 0, 0);
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
} // namespace fsim::app
