// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/dpi_marshalling.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <bit>
#include <cmath>

namespace fsim::runtime {

namespace {

[[nodiscard]] std::size_t dpi_word_count(const std::size_t width) {
  return width / 32U + static_cast<std::size_t>(width % 32U != 0);
}

[[nodiscard]] bool unused_bits_are_zero(
    const std::vector<std::uint32_t>& plane,
    const std::size_t width) {
  const auto remainder = width % 32U;
  if (plane.empty() || remainder == 0) {
    return true;
  }
  const auto mask = (std::uint32_t{1} << remainder) - 1U;
  return (plane.back() & ~mask) == 0;
}

}  // namespace

SystemVerilogDpiScalarMarshalResult marshal_systemverilog_dpi_scalar(
    const PackedLogic4& value,
    const SystemVerilogDpiScalarDomain domain,
    const std::size_t maximum_bits) {
  if (value.empty()) {
    return {
        SystemVerilogDpiScalarPayload{},
        SystemVerilogDpiMarshallingError::EmptyWidth};
  }
  if (value.width() > maximum_bits) {
    return {
        SystemVerilogDpiScalarPayload{},
        SystemVerilogDpiMarshallingError::WidthLimit};
  }
  SystemVerilogDpiScalarPayload payload;
  payload.domain = domain;
  payload.width = value.width();
  const auto words = dpi_word_count(value.width());
  payload.aval.assign(words, 0);
  if (domain == SystemVerilogDpiScalarDomain::Logic4) {
    payload.bval.assign(words, 0);
  }
  for (std::size_t index{}; index < value.width(); ++index) {
    const auto state = value.get(index);
    if (domain == SystemVerilogDpiScalarDomain::Bit2
        && (state == Logic4::x || state == Logic4::z)) {
      return {{}, SystemVerilogDpiMarshallingError::UnknownValue};
    }
    const auto word = index / 32U;
    const auto mask = std::uint32_t{1} << (index % 32U);
    if (state == Logic4::one || state == Logic4::x) {
      payload.aval[word] |= mask;
    }
    if (domain == SystemVerilogDpiScalarDomain::Logic4
        && (state == Logic4::x || state == Logic4::z)) {
      payload.bval[word] |= mask;
    }
  }
  return {std::move(payload), {}};
}

SystemVerilogDpiScalarUnmarshalResult unmarshal_systemverilog_dpi_scalar(
    const SystemVerilogDpiScalarPayload& value,
    const std::size_t expected_width,
    const std::size_t maximum_bits) {
  if (value.width == 0 || expected_width == 0) {
    return {PackedLogic4{}, SystemVerilogDpiMarshallingError::EmptyWidth};
  }
  if (value.width > maximum_bits || expected_width > maximum_bits) {
    return {PackedLogic4{}, SystemVerilogDpiMarshallingError::WidthLimit};
  }
  if (value.width != expected_width) {
    return {PackedLogic4{}, SystemVerilogDpiMarshallingError::WidthMismatch};
  }
  const auto words = dpi_word_count(value.width);
  if (value.aval.size() != words
      || (value.domain == SystemVerilogDpiScalarDomain::Logic4
              ? value.bval.size() != words
              : !value.bval.empty())) {
    return {PackedLogic4{}, SystemVerilogDpiMarshallingError::PlaneSize};
  }
  if (!unused_bits_are_zero(value.aval, value.width)
      || !unused_bits_are_zero(value.bval, value.width)) {
    return {PackedLogic4{}, SystemVerilogDpiMarshallingError::UnusedBits};
  }

  PackedLogic4 result(value.width, Logic4::zero);
  for (std::size_t index{}; index < value.width; ++index) {
    const auto word = index / 32U;
    const auto mask = std::uint32_t{1} << (index % 32U);
    const bool aval = (value.aval[word] & mask) != 0;
    const bool bval = value.domain == SystemVerilogDpiScalarDomain::Logic4
        && (value.bval[word] & mask) != 0;
    result.set(
        index,
        bval ? (aval ? Logic4::x : Logic4::z)
             : (aval ? Logic4::one : Logic4::zero));
  }
  return {std::move(result), {}};
}

SystemVerilogDpiRealMarshalResult marshal_systemverilog_dpi_real(
    const SystemVerilogScalarValue& value,
    const SystemVerilogDpiTransferMode mode) {
  SystemVerilogDpiRealPayload payload;
  payload.mode = mode;
  payload.bits = value.bits;
  switch (value.kind) {
  case SystemVerilogScalarKind::ShortReal: {
    payload.kind = SystemVerilogDpiRealKind::ShortReal;
    const auto number = std::bit_cast<float>(
        static_cast<std::uint32_t>(value.bits));
    if (!std::isfinite(number)) {
      return {{}, SystemVerilogDpiMarshallingError::Nonfinite};
    }
    break;
  }
  case SystemVerilogScalarKind::Real: {
    payload.kind = SystemVerilogDpiRealKind::Real;
    if (!std::isfinite(std::bit_cast<double>(value.bits))) {
      return {{}, SystemVerilogDpiMarshallingError::Nonfinite};
    }
    break;
  }
  case SystemVerilogScalarKind::Realtime: {
    payload.kind = SystemVerilogDpiRealKind::Realtime;
    if (!std::isfinite(std::bit_cast<double>(value.bits))) {
      return {{}, SystemVerilogDpiMarshallingError::Nonfinite};
    }
    break;
  }
  default:
    return {{}, SystemVerilogDpiMarshallingError::KindMismatch};
  }
  return {payload, {}};
}

SystemVerilogDpiRealUnmarshalResult unmarshal_systemverilog_dpi_real(
    const SystemVerilogDpiRealPayload& value,
    const SystemVerilogDpiRealKind expected_kind,
    const SystemVerilogDpiTransferMode expected_mode) {
  if (value.kind != expected_kind) {
    return {{}, SystemVerilogDpiMarshallingError::KindMismatch};
  }
  if (value.mode != expected_mode
      || expected_mode == SystemVerilogDpiTransferMode::Input) {
    return {{}, SystemVerilogDpiMarshallingError::DirectionMismatch};
  }
  switch (value.kind) {
  case SystemVerilogDpiRealKind::ShortReal: {
    if ((value.bits >> 32U) != 0) {
      return {{}, SystemVerilogDpiMarshallingError::UnusedBits};
    }
    const auto number = std::bit_cast<float>(
        static_cast<std::uint32_t>(value.bits));
    if (!std::isfinite(number)) {
      return {{}, SystemVerilogDpiMarshallingError::Nonfinite};
    }
    return {SystemVerilogScalarValue::shortreal(number), {}};
  }
  case SystemVerilogDpiRealKind::Real: {
    const auto number = std::bit_cast<double>(value.bits);
    if (!std::isfinite(number)) {
      return {{}, SystemVerilogDpiMarshallingError::Nonfinite};
    }
    return {SystemVerilogScalarValue::real(number), {}};
  }
  case SystemVerilogDpiRealKind::Realtime: {
    const auto number = std::bit_cast<double>(value.bits);
    if (!std::isfinite(number)) {
      return {{}, SystemVerilogDpiMarshallingError::Nonfinite};
    }
    return {SystemVerilogScalarValue::realtime(number), {}};
  }
  }
  return {{}, SystemVerilogDpiMarshallingError::KindMismatch};
}

SystemVerilogDpiStringResult marshal_systemverilog_dpi_string(
    const std::string_view value,
    const SystemVerilogDpiTransferMode mode,
    const std::size_t maximum_bytes) {
  if (value.size() > maximum_bytes) {
    return {{}, SystemVerilogDpiMarshallingError::StringLimit};
  }
  if (value.find('\0') != std::string_view::npos) {
    return {{}, SystemVerilogDpiMarshallingError::EmbeddedNul};
  }
  if (!systemverilog_string_is_valid(value)) {
    return {{}, SystemVerilogDpiMarshallingError::InvalidUtf8};
  }
  return {SystemVerilogDpiStringPayload{mode, std::string{value}}, {}};
}

SystemVerilogDpiStringResult unmarshal_systemverilog_dpi_string(
    const SystemVerilogDpiStringPayload& value,
    const SystemVerilogDpiTransferMode expected_mode,
    const std::size_t maximum_bytes) {
  if (value.mode != expected_mode
      || expected_mode == SystemVerilogDpiTransferMode::Input) {
    return {{}, SystemVerilogDpiMarshallingError::DirectionMismatch};
  }
  return marshal_systemverilog_dpi_string(
      value.bytes, value.mode, maximum_bytes);
}

SystemVerilogDpiChandleResult marshal_systemverilog_dpi_chandle(
    const SystemVerilogChandleRegistry& registry,
    const SystemVerilogChandle handle,
    const SystemVerilogDpiTransferMode mode) {
  if (handle != 0 && !registry.contains(handle)) {
    return {{}, SystemVerilogDpiMarshallingError::StaleHandle};
  }
  return {{mode, handle, mode == SystemVerilogDpiTransferMode::Input}, {}};
}

SystemVerilogDpiChandleResult unmarshal_systemverilog_dpi_chandle(
    const SystemVerilogChandleRegistry& registry,
    const SystemVerilogDpiChandlePayload& value,
    const SystemVerilogDpiTransferMode expected_mode) {
  if (value.mode != expected_mode
      || expected_mode == SystemVerilogDpiTransferMode::Input
      || value.borrowed) {
    return {{}, SystemVerilogDpiMarshallingError::DirectionMismatch};
  }
  if (value.handle != 0 && !registry.contains(value.handle)) {
    return {{}, SystemVerilogDpiMarshallingError::StaleHandle};
  }
  return {value, {}};
}

}  // namespace fsim::runtime
