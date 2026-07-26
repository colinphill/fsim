// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
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
  void report(Diagnostic diagnostic);

  void note(std::string code, std::string message, SourceSpan span = {});
  void warning(std::string code, std::string message, SourceSpan span = {});
  void error(std::string code, std::string message, SourceSpan span = {});
  void fatal(std::string code, std::string message, SourceSpan span = {});

  [[nodiscard]] bool has_error() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;
  void clear() noexcept;

 private:
  std::vector<Diagnostic> diagnostics_;
};

void print_text(std::ostream& output, const Diagnostic& diagnostic);
void print_text(std::ostream& output, const Engine& diagnostics);
void print_json(std::ostream& output, const Diagnostic& diagnostic);
void print_json(std::ostream& output, const Engine& diagnostics);

}  // namespace fsim::diagnostic
