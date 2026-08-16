// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_constraints.hpp"

#include "fsim/runtime/constraint_solver.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>

namespace fsim::systemc {
namespace {

    using runtime::PackedLogic4;
    using runtime::SystemVerilogConstraintAssignment;
    using runtime::SystemVerilogConstraintClauseState;
    using runtime::SystemVerilogConstraintDistributionWeight;
    using runtime::SystemVerilogConstraintDomainKind;
    using runtime::SystemVerilogConstraintSolver;
    using runtime::SystemVerilogConstraintSolveStatus;
    using runtime::SystemVerilogConstraintVariableId;

    PackedLogic4 packed(const std::int64_t value)
    {
        return PackedLogic4::from_aval_bval(
            64U, std::bit_cast<std::uint64_t>(value), 0U);
    }

    bool canonical_identity(
        const std::string_view identity,
        const ScvConstraintLimits& limits)
    {
        const auto invalid = std::ranges::find_if(identity, [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7fU || value == '\\';
        });
        return !identity.empty() && identity.size() <= limits.max_identity_bytes
            && invalid == identity.end() && identity.front() != ' '
            && identity.back() != ' ';
    }

    bool checked_add(
        const std::int64_t left,
        const std::int64_t right,
        std::int64_t& result)
    {
        if ((right > 0
                && left > std::numeric_limits<std::int64_t>::max() - right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::min() - right)) {
            return false;
        }
        result = left + right;
        return true;
    }

    bool checked_multiply(
        const std::int64_t left,
        const std::int64_t right,
        std::int64_t& result)
    {
        if (left == 0 || right == 0) {
            result = 0;
            return true;
        }
        if ((left == -1 && right == std::numeric_limits<std::int64_t>::min())
            || (right == -1
                && left == std::numeric_limits<std::int64_t>::min())) {
            return false;
        }
        if (left > 0) {
            if ((right > 0
                    && left > std::numeric_limits<std::int64_t>::max() / right)
                || (right < 0
                    && right < std::numeric_limits<std::int64_t>::min() / left)) {
                return false;
            }
        } else if ((right > 0
                       && left < std::numeric_limits<std::int64_t>::min() / right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::max() / right)) {
            return false;
        }
        result = left * right;
        return true;
    }

    bool relation_holds(
        const std::int64_t left,
        const ScvConstraintRelation relation,
        const std::int64_t right)
    {
        switch (relation) {
        case ScvConstraintRelation::equal:
            return left == right;
        case ScvConstraintRelation::not_equal:
            return left != right;
        case ScvConstraintRelation::less:
            return left < right;
        case ScvConstraintRelation::less_equal:
            return left <= right;
        case ScvConstraintRelation::greater:
            return left > right;
        case ScvConstraintRelation::greater_equal:
            return left >= right;
        }
        return false;
    }

    bool valid_limits(
        const ScvConstraintLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_variables == 0U || limits.max_clauses == 0U
            || limits.max_domain_values == 0U
            || limits.max_identity_bytes == 0U || limits.max_search_steps == 0U
            || limits.max_elapsed_work_ms == 0U
            || limits.max_variables > std::numeric_limits<std::uint32_t>::max()
            || limits.max_clauses > std::numeric_limits<std::uint32_t>::max()
            || limits.max_domain_values
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_identity_bytes
                > std::numeric_limits<std::uint32_t>::max()
            || limits.max_elapsed_work_ms
                > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error(
                "FSIM-SCV-Q003", "SCV constraint limits are inconsistent");
            return false;
        }
        return true;
    }

