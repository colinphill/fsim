// SPDX-License-Identifier: Apache-2.0
//
// Concurrent property evaluation (IEEE 1800-2017 16.7-16.14).
//
// A sequence runs as a set of threads over the plan's matcher program. A
// thread holds the window of ticks in which its next boolean is checked;
// threads with equal state merge, so unbounded delays and repetitions keep a
// bounded thread set. Properties form a tree of instances; each instance
// resolves to holds or fails, and the attempt reports its root's verdict.

#include "fsim/runtime/sva_engine.hpp"
#include "fsim/support/stack_growth.hpp"

#include "run_state_archive.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fsim::runtime::sva {

using semantic::sva::Bound;
using semantic::sva::Op;
using semantic::sva::PropertyKind;

namespace {

constexpr std::int64_t unbounded = std::numeric_limits<std::int64_t>::max();

struct Step {
    Op op { Op::check };
    std::uint32_t a { };
    std::uint32_t b { };
    std::uint32_t c { };
    std::int64_t lo { };
    std::int64_t hi { };
    // The source position of the boolean a check evaluates (0 when it has
    // none).
    std::uint32_t line { };
    std::uint32_t column { };
};

struct CompiledSequence {
    std::vector<Step> code;
    // The repetition counters, then the words of the local variables
    // (16.10): a thread's valuation follows its counters.
    std::uint32_t counters { };
    std::uint32_t locals_base { };
};

struct CompiledProperty {
    PropertyKind kind { PropertyKind::sequence };
    bool strong { };
    bool overlapped { };
    // The abort operators: sync_accept_on, sync_reject_on.
    bool synchronous { };
    // A multiclocked plan (16.13.3): the clock that flows into the property
    // (for a sequence and an implication, the semantic leading clock of its
    // sequence, 16.16.1).
    std::uint32_t clock { };
    std::uint32_t sequence { };
    std::uint32_t guard { };
    std::vector<std::uint32_t> children;
    std::int64_t lo { };
    std::int64_t hi { };
    // The position of the property's leftmost boolean expression.
    std::uint32_t line { };
    std::uint32_t column { };
    // The condition is an expression that reads local variables (16.10).
    bool guard_expression { };
    // The initialization assignments of the property's local variables.
    std::vector<std::uint32_t> initializers;
};

// The start of an instance of a multiclocked plan (16.13): a point of the
// trace, the time step held in the instance's start and whether the point is
// that time step itself (inclusive) or the first one after it (F.5.1: a
// clocked property waits for the nearest tick of its clock, possibly
// overlapping the point). An operator that counts ticks of its clock (if,
// nexttime, always, eventually, until) aligns at the first tick of its clock
// that reaches the point: its start becomes that tick's count and the point
// is cleared. The flags share the instance record's flag word.
constexpr std::int64_t at_point = 512;
constexpr std::int64_t point_inclusive = 1024;

// Where an attempt was rejected: the boolean expression that failed
// (IEEE 1800-2023 39.4.2 failExpr); line 0 when none is known.
struct FailPosition {
    std::uint32_t line { };
    std::uint32_t column { };
};

[[nodiscard]] std::int64_t resolve(
    const Bound& bound, const std::vector<std::int64_t>& bounds)
{
    switch (bound.mode) {
    case Bound::Mode::literal:
        return bound.value + bound.offset;
    case Bound::Mode::slot:
        if (bound.value < 0
            || static_cast<std::size_t>(bound.value) >= bounds.size()) {
            throw std::runtime_error {
                "concurrent property bound slot is out of range"
            };
        }
        return bounds[static_cast<std::size_t>(bound.value)] + bound.offset;
    case Bound::Mode::unbounded:
        return unbounded;
    }
    return unbounded;
}

[[nodiscard]] std::int64_t add(const std::int64_t base,
    const std::int64_t offset)
{
    if (base == unbounded || offset == unbounded) {
        return unbounded;
    }
    return base + offset;
}

} // namespace

struct CompiledPlan {
    std::vector<CompiledSequence> sequences;
    std::vector<CompiledProperty> properties;
    std::uint32_t root { };
    std::uint32_t guards { };
    bool every_match { };
    const std::vector<bool>* values { };
    std::int64_t now { };
    // A multiclocked plan (16.13): the guard of each clock's tick, the
    // ticks counted on each clock, the time step of each clock's last tick,
    // the clocks that ticked in this evaluation, and its time step.
    // The tick counts and time steps are working copies of the attempt
    // block's clock words for the tick in progress (AttemptStorage).
    std::vector<std::uint32_t> clock_guards;
    std::vector<std::int64_t> clock_ticks;
    std::vector<std::int64_t> clock_times;
    std::vector<bool> ticked;
    std::int64_t time { };
    // The plan checks an asynchronous abort (accept_on, reject_on; IEEE
    // 1800-2023 16.12.14), at every evaluation, a tick of its clocks or
    // not: an evaluation in which no clock ticked still applies it.
    bool async_abort { };
    // A recursive property (IEEE 1800-2023 16.12.17): a node reaches itself
    // through its children. Only such a plan forwards an instance to the
    // operand its outcome now is (Instance::forward), which keeps the chain
    // of instances short however long the recursion runs.
    bool recursive { };
    // Step events (39.4.2): produced only while `stepping` is set for the
    // attempt being advanced (39.5.2). `state_base[i]` numbers sequence i's
    // program counters as state ids; `guard_positions[g]` is the source
    // position of guard g (the step's matched expression), 0 when unknown.
    std::vector<std::uint32_t> state_base;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> guard_positions;
    mutable bool stepping { };
    mutable std::int64_t attempt_start { };
    mutable std::vector<StepEvent> events;
    // Local variables (IEEE 1800-2023 16.10): the word offset and size of
    // each local in a valuation, the valuation's words, each expression's
    // target (0 a check, else local target - 1) and the local sets of the
    // join ends. An expression's value for a valuation comes from the memo
    // of the attempt block (`storage`); a value the memo lacks becomes a
    // request for the assertion process, the tick is discarded (`missed`)
    // and runs again once the process answered.
    std::vector<std::size_t> local_offset;
    std::vector<std::size_t> local_size;
    std::size_t locals_words { };
    std::vector<std::uint32_t> expression_target;
    std::vector<std::vector<std::uint32_t>> local_sets;
    AttemptStorage* storage { };
    mutable bool missed { };

    [[nodiscard]] std::size_t result_words(const std::uint32_t expression) const
    {
        const auto target = expression_target[expression];
        return target == 0U ? 1U : local_size[target - 1U];
    }
    // The value of `expression` for the valuation `locals`: the result words
    // (zero while unknown).
    [[nodiscard]] std::vector<std::int64_t> evaluate(
        std::uint32_t expression, std::span<const std::int64_t> locals) const;
    [[nodiscard]] bool truth(const std::uint32_t expression,
        const std::span<const std::int64_t> locals) const
    {
        return evaluate(expression, locals).front() != 0;
    }

    [[nodiscard]] bool guard(const std::uint32_t index) const
    {
        return index < values->size() && (*values)[index];
    }
    [[nodiscard]] bool multiclocked() const
    {
        return !clock_guards.empty();
    }
    // The state id of program counter `pc` of sequence `sequence` (39.4.2):
    // ids number the points where a thread waits for a sampled boolean (a
    // check) or ends. Operations that only move time or a counter (delay,
    // counter_set, counter_increment, throughout_begin, first_match_begin)
    // are skipped, so a step's `to` is the next step's `from`. 0 is the
    // origin (where the program starts), 1 an accepting state (the match, or
    // the end of the program), and 2 + base + pc any other point.
    [[nodiscard]] std::int64_t state_id(
        const std::uint32_t sequence, std::uint32_t pc) const
    {
        const auto& code = sequences[sequence].code;
        const auto settle = [&](std::uint32_t at) {
            while (at < code.size()
                && (code[at].op == Op::delay || code[at].op == Op::counter_set
                    || code[at].op == Op::counter_increment
                    || code[at].op == Op::throughout_begin
                    || code[at].op == Op::first_match_begin)) {
                ++at;
            }
            return at;
        };
        pc = settle(pc);
        if (pc == settle(0U)) {
            return 0;
        }
        if (pc >= code.size() || code[pc].op == Op::match) {
            return 1;
        }
        return 2 + static_cast<std::int64_t>(state_base[sequence]) + pc;
    }
};

std::vector<std::int64_t> CompiledPlan::evaluate(const std::uint32_t expression,
    const std::span<const std::int64_t> locals) const
{
    const auto words = result_words(expression);
    auto& block = *storage;
    const auto count = block.memo_count();
    for (std::size_t index = 0; index < count; ++index) {
        const auto entry = std::as_const(block).memo_entry(index);
        if (entry[layout::memo_e_expression]
                != static_cast<std::int64_t>(expression)
            || !std::equal(locals.begin(), locals.end(),
                entry.begin() + layout::memo_e_locals)) {
            continue;
        }
        if (entry[layout::memo_e_answered] == 0) {
            missed = true;
            return std::vector<std::int64_t>(words, 0);
        }
        const auto result = entry.subspan(layout::memo_e_locals + locals_words, words);
        return { result.begin(), result.end() };
    }
    std::vector<std::int64_t> entry(block.memo_stride(), 0);
    entry[layout::memo_e_expression] = expression;
    std::ranges::copy(locals, entry.begin() + layout::memo_e_locals);
    block.push_memo(entry);
    block.set(layout::h_memo_pending, block.get(layout::h_memo_pending) + 1);
    missed = true;
    return std::vector<std::int64_t>(words, 0);
}

namespace {

// A reader over an attempt's automaton record. Every element of a counted
// sequence takes at least one word, so a count above the words left is
// corrupt; nesting is bounded so a damaged record cannot exhaust the stack.
class RecordReader {
public:
    explicit RecordReader(const std::span<const std::int64_t> words) noexcept
        : words_(words)
    {
    }
    [[nodiscard]] std::int64_t next()
    {
        if (position_ >= words_.size()) {
            throw std::runtime_error {
                "concurrent property attempt record is truncated"
            };
        }
        return words_[position_++];
    }
    [[nodiscard]] std::size_t count()
    {
        const auto value = next();
        if (value < 0
            || static_cast<std::uint64_t>(value) > words_.size() - position_) {
            throw std::runtime_error {
                "concurrent property attempt record is corrupt"
            };
        }
        return static_cast<std::size_t>(value);
    }
    [[nodiscard]] std::uint32_t index(const std::size_t limit)
    {
        const auto value = next();
        if (value < 0 || static_cast<std::uint64_t>(value) >= limit) {
            throw std::runtime_error {
                "concurrent property attempt record is corrupt"
            };
        }
        return static_cast<std::uint32_t>(value);
    }
    [[nodiscard]] bool finished() const noexcept
    {
        return position_ == words_.size();
    }

private:
    std::span<const std::int64_t> words_;
    std::size_t position_ { };
};


struct Throughout {
    // A guard, or an expression (16.10) when `expression`.
    std::uint32_t guard { };
    bool expression { };
    // The first tick of the operand; -1 until its first check.
    std::int64_t from { -1 };
    // The position of the condition, for a failure report.
    std::uint32_t line { };
    std::uint32_t column { };
};

struct JoinReference {
    std::uint32_t join { };
    std::uint32_t side { };
};

// A throughout entry's condition word: the guard, plus 2^32 for an
// expression.
[[nodiscard]] std::int64_t throughout_word(const Throughout& entry)
{
    return static_cast<std::int64_t>(entry.guard)
        | (entry.expression ? std::int64_t { 1 } << 32U : 0);
}

struct Thread {
    std::uint32_t pc { };
    // The clock whose ticks the window counts. A thread switching clocks
    // waits for the clock's first tick after `switch_time` (at it when
    // `switch_inclusive`) before its window is known.
    std::uint32_t clock { };
    bool switching { };
    bool switch_inclusive { };
    std::int64_t switch_time { };
    std::int64_t due_lo { };
    std::int64_t due_hi { };
    std::vector<std::int64_t> counters;
    std::vector<Throughout> throughout;
    std::vector<std::uint32_t> groups;
    std::vector<JoinReference> joins;

