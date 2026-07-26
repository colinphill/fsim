// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace fsim::runtime {

/// The four states used by Verilog and SystemVerilog scalar values.
enum class Logic4 : std::uint8_t {
  zero = 0,
  one = 1,
  x = 2,
  z = 3,
};

/// The nine states defined by IEEE std_logic_1164.
enum class Logic9 : std::uint8_t {
  u = 0,
  x = 1,
  zero = 2,
  one = 3,
  z = 4,
  w = 5,
  l = 6,
  h = 7,
  dont_care = 8,
};

[[nodiscard]] constexpr char to_char(Logic4 value) noexcept {
  switch (value) {
  case Logic4::zero:
    return '0';
  case Logic4::one:
    return '1';
  case Logic4::x:
    return 'X';
  case Logic4::z:
    return 'Z';
  }
  return 'X';
}

[[nodiscard]] constexpr char to_char(Logic9 value) noexcept {
  constexpr char states[] = {'U', 'X', '0', '1', 'Z', 'W', 'L', 'H', '-'};
  const auto index = static_cast<std::uint8_t>(value);
  return index < sizeof(states) ? states[index] : 'X';
}

[[nodiscard]] constexpr std::optional<Logic4>
parse_logic4(char value) noexcept {
  switch (value) {
  case '0':
    return Logic4::zero;
  case '1':
    return Logic4::one;
  case 'x':
  case 'X':
    return Logic4::x;
  case 'z':
  case 'Z':
    return Logic4::z;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] constexpr std::optional<Logic9>
parse_logic9(char value) noexcept {
  switch (value) {
  case 'u':
  case 'U':
    return Logic9::u;
  case 'x':
  case 'X':
    return Logic9::x;
  case '0':
    return Logic9::zero;
  case '1':
    return Logic9::one;
  case 'z':
  case 'Z':
    return Logic9::z;
  case 'w':
  case 'W':
    return Logic9::w;
  case 'l':
  case 'L':
    return Logic9::l;
  case 'h':
  case 'H':
    return Logic9::h;
  case '-':
    return Logic9::dont_care;
  default:
    return std::nullopt;
  }
}

/// Collapse a VHDL std_logic value at a mixed-language boundary.
[[nodiscard]] constexpr Logic4 to_logic4(Logic9 value) noexcept {
  switch (value) {
  case Logic9::zero:
  case Logic9::l:
    return Logic4::zero;
  case Logic9::one:
  case Logic9::h:
    return Logic4::one;
  case Logic9::z:
    return Logic4::z;
  case Logic9::u:
  case Logic9::x:
  case Logic9::w:
  case Logic9::dont_care:
    return Logic4::x;
  }
  return Logic4::x;
}

[[nodiscard]] constexpr Logic9 to_logic9(Logic4 value) noexcept {
  switch (value) {
  case Logic4::zero:
    return Logic9::zero;
  case Logic4::one:
    return Logic9::one;
  case Logic4::x:
    return Logic9::x;
  case Logic4::z:
    return Logic9::z;
  }
  return Logic9::x;
}

[[nodiscard]] constexpr Logic4 logic_not(Logic4 value) noexcept {
  switch (value) {
  case Logic4::zero:
    return Logic4::one;
  case Logic4::one:
    return Logic4::zero;
  case Logic4::x:
  case Logic4::z:
    return Logic4::x;
  }
  return Logic4::x;
}

[[nodiscard]] constexpr Logic4 logic_and(Logic4 lhs, Logic4 rhs) noexcept {
  if (lhs == Logic4::zero || rhs == Logic4::zero) {
    return Logic4::zero;
  }
  if (lhs == Logic4::one && rhs == Logic4::one) {
    return Logic4::one;
  }
  return Logic4::x;
}

[[nodiscard]] constexpr Logic4 logic_or(Logic4 lhs, Logic4 rhs) noexcept {
  if (lhs == Logic4::one || rhs == Logic4::one) {
    return Logic4::one;
  }
  if (lhs == Logic4::zero && rhs == Logic4::zero) {
    return Logic4::zero;
  }
  return Logic4::x;
}

[[nodiscard]] constexpr Logic4 logic_xor(Logic4 lhs, Logic4 rhs) noexcept {
  const bool lhs_known = lhs == Logic4::zero || lhs == Logic4::one;
  const bool rhs_known = rhs == Logic4::zero || rhs == Logic4::one;
  if (!lhs_known || !rhs_known) {
    return Logic4::x;
  }
  return lhs == rhs ? Logic4::zero : Logic4::one;
}

/// Resolve two four-state wire drivers. Z is the identity element.
[[nodiscard]] constexpr Logic4 resolve(Logic4 lhs, Logic4 rhs) noexcept {
  if (lhs == Logic4::z) {
    return rhs;
  }
  if (rhs == Logic4::z) {
    return lhs;
  }
  if (lhs == rhs) {
    return lhs;
  }
  return Logic4::x;
}

/// IEEE std_logic_1164 resolution for a pair of drivers.
[[nodiscard]] constexpr Logic9 resolve(Logic9 lhs, Logic9 rhs) noexcept {
  constexpr Logic9 table[9][9] = {
      {Logic9::u, Logic9::u, Logic9::u, Logic9::u, Logic9::u, Logic9::u,
       Logic9::u, Logic9::u, Logic9::u},
      {Logic9::u, Logic9::x, Logic9::x, Logic9::x, Logic9::x, Logic9::x,
       Logic9::x, Logic9::x, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::zero, Logic9::x, Logic9::zero,
       Logic9::zero, Logic9::zero, Logic9::zero, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::x, Logic9::one, Logic9::one,
       Logic9::one, Logic9::one, Logic9::one, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
       Logic9::w, Logic9::l, Logic9::h, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::w,
       Logic9::w, Logic9::w, Logic9::w, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::l,
       Logic9::w, Logic9::l, Logic9::w, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::h,
       Logic9::w, Logic9::w, Logic9::h, Logic9::x},
      {Logic9::u, Logic9::x, Logic9::x, Logic9::x, Logic9::x, Logic9::x,
       Logic9::x, Logic9::x, Logic9::x},
  };
  const auto left = static_cast<std::uint8_t>(lhs);
  const auto right = static_cast<std::uint8_t>(rhs);
  return left < 9 && right < 9 ? table[left][right] : Logic9::x;
}

[[nodiscard]] constexpr Logic4 resolve(std::span<const Logic4> drivers) noexcept {
  auto result = Logic4::z;
  for (const auto driver : drivers) {
    result = resolve(result, driver);
  }
  return result;
}

[[nodiscard]] constexpr Logic9 resolve(std::span<const Logic9> drivers) noexcept {
  // std_logic_1164 specifies Z for an empty collection of drivers.
  auto result = Logic9::z;
  for (const auto driver : drivers) {
    result = resolve(result, driver);
  }
  return result;
}

} // namespace fsim::runtime
