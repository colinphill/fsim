// SPDX-License-Identifier: Apache-2.0
#include "fsim/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>

namespace fsim::diagnostic {
namespace {

void print_json_string(std::ostream& output, const std::string_view value) {
  output.put('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20U) {
          const auto flags = output.flags();
          const auto fill = output.fill();
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned>(character);
          output.flags(flags);
          output.fill(fill);
        } else {
          output.put(static_cast<char>(character));
        }
        break;
    }
  }
  output.put('"');
}

void print_json_position(std::ostream& output, const SourcePosition& position) {
  output << "{\"line\":" << position.line << ",\"column\":" << position.column
         << ",\"offset\":" << position.offset << '}';
}

void print_json_span(std::ostream& output, const SourceSpan& span) {
  output << "{\"path\":";
  print_json_string(output, span.path);
  output << ",\"begin\":";
  print_json_position(output, span.begin);
  output << ",\"end\":";
  print_json_position(output, span.end);
  output << '}';
}

}  // namespace

std::string_view to_string(const Severity severity) noexcept {
  switch (severity) {
    case Severity::note:
      return "note";
    case Severity::warning:
      return "warning";
    case Severity::error:
      return "error";
    case Severity::fatal:
      return "fatal";
  }
  return "error";
}

void Engine::report(Diagnostic diagnostic) {
  diagnostics_.push_back(std::move(diagnostic));
}

void Engine::note(std::string code, std::string message, SourceSpan span) {
  report({Severity::note, std::move(code), std::move(message), std::move(span), {}});
}

void Engine::warning(std::string code, std::string message, SourceSpan span) {
  report(
      {Severity::warning, std::move(code), std::move(message), std::move(span), {}});
}

void Engine::error(std::string code, std::string message, SourceSpan span) {
  report({Severity::error, std::move(code), std::move(message), std::move(span), {}});
}

void Engine::fatal(std::string code, std::string message, SourceSpan span) {
  report({Severity::fatal, std::move(code), std::move(message), std::move(span), {}});
}

bool Engine::has_error() const noexcept {
  return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const auto& value) {
    return value.severity == Severity::error || value.severity == Severity::fatal;
  });
}

bool Engine::empty() const noexcept {
  return diagnostics_.empty();
}

const std::vector<Diagnostic>& Engine::diagnostics() const noexcept {
  return diagnostics_;
}

void Engine::clear() noexcept {
  diagnostics_.clear();
}

void print_text(std::ostream& output, const Diagnostic& diagnostic) {
  if (!diagnostic.span.path.empty()) {
    output << diagnostic.span.path << ':' << diagnostic.span.begin.line << ':'
           << diagnostic.span.begin.column << ": ";
  }
  output << to_string(diagnostic.severity);
  if (!diagnostic.code.empty()) {
    output << '[' << diagnostic.code << ']';
  }
  output << ": " << diagnostic.message << '\n';

  for (const auto& note : diagnostic.notes) {
    if (!note.span.path.empty()) {
      output << note.span.path << ':' << note.span.begin.line << ':'
             << note.span.begin.column << ": ";
    }
    output << "note: " << note.message << '\n';
  }
}

void print_text(std::ostream& output, const Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    print_text(output, diagnostic);
  }
}

void print_json(std::ostream& output, const Diagnostic& diagnostic) {
  output << "{\"severity\":";
  print_json_string(output, to_string(diagnostic.severity));
  output << ",\"code\":";
  print_json_string(output, diagnostic.code);
  output << ",\"message\":";
  print_json_string(output, diagnostic.message);
  output << ",\"span\":";
  print_json_span(output, diagnostic.span);
  output << ",\"notes\":[";
  for (std::size_t index = 0; index < diagnostic.notes.size(); ++index) {
    if (index != 0) {
      output.put(',');
    }
    output << "{\"message\":";
    print_json_string(output, diagnostic.notes[index].message);
    output << ",\"span\":";
    print_json_span(output, diagnostic.notes[index].span);
    output.put('}');
  }
  output << "]}";
}

void print_json(std::ostream& output, const Engine& diagnostics) {
  output.put('[');
  for (std::size_t index = 0; index < diagnostics.diagnostics().size(); ++index) {
    if (index != 0) {
      output.put(',');
    }
    print_json(output, diagnostics.diagnostics()[index]);
  }
  output << "]\n";
}

}  // namespace fsim::diagnostic
