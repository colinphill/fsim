// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

enum class UnitKind {
  VhdlEntity,
  VhdlArchitecture,
  VhdlPackage,
  VhdlContext,
  SystemVerilogPackage,
  VerilogModule,
};

enum class PortDirection {
  Unknown,
  Input,
  Output,
  Inout,
  Buffer,
};

enum class ValueDomain {
  Unknown,
  Bit2,
  Logic4,
  Logic9,
  Boolean,
  Integer,
};

struct PackedRange {
  std::int64_t left{};
  std::int64_t right{};
  bool descending{true};

  [[nodiscard]] std::uint64_t width() const noexcept;
};

enum class ExpressionKind {
  Invalid,
  Identifier,
  IntegerLiteral,
  BooleanLiteral,
  LogicLiteral,
  StringLiteral,
  Unary,
  Binary,
  Call,
  Index,
  Slice,
  Concatenation,
  Replication,
};

// `text` contains the identifier/literal/operator/callee. Operands retain
// source order, so this compact tree can be lowered without
// language-specific nodes.
struct Expression {
  ExpressionKind kind{ExpressionKind::Invalid};
  std::string text;
  std::vector<Expression> operands;
  SourceSpan span;

  [[nodiscard]] bool valid() const noexcept {
    return kind != ExpressionKind::Invalid;
  }
};

struct PackedRangeExpression {
  Expression left;
  Expression right;
  SourceSpan span;
  // VHDL ranges retain their explicit `downto`/`to` direction. Verilog and
  // SystemVerilog leave this empty and derive direction from evaluated bounds.
  std::optional<bool> descending;
};

struct PackedMember {
  std::string name;
  ValueDomain domain{ValueDomain::Unknown};
  std::string spelling;
  std::optional<PackedRange> packed_range;
  bool is_signed{};
  std::optional<PackedRangeExpression> packed_range_expression;
  // Normalized offset from the least-significant bit of the containing
  // packed aggregate. Filled once every member width is concrete.
  std::uint64_t lsb_offset{};
  SourceSpan span;

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

enum class PackedAggregateKind {
  None,
  Struct,
  Union,
};

struct Type {
  ValueDomain domain{ValueDomain::Unknown};
  std::string spelling;
  std::optional<PackedRange> packed_range;
  bool is_signed{};
  // Retained until elaboration even when packed_range is already known, so a
  // parameterized unit can be specialized independently at every instance.
  std::optional<PackedRangeExpression> packed_range_expression;
  // Non-empty for a SystemVerilog user-defined type reference. Package
  // qualification is retained verbatim (for example `values::word_t`) until
  // elaboration resolves the alias in the owning specialization.
  std::string named_type;
  SourceSpan named_type_span;
  // Non-empty for a bounded SystemVerilog packed struct or union. Nested
  // aggregates are intentionally excluded from the current representation.
  std::vector<PackedMember> packed_members;
  PackedAggregateKind packed_aggregate{PackedAggregateKind::None};

  Type() = default;
  Type(
      ValueDomain domain_value,
      std::string spelling_value,
      std::optional<PackedRange> range_value,
      bool signed_value,
      std::optional<PackedRangeExpression> range_expression = {})
      : domain(domain_value),
        spelling(std::move(spelling_value)),
        packed_range(std::move(range_value)),
        is_signed(signed_value),
        packed_range_expression(std::move(range_expression)) {}

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

struct EnumLiteralDeclaration {
  std::string name;
  Expression value;
  SourceSpan span;
};

struct TypeAliasDeclaration {
  std::string name;
  Type type;
  SourceSpan span;
  // Non-empty only for a bounded SystemVerilog enum typedef. Values retain
  // their declaration-order expressions while matching immutable local
  // parameter declarations carry them through specialization.
  std::vector<EnumLiteralDeclaration> enum_literals;
};

struct SignalDeclaration {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Unknown};
  bool is_port{};
  SourceSpan span;
};

struct VariableDeclaration {
  std::string name;
  Type type;
  std::optional<Expression> initializer;
  SourceSpan span;
};

struct PortConnection {
  // Empty for a positional connection.
  std::optional<std::string> port;
  Expression value;
  SourceSpan span;
};

struct ParameterDeclaration {
  std::string name;
  Type type;
  // Invalid denotes a VHDL generic without a default. SystemVerilog value
  // parameters currently require a parsed default.
  Expression default_value;
  bool local{};
  SourceSpan span;
};

struct ParameterOverride {
  // Empty for a positional parameter override or VHDL generic actual.
  std::optional<std::string> name;
  Expression value;
  SourceSpan span;
};

enum class VerilogUnconnectedDrive {
  None,
  Pull0,
  Pull1,
};

struct Instance {
  // The source-language unit name is retained even when an explicit
  // cross-language manifest binding overrides it.
  std::string unit_name;
  std::string name;
  std::vector<ParameterOverride> parameter_overrides;
  std::vector<PortConnection> connections;
  // Compilation-directive state at the instance declaration. Pull values
  // apply only to omitted input ports.
  VerilogUnconnectedDrive unconnected_drive{
      VerilogUnconnectedDrive::None};
  SourceSpan span;
};

enum class AssignmentKind {
  VhdlSignal,
  Blocking,
  NonBlocking,
  Continuous,
};

struct Delay {
  std::uint64_t magnitude{};
  // Empty when the source supplies no physical unit or active `timescale.
  std::string unit;
  SourceSpan span;
};

enum class StatementKind {
  Assignment,
  If,
  Case,
  Loop,
  Break,
  Continue,
  Assert,
  Delay,
  WaitOn,
  WaitUntil,
  Finish,
  Block,
  Null,
};

enum class CaseMatchKind {
  Exact,
  WildcardZ,
  WildcardXZ,
};

enum class AssertionSeverity {
  Note,
  Warning,
  Error,
  Failure,
};

enum class EdgeKind {
  Any,
  Positive,
  Negative,
};

struct Sensitivity {
  EdgeKind edge{EdgeKind::Any};
  std::string signal;
  SourceSpan span;
};

struct CaseAlternative;

struct Statement {
  StatementKind kind{StatementKind::Null};
  SourceSpan span;

