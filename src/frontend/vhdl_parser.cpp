// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"

#include "parser_support.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

using detail::decimal_i64;
using detail::decimal_u64;
using detail::vhdl_name;

[[nodiscard]] std::optional<std::int64_t> simple_integer_constant(
    const Expression& expression) {
  if (expression.kind == ExpressionKind::IntegerLiteral) {
    return decimal_i64(expression.text);
  }
  if (expression.kind == ExpressionKind::Unary
      && expression.operands.size() == 1
      && (expression.text == "+" || expression.text == "-")) {
    const auto magnitude =
        simple_integer_constant(expression.operands.front());
    if (!magnitude) {
      return std::nullopt;
    }
    if (expression.text == "+") {
      return magnitude;
    }
    if (*magnitude == std::numeric_limits<std::int64_t>::min()) {
      return std::nullopt;
    }
    return -*magnitude;
  }
  return std::nullopt;
}

class VhdlParser final : private detail::ParserBase {
 public:
  explicit VhdlParser(LexResult lexed)
      : ParserBase(std::move(lexed.tokens),
                   std::move(lexed.diagnostics)) {}

  ParseResult run() {
    ParsedDesign design;
    std::vector<VhdlContextItem> pending_context;
    while (!at_end()) {
      if (keyword("context", 0, true)
          && at(TokenKind::Identifier, 1)
          && keyword("is", 2, true)) {
        const auto start = advance();
        auto unit = parse_context_declaration(start);
        unit.vhdl_context.insert(
            unit.vhdl_context.begin(),
            std::make_move_iterator(pending_context.begin()),
            std::make_move_iterator(pending_context.end()));
        pending_context.clear();
        design.units.push_back(std::move(unit));
      } else if (any_keyword({"library", "use", "context"}, true)) {
        if (auto item = parse_context_item()) {
          pending_context.push_back(std::move(*item));
        }
      } else if (match_keyword("entity", true)) {
        auto unit = parse_entity(previous());
        unit.vhdl_context = std::exchange(pending_context, {});
        design.units.push_back(std::move(unit));
      } else if (match_keyword("architecture", true)) {
        auto unit = parse_architecture(previous());
        unit.vhdl_context = std::exchange(pending_context, {});
        design.units.push_back(std::move(unit));
      } else if (match_keyword("package", true)) {
        const auto start = previous();
        if (match_keyword("body", true)) {
          error(
              start,
              "FSIM-VHDL-UNSUPPORTED-022",
              "VHDL package bodies are not implemented in this bounded "
              "package slice");
          skip_vhdl_package_body();
          pending_context.clear();
        } else {
          auto unit = parse_package(start);
          unit.vhdl_context = std::exchange(pending_context, {});
          design.units.push_back(std::move(unit));
        }
      } else {
        const auto unexpected = advance();
        error(unexpected, "FSIM-VHDL-UNSUPPORTED-001",
              "unsupported VHDL design unit or context item '" +
                  unexpected.text + "'");
        skip_to_semicolon();
      }
    }
    return ParseResult{std::move(design), std::move(diagnostics_)};
  }

 private:
  static SourceSpan span_from(const Token& first, const Token& last) {
    return cover(first.span, last.span);
  }

  static std::string string_literal_text(const Token& token) {
    if (token.text.size() >= 2 && token.text.front() == '"'
        && token.text.back() == '"') {
      return token.text.substr(1, token.text.size() - 2);
    }
    return token.text;
  }

  std::optional<VhdlContextItem> parse_context_item() {
    const auto start = advance();
    const auto context_kind =
        detail::iequals(start.text, "library")
            ? VhdlContextItemKind::LibraryClause
            : detail::iequals(start.text, "use")
                ? VhdlContextItemKind::UseClause
                : VhdlContextItemKind::ContextReference;

    if (context_kind == VhdlContextItemKind::ContextReference
        && at(TokenKind::Identifier)
        && keyword("is", 1, true)) {
      error(
          start,
          "FSIM-VHDL-UNSUPPORTED-015",
          "a context declaration cannot be nested where a context "
          "reference is required");
      while (!at_end()) {
        if (match_keyword("end", true)) {
          match_keyword("context", true);
          if (at(TokenKind::Identifier)) {
            advance();
          }
          skip_to_semicolon();
          break;
        }
        advance();
      }
      return std::nullopt;
    }

    VhdlContextItem item;
    item.kind = context_kind;
    std::string selected_name;
    bool malformed = false;
    while (!at_end() && !at(TokenKind::Semicolon)) {
      if (match(TokenKind::Comma)) {
        if (selected_name.empty() || selected_name.back() == '.') {
          malformed = true;
        } else {
          item.selected_names.push_back(std::move(selected_name));
          selected_name.clear();
        }
        continue;
      }
      if (at(TokenKind::Dot)) {
        if (selected_name.empty() || selected_name.back() == '.') {
          malformed = true;
        } else {
          selected_name.push_back('.');
        }
        advance();
        continue;
      }
      if (at(TokenKind::Identifier)) {
        if (!selected_name.empty() && selected_name.back() != '.') {
          malformed = true;
        } else {
          selected_name += vhdl_name(advance().text);
        }
        continue;
      }
      malformed = true;
      advance();
    }
    if (!selected_name.empty() && selected_name.back() != '.') {
      item.selected_names.push_back(std::move(selected_name));
    } else {
      malformed = true;
    }
    if (!match(TokenKind::Semicolon)) {
      malformed = true;
    }
    if (malformed) {
      error(
          start,
          "FSIM-VHDL-PARSE-044",
          "malformed or unterminated VHDL context clause");
    }
    item.span = span_from(start, previous());
    return item;
  }

  Token expect_identifier(std::string_view description) {
    return expect(TokenKind::Identifier, description, "FSIM-VHDL-PARSE-001");
  }

  DesignUnit parse_context_declaration(const Token& start) {
    DesignUnit unit;
    unit.kind = UnitKind::VhdlContext;
    unit.language = Language::Vhdl2008;
    const auto name = expect_identifier("context name");
    unit.name = vhdl_name(name.text);
    expect_keyword("is", true, "FSIM-VHDL-PARSE-090");
    while (!at_end() && !keyword("end", 0, true)) {
      if (any_keyword({"library", "use", "context"}, true)) {
        if (auto item = parse_context_item()) {
          unit.vhdl_context.push_back(std::move(*item));
        }
        continue;
      }
      const auto declaration = advance();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-024",
          "unsupported context declaration item '"
              + declaration.text + "'");
      skip_to_semicolon();
    }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-091");
    (void)match_keyword("context", true);
    if (at(TokenKind::Identifier)) {
      const auto end_name = advance();
      if (vhdl_name(end_name.text) != unit.name) {
        error(
            end_name,
            "FSIM-VHDL-PARSE-092",
            "context end name does not match '" + unit.name + "'");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after context declaration",
        "FSIM-VHDL-PARSE-093");
    unit.span = span_from(start, previous());
    return unit;
  }

  void skip_vhdl_package_body() {
    while (!at_end()) {
      if (keyword("end", 0, true)
          && keyword("package", 1, true)
          && keyword("body", 2, true)) {
        advance();
        advance();
        advance();
        if (at(TokenKind::Identifier)) {
          advance();
        }
        skip_to_semicolon();
        return;
      }
      advance();
    }
  }

  DesignUnit parse_package(const Token& start) {
    DesignUnit unit;
    unit.kind = UnitKind::VhdlPackage;
    unit.language = Language::Vhdl2008;
    const auto name = expect_identifier("package name");
    unit.name = vhdl_name(name.text);
    expect_keyword("is", true, "FSIM-VHDL-PARSE-086");
    while (!at_end() && !keyword("end", 0, true)) {
      if (match_keyword("constant", true)) {
        parse_package_constant(unit, previous());
      } else {
        const auto declaration = advance();
        error(
            declaration,
            "FSIM-VHDL-UNSUPPORTED-022",
            "unsupported package declaration '"
                + declaration.text + "'");
        skip_to_semicolon();
      }
    }
    parse_vhdl_end("package");
    unit.span = span_from(start, previous());
    return unit;
  }

