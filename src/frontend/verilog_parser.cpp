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
#include <unordered_map>
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

enum class KeywordSet {
  Verilog1995,
  Verilog2001,
  Verilog2001NoConfig,
  Verilog2005,
  SystemVerilog2005,
  SystemVerilog2009,
  SystemVerilog2012,
  SystemVerilog2017,
};

[[nodiscard]] bool contains_word(
    const std::initializer_list<std::string_view> words,
    const std::string_view word) {
  return std::find(words.begin(), words.end(), word) != words.end();
}

[[nodiscard]] bool is_verilog_1995_keyword(
    const std::string_view word) {
  return contains_word(
      {"always", "and", "assign", "begin", "buf", "bufif0",
       "bufif1", "case", "casex", "casez", "cmos", "deassign",
       "default", "defparam", "disable", "edge", "else", "end",
       "endcase", "endfunction", "endmodule", "endprimitive",
       "endspecify", "endtable", "endtask", "event", "for", "force",
       "forever", "fork", "function", "highz0", "highz1", "if",
       "ifnone", "initial", "inout", "input", "integer", "join",
       "large", "macromodule", "medium", "module", "nand", "negedge",
       "nmos", "nor", "not", "notif0", "notif1", "or", "output",
       "parameter", "pmos", "posedge", "primitive", "pull0", "pull1",
       "pulldown", "pullup", "rcmos", "real", "realtime", "reg",
       "release", "repeat", "rnmos", "rpmos", "rtran", "rtranif0",
       "rtranif1", "scalared", "small", "specify", "specparam",
       "strong0", "strong1", "supply0", "supply1", "table", "task",
       "time", "tran", "tranif0", "tranif1", "tri", "tri0", "tri1",
       "triand", "trior", "trireg", "vectored", "wait", "wand",
       "weak0", "weak1", "while", "wire", "wor", "xnor", "xor"},
      word);
}

[[nodiscard]] bool is_verilog_2001_keyword(
    const std::string_view word,
    const bool include_config) {
  if (is_verilog_1995_keyword(word)) {
    return true;
  }
  if (contains_word(
          {"automatic", "endgenerate", "generate", "genvar",
           "localparam", "noshowcancelled", "pulsestyle_ondetect",
           "pulsestyle_onevent", "showcancelled", "signed", "unsigned"},
          word)) {
    return true;
  }
  return include_config
      && contains_word(
          {"cell", "config", "design", "endconfig", "incdir",
           "include", "instance", "liblist", "library", "use"},
          word);
}

[[nodiscard]] bool is_system_verilog_2005_keyword(
    const std::string_view word) {
  return is_verilog_2001_keyword(word, true)
      || word == "uwire"
      || contains_word(
          {"alias", "always_comb", "always_ff", "always_latch",
           "assert", "assume", "before", "bind", "bins", "binsof",
           "bit", "break", "byte", "chandle", "class", "clocking",
           "const", "constraint", "context", "continue", "cover",
           "covergroup", "coverpoint", "cross", "dist", "do",
           "endclass", "endclocking", "endgroup", "endinterface",
           "endpackage", "endprogram", "endproperty", "endsequence",
           "enum", "expect", "export", "extends", "extern", "final",
           "first_match", "foreach", "forkjoin", "iff", "ignore_bins",
           "illegal_bins", "import", "inside", "int", "interface",
           "intersect", "join_any", "join_none", "local", "logic",
           "longint", "matches", "modport", "new", "null", "package",
           "packed", "priority", "program", "property", "protected",
           "pure", "rand", "randc", "randcase", "randsequence", "ref",
           "return", "sequence", "shortint", "shortreal", "solve",
           "static", "string", "struct", "super", "tagged", "this",
           "throughout", "timeprecision", "timeunit", "type", "typedef",
           "union", "unique", "var", "virtual", "void", "wait_order",
           "wildcard", "with", "within"},
          word);
}

[[nodiscard]] bool is_system_verilog_2009_keyword(
    const std::string_view word) {
  return is_system_verilog_2005_keyword(word)
      || contains_word(
          {"accept_on", "checker", "endchecker", "eventually", "global",
           "implies", "let", "nexttime", "reject_on", "restrict",
           "s_always", "s_eventually", "s_nexttime", "s_until",
           "s_until_with", "strong", "sync_accept_on", "sync_reject_on",
           "unique0", "until", "until_with", "untyped", "weak"},
          word);
}

[[nodiscard]] bool is_system_verilog_2012_keyword(
    const std::string_view word) {
  return is_system_verilog_2009_keyword(word)
      || contains_word(
          {"implements", "interconnect", "nettype", "soft"},
          word);
}

[[nodiscard]] bool keyword_reserved(
    const KeywordSet set,
    const std::string_view word) {
  switch (set) {
    case KeywordSet::Verilog1995:
      return is_verilog_1995_keyword(word);
    case KeywordSet::Verilog2001:
      return is_verilog_2001_keyword(word, true);
    case KeywordSet::Verilog2001NoConfig:
      return is_verilog_2001_keyword(word, false);
    case KeywordSet::Verilog2005:
      return is_verilog_2001_keyword(word, true) || word == "uwire";
    case KeywordSet::SystemVerilog2005:
      return is_system_verilog_2005_keyword(word);
    case KeywordSet::SystemVerilog2009:
      return is_system_verilog_2009_keyword(word);
    case KeywordSet::SystemVerilog2012:
    case KeywordSet::SystemVerilog2017:
      return is_system_verilog_2012_keyword(word);
  }
  return false;
}

[[nodiscard]] std::optional<KeywordSet> parse_keyword_set(
    const std::string_view spelling) {
  if (spelling == "1364-1995") {
    return KeywordSet::Verilog1995;
  }
  if (spelling == "1364-2001") {
    return KeywordSet::Verilog2001;
  }
  if (spelling == "1364-2001-noconfig") {
    return KeywordSet::Verilog2001NoConfig;
  }
  if (spelling == "1364-2005") {
    return KeywordSet::Verilog2005;
  }
  if (spelling == "1800-2005") {
    return KeywordSet::SystemVerilog2005;
  }
  if (spelling == "1800-2009") {
    return KeywordSet::SystemVerilog2009;
  }
  if (spelling == "1800-2012") {
    return KeywordSet::SystemVerilog2012;
  }
  if (spelling == "1800-2017") {
    return KeywordSet::SystemVerilog2017;
  }
  return std::nullopt;
}

class VerilogParser final : private detail::ParserBase {
 public:
  VerilogParser(LexResult lexed, bool system_verilog)
      : ParserBase(std::move(lexed.tokens),
                   std::move(lexed.diagnostics)),
        language_(system_verilog ? Language::SystemVerilog2017
                                 : Language::Verilog2005),
        keyword_set_(
            system_verilog ? KeywordSet::SystemVerilog2017
                           : KeywordSet::Verilog2005) {}

  ParseResult run() {
    ParsedDesign design;
    while (!at_end()) {
      if (match_keyword("module")) {
        design.units.push_back(parse_module(previous()));
      } else if (match_keyword("package")) {
        auto package = parse_package(previous());
        auto& exports =
            package_constant_names_[package.name];
        for (const auto& parameter : package.parameters) {
          exports.insert(parameter.name);
        }
        design.units.push_back(std::move(package));
      } else if (match_keyword("import")) {
        parse_import_clause(
            compilation_unit_imports_, previous());
      } else if (at(TokenKind::Backtick)) {
        parse_directive();
      } else {
        const auto unexpected = advance();
        error(unexpected, "FSIM-SV-UNSUPPORTED-001",
              "unsupported compilation-unit item '" + unexpected.text + "'");
        skip_to_semicolon();
      }
    }
    if (!keyword_stack_.empty()) {
      error(
          current(),
          "FSIM-SV-PP-038",
          "unterminated `begin_keywords region");
      keyword_stack_.clear();
    }
    return ParseResult{std::move(design), std::move(diagnostics_)};
  }

 private:
  [[nodiscard]] bool keyword(
      const std::string_view text,
      const std::size_t lookahead = 0,
      const bool case_insensitive = false) const {
    if (!detail::ParserBase::keyword(
            text, lookahead, case_insensitive)) {
      return false;
    }
    return text.front() == '$' || keyword_reserved(keyword_set_, text);
  }

  [[nodiscard]] bool any_keyword(
      const std::initializer_list<std::string_view> words,
      const bool case_insensitive = false) const {
    return std::any_of(
        words.begin(), words.end(),
        [&](const std::string_view word) {
          return keyword(word, 0, case_insensitive);
        });
  }

  bool match_keyword(
      const std::string_view text,
      const bool case_insensitive = false) {
    if (!keyword(text, 0, case_insensitive)) {
      return false;
    }
    advance();
    return true;
  }

