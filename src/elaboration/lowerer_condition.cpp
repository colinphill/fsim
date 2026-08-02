// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<RegisterId> Lowerer::lower_condition(
    const Expression& expression,
    std::string diagnostic_code,
    const std::string_view construct) {
  const auto expression_width =
      infer_width(expression).value_or(std::size_t{1});
  const auto source = lower_expression(expression, expression_width);
  if (!source) {
    return std::nullopt;
  }
  if (language_ == frontend::Language::Vhdl2008) {
    if (register_width(*source) != 1
        || register_domain(*source) != frontend::ValueDomain::Boolean) {
      report(
          std::move(diagnostic_code),
          "a VHDL " + std::string{construct}
              + " condition must have type boolean",
          expression.span);
      return std::nullopt;
    }
    return source;
  }

  // Applying logical negation twice retains X/Z truth while normalizing the
  // complete SystemVerilog expression to a scalar branch condition.
  const auto truth_domain =
      is_two_state_domain(register_domain(*source))
          ? frontend::ValueDomain::Bit2
          : frontend::ValueDomain::Logic4;
  const auto inverted = allocate_register(1, truth_domain);
  process_.operations.emplace_back(LogicalNot{inverted, *source});
  const auto normalized = allocate_register(1, truth_domain);
  process_.operations.emplace_back(LogicalNot{normalized, inverted});
  return normalized;
}

}  // namespace fsim::elaboration