  void parse_package_constant(
      DesignUnit& unit, const Token& start) {
    std::vector<Token> names;
    names.push_back(expect_identifier("package constant name"));
    while (match(TokenKind::Comma)) {
      names.push_back(
          expect_identifier("package constant name"));
    }
    expect(
        TokenKind::Colon,
        "':' after package constant names",
        "FSIM-VHDL-PARSE-087");
    const auto type = parse_vhdl_type(true);
    if (type.packed_range
        || (type.domain != ValueDomain::Integer
            && type.domain != ValueDomain::Boolean
            && type.domain != ValueDomain::Bit2)) {
      error(
          names.front(),
          "FSIM-VHDL-UNSUPPORTED-023",
          "package constants are bounded to scalar integer, Boolean, and "
          "bit types");
    }
    Expression value;
    if (match(TokenKind::ColonEqual)) {
      value = parse_expression();
    } else {
      error(
          current(),
          "FSIM-VHDL-PARSE-088",
          "a package constant requires a default expression");
    }
    expect(
        TokenKind::Semicolon,
        "';' after package constant declaration",
        "FSIM-VHDL-PARSE-089");
    for (const auto& constant_name : names) {
      const auto canonical =
          vhdl_name(constant_name.text);
      if (std::any_of(
              unit.parameters.begin(),
              unit.parameters.end(),
              [&](const ParameterDeclaration& existing) {
                return existing.name == canonical;
              })) {
        error(
            constant_name,
            "FSIM-VHDL-SEM-020",
            "duplicate package constant declaration '"
                + canonical + "'");
        continue;
      }
      unit.parameters.push_back(ParameterDeclaration{
          canonical,
          type,
          value,
          true,
          span_from(start, previous())});
    }
  }

  DesignUnit parse_entity(const Token& start) {
    DesignUnit unit;
    unit.kind = UnitKind::VhdlEntity;
    unit.language = Language::Vhdl2008;
    const auto name = expect_identifier("entity name");
    unit.name = vhdl_name(name.text);
    expect_keyword("is", true, "FSIM-VHDL-PARSE-002");

    while (!at_end() && !keyword("end", 0, true)) {
      if (match_keyword("port", true)) {
        parse_vhdl_ports(unit);
      } else if (match_keyword("generic", true)) {
        parse_vhdl_generics(unit, previous());
      } else {
        const auto declaration = advance();
        error(declaration, "FSIM-VHDL-UNSUPPORTED-003",
              "unsupported entity declaration '" + declaration.text + "'");
        skip_to_semicolon();
      }
    }

    parse_vhdl_end("entity");
    unit.span = span_from(start, previous());
    return unit;
  }

  void add_vhdl_generic(
      DesignUnit& unit,
      ParameterDeclaration generic,
      const Token& name) {
    const auto canonical = generic.name;
    const auto object_conflict =
        std::any_of(
            unit.ports.begin(),
            unit.ports.end(),
            [&](const SignalDeclaration& declaration) {
              return declaration.name == canonical;
            })
        || std::any_of(
            unit.signals.begin(),
            unit.signals.end(),
            [&](const SignalDeclaration& declaration) {
              return declaration.name == canonical;
            });
    if (object_conflict) {
      error(
          name,
          "FSIM-VHDL-SEM-014",
          "generic '" + canonical
              + "' conflicts with an object declaration");
      return;
    }
    if (std::any_of(
            unit.parameters.begin(),
            unit.parameters.end(),
            [&](const ParameterDeclaration& existing) {
              return existing.name == canonical;
            })) {
      error(
          name,
          "FSIM-VHDL-SEM-013",
          "duplicate generic declaration '" + canonical + "'");
      return;
    }
    unit.parameters.push_back(std::move(generic));
  }

