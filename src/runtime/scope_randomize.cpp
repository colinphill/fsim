// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scope_randomize.hpp"

#include "constraint_bounds_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

    [[nodiscard]] std::vector<PackedLogic4> complete_domain(
        const std::size_t width,
        const std::size_t available)
    {
        if (width >= std::numeric_limits<std::size_t>::digits) {
            throw SystemVerilogConstraintResourceError {
                SystemVerilogConstraintResource::DomainValues,
                "scope randomize domain exceeds the solver domain-value budget"
            };
        }
        const auto count = std::size_t { 1 } << width;
        if (count > available) {
            throw SystemVerilogConstraintResourceError {
                SystemVerilogConstraintResource::DomainValues,
                "scope randomize domain exceeds the solver domain-value budget"
            };
        }
        std::vector<PackedLogic4> result;
        result.reserve(count);
        for (std::size_t value = 0; value < count; ++value) {
            result.push_back(PackedLogic4::from_aval_bval(width, value, 0));
        }
        return result;
    }

    [[nodiscard]] std::uint64_t next_selection(std::uint64_t& state) noexcept
    {
        state += 0x9e3779b97f4a7c15ULL;
        auto value = state;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] PackedLogic4 arbitrary_value(
        const std::size_t width,
        std::uint64_t& selection)
    {
        const auto word_count = width / 64U + (width % 64U != 0U);
        std::vector<std::uint64_t> aval(word_count);
        for (auto& word : aval) {
            word = next_selection(selection);
        }
        const std::vector<std::uint64_t> bval(word_count);
        return PackedLogic4::from_word_planes(width, aval, bval);
    }

    void validate_request(const SystemVerilogScopeRandomizeRequest& request)
    {
        std::set<const PackedLogic4*> targets;
        for (const auto& input : request.variables) {
            if (input.target == nullptr
                || input.canonical_identity.empty()
                || input.profile.width == 0
                || input.target->width() != input.profile.width
                || input.target->is_logic9()
                || !targets.insert(input.target).second) {
                throw std::invalid_argument {
                    "scope randomize requires unique exact packed targets"
                };
            }
            for (const auto& value : input.domain) {
                if (value.width() != input.profile.width || value.is_logic9()) {
                    throw std::invalid_argument {
                        "scope randomize requires exact packed domain values"
                    };
                }
            }
        }
    }

    [[nodiscard]] SystemVerilogClassRandomizeResult randomize_unconstrained(
        const SystemVerilogScopeRandomizeRequest& request)
    {
        auto selection = request.selection;
        std::vector<PackedLogic4> staged;
        staged.reserve(request.variables.size());
        for (const auto& input : request.variables) {
            if (input.domain.empty()) {
                staged.push_back(arbitrary_value(input.profile.width, selection));
            } else {
                staged.push_back(
                    input.domain[next_selection(selection) % input.domain.size()]);
            }
        }
        for (std::size_t index = 0; index < staged.size(); ++index) {
            using std::swap;
            swap(*request.variables[index].target, staged[index]);
        }
        return {
            SystemVerilogConstraintSolveStatus::Satisfied,
            SystemVerilogConstraintResource::None,
            static_cast<std::uint64_t>(request.variables.size()), 0
        };
    }

} // namespace

