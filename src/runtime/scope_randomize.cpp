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
        SystemVerilogConstraintSolver solver { request.limits };
        SystemVerilogClassRandomizeVariables variables;
        std::size_t domain_values { };
        for (const auto& input : request.variables) {
            SystemVerilogConstraintVariable variable {
                input.canonical_identity, input.profile, input.domain
            };
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
