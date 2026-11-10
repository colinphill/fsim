// SPDX-License-Identifier: Apache-2.0
#pragma once

// The attempt state of one concurrent assertion directive (IEEE 1800-2023
// 16.14, 39.4.2, 39.5) as one flat block of 64-bit words.
//
// The block is position independent: every region is named by an offset in
// the header, never by a pointer, so generated code can address it as
// `base[offset + i]`, the run-state archive can save it as it is, and
// growing it moves nothing that holds a reference but the base pointer. Only
// growth (`reserve_*`, `relayout`) allocates; reading, advancing and
// retiring attempts do not.
//
// Layout (word index; `W` = std::int64_t). All regions follow the header in
// the order below; a ring's capacity is a power of two (or zero) so that
// generated code wraps with a mask.
//
//   header      [0, header_words)            see layout::Header
//   clocks      clocks_off, 3 * clocks_count tick count per clock, then the
//                                            time step of each clock's last
//                                            tick, then 1 for each clock that
//                                            ticked in the evaluation in
//                                            progress (multiclocked, 16.13)
//   attempts    attempts_off, count, cap     stride 5: started, blob, length,
//                                            serial, flags
//   outcomes    ring                         stride 4: outcome, started,
//                                            fail line, fail column
//   started     ring                         stride 1: started
//   events      ring                         stride 10: see StepRecord
//   requests    requests_off, count, cap     stride 2: start time, mode
//   heap        heap_off, heap_used, heap_cap       the attempts' automaton
//   heap (alt)  heap_alt_off, heap_alt_used, heap_cap  records, rebuilt per tick
//   scratch     scratch_off, scratch_used, scratch_cap  work words of one
//                                            sequence step in generated code;
//                                            empty between ticks
//   memo        memo_off, memo_used, memo_cap  the values of the local-
//                                            variable expressions of the tick
//                                            in progress (16.10), stride
//                                            memo_stride: expression,
//                                            answered, the thread's local
//                                            words, the result words; empty
//                                            between ticks
//   calls       calls_off, calls_used, calls_cap  the subroutine calls the
//                                            last tick queued (16.11): call,
//                                            then the thread's local words;
//                                            the process runs and empties
//                                            them after the tick; calls_taken
//                                            words of them it took
//
// An attempt's automaton (property tree and sequence threads) is a record in
// the current heap, at `blob` words from heap_off and `length` words long;
// the record format is documented at the encoder in sva_engine.cpp. A tick
// writes every surviving attempt's new record into the alternate heap and
// swaps the two, so records never grow in place.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fsim::runtime::sva {

