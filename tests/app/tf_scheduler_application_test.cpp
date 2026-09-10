// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application_tf_scheduler.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

struct ObservedPublication {
  fsim::app::TfSchedulerCallbackKind callback{};
  fsim::runtime::SchedulerPhase phase{};
  fsim::runtime::SimulationTick time{};
  std::uint32_t updates{};
  std::uint32_t controls{};
  std::uint32_t delays{};
  std::uint32_t synchronizations{};
};

std::uint32_t reactivations{};
bool callback_ok{true};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL coordinated_call(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  callback_ok = callback_ok && tf_getp(1) == 0 &&
                tf_putp(1, 1) == 0 && tf_setdelay(3) == 0 &&
                tf_synchronize() == 0 && tf_rosynchronize() == 0 &&
                tf_warning(const_cast<PLI_BYTE8*>("call")) == 0;
  return 11;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL coordinated_misc(
    const PLI_INT32, const PLI_INT32 reason, const PLI_INT32 parameter) {
  callback_ok = callback_ok && parameter == 0;
  if (reason == reason_synch) {
    callback_ok = callback_ok && tf_getp(1) == 1 &&
                  tf_putp(1, 2) == 0 && tf_setdelay(2) == 0 &&
                  tf_rosynchronize() == 0 &&
                  tf_message(1, const_cast<PLI_BYTE8*>("TF"),
                             const_cast<PLI_BYTE8*>("17"),
                             const_cast<PLI_BYTE8*>("read-write")) == 0;
    return 12;
  }
  if (reason == reason_rosynch) {
    callback_ok = callback_ok && tf_getp(1) == 2 &&
                  tf_putp(1, 9) != 0 && tf_setdelay(1) != 0;
    return 13;
  }
  if (reason == reason_reactivate) {
    const auto value = tf_getp(1);
    callback_ok = callback_ok && (value == 2 || value == 3) &&
                  tf_putp(1, value + 1) == 0;
    ++reactivations;
    return 14;
  }
  callback_ok = false;
  return 1;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL finish_call(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_calltf ? tf_dofinish() : 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL function_size(
    const PLI_INT32, const PLI_INT32) {
  return 17;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL function_call(
    const PLI_INT32, const PLI_INT32 reason) {
  return reason == reason_calltf ? tf_putp(0, 23) : 0;
}

fsim::runtime::TfRegistration task_registration(
    const char* name, const fsim_tf_routine_v3 call,
    const fsim_tf_misc_routine_v3 misc = nullptr) {
  return {.kind = fsim::runtime::TfRegistrationKind::Task,
          .user_data = 0,
          .checktf = nullptr,
          .sizetf = nullptr,
          .calltf = call,
          .misctf = misc,
          .name = name};
}

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error{message};
}

fsim::runtime::TfInstanceIdentity instance() {
  return {.design_id = 7, .hierarchy_id = 11, .generation = 3};
}

fsim::runtime::TfTimeProfile time_profile() {
  return {.unit_exponent = -12,
          .precision_exponent = -12,
          .tick_multiplier = 1};
}

}  // namespace

int main() {
  using fsim::app::TfSchedulerCallbackKind;
  using fsim::app::TfSchedulerCoordinator;
  using fsim::app::TfSchedulerError;
  using fsim::runtime::Scheduler;
  using fsim::runtime::SchedulerPhase;
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfRegistration;
  using fsim::runtime::TfRegistrationKind;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::bind_tf_call;

  const std::array arguments{TfArgument{
      .kind = TfArgumentKind::ReadWrite,
      .width = 8,
      .is_signed = false,
      .lhs_select = -1,
      .rhs_select = -1,
      .expression = "state"}};
  const std::array initial_values{TfArgumentValue{
      .kind = TfValueKind::Integral,
      .vector_words = {{.avalbits = 0, .bvalbits = 0}},
      .real = 0.0,
      .string = {}}};

  Scheduler scheduler;
  std::vector<ObservedPublication> observed;
  TfSchedulerCoordinator coordinator{
      scheduler, 500,
      [&](const fsim::app::TfSchedulerPublication& publication) {
        observed.push_back({
            .callback = publication.callback,
            .phase = publication.phase,
            .time = publication.time,
            .updates = static_cast<std::uint32_t>(
                publication.argument_updates.size()),
            .controls = static_cast<std::uint32_t>(
                publication.control_effects.size()),
            .delays = static_cast<std::uint32_t>(
                publication.delay_requests.size()),
            .synchronizations = static_cast<std::uint32_t>(
                publication.synchronization_requests.size()),
        });
        return true;
      }};
  auto bound = bind_tf_call(
      task_registration("$coordinated", coordinated_call,
                        coordinated_misc),
      arguments, instance(), time_profile());
  const auto adopted = coordinator.adopt(std::move(bound));
  require(adopted && coordinator.call_count() == 1,
          "coordinator adopts one validated HDL call site");
  require(coordinator.invoke(adopted.value, initial_values).error ==
              TfSchedulerError::InactiveScheduler,
          "TF invocation cannot bypass scheduler execution");

  fsim::app::TfSchedulerInvokeResult initial;
  scheduler.schedule(SchedulerPhase::active, 10,
                     [&](Scheduler&) {
                       initial = coordinator.invoke(
                           adopted.value, initial_values);
                     });
  const auto completed = scheduler.run();
  require(initial && initial.callback_value == 11 &&
              initial.argument_updates == 1 &&
              initial.control_effects == 1 &&
              initial.callbacks_scheduled == 3 && callback_ok,
          "calltf effects publish as one coordinator transaction");
  require(completed.status == fsim::runtime::RunStatus::completed &&
              completed.time == 3 && reactivations == 2,
          "synchronization and reactivation callbacks drain deterministically");
  require(observed.size() == 6 &&
              observed[0].callback == TfSchedulerCallbackKind::Call &&
              observed[0].phase == SchedulerPhase::active &&
              observed[0].time == 0 && observed[0].updates == 1 &&
              observed[0].controls == 1 && observed[0].delays == 1 &&
              observed[0].synchronizations == 2,
          "initial call preserves its complete effect inventory");
  require(observed[1].callback ==
                  TfSchedulerCallbackKind::ReadWriteSynchronize &&
              observed[1].phase == SchedulerPhase::reactive &&
              observed[1].time == 0 && observed[1].updates == 1 &&
              observed[1].controls == 1 && observed[1].delays == 1 &&
              observed[1].synchronizations == 1,
          "read-write synchronization executes in the reactive region");
  require(observed[2].callback ==
                  TfSchedulerCallbackKind::ReadOnlySynchronize &&
              observed[3].callback ==
                  TfSchedulerCallbackKind::ReadOnlySynchronize &&
              observed[2].phase == SchedulerPhase::postponed &&
              observed[3].phase == SchedulerPhase::postponed &&
              observed[2].time == 0 && observed[3].time == 0 &&
              observed[2].updates == 0 && observed[3].updates == 0,
          "read-only callbacks observe committed state in postponed order");
  require(observed[4].callback == TfSchedulerCallbackKind::Reactivate &&
              observed[5].callback == TfSchedulerCallbackKind::Reactivate &&
              observed[4].phase == SchedulerPhase::active &&
              observed[5].phase == SchedulerPhase::active &&
              observed[4].time == 2 && observed[5].time == 3,
          "delay requests reactivate through active scheduler events");
  const auto snapshot = coordinator.snapshot(adopted.value);
  require(snapshot && snapshot.invocations == 6 &&
              snapshot.callback_failures == 0 &&
              snapshot.pending_callbacks == 0 &&
              snapshot.values.size() == 1 &&
              snapshot.values[0].vector_words[0].avalbits == 4 &&
              coordinator.pending_callbacks() == 0,
          "coordinator commits values only after all callback publications");

  Scheduler function_scheduler;
  TfSchedulerCoordinator function_coordinator{function_scheduler};
  const TfRegistration function_registration{
      .kind = TfRegistrationKind::Function,
      .user_data = 0,
      .checktf = nullptr,
      .sizetf = function_size,
      .calltf = function_call,
      .misctf = nullptr,
      .name = "$coordinated_function"};
  const auto function = function_coordinator.adopt(bind_tf_call(
      function_registration, {}, instance(), time_profile()));
  require(static_cast<bool>(function),
          "coordinator adopts a sized system function");
  fsim::app::TfSchedulerInvokeResult function_result;
  function_scheduler.schedule(SchedulerPhase::active, 1,
      [&](Scheduler&) {
        function_result = function_coordinator.invoke(function.value);
      });
  (void)function_scheduler.run();
  require(function_result && function_result.function_result.has_value() &&
              function_result.function_result->width == 17 &&
              function_result.function_result->aval_words[0] == 23,
          "scheduler-coordinated functions return their typed value");

  Scheduler finish_scheduler;
  TfSchedulerCoordinator finish_coordinator{finish_scheduler};
  const auto finish = finish_coordinator.adopt(bind_tf_call(
      task_registration("$coordinated_finish", finish_call), {},
      instance(), time_profile()));
  finish_scheduler.schedule(SchedulerPhase::active, 1,
      [&](Scheduler&) {
        require(static_cast<bool>(finish_coordinator.invoke(finish.value)),
                "terminal control call publishes successfully");
      });
  const auto stopped = finish_scheduler.run();
  const auto finish_snapshot = finish_coordinator.snapshot(finish.value);
  require(stopped.status == fsim::runtime::RunStatus::stopped &&
              finish_snapshot.terminal_effect ==
                  fsim::runtime::TfControlEffectKind::Finish,
          "finish effects stop the scheduler only after publication");

  Scheduler rejecting_scheduler;
  TfSchedulerCoordinator rejecting_coordinator{
      rejecting_scheduler, 0,
      [](const fsim::app::TfSchedulerPublication&) { return false; }};
  const auto rejected = rejecting_coordinator.adopt(bind_tf_call(
      task_registration("$rejected", coordinated_call, coordinated_misc),
      arguments, instance(), time_profile()));
  fsim::app::TfSchedulerInvokeResult rejected_result;
  rejecting_scheduler.schedule(SchedulerPhase::active, 1,
      [&](Scheduler&) {
        rejected_result = rejecting_coordinator.invoke(
            rejected.value, initial_values);
      });
  (void)rejecting_scheduler.run();
  const auto rejected_snapshot =
      rejecting_coordinator.snapshot(rejected.value);
  require(rejected_result.error == TfSchedulerError::Publication &&
              rejected_snapshot.invocations == 0 &&
              rejected_snapshot.values.empty() &&
              rejecting_coordinator.pending_callbacks() == 0,
          "rejected publication rolls back values and queued callbacks");

  fsim::app::TfApplicationRegistry registry;
  require(static_cast<bool>(registry.load(FSIM_TF_LINK_PROBE_PLUGIN_PATH)),
          "application loads the independently authored C TF plug-in");
  Scheduler plugin_scheduler;
  TfSchedulerCoordinator plugin_coordinator{plugin_scheduler};
  const auto plugin_call = plugin_coordinator.bind(
      registry, fsim::frontend::StandardRevision::SystemVerilog2023,
      "$fsim_tf_link_probe", {}, instance(), time_profile());
  require(static_cast<bool>(plugin_call),
          "an exact-2023 TF task lowers into one coordinated call handle");
  fsim::app::TfSchedulerInvokeResult plugin_result;
  plugin_scheduler.schedule(SchedulerPhase::active, 1,
      [&](Scheduler&) {
        plugin_result = plugin_coordinator.invoke(plugin_call.value);
      });
  (void)plugin_scheduler.run();
  require(plugin_result && plugin_result.callback_value == 0,
          "loaded C task executes only through the scheduler coordinator");
  return 0;
}
