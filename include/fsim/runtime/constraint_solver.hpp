// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/support/systemverilog_builtin_type.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
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
  // The name of a user-defined type ("" for a built-in one).
  std::string nominal_type;
  bool four_state{};
  // The built-in data type (IEEE 1800-2023 6.11); none for a user type.
  support::SystemVerilogBuiltinType builtin{
      support::SystemVerilogBuiltinType::none};
  // The HIR identity index of a user-defined type, when it has one.
  std::optional<std::uint32_t> nominal_type_id{};
  // A non-random class property: the constraint reads it as its declared
  // type whatever profile the expression gives it (IEEE 1800-2023 18.3).
  bool state_property{};
};

// Whether a profile names its type: a built-in kind, a user type's
// identity or name, or a state property's storage.
[[nodiscard]] inline bool names_constraint_type(
    const SystemVerilogConstraintVariableProfile& profile) noexcept {
  return profile.builtin != support::SystemVerilogBuiltinType::none
      || profile.nominal_type_id.has_value()
      || !profile.nominal_type.empty()
      || profile.state_property;
}

// Whether two profiles name the same type.
[[nodiscard]] inline bool same_constraint_type(
    const SystemVerilogConstraintVariableProfile& left,
    const SystemVerilogConstraintVariableProfile& right) noexcept {
  return left.builtin == right.builtin
      && left.nominal_type_id == right.nominal_type_id
      && left.nominal_type == right.nominal_type
      && left.state_property == right.state_property;
}

struct SystemVerilogConstraintVariable {
  std::string canonical_identity;
  SystemVerilogConstraintVariableProfile profile;
  std::vector<PackedLogic4> domain;
};

using SystemVerilogConstraintVariables = std::map<
    std::string,
    SystemVerilogConstraintVariableId>;

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

struct SystemVerilogConstraintExpression;

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
  // The predicate's expression graph when the clause was built from one;
  // bound analysis narrows variable domains from it.
  std::shared_ptr<const SystemVerilogConstraintExpression> expression{};
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

struct SystemVerilogConstraintExpression;

struct SystemVerilogConstraintDistribution {
  std::string canonical_identity;
  SystemVerilogConstraintVariableId variable{};
  std::vector<SystemVerilogConstraintDistributionEntry> entries;
  /// A predicate over other variables: the weights apply only where it holds
  /// (a dist under a random `if` or implication, IEEE 1800-2017 18.5.4).
  std::shared_ptr<const SystemVerilogConstraintExpression> guard{};
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
  ShiftLeft,
  ShiftRight,
  ArithmeticShiftRight,
};

/// Frontend-neutral, owning constraint syntax retained by SimIR randomize
/// operations. Names are bound to exact solver variables when the call runs;
/// constants retain arbitrary-width packed payloads and their source profiles.
enum class SystemVerilogConstraintTemplateKind : std::uint8_t {
    Name,
    Constant,
    Unary,
    Binary,
    Conditional,
    InsideSet,
    InsideRange,
    Distribution,
    DistributionItem,
    Soft,
    Block,
    Implication,
    ConditionalConstraint,
    SolveBefore,
    SolveList,
    Unique,
    /// `a[i]`: operands are the array name and the index.
    Index,
    /// `foreach (a[i]) body`: operands are Index(a, i) and the body.
    Foreach,
    /// A method or built-in call on an operand list: `.size`, the array
    /// reductions (whose optional trailing operands are an iterator name and
    /// a with-expression), casts (cast_target) and bit counts.
    Method,
    /// `a[i].m`: the text is the member's name or canonical identity; the
    /// one operand is Index(a, i).
    Member,
};

/// What a Name or Constant template node stands for when it is a placeholder
/// the lowering or the runtime fills in before the solve (IEEE 1800-2017
/// 18.7, 18.12), instead of a name or constant written in the source.
enum class SystemVerilogConstraintTemplateRole : std::uint8_t {
    none,
    /// A Constant that takes the calling-scope value number `slot`.
    inline_value,
    /// A Name for the array number `slot` of the calling scope.
    inline_container,
    /// A Constant that takes the value held in register `slot`.
    inline_register,
    /// A Name for the array held in container register `slot`; the runtime
    /// turns it into inline_elements.
    inline_container_register,
    /// That array at the call: one constant operand per element and the
    /// array's size as the node's constant.
    inline_elements,
    /// A Name for a std::randomize argument or with-clause array; its text
    /// is the solver variable it names.
    std_randomize_variable,
    last = std_randomize_variable,
};