  Token expect_keyword(
      const std::string_view word,
      const bool case_insensitive,
      std::string code = "FSIM-FE-PARSE-001") {
    if (keyword(word, 0, case_insensitive)) {
      return advance();
    }
    error(
        current(), std::move(code),
        "expected '" + std::string(word) + "'");
    return current();
  }

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
    if (at(TokenKind::Identifier)
        && !keyword_reserved(keyword_set_, current().text)) {
      return advance();
    }
    error(
        current(),
        "FSIM-SV-PARSE-001",
        "expected " + std::string(description)
            + ", found reserved keyword or non-identifier '"
            + current().text + "'");
    return current();
  }

  [[nodiscard]] bool on_directive_line(const Token& tick) const {
    return !at_end()
        && current().span.source_name == tick.span.source_name
        && current().span.begin.line == tick.span.begin.line;
  }

  void reject_directive_arguments(
      const Token& tick,
      const Token& directive) {
    if (on_directive_line(tick)) {
      error(
          current(),
          "FSIM-SV-PP-032",
          "`" + directive.text + " does not accept arguments");
    }
  }

  void reset_compiler_directives() {
    current_time_unit_magnitude_ = 1;
    current_time_unit_.clear();
    current_time_precision_.clear();
    current_default_nettype_ = "wire";
    current_cell_define_ = false;
    current_unconnected_drive_ = VerilogUnconnectedDrive::None;
  }

  void parse_directive() {
    const auto tick = advance();
    const auto directive = at(TokenKind::Identifier) ? advance() : current();
    if (directive.text == "timescale") {
      parse_timescale(directive);
    } else if (directive.text == "default_nettype") {
      parse_default_nettype(tick, directive);
    } else if (directive.text == "resetall") {
      reset_compiler_directives();
      reject_directive_arguments(tick, directive);
    } else if (directive.text == "celldefine") {
      current_cell_define_ = true;
      reject_directive_arguments(tick, directive);
    } else if (directive.text == "endcelldefine") {
      if (!current_cell_define_) {
        error(
            directive,
            "FSIM-SV-PP-033",
            "`endcelldefine without an active `celldefine");
      }
      current_cell_define_ = false;
      reject_directive_arguments(tick, directive);
    } else if (directive.text == "unconnected_drive") {
      parse_unconnected_drive(tick, directive);
    } else if (directive.text == "nounconnected_drive") {
      current_unconnected_drive_ = VerilogUnconnectedDrive::None;
      reject_directive_arguments(tick, directive);
    } else if (directive.text == "begin_keywords") {
      parse_begin_keywords(tick, directive);
    } else if (directive.text == "end_keywords") {
      if (keyword_stack_.empty()) {
        error(
            directive,
            "FSIM-SV-PP-037",
            "`end_keywords without a matching `begin_keywords");
      } else {
        keyword_set_ = keyword_stack_.back();
        keyword_stack_.pop_back();
      }
      reject_directive_arguments(tick, directive);
    } else {
      error(directive, "FSIM-SV-UNSUPPORTED-002",
            "preprocessor directive `" + directive.text +
                " reached the parser without preprocessing");
    }
    const auto source_name = tick.span.source_name;
    const auto line = tick.span.begin.line;
    while (!at_end()
           && current().span.source_name == source_name
           && current().span.begin.line == line) {
      advance();
    }
  }

  void parse_default_nettype(
      const Token& tick,
      const Token& directive) {
    if (!on_directive_line(tick)
        || current().kind != TokenKind::Identifier) {
      error(
          directive,
          "FSIM-SV-PP-034",
          "`default_nettype requires a standard net type or none");
      return;
    }
    const auto net_type = advance();
    constexpr std::string_view legal[] = {
        "wire", "tri", "tri0", "tri1", "wand", "triand",
        "wor", "trior", "trireg", "uwire", "none"};
    if (std::find(
            std::begin(legal), std::end(legal), net_type.text)
        == std::end(legal)) {
      error(
          net_type,
          "FSIM-SV-PP-034",
          "invalid `default_nettype value '" + net_type.text + "'");
    } else {
      current_default_nettype_ = net_type.text;
    }
    if (on_directive_line(tick)) {
      error(
          current(),
          "FSIM-SV-PP-034",
          "unexpected tokens after `default_nettype value");
    }
  }

  void parse_unconnected_drive(
      const Token& tick,
      const Token& directive) {
    if (!on_directive_line(tick)
        || current().kind != TokenKind::Identifier) {
      error(
          directive,
          "FSIM-SV-PP-035",
          "`unconnected_drive requires pull0 or pull1");
      return;
    }
    const auto pull = advance();
    if (pull.text == "pull0") {
      current_unconnected_drive_ = VerilogUnconnectedDrive::Pull0;
    } else if (pull.text == "pull1") {
      current_unconnected_drive_ = VerilogUnconnectedDrive::Pull1;
    } else {
      error(
          pull,
          "FSIM-SV-PP-035",
          "`unconnected_drive requires pull0 or pull1");
    }
    if (on_directive_line(tick)) {
      error(
          current(),
          "FSIM-SV-PP-035",
          "unexpected tokens after `unconnected_drive value");
    }
  }

  void parse_begin_keywords(
      const Token& tick,
      const Token& directive) {
    if (!on_directive_line(tick)
        || current().kind != TokenKind::StringLiteral) {
      error(
          directive,
          "FSIM-SV-PP-036",
          "`begin_keywords requires a quoted IEEE language version");
      return;
    }
    const auto version = advance();
    const auto spelling = string_literal_text(version);
    const auto parsed = parse_keyword_set(spelling);
    if (!parsed) {
      error(
          version,
          "FSIM-SV-PP-036",
          "unsupported `begin_keywords version '" + spelling + "'");
    } else {
      keyword_stack_.push_back(keyword_set_);
      keyword_set_ = *parsed;
    }
    if (on_directive_line(tick)) {
      error(
          current(),
          "FSIM-SV-PP-036",
          "unexpected tokens after `begin_keywords version");
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

  struct ImplicitNetReference {
    std::string name;
    std::string net_type;
    SourceSpan span;
    std::vector<std::string> expansion_stack;
  };

  void note_implicit_net_reference(const Token& name) {
    for (const auto& import_item : active_package_imports_) {
      if ((!import_item.name.empty()
           && import_item.name == name.text)
          || (import_item.name.empty()
              && package_constant_names_[
                     import_item.package].contains(name.text))) {
        return;
      }
    }
    if (!current_procedural_names_.contains(name.text)
        && !current_generate_names_.contains(name.text)
        && !current_loop_names_.contains(name.text)) {
      implicit_net_references_.push_back(
          {name.text, current_default_nettype_, name.span,
           name.expansion_stack});
    }
  }

  void resolve_implicit_nets(DesignUnit& unit) {
    std::unordered_set<std::string> known;
    known.insert(
        declared_genvars_.begin(), declared_genvars_.end());
    for (const auto& parameter : unit.parameters) {
      known.insert(parameter.name);
    }
    for (const auto& port : unit.ports) {
      known.insert(port.name);
    }
    for (const auto& signal : unit.signals) {
      known.insert(signal.name);
    }
    std::unordered_set<std::string> rejected;
    for (const auto& reference : implicit_net_references_) {
      if (known.contains(reference.name)
          || rejected.contains(reference.name)) {
        continue;
      }
      if (reference.net_type == "none") {
        diagnostics_.push_back({
            DiagnosticSeverity::Error,
            "FSIM-SV-SEM-015",
            "implicit net '" + reference.name
                + "' is forbidden by `default_nettype none",
            reference.span,
            reference.expansion_stack});
        rejected.insert(reference.name);
        continue;
      }
      Type type{
          ValueDomain::Logic4,
          reference.net_type,
          std::nullopt,
          false};
      unit.signals.push_back({
          reference.name,
          std::move(type),
          PortDirection::Unknown,
          false,
          reference.span});
      known.insert(reference.name);
    }
  }

  DesignUnit parse_module(const Token& start) {
    non_ansi_ports_.clear();
    body_port_declarations_.clear();
    port_type_refinements_.clear();
    implicit_net_references_.clear();
    current_procedural_names_.clear();
    current_generate_names_.clear();
    current_loop_names_.clear();
    declared_genvars_.clear();
    external_genvar_uses_.clear();
    module_time_unit_magnitude_ = current_time_unit_magnitude_;
    module_time_unit_ = current_time_unit_;
    module_time_precision_ = current_time_precision_;
    DesignUnit unit;
    unit.kind = UnitKind::VerilogModule;
    unit.language = language_;
    unit.systemverilog_imports =
        compilation_unit_imports_;
    active_package_imports_ =
        unit.systemverilog_imports;
    unit.default_nettype = current_default_nettype_;
    unit.is_cell = current_cell_define_;
    if (!module_time_unit_.empty()) {
      unit.time_unit =
          std::to_string(module_time_unit_magnitude_)
          + module_time_unit_;
      unit.time_precision = module_time_precision_;
    }
    const auto name = expect_identifier("module name");
    unit.name = name.text;

    if (match(TokenKind::Hash)) {
      parse_parameter_port_list(unit, previous());
    }

    if (match(TokenKind::LeftParen)) {
      parse_module_ports(unit);
      expect(TokenKind::RightParen, "')' after module ports",
             "FSIM-SV-PARSE-002");
    }
    expect(TokenKind::Semicolon, "';' after module header",
           "FSIM-SV-PARSE-003");

    while (!at_end() && !keyword("endmodule")) {
      if (match_keyword("parameter")) {
        parse_parameter_group(unit, false, false, previous());
      } else if (match_keyword("localparam")) {
        parse_parameter_group(unit, true, false, previous());
      } else if (match_keyword("import")) {
        parse_import_clause(
            unit.systemverilog_imports, previous());
        active_package_imports_ =
            unit.systemverilog_imports;
      } else if (match_keyword("typedef")) {
        parse_typedef(unit, previous());
      } else if (match_keyword("genvar")) {
        parse_genvar_declaration(unit);
      } else if (match_keyword("event")) {
        parse_event_declaration(unit, previous());
      } else if (
          keyword("final")
          || (language_ == Language::Verilog2005
              && at(TokenKind::Identifier)
              && current().text == "final")) {
        unit.processes.push_back(parse_final());
      } else if (is_declaration_start()) {
        parse_declaration(unit);
      } else if (is_gate_primitive()) {
        parse_gate_primitive(unit.concurrent_statements);
      } else if (match_keyword("assign")) {
        if (auto assignment = parse_continuous_assignment(previous())) {
          unit.concurrent_statements.push_back(std::move(*assignment));
        }
      } else if (
          keyword("always") || keyword("always_ff")
          || keyword("always_comb") || keyword("always_latch")
          || (language_ == Language::Verilog2005
              && at(TokenKind::Identifier)
              && (current().text == "always_ff"
                  || current().text == "always_comb"
                  || current().text == "always_latch"))) {
        unit.processes.push_back(parse_always());
      } else if (keyword("initial")) {
        unit.processes.push_back(parse_initial());
      } else if (match_keyword("generate")) {
        parse_generate_region(unit, previous());
      } else if (match_keyword("if")) {
        unit.generate_regions.push_back(
            parse_conditional_generate(previous()));
      } else if (match_keyword("for")) {
        unit.generate_regions.push_back(
            parse_iterative_generate(previous()));
      } else if (match_keyword("case")) {
        unit.generate_regions.push_back(
            parse_selection_generate(previous()));
      } else if (
          at(TokenKind::Identifier)
          && ((at(TokenKind::Identifier, 1)
               && at(TokenKind::LeftParen, 2))
              || (at(TokenKind::Hash, 1)
                  && at(TokenKind::LeftParen, 2)))) {
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
    for (const auto& use : external_genvar_uses_) {
      if (!declared_genvars_.contains(use.text)) {
        error(
            use,
            "FSIM-SV-PARSE-066",
            "generate loop variable '" + use.text
                + "' is not declared by inline or module-scope genvar");
      }
    }
    resolve_implicit_nets(unit);
    unit.span = span_from(start, previous());
    return unit;
  }

  void parse_import_clause(
      std::vector<SystemVerilogImport>& imports,
      const Token& start) {
    for (;;) {
      const auto package =
          expect_identifier("package name in import");
      expect(
          TokenKind::Scope,
          "'::' after imported package name",
          "FSIM-SV-PARSE-078");
      std::string name;
      if (match(TokenKind::Star)) {
        name.clear();
      } else {
        name =
            expect_identifier("imported package item").text;
      }
      imports.push_back({
          package.text,
          std::move(name),
          cover(start.span, previous().span)});
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after package import",
        "FSIM-SV-PARSE-079");
  }

  DesignUnit parse_package(const Token& start) {
    non_ansi_ports_.clear();
    body_port_declarations_.clear();
    port_type_refinements_.clear();
    implicit_net_references_.clear();
    current_procedural_names_.clear();
    current_generate_names_.clear();
    declared_genvars_.clear();
    external_genvar_uses_.clear();
    DesignUnit unit;
    unit.kind = UnitKind::SystemVerilogPackage;
    unit.language = language_;
    unit.systemverilog_imports =
        compilation_unit_imports_;
    active_package_imports_ =
        unit.systemverilog_imports;
    const auto name = expect_identifier("package name");
    unit.name = name.text;
    expect(
        TokenKind::Semicolon,
        "';' after package header",
        "FSIM-SV-PARSE-080");
    while (!at_end() && !keyword("endpackage")) {
      if (match_keyword("parameter")
          || match_keyword("localparam")) {
        parse_parameter_group(
            unit, true, false, previous());
      } else if (match_keyword("import")) {
        parse_import_clause(
            unit.systemverilog_imports, previous());
        active_package_imports_ =
            unit.systemverilog_imports;
      } else if (match_keyword("typedef")) {
        parse_typedef(unit, previous());
      } else {
        const auto unsupported = advance();
        error(
            unsupported,
            "FSIM-SV-UNSUPPORTED-023",
            "unsupported package item starting with '"
                + unsupported.text + "'");
        skip_to_semicolon();
      }
    }
    expect_keyword(
        "endpackage", false, "FSIM-SV-PARSE-081");
    if (match(TokenKind::Colon)) {
      const auto end_name =
          expect_identifier("package name after endpackage");
      if (end_name.text != unit.name) {
        error(
            end_name,
            "FSIM-SV-SEM-023",
            "package end name does not match '"
                + unit.name + "'");
      }
    }
    unit.span = span_from(start, previous());
    return unit;
  }

  void parse_genvar_declaration(DesignUnit& unit) {
    for (;;) {
      const auto name = expect_identifier("genvar name");
      const bool object_conflict =
          std::any_of(
              unit.parameters.begin(),
              unit.parameters.end(),
              [&](const ParameterDeclaration& parameter) {
                return parameter.name == name.text;
              })
          || std::any_of(
              unit.ports.begin(),
              unit.ports.end(),
              [&](const SignalDeclaration& port) {
                return port.name == name.text;
              })
          || std::any_of(
              unit.signals.begin(),
              unit.signals.end(),
              [&](const SignalDeclaration& signal) {
                return signal.name == name.text;
              });
      if (object_conflict
          || !declared_genvars_.insert(name.text).second) {
        error(
            name,
            "FSIM-SV-SEM-022",
            "duplicate or conflicting genvar declaration '"
                + name.text + "'");
      } else {
        ++current_generate_names_[name.text];
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after genvar declaration",
        "FSIM-SV-PARSE-077");
  }

  void parse_generate_region(
      DesignUnit& unit, const Token& generate_token) {
    GenerateRegion direct_region;
    direct_region.kind = GenerateKind::StaticBlock;
    std::vector<std::string> direct_local_names;
    while (!at_end() && !keyword("endgenerate")) {
      if (match_keyword("if")) {
        direct_region.then_body.generate_regions.push_back(
            parse_conditional_generate(previous()));
        continue;
      }
      if (match_keyword("for")) {
        direct_region.then_body.generate_regions.push_back(
            parse_iterative_generate(previous()));
        continue;
      }
      if (match_keyword("case")) {
        direct_region.then_body.generate_regions.push_back(
            parse_selection_generate(previous()));
        continue;
      }
      if (keyword("begin")) {
        direct_region.then_body.generate_regions.push_back(
            parse_static_generate_block());
        continue;
      }
      if (match_keyword("parameter")) {
        parse_generated_parameter_group(
            direct_region.then_body,
            direct_local_names,
            false,
            previous());
        continue;
      }
      if (match_keyword("localparam")) {
        parse_generated_parameter_group(
            direct_region.then_body,
            direct_local_names,
            true,
            previous());
        continue;
      }
      if (
          keyword("final")
          || (language_ == Language::Verilog2005
              && at(TokenKind::Identifier)
              && current().text == "final")) {
        direct_region.then_body.processes.push_back(
            parse_final());
        continue;
      }
      if (is_declaration_start()) {
        parse_generate_declaration(
            direct_region.then_body,
            direct_local_names);
        continue;
      }
      if (match_keyword("assign")) {
        if (auto assignment =
                parse_continuous_assignment(previous())) {
          direct_region.then_body.concurrent_statements.push_back(
              std::move(*assignment));
        }
        continue;
      }
      if (
          keyword("always") || keyword("always_ff")
          || keyword("always_comb") || keyword("always_latch")) {
        direct_region.then_body.processes.push_back(
            parse_always());
        continue;
      }
      if (keyword("initial")) {
        direct_region.then_body.processes.push_back(
            parse_initial());
        continue;
      }
      if (
          at(TokenKind::Identifier)
          && ((at(TokenKind::Identifier, 1)
               && at(TokenKind::LeftParen, 2))
              || (at(TokenKind::Hash, 1)
                  && at(TokenKind::LeftParen, 2)))) {
        direct_region.then_body.instances.push_back(
            parse_instance());
        continue;
      }
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-UNSUPPORTED-021",
          "only conditional, canonical genvar-for, and case instance "
          "generate regions are executable");
      while (!at_end() && !keyword("endgenerate")
             && !keyword("if") && !keyword("for")
             && !keyword("case")) {
        advance();
      }
    }
    expect_keyword(
        "endgenerate", false, "FSIM-SV-PARSE-064");
    direct_region.span =
        span_from(generate_token, previous());
    const bool has_direct_items =
        !direct_region.then_body.constants.empty()
        || !direct_region.then_body.signals.empty()
        || !direct_region.then_body.concurrent_statements.empty()
        || !direct_region.then_body.processes.empty()
        || !direct_region.then_body.instances.empty();
    if (has_direct_items) {
      unit.generate_regions.push_back(
          std::move(direct_region));
    } else {
      auto& nested =
          direct_region.then_body.generate_regions;
      unit.generate_regions.insert(
          unit.generate_regions.end(),
          std::make_move_iterator(nested.begin()),
          std::make_move_iterator(nested.end()));
    }
    for (const auto& local_name : direct_local_names) {
      const auto found = current_generate_names_.find(local_name);
      if (found != current_generate_names_.end()
          && --found->second == 0) {
        current_generate_names_.erase(found);
      }
    }
  }

  GenerateRegion parse_static_generate_block() {
    GenerateRegion result;
    result.kind = GenerateKind::StaticBlock;
    const auto start = current();
    parse_generate_branch(
        result.then_scope, result.then_body);
    result.span = span_from(start, previous());
    return result;
  }

  GenerateRegion parse_conditional_generate(
      const Token& start) {
    GenerateRegion result;
    expect(
        TokenKind::LeftParen,
        "'(' after generate if",
        "FSIM-SV-PARSE-059");
    result.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after generate condition",
        "FSIM-SV-PARSE-060");
    parse_generate_branch(
        result.then_scope,
        result.then_body);
    if (match_keyword("else")) {
      parse_generate_branch(
          result.else_scope,
          result.else_body);
    }
    result.span = span_from(start, previous());
    return result;
  }

  GenerateRegion parse_iterative_generate(const Token& start) {
    GenerateRegion result;
    result.kind = GenerateKind::Iterative;
    expect(
        TokenKind::LeftParen,
        "'(' after generate for",
        "FSIM-SV-PARSE-065");
    const bool inline_genvar = match_keyword("genvar");
    const auto variable = expect_identifier("generate loop variable");
    result.variable = variable.text;
    if (!inline_genvar) {
      external_genvar_uses_.push_back(variable);
    }
    ++current_generate_names_[result.variable];
    expect(
        TokenKind::Assign,
        "'=' after generate loop variable",
        "FSIM-SV-PARSE-067");
    result.initial = parse_expression();
    expect(
        TokenKind::Semicolon,
        "';' after generate loop initializer",
        "FSIM-SV-PARSE-068");
    result.condition = parse_expression();
    expect(
        TokenKind::Semicolon,
        "';' after generate loop condition",
        "FSIM-SV-PARSE-069");
    std::optional<Token> prefix_update;
    if (match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
      prefix_update = previous();
    }
    const auto iteration_variable =
        expect_identifier("generate iteration variable");
    if (iteration_variable.text != result.variable) {
      error(
          iteration_variable,
          "FSIM-SV-PARSE-070",
          "generate iteration must assign loop variable '"
              + result.variable + "'");
    }
    const auto stepped_iteration =
        [&](const std::string_view operation,
            Expression amount,
            const SourceSpan& span) {
          return Expression{
              ExpressionKind::Binary,
              std::string{operation},
              {
                  Expression{
                      ExpressionKind::Identifier,
                      result.variable,
                      {},
                      iteration_variable.span},
                  std::move(amount)},
              span};
        };
    const auto one = [&]() {
      return Expression{
          ExpressionKind::IntegerLiteral,
          "1",
          {},
          iteration_variable.span};
    };
    if (prefix_update) {
      result.iteration = stepped_iteration(
          prefix_update->kind == TokenKind::PlusPlus
              ? "+" : "-",
          one(),
          cover(
              prefix_update->span,
              iteration_variable.span));
    } else if (match(TokenKind::Assign)) {
      result.iteration = parse_expression();
    } else if (
        match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
      const auto update = previous();
      result.iteration = stepped_iteration(
          update.kind == TokenKind::PlusPlus ? "+" : "-",
          one(),
          cover(iteration_variable.span, update.span));
    } else if (
        match(TokenKind::PlusAssign)
        || match(TokenKind::MinusAssign)) {
      const auto update = previous();
      auto amount = parse_expression();
      result.iteration = stepped_iteration(
          update.kind == TokenKind::PlusAssign ? "+" : "-",
          std::move(amount),
          cover(iteration_variable.span, previous().span));
    } else {
      error(
          current(),
          "FSIM-SV-PARSE-071",
          "generate loop iteration must use assignment, increment, "
          "decrement, +=, or -=");
      result.iteration = Expression{
          ExpressionKind::Invalid,
          current().text,
          {},
          current().span};
      while (!at_end() && !at(TokenKind::RightParen)) {
        advance();
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after generate loop header",
        "FSIM-SV-PARSE-072");
    parse_generate_branch(
        result.then_scope,
        result.then_body);
    const auto generate_name =
        current_generate_names_.find(result.variable);
    if (generate_name != current_generate_names_.end()
        && --generate_name->second == 0) {
      current_generate_names_.erase(generate_name);
    }
    result.span = span_from(start, previous());
    return result;
  }

  GenerateRegion parse_selection_generate(const Token& start) {
    GenerateRegion result;
    result.kind = GenerateKind::Selection;
    expect(
        TokenKind::LeftParen,
        "'(' after generate case",
        "FSIM-SV-PARSE-073");
    result.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after generate case selector",
        "FSIM-SV-PARSE-074");
    bool saw_default = false;
    while (!at_end() && !keyword("endcase")) {
      GenerateAlternative alternative;
      const auto alternative_start = current();
      if (match_keyword("default")) {
        alternative.is_default = true;
        if (saw_default) {
          error(
              previous(),
              "FSIM-SV-SEM-021",
              "generate case contains more than one default item");
        }
        saw_default = true;
      } else {
        do {
          auto expression = parse_expression();
          const auto span = expression.span;
          alternative.choices.push_back(GenerateChoice{
              std::move(expression),
              std::nullopt,
              false,
              span});
        } while (match(TokenKind::Comma));
      }
      expect(
          TokenKind::Colon,
          "':' after generate case choices",
          "FSIM-SV-PARSE-075");
      parse_generate_branch(
          alternative.scope,
          alternative.body);
      alternative.span =
          span_from(alternative_start, previous());
      result.alternatives.push_back(std::move(alternative));
    }
    expect_keyword("endcase", false, "FSIM-SV-PARSE-076");
    result.span = span_from(start, previous());
    return result;
  }

  void parse_generate_branch(
      std::string& scope,
      GenerateBody& body) {
    if (!match_keyword("begin")) {
      error(
          current(),
          "FSIM-SV-PARSE-061",
          "a conditional generate branch must use a labeled begin/end "
          "block");
      return;
    }
    expect(
        TokenKind::Colon,
        "':' before generate block label",
        "FSIM-SV-PARSE-062");
    const auto label = expect_identifier("generate block label");
    scope = label.text;
    std::vector<std::string> local_names;
    while (!at_end() && !keyword("end")) {
      if (
          keyword("final")
          || (language_ == Language::Verilog2005
              && at(TokenKind::Identifier)
              && current().text == "final")) {
        body.processes.push_back(parse_final());
      } else if (is_declaration_start()) {
        parse_generate_declaration(body, local_names);
      } else if (match_keyword("parameter")) {
        parse_generated_parameter_group(
            body, local_names, false, previous());
      } else if (match_keyword("localparam")) {
        parse_generated_parameter_group(
            body, local_names, true, previous());
      } else if (match_keyword("assign")) {
        if (auto assignment =
                parse_continuous_assignment(previous())) {
          body.concurrent_statements.push_back(
              std::move(*assignment));
        }
      } else if (
          keyword("always") || keyword("always_ff")
          || keyword("always_comb") || keyword("always_latch")) {
        body.processes.push_back(parse_always());
      } else if (keyword("initial")) {
        body.processes.push_back(parse_initial());
      } else if (match_keyword("if")) {
        body.generate_regions.push_back(
            parse_conditional_generate(previous()));
      } else if (match_keyword("for")) {
        body.generate_regions.push_back(
            parse_iterative_generate(previous()));
      } else if (match_keyword("case")) {
        body.generate_regions.push_back(
            parse_selection_generate(previous()));
      } else if (keyword("begin")) {
        body.generate_regions.push_back(
            parse_static_generate_block());
      } else if (
          at(TokenKind::Identifier)
          && ((at(TokenKind::Identifier, 1)
               && at(TokenKind::LeftParen, 2))
              || (at(TokenKind::Hash, 1)
                  && at(TokenKind::LeftParen, 2)))) {
        body.instances.push_back(parse_instance());
      } else {
        const auto unsupported = advance();
        error(
            unsupported,
            "FSIM-SV-UNSUPPORTED-021",
            "unsupported item in generated module body");
        skip_to_semicolon();
      }
    }
    expect_keyword("end", false, "FSIM-SV-PARSE-063");
    if (match(TokenKind::Colon)) {
      const auto end_label = expect_identifier(
          "generate block label after end");
      if (end_label.text != scope) {
        error(
            end_label,
            "FSIM-SV-PARSE-063",
            "generate end label does not match '" + scope + "'");
      }
    }
    for (const auto& local_name : local_names) {
      const auto found = current_generate_names_.find(local_name);
      if (found != current_generate_names_.end()
          && --found->second == 0) {
        current_generate_names_.erase(found);
      }
    }
  }

  void parse_generate_declaration(
      GenerateBody& body,
      std::vector<std::string>& local_names) {
    const auto start = current();
    Type type = default_verilog_type();
    if (is_direction_keyword()) {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-022",
          "port directions are not legal generated local "
          "declarations");
      (void)parse_direction();
    }
    if (is_named_type_reference_start()) {
      type = parse_named_type();
    } else {
      parse_optional_net_type(type);
      parse_optional_signedness(type);
      parse_optional_range(type);
    }
    for (;;) {
      const auto name = expect_identifier(
          "generated local signal name");
      if (at(TokenKind::LeftBracket)) {
        const auto dimension = current();
        error(
            dimension,
            "FSIM-SV-UNSUPPORTED-007",
            "unpacked generated arrays are not implemented");
        skip_balanced(
            TokenKind::LeftBracket, TokenKind::RightBracket);
      }
      if (match(TokenKind::Assign)) {
        (void)parse_expression();
        error(
            name,
            "FSIM-SV-UNSUPPORTED-011",
            "generated declaration initializers are not executable");
      }
      const auto duplicate = std::find_if(
          body.signals.begin(),
          body.signals.end(),
          [&](const SignalDeclaration& signal) {
            return signal.name == name.text;
          });
      const bool parameter_conflict =
          std::any_of(
              body.constants.begin(),
              body.constants.end(),
              [&](const ParameterDeclaration& parameter) {
                return parameter.name == name.text;
              });
      if (parameter_conflict) {
        error(
            name,
            "FSIM-SV-SEM-020",
            "generated signal '" + name.text
                + "' conflicts with a parameter declaration");
      } else if (duplicate != body.signals.end()) {
        error(
            name,
            "FSIM-SV-SEM-006",
            "duplicate generated signal declaration '"
                + name.text + "'");
      } else {
        body.signals.push_back({
            name.text,
            type,
            PortDirection::Unknown,
            false,
            span_from(start, previous())});
        ++current_generate_names_[name.text];
        local_names.push_back(name.text);
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after generated declaration",
        "FSIM-SV-PARSE-008");
  }

  void parse_generated_parameter_group(
      GenerateBody& body,
      std::vector<std::string>& local_names,
      const bool local,
      const Token& start) {
    const auto type = parse_parameter_type();
    for (;;) {
      const auto name = expect_identifier(
          local
              ? "generated localparam name"
              : "generated parameter name");
      Expression value;
      if (match(TokenKind::Assign)) {
        value = parse_expression();
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-050",
            "value parameters require a default constant expression");
      }
      const bool signal_conflict =
          std::any_of(
              body.signals.begin(),
              body.signals.end(),
              [&](const SignalDeclaration& signal) {
                return signal.name == name.text;
              });
      const bool duplicate =
          std::any_of(
              body.constants.begin(),
              body.constants.end(),
              [&](const ParameterDeclaration& parameter) {
                return parameter.name == name.text;
              });
      if (signal_conflict) {
        error(
            name,
            "FSIM-SV-SEM-020",
            "generated parameter '" + name.text
                + "' conflicts with a signal declaration");
      } else if (duplicate) {
        error(
            name,
            "FSIM-SV-SEM-017",
            "duplicate generated parameter declaration '"
                + name.text + "'");
      } else {
        body.constants.push_back(ParameterDeclaration{
            name.text,
            type,
            std::move(value),
            true,
            cover(start.span, previous().span)});
        ++current_generate_names_[name.text];
        local_names.push_back(name.text);
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after generated parameter declaration",
        "FSIM-SV-PARSE-051");
  }

  Type parse_parameter_type() {
    Type type{
        ValueDomain::Integer,
        "implicit",
        std::nullopt,
        true};
    if (keyword("type")) {
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-UNSUPPORTED-019",
          "type parameters are not implemented; use an integral value "
          "parameter");
      return type;
    }
    if (keyword("string") || keyword("byte")
        || keyword("shortint") || keyword("longint")) {
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-UNSUPPORTED-020",
          "this parameter data type is parsed but integral constant "
          "specialization currently supports int/integer/bit/logic/reg");
      return type;
    }
    if (keyword("integer") || keyword("int")) {
      const auto token = advance();
      type.spelling = token.text;
      type.domain = ValueDomain::Integer;
      type.is_signed = true;
    } else if (
        keyword("logic") || keyword("reg") || keyword("bit")) {
      const auto token = advance();
      type.spelling = token.text;
      type.domain =
          token.text == "bit"
              ? ValueDomain::Bit2
              : ValueDomain::Logic4;
      type.is_signed = false;
    } else if (
        keyword("signed") || keyword("unsigned")
        || at(TokenKind::LeftBracket)) {
      type.spelling = "logic";
      type.domain = ValueDomain::Logic4;
      type.is_signed = false;
    } else if (is_named_type_reference_start()) {
      return parse_named_type();
    }
    parse_optional_signedness(type);
    parse_optional_range(type);
    return type;
  }

  void add_parameter(
      DesignUnit& unit,
      ParameterDeclaration parameter,
      const Token& name) {
    if (declared_genvars_.contains(parameter.name)) {
      error(
          name,
          "FSIM-SV-SEM-022",
          "parameter '" + parameter.name
              + "' conflicts with a genvar declaration");
      return;
    }
    const auto object_conflict =
        std::any_of(
            unit.ports.begin(),
            unit.ports.end(),
            [&](const SignalDeclaration& declaration) {
              return declaration.name == parameter.name;
            })
        || std::any_of(
            unit.signals.begin(),
            unit.signals.end(),
            [&](const SignalDeclaration& declaration) {
              return declaration.name == parameter.name;
            });
    if (object_conflict) {
      error(
          name,
          "FSIM-SV-SEM-020",
          "parameter '" + parameter.name
              + "' conflicts with a port or signal declaration");
      return;
    }
    if (std::any_of(
            unit.parameters.begin(),
            unit.parameters.end(),
            [&](const ParameterDeclaration& existing) {
              return existing.name == parameter.name;
            })) {
      error(
          name,
          "FSIM-SV-SEM-017",
          "duplicate parameter declaration '" + parameter.name + "'");
      return;
    }
    unit.parameters.push_back(std::move(parameter));
  }

  void parse_parameter_group(
      DesignUnit& unit,
      const bool local,
      const bool port_list,
      const Token& start) {
    const auto type = parse_parameter_type();
    for (;;) {
      const auto name = expect_identifier(
          local ? "localparam name" : "parameter name");
      Expression value;
      if (match(TokenKind::Assign)) {
        value = parse_expression();
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-050",
            "value parameters require a default constant expression");
      }
      add_parameter(
          unit,
          ParameterDeclaration{
              name.text,
              type,
              std::move(value),
              local,
              cover(start.span, previous().span)},
          name);
      if (!match(TokenKind::Comma)) {
        break;
      }
      if (port_list
          && (keyword("parameter") || keyword("localparam"))) {
        break;
      }
    }
    if (!port_list) {
      expect(
          TokenKind::Semicolon,
          "';' after parameter declaration",
          "FSIM-SV-PARSE-051");
    }
  }

  void parse_parameter_port_list(
      DesignUnit& unit,
      const Token& hash) {
    expect(
        TokenKind::LeftParen,
        "'(' after module parameter '#'",
        "FSIM-SV-PARSE-052");
    while (!at_end() && !at(TokenKind::RightParen)) {
      if (match_keyword("parameter")) {
        parse_parameter_group(unit, false, true, previous());
      } else if (match_keyword("localparam")) {
        parse_parameter_group(unit, true, true, previous());
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-053",
            "a module parameter port list item must begin with parameter "
            "or localparam");
        while (!at_end() && !at(TokenKind::Comma)
               && !at(TokenKind::RightParen)) {
          advance();
        }
        (void)match(TokenKind::Comma);
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after module parameter port list",
        "FSIM-SV-PARSE-054");
    (void)hash;
  }

  Instance parse_instance() {
    const auto start = expect_identifier("instantiated module name");
    Instance instance;
    instance.unit_name = start.text;
    if (match(TokenKind::Hash)) {
      parse_parameter_overrides(instance, previous());
    }
    const auto name = expect_identifier("instance name");
    instance.name = name.text;
    instance.unconnected_drive = current_unconnected_drive_;
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

  void parse_parameter_overrides(
      Instance& instance,
      const Token& hash) {
    expect(
        TokenKind::LeftParen,
        "'(' after instance parameter '#'",
        "FSIM-SV-PARSE-055");
    bool saw_named = false;
    bool saw_positional = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      const auto start = current();
      ParameterOverride override;
      if (match(TokenKind::Dot)) {
        saw_named = true;
        const auto name = expect_identifier("overridden parameter name");
        override.name = name.text;
        expect(
            TokenKind::LeftParen,
            "'(' after named parameter override",
            "FSIM-SV-PARSE-056");
        override.value = parse_expression();
        expect(
            TokenKind::RightParen,
            "')' after named parameter override",
            "FSIM-SV-PARSE-057");
        if (std::any_of(
                instance.parameter_overrides.begin(),
                instance.parameter_overrides.end(),
                [&](const ParameterOverride& existing) {
                  return existing.name == override.name;
                })) {
          error(
              name,
              "FSIM-SV-SEM-018",
              "duplicate named parameter override '" + name.text + "'");
        }
      } else {
        saw_positional = true;
        override.value = parse_expression();
      }
      override.span = cover(start.span, previous().span);
      instance.parameter_overrides.push_back(std::move(override));
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after instance parameter overrides",
        "FSIM-SV-PARSE-058");
    if (saw_named && saw_positional) {
      error(
          hash,
          "FSIM-SV-SEM-019",
          "named and positional parameter overrides cannot be mixed");
    }
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
          spec.type = default_port_net_type();
          declared_here = true;
          const bool explicit_type =
              is_net_type_keyword()
              || is_named_type_reference_start();
          if (is_named_type_reference_start()) {
            spec.type = parse_named_type();
          } else {
            parse_optional_net_type(spec.type);
          }
          require_default_port_net_type(current(), explicit_type);
          if (spec.type.named_type.empty()) {
            parse_optional_signedness(spec.type);
            parse_optional_range(spec.type);
          }
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
        } else if (
            std::any_of(
                unit.parameters.begin(),
                unit.parameters.end(),
                [&](const ParameterDeclaration& parameter) {
                  return parameter.name == port_name.text;
                })) {
          error(
              port_name,
              "FSIM-SV-SEM-020",
              "port '" + port_name.text
                  + "' conflicts with a parameter declaration");
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

  Type default_port_net_type() const {
    if (current_default_nettype_ == "none") {
      return default_verilog_type();
    }
    return Type{
        ValueDomain::Logic4,
        current_default_nettype_,
        std::nullopt,
        false};
  }

  void require_default_port_net_type(
      const Token& location,
      const bool explicit_type) {
    if (current_default_nettype_ == "none" && !explicit_type) {
      error(
          location,
          "FSIM-SV-SEM-016",
          "a port without an explicit net or variable type is forbidden by "
          "`default_nettype none");
    }
  }

  [[nodiscard]] bool is_net_type_keyword() const {
    return any_keyword({"wire", "reg", "logic", "bit", "integer"});
  }

  [[nodiscard]] bool is_named_type_reference_start(
      const std::size_t offset = 0) const {
    if (!at(TokenKind::Identifier, offset)
        || keyword_reserved(keyword_set_, current(offset).text)) {
      return false;
    }
    if (contains_word(
            {"always_ff", "always_comb", "always_latch"},
            current(offset).text)) {
      return false;
    }
    if (at(TokenKind::Scope, offset + 1)) {
      return at(TokenKind::Identifier, offset + 2)
          && at(TokenKind::Identifier, offset + 3);
    }
    return at(TokenKind::Identifier, offset + 1)
        && !at(TokenKind::LeftParen, offset + 2)
        && !at(TokenKind::Hash, offset + 2);
  }

  Type parse_named_type() {
    const auto first =
        expect_identifier("SystemVerilog type name");
    std::string name = first.text;
    auto span = first.span;
    if (match(TokenKind::Scope)) {
      const auto selected =
          expect_identifier("package type name");
      name += "::";
      name += selected.text;
      span = cover(span, selected.span);
    }
    Type type{
        ValueDomain::Unknown,
        name,
        std::nullopt,
        false};
    type.named_type = name;
    type.named_type_span = span;
    return type;
  }

  void parse_typedef(
      DesignUnit& unit,
      const Token& start) {
    Type type;
    std::vector<std::pair<
        ParameterDeclaration,
        Token>> enum_parameters;
    std::vector<EnumLiteralDeclaration> enum_literals;
    if (keyword("struct") || keyword("union")) {
      const bool is_union = match_keyword("union");
      if (!is_union) {
        (void)match_keyword("struct");
      }
      if (!match_keyword("packed")) {
        error(
            current(),
            "FSIM-SV-UNSUPPORTED-027",
            "bounded struct/union typedefs require the packed qualifier");
        skip_to_semicolon();
        return;
      }
      type.spelling =
          is_union ? "union packed" : "struct packed";
      type.packed_aggregate =
          is_union
              ? PackedAggregateKind::Union
              : PackedAggregateKind::Struct;
      type.domain = ValueDomain::Bit2;
      parse_optional_signedness(type);
      expect(
          TokenKind::LeftBrace,
          "'{' before packed aggregate members",
          "FSIM-SV-PARSE-086");
      if (at(TokenKind::RightBrace)) {
        error(
            current(),
            "FSIM-SV-PARSE-089",
            "bounded packed aggregates require at least one member");
      }
      std::unordered_set<std::string> member_names;
      while (!at_end() && !at(TokenKind::RightBrace)) {
        const auto member_start = current();
        Type member_type;
        if (keyword("logic") || keyword("reg")
            || keyword("bit")) {
          const auto type_token = advance();
          member_type.spelling = type_token.text;
          member_type.domain =
              type_token.text == "bit"
                  ? ValueDomain::Bit2
                  : ValueDomain::Logic4;
          parse_optional_signedness(member_type);
          parse_optional_range(member_type);
        } else {
          error(
              current(),
              "FSIM-SV-UNSUPPORTED-028",
              "packed aggregate members require a non-aggregate bit, logic, "
              "or reg type");
          skip_to_semicolon();
          continue;
        }
        for (;;) {
          const auto member =
              expect_identifier("packed aggregate member name");
          if (at(TokenKind::LeftBracket)) {
            error(
                current(),
                "FSIM-SV-UNSUPPORTED-029",
                "unpacked aggregate member dimensions are not implemented");
            skip_balanced(
                TokenKind::LeftBracket,
                TokenKind::RightBracket);
          }
          if (match(TokenKind::Assign)) {
            (void)parse_expression();
            error(
                member,
                "FSIM-SV-UNSUPPORTED-029",
                "packed aggregate member initializers are not implemented");
          }
          if (!member_names.insert(member.text).second) {
            error(
                member,
                "FSIM-SV-SEM-025",
                "duplicate packed aggregate member '"
                    + member.text + "'");
          } else {
            type.packed_members.push_back(PackedMember{
                member.text,
                member_type.domain,
                member_type.spelling,
                member_type.packed_range,
                member_type.is_signed,
                member_type.packed_range_expression,
                0,
                cover(member_start.span, previous().span)});
            if (member_type.domain == ValueDomain::Logic4) {
              type.domain = ValueDomain::Logic4;
            }
          }
          if (!match(TokenKind::Comma)) {
            break;
          }
        }
        expect(
            TokenKind::Semicolon,
            "';' after packed aggregate member declaration",
            "FSIM-SV-PARSE-088");
      }
      expect(
          TokenKind::RightBrace,
          "'}' after packed aggregate members",
          "FSIM-SV-PARSE-087");
      std::uint64_t total_width = 0;
      std::optional<std::uint64_t> union_width;
      bool concrete = !type.packed_members.empty();
      for (const auto& member : type.packed_members) {
        const auto width = member.width();
        if (!width || *width == 0) {
          concrete = false;
          break;
        }
        if (is_union) {
          if (union_width && *union_width != *width) {
            concrete = false;
            break;
          }
          union_width = *width;
          total_width = *width;
        } else {
          if (*width
              > std::numeric_limits<std::uint64_t>::max()
                    - total_width) {
            concrete = false;
            break;
          }
          total_width += *width;
        }
      }
      if (concrete
          && total_width - 1U
              <= static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())) {
        if (!is_union) {
          auto offset = total_width;
          for (auto& member : type.packed_members) {
            offset -= *member.width();
            member.lsb_offset = offset;
          }
        }
        type.packed_range = PackedRange{
            static_cast<std::int64_t>(total_width - 1U),
            0,
            true};
      }
    } else if (match_keyword("enum")) {
      if (keyword("logic") || keyword("reg")
          || keyword("bit")) {
        const auto type_token = advance();
        type.spelling = type_token.text;
        type.domain =
            type_token.text == "bit"
                ? ValueDomain::Bit2
                : ValueDomain::Logic4;
        parse_optional_signedness(type);
        parse_optional_range(type);
      } else {
        error(
            current(),
            "FSIM-SV-UNSUPPORTED-026",
            "bounded enum typedefs require an explicit bit, logic, or "
            "reg base type");
        skip_to_semicolon();
        return;
      }
      expect(
          TokenKind::LeftBrace,
          "'{' before enum literals",
          "FSIM-SV-PARSE-083");
      if (at(TokenKind::RightBrace)) {
        error(
            current(),
            "FSIM-SV-PARSE-085",
            "bounded enum typedefs require at least one literal");
      }
      std::optional<std::string> previous_literal;
      while (!at_end() && !at(TokenKind::RightBrace)) {
        const auto literal =
            expect_identifier("enum literal name");
        Expression value;
        if (match(TokenKind::Assign)) {
          value = parse_expression();
        } else if (!previous_literal) {
          value = Expression{
              ExpressionKind::IntegerLiteral,
              "0",
              {},
              literal.span};
        } else {
          value = Expression{
              ExpressionKind::Binary,
              "+",
              {
                  Expression{
                      ExpressionKind::Identifier,
                      *previous_literal,
                      {},
                      literal.span},
                  Expression{
                      ExpressionKind::IntegerLiteral,
                      "1",
                      {},
                      literal.span},
              },
              literal.span};
        }
        enum_literals.push_back({
            literal.text, value,
            cover(literal.span, previous().span)});
        enum_parameters.push_back({
            ParameterDeclaration{
                literal.text,
                type,
                std::move(value),
                true,
                cover(literal.span, previous().span)},
            literal});
        previous_literal = literal.text;
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
      expect(
          TokenKind::RightBrace,
          "'}' after enum literals",
          "FSIM-SV-PARSE-084");
    } else if (keyword("logic") || keyword("reg")
        || keyword("bit") || keyword("integer")
        || keyword("int")) {
      const auto type_token = advance();
      type.spelling = type_token.text;
      if (type_token.text == "bit") {
        type.domain = ValueDomain::Bit2;
      } else if (
          type_token.text == "integer"
          || type_token.text == "int") {
        type.domain = ValueDomain::Integer;
        type.is_signed = true;
      } else {
        type.domain = ValueDomain::Logic4;
      }
      parse_optional_signedness(type);
      parse_optional_range(type);
    } else if (is_named_type_reference_start()) {
      type = parse_named_type();
    } else {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-024",
          "bounded typedef declarations require an integral built-in "
          "or previously declared user type");
      skip_to_semicolon();
      return;
    }
    const auto name = expect_identifier("typedef name");
    if (at(TokenKind::LeftBracket)) {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-025",
          "unpacked typedef dimensions are not implemented");
      skip_balanced(
          TokenKind::LeftBracket, TokenKind::RightBracket);
    }
    expect(
        TokenKind::Semicolon,
        "';' after typedef declaration",
        "FSIM-SV-PARSE-082");
    const bool duplicate = std::any_of(
        unit.type_aliases.begin(),
        unit.type_aliases.end(),
        [&](const TypeAliasDeclaration& alias) {
          return alias.name == name.text;
        });
    if (duplicate) {
      error(
          name,
          "FSIM-SV-SEM-024",
          "duplicate typedef declaration '" + name.text + "'");
      return;
    }
    unit.type_aliases.push_back({
        name.text,
        std::move(type),
        cover(start.span, previous().span),
        std::move(enum_literals)});
    for (auto& [parameter, parameter_name] :
         enum_parameters) {
      add_parameter(
          unit, std::move(parameter), parameter_name);
    }
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
    auto left_expression = parse_expression();
    expect(TokenKind::Colon, "':' in packed range", "FSIM-SV-PARSE-005");
    auto right_expression = parse_expression();
    expect(TokenKind::RightBracket, "']' after packed range",
           "FSIM-SV-PARSE-006");
    const auto left = simple_integer_constant(left_expression);
    const auto right = simple_integer_constant(right_expression);
    if (left && right) {
      type.packed_range = PackedRange{*left, *right, *left >= *right};
    }
    type.packed_range_expression = PackedRangeExpression{
        std::move(left_expression),
        std::move(right_expression),
        cover(start.span, previous().span),
        std::nullopt};
  }

  [[nodiscard]] bool is_declaration_start() const {
    return is_direction_keyword() || is_net_type_keyword()
        || is_named_type_reference_start();
  }

  void parse_event_declaration(
      DesignUnit& unit, const Token& start) {
    Type type = default_verilog_type();
    type.spelling = "event";
    type.domain = ValueDomain::Logic4;
    for (;;) {
      const auto name = expect_identifier("named event");
      const auto duplicate_signal = std::find_if(
          unit.signals.begin(),
          unit.signals.end(),
          [&](const SignalDeclaration& signal) {
            return signal.name == name.text;
          });
      const auto duplicate_port = std::find_if(
          unit.ports.begin(),
          unit.ports.end(),
          [&](const SignalDeclaration& port) {
            return port.name == name.text;
          });
      if (duplicate_signal != unit.signals.end()
          || duplicate_port != unit.ports.end()) {
        error(
            name,
            "FSIM-SV-SEM-035",
            "duplicate named event or object declaration '"
                + name.text + "'");
      } else {
        unit.signals.push_back(SignalDeclaration{
            name.text,
            type,
            PortDirection::Unknown,
            false,
            name.span});
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after named event declaration",
        "FSIM-SV-PARSE-117");
    if (!unit.signals.empty()) {
      unit.signals.back().span = span_from(start, previous());
    }
  }

  void parse_declaration(DesignUnit& unit) {
    const auto start = current();
    VerilogTypeSpec spec;
    spec.type = default_verilog_type();
    if (is_direction_keyword()) {
      spec.direction = parse_direction();
      spec.type = default_port_net_type();
      const bool explicit_type =
          is_net_type_keyword()
          || is_named_type_reference_start();
      if (is_named_type_reference_start()) {
        spec.type = parse_named_type();
      } else {
        parse_optional_net_type(spec.type);
      }
      require_default_port_net_type(current(), explicit_type);
    } else if (is_named_type_reference_start()) {
      spec.type = parse_named_type();
    } else {
      parse_optional_net_type(spec.type);
    }
    if (spec.type.named_type.empty()) {
      parse_optional_signedness(spec.type);
      parse_optional_range(spec.type);
    }

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
      const auto parameter_conflict = std::any_of(
          unit.parameters.begin(),
          unit.parameters.end(),
          [&](const ParameterDeclaration& parameter) {
            return parameter.name == declaration.name;
          });
      if (declared_genvars_.contains(declaration.name)) {
        error(
            name,
            "FSIM-SV-SEM-022",
            "object '" + declaration.name
                + "' conflicts with a genvar declaration");
        if (!match(TokenKind::Comma)) {
          break;
        }
        continue;
      }
      if (parameter_conflict) {
        error(
            name,
            "FSIM-SV-SEM-020",
            "object '" + declaration.name
                + "' conflicts with a parameter declaration");
        if (!match(TokenKind::Comma)) {
          break;
        }
        continue;
      }
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
    if (is_named_type_reference_start()) {
      type = parse_named_type();
    } else {
      parse_optional_net_type(type);
    }
    if (type.named_type.empty() && type.spelling == "wire") {
      error(
          start,
          "FSIM-SV-UNSUPPORTED-014",
          "procedural wire declarations are not supported; use a variable "
          "type");
    }
    if (type.named_type.empty()) {
      parse_optional_signedness(type);
      parse_optional_range(type);
    }

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
      current_procedural_names_.insert(name.text);
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

  [[nodiscard]] bool is_gate_primitive() const {
    return keyword("buf") || keyword("not")
        || keyword("and") || keyword("nand")
        || keyword("or") || keyword("nor")
        || keyword("xor") || keyword("xnor");
  }

  void parse_gate_primitive(std::vector<Statement>& statements) {
    const auto start = advance();
    const auto operation = detail::ascii_lower(start.text);
    const auto strength_keyword =
        [](const std::string_view text) {
          return text == "supply0" || text == "supply1"
              || text == "strong0" || text == "strong1"
              || text == "pull0" || text == "pull1"
              || text == "weak0" || text == "weak1"
              || text == "highz0" || text == "highz1";
        };
    if (at(TokenKind::LeftParen)
        && at(TokenKind::Identifier, 1)
        && strength_keyword(current(1).text)
        && at(TokenKind::Comma, 2)
        && at(TokenKind::Identifier, 3)
        && strength_keyword(current(3).text)
        && at(TokenKind::RightParen, 4)) {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-030",
          "gate drive strengths are not implemented");
      skip_to_semicolon();
      return;
    }
    std::optional<Delay> delay;
    if (match(TokenKind::Hash)) {
      delay = parse_verilog_delay(previous());
    }

    const bool unary = operation == "buf" || operation == "not";
    do {
      const auto instance_start = current();
      if (at(TokenKind::Identifier)
          && at(TokenKind::LeftParen, 1)) {
        advance();  // Optional instance name.
      }
      expect(
          TokenKind::LeftParen,
          "'(' after gate primitive name",
          "FSIM-SV-PARSE-091");
      if (!at(TokenKind::Identifier)) {
        error(
            current(),
            "FSIM-SV-PARSE-092",
            "a gate primitive requires an output lvalue first");
        skip_to_semicolon();
        return;
      }
      auto target = parse_lvalue();
      std::vector<Expression> inputs;
      while (match(TokenKind::Comma)) {
        inputs.push_back(parse_expression());
      }
      expect(
          TokenKind::RightParen,
          "')' after gate primitive terminals",
          "FSIM-SV-PARSE-093");

      if ((unary && inputs.size() != 1)
          || (!unary && inputs.size() < 2)) {
        error(
            instance_start,
            "FSIM-SV-SEM-026",
            unary
                ? "buf/not primitives require exactly one input terminal"
                : "logic gate primitives require at least two input terminals");
        continue;
      }

      Expression value = std::move(inputs.front());
      if (unary) {
        if (operation == "not") {
          const auto combined = cover(start.span, value.span);
          value = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(value)},
              combined};
        }
      } else {
        const auto binary_operation =
            operation == "and" || operation == "nand"
                ? "&"
                : operation == "or" || operation == "nor"
                    ? "|"
                    : "^";
        for (std::size_t index = 1;
             index < inputs.size(); ++index) {
          const auto combined =
              cover(value.span, inputs[index].span);
          value = Expression{
              ExpressionKind::Binary,
              binary_operation,
              {std::move(value), std::move(inputs[index])},
              combined};
        }
        if (operation == "nand" || operation == "nor"
            || operation == "xnor") {
          const auto combined = cover(start.span, value.span);
          value = Expression{
              ExpressionKind::Unary,
              "~",
              {std::move(value)},
              combined};
        }
      }

      Statement statement;
      statement.kind = StatementKind::Assignment;
      statement.assignment_kind = AssignmentKind::Continuous;
      statement.target = std::move(target);
      statement.value = std::move(value);
      statement.delay = delay;
      statement.span = cover(start.span, previous().span);
      statements.push_back(std::move(statement));
    } while (match(TokenKind::Comma));
    expect(
        TokenKind::Semicolon,
        "';' after gate primitive",
        "FSIM-SV-PARSE-094");
  }

  Process parse_always() {
    const auto start = advance();
    current_procedural_names_.clear();
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
      if (body->kind == StatementKind::Block
          && body->label.empty()) {
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
    current_procedural_names_.clear();
    return process;
  }

  Process parse_initial() {
    const auto start =
        expect_keyword("initial", false, "FSIM-SV-PARSE-013");
    current_procedural_names_.clear();
    Process process;
    process.kind = ProcessKind::Initial;
    auto body = parse_statement();
    if (body) {
      if (body->kind == StatementKind::Block
          && body->label.empty()) {
        process.variables = std::move(body->declarations);
        process.statements = std::move(body->statements);
      } else {
        process.statements.push_back(std::move(*body));
      }
    }
    process.span = span_from(start, previous());
    current_procedural_names_.clear();
    return process;
  }

  Process parse_final() {
    const auto start =
        keyword("final")
            || (language_ == Language::Verilog2005
                && at(TokenKind::Identifier)
                && current().text == "final")
        ? advance()
        : expect_keyword("final", false, "FSIM-SV-PARSE-111");
    current_procedural_names_.clear();
    Process process;
    process.kind = ProcessKind::Final;
    if (language_ == Language::Verilog2005) {
      error(
          start,
          "FSIM-VERILOG-SEM-006",
          "final procedures require SystemVerilog");
    }
    auto body = parse_statement();
    if (body) {
      if (body->kind == StatementKind::Block
          && body->label.empty()) {
        process.variables = std::move(body->declarations);
        process.statements = std::move(body->statements);
      } else {
        process.statements.push_back(std::move(*body));
      }
    }

    const auto inspect =
        [&](const auto& self,
            const std::vector<Statement>& statements,
            bool& has_suspension,
            bool& has_nonblocking) -> void {
      for (const auto& statement : statements) {
        has_suspension =
            has_suspension
            || statement.kind == StatementKind::Delay
            || statement.kind == StatementKind::WaitOn
            || statement.kind == StatementKind::WaitUntil
            || statement.kind == StatementKind::Pause
            || statement.kind == StatementKind::Finish;
        has_nonblocking =
            has_nonblocking
            || (statement.kind == StatementKind::Assignment
                && statement.assignment_kind
                    == AssignmentKind::NonBlocking);
        self(
            self, statement.statements,
            has_suspension, has_nonblocking);
        self(
            self, statement.else_statements,
            has_suspension, has_nonblocking);
        for (const auto& alternative :
             statement.case_alternatives) {
          self(
              self, alternative.statements,
              has_suspension, has_nonblocking);
        }
      }
    };
    bool has_suspension = false;
    bool has_nonblocking = false;
    inspect(
        inspect, process.statements,
        has_suspension, has_nonblocking);
    if (has_suspension) {
      error(
          start,
          "FSIM-SV-SEM-032",
          "a final procedure cannot contain a timing control, wait, "
          "or $finish");
    }
    if (has_nonblocking) {
      error(
          start,
          "FSIM-SV-SEM-033",
          "a final procedure cannot contain a nonblocking assignment");
    }
    process.span = span_from(start, previous());
    current_procedural_names_.clear();
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
      if (signal_name.find('.') == std::string::npos) {
        note_implicit_net_reference(signal);
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

  Statement parse_case_statement(
      const Token& start, const CaseMatchKind match_kind) {
    Statement statement;
    statement.kind = StatementKind::Case;
    statement.case_match_kind = match_kind;
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

  void parse_procedural_loop_body(
      const Token& start, Statement& statement) {
    (void)start;
    ++current_loop_depth_;
    auto body = parse_statement();
    --current_loop_depth_;
    if (!body) {
      return;
    }
    if (body->kind == StatementKind::Block
        && body->label.empty()
        && body->declarations.empty()) {
      statement.statements = std::move(body->statements);
      return;
    }
    statement.statements.push_back(std::move(*body));
  }

  Statement parse_procedural_for_statement(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::Loop;
    expect(
        TokenKind::LeftParen,
        "'(' after procedural for",
        "FSIM-SV-PARSE-095");
    if (!match_keyword("int") && !match_keyword("integer")) {
      error(
          current(),
          "FSIM-SV-UNSUPPORTED-031",
          "this bounded procedural for-loop slice requires an inline "
          "int or integer loop variable");
    }
    const auto variable =
        expect_identifier("procedural loop variable");
    statement.loop_variable = variable.text;
    expect(
        TokenKind::Assign,
        "'=' after procedural loop variable",
        "FSIM-SV-PARSE-096");
    statement.loop_initial = parse_expression();
    ++current_loop_names_[statement.loop_variable];
    expect(
        TokenKind::Semicolon,
        "';' after procedural loop initializer",
        "FSIM-SV-PARSE-097");

    auto condition = parse_expression();
    expect(
        TokenKind::Semicolon,
        "';' after procedural loop condition",
        "FSIM-SV-PARSE-098");
    if (condition.kind != ExpressionKind::Binary
        || condition.operands.size() != 2
        || condition.operands.front().kind
            != ExpressionKind::Identifier
        || condition.operands.front().text
            != statement.loop_variable
        || (condition.text != "<" && condition.text != "<="
            && condition.text != ">" && condition.text != ">=")) {
      error(
          variable,
          "FSIM-SV-SEM-027",
          "bounded procedural for-loop condition must compare the loop "
          "variable against a locally static upper or lower bound");
      statement.loop_limit = Expression{
          ExpressionKind::Invalid, {}, {}, condition.span};
    } else {
      statement.loop_descending =
          condition.text == ">" || condition.text == ">=";
      statement.loop_limit_exclusive =
          condition.text == "<" || condition.text == ">";
      statement.loop_limit = std::move(condition.operands[1]);
    }

    std::optional<Token> prefix_update;
    if (match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
      prefix_update = previous();
    }
    const auto iteration_variable =
        expect_identifier("procedural loop iteration variable");
    if (iteration_variable.text != statement.loop_variable) {
      error(
          iteration_variable,
          "FSIM-SV-SEM-028",
          "procedural loop iteration must update loop variable '"
              + statement.loop_variable + "'");
    }
    std::string update_operation;
    std::optional<std::int64_t> update_amount;
    if (prefix_update) {
      update_operation =
          prefix_update->kind == TokenKind::PlusPlus ? "+" : "-";
      update_amount = 1;
    } else if (
        match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
      update_operation =
          previous().kind == TokenKind::PlusPlus ? "+" : "-";
      update_amount = 1;
    } else if (
        match(TokenKind::PlusAssign)
        || match(TokenKind::MinusAssign)) {
      update_operation =
          previous().kind == TokenKind::PlusAssign ? "+" : "-";
      update_amount = simple_integer_constant(parse_expression());
    } else if (match(TokenKind::Assign)) {
      auto iteration = parse_expression();
      if (iteration.kind == ExpressionKind::Binary
          && iteration.operands.size() == 2
          && iteration.operands[0].kind
              == ExpressionKind::Identifier
          && iteration.operands[0].text
              == statement.loop_variable
          && (iteration.text == "+" || iteration.text == "-")) {
        update_operation = iteration.text;
        update_amount =
            simple_integer_constant(iteration.operands[1]);
      }
    }
    if (!update_amount || *update_amount != 1
        || (statement.loop_descending
                ? update_operation != "-"
                : update_operation != "+")) {
      error(
          iteration_variable,
          "FSIM-SV-SEM-029",
          "bounded procedural for-loop iteration must advance by one "
          "toward its comparison bound");
    }
    expect(
        TokenKind::RightParen,
        "')' after procedural loop header",
        "FSIM-SV-PARSE-099");
    parse_procedural_loop_body(start, statement);
    const auto loop_name =
        current_loop_names_.find(statement.loop_variable);
    if (loop_name != current_loop_names_.end()
        && --loop_name->second == 0) {
      current_loop_names_.erase(loop_name);
    }
    statement.span = span_from(start, previous());
    return statement;
  }

  Statement parse_repeat_statement(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_repeat = true;
    statement.loop_limit_exclusive = true;
    statement.loop_initial = Expression{
        ExpressionKind::IntegerLiteral,
        "0",
        {},
        start.span};
    expect(
        TokenKind::LeftParen,
        "'(' after repeat",
        "FSIM-SV-PARSE-100");
    statement.loop_limit = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after repeat count",
        "FSIM-SV-PARSE-101");
    parse_procedural_loop_body(start, statement);
    statement.span = span_from(start, previous());
    return statement;
  }

  Statement parse_while_statement(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_runtime = true;
    expect(
        TokenKind::LeftParen,
        "'(' after while",
        "FSIM-SV-PARSE-102");
    statement.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after while condition",
        "FSIM-SV-PARSE-103");
    parse_procedural_loop_body(start, statement);
    statement.span = span_from(start, previous());
    return statement;
  }

  Statement parse_do_while_statement(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_runtime = true;
    statement.loop_post_test = true;
    parse_procedural_loop_body(start, statement);
    expect_keyword(
        "while", false, "FSIM-SV-PARSE-105");
    expect(
        TokenKind::LeftParen,
        "'(' after do-while body",
        "FSIM-SV-PARSE-106");
    statement.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after do-while condition",
        "FSIM-SV-PARSE-107");
    expect(
        TokenKind::Semicolon,
        "';' after do-while statement",
        "FSIM-SV-PARSE-108");
    statement.span = span_from(start, previous());
    return statement;
  }

  Statement parse_forever_statement(const Token& start) {
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_runtime = true;
    statement.condition = Expression{
        ExpressionKind::LogicLiteral,
        "1'b1",
        {},
        start.span};
    parse_procedural_loop_body(start, statement);
    const auto contains_timing =
        [&](const auto& self,
            const std::vector<Statement>& statements) -> bool {
      for (const auto& child : statements) {
        if (child.kind == StatementKind::Delay
            || child.kind == StatementKind::WaitOn
            || self(self, child.statements)
            || self(self, child.else_statements)) {
          return true;
        }
        for (const auto& alternative :
             child.case_alternatives) {
          if (self(self, alternative.statements)) {
            return true;
          }
        }
      }
      return false;
    };
    if (!contains_timing(
            contains_timing, statement.statements)) {
      error(
          start,
          "FSIM-SV-SEM-030",
          "a bounded forever loop requires a timing control so it can "
          "suspend the simulation process");
    }
    statement.span = span_from(start, previous());
    return statement;
  }

  void parse_fatal_arguments(Statement& statement) {
    statement.assertion_message = "$fatal";
    statement.assertion_severity = AssertionSeverity::Failure;
    if (!match(TokenKind::LeftParen)) {
      return;
    }
    if (!at(TokenKind::RightParen)) {
      if (at(TokenKind::StringLiteral)) {
        statement.assertion_message =
            string_literal_text(advance());
      } else {
        // Accept and ignore the standard numeric finish control while
        // retaining one bounded literal display message.
        (void)parse_expression();
        if (match(TokenKind::Comma)) {
          const auto message = expect(
              TokenKind::StringLiteral,
              "literal message after the $fatal finish argument",
              "FSIM-SV-PARSE-114");
          statement.assertion_message =
              string_literal_text(message);
        }
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after $fatal arguments",
        "FSIM-SV-PARSE-115");
  }

  std::optional<Statement> parse_statement() {
    if (match(TokenKind::ThinArrow)) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::EventTrigger;
      if (match(TokenKind::Greater)) {
        statement.assignment_kind = AssignmentKind::NonBlocking;
        if (language_ == Language::Verilog2005) {
          error(
              previous(),
              "FSIM-VERILOG-SEM-008",
              "nonblocking named-event trigger '->>' requires "
              "SystemVerilog");
        }
      }
      if (match(TokenKind::Hash)) {
        const auto delay_start = previous();
        statement.delay = parse_verilog_delay(delay_start);
        if (statement.assignment_kind
            != AssignmentKind::NonBlocking) {
          error(
              delay_start,
              "FSIM-SV-SEM-036",
              "a delayed named-event trigger requires nonblocking "
              "'->>' syntax");
        }
      }
      const auto event = expect_identifier("named event after '->'");
      statement.target = Expression{
          ExpressionKind::Identifier,
          event.text,
          {},
          event.span};
      expect(
          TokenKind::Semicolon,
          "';' after named-event trigger",
          "FSIM-SV-PARSE-118");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (language_ == Language::SystemVerilog2017
        && (keyword("unique") || keyword("unique0")
            || keyword("priority"))
        && (keyword("case", 1) || keyword("casez", 1)
            || keyword("casex", 1))) {
      const auto qualifier = advance();
      error(qualifier, "FSIM-SV-UNSUPPORTED-017",
            "unique and priority case qualifiers are not implemented");
    }
    if (match_keyword("case")) {
      return parse_case_statement(previous(), CaseMatchKind::Exact);
    }
    if (match_keyword("casez")) {
      return parse_case_statement(previous(), CaseMatchKind::WildcardZ);
    }
    if (match_keyword("casex")) {
      return parse_case_statement(previous(), CaseMatchKind::WildcardXZ);
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("for")) {
      return parse_procedural_for_statement(previous());
    }
    if (match_keyword("repeat")) {
      return parse_repeat_statement(previous());
    }
    if (match_keyword("while")) {
      return parse_while_statement(previous());
    }
    if (language_ == Language::SystemVerilog2017
        && match_keyword("do")) {
      return parse_do_while_statement(previous());
    }
    if (match_keyword("forever")) {
      return parse_forever_statement(previous());
    }
    if (language_ == Language::SystemVerilog2017
        && (keyword("break") || keyword("continue"))) {
      const auto start = advance();
      const auto is_break = start.text == "break";
      if (current_loop_depth_ == 0) {
        error(
            start,
            "FSIM-SV-SEM-031",
            std::string{"a SystemVerilog "}
                + (is_break ? "break" : "continue")
                + " statement must be nested in a procedural loop");
      }
      expect(
          TokenKind::Semicolon,
          "';' after SystemVerilog break or continue statement",
          "FSIM-SV-PARSE-104");
      Statement statement;
      statement.kind =
          is_break ? StatementKind::Break : StatementKind::Continue;
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("wait")) {
      const auto start = previous();
      Statement statement;
      statement.kind = StatementKind::WaitUntil;
      expect(
          TokenKind::LeftParen,
          "'(' after wait",
          "FSIM-SV-PARSE-109");
      statement.condition = parse_expression();
      expect(
          TokenKind::RightParen,
          "')' after wait condition",
          "FSIM-SV-PARSE-110");
      if (auto controlled = parse_statement()) {
        statement.statements.push_back(
            std::move(*controlled));
      }
      statement.span = span_from(start, previous());
      return statement;
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
        if (match_keyword("$fatal")) {
          parse_fatal_arguments(statement);
        } else {
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
      }
      expect(TokenKind::Semicolon, "';' after assertion",
             "FSIM-SV-PARSE-044");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (keyword("$fatal")) {
      const auto start = advance();
      Statement statement;
      statement.kind = StatementKind::Assert;
      statement.condition = Expression{
          ExpressionKind::IntegerLiteral,
          "0",
          {},
          start.span};
      if (language_ == Language::Verilog2005) {
        error(
            start,
            "FSIM-VERILOG-SEM-007",
            "$fatal requires SystemVerilog");
      }
      parse_fatal_arguments(statement);
      expect(
          TokenKind::Semicolon,
          "';' after $fatal",
          "FSIM-SV-PARSE-116");
      statement.span = span_from(start, previous());
      return statement;
    }
    if (match_keyword("begin")) {
      const auto start = previous();
      std::string opening_label;
      if (match(TokenKind::Colon)) {
        opening_label = expect_identifier("block name").text;
      }
      Statement block;
      block.kind = StatementKind::Block;
      block.label = opening_label;
      while (!at_end() && !keyword("end")) {
        const auto before = position();
        if (is_declaration_start()) {
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
        const auto closing_label = expect_identifier("block name");
        if (opening_label.empty()) {
          error(
              closing_label,
              "FSIM-SV-SEM-034",
              "an end block label requires a matching opening label");
        } else if (closing_label.text != opening_label) {
          error(
              closing_label,
              "FSIM-SV-SEM-034",
              "end block label '" + closing_label.text
                  + "' does not match opening label '"
                  + opening_label + "'");
        }
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
        if (true_branch->kind == StatementKind::Block
            && true_branch->label.empty()
            && true_branch->declarations.empty()) {
          statement.statements = std::move(true_branch->statements);
        } else {
          statement.statements.push_back(std::move(*true_branch));
        }
      }
      if (match_keyword("else")) {
        if (auto false_branch = parse_statement()) {
          if (false_branch->kind == StatementKind::Block
              && false_branch->label.empty()
              && false_branch->declarations.empty()) {
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
    if (keyword("$stop")) {
      const auto start = advance();
      if (match(TokenKind::LeftParen)) {
        if (!at(TokenKind::RightParen)) {
          (void)parse_expression();
        }
        expect(
            TokenKind::RightParen,
            "')' after $stop",
            "FSIM-SV-PARSE-112");
      }
      expect(
          TokenKind::Semicolon,
          "';' after $stop",
          "FSIM-SV-PARSE-113");
      Statement statement;
      statement.kind = StatementKind::Pause;
      statement.span = span_from(start, previous());
      return statement;
    }

    if (match(TokenKind::Semicolon)) {
      Statement statement;
      statement.kind = StatementKind::Null;
      statement.span = previous().span;
      return statement;
    }

    if (at(TokenKind::Identifier)
        || at(TokenKind::PlusPlus)
        || at(TokenKind::MinusMinus)) {
      const auto before = position();
      const auto start = current();
      std::optional<Token> prefix_update;
      if (match(TokenKind::PlusPlus)
          || match(TokenKind::MinusMinus)) {
        prefix_update = previous();
      }
      Expression target = parse_lvalue();
      AssignmentKind assignment_kind{AssignmentKind::Blocking};
      std::optional<std::string> update_operation;
      bool unit_update = prefix_update.has_value();
      if (prefix_update) {
        update_operation =
            prefix_update->kind == TokenKind::PlusPlus ? "+" : "-";
      } else if (match(TokenKind::PlusPlus)
                 || match(TokenKind::MinusMinus)) {
        update_operation =
            previous().kind == TokenKind::PlusPlus ? "+" : "-";
        unit_update = true;
      } else if (match(TokenKind::LessEqual)) {
        assignment_kind = AssignmentKind::NonBlocking;
      } else if (match(TokenKind::Assign)) {
        assignment_kind = AssignmentKind::Blocking;
      } else {
        const auto compound_operation =
            [](const TokenKind kind)
                -> std::optional<std::string_view> {
              switch (kind) {
              case TokenKind::PlusAssign:
                return "+";
              case TokenKind::MinusAssign:
                return "-";
              case TokenKind::StarAssign:
                return "*";
              case TokenKind::SlashAssign:
                return "/";
              case TokenKind::PercentAssign:
                return "%";
              case TokenKind::AmpersandAssign:
                return "&";
              case TokenKind::PipeAssign:
                return "|";
              case TokenKind::CaretAssign:
                return "^";
              case TokenKind::ShiftLeftAssign:
                return "<<";
              case TokenKind::ShiftRightAssign:
                return ">>";
              case TokenKind::ArithmeticShiftLeftAssign:
                return "<<<";
              case TokenKind::ArithmeticShiftRightAssign:
                return ">>>";
              default:
                return std::nullopt;
              }
            }(current().kind);
        if (compound_operation) {
          update_operation = *compound_operation;
          advance();
        } else {
          rewind(before);
          const auto unsupported = advance();
          error(unsupported, "FSIM-SV-UNSUPPORTED-008",
                "unsupported procedural statement starting with '" +
                    unsupported.text + "'");
          skip_to_semicolon();
          return std::nullopt;
        }
      }
      if (update_operation
          && language_ == Language::Verilog2005) {
        error(
            start,
            "FSIM-VERILOG-SEM-005",
            "compound assignments and standalone increment/decrement "
            "require SystemVerilog");
      }

      std::optional<Delay> delay;
      if (!prefix_update && !update_operation
          && match(TokenKind::Hash)) {
        delay = parse_verilog_delay(previous());
      }
      Expression value;
      if (unit_update) {
        value = Expression{
            ExpressionKind::Binary,
            *update_operation,
            {
                target,
                Expression{
                    ExpressionKind::IntegerLiteral,
                    "1",
                    {},
                    previous().span}},
            cover(target.span, previous().span)};
      } else {
        value = parse_expression();
        if (update_operation) {
          const auto value_span = value.span;
          value = Expression{
              ExpressionKind::Binary,
              *update_operation,
              {target, std::move(value)},
              cover(target.span, value_span)};
        }
      }
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
        if (at(TokenKind::PlusColon)
            || at(TokenKind::MinusColon)) {
          const auto direction = advance();
          Expression width = parse_expression();
          expect(TokenKind::RightBracket,
                 "']' after indexed part-select",
                 "FSIM-SV-PARSE-025");
          expression =
              Expression{ExpressionKind::Slice, direction.text,
                         {std::move(expression), std::move(first),
                          std::move(width)},
                         cover(expression.span, previous().span)};
        } else if (match(TokenKind::Colon)) {
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
    const Expression* root = &expression;
    while ((root->kind == ExpressionKind::Index
            || root->kind == ExpressionKind::Slice)
           && !root->operands.empty()) {
      root = &root->operands.front();
    }
    if (root->kind == ExpressionKind::Identifier
        && root->text == name.text) {
      note_implicit_net_reference(name);
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
    if (at(TokenKind::Caret) || at(TokenKind::TildeCaret)
        || at(TokenKind::CaretTilde)) {
      return BinaryOperation{4, current().text};
    }
    if (at(TokenKind::Ampersand)) {
      return BinaryOperation{5, "&"};
    }
    if (at(TokenKind::EqualEqual) || at(TokenKind::CaseEqual)
        || at(TokenKind::WildcardEqual)
        || at(TokenKind::CaseNotEqual)
        || at(TokenKind::WildcardNotEqual)
        || (at(TokenKind::NotEqual) && current().text == "!=")) {
      return BinaryOperation{6, current().text};
    }
    if (at(TokenKind::Less) || at(TokenKind::LessEqual) ||
        at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
      return BinaryOperation{7, current().text};
    }
    if (at(TokenKind::ShiftLeft)
        || at(TokenKind::ShiftRight)
        || at(TokenKind::ArithmeticShiftLeft)
        || at(TokenKind::ArithmeticShiftRight)) {
      return BinaryOperation{8, current().text};
    }
    if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
      return BinaryOperation{9, current().text};
    }
    if (at(TokenKind::Star) || at(TokenKind::Slash) ||
        at(TokenKind::Percent)) {
      return BinaryOperation{10, current().text};
    }
    if (at(TokenKind::Power)) {
      return BinaryOperation{11, "**"};
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
      if (language_ != Language::SystemVerilog2017
          && (at(TokenKind::WildcardEqual)
              || at(TokenKind::WildcardNotEqual))) {
        error(
            current(),
            "FSIM-VERILOG-SEM-004",
            "wildcard equality operators require SystemVerilog");
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
        at(TokenKind::Caret) || at(TokenKind::TildeAmpersand) ||
        at(TokenKind::TildePipe) || at(TokenKind::TildeCaret) ||
        at(TokenKind::CaretTilde)) {
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
      std::string canonical = name.text;
      while (match(TokenKind::Scope)) {
        canonical += "::";
        canonical +=
            expect_identifier("package-scoped name").text;
      }
      Expression expression{
          ExpressionKind::Identifier,
          canonical,
          {},
          cover(name.span, previous().span)};
      if (match(TokenKind::LeftParen)) {
        std::vector<Expression> arguments;
        if (!at(TokenKind::RightParen)) {
          do {
            arguments.push_back(parse_expression());
          } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen, "')' after arguments",
               "FSIM-SV-PARSE-028");
        expression = Expression{ExpressionKind::Call, canonical,
                                std::move(arguments),
                                cover(name.span, previous().span)};
        return parse_postfix(std::move(expression));
      }
      expression = parse_postfix(std::move(expression));
      const Expression* root = &expression;
      while ((root->kind == ExpressionKind::Index
              || root->kind == ExpressionKind::Slice)
             && !root->operands.empty()) {
        root = &root->operands.front();
      }
      if (root->kind == ExpressionKind::Identifier
          && root->text == name.text) {
        note_implicit_net_reference(name);
      }
      return expression;
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
        Expression first = parse_expression();
        if (match(TokenKind::LeftBrace)) {
          elements.push_back(std::move(first));
          if (at(TokenKind::RightBrace)) {
            error(
                current(),
                "FSIM-SV-PARSE-090",
                "replication concatenations require at least one operand");
          } else {
            do {
              elements.push_back(parse_expression());
            } while (match(TokenKind::Comma));
          }
          expect(TokenKind::RightBrace,
                 "'}' after replication operands",
                 "FSIM-SV-PARSE-030");
          expect(TokenKind::RightBrace,
                 "'}' after replication concatenation",
                 "FSIM-SV-PARSE-030");
          return Expression{
              ExpressionKind::Replication,
              "replicate",
              std::move(elements),
              span_from(open, previous())};
        }
        elements.push_back(std::move(first));
        while (match(TokenKind::Comma)) {
          elements.push_back(parse_expression());
        }
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
        if (at(TokenKind::PlusColon)
            || at(TokenKind::MinusColon)) {
          const auto direction = advance();
          Expression width = parse_expression();
          expect(TokenKind::RightBracket,
                 "']' after indexed part-select",
                 "FSIM-SV-PARSE-032");
          expression =
              Expression{ExpressionKind::Slice, direction.text,
                         {std::move(expression), std::move(first),
                          std::move(width)},
                         cover(expression.span, previous().span)};
        } else if (match(TokenKind::Colon)) {
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
  KeywordSet keyword_set_;
  std::vector<KeywordSet> keyword_stack_;
  std::unordered_set<std::string> non_ansi_ports_;
  std::unordered_set<std::string> body_port_declarations_;
  std::unordered_set<std::string> port_type_refinements_;
  std::unordered_set<std::string> current_procedural_names_;
  std::unordered_map<std::string, std::size_t>
      current_generate_names_;
  std::unordered_map<std::string, std::size_t>
      current_loop_names_;
  std::size_t current_loop_depth_{};
  std::unordered_set<std::string> declared_genvars_;
  std::vector<Token> external_genvar_uses_;
  std::vector<ImplicitNetReference> implicit_net_references_;
  std::vector<SystemVerilogImport>
      compilation_unit_imports_;
  std::vector<SystemVerilogImport> active_package_imports_;
  std::unordered_map<
      std::string,
      std::unordered_set<std::string>>
      package_constant_names_;
  std::string current_default_nettype_{"wire"};
  bool current_cell_define_{};
  VerilogUnconnectedDrive current_unconnected_drive_{
      VerilogUnconnectedDrive::None};
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

ParseResult parse_verilog(LexResult lexed, bool system_verilog) {
  return VerilogParser(std::move(lexed), system_verilog).run();
}

}  // namespace fsim::frontend
