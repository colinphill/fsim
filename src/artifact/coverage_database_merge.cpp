// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_merge.hpp"

#include <new>
#include <set>
#include <utility>

namespace fsim::artifact {
namespace {

    bool checked_accumulate(std::size_t& total, const std::size_t amount,
        const std::size_t ceiling) noexcept
    {
        if (amount > ceiling || total > ceiling - amount) {
            return false;
        }
        total += amount;
        return true;
    }

    CoverageDatabaseMergeResult invalid_input(const std::size_t input_index,
        const CoverageDatabaseModelResult& model) noexcept
    {
        return { { }, CoverageDatabaseMergeError::InvalidInput, model.error,
            input_index, model.index };
    }

} // namespace

CoverageDatabaseMergeResult merge_coverage_databases(
    const std::span<const CoverageDatabaseContents> inputs,
    const CoverageDatabaseMergeLimits& limits) noexcept
{
    if (inputs.empty()) {
        return { { }, CoverageDatabaseMergeError::EmptyInput };
    }
    if (inputs.size() > limits.maximum_inputs) {
        return { { }, CoverageDatabaseMergeError::ResourceLimit,
            CoverageDatabaseModelError::ResourceLimit };
    }

    try {
        std::size_t run_count { };
        std::size_t metric_count { };
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            if (!checked_accumulate(
                    run_count, inputs[index].runs.size(),
                    limits.model.maximum_runs)
                || !checked_accumulate(metric_count,
                    inputs[index].metrics.size(),
                    limits.model.maximum_metrics)) {
                return { { }, CoverageDatabaseMergeError::ResourceLimit,
                    CoverageDatabaseModelError::ResourceLimit, index, 0U };
            }
        }

        auto reference
            = make_coverage_database_contents(inputs.front(), limits.model);
        if (!reference.ok()) {
            if (reference.error == CoverageDatabaseModelError::ResourceLimit
                || reference.error
                    == CoverageDatabaseModelError::ArithmeticOverflow) {
                return { { }, CoverageDatabaseMergeError::ResourceLimit,
                    reference.error, 0U, reference.index };
            }
            if (reference.error
                == CoverageDatabaseModelError::AllocationFailure) {
                return { { }, CoverageDatabaseMergeError::AllocationFailure,
                    reference.error, 0U, reference.index };
            }
            return invalid_input(0U, reference);
        }

        CoverageDatabaseContents merged = std::move(*reference.contents);
        merged.runs.reserve(run_count);
        merged.metrics.reserve(metric_count);
        std::set<CoverageDatabaseIdentity> run_identities;
        for (const auto& run : merged.runs) {
            run_identities.insert(run.identity);
        }

        for (std::size_t input_index = 1; input_index < inputs.size();
            ++input_index) {
            auto model
                = make_coverage_database_contents(inputs[input_index], limits.model);
            if (!model.ok()) {
                if (model.error == CoverageDatabaseModelError::ResourceLimit
                    || model.error
                        == CoverageDatabaseModelError::ArithmeticOverflow) {
                    return { { }, CoverageDatabaseMergeError::ResourceLimit,
                        model.error, input_index, model.index };
                }
                if (model.error
                    == CoverageDatabaseModelError::AllocationFailure) {
                    return { { },
                        CoverageDatabaseMergeError::AllocationFailure,
                        model.error, input_index, model.index };
                }
                return invalid_input(input_index, model);
            }

            auto& candidate = *model.contents;
            if (candidate.fingerprint != merged.fingerprint) {
                return { { },
                    CoverageDatabaseMergeError::FingerprintMismatch,
                    CoverageDatabaseModelError::None, input_index, 0U };
            }
            if (candidate.sources != merged.sources) {
                return { { },
                    CoverageDatabaseMergeError::SourceInventoryMismatch,
                    CoverageDatabaseModelError::None, input_index, 0U };
            }
            if (candidate.exclusions != merged.exclusions) {
                return { { },
                    CoverageDatabaseMergeError::ExclusionInventoryMismatch,
                    CoverageDatabaseModelError::None, input_index, 0U };
            }
            for (std::size_t run_index = 0; run_index < candidate.runs.size();
                ++run_index) {
                const auto& run = candidate.runs[run_index];
                if (!run_identities.insert(run.identity).second) {
                    return { { }, CoverageDatabaseMergeError::DuplicateRun,
                        CoverageDatabaseModelError::DuplicateRun, input_index,
                        run_index };
                }
                merged.runs.push_back(run);
            }
            merged.metrics.insert(merged.metrics.end(),
                std::make_move_iterator(candidate.metrics.begin()),
                std::make_move_iterator(candidate.metrics.end()));
        }

        auto result
            = make_coverage_database_contents(std::move(merged), limits.model);
        if (!result.ok()) {
            if (result.error == CoverageDatabaseModelError::ResourceLimit
                || result.error
                    == CoverageDatabaseModelError::ArithmeticOverflow) {
                return { { }, CoverageDatabaseMergeError::ResourceLimit,
                    result.error, 0U, result.index };
            }
            if (result.error == CoverageDatabaseModelError::AllocationFailure) {
                return { { }, CoverageDatabaseMergeError::AllocationFailure,
                    result.error, 0U, result.index };
            }
            return { { }, CoverageDatabaseMergeError::InvalidInput,
                result.error, 0U, result.index };
        }
        return { std::move(result.contents), CoverageDatabaseMergeError::None,
            CoverageDatabaseModelError::None, 0U, 0U };
    } catch (const std::bad_alloc&) {
        return { { }, CoverageDatabaseMergeError::AllocationFailure };
    }
}

} // namespace fsim::artifact