  void parse_vhdl_generics(
      DesignUnit& unit,
      const Token& start) {
    expect(
        TokenKind::LeftParen,
        "'(' after generic",
        "FSIM-VHDL-PARSE-050");
    while (!at_end() && !at(TokenKind::RightParen)) {
      std::vector<Token> names;
      names.push_back(expect_identifier("generic name"));
      while (match(TokenKind::Comma)) {
        names.push_back(expect_identifier("generic name"));
      }
      expect(
          TokenKind::Colon,
          "':' after generic name",
          "FSIM-VHDL-PARSE-051");
      const auto type = parse_vhdl_type(true);
      if (type.packed_range
          || (type.domain != ValueDomain::Integer
              && type.domain != ValueDomain::Boolean
              && type.domain != ValueDomain::Bit2)) {
        error(
            names.front(),
            "FSIM-VHDL-UNSUPPORTED-018",
            "this generic type is outside the bounded scalar integer, "
            "Boolean, and bit subset");
      }
      Expression default_value;
      if (match(TokenKind::ColonEqual)) {
        default_value = parse_expression();
      }
      for (const auto& name : names) {
        add_vhdl_generic(
            unit,
            ParameterDeclaration{
                vhdl_name(name.text),
                type,
                default_value,
                false,
                span_from(name, previous())},
            name);
      }
      if (!match(TokenKind::Semicolon)
          && !at(TokenKind::RightParen)) {
        error(
            current(),
            "FSIM-VHDL-PARSE-052",
            "expected ';' between generic declarations");
        skip_to_semicolon();
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after generic declarations",
        "FSIM-VHDL-PARSE-053");
    expect(
        TokenKind::Semicolon,
        "';' after generic clause",
        "FSIM-VHDL-PARSE-054");
    (void)start;
  }

  void parse_vhdl_ports(DesignUnit& unit) {
    expect(TokenKind::LeftParen, "'(' after port",
           "FSIM-VHDL-PARSE-003");
    while (!at_end() && !at(TokenKind::RightParen)) {
      std::vector<Token> names;
      names.push_back(expect_identifier("port name"));
      while (match(TokenKind::Comma)) {
        names.push_back(expect_identifier("port name"));
      }
      expect(TokenKind::Colon, "':' after port name",
             "FSIM-VHDL-PARSE-004");

      PortDirection direction = PortDirection::Unknown;
      if (match_keyword("in", true)) {
        direction = PortDirection::Input;
      } else if (match_keyword("out", true)) {
        direction = PortDirection::Output;
      } else if (match_keyword("inout", true)) {
        direction = PortDirection::Inout;
      } else if (match_keyword("buffer", true)) {
        direction = PortDirection::Buffer;
      } else {
        error(current(), "FSIM-VHDL-PARSE-005",
              "expected VHDL port mode");
      }

      Type type = parse_vhdl_type();
      if (match(TokenKind::ColonEqual)) {
        const auto initializer = previous();
        (void)parse_expression();
        error(
            initializer,
            "FSIM-VHDL-UNSUPPORTED-011",
            "VHDL port default expressions are not executable in this "
            "frontend slice");
      }
      for (const auto& name : names) {
        const auto canonical = vhdl_name(name.text);
        const auto duplicate = std::find_if(
            unit.ports.begin(),
            unit.ports.end(),
            [&](const SignalDeclaration& port) {
              return port.name == canonical;
            });
        if (duplicate != unit.ports.end()) {
          error(
              name,
              "FSIM-VHDL-SEM-002",
              "duplicate port declaration '" + canonical + "'");
        } else if (
            std::any_of(
                unit.parameters.begin(),
                unit.parameters.end(),
                [&](const ParameterDeclaration& generic) {
                  return generic.name == canonical;
                })) {
          error(
              name,
              "FSIM-VHDL-SEM-014",
              "port '" + canonical
                  + "' conflicts with a generic declaration");
        } else {
          unit.ports.push_back(
              SignalDeclaration{
                  canonical,
                  type,
                  direction,
                  true,
                  span_from(name, previous())});
        }
      }

      if (!match(TokenKind::Semicolon) && !at(TokenKind::RightParen)) {
        error(current(), "FSIM-VHDL-PARSE-006",
              "expected ';' between port declarations");
        skip_to_semicolon();
      }
    }
    expect(TokenKind::RightParen, "')' after port declarations",
           "FSIM-VHDL-PARSE-007");
    expect(TokenKind::Semicolon, "';' after port clause",
           "FSIM-VHDL-PARSE-008");
  }

  Type parse_vhdl_type(const bool allow_integer = false) {
    const auto first = expect_identifier("subtype indication");
    std::string spelling = vhdl_name(first.text);
    while (match(TokenKind::Dot)) {
      const auto selected = expect_identifier("selected type name");
      spelling += '.';
      spelling += vhdl_name(selected.text);
    }

    Type type;
    type.spelling = spelling;
    const auto simple_name =
        spelling.substr(spelling.find_last_of('.') == std::string::npos
                            ? 0
                            : spelling.find_last_of('.') + 1);
    if (simple_name == "bit" || simple_name == "bit_vector") {
      type.domain = ValueDomain::Bit2;
    } else if (simple_name == "std_logic" ||
               simple_name == "std_logic_vector" ||
               simple_name == "std_ulogic" ||
               simple_name == "std_ulogic_vector" ||
               simple_name == "signed" || simple_name == "unsigned") {
      type.domain = ValueDomain::Logic9;
      type.is_signed = simple_name == "signed";
    } else if (simple_name == "boolean") {
      type.domain = ValueDomain::Boolean;
    } else if (simple_name == "integer" || simple_name == "natural" ||
               simple_name == "positive") {
      type.domain = ValueDomain::Integer;
      type.is_signed = simple_name == "integer";
    }
    if (type.domain == ValueDomain::Unknown) {
      error(
          first,
          "FSIM-VHDL-UNSUPPORTED-013",
          "subtype '" + spelling
              + "' requires semantic type resolution that is not "
                "implemented in this frontend slice");
    } else if (
        type.domain == ValueDomain::Integer
        && !allow_integer) {
      error(
          first,
          "FSIM-VHDL-UNSUPPORTED-014",
          "VHDL integer-family objects are parsed but not executable in this "
          "frontend slice");
    }

    if (match(TokenKind::LeftParen)) {
      const auto range_start = previous();
      auto left_expression = parse_expression();
      bool descending = true;
      if (match_keyword("downto", true)) {
        descending = true;
      } else if (match_keyword("to", true)) {
        descending = false;
      } else {
        error(current(), "FSIM-VHDL-PARSE-009",
              "only locally static integer ranges are supported here");
      }
      auto right_expression = parse_expression();
      expect(TokenKind::RightParen, "')' after range",
             "FSIM-VHDL-PARSE-010");
      const auto left = simple_integer_constant(left_expression);
      const auto right = simple_integer_constant(right_expression);
      if (left && right) {
        type.packed_range = PackedRange{*left, *right, descending};
      }
      type.packed_range_expression = PackedRangeExpression{
          std::move(left_expression),
          std::move(right_expression),
          cover(range_start.span, previous().span),
          descending};
    }
    return type;
  }

  void parse_vhdl_end(std::string_view expected_kind) {
    expect_keyword("end", true, "FSIM-VHDL-PARSE-012");
    match_keyword(expected_kind, true);
    if (at(TokenKind::Identifier)) {
      advance();
    }
    expect(TokenKind::Semicolon, "';' after end clause",
           "FSIM-VHDL-PARSE-013");
  }

  DesignUnit parse_architecture(const Token& start) {
    DesignUnit unit;
    unit.kind = UnitKind::VhdlArchitecture;
    unit.language = Language::Vhdl2008;
    const auto name = expect_identifier("architecture name");
    unit.name = vhdl_name(name.text);
    expect_keyword("of", true, "FSIM-VHDL-PARSE-014");
    const auto entity = expect_identifier("entity name");
    unit.primary_name = vhdl_name(entity.text);
    expect_keyword("is", true, "FSIM-VHDL-PARSE-015");

    while (!at_end() && !keyword("begin", 0, true)) {
      if (match_keyword("signal", true)) {
        parse_signal_declaration(unit.signals);
      } else {
        const auto declaration = advance();
        error(declaration, "FSIM-VHDL-UNSUPPORTED-004",
              "unsupported architecture declaration '" + declaration.text +
                  "'");
        skip_to_semicolon();
      }
    }
    expect_keyword("begin", true, "FSIM-VHDL-PARSE-016");

    while (!at_end() && !keyword("end", 0, true)) {
      parse_concurrent_statement(unit);
    }
    parse_vhdl_end("architecture");
    unit.span = span_from(start, previous());
    return unit;
  }

  void parse_signal_declaration(
      std::vector<SignalDeclaration>& signals,
      const std::vector<ParameterDeclaration>* constants = nullptr) {
    const auto start = previous();
    std::vector<Token> names;
    names.push_back(expect_identifier("signal name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("signal name"));
    }
    expect(TokenKind::Colon, "':' after signal name",
           "FSIM-VHDL-PARSE-017");
    const Type type = parse_vhdl_type();
    if (match(TokenKind::ColonEqual)) {
      const auto initializer = previous();
      (void)parse_expression();
      error(
          initializer,
          "FSIM-VHDL-UNSUPPORTED-012",
          "VHDL signal initializers are not executable in this frontend "
          "slice");
    }
    expect(TokenKind::Semicolon, "';' after signal declaration",
           "FSIM-VHDL-PARSE-018");
    for (const auto& name : names) {
      const auto canonical = vhdl_name(name.text);
      const auto duplicate = std::find_if(
          signals.begin(),
          signals.end(),
          [&](const SignalDeclaration& signal) {
            return signal.name == canonical;
          });
      const bool constant_conflict =
          constants != nullptr
          && std::any_of(
              constants->begin(),
              constants->end(),
              [&](const ParameterDeclaration& constant) {
                return constant.name == canonical;
              });
      if (constant_conflict) {
        error(
            name,
            "FSIM-VHDL-SEM-019",
            "generated object '" + canonical
                + "' is declared as both a signal and a constant");
      } else if (duplicate != signals.end()) {
        error(
            name,
            "FSIM-VHDL-SEM-003",
            "duplicate signal declaration '" + canonical + "'");
      } else {
        signals.push_back(SignalDeclaration{
            canonical,
            type,
            PortDirection::Unknown,
            false,
            span_from(start, previous())});
      }
    }
  }

  void parse_concurrent_statement(DesignUnit& unit) {
    std::optional<Token> label_token;
    if (at(TokenKind::Identifier) && at(TokenKind::Colon, 1)) {
      label_token = advance();
      advance();
    }

    if (keyword("process", 0, true)) {
      unit.processes.push_back(parse_process(
          label_token ? vhdl_name(label_token->text) : std::string{}));
      return;
    }
    if (label_token && match_keyword("if", true)) {
      unit.generate_regions.push_back(
          parse_vhdl_conditional_generate(
              *label_token, previous()));
      return;
    }
    if (label_token && match_keyword("for", true)) {
      unit.generate_regions.push_back(
          parse_vhdl_iterative_generate(
              *label_token, previous()));
      return;
    }
    if (label_token && match_keyword("case", true)) {
      unit.generate_regions.push_back(
          parse_vhdl_selection_generate(
              *label_token, previous()));
      return;
    }
    if (label_token && match_keyword("block", true)) {
      unit.generate_regions.push_back(
          parse_vhdl_static_block(
              *label_token, previous()));
      return;
    }
    if (label_token &&
        (keyword("entity", 0, true) ||
         (at(TokenKind::Identifier) &&
          (keyword("port", 1, true) ||
           keyword("generic", 1, true))))) {
      unit.instances.push_back(parse_vhdl_instance(*label_token));
      return;
    }
    if (label_token) {
      error(previous(), "FSIM-VHDL-UNSUPPORTED-005",
            "this labeled concurrent statement is not supported");
    }

    const auto before = position();
    auto statement = parse_assignment(true);
    if (statement) {
      unit.concurrent_statements.push_back(std::move(*statement));
      return;
    }
    rewind(before);
    const auto unexpected = advance();
    error(unexpected, "FSIM-VHDL-UNSUPPORTED-006",
          "unsupported concurrent statement starting with '" +
              unexpected.text + "'");
    skip_to_semicolon();
  }

  GenerateRegion parse_vhdl_conditional_generate(
      const Token& label,
      const Token& start) {
    GenerateRegion result;
    result.then_scope = vhdl_name(label.text);
    result.else_scope = result.then_scope;
    result.condition = parse_expression();
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-056");
    parse_vhdl_generate_declarations(result.then_body);
    (void)match_keyword("begin", true);
    parse_vhdl_generate_branch(result.then_body);
    if (match_keyword("else", true)) {
      expect_keyword("generate", true, "FSIM-VHDL-PARSE-057");
      parse_vhdl_generate_declarations(result.else_body);
      (void)match_keyword("begin", true);
      parse_vhdl_generate_branch(result.else_body);
    }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-058");
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-059");
    if (at(TokenKind::Identifier)) {
      const auto end_label = advance();
      if (vhdl_name(end_label.text) != result.then_scope) {
        error(
            end_label,
            "FSIM-VHDL-PARSE-060",
            "generate end label does not match '"
                + result.then_scope + "'");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after conditional generate",
        "FSIM-VHDL-PARSE-061");
    result.span = span_from(start, previous());
    return result;
  }

  GenerateRegion parse_vhdl_iterative_generate(
      const Token& label,
      const Token& start) {
    GenerateRegion result;
    result.kind = GenerateKind::Iterative;
    result.then_scope = vhdl_name(label.text);
    const auto variable = expect_identifier("generate loop variable");
    result.variable = vhdl_name(variable.text);
    expect_keyword("in", true, "FSIM-VHDL-PARSE-062");
    result.initial = parse_expression();
    bool descending = false;
    if (match_keyword("to", true)) {
      descending = false;
    } else if (match_keyword("downto", true)) {
      descending = true;
    } else {
      error(
          current(),
          "FSIM-VHDL-PARSE-063",
          "expected 'to' or 'downto' in generate iteration range");
    }
    auto limit = parse_expression();
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-064");
    const auto expression_span = span_from(variable, previous());
    Expression loop_variable{
        ExpressionKind::Identifier,
        result.variable,
        {},
        variable.span};
    result.condition = {
        ExpressionKind::Binary,
        descending ? ">=" : "<=",
        {loop_variable, std::move(limit)},
        expression_span};
    result.iteration = {
        ExpressionKind::Binary,
        descending ? "-" : "+",
        {
            std::move(loop_variable),
            Expression{
                ExpressionKind::IntegerLiteral,
                "1",
                {},
                expression_span}},
        expression_span};
    parse_vhdl_generate_declarations(result.then_body);
    (void)match_keyword("begin", true);
    parse_vhdl_generate_branch(result.then_body);
    expect_keyword("end", true, "FSIM-VHDL-PARSE-065");
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-066");
    if (at(TokenKind::Identifier)) {
      const auto end_label = advance();
      if (vhdl_name(end_label.text) != result.then_scope) {
        error(
            end_label,
            "FSIM-VHDL-PARSE-067",
            "generate end label does not match '"
                + result.then_scope + "'");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after iterative generate",
        "FSIM-VHDL-PARSE-068");
    result.span = span_from(start, previous());
    return result;
  }

  GenerateRegion parse_vhdl_selection_generate(
      const Token& label,
      const Token& start) {
    GenerateRegion result;
    result.kind = GenerateKind::Selection;
    result.condition = parse_expression();
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-069");
    bool saw_default = false;
    while (!at_end() && !keyword("end", 0, true)) {
      GenerateAlternative alternative;
      const auto alternative_start = current();
      if (saw_default) {
        error(
            current(),
            "FSIM-VHDL-SEM-018",
            "an others case-generate alternative must be last");
      }
      if (!(at(TokenKind::Identifier)
            && at(TokenKind::Colon, 1)
            && keyword("when", 2, true))) {
        error(
            current(),
            "FSIM-VHDL-PARSE-070",
            "a case-generate alternative must have a stable label");
      }
      const auto alternative_label =
          expect_identifier("case-generate alternative label");
      alternative.scope = vhdl_name(alternative_label.text);
      expect(
          TokenKind::Colon,
          "':' after case-generate alternative label",
          "FSIM-VHDL-PARSE-071");
      expect_keyword("when", true, "FSIM-VHDL-PARSE-071");
      if (match_keyword("others", true)) {
        alternative.is_default = true;
        if (saw_default) {
          error(
              previous(),
              "FSIM-VHDL-SEM-017",
              "case generate contains more than one others "
              "alternative");
        }
        saw_default = true;
      } else {
        do {
          GenerateChoice choice;
          choice.left = parse_expression();
          choice.span = choice.left.span;
          if (match_keyword("to", true)
              || match_keyword("downto", true)) {
            choice.descending =
                vhdl_name(previous().text) == "downto";
            choice.right = parse_expression();
            choice.span =
                cover(choice.left.span, choice.right->span);
          }
          alternative.choices.push_back(
              std::move(choice));
        } while (match(TokenKind::Pipe));
        if (alternative.choices.empty()) {
          error(
              current(),
              "FSIM-VHDL-PARSE-072",
              "case-generate alternative requires a choice");
        }
      }
      expect(
          TokenKind::Arrow,
          "'=>' after case-generate choices",
          "FSIM-VHDL-PARSE-073");
      parse_vhdl_generate_declarations(alternative.body);
      (void)match_keyword("begin", true);
      parse_vhdl_generate_branch(
          alternative.body,
          true);
      alternative.span =
          span_from(alternative_start, previous());
      result.alternatives.push_back(std::move(alternative));
    }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-074");
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-075");
    if (at(TokenKind::Identifier)) {
      const auto end_label = advance();
      if (vhdl_name(end_label.text) != vhdl_name(label.text)) {
        error(
            end_label,
            "FSIM-VHDL-PARSE-076",
            "generate end label does not match '"
                + vhdl_name(label.text) + "'");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after case generate",
        "FSIM-VHDL-PARSE-077");
    result.span = span_from(start, previous());
    return result;
  }

  GenerateRegion parse_vhdl_static_block(
      const Token& label,
      const Token& start) {
    GenerateRegion result;
    result.kind = GenerateKind::StaticBlock;
    result.then_scope = vhdl_name(label.text);
    if (at(TokenKind::LeftParen)) {
      const auto guard = current();
      skip_balanced(
          TokenKind::LeftParen, TokenKind::RightParen);
      error(
          guard,
          "FSIM-VHDL-UNSUPPORTED-021",
          "guarded block statements are not executable yet");
    }
    (void)match_keyword("is", true);
    parse_vhdl_generate_declarations(result.then_body);
    expect_keyword("begin", true, "FSIM-VHDL-PARSE-078");
    parse_vhdl_generate_branch(result.then_body);
    expect_keyword("end", true, "FSIM-VHDL-PARSE-079");
    expect_keyword("block", true, "FSIM-VHDL-PARSE-080");
    if (at(TokenKind::Identifier)) {
      const auto end_label = advance();
      if (vhdl_name(end_label.text) != result.then_scope) {
        error(
            end_label,
            "FSIM-VHDL-PARSE-081",
            "block end label does not match '"
                + result.then_scope + "'");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after block statement",
        "FSIM-VHDL-PARSE-082");
    result.span = span_from(start, previous());
    return result;
  }

  void parse_vhdl_generate_declarations(GenerateBody& body) {
    for (;;) {
      if (match_keyword("signal", true)) {
        parse_signal_declaration(
            body.signals, &body.constants);
        continue;
      }
      if (match_keyword("constant", true)) {
        parse_vhdl_generate_constant(
            body, previous());
        continue;
      }
      break;
    }
  }

  void parse_vhdl_generate_constant(
      GenerateBody& body, const Token& start) {
    std::vector<Token> names;
    names.push_back(expect_identifier("constant name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("constant name"));
    }
    expect(
        TokenKind::Colon,
        "':' after constant names",
        "FSIM-VHDL-PARSE-083");
    const auto type = parse_vhdl_type(true);
    Expression value;
    if (match(TokenKind::ColonEqual)) {
      value = parse_expression();
    } else {
      error(
          current(),
          "FSIM-VHDL-PARSE-084",
          "a generated constant requires a default expression");
    }
    expect(
        TokenKind::Semicolon,
        "';' after constant declaration",
        "FSIM-VHDL-PARSE-085");
    for (const auto& name : names) {
      const auto canonical = vhdl_name(name.text);
      const bool duplicate =
          std::any_of(
              body.constants.begin(),
              body.constants.end(),
              [&](const ParameterDeclaration& constant) {
                return constant.name == canonical;
              })
          || std::any_of(
              body.signals.begin(),
              body.signals.end(),
              [&](const SignalDeclaration& signal) {
                return signal.name == canonical;
              });
      if (duplicate) {
        error(
            name,
            "FSIM-VHDL-SEM-019",
            "duplicate or conflicting generated constant declaration '"
                + canonical + "'");
        continue;
      }
      body.constants.push_back(ParameterDeclaration{
          canonical,
          type,
          value,
          true,
          span_from(start, previous())});
    }
  }

  void parse_vhdl_generate_branch(
      GenerateBody& body,
      const bool stop_at_case_alternative = false) {
    while (!at_end() && !keyword("else", 0, true)
           && !keyword("end", 0, true)
           && !(stop_at_case_alternative
                && at(TokenKind::Identifier)
                && at(TokenKind::Colon, 1)
                && keyword("when", 2, true))) {
      std::optional<Token> label;
      if (at(TokenKind::Identifier)
          && at(TokenKind::Colon, 1)) {
        label = advance();
        advance();
      }
      if (keyword("process", 0, true)) {
        body.processes.push_back(parse_process(
            label ? vhdl_name(label->text) : std::string{}));
        continue;
      }
      if (label && match_keyword("if", true)) {
        body.generate_regions.push_back(
            parse_vhdl_conditional_generate(
                *label, previous()));
        continue;
      }
      if (label && match_keyword("for", true)) {
        body.generate_regions.push_back(
            parse_vhdl_iterative_generate(
                *label, previous()));
        continue;
      }
      if (label && match_keyword("case", true)) {
        body.generate_regions.push_back(
            parse_vhdl_selection_generate(
                *label, previous()));
        continue;
      }
      if (label && match_keyword("block", true)) {
        body.generate_regions.push_back(
            parse_vhdl_static_block(
                *label, previous()));
        continue;
      }
      if (label && (
          keyword("entity", 0, true)
          || (at(TokenKind::Identifier)
              && (keyword("port", 1, true)
                  || keyword("generic", 1, true))))) {
        body.instances.push_back(parse_vhdl_instance(*label));
        continue;
      }
      const auto before = position();
      auto assignment = parse_assignment(true);
      if (assignment) {
        body.concurrent_statements.push_back(
            std::move(*assignment));
        continue;
      }
      rewind(before);
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-VHDL-UNSUPPORTED-020",
          "unsupported concurrent item in generate branch");
      skip_to_semicolon();
    }
  }

  Instance parse_vhdl_instance(const Token& label) {
    Instance instance;
    instance.name = vhdl_name(label.text);

    if (match_keyword("entity", true)) {
      const auto first = expect_identifier("entity name");
      instance.unit_name = vhdl_name(first.text);
      if (match(TokenKind::Dot)) {
        const auto unit = expect_identifier("entity name after library");
        instance.unit_name += '.';
        instance.unit_name += vhdl_name(unit.text);
      }
      if (match(TokenKind::LeftParen)) {
        const auto architecture =
            expect_identifier("architecture name in entity aspect");
        instance.unit_name += '(';
        instance.unit_name += vhdl_name(architecture.text);
        instance.unit_name += ')';
        expect(TokenKind::RightParen, "')' after architecture name",
               "FSIM-VHDL-PARSE-036");
      }
    } else {
      const auto component = expect_identifier("component name");
      instance.unit_name = vhdl_name(component.text);
    }

    if (match_keyword("generic", true)) {
      parse_vhdl_generic_map(instance, previous());
    }

    if (!match_keyword("port", true)) {
      error(current(), "FSIM-VHDL-PARSE-039",
            "expected 'port map' in VHDL instance");
      skip_to_semicolon();
      instance.span = span_from(label, previous());
      return instance;
    }
    expect_keyword("map", true, "FSIM-VHDL-PARSE-040");
    if (!match(TokenKind::LeftParen)) {
      error(current(), "FSIM-VHDL-PARSE-041",
            "expected '(' after port map");
      skip_to_semicolon();
      instance.span = span_from(label, previous());
      return instance;
    }

    while (!at_end() && !at(TokenKind::RightParen)) {
      instance.connections.push_back(parse_vhdl_port_connection());
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(TokenKind::RightParen, "')' after port map",
           "FSIM-VHDL-PARSE-042");
    expect(TokenKind::Semicolon, "';' after VHDL instance",
           "FSIM-VHDL-PARSE-043");
    instance.span = span_from(label, previous());
    return instance;
  }

  void parse_vhdl_generic_map(
      Instance& instance,
      const Token& start) {
    expect_keyword("map", true, "FSIM-VHDL-PARSE-037");
    expect(
        TokenKind::LeftParen,
        "'(' after generic map",
        "FSIM-VHDL-PARSE-038");
    bool saw_named = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      const auto association_start = current();
      ParameterOverride actual;
      if (at(TokenKind::Identifier)
          && at(TokenKind::Arrow, 1)) {
        saw_named = true;
        const auto name = advance();
        advance();
        actual.name = vhdl_name(name.text);
        if (std::any_of(
                instance.parameter_overrides.begin(),
                instance.parameter_overrides.end(),
                [&](const ParameterOverride& existing) {
                  return existing.name == actual.name;
                })) {
          error(
              name,
              "FSIM-VHDL-SEM-015",
              "duplicate named generic actual '"
                  + *actual.name + "'");
        }
      } else if (saw_named) {
        error(
            current(),
            "FSIM-VHDL-SEM-016",
            "a positional generic actual cannot follow a named actual");
      }
      if (match_keyword("open", true)) {
        actual.value = Expression{
            ExpressionKind::Invalid,
            "open",
            {},
            previous().span};
        error(
            previous(),
            "FSIM-VHDL-UNSUPPORTED-019",
            "open generic actuals are not implemented in this frontend "
            "slice");
      } else {
        actual.value = parse_expression();
      }
      actual.span =
          cover(association_start.span, previous().span);
      instance.parameter_overrides.push_back(std::move(actual));
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after generic map",
        "FSIM-VHDL-PARSE-055");
    (void)start;
  }

  PortConnection parse_vhdl_port_connection() {
    const auto start = current();
    PortConnection connection;
    if (at(TokenKind::Identifier) && at(TokenKind::Arrow, 1)) {
      connection.port = vhdl_name(advance().text);
      advance();
    }

    bool simple_identifier = false;
    if (at(TokenKind::Identifier) && !keyword("open", 0, true)) {
      const auto actual = advance();
      connection.value =
          Expression{ExpressionKind::Identifier, vhdl_name(actual.text), {},
                     actual.span};
      simple_identifier =
          at(TokenKind::Comma) || at(TokenKind::RightParen);
    }

    if (!simple_identifier) {
      const auto unsupported = current();
      error(unsupported, "FSIM-VHDL-UNSUPPORTED-010",
            "port-map actuals must be simple identifiers in the "
            "vertical-slice frontend");
      skip_vhdl_connection_actual();
      const auto end = previous();
      connection.value.kind = ExpressionKind::Invalid;
      if (connection.value.text.empty()) {
        connection.value.text = unsupported.text;
        connection.value.span = unsupported.span;
      } else {
        connection.value.span = cover(connection.value.span, end.span);
      }
    }
    connection.span = cover(start.span, previous().span);
    return connection;
  }

  void skip_vhdl_connection_actual() {
    std::size_t parenthesis_depth = 0;
    std::size_t bracket_depth = 0;
    std::size_t brace_depth = 0;
    while (!at_end()) {
      if (at(TokenKind::Comma) && parenthesis_depth == 0 &&
          bracket_depth == 0 && brace_depth == 0) {
        return;
      }
      if (at(TokenKind::RightParen) && parenthesis_depth == 0 &&
          bracket_depth == 0 && brace_depth == 0) {
        return;
      }
      if (match(TokenKind::LeftParen)) {
        ++parenthesis_depth;
      } else if (at(TokenKind::RightParen)) {
        advance();
        if (parenthesis_depth != 0) {
          --parenthesis_depth;
        }
      } else if (match(TokenKind::LeftBracket)) {
        ++bracket_depth;
      } else if (at(TokenKind::RightBracket)) {
        advance();
        if (bracket_depth != 0) {
          --bracket_depth;
        }
      } else if (match(TokenKind::LeftBrace)) {
        ++brace_depth;
      } else if (at(TokenKind::RightBrace)) {
        advance();
        if (brace_depth != 0) {
          --brace_depth;
        }
      } else {
        advance();
      }
    }
  }

  Process parse_process(std::string label) {
    const auto start =
        expect_keyword("process", true, "FSIM-VHDL-PARSE-019");
    Process process;
    process.kind = ProcessKind::VhdlProcess;
    process.name = std::move(label);

    if (match(TokenKind::LeftParen)) {
      while (!at_end() && !at(TokenKind::RightParen)) {
        const auto signal = expect_identifier("sensitivity name");
        process.sensitivities.push_back(
            Sensitivity{EdgeKind::Any, vhdl_name(signal.text), signal.span});
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
      expect(TokenKind::RightParen, "')' after sensitivity list",
             "FSIM-VHDL-PARSE-020");
    }
    match_keyword("is", true);
    while (!at_end() && !keyword("begin", 0, true)) {
      if (!match_keyword("variable", true)) {
        const auto declaration = current();
        error(
            declaration,
            "FSIM-VHDL-UNSUPPORTED-007",
            "only process variable declarations are implemented in this "
            "declarative slice");
        while (!at_end() && !keyword("begin", 0, true)
               && !at(TokenKind::Semicolon)) {
          advance();
        }
        match(TokenKind::Semicolon);
        continue;
      }
      std::vector<Token> names;
      names.push_back(expect_identifier("variable name"));
      while (match(TokenKind::Comma)) {
        names.push_back(expect_identifier("variable name"));
      }
      expect(
          TokenKind::Colon,
          "':' after variable names",
          "FSIM-VHDL-PARSE-047");
      const auto type = parse_vhdl_type();
      std::optional<Expression> initializer;
      if (match(TokenKind::ColonEqual)) {
        initializer = parse_expression();
      }
      expect(
          TokenKind::Semicolon,
          "';' after variable declaration",
          "FSIM-VHDL-PARSE-048");
      for (const auto& name : names) {
        process.variables.push_back(VariableDeclaration{
            vhdl_name(name.text),
            type,
            initializer,
            span_from(name, previous())});
      }
    }
    expect_keyword("begin", true, "FSIM-VHDL-PARSE-021");
    sequential_loop_labels_seen_.clear();
    process.statements = parse_statement_list({"end"});
    sequential_loop_labels_seen_.clear();
    const auto contains_explicit_wait =
        [&](const auto& self,
            const std::vector<Statement>& statements) -> bool {
          for (const auto& statement : statements) {
            if (statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn
                || statement.kind == StatementKind::WaitUntil
                || self(self, statement.statements)
                || self(self, statement.else_statements)) {
              return true;
            }
          }
          return false;
        };
    if (!process.sensitivities.empty()
        && contains_explicit_wait(
            contains_explicit_wait, process.statements)) {
      error(
          start,
          "FSIM-VHDL-SEM-012",
          "a process sensitivity list cannot be combined with an explicit "
          "wait statement");
    }
    for (const auto& statement : process.statements) {
      if (contains_explicit_wait(
              contains_explicit_wait, statement.statements)
          || contains_explicit_wait(
              contains_explicit_wait, statement.else_statements)) {
        error(
            start,
            "FSIM-VHDL-UNSUPPORTED-017",
            "wait statements nested in conditional control flow require "
            "suspension-path analysis not implemented in this frontend "
            "slice");
        break;
      }
    }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-022");
    match_keyword("process", true);
    if (at(TokenKind::Identifier)) {
      advance();
    }
    expect(TokenKind::Semicolon, "';' after process",
           "FSIM-VHDL-PARSE-023");
    process.span = span_from(start, previous());
    infer_process_edge(process);
    return process;
  }

  std::vector<Statement> parse_statement_list(
      std::initializer_list<std::string_view> terminators) {
    std::vector<Statement> statements;
    while (!at_end()) {
      bool stop = false;
      for (const auto terminator : terminators) {
        stop = stop || keyword(terminator, 0, true);
      }
      if (stop) {
        break;
      }
      const auto before = position();
      if (auto statement = parse_sequential_statement()) {
        statements.push_back(std::move(*statement));
      }
      if (position() == before) {
        advance();
      }
    }
    return statements;
  }

  std::optional<Statement> parse_sequential_statement() {
    std::optional<Token> opening_loop_label;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Colon, 1)
        && (keyword("for", 2, true)
            || keyword("while", 2, true)
            || keyword("loop", 2, true))) {
      opening_loop_label = advance();
      advance();
    }
    if (match_keyword("wait", true)) {
      const auto start = previous();
      Statement statement;
      bool has_sensitivity_clause = false;
      bool has_condition_clause = false;
      if (match_keyword("on", true)) {
        has_sensitivity_clause = true;
        do {
          const auto signal = expect_identifier("wait sensitivity name");
          statement.sensitivities.push_back(Sensitivity{
              EdgeKind::Any, vhdl_name(signal.text), signal.span});
        } while (match(TokenKind::Comma));
      }
      if (match_keyword("until", true)) {
        has_condition_clause = true;
        statement.condition = parse_expression();
      }
      if (match_keyword("for", true)) {
        statement.delay = parse_vhdl_delay(previous());
      }
      if (has_condition_clause) {
        statement.kind = StatementKind::WaitUntil;
      } else if (has_sensitivity_clause) {
        statement.kind = StatementKind::WaitOn;
      } else if (statement.delay) {
        statement.kind = StatementKind::Delay;
      } else {
        // The default sensitivity set is empty, so both a bare wait and
        // `wait until true` suspend permanently.
        statement.kind = StatementKind::WaitUntil;
        statement.condition = Expression{
            ExpressionKind::BooleanLiteral,
            "true",
            {},
            start.span};
      }
      expect(
          TokenKind::Semicolon,
          "';' after wait statement",
          "FSIM-VHDL-PARSE-049");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("assert", true)) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::Assert;
      statement.condition = parse_expression();
      if (match_keyword("report", true)) {
        const auto message = expect(
            TokenKind::StringLiteral, "string literal after report",
            "FSIM-VHDL-PARSE-045");
        statement.assertion_message = string_literal_text(message);
      }
      if (match_keyword("severity", true)) {
        const auto severity = expect_identifier("assertion severity");
        const auto canonical = detail::ascii_lower(severity.text);
        if (canonical == "note") {
          statement.assertion_severity = AssertionSeverity::Note;
        } else if (canonical == "warning") {
          statement.assertion_severity = AssertionSeverity::Warning;
        } else if (canonical == "error") {
          statement.assertion_severity = AssertionSeverity::Error;
        } else if (canonical == "failure") {
          statement.assertion_severity = AssertionSeverity::Failure;
        } else {
          error(
              severity, "FSIM-VHDL-SEM-011",
              "assertion severity must be note, warning, error, or failure");
        }
      }
      expect(TokenKind::Semicolon, "';' after assertion",
             "FSIM-VHDL-PARSE-046");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("if", true)) {
      const auto start = previous();
      auto statement = parse_if_branch(start);
      expect_keyword("end", true, "FSIM-VHDL-PARSE-024");
      expect_keyword("if", true, "FSIM-VHDL-PARSE-025");
      expect(TokenKind::Semicolon, "';' after if statement",
             "FSIM-VHDL-PARSE-026");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("case", true)) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::Case;
      statement.condition = parse_expression();
      expect_keyword("is", true, "FSIM-VHDL-PARSE-094");
      bool saw_others = false;
      while (!at_end() && !keyword("end", 0, true)) {
        expect_keyword("when", true, "FSIM-VHDL-PARSE-095");
        if (saw_others) {
          error(
              previous(),
              "FSIM-VHDL-SEM-022",
              "the others alternative must be last in a VHDL case "
              "statement");
        }
        CaseAlternative alternative;
        const auto alternative_start = previous();
        if (match_keyword("others", true)) {
          alternative.is_default = true;
          if (saw_others) {
            error(
                previous(),
                "FSIM-VHDL-SEM-021",
                "a VHDL case statement contains more than one others "
                "alternative");
          }
          saw_others = true;
        } else {
          do {
            alternative.choices.push_back(parse_expression());
          } while (match(TokenKind::Pipe));
        }
        expect(
            TokenKind::Arrow,
            "'=>' after VHDL case choices",
            "FSIM-VHDL-PARSE-096");
        alternative.statements =
            parse_statement_list({"when", "end"});
        alternative.span =
            span_from(alternative_start, previous());
        statement.case_alternatives.push_back(
            std::move(alternative));
      }
      expect_keyword("end", true, "FSIM-VHDL-PARSE-097");
      expect_keyword("case", true, "FSIM-VHDL-PARSE-098");
      expect(
          TokenKind::Semicolon,
          "';' after VHDL case statement",
          "FSIM-VHDL-PARSE-099");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("for", true)) {
      const auto start =
          opening_loop_label.value_or(previous());
      Statement statement;
      statement.kind = StatementKind::Loop;
      statement.loop_label =
          opening_loop_label
              ? vhdl_name(opening_loop_label->text)
              : std::string{};
      const auto variable =
          expect_identifier("for-loop parameter");
      statement.loop_variable = vhdl_name(variable.text);
      expect_keyword("in", true, "FSIM-VHDL-PARSE-100");
      statement.loop_initial = parse_expression();
      if (match_keyword("to", true)) {
        statement.loop_descending = false;
      } else if (match_keyword("downto", true)) {
        statement.loop_descending = true;
      } else {
        error(
            current(),
            "FSIM-VHDL-PARSE-101",
            "expected 'to' or 'downto' in sequential for-loop range");
      }
      statement.loop_limit = parse_expression();
      expect_keyword("loop", true, "FSIM-VHDL-PARSE-102");
      validate_opening_loop_label(
          statement.loop_label,
          opening_loop_label.value_or(start));
      ++sequential_loop_depth_;
      sequential_loop_labels_.push_back(
          statement.loop_label);
      statement.statements = parse_statement_list({"end"});
      sequential_loop_labels_.pop_back();
      --sequential_loop_depth_;
      expect_keyword("end", true, "FSIM-VHDL-PARSE-103");
      expect_keyword("loop", true, "FSIM-VHDL-PARSE-104");
      parse_loop_end_label(statement.loop_label);
      expect(
          TokenKind::Semicolon,
          "';' after sequential for loop",
          "FSIM-VHDL-PARSE-105");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("while", true)) {
      const auto start =
          opening_loop_label.value_or(previous());
      Statement statement;
      statement.kind = StatementKind::Loop;
      statement.loop_label =
          opening_loop_label
              ? vhdl_name(opening_loop_label->text)
              : std::string{};
      statement.loop_runtime = true;
      statement.condition = parse_expression();
      expect_keyword("loop", true, "FSIM-VHDL-PARSE-106");
      validate_opening_loop_label(
          statement.loop_label,
          opening_loop_label.value_or(start));
      ++sequential_loop_depth_;
      sequential_loop_labels_.push_back(
          statement.loop_label);
      statement.statements = parse_statement_list({"end"});
      sequential_loop_labels_.pop_back();
      --sequential_loop_depth_;
      expect_keyword("end", true, "FSIM-VHDL-PARSE-107");
      expect_keyword("loop", true, "FSIM-VHDL-PARSE-108");
      parse_loop_end_label(statement.loop_label);
      expect(
          TokenKind::Semicolon,
          "';' after sequential while loop",
          "FSIM-VHDL-PARSE-109");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("loop", true)) {
      const auto start =
          opening_loop_label.value_or(previous());
      Statement statement;
      statement.kind = StatementKind::Loop;
      statement.loop_label =
          opening_loop_label
              ? vhdl_name(opening_loop_label->text)
              : std::string{};
      statement.loop_runtime = true;
      statement.condition = Expression{
          ExpressionKind::BooleanLiteral,
          "true",
          {},
          start.span};
      validate_opening_loop_label(
          statement.loop_label,
          opening_loop_label.value_or(start));
      ++sequential_loop_depth_;
      sequential_loop_labels_.push_back(
          statement.loop_label);
      statement.statements = parse_statement_list({"end"});
      sequential_loop_labels_.pop_back();
      --sequential_loop_depth_;
      expect_keyword("end", true, "FSIM-VHDL-PARSE-111");
      expect_keyword("loop", true, "FSIM-VHDL-PARSE-112");
      parse_loop_end_label(statement.loop_label);
      expect(
          TokenKind::Semicolon,
          "';' after unconditional sequential loop",
          "FSIM-VHDL-PARSE-113");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (keyword("exit", 0, true) || keyword("next", 0, true)) {
      const auto start = advance();
      const auto is_exit = detail::iequals(start.text, "exit");
      if (sequential_loop_depth_ == 0) {
        error(
            start,
            "FSIM-VHDL-SEM-023",
            std::string{"a VHDL "}
                + (is_exit ? "exit" : "next")
                + " statement must be nested in a sequential loop");
      }
      Statement control;
      control.kind =
          is_exit ? StatementKind::Break : StatementKind::Continue;
      control.span = start.span;
      if (at(TokenKind::Identifier)
          && !keyword("when", 0, true)) {
        const auto label = advance();
        control.loop_control_label =
            vhdl_name(label.text);
        if (sequential_loop_depth_ != 0
            && std::ranges::find(
                   sequential_loop_labels_,
                   control.loop_control_label)
                == sequential_loop_labels_.end()) {
          error(
              label,
              "FSIM-VHDL-SEM-024",
              "target loop label '"
                  + control.loop_control_label
                  + "' is not visible at this exit or next statement");
        }
      }
      if (match_keyword("when", true)) {
        Statement conditional;
        conditional.kind = StatementKind::If;
        conditional.condition = parse_expression();
        control.span = span_from(start, previous());
        conditional.statements.push_back(std::move(control));
        expect(
            TokenKind::Semicolon,
            "';' after VHDL exit or next statement",
            "FSIM-VHDL-PARSE-110");
        conditional.span = span_from(start, previous());
        return conditional;
      }
      expect(
          TokenKind::Semicolon,
          "';' after VHDL exit or next statement",
          "FSIM-VHDL-PARSE-110");
      control.span = span_from(start, previous());
      return control;
    }
    if (match_keyword("null", true)) {
      const auto start = previous();
      expect(TokenKind::Semicolon, "';' after null",
             "FSIM-VHDL-PARSE-027");
      Statement statement;
      statement.kind = StatementKind::Null;
      statement.span = span_from(start, previous());
      return statement;
    }
    if (auto assignment = parse_assignment(false)) {
      return assignment;
    }
    const auto unsupported = advance();
    error(unsupported, "FSIM-VHDL-UNSUPPORTED-008",
          "unsupported sequential statement starting with '" +
              unsupported.text + "'");
    skip_to_semicolon();
    return std::nullopt;
  }

  void validate_opening_loop_label(
      const std::string_view label,
      const Token& token) {
    if (label.empty()) {
      return;
    }
    if (std::ranges::find(
            sequential_loop_labels_seen_, label)
        != sequential_loop_labels_seen_.end()) {
      error(
          token,
          "FSIM-VHDL-SEM-026",
          "sequential loop label '"
              + std::string{label}
              + "' duplicates another label in this process");
      return;
    }
    sequential_loop_labels_seen_.emplace_back(label);
  }

  void parse_loop_end_label(
      const std::string_view opening_label) {
    if (!at(TokenKind::Identifier)) {
      return;
    }
    const auto end_label = advance();
    const auto canonical =
        vhdl_name(end_label.text);
    if (opening_label.empty()) {
      error(
          end_label,
          "FSIM-VHDL-SEM-025",
          "an end-loop label requires a matching opening loop label");
    } else if (canonical != opening_label) {
      error(
          end_label,
          "FSIM-VHDL-SEM-025",
          "end-loop label '" + canonical
              + "' does not match opening label '"
              + std::string{opening_label} + "'");
    }
  }

  Statement parse_if_branch(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::If;
    statement.condition = parse_expression();
    expect_keyword("then", true, "FSIM-VHDL-PARSE-028");
    statement.statements = parse_statement_list({"elsif", "else", "end"});
    if (match_keyword("elsif", true)) {
      const auto elsif = previous();
      statement.else_statements.push_back(parse_if_branch(elsif));
    } else if (match_keyword("else", true)) {
      statement.else_statements = parse_statement_list({"end"});
    }
    statement.span = span_from(start, previous());
    return statement;
  }

  std::optional<Statement> parse_assignment(bool concurrent) {
    const auto start_position = position();
    if (!at(TokenKind::Identifier)) {
      return std::nullopt;
    }
    const auto start = current();
    Expression target = parse_lvalue();
    AssignmentKind kind = AssignmentKind::VhdlSignal;
    if (match(TokenKind::LessEqual)) {
      kind = concurrent ? AssignmentKind::Continuous
                        : AssignmentKind::VhdlSignal;
    } else if (!concurrent && match(TokenKind::ColonEqual)) {
      kind = AssignmentKind::Blocking;
    } else {
      rewind(start_position);
      return std::nullopt;
    }

    Statement statement;
    statement.kind = StatementKind::Assignment;
    statement.assignment_kind = kind;
    statement.target = std::move(target);
    statement.value = parse_expression();
    if (match_keyword("after", true)) {
      statement.delay = parse_vhdl_delay(previous());
    }
    expect(TokenKind::Semicolon, "';' after assignment",
           "FSIM-VHDL-PARSE-029");
    statement.span = span_from(start, previous());
    return statement;
  }

  Delay parse_vhdl_delay(const Token& start) {
    Delay delay;
    const auto magnitude =
        expect(TokenKind::Number, "delay magnitude", "FSIM-VHDL-PARSE-030");
    if (const auto parsed = decimal_u64(magnitude.text)) {
      delay.magnitude = *parsed;
    } else {
      error(magnitude, "FSIM-VHDL-SEM-004",
            "delay magnitude must be an integer literal");
    }
    const auto unit = expect_identifier("physical time unit");
    delay.unit = detail::ascii_lower(unit.text);
    delay.span = span_from(start, unit);
    return delay;
  }

  Expression parse_lvalue() {
    const auto name = expect_identifier("assignment target");
    Expression expression{ExpressionKind::Identifier, vhdl_name(name.text),
                          {}, name.span};
    while (match(TokenKind::LeftParen)) {
      const auto open = previous();
      Expression first = parse_expression();
      if (match_keyword("downto", true) || match_keyword("to", true)) {
        const auto direction = previous();
        Expression second = parse_expression();
        expect(TokenKind::RightParen, "')' after slice",
               "FSIM-VHDL-PARSE-031");
        expression =
            Expression{ExpressionKind::Slice, detail::ascii_lower(direction.text),
                       {std::move(expression), std::move(first),
                        std::move(second)},
                       cover(expression.span, previous().span)};
      } else {
        expect(TokenKind::RightParen, "')' after index",
               "FSIM-VHDL-PARSE-032");
        expression =
            Expression{ExpressionKind::Index, "index",
                       {std::move(expression), std::move(first)},
                       cover(expression.span, previous().span)};
      }
      (void)open;
    }
    return expression;
  }

  Expression parse_expression(int minimum_precedence = 0) {
    Expression left = parse_unary();
    for (;;) {
      const auto operation = binary_operation();
      if (!operation || operation->precedence < minimum_precedence) {
        break;
      }
      const auto operator_token = advance();
      Expression right = parse_expression(operation->precedence + 1);
      const auto combined_span = cover(left.span, right.span);
      left = Expression{ExpressionKind::Binary, operation->name,
                        {std::move(left), std::move(right)}, combined_span};
      (void)operator_token;
    }
    return left;
  }

  struct BinaryOperation {
    int precedence;
    std::string name;
  };

  std::optional<BinaryOperation> binary_operation() const {
    if (keyword("or", 0, true) || keyword("nor", 0, true) ||
        keyword("xor", 0, true) || keyword("xnor", 0, true)) {
      return BinaryOperation{1, detail::ascii_lower(current().text)};
    }
    if (keyword("and", 0, true) || keyword("nand", 0, true)) {
      return BinaryOperation{2, detail::ascii_lower(current().text)};
    }
    if (at(TokenKind::Assign)
        || (at(TokenKind::NotEqual) && current().text == "/=")
        || at(TokenKind::Less) || at(TokenKind::LessEqual) ||
        at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
      return BinaryOperation{3, current().text};
    }
    if (keyword("sll", 0, true) || keyword("srl", 0, true)
        || keyword("sra", 0, true)) {
      return BinaryOperation{
          4, detail::ascii_lower(current().text)};
    }
    if (at(TokenKind::Ampersand) || at(TokenKind::Plus) ||
        at(TokenKind::Minus)) {
      return BinaryOperation{5, current().text};
    }
    if (at(TokenKind::Star) || at(TokenKind::Slash) ||
        keyword("mod", 0, true) || keyword("rem", 0, true)) {
      return BinaryOperation{6, detail::ascii_lower(current().text)};
    }
    return std::nullopt;
  }

  Expression parse_unary() {
    if (at(TokenKind::Plus) || at(TokenKind::Minus) ||
        keyword("not", 0, true) || keyword("abs", 0, true)) {
      const auto operation = advance();
      Expression operand = parse_unary();
      return Expression{ExpressionKind::Unary,
                        detail::ascii_lower(operation.text),
                        {std::move(operand)},
                        cover(operation.span, operand.span)};
    }
    return parse_primary();
  }

  Expression parse_primary() {
    if (keyword("true", 0, true) || keyword("false", 0, true)) {
      const auto token = advance();
      return Expression{
          ExpressionKind::BooleanLiteral,
          vhdl_name(token.text),
          {},
          token.span};
    }
    if (at(TokenKind::Number)) {
      const auto token = advance();
      return Expression{ExpressionKind::IntegerLiteral, token.text, {},
                        token.span};
    }
    if (at(TokenKind::CharacterLiteral)) {
      const auto token = advance();
      return Expression{ExpressionKind::LogicLiteral, token.text, {},
                        token.span};
    }
    if (at(TokenKind::StringLiteral)) {
      const auto token = advance();
      return Expression{ExpressionKind::StringLiteral, token.text, {},
                        token.span};
    }
    if (at(TokenKind::Identifier)) {
      const auto name = advance();
      std::string canonical = vhdl_name(name.text);
      while (match(TokenKind::Dot)) {
        canonical += '.';
        canonical += vhdl_name(expect_identifier("selected name").text);
      }
      if (match(TokenKind::LeftParen)) {
        const auto base =
            Expression{
                ExpressionKind::Identifier,
                canonical,
                {},
                name.span};
        std::vector<Expression> arguments;
        if (!at(TokenKind::RightParen)) {
          auto first = parse_expression();
          if (match_keyword("downto", true)
              || match_keyword("to", true)) {
            const auto direction = previous();
            auto second = parse_expression();
            expect(TokenKind::RightParen, "')' after slice",
                   "FSIM-VHDL-PARSE-033");
            return Expression{
                ExpressionKind::Slice,
                detail::ascii_lower(direction.text),
                {base, std::move(first), std::move(second)},
                cover(name.span, previous().span)};
          }
          arguments.push_back(std::move(first));
          while (match(TokenKind::Comma)) {
            arguments.push_back(parse_expression());
          }
        }
        expect(TokenKind::RightParen, "')' after arguments",
               "FSIM-VHDL-PARSE-033");
        return Expression{ExpressionKind::Call, std::move(canonical),
                          std::move(arguments),
                          cover(name.span, previous().span)};
      }
      return Expression{ExpressionKind::Identifier, std::move(canonical), {},
                        name.span};
    }
    if (match(TokenKind::LeftParen)) {
      const auto open = previous();
      Expression expression = parse_expression();
      expect(TokenKind::RightParen, "')' after expression",
             "FSIM-VHDL-PARSE-034");
      expression.span = span_from(open, previous());
      return expression;
    }

    const auto invalid = advance();
    error(invalid, "FSIM-VHDL-PARSE-035", "expected expression");
    return Expression{ExpressionKind::Invalid, invalid.text, {},
                      invalid.span};
  }

  static void infer_process_edge(Process& process) {
    if (process.statements.size() != 1
        || process.statements.front().kind != StatementKind::If
        || !process.statements.front().else_statements.empty()) {
      return;
    }
    const auto& condition = process.statements.front().condition;
    if (condition.kind != ExpressionKind::Call ||
        condition.operands.size() != 1 ||
        condition.operands.front().kind != ExpressionKind::Identifier) {
      return;
    }
    EdgeKind edge = EdgeKind::Any;
    if (condition.text == "rising_edge") {
      edge = EdgeKind::Positive;
    } else if (condition.text == "falling_edge") {
      edge = EdgeKind::Negative;
    } else {
      return;
    }
    const auto& signal = condition.operands.front().text;
    for (auto& sensitivity : process.sensitivities) {
      if (sensitivity.signal == signal) {
        sensitivity.edge = edge;
      }
    }
  }

  std::size_t sequential_loop_depth_{};
  std::vector<std::string> sequential_loop_labels_;
  std::vector<std::string> sequential_loop_labels_seen_;
};

}  // namespace

ParseResult parse_vhdl(SourceText source) {
  return VhdlParser(lex(std::move(source), Language::Vhdl2008)).run();
}

}  // namespace fsim::frontend
