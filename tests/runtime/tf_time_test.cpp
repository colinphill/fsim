// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_call.hpp"
#include "fsim/runtime/tf_time.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

PLI_BYTE8* expected_instance{};
bool callback_ok{true};
std::uint32_t callback_calls{};
bool no_next_ok{};

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL time_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_checktf) {
    expected_instance = tf_getinstance();
    callback_ok = callback_ok && tf_gettime() == 0 &&
                  tf_gettimeunit() == 0 && tf_setdelay(1) != 0;
    return 0;
  }
  ++callback_calls;
  PLI_INT32 high{};
  PLI_INT32 next_low{};
  PLI_INT32 next_high{};
  PLI_INT32 scaled_low{};
  PLI_INT32 scaled_high{};
  PLI_INT32 unscaled_low{};
  PLI_INT32 unscaled_high{};
  double scaled_real{};
  double unscaled_real{};
  auto* const instance = tf_getinstance();
  PLI_INT32 rejected_high{1};
  callback_ok = callback_ok && instance == expected_instance &&
                tf_gettime() == 2 && tf_getlongtime(&high) == 2 && high == 0 &&
                tf_getrealtime() == 1.5 && tf_gettimeunit() == -9 &&
                tf_gettimeprecision() == -12 &&
                tf_getnextlongtime(&next_low, &next_high) == 1 &&
                next_low == 4 && next_high == 0 &&
                std::string_view{tf_strgettime()} == "2" &&
                tf_igettime(instance) == 2 &&
                tf_igetrealtime(instance) == 1.5 &&
                tf_igettimeunit(instance) == -9 &&
                tf_igettimeprecision(instance) == -12 &&
                tf_igetlongtime(&rejected_high,
                                reinterpret_cast<PLI_BYTE8*>(1)) == 0 &&
                rejected_high == 0;
  tf_scale_longdelay(instance, 3, 0, &scaled_low, &scaled_high);
  tf_unscale_longdelay(instance, scaled_low, scaled_high,
                       &unscaled_low, &unscaled_high);
  tf_scale_realdelay(instance, 1.25, &scaled_real);
  tf_unscale_realdelay(instance, scaled_real, &unscaled_real);
  callback_ok = callback_ok && scaled_low == 1500 && scaled_high == 0 &&
                unscaled_low == 3 && unscaled_high == 0 &&
                scaled_real == 625.0 && unscaled_real == 1.25 &&
                tf_setdelay(3) == 0 && tf_clearalldelays() == 0 &&
                tf_isetlongdelay(4, 0, instance) == 0 &&
                tf_isetrealdelay(1.25, instance) == 0 &&
                tf_setdelay(-1) != 0 &&
                tf_isetdelay(1, reinterpret_cast<PLI_BYTE8*>(1)) != 0;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL no_next_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  PLI_INT32 low{1};
  PLI_INT32 high{1};
  no_next_ok = tf_getnextlongtime(&low, &high) == 0 && low == 0 && high == 0;
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL throwing_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason == reason_calltf) {
    (void)tf_setdelay(1);
    throw std::runtime_error{"contained TF time callback"};
  }
  return 0;
}

PLI_INT32 FSIM_NATIVE_PLUGIN_CALL capacity_callback(
    const PLI_INT32, const PLI_INT32 reason) {
  if (reason != reason_calltf) return 0;
  for (std::uint32_t index = 0; index < fsim::runtime::kMaxTfDelayRequests;
       ++index) {
    callback_ok = callback_ok && tf_setdelay(0) == 0;
  }
  callback_ok = callback_ok && tf_setdelay(0) != 0;
  return 0;
}

void require(const bool condition, const char* const message) {
  if (!condition) throw std::runtime_error{message};
}

fsim::runtime::TfRegistration registration(
    const char* const name, const fsim_tf_routine_v3 callback,
    const fsim_tf_routine_v3 check = nullptr) {
  return {.kind = fsim::runtime::TfRegistrationKind::Task,
          .user_data = 0,
          .checktf = check,
          .sizetf = nullptr,
          .calltf = callback,
          .misctf = nullptr,
          .name = name};
}

}  // namespace

