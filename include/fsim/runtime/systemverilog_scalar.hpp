// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/systemverilog_scalars.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/systemverilog_chandle.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogScalarKind = frontend::SystemVerilogScalarKind;
using SystemVerilogScalarId = std::uint32_t;

struct SystemVerilogScalarValue {
  SystemVerilogScalarKind kind{SystemVerilogScalarKind::None};
  std::uint64_t bits{};

  [[nodiscard]] static SystemVerilogScalarValue shortreal(float value) noexcept;
  [[nodiscard]] static SystemVerilogScalarValue real(double value) noexcept;
  [[nodiscard]] static SystemVerilogScalarValue realtime(double value) noexcept;
  [[nodiscard]] static SystemVerilogScalarValue time(std::uint64_t ticks) noexcept;
  [[nodiscard]] static SystemVerilogScalarValue chandle(
      SystemVerilogChandle handle) noexcept;
  [[nodiscard]] static SystemVerilogScalarValue integral(std::int64_t value) noexcept;
  [[nodiscard]] std::optional<float> as_shortreal() const noexcept;
  [[nodiscard]] std::optional<double> as_real() const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> as_time() const noexcept;
  [[nodiscard]] std::optional<SystemVerilogChandle>
  as_chandle() const noexcept;
  [[nodiscard]] std::optional<std::int64_t> as_integral() const noexcept;
  [[nodiscard]] std::string canonical() const;
  friend bool operator==(
      const SystemVerilogScalarValue&,
      const SystemVerilogScalarValue&) = default;
};

enum class SystemVerilogScalarArithmetic : std::uint8_t {
  Add,
  Subtract,
  Multiply,
  Divide,
};

enum class SystemVerilogScalarError : std::uint8_t {
  None,
  InvalidKind,
  InvalidId,
  DivideByZero,
  Overflow,
  Nonfinite,
  UnsupportedRoundingMode,
  UnknownValue,
  ResourceLimit,
  InvalidText,
  InvalidTimeContext,
  NegativeDelay,
};

