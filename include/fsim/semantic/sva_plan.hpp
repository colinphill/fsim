// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
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

// The assertion control word of one assertion or expect statement (IEEE
// 1800-2023 20.11): what an assertion control read (runtime::simir::
// SvaKind::control, `$__fsim_assertion_control`) gives. The host sets it
// whenever a control task or VPI control changes the assertion's state;
// generated code reads it inline. Bit control_on: the assertion is checked
// (On, Off, Kill); control_pass, control_fail and control_vacuous: the pass
// statement on nonvacuous success, the fail statement and the pass
// statement on vacuous success run (PassOn to VacuousOff). The bits from
// control_generation_shift up (the word is 32 bits) count the Kills that
// selected it, modulo 2^28: a deferred assertion report queued before a
// Kill does not mature after it (16.4).
inline constexpr std::uint32_t control_on = 1U;
inline constexpr std::uint32_t control_pass = 2U;
inline constexpr std::uint32_t control_fail = 4U;
inline constexpr std::uint32_t control_vacuous = 8U;
inline constexpr std::uint32_t control_generation_shift = 4U;
inline constexpr std::int64_t control_default
    = control_on | control_pass | control_fail | control_vacuous;
// The assertion types (Table 20-6) and directive types (Table 20-7) a
// control read names in its description, `type|directive|name`
// (SvaOperation::plan): name is the assertion's label path below the scope
// of its process, empty when the assertion has no label.
inline constexpr std::uint32_t control_concurrent = 1U;
inline constexpr std::uint32_t control_simple_immediate = 2U;
inline constexpr std::uint32_t control_observed_deferred = 4U;
inline constexpr std::uint32_t control_final_deferred = 8U;
inline constexpr std::uint32_t control_expect = 16U;
inline constexpr std::uint32_t control_assert = 1U;
inline constexpr std::uint32_t control_cover = 2U;
inline constexpr std::uint32_t control_assume = 4U;

// The monitor flags of a monitor tick (runtime::simir::SvaOperation::
// monitor_flags; the `$__fsim_sva_tick` operand after the wake bit and the
// abort net). monitor_every_step: the monitor is woken at the start of every
// time step while an attempt is in progress, as its disable or abort
// condition reads sampled value functions, `.triggered` or automatic
// variables, which no net can follow (IEEE 1800-2023 16.12, 16.12.14).
// monitor_expect: the monitor is an expect statement's (16.17); an attempt
// that its disable condition ends between clock ticks still delivers the
// disabled outcome, which ends the statement.
inline constexpr std::uint32_t monitor_every_step = 1U;
inline constexpr std::uint32_t monitor_expect = 2U;

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
    // `##1 @(c)` (lo == 1) or `##0 @(c)` (lo == 0) in a multiclocked
    // sequence (IEEE 1800-2017 16.13.1): the thread continues on clock `a`
    // at its first tick strictly after, or at or after, the current one.
    clock,
    // Check expression `a` (Plan::expressions) at the current tick inside
    // the window: a boolean that reads the thread's local variables (IEEE
    // 1800-2023 16.10), evaluated per thread with its own values.
    check_expression,
    // Local variable `a` := expression `b` of the thread (16.10): a
    // sequence match item at the end of the subsequence before it (c == 0),
    // or the initialization assignment of a named sequence instance's local
    // variable (c == 1), performed at the first tick of the instance's
    // evaluation, the tick the thread's window opens.
    assign,
    // Subroutine call `a` (Plan::calls) attached to the subsequence before
    // it (IEEE 1800-2023 16.11): queued with the thread's local variables at
    // each end point of the subsequence, for the assertion process to run
    // after the tick.
    call,
};

