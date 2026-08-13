// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/token.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::frontend {
namespace {

    template <std::size_t Size>
    [[nodiscard]] bool contains(
        const std::array<std::string_view, Size>& words,
        const std::string_view word) noexcept
    {
        return std::ranges::find(words, word) != words.end();
    }

    [[nodiscard]] std::string ascii_lower(std::string_view text)
    {
        std::string result { text };
        std::ranges::transform(result, result.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return result;
    }

    [[nodiscard]] bool vhdl_reserved_word_impl(
        const std::string_view word, const VhdlStandard standard) noexcept
    {
        static constexpr auto vhdl_1987 = std::to_array<std::string_view>({ "abs", "access", "after", "alias", "all", "and", "architecture",
            "array", "assert", "attribute", "begin", "block", "body", "buffer",
            "bus", "case", "component", "configuration", "constant",
            "disconnect", "downto", "else", "elsif", "end", "entity", "exit",
            "file", "for", "function", "generate", "generic", "guarded", "if",
            "in", "inout", "is", "label", "library", "linkage", "loop", "map",
            "mod", "nand", "new", "next", "nor", "not", "null", "of", "on",
            "open", "or", "others", "out", "package", "port", "procedure",
            "process", "range", "record", "register", "rem", "report", "return",
            "select", "severity", "signal", "subtype", "then", "to", "transport",
            "type", "units", "until", "use", "variable", "wait", "when", "while",
            "with", "xor" });
        static constexpr auto vhdl_1993 = std::to_array<std::string_view>({ "group", "impure", "inertial", "literal", "postponed", "pure",
            "reject", "rol", "ror", "shared", "sla", "sll", "sra", "srl",
            "unaffected", "xnor" });
        static constexpr auto vhdl_2008 = std::to_array<std::string_view>({ "assume", "assume_guarantee", "context", "cover", "default",
            "fairness", "force", "parameter", "property", "release", "restrict",
            "restrict_guarantee", "sequence", "strong", "vmode", "vprop",
            "vunit" });
        if (contains(vhdl_1987, word)) {
            return true;
        }
        if (standard != VhdlStandard::Vhdl1987 && contains(vhdl_1993, word)) {
            return true;
        }
        if (standard != VhdlStandard::Vhdl1987 && standard != VhdlStandard::Vhdl1993 && word == "protected") {
            return true;
        }
        return standard == VhdlStandard::Vhdl2008 && contains(vhdl_2008, word);
    }

    [[nodiscard]] bool vhdl_2008_bit_string_specifier(
        const std::string_view word) noexcept
    {
        static constexpr auto specifiers = std::to_array<std::string_view>({ "d", "ub", "uo", "ux", "sb", "so", "sx", "ud", "sd" });
        return contains(specifiers, word);
    }

class Lexer {
 public:
     Lexer(SourceText source, Language language, VhdlStandard vhdl_standard)
         : source_(std::move(source))
         , language_(language)
         , vhdl_standard_(vhdl_standard)
     {
     }

  LexResult run() {
    skip_utf8_bom();
    while (!at_end()) {
      skip_trivia();
      if (at_end()) {
        break;
      }
      lex_one();
    }
    const auto location = current_location();
    result_.tokens.push_back(
        Token{TokenKind::EndOfFile, {}, span(location, location), {}});
    return std::move(result_);
  }

 private:
  void skip_utf8_bom() noexcept {
    if (source_.text.size() < 3U) {
      return;
    }
    const auto first = static_cast<unsigned char>(source_.text[0]);
    const auto second = static_cast<unsigned char>(source_.text[1]);
    const auto third = static_cast<unsigned char>(source_.text[2]);
    if (first == 0xefU && second == 0xbbU && third == 0xbfU) {
      index_ = 3U;
    }
  }

  [[nodiscard]] bool is_vhdl() const noexcept {
    return language_ == Language::Vhdl2008;
  }

  [[nodiscard]] bool at_end(std::size_t lookahead = 0) const noexcept {
    return index_ + lookahead >= source_.text.size();
  }

  [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept {
    return at_end(lookahead) ? '\0' : source_.text[index_ + lookahead];
  }

  [[nodiscard]] SourceLocation current_location() const noexcept {
    return SourceLocation{index_, line_, column_};
  }

  [[nodiscard]] SourceSpan span(SourceLocation begin,
                                SourceLocation end) const {
    return SourceSpan{source_.name, begin, end, source_.name, {}};
  }

  char advance() {
    const char character = source_.text[index_++];
    if (character == '\r') {
      if (!at_end() && peek() == '\n') {
        ++index_;
      }
      ++line_;
      column_ = 1;
    } else if (character == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return character;
  }

  bool consume_if(char expected) {
    if (peek() != expected) {
      return false;
    }
    advance();
    return true;
  }

  void emit(TokenKind kind, SourceLocation begin) {
    const auto end = current_location();
    result_.tokens.push_back(Token{
        kind,
        source_.text.substr(begin.offset, end.offset - begin.offset),
        span(begin, end),
        {},
    });
  }

  void diagnose(std::string code, std::string message, SourceLocation begin) {
    result_.diagnostics.push_back(
        Diagnostic{DiagnosticSeverity::Error, std::move(code),
                   std::move(message), span(begin, current_location()), {}});
  }

  void diagnose_revision_feature(
      const std::string_view feature, const VhdlStandard required,
      const SourceLocation begin)
  {
      diagnose(
          "FSIM-VHDL-LEX-001",
          std::string(feature) + " requires VHDL-" + std::string(to_string(required)) + "; select that or a later "
                                                                                        "revision, or rewrite the lexical form for VHDL-"
              + std::string(to_string(vhdl_standard_)),
          begin);
  }

  [[nodiscard]] bool vhdl_psl_comment() const noexcept {
    if (!is_vhdl() || peek() != '-' || peek(1) != '-') {
      return false;
    }
    std::size_t offset = 2;
    while (peek(offset) == ' ' || peek(offset) == '\t') {
      ++offset;
    }
    const auto lower = [](const char value) {
      return static_cast<char>(
          std::tolower(static_cast<unsigned char>(value)));
    };
    if (lower(peek(offset)) != 'p' || lower(peek(offset + 1)) != 's'
        || lower(peek(offset + 2)) != 'l') {
      return false;
    }
    const auto following = peek(offset + 3);
    return following == '\0' || following == ' ' || following == '\t'
        || following == '\r' || following == '\n';
  }

  void lex_vhdl_psl_directive() {
    const auto begin = current_location();
    advance();
    advance();
    while (peek() == ' ' || peek() == '\t') {
      advance();
    }
    advance();
    advance();
    advance();
    emit(TokenKind::PslDirective, begin);
  }

  void skip_trivia() {
    for (;;) {
      while (std::isspace(static_cast<unsigned char>(peek()))) {
        advance();
      }
      if (is_vhdl() && peek() == '-' && peek(1) == '-') {
        if (vhdl_psl_comment()) {
          return;
        }
        while (!at_end() && peek() != '\n' && peek() != '\r') {
          advance();
        }
        continue;
      }
      if (!is_vhdl() && peek() == '/' && peek(1) == '/') {
        while (!at_end() && peek() != '\n' && peek() != '\r') {
          // UVM and other established SystemVerilog libraries place a macro
          // continuation after an end-of-line comment. Preserve that marker
          // as a token while still ending the comment at the physical line.
          if (peek() == '\\'
              && (peek(1) == '\n' || peek(1) == '\r')) {
            return;
          }
          advance();
        }
        continue;
      }
      if (peek() == '/' && peek(1) == '*') {
        const auto begin = current_location();
        if (is_vhdl() && vhdl_standard_ != VhdlStandard::Vhdl2008) {
            diagnose_revision_feature(
                "a delimited block comment", VhdlStandard::Vhdl2008, begin);
        }
        advance();
        advance();
        std::size_t depth = 1;
        bool nesting_reported = false;
        while (!at_end() && depth != 0) {
          if (is_vhdl() && peek() == '/' && peek(1) == '*') {
            advance();
            advance();
            ++depth;
            if (depth > 64 && !nesting_reported) {
              diagnose(
                  "FSIM-FE-LEX-007",
                  "VHDL block-comment nesting exceeds 64 levels",
                  begin);
              nesting_reported = true;
            }
            continue;
          }
          if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            --depth;
            continue;
          }
          advance();
        }
        if (depth != 0) {
          diagnose("FSIM-FE-LEX-002", "unterminated block comment", begin);
          return;
        }
        continue;
      }
      return;
    }
  }

  static bool identifier_start(char character, bool verilog) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalpha(value) || character == '_' ||
           (verilog && character == '$');
  }

  static bool identifier_continue(char character, bool verilog) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '_' ||
           (verilog && character == '$');
  }

  void lex_identifier() {
    const auto begin = current_location();
    advance();
    while (identifier_continue(peek(), !is_vhdl())) {
      advance();
    }
    if (is_vhdl()) {
      const auto text = source_.text.substr(
          begin.offset, current_location().offset - begin.offset);
      if (text.front() == '_' || text.back() == '_'
          || text.find("__") != std::string::npos) {
        diagnose(
            "FSIM-FE-LEX-005",
            "a VHDL basic identifier must start with a letter and cannot "
            "contain adjacent or trailing underscores",
            begin);
      }
      const auto lower = ascii_lower(text);
      const bool follows_explicit_width = begin.offset != 0U && std::isdigit(static_cast<unsigned char>(source_.text[begin.offset - 1U]));
      if (vhdl_standard_ != VhdlStandard::Vhdl2008 && peek() == '"' && vhdl_2008_bit_string_specifier(lower) && !follows_explicit_width) {
          diagnose_revision_feature(
              "a D, signed, or unsigned bit-string base specifier",
              VhdlStandard::Vhdl2008, begin);
      }
    }
    emit(TokenKind::Identifier, begin);
  }

  void lex_extended_identifier() {
    const auto begin = current_location();
    advance();
    const bool unavailable_in_revision = is_vhdl() && vhdl_standard_ == VhdlStandard::Vhdl1987;
    if (!is_vhdl() && (peek() == '\n' || peek() == '\r')) {
      advance();
      emit(TokenKind::Identifier, begin);
      return;
    }
    bool content = false;
    while (!at_end()
           && (!is_vhdl() || (peek() != '\n' && peek() != '\r'))) {
      if (!is_vhdl() &&
          std::isspace(static_cast<unsigned char>(peek()))) {
        break;
      }
      if (peek() == '\\') {
        if (is_vhdl() && peek(1) == '\\') {
          advance();
          advance();
          content = true;
          continue;
        }
        break;
      }
      advance();
      content = true;
    }
    if (is_vhdl()) {
      if (!consume_if('\\')) {
        diagnose("FSIM-FE-LEX-003", "unterminated extended identifier",
                 begin);
      } else if (!content) {
        diagnose(
            "FSIM-FE-LEX-006",
            "a VHDL extended identifier cannot be empty",
            begin);
      }
    }
    if (unavailable_in_revision) {
        diagnose_revision_feature(
            "an extended identifier", VhdlStandard::Vhdl1993, begin);
    }
    emit(TokenKind::Identifier, begin);
  }

  void lex_number() {
    const auto begin = current_location();

    if (peek() == '\'') {
      advance();
      if (peek() == 's' || peek() == 'S') {
        advance();
      }
      if (std::isalpha(static_cast<unsigned char>(peek())) ||
          std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
      }
      while (std::isalnum(static_cast<unsigned char>(peek())) ||
             peek() == '_' || peek() == '?' || peek() == 'x' ||
             peek() == 'X' || peek() == 'z' || peek() == 'Z') {
        advance();
      }
      emit(TokenKind::Number, begin);
      return;
    }

    while (std::isdigit(static_cast<unsigned char>(peek())) ||
           peek() == '_') {
      advance();
    }

    if (is_vhdl() && vhdl_standard_ != VhdlStandard::Vhdl2008) {
        std::size_t letters = 0;
        while (letters != 2U && std::isalpha(static_cast<unsigned char>(peek(letters)))) {
            ++letters;
        }
        if (letters != 0U && peek(letters) == '"') {
            const auto specifier = ascii_lower(source_.text.substr(
                current_location().offset, letters));
            if (specifier == "b" || specifier == "o" || specifier == "x" || vhdl_2008_bit_string_specifier(specifier)) {
                diagnose_revision_feature(
                    "an explicit bit-string length", VhdlStandard::Vhdl2008,
                    begin);
            }
        }
    }

    if (is_vhdl() && peek() == '#') {
      advance();
      while (!at_end() && peek() != '#') {
        advance();
      }
      if (consume_if('#') && (peek() == 'e' || peek() == 'E')) {
        advance();
        if (peek() == '+' || peek() == '-') {
          advance();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) ||
               peek() == '_') {
          advance();
        }
      }
    } else if (!is_vhdl() && peek() == '\'') {
      advance();
      if (peek() == 's' || peek() == 'S') {
        advance();
      }
      if (std::isalpha(static_cast<unsigned char>(peek())) ||
          std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
      }
      while (std::isalnum(static_cast<unsigned char>(peek())) ||
             peek() == '_' || peek() == '?' || peek() == 'x' ||
             peek() == 'X' || peek() == 'z' || peek() == 'Z') {
        advance();
      }
    } else {
      if (peek() == '.' &&
          std::isdigit(static_cast<unsigned char>(peek(1)))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek())) ||
               peek() == '_') {
          advance();
        }
      }
      if (peek() == 'e' || peek() == 'E') {
        advance();
        if (peek() == '+' || peek() == '-') {
          advance();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) ||
               peek() == '_') {
          advance();
        }
      }
    }
    emit(TokenKind::Number, begin);
  }

  void lex_string() {
    const auto begin = current_location();
    advance();
    bool terminated = false;
    while (!at_end()) {
      if (peek() == '"') {
        advance();
        if (is_vhdl() && peek() == '"') {
          advance();
          continue;
        }
        terminated = true;
        break;
      }
      if (!is_vhdl() && peek() == '\\') {
        advance();
        if (!at_end()) {
          advance();
        }
        continue;
      }
      if (peek() == '\n' || peek() == '\r') {
        break;
      }
      advance();
    }
    if (!terminated) {
      diagnose("FSIM-FE-LEX-004", "unterminated string literal", begin);
    }
    if (is_vhdl() && vhdl_standard_ != VhdlStandard::Vhdl2008 && terminated && !result_.tokens.empty()) {
        const auto& specifier_token = result_.tokens.back();
        const auto specifier = ascii_lower(specifier_token.text);
        if (specifier_token.kind == TokenKind::Identifier && specifier_token.span.end.offset == begin.offset && (specifier == "b" || specifier == "o" || specifier == "x")) {
            const auto digits = source_.text.substr(
                begin.offset + 1U,
                current_location().offset - begin.offset - 2U);
            const auto valid_digit = [&](const char raw) {
                const auto character = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(raw)));
                if (character == '_') {
                    return true;
                }
                if (specifier == "b") {
                    return character == '0' || character == '1';
                }
                if (specifier == "o") {
                    return character >= '0' && character <= '7';
                }
                return std::isdigit(static_cast<unsigned char>(character)) || (character >= 'a' && character <= 'f');
            };
            if (!std::ranges::all_of(digits, valid_digit)) {
                diagnose_revision_feature(
                    "non-numeric bit-string digits", VhdlStandard::Vhdl2008,
                    begin);
            }
        }
    }
    emit(TokenKind::StringLiteral, begin);
  }

  void lex_quote() {
    const auto begin = current_location();
    if (is_vhdl() && !at_end(2) && peek(2) == '\'') {
      advance();
      advance();
      advance();
      emit(TokenKind::CharacterLiteral, begin);
      return;
    }
    if (!is_vhdl() &&
        (std::isdigit(static_cast<unsigned char>(peek(1))) ||
         peek(1) == 'b' || peek(1) == 'B' ||
         peek(1) == 'o' || peek(1) == 'O' ||
         peek(1) == 'd' || peek(1) == 'D' ||
         peek(1) == 'h' || peek(1) == 'H' ||
         peek(1) == 's' || peek(1) == 'S' ||
         peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'z' ||
         peek(1) == 'Z')) {
      lex_number();
      return;
    }
    advance();
    emit(TokenKind::Apostrophe, begin);
  }

  void lex_one() {
    const auto begin = current_location();
    const char character = peek();
    if (vhdl_psl_comment()) {
      lex_vhdl_psl_directive();
      return;
    }
    if (identifier_start(character, !is_vhdl())) {
      lex_identifier();
      return;
    }
    if (character == '\\') {
      lex_extended_identifier();
      return;
    }
    if (std::isdigit(static_cast<unsigned char>(character))) {
      lex_number();
      return;
    }
    if (character == '"') {
      lex_string();
      return;
    }
    if (character == '\'') {
      lex_quote();
      return;
    }

    advance();
    switch (character) {
      case '(':
        emit(TokenKind::LeftParen, begin);
        return;
      case ')':
        emit(TokenKind::RightParen, begin);
        return;
      case '[':
        emit(TokenKind::LeftBracket, begin);
        return;
      case ']':
        emit(TokenKind::RightBracket, begin);
        return;
      case '{':
        emit(TokenKind::LeftBrace, begin);
        return;
      case '}':
        emit(TokenKind::RightBrace, begin);
        return;
      case ';':
        emit(TokenKind::Semicolon, begin);
        return;
      case ',':
        emit(TokenKind::Comma, begin);
        return;
      case '.':
        emit(TokenKind::Dot, begin);
        return;
      case '#':
        emit(TokenKind::Hash, begin);
        return;
      case '@':
        emit(TokenKind::At, begin);
        return;
      case '?':
          if (is_vhdl() && vhdl_standard_ != VhdlStandard::Vhdl2008) {
              diagnose_revision_feature(
                  "a question-mark delimiter", VhdlStandard::Vhdl2008, begin);
          }
        emit(TokenKind::Question, begin);
        return;
      case '`':
        emit(TokenKind::Backtick, begin);
        return;
      case ':':
        if (consume_if('=')) {
          emit(TokenKind::ColonEqual, begin);
        } else if (consume_if(':')) {
          emit(TokenKind::Scope, begin);
        } else {
          emit(TokenKind::Colon, begin);
        }
        return;
      case '=':
        if (consume_if('>')) {
          emit(TokenKind::Arrow, begin);
        } else if (consume_if('=')) {
          if (!is_vhdl() && consume_if('?')) {
            emit(TokenKind::WildcardEqual, begin);
          } else {
            emit(
                !is_vhdl() && consume_if('=')
                    ? TokenKind::CaseEqual
                    : TokenKind::EqualEqual,
                begin);
          }
        } else {
          emit(TokenKind::Assign, begin);
        }
        return;
      case '<':
        if (consume_if('=')) {
          emit(TokenKind::LessEqual, begin);
        } else if (consume_if('<')) {
            if (is_vhdl() && vhdl_standard_ != VhdlStandard::Vhdl2008) {
                diagnose_revision_feature(
                    "an external-name delimiter", VhdlStandard::Vhdl2008,
                    begin);
            }
          if (!is_vhdl() && consume_if('<')) {
            emit(
                consume_if('=')
                    ? TokenKind::ArithmeticShiftLeftAssign
                    : TokenKind::ArithmeticShiftLeft,
                begin);
          } else {
            emit(
                !is_vhdl() && consume_if('=')
                    ? TokenKind::ShiftLeftAssign
                    : TokenKind::ShiftLeft,
                begin);
          }
        } else {
          emit(TokenKind::Less, begin);
        }
        return;
      case '>':
        if (consume_if('=')) {
          emit(TokenKind::GreaterEqual, begin);
        } else if (consume_if('>')) {
            if (is_vhdl() && vhdl_standard_ != VhdlStandard::Vhdl2008) {
                diagnose_revision_feature(
                    "an external-name delimiter", VhdlStandard::Vhdl2008,
                    begin);
            }
          if (!is_vhdl() && consume_if('>')) {
            emit(
                consume_if('=')
                    ? TokenKind::ArithmeticShiftRightAssign
                    : TokenKind::ArithmeticShiftRight,
                begin);
          } else {
            emit(
                !is_vhdl() && consume_if('=')
                    ? TokenKind::ShiftRightAssign
                    : TokenKind::ShiftRight,
                begin);
          }
        } else {
          emit(TokenKind::Greater, begin);
        }
        return;
      case '!':
        if (consume_if('=')) {
          if (!is_vhdl() && consume_if('?')) {
            emit(TokenKind::WildcardNotEqual, begin);
          } else {
            emit(
                !is_vhdl() && consume_if('=')
                    ? TokenKind::CaseNotEqual
                    : TokenKind::NotEqual,
                begin);
          }
        } else {
          emit(TokenKind::Bang, begin);
        }
        return;
      case '&':
        if (consume_if('&')) {
          emit(
              !is_vhdl() && consume_if('&')
                  ? TokenKind::AndAndAnd
                  : TokenKind::AndAnd,
              begin);
        } else if (!is_vhdl() && consume_if('=')) {
          emit(TokenKind::AmpersandAssign, begin);
        } else {
          emit(TokenKind::Ampersand, begin);
        }
        return;
      case '|':
        if (consume_if('|')) {
          emit(TokenKind::OrOr, begin);
        } else if (!is_vhdl() && consume_if('=')) {
          emit(TokenKind::PipeAssign, begin);
        } else {
          emit(TokenKind::Pipe, begin);
        }
        return;
      case '+':
        if (!is_vhdl() && consume_if(':')) {
          emit(TokenKind::PlusColon, begin);
        } else if (!is_vhdl() && consume_if('+')) {
          emit(TokenKind::PlusPlus, begin);
        } else if (!is_vhdl() && consume_if('=')) {
          emit(TokenKind::PlusAssign, begin);
        } else {
          emit(TokenKind::Plus, begin);
        }
        return;
      case '-':
        if (!is_vhdl() && consume_if('>')) {
          emit(TokenKind::ThinArrow, begin);
        } else if (!is_vhdl() && consume_if(':')) {
          emit(TokenKind::MinusColon, begin);
        } else if (!is_vhdl() && consume_if('-')) {
          emit(TokenKind::MinusMinus, begin);
        } else if (!is_vhdl() && consume_if('=')) {
          emit(TokenKind::MinusAssign, begin);
        } else {
          emit(TokenKind::Minus, begin);
        }
        return;
      case '*':
        if (consume_if('*')) {
          emit(TokenKind::Power, begin);
        } else if (!is_vhdl() && consume_if('=')) {
          emit(TokenKind::StarAssign, begin);
        } else {
          emit(TokenKind::Star, begin);
        }
        return;
      case '/':
        if (consume_if('=')) {
          emit(
              is_vhdl() ? TokenKind::NotEqual
                        : TokenKind::SlashAssign,
              begin);
        } else {
          emit(TokenKind::Slash, begin);
        }
        return;
      case '%':
        emit(
            !is_vhdl() && consume_if('=')
                ? TokenKind::PercentAssign
                : TokenKind::Percent,
            begin);
        return;
      case '^':
        if (!is_vhdl() && consume_if('~')) {
          emit(TokenKind::CaretTilde, begin);
        } else if (!is_vhdl() && consume_if('=')) {
          emit(TokenKind::CaretAssign, begin);
        } else {
          emit(TokenKind::Caret, begin);
        }
        return;
      case '~':
        if (!is_vhdl() && consume_if('&')) {
          emit(TokenKind::TildeAmpersand, begin);
        } else if (!is_vhdl() && consume_if('|')) {
          emit(TokenKind::TildePipe, begin);
        } else if (!is_vhdl() && consume_if('^')) {
          emit(TokenKind::TildeCaret, begin);
        } else {
          emit(TokenKind::Tilde, begin);
        }
        return;
      default:
        diagnose("FSIM-FE-LEX-001",
                 std::string("unexpected character '") + character + '\'',
                 begin);
        return;
    }
  }

  SourceText source_;
  Language language_;
  VhdlStandard vhdl_standard_;
  std::size_t index_{};
  std::size_t line_{1};
  std::size_t column_{1};
  LexResult result_;
};

}  // namespace

