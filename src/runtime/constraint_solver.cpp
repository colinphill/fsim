// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/constraint_solver.hpp"

#include <algorithm>
#include <numeric>
#include <compare>
#include <span>
#include <utility>
#include <vector>

namespace fsim::runtime {

namespace {

// Whether a domain repeats a value: sorted by planes, O(n log n), so a
// 16-bit randc domain is checked in milliseconds.
[[nodiscard]] bool domain_has_duplicate(
    const std::span<const PackedLogic4> domain) {
  std::vector<std::size_t> order(domain.size());
  std::iota(order.begin(), order.end(), std::size_t{});
  const auto planes = [&](const std::size_t index) {
    return std::pair{domain[index].aval_words(), domain[index].bval_words()};
  };
  const auto less = [&](const std::size_t left, const std::size_t right) {
    const auto [left_aval, left_bval] = planes(left);
    const auto [right_aval, right_bval] = planes(right);
    if (domain[left].width() != domain[right].width()) {
      return domain[left].width() < domain[right].width();
    }
    if (const auto order_aval = std::lexicographical_compare_three_way(
            left_aval.begin(), left_aval.end(),
            right_aval.begin(), right_aval.end());
        order_aval != 0) {
      return order_aval < 0;
    }
    return std::lexicographical_compare(left_bval.begin(), left_bval.end(),
        right_bval.begin(), right_bval.end());
  };
  std::ranges::sort(order, less);
  return std::ranges::adjacent_find(order,
             [&](const std::size_t left, const std::size_t right) {
               return domain[left] == domain[right];
             })
      != order.end();
}

[[nodiscard]] bool elapsed(
    const SystemVerilogConstraintSolverLimits& limits,
    const SystemVerilogConstraintSolver::Clock& clock,
    const std::chrono::steady_clock::time_point start) {
  if (limits.maximum_elapsed_work
      == std::chrono::steady_clock::duration::max()) {
    return false;
  }
  const auto current = clock();
  return current >= start
      && current - start >= limits.maximum_elapsed_work;
}

[[nodiscard]] SystemVerilogConstraintSolveResult exhausted(
    const SystemVerilogConstraintResource resource,
    const std::uint64_t search_steps,
    const std::uint64_t clause_evaluations) {
  return {
      SystemVerilogConstraintSolveStatus::ResourceExhausted,
      resource,
      {},
      search_steps,
      clause_evaluations};
}

[[nodiscard]] bool known(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    const auto bit = value.get(index);
    if (bit == Logic4::x || bit == Logic4::z) return false;
  }
  return true;
}

[[nodiscard]] int compare(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const bool signed_value) {
  if (signed_value) {
    const auto left_negative = left.get(left.width() - 1U) == Logic4::one;
    const auto right_negative = right.get(right.width() - 1U) == Logic4::one;
    if (left_negative != right_negative) return left_negative ? -1 : 1;
  }
  for (std::size_t offset = 0; offset < left.width(); ++offset) {
    const auto index = left.width() - offset - 1U;
    if (left.get(index) == right.get(index)) continue;
    return left.get(index) == Logic4::one ? 1 : -1;
  }
  return 0;
}

[[nodiscard]] std::uint64_t mixed(std::uint64_t value) noexcept {
  value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t identity_hash(
    const std::string_view identity) noexcept {
  auto value = UINT64_C(14695981039346656037);
  for (const auto character : identity) {
    value ^= static_cast<std::uint8_t>(character);
    value *= UINT64_C(1099511628211);
  }
  return value;
}

}  // namespace

std::size_t SystemVerilogConstraintAssignment::size() const noexcept {
  return variables_.size();
}

bool SystemVerilogConstraintAssignment::assigned(
    const SystemVerilogConstraintVariableId id) const {
  if (id >= values_.size()) {
    throw std::out_of_range{"constraint variable id is out of range"};
  }
  return values_[id].has_value();
}

const PackedLogic4& SystemVerilogConstraintAssignment::value(
    const SystemVerilogConstraintVariableId id) const {
  if (id >= values_.size()) {
    throw std::out_of_range{"constraint variable id is out of range"};
  }
  if (!values_[id]) {
    throw std::logic_error{"constraint variable is not assigned"};
  }
  return *values_[id];
}

const SystemVerilogConstraintVariableProfile&
SystemVerilogConstraintAssignment::profile(
    const SystemVerilogConstraintVariableId id) const {
  if (id >= variables_.size()) {
    throw std::out_of_range{"constraint variable id is out of range"};
  }
  return variables_[id].profile;
}

std::optional<SystemVerilogConstraintVariableId>
SystemVerilogConstraintAssignment::find(
    const std::string_view canonical_identity) const noexcept {
  for (std::size_t index = 0; index < variables_.size(); ++index) {
    if (variables_[index].canonical_identity == canonical_identity) {
      return index;
    }
  }
  return std::nullopt;
}

SystemVerilogConstraintResourceError::SystemVerilogConstraintResourceError(
    const SystemVerilogConstraintResource resource,
    std::string message)
    : std::length_error(std::move(message)), resource_(resource) {}

SystemVerilogConstraintSolver::SystemVerilogConstraintSolver(
    const SystemVerilogConstraintSolverLimits limits,
    Clock clock)
    : limits_(limits), clock_(std::move(clock)) {
  if (!clock_) {
    throw std::invalid_argument{"constraint solver requires a clock"};
  }
  if (limits_.maximum_elapsed_work
      < std::chrono::steady_clock::duration::zero()) {
    throw std::invalid_argument{
        "constraint solver elapsed-work budget must not be negative"};
  }
}

SystemVerilogConstraintVariableId
SystemVerilogConstraintSolver::add_variable(
    SystemVerilogConstraintVariable variable) {
  if (variables_.size() >= limits_.maximum_variables) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::Variables,
        "constraint solver variable budget exceeded"};
  }
  if (variable.canonical_identity.empty()
      || variable.profile.width == 0
      || variable.profile.nominal_type.empty()
      || variable.domain.empty()) {
    throw std::invalid_argument{
        "constraint variable requires an identity, exact profile, and finite domain"};
  }
  if (std::ranges::any_of(
          variables_, [&](const auto& existing) {
            return existing.canonical_identity
                == variable.canonical_identity;
          })) {
    throw std::invalid_argument{
        "duplicate constraint variable '" + variable.canonical_identity + "'"};
  }
  for (const auto& value : variable.domain) {
    if (value.width() != variable.profile.width || value.is_logic9()) {
      throw std::invalid_argument{
          "constraint domain value does not match its exact profile"};
    }
  }
  if (domain_has_duplicate(variable.domain)) {
    throw std::invalid_argument{
        "constraint variable domain contains a duplicate value"};
  }
  if (variable.domain.size()
      > limits_.maximum_domain_values - domain_values_) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "constraint solver domain-value budget exceeded"};
  }
  const auto id = variables_.size();
  domain_values_ += variable.domain.size();
  variables_.push_back(std::move(variable));
  return id;
}

