// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace fsim::frontend {

enum class SystemVerilogScalarKind {
  None,
  ShortReal,
  Real,
  Realtime,
  Time,
  Chandle,
};

// shortreal, real and realtime (IEEE 1800-2017 6.12).
[[nodiscard]] constexpr bool is_real_scalar_kind(
    const SystemVerilogScalarKind kind) noexcept {
  return kind == SystemVerilogScalarKind::ShortReal
      || kind == SystemVerilogScalarKind::Real
      || kind == SystemVerilogScalarKind::Realtime;
}

enum class SystemVerilogDecimalLiteralKind {
  Real,
  Time,
};

// An exact source decimal before semantic conversion to IEEE-754 or project
// ticks. Its value is `digits * 10^decimal_exponent`; digits has no separators,
// sign, decimal point, exponent, or redundant leading/trailing zeroes. Time
// literals additionally retain their source unit.
struct SystemVerilogDecimalLiteral {
  SystemVerilogDecimalLiteralKind kind{
      SystemVerilogDecimalLiteralKind::Real};
  std::string digits;
  std::int64_t decimal_exponent{};
  std::string time_unit;
};

}  // namespace fsim::frontend
