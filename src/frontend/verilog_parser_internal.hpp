// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/output_format.hpp"

#include "parser_support.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {

// Static instance arrays are materialized as owning frontend records.  This
// host-resource guard is intentionally derived from their actual in-memory
// representation; it is not a Verilog language-size restriction.
inline constexpr std::size_t maximum_instance_array_storage_bytes =
    256U * 1024U * 1024U;
inline constexpr std::size_t maximum_instance_array_elements =
    maximum_instance_array_storage_bytes / sizeof(Instance);


using detail::decimal_i64;
using detail::decimal_u64;

struct VerilogTypeSpec {
  Type type;
  PortDirection direction{PortDirection::Unknown};
};

[[nodiscard]] inline std::optional<std::string>
constant_output_number(const std::string_view spelling) {
  const auto quote = spelling.find('\'');
  if (quote == std::string_view::npos) {
    const auto value = decimal_u64(spelling);
    return value
        ? std::optional{std::to_string(*value)}
        : std::nullopt;
  }
  const auto width = decimal_u64(spelling.substr(0, quote));
  if (!width || *width == 0) {
    return std::nullopt;
  }
  auto digits = spelling.substr(quote + 1);
  bool is_signed = false;
  if (!digits.empty()
      && (digits.front() == 's' || digits.front() == 'S')) {
    is_signed = true;
    digits.remove_prefix(1);
  }
  if (digits.size() < 2) {
    return std::nullopt;
  }
  const char radix = detail::ascii_lower(digits.substr(0, 1)).front();
  digits.remove_prefix(1);
  int base{};
  switch (radix) {
  case 'b':
    base = 2;
    break;
  case 'o':
    base = 8;
    break;
  case 'd':
    base = 10;
    break;
  case 'h':
    base = 16;
    break;
  default:
    return std::nullopt;
  }
  std::string cleaned;
  cleaned.reserve(digits.size());
  for (const char digit : digits) {
    if (digit != '_') {
      cleaned.push_back(digit);
    }
  }
  std::uint64_t value{};
  const auto [end, error] = std::from_chars(
      cleaned.data(),
      cleaned.data() + cleaned.size(),
      value,
      base);
  if (error != std::errc{}
      || end != cleaned.data() + cleaned.size()) {
    return std::nullopt;
  }
  if (*width < 64) {
    value &= (std::uint64_t{1} << *width) - 1U;
  }
  if (!is_signed
      || *width > 64
      || (value & (std::uint64_t{1} << (*width - 1U))) == 0) {
    return std::to_string(value);
  }
  const auto magnitude =
      *width == 64
          ? (~value) + 1U
          : (std::uint64_t{1} << *width) - value;
  if (magnitude == (std::uint64_t{1} << 63U)) {
    return std::to_string(std::numeric_limits<std::int64_t>::min());
  }
  return "-" + std::to_string(magnitude);
}

