// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "fsim/frontend/parser.hpp"

#include "parser_support.hpp"

#include <algorithm>
#include <array>
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

using detail::decimal_i64;
using detail::decimal_u64;
using detail::vhdl_name;

[[nodiscard]] std::optional<std::int64_t>
simple_vhdl_integer_constant(const Expression& expression);

class VhdlParser final : private detail::ParserBase {
public:
    explicit VhdlParser(
        LexResult lexed,
        VhdlStandard standard = VhdlStandard::Vhdl2008);

    ParseResult run();

private:
  static SourceSpan span_from(const Token &first, const Token &last);

  static std::string string_literal_text(const Token &token);

  std::optional<VhdlContextItem> parse_context_item();
  [[nodiscard]] std::optional<Statement> split_vhdl_aggregate_assignment(
      const Statement& statement,
      const std::vector<Expression>& targets) const;
  // Use clauses of process declarative parts, joined to the enclosing
  // unit's context clause when the unit ends.
  std::vector<VhdlContextItem> deferred_context_items_;
  // Implicit instances of generic subprograms named by the generic maps of
  // calls (IEEE 1076-2019 9.3.4), joined to the enclosing process or unit
  // when it ends. `vhdl_call_instance_anchor_` is where the declarative item
  // or statement part being parsed begins, so an instance precedes the use.
  std::vector<GenericSubprogramInstantiation> pending_call_function_instances_;
  std::vector<GenericSubprogramInstantiation> pending_call_procedure_instances_;
  std::optional<SourceSpan> vhdl_call_instance_anchor_;
  std::size_t vhdl_call_instance_count_ { };
  /// After the name of a call, `generic map (...)` makes an implicit instance
  /// of the template and returns the instance's name; otherwise the name.
  std::string parse_vhdl_call_generic_map(
      const Token& name, std::string canonical, bool procedure);
  void flush_pending_call_instances(DesignUnit& unit);
  // Type mark of each attribute declared so far, by attribute name;
  // STANDARD declares FOREIGN (IEEE 1076-2008 16.3).
  std::unordered_map<std::string, std::string> vhdl_declared_attributes_ {
      {"foreign", "string"}};
  // Attribute specification values by attribute and entity name, read in
  // place of `name'attribute`.
  std::unordered_map<std::string, std::unordered_map<std::string, Expression>>
      vhdl_attribute_values_;

  Token expect_identifier(std::string_view description);

  bool require_vhdl_standard(const Token& token, VhdlStandard minimum,
      std::string_view feature,
      std::string_view migration);

  DesignUnit parse_context_declaration(const Token &start);

  DesignUnit parse_vhdl_psl_verification_unit(const Token &start,
                                               bool comment_embedded);

  bool parse_vhdl_psl_declaration(DesignUnit &unit);

  [[nodiscard]] bool psl_property_follows() const;
  bool parse_vhdl_psl_directive(
      DesignUnit &unit, std::optional<Token> label = std::nullopt,
      bool marker_consumed = false);

  void skip_vhdl_psl_markers();

  void analyze_vhdl_psl(ParsedDesign &design);

  DesignUnit parse_package(const Token &start, bool body = false);

  void parse_package_constant(DesignUnit &unit, const Token &start, bool body);

  std::string parse_vhdl_selected_name(
      std::string_view description, std::string* source_spelling = nullptr);

  // The resolution indication before a subtype's type mark, if any:
  // `f T`, `(f) T` or `((f)) T` (IEEE 1076-2008 6.3).
  std::string parse_vhdl_resolution_indication(bool signal_kind_follows);

  void
  parse_vhdl_package_generic_map(std::vector<ParameterOverride> &associations,
                                 bool &box, const Token &start);

  ParameterDeclaration parse_vhdl_interface_package(const Token &start);

