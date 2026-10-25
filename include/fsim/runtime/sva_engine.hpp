// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/semantic/sva_plan.hpp"

#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <vector>

// The evaluation of one concurrent assertion directive (IEEE 1800-2017
// 16.7-16.14). Each clock tick starts an attempt (16.14.1) and advances every
// running attempt over the sampled values of the plan's guards; an attempt
// ends with one outcome. Running attempts form a dynamic list, so neither the
// number of attempts nor the length of a delay or repetition is bounded.
namespace fsim::runtime::sva {

enum class Outcome : std::uint8_t {
    none,
    success,
    failure,
    vacuous,
    // `disable iff` stopped the running attempts (16.12).
    disabled,
};

class Instance;
struct CompiledPlan;

class Monitor {
public:
    // `bounds` holds the plan's per-instance delay and repetition bounds.
    Monitor(semantic::sva::Plan plan, const std::vector<std::int64_t>& bounds);
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    // One clock tick: `guards[i]` is guard i sampled at this tick; `start`
    // starts a new attempt; `disabled` is the disable condition. `time`
    // identifies the time step; a multiclocked plan reads which of its
    // clocks ticked from its clock guards.
    void tick(const std::vector<bool>& guards, bool start, bool disabled,
        std::int64_t time);
    [[nodiscard]] std::size_t pending_outcomes() const;
    [[nodiscard]] Outcome next_outcome();
    // Attempts running at the end of simulation: those whose strong
    // obligations fail, and those that hold on the truncated trace (16.12).
    struct Finished {
        std::size_t failures { };
        std::size_t holds { };
    };
    [[nodiscard]] Finished finish();
    // $assertkill: stop the running attempts (20.12).
    void kill();
    [[nodiscard]] std::size_t running_attempts() const;

private:
    std::unique_ptr<CompiledPlan> plan_;
    std::list<std::unique_ptr<Instance>> attempts_;
    std::deque<Outcome> outcomes_;
    std::int64_t tick_ { -1 };
};

} // namespace fsim::runtime::sva
