// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace fsim::frontend {

struct SystemVerilogScalarConstant {
  SystemVerilogScalarKind kind{SystemVerilogScalarKind::None};
  // Integral/time values use the complete signed bit pattern. IEEE values use
  // binary32 in the low half or binary64 in the complete word.
  std::uint64_t bits{};

  [[nodiscard]] std::optional<std::int64_t> integral() const noexcept;
  [[nodiscard]] std::optional<double> real() const noexcept;
  [[nodiscard]] bool truth() const noexcept;
  [[nodiscard]] std::string display() const;
  [[nodiscard]] std::string canonical() const;
  [[nodiscard]] Expression expression(const SourceSpan& use_span) const;
};

using SystemVerilogScalarTypeEnvironment =
    std::unordered_map<std::string, SystemVerilogScalarKind>;
using SystemVerilogScalarConstantEnvironment =
    std::unordered_map<std::string, SystemVerilogScalarConstant>;

struct SystemVerilogScalarEvaluationContext {
  std::uint64_t time_unit_femtoseconds{1};
  std::uint64_t time_precision_femtoseconds{1};
};

[[nodiscard]] bool propagate_systemverilog_scalar_types(
    Expression& expression,
    const SystemVerilogScalarTypeEnvironment& environment,
    std::string& error);

[[nodiscard]] std::optional<SystemVerilogScalarConstant>
evaluate_systemverilog_scalar_constant(
    const Expression& expression,
    const SystemVerilogScalarConstantEnvironment& environment,
    const SystemVerilogScalarEvaluationContext& context,
    std::string& error);

[[nodiscard]] std::optional<SystemVerilogScalarConstant>
convert_systemverilog_scalar_constant(
    const SystemVerilogScalarConstant& value,
    SystemVerilogScalarKind target,
    std::string& error);

}  // namespace fsim::frontend