  // The lookahead just past the parenthesis matching the one at
  // `lookahead`, or nothing when the file ends first.
  [[nodiscard]] std::optional<std::size_t>
  skip_vhdl_parentheses(std::size_t lookahead) const;
  Expression parse_vhdl_aggregate_target(std::vector<Expression>& leaves);
  PackageInstantiation parse_vhdl_package_instantiation(const Token &start);
  // Parses a package instantiation in a declarative region from its
  // `package` token and appends it, rejecting a duplicate name.
  template <typename Instances>
  void parse_local_vhdl_package_instantiation(Instances &instances,
                                              const Token &start) {
    auto instance = parse_vhdl_package_instantiation(start);
    if (std::ranges::any_of(instances, [&](const auto &existing) {
          return existing.name == instance.name;
        })) {
      error(start, "FSIM-VHDL-SEM-059",
            "duplicate local package instance '" + instance.name + "'");
    } else {
      instances.push_back(std::move(instance));
    }
  }
  // Expands a design-unit-level `package p is new g generic map (...)` whose
  // template is declared in the same design file into an ordinary package
  // and body (IEEE 1076-2008 4.9): constant and type generics become
  // constant and subtype declarations. Returns false, leaving the tokens
  // unchanged, for any other form.
  bool expand_vhdl_library_package_instance();

  DesignUnit parse_entity(const Token &start);

  DesignUnit parse_vhdl_configuration(const Token &start);

  VhdlComponentDeclaration
  parse_vhdl_component_declaration(const Token &start,
                                   std::size_t declaration_order);

  void add_vhdl_component_declaration(
      std::vector<VhdlComponentDeclaration> &declarations,
      VhdlComponentDeclaration declaration, const Token &start);

  void parse_vhdl_component_ports(VhdlComponentDeclaration &declaration,
                                  const Token &start);

  VhdlBlockConfiguration parse_vhdl_block_configuration(const Token &start);

  VhdlComponentConfiguration
  parse_vhdl_component_configuration(const Token &start, bool require_end_for);

  VhdlBindingIndication parse_vhdl_binding_indication(const Token &start);

  void parse_vhdl_binding_port_map(std::vector<PortConnection> &associations,
                                   const Token &start);


  void add_vhdl_generic(DesignUnit &unit, ParameterDeclaration generic,
                        const Token &name);

  void parse_vhdl_generics(DesignUnit &unit, const Token &start,
                           bool expect_terminating_semicolon = true);

  [[nodiscard]] bool vhdl_generic_clause_precedes_subprogram() const;

  void parse_vhdl_generic_subprogram(DesignUnit &unit, const Token &start,
                                     bool allow_declaration);

  void parse_vhdl_severity_clause(Statement& statement);
  // The diagnostics of one generic association list.
  struct VhdlGenericAssociationText {
    std::string_view duplicate_code;
    std::string_view duplicate;
    std::string_view positional_code;
    std::string_view positional;
    std::string_view box;
    std::string_view box_code;
    bool allow_open{};
  };
  // Parses the associations of a generic map up to its ')'.
  void parse_vhdl_generic_associations(
      std::vector<ParameterOverride>& associations,
      const VhdlGenericAssociationText& text);
  void parse_vhdl_subprogram_generic_map(
      std::vector<ParameterOverride> &associations, bool &box,
      const Token &start);

  GenericSubprogramInstantiation
  parse_vhdl_subprogram_instantiation(const Token &start,
                                      std::string_view kind);

  void parse_vhdl_function_item(DesignUnit &unit, const Token &start, bool pure,
                                bool allow_declaration);

  void parse_vhdl_procedure_item(DesignUnit &unit, const Token &start,
                                 bool allow_declaration);

  ParameterDeclaration parse_vhdl_interface_function(const Token &start,
                                                     bool pure);

  ParameterDeclaration parse_vhdl_interface_procedure(const Token &start);

  FunctionDeclaration parse_vhdl_function(const Token &start, bool pure,
                                          bool allow_declaration);

  ProcedureDeclaration parse_vhdl_procedure(const Token &start,
                                            bool allow_declaration);

  std::vector<FunctionArgument> parse_vhdl_function_parameters(bool pure = true);

  std::vector<ProcedureArgument> parse_vhdl_procedure_parameters();

  void parse_vhdl_ports(DesignUnit &unit);

  struct ParsedVhdlModeViewIndication {
    Type type;
    VhdlModeViewIndication indication;
  };

  ParsedVhdlModeViewIndication
  parse_vhdl_mode_view_indication(const Token &start);

  Type parse_vhdl_type(const bool allow_integer = false,
                       const bool /*runtime_base_integer_only*/ = false);