void SystemVerilogConstraintSolver::replace_domain(
    const SystemVerilogConstraintVariableId variable,
    std::vector<PackedLogic4> domain) {
  if (variable >= variables_.size() || domain.empty()) {
    throw std::invalid_argument{
        "constraint domain replacement requires a known variable and finite domain"};
  }
  if (std::ranges::any_of(
          distributions_, [=](const auto& distribution) {
            return distribution.variable == variable;
          })) {
    throw std::invalid_argument{
        "constraint domain cannot change after distribution registration"};
  }
  const auto& retained = variables_[variable];
  if (std::ranges::any_of(domain, [&](const PackedLogic4& value) {
        return value.width() != retained.profile.width || value.is_logic9();
      })
      || domain_has_duplicate(domain)) {
    throw std::invalid_argument{
        "replacement constraint domain does not match its exact profile"};
  }
  const auto prior_size = retained.domain.size();
  if (domain.size() > prior_size
      && domain.size() - prior_size
          > limits_.maximum_domain_values - domain_values_) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "replacement constraint domain exceeds the domain-value budget"};
  }
  domain_values_ -= prior_size;
  domain_values_ += domain.size();
  variables_[variable].domain = std::move(domain);
}

void SystemVerilogConstraintSolver::discard_soft_clauses(
    const std::span<const SystemVerilogConstraintVariableId> variables) {
  std::erase_if(clauses_, [&](const SystemVerilogConstraintClause& clause) {
    return clause.soft
        && std::ranges::any_of(clause.variables, [&](const auto variable) {
             return std::ranges::find(variables, variable) != variables.end();
           });
  });
}

