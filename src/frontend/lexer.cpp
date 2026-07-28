// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/token.hpp"

#include <cctype>
#include <string_view>
#include <utility>

namespace fsim::frontend {
namespace {

class Lexer {
 public:
  Lexer(SourceText source, Language language)
      : source_(std::move(source)), language_(language) {}

  LexResult run() {
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
    return SourceSpan{source_.name, begin, end};
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

  void skip_trivia() {
    for (;;) {
      while (std::isspace(static_cast<unsigned char>(peek()))) {
        advance();
      }
      if (is_vhdl() && peek() == '-' && peek(1) == '-') {
        while (!at_end() && peek() != '\n' && peek() != '\r') {
          advance();
        }
        continue;
      }
      if (!is_vhdl() && peek() == '/' && peek(1) == '/') {
        while (!at_end() && peek() != '\n' && peek() != '\r') {
          advance();
        }
        continue;
      }
      if (!is_vhdl() && peek() == '/' && peek(1) == '*') {
        const auto begin = current_location();
        advance();
        advance();
        while (!at_end() && !(peek() == '*' && peek(1) == '/')) {
          advance();
        }
        if (at_end()) {
          diagnose("FSIM-FE-LEX-002", "unterminated block comment", begin);
          return;
        }
        advance();
        advance();
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
    emit(TokenKind::Identifier, begin);
  }

  void lex_extended_identifier() {
    const auto begin = current_location();
    advance();
    if (!is_vhdl() && (peek() == '\n' || peek() == '\r')) {
      advance();
      emit(TokenKind::Identifier, begin);
      return;
    }
    while (!at_end() && peek() != '\\' &&
           (!is_vhdl() || (peek() != '\n' && peek() != '\r'))) {
      if (!is_vhdl() &&
          std::isspace(static_cast<unsigned char>(peek()))) {
        break;
      }
      advance();
    }
    if (is_vhdl()) {
      if (!consume_if('\\')) {
        diagnose("FSIM-FE-LEX-003", "unterminated extended identifier",
                 begin);
      }
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
          emit(TokenKind::EqualEqual, begin);
        } else {
          emit(TokenKind::Assign, begin);
        }
        return;
      case '<':
        if (consume_if('=')) {
          emit(TokenKind::LessEqual, begin);
        } else if (consume_if('<')) {
          emit(
              !is_vhdl() && consume_if('<')
                  ? TokenKind::ArithmeticShiftLeft
                  : TokenKind::ShiftLeft,
              begin);
        } else {
          emit(TokenKind::Less, begin);
        }
        return;
      case '>':
        if (consume_if('=')) {
          emit(TokenKind::GreaterEqual, begin);
        } else if (consume_if('>')) {
          emit(
              !is_vhdl() && consume_if('>')
                  ? TokenKind::ArithmeticShiftRight
                  : TokenKind::ShiftRight,
              begin);
        } else {
          emit(TokenKind::Greater, begin);
        }
        return;
      case '!':
        if (consume_if('=')) {
          emit(TokenKind::NotEqual, begin);
        } else {
          emit(TokenKind::Bang, begin);
        }
        return;
      case '&':
        if (consume_if('&')) {
          emit(TokenKind::AndAnd, begin);
        } else {
          emit(TokenKind::Ampersand, begin);
        }
        return;
      case '|':
        if (consume_if('|')) {
          emit(TokenKind::OrOr, begin);
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
        if (!is_vhdl() && consume_if(':')) {
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
        emit(TokenKind::Star, begin);
        return;
      case '/':
        if (consume_if('=')) {
          emit(TokenKind::NotEqual, begin);
        } else {
          emit(TokenKind::Slash, begin);
        }
        return;
      case '%':
        emit(TokenKind::Percent, begin);
        return;
      case '^':
        emit(TokenKind::Caret, begin);
        return;
      case '~':
        emit(TokenKind::Tilde, begin);
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
  std::size_t index_{};
  std::size_t line_{1};
  std::size_t column_{1};
  LexResult result_;
};

}  // namespace

LexResult lex(SourceText source, Language language) {
  return Lexer(std::move(source), language).run();
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
    case TokenKind::NotEqual:
      return "'!='";
    case TokenKind::Arrow:
      return "'=>'";
    case TokenKind::ColonEqual:
      return "':='";
    case TokenKind::Scope:
      return "'::'";
    case TokenKind::ShiftLeft:
      return "'<<'";
    case TokenKind::ShiftRight:
      return "'>>'";
    case TokenKind::ArithmeticShiftLeft:
      return "'<<<'";
    case TokenKind::ArithmeticShiftRight:
      return "'>>>'";
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
    case TokenKind::Slash:
      return "'/'";
    case TokenKind::Percent:
      return "'%'";
    case TokenKind::Ampersand:
      return "'&'";
    case TokenKind::Pipe:
      return "'|'";
    case TokenKind::Caret:
      return "'^'";
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