namespace layout {

/// "SVA1" and the layout version; any change of a stride, an offset or a
/// field below changes it. Layout 2 (#186 stage 2): the step record carries
/// the guard's source position, and the header the fail position and the
/// pop serial of the outcome taken last. Layout 3: the scratch region the
/// generated attempt automaton works in (header words 36 to 38). Layout 4: a
/// thread's throughout entries in a record carry the condition's source
/// position (guard, from, line, column). Layout 6: an instance record of a
/// multiclocked plan carries its start point (flag bits 9 and 10), and its
/// start is a time step until the instance aligns to its clock (16.13.3);
/// h_flags carries the host's action controls (flag_actions_off). Layout 7:
/// the memo region of the local-variable expressions (header words 40 to
/// 44; IEEE 1800-2023 16.10) and the queue of subroutine calls attached to
/// sequences (header words 45 to 48; 16.11).
inline constexpr std::int64_t magic = 0x5356'4131'0007LL;

enum Header : std::size_t {
    h_magic = 0,
    h_tick,
    h_last_start,
    h_flags,
    // Attempts with stepping enabled plus pending step requests; the tick
    // produces step records only while it is nonzero (39.5.2).
    h_steps_active,
    h_next_serial,
    // The time step of the last tick.
    h_time,
    h_clocks_count,
    h_clocks_off,
    h_attempts_off,
    h_attempts_count,
    h_attempts_cap,
    h_outcomes_off,
    h_outcomes_head,
    h_outcomes_count,
    h_outcomes_cap,
    h_started_off,
    h_started_head,
    h_started_count,
    h_started_cap,
    h_events_off,
    h_events_head,
    h_events_count,
    h_events_cap,
    h_requests_off,
    h_requests_count,
    h_requests_cap,
    h_heap_off,
    h_heap_used,
    h_heap_alt_off,
    h_heap_alt_used,
    h_heap_cap,
    h_total_words,
    // The fail position (1-based line and column, 0 when unknown) of the
    // outcome taken last (39.4.2 failExpr), and the serial of that pop: the
    // host orders the monitors of one process by it (sva_outcome_start).
    h_last_fail_line,
    h_last_fail_column,
    h_pop_serial,
    // The scratch region of generated code (layout 3): the words of the
    // sequence step in progress live in [off, off + used); `used` is zero
    // between ticks, and growth keeps the used words.
    h_scratch_off,
    h_scratch_used,
    h_scratch_cap,
    // The per-instance delay and repetition bounds of the plan (layout 5):
    // the offset of a region whose first word is the count, then the values;
    // 0 when the plan has none. Generated code reads a slot bound here.
    h_bounds_off,
    // The memo of the local-variable expressions (layout 7): one entry per
    // expression and thread valuation the tick in progress needed, in the
    // words [off, off + used); `stride` words each (memo_e_* fields), and the
    // last `pending` entries are the requests the assertion process has not
    // answered yet. The tick that finds every value it needs clears it.
    h_memo_off,
    h_memo_used,
    h_memo_cap,
    h_memo_stride,
    h_memo_pending,
    // The subroutine calls attached to sequences that the last tick queued
    // (16.11), in the words [off, off + used): the call's number, then the
    // local words of the thread that reached it, for each; `taken` words of
    // them the process took already (the last call taken ends there).
    h_calls_off,
    h_calls_used,
    h_calls_cap,
    h_calls_taken,
};
inline constexpr std::size_t header_words = 49;
static_assert(h_total_words < header_words);

// h_flags bits.
inline constexpr std::int64_t flag_every_match = 1; // cover sequence (16.14.3)
inline constexpr std::int64_t flag_multiclocked = 2; // 16.13
// The plan's bits; a restored block must carry the same.
inline constexpr std::int64_t flag_plan = flag_every_match | flag_multiclocked;
// The action controls of the run (IEEE 1800-2023 20.11 PassOff, FailOff,
// VacuousOff and their VPI forms, 39.5.2), set by the host whenever they
// change: bit action_off_shift + n set means the action block of outcome n
// (1 success, 2 failure, 3 vacuous success) does not run, and the outcome
// pop returns n + outcome_action_off for it. The pass or fail statement is
// the only thing skipped; the outcome itself still counts (20.11).
inline constexpr std::int64_t action_off_shift = 1;
inline constexpr std::int64_t flag_actions_off = 0b111 << 2;
inline constexpr std::int64_t outcome_action_off = 8;
// The code the outcome pop returns for outcome `outcome` (1 to 4) of a block
// whose h_flags is `flags`; generated code computes the same.
[[nodiscard]] constexpr std::int64_t outcome_code(
    const std::int64_t outcome, const std::int64_t flags) noexcept
{
    return outcome
        + (((flags >> (outcome + action_off_shift)) & 1) * outcome_action_off);
}

inline constexpr std::size_t attempt_stride = 5;
inline constexpr std::size_t outcome_stride = 4;
inline constexpr std::size_t started_stride = 1;
inline constexpr std::size_t event_stride = 10;
inline constexpr std::size_t request_stride = 2;

// Attempt record fields.
inline constexpr std::size_t a_started = 0;
inline constexpr std::size_t a_blob = 1;
inline constexpr std::size_t a_length = 2;
inline constexpr std::size_t a_serial = 3;
inline constexpr std::size_t a_flags = 4;
// a_flags bit: step callbacks are enabled for this attempt (39.5.2).
inline constexpr std::int64_t attempt_stepping = 1;

// Step record fields (39.4.2 cbAssertionStepSuccess/Failure).
inline constexpr std::size_t e_failure = 0; // 0 success, 1 failure
inline constexpr std::size_t e_attempt_start = 1;
inline constexpr std::size_t e_time = 2;
inline constexpr std::size_t e_state_from = 3;
inline constexpr std::size_t e_state_to = 4;
inline constexpr std::size_t e_guard = 5; // the sampled guard checked
inline constexpr std::size_t e_sequence = 6;
inline constexpr std::size_t e_tick = 7;
// The source position of the sampled boolean checked (plan guard span):
// the step's matched expression (39.4.2 matched_exprs).
inline constexpr std::size_t e_guard_line = 8;
inline constexpr std::size_t e_guard_column = 9;

// Memo entry fields: the expression, whether its value is known, then the
// thread's local words and the result words.
inline constexpr std::size_t memo_e_expression = 0;
inline constexpr std::size_t memo_e_answered = 1;
inline constexpr std::size_t memo_e_locals = 2;

} // namespace layout