std::optional<bool> SystemVerilogConstraintSolver::fixed_truth(
    const SystemVerilogConstraintExpression& predicate) const {
  std::vector<std::optional<PackedLogic4>> values(variables_.size());
  for (const auto& node : predicate.nodes) {
    if (node.operation != SystemVerilogConstraintExpressionOperator::Variable) {
      continue;
    }
    if (node.variable >= variables_.size()
        || variables_[node.variable].domain.size() != 1U) {
      return std::nullopt;
    }
    values[node.variable] = variables_[node.variable].domain.front();
  }
  const SystemVerilogConstraintAssignment assignment{variables_, values};
  const auto result = evaluate_systemverilog_constraint_expression(
      predicate, assignment);
  if (result.width() != 1U || result.get(0) == Logic4::x
      || result.get(0) == Logic4::z) {
    return std::nullopt;
  }
  return result.get(0) == Logic4::one;
}

void SystemVerilogConstraintSolver::add_clause(
    SystemVerilogConstraintClause clause) {
  if (clauses_.size() >= limits_.maximum_clauses) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::Clauses,
        "constraint solver clause budget exceeded"};
  }
  if (clause.canonical_identity.empty() || !clause.evaluate) {
    throw std::invalid_argument{
        "constraint clause requires an identity and evaluator"};
  }
  if (std::ranges::any_of(
          clauses_, [&](const auto& existing) {
            return existing.canonical_identity == clause.canonical_identity;
          })) {
    throw std::invalid_argument{
        "duplicate constraint clause '" + clause.canonical_identity + "'"};
  }
  std::vector<SystemVerilogConstraintVariableId> seen;
  seen.reserve(clause.variables.size());
  for (const auto variable : clause.variables) {
    if (variable >= variables_.size()) {
      throw std::invalid_argument{
          "constraint clause references an unknown variable"};
    }
    if (std::ranges::find(seen, variable) != seen.end()) {
      throw std::invalid_argument{
          "constraint clause repeats a variable dependency"};
    }
    seen.push_back(variable);
  }
  clauses_.push_back(std::move(clause));
}

