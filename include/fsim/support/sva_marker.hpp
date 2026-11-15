// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string_view>

namespace fsim::support {

/// What a concurrent assertion's generated monitor tells the engine at one
/// point of its code. The parser places these in the monitor process; the
/// lowering turns the region kinds into scheduling and the outcome kinds
/// into host requests (IEEE 1800-2023 16.14, 39.4.2).
enum class SvaMarkerKind : std::uint8_t {
    /// One outcome of an attempt (SvaOutcome pass to start).
    outcome,
    /// The actions replayed for an attempt the end of simulation resolves
    /// (SvaOutcome failure, vacuous, obligation, obligation_vacuous).
    pending,
    /// The pass or fail statement that follows runs in the Reactive region
    /// (16.14.1) and reads current values.
    action_region,
    /// The subroutine calls attached to a sequence that follow run in the
    /// Reactive region with sampled by-value arguments (16.11).
    call_region,
    /// The reads that follow use sampled values (16.5.1).
    sampled_reads_on,
    /// The reads that follow use current values.
    sampled_reads_off,
};

/// The directive a monitor checks (16.14).
enum class SvaDirective : std::uint8_t {
    assertion,
    assumption,
    cover,
    restriction,
};

enum class SvaOutcome : std::uint8_t {
    pass,
    failure,
    vacuous,
    /// disable iff ended the attempt (16.12).
    abort,
    /// The monitor names itself to the host when its process starts.
    registration,
    /// An attempt of an evaluator that is not the monitor starts
    /// (39.4.2 cbAssertionStart).
    start,
    /// The actions of an attempt still obligated when simulation ends
    /// (16.12.2 strong obligations).
    obligation,
    /// The pass actions of an attempt that holds on the truncated trace.
    obligation_vacuous,
};

/// The name the assertion API and the coverage report give a directive.
[[nodiscard]] constexpr std::string_view sva_directive_name(
    const SvaDirective directive) noexcept
{
    switch (directive) {
    case SvaDirective::assertion:
        return "assertion";
    case SvaDirective::assumption:
        return "assumption";
    case SvaDirective::cover:
        return "cover";
    case SvaDirective::restriction:
        return "restriction";
    }
    return "assertion";
}

[[nodiscard]] constexpr bool sva_marker_kind_valid(
    const SvaMarkerKind kind) noexcept
{
    return kind >= SvaMarkerKind::outcome
        && kind <= SvaMarkerKind::sampled_reads_off;
}

[[nodiscard]] constexpr bool sva_directive_valid(
    const SvaDirective directive) noexcept
{
    return directive >= SvaDirective::assertion
        && directive <= SvaDirective::restriction;
}

/// Whether `outcome` is one a marker of `kind` may carry.
[[nodiscard]] constexpr bool sva_outcome_valid(
    const SvaMarkerKind kind, const SvaOutcome outcome) noexcept
{
    switch (kind) {
    case SvaMarkerKind::outcome:
        return outcome >= SvaOutcome::pass && outcome <= SvaOutcome::start;
    case SvaMarkerKind::pending:
        return outcome == SvaOutcome::failure
            || outcome == SvaOutcome::vacuous
            || outcome == SvaOutcome::obligation
            || outcome == SvaOutcome::obligation_vacuous;
    case SvaMarkerKind::action_region:
    case SvaMarkerKind::call_region:
    case SvaMarkerKind::sampled_reads_on:
    case SvaMarkerKind::sampled_reads_off:
        return outcome == SvaOutcome::pass;
    }
    return false;
}

/// What a statement the frontend generated for an assertion construct stands
/// for in the VPI statement model (IEEE 1800-2023 37.60, 37.73). A source
/// statement has none.
enum class GeneratedStatementRole : std::uint8_t {
    none,
    // The statement an expect statement expands into (16.17): VPI presents
    // it as one vpiExpectStmt and none of the statements inside it.
    expect,
    // Inside an expect statement: the block holding its pass statement, and
    // the one holding its else statement, when the source has one.
    expect_pass,
    expect_else,
    // The statement that stands for a procedural concurrent assertion in its
    // procedure (16.14.6). It is no statement of the VPI model (37.60); the
    // assertion is reached from its scope (37.12).
    procedural_assertion,
    // A continuous assignment of a net the frontend generated for an
    // assertion (a disable condition's or abort's net, 16.12): no
    // continuous assignment of the source (37.47).
    synthesized,
    // The case statement an `if (e matches p [&&& g]) s1 [else s2]` is
    // written as (12.6.2): VPI presents it as the if statement, its
    // condition a vpiMatchOp of e and p (37.72, Annex M), its default
    // item's statement the else statement.
    if_matches,
    // A loop or block of the nested loops a foreach statement is written as
    // (12.7.3): no statement or scope of the source. Its contents belong to
    // the foreach statement's implicit block.
    foreach_layer,
};

[[nodiscard]] constexpr bool generated_statement_role_valid(
    const GeneratedStatementRole role) noexcept
{
    return role >= GeneratedStatementRole::none
        && role <= GeneratedStatementRole::foreach_layer;
}

} // namespace fsim::support
