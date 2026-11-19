// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"
#include "fsim/support/expression_marker.hpp"
#include "fsim/support/systemverilog_builtin_type.hpp"
#include "fsim/support/symbol.hpp"

#include <cstdint>
#include <optional>
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
    SystemVerilog2023,
};

// Identifies text whose spelling depends on the source name selected by the
// SystemVerilog preprocessor. The derived form is used when stringification or
// token concatenation transforms a direct `__FILE__` expansion.
enum class GeneratedTextKind : std::uint8_t {
  none,
  systemverilog_file_macro,
  systemverilog_file_macro_derived,
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
    case StandardRevision::SystemVerilog2023:
        return "systemverilog-2023";
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
    case StandardRevision::SystemVerilog2023:
        return "2023";
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
  AbsoluteTolerance,
  RelativeTolerance,
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
  GeneratedTextKind generated_text { GeneratedTextKind::none };
  // The SystemVerilog parser's reading of a data-type keyword (IEEE 1800-2023
  // 5.6.2) on tokens it keeps as source text (DPI prototypes): the built-in
  // type it names, or another type keyword; `none` on every other token.
  support::SystemVerilogBuiltinType systemverilog_builtin {
      support::SystemVerilogBuiltinType::none };
  support::SystemVerilogTypeWord systemverilog_type_word {
      support::SystemVerilogTypeWord::none };
  // The kind of node a generated name token stands for (a monitor call, a
  // placeholder action) and its payload; `none` on every other token. The
  // token's text is then only a display name.
  support::ExpressionMarker marker { support::ExpressionMarker::none };
  std::string marker_argument { };
  // The interned canonical form of an Identifier token (support/symbol.hpp),
  // set by the lexer and by whatever rewrites an identifier's text; invalid on
  // other tokens. Left out of equality: it is derived from `text`.
  support::SymbolId symbol { };

  [[nodiscard]] friend bool operator==(const Token& left, const Token& right)
  {
    return left.kind == right.kind && left.text == right.text
        && left.span == right.span
        && left.expansion_stack == right.expansion_stack
        && left.generated_text == right.generated_text
        && left.systemverilog_builtin == right.systemverilog_builtin
        && left.systemverilog_type_word == right.systemverilog_type_word
        && left.marker == right.marker
        && left.marker_argument == right.marker_argument;
  }
};

[[nodiscard]] std::string systemverilog_string_literal_spelling(
    std::string_view value);
[[nodiscard]] std::optional<std::string>
decode_systemverilog_string_literal(std::string_view spelling);

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