LexResult lex(
    SourceText source, Language language, const VhdlStandard vhdl_standard)
{
    return Lexer(std::move(source), language, vhdl_standard).run();
}

bool is_vhdl_reserved_word(
    const std::string_view word, const VhdlStandard standard)
{
    return vhdl_reserved_word_impl(ascii_lower(word), standard);
}

const char* to_string(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::Number:
      return "number";
    case TokenKind::StringLiteral:
      return "string literal";
    case TokenKind::CharacterLiteral:
      return "character literal";
    case TokenKind::LeftParen:
      return "'('";
    case TokenKind::RightParen:
      return "')'";
    case TokenKind::LeftBracket:
      return "'['";
    case TokenKind::RightBracket:
      return "']'";
    case TokenKind::LeftBrace:
      return "'{'";
    case TokenKind::RightBrace:
      return "'}'";
    case TokenKind::Colon:
      return "':'";
    case TokenKind::Semicolon:
      return "';'";
    case TokenKind::Comma:
      return "','";
    case TokenKind::Dot:
      return "'.'";
    case TokenKind::Apostrophe:
      return "apostrophe";
    case TokenKind::Hash:
      return "'#'";
    case TokenKind::At:
      return "'@'";
    case TokenKind::Question:
      return "'?'";
    case TokenKind::Backtick:
      return "'`'";
    case TokenKind::PslDirective:
      return "VHDL PSL comment marker";
    case TokenKind::Assign:
      return "'='";
    case TokenKind::Less:
      return "'<'";
    case TokenKind::Greater:
      return "'>'";
    case TokenKind::LessEqual:
      return "'<='";
    case TokenKind::GreaterEqual:
      return "'>='";
    case TokenKind::EqualEqual:
      return "'=='";
    case TokenKind::CaseEqual:
      return "'==='";
    case TokenKind::WildcardEqual:
      return "'==?'";
    case TokenKind::NotEqual:
      return "'!='";
    case TokenKind::CaseNotEqual:
      return "'!=='";
    case TokenKind::WildcardNotEqual:
      return "'!=?'";
    case TokenKind::Arrow:
      return "'=>'";
    case TokenKind::ThinArrow:
      return "'->'";
    case TokenKind::ColonEqual:
      return "':='";
    case TokenKind::Scope:
      return "'::'";
    case TokenKind::ShiftLeft:
      return "'<<'";
    case TokenKind::ShiftLeftAssign:
      return "'<<='";
    case TokenKind::ShiftRight:
      return "'>>'";
    case TokenKind::ShiftRightAssign:
      return "'>>='";
    case TokenKind::ArithmeticShiftLeft:
      return "'<<<'";
    case TokenKind::ArithmeticShiftLeftAssign:
      return "'<<<='";
    case TokenKind::ArithmeticShiftRight:
      return "'>>>'";
    case TokenKind::ArithmeticShiftRightAssign:
      return "'>>>='";
    case TokenKind::AndAndAnd:
      return "'&&&'";
    case TokenKind::AndAnd:
      return "'&&'";
    case TokenKind::OrOr:
      return "'||'";
    case TokenKind::Plus:
      return "'+'";
    case TokenKind::PlusColon:
      return "'+:'";
    case TokenKind::PlusPlus:
      return "'++'";
    case TokenKind::PlusAssign:
      return "'+='";
    case TokenKind::Minus:
      return "'-'";
    case TokenKind::MinusColon:
      return "'-:'";
    case TokenKind::MinusMinus:
      return "'--'";
    case TokenKind::MinusAssign:
      return "'-='";
    case TokenKind::Star:
      return "'*'";
    case TokenKind::StarAssign:
      return "'*='";
    case TokenKind::Power:
      return "'**'";
    case TokenKind::Slash:
      return "'/'";
    case TokenKind::SlashAssign:
      return "'/='";
    case TokenKind::Percent:
      return "'%'";
    case TokenKind::PercentAssign:
      return "'%='";
    case TokenKind::Ampersand:
      return "'&'";
    case TokenKind::AmpersandAssign:
      return "'&='";
    case TokenKind::Pipe:
      return "'|'";
    case TokenKind::PipeAssign:
      return "'|='";
    case TokenKind::Caret:
      return "'^'";
    case TokenKind::CaretAssign:
      return "'^='";
    case TokenKind::TildeAmpersand:
      return "'~&'";
    case TokenKind::TildePipe:
      return "'~|'";
    case TokenKind::TildeCaret:
      return "'~^'";
    case TokenKind::CaretTilde:
      return "'^~'";
    case TokenKind::Tilde:
      return "'~'";
    case TokenKind::Bang:
      return "'!'";
    case TokenKind::EndOfFile:
      return "end of file";
  }
  return "token";
}

}  // namespace fsim::frontend
