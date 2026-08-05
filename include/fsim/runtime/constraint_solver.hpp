// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogConstraintVariableId = std::size_t;

enum class SystemVerilogConstraintDomainKind : std::uint8_t {
  BitVector,
  Integer,
  Enumeration,
};

struct SystemVerilogConstraintVariableProfile {
  SystemVerilogConstraintDomainKind kind{
      SystemVerilogConstraintDomainKind::BitVector};
  std::size_t width{};
  bool signed_value{};
  std::string nominal_type;
  bool four_state{};
};

struct SystemVerilogConstraintVariable {
  std::string canonical_identity;
  SystemVerilogConstraintVariableProfile profile;
  std::vector<PackedLogic4> domain;
};

/// Read-only partial assignment presented to one solver clause. Accessing an
/// unassigned value is an error; clauses use assigned() to return Undetermined
/// until every value they require is available.
class SystemVerilogConstraintAssignment final {
 public:
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool assigned(SystemVerilogConstraintVariableId id) const;
  [[nodiscard]] const PackedLogic4& value(
      SystemVerilogConstraintVariableId id) const;
  [[nodiscard]] const SystemVerilogConstraintVariableProfile& profile(
      SystemVerilogConstraintVariableId id) const;
  [[nodiscard]] std::optional<SystemVerilogConstraintVariableId> find(
      std::string_view canonical_identity) const noexcept;

 private:
  friend class SystemVerilogConstraintSolver;
  SystemVerilogConstraintAssignment(
      std::span<const SystemVerilogConstraintVariable> variables,
      std::span<const std::optional<PackedLogic4>> values)
      : variables_(variables), values_(values) {}

  std::span<const SystemVerilogConstraintVariable> variables_;
  std::span<const std::optional<PackedLogic4>> values_;
};

enum class SystemVerilogConstraintClauseState : std::uint8_t {
  Undetermined,
  Satisfied,
  Violated,
};

struct SystemVerilogConstraintClause {
  std::string canonical_identity;
  std::vector<SystemVerilogConstraintVariableId> variables;
  std::function<SystemVerilogConstraintClauseState(
      const SystemVerilogConstraintAssignment&)> evaluate;
  bool soft{};
};

enum class SystemVerilogConstraintDistributionWeight : std::uint8_t {
  PerValue,
  AcrossRange,
};

struct SystemVerilogConstraintDistributionEntry {
  PackedLogic4 low;
  PackedLogic4 high;
  std::uint64_t weight{};
  SystemVerilogConstraintDistributionWeight weight_kind{
      SystemVerilogConstraintDistributionWeight::PerValue};
};

struct SystemVerilogConstraintDistribution {
  std::string canonical_identity;
  SystemVerilogConstraintVariableId variable{};
  std::vector<SystemVerilogConstraintDistributionEntry> entries;
};

enum class SystemVerilogConstraintResource : std::uint8_t {
  None,
  Variables,
  Clauses,
  DomainValues,
  SearchSteps,
  ElapsedWork,
};

class SystemVerilogConstraintResourceError final : public std::length_error {
 public:
  SystemVerilogConstraintResourceError(
      SystemVerilogConstraintResource resource,
      std::string message);

  [[nodiscard]] SystemVerilogConstraintResource resource() const noexcept {
    return resource_;
  }

 private:
  SystemVerilogConstraintResource resource_;
};

