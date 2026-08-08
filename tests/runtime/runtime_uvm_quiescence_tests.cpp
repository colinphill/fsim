// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_objection.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

template <typename Callback>
bool rejects(const std::string_view code, Callback&& callback) {
  try {
    std::invoke(std::forward<Callback>(callback));
  } catch (const SystemVerilogUvmPhaseError& error) {
    return error.diagnostic_code() == code;
  }
  return false;
}

struct QuiescenceFixture {
  SystemVerilogClassHeap heap{{64, 1'024}};
  SystemVerilogUvmObjectService objects{
      heap,
      [](const std::string_view,
         const std::string_view,
         const std::string_view) {
        return SystemVerilogClassHandle{};
      }};
  SystemVerilogUvmComponentService components{heap, objects};
  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};

  QuiescenceFixture() {
    SystemVerilogUvmObjectDescriptor object_type;
    object_type.specialization_identity = "work::quiescence_component";
    object_type.type_name = "quiescence_component";
    objects.register_type(std::move(object_type));
    first_root = components.create_root("first");
    second_root = components.create_root("second");
  }

  [[nodiscard]] static SystemVerilogClassDescriptor descriptor() {
    SystemVerilogClassDescriptor result;
    result.declared_type = "uvm_pkg::uvm_component";
    result.dynamic_type = "work::quiescence_component";
    result.specialization_identity = result.dynamic_type;
    result.assignable_declared_types = {
        result.dynamic_type, "uvm_pkg::uvm_component",
        "uvm_pkg::uvm_object"};
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_component(
      std::string name,
      const SystemVerilogUvmRootHandle root) {
    const auto object = heap.allocate(descriptor());
    objects.initialize(object);
    components.initialize(object, std::move(name), 0, root);
    return object;
  }
};

}  // namespace

void test_systemverilog_uvm_quiescence() {
  QuiescenceFixture fixture;
  const auto top = fixture.make_component("top", fixture.first_root);
  const auto isolated = fixture.make_component(
      "isolated", fixture.second_root);
  (void)isolated;
  Scheduler scheduler;
  SystemVerilogUvmPhaseService phases{fixture.components, scheduler};
  SystemVerilogUvmActivityService activity{scheduler};
  phases.set_activity_service(activity);
  const auto domain = phases.create_domain(
      "quiescence", SystemVerilogUvmDomainKind::Custom);
  phases.participate(domain, fixture.first_root);
  phases.participate(domain, fixture.second_root);
  const auto settle_phase = phases.create_custom_phase(
      domain, "settle", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmObjectionService objections{
      fixture.objects, fixture.components, phases, scheduler};
  phases.set_objection_service(objections);
  const auto source = objections.bind_source(top);
  objections.set_drain_time(settle_phase, source, 3);

  const auto execution = phases.execute_task_phase(
      settle_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  for (const auto& process : execution.processes) {
    phases.complete_task_process(process);
  }
  std::size_t ready_calls{};
  std::size_t ended_calls{};
  bool raced{};
  const auto hooks = [&](const auto component, const auto phase, const auto kind) {
    if (kind == SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd) {
      ++ready_calls;
      if (!raced && component == top) {
        raced = true;
        (void)objections.raise(phase, source, "ready race", 1);
        (void)objections.drop(phase, source, "ready race", 1);
      }
    } else if (kind == SystemVerilogUvmPhaseCallbackKind::PhaseEnded) {
      ++ended_calls;
    }
  };
  const auto settled = phases.settle_task_phase(
      settle_phase, hooks, 10);
  require(
      settled.status == SystemVerilogUvmQuiescenceStatus::Completed
          && settled.final_state == SystemVerilogUvmPhaseState::Done
          && settled.time == 3 && settled.callbacks == 1
          && settled.execution && settled.execution->success()
          && ready_calls == 4 && ended_calls == 2
          && phases.process_count() == 0,
      "quiescence must re-enter ready-to-end after an objection drain race");
  require(
      std::ranges::any_of(activity.events(), [](const auto& event) {
        return event.kind == SystemVerilogUvmActivityKind::Quiescence
            && event.action == SystemVerilogUvmActivityAction::Completed;
      }),
      "quiescence activity must publish its deterministic completion");

  const auto timeout_phase = phases.create_custom_phase(
      domain, "timeout", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmPhaseProcessHandle timeout_parent;
  (void)phases.execute_task_phase(
      timeout_phase,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto, const auto process) {
        if (component == top) timeout_parent = process;
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool phase_wait_ran{};
  bool caller_wait_ran{};
  (void)phases.schedule_child_process(
      timeout_parent, 10, [&](const auto) { phase_wait_ran = true; });
  scheduler.schedule_after(
      10, SchedulerPhase::reactive, 100,
      [&](Scheduler&) { caller_wait_ran = true; });
  const auto timed_out = phases.settle_task_phase(
      timeout_phase, [](const auto, const auto, const auto) {}, 5);
  require(
      timed_out.status == SystemVerilogUvmQuiescenceStatus::TimedOut
          && timed_out.time == 5 && !timed_out.cancelled.empty()
          && phases.snapshot(timeout_phase).state
              == SystemVerilogUvmPhaseState::Done,
      "a quiescence deadline must cancel only the timed-out phase at its tick");
  (void)scheduler.run();
  require(
      !phase_wait_ran && caller_wait_ran,
      "timeout cleanup must preserve caller-owned future scheduler work");

  const auto stop_phase = phases.create_custom_phase(
      domain, "stop", SystemVerilogUvmPhaseExecutionKind::Task);
  (void)phases.execute_task_phase(
      stop_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  scheduler.request_stop();
  const auto stopped = phases.settle_task_phase(
      stop_phase, [](const auto, const auto, const auto) {});
  require(
      stopped.status == SystemVerilogUvmQuiescenceStatus::Stopped
          && !stopped.cancelled.empty()
          && phases.snapshot(stop_phase).state
              == SystemVerilogUvmPhaseState::Done,
      "a simulation stop must deterministically reclaim active phase work");
  scheduler.clear_stop();

  const auto restart_first = phases.create_custom_phase(
      domain, "restart_first", SystemVerilogUvmPhaseExecutionKind::Task);
  const auto restart_second = phases.create_custom_phase(
      domain, "restart_second", SystemVerilogUvmPhaseExecutionKind::Task);
  phases.connect(restart_first, restart_second);
  (void)phases.execute_task_phase(
      restart_first,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });
  (void)phases.execute_task_phase(
      restart_second,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  (void)phases.timeout_task_phase(restart_second);
  const auto restarted = phases.jump(restart_second, restart_first);
  require(
      restarted.kind == SystemVerilogUvmPhaseJumpKind::Backward
          && phases.snapshot(restart_first).state
              == SystemVerilogUvmPhaseState::Dormant
          && phases.snapshot(restart_second).state
              == SystemVerilogUvmPhaseState::Dormant,
      "stopped or timed-out phase intervals must remain restartable by jump");
  const auto restarted_execution = phases.execute_task_phase(
      restart_first,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Completed;
      });
  require(
      restarted_execution.final_state == SystemVerilogUvmPhaseState::Done,
      "a reset phase interval must execute without leaked process state");

  const auto deadlock_phase = phases.create_custom_phase(
      domain, "deadlock", SystemVerilogUvmPhaseExecutionKind::Task);
  (void)phases.execute_task_phase(
      deadlock_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  require(
      rejects("FSIM-UVM-PHASE-008", [&] {
        (void)phases.settle_task_phase(
            deadlock_phase, [](const auto, const auto, const auto) {});
      })
          && phases.snapshot(deadlock_phase).state
              == SystemVerilogUvmPhaseState::Done
          && phases.process_count() == 0,
      "no-work quiescence deadlock must diagnose after deterministic cleanup");

  QuiescenceFixture livelock_fixture;
  const auto livelock_top = livelock_fixture.make_component(
      "top", livelock_fixture.first_root);
  (void)livelock_top;
  Scheduler livelock_scheduler{{3, 4}};
  SystemVerilogUvmPhaseService livelock_phases{
      livelock_fixture.components, livelock_scheduler};
  const auto livelock_domain = livelock_phases.create_domain(
      "livelock", SystemVerilogUvmDomainKind::Custom);
  livelock_phases.participate(
      livelock_domain, livelock_fixture.first_root);
  const auto livelock_phase = livelock_phases.create_custom_phase(
      livelock_domain, "livelock", SystemVerilogUvmPhaseExecutionKind::Task);
  (void)livelock_phases.execute_task_phase(
      livelock_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  std::function<void(Scheduler&)> oscillate;
  oscillate = [&](Scheduler& runtime) {
    runtime.schedule_next_delta(SchedulerPhase::active, 1, oscillate);
  };
  livelock_scheduler.schedule(SchedulerPhase::active, 1, oscillate);
  require(
      rejects("FSIM-UVM-PHASE-008", [&] {
        (void)livelock_phases.settle_task_phase(
            livelock_phase, [](const auto, const auto, const auto) {});
      })
          && livelock_phases.process_count() == 0,
      "zero-time delta livelock must map to a stable UVM diagnostic");

  SystemVerilogUvmPhaseLimits callback_limits;
  callback_limits.maximum_quiescence_callbacks = 2;
  Scheduler callback_scheduler;
  SystemVerilogUvmPhaseService callback_phases{
      livelock_fixture.components, callback_scheduler, callback_limits};
  const auto callback_domain = callback_phases.create_domain(
      "callbacks", SystemVerilogUvmDomainKind::Custom);
  callback_phases.participate(
      callback_domain, livelock_fixture.first_root);
  const auto callback_phase = callback_phases.create_custom_phase(
      callback_domain, "callbacks", SystemVerilogUvmPhaseExecutionKind::Task);
  (void)callback_phases.execute_task_phase(
      callback_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  for (StableOrder order{}; order < 3; ++order) {
    callback_scheduler.schedule_after(
        1, SchedulerPhase::active, order, [](Scheduler&) {});
  }
  require(
      rejects("FSIM-UVM-PHASE-008", [&] {
        (void)callback_phases.settle_task_phase(
            callback_phase, [](const auto, const auto, const auto) {});
      }),
      "quiescence callback work must stop at its configured ceiling");

  SystemVerilogUvmPhaseLimits reentry_limits;
  reentry_limits.maximum_ready_to_end_reentries = 2;
  Scheduler reentry_scheduler;
  SystemVerilogUvmPhaseService reentry_phases{
      livelock_fixture.components, reentry_scheduler, reentry_limits};
  const auto reentry_domain = reentry_phases.create_domain(
      "reentry", SystemVerilogUvmDomainKind::Custom);
  reentry_phases.participate(
      reentry_domain, livelock_fixture.first_root);
  const auto reentry_phase = reentry_phases.create_custom_phase(
      reentry_domain, "reentry", SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmObjectionService reentry_objections{
      livelock_fixture.objects,
      livelock_fixture.components,
      reentry_phases,
      reentry_scheduler};
  reentry_phases.set_objection_service(reentry_objections);
  const auto reentry_source = reentry_objections.bind_source(livelock_top);
  reentry_objections.set_drain_time(reentry_phase, reentry_source, 1);
  const auto reentry_execution = reentry_phases.execute_task_phase(
      reentry_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  for (const auto& process : reentry_execution.processes) {
    reentry_phases.complete_task_process(process);
  }
  require(
      rejects("FSIM-UVM-PHASE-008", [&] {
        (void)reentry_phases.settle_task_phase(
            reentry_phase,
            [&](const auto, const auto phase, const auto kind) {
              if (kind
                  == SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd) {
                (void)reentry_objections.raise(
                    phase, reentry_source, "repeat", 1);
                (void)reentry_objections.drop(
                    phase, reentry_source, "repeat", 1);
              }
            });
      }),
      "repeated ready-to-end objection races must diagnose at their ceiling");

  QuiescenceFixture independent_fixture;
  const auto independent_top = independent_fixture.make_component(
      "top", independent_fixture.first_root);
  Scheduler independent_scheduler;
  SystemVerilogUvmPhaseService independent_phases{
      independent_fixture.components, independent_scheduler};
  const auto independent_domain = independent_phases.create_domain(
      "independent", SystemVerilogUvmDomainKind::Custom);
  independent_phases.participate(
      independent_domain, independent_fixture.first_root);
  const auto independent_phase = independent_phases.create_custom_phase(
      independent_domain,
      "independent",
      SystemVerilogUvmPhaseExecutionKind::Task);
  SystemVerilogUvmObjectionService independent_objections{
      independent_fixture.objects,
      independent_fixture.components,
      independent_phases,
      independent_scheduler};
  independent_phases.set_objection_service(independent_objections);
  const auto independent_source = independent_objections.bind_source(
      independent_top);
  independent_objections.set_drain_time(
      independent_phase, independent_source, 2);
  const auto independent_execution = independent_phases.execute_task_phase(
      independent_phase,
      [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  for (const auto& process : independent_execution.processes) {
    independent_phases.complete_task_process(process);
  }
  (void)independent_objections.raise(
      independent_phase, independent_source, "independent", 1);
  (void)independent_objections.drop(
      independent_phase, independent_source, "independent", 1);
  const auto independent = independent_phases.settle_task_phase(
      independent_phase, [](const auto, const auto, const auto) {});
  require(
      independent.status == SystemVerilogUvmQuiescenceStatus::Completed
          && independent.time == 2 && independent_phases.process_count() == 0
          && independent_objections.phase_quiescent(independent_phase),
      "a concurrently alive simulation must settle independently without leaked state");
}

}  // namespace fsim::tests::runtime