    bool preflight(
        ScvNativeSmartPtrRegistry& registry,
        const ScvConstraintRequest& request,
        std::vector<std::int64_t>& original_values,
        diagnostic::Engine& diagnostics)
    {
        if (!valid_limits(request.limits, diagnostics)
            || request.variables.empty()
            || request.variables.size() > request.limits.max_variables
            || request.clauses.size() > request.limits.max_clauses
            || request.distributions.size() > request.limits.max_clauses
            || request.solve_before.size() > request.limits.max_clauses) {
            if (request.variables.empty()
                || request.variables.size() > request.limits.max_variables
                || request.clauses.size() > request.limits.max_clauses
                || request.distributions.size() > request.limits.max_clauses
                || request.solve_before.size() > request.limits.max_clauses) {
                diagnostics.error("FSIM-SCV-Q003",
                    "SCV constraint request exceeds variable or clause limits");
            }
            return false;
        }
        std::size_t domain_values { };
        std::vector<std::pair<ScvObjectId,
            std::vector<ScvNativeExtensionStep>>>
            targets;
        targets.reserve(request.variables.size());
        original_values.reserve(request.variables.size());
        for (const auto& variable : request.variables) {
            if (!canonical_identity(variable.canonical_identity, request.limits)
                || variable.domain.empty()
                || variable.domain.size()
                    > request.limits.max_domain_values - domain_values) {
                diagnostics.error("FSIM-SCV-Q001",
                    "SCV constraint variable identity or domain is invalid");
                return false;
            }
            auto domain = variable.domain;
            std::ranges::sort(domain);
            if (std::ranges::adjacent_find(domain) != domain.end()) {
                diagnostics.error("FSIM-SCV-Q001",
                    "SCV constraint variable domain contains duplicates");
                return false;
            }
            const auto info = registry.info(variable.handle, diagnostics);
            const auto extension = registry.extension(
                variable.handle, variable.path, diagnostics);
            if (!info || !extension
                || extension->kind != ScvNativeExtensionKind::signed_integer
                || !extension->signed_value) {
                diagnostics.error("FSIM-SCV-Q001",
                    "SCV constraint target is not a live signed extension");
                return false;
            }
            const auto duplicate = std::ranges::find_if(targets,
                [&](const auto& target) {
                    return target.first == info->object
                        && target.second.size() == variable.path.size()
                        && std::ranges::equal(target.second, variable.path,
                            [](const auto left, const auto right) {
                                return left.kind == right.kind
                                    && left.index == right.index;
                            });
                });
            if (duplicate != targets.end()) {
                diagnostics.error("FSIM-SCV-Q001",
                    "SCV constraint variables alias the same native extension");
                return false;
            }
            targets.emplace_back(info->object, variable.path);
            original_values.push_back(*extension->signed_value);
            domain_values += variable.domain.size();
        }
        return true;
    }

} // namespace

