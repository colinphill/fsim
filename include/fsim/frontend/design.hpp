// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design_core.hpp"

namespace fsim::frontend {

/// Owning HIR for one SystemVerilog covergroup declaration.
///
/// Covergroups remain declarations within compilation-unit, package, module,
/// interface, program, or class scopes. Semantic passes populate canonical
/// identities and per-instance specialization state without replacing source
/// spellings.
enum class SystemVerilogCovergroupOwnerKind {
  DesignUnit,
  Class
};

struct SystemVerilogCovergroupFormal {
  PortDirection direction{PortDirection::Unknown};
  bool const_ref{};
  std::vector<Token> type_tokens;
  std::string name;
  std::optional<Token> name_token;
  SourceSpan name_span;
  std::vector<Token> default_tokens;
  SourceSpan span;
};

enum class SystemVerilogCovergroupSamplingKind {
  Event,
  WithFunctionSample
};

struct SystemVerilogCovergroupSampling {
  SystemVerilogCovergroupSamplingKind kind{
      SystemVerilogCovergroupSamplingKind::Event};
  std::vector<Token> tokens;
  std::vector<SystemVerilogCovergroupFormal> formals;
  SourceSpan span;
};

enum class SystemVerilogCovergroupOptionScope {
  Instance,
  Type
};

struct SystemVerilogCovergroupOptionAssignment {
  SystemVerilogCovergroupOptionScope scope{
      SystemVerilogCovergroupOptionScope::Instance};
  std::string name;
  std::optional<Token> name_token;
  SourceSpan name_span;
  std::vector<Token> value_tokens;
  SourceSpan span;
};

enum class SystemVerilogCoverageDeclarationKind {
  Coverpoint,
  Cross
};

enum class SystemVerilogCoverageReferenceKind {
  ConstructorFormal,
  SampleFormal,
  OwnerObject,
  Coverpoint,
  Qualified
};

struct SystemVerilogCoverageReference {
  SystemVerilogCoverageReferenceKind kind{
      SystemVerilogCoverageReferenceKind::OwnerObject};
  std::string canonical_name;
  std::vector<Token> tokens;
  SourceSpan span;
};

struct SystemVerilogCoverageCrossOperand {
  std::string name;
  std::vector<Token> tokens;
  SourceSpan span;
  std::optional<std::size_t> resolved_declaration_index;
  bool implicit_coverpoint{};
};

enum class SystemVerilogCoverageBinKind {
  Regular,
  Ignore,
  Illegal,
};

enum class SystemVerilogCoverageBinSelection {
  Explicit,
  Automatic,
  Default,
  DefaultSequence,
};

struct SystemVerilogCoverageBinValue {
  std::vector<Token> tokens;
  std::optional<std::int64_t> exact_value;
  std::optional<std::int64_t> range_left;
  std::optional<std::int64_t> range_right;
  std::uint64_t wildcard_value{};
  std::uint64_t wildcard_mask{};
  std::uint32_t width{};
  bool wildcard{};
  SourceSpan span;
};

enum class SystemVerilogCoverageTransitionRepetitionKind {
  None,
  Consecutive,
  Goto,
  Nonconsecutive,
};

struct SystemVerilogCoverageTransitionRepetition {
  SystemVerilogCoverageTransitionRepetitionKind kind{
      SystemVerilogCoverageTransitionRepetitionKind::None};
  std::uint32_t minimum{1U};
  std::optional<std::uint32_t> maximum{1U};
  SourceSpan span;
};

struct SystemVerilogCoverageTransitionStep {
  std::vector<SystemVerilogCoverageBinValue> values;
  SystemVerilogCoverageTransitionRepetition repetition;
  SourceSpan span;
};

struct SystemVerilogCoverageTransitionDelay {
  std::uint32_t minimum{1U};
  std::uint32_t maximum{1U};
  SourceSpan span;
};

struct SystemVerilogCoverageTransitionSequence {
  std::vector<SystemVerilogCoverageTransitionStep> steps;
  std::vector<SystemVerilogCoverageTransitionDelay> delays;
  SourceSpan span;
};

struct SystemVerilogCoverageBin {
  SystemVerilogCoverageBinKind kind{
      SystemVerilogCoverageBinKind::Regular};
  SystemVerilogCoverageBinSelection selection{
      SystemVerilogCoverageBinSelection::Explicit};
  std::string name;
  std::optional<Token> name_token;
  std::size_t declaration_index{};
  std::string source_name;
  std::optional<std::size_t> array_index;
  std::optional<std::size_t> declared_array_size;
  bool wildcard{};
  std::vector<SystemVerilogCoverageBinValue> values;
  std::vector<SystemVerilogCoverageTransitionSequence> transitions;
  std::vector<Token> cross_selection_tokens;
  std::vector<Token> iff_tokens;
  SourceSpan iff_span;
  std::uint32_t weight{1U};
  std::uint32_t goal{100U};
  std::uint64_t at_least{1U};
  SourceSpan span;
};

struct SystemVerilogCoverageDeclaration {
  SystemVerilogCoverageDeclarationKind kind{
      SystemVerilogCoverageDeclarationKind::Coverpoint};
  std::string name;
  std::optional<Token> name_token;
  SourceSpan name_span;
  bool explicit_name{};
  std::size_t declaration_index{};
  std::vector<Token> expression_tokens;
  SourceSpan expression_span;
  std::vector<SystemVerilogCoverageCrossOperand> cross_operands;
  std::vector<Token> iff_tokens;
  SourceSpan iff_span;
  std::vector<Token> body_tokens;
  SourceSpan body_span;
  std::vector<SystemVerilogCovergroupOptionAssignment> option_assignments;
  std::uint32_t effective_weight{1U};
  std::uint32_t effective_goal{100U};
  std::uint64_t effective_at_least{1U};
  std::vector<SystemVerilogCoverageBin> bins;
  std::vector<SystemVerilogCoverageReference> references;
  SourceSpan span;
};

// Raw covergroup ownership is deliberately independent of parser storage.
// Later coverage passes structure the retained header and body tokens without
// losing exact source and macro-expansion provenance.
struct SystemVerilogCovergroupDeclaration {
  SystemVerilogCovergroupOwnerKind owner_kind{
      SystemVerilogCovergroupOwnerKind::DesignUnit};
  std::string name;
  std::string owner_identity;
  std::string canonical_identity;
  std::string specialization_identity;
  std::string runtime_identity_prefix;
  std::optional<Token> name_token;
  SourceSpan name_span;
  std::vector<Token> header_tokens;
  SourceSpan header_span;
  std::vector<SystemVerilogCovergroupFormal> formals;
  std::optional<SystemVerilogCovergroupSampling> sampling;
  std::vector<Token> body_tokens;
  SourceSpan body_span;
  std::vector<SystemVerilogCovergroupOptionAssignment> option_assignments;
  std::uint32_t effective_instance_weight{1U};
  std::uint32_t effective_instance_goal{100U};
  std::uint32_t effective_type_weight{1U};
  std::uint32_t effective_type_goal{100U};
  bool effective_per_instance{};
  bool effective_merge_instances{};
  std::vector<SystemVerilogCoverageDeclaration> coverage_declarations;
  std::optional<std::string> end_name;
  std::optional<Token> end_name_token;
  SourceSpan end_name_span;
  SourceSpan span;
};

struct SystemVerilogCovergroupSampleCall {
  std::string declaration_identity;
  std::string instance_identity;
  std::vector<Expression> actuals;
  SourceSpan span;
};

struct SystemVerilogCoverageBinHit {
  std::size_t coverage_declaration_index{};
  std::size_t bin_declaration_index{};
  std::string identity;
  std::optional<std::int64_t> automatic_value;
  std::uint64_t hit_count{};
  std::uint64_t at_least{1U};
  bool covered{};
};

struct SystemVerilogCoverageTransitionProgress {
  std::size_t coverage_declaration_index{};
  std::size_t bin_declaration_index{};
  std::size_t sequence_index{};
  std::size_t step_index{};
  std::uint32_t repetition_count{};
  std::uint32_t samples_since_step{};
};

struct SystemVerilogCoveragePreviousSample {
  std::size_t coverage_declaration_index{};
  std::int64_t value{};
  std::uint64_t unknown_mask{};
  std::uint32_t width{64U};
};

struct SystemVerilogCoverageCrossBinState {
  std::size_t coverage_declaration_index{};
  std::optional<std::size_t> bin_declaration_index;
  std::string identity;
  std::vector<std::string> operand_bin_identities;
  std::uint64_t hit_count{};
  std::uint64_t exclusion_count{};
  std::uint32_t weight{1U};
  std::uint32_t goal{100U};
  std::uint64_t at_least{1U};
  bool covered{};
  bool excluded{};
};

struct SystemVerilogCoverageIllegalBinReport {
  std::string bin_identity;
  std::int64_t sampled_value{};
  SourceSpan span;
};

struct SystemVerilogCovergroupInstance {
  std::string name;
  std::string owner_identity;
  std::string declaration_identity;
  std::string specialization_identity;
  std::string runtime_identity;
  std::vector<Expression> constructor_actuals;
  std::vector<SystemVerilogCovergroupOptionAssignment> initial_option_state;
  std::vector<SystemVerilogCovergroupSampleCall> sample_calls;
  std::vector<SystemVerilogCoverageBinHit> bin_hits;
  std::vector<SystemVerilogCoverageTransitionProgress> transition_progress;
  std::vector<SystemVerilogCoveragePreviousSample> previous_samples;
  std::vector<SystemVerilogCoverageCrossBinState> cross_bin_state;
  std::vector<SystemVerilogCoverageIllegalBinReport> illegal_bin_reports;
  bool class_member_template{};
  SourceSpan span;
};

/// Owning HIR for one SystemVerilog class declaration.
///
/// Classes remain declarations within compilation-unit, package, module,
/// interface, or class scopes and deliberately are not UnitKind values. This
/// makes them impossible to select as simulation tops. Nested declarations
/// are recursively owned by their lexical parent; semantic passes populate
/// canonical_identity and the resolved base declaration without replacing
/// source spellings.
struct SystemVerilogClassDeclaration {
  std::string name;
  std::string canonical_identity;
  std::string enclosing_scope;
  std::string library;
  std::string compilation_unit_identity;
  std::vector<ParameterDeclaration> parameters;
  std::optional<SystemVerilogClassBase> base;
  std::vector<SystemVerilogClassBase> implemented_interfaces;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SystemVerilogClassProperty> properties;
  std::vector<SystemVerilogClassMethod> methods;
  std::vector<SystemVerilogClassConstraint> constraints;
  std::vector<SystemVerilogCovergroupDeclaration> covergroups;
  std::vector<SystemVerilogClassDeclaration> nested_classes;
  SystemVerilogClassLifetime lifetime{
      SystemVerilogClassLifetime::Inherited};
  bool is_virtual{};
  bool is_interface{};
  bool is_forward_declaration{};
  std::optional<std::string> end_name;
  SourceSpan span;
};

/// Typed source-level VHDL procedure.
///
/// Procedures are retained independently from functions and SystemVerilog
/// tasks. The bounded v1 slice is same-language, scalar, time-free, and uses
/// deterministic copy-in/copy-out for variable-class formals.
struct ProcedureDeclaration {
  std::string name;
  std::vector<ProcedureArgument> arguments;
  std::vector<ParameterDeclaration> constants;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SignalAliasDeclaration> signal_aliases;
  std::vector<PackageInstantiation> package_instances;
  std::vector<FunctionDeclaration> functions;
  std::vector<ProcedureDeclaration> procedures;
  std::vector<VariableDeclaration> variables;
  std::vector<Statement> statements;
  Language language{Language::Vhdl2008};
  bool defined{true};
  // Nonempty only for an elaborated generic-subprogram instance.
  std::string specialization_identity;
  SourceSpan span;
  // Package/body/context sources required to specialize this callable.
  std::vector<std::string> source_dependencies;
  // Nonempty on an elaboration copy made visible through a package. Used to
  // distinguish use-visible homographs from duplicates in one local region.
  std::string visibility_owner;
};

/// Source-ordered private state and methods retained from one VHDL protected
/// type declaration or body. Declaration/body conformance and execution-time
/// mutual exclusion are semantic phases, not parser guesses.
struct VhdlProtectedInfo {
  bool body{};
  // Semantic analysis folds a conforming body into its declaration. The
  // public declaration retains nominal identity while these fields record
  // that executable private state and method bodies are available.
  bool has_body{};
  bool body_conformant{};
  std::vector<VariableDeclaration> variables;
  std::vector<FunctionDeclaration> functions;
  std::vector<ProcedureDeclaration> procedures;
  // Source-ordered packed offsets for private variables after resolution.
  // Each member remains independently stored at runtime; this layout is the
  // stable debugger/provenance view and validates the bounded representation.
  std::vector<std::size_t> variable_offsets;
  std::size_t storage_width{};
  SourceSpan span;
};

/// Retained VHDL-2008 generic function template.
///
/// A template is not callable. Its ordinary function declaration/body is
/// retained separately so elaboration can specialize it with the generic
/// interface before publishing a callable instance.
struct GenericFunctionTemplate {
  std::vector<ParameterDeclaration> generic_parameters;
  FunctionDeclaration function;
  // The complete generic-clause plus subprogram span. The nested function
  // span may instead identify a matching package-body implementation.
  SourceSpan span;
};

/// Retained VHDL-2008 generic procedure template.
struct GenericProcedureTemplate {
  std::vector<ParameterDeclaration> generic_parameters;
  ProcedureDeclaration procedure;
  SourceSpan span;
};

/// A declarative VHDL-2008 generic subprogram instantiation.
///
/// Function and procedure instances use separate DesignUnit collections so
/// their kind remains explicit even before template lookup succeeds.
struct GenericSubprogramInstantiation {
  std::string name;
  std::string template_name;
  std::vector<ParameterOverride> generic_map;
  bool generic_map_box{};
  SourceSpan span;
};

enum class GenerateKind {
  StaticBlock,
  Conditional,
  Iterative,
  Selection,
};

struct GenerateRegion;

struct GenerateBody {
  // Locally static VHDL constants and SystemVerilog parameters/localparams.
  // These are evaluated in declaration order during generate expansion and
  // are not externally overridable specialization parameters.
  std::vector<ParameterDeclaration> constants;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SignalDeclaration> signals;
  std::vector<SignalAliasDeclaration> signal_aliases;
  std::vector<VariableDeclaration> variables;
  std::vector<FunctionDeclaration> functions;
  std::vector<TaskDeclaration> tasks;
  std::vector<ProcedureDeclaration> procedures;
  std::vector<GenericFunctionTemplate> generic_function_templates;
  std::vector<GenericProcedureTemplate> generic_procedure_templates;
  std::vector<GenericSubprogramInstantiation> generic_function_instances;
  std::vector<GenericSubprogramInstantiation> generic_procedure_instances;
  std::vector<PackageInstantiation> package_instances;
  std::vector<VhdlComponentDeclaration> vhdl_component_declarations;
  std::vector<Statement> concurrent_statements;
  std::vector<Process> processes;
  std::vector<Instance> instances;
  std::vector<GenerateRegion> generate_regions;
};

struct GenerateChoice {
  Expression left;
  std::optional<Expression> right;
  bool descending{};
  SourceSpan span;
};

struct GenerateAlternative {
  std::string scope;
  std::vector<GenerateChoice> choices;
  bool is_default{};
  GenerateBody body;
  SourceSpan span;
};

/// Elaboration-time hierarchy region. Conditional regions use `condition`
/// and both branches. Iterative regions use `variable`, `initial`,
/// `condition`, and `iteration`, with their body in `then_body`.
/// Selection regions use `condition` as the selector plus `alternatives`.
/// Static regions always elaborate `then_body`, optionally beneath
/// `then_scope`; a valid `condition` retains a VHDL block guard expression.
/// Regions recursively compose while the current executable subset admits
/// module/entity instances as leaf items.
struct GenerateRegion {
  GenerateKind kind{GenerateKind::Conditional};
  std::string then_scope;
  std::string else_scope;
  std::string variable;
  Expression initial;
  Expression condition;
  Expression iteration;
  GenerateBody then_body;
  GenerateBody else_body;
  std::vector<GenerateAlternative> alternatives;
  // VHDL block-header interfaces and association aspects. Other generate
  // kinds and SystemVerilog static regions leave these collections empty.
  std::vector<ParameterDeclaration> block_generics;
  std::vector<ParameterOverride> block_generic_map;
  std::vector<SignalDeclaration> block_ports;
  std::vector<PortConnection> block_port_map;
  SourceSpan span;
};

enum class VhdlContextItemKind {
  LibraryClause,
  UseClause,
  ContextReference,
};

struct VhdlContextItem {
  VhdlContextItemKind kind{VhdlContextItemKind::LibraryClause};
  std::vector<std::string> selected_names;
  SourceSpan span;
};

struct SystemVerilogImport {
  std::string package;
  // Empty means wildcard import.
  std::string name;
  SourceSpan span;
};

enum class SystemVerilogModportMemberKind {
  Signal,
  FunctionImport,
  FunctionExport,
  TaskImport,
  TaskExport,
  Clocking,
};

struct SystemVerilogModportMember {
  std::string name;
  PortDirection direction{PortDirection::Unknown};
  SourceSpan span;
  SystemVerilogModportMemberKind kind{
      SystemVerilogModportMemberKind::Signal};
};

struct SystemVerilogModport {
  std::string name;
  std::vector<SystemVerilogModportMember> members;
  SourceSpan span;
};

struct SystemVerilogClockingSkew {
  EdgeKind edge{EdgeKind::Any};
  std::optional<Delay> delay;
  bool one_step{};
  SourceSpan span;
};

struct SystemVerilogClockingSignal {
  std::string name;
  PortDirection direction{PortDirection::Unknown};
  std::optional<SystemVerilogClockingSkew> skew;
  std::optional<Expression> expression;
  SourceSpan span;
};

struct SystemVerilogClockingBlock {
  std::string name;
  std::vector<Sensitivity> event;
  std::optional<SystemVerilogClockingSkew> default_input_skew;
  std::optional<SystemVerilogClockingSkew> default_output_skew;
  std::vector<SystemVerilogClockingSignal> signals;
  SourceSpan span;
};

enum class SystemVerilogAssertionDeclarationKind {
  Sequence,
  Property,
  Checker,
};

enum class SystemVerilogAssertionFormalKind {
  Value,
  Sequence,
  Property,
  Untyped,
};

struct SystemVerilogAssertionFormal {
  SystemVerilogAssertionFormalKind kind{
      SystemVerilogAssertionFormalKind::Untyped};
  PortDirection direction{PortDirection::Unknown};
  bool local{};
  std::vector<Token> type_tokens;
  std::string name;
  SourceSpan name_span;
  std::vector<Token> default_tokens;
  SourceSpan span;
};

struct SystemVerilogAssertionLocalVariable {
  std::vector<Token> type_tokens;
  std::string name;
  SourceSpan name_span;
  std::vector<Token> declarator_tokens;
  std::vector<Token> initializer_tokens;
  SourceSpan span;
};

struct SystemVerilogAssertionClock {
  std::vector<Token> event_tokens;
  SourceSpan span;
};

struct SystemVerilogAssertionDisable {
  std::vector<Token> condition_tokens;
  SourceSpan span;
};

enum class SystemVerilogAssertionReferenceKind {
  Formal,
  LocalVariable,
  DesignUnitObject,
  AssertionDeclaration,
  Hierarchical,
  Package,
};

struct SystemVerilogAssertionReference {
  SystemVerilogAssertionReferenceKind kind{
      SystemVerilogAssertionReferenceKind::DesignUnitObject};
  std::string canonical_name;
  std::vector<std::string> path;
  std::vector<Token> tokens;
  SourceSpan span;
};

enum class SystemVerilogSequenceRepetitionKind {
  None,
  Consecutive,
  Nonconsecutive,
  Goto,
};

struct SystemVerilogSequenceRange {
  std::vector<Token> minimum_tokens;
  std::vector<Token> maximum_tokens;
  SourceSpan span;
};

struct SystemVerilogSequenceElement {
  std::vector<Token> expression_tokens;
  SystemVerilogSequenceRepetitionKind repetition{
      SystemVerilogSequenceRepetitionKind::None};
  std::optional<SystemVerilogSequenceRange> repetition_range;
  SourceSpan span;
};

struct SystemVerilogSequenceDelay {
  SystemVerilogSequenceRange range;
  bool fusion{};
  SourceSpan span;
};

struct SystemVerilogSequenceIntersectionOperand {
  std::vector<Token> tokens;
  SourceSpan span;
};

enum class SystemVerilogSequenceBinaryKind {
  Throughout,
  Within,
};

struct SystemVerilogSequenceBinaryOperation {
  SystemVerilogSequenceBinaryKind kind{
      SystemVerilogSequenceBinaryKind::Throughout};
  std::vector<Token> left_tokens;
  std::vector<Token> right_tokens;
  SourceSpan span;
};

struct SystemVerilogSequenceFirstMatch {
  std::vector<Token> sequence_tokens;
  std::vector<Token> match_item_tokens;
  SourceSpan span;
};

enum class SystemVerilogSequenceEndpointKind {
  Matched,
  Triggered,
};

struct SystemVerilogSequenceEndpoint {
  SystemVerilogSequenceEndpointKind kind{
      SystemVerilogSequenceEndpointKind::Matched};
  std::string receiver_name;
  std::vector<Token> receiver_tokens;
  bool method_parentheses{};
  SourceSpan span;
};

struct SystemVerilogSequenceExpression {
  std::vector<SystemVerilogSequenceElement> elements;
  std::vector<SystemVerilogSequenceDelay> delays;
  std::vector<SystemVerilogSequenceIntersectionOperand>
      intersection_operands;
  std::vector<SystemVerilogSequenceBinaryOperation> binary_operations;
  std::vector<SystemVerilogSequenceFirstMatch> first_matches;
  SourceSpan span;
};

enum class SystemVerilogPropertyImplicationKind {
  Overlapped,
  Nonoverlapped,
};

struct SystemVerilogPropertyImplication {
  SystemVerilogPropertyImplicationKind kind{
      SystemVerilogPropertyImplicationKind::Overlapped};
  std::vector<Token> antecedent_tokens;
  std::vector<Token> consequent_tokens;
  SourceSpan span;
};

enum class SystemVerilogPropertyUntilKind {
  Until,
  StrongUntil,
  UntilWith,
  StrongUntilWith,
};

struct SystemVerilogPropertyUntilOperation {
  SystemVerilogPropertyUntilKind kind{
      SystemVerilogPropertyUntilKind::Until};
  std::vector<Token> left_tokens;
  std::vector<Token> right_tokens;
  SourceSpan span;
};

enum class SystemVerilogPropertyNexttimeKind {
  Nexttime,
  StrongNexttime,
};

struct SystemVerilogPropertyNexttime {
  SystemVerilogPropertyNexttimeKind kind{
      SystemVerilogPropertyNexttimeKind::Nexttime};
  std::vector<Token> count_tokens;
  std::vector<Token> operand_tokens;
  SourceSpan span;
};

enum class SystemVerilogPropertyRecurrenceKind {
  Always,
  StrongAlways,
  Eventually,
  StrongEventually,
};

struct SystemVerilogPropertyRecurrence {
  SystemVerilogPropertyRecurrenceKind kind{
      SystemVerilogPropertyRecurrenceKind::Always};
  std::optional<SystemVerilogSequenceRange> range;
  std::vector<Token> operand_tokens;
  SourceSpan span;
};

enum class SystemVerilogPropertySequenceStrengthKind {
  Strong,
  Weak,
};

struct SystemVerilogPropertySequenceStrength {
  SystemVerilogPropertySequenceStrengthKind kind{
      SystemVerilogPropertySequenceStrengthKind::Strong};
  std::vector<Token> sequence_tokens;
  SourceSpan span;
};

enum class SystemVerilogPropertyAbortOutcome {
  VacuousSuccess,
  Failure,
};

struct SystemVerilogPropertyAbort {
  SystemVerilogPropertyAbortOutcome outcome{
      SystemVerilogPropertyAbortOutcome::VacuousSuccess};
  bool synchronous{};
  std::vector<Token> condition_tokens;
  std::vector<Token> property_tokens;
  SourceSpan span;
};

struct SystemVerilogPropertyExpression {
  std::vector<SystemVerilogPropertyImplication> implications;
  std::vector<SystemVerilogSequenceDelay> delays;
  std::vector<SystemVerilogPropertyUntilOperation> until_operations;
  std::vector<SystemVerilogPropertyNexttime> nexttimes;
  std::vector<SystemVerilogPropertyRecurrence> recurrences;
  std::vector<SystemVerilogPropertySequenceStrength> sequence_strengths;
  std::vector<SystemVerilogPropertyAbort> aborts;
  SourceSpan span;
};

// Sequence, property, and checker declarations are source-owned independently
// of parser storage. Change 1 retains the exact header/body token stream so
// later Batch 154 changes can add formal, clock, disable, and expression HIR
// without reparsing source text or losing macro/source provenance.
struct SystemVerilogAssertionDeclaration {
  SystemVerilogAssertionDeclarationKind kind{
      SystemVerilogAssertionDeclarationKind::Sequence};
  std::string name;
  SourceSpan name_span;
  std::vector<Token> header_tokens;
  SourceSpan header_span;
  std::vector<SystemVerilogAssertionFormal> formals;
  std::vector<Token> body_tokens;
  SourceSpan body_span;
  std::vector<SystemVerilogAssertionLocalVariable> local_variables;
  std::optional<SystemVerilogAssertionClock> clock;
  std::optional<SystemVerilogAssertionDisable> disable;
  std::vector<Token> expression_tokens;
  SourceSpan expression_span;
  std::vector<SystemVerilogAssertionReference> references;
  std::optional<SystemVerilogSequenceExpression> sequence_expression;
  std::optional<SystemVerilogPropertyExpression> property_expression;
  std::vector<SystemVerilogSequenceEndpoint> sequence_endpoints;
  SourceSpan span;
};

enum class SystemVerilogConcurrentAssertionKind {
  Assert,
  Assume,
  Cover,
  Restrict,
};

enum class SystemVerilogAssertionRegion {
  Preponed,
  Observed,
  Reactive,
};

struct SystemVerilogConcurrentAssertion {
  SystemVerilogConcurrentAssertionKind kind{
      SystemVerilogConcurrentAssertionKind::Assert};
  std::string label;
  SourceSpan label_span;
  std::vector<Token> property_tokens;
  bool has_pass_action{};
  std::vector<Token> pass_action_tokens;
  SourceSpan pass_action_span;
  bool has_failure_action{};
  std::vector<Token> failure_action_tokens;
  SourceSpan failure_action_span;
  SystemVerilogAssertionRegion sampling_region{
      SystemVerilogAssertionRegion::Preponed};
  SystemVerilogAssertionRegion evaluation_region{
      SystemVerilogAssertionRegion::Observed};
  SystemVerilogAssertionRegion action_region{
      SystemVerilogAssertionRegion::Reactive};
  SourceSpan span;
};

struct SystemVerilogExport {
  std::string package;
  // Empty means every explicitly imported item from this package.
  std::string name;
  SourceSpan span;
};

enum class VerilogUdpLevelSymbol {
  Zero,
  One,
  Unknown,
  DontCare,
  Binary,
};

enum class VerilogUdpEdgeSymbol {
  None,
  Rising,
  Falling,
  Positive,
  Negative,
  Any,
  Explicit,
};

enum class VerilogUdpOutputSymbol {
  Zero,
  One,
  Unknown,
  NoChange,
};

struct VerilogUdpInputPattern {
  VerilogUdpLevelSymbol level{VerilogUdpLevelSymbol::DontCare};
  VerilogUdpEdgeSymbol edge{VerilogUdpEdgeSymbol::None};
  VerilogUdpLevelSymbol previous{VerilogUdpLevelSymbol::DontCare};
  VerilogUdpLevelSymbol current{VerilogUdpLevelSymbol::DontCare};
  SourceSpan span;
};

struct VerilogUdpTableRow {
  std::vector<VerilogUdpInputPattern> inputs;
  std::optional<VerilogUdpLevelSymbol> current_state;
  VerilogUdpOutputSymbol output{VerilogUdpOutputSymbol::Unknown};
  SourceSpan span;
};

struct VerilogUdpDeclaration {
  Language language{Language::Verilog2005};
  std::string library;
  std::string name;
  std::string output;
  std::vector<std::string> inputs;
  bool sequential{};
  bool output_reg{};
  std::optional<VerilogUdpOutputSymbol> initial_output;
  std::vector<VerilogUdpTableRow> rows;
  std::string time_unit;
  std::string time_precision;
  SourceSpan span;
};

/// Owning metadata budget for one materialized UDP table. This is a host
/// resource guard rather than an IEEE 1364 terminal- or row-count limit.
inline constexpr std::size_t maximum_udp_table_storage_bytes =
    256U * 1024U * 1024U;

[[nodiscard]] bool verilog_udp_table_within_resource_budget(
    std::size_t input_count,
    std::size_t row_count) noexcept;

/// Validate an owning UDP declaration restored from an untrusted artifact.
/// Source parsing emits more specific diagnostics before producing this HIR.
[[nodiscard]] bool verilog_udp_declaration_well_formed(
    const VerilogUdpDeclaration& declaration) noexcept;

[[nodiscard]] bool verilog_udp_level_matches(
    VerilogUdpLevelSymbol pattern,
    VerilogUdpLevelSymbol actual) noexcept;

[[nodiscard]] bool verilog_udp_input_matches(
    const VerilogUdpInputPattern& pattern,
    VerilogUdpLevelSymbol previous,
    VerilogUdpLevelSymbol current) noexcept;

[[nodiscard]] const VerilogUdpTableRow* find_verilog_udp_table_row(
    const VerilogUdpDeclaration& declaration,
    std::span<const VerilogUdpLevelSymbol> previous_inputs,
    std::span<const VerilogUdpLevelSymbol> current_inputs,
    VerilogUdpLevelSymbol current_output) noexcept;

enum class VerilogSpecifyEdge : std::uint8_t {
  None,
  Posedge,
  Negedge,
  Edge,
};

enum class VerilogModulePathKind : std::uint8_t {
  Parallel,
  Full,
};

enum class VerilogPathPolarity : std::uint8_t {
  None,
  Positive,
  Negative,
};

enum class VerilogPulseStyle : std::uint8_t {
  Onevent,
  Ondetect,
};

enum class VerilogTimingCheckKind : std::uint8_t {
  Setup,
  Hold,
  SetupHold,
  Recovery,
  Removal,
  RecRem,
  Skew,
  TimeSkew,
  FullSkew,
  Period,
  Width,
  NoChange,
};

struct VerilogSpecparamDeclaration {
  std::string name;
  Expression value;
  std::optional<Expression> minimum;
  std::optional<Expression> typical;
  std::optional<Expression> maximum;
  bool path_pulse{};
  std::string path_pulse_input;
  std::string path_pulse_output;
  std::optional<Expression> path_pulse_error_limit;
  std::optional<Delay> path_pulse_reject_delay;
  std::optional<Delay> path_pulse_error_delay;
  SourceSpan span;
};

struct VerilogModulePathDeclaration {
  VerilogModulePathKind kind{VerilogModulePathKind::Parallel};
  std::vector<Expression> sources;
  std::vector<Expression> destinations;
  VerilogSpecifyEdge source_edge{VerilogSpecifyEdge::None};
  VerilogPathPolarity polarity{VerilogPathPolarity::None};
  Expression destination_data_source;
  Expression condition;
  bool conditional{};
  bool ifnone{};
  std::vector<Delay> delays;
  SourceSpan span;
};

struct VerilogSpecifyPulseDeclaration {
  std::vector<Expression> terminals;
  VerilogPulseStyle style{VerilogPulseStyle::Onevent};
  bool controls_style{};
  bool show_cancelled{};
  SourceSpan span;
};

struct VerilogTimingCheckEvent {
  Expression expression;
  VerilogSpecifyEdge edge{VerilogSpecifyEdge::None};
  std::vector<std::string> edge_descriptors;
  Expression condition;
  SourceSpan span;
};

struct VerilogTimingCheckDeclaration {
  VerilogTimingCheckKind kind{VerilogTimingCheckKind::Setup};
  VerilogTimingCheckEvent reference_event;
  VerilogTimingCheckEvent data_event;
  std::vector<Expression> limits;
  std::vector<Delay> normalized_limits;
  Expression threshold;
  std::optional<Delay> normalized_threshold;
  Expression notifier;
  Expression timestamp_condition;
  Expression timecheck_condition;
  Expression delayed_reference;
  Expression delayed_data;
  Expression event_based_flag;
  Expression remain_active_flag;
  SourceSpan span;
};

struct VerilogSpecifyBlock {
  std::vector<VerilogSpecparamDeclaration> specparams;
  std::vector<VerilogModulePathDeclaration> module_paths;
  std::vector<VerilogSpecifyPulseDeclaration> pulse_declarations;
  std::vector<VerilogTimingCheckDeclaration> timing_checks;
  SourceSpan span;
};

enum class SystemVerilogDpiDirection {
  Import,
  Export,
};

enum class SystemVerilogDpiOwnerKind {
  CompilationUnit,
  DesignUnit,
};

enum class SystemVerilogDpiQualifier {
  None,
  Pure,
  Context,
};

enum class SystemVerilogDpiCallableKind {
  Function,
  Task,
};

struct SystemVerilogDpiFormal {
  PortDirection direction{PortDirection::Input};
  std::optional<Token> direction_token;
  bool const_reference{};
  std::vector<Token> type_tokens;
  std::string name;
  std::optional<Token> name_token;
  std::vector<Token> dimension_tokens;
  std::vector<Token> default_tokens;
  std::vector<Token> tokens;
  SourceSpan span;
};

struct SystemVerilogDpiResolvedFormal {
  PortDirection direction{PortDirection::Input};
  Type type;
  std::string name;
  bool reference{};
  SourceSpan span;
};

struct SystemVerilogDpiResolvedProfile {
  std::optional<Type> return_type;
  std::vector<SystemVerilogDpiResolvedFormal> formals;
  SourceSpan callable_span;
};

// Lossless source ownership for a SystemVerilog DPI import or export. Later
// DPI closure phases validate and lower the retained declaration without
// depending on the parser token stream remaining alive.
struct SystemVerilogDpiDeclaration {
  SystemVerilogDpiDirection direction{
      SystemVerilogDpiDirection::Import};
  SystemVerilogDpiOwnerKind owner_kind{
      SystemVerilogDpiOwnerKind::CompilationUnit};
  std::string owner_identity;
  std::string link_name;
  std::optional<Token> link_name_token;
  SystemVerilogDpiQualifier qualifier{
      SystemVerilogDpiQualifier::None};
  std::optional<Token> qualifier_token;
  SystemVerilogDpiCallableKind callable_kind{
      SystemVerilogDpiCallableKind::Function};
  std::optional<Token> callable_token;
  std::string systemverilog_name;
  std::optional<Token> systemverilog_name_token;
  std::optional<std::string> c_identifier;
  std::optional<Token> c_identifier_token;
  std::vector<Token> return_type_tokens;
  std::vector<Token> formal_tokens;
  std::vector<SystemVerilogDpiFormal> formals;
  std::vector<Token> profile_tokens;
  SourceSpan profile_span;
  std::string linkage_name;
  bool validated{};
  std::optional<SystemVerilogDpiResolvedProfile> resolved_profile;
  std::vector<Token> tokens;
  SourceSpan span;
};

struct DesignUnit {
  UnitKind kind{UnitKind::VerilogModule};
  Language language{Language::SystemVerilog2017};
  std::string library;
  std::string compilation_unit_identity;
  std::string name;
  // For a VHDL architecture, `name` is the architecture and `primary_name`
  // is the entity it implements.
  std::string primary_name;
  // Nonempty only for a checksum-pinned compiler-supplied standard package.
  // Intrinsic exports remain explicit without masquerading as ordinary user
  // declarations whose bodies and nominal types would require general source
  // lowering. The exact upstream files remain source dependencies.
  std::string standard_package_revision;
  std::vector<std::string> standard_package_declarations;
  // Verilog/SystemVerilog compilation-unit timing context. Empty when no
  // `timescale directive precedes this unit.
  std::string time_unit;
  std::string time_precision;
  // Verilog/SystemVerilog compilation-directive state at unit declaration.
  std::string default_nettype;
  bool is_cell{};
  // VHDL context items immediately preceding this library unit, or the
  // reusable items contained by a bounded VHDL context declaration.
  std::vector<VhdlContextItem> vhdl_context;
  // Compilation-unit or unit-local SystemVerilog package imports.
  std::vector<SystemVerilogImport> systemverilog_imports;
  // Package export/re-export declarations and interface modport views.
  std::vector<SystemVerilogExport> systemverilog_exports;
  std::vector<SystemVerilogDpiDeclaration>
      systemverilog_dpi_declarations;
  std::vector<SystemVerilogModport> systemverilog_modports;
  // Clocking declarations are unit-owned for modules, interfaces, and
  // programs. Later semantic lowering preserves their event and signal view.
  std::vector<SystemVerilogClockingBlock>
      systemverilog_clocking_blocks;
  std::optional<std::string>
      systemverilog_default_clocking_block;
  SourceSpan systemverilog_default_clocking_span;
  std::vector<SystemVerilogAssertionDeclaration>
      systemverilog_assertion_declarations;
  std::vector<SystemVerilogConcurrentAssertion>
      systemverilog_concurrent_assertions;
  std::vector<SystemVerilogCovergroupDeclaration>
      systemverilog_covergroups;
  // Package, module, and interface class declarations in lexical order.
  // Compilation-unit declarations instead live on ParsedDesign.
  std::vector<SystemVerilogClassDeclaration> systemverilog_classes;
  // Qualified definitions linked to extern class prototypes.
  std::vector<SystemVerilogClassMethod>
      systemverilog_class_method_definitions;
  // Bounded SystemVerilog packed integral typedef declarations.
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<ParameterDeclaration> parameters;
  std::vector<SignalDeclaration> ports;
  std::vector<SignalDeclaration> signals;
  std::vector<SignalAliasDeclaration> signal_aliases;
  // SystemVerilog module-scope variable objects that do not have net/signal
  // semantics. Mutable strings live here so later DesignIR lowering can give
  // them stable object identities without pretending they are packed nets.
  std::vector<VariableDeclaration> variables;
  std::vector<FunctionDeclaration> functions;
  std::vector<TaskDeclaration> tasks;
  std::vector<ProcedureDeclaration> procedures;
  // Generic subprogram templates are never directly callable. Successful
  // instances are materialized into functions/procedures during elaboration.
  std::vector<GenericFunctionTemplate> generic_function_templates;
  std::vector<GenericProcedureTemplate> generic_procedure_templates;
  std::vector<GenericSubprogramInstantiation> generic_function_instances;
  std::vector<GenericSubprogramInstantiation> generic_procedure_instances;
  // Local VHDL generic-package instances declared in this unit's declarative
  // region. Interface package formals remain ParameterKind::Package entries.
  std::vector<PackageInstantiation> package_instances;
  // Architecture-local component declarations retained in source declaration
  // order for component instantiation and configuration binding.
  std::vector<VhdlComponentDeclaration> vhdl_component_declarations;
  // Architecture declarative configuration specifications. A configuration
  // declaration instead uses vhdl_configuration on its own design unit.
  std::vector<VhdlComponentConfiguration>
      vhdl_configuration_specifications;
  std::optional<VhdlConfigurationDeclaration> vhdl_configuration;
  std::vector<Statement> concurrent_statements;
  std::vector<Process> processes;
  std::vector<Instance> instances;
  std::vector<GenerateRegion> generate_regions;
  // Verilog specify blocks retain timing-only declarations separately from
  // executable module items. Elaboration specializes and normalizes them into
  // scheduler arcs without introducing hierarchy objects or processes.
  std::vector<VerilogSpecifyBlock> verilog_specify_blocks;
  // Semantically imported design-unit sources that affect specialization and
  // native-cache identity (for example bounded VHDL package constants).
  std::vector<std::string> source_dependencies;
  SourceSpan span;
};

struct ParsedDesign {
  std::vector<DesignUnit> units;
  std::vector<VerilogUdpDeclaration> udp_declarations;
  // Ordinary compilation-unit callables are distinct from class-qualified
  // out-of-block definitions and provide exact scope ownership for DPI export.
  std::vector<FunctionDeclaration> functions;
  std::vector<TaskDeclaration> tasks;
  // Compilation-unit DPI declarations remain outside selectable design units.
  std::vector<SystemVerilogDpiDeclaration>
      systemverilog_dpi_declarations;
  // Compilation-unit classes remain non-top-selectable declarations.
  std::vector<SystemVerilogClassDeclaration> systemverilog_classes;
  std::vector<SystemVerilogClassMethod>
      systemverilog_class_method_definitions;
  std::vector<SystemVerilogCovergroupInstance>
      systemverilog_covergroup_instances;

  [[nodiscard]] const DesignUnit* find(UnitKind kind,
                                       std::string_view name) const noexcept;
};

}  // namespace fsim::frontend
