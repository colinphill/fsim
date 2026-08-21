// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/support/rare_vector.hpp"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

/// Immutable interned source identity. Source spans are copied throughout
/// parsing and specialization, while each compilation contains only a small
/// number of distinct logical and physical source names.
class SourceName {
public:
  SourceName() = default;
  SourceName(std::string value);
  SourceName(std::string_view value);
  SourceName(const char* value);

  SourceName& operator=(std::string value);
  SourceName& operator=(std::string_view value);
  SourceName& operator=(const char* value);

  [[nodiscard]] const std::string& str() const noexcept;
  [[nodiscard]] const char* c_str() const noexcept { return str().c_str(); }
  [[nodiscard]] bool empty() const noexcept { return str().empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return str().size(); }
  [[nodiscard]] auto begin() const noexcept { return str().begin(); }
  [[nodiscard]] auto end() const noexcept { return str().end(); }
  [[nodiscard]] auto find(
      const std::string_view value,
      const std::size_t offset = 0) const noexcept {
    return str().find(value, offset);
  }
  [[nodiscard]] bool starts_with(const std::string_view value) const noexcept {
    return str().starts_with(value);
  }
  [[nodiscard]] bool ends_with(const std::string_view value) const noexcept {
    return str().ends_with(value);
  }
  void clear() noexcept { value_.reset(); }

  [[nodiscard]] operator std::string_view() const noexcept { return str(); }
  [[nodiscard]] operator std::string() const { return str(); }

  friend std::ostream& operator<<(std::ostream& output,
                                  const SourceName& value) {
    return output << value.str();
  }
  friend bool operator==(const SourceName& left,
                         const SourceName& right) noexcept {
    return left.value_ == right.value_ || left.str() == right.str();
  }
  friend bool operator==(const SourceName& left,
                         const std::string_view right) noexcept {
    return left.str() == right;
  }
  friend bool operator==(const SourceName& left,
                         const std::string& right) noexcept {
    return left.str() == right;
  }
  friend bool operator==(const SourceName& left, const char* right) noexcept {
    return left.str() == right;
  }
  friend bool operator==(const std::string_view left,
                         const SourceName& right) noexcept {
    return left == right.str();
  }
  friend bool operator==(const std::string& left,
                         const SourceName& right) noexcept {
    return left == right.str();
  }
  friend bool operator==(const char* left, const SourceName& right) noexcept {
    return left == right.str();
  }
  friend std::string operator+(const SourceName& left,
                               const std::string_view right) {
    return left.str() + std::string { right };
  }
  friend std::string operator+(const SourceName& left, const char right) {
    auto result = left.str();
    result.push_back(right);
    return result;
  }
  friend std::string operator+(const std::string_view left,
                               const SourceName& right) {
    return std::string { left } + right.str();
  }

private:
  static std::shared_ptr<const std::string> intern(std::string value);
  std::shared_ptr<const std::string> value_;
};

struct SourceLocation {
  std::size_t offset{};
  std::size_t line{1};
  std::size_t column{1};

  friend constexpr bool operator==(const SourceLocation&,
                                   const SourceLocation&) = default;
};

struct SourceSpan {
  SourceName source_name;
  SourceLocation begin;
  SourceLocation end;
  // Physical input identity retained when a source-language directive
  // changes the logical diagnostic/debug name.
  SourceName physical_source_name;
  // Ordered outermost-to-innermost preprocessor expansion descriptions.
  // Parsed nodes own this copy so semantic provenance survives after the
  // preprocessor token stream is released.
  support::RareVector<std::string> expansion_stack;

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