    [[nodiscard]] std::vector<std::int64_t> key() const
    {
        std::vector<std::int64_t> result;
        result.reserve(10U + counters.size() + throughout.size() * 2U
            + groups.size() + joins.size() * 2U);
        result.push_back(pc);
        result.push_back(clock);
        result.push_back((switching ? 1 : 0) | (switch_inclusive ? 2 : 0));
        result.push_back(switch_time);
        result.push_back(due_lo);
        result.push_back(due_hi);
        result.push_back(static_cast<std::int64_t>(counters.size()));
        result.insert(result.end(), counters.begin(), counters.end());
        result.push_back(static_cast<std::int64_t>(throughout.size()));
        for (const auto& entry : throughout) {
            result.push_back(throughout_word(entry));
            result.push_back(entry.from);
        }
        result.push_back(static_cast<std::int64_t>(groups.size()));
        result.insert(result.end(), groups.begin(), groups.end());
        for (const auto& reference : joins) {
            result.push_back(reference.join);
            result.push_back(reference.side);
        }
        return result;
    }
};

// A thread as record words: pc, clock, flags, switch_time, due_lo, due_hi,
// then the counted counters, throughout entries (guard, from, line, column),
// groups and join
// references (join, side).
void encode_thread(const Thread& thread, std::vector<std::int64_t>& out)
{
    out.push_back(thread.pc);
    out.push_back(thread.clock);
    out.push_back((thread.switching ? 1 : 0) | (thread.switch_inclusive ? 2 : 0));
    out.push_back(thread.switch_time);
    out.push_back(thread.due_lo);
    out.push_back(thread.due_hi);
    out.push_back(static_cast<std::int64_t>(thread.counters.size()));
    out.insert(out.end(), thread.counters.begin(), thread.counters.end());
    out.push_back(static_cast<std::int64_t>(thread.throughout.size()));
    for (const auto& entry : thread.throughout) {
        out.push_back(throughout_word(entry));
        out.push_back(entry.from);
        out.push_back(entry.line);
        out.push_back(entry.column);
    }
    out.push_back(static_cast<std::int64_t>(thread.groups.size()));
    out.insert(out.end(), thread.groups.begin(), thread.groups.end());
    out.push_back(static_cast<std::int64_t>(thread.joins.size()));
    for (const auto& reference : thread.joins) {
        out.push_back(reference.join);
        out.push_back(reference.side);
    }
}

[[nodiscard]] Thread decode_thread(RecordReader& in,
    const std::size_t counters, const std::size_t clocks)
{
    Thread thread;
    thread.pc = in.index(std::numeric_limits<std::uint32_t>::max());
    thread.clock = in.index(clocks);
    const auto flags = in.next();
    thread.switching = (flags & 1) != 0;
    thread.switch_inclusive = (flags & 2) != 0;
    thread.switch_time = in.next();
    thread.due_lo = in.next();
    thread.due_hi = in.next();
    if (in.count() != counters) {
        throw std::runtime_error {
            "concurrent property attempt record does not match its plan"
        };
    }
    thread.counters.resize(counters);
    for (auto& counter : thread.counters) {
        counter = in.next();
    }
    const auto throughout = in.count();
    thread.throughout.reserve(throughout);
    for (std::size_t index = 0; index < throughout; ++index) {
        const auto word = in.next();
        if (word < 0 || (word >> 32U) > 1) {
            throw std::runtime_error {
                "concurrent property attempt record is corrupt"
            };
        }
        const auto guard = static_cast<std::uint32_t>(word & 0xffffffff);
        const auto from = in.next();
        const auto line = in.index(std::numeric_limits<std::uint32_t>::max());
        const auto column = in.index(std::numeric_limits<std::uint32_t>::max());
        thread.throughout.push_back(
            { guard, (word >> 32U) != 0, from, line, column });
    }
    const auto groups = in.count();
    thread.groups.reserve(groups);
    for (std::size_t index = 0; index < groups; ++index) {
        thread.groups.push_back(
            in.index(std::numeric_limits<std::uint32_t>::max()));
    }
    const auto joins = in.count();
    thread.joins.reserve(joins);
    for (std::size_t index = 0; index < joins; ++index) {
        const auto join = in.index(std::numeric_limits<std::uint32_t>::max());
        thread.joins.push_back({ join, in.index(2U) });
    }
    return thread;
}

// The matches of one sequence started at one tick.
class Matcher {
public:
    // `locals` is the valuation of the local variables the sequence starts
    // with (16.10; empty for a plan without locals).
    Matcher(const CompiledPlan& plan, const CompiledSequence& sequence,
        const std::int64_t start, const std::span<const std::int64_t> locals)
        : plan_(plan)
        , sequence_(sequence)
    {
        Thread initial;
        initial.due_lo = start;
        initial.due_hi = start;
        initial.counters.assign(sequence.counters, 0);
        std::ranges::copy(locals, initial.counters.begin() + sequence.locals_base);
        threads_.push_back(std::move(initial));
    }

    // A matcher of a multiclocked plan started at a point of the trace
    // (16.13): its first thread waits on `clock` for the first tick at or
    // after (`inclusive`) or strictly after time step `time`, as a thread
    // switching clocks does.
    Matcher(const CompiledPlan& plan, const CompiledSequence& sequence,
        const std::uint32_t clock, const std::int64_t time,
        const bool inclusive, const std::span<const std::int64_t> locals)
        : plan_(plan)
        , sequence_(sequence)
    {
        Thread initial;
        initial.clock = clock;
        initial.switching = true;
        initial.switch_inclusive = inclusive;
        initial.switch_time = time;
        initial.counters.assign(sequence.counters, 0);
        std::ranges::copy(locals, initial.counters.begin() + sequence.locals_base);
        threads_.push_back(std::move(initial));
    }

    // An empty matcher, filled by decode.
    Matcher(const CompiledPlan& plan, const CompiledSequence& sequence)
        : plan_(plan)
        , sequence_(sequence)
    {
    }

    // The matcher as record words: the join and group counters, the position
    // of the last check that killed a thread (failExpr, 39.4.2), the counted
    // threads, then the counted joins (id, kind, continuation thread and the
    // two counted sets of ends: the end tick, then the valuation of the
    // local variables the operand's thread ended with, 16.10). The per-tick
    // scratch sets are not state.
    void encode(std::vector<std::int64_t>& out) const
    {
        out.push_back(next_join_);
        out.push_back(next_group_);
        out.push_back(failed_.line);
        out.push_back(failed_.column);
        out.push_back(static_cast<std::int64_t>(threads_.size()));
        for (const auto& thread : threads_) {
            encode_thread(thread, out);
        }
        out.push_back(static_cast<std::int64_t>(joins_.size()));
        for (const auto& [id, join] : joins_) {
            out.push_back(id);
            out.push_back(join.kind);
            encode_thread(join.continuation, out);
            for (const auto& ends : join.ends) {
                out.push_back(static_cast<std::int64_t>(ends.size()));
                for (const auto& end : ends) {
                    out.insert(out.end(), end.begin(), end.end());
                }
            }
        }
    }

    void decode(RecordReader& in)
    {
        const auto counters = sequence_.counters;
        const auto clocks = std::max<std::size_t>(1U, plan_.clock_ticks.size());
        next_join_ = in.index(std::numeric_limits<std::uint32_t>::max());
        next_group_ = in.index(std::numeric_limits<std::uint32_t>::max());
        failed_.line = in.index(std::numeric_limits<std::uint32_t>::max());
        failed_.column = in.index(std::numeric_limits<std::uint32_t>::max());
        const auto threads = in.count();
        threads_.reserve(threads);
        for (std::size_t index = 0; index < threads; ++index) {
            threads_.push_back(decode_thread(in, counters, clocks));
        }
        const auto joins = in.count();
        for (std::size_t index = 0; index < joins; ++index) {
            const auto id = in.index(std::numeric_limits<std::uint32_t>::max());
            Join join;
            join.kind = in.index(2U);
            join.continuation = decode_thread(in, counters, clocks);
            for (auto& ends : join.ends) {
                const auto size = in.count();
                for (std::size_t end = 0; end < size; ++end) {
                    std::vector<std::int64_t> entry(1U + plan_.locals_words);
                    for (auto& word : entry) {
                        word = in.next();
                    }
                    ends.insert(std::move(entry));
                }
            }
            if (!joins_.emplace(id, std::move(join)).second) {
                throw std::runtime_error {
                    "concurrent property attempt record is corrupt"
                };
            }
        }
    }

    // Advance to tick `now`; true when the sequence matched at `now`.
    bool step(const std::int64_t now)
    {
        now_ = now;
        matched_ = false;
        matches_.clear();
        matched_groups_.clear();
        visited_.clear();
        kept_.clear();
        std::deque<Thread> queue;
        std::vector<Thread> waiting;
        for (auto& thread : threads_) {
            // In a multiclocked sequence only the threads on a clock that
            // ticked advance. A thread switching clocks (or a sequence that
            // starts at a point) goes on at the first tick of its clock at or
            // after, or strictly after, its switch time (16.13.1): the time
            // step of the clock's last tick tells, so a tick earlier in the
            // same time step counts for an inclusive switch.
            if (plan_.multiclocked()) {
                if (thread.switching) {
                    const auto last = plan_.clock_times[thread.clock];
                    if (last < thread.switch_time
                        || (last == thread.switch_time
                            && !thread.switch_inclusive)) {
                        waiting.push_back(std::move(thread));
                        continue;
                    }
                    thread.switching = false;
                    thread.due_lo = plan_.clock_ticks[thread.clock];
                    thread.due_hi = thread.due_lo;
                } else if (!plan_.ticked[thread.clock]) {
                    waiting.push_back(std::move(thread));
                    continue;
                }
            }
            const auto tick = thread_now(thread);
            // A thread inside `throughout` checks its condition on every
            // tick after the operand started.
            const bool holds = std::ranges::all_of(thread.throughout,
                [&](const Throughout& entry) {
                    const bool kept = entry.from < 0 || entry.from > tick
                        || condition(entry, thread);
                    if (!kept) {
                        failed_ = { entry.line, entry.column };
                    }
                    return kept;
                });
            if (holds) {
                queue.push_back(std::move(thread));
            }
        }
        threads_.clear();
        for (auto& thread : waiting) {
            keep(std::move(thread));
        }
        while (!queue.empty()) {
            auto thread = std::move(queue.front());
            queue.pop_front();
            run(std::move(thread), queue);
        }
        if (!matched_groups_.empty()) {
            std::erase_if(threads_, [&](const Thread& thread) {
                return std::ranges::any_of(
                    thread.groups, [&](const std::uint32_t group) {
                        return matched_groups_.contains(group);
                    });
            });
        }
        collect_joins();
        return matched_;
    }