struct SystemVerilogScalarResult {
  SystemVerilogScalarValue value;
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

[[nodiscard]] SystemVerilogScalarResult systemverilog_scalar_arithmetic(
    SystemVerilogScalarArithmetic operation,
    const SystemVerilogScalarValue& left,
    const SystemVerilogScalarValue& right) noexcept;

enum class SystemVerilogScalarComparison : std::uint8_t {
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
};

enum class SystemVerilogScalarBinaryOperator : std::uint8_t {
  Add,
  Subtract,
  Multiply,
  Divide,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
};

struct SystemVerilogScalarPredicate {
  bool value{};
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

[[nodiscard]] SystemVerilogScalarPredicate systemverilog_scalar_compare(
    SystemVerilogScalarComparison operation,
    const SystemVerilogScalarValue& left,
    const SystemVerilogScalarValue& right) noexcept;
[[nodiscard]] SystemVerilogScalarPredicate systemverilog_scalar_truth(
    const SystemVerilogScalarValue& value) noexcept;

enum class SystemVerilogScalarRounding : std::uint8_t {
  NearestAwayFromZero,
  TowardZero,
  Floor,
  Ceil,
};

[[nodiscard]] SystemVerilogScalarResult convert_systemverilog_scalar(
    const SystemVerilogScalarValue& value,
    SystemVerilogScalarKind target,
    SystemVerilogScalarRounding rounding =
        SystemVerilogScalarRounding::NearestAwayFromZero) noexcept;
[[nodiscard]] SystemVerilogScalarResult systemverilog_scalar_from_packed(
    const PackedLogic4& value,
    bool is_signed,
    SystemVerilogScalarKind target) noexcept;

struct SystemVerilogPackedScalarResult {
  PackedLogic4 value;
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

[[nodiscard]] SystemVerilogPackedScalarResult systemverilog_scalar_to_packed(
    const SystemVerilogScalarValue& value,
    std::size_t width,
    bool is_signed,
    SystemVerilogScalarRounding rounding =
        SystemVerilogScalarRounding::NearestAwayFromZero);

/// Encode/decode the canonical raw payload used by SimIR and native service
/// boundaries. Unlike numeric packed conversions, these functions preserve
/// IEEE sign/payload bits and exact ticks without applying a cast.
[[nodiscard]] SystemVerilogPackedScalarResult
encode_systemverilog_scalar_payload(
    const SystemVerilogScalarValue& value);
[[nodiscard]] SystemVerilogScalarResult
decode_systemverilog_scalar_payload(
    const PackedLogic4& payload,
    SystemVerilogScalarKind kind) noexcept;

/// Execute one scalar arithmetic/comparison operation directly on canonical
/// SimIR payloads. Arithmetic is converted once to result_kind; comparisons
/// return a known one-bit packed predicate.
[[nodiscard]] SystemVerilogPackedScalarResult
systemverilog_scalar_binary_payload(
    SystemVerilogScalarBinaryOperator operation,
    const PackedLogic4& left,
    SystemVerilogScalarKind left_kind,
    const PackedLogic4& right,
    SystemVerilogScalarKind right_kind,
    SystemVerilogScalarKind result_kind) noexcept;

struct SystemVerilogScalarClassification {
  bool finite{};
  bool zero{};
  bool negative{};
  bool normal{};
  bool subnormal{};
  bool infinite{};
  bool nan{};
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

[[nodiscard]] SystemVerilogScalarClassification
classify_systemverilog_scalar(
    const SystemVerilogScalarValue& value) noexcept;

enum class SystemVerilogScalarTextFormat : std::uint8_t {
  General,
  Fixed,
  Scientific,
  Decimal,
  Time,
};

struct SystemVerilogScalarFormatOptions {
  static constexpr std::uint32_t automatic_precision =
      std::numeric_limits<std::uint32_t>::max();
  SystemVerilogScalarTextFormat format{SystemVerilogScalarTextFormat::General};
  std::uint32_t precision{automatic_precision};
  std::size_t minimum_width{};
  std::size_t maximum_output_bytes{4096};
  bool left_justify{};
  char padding{' '};
  std::string_view suffix;
};

struct SystemVerilogScalarTextResult {
  std::string text;
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

[[nodiscard]] SystemVerilogScalarTextResult format_systemverilog_scalar(
    const SystemVerilogScalarValue& value,
    const SystemVerilogScalarFormatOptions& options = {});
[[nodiscard]] SystemVerilogScalarResult scan_systemverilog_scalar(
    std::string_view text,
    SystemVerilogScalarKind target,
    SystemVerilogScalarRounding rounding =
        SystemVerilogScalarRounding::NearestAwayFromZero,
    std::size_t maximum_input_bytes = 4096) noexcept;

struct SystemVerilogTimeContext {
  std::uint64_t time_unit_femtoseconds{1};
  std::uint64_t time_precision_femtoseconds{1};
  std::uint64_t project_resolution_femtoseconds{1};
};

struct SystemVerilogDelayResult {
  std::uint64_t ticks{};
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

[[nodiscard]] SystemVerilogDelayResult scale_systemverilog_delay(
    const SystemVerilogScalarValue& delay,
    const SystemVerilogTimeContext& context) noexcept;
[[nodiscard]] SystemVerilogDelayResult schedule_systemverilog_delay(
    std::uint64_t current_tick,
    const SystemVerilogScalarValue& delay,
    const SystemVerilogTimeContext& context) noexcept;

enum class SystemVerilogTimeFunction : std::uint8_t {
  Time,
  Stime,
  Realtime,
};

[[nodiscard]] SystemVerilogScalarResult systemverilog_time_function(
    SystemVerilogTimeFunction function,
    std::uint64_t current_tick,
    const SystemVerilogTimeContext& context) noexcept;

struct SystemVerilogScalarStorageLimits {
  std::size_t maximum_values{1U << 20U};
  std::size_t maximum_bytes{16U << 20U};
  std::uint64_t maximum_operations{1U << 24U};
};

struct SystemVerilogScalarMaterialization {
  SystemVerilogScalarId id{};
  SystemVerilogScalarError error{SystemVerilogScalarError::None};
  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogScalarError::None;
  }
};

class SystemVerilogScalarStorage {
public:
  static constexpr std::size_t canonical_bytes_per_value = 16;
  explicit SystemVerilogScalarStorage(
      SystemVerilogScalarStorageLimits limits = {});
  [[nodiscard]] SystemVerilogScalarMaterialization materialize(
      SystemVerilogScalarValue value);
  [[nodiscard]] std::optional<SystemVerilogScalarValue> load(
      SystemVerilogScalarId id) const noexcept;
  [[nodiscard]] SystemVerilogScalarError store(
      SystemVerilogScalarId id,
      SystemVerilogScalarValue value) noexcept;
  [[nodiscard]] SystemVerilogScalarMaterialization arithmetic(
      SystemVerilogScalarArithmetic operation,
      SystemVerilogScalarId left,
      SystemVerilogScalarId right);
  [[nodiscard]] std::span<const SystemVerilogScalarValue> values() const noexcept {
    return values_;
  }
  [[nodiscard]] std::size_t materialized_bytes() const noexcept;
  [[nodiscard]] std::uint64_t operations() const noexcept { return operations_; }

private:
  [[nodiscard]] SystemVerilogScalarError value_error(
      SystemVerilogScalarValue value) const noexcept;
  SystemVerilogScalarStorageLimits limits_;
  std::vector<SystemVerilogScalarValue> values_;
  std::uint64_t operations_{};
};

}  // namespace fsim::runtime
