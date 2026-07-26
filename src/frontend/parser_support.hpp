// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/token.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend::detail {

inline std::string ascii_lower(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    result.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

inline bool iequals(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(left[index])) !=
        std::tolower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

inline std::string vhdl_name(std::string_view raw) {
  if (raw.size() >= 2 && raw.front() == '\\' && raw.back() == '\\') {
    return std::string(raw.substr(1, raw.size() - 2));
  }
  return ascii_lower(raw);
}

inline std::optional<std::uint64_t> decimal_u64(std::string_view raw) {
  std::string cleaned;
  cleaned.reserve(raw.size());
  for (const char character : raw) {
    if (character != '_') {
      cleaned.push_back(character);
    }
  }
  std::uint64_t value{};
  const auto [end, error] =
      std::from_chars(cleaned.data(), cleaned.data() + cleaned.size(), value);
  if (error != std::errc{} || end != cleaned.data() + cleaned.size()) {
    return std::nullopt;
  }
  return value;
}

inline std::optional<std::int64_t> decimal_i64(std::string_view raw,
                                               bool negative = false) {
  const auto unsigned_value = decimal_u64(raw);
  if (!unsigned_value) {
    return std::nullopt;
  }
  if (negative) {
    constexpr auto minimum_magnitude =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
        1U;
    if (*unsigned_value > minimum_magnitude) {
      return std::nullopt;
    }
    if (*unsigned_value == minimum_magnitude) {
      return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(*unsigned_value);
  }
  if (*unsigned_value >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(*unsigned_value);
}

class ParserBase {
 protected:
  ParserBase(std::vector<Token> tokens, std::vector<Diagnostic> diagnostics)
      : tokens_(std::move(tokens)), diagnostics_(std::move(diagnostics)) {}

  [[nodiscard]] const Token& current(std::size_t lookahead = 0) const {
    const auto target = std::min(index_ + lookahead, tokens_.size() - 1);
    return tokens_[target];
  }

  [[nodiscard]] const Token& previous() const {
    return tokens_[index_ == 0 ? 0 : index_ - 1];
  }

  [[nodiscard]] bool at_end() const {
    return current().kind == TokenKind::EndOfFile;
  }

  [[nodiscard]] bool at(TokenKind kind,
                        std::size_t lookahead = 0) const {
    return current(lookahead).kind == kind;
  }

  [[nodiscard]] bool keyword(std::string_view text,
                             std::size_t lookahead = 0,
                             bool case_insensitive = false) const {
    const auto& token = current(lookahead);
    if (token.kind != TokenKind::Identifier) {
      return false;
    }
    return case_insensitive ? iequals(token.text, text) : token.text == text;
  }

  [[nodiscard]] bool any_keyword(
      std::initializer_list<std::string_view> words,
      bool case_insensitive = false) const {
    return std::any_of(words.begin(), words.end(), [&](std::string_view word) {
      return keyword(word, 0, case_insensitive);
    });
  }

  Token advance() {
    const Token token = current();
    if (!at_end()) {
      ++index_;
    }
    return token;
  }

  bool match(TokenKind kind) {
    if (!at(kind)) {
      return false;
    }
    advance();
    return true;
  }

  bool match_keyword(std::string_view text,
                     bool case_insensitive = false) {
    if (!keyword(text, 0, case_insensitive)) {
      return false;
    }
    advance();
    return true;
  }

  Token expect(TokenKind kind, std::string_view description,
               std::string code = "FSIM-FE-PARSE-001") {
    if (at(kind)) {
      return advance();
    }
    error(current(), std::move(code),
          "expected " + std::string(description) + ", found " +
              to_string(current().kind));
    return current();
  }

  Token expect_keyword(std::string_view word, bool case_insensitive,
                       std::string code = "FSIM-FE-PARSE-001") {
    if (keyword(word, 0, case_insensitive)) {
      return advance();
    }
    error(current(), std::move(code),
          "expected '" + std::string(word) + "'");
    return current();
  }

  void error(const Token& token, std::string code, std::string message) {
    diagnostics_.push_back(Diagnostic{DiagnosticSeverity::Error,
                                      std::move(code), std::move(message),
                                      token.span, {}});
  }

  void warning(const Token& token, std::string code, std::string message) {
    diagnostics_.push_back(Diagnostic{DiagnosticSeverity::Warning,
                                      std::move(code), std::move(message),
                                      token.span, {}});
  }

  void skip_to_semicolon() {
    while (!at_end() && !at(TokenKind::Semicolon)) {
      advance();
    }
    match(TokenKind::Semicolon);
  }

  void skip_balanced(TokenKind open, TokenKind close) {
    if (!match(open)) {
      return;
    }
    std::size_t depth = 1;
    while (!at_end() && depth != 0) {
      if (match(open)) {
        ++depth;
      } else if (match(close)) {
        --depth;
      } else {
        advance();
      }
    }
  }

  [[nodiscard]] std::size_t position() const noexcept { return index_; }
  void rewind(std::size_t position) noexcept { index_ = position; }

  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t index_{};
};

}  // namespace fsim::frontend::detail
