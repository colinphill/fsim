// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/parser.hpp"

#include "parser_support.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

using detail::decimal_i64;
using detail::decimal_u64;

struct VerilogTypeSpec {
  Type type;
  PortDirection direction{PortDirection::Unknown};
};

class VerilogParser final : private detail::ParserBase {
 public:
  VerilogParser(LexResult lexed, bool system_verilog)
      : ParserBase(std::move(lexed.tokens),
                   std::move(lexed.diagnostics)),
        language_(system_verilog ? Language::SystemVerilog2017
                                 : Language::Verilog2005) {}

  ParseResult run() {
    ParsedDesign design;
    while (!at_end()) {
      if (match_keyword("module")) {
        design.units.push_back(parse_module(previous()));
      } else if (at(TokenKind::Backtick)) {
        parse_directive();
      } else {
        const auto unexpected = advance();
        error(unexpected, "FSIM-SV-UNSUPPORTED-001",
              "unsupported compilation-unit item '" + unexpected.text + "'");
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

  Token expect_identifier(std::string_view description) {
    return expect(TokenKind::Identifier, description, "FSIM-SV-PARSE-001");
  }

  void parse_directive() {
    const auto tick = advance();
    const auto directive = at(TokenKind::Identifier) ? advance() : current();
    if (directive.text == "timescale") {
      parse_timescale(directive);
    } else if (directive.text == "default_nettype") {
      error(
          directive,
          "FSIM-SV-UNSUPPORTED-013",
          "`default_nettype is recognized but not executable until the "
          "preprocessor and implicit-net legality rules are implemented");
    } else {
      error(directive, "FSIM-SV-UNSUPPORTED-002",
            "preprocessor directive `" + directive.text +
                " requires the future preprocessing stage");
    }
    const auto line = tick.span.begin.line;
    while (!at_end() && current().span.begin.line == line) {
      advance();
    }
  }

  static std::optional<std::uint64_t> time_unit_femtoseconds(
      const std::string_view unit) {
    if (unit == "fs") {
      return 1;
    }
    if (unit == "ps") {
      return 1'000;
    }
    if (unit == "ns") {
      return 1'000'000;
    }
    if (unit == "us") {
      return 1'000'000'000;
    }
    if (unit == "ms") {
      return 1'000'000'000'000;
    }
    if (unit == "s") {
      return 1'000'000'000'000'000;
    }
    return std::nullopt;
  }

  void parse_timescale(const Token& directive) {
    const auto unit_magnitude_token = expect(
        TokenKind::Number,
        "time-unit magnitude after `timescale",
        "FSIM-SV-PARSE-039");
    const auto unit_token = expect(
        TokenKind::Identifier,
        "time-unit name after `timescale magnitude",
        "FSIM-SV-PARSE-040");
    expect(
        TokenKind::Slash,
        "'/' between `timescale unit and precision",
        "FSIM-SV-PARSE-041");
    const auto precision_magnitude_token = expect(
        TokenKind::Number,
        "time-precision magnitude after '/'",
        "FSIM-SV-PARSE-042");
    const auto precision_token = expect(
        TokenKind::Identifier,
        "time-precision unit",
        "FSIM-SV-PARSE-043");

    const auto unit_magnitude =
        decimal_u64(unit_magnitude_token.text);
    const auto precision_magnitude =
        decimal_u64(precision_magnitude_token.text);
    const auto unit_factor = time_unit_femtoseconds(unit_token.text);
    const auto precision_factor =
        time_unit_femtoseconds(precision_token.text);
    const auto legal_magnitude = [](const std::uint64_t value) {
      return value == 1 || value == 10 || value == 100;
    };
    if (!unit_magnitude || !precision_magnitude
        || !legal_magnitude(*unit_magnitude)
        || !legal_magnitude(*precision_magnitude)
        || !unit_factor || !precision_factor) {
      error(
          directive,
          "FSIM-SV-SEM-007",
          "`timescale magnitudes must be 1, 10, or 100 and units must "
          "be fs, ps, ns, us, ms, or s");
      return;
    }
    if (*unit_magnitude
            > std::numeric_limits<std::uint64_t>::max() / *unit_factor
        || *precision_magnitude
            > std::numeric_limits<std::uint64_t>::max()
                / *precision_factor) {
      error(
          directive,
          "FSIM-SV-SEM-008",
          "`timescale value exceeds fsim's 64-bit time range");
      return;
    }
    const auto unit_fs = *unit_magnitude * *unit_factor;
    const auto precision_fs = *precision_magnitude * *precision_factor;
    if (precision_fs > unit_fs) {
      error(
          directive,
          "FSIM-SV-SEM-009",
          "`timescale precision cannot be coarser than its time unit");
      return;
    }
    current_time_unit_magnitude_ = *unit_magnitude;
    current_time_unit_ = unit_token.text;
    current_time_precision_ =
        std::to_string(*precision_magnitude) + precision_token.text;
  }

  DesignUnit parse_module(const Token& start) {
    non_ansi_ports_.clear();
    body_port_declarations_.clear();
    port_type_refinements_.clear();
    module_time_unit_magnitude_ = current_time_unit_magnitude_;
    module_time_unit_ = current_time_unit_;
    module_time_precision_ = current_time_precision_;
    DesignUnit unit;
    unit.kind = UnitKind::VerilogModule;
    unit.language = language_;
    if (!module_time_unit_.empty()) {
      unit.time_unit =
          std::to_string(module_time_unit_magnitude_)
          + module_time_unit_;
      unit.time_precision = module_time_precision_;
    }
    const auto name = expect_identifier("module name");
    unit.name = name.text;

    if (match(TokenKind::Hash)) {
      error(previous(), "FSIM-SV-UNSUPPORTED-003",
            "parameter port lists are not implemented in this frontend "
            "slice");
      if (at(TokenKind::LeftParen)) {
        skip_balanced(TokenKind::LeftParen, TokenKind::RightParen);
      }
    }

    if (match(TokenKind::LeftParen)) {
      parse_module_ports(unit);
      expect(TokenKind::RightParen, "')' after module ports",
             "FSIM-SV-PARSE-002");
    }
    expect(TokenKind::Semicolon, "';' after module header",
           "FSIM-SV-PARSE-003");

    while (!at_end() && !keyword("endmodule")) {
      if (is_declaration_start()) {
        parse_declaration(unit);
      } else if (match_keyword("assign")) {
        if (auto assignment = parse_continuous_assignment(previous())) {
          unit.concurrent_statements.push_back(std::move(*assignment));
        }
      } else if (
          keyword("always") || keyword("always_ff")
          || keyword("always_comb") || keyword("always_latch")) {
        unit.processes.push_back(parse_always());
      } else if (keyword("initial")) {
        unit.processes.push_back(parse_initial());
      } else if (
          at(TokenKind::Identifier) && at(TokenKind::Identifier, 1)
          && at(TokenKind::LeftParen, 2)) {
        unit.instances.push_back(parse_instance());
      } else if (at(TokenKind::Backtick)) {
        parse_directive();
      } else {
        const auto unexpected = advance();
        error(unexpected, "FSIM-SV-UNSUPPORTED-004",
              "unsupported module item starting with '" + unexpected.text +
                  "'");
        skip_to_semicolon();
      }
    }
    expect_keyword("endmodule", false, "FSIM-SV-PARSE-004");
    if (match(TokenKind::Colon)) {
      expect_identifier("module name after endmodule");
    }
    unit.span = span_from(start, previous());
    return unit;
  }

  Instance parse_instance() {
    const auto start = expect_identifier("instantiated module name");
    const auto name = expect_identifier("instance name");
    Instance instance;
    instance.unit_name = start.text;
    instance.name = name.text;
    expect(
        TokenKind::LeftParen, "'(' after instance name",
        "FSIM-SV-PARSE-034");
    while (!at_end() && !at(TokenKind::RightParen)) {
      PortConnection connection;
      const auto connection_start = current();
      if (match(TokenKind::Dot)) {
        const auto port = expect_identifier("port name");
        connection.port = port.text;
        expect(
            TokenKind::LeftParen, "'(' after named port",
            "FSIM-SV-PARSE-035");
        connection.value = parse_expression();
        expect(
            TokenKind::RightParen, "')' after named port connection",
            "FSIM-SV-PARSE-036");
      } else {
        connection.value = parse_expression();
      }
      connection.span = cover(connection_start.span, previous().span);
      instance.connections.push_back(std::move(connection));
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::RightParen, "')' after instance connections",
        "FSIM-SV-PARSE-037");
    expect(
        TokenKind::Semicolon, "';' after module instance",
        "FSIM-SV-PARSE-038");
    instance.span = span_from(start, previous());
    return instance;
  }

  void parse_module_ports(DesignUnit& unit) {
    VerilogTypeSpec inherited;
    bool have_inherited_type = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      if (at(TokenKind::Dot)) {
        const auto dot = advance();
        error(dot, "FSIM-SV-UNSUPPORTED-005",
              "named port connections are not valid in a module declaration");
        skip_to_port_delimiter();
      } else {
        VerilogTypeSpec spec = inherited;
        bool declared_here = false;
        if (is_direction_keyword()) {
          spec.direction = parse_direction();
          spec.type = default_verilog_type();
          declared_here = true;
          parse_optional_net_type(spec.type);
          parse_optional_signedness(spec.type);
          parse_optional_range(spec.type);
          inherited = spec;
          have_inherited_type = true;
        } else if (!have_inherited_type) {
          spec.type = Type{ValueDomain::Unknown, {}, std::nullopt, false};
          spec.direction = PortDirection::Unknown;
        }

        const auto port_name = expect_identifier("port name");
        SignalDeclaration declaration{
            port_name.text, spec.type, spec.direction, true, port_name.span};
        if (spec.direction == PortDirection::Unknown) {
          non_ansi_ports_.insert(port_name.text);
        }
        if (match(TokenKind::Assign)) {
          const auto initializer = previous();
          (void)parse_expression();
          error(
              initializer,
              "FSIM-SV-UNSUPPORTED-010",
              "ANSI port default expressions are not executable in this "
              "frontend slice");
        }
        if (at(TokenKind::LeftBracket)) {
          const auto dimension = current();
          error(dimension, "FSIM-SV-UNSUPPORTED-006",
                "unpacked port dimensions are not implemented in this "
                "frontend slice");
          skip_balanced(TokenKind::LeftBracket, TokenKind::RightBracket);
        }
        const auto duplicate = std::find_if(
            unit.ports.begin(),
            unit.ports.end(),
            [&](const SignalDeclaration& port) {
              return port.name == port_name.text;
            });
        if (duplicate != unit.ports.end()) {
          error(
              port_name,
              "FSIM-SV-SEM-003",
              "duplicate module port declaration '" + port_name.text + "'");
        } else {
          unit.ports.push_back(std::move(declaration));
        }
        (void)declared_here;
      }

      if (!match(TokenKind::Comma)) {
        break;
      }
    }
  }

  void skip_to_port_delimiter() {
    std::size_t depth = 0;
    while (!at_end()) {
      if (at(TokenKind::LeftParen) || at(TokenKind::LeftBracket) ||
          at(TokenKind::LeftBrace)) {
        ++depth;
      } else if (at(TokenKind::RightParen) ||
                 at(TokenKind::RightBracket) ||
                 at(TokenKind::RightBrace)) {
        if (depth == 0) {
          return;
        }
        --depth;
      } else if (at(TokenKind::Comma) && depth == 0) {
        return;
      }
      advance();
    }
  }

  [[nodiscard]] bool is_direction_keyword() const {
    return keyword("input") || keyword("output") || keyword("inout");
  }

  PortDirection parse_direction() {
    if (match_keyword("input")) {
      return PortDirection::Input;
    }
    if (match_keyword("output")) {
      return PortDirection::Output;
    }
    if (match_keyword("inout")) {
      return PortDirection::Inout;
    }
    return PortDirection::Unknown;
  }

  static Type default_verilog_type() {
    return Type{ValueDomain::Logic4, "wire", std::nullopt, false};
  }

  [[nodiscard]] bool is_net_type_keyword() const {
    return any_keyword({"wire", "reg", "logic", "bit", "integer"});
  }

  void parse_optional_net_type(Type& type) {
    if (!is_net_type_keyword()) {
      return;
    }
    const auto keyword_token = advance();
    type.spelling = keyword_token.text;
    if (keyword_token.text == "bit") {
      type.domain = ValueDomain::Bit2;
    } else if (keyword_token.text == "integer") {
      type.domain = ValueDomain::Integer;
      type.is_signed = true;
      error(
          keyword_token,
          "FSIM-SV-UNSUPPORTED-012",
          "integer objects are parsed but not executable in this frontend "
          "slice");
    } else {
      type.domain = ValueDomain::Logic4;
    }
  }

  void parse_optional_signedness(Type& type) {
    if (match_keyword("signed")) {
      type.is_signed = true;
    } else if (match_keyword("unsigned")) {
      type.is_signed = false;
    }
  }

  void parse_optional_range(Type& type) {
    if (!match(TokenKind::LeftBracket)) {
      return;
    }
    const auto start = previous();
    const auto left = parse_signed_decimal();
    expect(TokenKind::Colon, "':' in packed range", "FSIM-SV-PARSE-005");
    const auto right = parse_signed_decimal();
    expect(TokenKind::RightBracket, "']' after packed range",
           "FSIM-SV-PARSE-006");
    if (left && right) {
      type.packed_range = PackedRange{*left, *right, *left >= *right};
    } else {
      error(start, "FSIM-SV-SEM-001",
            "packed range bounds must be decimal literals in this frontend "
            "slice");
    }
  }

  std::optional<std::int64_t> parse_signed_decimal() {
    const bool negative = match(TokenKind::Minus);
    const auto number =
        expect(TokenKind::Number, "integer literal", "FSIM-SV-PARSE-007");
    return decimal_i64(number.text, negative);
  }

  [[nodiscard]] bool is_declaration_start() const {
    return is_direction_keyword() || is_net_type_keyword();
  }

  void parse_declaration(DesignUnit& unit) {
    const auto start = current();
    VerilogTypeSpec spec;
    spec.type = default_verilog_type();
    if (is_direction_keyword()) {
      spec.direction = parse_direction();
      parse_optional_net_type(spec.type);
    } else {
      parse_optional_net_type(spec.type);
    }
    parse_optional_signedness(spec.type);
    parse_optional_range(spec.type);

    for (;;) {
      const auto name = expect_identifier("declared name");
      if (at(TokenKind::LeftBracket)) {
        const auto dimension = current();
        error(dimension, "FSIM-SV-UNSUPPORTED-007",
              "unpacked arrays are not implemented in this frontend slice");
        skip_balanced(TokenKind::LeftBracket, TokenKind::RightBracket);
      }
      if (match(TokenKind::Assign)) {
        const auto initializer = parse_expression();
        error(
            name,
            "FSIM-SV-UNSUPPORTED-011",
            "declaration initializers are not executable in this frontend "
            "slice");
        (void)initializer;
      }

      SignalDeclaration declaration{
          name.text, spec.type, spec.direction,
          spec.direction != PortDirection::Unknown,
          span_from(start, previous())};
      const auto existing_port = std::find_if(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& port) {
            return port.name == declaration.name;
          });
      if (declaration.is_port) {
        if (!non_ansi_ports_.contains(declaration.name)
            || !body_port_declarations_.insert(declaration.name).second) {
          error(
              name,
              "FSIM-SV-SEM-004",
              "duplicate port declaration '" + declaration.name + "'");
        }
        update_or_add_port(unit, std::move(declaration));
      } else if (existing_port != unit.ports.end()) {
        // In a non-ANSI declaration, `output q; reg q;` describes one port,
        // not a distinct internal signal.
        if (!non_ansi_ports_.contains(declaration.name)
            || !port_type_refinements_.insert(declaration.name).second) {
          error(
              name,
              "FSIM-SV-SEM-005",
              "duplicate declaration of port '" + declaration.name + "'");
        } else {
          (void)update_existing_port_type(unit, declaration);
        }
      } else {
        const auto duplicate = std::find_if(
            unit.signals.begin(),
            unit.signals.end(),
            [&](const SignalDeclaration& signal) {
              return signal.name == declaration.name;
            });
        if (duplicate != unit.signals.end()) {
          error(
              name,
              "FSIM-SV-SEM-006",
              "duplicate signal declaration '" + declaration.name + "'");
        } else {
          unit.signals.push_back(std::move(declaration));
        }
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(TokenKind::Semicolon, "';' after declaration",
           "FSIM-SV-PARSE-008");
  }

  void parse_procedural_declaration(Statement& block) {
    const auto start = current();
    Type type = default_verilog_type();
    parse_optional_net_type(type);
    if (type.spelling == "wire") {
      error(
          start,
          "FSIM-SV-UNSUPPORTED-014",
          "procedural wire declarations are not supported; use a variable "
          "type");
    }
    parse_optional_signedness(type);
    parse_optional_range(type);

    for (;;) {
      const auto name = expect_identifier("local variable name");
      std::optional<Expression> initializer;
      if (match(TokenKind::Assign)) {
        initializer = parse_expression();
      }
      block.declarations.push_back(VariableDeclaration{
          name.text,
          type,
          std::move(initializer),
          span_from(name, previous())});
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after local variable declaration",
        "FSIM-SV-PARSE-045");
  }

  static void update_or_add_port(DesignUnit& unit,
                                 SignalDeclaration declaration) {
    for (auto& existing : unit.ports) {
      if (existing.name == declaration.name) {
        existing.type = std::move(declaration.type);
        existing.direction = declaration.direction;
        existing.span = std::move(declaration.span);
        return;
      }
    }
    unit.ports.push_back(std::move(declaration));
  }

  static bool update_existing_port_type(
      DesignUnit& unit, const SignalDeclaration& declaration) {
    for (auto& existing : unit.ports) {
      if (existing.name == declaration.name) {
        existing.type = declaration.type;
        existing.span = cover(existing.span, declaration.span);
        return true;
      }
    }
    return false;
  }

  std::optional<Statement> parse_continuous_assignment(const Token& start) {
    std::optional<Delay> delay;
    if (match(TokenKind::Hash)) {
      delay = parse_verilog_delay(previous());
    }
    if (!at(TokenKind::Identifier)) {
      error(current(), "FSIM-SV-PARSE-009",
            "expected continuous assignment target");
      skip_to_semicolon();
      return std::nullopt;
    }
    Expression target = parse_lvalue();
    expect(TokenKind::Assign, "'=' in continuous assignment",
           "FSIM-SV-PARSE-010");
    Expression value = parse_expression();
    expect(TokenKind::Semicolon, "';' after continuous assignment",
           "FSIM-SV-PARSE-011");
    Statement statement;
    statement.kind = StatementKind::Assignment;
    statement.assignment_kind = AssignmentKind::Continuous;
    statement.target = std::move(target);
    statement.value = std::move(value);
    statement.delay = std::move(delay);
    statement.span = span_from(start, previous());
    return statement;
  }

  Process parse_always() {
    const auto start = advance();
    Process process;
    if (start.text == "always_ff") {
      process.kind = ProcessKind::SystemVerilogAlwaysFF;
    } else if (start.text == "always_comb") {
      process.kind = ProcessKind::SystemVerilogAlwaysComb;
    } else if (start.text == "always_latch") {
      process.kind = ProcessKind::SystemVerilogAlwaysLatch;
    } else {
      process.kind = ProcessKind::VerilogAlways;
    }
    if (process.kind == ProcessKind::SystemVerilogAlwaysFF
        && language_ == Language::Verilog2005) {
      error(start, "FSIM-VERILOG-SEM-001",
            "always_ff requires SystemVerilog");
    }
    if (process.kind == ProcessKind::SystemVerilogAlwaysComb
        && language_ == Language::Verilog2005) {
      error(start, "FSIM-VERILOG-SEM-002",
            "always_comb requires SystemVerilog");
    }
    if (process.kind == ProcessKind::SystemVerilogAlwaysLatch
        && language_ == Language::Verilog2005) {
      error(start, "FSIM-VERILOG-SEM-003",
            "always_latch requires SystemVerilog");
    }
    const bool implicit_sensitivity =
        process.kind == ProcessKind::SystemVerilogAlwaysComb
        || process.kind == ProcessKind::SystemVerilogAlwaysLatch;
    if (implicit_sensitivity) {
      if (match(TokenKind::At)) {
        error(
            previous(),
            "FSIM-SV-SEM-011",
            "always_comb/always_latch supplies its own implicit sensitivity "
            "and cannot have an explicit event control");
        (void)parse_sensitivity();
      }
      process.sensitivities.push_back(
          Sensitivity{EdgeKind::Any, "*", start.span});
    } else if (match(TokenKind::At)) {
      process.sensitivities = parse_sensitivity();
    } else {
      error(current(), "FSIM-SV-PARSE-012",
            "always process requires an event control in this frontend "
            "slice");
    }
    auto body = parse_statement();
    if (body) {
      if (body->kind == StatementKind::Block) {
        process.variables = std::move(body->declarations);
        process.statements = std::move(body->statements);
      } else {
        process.statements.push_back(std::move(*body));
      }
    }
    if (implicit_sensitivity) {
      const auto inspect =
          [&](const auto& self,
              const std::vector<Statement>& statements,
              bool& has_timing,
              bool& has_nonblocking) -> void {
        for (const auto& statement : statements) {
          has_timing =
              has_timing
              || statement.kind == StatementKind::Delay
              || statement.kind == StatementKind::WaitOn;
          has_nonblocking =
              has_nonblocking
              || (statement.kind == StatementKind::Assignment
                  && statement.assignment_kind
                      == AssignmentKind::NonBlocking);
          self(
              self, statement.statements, has_timing,
              has_nonblocking);
          self(
              self, statement.else_statements, has_timing,
              has_nonblocking);
          for (const auto& alternative : statement.case_alternatives) {
            self(
                self, alternative.statements, has_timing,
                has_nonblocking);
          }
        }
      };
      bool has_timing = false;
      bool has_nonblocking = false;
      inspect(
          inspect, process.statements, has_timing,
          has_nonblocking);
      if (has_timing) {
        error(
            start,
            "FSIM-SV-SEM-012",
            "always_comb/always_latch cannot contain timing controls");
      }
      if (has_nonblocking) {
        error(
            start,
            "FSIM-SV-SEM-013",
            "always_comb/always_latch assignments must be blocking in this "
            "executable slice");
      }
    }
    process.span = span_from(start, previous());
    return process;
  }

  Process parse_initial() {
    const auto start =
        expect_keyword("initial", false, "FSIM-SV-PARSE-013");
    Process process;
    process.kind = ProcessKind::Initial;
    auto body = parse_statement();
    if (body) {
      if (body->kind == StatementKind::Block) {
        process.variables = std::move(body->declarations);
        process.statements = std::move(body->statements);
      } else {
        process.statements.push_back(std::move(*body));
      }
    }
    process.span = span_from(start, previous());
    return process;
  }

  std::vector<Sensitivity> parse_sensitivity() {
    std::vector<Sensitivity> sensitivities;
    if (match(TokenKind::Star)) {
      sensitivities.push_back(
          Sensitivity{EdgeKind::Any, "*", previous().span});
      return sensitivities;
    }
    expect(TokenKind::LeftParen, "'(' after '@'", "FSIM-SV-PARSE-014");
    if (match(TokenKind::Star)) {
      sensitivities.push_back(
          Sensitivity{EdgeKind::Any, "*", previous().span});
      expect(TokenKind::RightParen, "')' after '@*'",
             "FSIM-SV-PARSE-015");
      return sensitivities;
    }
    while (!at_end() && !at(TokenKind::RightParen)) {
      EdgeKind edge = EdgeKind::Any;
      if (match_keyword("posedge")) {
        edge = EdgeKind::Positive;
      } else if (match_keyword("negedge")) {
        edge = EdgeKind::Negative;
      }
      const auto signal = expect_identifier("sensitivity signal");
      std::string signal_name = signal.text;
      while (match(TokenKind::Dot)) {
        signal_name += '.';
        signal_name += expect_identifier("selected signal name").text;
      }
      sensitivities.push_back(
          Sensitivity{edge, std::move(signal_name), signal.span});
      if (match(TokenKind::Comma) || match_keyword("or")) {
        continue;
      }
      break;
    }
    expect(TokenKind::RightParen, "')' after sensitivity list",
           "FSIM-SV-PARSE-016");
    return sensitivities;
  }

  void skip_case_statement() {
    std::size_t depth = 1;
    while (!at_end() && depth != 0) {
      if (keyword("case") || keyword("casez") || keyword("casex")) {
        ++depth;
        advance();
      } else if (match_keyword("endcase")) {
        --depth;
      } else {
        advance();
      }
    }
  }

  Statement parse_case_statement(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::Case;
    expect(TokenKind::LeftParen, "'(' after case", "FSIM-SV-PARSE-046");
    statement.condition = parse_expression();
    expect(TokenKind::RightParen, "')' after case expression",
           "FSIM-SV-PARSE-047");
    if (match_keyword("inside")) {
      error(previous(), "FSIM-SV-UNSUPPORTED-018",
            "case inside matching is not implemented");
      skip_case_statement();
      statement.span = span_from(start, previous());
      return statement;
    }

    bool saw_default = false;
    while (!at_end() && !keyword("endcase")) {
      const auto item_start = current();
      CaseAlternative alternative;
      if (match_keyword("default")) {
        alternative.is_default = true;
        if (saw_default) {
          error(item_start, "FSIM-SV-SEM-014",
                "a case statement may contain only one default item");
        }
        saw_default = true;
      } else {
        alternative.choices.push_back(parse_expression());
        while (match(TokenKind::Comma)) {
          alternative.choices.push_back(parse_expression());
        }
      }
      expect(TokenKind::Colon, "':' after case item",
             "FSIM-SV-PARSE-048");
      if (auto body = parse_statement()) {
        alternative.statements.push_back(std::move(*body));
      }
      alternative.span = cover(item_start.span, previous().span);
      statement.case_alternatives.push_back(std::move(alternative));
    }
    expect_keyword("endcase", false, "FSIM-SV-PARSE-049");
    statement.span = span_from(start, previous());
    return statement;
  }

  std::optional<Statement> parse_statement() {
    if (language_ == Language::SystemVerilog2017
        && (keyword("unique") || keyword("unique0")
            || keyword("priority"))
        && (keyword("case", 1) || keyword("casez", 1)
            || keyword("casex", 1))) {
      const auto qualifier = advance();
      error(qualifier, "FSIM-SV-UNSUPPORTED-017",
            "unique and priority case qualifiers are not implemented");
    }
    if (keyword("casez") || keyword("casex")) {
      const auto unsupported = advance();
      error(unsupported, "FSIM-SV-UNSUPPORTED-016",
            unsupported.text
                + " wildcard matching is not implemented; use exact case");
      skip_case_statement();
      return std::nullopt;
    }
    if (match_keyword("case")) {
      return parse_case_statement(previous());
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("assert")) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::Assert;
      expect(TokenKind::LeftParen, "'(' after assert",
             "FSIM-SV-PARSE-039");
      statement.condition = parse_expression();
      expect(TokenKind::RightParen, "')' after assertion condition",
             "FSIM-SV-PARSE-040");
      if (match_keyword("else")) {
        expect_keyword("$error", false, "FSIM-SV-PARSE-041");
        if (match(TokenKind::LeftParen)) {
          const auto message = expect(
              TokenKind::StringLiteral, "string literal passed to $error",
              "FSIM-SV-PARSE-042");
          statement.assertion_message = string_literal_text(message);
          expect(TokenKind::RightParen, "')' after $error message",
                 "FSIM-SV-PARSE-043");
        }
      }
      expect(TokenKind::Semicolon, "';' after assertion",
             "FSIM-SV-PARSE-044");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("begin")) {
      const auto start = previous();
      if (match(TokenKind::Colon)) {
        expect_identifier("block name");
      }
      Statement block;
      block.kind = StatementKind::Block;
      while (!at_end() && !keyword("end")) {
        const auto before = position();
        if (is_net_type_keyword()) {
          parse_procedural_declaration(block);
        } else if (auto child = parse_statement()) {
          block.statements.push_back(std::move(*child));
        }
        if (position() == before) {
          advance();
        }
      }
      expect_keyword("end", false, "FSIM-SV-PARSE-017");
      if (match(TokenKind::Colon)) {
        expect_identifier("block name");
      }
      block.span = span_from(start, previous());
      return block;
    }

    if (match_keyword("if")) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::If;
      expect(TokenKind::LeftParen, "'(' after if", "FSIM-SV-PARSE-018");
      statement.condition = parse_expression();
      expect(TokenKind::RightParen, "')' after if condition",
             "FSIM-SV-PARSE-019");
      if (auto true_branch = parse_statement()) {
        if (true_branch->kind == StatementKind::Block) {
          if (!true_branch->declarations.empty()) {
            error(
                current(),
                "FSIM-SV-UNSUPPORTED-015",
                "nested procedural block declarations are not implemented "
                "in this frontend slice");
          }
          statement.statements = std::move(true_branch->statements);
        } else {
          statement.statements.push_back(std::move(*true_branch));
        }
      }
      if (match_keyword("else")) {
        if (auto false_branch = parse_statement()) {
          if (false_branch->kind == StatementKind::Block) {
            if (!false_branch->declarations.empty()) {
              error(
                  current(),
                  "FSIM-SV-UNSUPPORTED-015",
                  "nested procedural block declarations are not implemented "
                  "in this frontend slice");
            }
            statement.else_statements =
                std::move(false_branch->statements);
          } else {
            statement.else_statements.push_back(
                std::move(*false_branch));
          }
        }
      }
      statement.span = span_from(start, previous());
      return statement;
    }

    if (match(TokenKind::At)) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::WaitOn;
      statement.sensitivities = parse_sensitivity();
      if (!match(TokenKind::Semicolon)) {
        if (auto controlled = parse_statement()) {
          statement.statements.push_back(std::move(*controlled));
        }
      }
      statement.span = span_from(start, previous());
      return statement;
    }

    if (match(TokenKind::Hash)) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::Delay;
      statement.delay = parse_verilog_delay(start);
      if (match(TokenKind::Semicolon)) {
        statement.span = span_from(start, previous());
        return statement;
      }
      if (auto delayed = parse_statement()) {
        statement.statements.push_back(std::move(*delayed));
      }
      statement.span = span_from(start, previous());
      return statement;
    }