// A `throughout_begin` whose `c` is 1 names an expression (Plan::
// expressions) in `a`, not a guard: a condition that reads local variables.
// A `join_end` names in `b` the local variables its operand assigns and
// that flow out of the composite (16.10 d): 0 none, else the set
// Plan::local_sets[b - 1].
struct Instruction {
    Op op { Op::check };
    std::uint32_t a { };
    std::uint32_t b { };
    std::uint32_t c { };
    Bound lo;
    Bound hi;
    // The source position of the boolean expression a `check` or
    // `throughout_begin` evaluates (1-based; 0 when it has none, as for a
    // synthesized truth guard): the failing expression a rejected attempt
    // reports (IEEE 1800-2023 39.4.2 failExpr).
    std::uint32_t line { };
    std::uint32_t column { };
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
    // A multiclocked plan (IEEE 1800-2023 16.13): the index in clock_guards
    // of the clock that flows into the property (16.13.3); for a sequence,
    // an implication and a followed-by, the semantic leading clock of the
    // sequence (16.16.1). An instance of the property starts at a point of
    // the trace and waits for the nearest tick of this clock. 0 for a
    // singly clocked plan.
    std::uint32_t clock { };
    // The position of the leftmost boolean expression of the property: what
    // a rejected attempt reports when the property fails without a failing
    // check (a negation whose operand held, an `eventually` that never
    // happened).
    std::uint32_t line { };
    std::uint32_t column { };
    // `guard` names an expression (Plan::expressions) that reads local
    // variables (a conditional's condition), not a sampled guard.
    bool guard_expression { };
    // The initialization assignments of the property's local variables
    // (16.10), in declaration order: expressions whose targets are the
    // locals, performed when an instance of the property begins.
    std::vector<std::uint32_t> initializers;
};

// One local variable of a plan (IEEE 1800-2023 16.10): its width in bits.
// The frontend writes 0; elaboration fills in the width of the variable's
// type (the tick's witness operands). A local holds 2 * ceil(width / 64)
// words in a thread: the value words, then the unknown words.
struct Local {
    std::uint32_t width { };
};

// An expression that reads local variables, which the assertion process
// evaluates for one thread's values on request: a check (target 0), whose
// result is its truth, or an assignment to local target - 1, whose result
// is the local's new value.
struct Expression {
    std::uint32_t target { };
};

// The source position (1-based; 0 when none) of the boolean expression a
// guard samples: what a step callback names as its matched expression
// (IEEE 1800-2023 39.4.2 matched_exprs).
struct GuardSpan {
    std::uint32_t line { };
    std::uint32_t column { };
};

struct Plan {
    std::vector<Sequence> sequences;
    std::vector<Property> properties;
    std::uint32_t root { };
    std::uint32_t guards { };
    std::uint32_t bounds { };
    // One span per guard (plan text "sva3"); empty for older plan texts.
    std::vector<GuardSpan> guard_spans;
    // `cover sequence` reports every match (16.14.3).
    bool every_match { };
    // A multiclocked plan: the guard that is true when clock i ticked in
    // the current time step. Clock 0 is the leading clock, on which
    // attempts start and the property operators count ticks. Empty for a
    // singly clocked plan, whose every evaluation is a tick of its clock;
    // one guard for a singly clocked plan whose monitor also evaluates at
    // time steps without a tick (an asynchronous abort, 16.12.14, or a
    // disable condition no net follows, 16.12), which reads its clock's
    // tick as a multiclocked plan does.
    std::vector<std::uint32_t> clock_guards;
    // Local variables (plan text "sva5"; none for older texts).
    std::vector<Local> locals;
    std::vector<Expression> expressions;
    std::vector<std::vector<std::uint32_t>> local_sets;
    // The number of subroutine calls attached to sequences (16.11).
    std::uint32_t calls { };
};

// The words a local of `width` bits holds (value words, then unknown words).
[[nodiscard]] inline std::uint32_t local_words(const std::uint32_t width)
{
    return 2U * ((std::max<std::uint32_t>(width, 1U) + 63U) / 64U);
}

[[nodiscard]] std::string encode(const Plan& plan);
[[nodiscard]] std::optional<Plan> decode(std::string_view text);

} // namespace fsim::semantic::sva