  // A range in a range constraint (`L to R`, `L downto R`, or a range
  // attribute such as `x'range`) or, with allow_subtype_name, a discrete
  // range in an index constraint, which may also be a discrete subtype name
  // or `T range ...` (IEEE 1076-2008 5.2.1, 5.3.2.1).
  std::optional<DiscreteRangeExpression>
  parse_vhdl_discrete_range(bool allow_subtype_name);
  bool vhdl_record_constraint_ahead();
  void parse_vhdl_record_constraint(Type &type,
                                    const std::vector<std::string> &path);
  void parse_vhdl_element_constraints(Type &type,
                                      std::vector<std::string> path);

  Type parse_vhdl_unspecified_type(const Token &start);

  void parse_vhdl_end(std::string_view expected_kind,
      std::string_view expected_name = {});

  DesignUnit parse_architecture(const Token &start);

  void parse_type_declaration(DesignUnit &unit, const Token &start,
                              bool nested_scope = false);

  void parse_vhdl_mode_view_declaration(
      std::vector<TypeAliasDeclaration> &declarations,
      const Token &start,
      bool nested_scope = false);

  std::shared_ptr<VhdlProtectedInfo>
  parse_vhdl_protected_type(const Token &start,
                            std::string_view canonical_name, bool body);

  void parse_vhdl_attribute_declaration(DesignUnit &unit, const Token &start);

  void parse_vhdl_group_declaration(DesignUnit &unit, const Token &start);

  void parse_vhdl_disconnection_specification(
      std::vector<VhdlDisconnectionSpecification> &specifications,
      const Token &start);
  // `conv(formal) => actual` on an output or inout port of a declared
  // component, rewritten through an implicit signal (IEEE 1076-2008
  // 6.5.7.1).
  void rewrite_vhdl_formal_conversions(DesignUnit& unit,
      const std::vector<DesignUnit>* entities = nullptr);
  void apply_vhdl_disconnection_specifications(
      const std::vector<SignalDeclaration> &signals,
      const std::vector<VhdlDisconnectionSpecification> &specifications,
      std::vector<Statement> &statements);

  void parse_subtype_declaration(DesignUnit &unit, const Token &start,
                                 bool nested_scope = false);

  void parse_signal_declaration(
      std::vector<SignalDeclaration> &signals,
      const std::vector<ParameterDeclaration> *constants = nullptr);
  // A subprogram alias signature after its '[' (IEEE 1076-2008 4.5.3).
  std::optional<VhdlAliasSignature> parse_vhdl_alias_signature();
  void parse_vhdl_object_alias(
      std::vector<TypeAliasDeclaration> &type_aliases,
      std::vector<SignalAliasDeclaration> &aliases,
      const Token &start);
  void parse_vhdl_shared_variable(DesignUnit &unit, const Token &start);
  void parse_vhdl_file_declaration(std::vector<VariableDeclaration> &files,
                                   const Token &start);
  bool parse_vhdl_local_nonobject_declaration(
      std::vector<ParameterDeclaration> &constants,
      std::vector<TypeAliasDeclaration> &type_aliases,
      std::vector<SignalAliasDeclaration> &signal_aliases,
      const std::vector<VariableDeclaration> &variables,
      std::vector<PackageInstantiation> &package_instances,
      std::vector<FunctionDeclaration> &functions,
      std::vector<ProcedureDeclaration> &procedures,
      std::vector<VhdlAttributeDeclaration> &attributes,
      std::vector<VhdlGroupDeclaration> &groups);
  void validate_vhdl_local_declaration_names(
      const std::vector<ParameterDeclaration> &constants,
      const std::vector<TypeAliasDeclaration> &type_aliases,
      const std::vector<SignalAliasDeclaration> &signal_aliases,
      const std::vector<VariableDeclaration> &variables,
      const std::vector<PackageInstantiation> &package_instances,
      const std::vector<FunctionDeclaration> &functions,
      const std::vector<ProcedureDeclaration> &procedures);

  void parse_concurrent_statement(DesignUnit &unit);

  GenerateRegion parse_vhdl_conditional_generate(const Token &label,
                                                 const Token &start);

  GenerateRegion parse_vhdl_iterative_generate(const Token &label,
                                               const Token &start);

  GenerateRegion parse_vhdl_selection_generate(const Token &label,
                                               const Token &start);

  GenerateRegion parse_vhdl_static_block(const Token &label,
                                         const Token &start);

  bool parse_vhdl_generate_declarations(GenerateBody &body,
                                        VhdlComponentDeclarationRegion region);