    if (keyword("$finish")) {
      const auto start = advance();
      if (match(TokenKind::LeftParen)) {
        if (!at(TokenKind::RightParen)) {
          (void)parse_expression();
        }
        expect(TokenKind::RightParen, "')' after $finish",
               "FSIM-SV-PARSE-020");
      }
      expect(TokenKind::Semicolon, "';' after $finish",
             "FSIM-SV-PARSE-021");
      Statement statement;
      statement.kind = StatementKind::Finish;
      statement.span = span_from(start, previous());
      return statement;
    }

    if (match(TokenKind::Semicolon)) {
      Statement statement;
      statement.kind = StatementKind::Null;
      statement.span = previous().span;
      return statement;
    }

    if (at(TokenKind::Identifier)) {
      const auto before = position();
      const auto start = current();
      Expression target = parse_lvalue();
      AssignmentKind assignment_kind;
      if (match(TokenKind::LessEqual)) {
        assignment_kind = AssignmentKind::NonBlocking;
      } else if (match(TokenKind::Assign)) {
        assignment_kind = AssignmentKind::Blocking;
      } else {
        rewind(before);
        const auto unsupported = advance();
        error(unsupported, "FSIM-SV-UNSUPPORTED-008",
              "unsupported procedural statement starting with '" +
                  unsupported.text + "'");
        skip_to_semicolon();
        return std::nullopt;
      }

      std::optional<Delay> delay;
      if (match(TokenKind::Hash)) {
        delay = parse_verilog_delay(previous());
      }
      Expression value = parse_expression();
      expect(TokenKind::Semicolon, "';' after assignment",
             "FSIM-SV-PARSE-022");
      Statement statement;
      statement.kind = StatementKind::Assignment;
      statement.assignment_kind = assignment_kind;
      statement.target = std::move(target);
      statement.value = std::move(value);
      statement.delay = std::move(delay);
      statement.span = span_from(start, previous());
      return statement;
    }