struct SystemVerilogConstraintSolverLimits {
  std::size_t maximum_variables{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_clauses{
      std::numeric_limits<std::size_t>::max()};
  std::size_t maximum_domain_values{
      std::numeric_limits<std::size_t>::max()};
  std::uint64_t maximum_search_steps{
      std::numeric_limits<std::uint64_t>::max()};
  std::chrono::steady_clock::duration maximum_elapsed_work{
      std::chrono::steady_clock::duration::max()};
};

enum class SystemVerilogConstraintSolveStatus : std::uint8_t {
  Satisfied,
  Unsatisfiable,
  ResourceExhausted,
};

struct SystemVerilogConstraintSolveResult {
  SystemVerilogConstraintSolveStatus status{
      SystemVerilogConstraintSolveStatus::Unsatisfiable};
  SystemVerilogConstraintResource exhausted_resource{
      SystemVerilogConstraintResource::None};
  std::vector<PackedLogic4> values;
  std::uint64_t search_steps{};
  std::uint64_t clause_evaluations{};
};

using SystemVerilogConstraintExpressionId = std::size_t;

enum class SystemVerilogConstraintExpressionOperator : std::uint8_t {
  Variable,
  Constant,
  UnaryPlus,
  UnaryMinus,
  BitwiseNot,
  LogicalNot,
  Add,
  Subtract,
  Multiply,
  Power,
  Divide,
  Modulo,
  BitwiseAnd,
  BitwiseOr,
  BitwiseXor,
  Equal,
  NotEqual,
  CaseEqual,
  CaseNotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LogicalAnd,
  LogicalOr,
  Conditional,
};

struct SystemVerilogConstraintExpressionNode {
  SystemVerilogConstraintExpressionOperator operation{
      SystemVerilogConstraintExpressionOperator::Constant};
  SystemVerilogConstraintVariableProfile profile;
  SystemVerilogConstraintVariableId variable{};
  PackedLogic4 constant;
  std::vector<SystemVerilogConstraintExpressionId> operands;
};

struct SystemVerilogConstraintExpression {
  std::vector<SystemVerilogConstraintExpressionNode> nodes;
  SystemVerilogConstraintExpressionId root{};
};

/// Constructs a validated source-order expression graph. Every operand must
/// refer to an earlier node, keeping evaluation iterative and corruption
/// checks independent of the host call stack.
class SystemVerilogConstraintExpressionBuilder final {
 public:
  [[nodiscard]] SystemVerilogConstraintExpressionId variable(
      SystemVerilogConstraintVariableId variable,
      SystemVerilogConstraintVariableProfile profile);
  [[nodiscard]] SystemVerilogConstraintExpressionId constant(
      PackedLogic4 value,
      SystemVerilogConstraintVariableProfile profile);
  [[nodiscard]] SystemVerilogConstraintExpressionId unary(
      SystemVerilogConstraintExpressionOperator operation,
      SystemVerilogConstraintExpressionId operand);
  [[nodiscard]] SystemVerilogConstraintExpressionId binary(
      SystemVerilogConstraintExpressionOperator operation,
      SystemVerilogConstraintExpressionId left,
      SystemVerilogConstraintExpressionId right);
  [[nodiscard]] SystemVerilogConstraintExpressionId conditional(
      SystemVerilogConstraintExpressionId condition,
      SystemVerilogConstraintExpressionId when_true,
      SystemVerilogConstraintExpressionId when_false);
  [[nodiscard]] SystemVerilogConstraintExpressionId implication(
      SystemVerilogConstraintExpressionId antecedent,
      SystemVerilogConstraintExpressionId consequent);
  [[nodiscard]] SystemVerilogConstraintExpressionId conditional_constraint(
      SystemVerilogConstraintExpressionId condition,
      SystemVerilogConstraintExpressionId when_true,
      std::optional<SystemVerilogConstraintExpressionId> when_false =
          std::nullopt);
  [[nodiscard]] SystemVerilogConstraintExpressionId conjunction(
      std::span<const SystemVerilogConstraintExpressionId> predicates);
  [[nodiscard]] SystemVerilogConstraintExpression finish(
      SystemVerilogConstraintExpressionId root) &&;

 private:
  [[nodiscard]] SystemVerilogConstraintExpressionId append(
      SystemVerilogConstraintExpressionNode node);

  std::vector<SystemVerilogConstraintExpressionNode> nodes_;
};

[[nodiscard]] PackedLogic4 evaluate_systemverilog_constraint_expression(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintAssignment& assignment);

[[nodiscard]] SystemVerilogConstraintClause
systemverilog_constraint_expression_clause(
    std::string canonical_identity,
    std::vector<SystemVerilogConstraintVariableId> variables,
    SystemVerilogConstraintExpression expression);

/// Deterministic finite-domain solver. Variable and domain declaration order
/// define the current search order; source-independent ordering is introduced
/// by the later solve-before/dependency scheduling layer.
class SystemVerilogConstraintSolver final {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  explicit SystemVerilogConstraintSolver(
      SystemVerilogConstraintSolverLimits limits = {},
      Clock clock = std::chrono::steady_clock::now);

  [[nodiscard]] SystemVerilogConstraintVariableId add_variable(
      SystemVerilogConstraintVariable variable);
  void replace_domain(
      SystemVerilogConstraintVariableId variable,
      std::vector<PackedLogic4> domain);
  void add_clause(SystemVerilogConstraintClause clause);
  void add_distribution(SystemVerilogConstraintDistribution distribution);
  void add_solve_before(
      SystemVerilogConstraintVariableId earlier,
      SystemVerilogConstraintVariableId later);
  [[nodiscard]] std::vector<SystemVerilogConstraintVariableId>
  search_order() const;
  [[nodiscard]] SystemVerilogConstraintSolveResult solve(
      std::uint64_t selection = 0) const;

  [[nodiscard]] std::span<const SystemVerilogConstraintVariable>
  variables() const noexcept {
    return variables_;
  }
  [[nodiscard]] std::span<const SystemVerilogConstraintClause>
  clauses() const noexcept {
    return clauses_;
  }
  [[nodiscard]] std::size_t distribution_count() const noexcept {
    return distributions_.size();
  }
  [[nodiscard]] std::span<const std::uint64_t> distribution_weights(
      std::string_view canonical_identity) const;
  [[nodiscard]] const SystemVerilogConstraintSolverLimits& limits()
      const noexcept {
    return limits_;
  }

 private:
  SystemVerilogConstraintSolverLimits limits_;
  Clock clock_;
  std::vector<SystemVerilogConstraintVariable> variables_;
  std::vector<SystemVerilogConstraintClause> clauses_;
  struct RetainedDistribution {
    std::string canonical_identity;
    SystemVerilogConstraintVariableId variable{};
    std::vector<std::uint64_t> weights;
  };
  std::vector<RetainedDistribution> distributions_;
  std::vector<std::pair<SystemVerilogConstraintVariableId,
                        SystemVerilogConstraintVariableId>> solve_before_;
  std::size_t domain_values_{};
};

}  // namespace fsim::runtime
