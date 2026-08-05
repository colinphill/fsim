// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace fsim::runtime {
namespace {

constexpr std::uint32_t maximum_text_precision = 64;

bool real_kind(const SystemVerilogScalarKind kind) noexcept {
  return kind == SystemVerilogScalarKind::ShortReal
      || kind == SystemVerilogScalarKind::Real
      || kind == SystemVerilogScalarKind::Realtime;
}

bool numeric_kind(const SystemVerilogScalarKind kind) noexcept {
  return kind == SystemVerilogScalarKind::None
      || kind == SystemVerilogScalarKind::Time || real_kind(kind);
}

bool ascii_space(const char character) noexcept {
  return character == ' ' || character == '\t' || character == '\n'
      || character == '\r' || character == '\f' || character == '\v';
}

std::string_view trim_ascii(std::string_view text) noexcept {
  while (!text.empty() && ascii_space(text.front())) text.remove_prefix(1);
  while (!text.empty() && ascii_space(text.back())) text.remove_suffix(1);
  return text;
}

SystemVerilogScalarError context_error(
    const SystemVerilogTimeContext& context) noexcept {
  if (context.time_unit_femtoseconds == 0
      || context.time_precision_femtoseconds == 0
      || context.project_resolution_femtoseconds == 0
      || context.time_unit_femtoseconds
              % context.time_precision_femtoseconds != 0
      || context.time_precision_femtoseconds
              % context.project_resolution_femtoseconds != 0) {
    return SystemVerilogScalarError::InvalidTimeContext;
  }
  return SystemVerilogScalarError::None;
}

template <typename Number>
SystemVerilogScalarTextResult format_number(
    const Number value,
    const SystemVerilogScalarFormatOptions& options) {
  std::array<char, 512> buffer{};
  auto format = std::chars_format::general;
  if (options.format == SystemVerilogScalarTextFormat::Fixed) {
    format = std::chars_format::fixed;
  } else if (options.format == SystemVerilogScalarTextFormat::Scientific) {
    format = std::chars_format::scientific;
  }
  const auto precision = options.precision
          == SystemVerilogScalarFormatOptions::automatic_precision
      ? static_cast<std::uint32_t>(std::numeric_limits<Number>::max_digits10)
      : options.precision;
  const auto converted = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value, format,
      static_cast<int>(precision));
  return converted.ec == std::errc{}
      ? SystemVerilogScalarTextResult{
            std::string{buffer.data(), converted.ptr}, {}}
      : SystemVerilogScalarTextResult{
            {}, SystemVerilogScalarError::ResourceLimit};
}

template <typename Integer>
SystemVerilogScalarTextResult format_integer(const Integer value) {
  std::array<char, 32> buffer{};
  const auto converted = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value);
  return converted.ec == std::errc{}
      ? SystemVerilogScalarTextResult{
            std::string{buffer.data(), converted.ptr}, {}}
      : SystemVerilogScalarTextResult{
            {}, SystemVerilogScalarError::ResourceLimit};
}

template <typename Number>
SystemVerilogScalarResult scan_real(
    std::string_view text,
    const SystemVerilogScalarKind target) noexcept {
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  if (text.empty()) return {{}, SystemVerilogScalarError::InvalidText};
  Number number{};
  const auto parsed = std::from_chars(
      text.data(), text.data() + text.size(), number,
      std::chars_format::general);
  if (parsed.ec == std::errc::result_out_of_range) {
    return {{}, SystemVerilogScalarError::Overflow};
  }
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return {{}, SystemVerilogScalarError::InvalidText};
  }
  if (!std::isfinite(number)) {
    return {{}, SystemVerilogScalarError::Nonfinite};
  }
  if constexpr (std::is_same_v<Number, float>) {
    return {SystemVerilogScalarValue::shortreal(number), {}};
  }
  return target == SystemVerilogScalarKind::Realtime
      ? SystemVerilogScalarResult{
            SystemVerilogScalarValue::realtime(number), {}}
      : SystemVerilogScalarResult{
            SystemVerilogScalarValue::real(number), {}};
}

