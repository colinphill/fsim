// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

#include <cstddef>
#include <optional>

namespace fsim::app::application_detail {

// Compilation selection only: a backward edge spanning a dynamic suspension
// identifies small timed/edge loops which can repay native compilation. This
// is not a scheduling, termination, purity, or native-region certificate.
// Process validation and ordinary checked suspension still apply.
template<class ProcessLike>
[[nodiscard]] inline bool has_dynamic_wait_backedge(
    const ProcessLike& process)
{
    using namespace runtime::simir;
    std::optional<std::size_t> last_wait;
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        const auto& operation = process.operations[index];
        const auto* const wait = operation_get_if<WaitOn>(&operation);
        if (operation_holds<WaitFor>(operation)
            || (wait != nullptr && (!wait->signals.empty() || wait->timeout))) {
            last_wait = index;
            continue;
        }
        if (!last_wait) {
            continue;
        }
        if (const auto* const jump = operation_get_if<Jump>(&operation)) {
            if (jump->target <= *last_wait && *last_wait < index) {
                return true;
            }
        } else if (const auto* const branch = operation_get_if<Branch>(&operation)) {
            if ((branch->when_true <= *last_wait
                    || branch->when_false <= *last_wait)
                && *last_wait < index) {
                return true;
            }
        }
    }
    return false;
}

} // namespace fsim::app::application_detail