struct SystemVerilogConstraintTemplate {
    SystemVerilogConstraintTemplateKind kind {
        SystemVerilogConstraintTemplateKind::Constant
    };
    std::string text;
    PackedLogic4 constant;
    SystemVerilogConstraintVariableProfile profile;
    std::vector<SystemVerilogConstraintTemplate> operands;
    // A Method node that is a cast (IEEE 1800-2023 6.24.1): the target kind,
    // its built-in type, and the width of a size or resolved typedef cast.
    support::SystemVerilogCastTarget cast_target {
        support::SystemVerilogCastTarget::none
    };
    support::SystemVerilogBuiltinType cast_builtin {
        support::SystemVerilogBuiltinType::none
    };
    std::size_t cast_width { };
    // A DistributionItem: whether its weight is `:=` or `:/`.
    SystemVerilogConstraintDistributionWeight weight_kind {
        SystemVerilogConstraintDistributionWeight::PerValue
    };
    SystemVerilogConstraintTemplateRole role {
        SystemVerilogConstraintTemplateRole::none
    };
    std::size_t slot { };
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
  /// The operand converted to `profile`: truncated, or extended by its own
  /// signedness (a cast, IEEE 1800-2017 6.24.1).
  [[nodiscard]] SystemVerilogConstraintExpressionId resize(
      SystemVerilogConstraintExpressionId operand,
      SystemVerilogConstraintVariableProfile profile);
  [[nodiscard]] const SystemVerilogConstraintVariableProfile& profile(
      SystemVerilogConstraintExpressionId expression) const;
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

/// As above, with each unassigned variable read as all X. Evaluation is
/// X-pessimistic, so a known result holds for every completion.
[[nodiscard]] PackedLogic4 evaluate_systemverilog_constraint_expression_partially(
    const SystemVerilogConstraintExpression& expression,
    const SystemVerilogConstraintAssignment& assignment);

/// An inclusive range of value keys. A key is a value's bits with the sign
/// bit inverted for a signed variable, so keys order like the values.
struct SystemVerilogConstraintKeyRange {
  std::uint64_t low{};
  std::uint64_t high{};
};

/// The values of one variable (1 to 64 bits wide) that can satisfy a
/// predicate, as sorted disjoint key ranges. Comparisons of the variable
/// with constants, combined by `&&`, `||` and `!`, narrow the set; any other
/// term is treated as satisfiable, so the result may be a superset.
[[nodiscard]] std::vector<SystemVerilogConstraintKeyRange>
systemverilog_constraint_key_ranges(
    const SystemVerilogConstraintExpression& expression,
    SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile);

/// A solver domain for a 1- to 64-bit variable from the clauses that depend
/// on it, in ascending value order. The hard clauses' comparisons with
/// constants bound it; within those bounds it holds every value when there
/// are at most 4096, otherwise each range's ends and a uniform sample drawn
/// with `random`, and likewise the values each soft clause prefers. It is
/// empty when the hard clauses exclude every value.
[[nodiscard]] std::vector<PackedLogic4> systemverilog_constraint_variable_domain(
    std::span<const SystemVerilogConstraintClause> clauses,
    SystemVerilogConstraintVariableId variable,
    const SystemVerilogConstraintVariableProfile& profile,
    const std::function<std::uint64_t()>& random);

[[nodiscard]] SystemVerilogConstraintClause
systemverilog_constraint_expression_clause(
    std::string canonical_identity,
    std::vector<SystemVerilogConstraintVariableId> variables,
    SystemVerilogConstraintExpression expression);

/// How the language index of one container in a solve maps to the position
/// of its element: a fixed-size array counts from its left bound, an
/// associative array lists its keys in element order (IEEE 1800-2017 7.4,
/// 7.8); a dynamic array or queue indexes positions directly.
struct SystemVerilogConstraintContainerShape {
  bool fixed{};
  std::int64_t left{};
  bool descending{};
  bool associative{};
  bool string_keys{};
  std::vector<PackedLogic4> keys;
  std::vector<std::string> string_key_values;
};

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
  // `disable soft v` (IEEE 1800-2017 18.5.14.2): drops the soft clauses
  // added so far that depend on one of the variables.
  void discard_soft_clauses(std::span<const SystemVerilogConstraintVariableId> variables);
  /// The predicate's value when every variable it reads has one value in
  /// its domain (state variables); nothing when one is random.
  [[nodiscard]] std::optional<bool> fixed_truth(
      const SystemVerilogConstraintExpression& predicate) const;
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
  // The value ranges of every distribution over `variable`.
  [[nodiscard]] std::vector<SystemVerilogConstraintDistributionEntry>
  distribution_entries(SystemVerilogConstraintVariableId variable) const;
  [[nodiscard]] const SystemVerilogConstraintSolverLimits& limits()
      const noexcept {
    return limits_;
  }
  /// The shape of the container whose elements are `path[0]`, `path[1]`,
  /// ...; a container without one indexes its positions directly.
  void set_container_shape(
      std::string path, SystemVerilogConstraintContainerShape shape) {
    container_shapes_.insert_or_assign(std::move(path), std::move(shape));
  }
  [[nodiscard]] const SystemVerilogConstraintContainerShape* container_shape(
      std::string_view path) const {
    const auto found = container_shapes_.find(path);
    return found == container_shapes_.end() ? nullptr : &found->second;
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
    std::vector<SystemVerilogConstraintDistributionEntry> entries;
    std::shared_ptr<const SystemVerilogConstraintExpression> guard{};
  };
  std::vector<RetainedDistribution> distributions_;
  std::vector<std::pair<SystemVerilogConstraintVariableId,
                        SystemVerilogConstraintVariableId>> solve_before_;
  std::size_t domain_values_{};
  std::map<std::string, SystemVerilogConstraintContainerShape, std::less<>>
      container_shapes_;
};

/// Bind and register one source-ordered inline constraint block. The identity
/// prefix is diagnostic/cache provenance, not a language-width limit.
void configure_systemverilog_inline_constraints(
    SystemVerilogConstraintSolver& solver,
    const SystemVerilogConstraintVariables& variables,
    std::span<const SystemVerilogConstraintTemplate> expressions,
    std::string_view identity_prefix);

}  // namespace fsim::runtime