void SystemVerilogConstraintSolver::add_distribution(
    SystemVerilogConstraintDistribution distribution) {
  if (distribution.canonical_identity.empty()
      || distribution.variable >= variables_.size()
      || distribution.entries.empty()) {
    throw std::invalid_argument{
        "constraint distribution requires an identity, variable, and entries"};
  }
  if (std::ranges::any_of(
          distributions_, [&](const auto& existing) {
            return existing.canonical_identity
                == distribution.canonical_identity;
          })) {
    throw std::invalid_argument{
        "duplicate constraint distribution '"
        + distribution.canonical_identity + "'"};
  }
  const auto& variable = variables_[distribution.variable];
  std::vector<std::size_t> matches(distribution.entries.size());
  std::uint64_t denominator{1};
  for (std::size_t entry_index = 0;
       entry_index < distribution.entries.size(); ++entry_index) {
    const auto& entry = distribution.entries[entry_index];
    // A zero weight is legal and gives its values no probability (IEEE
    // 1800-2017 18.5.4).
    if (entry.low.width() != variable.profile.width
        || entry.high.width() != variable.profile.width
        || !known(entry.low) || !known(entry.high)
        || compare(entry.low, entry.high, variable.profile.signed_value) > 0) {
      throw std::invalid_argument{
          "constraint distribution entry has an invalid range or weight"};
    }
    for (const auto& value : variable.domain) {
      if (known(value)
          && compare(value, entry.low, variable.profile.signed_value) >= 0
          && compare(value, entry.high, variable.profile.signed_value) <= 0) {
        ++matches[entry_index];
      }
    }
    if (entry.weight_kind
            == SystemVerilogConstraintDistributionWeight::AcrossRange
        && matches[entry_index] != 0U) {
      const auto divisor = static_cast<std::uint64_t>(matches[entry_index]);
      const auto common = std::gcd(denominator, divisor);
      if (denominator > std::numeric_limits<std::uint64_t>::max()
              / (divisor / common)) {
        throw std::overflow_error{
            "constraint distribution denominator overflows"};
      }
      denominator *= divisor / common;
    }
  }
  std::vector<std::uint64_t> weights(variable.domain.size());
  for (std::size_t entry_index = 0;
       entry_index < distribution.entries.size(); ++entry_index) {
    const auto& entry = distribution.entries[entry_index];
    if (matches[entry_index] == 0U) continue;
    const auto divisor = entry.weight_kind
            == SystemVerilogConstraintDistributionWeight::AcrossRange
        ? static_cast<std::uint64_t>(matches[entry_index]) : UINT64_C(1);
    const auto scale = denominator / divisor;
    if (entry.weight > std::numeric_limits<std::uint64_t>::max() / scale) {
      throw std::overflow_error{
          "constraint distribution weight normalization overflows"};
    }
    const auto contribution = entry.weight * scale;
    for (std::size_t value_index = 0;
         value_index < variable.domain.size(); ++value_index) {
      const auto& value = variable.domain[value_index];
      if (!known(value)
          || compare(value, entry.low, variable.profile.signed_value) < 0
          || compare(value, entry.high, variable.profile.signed_value) > 0) {
        continue;
      }
      if (contribution > std::numeric_limits<std::uint64_t>::max()
              - weights[value_index]) {
        throw std::overflow_error{
            "overlapping constraint distribution weights overflow"};
      }
      weights[value_index] += contribution;
    }
  }
  distributions_.push_back({
      std::move(distribution.canonical_identity),
      distribution.variable,
      std::move(weights),
      std::move(distribution.entries),
      std::move(distribution.guard)});
}

std::vector<SystemVerilogConstraintDistributionEntry>
SystemVerilogConstraintSolver::distribution_entries(
    const SystemVerilogConstraintVariableId variable) const {
  std::vector<SystemVerilogConstraintDistributionEntry> result;
  for (const auto& distribution : distributions_) {
    if (distribution.variable == variable) {
      result.insert(result.end(), distribution.entries.begin(),
          distribution.entries.end());
    }
  }
  return result;
}

std::span<const std::uint64_t>
SystemVerilogConstraintSolver::distribution_weights(
    const std::string_view canonical_identity) const {
  const auto found = std::ranges::find(
      distributions_, canonical_identity,
      &RetainedDistribution::canonical_identity);
  if (found == distributions_.end()) {
    throw std::out_of_range{
        "constraint distribution is not registered"};
  }
  return found->weights;
}

void SystemVerilogConstraintSolver::add_solve_before(
    const SystemVerilogConstraintVariableId earlier,
    const SystemVerilogConstraintVariableId later) {
  if (earlier >= variables_.size() || later >= variables_.size()
      || earlier == later) {
    throw std::invalid_argument{
        "solve-before requires two distinct registered variables"};
  }
  if (std::ranges::find(
          solve_before_, std::pair{earlier, later})
      != solve_before_.end()) {
    return;
  }
  std::vector<SystemVerilogConstraintVariableId> pending{later};
  std::vector<bool> visited(variables_.size());
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    if (current == earlier) {
      throw std::invalid_argument{
          "solve-before dependency cycle reaches '"
          + variables_[earlier].canonical_identity + "'"};
    }
    if (visited[current]) continue;
    visited[current] = true;
    for (const auto& edge : solve_before_) {
      if (edge.first == current) pending.push_back(edge.second);
    }
  }
  solve_before_.emplace_back(earlier, later);
}

