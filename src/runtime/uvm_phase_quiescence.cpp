// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/uvm_objection.hpp"
#include "uvm_phase_internal.hpp"

#include <algorithm>
#include <ranges>
#include <string>

namespace fsim::runtime {

namespace {

[[noreturn]] void fail_uvm(
    const std::string_view code, const std::string_view message)
{
    throw SystemVerilogUvmPhaseError {
        std::string { code }, std::string { message }
    };
}

} // namespace

SystemVerilogUvmQuiescenceResult
SystemVerilogUvmPhaseService::settle_task_phase(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const FunctionPhaseCallback& hook_callback,
    const std::optional<SimulationTick> deadline) {
  auto& selected = phase(phase_handle);
  if (!scheduler_ || !hook_callback
      || selected.execution != SystemVerilogUvmPhaseExecutionKind::Task
      || selected.state != SystemVerilogUvmPhaseState::Executing
      || !selected.task_result) {
    fail_uvm("FSIM-UVM-PHASE-007", "UVM task phase cannot enter quiescence here");
  }
  if (deadline && *deadline < scheduler_->now()) {
    fail_uvm("FSIM-UVM-PHASE-007", "UVM quiescence deadline is in the past");
  }

  SystemVerilogUvmQuiescenceResult result;
  result.phase = phase_handle;
  result.final_state = selected.state;
  result.time = scheduler_->now();
  std::size_t zero_time_iterations{};

  const auto publish = [&](const SystemVerilogUvmActivityAction action,
                           const std::uint64_t value) {
    if (!activity_) return;
    activity_->publish({
        SystemVerilogUvmActivityKind::Quiescence,
        action,
        domain(selected.domain).identity + "." + selected.identity,
        "quiescence",
        0,
        value});
  };

  const auto cancel = [&](const SystemVerilogUvmQuiescenceStatus status) {
    result.status = status;
    result.cancelled = timeout_task_phase(phase_handle);
    result.final_state = phase(phase_handle).state;
    result.time = scheduler_->now();
    publish(SystemVerilogUvmActivityAction::Cancelled,
            static_cast<std::uint64_t>(status));
    return result;
  };
  const auto abort = [&](const std::string_view message) -> void {
    (void)timeout_task_phase(phase_handle);
    publish(SystemVerilogUvmActivityAction::Failed, result.iterations);
    fail_uvm("FSIM-UVM-PHASE-008", message);
  };

  while (result.iterations < limits_.maximum_quiescence_iterations) {
    ++result.iterations;
    const auto running = std::ranges::any_of(
        selected.processes,
        [&](const auto slot) {
          const auto found = processes_.find(slot);
          return found != processes_.end()
              && found->second.state
                  == SystemVerilogUvmPhaseProcessState::Running;
        });
    const auto objection_quiescent = !objections_
        || objections_->phase_quiescent(phase_handle);
    if (!running && objection_quiescent) {
      auto execution = complete_task_phase(phase_handle, hook_callback);
      result.final_state = execution.final_state;
      if (execution.final_state == SystemVerilogUvmPhaseState::Done) {
        result.status = SystemVerilogUvmQuiescenceStatus::Completed;
        result.time = scheduler_->now();
        result.execution = std::move(execution);
        publish(SystemVerilogUvmActivityAction::Completed, result.iterations);
        return result;
      }
    }

    if (scheduler_->stop_requested()) {
      return cancel(SystemVerilogUvmQuiescenceStatus::Stopped);
    }
    if (deadline && scheduler_->now() >= *deadline) {
      return cancel(SystemVerilogUvmQuiescenceStatus::TimedOut);
    }
    const auto next = scheduler_->next_pending_time();
    if (!next) {
      abort("UVM task phase is deadlocked without pending scheduler work");
    }
    if (deadline && *next > *deadline) {
      const auto run = scheduler_->run(*deadline);
      result.callbacks += run.callbacks_executed;
      if (run.status == RunStatus::stopped) {
        return cancel(SystemVerilogUvmQuiescenceStatus::Stopped);
      }
      return cancel(SystemVerilogUvmQuiescenceStatus::TimedOut);
    }

    const auto before = scheduler_->now();
    RunResult run;
    try {
      run = scheduler_->run(*next);
    } catch (const DeltaCycleLimitError&) {
      abort("UVM task phase exceeded zero-time scheduler stabilization");
    }
    if (run.callbacks_executed
        > std::numeric_limits<std::uint64_t>::max() - result.callbacks) {
      abort("UVM quiescence callback accounting overflowed");
    }
    result.callbacks += run.callbacks_executed;
    if (result.callbacks > limits_.maximum_quiescence_callbacks) {
      abort("UVM task phase exceeded its quiescence callback ceiling");
    }
    if (run.status == RunStatus::stopped) {
      return cancel(SystemVerilogUvmQuiescenceStatus::Stopped);
    }
    if (run.time == before) {
      ++zero_time_iterations;
      if (zero_time_iterations > limits_.maximum_zero_time_iterations) {
      abort("UVM task phase exceeded its zero-time iteration ceiling");
      }
    } else {
      zero_time_iterations = 0;
    }
    result.time = run.time;
  }

      abort("UVM task phase exceeded its quiescence iteration ceiling");
  return result;
}

} // namespace fsim::runtime
