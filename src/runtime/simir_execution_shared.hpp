// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] PackedLogic4 resize_class_value(
    const PackedLogic4& value, std::size_t width);
[[nodiscard]] SimulationTick normalized_dynamic_wait_delay(
    const WaitFor& wait, const PackedLogic4& payload);
[[nodiscard]] bool is_class_execution_boundary(const Operation& operation);
[[nodiscard]] bool is_immediate_process_boundary(const Operation& operation);
[[nodiscard]] bool is_synchronization_boundary(const Operation& operation);
[[nodiscard]] bool is_dynamic_callable_boundary(const Operation& operation);
[[nodiscard]] double known_real(const PackedLogic4& value, const char* name);
[[nodiscard]] PackedLogic4 real_value(double value);
[[nodiscard]] bool vhdl_path_below_root(
    const std::filesystem::path& root, const std::filesystem::path& path);
[[nodiscard]] std::optional<std::filesystem::path> vhdl_directory_path(
    const std::filesystem::path& root,
    const std::filesystem::path& working,
    std::string_view text,
    std::error_code& error);
[[nodiscard]] bool vhdl_access_denied(const std::error_code& error);
// Whether a live child of a deferred-report Fork, resuming at `pc`, is still
// a pending report that a flush point clears (IEEE 1800-2023 16.4.1).
// The child starts at its branch with a WaitRegion for the region the
// action runs in, and suspends there with its pc one past it. Until then it
// is pending. An observed report matures in the Observed region, and after
// that it may no longer be flushed, even while its action waits to start in
// the Reactive region. A final report matures in the Postponed region, where
// no process resumes. A started action has moved past the wait and runs to
// its end.
template <typename Operations>
[[nodiscard]] bool deferred_report_pending(
    const Operations& operations, const Fork& fork,
    const InstructionIndex pc, const SchedulerPhase current)
{
    if (fork.branches.empty()) {
        return false;
    }
    const auto branch = fork.branches.front();
    if (pc == branch) {
        return true;
    }
    if (pc != branch + 1U || branch >= operations.size()) {
        return false;
    }
    const auto* const wait = operation_get_if<WaitRegion>(&operations[branch]);
    if (wait == nullptr) {
        return false;
    }
    return wait->phase == SchedulerPhase::postponed
        || current < SchedulerPhase::observed;
}

} // namespace fsim::runtime::simir
