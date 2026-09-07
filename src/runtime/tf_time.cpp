// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/tf_time.hpp"

#include <cmath>
#include <limits>

namespace fsim::runtime {

namespace {

[[nodiscard]] std::uint64_t unit_scale(const TfTimeProfile& profile) noexcept {
  std::uint64_t scale{1};
  for (auto exponent = profile.precision_exponent;
       exponent < profile.unit_exponent; ++exponent) {
    scale *= 10U;
  }
  return scale;
}

[[nodiscard]] bool add_ratio(std::uint64_t& quotient,
                             std::uint64_t& remainder,
                             const std::uint64_t addend_quotient,
                             const std::uint64_t addend_remainder,
                             const std::uint64_t denominator) noexcept {
  if (quotient > std::numeric_limits<std::uint64_t>::max() -
                     addend_quotient) {
    return false;
  }
  quotient += addend_quotient;
  if (addend_remainder != 0 &&
      remainder >= denominator - addend_remainder) {
    remainder -= denominator - addend_remainder;
    if (quotient == std::numeric_limits<std::uint64_t>::max()) {
      return false;
    }
    ++quotient;
  } else {
    remainder += addend_remainder;
  }
  return true;
}

[[nodiscard]] bool multiply_divide_rounded(
    const std::uint64_t multiplicand, std::uint64_t multiplier,
    const std::uint64_t denominator, std::uint64_t& result) noexcept {
  std::uint64_t quotient{};
  std::uint64_t remainder{};
  std::uint64_t term_quotient = multiplicand / denominator;
  std::uint64_t term_remainder = multiplicand % denominator;
  while (multiplier != 0) {
    if ((multiplier & UINT64_C(1)) != 0 &&
        !add_ratio(quotient, remainder, term_quotient, term_remainder,
                   denominator)) {
      return false;
    }
    multiplier >>= 1U;
    if (multiplier == 0) {
      break;
    }
    const bool carry = term_remainder >= denominator - term_remainder;
    term_remainder = carry
        ? term_remainder - (denominator - term_remainder)
        : term_remainder + term_remainder;
    const auto carry_value = static_cast<std::uint64_t>(carry);
    if (term_quotient >
        (std::numeric_limits<std::uint64_t>::max() - carry_value) / 2U) {
      return false;
    }
    term_quotient = term_quotient * 2U + carry_value;
  }
  const bool round_up =
      remainder >= denominator / 2U + denominator % 2U;
  if (round_up && quotient == std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }
  result = quotient + static_cast<std::uint64_t>(round_up);
  return true;
}

}  // namespace

TfTimeError validate_tf_time_profile(const TfTimeProfile& profile) noexcept {
  if (profile.unit_exponent > 0 || profile.unit_exponent < -15) {
    return TfTimeError::Unit;
  }
  if (profile.precision_exponent > profile.unit_exponent ||
      profile.precision_exponent < -15) {
    return TfTimeError::Precision;
  }
  return profile.tick_multiplier == 0 ? TfTimeError::TickMultiplier
                                      : TfTimeError::None;
}

TfTimeError tf_time_to_local_integer(const TfTimeProfile& profile,
                                     const std::uint64_t scheduler_ticks,
                                     std::uint64_t& local_time) noexcept {
  if (const auto error = validate_tf_time_profile(profile);
      error != TfTimeError::None) {
    return error;
  }
  return multiply_divide_rounded(scheduler_ticks, profile.tick_multiplier,
                                 unit_scale(profile), local_time)
             ? TfTimeError::None
             : TfTimeError::Overflow;
}

TfTimeError tf_local_integer_to_ticks(const TfTimeProfile& profile,
                                      const std::uint64_t local_time,
                                      std::uint64_t& scheduler_ticks) noexcept {
  if (const auto error = validate_tf_time_profile(profile);
      error != TfTimeError::None) {
    return error;
  }
  const auto scale = unit_scale(profile);
  return multiply_divide_rounded(local_time, scale, profile.tick_multiplier,
                                 scheduler_ticks)
             ? TfTimeError::None
             : TfTimeError::Overflow;
}

TfTimeError tf_local_real_to_ticks(const TfTimeProfile& profile,
                                   const double local_time,
                                   std::uint64_t& scheduler_ticks) noexcept {
  if (const auto error = validate_tf_time_profile(profile);
      error != TfTimeError::None) {
    return error;
  }
  if (!std::isfinite(local_time)) {
    return TfTimeError::NonFinite;
  }
  if (local_time < 0.0) {
    return TfTimeError::Negative;
  }
  const auto scaled = static_cast<long double>(local_time) *
                      static_cast<long double>(unit_scale(profile)) /
                      static_cast<long double>(profile.tick_multiplier);
  const auto rounded = std::floor(scaled + 0.5L);
  if (!(rounded < std::ldexp(1.0L, 64))) {
    return TfTimeError::Overflow;
  }
  scheduler_ticks = static_cast<std::uint64_t>(rounded);
  return TfTimeError::None;
}

}  // namespace fsim::runtime
