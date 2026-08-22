// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/runtime/simir_coverage.hpp"

#include <new>
#include <unordered_set>

namespace fsim::app::application_detail {
namespace {

    struct PointHash {
        [[nodiscard]] std::size_t operator()(
            const runtime::CodeCoveragePointId point) const noexcept
        {
            const auto mixed = point.high
                ^ (point.low + UINT64_C(0x9e3779b97f4a7c15)
                    + (point.high << 6U) + (point.high >> 2U));
            return static_cast<std::size_t>(mixed);
        }
    };

} // namespace

DebugCodeCoverageSnapshotResult debug_code_coverage_snapshot(
    const runtime::simir::Process& process,
    const std::span<const std::uint64_t> counters,
    const std::size_t maximum_points) noexcept
{
    DebugCodeCoverageSnapshotResult result;
    try {
        const auto retained_capacity
            = std::min(process.operations.size(), maximum_points);
        result.observations.reserve(retained_capacity);
        std::unordered_set<runtime::CodeCoveragePointId, PointHash> points;
        points.reserve(retained_capacity);
        for (std::size_t index = 0U;
            index < process.operations.size(); ++index) {
            const auto* hit = runtime::simir::operation_get_if<
                runtime::simir::CodeCoverageHit>(
                &process.operations[index]);
            if (hit == nullptr) {
                continue;
            }
            if (result.observations.size() >= maximum_points) {
                result.error
                    = DebugCodeCoverageSnapshotError::resource_limit;
                result.observations.clear();
                return result;
            }
            if (index
                > std::numeric_limits<
                    runtime::simir::InstructionIndex>::max()) {
                result.error
                    = DebugCodeCoverageSnapshotError::resource_limit;
                result.observations.clear();
                return result;
            }
            result.instruction = static_cast<
                runtime::simir::InstructionIndex>(index);
            if (!runtime::is_code_coverage_identity_valid(hit->point)) {
                result.error = DebugCodeCoverageSnapshotError::invalid_point;
                result.observations.clear();
                return result;
            }
            if (!runtime::is_code_coverage_point_metric(hit->metric)) {
                result.error = DebugCodeCoverageSnapshotError::invalid_metric;
                result.observations.clear();
                return result;
            }
            if (!points.emplace(hit->point).second) {
                result.error = DebugCodeCoverageSnapshotError::duplicate_point;
                result.observations.clear();
                return result;
            }
            const auto counter = process.operations.code_coverage_counter(
                index, hit->counter);
            if (counter.value >= counters.size()) {
                result.error
                    = DebugCodeCoverageSnapshotError::counter_out_of_range;
                result.observations.clear();
                return result;
            }
            result.observations.push_back({
                result.instruction,
                hit->point,
                hit->metric,
                counter,
                counters[counter.value],
            });
        }
        return result;
    } catch (const std::bad_alloc&) {
        result.observations.clear();
        result.error = DebugCodeCoverageSnapshotError::resource_limit;
        return result;
    } catch (const std::length_error&) {
        result.observations.clear();
        result.error = DebugCodeCoverageSnapshotError::resource_limit;
        return result;
    }
}

} // namespace fsim::app::application_detail