  AssignmentKind assignment_kind{AssignmentKind::Blocking};
  Expression target;
  Expression value;
  Expression condition;
  // A VHDL sequential for-loop retains its implicit constant name and
  // locally-static discrete range until elaboration unrolls the body.
  std::string loop_variable;
  // Canonical VHDL opening label for a loop, and the optional label selected
  // by exit/next on a loop-control statement.
  std::string loop_label;
  std::string loop_control_label;
  Expression loop_initial;
  Expression loop_limit;
  bool loop_descending{};
  // SystemVerilog `<`/`>` loop conditions exclude the retained limit;
  // VHDL discrete ranges and SV `<=`/`>=` include it.
  bool loop_limit_exclusive{};
  // Verilog/SystemVerilog repeat statements use an anonymous ascending loop
  // from zero to the exclusive, locally-static repeat count.
  bool loop_repeat{};
  // Runtime loop forms lower to an executable SimIR backedge instead of
  // elaboration-time unrolling. Their condition is stored in `condition`.
  bool loop_runtime{};
  // SystemVerilog do-while evaluates its condition after the body. Other
  // runtime loops use the default pre-test form.
  bool loop_post_test{};
  std::optional<Delay> delay;
  std::vector<Sensitivity> sensitivities;
  std::string assertion_message;
  AssertionSeverity assertion_severity{AssertionSeverity::Error};

  // Block contents or the true branch/delayed statement.
  std::vector<Statement> statements;
  // The false branch of an If statement.
  std::vector<Statement> else_statements;
  // Ordered alternatives and matching policy of a Verilog/SystemVerilog case
  // statement. VHDL sequential case retains the exact default.
  CaseMatchKind case_match_kind{CaseMatchKind::Exact};
  std::vector<CaseAlternative> case_alternatives;
  // Declarations directly owned by a procedural block.
  std::vector<VariableDeclaration> declarations;
};

struct CaseAlternative {
  std::vector<Expression> choices;
  std::vector<Statement> statements;
  bool is_default{};
  SourceSpan span;
};

enum class ProcessKind {
  VhdlProcess,
  VerilogAlways,
  SystemVerilogAlwaysFF,
  SystemVerilogAlwaysComb,
  SystemVerilogAlwaysLatch,
  Initial,
};

struct Process {
  ProcessKind kind{ProcessKind::VhdlProcess};
  std::string name;
  std::vector<VariableDeclaration> variables;
  std::vector<Sensitivity> sensitivities;
  std::vector<Statement> statements;
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
  std::vector<SignalDeclaration> signals;
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
/// `then_scope`.
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

struct DesignUnit {
  UnitKind kind{UnitKind::VerilogModule};
  Language language{Language::SystemVerilog2017};
  std::string library;
  std::string name;
  // For a VHDL architecture, `name` is the architecture and `primary_name`
  // is the entity it implements.
  std::string primary_name;
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
  // Bounded SystemVerilog packed integral typedef declarations.
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<ParameterDeclaration> parameters;
  std::vector<SignalDeclaration> ports;
  std::vector<SignalDeclaration> signals;
  std::vector<Statement> concurrent_statements;
  std::vector<Process> processes;
  std::vector<Instance> instances;
  std::vector<GenerateRegion> generate_regions;
  // Semantically imported design-unit sources that affect specialization and
  // native-cache identity (for example bounded VHDL package constants).
  std::vector<std::string> source_dependencies;
  SourceSpan span;
};

struct ParsedDesign {
  std::vector<DesignUnit> units;

  [[nodiscard]] const DesignUnit* find(UnitKind kind,
                                       std::string_view name) const noexcept;
};

}  // namespace fsim::frontend
