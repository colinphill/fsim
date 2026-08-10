// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

enum class Language {
  Vhdl2008,
  Verilog2005,
  SystemVerilog2017,
};

enum class TokenKind {
  Identifier,
  Number,
  StringLiteral,
  CharacterLiteral,

  LeftParen,
  RightParen,
  LeftBracket,
  RightBracket,
  LeftBrace,
  RightBrace,
  Colon,
  Semicolon,
  Comma,
  Dot,
  Apostrophe,
  Hash,
  At,
  Question,
  Backtick,
  // VHDL line-comment embedding marker (`-- psl`). The following tokens on
  // that physical line use the ordinary VHDL/PSL lexical surface.
  PslDirective,

  Assign,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  EqualEqual,
  CaseEqual,
  WildcardEqual,
  NotEqual,
  CaseNotEqual,
  WildcardNotEqual,
  Arrow,
  ThinArrow,
  ColonEqual,
  Scope,
  ShiftLeft,
  ShiftLeftAssign,
  ShiftRight,
  ShiftRightAssign,
  ArithmeticShiftLeft,
  ArithmeticShiftLeftAssign,
  ArithmeticShiftRight,
  ArithmeticShiftRightAssign,
  AndAndAnd,
  AndAnd,
  OrOr,
  Plus,
  PlusColon,
  PlusPlus,
  PlusAssign,
  Minus,
  MinusColon,
  MinusMinus,
  MinusAssign,
  Star,
  StarAssign,
  Power,
  Slash,
  SlashAssign,
  Percent,
  PercentAssign,
  Ampersand,
  AmpersandAssign,
  Pipe,
  PipeAssign,
  Caret,
  CaretAssign,
  TildeAmpersand,
  TildePipe,
  TildeCaret,
  CaretTilde,
  Tilde,
  Bang,

  EndOfFile,
};

struct Token {
  TokenKind kind{TokenKind::EndOfFile};
  std::string text;
  SourceSpan span;
  // Ordered outermost-to-innermost macro expansion descriptions. Tokens read
  // directly from a source file leave this empty.
  std::vector<std::string> expansion_stack;

  friend bool operator==(const Token&, const Token&) = default;
};

struct LexResult {
  std::vector<Token> tokens;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool ok() const { return !has_errors(diagnostics); }
};

[[nodiscard]] LexResult lex(SourceText source, Language language);
[[nodiscard]] const char* to_string(TokenKind kind) noexcept;

}  // namespace fsim::frontend
