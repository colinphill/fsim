<!-- SPDX-License-Identifier: Apache-2.0 -->
# VHDL-2008 and embedded PSL support

Fsim implements a governed digital VHDL-2008 boundary with an embedded IEEE
1850 PSL subset. The authoritative inventory is
[`vhdl_psl_gap_inventory.tsv`](../tests/feature_matrix/vhdl_psl_gap_inventory.tsv):
29 active rows are supported with independent positive, negative, and runtime
witnesses, no active row is unresolved, and four boundaries are explicitly
deferred.

The deferrals are VHDL-AMS, SDF parsing and backannotation (owned by Batch
170), PSL features beyond the embedded digital subset, and post-2008/vendor
extensions. This is a closed support statement for the inventoried boundary,
not a claim of exhaustive IEEE 1076 or IEEE 1850 conformance.

## Source and analysis boundary

VHDL source sets select standard `2008` or `08`. Case-insensitive `-- psl`
comments retain PSL source ownership while ordinary comments remain trivia.
Native or embedded verification units, default clocks, Boolean/sequence/
property/endpoint declarations, formal profiles, and labeled `assert`,
`assume`, `restrict`, and `cover` directives are copied into owning semantic
HIR with exact source spans.

Supported temporal analysis includes canonical rising/falling clocks,
concatenation and fusion, consecutive/nonconsecutive/goto repetition, suffix
implication, `next`, `prev`, `eventually`, `always`, `until`, `before`,
`within`, endpoints, static ranges, declaration formals/defaults, strong and
weak completion, and synchronous/asynchronous abort. Unknown clock values do
not create an edge; sampled unknown scalar values use the documented false
policy. Cross-clock references, cycles, nonstatic bounds, malformed forms,
and incompatible sampled objects reject before elaboration.

## Execution and observation

PSL samples one immutable value set after ordinary scheduler updates. Monitor
attempts are occurrence-owned and source ordered. Each completion retains its
directive kind, unit and root identity, source span, clock/sample/time/delta
coordinates, and pass, failure, vacuous, or aborted outcome. Coverage and
callbacks use the same attempt record; observer exceptions are contained
after state commits.

The governed runner requires exact equality through:

- direct source analysis and execution;
- interpreter, compiled LLVM O0, debug, and LLVM O2;
- LLVM O2 cold and warm native caches;
- debugger snapshots and VCD signal activity;
- `.fsimobj`, relocated `.fsimlib`, and relocated `.fsimdesign` artifacts;
- source-independent replay and portable VHPI checkpoint remapping;
- duplicated/reordered VHDL roots and adjacent SystemVerilog/SystemC roots.

Owning-unit schema 17 preserves frontend PSL state. Standalone designs add the
checksummed schema-1 `FSIMVHIR` payload for complete VHDL semantic HIR,
including analyzed PSL state. Load rejects incompatible schema, checksum,
trailing bytes, enum values, duplicate identities, invalid semantic links, or
cross-payload inconsistency before publishing a project. Cache keys include
the compiler IEEE identity `ieee-1076-2019-16a01232-vhdl-psl-wide-v2`.

## Resource and platform contract

The PSL attempt engine enforces monitor, active/lifetime attempt, sampled
history, evaluation, temporal-step, and conservatively accounted owned-storage
limits transactionally. Debug/VHPI snapshots have record, payload, text, and
format ceilings. The governed application process is capped at 6 GiB through
POSIX `RLIMIT_AS` or a Windows Job Object, simulation work at 1,000 deltas,
trace registration at 64 signals, and its CTest at 1,200 seconds.

The same support is built through the installed `fsim-vhdl` alias. Its
presence and executable help path are part of the installed-public contract.
All new C/C++ formatting follows the repository `.clang-format`, based on the
WebKit preset.

## Diagnostics and evidence

`FSIM-VHDL-PSL-001` through `019` cover PSL parse, ownership, clock, type,
range, reference, and cycle failures. Runtime resource failures retain typed
`VhdlPslResourceKind` identities and reject before partial mutation. Artifact,
VHPI, cache, scheduler, VITAL, and mixed-boundary failures use their existing
cataloged domains.

See the [tutorial](vhdl-psl-tutorial.md) for ordinary project and portable
artifact flows, [language support](language-support.md) for the surrounding
VHDL boundary, [architecture](architecture.md) for ownership and scheduling,
and the [closure audit](vhdl-psl-closure-audit.md) for exact counts and gates.
