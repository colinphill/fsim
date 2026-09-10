// SPDX-License-Identifier: Apache-2.0
#include "class_expression_resolution.hpp"
#include "class_expression_resolution_internal.hpp"

#include <algorithm>

namespace fsim::frontend {

bool resolve_systemverilog_class_expressions(
    ParsedDesign& design,
    std::vector<Diagnostic>& diagnostics) {
  const auto initial_diagnostic_count = diagnostics.size();
  class_resolution_detail::Resolver{design, diagnostics}.run();
  return std::none_of(
      diagnostics.begin()
          + static_cast<std::ptrdiff_t>(initial_diagnostic_count),
      diagnostics.end(),
      [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
      });
}

}  // namespace fsim::frontend
