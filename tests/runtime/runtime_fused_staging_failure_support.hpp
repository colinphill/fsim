// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace fsim::tests::runtime::staging_failure_support {

using Publication = std::tuple<fsim::runtime::SimulationTick,
    std::uint64_t, std::string>;

// These semantic publication fields remain comparable after a failed task is
// retried. Scheduler task sequence numbers describe queue identities instead.
struct PublicationEvent {
    fsim::runtime::SchedulerTraceKind kind { };
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };
    std::optional<fsim::runtime::SchedulerPhase> phase;
    fsim::runtime::RuntimeSignalId signal { };
    std::uint64_t systemverilog_round { };
    bool systemverilog { };
    bool end_of_time_slot { };

    bool operator==(const PublicationEvent&) const = default;
};

struct PublicationTrace {
    // The masked fixture has 65 owners. Keep enough room for each owner's
    // transaction and change records without allocating in the failure window.
    std::array<PublicationEvent, 256U> records { };
    std::size_t count { };
    bool overflow { };

    bool operator==(const PublicationTrace&) const = default;
};

struct Observation {
    std::string signal;
    std::vector<std::string> drivers;
    std::vector<Publication> publications;
    PublicationTrace publication_trace;
};

void arm_allocation_failure(std::size_t successful_allocations);
using AllocationFailureObserver = void (*)(void*) noexcept;
void set_allocation_failure_observer(
    void* context, AllocationFailureObserver observer) noexcept;
void clear_allocation_failure() noexcept;
[[nodiscard]] bool allocation_failure_was_injected() noexcept;
void begin_allocation_count() noexcept;
std::size_t end_allocation_count() noexcept;
void require(bool condition, const char* message);
void print_observation(const char* label, const Observation& observation);

struct AllocationFailureWindow {
    bool* executor_called { };
    bool closed { };
    std::array<fsim::runtime::RuntimeSignalId, 2U> observed_signals {
        std::numeric_limits<fsim::runtime::RuntimeSignalId>::max(),
        std::numeric_limits<fsim::runtime::RuntimeSignalId>::max()
    };
    PublicationTrace publications;

    static void trace(void* context,
        const fsim::runtime::SchedulerTraceRecord& record) noexcept;
};

} // namespace fsim::tests::runtime::staging_failure_support
