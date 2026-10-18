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
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::frontend {

/// The binary operator a compound assignment applies (`+=` applies "+"),
/// or empty for any other token (IEEE 1800-2017 11.4.1).
[[nodiscard]] constexpr std::string_view compound_assignment_operator(
    const TokenKind kind) noexcept
{
    switch (kind) {
    case TokenKind::PlusAssign: return "+";
    case TokenKind::MinusAssign: return "-";
    case TokenKind::StarAssign: return "*";
    case TokenKind::SlashAssign: return "/";
    case TokenKind::PercentAssign: return "%";
    case TokenKind::AmpersandAssign: return "&";
    case TokenKind::PipeAssign: return "|";
    case TokenKind::CaretAssign: return "^";
    case TokenKind::ShiftLeftAssign: return "<<";
    case TokenKind::ShiftRightAssign: return ">>";
    case TokenKind::ArithmeticShiftLeftAssign: return "<<<";
    case TokenKind::ArithmeticShiftRightAssign: return ">>>";
    default: return { };
    }
}

/// Whether a token is `=` or a compound assignment operator.
[[nodiscard]] constexpr bool is_blocking_assignment_operator(
    const TokenKind kind) noexcept
{
    return kind == TokenKind::Assign
        || !compound_assignment_operator(kind).empty();
}

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
// The constant as `$display` prints it, padded to its automatic decimal
// field width.
[[nodiscard]] std::optional<std::string> constant_display_number(
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

enum class SystemVerilogAnnexConstruct {
  sampled_clock_argument,
  ended_sequence_method,
  checker_always,
  operator_overloading,
  defparam,
  procedural_assign_deassign,
};

[[nodiscard]] Language language_for_standard_revision(
    StandardRevision standard);
[[nodiscard]] KeywordSet keyword_set_for_standard_revision(
    StandardRevision standard);
[[nodiscard]] unsigned keyword_set_rank(KeywordSet set);
[[nodiscard]] std::optional<StandardRevision> declaration_word_standard(
    std::string_view word);
[[nodiscard]] std::optional<StandardRevision> structural_word_standard(
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

struct GeneralPropertyActions {
  std::vector<Statement> success;
  std::vector<Statement> failure;
  std::vector<Statement> vacuous;
  // Distinguishes the evaluation's state variables from other directives'.
  std::string name_prefix { "__fsim_a" };
  // `disable iff` aborts every running attempt and runs `abort`.
  std::optional<Expression> disable;
  std::vector<Statement> abort;
  struct LocalState {
      std::string name;
      std::string type_spelling;
      std::optional<Expression> initializer;
  };
  // Local values are stored per live attempt slot by the general evaluator.
  std::vector<LocalState> locals;
  support::RareVector<Sensitivity> clock;
  SourceSpan span;
};

class VerilogParser final : private detail::ParserBase {
 public:
  VerilogParser(LexResult lexed, bool system_verilog);
  VerilogParser(LexResult lexed, StandardRevision standard_revision);
  VerilogParser(LexResult lexed, StandardRevision standard_revision,
      std::string compatibility_profile,
      SystemVerilogPackageMemberLookup package_member_lookup = {});

  ParseResult run();

 private:
  // A statement label (`name: begin`) waiting for its block (IEEE
  // 1800-2017 9.3.5).
  std::string pending_block_label_;

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

  void diagnose_systemverilog_2023_annex(
      SystemVerilogAnnexConstruct construct,
      const Token& token);

  bool require_standard(
      std::string_view feature,
      StandardRevision required,
      const Token& token,
      std::string_view diagnostic_code = "FSIM-SV-PARSE-346");
  // A later standard's system service used in an earlier revision is an
  // implementation-defined extension there (IEEE 1364-2005 17), so it only
  // warns.
  void note_system_service_standard(
      const std::string& name, StandardRevision required, const Token& token);

  Token expect_keyword(
      const std::string_view word,
      const bool case_insensitive,
      std::string code = "FSIM-FE-PARSE-001");

  static SourceSpan span_from(const Token& first, const Token& last);
  [[nodiscard]] std::uint64_t systemverilog_source_order() const noexcept;

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

  void parse_directive(bool design_element = false);
  void check_class_randomization_rules(
      const SystemVerilogClassDeclaration& declaration);
  void link_out_of_block_constraints(ParsedDesign& design);
  std::vector<std::pair<std::string, SystemVerilogClassConstraint>>
      out_of_block_constraints_;

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
    // Only a port or terminal connection or a continuous-assignment target
    // implicitly declares a net (IEEE 1364-2005 4.5, 1800-2017 6.10).
    bool declares { true };
  };
  struct WildcardImportUse {
    std::string name;
    SourceSpan use_span;
  };
  bool implicit_net_context_ { };
  // The `iff` guard of the single event of the last event control parsed
  // (IEEE 1800-2017 9.4.2.3).
  std::optional<Expression> event_guard_;
  // Compilation-unit forward type declarations awaiting their definition.
  std::vector<Token> compilation_unit_forward_types_;
  // Reports undeclared names a compilation-unit subprogram noted since
  // `first` (it declares no implicit nets) and drops the references.
  void resolve_compilation_unit_references(std::size_t first, const std::string& callable);
  // Value parameters declared so far in the current unit: `[N]` with a
  // parameter N is a constant array size, not an associative index type.
  std::unordered_set<std::string> unit_value_parameter_names_;
  // Discards the implicit-net references noted while it lives: names in a
  // class body or an out-of-block method (`C::m`) resolve to class members
  // (IEEE 1800-2017 6.10, 8.24).
  struct ImplicitNetDiscard {
    std::vector<ImplicitNetReference>& references;
    std::size_t count;
    bool active;
    ~ImplicitNetDiscard() {
      if (active && references.size() > count) {
        references.resize(count);
      }
    }
  };
  // Before SystemVerilog a primitive's output or bidirectional terminal is
  // a net (IEEE 1364-2005 7.1); report one that names a variable.
  void check_verilog_primitive_net_terminal(const Expression& terminal,
      const std::vector<SignalDeclaration>& signals, const Token& instance_start,
      bool bidirectional = false, bool continuous_assignment = false);
  // Names of every class declared so far; a class name is never a net.
  std::unordered_set<std::string> known_class_names_;

  void note_implicit_net_reference(const Token& name, bool call = false);
  void note_wildcard_import_type_reference(const Token& name);

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

  void parse_clocking_block(
      DesignUnit& unit, const Token& start, bool global = false);

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

  // A concurrent property evaluated by a general sequence automaton: the
  // returned statement runs one attempt from the current clock tick and
  // executes the success, failure, or vacuous statements.
  std::optional<Statement> general_property_evaluation(
      std::span<const Token> tokens, const DesignUnit& unit,
      const GeneralPropertyActions& actions);
  // Validate attempt-local initialization and assigned-before-read flow
  // without selecting the general automaton for a specialized property.
  [[nodiscard]] bool general_property_local_flow_valid(
      std::span<const Token> tokens, const DesignUnit& unit,
      const GeneralPropertyActions& actions);
  // A boolean assertion operand parsed as an ordinary expression; empty
  // when the tokens use sequence or property operators.
  std::optional<Expression> assertion_expression(std::span<const Token> tokens);
  // A clocked inline property `assert property (@(e) ...)` becomes a
  // synthesized property declaration that the directive names.
  bool parse_generate_concurrent_assertion(GenerateBody& body);
  void resolve_generate_assertion_references(DesignUnit& unit);
  void declare_inline_property(
      SystemVerilogConcurrentAssertion& assertion,
      std::vector<SystemVerilogAssertionDeclaration>& declarations);
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

  // Instantiates the unit's checkers and resolves its assertions.
  void resolve_unit_checkers(DesignUnit& unit);
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

  struct AnsiCallableFormal {
    Token name;
    Type type;
    PortDirection direction{PortDirection::Input};
    bool reference{};
    bool const_reference{};
    bool static_reference{};
    std::optional<Expression> default_value;
  };

  std::vector<AnsiCallableFormal> parse_ansi_callable_formals(
      std::string_view kind,
      const std::function<void(const Token&)>& declare);

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

  Expression signed_parameter_value(

      Expression value, bool signing_only, bool is_signed);
  // `type(data_type)` or `type(expression)` as a data type (6.23).
  [[nodiscard]] bool type_operator_data_type_start() const;
  std::optional<Type> parse_type_operator_type();
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
  void validate_non_ansi_port_selections(const DesignUnit& unit);
  void finalize_non_ansi_port_expressions(DesignUnit& unit);

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

  // A struct or union type; `complete` is cleared when its member list is
  // missing.
  Type parse_systemverilog_aggregate_type(bool* complete = nullptr);
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

  [[nodiscard]] bool is_block_declaration_start() const;

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
  Statement parse_procedural_for_statement_body(
      const Token& start,
      std::vector<VariableDeclaration>& extra_declarations,
      std::vector<Statement>& extra_initializations);

  Statement parse_procedural_foreach_statement(const Token& start);

  Statement parse_repeat_statement(const Token& start);

  Statement parse_while_statement(const Token& start);

  Statement parse_do_while_statement(const Token& start);

  Statement parse_forever_statement(const Token& start);
  Statement parse_fork_statement(const Token& start);

  void parse_report_message(Statement& statement);
  void parse_fatal_arguments(Statement& statement);

  void parse_nonfatal_report_arguments(
      Statement& statement,
      const Token& task);

  std::optional<Statement> parse_statement();
  [[gnu::noinline]] std::optional<Statement> parse_if_statement(
      const Token& start);
  std::optional<Statement> parse_statement_unhoisted();
  std::optional<Type> import_interface_type(DesignUnit& unit);
  // The design being parsed; earlier units are visible to later ones.
  ParsedDesign* parsed_design_{};
  [[gnu::noinline]] void prepend_hoisted_statements(
      std::optional<Statement>& statement, std::size_t mark);
  std::optional<Statement> parse_randcase(const Token& start);
  std::optional<Statement> parse_randsequence(const Token& start);
  [[nodiscard]] std::vector<Token> synthetic_tokens(
      std::string_view text, const SourceSpan& span) const;
  std::optional<Statement> parse_generated_statement(
      std::vector<Token> tokens, const SourceSpan& span);
  std::vector<Token> balanced_tokens(TokenKind open, TokenKind close);
  // Rewritten randcase/randsequence blocks get distinct generated names.
  std::size_t generated_statement_counter_ {};
  // Assignments nested in expressions (IEEE 1800-2017 11.3.6) run as
  // statements ahead of the statement that contains them.
  std::size_t statement_hoist_depth_ {};
  std::vector<Statement> hoisted_statements_;

  struct DecimalRatio {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};
  };

  static std::optional<DecimalRatio> decimal_ratio(
      const std::string_view spelling);

  DelayAlternative parse_verilog_delay_alternative(bool parenthesized = true);

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
  Expression parse_primary_impl();

  Expression parse_postfix(Expression expression);
  // `subject dist { ... }` after its subject (IEEE 1800-2017 18.5.4).
  Expression parse_dist_list(Expression subject);

  Language language_;
  StandardRevision standard_revision_;
  // Synthetic randsequence token vectors do not share the preprocessed input
  // ordinal space. Their references intentionally carry ordinal zero.
  bool synthetic_token_stream_ { };
  std::string compatibility_profile_ { "none" };
  KeywordSet keyword_set_;
  std::vector<KeywordSet> keyword_stack_;
  std::unordered_set<std::string> non_ansi_ports_;
  struct NonAnsiPortSelection {
    std::string port_name;
    Expression left;
    Expression right;
    SourceSpan span;
    // The whole `name[...]` port expression, for a selection that becomes
    // a port expression.
    Expression expression;
  };
  std::vector<NonAnsiPortSelection> non_ansi_port_selections_;
  // A non-ANSI port whose port expression is not a whole port name: an
  // explicit `.name(expression)`, a concatenation, a selection, or a name
  // listed more than once (IEEE 1800-2017 23.2.2.1). An unnamed port has a
  // generated external name.
  struct NonAnsiPortExpression {
    std::string external;
    std::optional<Expression> expression;
    SourceSpan span;
  };
  std::vector<NonAnsiPortExpression> non_ansi_port_expressions_;
  // The design unit's default lifetime for its tasks and functions
  // (`module automatic m;`, IEEE 1800-2017 6.21).
  bool unit_default_automatic_ = false;
  // Typedef data types by name, latest first, so `T [N:M]` can expand a
  // packed array of a named integral vector type (IEEE 1800-2017 7.4.1).
  std::unordered_map<std::string, Type> packed_typedef_types_;
  [[nodiscard]] std::optional<Type> parse_named_packed_array_type();
  // Declares, in `unit`, the literals of anonymous enumerations parsed as
  // aggregate members since the last call (IEEE 1800-2017 6.19, 7.2).
  void declare_member_enumerations(DesignUnit& unit);
  std::vector<std::pair<Type, std::vector<EnumLiteralDeclaration>>>
      member_enumerations_;
  // Parses the packed dimensions after `element` and flattens them, with
  // the element's own range innermost.
  [[nodiscard]] Type packed_array_of_type(
      const Type& element, const SourceSpan& span);
  // Gives a packed structure with parameter-dependent member ranges a
  // range expression spanning its members; false when it cannot.
  [[nodiscard]] bool aggregate_width_expression(
      Type& type, const SourceSpan& span) const;
  std::unordered_set<std::string> body_port_declarations_;
  // Modport callables named before their declaration, resolved at the end
  // of the interface (IEEE 1800-2017 25.7).
  struct PendingModportCallable {
    std::size_t modport { };
    std::size_t member { };
    Token token;
    bool import { };
    std::optional<bool> explicit_function;
  };
  std::vector<PendingModportCallable> pending_modport_callables_;
  std::unordered_set<std::string> port_type_refinements_;
  // Non-ANSI ports whose direction declaration names a net or data type;
  // such a port cannot be declared again (IEEE 1800-2017 23.2.2.1).
  std::unordered_set<std::string> explicit_port_types_;
  void check_port_redeclaration(const Type& port_type, bool port_explicit,
      const Type& data_type, const Token& name, std::string_view what);
  std::unordered_set<std::string> current_procedural_names_;
  std::unordered_map<std::string, Type> current_procedural_types_;
  // Typedefs declared in the enclosing procedural blocks (IEEE 1800-2017
  // 6.18): a use of the name denotes the aliased type.
  std::unordered_map<std::string, Type> block_typedef_types_;
  // Nonzero while parsing a class declaration.
  std::size_t class_parse_depth_ { };
  // Design-unit-level dynamic arrays, queues and associative arrays, whose
  // built-in methods may omit empty parentheses (IEEE 1800-2017 13.4.5).
  std::unordered_set<std::string> unit_container_names_;
  // Module-level string variables, for `foreach (s[i])` (IEEE 1800-2023
  // 12.7.3).
  std::unordered_set<std::string> unit_string_names_;
  // Index types of the unit's associative arrays, for foreach.
  std::unordered_map<std::string, Type> unit_associative_index_types_;
  // Types of unit-level variables used to validate foreach array rank.
  std::unordered_map<std::string, Type> unit_foreach_types_;
  // Nonzero while parsing a constraint block or a delay value, where `->`
  // is not the logical implication operator.
  std::size_t constraint_parse_depth_ { };
  std::unordered_set<std::string> current_function_arguments_;
  std::string current_function_name_;
  bool in_function_{};
  // Whether the callable being parsed has automatic lifetime.
  bool automatic_callable_{};
  struct LifetimeScope {
    bool& flag;
    bool saved;
    LifetimeScope(bool& target, const bool automatic)
        : flag(target), saved(target) {
      flag = automatic;
    }
    LifetimeScope(const LifetimeScope&) = delete;
    LifetimeScope& operator=(const LifetimeScope&) = delete;
    ~LifetimeScope() { flag = saved; }
  };
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
  std::vector<WildcardImportUse> wildcard_import_uses_;
  std::size_t unit_package_import_count_ { };
  std::unordered_set<std::string> container_iterator_names_;
  std::vector<SystemVerilogImport>
      compilation_unit_imports_;
  // Compilation-unit ($unit) parameters, typedefs and data objects
  // (IEEE 1800-2017 3.12.1) collect in a synthetic package that later units
  // of the compilation unit import.
  std::optional<DesignUnit> compilation_unit_package_;
  DesignUnit& compilation_unit_package(const Token& start);
  void parse_compilation_unit_declaration();
  std::unordered_map<std::string, std::string>
      dpi_import_linkage_profiles_;
  std::vector<SystemVerilogAssertionDeclaration>
      compilation_unit_checkers_;
  std::vector<SystemVerilogImport> active_package_imports_;
  std::unordered_map<
      std::string,
      std::unordered_set<std::string>>
      package_constant_names_;
  SystemVerilogPackageMemberLookup package_member_lookup_;
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
  // Checkers declared anywhere in the current module body.
  std::unordered_set<std::string> unit_checker_names_;
  // Units whose checker instances wait for a later checker declaration.
  std::vector<std::string> deferred_checker_units_;
  bool module_has_non_time_item_{};
  bool program_generate_context_{};
  // The last parameter type was a signing without a range (6.20.2).
  bool parameter_signing_only_{};
  bool in_verilog_attribute_ { };
  std::vector<SystemVerilogFsmPragma> pending_systemverilog_fsm_pragmas_;
  std::optional<std::size_t>
      pending_systemverilog_fsm_pragma_target_position_;
};

}  // namespace fsim::frontend