    [[nodiscard]] bool dead() const
    {
        return threads_.empty();
    }
    // The last boolean that ended a thread of this sequence: where an
    // attempt that no thread survives was rejected.
    [[nodiscard]] FailPosition failed() const
    {
        return failed_;
    }
    // The distinct valuations of the local variables the sequence matched
    // with at the last step (one empty valuation for a plan without
    // locals): each match continues separately (16.10 c).
    [[nodiscard]] const std::vector<std::vector<std::int64_t>>& matches() const
    {
        return matches_;
    }

private:
    // One step event of the attempt being advanced, when stepping is on for
    // it: a thread passed (or died at) the sampled boolean `guard` of the
    // program counter `from` (39.4.2 cbAssertionStepSuccess/Failure).
    void note_step(const bool failure, const std::uint32_t from,
        const std::uint32_t to, const std::int64_t guard) const
    {
        if (!plan_.stepping) {
            return;
        }
        const auto sequence = static_cast<std::uint32_t>(
            &sequence_ - plan_.sequences.data());
        const auto position = guard >= 0
                && static_cast<std::uint64_t>(guard) < plan_.guard_positions.size()
            ? plan_.guard_positions[static_cast<std::size_t>(guard)]
            : std::pair<std::uint32_t, std::uint32_t> { };
        plan_.events.push_back({ failure, plan_.attempt_start, plan_.time,
            plan_.state_id(sequence, from), plan_.state_id(sequence, to),
            guard, sequence, now_, position.first, position.second });
    }

    // The valuation of the thread's local variables.
    [[nodiscard]] std::span<const std::int64_t> locals_of(const Thread& thread) const
    {
        return std::span<const std::int64_t> { thread.counters }.subspan(
            sequence_.locals_base);
    }
    // A throughout condition at the current tick.
    [[nodiscard]] bool condition(const Throughout& entry, const Thread& thread) const
    {
        return entry.expression ? plan_.truth(entry.guard, locals_of(thread))
                                : plan_.guard(entry.guard);
    }

    // The current tick of the thread's clock.
    [[nodiscard]] std::int64_t thread_now(const Thread& thread) const
    {
        return plan_.multiclocked() ? plan_.clock_ticks[thread.clock]
                                    : now_;
    }

    struct Join {
        std::uint32_t kind { };
        Thread continuation;
        std::set<std::vector<std::int64_t>> ends[2];
    };

    void keep(Thread thread)
    {
        if (kept_.insert(thread.key()).second) {
            threads_.push_back(std::move(thread));
        }
    }

    // A thread at the end of an operand: true when it ends at this tick;
    // otherwise it waits or dies.
    bool settle(Thread& thread)
    {
        const auto end_lo = thread.due_lo - 1;
        const auto end_hi = thread.due_hi == unbounded
            ? unbounded : thread.due_hi - 1;
        if (end_hi < now_) {
            return false;
        }
        if (end_lo > now_) {
            keep(thread);
            return false;
        }
        if (end_hi > now_) {
            auto waiting = thread;
            waiting.due_lo = now_ + 2;
            keep(std::move(waiting));
        }
        thread.due_lo = now_ + 1;
        thread.due_hi = now_ + 1;
        return true;
    }

    void run(Thread thread, std::deque<Thread>& queue)
    {
        const auto& code = sequence_.code;
        now_ = thread_now(thread);
        while (thread.pc < code.size()) {
            const auto& step = code[thread.pc];
            switch (step.op) {
            case Op::check:
            case Op::check_expression: {
                if (thread.due_hi < now_) {
                    return;
                }
                if (thread.due_lo > now_) {
                    keep(std::move(thread));
                    return;
                }
                // A thread whose window stays open keeps a copy waiting: it
                // has not failed to progress (39.4.2).
                const bool lingers = thread.due_hi > now_;
                if (lingers) {
                    auto waiting = thread;
                    waiting.due_lo = now_ + 1;
                    keep(std::move(waiting));
                }
                // A check of an expression that reads local variables has no
                // sampled guard; its step reports no guard (-1).
                const bool expression = step.op == Op::check_expression;
                const auto checked = expression ? std::int64_t { -1 }
                                                : std::int64_t { step.a };
                if (!(expression ? plan_.truth(step.a, locals_of(thread))
                                 : plan_.guard(step.a))) {
                    failed_ = { step.line, step.column };
                    if (!lingers) {
                        note_step(true, thread.pc, thread.pc + 1U, checked);
                    }
                    return;
                }
                for (auto& entry : thread.throughout) {
                    if (entry.from < 0) {
                        entry.from = now_;
                        if (!condition(entry, thread)) {
                            failed_ = { entry.line, entry.column };
                            if (!lingers) {
                                note_step(true, thread.pc, thread.pc + 1U,
                                    entry.expression ? -1 : entry.guard);
                            }
                            return;
                        }
                    }
                }
                note_step(false, thread.pc, thread.pc + 1U, checked);
                thread.due_lo = now_ + 1;
                thread.due_hi = now_ + 1;
                ++thread.pc;
                break;
            }
            case Op::delay:
                thread.due_lo += step.lo - 1;
                thread.due_hi = step.hi == unbounded || thread.due_hi == unbounded
                    ? unbounded
                    : thread.due_hi + step.hi - 1;
                ++thread.pc;
                break;
            case Op::fork: {
                auto other = thread;
                other.pc = step.a;
                queue.push_back(std::move(other));
                ++thread.pc;
                break;
            }
            case Op::jump:
                thread.pc = step.a;
                break;
            case Op::counter_set:
                thread.counters[step.a] = 0;
                ++thread.pc;
                break;
            case Op::counter_branch: {
                // A loop that returns with an unchanged state matched
                // nothing new; stop it.
                if (!visited_.insert(thread.key()).second) {
                    return;
                }
                const auto count = thread.counters[step.a];
                const bool leave = count >= step.lo;
                const bool repeat = step.hi == unbounded || count < step.hi;
                if (leave && repeat) {
                    auto other = thread;
                    other.pc = step.b;
                    queue.push_back(std::move(other));
                    ++thread.pc;
                } else if (leave) {
                    thread.pc = step.b;
                } else if (repeat) {
                    ++thread.pc;
                } else {
                    return;
                }
                break;
            }
            case Op::counter_increment: {
                auto count = thread.counters[step.a] + 1;
                if (step.hi == unbounded) {
                    count = std::min(count, std::max<std::int64_t>(step.lo, 1));
                }
                thread.counters[step.a] = count;
                ++thread.pc;
                break;
            }
            case Op::throughout_begin:
                thread.throughout.push_back(
                    { step.a, step.c == 1U, -1, step.line, step.column });
                ++thread.pc;
                break;
            case Op::assign: {
                // The initialization assignment of a named sequence
                // instance's local variable happens at the first tick of
                // the instance's evaluation (16.10): a thread whose window
                // opens later waits, and one whose window stays open keeps
                // a copy for the later ticks, as at a check.
                if (step.c == 1U) {
                    if (thread.due_hi < now_) {
                        return;
                    }
                    if (thread.due_lo > now_) {
                        keep(std::move(thread));
                        return;
                    }
                    if (thread.due_hi > now_) {
                        auto waiting = thread;
                        waiting.due_lo = now_ + 1;
                        keep(std::move(waiting));
                    }
                    thread.due_lo = now_;
                    thread.due_hi = now_;
                }
                const auto value = plan_.evaluate(step.b, locals_of(thread));
                std::ranges::copy(value,
                    thread.counters.begin() + sequence_.locals_base
                        + static_cast<std::ptrdiff_t>(plan_.local_offset[step.a]));
                ++thread.pc;
                break;
            }
            case Op::call: {
                // A subroutine call at an end point of the subsequence it is
                // attached to (16.11): queued with the thread's locals.
                std::vector<std::int64_t> entry { step.a };
                const auto locals = locals_of(thread);
                entry.insert(entry.end(), locals.begin(), locals.end());
                plan_.storage->push_call(entry);
                ++thread.pc;
                break;
            }
            case Op::throughout_end:
                if (!settle(thread)) {
                    return;
                }
                if (!thread.throughout.empty()) {
                    thread.throughout.pop_back();
                }
                ++thread.pc;
                break;
            case Op::first_match_begin:
                thread.groups.push_back(next_group_++);
                ++thread.pc;
                break;
            case Op::first_match_end:
                if (!settle(thread)) {
                    return;
                }
                if (!thread.groups.empty()) {
                    matched_groups_.insert(thread.groups.back());
                    thread.groups.pop_back();
                }
                ++thread.pc;
                break;
            case Op::join_begin: {
                const auto id = next_join_++;
                Join join;
                join.kind = step.a;
                join.continuation = thread;
                join.continuation.pc = step.c;
                joins_.emplace(id, std::move(join));
                auto right = thread;
                right.joins.push_back({ id, 1U });
                right.pc = step.b;
                queue.push_back(std::move(right));
                thread.joins.push_back({ id, 0U });
                ++thread.pc;
                break;
            }
            case Op::join_end: {
                if (!settle(thread) || thread.joins.empty()) {
                    return;
                }
                const auto reference = thread.joins.back();
                const auto found = joins_.find(reference.join);
                if (found == joins_.end()) {
                    return;
                }
                auto& join = found->second;
                std::vector<std::int64_t> end { now_ };
                const auto locals = locals_of(thread);
                end.insert(end.end(), locals.begin(), locals.end());
                join.ends[reference.side].insert(end);
                // The operands' threads merge into one per pair of ends
                // (16.9.5, 16.9.6): a local variable the operand assigned
                // and that flows out takes the operand's value, the others
                // keep the value they had when the composite began (16.10
                // d; F.5.4). `b` names this operand's local set and `c` the
                // other's. Without locals there is one continuation.
                const auto& other = join.ends[1U - reference.side];
                std::set<std::vector<std::int64_t>> merged;
                for (const auto& entry : other) {
                    if (join.kind == 0U ? entry.front() > now_
                                        : entry.front() != now_) {
                        continue;
                    }
                    auto valuation = std::vector<std::int64_t>(
                        join.continuation.counters.begin() + sequence_.locals_base,
                        join.continuation.counters.end());
                    const auto take = [&](const std::uint32_t set,
                                          const std::vector<std::int64_t>& from) {
                        if (set == 0U) {
                            return;
                        }
                        for (const auto local : plan_.local_sets[set - 1U]) {
                            const auto at = plan_.local_offset[local];
                            std::copy_n(from.begin() + 1
                                    + static_cast<std::ptrdiff_t>(at),
                                plan_.local_size[local],
                                valuation.begin() + static_cast<std::ptrdiff_t>(at));
                        }
                    };
                    take(step.b, end);
                    take(step.c, entry);
                    merged.insert(std::move(valuation));
                }
                for (const auto& valuation : merged) {
                    auto continuation = join.continuation;
                    continuation.due_lo = now_ + 1;
                    continuation.due_hi = now_ + 1;
                    std::ranges::copy(valuation,
                        continuation.counters.begin() + sequence_.locals_base);
                    queue.push_back(std::move(continuation));
                }
                return;
            }
            case Op::match:
                if (settle(thread)) {
                    matched_ = true;
                    const auto locals = locals_of(thread);
                    if (std::ranges::none_of(matches_, [&](const auto& match) {
                            return std::ranges::equal(match, locals);
                        })) {
                        matches_.emplace_back(locals.begin(), locals.end());
                    }
                }
                return;
            case Op::clock: {
                // The operand before ended at this tick; the rest starts on
                // the clock's first tick strictly after this time step
                // (`##1`) or at or after it (`##0`) (16.13.1).
                const auto target = step.a;
                const bool inclusive = step.lo == 0;
                thread.clock = target;
                ++thread.pc;
                if (inclusive && plan_.clock_times[target] == plan_.time) {
                    thread.due_lo = plan_.clock_ticks[target];
                    thread.due_hi = thread.due_lo;
                    now_ = thread.due_lo;
                    break;
                }
                thread.switching = true;
                thread.switch_inclusive = inclusive;
                thread.switch_time = plan_.time;
                keep(std::move(thread));
                return;
            }
            }
        }
    }