ScvConstraintResult solve_scv_constraints(
    ScvNativeSmartPtrRegistry& registry,
    const ScvConstraintRequest& request,
    diagnostic::Engine& diagnostics)
{
    ScvConstraintResult result;
    std::vector<std::int64_t> original_values;
    if (!preflight(registry, request, original_values, diagnostics)) {
        return result;
    }
    try {
        runtime::SystemVerilogConstraintSolverLimits solver_limits;
        solver_limits.maximum_variables = request.limits.max_variables;
        solver_limits.maximum_clauses = request.limits.max_clauses;
        solver_limits.maximum_domain_values = request.limits.max_domain_values;
        solver_limits.maximum_search_steps = request.limits.max_search_steps;
        solver_limits.maximum_elapsed_work = std::chrono::milliseconds {
            static_cast<std::int64_t>(request.limits.max_elapsed_work_ms)
        };
        SystemVerilogConstraintSolver solver { solver_limits };
        std::vector<SystemVerilogConstraintVariableId> variables;
        variables.reserve(request.variables.size());
        for (const auto& variable : request.variables) {
            runtime::SystemVerilogConstraintVariable retained;
            retained.canonical_identity = variable.canonical_identity;
            retained.profile = { SystemVerilogConstraintDomainKind::BitVector,
                64U, true, "scv::signed", false };
            retained.domain.reserve(variable.domain.size());
            for (const auto value : variable.domain) {
                retained.domain.push_back(packed(value));
            }
            variables.push_back(solver.add_variable(std::move(retained)));
        }

        for (const auto& clause : request.clauses) {
            if (!canonical_identity(clause.canonical_identity, request.limits)
                || clause.terms.empty()
                || clause.relation < ScvConstraintRelation::equal
                || clause.relation > ScvConstraintRelation::greater_equal) {
                diagnostics.error(
                    "FSIM-SCV-Q001", "SCV constraint clause is malformed");
                return result;
            }
            std::vector<SystemVerilogConstraintVariableId> dependencies;
            dependencies.reserve(clause.terms.size());
            std::int64_t minimum { };
            std::int64_t maximum { };
            for (const auto term : clause.terms) {
                if (term.variable >= request.variables.size()
                    || term.coefficient == 0) {
                    diagnostics.error("FSIM-SCV-Q001",
                        "SCV constraint term has an invalid variable or coefficient");
                    return result;
                }
                dependencies.push_back(variables[term.variable]);
                const auto [low, high] = std::ranges::minmax(
                    request.variables[term.variable].domain);
                std::int64_t product_low { };
                std::int64_t product_high { };
                if (!checked_multiply(term.coefficient, low, product_low)
                    || !checked_multiply(term.coefficient, high, product_high)) {
                    diagnostics.error("FSIM-SCV-Q001",
                        "SCV constraint arithmetic exceeds signed 64-bit range");
                    return result;
                }
                if (product_low > product_high) {
                    std::swap(product_low, product_high);
                }
                if (!checked_add(minimum, product_low, minimum)
                    || !checked_add(maximum, product_high, maximum)) {
                    diagnostics.error("FSIM-SCV-Q001",
                        "SCV constraint arithmetic exceeds signed 64-bit range");
                    return result;
                }
            }
            runtime::SystemVerilogConstraintClause retained;
            retained.canonical_identity = clause.canonical_identity;
            retained.variables = dependencies;
            retained.soft = clause.soft;
            retained.evaluate = [terms = clause.terms,
                                    relation = clause.relation,
                                    right = clause.right_hand_side,
                                    variables](
                                    const SystemVerilogConstraintAssignment& assignment) {
                std::int64_t sum { };
                for (const auto term : terms) {
                    const auto id = variables[term.variable];
                    if (!assignment.assigned(id)) {
                        return SystemVerilogConstraintClauseState::Undetermined;
                    }
                    const auto value = assignment.value(id).known_signed_value();
                    std::int64_t product { };
                    if (!value
                        || !checked_multiply(
                            term.coefficient, *value, product)
                        || !checked_add(sum, product, sum)) {
                        return SystemVerilogConstraintClauseState::Violated;
                    }
                }
                return relation_holds(sum, relation, right)
                    ? SystemVerilogConstraintClauseState::Satisfied
                    : SystemVerilogConstraintClauseState::Violated;
            };
            solver.add_clause(std::move(retained));
        }

        for (const auto& distribution : request.distributions) {
            if (!canonical_identity(
                    distribution.canonical_identity, request.limits)
                || distribution.variable >= request.variables.size()
                || distribution.values.empty()) {
                diagnostics.error(
                    "FSIM-SCV-Q001", "SCV constraint distribution is malformed");
                return result;
            }
            runtime::SystemVerilogConstraintDistribution retained;
            retained.canonical_identity = distribution.canonical_identity;
            retained.variable = variables[distribution.variable];
            for (const auto entry : distribution.values) {
                if (entry.weight == 0U
                    || std::ranges::find(
                           request.variables[distribution.variable].domain,
                           entry.value)
                        == request.variables[distribution.variable].domain.end()) {
                    diagnostics.error("FSIM-SCV-Q001",
                        "SCV constraint distribution value or weight is invalid");
                    return result;
                }
                retained.entries.push_back({ packed(entry.value),
                    packed(entry.value), entry.weight,
                    SystemVerilogConstraintDistributionWeight::PerValue });
            }
            solver.add_distribution(std::move(retained));
        }
        for (const auto order : request.solve_before) {
            if (order.earlier >= variables.size() || order.later >= variables.size()
                || order.earlier == order.later) {
                diagnostics.error(
                    "FSIM-SCV-Q001", "SCV solve-before relation is malformed");
                return result;
            }
            solver.add_solve_before(
                variables[order.earlier], variables[order.later]);
        }

        const auto solved = solver.solve(request.selection);
        result.search_steps = solved.search_steps;
        result.clause_evaluations = solved.clause_evaluations;
        if (solved.status == SystemVerilogConstraintSolveStatus::Unsatisfiable) {
            result.status = ScvConstraintStatus::unsatisfiable;
            diagnostics.error(
                "FSIM-SCV-Q002", "SCV constraints are unsatisfiable");
            return result;
        }
        if (solved.status != SystemVerilogConstraintSolveStatus::Satisfied) {
            result.status = ScvConstraintStatus::resource_exhausted;
            diagnostics.error(
                "FSIM-SCV-Q003", "SCV constraint solver exhausted its resource limit");
            return result;
        }
        if (solved.values.size() != request.variables.size()) {
            diagnostics.error(
                "FSIM-SCV-Q001", "SCV constraint solver returned a partial solution");
            return result;
        }
        result.assignments.reserve(solved.values.size());
        for (std::size_t index = 0; index < solved.values.size(); ++index) {
            const auto value = solved.values[index].known_signed_value();
            if (!value) {
                diagnostics.error(
                    "FSIM-SCV-Q001", "SCV constraint solution is not signed integral");
                result.assignments.clear();
                return result;
            }
            result.assignments.push_back({ index, *value });
        }
        for (std::size_t index = 0; index < result.assignments.size(); ++index) {
            const auto& variable = request.variables[index];
            if (!registry.assign_signed(variable.handle, variable.path,
                    result.assignments[index].value, diagnostics)) {
                for (std::size_t rollback = 0; rollback < index; ++rollback) {
                    const auto& prior = request.variables[rollback];
                    (void)registry.assign_signed(prior.handle, prior.path,
                        original_values[rollback], diagnostics);
                }
                result.assignments.clear();
                result.status = ScvConstraintStatus::rejected;
                diagnostics.error("FSIM-SCV-Q001",
                    "SCV constraint solution publication failed and was rolled back");
                return result;
            }
        }
        result.status = ScvConstraintStatus::satisfied;
        return result;
    } catch (const std::exception& error) {
        diagnostics.error(
            "FSIM-SCV-Q001", std::string { "SCV constraint request rejected: " } + error.what());
        return result;
    }
}

} // namespace fsim::systemc
