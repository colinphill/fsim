// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/scv_smart_ptr.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fsim::systemc {

struct ScvConstraintLimits {
    std::size_t max_variables { 256U };
    std::size_t max_clauses { 1024U };
    std::size_t max_domain_values { 65536U };
    std::size_t max_identity_bytes { 4096U };
    std::uint64_t max_search_steps { 1024U * 1024U };
    std::uint64_t max_elapsed_work_ms { 1000U };
};

struct ScvConstraintVariable {
    std::string canonical_identity;
    ScvNativeSmartPtrHandle handle;
    std::vector<ScvNativeExtensionStep> path;
    std::vector<std::int64_t> domain;
};

struct ScvConstraintTerm {
    std::size_t variable { };
    std::int64_t coefficient { 1 };
};

enum class ScvConstraintRelation : std::uint8_t {
    equal = 1,
    not_equal = 2,
    less = 3,
    less_equal = 4,
    greater = 5,
    greater_equal = 6,
};

struct ScvConstraintClause {
    std::string canonical_identity;
    std::vector<ScvConstraintTerm> terms;
    ScvConstraintRelation relation { ScvConstraintRelation::equal };
    std::int64_t right_hand_side { };
    bool soft { };
};

struct ScvConstraintDistributionValue {
    std::int64_t value { };
    std::uint64_t weight { 1U };
};

struct ScvConstraintDistribution {
    std::string canonical_identity;
    std::size_t variable { };
    std::vector<ScvConstraintDistributionValue> values;
};

struct ScvConstraintSolveBefore {
    std::size_t earlier { };
    std::size_t later { };
};

struct ScvConstraintRequest {
    std::vector<ScvConstraintVariable> variables;
    std::vector<ScvConstraintClause> clauses;
    std::vector<ScvConstraintDistribution> distributions;
    std::vector<ScvConstraintSolveBefore> solve_before;
    std::uint64_t selection { };
    ScvConstraintLimits limits;
};

enum class ScvConstraintStatus : std::uint8_t {
    satisfied = 1,
    unsatisfiable = 2,
    resource_exhausted = 3,
    rejected = 4,
};

struct ScvConstraintAssignment {
    std::size_t variable { };
    std::int64_t value { };

    friend bool operator==(
        const ScvConstraintAssignment&, const ScvConstraintAssignment&) = default;
};

struct ScvConstraintResult {
    ScvConstraintStatus status { ScvConstraintStatus::rejected };
    std::vector<ScvConstraintAssignment> assignments;
    std::uint64_t search_steps { };
    std::uint64_t clause_evaluations { };
};

[[nodiscard]] ScvConstraintResult solve_scv_constraints(
    ScvNativeSmartPtrRegistry& registry,
    const ScvConstraintRequest& request,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
