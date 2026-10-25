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

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace fsim::runtime::sva {

using semantic::sva::Bound;
using semantic::sva::Op;
using semantic::sva::PropertyKind;

namespace {

constexpr std::int64_t unbounded = std::numeric_limits<std::int64_t>::max();
// A running sequence attempt may hold at most this many distinct threads.
constexpr std::size_t maximum_threads = 1U << 16U;

struct Step {
    Op op { Op::check };
    std::uint32_t a { };
    std::uint32_t b { };
    std::uint32_t c { };
    std::int64_t lo { };
    std::int64_t hi { };
};

struct CompiledSequence {
    std::vector<Step> code;
    std::uint32_t counters { };
};

struct CompiledProperty {
    PropertyKind kind { PropertyKind::sequence };
    bool strong { };
    bool overlapped { };
    std::uint32_t sequence { };
    std::uint32_t guard { };
    std::vector<std::uint32_t> children;
    std::int64_t lo { };
    std::int64_t hi { };
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
    std::vector<std::uint32_t> clock_guards;
    std::vector<std::int64_t> clock_ticks;
    std::vector<std::int64_t> clock_times;
    std::vector<bool> ticked;
    std::int64_t time { };

    [[nodiscard]] bool guard(const std::uint32_t index) const
    {
        return index < values->size() && (*values)[index];
    }
    [[nodiscard]] bool multiclocked() const
    {
        return !clock_guards.empty();
    }
};

namespace {

struct Throughout {
    std::uint32_t guard { };
    // The first tick of the operand; -1 until its first check.
    std::int64_t from { -1 };
};

struct JoinReference {
    std::uint32_t join { };
    std::uint32_t side { };
};

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
            result.push_back(entry.guard);
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

// The matches of one sequence started at one tick.
class Matcher {
public:
    Matcher(const CompiledPlan& plan, const CompiledSequence& sequence,
        const std::int64_t start)
        : plan_(plan)
        , sequence_(sequence)
    {
        Thread initial;
        initial.due_lo = start;
        initial.due_hi = start;
        initial.counters.assign(sequence.counters, 0);
        threads_.push_back(std::move(initial));
    }

    // Advance to tick `now`; true when the sequence matched at `now`.
    bool step(const std::int64_t now)
    {
        now_ = now;
        matched_ = false;
        matched_groups_.clear();
        visited_.clear();
        kept_.clear();
        std::deque<Thread> queue;
        std::vector<Thread> waiting;
        for (auto& thread : threads_) {
            // In a multiclocked sequence only the threads on a clock that
            // ticked advance.
            if (plan_.multiclocked()) {
                if (!plan_.ticked[thread.clock]) {
                    waiting.push_back(std::move(thread));
                    continue;
                }
                if (thread.switching) {
                    if (plan_.time < thread.switch_time
                        || (plan_.time == thread.switch_time
                            && !thread.switch_inclusive)) {
                        waiting.push_back(std::move(thread));
                        continue;
                    }
                    thread.switching = false;
                    thread.due_lo = plan_.clock_ticks[thread.clock];
                    thread.due_hi = thread.due_lo;
                }
            }
            const auto tick = thread_now(thread);
            // A thread inside `throughout` checks its condition on every
            // tick after the operand started.
            const bool holds = std::ranges::all_of(thread.throughout,
                [&](const Throughout& entry) {
                    return entry.from < 0 || entry.from > tick
                        || plan_.guard(entry.guard);
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

private:
    // The current tick of the thread's clock.
    [[nodiscard]] std::int64_t thread_now(const Thread& thread) const
    {
        return plan_.multiclocked() ? plan_.clock_ticks[thread.clock]
                                    : now_;
    }

    struct Join {
        std::uint32_t kind { };
        Thread continuation;
        std::set<std::int64_t> ends[2];
    };

    void keep(Thread thread)
    {
        if (kept_.insert(thread.key()).second) {
            if (threads_.size() >= maximum_threads) {
                throw std::runtime_error {
                    "concurrent property evaluation exceeded its thread limit"
                };
            }
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
            case Op::check: {
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
                if (!plan_.guard(step.a)) {
                    return;
                }
                for (auto& entry : thread.throughout) {
                    if (entry.from < 0) {
                        entry.from = now_;
                        if (!plan_.guard(entry.guard)) {
                            return;
                        }
                    }
                }
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
                thread.throughout.push_back({ step.a, -1 });
                ++thread.pc;
                break;
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
                join.ends[reference.side].insert(now_);
                const auto& other = join.ends[1U - reference.side];
                const bool paired = join.kind == 0U
                    ? !other.empty() && *other.begin() <= now_
                    : other.contains(now_);
                if (paired) {
                    auto continuation = join.continuation;
                    continuation.due_lo = now_ + 1;
                    continuation.due_hi = now_ + 1;
                    queue.push_back(std::move(continuation));
                }
                return;
            }
            case Op::match:
                if (settle(thread)) {
                    matched_ = true;
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
};

enum class Verdict : std::uint8_t { pending, holds, fails };

} // namespace

class Instance {
public:
    Instance(const CompiledPlan& plan, const std::uint32_t node,
        const std::int64_t start)
        : plan_(plan)
        , node_(node)
        , start_(start)
    {
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

    // `cover sequence`: whether the sequence matched at this tick; the
    // attempt ends once no thread is left.
    bool step_every_match(const std::int64_t now)
    {
        const auto& property = plan_.properties[node_];
        if (!matcher_) {
            matcher_ = std::make_unique<Matcher>(
                plan_, plan_.sequences[property.sequence], start_);
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
        if (decided() || now < start_) {
            return;
        }
        const auto& property = plan_.properties[node_];
        switch (property.kind) {
        case PropertyKind::sequence: {
            if (!matcher_) {
                matcher_ = std::make_unique<Matcher>(
                    plan_, plan_.sequences[property.sequence], start_);
            }
            if (matcher_->step(now)) {
                resolve(true, false);
            } else if (matcher_->dead()) {
                resolve(false, false);
            }
            break;
        }
        case PropertyKind::negation: {
            auto& operand = child(0, start_);
            operand.step(now);
            if (operand.decided()) {
                resolve(!operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::conjunction:
        case PropertyKind::disjunction:
        case PropertyKind::implies:
        case PropertyKind::equivalence: {
            auto& left = child(0, start_);
            auto& right = child(1, start_);
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
            if (!chosen_) {
                const bool condition = plan_.guard(property.guard);
                const auto branch = condition ? 0U : 1U;
                if (branch >= property.children.size()) {
                    resolve(true, true);
                    break;
                }
                chosen_ = branch;
            }
            auto& operand = child(*chosen_, start_);
            operand.step(now);
            if (operand.decided()) {
                resolve(operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::nexttime: {
            auto& operand = child(0, add(start_, property.lo));
            operand.step(now);
            if (operand.decided()) {
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
            if (plan_.guard(property.guard)) {
                const bool accept = property.kind == PropertyKind::accept;
                resolve(accept, accept);
                break;
            }
            auto& operand = child(0, start_);
            operand.step(now);
            if (operand.decided()) {
                resolve(operand.holds(), operand.vacuous());
            }
            break;
        }
        case PropertyKind::vacuous:
            resolve(true, true);
            break;
        }
    }

    // The verdict at the end of simulation: weak obligations hold, strong
    // obligations that did not complete fail (16.12.2).
    [[nodiscard]] bool finish(const std::int64_t now) const
    {
        if (decided()) {
            return holds();
        }
        const auto& property = plan_.properties[node_];
        if (now < start_) {
            return empty_trace(node_);
        }
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
            if (now < add(start_, property.lo)) {
                return !property.strong;
            }
            return child_finish(0, add(start_, property.lo));
        case PropertyKind::always: {
            const bool complete = property.hi != unbounded
                && now >= add(start_, property.hi);
            return std::ranges::all_of(started_, [&](const auto& item) {
                return item->finish(now);
            }) && (complete || !property.strong);
        }
        case PropertyKind::eventually: {
            const bool complete = property.hi != unbounded
                && now >= add(start_, property.hi);
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
        vacuous_ = holds && vacuous;
        operands_.clear();
        started_.clear();
        until_.clear();
        matcher_.reset();
    }

    Instance& child(const std::size_t index, const std::int64_t start)
    {
        for (auto& [position, instance] : operands_) {
            if (position == index) {
                return *instance;
            }
        }
        const auto& property = plan_.properties[node_];
        operands_.emplace_back(index,
            std::make_unique<Instance>(
                plan_, property.children[index], start));
        return *operands_.back().second;
    }

    void combine(const PropertyKind kind, const Instance& left,
        const Instance& right)
    {
        switch (kind) {
        case PropertyKind::conjunction:
            if ((left.decided() && !left.holds())
                || (right.decided() && !right.holds())) {
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
                resolve(false, false);
            }
            break;
        case PropertyKind::implies:
            if (left.decided() && !left.holds()) {
                resolve(true, true);
            } else if (right.decided() && right.holds()) {
                resolve(true, right.vacuous());
            } else if (left.decided() && right.decided()) {
                resolve(false, false);
            }
            break;
        case PropertyKind::equivalence:
            if (left.decided() && right.decided()) {
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
            matcher_ = std::make_unique<Matcher>(
                plan_, plan_.sequences[property.sequence], start_);
        }
        if (matcher_) {
            if (matcher_->step(now)) {
                const auto start = property.overlapped ? now : now + 1;
                started_.push_back(std::make_unique<Instance>(
                    plan_, property.children[0], start));
                ++consequents_;
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
                resolve(false, false);
            }
        }
    }

    // always / eventually over a window of start ticks.
    void step_window(const CompiledProperty& property, const std::int64_t now)
    {
        const auto first = add(start_, property.lo);
        const auto last = add(start_, property.hi);
        if (now >= first && now <= last) {
            started_.push_back(std::make_unique<Instance>(
                plan_, property.children[0], now));
        }
        const bool always = property.kind == PropertyKind::always;
        for (auto& operand : started_) {
            operand->step(now);
            if (!operand->decided()) {
                continue;
            }
            if (always && !operand->holds()) {
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
        if (last != unbounded && now >= last && started_.empty()) {
            resolve(always, false);
        }
    }

    // p until q (until_with folds p into q): q holds at some tick k and p
    // at every tick before k; the weak forms also hold when q never does.
    void step_until(const std::int64_t now)
    {
        const auto& property = plan_.properties[node_];
        if (!until_stopped_) {
            until_.push_back({
                std::make_unique<Instance>(plan_, property.children[0], now),
                std::make_unique<Instance>(plan_, property.children[1], now),
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
    Verdict verdict_ { Verdict::pending };
    bool vacuous_ { };
    bool matched_once_ { };
    std::unique_ptr<Matcher> matcher_;
    bool antecedent_done_ { };
    bool satisfied_ { };
    std::size_t consequents_ { };
    std::optional<std::size_t> chosen_;
    bool until_stopped_ { };
    std::vector<std::pair<std::size_t, std::unique_ptr<Instance>>> operands_;
    std::vector<std::unique_ptr<Instance>> started_;
    std::vector<UntilEntry> until_;
};

Monitor::Monitor(semantic::sva::Plan plan,
    const std::vector<std::int64_t>& bounds)
    : plan_(std::make_unique<CompiledPlan>())
{
    if (plan.root >= plan.properties.size()) {
        throw std::runtime_error { "concurrent property plan has no root" };
    }
    for (const auto& sequence : plan.sequences) {
        CompiledSequence compiled;
        compiled.counters = sequence.counters;
        for (const auto& instruction : sequence.code) {
            compiled.code.push_back({ instruction.op, instruction.a,
                instruction.b, instruction.c,
                resolve(instruction.lo, bounds),
                resolve(instruction.hi, bounds) });
        }
        plan_->sequences.push_back(std::move(compiled));
    }
    for (const auto& property : plan.properties) {
        plan_->properties.push_back({ property.kind, property.strong,
            property.overlapped, property.sequence, property.guard,
            property.children, resolve(property.lo, bounds),
            resolve(property.hi, bounds) });
    }
    plan_->root = plan.root;
    plan_->guards = plan.guards;
    if (plan.clock_guards.size() > 1U) {
        if (plan.properties[plan.root].kind != PropertyKind::sequence) {
            throw std::runtime_error {
                "a multiclocked concurrent property must be a sequence"
            };
        }
        plan_->clock_guards = plan.clock_guards;
        plan_->clock_ticks.assign(plan.clock_guards.size(), -1);
        plan_->clock_times.assign(plan.clock_guards.size(),
            std::numeric_limits<std::int64_t>::min());
        plan_->ticked.assign(plan.clock_guards.size(), false);
    }
    plan_->every_match = plan.every_match
        && plan.properties[plan.root].kind == PropertyKind::sequence;
}

Monitor::~Monitor() = default;

void Monitor::tick(const std::vector<bool>& guards, const bool start,
    const bool disabled, const std::int64_t time)
{
    plan_->values = &guards;
    bool leading = true;
    if (plan_->multiclocked()) {
        // A clock ticks at most once in a time step; its tick guard stays
        // set for the rest of the step (15.5.3).
        bool any { };
        for (std::size_t clock = 0; clock < plan_->clock_guards.size();
             ++clock) {
            const bool ticked = plan_->guard(plan_->clock_guards[clock])
                && plan_->clock_times[clock] != time;
            plan_->ticked[clock] = ticked;
            if (ticked) {
                ++plan_->clock_ticks[clock];
                plan_->clock_times[clock] = time;
                any = true;
            }
        }
        plan_->time = time;
        leading = plan_->ticked[0];
        if (!any && !disabled) {
            return;
        }
        // The property counts ticks of the leading clock.
        tick_ = plan_->clock_ticks[0];
    } else {
        ++tick_;
    }
    plan_->now = tick_;
    if (disabled) {
        if (start || !attempts_.empty()) {
            outcomes_.push_back(Outcome::disabled);
        }
        attempts_.clear();
        return;
    }
    if (start && leading) {
        attempts_.push_back(
            std::make_unique<Instance>(*plan_, plan_->root, tick_));
    }
    for (auto attempt = attempts_.begin(); attempt != attempts_.end();) {
        auto& instance = **attempt;
        if (plan_->every_match) {
            if (instance.step_every_match(tick_)) {
                outcomes_.push_back(Outcome::success);
            }
            // An attempt that ends without a match is a failed cover
            // attempt.
            if (instance.decided() && !instance.matched_once()) {
                outcomes_.push_back(Outcome::failure);
            }
        } else {
            instance.step(tick_);
            if (instance.decided()) {
                outcomes_.push_back(!instance.holds() ? Outcome::failure
                        : instance.vacuous()          ? Outcome::vacuous
                                                      : Outcome::success);
            }
        }
        if (instance.decided()) {
            attempt = attempts_.erase(attempt);
        } else {
            ++attempt;
        }
    }
}

std::size_t Monitor::pending_outcomes() const
{
    return outcomes_.size();
}

Outcome Monitor::next_outcome()
{
    if (outcomes_.empty()) {
        return Outcome::none;
    }
    const auto outcome = outcomes_.front();
    outcomes_.pop_front();
    return outcome;
}

Monitor::Finished Monitor::finish()
{
    Finished result;
    if (!plan_->every_match) {
        for (const auto& attempt : attempts_) {
            if (attempt->finish(tick_)) {
                ++result.holds;
            } else {
                ++result.failures;
            }
        }
    }
    attempts_.clear();
    return result;
}

void Monitor::kill()
{
    attempts_.clear();
    outcomes_.clear();
}

std::size_t Monitor::running_attempts() const
{
    return attempts_.size();
}

} // namespace fsim::runtime::sva
