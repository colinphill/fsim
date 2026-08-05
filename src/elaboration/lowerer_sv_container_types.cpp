// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<ContainerType> Lowerer::container_type(
    const frontend::Type& type,
    const frontend::SourceSpan& span) {
  const auto evaluate = [&](const frontend::Expression& expression) {
    if (const auto simple = constant_index(expression)) return simple;
    std::string error;
    const auto value = evaluate_systemverilog_constant_expression(
        expression, {}, {}, error);
    return value ? value->integer_value()
                 : std::optional<std::int64_t>{};
  };
  return materialize_systemverilog_container_type(
      type, span, evaluate,
      [&](std::string code,
          std::string message,
          frontend::SourceSpan source) {
        report(std::move(code), std::move(message), std::move(source));
      });
}

}  // namespace fsim::elaboration
