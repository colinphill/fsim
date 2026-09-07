// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::runtime {

inline constexpr std::uint32_t kMaxTfDelayRequests = 256;

struct TfTimeProfile {
  std::int8_t unit_exponent{-9};
  std::int8_t precision_exponent{-12};
  std::uint64_t tick_multiplier{1};

  [[nodiscard]] friend bool operator==(
      const TfTimeProfile&, const TfTimeProfile&) = default;
};

struct TfTimeState {
  std::uint64_t scheduler_ticks{};
  bool has_next_event{};
  std::uint64_t next_event_ticks{};
};

enum class TfTimeError {
  None,
  Unit,
  Precision,
  TickMultiplier,
  Overflow,
  Negative,
  NonFinite,
};

struct TfDelayRequest {
  std::uint64_t scheduler_ticks{};
};

[[nodiscard]] TfTimeError validate_tf_time_profile(
    const TfTimeProfile& profile) noexcept;

[[nodiscard]] TfTimeError tf_time_to_local_integer(
    const TfTimeProfile& profile, std::uint64_t scheduler_ticks,
    std::uint64_t& local_time) noexcept;

[[nodiscard]] TfTimeError tf_local_integer_to_ticks(
    const TfTimeProfile& profile, std::uint64_t local_time,
    std::uint64_t& scheduler_ticks) noexcept;

[[nodiscard]] TfTimeError tf_local_real_to_ticks(
    const TfTimeProfile& profile, double local_time,
    std::uint64_t& scheduler_ticks) noexcept;

}  // namespace fsim::runtime