    // Drops joins no thread can still reach, and threads that must end
    // through a join that can never pair again: an `intersect` needs both
    // operands to end at one tick, an `and` an end on each side (16.9.5,
    // 16.9.6). Without the pruning, an operand that can no longer match
    // would keep the other side's unbounded threads (`within`) alive.
    void collect_joins()
    {
        if (joins_.empty()) {
            return;
        }
        std::map<std::uint32_t, std::array<bool, 2>> live_sides;
        const auto mark = [&](const Thread& thread) {
            bool changed { };
            for (const auto& reference : thread.joins) {
                auto& sides = live_sides[reference.join];
                if (!sides[reference.side]) {
                    sides[reference.side] = true;
                    changed = true;
                }
            }
            return changed;
        };
        for (const auto& thread : threads_) {
            static_cast<void>(mark(thread));
        }
        const auto productive = [&](const std::uint32_t id, const Join& join) {
            const auto found = live_sides.find(id);
            const std::array<bool, 2> sides = found == live_sides.end()
                ? std::array<bool, 2> { }
                : found->second;
            if (join.kind != 0U) {
                return sides[0] && sides[1];
            }
            return (sides[0] || sides[1])
                && (sides[0] || !join.ends[0].empty())
                && (sides[1] || !join.ends[1].empty());
        };
        // A productive join's continuation keeps its enclosing joins'
        // sides live.
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [id, join] : joins_) {
                if (productive(id, join) && mark(join.continuation)) {
                    changed = true;
                }
            }
        }
        std::erase_if(joins_, [&](const auto& item) {
            return !productive(item.first, item.second);
        });
        std::erase_if(threads_, [&](const Thread& thread) {
            return std::ranges::any_of(
                thread.joins, [&](const JoinReference& reference) {
                    return !joins_.contains(reference.join);
                });
        });
    }

    const CompiledPlan& plan_;
    const CompiledSequence& sequence_;
    std::vector<Thread> threads_;
    std::map<std::uint32_t, Join> joins_;
    std::set<std::vector<std::int64_t>> kept_;
    std::set<std::vector<std::int64_t>> visited_;
    std::unordered_set<std::uint32_t> matched_groups_;
    std::uint32_t next_join_ { 1 };
    std::uint32_t next_group_ { 1 };
    std::int64_t now_ { };
    bool matched_ { };
    std::vector<std::vector<std::int64_t>> matches_;
    FailPosition failed_;
};

enum class Verdict : std::uint8_t { pending, holds, fails };

} // namespace

class Instance {
public:
    // `point` is 0 for a singly clocked plan, whose `start` is a tick;
    // otherwise `start` is a time step and `point` says how the instance
    // starts there (at_point, point_inclusive).
    // `locals` is the valuation of the local variables the instance starts
    // with (16.10): its parent's, or the match of an antecedent.
    Instance(const CompiledPlan& plan, const std::uint32_t node,
        const std::int64_t start, const std::int64_t point = 0,
        std::vector<std::int64_t> locals = { })
        : plan_(plan)
        , node_(node)
        , start_(start)
        , point_(point)
        , locals_(std::move(locals))
    {
        locals_.resize(plan.locals_words, 0);
    }

    [[nodiscard]] bool decided() const
    {
        return verdict_ != Verdict::pending;
    }
    [[nodiscard]] bool holds() const
    {
        return verdict_ == Verdict::holds;
    }
    [[nodiscard]] bool vacuous() const
    {
        return vacuous_;
    }
    [[nodiscard]] std::int64_t start() const
    {
        return start_;
    }
    // Where a failed attempt was rejected: the innermost failed node.
    [[nodiscard]] FailPosition fail_position() const
    {
        return fail_;
    }

    // The instance tree as record words, preorder: property node, start,
    // flags (verdict 0-1, vacuous 2, matched_once 3, antecedent_done 4,
    // satisfied 5, until_stopped 6, has matcher 7, has chosen 8, at_point 9,
    // point_inclusive 10, initialized 11, nonvacuous 12), chosen
    // branch, consequent count, the failed node's position (line, column),
    // then the counts of operands, started
    // children and until entries, the valuation of the local variables
    // (16.10; the plan's local words, none without locals), the matcher (if
    // any), the operands
    // (position, instance), the started instances and the until
    // (left, right) pairs.
    void encode(std::vector<std::int64_t>& out) const
    {
        out.push_back(node_);
        out.push_back(start_);
        out.push_back(static_cast<std::int64_t>(verdict_)
            | (vacuous_ ? 4 : 0) | (matched_once_ ? 8 : 0)
            | (antecedent_done_ ? 16 : 0) | (satisfied_ ? 32 : 0)
            | (until_stopped_ ? 64 : 0) | (matcher_ ? 128 : 0)
            | (chosen_ ? 256 : 0) | (initialized_ ? 2048 : 0)
            | (nonvacuous_ ? 4096 : 0) | point_);
        out.push_back(chosen_ ? static_cast<std::int64_t>(*chosen_) : 0);
        out.push_back(static_cast<std::int64_t>(consequents_));
        out.push_back(fail_.line);
        out.push_back(fail_.column);
        out.push_back(static_cast<std::int64_t>(operands_.size()));
        out.push_back(static_cast<std::int64_t>(started_.size()));
        out.push_back(static_cast<std::int64_t>(until_.size()));
        out.insert(out.end(), locals_.begin(), locals_.end());
        if (matcher_) {
            matcher_->encode(out);
        }
        for (const auto& [position, operand] : operands_) {
            out.push_back(static_cast<std::int64_t>(position));
            operand->encode(out);
        }
        for (const auto& item : started_) {
            item->encode(out);
        }
        for (const auto& entry : until_) {
            entry.left->encode(out);
            entry.right->encode(out);
        }
    }

    [[nodiscard]] static std::unique_ptr<Instance> decode(
        const CompiledPlan& plan, RecordReader& in, const unsigned depth)
    {
        const auto corrupt = [] {
            return std::runtime_error {
                "concurrent property attempt record is corrupt"
            };
        };
        // Nested records continue on a new stack segment when the stack is
        // nearly used up, so the nesting depth is bounded by memory.
        if (!support::stack_growth_detail::has_room()) {
            return support::with_stack(
                [&] { return decode(plan, in, depth); });
        }
        const auto node = in.index(plan.properties.size());
        const auto start = in.next();
        auto instance = std::make_unique<Instance>(plan, node, start);
        const auto& property = plan.properties[node];
        const auto flags = in.next();
        if ((flags & 3) > 2 || flags < 0 || flags >= 8192
            || ((flags & point_inclusive) != 0 && (flags & at_point) == 0)
            || ((flags & at_point) != 0 && !plan.multiclocked())) {
            throw corrupt();
        }
        instance->point_ = flags & (at_point | point_inclusive);
        instance->verdict_ = static_cast<Verdict>(flags & 3);
        instance->vacuous_ = (flags & 4) != 0;
        instance->matched_once_ = (flags & 8) != 0;
        instance->antecedent_done_ = (flags & 16) != 0;
        instance->satisfied_ = (flags & 32) != 0;
        instance->until_stopped_ = (flags & 64) != 0;
        instance->initialized_ = (flags & 2048) != 0;
        instance->nonvacuous_ = (flags & 4096) != 0;
        const auto chosen = in.next();
        if ((flags & 256) != 0) {
            if (chosen < 0
                || static_cast<std::uint64_t>(chosen) >= property.children.size()) {
                throw corrupt();
            }
            instance->chosen_ = static_cast<std::size_t>(chosen);
        }
        const auto consequents = in.next();
        if (consequents < 0) {
            throw corrupt();
        }
        instance->consequents_ = static_cast<std::size_t>(consequents);
        instance->fail_.line
            = in.index(std::numeric_limits<std::uint32_t>::max());
        instance->fail_.column
            = in.index(std::numeric_limits<std::uint32_t>::max());
        const auto operands = in.count();
        const auto started = in.count();
        const auto until = in.count();
        for (auto& word : instance->locals_) {
            word = in.next();
        }
        if ((flags & 128) != 0) {
            if (property.sequence >= plan.sequences.size()) {
                throw corrupt();
            }
            instance->matcher_ = std::make_unique<Matcher>(
                plan, plan.sequences[property.sequence]);
            instance->matcher_->decode(in);
        }
        for (std::size_t index = 0; index < operands; ++index) {
            const auto position = in.index(property.children.size());
            instance->operands_.emplace_back(
                position, decode(plan, in, depth + 1U));
        }
        for (std::size_t index = 0; index < started; ++index) {
            instance->started_.push_back(decode(plan, in, depth + 1U));
        }
        for (std::size_t index = 0; index < until; ++index) {
            auto left = decode(plan, in, depth + 1U);
            auto right = decode(plan, in, depth + 1U);
            instance->until_.push_back({ std::move(left), std::move(right) });
        }
        return instance;
    }

    // `cover sequence`: whether the sequence matched at this tick; the
    // attempt ends once no thread is left.
    bool step_every_match(const std::int64_t now)
    {
        const auto& property = plan_.properties[node_];
        if (!matcher_) {
            matcher_ = new_matcher(property.sequence);
        }
        const bool matched = matcher_->step(now);
        matched_once_ = matched_once_ || matched;
        if (matcher_->dead()) {
            verdict_ = Verdict::fails;
        }
        return matched;
    }

    // Whether a `cover sequence` attempt matched at least once.
    [[nodiscard]] bool matched_once() const
    {
        return matched_once_;
    }

