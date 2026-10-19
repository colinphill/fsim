// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The evaluation plan of one SystemVerilog concurrent property
// (IEEE 1800-2017 16.7-16.14). The frontend compiles a property into a plan;
// the runtime monitor evaluates it once per clock tick from a vector of
// sampled boolean guards. The plan is pure data and travels as text in the
// directive's SimIR operation.
namespace fsim::semantic::sva {

// A delay or repetition bound: a literal, a per-instance constant passed
// when the monitor is created (slot plus offset), or `$`.
struct Bound {
    enum class Mode : std::uint8_t { literal, slot, unbounded };
    Mode mode { Mode::literal };
    std::int64_t value { };
    std::int64_t offset { };
};

// One instruction of a sequence matcher. A thread holds a program counter
// and the window of ticks in which its next boolean must be checked; a
// sequence that starts at tick t first checks at t, and each `##n` moves the
// window by n - 1 after the last checked tick.
enum class Op : std::uint8_t {
    // Check guard `a` at the current tick inside the window.
    check,
    // `##[lo:hi]`: move the window.
    delay,
    // Continue at pc + 1 and at `a`.
    fork,
    jump,
    // Repetition counter `a` := 0.
    counter_set,
    // Counter `a` within [lo, hi]: continue at `b` when it reached lo and
    // at pc + 1 while it is below hi.
    counter_branch,
    // Counter `a` += 1, saturating once it reaches max(lo, 1) for an
    // unbounded hi.
    counter_increment,
    // `a throughout s`: guard `a` holds on every tick of s.
    throughout_begin,
    throughout_end,
    // first_match(s): one match tick, the earliest.
    first_match_begin,
    first_match_end,
    // `and` (a == 0) or `intersect` (a == 1) of the operand at pc + 1 and
    // the operand at `b`; both continue at `c` once matched.
    join_begin,
    // End of join operand `a` (0 left, 1 right).
    join_end,
    // The sequence matched.
    match,
};

struct Instruction {
    Op op { Op::check };
    std::uint32_t a { };
    std::uint32_t b { };
    std::uint32_t c { };
    Bound lo;
    Bound hi;
};

struct Sequence {
    std::vector<Instruction> code;
    std::uint32_t counters { };
};

enum class PropertyKind : std::uint8_t {
    // Sequence `sequence`; strong when `strong`.
    sequence,
    negation,
    conjunction,
    disjunction,
    equivalence,
    implies,
    // `sequence |-> children[0]`, or `|=>` when not `overlapped`.
    implication,
    // `sequence #-# children[0]`, or `#=#` when not `overlapped`.
    followed_by,
    // if (guard) children[0] else children[1] (absent: vacuous success).
    conditional,
    // nexttime[lo] children[0].
    nexttime,
    // always [lo:hi] children[0].
    always,
    // eventually [lo:hi] children[0].
    eventually,
    // children[0] until children[1]; `with` folds into children[1].
    until,
    // accept_on / reject_on (guard) children[0].
    accept,
    reject,
    // Vacuous success.
    vacuous,
};

struct Property {
    PropertyKind kind { PropertyKind::sequence };
    bool strong { };
    bool overlapped { };
    // The abort operators: synchronous (sync_accept_on / sync_reject_on).
    bool synchronous { };
    std::uint32_t sequence { };
    std::uint32_t guard { };
    std::vector<std::uint32_t> children;
    Bound lo;
    Bound hi;
};

struct Plan {
    std::vector<Sequence> sequences;
    std::vector<Property> properties;
    std::uint32_t root { };
    std::uint32_t guards { };
    std::uint32_t bounds { };
    // `cover sequence` reports every match (16.14.3).
    bool every_match { };
};

[[nodiscard]] std::string encode(const Plan& plan);
[[nodiscard]] std::optional<Plan> decode(std::string_view text);

} // namespace fsim::semantic::sva
