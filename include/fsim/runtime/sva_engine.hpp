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
    // The tick (simulation time) the attempt of the outcome last taken by
    // next_outcome started at (IEEE 1800-2023 39.4.2 attemptStartTime).
    [[nodiscard]] std::int64_t last_outcome_start() const noexcept;
    // The start times of the attempts begun since the last call, oldest
    // first (cbAssertionStart, IEEE 1800-2023 39.4.2).
    [[nodiscard]] std::vector<std::int64_t> take_started();
    // Attempts running at the end of simulation: those whose strong
    // obligations fail, and those that hold on the truncated trace (16.12).
    struct Finished {
        std::size_t failures { };
        std::size_t holds { };
    };
    [[nodiscard]] Finished finish();
    // $assertkill: stop the running attempts (20.12).
    void kill();
    // vpiAssertionKill with an attempt start time: discard the running
    // attempts that started at `start_time` and leave the rest (39.5.2).
    // Returns how many were discarded.
    std::size_t kill_attempt(std::int64_t start_time);
    [[nodiscard]] std::size_t running_attempts() const;

private:
    struct Running {
        std::unique_ptr<Instance> instance;
        std::int64_t started { };
    };
    struct Resolved {
        Outcome outcome { Outcome::none };
        std::int64_t started { };
    };
    std::unique_ptr<CompiledPlan> plan_;
    std::list<Running> attempts_;
    std::deque<Resolved> outcomes_;
    std::vector<std::int64_t> started_;
    std::int64_t last_start_ { };
    std::int64_t tick_ { -1 };
};

} // namespace fsim::runtime::sva