  void parse_vhdl_generate_constant(GenerateBody &body, const Token &start);

  void parse_vhdl_generate_branch(GenerateBody &body,
                                  const bool stop_at_case_alternative = false);

  Instance parse_vhdl_instance(const Token &label);

  void parse_vhdl_generic_map(Instance &instance, const Token &start);

  void parse_vhdl_port_map(std::vector<PortConnection> &connections,
                           const Token &start);

  PortConnection parse_vhdl_port_connection();

  void skip_vhdl_connection_actual();

  Process parse_process(
      std::optional<Token> label, bool postponed = false);

  std::vector<Statement>
  parse_statement_list(std::initializer_list<std::string_view> terminators);

  std::optional<Statement>
  parse_sequential_statement(const std::optional<Token> &opening_label);

  void parse_sequential_block_declarations(Statement &statement);

  std::optional<Statement> parse_vhdl_procedure_call();

  // A formal designator naming a subelement of a formal (`p(1)`, `p.f`,
  // `p(1)(2)`, `p(1, 2)`, `p(3 downto 0)`) followed by `=>` associates that
  // subelement individually (IEEE 1076-2008 6.5.7.1).
  struct VhdlIndividualPart {
    std::vector<Expression> path;
    Expression value;
  };
  [[nodiscard]] bool vhdl_individual_formal_ahead() const;
  // Parses `formal selectors => actual`; returns the formal's simple name.
  std::string parse_vhdl_individual_part(VhdlIndividualPart &part);
  // The named aggregate actual that the individual associations of one
  // formal make together.
  [[nodiscard]] Expression build_vhdl_individual_aggregate(
      const std::vector<VhdlIndividualPart> &parts,
      const SourceSpan &span) const;

  Statement parse_vhdl_assertion(const Token &start);

  void validate_opening_loop_label(const std::string_view label,
                                   const Token &token);

  void parse_loop_end_label(const std::string_view opening_label);

  void parse_statement_end_label(std::string_view opening_label,
                                 std::string_view statement_kind);

  Statement parse_if_branch(const Token &start);

  std::optional<Statement> parse_assignment(bool concurrent);

  Statement parse_vhdl_selected_assignment(const Token &start, bool concurrent);

  Expression parse_vhdl_case_choice();

  Statement parse_conditional_signal_assignment(Statement assignment);

  void parse_vhdl_waveform(Statement &statement);

  void parse_vhdl_delay_mechanism(Statement &statement);

  void diagnose_misplaced_vhdl_delay_mechanism();

  Expression parse_conditional_assignment_value();

  Delay parse_vhdl_delay(const Token &start);

  Expression parse_lvalue();

  // After '(': a slice `left to|downto right)` or an index group
  // `i[, j...])` of `prefix` (IEEE 1076-2008 8.4, 8.5). A chained suffix
  // takes one index only.
  Expression parse_vhdl_name_suffixes(Expression prefix,
      const SourceSpan& start);
  Expression parse_index_or_slice_suffix(Expression prefix,
      const SourceSpan& start, std::string_view slice_close,
      std::string_view index_close, std::string_view code,
      bool index_list = true);
  Expression parse_expression(int minimum_precedence = 0,
                              bool allow_conditional = true);

  struct BinaryOperation {
    int precedence;
    std::string name;
    std::size_t token_count{1};
  };

  std::optional<BinaryOperation> binary_operation() const;

  Expression parse_unary();

  Expression parse_primary();

  std::optional<Expression> parse_vhdl_bit_string_literal();

  static void infer_process_edge(Process &process);

