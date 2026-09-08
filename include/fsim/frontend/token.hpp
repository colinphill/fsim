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

enum class VhdlStandard {
    Vhdl1987,
    Vhdl1993,
    Vhdl2000,
    Vhdl2002,
    Vhdl2008,
    Vhdl2019,
};

enum class StandardRevision {
    Vhdl1987,
    Vhdl1993,
    Vhdl2000,
    Vhdl2002,
    Vhdl2008,
    Verilog1995,
    Verilog2001,
    Verilog2001NoConfig,
    Verilog2005,
    SystemVerilog2005,
    SystemVerilog2009,
    SystemVerilog2012,
    SystemVerilog2017,
    // Appended to preserve the serialized numeric identities of every
    // retained v3 standard revision.
    Vhdl2019,
};

[[nodiscard]] constexpr std::string_view to_string(
    const StandardRevision standard) noexcept
{
    switch (standard) {
    case StandardRevision::Vhdl1987:
        return "vhdl-1987";
    case StandardRevision::Vhdl1993:
        return "vhdl-1993";
    case StandardRevision::Vhdl2000:
        return "vhdl-2000";
    case StandardRevision::Vhdl2002:
        return "vhdl-2002";
    case StandardRevision::Vhdl2008:
        return "vhdl-2008";
    case StandardRevision::Vhdl2019:
        return "vhdl-2019";
    case StandardRevision::Verilog1995:
        return "verilog-1995";
    case StandardRevision::Verilog2001:
        return "verilog-2001";
    case StandardRevision::Verilog2001NoConfig:
        return "verilog-2001-noconfig";
    case StandardRevision::Verilog2005:
        return "verilog-2005";
    case StandardRevision::SystemVerilog2005:
        return "systemverilog-2005";
    case StandardRevision::SystemVerilog2009:
        return "systemverilog-2009";
    case StandardRevision::SystemVerilog2012:
        return "systemverilog-2012";
    case StandardRevision::SystemVerilog2017:
        return "systemverilog-2017";
    }
    return "systemverilog-2017";
}

[[nodiscard]] constexpr std::string_view revision_string(
    const StandardRevision standard) noexcept
{
    switch (standard) {
    case StandardRevision::Vhdl1987:
        return "1987";
    case StandardRevision::Vhdl1993:
        return "1993";
    case StandardRevision::Vhdl2000:
        return "2000";
    case StandardRevision::Vhdl2002:
        return "2002";
    case StandardRevision::Vhdl2008:
        return "2008";
    case StandardRevision::Vhdl2019:
        return "2019";
    case StandardRevision::Verilog1995:
        return "1995";
    case StandardRevision::Verilog2001:
        return "2001";
    case StandardRevision::Verilog2001NoConfig:
        return "2001-noconfig";
    case StandardRevision::Verilog2005:
    case StandardRevision::SystemVerilog2005:
        return "2005";
    case StandardRevision::SystemVerilog2009:
        return "2009";
    case StandardRevision::SystemVerilog2012:
        return "2012";
    case StandardRevision::SystemVerilog2017:
        return "2017";
    }
    return "2017";
}

[[nodiscard]] constexpr std::string_view to_string(
    const VhdlStandard standard) noexcept
{
    switch (standard) {
    case VhdlStandard::Vhdl1987:
        return "1987";
    case VhdlStandard::Vhdl1993:
        return "1993";
    case VhdlStandard::Vhdl2000:
        return "2000";
    case VhdlStandard::Vhdl2002:
        return "2002";
    case VhdlStandard::Vhdl2008:
        return "2008";
    case VhdlStandard::Vhdl2019:
        return "2019";
    }
    return "2008";
}

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

[[nodiscard]] LexResult lex(
    SourceText source, Language language,
    VhdlStandard vhdl_standard = VhdlStandard::Vhdl2008);
[[nodiscard]] bool is_vhdl_reserved_word(
    std::string_view word, VhdlStandard standard);
[[nodiscard]] const char* to_string(TokenKind kind) noexcept;

}  // namespace fsim::frontend