[[nodiscard]] inline std::optional<std::int64_t> simple_integer_constant(
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

[[nodiscard]] inline bool contains_word(
    const std::initializer_list<std::string_view> words,
    const std::string_view word) {
  return std::find(words.begin(), words.end(), word) != words.end();
}

[[nodiscard]] inline bool is_verilog_1995_keyword(
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

[[nodiscard]] inline bool is_verilog_2001_keyword(
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

[[nodiscard]] inline bool is_system_verilog_2005_keyword(
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

[[nodiscard]] inline bool is_system_verilog_2009_keyword(
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

[[nodiscard]] inline bool is_system_verilog_2012_keyword(
    const std::string_view word) {
  return is_system_verilog_2009_keyword(word)
      || contains_word(
          {"implements", "interconnect", "nettype", "soft"},
          word);
}

[[nodiscard]] inline bool keyword_reserved(
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

[[nodiscard]] inline std::optional<KeywordSet> parse_keyword_set(
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
  VerilogParser(LexResult lexed, bool system_verilog);

  ParseResult run();

 private:
  [[nodiscard]] bool keyword(
      const std::string_view text,
      const std::size_t lookahead = 0,
      const bool case_insensitive = false) const;

  [[nodiscard]] bool any_keyword(
      const std::initializer_list<std::string_view> words,
      const bool case_insensitive = false) const;

  bool match_keyword(
      const std::string_view text,
      const bool case_insensitive = false);

  Token expect_keyword(
      const std::string_view word,
      const bool case_insensitive,
      std::string code = "FSIM-FE-PARSE-001");

  static SourceSpan span_from(const Token& first, const Token& last);

  static std::string string_literal_text(const Token& token);

  std::string decoded_string_literal_text(const Token& token);

  Token expect_identifier(std::string_view description);

  [[nodiscard]] bool on_directive_line(const Token& tick) const;

  void reject_directive_arguments(
      const Token& tick,
      const Token& directive);

  void reset_compiler_directives();

  void parse_directive();

  void parse_default_nettype(
      const Token& tick,
      const Token& directive);

  void parse_unconnected_drive(
      const Token& tick,
      const Token& directive);

  void parse_begin_keywords(
      const Token& tick,
      const Token& directive);

  static std::optional<std::uint64_t> time_unit_femtoseconds(
      const std::string_view unit);

  struct DeclaredTime {
    std::uint64_t magnitude{1};
    std::string unit;
    SourceSpan span;
    bool valid{};

    [[nodiscard]] std::string spelling() const {
      return std::to_string(magnitude) + unit;
    }
  };

  [[nodiscard]] bool time_declaration_start() const;

  DeclaredTime parse_declared_time_value(
      const std::string_view description);

  void validate_effective_time_declaration(
      const Token& declaration,
      const std::uint64_t unit_magnitude,
      const std::string_view unit,
      const std::string_view precision);

  static std::optional<std::pair<std::uint64_t, std::uint64_t>>
  parse_declared_time_spelling(const std::string_view spelling);

  void update_unit_time(DesignUnit& unit);

  void parse_time_declaration(
      DesignUnit* unit,
      const Token& declaration);

  void parse_timescale(const Token& directive);

  struct ImplicitNetReference {
    std::string name;
    std::string net_type;
    SourceSpan span;
    std::vector<std::string> expansion_stack;
  };

  void note_implicit_net_reference(const Token& name);

  void resolve_implicit_nets(DesignUnit& unit);

  DesignUnit parse_module(
      const Token& start,
      bool interface_unit = false);

  void parse_modport(DesignUnit& unit, const Token& start);

  void parse_import_clause(
      std::vector<SystemVerilogImport>& imports,
      const Token& start);

  void parse_export_clause(
      std::vector<SystemVerilogExport>& exports,
      const Token& start);

  DesignUnit parse_package(const Token& start);

  SystemVerilogClassDeclaration parse_class(
      const Token& start,
      std::string enclosing_scope,
      bool virtual_class = false,
      bool interface_class = false);

  SystemVerilogClassDeclaration parse_class_forward_declaration(
      const Token& start,
      std::string enclosing_scope);

  void add_class_declaration(
      std::vector<SystemVerilogClassDeclaration>& declarations,
      SystemVerilogClassDeclaration declaration,
      const Token& location);

  bool parse_class_property(
      SystemVerilogClassDeclaration& declaration,
      const Token& start);

  SystemVerilogClassMethod parse_class_method(
      const Token& start,
      SystemVerilogClassMethodKind kind,
      SystemVerilogClassVisibility visibility,
      bool is_static,
      bool is_virtual,
      bool is_pure,
      bool is_final,
      bool is_extern,
      std::string_view owner_identity);

  SystemVerilogClassMethod parse_class_out_of_block_method(
      const Token& start,
      SystemVerilogClassMethodKind kind);

  SystemVerilogClassConstraint parse_class_constraint(
      const Token& start,
      std::string_view owner_identity,
      SystemVerilogClassVisibility visibility,
      bool is_static,
      bool is_pure,
      bool is_extern);

  VerilogUdpDeclaration parse_udp_declaration(const Token& start);

  VerilogSpecifyBlock parse_specify_block(const Token& start);

  void parse_specparam_declaration(
      VerilogSpecifyBlock& block,
      const Token& start);

  VerilogModulePathDeclaration parse_specify_module_path(
      const Token& start,
      Expression condition = {},
      bool conditional = false,
      bool ifnone = false);

  VerilogSpecifyPulseDeclaration parse_specify_pulse_declaration(
      const Token& start,
      bool controls_style,
      VerilogPulseStyle style,
      bool show_cancelled);

  VerilogTimingCheckEvent parse_verilog_timing_check_event();

  VerilogTimingCheckDeclaration parse_verilog_timing_check(
      const Token& start,
      VerilogTimingCheckKind kind);

  VerilogUdpTableRow parse_udp_table_row(
      const VerilogUdpDeclaration& declaration);

  std::optional<VerilogUdpLevelSymbol> parse_udp_level_symbol();

  std::optional<VerilogUdpOutputSymbol> parse_udp_output_symbol(
      bool allow_no_change);

  FunctionDeclaration parse_function(
      const Token& start,
      bool prototype = false,
      bool default_automatic = false);

  void validate_function_body(
      const FunctionDeclaration& function,
      const Token& start);

  TaskDeclaration parse_task(
      const Token& start,
      bool prototype = false,
      bool default_automatic = false);

  void validate_task_body(
      const TaskDeclaration& task,
      const Token& start);

  void parse_genvar_declaration(DesignUnit& unit);

  void parse_generate_region(
      DesignUnit& unit, const Token& generate_token);

  GenerateRegion parse_static_generate_block();

  GenerateRegion parse_conditional_generate(
      const Token& start);

  GenerateRegion parse_iterative_generate(const Token& start);

  GenerateRegion parse_selection_generate(const Token& start);

  void parse_generate_branch(
      std::string& scope,
      GenerateBody& body);

  void parse_generate_declaration(
      GenerateBody& body,
      std::vector<std::string>& local_names);

  void parse_generate_typedef(
      GenerateBody& body,
      std::vector<std::string>& local_names,
      const Token& start);

  void parse_generated_parameter_group(
      GenerateBody& body,
      std::vector<std::string>& local_names,
      const bool local,
      const Token& start);

  Type parse_parameter_type();
  Type parse_type_parameter_actual();

  void add_parameter(
      DesignUnit& unit,
      ParameterDeclaration parameter,
      const Token& name);

  void parse_parameter_group(
      DesignUnit& unit,
      const bool local,
      const bool port_list,
      const Token& start);

  void parse_parameter_port_list(
      DesignUnit& unit,
      const Token& hash);

  std::vector<Instance> parse_instances();

  void normalize_udp_instances(ParsedDesign& design);

  void parse_parameter_overrides(
      Instance& instance,
      const Token& hash);

  void parse_module_ports(DesignUnit& unit);

  void skip_to_port_delimiter();

  [[nodiscard]] bool is_direction_keyword() const;

  PortDirection parse_direction();

  static Type default_verilog_type();

  Type default_port_net_type() const;

  void require_default_port_net_type(
      const Token& location,
      const bool explicit_type);

  [[nodiscard]] bool is_net_type_keyword() const;

  [[nodiscard]] bool is_named_type_reference_start(
      const std::size_t offset = 0) const;

  Type parse_named_type();

  void parse_typedef(
      DesignUnit& unit,
      const Token& start);

  void parse_optional_net_type(Type& type);

  void parse_optional_signedness(Type& type);

  void parse_optional_range(Type& type);

  bool parse_optional_container_dimension(Type& type);

  [[nodiscard]] bool is_declaration_start() const;

  [[nodiscard]] bool instance_start() const;

  void parse_event_declaration(
      DesignUnit& unit, const Token& start);

  void parse_declaration(DesignUnit& unit);

  void parse_procedural_declaration(Statement& block);

  static void update_or_add_port(DesignUnit& unit,
                                 SignalDeclaration declaration);

  static bool update_existing_port_type(
      DesignUnit& unit, const SignalDeclaration& declaration);

  [[nodiscard]] bool verilog_drive_strength_start() const;

  std::optional<VerilogDriveStrength>
  parse_verilog_drive_strength(std::string_view context);

  std::optional<VerilogChargeStrength>
  parse_verilog_charge_strength(std::string_view context);

  std::optional<Statement> parse_continuous_assignment(const Token& start);

  [[nodiscard]] bool is_gate_primitive() const;

  void parse_gate_primitive(
      std::vector<Statement>& statements,
      const std::vector<SignalDeclaration>& signals,
      const std::vector<SignalDeclaration>& ports);

  void parse_switch_primitive(
      std::vector<Statement>& statements,
      const std::vector<SignalDeclaration>& signals,
      const std::vector<SignalDeclaration>& ports);

  Process parse_always();

  Process parse_initial();

  Process parse_final();

  std::vector<Sensitivity> parse_sensitivity();

  [[nodiscard]] bool cycle_paths_are_safe(
      const std::vector<Statement>& statements) const;

  void skip_case_statement();

  Statement parse_case_statement(
      const Token& start,
      const CaseMatchKind match_kind,
      const CaseQualifier qualifier = CaseQualifier::None);

  void parse_procedural_loop_body(
      const Token& start, Statement& statement);

  Statement parse_procedural_for_statement(const Token& start);

  Statement parse_repeat_statement(const Token& start);

  Statement parse_while_statement(const Token& start);

  Statement parse_do_while_statement(const Token& start);

  Statement parse_forever_statement(const Token& start);
  Statement parse_fork_statement(const Token& start);

  void parse_fatal_arguments(Statement& statement);

  void parse_nonfatal_report_arguments(
      Statement& statement,
      const Token& task);

  std::optional<Statement> parse_statement();

  struct DecimalRatio {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};
  };

  static std::optional<DecimalRatio> decimal_ratio(
      const std::string_view spelling);

  DelayAlternative parse_verilog_delay_alternative();

  static void set_selected_delay(
      Delay& delay,
      const DelayAlternative& alternative);

  Delay parse_verilog_delay_value(const bool parenthesized);

  Delay parse_verilog_delay(
      const Token& start,
      const std::size_t maximum_values = 1);

  Expression parse_lvalue();

  struct BinaryOperation {
    int precedence;
    std::string name;
  };

  std::optional<BinaryOperation> binary_operation() const;

  Expression parse_expression(int minimum_precedence = 0);

  std::optional<SystemVerilogDecimalLiteral>
      parse_systemverilog_decimal_literal(
          const Token& number,
          const std::optional<Token>& unit);

  Expression parse_unary();

  Expression parse_primary();

  Expression parse_postfix(Expression expression);

  Language language_;
  KeywordSet keyword_set_;
  std::vector<KeywordSet> keyword_stack_;
  std::unordered_set<std::string> non_ansi_ports_;
  std::unordered_set<std::string> body_port_declarations_;
  std::unordered_set<std::string> port_type_refinements_;
  std::unordered_set<std::string> current_procedural_names_;
  std::unordered_set<std::string> current_function_arguments_;
  std::string current_function_name_;
  bool in_function_{};
  bool in_task_{};
  std::unordered_map<std::string, std::size_t>
      current_generate_names_;
  std::unordered_map<std::string, std::size_t>
      current_loop_names_;
  std::size_t current_loop_depth_{};
  std::unordered_set<std::string> declared_genvars_;
  std::vector<Token> external_genvar_uses_;
  std::size_t next_implicit_generate_scope_{1};
  std::vector<ImplicitNetReference> implicit_net_references_;
  std::unordered_set<std::string> container_iterator_names_;
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
  std::uint64_t compilation_time_unit_magnitude_{1};
  std::string compilation_time_unit_;
  std::string compilation_time_precision_;
  bool compilation_time_unit_declared_{};
  bool compilation_time_precision_declared_{};
  bool compilation_unit_has_design_item_{};
  std::uint64_t module_time_unit_magnitude_{1};
  std::string module_time_unit_;
  std::string module_time_precision_;
  bool module_time_unit_declared_{};
  bool module_time_precision_declared_{};
  bool module_has_non_time_item_{};
};

}  // namespace fsim::frontend
