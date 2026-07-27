// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"

#include "parser_support.hpp"

#include <algorithm>
#include <cstdint>
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

class VhdlParser final : private detail::ParserBase {
 public:
  explicit VhdlParser(LexResult lexed)
      : ParserBase(std::move(lexed.tokens),
                   std::move(lexed.diagnostics)) {}

  ParseResult run() {
    ParsedDesign design;
    std::vector<VhdlContextItem> pending_context;
    while (!at_end()) {
      if (any_keyword({"library", "use", "context"}, true)) {
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
          "VHDL context declarations are not implemented in this "
          "frontend slice");
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
        const auto generic = previous();
        error(generic, "FSIM-VHDL-UNSUPPORTED-002",
              "generic clauses are not implemented in the vertical-slice "
              "frontend");
        if (at(TokenKind::LeftParen)) {
          skip_balanced(TokenKind::LeftParen, TokenKind::RightParen);
        }
        match(TokenKind::Semicolon);
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

  Type parse_vhdl_type() {
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
    } else if (type.domain == ValueDomain::Integer) {
      error(
          first,
          "FSIM-VHDL-UNSUPPORTED-014",
          "VHDL integer-family objects are parsed but not executable in this "
          "frontend slice");
    }

    if (match(TokenKind::LeftParen)) {
      const auto range_start = previous();
      const auto left = parse_signed_decimal();
      bool descending = true;
      if (match_keyword("downto", true)) {
        descending = true;
      } else if (match_keyword("to", true)) {
        descending = false;
      } else {
        error(current(), "FSIM-VHDL-PARSE-009",
              "only locally static integer ranges are supported here");
      }
      const auto right = parse_signed_decimal();
      expect(TokenKind::RightParen, "')' after range",
             "FSIM-VHDL-PARSE-010");
      if (left && right) {
        type.packed_range = PackedRange{*left, *right, descending};
      } else {
        error(range_start, "FSIM-VHDL-SEM-001",
              "range bounds must be decimal integer literals in this "
              "frontend slice");
      }
    }
    return type;
  }

  std::optional<std::int64_t> parse_signed_decimal() {
    const bool negative = match(TokenKind::Minus);
    const auto number =
        expect(TokenKind::Number, "integer literal", "FSIM-VHDL-PARSE-011");
    return decimal_i64(number.text, negative);
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
        parse_signal_declaration(unit);
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

  void parse_signal_declaration(DesignUnit& unit) {
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
          unit.signals.begin(),
          unit.signals.end(),
          [&](const SignalDeclaration& signal) {
            return signal.name == canonical;
          });
      if (duplicate != unit.signals.end()) {
        error(
            name,
            "FSIM-VHDL-SEM-003",
            "duplicate signal declaration '" + canonical + "'");
      } else {
        unit.signals.push_back(SignalDeclaration{
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
      const auto generic = previous();
      error(generic, "FSIM-VHDL-UNSUPPORTED-009",
            "generic maps are not implemented in the vertical-slice "
            "frontend");
      expect_keyword("map", true, "FSIM-VHDL-PARSE-037");
      if (at(TokenKind::LeftParen)) {
        skip_balanced(TokenKind::LeftParen, TokenKind::RightParen);
      } else {
        error(current(), "FSIM-VHDL-PARSE-038",
              "expected '(' after generic map");
      }
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
    process.statements = parse_statement_list({"end"});
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
    if (at(TokenKind::Assign) || at(TokenKind::NotEqual) ||
        at(TokenKind::Less) || at(TokenKind::LessEqual) ||
        at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
      return BinaryOperation{3, current().text};
    }
    if (at(TokenKind::Ampersand) || at(TokenKind::Plus) ||
        at(TokenKind::Minus)) {
      return BinaryOperation{4, current().text};
    }
    if (at(TokenKind::Star) || at(TokenKind::Slash) ||
        keyword("mod", 0, true) || keyword("rem", 0, true)) {
      return BinaryOperation{5, detail::ascii_lower(current().text)};
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
        std::vector<Expression> arguments;
        if (!at(TokenKind::RightParen)) {
          do {
            arguments.push_back(parse_expression());
          } while (match(TokenKind::Comma));
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
};

}  // namespace

ParseResult parse_vhdl(SourceText source) {
  return VhdlParser(lex(std::move(source), Language::Vhdl2008)).run();
}

}  // namespace fsim::frontend