  std::size_t sequential_loop_depth_{};
  std::unordered_set<std::string> vhdl_named_types_;
  // Type and subtype names declared anywhere in this design file, collected
  // on first use.
  std::optional<std::unordered_set<std::string>> vhdl_source_type_names_;
  bool vhdl_source_declares_type(const std::string &name);
  std::optional<bool> vhdl_source_uses_reflection_;
  // The source names STD.REFLECTION (`use std.reflection.all` on a unit or
  // its package declaration).
  bool vhdl_source_uses_reflection();
  std::unordered_map<std::string, TypeDeclarationKind> vhdl_named_type_kinds_;
  // Package declarations and bodies declared in a package declarative region
  // (IEEE 1076-2008 4.7, 4.8), emitted as units after their enclosing unit.
  std::vector<DesignUnit> nested_package_units_;
  // Token ranges [begin, end) expanded from design-unit-level package
  // instances, with the instance name. Their tokens keep the template's
  // spans, so declaration identities also name the instance: each instance
  // declares distinct types (IEEE 1076-2008 4.9).
  struct ExpandedPackageInstance {
    std::size_t begin{};
    std::size_t end{};
    std::string name;
  };
  std::vector<ExpandedPackageInstance> expanded_package_instances_;
  // Design-unit-level instances of an IEEE intrinsic generic package: the
  // instance name and the template, whose intrinsic names the unit exports.
  std::vector<std::pair<std::string, std::string>>
      ieee_intrinsic_package_instances_;
  // The identity of a declaration begun at `start`: its source position and
  // name, qualified by the package instance it was expanded into.
  std::string vhdl_declaration_identity(const Token &start,
                                        std::string_view name) const;
  std::vector<std::string> sequential_loop_labels_;
  std::vector<std::string> sequential_loop_labels_seen_;
  bool numeric_bit_context_{};
  // The design unit's context imports STD.ENV (`use std.env.all`).
  bool std_env_context_{};
  // Group templates declared by the units parsed so far.
  std::unordered_set<std::string> vhdl_group_templates_;
  // Units of physical types declared so far in this design file; a unit
  // name alone denotes one unit (IEEE 1076-2008 5.2.4.1).
  std::unordered_set<std::string> vhdl_physical_unit_names_;
  // Objects whose names hide a physical unit name in a nested region.
  std::unordered_set<std::string> vhdl_unit_hiding_object_names_;
  // Component names declared so far: `label : name;` in a block or generate
  // body instantiates such a component (IEEE 1076-2008 11.7.1).
  std::unordered_set<std::string> vhdl_component_names_;
  // Functions declared so far that can be called without parameters.
  std::unordered_set<std::string> vhdl_parameterless_function_names_;
  // Functions whose declarative part or body is being parsed.
  std::vector<std::string> vhdl_enclosing_function_names_;
  // Configuration specifications in block declarative parts, moved to the
  // enclosing architecture once it is parsed.
  std::vector<VhdlComponentConfiguration> block_configuration_specifications_;
  bool in_vhdl_function_{};
  bool in_vhdl_procedure_{};
  // True while parsing a concurrent signal assignment.
  bool parsing_concurrent_assignment_{};
  // Access types declared so far, and objects declared with one: a name of
  // such an object followed by an index or a record element selects from
  // the designated object (IEEE 1076-2008 8.3, implicit dereference).
  std::unordered_set<std::string> vhdl_access_type_names_;
  // Names of signals declared guarded (bus or register, IEEE 1076-2008
  // 6.4.2.3), for the guarded-target rule of 11.6.
  std::unordered_set<std::string> vhdl_guarded_signal_names_;
  // Deferred constants of each package (IEEE 1076-2008 4.8) whose full
  // declaration a package body has not yet given.
  std::unordered_map<std::string, std::unordered_set<std::string>>
      vhdl_pending_deferred_constants_;
  // Aliases of external names (IEEE 1076-2008 6.6.2, 8.7): each use of the
  // alias denotes the external name.
  std::unordered_map<std::string, Expression> vhdl_external_aliases_;
  // Object alias name -> simple name of the whole object it denotes, so a
  // user-defined attribute of the object applies through the alias (IEEE
  // 1076-2008 6.6.2).
  std::unordered_map<std::string, std::string> vhdl_object_alias_targets_;
  // Entity class (signal, constant, variable) of each object name declared
  // so far, for `attribute a of all : signal` (IEEE 1076-2008 7.2).
  std::unordered_map<std::string, std::string> vhdl_object_classes_;
  // Type mark of each subtype declared in this design file; a type
  // declaration maps to itself. Resolves `T'BASE` (IEEE 1076-2008 16.2.2).
  std::unordered_map<std::string, std::string> vhdl_subtype_type_marks_;
  [[nodiscard]] std::optional<std::string> vhdl_base_type_mark(
      std::string name) const;
  std::unordered_set<std::string> vhdl_access_object_names_;
  void note_vhdl_access_object(const std::string& name, const Type& type);
  bool vhdl_profile_compatible_{true};
  VhdlStandard vhdl_standard_ { VhdlStandard::Vhdl2008 };
};

} // namespace fsim::frontend