    void step(const std::int64_t now)
    {
        // A singly clocked instance waits for its start tick; an instance of
        // a multiclocked plan steps at every evaluation and waits for its
        // clock itself (16.13).
        const bool multi = plan_.multiclocked();
        if (decided() || (!multi && now < start_)) {
            return;
        }
        const auto& property = plan_.properties[node_];
        // A new copy of the property's local variables gets its
        // initialization assignments when the instance's evaluation begins,
        // in declaration order, with the sampled values of that time step
        // (16.10).
        if (!initialized_ && !property.initializers.empty()) {
            initialized_ = true;
            for (const auto initializer : property.initializers) {
                const auto local = plan_.expression_target[initializer] - 1U;
                const auto value = plan_.evaluate(initializer, locals_);
                std::ranges::copy(value,
                    locals_.begin()
                        + static_cast<std::ptrdiff_t>(plan_.local_offset[local]));
            }
        }
        switch (property.kind) {
        case PropertyKind::sequence: {
            if (!matcher_) {
                matcher_ = new_matcher(property.sequence);
            }
            if (matcher_->step(now)) {
                resolve(true, false);
            } else if (matcher_->dead()) {
                fail_ = matcher_->failed();
                resolve(false, false);
            }
            break;
        }
        case PropertyKind::negation: {
            auto& operand = child(0, start_, point_);
            operand.step(now);
            if (operand.decided()) {
                // `not p` fails because p held: the property itself is the
                // failed node.
                fail_ = own_position();
                resolve(!operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::conjunction:
        case PropertyKind::disjunction:
        case PropertyKind::implies:
        case PropertyKind::equivalence: {
            auto& left = child(0, start_, point_);
            auto& right = child(1, start_, point_);
            left.step(now);
            right.step(now);
            combine(property.kind, left, right);
            break;
        }
        case PropertyKind::implication:
        case PropertyKind::followed_by:
            step_consequents(property, now);
            break;
        case PropertyKind::conditional: {
            if (multi && !chosen_) {
                step_condition(property, now);
                break;
            }
            if (!chosen_) {
                const bool condition = holds_condition(property);
                const auto branch = condition ? 0U : 1U;
                if (branch >= property.children.size()) {
                    resolve(true, true);
                    break;
                }
                chosen_ = branch;
            }
            auto& operand = child(*chosen_, start_, point_);
            operand.step(now);
            if (operand.decided()) {
                fail_ = operand.fail_;
                resolve(operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::nexttime: {
            if (multi) {
                // nexttime[n] p (F.5.1.2): p starts at the n-th tick of the
                // clock after the first one that reaches the start point.
                const bool ticks = clock_tick();
                if (point_ != 0
                    || (!has_operand(0U)
                        && (!ticks || clock_now() < add(start_, property.lo)))) {
                    break;
                }
            }
            auto& operand = multi
                ? child(0, plan_.time, at_point | point_inclusive)
                : child(0, add(start_, property.lo), 0);
            operand.step(now);
            if (operand.decided()) {
                fail_ = operand.fail_;
                resolve(operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::always:
        case PropertyKind::eventually:
            step_window(property, now);
            break;
        case PropertyKind::until:
            step_until(now);
            break;
        case PropertyKind::accept:
        case PropertyKind::reject: {
            // A multiclocked plan samples an asynchronous abort condition at
            // every evaluation from the start point on, and a synchronous
            // one only at the ticks of the operator's clock (F.5.1.2:
            // sync_accept_on (b) is accept_on (b && c)).
            const bool sampled = !multi
                || (reached_time(plan_.time)
                    && (!property.synchronous
                        || plan_.clock_times[property.clock] == plan_.time));
            if (sampled && plan_.guard(property.guard)) {
                const bool accept = property.kind == PropertyKind::accept;
                fail_ = own_position();
                resolve(accept, accept);
                break;
            }
            auto& operand = child(0, start_, point_);
            operand.step(now);
            if (operand.decided()) {
                fail_ = operand.fail_;
                resolve(operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::vacuous:
            resolve(true, true);
            break;
        }
        if (decided() || !plan_.recursive) {
            return;
        }
        // An operand whose outcome is now exactly that of one of its own
        // operands gives way to it: a recursive property (16.12.17) then
        // keeps a short chain of instances however long it runs.
        const auto collapse = [now](std::unique_ptr<Instance>& operand) {
            while (auto next = operand->forward(now)) {
                operand = std::move(next);
            }
        };
        for (auto& entry : operands_) {
            collapse(entry.second);
        }
        for (auto& item : started_) {
            collapse(item);
        }
    }

    // The operand this undecided instance's outcome now is exactly, taken
    // out of it: its verdict, its failure and, with this instance's floor
    // carried over, its vacuity. None when the instance still combines
    // operands, waits for an antecedent, or is a nexttime whose operand has
    // not started (at the end of the run that nexttime holds, 16.12.10,
    // whatever the operand's empty trace gives).
    [[nodiscard]] std::unique_ptr<Instance> forward(const std::int64_t now)
    {
        if (!plan_.recursive || decided() || plan_.multiclocked()) {
            return nullptr;
        }
        std::unique_ptr<Instance>* sole { };
        bool nonvacuous = nonvacuous_;
        switch (plan_.properties[node_].kind) {
        case PropertyKind::conjunction:
            // `p and q` once p held: q's outcome, nonvacuous if p was.
            if (operands_.size() == 2U && started_.empty()) {
                for (std::size_t index = 0; index < 2U; ++index) {
                    const auto& held = operands_[index].second;
                    auto& other = operands_[1U - index].second;
                    if (held->decided() && held->holds() && !other->decided()) {
                        sole = &other;
                        nonvacuous = nonvacuous || !held->vacuous();
                        break;
                    }
                }
            }
            break;
        case PropertyKind::implication:
            // `s |-> p` once s can match no more and one consequent is left.
            if (antecedent_done_ && !matcher_ && operands_.empty()
                && started_.size() == 1U && !started_.front()->decided()) {
                sole = &started_.front();
                nonvacuous = nonvacuous || satisfied_;
            }
            break;
        case PropertyKind::conditional:
        case PropertyKind::nexttime:
            // A strong nexttime keeps its own end-of-run obligation (16.12.10).
            if (operands_.size() == 1U && started_.empty()
                && !(plan_.properties[node_].kind == PropertyKind::nexttime
                    && plan_.properties[node_].strong)
                && (plan_.properties[node_].kind == PropertyKind::nexttime
                    || (chosen_ && operands_.front().first == *chosen_))
                && (plan_.properties[node_].kind != PropertyKind::nexttime
                    || operands_.front().second->start_ <= now)
                && !operands_.front().second->decided()) {
                sole = &operands_.front().second;
            }
            break;
        default:
            break;
        }
        if (sole == nullptr) {
            return nullptr;
        }
        auto result = std::move(*sole);
        result->nonvacuous_ = result->nonvacuous_ || nonvacuous;
        return result;
    }

    // The verdict at the end of simulation: weak obligations hold, strong
    // obligations that did not complete fail (16.12.2).
    [[nodiscard]] bool finish(const std::int64_t now) const
    {
        if (decided()) {
            return holds();
        }
        const auto& property = plan_.properties[node_];
        // A multiclocked instance whose clock never reached its start point
        // saw an empty trace; the operators that count ticks compare the
        // tick count of their own clock.
        const bool multi = plan_.multiclocked();
        if (multi ? point_ != 0 && counts_ticks(property.kind)
                  : now < start_) {
            return empty_trace(node_);
        }
        const auto at = multi && point_ == 0 ? clock_now() : now;
        const auto child_finish = [&](const std::size_t index,
                                      const std::int64_t start) {
            const auto found = std::ranges::find_if(
                operands_, [&](const auto& item) {
                    return item.first == index;
                });
            if (found == operands_.end()) {
                return start > now
                    ? empty_trace(property.children[index])
                    : empty_trace(property.children[index]);
            }
            return found->second->finish(now);
        };
        switch (property.kind) {
        case PropertyKind::sequence:
            return !property.strong;
        case PropertyKind::negation:
            return !child_finish(0, start_);
        case PropertyKind::conjunction:
            return child_finish(0, start_) && child_finish(1, start_);
        case PropertyKind::disjunction:
            return child_finish(0, start_) || child_finish(1, start_);
        case PropertyKind::implies:
            return !child_finish(0, start_) || child_finish(1, start_);
        case PropertyKind::equivalence:
            return child_finish(0, start_) == child_finish(1, start_);
        case PropertyKind::implication:
            return std::ranges::all_of(started_, [&](const auto& item) {
                return item->finish(now);
            });
        case PropertyKind::followed_by:
            return std::ranges::any_of(started_, [&](const auto& item) {
                return item->finish(now);
            });
        case PropertyKind::conditional:
            return !chosen_ || child_finish(*chosen_, start_);
        case PropertyKind::nexttime:
            // The clock tick the operand needs never came: s_nexttime
            // fails and nexttime holds (16.12.10).
            if (at < add(start_, property.lo)) {
                return !property.strong;
            }
            return child_finish(0, add(start_, property.lo));
        case PropertyKind::always: {
            const bool complete = property.hi != unbounded
                && at >= add(start_, property.hi);
            return std::ranges::all_of(started_, [&](const auto& item) {
                return item->finish(now);
            }) && (complete || !property.strong);
        }
        case PropertyKind::eventually: {
            const bool complete = property.hi != unbounded
                && at >= add(start_, property.hi);
            return std::ranges::any_of(started_, [&](const auto& item) {
                return item->finish(now);
            }) || (!complete && !property.strong);
        }
        case PropertyKind::until: {
            bool earlier = true;
            bool violated { };
            for (const auto& entry : until_) {
                const bool right = entry.right->finish(now);
                if (earlier && right) {
                    return true;
                }
                const bool left = entry.left->finish(now);
                if (!left && !violated) {
                    violated = true;
                }
                earlier = earlier && left;
                if (violated) {
                    break;
                }
            }
            return !property.strong && !violated;
        }
        case PropertyKind::accept:
        case PropertyKind::reject:
            return child_finish(0, start_);
        case PropertyKind::vacuous:
            return true;
        }
        return true;
    }

private:
    struct UntilEntry {
        std::unique_ptr<Instance> left;
        std::unique_ptr<Instance> right;
    };

    // A conditional's condition: a sampled guard, or an expression that
    // reads the instance's local variables (16.10).
    [[nodiscard]] bool holds_condition(const CompiledProperty& property) const
    {
        return property.guard_expression
            ? plan_.truth(property.guard, locals_)
            : plan_.guard(property.guard);
    }

    [[nodiscard]] FailPosition own_position() const
    {
        const auto& property = plan_.properties[node_];
        return { property.line, property.column };
    }

    // ---- multiclocked plans (16.13) ----

    // The operators that count ticks of their clock and align to it.
    [[nodiscard]] static bool counts_ticks(const PropertyKind kind)
    {
        switch (kind) {
        case PropertyKind::conditional:
        case PropertyKind::nexttime:
        case PropertyKind::always:
        case PropertyKind::eventually:
        case PropertyKind::until:
            return true;
        default:
            return false;
        }
    }
    [[nodiscard]] std::uint32_t clock() const
    {
        return plan_.properties[node_].clock;
    }
    // The tick count of the node's clock.
    [[nodiscard]] std::int64_t clock_now() const
    {
        return plan_.clock_ticks[clock()];
    }
    // Whether time step `time` is at or after the start point.
    [[nodiscard]] bool reached_time(const std::int64_t time) const
    {
        return time > start_
            || (time == start_ && (point_ & point_inclusive) != 0);
    }
    // An operator that counts ticks aligns at the first tick of its clock
    // that reaches its start point (the nearest tick, 16.13.1): its start
    // becomes that tick's count. The result is whether the operator acts at
    // this evaluation: at the tick that aligned it (which may have come
    // earlier in the same time step) and at each later tick of its clock.
    [[nodiscard]] bool clock_tick()
    {
        if (point_ != 0) {
            if (!reached_time(plan_.clock_times[clock()])) {
                return false;
            }
            start_ = clock_now();
            point_ = 0;
            return true;
        }
        return plan_.ticked[clock()];
    }
    [[nodiscard]] bool has_operand(const std::size_t index) const
    {
        return std::ranges::any_of(operands_, [&](const auto& item) {
            return item.first == index;
        });
    }
    // The matcher of a sequence that starts with this instance: at its start
    // tick, or for a multiclocked plan at its start point on the node's
    // clock.
    [[nodiscard]] std::unique_ptr<Matcher> new_matcher(
        const std::uint32_t sequence) const
    {
        const auto& compiled = plan_.sequences[sequence];
        if (point_ != 0) {
            return std::make_unique<Matcher>(plan_, compiled, clock(), start_,
                (point_ & point_inclusive) != 0, locals_);
        }
        return std::make_unique<Matcher>(plan_, compiled, start_, locals_);
    }

    // `if (b) p else q` of a multiclocked plan before b is known (16.13.2;
    // F.3.4.3.4: (b |-> p) and (weak(b) or q)). b is sampled at the first
    // tick of the operator's clock that reaches its start point, and p
    // starts at that tick; q starts at the start point itself, so it runs
    // while b waits and is dropped when b holds.
    void step_condition(const CompiledProperty& property,
        const std::int64_t now)
    {
        const bool otherwise = property.children.size() > 1U;
        Instance* other = otherwise ? &child(1U, start_, point_) : nullptr;
        if (other != nullptr) {
            other->step(now);
        }
        if (!clock_tick()) {
            return;
        }
        if (holds_condition(property)) {
            std::erase_if(operands_,
                [](const auto& item) { return item.first == 1U; });
            chosen_ = 0U;
            auto& operand = child(0U, plan_.time, at_point | point_inclusive);
            operand.step(now);
            if (operand.decided()) {
                fail_ = operand.fail_;
                resolve(operand.holds(), operand.vacuous());
            }
            return;
        }
        if (other == nullptr) {
            resolve(true, true);
            return;
        }
        chosen_ = 1U;
        if (other->decided()) {
            fail_ = other->fail_;
            resolve(other->holds(), other->vacuous());
        }
    }

    // The value of a property over an empty remaining trace.
    [[nodiscard]] bool empty_trace(const std::uint32_t node) const
    {
        const auto& property = plan_.properties[node];
        const auto operand = [&](const std::size_t index) {
            return index < property.children.size()
                && empty_trace(property.children[index]);
        };
        switch (property.kind) {
        case PropertyKind::sequence:
        case PropertyKind::nexttime:
        case PropertyKind::always:
        case PropertyKind::eventually:
        case PropertyKind::until:
            return !property.strong;
        case PropertyKind::negation:
            return !operand(0);
        case PropertyKind::conjunction:
            return operand(0) && operand(1);
        case PropertyKind::disjunction:
            return operand(0) || operand(1);
        case PropertyKind::implies:
            return !operand(0) || operand(1);
        case PropertyKind::equivalence:
            return operand(0) == operand(1);
        case PropertyKind::implication:
        case PropertyKind::conditional:
        case PropertyKind::vacuous:
            return true;
        case PropertyKind::followed_by:
            return false;
        case PropertyKind::accept:
        case PropertyKind::reject:
            return operand(0);
        }
        return true;
    }

    void resolve(const bool holds, const bool vacuous)
    {
        verdict_ = holds ? Verdict::holds : Verdict::fails;
        vacuous_ = holds && vacuous && !nonvacuous_;
        operands_.clear();
        started_.clear();
        until_.clear();
        matcher_.reset();
    }

    Instance& child(const std::size_t index, const std::int64_t start,
        const std::int64_t point)
    {
        for (auto& [position, instance] : operands_) {
            if (position == index) {
                return *instance;
            }
        }
        const auto& property = plan_.properties[node_];
        operands_.emplace_back(index,
            std::make_unique<Instance>(
                plan_, property.children[index], start, point, locals_));
        return *operands_.back().second;
    }

    void combine(const PropertyKind kind, const Instance& left,
        const Instance& right)
    {
        switch (kind) {
        case PropertyKind::conjunction:
            if ((left.decided() && !left.holds())
                || (right.decided() && !right.holds())) {
                fail_ = left.decided() && !left.holds() ? left.fail_
                                                        : right.fail_;
                resolve(false, false);
            } else if (left.decided() && right.decided()) {
                resolve(true, left.vacuous() && right.vacuous());
            }
            break;
        case PropertyKind::disjunction:
            if ((left.decided() && left.holds())
                || (right.decided() && right.holds())) {
                const bool vacuous
                    = !(left.decided() && left.holds() && !left.vacuous())
                    && !(right.decided() && right.holds()
                        && !right.vacuous());
                resolve(true, vacuous);
            } else if (left.decided() && right.decided()) {
                fail_ = right.fail_;
                resolve(false, false);
            }
            break;
        case PropertyKind::implies:
            if (left.decided() && !left.holds()) {
                resolve(true, true);
            } else if (right.decided() && right.holds()) {
                resolve(true, right.vacuous());
            } else if (left.decided() && right.decided()) {
                fail_ = right.fail_;
                resolve(false, false);
            }
            break;
        case PropertyKind::equivalence:
            if (left.decided() && right.decided()) {
                fail_ = own_position();
                resolve(left.holds() == right.holds(), false);
            }
            break;
        default:
            break;
        }
    }

    // `s |-> p`, `s #-# p`: one consequent per match of s.
    void step_consequents(const CompiledProperty& property,
        const std::int64_t now)
    {
        if (!matcher_ && !antecedent_done_) {
            matcher_ = new_matcher(property.sequence);
        }
        if (matcher_) {
            if (matcher_->step(now)) {
                // A multiclocked consequent starts at the end of the match:
                // at the nearest tick of its clock (`|->`), or the nearest
                // one strictly after it (`|=>`, 16.13.2). Each match with
                // its own valuation of the local variables starts its own
                // consequent with that valuation (16.10).
                for (const auto& match : matcher_->matches()) {
                    if (plan_.multiclocked()) {
                        started_.push_back(std::make_unique<Instance>(plan_,
                            property.children[0], plan_.time,
                            at_point
                                | (property.overlapped ? point_inclusive : 0),
                            match));
                    } else {
                        const auto start = property.overlapped ? now : now + 1;
                        started_.push_back(std::make_unique<Instance>(
                            plan_, property.children[0], start, 0, match));
                    }
                    ++consequents_;
                }
            }
            if (matcher_->dead()) {
                matcher_.reset();
                antecedent_done_ = true;
            }
        }
        const bool implication = property.kind == PropertyKind::implication;
        bool nonvacuous { };
        for (auto& consequent : started_) {
            consequent->step(now);
            if (!consequent->decided()) {
                continue;
            }
            if (implication && !consequent->holds()) {
                fail_ = consequent->fail_;
                resolve(false, false);
                return;
            }
            if (!implication && consequent->holds()) {
                resolve(true, consequent->vacuous());
                return;
            }
            nonvacuous = nonvacuous || !consequent->vacuous();
        }
        satisfied_ = satisfied_ || nonvacuous;
        std::erase_if(started_, [](const auto& consequent) {
            return consequent->decided();
        });
        if (antecedent_done_ && started_.empty()) {
            if (implication) {
                resolve(true, consequents_ == 0U || !satisfied_);
            } else {
                fail_ = own_position();
                resolve(false, false);
            }
        }
    }

    // always / eventually over a window of start ticks.
    void step_window(const CompiledProperty& property, const std::int64_t now)
    {
        // A multiclocked window counts ticks of the operator's clock from
        // the one that reaches its start point; an operand starts at each
        // tick of the window (F.3.4.3.8: always p is p until 0).
        const bool multi = plan_.multiclocked();
        const bool ticks = !multi || clock_tick();
        if (multi && point_ != 0) {
            return;
        }
        const auto at = multi ? clock_now() : now;
        const auto first = add(start_, property.lo);
        const auto last = add(start_, property.hi);
        if (ticks && at >= first && at <= last) {
            started_.push_back(multi
                    ? std::make_unique<Instance>(plan_, property.children[0],
                          plan_.time, at_point | point_inclusive, locals_)
                    : std::make_unique<Instance>(
                          plan_, property.children[0], now, 0, locals_));
        }
        const bool always = property.kind == PropertyKind::always;
        for (auto& operand : started_) {
            operand->step(now);
            if (!operand->decided()) {
                continue;
            }
            if (always && !operand->holds()) {
                fail_ = operand->fail_;
                resolve(false, false);
                return;
            }
            if (!always && operand->holds()) {
                resolve(true, operand->vacuous());
                return;
            }
        }
        std::erase_if(started_, [](const auto& operand) {
            return operand->decided();
        });
        if (last != unbounded && at >= last && started_.empty()) {
            fail_ = own_position();
            resolve(always, false);
        }
    }

    // p until q (until_with folds p into q): q holds at some tick k and p
    // at every tick before k; the weak forms also hold when q never does.
    void step_until(const std::int64_t now)
    {
        const auto& property = plan_.properties[node_];
        // A multiclocked `until` starts a pair at each tick of its clock from
        // the one that reaches its start point (F.5.1.2).
        const bool multi = plan_.multiclocked();
        const bool ticks = !multi || clock_tick();
        if (multi && point_ != 0) {
            return;
        }
        if (!until_stopped_ && ticks) {
            const auto start = multi ? plan_.time : now;
            const auto point = multi ? at_point | point_inclusive : 0;
            until_.push_back({
                std::make_unique<Instance>(
                    plan_, property.children[0], start, point, locals_),
                std::make_unique<Instance>(
                    plan_, property.children[1], start, point, locals_),
            });
        }
        for (auto& entry : until_) {
            entry.left->step(now);
            entry.right->step(now);
        }
        bool earlier = true;
        bool right_pending { };
        for (const auto& entry : until_) {
            const auto& left = *entry.left;
            const auto& right = *entry.right;
            if (right.decided() && right.holds()) {
                until_stopped_ = true;
                if (earlier) {
                    resolve(true, false);
                    return;
                }
            }
            right_pending = right_pending || !right.decided();
            if (left.decided() && !left.holds()) {
                until_stopped_ = true;
                if (!right_pending) {
                    fail_ = left.fail_;
                    resolve(false, false);
                }
                return;
            }
            earlier = earlier && left.decided();
        }
        // Entries whose left operand held and whose right operand failed
        // decide nothing further.
        while (!until_.empty() && until_.front().left->decided()
            && until_.front().left->holds()
            && until_.front().right->decided()
            && !until_.front().right->holds()) {
            until_.erase(until_.begin());
        }
    }

    const CompiledPlan& plan_;
    std::uint32_t node_ { };
    std::int64_t start_ { };
    // The start point of a multiclocked instance (at_point,
    // point_inclusive); 0 once aligned or for a singly clocked plan.
    std::int64_t point_ { };
    Verdict verdict_ { Verdict::pending };
    bool vacuous_ { };
    bool matched_once_ { };
    std::unique_ptr<Matcher> matcher_;
    FailPosition fail_;
    bool antecedent_done_ { };
    bool satisfied_ { };
    std::size_t consequents_ { };
    std::optional<std::size_t> chosen_;
    bool until_stopped_ { };
    bool initialized_ { };
    // The instance took the place of one whose success is nonvacuous
    // whatever this instance's is (forward).
    bool nonvacuous_ { };
    std::vector<std::int64_t> locals_;
    std::vector<std::pair<std::size_t, std::unique_ptr<Instance>>> operands_;
    std::vector<std::unique_ptr<Instance>> started_;
    std::vector<UntilEntry> until_;
};

Monitor::Monitor() = default;

Monitor::Monitor(std::string plan_text, std::vector<std::int64_t> bounds)
    : plan_text_(std::move(plan_text))
    , bounds_(std::move(bounds))
{
    build(bounds_);
}

Monitor::~Monitor() = default;

std::size_t Monitor::clock_count(const semantic::sva::Plan& plan)
{
    return plan.clock_guards.size();
}

void Monitor::build(const std::vector<std::int64_t>& bounds)
{
    auto decoded = semantic::sva::decode(plan_text_);
    if (!decoded) {
        throw std::runtime_error { "concurrent property plan is malformed" };
    }
    auto& plan = *decoded;
    if (plan.root >= plan.properties.size()) {
        throw std::runtime_error { "concurrent property plan has no root" };
    }
    plan_ = std::make_unique<CompiledPlan>();
    // The local variables (16.10): a valuation holds each local's words in
    // declaration order; a thread keeps it after its repetition counters.
    for (const auto& local : plan.locals) {
        plan_->local_offset.push_back(plan_->locals_words);
        plan_->local_size.push_back(semantic::sva::local_words(local.width));
        plan_->locals_words += plan_->local_size.back();
    }
    std::size_t result_words = 0U;
    for (const auto& expression : plan.expressions) {
        plan_->expression_target.push_back(expression.target);
        result_words = std::max(result_words,
            expression.target == 0U ? std::size_t { 1 }
                                    : plan_->local_size[expression.target - 1U]);
    }
    plan_->local_sets = plan.local_sets;
    std::uint32_t base = 0;
    for (const auto& sequence : plan.sequences) {
        CompiledSequence compiled;
        compiled.locals_base = sequence.counters;
        compiled.counters = sequence.counters
            + static_cast<std::uint32_t>(plan_->locals_words);
        for (const auto& instruction : sequence.code) {
            compiled.code.push_back({ instruction.op, instruction.a,
                instruction.b, instruction.c,
                resolve(instruction.lo, bounds),
                resolve(instruction.hi, bounds), instruction.line,
                instruction.column });
        }
        plan_->state_base.push_back(base);
        base += static_cast<std::uint32_t>(compiled.code.size());
        plan_->sequences.push_back(std::move(compiled));
    }
    for (const auto& property : plan.properties) {
        plan_->properties.push_back({ property.kind, property.strong,
            property.overlapped, property.synchronous, property.clock,
            property.sequence, property.guard, property.children,
            resolve(property.lo, bounds), resolve(property.hi, bounds),
            property.line, property.column, property.guard_expression,
            property.initializers });
    }
    plan_->root = plan.root;
    plan_->guards = plan.guards;
    for (const auto& span : plan.guard_spans) {
        plan_->guard_positions.emplace_back(span.line, span.column);
    }
    // A plan with clock tick guards: a multiclocked one, or a singly
    // clocked one whose monitor also evaluates at time steps without a tick
    // of its clock (runtime::simir::SvaOperation::wake_signal).
    if (!plan.clock_guards.empty()) {
        plan_->clock_guards = plan.clock_guards;
        plan_->clock_ticks.assign(plan.clock_guards.size(), -1);
        plan_->clock_times.assign(plan.clock_guards.size(),
            std::numeric_limits<std::int64_t>::min());
        plan_->ticked.assign(plan.clock_guards.size(), false);
    }
    local_widths_.clear();
    for (const auto& local : plan.locals) {
        local_widths_.push_back(local.width);
    }
    plan_->every_match = plan.every_match
        && plan.properties[plan.root].kind == PropertyKind::sequence;
    plan_->async_abort = std::ranges::any_of(plan.properties,
        [](const auto& property) {
            return (property.kind == PropertyKind::accept
                       || property.kind == PropertyKind::reject)
                && !property.synchronous;
        });
    {
        std::vector<std::uint8_t> mark(plan_->properties.size());
        const auto cyclic = [&](const auto& self, const std::uint32_t node) -> bool {
            if (node >= mark.size() || mark[node] == 2U) {
                return false;
            }
            if (mark[node] == 1U) {
                return true;
            }
            mark[node] = 1U;
            for (const auto child : plan_->properties[node].children) {
                if (self(self, child)) {
                    return true;
                }
            }
            mark[node] = 2U;
            return false;
        };
        plan_->recursive = cyclic(cyclic, plan_->root);
    }
    storage_ = AttemptStorage(clock_count(plan));
    storage_.set(layout::h_tick, -1);
    storage_.set(layout::h_flags,
        (plan_->every_match ? layout::flag_every_match : 0)
            | (plan_->multiclocked() ? layout::flag_multiclocked : 0));
    if (!bounds.empty()) {
        storage_.set_bounds(bounds);
    }
    if (!plan.expressions.empty()) {
        storage_.set_memo_stride(
            layout::memo_e_locals + plan_->locals_words + result_words);
    }
    plan_->storage = &storage_;
}

void Monitor::run_state_io(RunStateArchive& archive)
{
    archive.io(plan_text_);
    archive.io(bounds_);
    std::vector<std::int64_t> words;
    if (archive.saving()) {
        words = storage_.words();
    }
    archive.io(words);
    if (archive.saving() || !archive.ok()) {
        return;
    }
    try {
        build(bounds_);
    } catch (const std::runtime_error& failure) {
        archive.fail(failure.what());
        return;
    }
    auto adopted = AttemptStorage::adopt(std::move(words));
    if (!adopted || adopted->clocks() != storage_.clocks()
        || (adopted->get(layout::h_flags) & layout::flag_plan)
            != (storage_.get(layout::h_flags) & layout::flag_plan)) {
        archive.fail("assertion attempt block does not fit its plan");
        return;
    }
    // The kernel's table entry (if bound) follows the adopted block.
    auto slot = storage_.slot();
    storage_ = std::move(*adopted);
    storage_.bind_slot(std::move(slot));
}

std::unique_ptr<Monitor> Monitor::reference_copy() const
{
    auto copy = std::make_unique<Monitor>(plan_text_, bounds_);
    if (!copy->load_block(storage_.words())) {
        return nullptr;
    }
    return copy;
}

bool Monitor::load_block(std::vector<std::int64_t> words)
{
    auto adopted = AttemptStorage::adopt(std::move(words));
    if (!adopted || adopted->clocks() != storage_.clocks()) {
        return false;
    }
    auto slot = storage_.slot();
    storage_ = std::move(*adopted);
    storage_.bind_slot(std::move(slot));
    return true;
}

void Monitor::tick(const std::vector<bool>& guards, const bool start,
    const bool disabled, const std::int64_t time)
{
    // The block of this monitor (a moved monitor keeps its plan).
    plan_->storage = &storage_;
    // The calls of the tick before ran after it (16.11).
    storage_.clear_calls();
    // A plan without local variables needs no value from the assertion
    // process.
    if (plan_->expression_target.empty()) {
        advance(guards, start, disabled, time);
        return;
    }
    // A plan with local variables evaluates its local expressions through
    // the memo (16.10). A tick that needed a value the memo lacks did not
    // happen: the block is restored as it was, the requests stay in the
    // memo for the assertion process to answer, and the process runs the
    // tick again in the same time step with the same operands. Only the
    // tick that finds every value counts, so the attempt state, the outcome
    // queue, the attempt-start and step reports see one tick.
    // The memo keeps the answered values; a request left unanswered is
    // asked again if the tick still needs it.
    {
        const auto stride = storage_.memo_stride();
        std::vector<std::int64_t> answered;
        for (std::size_t index = 0; index < storage_.memo_count(); ++index) {
            const auto entry = std::as_const(storage_).memo_entry(index);
            if (entry[layout::memo_e_answered] != 0) {
                answered.insert(answered.end(), entry.begin(), entry.end());
            }
        }
        storage_.clear_memo();
        for (std::size_t at = 0; at < answered.size(); at += stride) {
            storage_.push_memo(
                std::span<const std::int64_t> { answered }.subspan(at, stride));
        }
    }
    auto before = storage_.words();
    plan_->missed = false;
    advance(guards, start, disabled, time);
    if (!plan_->missed) {
        storage_.clear_memo();
        return;
    }
    const auto stride = storage_.memo_stride();
    const auto used = static_cast<std::size_t>(storage_.get(layout::h_memo_used));
    const auto pending = static_cast<std::size_t>(storage_.get(layout::h_memo_pending));
    const auto first = used - pending * stride;
    const auto memo = storage_.memo_entry(0U).data();
    std::vector<std::int64_t> requests(memo + first, memo + used);
    const std::vector<std::int64_t> earlier(memo, memo + first);
    storage_.restore(std::move(before));
    storage_.clear_memo();
    for (std::size_t at = 0; at < earlier.size(); at += stride) {
        storage_.push_memo(std::span<const std::int64_t> { earlier }.subspan(at, stride));
    }
    for (std::size_t at = 0; at < requests.size(); at += stride) {
        storage_.push_memo(std::span<const std::int64_t> { requests }.subspan(at, stride));
    }
    storage_.set(layout::h_memo_pending, static_cast<std::int64_t>(pending));
    plan_->missed = false;
}

std::size_t Monitor::pending_requests() const noexcept
{
    return static_cast<std::size_t>(storage_.get(layout::h_memo_pending));
}

std::uint32_t Monitor::take_call()
{
    // The calls taken so far are the first h_calls_taken words; the last
    // one taken ends there (its locals are request_local's call_request).
    const auto calls = storage_.calls();
    const auto stride = 1U + plan_->locals_words;
    const auto cursor = static_cast<std::size_t>(storage_.get(layout::h_calls_taken));
    if (cursor + stride > calls.size()) {
        storage_.clear_calls();
        return 0U;
    }
    const auto call = static_cast<std::uint32_t>(calls[cursor]);
    storage_.set(layout::h_calls_taken, static_cast<std::int64_t>(cursor + stride));
    return call + 1U;
}

std::size_t Monitor::request_entry(const std::size_t request) const
{
    const auto pending = pending_requests();
    if (request >= pending) {
        throw std::runtime_error {
            "concurrent property local-variable request is out of range"
        };
    }
    return storage_.memo_count() - pending + request;
}

std::uint32_t Monitor::request_expression(const std::size_t request) const
{
    return static_cast<std::uint32_t>(
        storage_.memo_entry(request_entry(request))[layout::memo_e_expression]);
}

std::vector<std::int64_t> Monitor::request_local(
    const std::size_t request, const std::uint32_t local) const
{
    if (local >= plan_->local_offset.size()) {
        throw std::runtime_error {
            "concurrent property local variable is out of range"
        };
    }
    if (request == call_request) {
        const auto taken = static_cast<std::size_t>(storage_.get(layout::h_calls_taken));
        const auto stride = 1U + plan_->locals_words;
        if (taken < stride) {
            throw std::runtime_error {
                "concurrent property subroutine call local is read before a call"
            };
        }
        const auto words = storage_.calls().subspan(
            taken - stride + 1U + plan_->local_offset[local], plan_->local_size[local]);
        return { words.begin(), words.end() };
    }
    const auto entry = storage_.memo_entry(request_entry(request));
    const auto words = entry.subspan(
        layout::memo_e_locals + plan_->local_offset[local],
        plan_->local_size[local]);
    return { words.begin(), words.end() };
}

std::uint32_t Monitor::local_width(const std::uint32_t local) const
{
    return local < plan_->local_size.size()
        ? local_widths_[local]
        : 0U;
}

void Monitor::answer(
    const std::size_t request, const std::span<const std::int64_t> value)
{
    const auto index = request_entry(request);
    auto entry = storage_.memo_entry(index);
    const auto expression
        = static_cast<std::uint32_t>(entry[layout::memo_e_expression]);
    const auto words = plan_->result_words(expression);
    auto result = entry.subspan(layout::memo_e_locals + plan_->locals_words, words);
    std::ranges::fill(result, 0);
    std::copy_n(value.begin(), std::min(words, value.size()), result.begin());
    entry[layout::memo_e_answered] = 1;
}

void Monitor::advance(const std::vector<bool>& guards, const bool start,
    const bool disabled, const std::int64_t time)
{
    using namespace layout;
    plan_->values = &guards;
    plan_->time = time;
    storage_.set(h_time, time);
    bool leading = true;
    std::int64_t tick = storage_.get(h_tick);
    if (plan_->multiclocked()) {
        // A clock ticks at most once in a time step; its tick guard stays
        // set for the rest of the step (15.5.3).
        bool any { };
        for (std::size_t clock = 0; clock < plan_->clock_guards.size();
             ++clock) {
            const bool ticked = plan_->guard(plan_->clock_guards[clock])
                && storage_.clock_time(clock) != time;
            plan_->ticked[clock] = ticked;
            storage_.clock_ticked(clock) = ticked ? 1 : 0;
            if (ticked) {
                ++storage_.clock_ticks(clock);
                storage_.clock_time(clock) = time;
                any = true;
            }
            plan_->clock_ticks[clock] = storage_.clock_ticks(clock);
            plan_->clock_times[clock] = storage_.clock_time(clock);
        }
        leading = plan_->ticked[0];
        // Without a tick only the disable condition and the asynchronous
        // aborts, sampled from each attempt's start on, apply (16.12,
        // 16.12.14).
        if (!any && !disabled && !plan_->async_abort) {
            return;
        }
        // The property counts ticks of the leading clock.
        tick = plan_->clock_ticks[0];
    } else {
        ++tick;
    }
    storage_.set(h_tick, tick);
    plan_->now = tick;
    const bool fresh = start && leading;
    if (fresh) {
        storage_.push_started(time);
    }
    if (disabled) {
        // The disabled state ends the evaluation of every running attempt,
        // one disabled outcome each with that attempt's own start, and of
        // the attempt that just started (39.4.2).
        const auto running = storage_.attempt_count();
        for (std::size_t index = 0; index < running; ++index) {
            storage_.push_outcome({ static_cast<std::int64_t>(Outcome::disabled),
                storage_.attempt(index).started });
        }
        if (fresh) {
            storage_.push_outcome(
                { static_cast<std::int64_t>(Outcome::disabled), time });
        }
        storage_.clear_attempts();
        storage_.refresh_steps_active();
        return;
    }
    // A step request for an attempt that began before this tick can never
    // match one (39.5.2).
    storage_.expire_requests(time);
    const auto existing = storage_.attempt_count();
    if (fresh) {
        // The new attempt's row; its first record is written below.
        storage_.push_attempt({ time, 0, 0, 0, 0 });
    }
    const auto total = existing + (fresh ? 1U : 0U);
    const auto outcome_code = [](const Outcome outcome) {
        return static_cast<std::int64_t>(outcome);
    };
    // One pass: decode each attempt's record, advance it over this tick's
    // guards, and write the survivors' records into the alternate heap.
    storage_.begin_pass();
    std::size_t kept = 0;
    for (std::size_t index = 0; index < total; ++index) {
        auto row = storage_.attempt(index);
        std::unique_ptr<Instance> instance;
        if (index < existing) {
            RecordReader reader { storage_.record(row) };
            instance = Instance::decode(*plan_, reader, 0U);
        } else {
            row.serial = storage_.get(h_next_serial);
            storage_.set(h_next_serial, row.serial + 1);
            if (storage_.erase_requests(row.started)) {
                row.flags |= attempt_stepping;
            }
            // A multiclocked attempt starts at this time step, at the tick
            // of the leading clock (16.16).
            instance = plan_->multiclocked()
                ? std::make_unique<Instance>(*plan_, plan_->root, time,
                      at_point | point_inclusive)
                : std::make_unique<Instance>(*plan_, plan_->root, tick);
        }
        plan_->stepping = (row.flags & attempt_stepping) != 0;
        plan_->attempt_start = row.started;
        plan_->events.clear();
        if (plan_->every_match) {
            if (instance->step_every_match(tick)) {
                storage_.push_outcome(
                    { outcome_code(Outcome::success), row.started });
            }
            // An attempt that ends without a match is a failed cover
            // attempt.
            if (instance->decided() && !instance->matched_once()) {
                storage_.push_outcome(
                    { outcome_code(Outcome::failure), row.started });
            }
        } else {
            instance->step(tick);
            if (instance->decided()) {
                const auto where = instance->holds()
                    ? FailPosition { }
                    : instance->fail_position();
                storage_.push_outcome({ outcome_code(!instance->holds()
                                               ? Outcome::failure
                                               : instance->vacuous()
                                               ? Outcome::vacuous
                                               : Outcome::success),
                    row.started, where.line, where.column });
            }
        }
        for (const auto& event : plan_->events) {
            storage_.push_event(event);
        }
        if (instance->decided()) {
            continue;
        }
        scratch_.clear();
        instance->encode(scratch_);
        row.blob = storage_.append_record(scratch_);
        row.length = static_cast<std::int64_t>(scratch_.size());
        storage_.set_attempt(kept++, row);
    }
    storage_.truncate_attempts(kept);
    storage_.end_pass();
    plan_->events.clear();
    plan_->stepping = false;
    storage_.refresh_steps_active();
}

std::size_t Monitor::pending_outcomes() const
{
    return storage_.outcome_count();
}

Outcome Monitor::next_outcome(const std::int64_t serial)
{
    // The same pop as the compiled kernel's inline KOp::sva_outcome.
    const auto resolved = storage_.pop_outcome();
    if (!resolved) {
        return Outcome::none;
    }
    storage_.set(layout::h_last_start, resolved->started);
    storage_.set(layout::h_last_fail_line, resolved->fail_line);
    storage_.set(layout::h_last_fail_column, resolved->fail_column);
    storage_.set(layout::h_pop_serial, serial);
    return static_cast<Outcome>(resolved->outcome);
}

std::int64_t Monitor::reported_code(const Outcome outcome) const noexcept
{
    return outcome == Outcome::none
        ? 0
        : layout::outcome_code(static_cast<std::int64_t>(outcome),
              storage_.get(layout::h_flags));
}

void Monitor::set_actions_off(const std::uint32_t outcomes) noexcept
{
    using namespace layout;
    storage_.set(h_flags,
        (storage_.get(h_flags) & ~flag_actions_off)
            | ((static_cast<std::int64_t>(outcomes) << action_off_shift)
                & flag_actions_off));
}

Monitor::FailPoint Monitor::last_outcome_fail() const noexcept
{
    return { static_cast<std::uint32_t>(storage_.get(layout::h_last_fail_line)),
        static_cast<std::uint32_t>(storage_.get(layout::h_last_fail_column)) };
}

std::int64_t Monitor::last_outcome_start() const noexcept
{
    return storage_.get(layout::h_last_start);
}

std::int64_t Monitor::last_outcome_serial() const noexcept
{
    return storage_.get(layout::h_pop_serial);
}

std::vector<std::int64_t> Monitor::take_started()
{
    return storage_.take_started();
}

Monitor::Finished Monitor::finish()
{
    Finished result;
    if (!plan_->every_match) {
        const auto tick = storage_.get(layout::h_tick);
        for (std::size_t index = 0; index < storage_.attempt_count(); ++index) {
            const auto row = storage_.attempt(index);
            RecordReader reader { storage_.record(row) };
            const auto instance = Instance::decode(*plan_, reader, 0U);
            if (instance->finish(tick)) {
                ++result.holds;
                result.hold_starts.push_back(row.started);
            } else {
                ++result.failures;
                result.failure_starts.push_back(row.started);
            }
        }
    }
    storage_.clear_attempts();
    storage_.refresh_steps_active();
    return result;
}

void Monitor::kill()
{
    storage_.clear_attempts();
    storage_.clear_outcomes();
    storage_.refresh_steps_active();
}

std::vector<std::int64_t> Monitor::disable_running(const bool outcomes)
{
    std::vector<std::int64_t> starts;
    starts.reserve(storage_.attempt_count());
    for (std::size_t index = 0; index < storage_.attempt_count(); ++index) {
        starts.push_back(storage_.attempt(index).started);
        if (outcomes) {
            storage_.push_outcome({ static_cast<std::int64_t>(Outcome::disabled),
                storage_.attempt(index).started });
        }
    }
    storage_.clear_attempts();
    storage_.refresh_steps_active();
    return starts;
}

std::size_t Monitor::kill_attempt(const std::int64_t start_time)
{
    const auto killed = storage_.erase_attempts_started_at(start_time);
    storage_.refresh_steps_active();
    return killed;
}

std::size_t Monitor::running_attempts() const
{
    return storage_.attempt_count();
}

bool Monitor::request_step(const std::int64_t start_time, const bool enable)
{
    // "The stepping mode of any particular attempt cannot be modified after
    // the assertion attempt in question has started" (39.5.2): an attempt
    // of a time step the clock has ticked in has started (or will never
    // start), so the request is refused. A monitor that has not ticked yet
    // (h_tick -1) has started nothing.
    const bool ticked = storage_.get(layout::h_tick) >= 0;
    if (ticked && start_time <= storage_.get(layout::h_time)) {
        return false;
    }
    for (std::size_t index = 0; index < storage_.attempt_count(); ++index) {
        if (storage_.attempt(index).started == start_time) {
            return false;
        }
    }
    // The attempt starts later: the request waits for it. A repeated
    // request changes nothing, and disabling drops the pending request
    // ("no effect if stepping is not enabled", 39.5.2).
    storage_.erase_requests(start_time);
    if (enable) {
        storage_.push_request(start_time, 1);
    }
    storage_.refresh_steps_active();
    return true;
}

bool Monitor::stepping() const noexcept
{
    return storage_.get(layout::h_steps_active) != 0;
}

std::vector<StepEvent> Monitor::take_step_events()
{
    return storage_.take_events();
}

} // namespace fsim::runtime::sva
