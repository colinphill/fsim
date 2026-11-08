// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/sva_storage.hpp"
#include "fsim/semantic/sva_plan.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The evaluation of one concurrent assertion directive (IEEE 1800-2017
// 16.7-16.14). Each clock tick starts an attempt (16.14.1) and advances every
// running attempt over the sampled values of the plan's guards; an attempt
// ends with one outcome. Running attempts form a dynamic list, so neither the
// number of attempts nor the length of a delay or repetition is bounded.
// The attempts, the outcome queue, the start times and the step events live in
// one flat AttemptStorage block (sva_storage.hpp), which the interpreter and
// the static kernel share; the automaton of an attempt is a record in it.
namespace fsim::runtime {
class RunStateArchive;
}

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

/// One step of an attempt (cbAssertionStepSuccess / cbAssertionStepFailure,
/// IEEE 1800-2023 39.4.2): a sequence thread advanced past a sampled boolean
/// or died at it. State 0 is the origin, 1 an accepting state, and any other
/// number an intermediate point of the sequence program.
using StepEvent = StepRecord;

class Monitor {
public:
    /// An empty monitor, for the run-state archive to fill (run_state_io).
    Monitor();
    /// `plan_text` is an encoded semantic::sva::Plan; `bounds` holds the
    /// plan's per-instance delay and repetition bounds. Throws
    /// std::runtime_error for a malformed plan.
    Monitor(std::string plan_text, std::vector<std::int64_t> bounds);
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    /// One clock tick: `guards[i]` is guard i sampled at this tick; `start`
    /// starts a new attempt; `disabled` is the disable condition. `time`
    /// identifies the time step; a multiclocked plan reads which of its
    /// clocks ticked from its clock guards.
    void tick(const std::vector<bool>& guards, bool start, bool disabled,
        std::int64_t time);
    [[nodiscard]] std::size_t pending_outcomes() const;
    /// Takes the next outcome; `serial` stamps the block (h_pop_serial) so
    /// the host can tell which of a process's monitors popped last.
    [[nodiscard]] Outcome next_outcome(std::int64_t serial);
    /// The tick (simulation time) the attempt of the outcome last taken by
    /// next_outcome started at (IEEE 1800-2023 39.4.2 attemptStartTime).
    [[nodiscard]] std::int64_t last_outcome_start() const noexcept;
    /// The serial of the last pop (0 before the first).
    [[nodiscard]] std::int64_t last_outcome_serial() const noexcept;
    // The source position (1-based line and column, 0 when unknown) of the
    // boolean expression that rejected the attempt of the failure last taken
    // by next_outcome: the innermost failed node (IEEE 1800-2023 39.4.2
    // failExpr).
    struct FailPoint {
        std::uint32_t line { };
        std::uint32_t column { };
    };
    [[nodiscard]] FailPoint last_outcome_fail() const noexcept;
    /// The start times of the attempts begun since the last call, oldest
    /// first (cbAssertionStart, IEEE 1800-2023 39.4.2).
    [[nodiscard]] std::vector<std::int64_t> take_started();
    /// Attempts running at the end of simulation: those whose strong
    /// obligations fail, and those that hold on the truncated trace (16.12).
    struct Finished {
        std::size_t failures { };
        std::size_t holds { };
        /// The tick each failing and each holding attempt started at, in
        /// the order the attempts started (39.4.2 attemptStartTime).
        std::vector<std::int64_t> failure_starts;
        std::vector<std::int64_t> hold_starts;
    };
    [[nodiscard]] Finished finish();
    /// $assertkill: stop the running attempts (20.12).
    void kill();
    /// vpiAssertionKill with an attempt start time: discard the running
    /// attempts that started at `start_time` and leave the rest (39.5.2).
    /// Returns how many were discarded.
    std::size_t kill_attempt(std::int64_t start_time);
    [[nodiscard]] std::size_t running_attempts() const;

    /// vpiAssertionEnableStep / vpiAssertionDisableStep for the attempt that
    /// starts at `start_time` (39.5.2). A request for an attempt that has not
    /// started waits for it. The stepping mode of an attempt cannot change
    /// once the attempt has started (39.5.2): a request for a start time at
    /// or before the last tick's time step is refused (false) and changes
    /// nothing.
    [[nodiscard]] bool request_step(std::int64_t start_time, bool enable);
    /// Whether any attempt or request has stepping enabled; the tick
    /// produces no step events otherwise.
    [[nodiscard]] bool stepping() const noexcept;
    [[nodiscard]] bool has_step_events() const noexcept
    {
        return storage_.event_count() != 0U;
    }
    /// The step events produced since the last call, in tick order.
    [[nodiscard]] std::vector<StepEvent> take_step_events();

    /// The flat attempt block (sva_storage.hpp).
    [[nodiscard]] const AttemptStorage& storage() const noexcept
    {
        return storage_;
    }
    /// Publishes the block's base in a table entry (BlockSlot), kept
    /// through growth and a restore; the compiled kernel pops outcomes
    /// through it (KOp::sva_outcome).
    void bind_slot(BlockSlot slot) noexcept { storage_.bind_slot(std::move(slot)); }
    [[nodiscard]] bool slot_bound() const noexcept
    {
        return storage_.slot().bound();
    }
    /// The monitor as a run-state record: the plan, its bounds and the
    /// attempt block (D.9 $save / $restart). Loading rebuilds the compiled
    /// plan and adopts the block.
    void run_state_io(RunStateArchive& archive);

private:
    void build(const std::vector<std::int64_t>& bounds);
    [[nodiscard]] static std::size_t clock_count(
        const semantic::sva::Plan& plan);

    std::string plan_text_;
    std::vector<std::int64_t> bounds_;
    std::unique_ptr<CompiledPlan> plan_;
    AttemptStorage storage_;
    // Scratch of one pass over the attempts; never part of the state.
    std::vector<std::int64_t> scratch_;
};

} // namespace fsim::runtime::sva
