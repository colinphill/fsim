// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/token.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend::detail {

[[nodiscard]] std::string ascii_lower(std::string_view value);
[[nodiscard]] bool iequals(std::string_view left, std::string_view right);
[[nodiscard]] std::string vhdl_name(std::string_view raw);
[[nodiscard]] std::optional<std::uint64_t> decimal_u64(
    std::string_view raw);
[[nodiscard]] std::optional<std::int64_t> decimal_i64(
    std::string_view raw, bool negative = false);

class ParserBase {
 protected:
  ParserBase(std::vector<Token> tokens, std::vector<Diagnostic> diagnostics);

  [[nodiscard]] const Token& current(std::size_t lookahead = 0) const;
  [[nodiscard]] const Token& previous() const;
  [[nodiscard]] bool at_end() const;
  [[nodiscard]] bool at(TokenKind kind, std::size_t lookahead = 0) const;
  [[nodiscard]] bool keyword(std::string_view text,
                             std::size_t lookahead = 0,
                             bool case_insensitive = false) const;

  [[nodiscard]] bool any_keyword(
      std::initializer_list<std::string_view> words,
      bool case_insensitive = false) const;

  Token advance();
  bool match(TokenKind kind);

  bool match_keyword(std::string_view text,
                     bool case_insensitive = false);

  Token expect(TokenKind kind, std::string_view description,
               std::string code = "FSIM-FE-PARSE-001");

  Token expect_keyword(std::string_view word, bool case_insensitive,
                       std::string code = "FSIM-FE-PARSE-001");

  void error(const Token& token, std::string code, std::string message);
  void warning(const Token& token, std::string code, std::string message);
  void skip_to_semicolon();
  void skip_balanced(TokenKind open, TokenKind close);

  [[nodiscard]] std::size_t position() const noexcept;
  void rewind(std::size_t position) noexcept;

  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t index_{};
};

}  // namespace fsim::frontend::detail
