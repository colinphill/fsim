// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/constraint_solver.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

namespace fsim::runtime {

namespace {

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
  for (std::size_t index = 0; index < variable.domain.size(); ++index) {
    if (variable.domain[index].width() != variable.profile.width
        || variable.domain[index].is_logic9()) {
      throw std::invalid_argument{
          "constraint domain value does not match its exact profile"};
    }
    if (std::ranges::find(
            std::span{variable.domain}.first(index),
            variable.domain[index])
        != std::span{variable.domain}.first(index).end()) {
      throw std::invalid_argument{
          "constraint variable domain contains a duplicate value"};
    }
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
  for (std::size_t index = 0; index < domain.size(); ++index) {
    if (domain[index].width() != retained.profile.width
        || domain[index].is_logic9()
        || std::ranges::find(
               std::span{domain}.first(index), domain[index])
            != std::span{domain}.first(index).end()) {
      throw std::invalid_argument{
          "replacement constraint domain does not match its exact profile"};
    }
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
    if (entry.weight == 0 || entry.low.width() != variable.profile.width
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
      std::move(weights)});
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
  struct Evaluation {
    SystemVerilogConstraintClauseState hard{
        SystemVerilogConstraintClauseState::Satisfied};
    std::vector<SystemVerilogConstraintClauseState> soft;
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
      if (clause_state == SystemVerilogConstraintClauseState::Violated) {
        result.hard = clause_state;
        return result;
      }
      if (clause_state == SystemVerilogConstraintClauseState::Undetermined) {
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

  const auto variable_order = search_order();
  std::vector<std::vector<std::size_t>> domain_order(variables_.size());
  for (std::size_t variable = 0; variable < variables_.size(); ++variable) {
    std::vector<std::uint64_t> weights(variables_[variable].domain.size(), 1);
    bool weighted{};
    for (const auto& distribution : distributions_) {
      if (distribution.variable != variable) continue;
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
      domain_order[variable].resize(weights.size());
      std::iota(
          domain_order[variable].begin(),
          domain_order[variable].end(), 0U);
      continue;
    }
    std::vector<std::size_t> remaining;
    for (std::size_t index = 0; index < weights.size(); ++index) {
      if (weights[index] != 0) remaining.push_back(index);
    }
    auto state = mixed(
        selection ^ identity_hash(variables_[variable].canonical_identity));
    while (!remaining.empty()) {
      std::uint64_t total{};
      for (const auto index : remaining) {
        if (weights[index]
            > std::numeric_limits<std::uint64_t>::max() - total) {
          throw std::overflow_error{
              "constraint distribution total weight overflows"};
        }
        total += weights[index];
      }
      auto target = state % total;
      auto selected = remaining.begin();
      for (; selected != remaining.end(); ++selected) {
        if (target < weights[*selected]) break;
        target -= weights[*selected];
      }
      domain_order[variable].push_back(*selected);
      remaining.erase(selected);
      state = mixed(state + UINT64_C(0x9e3779b97f4a7c15));
    }
  }
  std::vector<std::size_t> next_domain_index(variables_.size());
  std::vector<PackedLogic4> best_values;
  std::vector<SystemVerilogConstraintClauseState> best_soft;
  bool has_best{};
  const auto retain_values = [&]() {
    std::vector<PackedLogic4> values;
    values.reserve(assigned_values.size());
    for (const auto& value : assigned_values) values.push_back(*value);
    return values;
  };
  const auto better_soft = [&](const auto& candidate) {
    if (!has_best) return true;
    for (std::size_t offset = 0; offset < candidate.size(); ++offset) {
      const auto index = candidate.size() - offset - 1U;
      const auto candidate_satisfied = candidate[index]
          == SystemVerilogConstraintClauseState::Satisfied;
      const auto retained_satisfied = best_soft[index]
          == SystemVerilogConstraintClauseState::Satisfied;
      if (candidate_satisfied != retained_satisfied) {
        return candidate_satisfied;
      }
    }
    return false;
  };
  std::size_t depth{};
  while (true) {
    if (elapsed(limits_, clock_, start)) {
      return exhausted(
          SystemVerilogConstraintResource::ElapsedWork,
          search_steps,
          clause_evaluations);
    }
    const auto variable = variable_order[depth];
    if (next_domain_index[depth] >= domain_order[variable].size()) {
      next_domain_index[depth] = 0;
      assigned_values[variable].reset();
      if (depth == 0) {
        if (has_best) {
          return {
              SystemVerilogConstraintSolveStatus::Satisfied,
              SystemVerilogConstraintResource::None,
              std::move(best_values),
              search_steps,
              clause_evaluations};
        }
        return {
            SystemVerilogConstraintSolveStatus::Unsatisfiable,
            SystemVerilogConstraintResource::None,
            {},
            search_steps,
            clause_evaluations};
      }
      --depth;
      continue;
    }
    if (search_steps >= limits_.maximum_search_steps) {
      return exhausted(
          SystemVerilogConstraintResource::SearchSteps,
          search_steps,
          clause_evaluations);
    }
    assigned_values[variable] =
        variables_[variable].domain[
            domain_order[variable][next_domain_index[depth]++]];
    ++search_steps;
    const auto evaluation = evaluate();
    if (evaluation.hard == SystemVerilogConstraintClauseState::Violated) {
      continue;
    }
    if (depth + 1U == variables_.size()) {
      if (evaluation.hard
              == SystemVerilogConstraintClauseState::Undetermined
          || std::ranges::find(
                 evaluation.soft,
                 SystemVerilogConstraintClauseState::Undetermined)
              != evaluation.soft.end()) {
        throw std::logic_error{
            "constraint clause remained undetermined for a complete assignment"};
      }
      if (evaluation.soft.empty()) {
        return {
            SystemVerilogConstraintSolveStatus::Satisfied,
            SystemVerilogConstraintResource::None,
            retain_values(),
            search_steps,
            clause_evaluations};
      }
      if (better_soft(evaluation.soft)) {
        best_values = retain_values();
        best_soft = evaluation.soft;
        has_best = true;
      }
      continue;
    }
    ++depth;
  }
}

}  // namespace fsim::runtime