int main() {
  using fsim::runtime::TfCallError;
  using fsim::runtime::TfTimeError;
  using fsim::runtime::TfTimeProfile;
  using fsim::runtime::TfTimeState;
  using fsim::runtime::bind_tf_call;
  using fsim::runtime::tf_local_integer_to_ticks;
  using fsim::runtime::tf_local_real_to_ticks;
  using fsim::runtime::tf_time_to_local_integer;
  using fsim::runtime::validate_tf_time_profile;

  require(tf_gettime() == 0 && tf_getrealtime() == 0.0 &&
              tf_setdelay(1) != 0 && tf_strgettime() == nullptr,
          "TF time services are neutral outside a callback");
  const TfTimeProfile profile{
      .unit_exponent = -9, .precision_exponent = -12, .tick_multiplier = 2};
  require(validate_tf_time_profile(profile) == TfTimeError::None &&
              validate_tf_time_profile({.unit_exponent = 1,
                                        .precision_exponent = -12,
                                        .tick_multiplier = 1}) ==
                  TfTimeError::Unit &&
              validate_tf_time_profile({.unit_exponent = -12,
                                        .precision_exponent = -9,
                                        .tick_multiplier = 1}) ==
                  TfTimeError::Precision &&
              validate_tf_time_profile({.unit_exponent = -9,
                                        .precision_exponent = -12,
                                        .tick_multiplier = 0}) ==
                  TfTimeError::TickMultiplier,
          "TF timescale profile bounds match the common scheduler model");
  std::uint64_t converted{};
  require(tf_time_to_local_integer(profile, 750, converted) ==
                  TfTimeError::None &&
              converted == 2 &&
              tf_local_integer_to_ticks(profile, 3, converted) ==
                  TfTimeError::None &&
              converted == 1500 &&
              tf_local_real_to_ticks(profile, 1.25, converted) ==
                  TfTimeError::None &&
              converted == 625 &&
              tf_local_real_to_ticks(profile, -1.0, converted) ==
                  TfTimeError::Negative &&
              tf_local_real_to_ticks(
                  profile, std::numeric_limits<double>::infinity(), converted) ==
                  TfTimeError::NonFinite &&
              tf_time_to_local_integer(
                  profile, std::numeric_limits<std::uint64_t>::max(),
                  converted) == TfTimeError::None &&
              converted == UINT64_C(36893488147419103) &&
              tf_local_integer_to_ticks(
                  profile, std::numeric_limits<std::uint64_t>::max(),
                  converted) == TfTimeError::Overflow &&
              tf_local_integer_to_ticks(
                  {.unit_exponent = -9,
                   .precision_exponent = -12,
                   .tick_multiplier =
                       std::numeric_limits<std::uint64_t>::max()},
                  std::numeric_limits<std::uint64_t>::max(), converted) ==
                  TfTimeError::None &&
              converted == 1000 &&
              tf_time_to_local_integer(
                  {.unit_exponent = -9,
                   .precision_exponent = -9,
                   .tick_multiplier =
                       std::numeric_limits<std::uint64_t>::max()},
                  2, converted) == TfTimeError::Overflow,
          "TF integer and real conversions round deterministically");

  auto bound = bind_tf_call(registration("$time", time_callback, time_callback),
                            {}, {}, profile);
  require(static_cast<bool>(bound) && bound.value->time_profile() == profile,
          "TF call binds one validated time profile");
  const auto result = bound.value->invoke(TfTimeState{
      .scheduler_ticks = 750,
      .has_next_event = true,
      .next_event_ticks = 2000});
  require(result && callback_ok && callback_calls == 1 &&
              result.delay_requests.size() == 2 &&
              result.delay_requests[0].scheduler_ticks == 2000 &&
              result.delay_requests[1].scheduler_ticks == 625,
          "TF queries and reactivation delays publish atomically");

  auto capacity = bind_tf_call(registration("$capacity", capacity_callback));
  const auto capacity_result = capacity.value->invoke();
  require(capacity_result && callback_ok &&
              capacity_result.delay_requests.size() ==
                  fsim::runtime::kMaxTfDelayRequests,
          "TF delay requests enforce their fixed per-call ceiling");
  auto no_next = bind_tf_call(registration("$no_next", no_next_callback));
  require(static_cast<bool>(no_next) && no_next.value->invoke() && no_next_ok,
          "TF next-event queries distinguish an empty scheduler queue");
  auto throwing = bind_tf_call(registration("$throwing", throwing_callback));
  const auto thrown = throwing.value->invoke();
  require(thrown.error == TfCallError::CallbackException &&
              thrown.delay_requests.empty(),
          "TF callback failure publishes no scheduled delay residue");
  const auto invalid = bind_tf_call(
      registration("$bad_time", time_callback), {}, {},
      {.unit_exponent = -9, .precision_exponent = -12, .tick_multiplier = 0});
  require(!invalid && invalid.error == TfCallError::InvalidTime &&
              callback_calls == 1,
          "invalid timescale prevents callback entry");
  return 0;
}