SystemVerilogScalarResult scan_integer(
    std::string_view text,
    const SystemVerilogScalarKind target) noexcept {
  if (!text.empty() && text.front() == '+') text.remove_prefix(1);
  if (text.empty()) return {{}, SystemVerilogScalarError::InvalidText};
  if (target == SystemVerilogScalarKind::Time) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec == std::errc::result_out_of_range) {
      return {{}, SystemVerilogScalarError::Overflow};
    }
    return parsed.ec == std::errc{}
            && parsed.ptr == text.data() + text.size()
        ? SystemVerilogScalarResult{SystemVerilogScalarValue::time(value), {}}
        : SystemVerilogScalarResult{{}, SystemVerilogScalarError::InvalidText};
  }
  std::int64_t value{};
  const auto parsed = std::from_chars(
      text.data(), text.data() + text.size(), value);
  if (parsed.ec == std::errc::result_out_of_range) {
    return {{}, SystemVerilogScalarError::Overflow};
  }
  return parsed.ec == std::errc{}
          && parsed.ptr == text.data() + text.size()
      ? SystemVerilogScalarResult{SystemVerilogScalarValue::integral(value), {}}
      : SystemVerilogScalarResult{{}, SystemVerilogScalarError::InvalidText};
}

}  // namespace

SystemVerilogScalarTextResult format_systemverilog_scalar(
    const SystemVerilogScalarValue& value,
    const SystemVerilogScalarFormatOptions& options) {
  if (!numeric_kind(value.kind)) {
    return {{}, SystemVerilogScalarError::InvalidKind};
  }
  if (options.maximum_output_bytes == 0
      || options.minimum_width > options.maximum_output_bytes
      || (options.padding != ' ' && options.padding != '0')
      || (options.precision
              != SystemVerilogScalarFormatOptions::automatic_precision
          && options.precision > maximum_text_precision)) {
    return {{}, SystemVerilogScalarError::ResourceLimit};
  }
  const bool decimal = options.format == SystemVerilogScalarTextFormat::Decimal
      || options.format == SystemVerilogScalarTextFormat::Time;
  SystemVerilogScalarTextResult result;
  if (value.kind == SystemVerilogScalarKind::Time) {
    result = format_integer(value.bits);
  } else if (const auto integral = value.as_integral()) {
    result = format_integer(*integral);
  } else if (decimal) {
    return {{}, SystemVerilogScalarError::InvalidKind};
  } else if (const auto short_value = value.as_shortreal()) {
    result = format_number(*short_value, options);
  } else {
    result = format_number(*value.as_real(), options);
  }
  if (!result) return result;
  if (result.text.size() > options.maximum_output_bytes
      || options.suffix.size()
              > options.maximum_output_bytes - result.text.size()) {
    return {{}, SystemVerilogScalarError::ResourceLimit};
  }
  result.text.append(options.suffix);
  if (result.text.size() < options.minimum_width) {
    const auto count = options.minimum_width - result.text.size();
    if (options.left_justify) {
      result.text.append(count, options.padding);
    } else if (options.padding == '0' && !result.text.empty()
               && (result.text.front() == '-' || result.text.front() == '+')) {
      result.text.insert(1, count, options.padding);
    } else {
      result.text.insert(0, count, options.padding);
    }
  }
  return result;
}

SystemVerilogScalarResult scan_systemverilog_scalar(
    const std::string_view source,
    const SystemVerilogScalarKind target,
    const SystemVerilogScalarRounding rounding,
    const std::size_t maximum_input_bytes) noexcept {
  if (!numeric_kind(target)) {
    return {{}, SystemVerilogScalarError::InvalidKind};
  }
  if (maximum_input_bytes == 0 || source.size() > maximum_input_bytes) {
    return {{}, SystemVerilogScalarError::ResourceLimit};
  }
  const auto text = trim_ascii(source);
  if (text.empty()) return {{}, SystemVerilogScalarError::InvalidText};
  if (real_kind(target)) {
    return target == SystemVerilogScalarKind::ShortReal
        ? scan_real<float>(text, target) : scan_real<double>(text, target);
  }
  const bool decimal = text.find_first_of(".eE") != std::string_view::npos;
  if (!decimal) return scan_integer(text, target);
  const auto parsed = scan_real<double>(text, SystemVerilogScalarKind::Real);
  return parsed ? convert_systemverilog_scalar(parsed.value, target, rounding)
                : parsed;
}