/// One outcome in the queue (`Outcome` of the engine as an integer).
struct StoredOutcome {
    std::int64_t outcome { };
    std::int64_t started { };
    // The failed node's source position of a failure (39.4.2 failExpr).
    std::int64_t fail_line { };
    std::int64_t fail_column { };
};

/// One attempt's table row.
struct StoredAttempt {
    std::int64_t started { };
    std::int64_t blob { };
    std::int64_t length { };
    std::int64_t serial { };
    std::int64_t flags { };
};

/// One step event: which sequence thread advanced or died at a tick.
struct StepRecord {
    bool failure { };
    std::int64_t attempt_start { };
    std::int64_t time { };
    std::int64_t state_from { };
    std::int64_t state_to { };
    std::int64_t guard { };
    std::int64_t sequence { };
    std::int64_t tick { };
    std::int64_t guard_line { };
    std::int64_t guard_column { };
};

/// Where a block publishes its base pointer: entry `index` of the owner's
/// table, while the owner lives. The compiled kernel addresses a monitor's
/// block through such an entry (StaticKernelSvaTable::blocks); growth
/// rewrites the entry, so generated code never holds a stale base.
struct BlockSlot {
    std::weak_ptr<std::vector<std::int64_t*>> owner;
    std::size_t index { };

    [[nodiscard]] bool bound() const noexcept { return !owner.expired(); }
};

class AttemptStorage {
public:
    explicit AttemptStorage(std::size_t clocks = 0);
    ~AttemptStorage();
    AttemptStorage(AttemptStorage&& other) noexcept;
    AttemptStorage& operator=(AttemptStorage&& other) noexcept;
    AttemptStorage(const AttemptStorage&) = delete;
    AttemptStorage& operator=(const AttemptStorage&) = delete;

    /// Take saved words; nullopt when the layout is not this build's or any
    /// region lies outside the block.
    [[nodiscard]] static std::optional<AttemptStorage> adopt(
        std::vector<std::int64_t> words);

    [[nodiscard]] const std::vector<std::int64_t>& words() const noexcept
    {
        return words_;
    }
    /// The block generated code addresses; invalidated by growth.
    [[nodiscard]] std::int64_t* data() noexcept { return words_.data(); }
    /// Publishes the base pointer in `slot` now and after every growth;
    /// an unbound slot (default) publishes nowhere. The entry is cleared
    /// when the storage is destroyed.
    void bind_slot(BlockSlot slot) noexcept;
    [[nodiscard]] const BlockSlot& slot() const noexcept { return slot_; }

