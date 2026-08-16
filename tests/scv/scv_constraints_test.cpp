// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_constraints.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string_view>

namespace {

bool has_code(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    using namespace fsim::systemc;

    ScvNativeSmartPtrRegistry registry;
    fsim::diagnostic::Engine diagnostics;
    const auto aggregate = registry.create(ScvNativeValueKind::aggregate,
        { 10U, 20U }, "top.constraints", 17311U, diagnostics);
    assert(aggregate && !diagnostics.has_error());
    const std::vector<ScvNativeExtensionStep> x_path {
        { ScvNativeExtensionStepKind::field, 0U }
    };
    const std::vector<ScvNativeExtensionStep> y_path {
        { ScvNativeExtensionStepKind::field, 1U },
        { ScvNativeExtensionStepKind::field, 0U }
    };
    const std::vector<ScvNativeExtensionStep> z_path {
        { ScvNativeExtensionStepKind::field, 2U },
        { ScvNativeExtensionStepKind::element, 1U }
    };
    assert(registry.assign_signed(*aggregate, x_path, 100, diagnostics));
    assert(registry.assign_signed(*aggregate, y_path, 200, diagnostics));
    assert(registry.assign_signed(*aggregate, z_path, 300, diagnostics));

    ScvConstraintRequest request;
    request.variables = {
        { "top.constraints.x", *aggregate, x_path, { 1, 2, 3 } },
        { "top.constraints.y", *aggregate, y_path, { 4, 5, 6 } },
        { "top.constraints.z", *aggregate, z_path, { 7, 8, 9 } },
    };
    request.clauses = {
        { "x-plus-y", { { 0U, 1 }, { 1U, 1 } },
            ScvConstraintRelation::equal, 8, false },
        { "z-after-x", { { 2U, 1 }, { 0U, -1 } },
            ScvConstraintRelation::greater, 0, false },
        { "x-not-two", { { 0U, 1 } },
            ScvConstraintRelation::not_equal, 2, false },
        { "prefer-z-nine", { { 2U, 1 } },
            ScvConstraintRelation::equal, 9, true },
    };
    request.distributions = {
        { "x-weight", 0U, { { 1, 1U }, { 3, 7U } } },
    };
    request.solve_before = { { 0U, 1U }, { 1U, 2U } };
    request.selection = 77U;
    const auto solved = solve_scv_constraints(registry, request, diagnostics);
    assert(solved.status == ScvConstraintStatus::satisfied);
    assert(solved.assignments.size() == 3U);
    assert(solved.assignments[0].value + solved.assignments[1].value == 8);
    assert(solved.assignments[0].value != 2);
    assert(solved.assignments[2].value > solved.assignments[0].value);
    assert(registry.extension(*aggregate, x_path, diagnostics)->signed_value
        == solved.assignments[0].value);
    assert(registry.extension(*aggregate, y_path, diagnostics)->signed_value
        == solved.assignments[1].value);
    assert(registry.extension(*aggregate, z_path, diagnostics)->signed_value
        == solved.assignments[2].value);

    assert(registry.assign_signed(*aggregate, x_path, -10, diagnostics));
    assert(registry.assign_signed(*aggregate, y_path, -20, diagnostics));
    assert(registry.assign_signed(*aggregate, z_path, -30, diagnostics));
    fsim::diagnostic::Engine replay_diagnostics;
    const auto replayed = solve_scv_constraints(
        registry, request, replay_diagnostics);
    assert(replayed.status == ScvConstraintStatus::satisfied);
    assert(replayed.assignments == solved.assignments);

    auto unsatisfiable = request;
    unsatisfiable.clauses.push_back({ "x-is-one", { { 0U, 1 } },
        ScvConstraintRelation::equal, 1, false });
    unsatisfiable.clauses.push_back({ "x-is-three", { { 0U, 1 } },
        ScvConstraintRelation::equal, 3, false });
    const auto before_unsatisfied = registry.extension(*aggregate, x_path, diagnostics)->signed_value;
    fsim::diagnostic::Engine unsatisfied_diagnostics;
    const auto unsatisfied = solve_scv_constraints(
        registry, unsatisfiable, unsatisfied_diagnostics);
    assert(unsatisfied.status == ScvConstraintStatus::unsatisfiable);
    assert(unsatisfied.assignments.empty());
    assert(has_code(unsatisfied_diagnostics, "FSIM-SCV-Q002"));
    assert(registry.extension(*aggregate, x_path, diagnostics)->signed_value
        == before_unsatisfied);

    const auto alias = registry.copy(*aggregate, diagnostics);
    assert(alias);
    auto aliased = request;
    aliased.variables[1].handle = *alias;
    aliased.variables[1].path = x_path;
    fsim::diagnostic::Engine alias_diagnostics;
    const auto alias_result = solve_scv_constraints(
        registry, aliased, alias_diagnostics);
    assert(alias_result.status == ScvConstraintStatus::rejected);
    assert(has_code(alias_diagnostics, "FSIM-SCV-Q001"));

    auto malformed = request;
    malformed.distributions[0].values[0].weight = 0U;
    fsim::diagnostic::Engine malformed_diagnostics;
    assert(solve_scv_constraints(registry, malformed, malformed_diagnostics)
               .status
        == ScvConstraintStatus::rejected);
    assert(has_code(malformed_diagnostics, "FSIM-SCV-Q001"));

    auto overflow = request;
    overflow.variables[0].domain = {
        std::numeric_limits<std::int64_t>::max()
    };
    overflow.clauses = { { "overflow", { { 0U, 2 } },
        ScvConstraintRelation::equal, 0, false } };
    overflow.distributions.clear();
    overflow.solve_before.clear();
    fsim::diagnostic::Engine overflow_diagnostics;
    assert(solve_scv_constraints(registry, overflow, overflow_diagnostics)
               .status
        == ScvConstraintStatus::rejected);
    assert(has_code(overflow_diagnostics, "FSIM-SCV-Q001"));

    auto resource = unsatisfiable;
    resource.limits.max_search_steps = 1U;
    fsim::diagnostic::Engine resource_diagnostics;
    const auto exhausted = solve_scv_constraints(
        registry, resource, resource_diagnostics);
    assert(exhausted.status == ScvConstraintStatus::resource_exhausted);
    assert(has_code(resource_diagnostics, "FSIM-SCV-Q003"));
    assert(registry.extension(*aggregate, x_path, diagnostics)->signed_value
        == before_unsatisfied);
}
