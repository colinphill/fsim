// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/design.hpp"
#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/source.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace fsim::frontend {

SourceSpan cover(const SourceSpan& first, const SourceSpan& last) {
  SourceSpan result = first;
  result.end = last.end;
  if (result.source_name.empty()) {
    result.source_name = last.source_name;
  }
  return result;
}

const char* to_string(DiagnosticSeverity severity) noexcept {
  switch (severity) {
    case DiagnosticSeverity::Note:
      return "note";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Error:
      return "error";
  }
  return "error";
}

std::string format_diagnostic(const Diagnostic& diagnostic) {
  std::ostringstream output;
  output << diagnostic.span.source_name << ':' << diagnostic.span.begin.line
         << ':' << diagnostic.span.begin.column << ": "
         << to_string(diagnostic.severity) << '[' << diagnostic.code
         << "]: " << diagnostic.message;
  for (const auto& expansion : diagnostic.expansion_stack) {
    output << "\n  note: " << expansion;
  }
  return output.str();
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic& diagnostic) {
                       return diagnostic.severity == DiagnosticSeverity::Error;
                     });
}

std::uint64_t PackedRange::width() const noexcept {
  const auto unsigned_left = static_cast<std::uint64_t>(left);
  const auto unsigned_right = static_cast<std::uint64_t>(right);
  const auto distance =
      left >= right ? unsigned_left - unsigned_right
                    : unsigned_right - unsigned_left;
  if (distance == std::numeric_limits<std::uint64_t>::max()) {
    return 0;
  }
  return distance + 1;
}

std::optional<std::uint64_t> Type::width() const noexcept {
  if (packed_range) {
    return packed_range->width();
  }
  switch (domain) {
    case ValueDomain::Bit2:
    case ValueDomain::Logic4:
    case ValueDomain::Logic9:
    case ValueDomain::Boolean:
      return 1;
    case ValueDomain::Integer:
    case ValueDomain::Unknown:
      return std::nullopt;
  }
  return std::nullopt;
}

const DesignUnit* ParsedDesign::find(UnitKind kind,
                                     std::string_view name) const noexcept {
  const auto found =
      std::find_if(units.begin(), units.end(), [&](const DesignUnit& unit) {
        return unit.kind == kind && unit.name == name;
      });
  return found == units.end() ? nullptr : &*found;
}

}  // namespace fsim::frontend
