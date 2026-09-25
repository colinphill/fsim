// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::diagnostic {

enum class Severity : std::uint8_t {
  note,
  warning,
  error,
  fatal,
};

enum class ColorMode : std::uint8_t {
  automatic,
  always,
  never,
};

enum class SeverityColor : std::uint8_t {
  cyan,
  yellow,
  red,
  bold_red,
};

/// Stable severity style mapping for text and interactive renderers.
[[nodiscard]] constexpr SeverityColor severity_color(
    const Severity severity) noexcept
{
  switch (severity) {
    case Severity::note:
      return SeverityColor::cyan;
    case Severity::warning:
      return SeverityColor::yellow;
    case Severity::error:
      return SeverityColor::red;
    case Severity::fatal:
      return SeverityColor::bold_red;
  }
  return SeverityColor::red;
}

/// Resolve automatic mode from terminal status and the NO_COLOR policy.
[[nodiscard]] constexpr bool color_enabled(
    const ColorMode mode,
    const bool is_terminal,
    const bool no_color) noexcept
{
  switch (mode) {
    case ColorMode::automatic:
      return is_terminal && !no_color;
    case ColorMode::always:
      return true;
    case ColorMode::never:
      return false;
  }
  return false;
}

struct SourcePosition {
  std::uint32_t line{1};
  std::uint32_t column{1};
  std::uint64_t offset{0};
};

struct SourceSpan {
  std::string path;
  SourcePosition begin{};
  SourcePosition end{};
};

struct Note {
  std::string message;
  SourceSpan span{};
};

struct Diagnostic {
  Severity severity{Severity::error};
  std::string code;
  std::string message;
  SourceSpan span{};
  std::vector<Note> notes;
};

[[nodiscard]] std::string_view to_string(Severity severity) noexcept;

class Engine {
 public:
  using ReportObserver = std::function<void(const Diagnostic&)>;

  void report(Diagnostic diagnostic);

  /// Observe newly reported diagnostics without changing their stored form.
  void set_report_observer(ReportObserver observer);

  void note(std::string code, std::string message, SourceSpan span = {});
  void warning(std::string code, std::string message, SourceSpan span = {});
  void error(std::string code, std::string message, SourceSpan span = {});
  void fatal(std::string code, std::string message, SourceSpan span = {});

  [[nodiscard]] bool has_error() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return diagnostics_.empty(); }
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept
  {
    return diagnostics_;
  }
  void clear() noexcept;

 private:
  std::vector<Diagnostic> diagnostics_;
  ReportObserver report_observer_;
};

void print_text(std::ostream& output, const Diagnostic& diagnostic);
void print_text(
    std::ostream& output,
    const Diagnostic& diagnostic,
    bool color_enabled);
void print_text(std::ostream& output, const Engine& diagnostics);
void print_text(
    std::ostream& output,
    const Engine& diagnostics,
    bool color_enabled);
void print_json(std::ostream& output, const Diagnostic& diagnostic);
void print_json(std::ostream& output, const Engine& diagnostics);

}  // namespace fsim::diagnostic