    [[nodiscard]] std::int64_t get(layout::Header field) const noexcept
    {
        return words_[field];
    }
    void set(layout::Header field, std::int64_t value) noexcept
    {
        words_[field] = value;
    }

    // --- clocks (multiclocked plans) ---
    [[nodiscard]] std::size_t clocks() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_clocks_count));
    }
    [[nodiscard]] std::int64_t& clock_ticks(std::size_t clock) noexcept
    {
        return words_[static_cast<std::size_t>(get(layout::h_clocks_off)) + clock];
    }
    [[nodiscard]] std::int64_t& clock_time(std::size_t clock) noexcept
    {
        return words_[static_cast<std::size_t>(get(layout::h_clocks_off))
            + clocks() + clock];
    }
    [[nodiscard]] std::int64_t& clock_ticked(std::size_t clock) noexcept
    {
        return words_[static_cast<std::size_t>(get(layout::h_clocks_off))
            + 2U * clocks() + clock];
    }

    // --- attempts ---
    [[nodiscard]] std::size_t attempt_count() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_attempts_count));
    }
    [[nodiscard]] StoredAttempt attempt(std::size_t index) const noexcept;
    void set_attempt(std::size_t index, const StoredAttempt& attempt) noexcept;
    /// Appends a row; growth when the table is full.
    void push_attempt(const StoredAttempt& attempt);
    void truncate_attempts(std::size_t count) noexcept
    {
        set(layout::h_attempts_count, static_cast<std::int64_t>(count));
    }
    /// Removes the rows whose start is `start`, keeping the order of the
    /// rest; returns how many.
    std::size_t erase_attempts_started_at(std::int64_t start) noexcept;
    /// The automaton record of a row, in the current heap.
    [[nodiscard]] std::span<const std::int64_t> record(
        const StoredAttempt& attempt) const noexcept;
    /// A tick's rewrite of the records: `begin_pass`, then `append_record`
    /// for each surviving attempt (returns the record's `blob`), then
    /// `end_pass` swaps the heaps.
    void begin_pass() noexcept { set(layout::h_heap_alt_used, 0); }
    [[nodiscard]] std::int64_t append_record(std::span<const std::int64_t> words);
    void end_pass() noexcept;
    /// Drops every attempt and record.
    void clear_attempts() noexcept;

    // --- outcome queue ---
    [[nodiscard]] std::size_t outcome_count() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_outcomes_count));
    }
    void push_outcome(const StoredOutcome& outcome);
    [[nodiscard]] std::optional<StoredOutcome> pop_outcome() noexcept;
    void clear_outcomes() noexcept
    {
        set(layout::h_outcomes_count, 0);
        set(layout::h_outcomes_head, 0);
    }

    // --- start times not yet reported (cbAssertionStart) ---
    void push_started(std::int64_t start);
    [[nodiscard]] std::vector<std::int64_t> take_started();

    // --- step events ---
    [[nodiscard]] std::size_t event_count() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_events_count));
    }
    void push_event(const StepRecord& record);
    [[nodiscard]] std::vector<StepRecord> take_events();

    // --- pending step requests (39.5.2: stepping for a future attempt) ---
    [[nodiscard]] std::size_t request_count() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_requests_count));
    }
    [[nodiscard]] std::int64_t request_start(std::size_t index) const noexcept;
    void push_request(std::int64_t start, std::int64_t mode);
    /// Removes the requests for `start`; true when there was one.
    bool erase_requests(std::int64_t start) noexcept;
    /// Removes the requests of attempts that began before `time`.
    void expire_requests(std::int64_t time) noexcept;

    /// Recomputes h_steps_active from the attempt flags and the requests.
    void refresh_steps_active() noexcept;

    /// Growth for generated code (fsim_sva_grow): makes region `region`
    /// (see grow_region_*) hold at least `needed` rows (words, for the heap
    /// and the scratch), growing it as the host's own pushes do.
    void reserve_region(std::size_t region, std::size_t needed);
    /// Stores the plan's bounds in the block (layout 5).
    void set_bounds(const std::vector<std::int64_t>& bounds);

    // --- the memo of local-variable expressions (layout 7) ---
    void set_memo_stride(std::size_t stride) noexcept
    {
        set(layout::h_memo_stride, static_cast<std::int64_t>(stride));
    }
    [[nodiscard]] std::size_t memo_stride() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_memo_stride));
    }
    [[nodiscard]] std::size_t memo_count() const noexcept
    {
        const auto stride = memo_stride();
        return stride == 0U
            ? 0U
            : static_cast<std::size_t>(get(layout::h_memo_used)) / stride;
    }
    [[nodiscard]] std::span<std::int64_t> memo_entry(std::size_t index) noexcept
    {
        return { words_.data() + static_cast<std::size_t>(get(layout::h_memo_off))
                + index * memo_stride(),
            memo_stride() };
    }
    [[nodiscard]] std::span<const std::int64_t> memo_entry(
        std::size_t index) const noexcept
    {
        return { words_.data() + static_cast<std::size_t>(get(layout::h_memo_off))
                + index * memo_stride(),
            memo_stride() };
    }
    /// Appends an entry of memo_stride() words; growth when full.
    void push_memo(std::span<const std::int64_t> entry);
    void clear_memo() noexcept
    {
        set(layout::h_memo_used, 0);
        set(layout::h_memo_pending, 0);
    }
    // --- the queued subroutine calls (layout 7, 16.11) ---
    [[nodiscard]] std::size_t calls_used() const noexcept
    {
        return static_cast<std::size_t>(get(layout::h_calls_used));
    }
    [[nodiscard]] std::span<const std::int64_t> calls() const noexcept
    {
        return { words_.data() + static_cast<std::size_t>(get(layout::h_calls_off)),
            calls_used() };
    }
    /// Appends a call's words; growth when full.
    void push_call(std::span<const std::int64_t> entry);
    void clear_calls() noexcept
    {
        set(layout::h_calls_used, 0);
        set(layout::h_calls_taken, 0);
    }
    /// Replaces the block by `words` (an earlier copy of it), keeping the
    /// binding of its table entry.
    void restore(std::vector<std::int64_t> words) noexcept;

    /// The regions reserve_region grows.
    enum : std::size_t {
        grow_attempts,
        grow_outcomes,
        grow_started,
        grow_events,
        grow_requests,
        grow_heap,
        grow_scratch,
        grow_memo,
        grow_calls,
    };