std::vector<SystemVerilogConstraintVariableId>
SystemVerilogConstraintSolver::search_order() const {
  std::vector<std::vector<SystemVerilogConstraintVariableId>> outgoing(
      variables_.size());
  std::vector<std::size_t> indegree(variables_.size());
  for (const auto& [earlier, later] : solve_before_) {
    outgoing[earlier].push_back(later);
    ++indegree[later];
  }
  const auto less_identity = [&](const auto left, const auto right) {
    if (variables_[left].canonical_identity
        != variables_[right].canonical_identity) {
      return variables_[left].canonical_identity
          < variables_[right].canonical_identity;
    }
    return left < right;
  };
  std::vector<SystemVerilogConstraintVariableId> ready;
  for (std::size_t variable = 0; variable < variables_.size(); ++variable) {
    if (indegree[variable] == 0U) ready.push_back(variable);
  }
  std::ranges::sort(ready, less_identity);
  std::vector<SystemVerilogConstraintVariableId> result;
  result.reserve(variables_.size());
  while (!ready.empty()) {
    const auto selected = ready.front();
    ready.erase(ready.begin());
    result.push_back(selected);
    for (const auto later : outgoing[selected]) {
      if (--indegree[later] == 0U) {
        ready.push_back(later);
        std::ranges::sort(ready, less_identity);
      }
    }
  }
  if (result.size() != variables_.size()) {
    throw std::logic_error{"constraint solve-before graph is cyclic"};
  }
  return result;
}