    const auto unexpected = advance();
    error(unexpected, "FSIM-SV-UNSUPPORTED-009",
          "unsupported statement starting with '" + unexpected.text + "'");
    skip_to_semicolon();
    return std::nullopt;
  }

  Delay parse_verilog_delay(const Token& start) {
    Delay delay;
    bool parenthesized = match(TokenKind::LeftParen);
    const auto magnitude =
        expect(TokenKind::Number, "delay magnitude", "FSIM-SV-PARSE-023");
    if (const auto parsed = decimal_u64(magnitude.text)) {
      if (!module_time_unit_.empty()
          && *parsed
              > std::numeric_limits<std::uint64_t>::max()
                  / module_time_unit_magnitude_) {
        error(
            magnitude,
            "FSIM-SV-SEM-010",
            "delay magnitude overflows after applying `timescale");
      } else {
        delay.magnitude =
            *parsed
            * (module_time_unit_.empty()
                   ? std::uint64_t{1}
                   : module_time_unit_magnitude_);
        delay.unit = module_time_unit_;
      }
    } else {
      error(magnitude, "FSIM-SV-SEM-002",
            "delay magnitude must be a decimal integer literal");
    }
    if (parenthesized) {
      expect(TokenKind::RightParen, "')' after delay",
             "FSIM-SV-PARSE-024");
    }
    delay.span = span_from(start, previous());
    return delay;
  }

  Expression parse_lvalue() {
    const auto name = expect_identifier("assignment target");
    Expression expression{ExpressionKind::Identifier, name.text, {},
                          name.span};
    for (;;) {
      if (match(TokenKind::Dot)) {
        const auto selected = expect_identifier("selected name");
        expression.text += '.';
        expression.text += selected.text;
        expression.span = cover(expression.span, selected.span);
      } else if (match(TokenKind::LeftBracket)) {
        Expression first = parse_expression();
        if (match(TokenKind::Colon)) {
          Expression second = parse_expression();
          expect(TokenKind::RightBracket, "']' after part-select",
                 "FSIM-SV-PARSE-025");
          expression =
              Expression{ExpressionKind::Slice, ":",
                         {std::move(expression), std::move(first),
                          std::move(second)},
                         cover(expression.span, previous().span)};
        } else {
          expect(TokenKind::RightBracket, "']' after index",
                 "FSIM-SV-PARSE-026");
          expression =
              Expression{ExpressionKind::Index, "index",
                         {std::move(expression), std::move(first)},
                         cover(expression.span, previous().span)};
        }
      } else {
        break;
      }
    }
    return expression;
  }

  struct BinaryOperation {
    int precedence;
    std::string name;
  };

  std::optional<BinaryOperation> binary_operation() const {
    if (at(TokenKind::OrOr)) {
      return BinaryOperation{1, "||"};
    }
    if (at(TokenKind::AndAnd)) {
      return BinaryOperation{2, "&&"};
    }
    if (at(TokenKind::Pipe)) {
      return BinaryOperation{3, "|"};
    }
    if (at(TokenKind::Caret)) {
      return BinaryOperation{4, "^"};
    }
    if (at(TokenKind::Ampersand)) {
      return BinaryOperation{5, "&"};
    }
    if (at(TokenKind::EqualEqual)
        || (at(TokenKind::NotEqual) && current().text == "!=")) {
      return BinaryOperation{6, current().text};
    }
    if (at(TokenKind::Less) || at(TokenKind::LessEqual) ||
        at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
      return BinaryOperation{7, current().text};
    }
    if (at(TokenKind::ShiftLeft) || at(TokenKind::ShiftRight)) {
      return BinaryOperation{8, current().text};
    }
    if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
      return BinaryOperation{9, current().text};
    }
    if (at(TokenKind::Star) || at(TokenKind::Slash) ||
        at(TokenKind::Percent)) {
      return BinaryOperation{10, current().text};
    }
    return std::nullopt;
  }

  Expression parse_expression(int minimum_precedence = 0) {
    Expression left = parse_unary();
    for (;;) {
      const auto operation = binary_operation();
      if (!operation || operation->precedence < minimum_precedence) {
        break;
      }
      advance();
      Expression right = parse_expression(operation->precedence + 1);
      const auto combined = cover(left.span, right.span);
      left = Expression{ExpressionKind::Binary, operation->name,
                        {std::move(left), std::move(right)}, combined};
    }
    if (minimum_precedence == 0 && match(TokenKind::Question)) {
      Expression when_true = parse_expression();
      expect(TokenKind::Colon, "':' in conditional expression",
             "FSIM-SV-PARSE-027");
      Expression when_false = parse_expression();
      const auto combined = cover(left.span, when_false.span);
      left = Expression{ExpressionKind::Call, "?:",
                        {std::move(left), std::move(when_true),
                         std::move(when_false)},
                        combined};
    }
    return left;
  }

  Expression parse_unary() {
    if (at(TokenKind::Plus) || at(TokenKind::Minus) ||
        at(TokenKind::Bang) || at(TokenKind::Tilde) ||
        at(TokenKind::Ampersand) || at(TokenKind::Pipe) ||
        at(TokenKind::Caret)) {
      const auto operation = advance();
      Expression operand = parse_unary();
      return Expression{ExpressionKind::Unary, operation.text,
                        {std::move(operand)},
                        cover(operation.span, operand.span)};
    }
    return parse_primary();
  }

  Expression parse_primary() {
    if (at(TokenKind::Number)) {
      const auto token = advance();
      const auto kind = token.text.find('\'') == std::string::npos
                            ? ExpressionKind::IntegerLiteral
                            : ExpressionKind::LogicLiteral;
      return Expression{kind, token.text, {}, token.span};
    }
    if (at(TokenKind::StringLiteral)) {
      const auto token = advance();
      return Expression{ExpressionKind::StringLiteral, token.text, {},
                        token.span};
    }
    if (at(TokenKind::Identifier)) {
      const auto name = advance();
      Expression expression{ExpressionKind::Identifier, name.text, {},
                            name.span};
      if (match(TokenKind::LeftParen)) {
        std::vector<Expression> arguments;
        if (!at(TokenKind::RightParen)) {
          do {
            arguments.push_back(parse_expression());
          } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen, "')' after arguments",
               "FSIM-SV-PARSE-028");
        expression = Expression{ExpressionKind::Call, name.text,
                                std::move(arguments),
                                cover(name.span, previous().span)};
      }
      return parse_postfix(std::move(expression));
    }
    if (match(TokenKind::LeftParen)) {
      const auto open = previous();
      Expression expression = parse_expression();
      expect(TokenKind::RightParen, "')' after expression",
             "FSIM-SV-PARSE-029");
      expression.span = span_from(open, previous());
      return expression;
    }
    if (match(TokenKind::LeftBrace)) {
      const auto open = previous();
      std::vector<Expression> elements;
      if (!at(TokenKind::RightBrace)) {
        do {
          elements.push_back(parse_expression());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RightBrace, "'}' after concatenation",
             "FSIM-SV-PARSE-030");
      return Expression{ExpressionKind::Concatenation, "concat",
                        std::move(elements), span_from(open, previous())};
    }
    const auto invalid = advance();
    error(invalid, "FSIM-SV-PARSE-031", "expected expression");
    return Expression{ExpressionKind::Invalid, invalid.text, {},
                      invalid.span};
  }

  Expression parse_postfix(Expression expression) {
    for (;;) {
      if (match(TokenKind::Dot)) {
        const auto member = expect_identifier("member name");
        expression.text += '.';
        expression.text += member.text;
        expression.span = cover(expression.span, member.span);
      } else if (match(TokenKind::LeftBracket)) {
        Expression first = parse_expression();
        if (match(TokenKind::Colon)) {
          Expression second = parse_expression();
          expect(TokenKind::RightBracket, "']' after part-select",
                 "FSIM-SV-PARSE-032");
          expression =
              Expression{ExpressionKind::Slice, ":",
                         {std::move(expression), std::move(first),
                          std::move(second)},
                         cover(expression.span, previous().span)};
        } else {
          expect(TokenKind::RightBracket, "']' after index",
                 "FSIM-SV-PARSE-033");
          expression =
              Expression{ExpressionKind::Index, "index",
                         {std::move(expression), std::move(first)},
                         cover(expression.span, previous().span)};
        }
      } else {
        break;
      }
    }
    return expression;
  }

  Language language_;
  std::unordered_set<std::string> non_ansi_ports_;
  std::unordered_set<std::string> body_port_declarations_;
  std::unordered_set<std::string> port_type_refinements_;
  std::uint64_t current_time_unit_magnitude_{1};
  std::string current_time_unit_;
  std::string current_time_precision_;
  std::uint64_t module_time_unit_magnitude_{1};
  std::string module_time_unit_;
  std::string module_time_precision_;
};

}  // namespace

ParseResult parse_verilog(SourceText source, bool system_verilog) {
  const auto language = system_verilog ? Language::SystemVerilog2017
                                       : Language::Verilog2005;
  return VerilogParser(lex(std::move(source), language), system_verilog).run();
}

}  // namespace fsim::frontend
