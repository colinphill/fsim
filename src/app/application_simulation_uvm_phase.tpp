// SPDX-License-Identifier: Apache-2.0
[[nodiscard]] runtime::SystemVerilogUvmPhaseExecutionResult
execute_uvm_function_phase(
    const runtime::SystemVerilogUvmPhaseHandle phase_handle) {
  const auto phase_snapshot = uvm_phases.snapshot(phase_handle);
  (void)apply_uvm_report_settings(
      phase_snapshot.identity, interpreter->scheduler().now());
  const auto invoke_callback =
      [&](const runtime::SystemVerilogClassHandle component,
          const std::string_view method_name) {
        const auto *method = source_method_named(component, method_name);
        if (method == nullptr)
          return;
        if (method->kind != frontend::SystemVerilogClassMethodKind::Function ||
            method->is_static || method->arguments.size() > 1U) {
          throw std::invalid_argument{
              "UVM function-phase callback has an unsupported profile"};
        }
        std::vector<runtime::PackedLogic4> actuals;
        if (method->arguments.size() == 1U) {
          actuals.push_back(runtime::PackedLogic4(64));
        }
        const auto before = packed_class_snapshot();
        const auto static_before = packed_static_snapshot();
        (void)invoke_source_profile(*method, component, actuals, {}, {});
        notify_class_changes(before);
        notify_static_changes(static_before);
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
execute_uvm_task_phase(const runtime::SystemVerilogUvmPhaseHandle phase_handle,
                       const UvmTaskPhaseContinuation &continuation) {
  const auto phase_snapshot = uvm_phases.snapshot(phase_handle);
  (void)apply_uvm_report_settings(
      phase_snapshot.identity, interpreter->scheduler().now());
  const auto invoke_hook =
      [&](const runtime::SystemVerilogClassHandle component,
          const runtime::SystemVerilogUvmPhaseHandle,
          const runtime::SystemVerilogUvmPhaseCallbackKind callback) {
        const auto method_name =
            callback == runtime::SystemVerilogUvmPhaseCallbackKind::PhaseStarted
                ? std::string_view{"phase_started"}
            : callback ==
                    runtime::SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd
                ? std::string_view{"phase_ready_to_end"}
                : std::string_view{"phase_ended"};
        const auto *method = source_method_named(component, method_name);
        if (method != nullptr) {
          if (method->kind !=
                  frontend::SystemVerilogClassMethodKind::Function ||
              method->is_static || method->arguments.size() > 1U) {
            throw std::invalid_argument{
                "UVM task-phase hook has an unsupported profile"};
          }
          std::vector<runtime::PackedLogic4> actuals;
          if (method->arguments.size() == 1U) {
            actuals.push_back(runtime::PackedLogic4(64));
          }
          const auto before = packed_class_snapshot();
          const auto static_before = packed_static_snapshot();
          (void)invoke_source_profile(*method, component, actuals, {}, {});
          notify_class_changes(before);
          notify_static_changes(static_before);
        }
        uvm_sequences.dispatch_role_function(component, phase_handle, callback);
      };
  return uvm_phases.execute_task_phase(
      phase_handle, invoke_hook,
      [&](const runtime::SystemVerilogClassHandle component,
          const runtime::SystemVerilogUvmPhaseHandle callback_phase,
          const runtime::SystemVerilogUvmPhaseProcessHandle process) {
        const auto *method =
            source_method_named(component, phase_snapshot.identity + "_phase");
        if (method != nullptr) {
          if (method->kind != frontend::SystemVerilogClassMethodKind::Task ||
              method->is_static || method->arguments.size() > 1U) {
            throw std::invalid_argument{
                "UVM task-phase callback has an unsupported profile"};
          }
          std::vector<runtime::PackedLogic4> actuals;
          if (method->arguments.size() == 1U) {
            actuals.push_back(runtime::PackedLogic4(64));
          }
          const auto before = packed_class_snapshot();
          const auto static_before = packed_static_snapshot();
          invoke_source_task_profile(*method, component, actuals);
          notify_class_changes(before);
          notify_static_changes(static_before);
        }
        const auto role_status = uvm_sequences.dispatch_role_task(
            component, callback_phase, process);
        const auto continuation_status =
            continuation ? continuation(component, callback_phase, process)
                         : runtime::SystemVerilogUvmTaskPhaseStatus::Completed;
        return role_status == runtime::SystemVerilogUvmTaskPhaseStatus::
                                  Suspended ||
                       continuation_status ==
                           runtime::SystemVerilogUvmTaskPhaseStatus::Suspended
                   ? runtime::SystemVerilogUvmTaskPhaseStatus::Suspended
                   : runtime::SystemVerilogUvmTaskPhaseStatus::Completed;
      });
}