SystemVerilogConstraintSolveResult
SystemVerilogConstraintSolver::solve(const std::uint64_t selection) const {
  const auto start = clock_();
  std::uint64_t search_steps{};
  std::uint64_t clause_evaluations{};
  std::vector<std::optional<PackedLogic4>> assigned_values(
      variables_.size());
  const SystemVerilogConstraintAssignment assignment{
      variables_, assigned_values};
  using State = SystemVerilogConstraintClauseState;
  struct Evaluation {
    State hard{State::Satisfied};
    std::vector<State> soft;
  };
  const auto evaluate = [&]() {
    Evaluation result;
    for (const auto& clause : clauses_) {
      ++clause_evaluations;
      const auto clause_state = clause.evaluate(assignment);
      if (clause.soft) {
        result.soft.push_back(clause_state);
        continue;
      }
      if (clause_state == State::Violated) {
        result.hard = clause_state;
        return result;
      }
      if (clause_state == State::Undetermined) {
        result.hard = clause_state;
      }
    }
    return result;
  };

  if (elapsed(limits_, clock_, start)) {
    return exhausted(
        SystemVerilogConstraintResource::ElapsedWork,
        search_steps,
        clause_evaluations);
  }
  if (variables_.empty()) {
    const auto evaluation = evaluate();
    if (evaluation.hard == SystemVerilogConstraintClauseState::Undetermined
        || std::ranges::find(
               evaluation.soft,
               SystemVerilogConstraintClauseState::Undetermined)
            != evaluation.soft.end()) {
      throw std::logic_error{
          "constraint clause remained undetermined for a complete assignment"};
    }
    return {
        evaluation.hard == SystemVerilogConstraintClauseState::Satisfied
            ? SystemVerilogConstraintSolveStatus::Satisfied
            : SystemVerilogConstraintSolveStatus::Unsatisfiable,
        SystemVerilogConstraintResource::None,
        {},
        search_steps,
        clause_evaluations};
  }

  // A variable's values in search order: drawn without replacement, each in
  // proportion to the product of its distribution weights. A guarded
  // distribution contributes only where its guard holds under the current
  // assignment.
  const auto guard_holds = [&](const RetainedDistribution& distribution) {
    if (!distribution.guard) return true;
    for (const auto& node : distribution.guard->nodes) {
      if (node.operation == SystemVerilogConstraintExpressionOperator::Variable
          && (node.variable >= assigned_values.size()
              || !assigned_values[node.variable])) {
        return false;
      }
    }
    const auto value = evaluate_systemverilog_constraint_expression(
        *distribution.guard, assignment);
    return value.width() == 1U && value.get(0) == Logic4::one;
  };
  const auto value_order = [&](const std::size_t variable,
                               const bool guarded) {
    std::vector<std::size_t> order;
    std::vector<std::uint64_t> weights(variables_[variable].domain.size(), 1);
    bool weighted{};
    for (const auto& distribution : distributions_) {
      if (distribution.variable != variable) continue;
      if (distribution.guard && (!guarded || !guard_holds(distribution))) {
        continue;
      }
      weighted = true;
      for (std::size_t index = 0; index < weights.size(); ++index) {
        if (weights[index] != 0 && distribution.weights[index]
            > std::numeric_limits<std::uint64_t>::max() / weights[index]) {
          throw std::overflow_error{
              "composed constraint distribution weights overflow"};
        }
        weights[index] *= distribution.weights[index];
      }
    }
    if (!weighted) {
      order.resize(weights.size());
      std::iota(order.begin(), order.end(), 0U);
      return order;
    }
    // A Fenwick tree over the weights finds each draw in logarithmic time.
    const auto count = weights.size();
    std::vector<std::uint64_t> tree(count + 1U);
    std::uint64_t total{};
    std::size_t remaining{};
    for (std::size_t index = 0; index < count; ++index) {
      if (weights[index] == 0) continue;
      if (weights[index] > std::numeric_limits<std::uint64_t>::max() - total) {
        throw std::overflow_error{
            "constraint distribution total weight overflows"};
      }
      total += weights[index];
      ++remaining;
      for (auto node = index + 1U; node <= count; node += node & (~node + 1U)) {
        tree[node] += weights[index];
      }
    }
    std::size_t top = 1U;
    while (top * 2U <= count) top *= 2U;
    auto state = mixed(
        selection ^ identity_hash(variables_[variable].canonical_identity));
    order.reserve(remaining);
    for (; remaining > 0U; --remaining) {
      auto target = state % total;
      std::size_t position{};
      for (auto step = top; step > 0U; step /= 2U) {
        if (position + step <= count && tree[position + step] <= target) {
          position += step;
          target -= tree[position];
        }
      }
      order.push_back(position);
      const auto weight = weights[position];
      total -= weight;
      weights[position] = 0;
      for (auto node = position + 1U; node <= count;
           node += node & (~node + 1U)) {
        tree[node] -= weight;
      }
      state = mixed(state + UINT64_C(0x9e3779b97f4a7c15));
    }
    return order;
  };
  std::vector<std::vector<std::size_t>> domain_order(variables_.size());
  std::vector<bool> guarded_variable(variables_.size());
  for (std::size_t variable = 0; variable < variables_.size(); ++variable) {
    domain_order[variable] = value_order(variable, false);
  }
  for (const auto& distribution : distributions_) {
    if (distribution.guard && distribution.variable < variables_.size()) {
      guarded_variable[distribution.variable] = true;
    }
  }
  // Assigning a variable can change only the clauses that read it; a clause
  // with no recorded variables is checked at every step.
  std::vector<std::vector<std::size_t>> clauses_of(variables_.size());
  std::vector<std::size_t> unbound_clauses;
  for (std::size_t index = 0; index < clauses_.size(); ++index) {
    const auto& clause = clauses_[index];
    if (clause.variables.empty()) {
      unbound_clauses.push_back(index);
      continue;
    }
    for (const auto variable : clause.variables) {
      if (variable < variables_.size()
          && (clauses_of[variable].empty()
              || clauses_of[variable].back() != index)) {
        clauses_of[variable].push_back(index);
      }
    }
  }
  // The values a variable may take: a hard clause of that variable alone
  // rejects the others before the search. The search visits the remaining
  // values in the same order, so it finds the same assignment.
  auto candidates = std::move(domain_order);
  for (const auto& clause : clauses_) {
    if (clause.soft || clause.variables.empty()
        || clause.variables.front() >= variables_.size()
        || !std::ranges::all_of(clause.variables, [&](const auto variable) {
             return variable == clause.variables.front();
           })) {
      continue;
    }
    const auto variable = clause.variables.front();
    std::erase_if(candidates[variable], [&](const std::size_t index) {
      assigned_values[variable] = variables_[variable].domain[index];
      ++clause_evaluations;
      const bool rejected = clause.evaluate(assignment) == State::Violated;
      assigned_values[variable].reset();
      return rejected;
    });
  }
  // A variable left one value is assigned first; that changes no other
  // variable's order, so the search finds the same assignment.
  auto variable_order = search_order();
  std::ranges::stable_partition(variable_order, [&](const auto variable) {
    return candidates[variable].size() == 1U;
  });
  // A variable with a guarded distribution follows the guard's variables,
  // so its weights are known when it is assigned.
  for (const auto& distribution : distributions_) {
    if (!distribution.guard) continue;
    const auto variable = distribution.variable;
    auto position = std::ranges::find(variable_order, variable);
    if (position == variable_order.end()) continue;
    std::ptrdiff_t last = -1;
    for (const auto& node : distribution.guard->nodes) {
      if (node.operation != SystemVerilogConstraintExpressionOperator::Variable) {
        continue;
      }
      const auto guard_position = std::ranges::find(
          variable_order, node.variable);
      last = std::max(last, std::distance(
          variable_order.begin(), guard_position));
    }
    const auto index = std::distance(variable_order.begin(), position);
    if (last > index) {
      variable_order.erase(position);
      variable_order.insert(variable_order.begin() + last, variable);
    }
  }
  // The values a guarded variable may take, for reordering during search.
  std::vector<std::vector<bool>> allowed(variables_.size());
  for (std::size_t variable = 0; variable < variables_.size(); ++variable) {
    if (!guarded_variable[variable]) continue;
    allowed[variable].assign(variables_[variable].domain.size(), false);
    for (const auto index : candidates[variable]) {
      allowed[variable][index] = true;
    }
  }

  // Clauses the search enforces: every hard clause, and the soft clauses
  // already found satisfiable together with them.
  std::vector<bool> enforced(clauses_.size());
  for (std::size_t index = 0; index < clauses_.size(); ++index) {
    enforced[index] = !clauses_[index].soft;
  }
  // A clause left with one unassigned variable must allow one of its
  // values; otherwise no completion of the assignment satisfies it.
  constexpr std::size_t forward_check_values = 4096U;
  const auto supported = [&](const SystemVerilogConstraintClause& clause) {
    std::optional<SystemVerilogConstraintVariableId> open;
    for (const auto variable : clause.variables) {
      if (variable >= variables_.size() || assigned_values[variable]) {
        continue;
      }
      if (open && *open != variable) return true;
      open = variable;
    }
    if (!open || candidates[*open].size() > forward_check_values) {
      return true;
    }
    bool found{};
    for (const auto index : candidates[*open]) {
      assigned_values[*open] = variables_[*open].domain[index];
      ++clause_evaluations;
      if (clause.evaluate(assignment) != State::Violated) {
        found = true;
        break;
      }
    }
    assigned_values[*open].reset();
    return found;
  };
  const auto violated_after = [&](const SystemVerilogConstraintVariableId
                                      assigned) {
    for (const auto index : unbound_clauses) {
      if (!enforced[index]) continue;
      ++clause_evaluations;
      if (clauses_[index].evaluate(assignment) == State::Violated) {
        return true;
      }
    }
    for (const auto index : clauses_of[assigned]) {
      if (!enforced[index]) continue;
      const auto& clause = clauses_[index];
      ++clause_evaluations;
      const auto state = clause.evaluate(assignment);
      if (state == State::Violated) return true;
      if (state == State::Undetermined && !supported(clause)) return true;
    }
    return false;
  };
  enum class Outcome : std::uint8_t { Found, Unsatisfiable, Exhausted };
  auto exhausted_resource = SystemVerilogConstraintResource::None;
  // The first assignment, in search order, meeting every enforced clause.
  const auto search = [&]() -> Outcome {
    for (auto& value : assigned_values) value.reset();
    std::vector<std::size_t> next_index(variables_.size());
    std::size_t depth{};
    while (true) {
      if (elapsed(limits_, clock_, start)) {
        exhausted_resource = SystemVerilogConstraintResource::ElapsedWork;
        return Outcome::Exhausted;
      }
      const auto variable = variable_order[depth];
      if (next_index[depth] == 0U && guarded_variable[variable]) {
        auto order = value_order(variable, true);
        std::erase_if(order, [&](const std::size_t index) {
          return !allowed[variable][index];
        });
        candidates[variable] = std::move(order);
      }
      if (next_index[depth] >= candidates[variable].size()) {
        next_index[depth] = 0;
        assigned_values[variable].reset();
        if (depth == 0) return Outcome::Unsatisfiable;
        --depth;
        continue;
      }
      if (search_steps >= limits_.maximum_search_steps) {
        exhausted_resource = SystemVerilogConstraintResource::SearchSteps;
        return Outcome::Exhausted;
      }
      assigned_values[variable] = variables_[variable].domain[
          candidates[variable][next_index[depth]++]];
      ++search_steps;
      if (violated_after(variable)) continue;
      if (depth + 1U < variables_.size()) {
        ++depth;
        continue;
      }
      bool satisfied = true;
      for (std::size_t index = 0; index < clauses_.size(); ++index) {
        ++clause_evaluations;
        const auto state = clauses_[index].evaluate(assignment);
        if (state == State::Undetermined) {
          throw std::logic_error{
              "constraint clause remained undetermined for a complete "
              "assignment"};
        }
        if (enforced[index] && state == State::Violated) satisfied = false;
      }
      if (satisfied) return Outcome::Found;
    }
  };
  const auto retain_values = [&]() {
    std::vector<PackedLogic4> values;
    values.reserve(assigned_values.size());
    for (const auto& value : assigned_values) values.push_back(*value);
    return values;
  };
  const auto finish = [&](const Outcome outcome,
                          std::vector<PackedLogic4> values)
      -> SystemVerilogConstraintSolveResult {
    if (outcome == Outcome::Exhausted) {
      return exhausted(exhausted_resource, search_steps, clause_evaluations);
    }
    return {
        outcome == Outcome::Found
            ? SystemVerilogConstraintSolveStatus::Satisfied
            : SystemVerilogConstraintSolveStatus::Unsatisfiable,
        SystemVerilogConstraintResource::None,
        std::move(values),
        search_steps,
        clause_evaluations};
  };
  auto outcome = search();
  if (outcome != Outcome::Found) return finish(outcome, {});
  auto best = retain_values();
  // Soft constraints are met in priority order, the later declared first
  // (IEEE 1800-2017 18.5.14.1): each joins the enforced set when the
  // set stays satisfiable. The first assignment meeting the final set is
  // the first one meeting the most preferred soft constraints.
  for (std::size_t offset = 0; offset < clauses_.size(); ++offset) {
    const auto index = clauses_.size() - offset - 1U;
    if (!clauses_[index].soft) continue;
    for (std::size_t variable = 0; variable < variables_.size(); ++variable) {
      assigned_values[variable] = best[variable];
    }
    ++clause_evaluations;
    if (clauses_[index].evaluate(assignment) == State::Satisfied) {
      enforced[index] = true;
      continue;
    }
    enforced[index] = true;
    outcome = search();
    if (outcome == Outcome::Found) {
      best = retain_values();
    } else if (outcome == Outcome::Unsatisfiable) {
      enforced[index] = false;
    } else {
      return finish(outcome, {});
    }
  }
  return finish(Outcome::Found, std::move(best));
}

}  // namespace fsim::runtime