SystemVerilogClassRandomizeResult randomize_systemverilog_scope(
    const SystemVerilogScopeRandomizeRequest& request)
{
    try {
        validate_request(request);
        if (!request.inline_constraints) {
            return randomize_unconstrained(request);
        }
        // A wide variable takes a uniformly chosen value when no clause
        // depends on it, and otherwise draws from the values its hard
        // clauses' comparisons with constants allow (IEEE 1800-2017 18.12).
        auto selection = request.selection;
        std::set<std::size_t> direct;
        std::map<std::size_t, std::vector<PackedLogic4>> bounded;
        {
            std::vector<std::size_t> wide;
            for (std::size_t index = 0; index < request.variables.size(); ++index) {
                const auto& input = request.variables[index];
                if (input.domain.empty() && input.profile.width > 16U) {
                    wide.push_back(index);
                }
            }
            if (!wide.empty()) {
                try {
                    SystemVerilogConstraintSolver probe;
                    SystemVerilogClassRandomizeVariables probe_variables;
                    for (const auto& input : request.variables) {
                        const auto id = probe.add_variable({ input.canonical_identity,
                            input.profile, { *input.target } });
                        probe_variables.emplace(input.canonical_identity, id);
                    }
                    request.inline_constraints(probe, probe_variables);
                    // A calling-scope array's elements keep their values,
                    // so `x inside {q}` bounds `x` (18.7).
                    std::map<SystemVerilogConstraintVariableId, PackedLogic4>
                        fixed;
                    for (const auto& input : request.variables) {
                        if (input.domain.size() == 1U) {
                            fixed.emplace(
                                probe_variables.at(input.canonical_identity),
                                input.domain.front());
                        }
                    }
                    // A variable with a listed domain spans its values; a
                    // bounded wide variable's interval bounds the variables
                    // compared with it in the next pass (`x < a + 5`).
                    SystemVerilogConstraintValueIntervals intervals;
                    for (const auto& input : request.variables) {
                        if (const auto interval
                            = systemverilog_constraint_value_interval(
                                input.domain, input.profile)) {
                            intervals.emplace(
                                probe_variables.at(input.canonical_identity),
                                *interval);
                        }
                    }
                    std::set<SystemVerilogConstraintVariableId> wide_ids;
                    for (const auto index : wide) {
                        wide_ids.insert(probe_variables.at(
                            request.variables[index].canonical_identity));
                    }
                    const bool coupled = std::ranges::any_of(
                        probe.clauses(), [&](const auto& clause) {
                            return std::ranges::count_if(clause.variables,
                                       [&](const auto id) {
                                           return wide_ids.contains(id);
                                       })
                                > 1;
                        });
                    if (probe.distribution_count() == 0U) {
                        const auto passes = std::min<std::size_t>(
                            16U, wide.size() + 1U);
                        for (std::size_t pass = 0; pass < passes; ++pass) {
                            bool narrowed { };
                            for (const auto index : wide) {
                                const auto& input = request.variables[index];
                                const auto id = probe_variables.at(
                                    input.canonical_identity);
                                const bool referenced = std::ranges::any_of(
                                    probe.clauses(), [&](const auto& clause) {
                                        return std::ranges::find(
                                                   clause.variables, id)
                                            != clause.variables.end();
                                    });
                                if (!referenced) {
                                    direct.insert(index);
                                    continue;
                                }
                                if (input.profile.width > 64U) {
                                    bounded.insert_or_assign(index,
                                        systemverilog_constraint_wide_variable_domain(
                                            probe.clauses(), id, input.profile,
                                            [&selection] {
                                                return next_selection(selection);
                                            },
                                            fixed));
                                    continue;
                                }
                                auto domain
                                    = systemverilog_constraint_variable_domain_with_fixed(
                                        probe.clauses(), id, input.profile,
                                        [&selection] {
                                            return next_selection(selection);
                                        },
                                        fixed, &intervals);
                                if (const auto interval
                                    = systemverilog_constraint_value_interval(
                                        domain, input.profile)) {
                                    const auto known = intervals.find(id);
                                    if (known == intervals.end()
                                        || !(known->second == *interval)) {
                                        intervals.insert_or_assign(
                                            id, *interval);
                                        narrowed = true;
                                    }
                                }
                                bounded.insert_or_assign(
                                    index, std::move(domain));
                            }
                            if (!coupled || !narrowed) {
                                break;
                            }
                        }
                    }
                } catch (const std::exception&) {
                    direct.clear();
                    bounded.clear();
                }
            }
        }
        SystemVerilogConstraintSolver solver { request.limits };
        SystemVerilogClassRandomizeVariables variables;
        std::size_t domain_values { };
        for (std::size_t index = 0; index < request.variables.size(); ++index) {
            const auto& input = request.variables[index];
            SystemVerilogConstraintVariable variable {
                input.canonical_identity, input.profile, input.domain
            };
            if (direct.contains(index)) {
                variable.domain.push_back(
                    arbitrary_value(input.profile.width, selection));
            } else if (const auto found = bounded.find(index);
                       found != bounded.end()) {
                if (found->second.empty()) {
                    return { SystemVerilogConstraintSolveStatus::Unsatisfiable,
                        SystemVerilogConstraintResource::None, 0, 0 };
                }
                variable.domain = found->second;
            }
            if (variable.domain.empty()) {
                const auto available = request.limits.maximum_domain_values
                        >= domain_values
                    ? request.limits.maximum_domain_values - domain_values
                    : 0;
                variable.domain = complete_domain(variable.profile.width, available);
            }
            domain_values += variable.domain.size();
            const auto id = solver.add_variable(std::move(variable));
            variables.emplace(input.canonical_identity, id);
            const auto& retained = solver.variables()[id];
            solver.add_distribution({ retained.canonical_identity + "::$std-randomize", id,
                { { retained.domain.front(), retained.domain.back(), 1,
                    SystemVerilogConstraintDistributionWeight::PerValue } } });
        }
        request.inline_constraints(solver, variables);
        auto solved = solver.solve(request.selection);
        if (solved.status != SystemVerilogConstraintSolveStatus::Satisfied) {
            return {
                solved.status, solved.exhausted_resource,
                solved.search_steps, solved.clause_evaluations
            };
        }
        if (solved.values.size() != request.variables.size()) {
            throw std::logic_error {
                "scope randomize solver returned an incomplete assignment"
            };
        }
        std::vector<PackedLogic4> staged;
        staged.reserve(solved.values.size());
        for (std::size_t index = 0; index < solved.values.size(); ++index) {
            if (solved.values[index].width()
                    != request.variables[index].profile.width
                || solved.values[index].is_logic9()) {
                throw std::logic_error {
                    "scope randomize solver returned an invalid assignment"
                };
            }
            staged.push_back(solved.values[index]);
        }
        for (std::size_t index = 0; index < staged.size(); ++index) {
            using std::swap;
            swap(*request.variables[index].target, staged[index]);
        }
        return {
            solved.status, solved.exhausted_resource,
            solved.search_steps, solved.clause_evaluations
        };
    } catch (const SystemVerilogConstraintResourceError& error) {
        return {
            SystemVerilogConstraintSolveStatus::ResourceExhausted,
            error.resource(), 0, 0
        };
    } catch (const std::bad_alloc&) {
        return {
            SystemVerilogConstraintSolveStatus::ResourceExhausted,
            SystemVerilogConstraintResource::DomainValues, 0, 0
        };
    }
}

} // namespace fsim::runtime