SystemVerilogDelayResult scale_systemverilog_delay(
    const SystemVerilogScalarValue& delay,
    const SystemVerilogTimeContext& context) noexcept {
  if (const auto error = context_error(context);
      error != SystemVerilogScalarError::None) return {0, error};
  if (!numeric_kind(delay.kind)) {
    return {0, SystemVerilogScalarError::InvalidKind};
  }
  const auto ticks_per_unit = context.time_unit_femtoseconds
      / context.project_resolution_femtoseconds;
  const auto ticks_per_quantum = context.time_precision_femtoseconds
      / context.project_resolution_femtoseconds;
  if (delay.kind == SystemVerilogScalarKind::Time) {
    if (delay.bits != 0
        && ticks_per_unit > std::numeric_limits<std::uint64_t>::max()
                / delay.bits) {
      return {0, SystemVerilogScalarError::Overflow};
    }
    return {delay.bits * ticks_per_unit, {}};
  }
  if (const auto integral = delay.as_integral()) {
    if (*integral < 0) return {0, SystemVerilogScalarError::NegativeDelay};
    const auto magnitude = static_cast<std::uint64_t>(*integral);
    if (magnitude != 0
        && ticks_per_unit > std::numeric_limits<std::uint64_t>::max()
                / magnitude) {
      return {0, SystemVerilogScalarError::Overflow};
    }
    return {magnitude * ticks_per_unit, {}};
  }
  if (std::fegetround() != FE_TONEAREST) {
    return {0, SystemVerilogScalarError::UnsupportedRoundingMode};
  }
  const auto number = delay.as_real();
  if (!number || !std::isfinite(*number)) {
    return {0, SystemVerilogScalarError::Nonfinite};
  }
  if (*number < 0.0) return {0, SystemVerilogScalarError::NegativeDelay};
  const auto units_per_quantum = static_cast<double>(
      context.time_unit_femtoseconds / context.time_precision_femtoseconds);
  const auto quantum_value = *number * units_per_quantum;
  if (!std::isfinite(quantum_value)) {
    return {0, SystemVerilogScalarError::Overflow};
  }
  const auto rounded = std::round(quantum_value);
  constexpr long double exclusive_upper = 18446744073709551616.0L;
  if (static_cast<long double>(rounded) >= exclusive_upper) {
    return {0, SystemVerilogScalarError::Overflow};
  }
  const auto quanta = static_cast<std::uint64_t>(rounded);
  if (quanta != 0
      && ticks_per_quantum > std::numeric_limits<std::uint64_t>::max()
              / quanta) {
    return {0, SystemVerilogScalarError::Overflow};
  }
  return {quanta * ticks_per_quantum, {}};
}

SystemVerilogDelayResult schedule_systemverilog_delay(
    const std::uint64_t current_tick,
    const SystemVerilogScalarValue& delay,
    const SystemVerilogTimeContext& context) noexcept {
  const auto scaled = scale_systemverilog_delay(delay, context);
  if (!scaled) return scaled;
  if (scaled.ticks > std::numeric_limits<std::uint64_t>::max() - current_tick) {
    return {0, SystemVerilogScalarError::Overflow};
  }
  return {current_tick + scaled.ticks, {}};
}

SystemVerilogScalarResult systemverilog_time_function(
    const SystemVerilogTimeFunction function,
    const std::uint64_t current_tick,
    const SystemVerilogTimeContext& context) noexcept {
  if (const auto error = context_error(context);
      error != SystemVerilogScalarError::None) return {{}, error};
  const auto ticks_per_unit = context.time_unit_femtoseconds
      / context.project_resolution_femtoseconds;
  if (function == SystemVerilogTimeFunction::Realtime) {
    if (std::fegetround() != FE_TONEAREST) {
      return {{}, SystemVerilogScalarError::UnsupportedRoundingMode};
    }
    const auto value = static_cast<double>(current_tick)
        / static_cast<double>(ticks_per_unit);
    return std::isfinite(value)
        ? SystemVerilogScalarResult{
              SystemVerilogScalarValue::realtime(value), {}}
        : SystemVerilogScalarResult{{}, SystemVerilogScalarError::Overflow};
  }
  auto units = current_tick / ticks_per_unit;
  const auto remainder = current_tick % ticks_per_unit;
  const auto half = ticks_per_unit / 2
      + static_cast<std::uint64_t>(ticks_per_unit % 2 != 0);
  if (remainder >= half) ++units;
  if (function == SystemVerilogTimeFunction::Stime) {
    units &= std::numeric_limits<std::uint32_t>::max();
  }
  return {SystemVerilogScalarValue::time(units), {}};
}

}  // namespace fsim::runtime
