// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"

#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

[[nodiscard]] std::vector<PackedLogic4> complete_domain(
    const std::size_t width,
    const std::size_t available) {
  if (width >= std::numeric_limits<std::size_t>::digits) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "scope randomize domain exceeds the solver domain-value budget"};
  }
  const auto count = std::size_t{1} << width;
  if (count > available) {
    throw SystemVerilogConstraintResourceError{
        SystemVerilogConstraintResource::DomainValues,
        "scope randomize domain exceeds the solver domain-value budget"};
  }
  std::vector<PackedLogic4> result;
  result.reserve(count);
  for (std::size_t value = 0; value < count; ++value) {
    result.push_back(PackedLogic4::from_aval_bval(width, value, 0));
  }
  return result;
}

}  // namespace

SystemVerilogClassRandomizeResult randomize_systemverilog_scope(
    const SystemVerilogScopeRandomizeRequest& request) {
  try {
    SystemVerilogConstraintSolver solver{request.limits};
    SystemVerilogClassRandomizeVariables variables;
    std::set<const PackedLogic4*> targets;
    std::size_t domain_values{};
    for (const auto& input : request.variables) {
      if (input.target == nullptr
          || input.canonical_identity.empty()
          || input.profile.width == 0
          || input.target->width() != input.profile.width
          || input.target->is_logic9()
          || !targets.insert(input.target).second) {
        throw std::invalid_argument{
            "scope randomize requires unique exact packed targets"};
      }
      SystemVerilogConstraintVariable variable{
          input.canonical_identity, input.profile, input.domain};
      if (variable.domain.empty()) {
        const auto available = request.limits.maximum_domain_values
                >= domain_values
            ? request.limits.maximum_domain_values - domain_values : 0;
        variable.domain = complete_domain(variable.profile.width, available);
      }
      domain_values += variable.domain.size();
      const auto id = solver.add_variable(std::move(variable));
      variables.emplace(input.canonical_identity, id);
      const auto& retained = solver.variables()[id];
      solver.add_distribution({
          retained.canonical_identity + "::$std-randomize", id,
          {{retained.domain.front(), retained.domain.back(), 1,
            SystemVerilogConstraintDistributionWeight::PerValue}}});
    }
    if (request.inline_constraints) {
      request.inline_constraints(solver, variables);
    }
    auto solved = solver.solve(request.selection);
    if (solved.status != SystemVerilogConstraintSolveStatus::Satisfied) {
      return {
          solved.status, solved.exhausted_resource,
          solved.search_steps, solved.clause_evaluations};
    }
    if (solved.values.size() != request.variables.size()) {
      throw std::logic_error{
          "scope randomize solver returned an incomplete assignment"};
    }
    std::vector<PackedLogic4> staged;
    staged.reserve(solved.values.size());
    for (std::size_t index = 0; index < solved.values.size(); ++index) {
      if (solved.values[index].width()
              != request.variables[index].profile.width
          || solved.values[index].is_logic9()) {
        throw std::logic_error{
            "scope randomize solver returned an invalid assignment"};
      }
      staged.push_back(solved.values[index]);
    }
    for (std::size_t index = 0; index < staged.size(); ++index) {
      using std::swap;
      swap(*request.variables[index].target, staged[index]);
    }
    return {
        solved.status, solved.exhausted_resource,
        solved.search_steps, solved.clause_evaluations};
  } catch (const SystemVerilogConstraintResourceError& error) {
    return {
        SystemVerilogConstraintSolveStatus::ResourceExhausted,
        error.resource(), 0, 0};
  } catch (const std::bad_alloc&) {
    return {
        SystemVerilogConstraintSolveStatus::ResourceExhausted,
        SystemVerilogConstraintResource::DomainValues, 0, 0};
  }
}

}  // namespace fsim::runtime
