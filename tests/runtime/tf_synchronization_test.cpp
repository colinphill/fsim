// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace {

PLI_BYTE8* text(const char* const value) {
  return const_cast<PLI_BYTE8*>(value);
}

bool callback_ok{true};
std::array<bool, 7> read_write_checks{};
PLI_BYTE8* expected_instance{};
fsim::runtime::TfBoundCall* reentrant_call{};
fsim::runtime::TfCallError nested_error{fsim::runtime::TfCallError::None};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL synchronization_call(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_checktf) {
    callback_ok = callback_ok && tf_synchronize() != 0 &&
                  tf_rosynchronize() != 0;
    return 0;
  }
  if (reason == reason_calltf) {
    expected_instance = tf_getinstance();
    callback_ok = callback_ok && expected_instance != nullptr &&
                  tf_synchronize() == 0 && tf_rosynchronize() == 0 &&
                  tf_isynchronize(expected_instance) == 0 &&
                  tf_irosynchronize(expected_instance) == 0 &&
                  tf_isynchronize(reinterpret_cast<PLI_BYTE8*>(1)) != 0 &&
                  tf_irosynchronize(reinterpret_cast<PLI_BYTE8*>(1)) != 0;
    return 17;
  }
  callback_ok = false;
  return 1;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL synchronization_misc(
    const PLI_INT32, const PLI_INT32 reason, const PLI_INT32 parameter) {
  callback_ok = callback_ok && parameter == 0;
  if (reason == reason_synch) {
    read_write_checks[0] = tf_getinstance() == expected_instance;
    read_write_checks[1] = tf_getp(1) == 5;
    read_write_checks[2] = tf_gettime() == 9;
    read_write_checks[3] = tf_putp(1, 9) == 0;
    read_write_checks[4] = tf_setdelay(2) == 0;
    read_write_checks[5] = tf_rosynchronize() == 0;
    read_write_checks[6] = tf_warning(text("read-write")) == 0;
    return 41;
  }
  if (reason == reason_rosynch) {
    callback_ok = callback_ok && tf_getinstance() == expected_instance &&
                  tf_getp(1) == 9 && tf_gettime() == 12 &&
                  tf_putp(1, 3) != 0 && tf_setdelay(1) != 0 &&
                  tf_synchronize() != 0 && tf_rosynchronize() != 0 &&
                  tf_text(text("read-only")) == 0;
    return 42;
  }
  callback_ok = false;
  return 1;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL no_op_call(
    const PLI_INT32, const PLI_INT32) {
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throwing_misc(
    const PLI_INT32, const PLI_INT32 reason, const PLI_INT32) {
  if (reason == reason_synch) {
    (void)tf_putp(1, 7);
    (void)tf_setdelay(1);
    (void)tf_rosynchronize();
    (void)tf_error(text("discarded"));
    throw std::runtime_error{"contained synchronization callback"};
  }
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL capacity_call(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  for (std::uint32_t index = 0;
       index < fsim::runtime::kMaxTfSynchronizationRequests; ++index) {
    callback_ok = callback_ok && tf_synchronize() == 0;
  }
  callback_ok = callback_ok && tf_synchronize() != 0;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL reentrant_misc(
    const PLI_INT32, const PLI_INT32 reason, const PLI_INT32) {
  if (reason == reason_synch && reentrant_call != nullptr) {
    nested_error =
        reentrant_call
            ->synchronize(fsim::runtime::TfSynchronizationKind::ReadOnly)
            .error;
  }
  return 0;
}

fsim::runtime::TfRegistration registration(
    const char* const name, const fsim_tf_routine_v3 call,
    const fsim_tf_misc_routine_v3 misc,
    const fsim_tf_routine_v3 check = nullptr) {
  return {.kind = fsim::runtime::TfRegistrationKind::Task,
          .user_data = 0,
          .checktf = check,
          .sizetf = nullptr,
          .calltf = call,
          .misctf = misc,
          .name = name};
}

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

}  // namespace

int main() {
  using fsim::runtime::TfArgument;
  using fsim::runtime::TfArgumentKind;
  using fsim::runtime::TfArgumentValue;
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfControlEffectKind;
  using fsim::runtime::TfInstanceIdentity;
  using fsim::runtime::TfSynchronizationKind;
  using fsim::runtime::TfTimeProfile;
  using fsim::runtime::TfTimeState;
  using fsim::runtime::TfValueKind;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::tf_synchronization_reason;
  using fsim::runtime::valid_tf_synchronization_kind;

  require(valid_tf_synchronization_kind(TfSynchronizationKind::ReadWrite) &&
              valid_tf_synchronization_kind(
                  TfSynchronizationKind::ReadOnly) &&
              !valid_tf_synchronization_kind(
                  static_cast<TfSynchronizationKind>(99)) &&
              tf_synchronization_reason(TfSynchronizationKind::ReadWrite) ==
                  reason_synch &&
              tf_synchronization_reason(TfSynchronizationKind::ReadOnly) ==
                  reason_rosynch &&
              tf_synchronization_reason(
                  static_cast<TfSynchronizationKind>(99)) == 0,
          "synchronization kinds map exactly onto standardized misctf reasons");

  require(tf_synchronize() != 0 && tf_rosynchronize() != 0 &&
              tf_isynchronize(reinterpret_cast<PLI_BYTE8*>(1)) != 0 &&
              tf_irosynchronize(reinterpret_cast<PLI_BYTE8*>(1)) != 0,
          "synchronization requests are neutral outside callbacks");

  const std::array arguments{TfArgument{.kind = TfArgumentKind::ReadWrite,
                                        .width = 8,
                                        .is_signed = false,
                                        .lhs_select = -1,
                                        .rhs_select = -1,
                                        .expression = "state"}};
  const std::array values{TfArgumentValue{
      .kind = TfValueKind::Integral,
      .vector_words = {{.avalbits = 5, .bvalbits = 0}},
      .real = 0.0,
      .string = {}}};
  const TfInstanceIdentity instance{
      .design_id = 7, .hierarchy_id = 11, .generation = 3};
  const TfTimeProfile time_profile{
      .unit_exponent = -12, .precision_exponent = -12, .tick_multiplier = 1};
  auto bound = bind_tf_call(
      registration("$synchronization", synchronization_call,
                   synchronization_misc, synchronization_call),
      arguments, instance, time_profile);
  require(static_cast<bool>(bound) && callback_ok,
          "synchronization-capable TF instance binds transactionally");

  const auto invoked = bound.value->invoke(values);
  require(invoked && invoked.callback_value == 17 && callback_ok &&
              invoked.synchronization_requests.size() == 4,
          "calltf captures current and explicit synchronization requests");
  const std::array expected_kinds{
      TfSynchronizationKind::ReadWrite, TfSynchronizationKind::ReadOnly,
      TfSynchronizationKind::ReadWrite, TfSynchronizationKind::ReadOnly};
  for (std::size_t index = 0; index < expected_kinds.size(); ++index) {
    require(invoked.synchronization_requests[index].kind ==
                    expected_kinds[index] &&
                invoked.synchronization_requests[index].instance == instance,
            "synchronization requests preserve order and instance identity");
  }

  const auto read_write = bound.value->synchronize(
      TfSynchronizationKind::ReadWrite, values,
      TfTimeState{.scheduler_ticks = 9});
  require(static_cast<bool>(read_write),
          "read-write synchronization callback returns success");
  require(read_write.callback_value == 41,
          "read-write synchronization preserves callback return value");
  require(read_write_checks[0],
          "read-write synchronization preserves instance identity");
  require(read_write_checks[1],
          "read-write synchronization reads current argument values");
  require(read_write_checks[2],
          "read-write synchronization reads current simulation time");
  require(read_write_checks[3],
          "read-write synchronization permits argument updates");
  require(read_write_checks[4],
          "read-write synchronization permits delay requests");
  require(read_write_checks[5],
          "read-write synchronization permits follow-up requests");
  require(read_write_checks[6],
          "read-write synchronization permits output effects");
  require(read_write.argument_updates.size() == 1 &&
              read_write.argument_updates[0].parameter == 1 &&
              read_write.argument_updates[0].value.vector_words[0].avalbits ==
                  9,
          "read-write synchronization stages argument updates");
  require(read_write.delay_requests.size() == 1 &&
              read_write.delay_requests[0].scheduler_ticks == 2 &&
              read_write.synchronization_requests.size() == 1,
          "read-write synchronization stages scheduler requests");
  require(read_write.synchronization_requests[0].kind ==
                  TfSynchronizationKind::ReadOnly &&
              read_write.synchronization_requests[0].instance == instance &&
              read_write.control_effects.size() == 1 &&
              read_write.control_effects[0].kind ==
                  TfControlEffectKind::Warning,
          "read-write synchronization preserves follow-up and output effects");

  auto read_only_values = values;
  read_only_values[0].vector_words[0].avalbits = 9;
  const auto read_only = bound.value->synchronize(
      TfSynchronizationKind::ReadOnly, read_only_values,
      TfTimeState{.scheduler_ticks = 12});
  require(read_only && read_only.callback_value == 42 && callback_ok &&
              read_only.argument_updates.empty() &&
              read_only.delay_requests.empty() &&
              read_only.synchronization_requests.empty() &&
              read_only.control_effects.size() == 1 &&
              read_only.control_effects[0].kind ==
                  TfControlEffectKind::Output,
          "read-only synchronization observes state but rejects every mutation");

  auto missing =
      bind_tf_call(registration("$missing", no_op_call, nullptr), arguments);
  require(missing.value
                  ->synchronize(TfSynchronizationKind::ReadWrite, values)
                  .error == TfCallError::MissingMiscCallback &&
              missing.value
                      ->synchronize(
                          static_cast<TfSynchronizationKind>(99), values)
                      .error == TfCallError::InvalidSynchronization,
          "synchronization dispatch rejects missing callbacks and invalid kinds");

  auto throwing = bind_tf_call(
      registration("$throwing", no_op_call, throwing_misc), arguments);
  const auto thrown = throwing.value->synchronize(
      TfSynchronizationKind::ReadWrite, values);
  require(thrown.error == TfCallError::CallbackException &&
              thrown.argument_updates.empty() &&
              thrown.delay_requests.empty() &&
              thrown.synchronization_requests.empty() &&
              thrown.control_effects.empty(),
          "synchronization exceptions discard all callback effects");

  auto capacity = bind_tf_call(
      registration("$capacity", capacity_call, synchronization_misc));
  const auto capacity_result = capacity.value->invoke();
  require(capacity_result && callback_ok &&
              capacity_result.synchronization_requests.size() ==
                  fsim::runtime::kMaxTfSynchronizationRequests,
          "synchronization requests enforce their fixed per-callback ceiling");

  auto reentrant =
      bind_tf_call(registration("$reentrant", no_op_call, reentrant_misc));
  reentrant_call = reentrant.value.get();
  const auto outer =
      reentrant_call->synchronize(TfSynchronizationKind::ReadWrite);
  reentrant_call = nullptr;
  require(outer && nested_error == TfCallError::ContextBusy,
          "synchronization callbacks cannot replace an active TF context");
  return 0;
}
