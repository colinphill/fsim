// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

enum class UnitKind {
  VhdlEntity,
  VhdlArchitecture,
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

struct Type {
  ValueDomain domain{ValueDomain::Unknown};
  std::string spelling;
  std::optional<PackedRange> packed_range;
  bool is_signed{};

  [[nodiscard]] std::optional<std::uint64_t> width() const noexcept;
};

struct SignalDeclaration {
  std::string name;
  Type type;
  PortDirection direction{PortDirection::Unknown};
  bool is_port{};
  SourceSpan span;
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
};

// `text` contains the identifier/literal/operator/callee. Operands retain source
// order, so this compact tree can be lowered without language-specific nodes.
struct Expression {
  ExpressionKind kind{ExpressionKind::Invalid};
  std::string text;
  std::vector<Expression> operands;
  SourceSpan span;

  [[nodiscard]] bool valid() const noexcept {
    return kind != ExpressionKind::Invalid;
  }
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

struct Instance {
  // The source-language unit name is retained even when an explicit
  // cross-language manifest binding overrides it.
  std::string unit_name;
  std::string name;
  std::vector<PortConnection> connections;
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
  Assert,
  Delay,
  WaitOn,
  Finish,
  Block,
  Null,
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
  std::optional<Delay> delay;
  std::vector<Sensitivity> sensitivities;
  std::string assertion_message;
  AssertionSeverity assertion_severity{AssertionSeverity::Error};

  // Block contents or the true branch/delayed statement.
  std::vector<Statement> statements;
  // The false branch of an If statement.
  std::vector<Statement> else_statements;
  // Ordered alternatives of a SystemVerilog case statement.
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
  // VHDL context items immediately preceding this library unit. Context
  // declarations and semantic visibility resolution are not part of the
  // current frontend slice.
  std::vector<VhdlContextItem> vhdl_context;
  std::vector<SignalDeclaration> ports;
  std::vector<SignalDeclaration> signals;
  std::vector<Statement> concurrent_statements;
  std::vector<Process> processes;
  std::vector<Instance> instances;
  SourceSpan span;
};

struct ParsedDesign {
  std::vector<DesignUnit> units;

  [[nodiscard]] const DesignUnit* find(UnitKind kind,
                                       std::string_view name) const noexcept;
};

}  // namespace fsim::frontend
