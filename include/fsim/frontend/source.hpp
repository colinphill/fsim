// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::frontend {

struct SourceLocation {
  std::size_t offset{};
  std::size_t line{1};
  std::size_t column{1};

  friend constexpr bool operator==(const SourceLocation&,
                                   const SourceLocation&) = default;
};

struct SourceSpan {
  std::string source_name;
  SourceLocation begin;
  SourceLocation end;
  // Physical input identity retained when a source-language directive
  // changes the logical diagnostic/debug name.
  std::string physical_source_name;

  [[nodiscard]] bool empty() const noexcept {
    return begin.offset == end.offset;
  }

  friend bool operator==(const SourceSpan&, const SourceSpan&) = default;
};

[[nodiscard]] SourceSpan cover(const SourceSpan& first,
                               const SourceSpan& last);
[[nodiscard]] std::string_view physical_source(
    const SourceSpan& span) noexcept;

struct SourceText {
  std::string name;
  std::string text;

  SourceText() = default;
  SourceText(std::string source_name, std::string source_text)
      : name(std::move(source_name)), text(std::move(source_text)) {}

  [[nodiscard]] std::string_view view() const noexcept { return text; }
};

}  // namespace fsim::frontend
