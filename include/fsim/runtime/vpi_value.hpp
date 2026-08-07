// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/vpi_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace fsim::runtime {

enum class SystemVerilogVpiValueFormat {
  Scalar,
  Integer,
  Real,
  String,
  Time,
  Strength,
  BitVector,
  Logic4Vector,
  Logic9Vector,
};

enum class SystemVerilogVpiStrengthRank : std::uint8_t {
  HighZ,
  Small,
  Medium,
  Weak,
  Large,
  Pull,
  Strong,
  Supply,
};

struct SystemVerilogVpiDriveStrength {
  SystemVerilogVpiStrengthRank zero{SystemVerilogVpiStrengthRank::Strong};
  SystemVerilogVpiStrengthRank one{SystemVerilogVpiStrengthRank::Strong};

  friend bool operator==(
      const SystemVerilogVpiDriveStrength&,
      const SystemVerilogVpiDriveStrength&) = default;
};

struct SystemVerilogVpiStrengthValue {
  Logic4 state{Logic4::x};
  SystemVerilogVpiDriveStrength drive;

  friend bool operator==(
      const SystemVerilogVpiStrengthValue&,
      const SystemVerilogVpiStrengthValue&) = default;
};

using SystemVerilogVpiValuePayload = std::variant<
    std::monostate,
    PackedBit2,
    PackedLogic4,
    PackedLogic9,
    double,
    float,
    std::string,
    std::uint64_t>;

struct SystemVerilogVpiStoredValue {
  SystemVerilogVpiValuePayload payload;
  std::optional<SystemVerilogVpiDriveStrength> strength;

  friend bool operator==(
      const SystemVerilogVpiStoredValue&,
      const SystemVerilogVpiStoredValue&) = default;
};

enum class SystemVerilogVpiValueError {
  None,
  InvalidSimulation,
  InvalidHandle,
  CrossSimulation,
  StaleHandle,
  ReleasedHandle,
  NotReadable,
  NotBound,
  AlreadyBound,
  ReadOnly,
  InputOnly,
  NotForced,
  TypeMismatch,
  UnsupportedFormat,
  UnknownState,
  BufferTooSmall,
  ResourceLimit,
  InvalidEncoding,
};

struct SystemVerilogVpiValueReadBuffers {
  std::span<std::uint64_t> words;
  std::span<char> characters;
};

struct SystemVerilogVpiValueReadResult {
  SystemVerilogVpiValueError error{SystemVerilogVpiValueError::None};
  std::size_t required_words{};
  std::size_t required_characters{};
  Logic9 scalar{Logic9::x};
  std::uint64_t integer{};
  bool integer_is_signed{};
  double real{};
  std::uint64_t time{};
  SystemVerilogVpiStrengthValue strength;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiValueError::None;
  }
};
struct SystemVerilogVpiValueWriteData {
  Logic9 scalar{Logic9::x};
  std::uint64_t integer{};
  double real{};
  std::uint64_t time{};
  SystemVerilogVpiStrengthValue strength;
  std::span<const std::uint64_t> words;
  std::span<const char> characters;
};

struct SystemVerilogVpiValueConversionResult {
  std::optional<SystemVerilogVpiStoredValue> value;
  SystemVerilogVpiValueError error{SystemVerilogVpiValueError::None};
  std::size_t required_words{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiValueError::None && value.has_value();
  }
};


[[nodiscard]] bool validate_systemverilog_vpi_stored_value(
    const SystemVerilogVpiTypeInfo& type,
    const SystemVerilogVpiStoredValue& value) noexcept;

[[nodiscard]] SystemVerilogVpiValueReadResult
read_systemverilog_vpi_value(
    const SystemVerilogVpiTypeInfo& type,
    const SystemVerilogVpiStoredValue& value,
    SystemVerilogVpiValueFormat format,
    SystemVerilogVpiValueReadBuffers buffers = {});

[[nodiscard]] SystemVerilogVpiValueConversionResult
make_systemverilog_vpi_stored_value(
    const SystemVerilogVpiTypeInfo& type,
    SystemVerilogVpiValueFormat format,
    const SystemVerilogVpiValueWriteData& input);

}  // namespace fsim::runtime