private:
    struct Caps {
        std::size_t clocks { };
        std::size_t attempts { };
        std::size_t outcomes { };
        std::size_t started { };
        std::size_t events { };
        std::size_t requests { };
        std::size_t heap { };
        std::size_t scratch { };
        std::size_t memo { };
        std::size_t calls { };
        // Words of the bounds region (count, values); 0 for none.
        std::size_t bounds { };
    };
    [[nodiscard]] Caps caps() const noexcept;
    [[nodiscard]] static std::size_t total_words(const Caps& caps) noexcept;
    /// The only allocation: moves every region into a block of `next`.
    void relayout(const Caps& next);
    [[nodiscard]] static std::size_t grown(std::size_t capacity,
        std::size_t needed) noexcept;
    void ring_push(layout::Header off, layout::Header head,
        layout::Header count, layout::Header cap, std::size_t Caps::*field,
        std::size_t stride, std::span<const std::int64_t> record);
    [[nodiscard]] bool ring_pop(layout::Header off, layout::Header head,
        layout::Header count, layout::Header cap, std::size_t stride,
        std::span<std::int64_t> out) noexcept;
    void publish(std::int64_t* base) noexcept;

    std::vector<std::int64_t> words_;
    BlockSlot slot_;
};

} // namespace fsim::runtime::sva
