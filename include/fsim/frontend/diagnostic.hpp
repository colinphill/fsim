// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/source.hpp"

#include <string>
#include <vector>

namespace fsim::frontend {

enum class DiagnosticSeverity {
  Note,
  Warning,
  Error,
};

struct Diagnostic {
  DiagnosticSeverity severity{DiagnosticSeverity::Error};
  std::string code;
  std::string message;
  SourceSpan span;
  std::vector<std::string> expansion_stack;

  friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

[[nodiscard]] const char* to_string(DiagnosticSeverity severity) noexcept;
[[nodiscard]] std::string format_diagnostic(const Diagnostic& diagnostic);
[[nodiscard]] bool has_errors(const std::vector<Diagnostic>& diagnostics);

}  // namespace fsim::frontend
