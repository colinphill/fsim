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
    PortDirection direction { PortDirection::Unknown };
};

[[nodiscard]] std::optional<std::string> constant_output_number(
    std::string_view spelling);
[[nodiscard]] std::optional<std::size_t> output_unsigned_integer_bit_width(
    std::string_view spelling, unsigned base);
[[nodiscard]] std::optional<std::int64_t> simple_verilog_integer_constant(
    const Expression& expression);

enum class KeywordSet {
  Verilog1995,
  Verilog2001,
  Verilog2001NoConfig,
  Verilog2005,
  SystemVerilog2005,
  SystemVerilog2009,
  SystemVerilog2012,
  SystemVerilog2017,
  SystemVerilog2023,
};

[[nodiscard]] Language language_for_standard_revision(
    StandardRevision standard);
[[nodiscard]] KeywordSet keyword_set_for_standard_revision(
    StandardRevision standard);
[[nodiscard]] unsigned keyword_set_rank(KeywordSet set);
[[nodiscard]] std::optional<StandardRevision> declaration_word_standard(
    std::string_view word);
[[nodiscard]] bool contains_word(
    std::initializer_list<std::string_view> words,
    std::string_view word);
[[nodiscard]] bool is_verilog_1995_keyword(std::string_view word);
[[nodiscard]] bool is_verilog_2001_keyword(
    std::string_view word, bool include_config);
[[nodiscard]] bool is_system_verilog_2005_keyword(std::string_view word);
[[nodiscard]] bool is_system_verilog_2009_keyword(std::string_view word);
[[nodiscard]] bool is_system_verilog_2012_keyword(std::string_view word);
[[nodiscard]] bool keyword_reserved(
    KeywordSet set, std::string_view word);
[[nodiscard]] std::optional<KeywordSet> parse_keyword_set(
    std::string_view spelling);

class VerilogParser final : private detail::ParserBase {
 public:
  VerilogParser(LexResult lexed, bool system_verilog);
  VerilogParser(LexResult lexed, StandardRevision standard_revision);
  VerilogParser(LexResult lexed, StandardRevision standard_revision,
      std::string compatibility_profile);

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

  [[nodiscard]] bool compatibility_enabled(
      std::string_view name) const noexcept;

  bool require_standard(
      std::string_view feature,
      StandardRevision required,
      const Token& token,
      std::string_view diagnostic_code = "FSIM-SV-PARSE-346");

  Token expect_keyword(
      const std::string_view word,
      const bool case_insensitive,
      std::string code = "FSIM-FE-PARSE-001");

  static SourceSpan span_from(const Token& first, const Token& last);

  [[nodiscard]] bool verilog_attribute_instance_start() const;

