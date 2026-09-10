// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_physical_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expected_type == nullptr
      || !expected_type->vhdl_physical
      || expression.kind != ExpressionKind::Call
      || !expression.text.starts_with("@vhdl-physical:")) {
    return ExpressionAttempt{};
  }
  if (expected_width != 32 && expected_width != 64) {
    report(
        "FSIM-ELAB-VHPHYSICAL-005",
        "bounded physical values require a signed 32- or 64-bit runtime "
        "representation",
        expression.span);
    return ExpressionAttempt{std::nullopt};
  }
  std::string error;
  const auto value = vhdl_physical_literal_value(
      expression, *expected_type, error);
  if (!value) {
    report(
        "FSIM-ELAB-VHPHYSICAL-006",
        "invalid physical literal for type '"
            + expected_type->spelling + "': " + error,
        expression.span);
    return ExpressionAttempt{std::nullopt};
  }
  const auto result = allocate_register(
      expected_width, frontend::ValueDomain::Integer);
  process_.operations.emplace_back(
      LoadConstant{
          result,
          unsigned_value(
              static_cast<std::uint64_t>(*value), expected_width)});
  return ExpressionAttempt{result};
}

}  // namespace fsim::elaboration