  void parse_verilog_attribute_instances();

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
      UnitKind kind = UnitKind::VerilogModule,
      bool extern_declaration = false);

  void parse_anonymous_program(
      ParsedDesign& design, const Token& start);

  DesignUnit parse_systemverilog_configuration(const Token& start);

  SystemVerilogBindDirective parse_systemverilog_bind(
      const Token& start);

  DesignUnit make_systemverilog_bind_unit(
      SystemVerilogBindDirective directive);

  std::string parse_systemverilog_hierarchical_name(
      std::string_view description,
      bool allow_indices);

  void parse_modport(DesignUnit& unit, const Token& start);

  void parse_clocking_block(DesignUnit& unit, const Token& start);

  void parse_default_clocking(
      DesignUnit& unit,
      const Token& start);

  SystemVerilogClockingSkew parse_clocking_skew();

  SystemVerilogCovergroupDeclaration parse_covergroup_declaration(
      const Token& start,
      SystemVerilogCovergroupOwnerKind owner_kind);

  void structure_covergroup_header(
      SystemVerilogCovergroupDeclaration& declaration);

  void structure_covergroup_formals(
      std::span<const Token> tokens,
      std::vector<SystemVerilogCovergroupFormal>& formals,
      std::string_view description);

  void structure_covergroup_options(
      SystemVerilogCovergroupDeclaration& declaration);

  void structure_covergroup_declarations(
      SystemVerilogCovergroupDeclaration& declaration);

  void structure_coverpoint_bins(
      SystemVerilogCoverageDeclaration& declaration);
  void structure_coverage_options(
      SystemVerilogCoverageDeclaration& declaration);
  void structure_cross_bins(
      SystemVerilogCoverageDeclaration& declaration);

  void add_covergroup_declaration(
      std::vector<SystemVerilogCovergroupDeclaration>& declarations,
      SystemVerilogCovergroupDeclaration declaration,
      const Token& start);

  SystemVerilogAssertionDeclaration parse_assertion_declaration(
      const Token& start,
      SystemVerilogAssertionDeclarationKind kind);

  SystemVerilogConcurrentAssertion parse_concurrent_assertion(
      const Token& start,
      SystemVerilogConcurrentAssertionKind kind,
      std::optional<Token> label = std::nullopt);

  Statement parse_immediate_assertion(const Token& start);

  void structure_assertion_formals(
      SystemVerilogAssertionDeclaration& declaration);

  std::size_t structure_assertion_locals(
      SystemVerilogAssertionDeclaration& declaration);

  void structure_assertion_clock_and_disable(
      SystemVerilogAssertionDeclaration& declaration,
      std::size_t position);

  void resolve_assertion_references(DesignUnit& unit);

  void resolve_assertion_declaration_references(DesignUnit& unit);

  std::vector<SystemVerilogCheckerInstance> parse_checker_instances();

  void instantiate_checkers(DesignUnit& unit);

  void structure_sequence_expression(
      SystemVerilogAssertionDeclaration& declaration);

  void structure_property_expression(
      SystemVerilogAssertionDeclaration& declaration);

  void structure_assertion_endpoints(
      SystemVerilogAssertionDeclaration& declaration);

  void parse_import_clause(
      std::vector<SystemVerilogImport>& imports,
      const Token& start);

  void parse_export_clause(
      std::vector<SystemVerilogExport>& exports,
      const Token& start);

  [[nodiscard]] bool dpi_declaration_start(
      std::string_view direction) const;

  void parse_dpi_declaration(
      std::vector<SystemVerilogDpiDeclaration>& declarations,
      const Token& start,
      SystemVerilogDpiDirection direction,
      SystemVerilogDpiOwnerKind owner_kind,
      std::string owner_identity);

  void structure_dpi_declaration(
      SystemVerilogDpiDeclaration& declaration);

  void structure_dpi_profile(
      SystemVerilogDpiDeclaration& declaration,
      std::size_t callable_position,
      std::size_t name_position);

  [[nodiscard]] bool
  compilation_unit_class_method_definition_start() const;

  [[nodiscard]] bool compilation_unit_dpi_export_definition_start(
      const std::vector<SystemVerilogDpiDeclaration>& declarations,
      SystemVerilogDpiCallableKind kind) const;

  void resolve_dpi_declarations(
      std::vector<SystemVerilogDpiDeclaration>& declarations,
      const std::vector<FunctionDeclaration>& functions,
      const std::vector<TaskDeclaration>& tasks);

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

  std::vector<Expression> parse_constraint_block_expressions(
      std::string_view close_message,
      std::string close_code);

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

  void parse_generated_genvar_declaration(
      GenerateBody& body,
      std::vector<std::string>& local_genvars);

  void parse_generate_region(
      DesignUnit& unit, const Token& generate_token);

  GenerateRegion parse_static_generate_block();

  GenerateRegion parse_conditional_generate(
      const Token& start,
      std::optional<std::string> direct_scope = std::nullopt);

  GenerateRegion parse_iterative_generate(const Token& start);

  GenerateRegion parse_selection_generate(
      const Token& start,
      std::optional<std::string> direct_scope = std::nullopt);

  void parse_generate_branch(
      std::string& scope,
      GenerateBody& body,
      std::string_view implicit_scope,
      bool allow_direct_conditional_nesting);

  void normalize_systemverilog_generate_names(DesignUnit& unit);

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
      const Token& start,
      bool class_list = false);

  void parse_parameter_port_list(
      DesignUnit& unit,
      const Token& hash,
      bool class_list = false);

  std::vector<Instance> parse_instances();

  void parse_defparam_declaration(
      std::vector<VerilogDefparamDeclaration>& declarations,
      const Token& start);

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

  Type parse_virtual_interface_type(
      const Token& start);

  void parse_virtual_interface_declaration(
      DesignUnit& unit,
      const Token& start);

  Type parse_systemverilog_aggregate_type();
  Type parse_systemverilog_enum_type(
      std::vector<EnumLiteralDeclaration>* literals = nullptr);

  void parse_typedef(
      DesignUnit& unit,
      const Token& start);

  void parse_nettype(
      DesignUnit& unit,
      const Token& start);

  void parse_alias_statement(
      DesignUnit& unit,
      const Token& start);

  void parse_let_declaration(
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

  std::vector<Statement> parse_continuous_assignments(const Token& start);

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

  Statement parse_procedural_foreach_statement(const Token& start);

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
  StandardRevision standard_revision_;
  std::string compatibility_profile_ { "none" };
  KeywordSet keyword_set_;
  std::vector<KeywordSet> keyword_stack_;
  std::unordered_set<std::string> non_ansi_ports_;
  std::unordered_set<std::string> body_port_declarations_;
  std::unordered_set<std::string> port_type_refinements_;
  std::unordered_set<std::string> current_procedural_names_;
  std::unordered_map<std::string, Type> current_procedural_types_;
  std::unordered_set<std::string> current_function_arguments_;
  std::string current_function_name_;
  bool in_function_{};
  bool current_function_returns_void_{};
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
  std::vector<SystemVerilogAssertionDeclaration>
      compilation_unit_checkers_;
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
  std::size_t next_bind_unit_ { };
  bool module_has_non_time_item_{};
  bool program_generate_context_{};
  bool in_verilog_attribute_ { };
  std::vector<SystemVerilogFsmPragma> pending_systemverilog_fsm_pragmas_;
  std::optional<std::size_t>
      pending_systemverilog_fsm_pragma_target_position_;
};

}  // namespace fsim::frontend
