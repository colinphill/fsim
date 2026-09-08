<!-- SPDX-License-Identifier: Apache-2.0 -->
# Feature-matrix test contract

This directory is reserved for atomic tests that satisfy
[the v1 feature matrix](../../docs/feature-matrix.md). The current vertical
slice keeps most evidence in subsystem test executables, listed below. This
README is a test-authoring contract; it is not itself feature evidence.

## Required evidence for each v1 construct

Every required feature ID must eventually have all four kinds of automated
evidence:

1. **Positive (P+)**: the smallest valid source is accepted and its typed
   representation is checked.
2. **Negative (P-)**: a nearby invalid or unsupported source is rejected with
   the expected stable diagnostic code and useful source span.
3. **Elaboration (E)**: hierarchy, specialization, types, drivers, and emitted
   SimIR are checked as applicable. Acceptance without semantic checking does
   not count.
4. **Runtime (R)**: observable semantics are compared through the SimIR
   interpreter and LLVM JIT, including final values, assertions, scheduler
   observations, and trace events.

A path counts as evidence only when it is built and registered as an automated
test. Example designs, parser implementation paths, and documentation do not
count.

The governed SystemVerilog-2017 release contract is the composition of
`systemverilog_gap_inventory.tsv`,
`systemverilog_literal_width_inventory.tsv`, and
`systemverilog_release_closure.tsv`. It contains 30 supported clause/integration
rows, 21 preserved width paths, 153 witness cells, zero active rows, and five
explicit later-owned deferrals. Address-space, work, trace, and timeout values
in that matrix are evidence ceilings and must never be described as language
legality or packed-width limits.

Batch 166's older-VHDL planning contract is the composition of
`vhdl_standard_mode_inventory.tsv` and
`vhdl_synopsys_package_inventory.tsv`. Each contains 17 preserved and zero
active obligations assigned one-to-one to Changes 2-18: the first spans
VHDL-87/93/2000/2002 revision behavior, and the second spans the explicitly
non-standard Synopsys `ieee` compatibility packages. The revision and package
corpus tables bind positive, negative, exact-diagnostic, arbitrary-width and
provenance anchors. The registered 15-witness serial closure matrix retains
one log per stage and covers interpreter/LLVM, artifacts/caches, relocation,
public boundaries and platform/resource contracts.

Batch 167's completed older-Verilog/SystemVerilog evidence contract is the composition
of `verilog_systemverilog_standard_mode_inventory.tsv` and
`verilog_systemverilog_compatibility_inventory.tsv`. Each contains 17 preserved and zero active obligations
assigned one-to-one to Changes 2-18. The first spans Verilog-1995,
Verilog-2001, Verilog-2001-noconfig, SystemVerilog-2005, SystemVerilog-2009 and
SystemVerilog-2012 behavior. The second independently governs keyword-profile,
implicit-net, port-connection, sizing, lifetime, scheduler/assertion and
configuration switches so compatibility never silently enables a later
standard. The six-row revision corpus and seven-row switch corpus bind exact
positive/negative stages, diagnostic coordinates, execution, arbitrary-width,
provenance and artifact anchors. The registered 16-witness serial matrix
retains one log per witness and a 17-row stage ledger across engines, caches,
artifacts, mixed languages, public services, platforms and resources. The
mode, compatibility, revision-corpus and switch-corpus SHA-256 identities are
`21a05e4732aa8da8b546f2c7048ee2b5d51cdca4572114fb5c9946d4bfad8d3e`,
`5e7a74d4f9c0af29e28c4d0b9b35b96a5c9df6482b53759633f80904812c5f6f`,
`8c3018215c3e8c7d1fdc90c275d2f3d3e4ee065956c8b205fb740a8d750981f7`, and
`34eca16ab78a6e2ab73f1e7d77cb36091fad5571b89f4ba5f8c512d69355a24a`.

Batch 168 starts with the authoritative 17-row `sdf_inventory.tsv` ledger.
Rows are assigned one-to-one to Changes 2-18 and remain active until their
owning change closes. Every row binds the exact SDF 2.1/3.0/4.0 revisions,
domain, implementation owner, positive and negative evidence, revision and
diagnostic-coordinate evidence, portable round-trip evidence, diagnostic
catalog and governed resource contract. The ledger does not claim timing
annotation behavior: Batch 168 parses, normalizes, resolves and persists SDF,
while Batches 169-170 own application to simulator timing.
All 17 rows are now preserved. The complete surface includes bounded lexical
and typed revision parsing, exact normalization, immutable IR, mixed-language
scope/cell/endpoint resolution, whole-mapping validation, versioned schemas,
format-9 artifact/cache identity, mapped-library and source-hidden standalone
design persistence, interpreter/LLVM cold/warm/relocated cache evidence, six
authoritative revision-indexed corpus files, and retained-log negative/resource
closure. The ledger intentionally stops before timing application and has
SHA-256 identity
`5a4817053834c76fef49650d38b73957a4be5560c62d1eb0f8c72c7180340c4b`.

Batch 169 starts with the independent 17-row
`sdf_application_inventory.tsv` ledger. Rows map one-to-one to Changes 2-18
across exact value selection, atomic target plans, Verilog/SystemVerilog path,
interconnect, timing-check, pulse, scheduler, drive-state and reannotation
behavior, public control, persistence, observability and executable closure.
Changes 2-18 exact value-policy, atomic-target-plan, path-annotation,
interconnect/device, delay-list/mode, primary/secondary timing-check and
condition/notifier, pulse/RETAIN and effective-value precedence rows are
preserved with runtime scheduling, drive-state propagation and transactional
safe-point reannotation, public annotation control, effective-archive
persistence, bounded public observability and executable corpus closure; all
seventeen rows are preserved.
Every row binds the exact SDF revisions, eight governed HDL profiles, planned
implementation and evidence owners, engine/phase/artifact coverage,
diagnostics and physical resource contracts without claiming VHDL/VITAL timing
application reserved for Batch 170. The current SHA-256 identity is
`47e7f5b95aae9f0e3df5cb4fcb4939255e1803f9c920081a47198e21b75f2754`.

Batch 170 starts with the independent 17-row `sdf_vital_inventory.tsv`
ledger. Its rows assign Changes 2-18 exactly across VHDL/VITAL target plans,
paths/checks/models, precedence, scheduling, negative checks, reannotation,
VHDL-to-Verilog/SystemVerilog/SystemC boundaries, multiple-root resolution,
foreign interfaces, observability, artifacts, phases, mixed corpora and
closure negatives. Every row binds SDF 2.1/3.0/4.0, all supported VHDL modes,
the governed Verilog/SystemVerilog profiles and the native SystemC boundary to
planned implementation, positive/negative/engine/phase/artifact evidence,
diagnostics and physical resource ownership. Change 2 target planning is
preserved and sixteen rows remain active. The current SHA-256 identity is
`59de8a2f5a0ff3dcd65a907a6355bbfd9952d75417650a6db89a0fab5f4ff095`.

Batch 178 starts with the clause-neutral 17-row
`code_coverage_inventory.tsv` ledger. Its stable rows assign Changes 2-18
one-to-one across the language-neutral model, source and point identity,
Verilog/SystemVerilog and VHDL statement discovery, branches, line state,
instance inventories, SimIR hits, interpreter/LLVM/Debug execution,
exclusions, instance identity, aggregation, opt-in control, and v3
artifact/cache identity. Every row binds all thirteen retained HDL profiles to
independently written obligations and planned positive, negative, engine,
aggregation, artifact, diagnostic, and resource owners. The language-neutral
All seventeen rows are preserved. Change 19 proves cross-row engine and
aggregation equivalence, and Change 20 freezes the completed batch. The
registered normalized SHA-256 identity is
`06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234`.

Batch 179 starts with the exact 17-row
`code_coverage_metrics_inventory.tsv` ledger. All seventeen rows are preserved
and zero remain active. Its stable rows assign
Changes 2-18 one-to-one across Verilog/SystemVerilog and VHDL atomic-condition
decomposition, short-circuit preservation, binary and auxiliary unknown
outcomes, bounded expression combinations, binary toggle bins and selected
objects, default and explicit container selection, unknown transition
diagnostics, inferred current/next/legal FSM state, standard pragmas, VHDL and
manifest hints, separate visit/transition recording, and transactional
description validation. Every row binds all thirteen retained HDL profiles to
independently written obligations and planned positive, negative, engine,
aggregation, artifact, diagnostic, and resource owners. MC/DC is explicitly
outside Batch 179. Change 19 proves generate-instance, engine, and
mixed-language equivalence and advances enabled artifact identity to the
broad-metrics model; Change 20 freezes the complete broad-metric set with
MC/DC still excluded. The registered normalized
SHA-256 identity is
`e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443`.

Batch 181 starts with the IEEE-only 18-row `legacy_tf_inventory.tsv` ledger.
Its stable rows assign Changes 2-19 one-to-one across the direct v3 native
plug-in ABI, public `veriuser.h`, Linux and Windows link surfaces, registration
and callback lifecycles, argument/value/time/context services, synchronization,
HDL registration, scheduler coordination, failure containment, and
cross-platform C/C++ plug-in evidence. The direct v3 ABI, public TF header, and
platform-link, registration-discovery, descriptor-validation, callback,
miscellaneous-lifecycle, argument-inspection, value-access, and
parameter-instance, time-delay-timescale, and scope-work-area-user-data rows
plus the output-control, synchronization-callback, HDL-system-registration,
scheduler-coordination, failure-containment, and cross-platform plug-in rows
are preserved; zero rows remain active.
All bind IEEE 1364-2005 TF
ownership to all eight retained Verilog and SystemVerilog profiles. Vendor extensions are excluded
by the machine-checked
`ieee-only-no-vendor-extensions` policy; no compatibility reader or migration
for a v2 plug-in ABI is permitted. The registered normalized SHA-256 identity
is `a3bff1ef32fbf3fd412556c63c39e69a45f4efdd5cb599d07b3ac47136b531e0`.

Batch 182 starts with the IEEE-only 18-row `legacy_acc_inventory.tsv`
ledger. Changes 2-19 own, one-to-one, the public header, lifecycle, handle,
lookup, hierarchy, object, value, iterator, timing, callback, TF-coherence,
VPI-coherence, scheduler, diagnostic-rejection, and closure-corpus domains.
The ledger accounts for 102 standardized ACC routines and
115 canonical ACC object kinds across all eight retained Verilog and
SystemVerilog profiles.
Its machine-checked `ieee-only-no-vendor-extensions` policy excludes vendor
additions and does not permit a v2 compatibility reader or migration. The
public-header, lifecycle, generation-qualified-handle, name-lookup,
hierarchy-traversal, complete-object-model, value/property-read, value-update,
indexed-iterator, path-delay/timing-check, value-change-link, callback-
cancellation, safe-point-lifetime, TF/ACC-coherence, ACC/VPI-object-
equivalence, parallel-coordination, vendor-name-rejection, and complete
TF/ACC closure-corpus rows are preserved and zero rows remain active
at normalized
SHA-256
`1fa0db768c78515e6596639dc3da43b323c1e7f8c2b209d668c02f35071bb1a7`.

## Intended atomic layout

New tests should use the stable feature ID from the matrix and keep one
construct or one tightly coupled rule per fixture:

```text
tests/feature_matrix/
  vhdl/
    V1-VH-03/
      positive.vhd
      negative.vhd
      elaborate.expected.json
      runtime.expected.json
  systemverilog/
    V1-SV-07/
      positive.sv
      negative.sv
      elaborate.expected.json
      runtime.expected.json
  mixed/
    ML-004/
      fsim.toml
      ...
  systemc/
    SC-007/
      fsim.toml
      ...
```

Fixture names may be more descriptive when a feature needs several atomic
cases, but each case must remain attributable to one matrix row. Golden output
must normalize temporary paths, platform path separators, VCD identifiers, and
other non-semantic variation.

## Existing vertical-slice evidence inventory

These suites are the evidence currently referenced by the matrix:

| Suite | Present evidence |
|---|---|
| [fixtures/systemverilog/uvm_legacy_macro_probe.sv](../fixtures/systemverilog/uvm_legacy_macro_probe.sv) | Untouched UVM 1.2 field/object/component/registry, sequence, callback, analysis-implementation, and report macro expansion retained as generated method, factory-wrapper, inheritance, and executable-body metadata across direct and portable two-release stages |
| [fixtures/systemverilog/uvm_core_smoke_probe.sv](../fixtures/systemverilog/uvm_core_smoke_probe.sv) | Project-owned core source binds each governed package's object-policy, factory, resource/configuration, command-line, report-server, callback, and test surfaces; eleven named methods execute and retain nonempty bodies through direct and portable two-release stages |
| [frontend/frontend_preprocessing_tests.cpp](../frontend/frontend_preprocessing_tests.cpp) | Governed two-release version ladders and macro inventory; UVM 1.2 deprecated package/sequence/sequencer expansion plus `UVM_NO_DEPRECATED`; UVM 2020-3.1 IEEE policy/copier/revision names plus removed global and registration aliases; exact undefined/depth/arity diagnostics |
| [app/uvm_phase_tlm_application_test.cpp](../app/uvm_phase_tlm_application_test.cpp) | Exact UVM 1.2 legacy and UVM 2020-3.1 IEEE/new-or-retained compatibility method profiles, normalized difference dispatch, mixed/API mismatch rejection, release/source provenance through object/design/cache/checkpoint/replay, retained register field/map/memory profiles, and interpreter/LLVM/debug differential |
| [fixtures/systemverilog/uvm_phase_tlm_example.sv](../fixtures/systemverilog/uvm_phase_tlm_example.sv) | Project-owned phase/TLM/register source retains nine callback bodies, TLM1 members, sequence/sequencer/role/virtual/callback inheritance, six register roles, and governed TLM2 payload profiles across the serial two-release direct/object/design/interpreter/LLVM/cache/debug/root/replay matrix |
| [app/uvm_phase_tlm_register_probe.cpp](../app/uvm_phase_tlm_register_probe.cpp) | Exact register block/map/field/memory, endian/byte-enable, adapter/predictor/frontdoor, mixed-VPI/VHPI backdoor, standard-sequence, callback/coverage, negative rollback, relocated checkpoint, debugger inventory, and retained-cap evidence |
| [app/uvm_process_limits.cpp](../app/uvm_process_limits.cpp) | Cross-platform 6 GiB UVM test-process ceiling through POSIX `RLIMIT_AS` or a Windows Job Object, paired with per-stage and whole-matrix timeouts in the exact runner |
| [feature_matrix/uvm_conformance_inventory.tsv](uvm_conformance_inventory.tsv) | Authoritative 18-row supported UVM boundary selecting 17 families per governed release, exact 53/56 standard and 27 project class identities, and positive/negative/execution owners with no supported-failure waiver |
| [feature_matrix/uvm_release_closure.tsv](uvm_release_closure.tsv) | Authoritative 21-row two-release closure matrix freezing compatibility switches, retained counts and resources, trace identity, artifact/cache provenance, evidence ownership, and zero unresolved supported gaps |
| [feature_matrix/vhdl_psl_gap_inventory.tsv](vhdl_psl_gap_inventory.tsv) | Authoritative 33-row Batch 163 scope split across 29 supported IEEE 1076-2008 and embedded-PSL rows, zero unresolved active rows, and four explicit deferrals; every supported row freezes independent positive, negative, and execution witnesses plus parser, analyzer, elaboration, runtime, diagnostic, and resource owners |
| [feature_matrix/vhdl_2019_inventory.tsv](vhdl_2019_inventory.tsv) | Independently worded 37-row IEEE 1076-2019 delta inventory assigning every frontend and runtime obligation one-to-one to Batch 183 Changes 2-19 and Batch 184 Changes 1-19; all rows are preserved after Batch 184 Change 19, and the ledger records only standard identifiers, clause numbers, project-authored summaries, and repository ownership |
| [feature_matrix/vhdl_psl_release_closure.tsv](vhdl_psl_release_closure.tsv) | Authoritative 44-row Batch 163 closure contract freezing 17 direct/interpreter/LLVM/cache/debug/trace/artifact/relocation/replay/root/mixed stages, malformed/race/cancellation/nonconvergence/resource evidence, POSIX/Windows and installed-public paths, exact inventory and reviewed-complexity counts, observed RSS, and a 6 GiB process ceiling |
| [feature_matrix/verilog_gap_inventory.tsv](verilog_gap_inventory.tsv) | Authoritative Batch 164 IEEE 1364-2005 clause inventory with 34 supported rows, zero active residual rows, and three explicit SDF, removed TF/ACC, and informative-optional deferrals after Change 16 closed the mixed-language and public-boundary surface; every supported row has parser, analyzer, elaboration, runtime, positive, negative, execution, diagnostic, and resource ownership |
| [feature_matrix/verilog_literal_width_inventory.tsv](verilog_literal_width_inventory.tsv) | Separate Batch 164 audit of every known Verilog literal-width cap and host-word assumption across lexical text, parsing, folding, materialization, runtime packed storage, LLVM, scalar/VPI host formats, memory files, traces, public boundaries, artifacts, and caches, now closed at 12 preserved paths, zero active obligations, and three governed physical boundaries after Change 17 proved exact artifact, checkpoint, relocation, replay, and native-cache identity |
| [feature_matrix/verilog_release_closure.tsv](verilog_release_closure.tsv) | Authoritative Batch 164 execution closure mapping all 34 supported clause rows and 12 preserved width paths to 138 positive, negative, and execution witnesses across exactly 23 registered CTests and 17 governed direct, engine, cache, debugger, trace, artifact, checkpoint, multiple-root, VHDL, and SystemC stages under explicit 6 GiB, delta, trace, stage, and matrix limits |
| [feature_matrix/verilog_systemverilog_standard_mode_inventory.tsv](verilog_systemverilog_standard_mode_inventory.tsv) | Batch 167's authoritative 17-row older-revision obligation ledger, preserving Changes 2-18 with zero active rows across Verilog-1995, Verilog-2001, Verilog-2001-noconfig, and SystemVerilog-2005/2009/2012 |
| [feature_matrix/verilog_systemverilog_compatibility_inventory.tsv](verilog_systemverilog_compatibility_inventory.tsv) | Batch 167's independent 17-row ledger for the seven explicit keyword, implicit-net, port, sizing, lifetime, scheduler/assertion, and configuration compatibility switches; no row authorizes later grammar |
| [feature_matrix/verilog_systemverilog_revision_corpus.tsv](verilog_systemverilog_revision_corpus.tsv) | Six ordered revision rows freezing positive/negative stages, exact diagnostic codes and coordinates, execution, arbitrary-width, include provenance, and artifact-mismatch anchors |
| [feature_matrix/verilog_systemverilog_compatibility_corpus.tsv](verilog_systemverilog_compatibility_corpus.tsv) | Seven ordered switch rows freezing independent canonical selection, 257-bit preservation, later-grammar rejection, public provenance, execution, and artifact evidence |
| [RunVerilogSystemVerilogStandardModeClosureMatrix.cmake](../../cmake/RunVerilogSystemVerilogStandardModeClosureMatrix.cmake) | Registered 16-witness serial retained-log closure across revisions/switches, interpreter/LLVM O0/O2, cold/warm caches, artifacts/replay, mixed VHDL/SystemC, C/C++/Tcl/VPI, MSVC/Windows, and governed resource contracts |
| [feature_matrix/sdf_inventory.tsv](sdf_inventory.tsv) | Batch 168's authoritative 17-row SDF 2.1/3.0/4.0 obligation ledger assigning lexical, header, construct, adapter, normalization, immutable IR, hierarchy-resolution, schema/artifact, relocation/cache, corpus and closure ownership one-to-one to Changes 2-18 |
| [feature_matrix/sdf_application_inventory.tsv](sdf_application_inventory.tsv) | Batch 169's authoritative 17-row SDF-to-Verilog/SystemVerilog timing-application ledger assigning exact values, atomic plans, path/interconnect/check/pulse/scheduler/drive/reannotation behavior, controls, persistence, observability and closure one-to-one to Changes 2-18 |
| [feature_matrix/systemverilog_gap_inventory.tsv](systemverilog_gap_inventory.tsv) | Authoritative Batch 165 IEEE 1800-2017 clause inventory currently freezing 25 reviewed or closed supported families, five one-to-one active Changes 12-16 residual owners, and five explicit SDF, protected-envelope, FST, Accellera-SystemC/TLM/SCV, and legacy-PLI deferrals after timing-region closure |
| [feature_matrix/systemverilog_literal_width_inventory.tsv](systemverilog_literal_width_inventory.tsv) | Separate Batch 165 audit of SystemVerilog bit-string, based-number, unbased-unsized and host-word assumptions across lexical text, folding, types, constraints, containers, both engines, DPI/VPI, mixed debugging, traces, artifacts and UVM; it freezes 17 preserved paths, four active removal obligations and four explicitly governed physical boundaries |
| [app/sv_parameter_sizing_application_test.cpp](../app/sv_parameter_sizing_application_test.cpp) | Exact arbitrary-width self/context sizing and signedness, two-state casts and aggregate stores, lazy logical/conditional evaluation, concatenation/replication/streaming and dynamic selections, common-profile `inside`/`case inside`, wildcard equality, guarded/binding/tagged/structured `case matches`, and interpreter/LLVM O0/O2 cold/warm parity |
| [app/systemverilog_hir_application_test.cpp](../app/systemverilog_hir_application_test.cpp) | Retained SystemVerilog semantic HIR and constraint identities including a declared 137-bit `dist` weight whose known magnitude remains representable by the governed deterministic sampler |
| [../docs/verilog-2005.md](../../docs/verilog-2005.md), [tutorial](../../docs/verilog-2005-tutorial.md), and [closure audit](../../docs/verilog-2005-closure-audit.md) | Installed public statement of the zero-gap Verilog-2005 surface, producer-independent wide-literal workflow, exact clause/width/closure digests, and the rule that host/resource ceilings are physical evidence boundaries rather than language-width limits |
| [app/uvm_conformance_inventory.cpp](../app/uvm_conformance_inventory.cpp) | Post-load direct/object/design enforcement of the selected release inventory, retained class identities, evidence paths, uniqueness, counts, and zero supported gaps across the exact engine/cache/replay matrix |
| [../docs/uvm-tutorial.md](../../docs/uvm-tutorial.md) | Producer-independent source-set, interpreter/compiled/debug, cache/trace, portable artifact, plusarg, resource, pass-criteria, and failure-diagnosis usage bound to the executable inventory rather than parser-only claims |
| [runtime/runtime_uvm_foreign_abi_c_test.c](../runtime/runtime_uvm_foreign_abi_c_test.c) | C11 compile-time UVM foreign ABI version, x64 structure size/offset, append-only host-tail, and Windows calling-convention-compatible callback evidence |
| [frontend/frontend_tests.cpp](../frontend/frontend_tests.cpp) | Minimal VHDL context/package/entity/architecture/process syntax including reusable context declarations/references, constant-only packages, selected package-constant names, retained visibility, rising/falling-edge sensitivity refinement, Boolean literals/operators, packed shifts/rotates, conditional and selected assignments, nested conditional and sequential-case trees, static for, runtime while, and unconditional loops with conditional/unconditional `exit`/`next`, bounded process variables, and timed/any-change/condition waits; Verilog-2005/SystemVerilog `$clog2` constant-call HIR plus SystemVerilog package/import/scoped-constant/packed-typedef/enum/struct/union/member-select/indexed-select/replication-concatenation, complemented reduction/XNOR, bounded built-in gate primitives, `final`, all procedural compound assignments and standalone prefix/postfix updates, and module/procedural-variable/nested-conditional/event-control/wildcard/always_comb/always_latch syntax plus canonical bounded procedural for loops, locally static repeat, runtime while/post-test do-while/timed forever, nested `break`/`continue`, procedural condition waits with attached statements, and inline/module-scope genvar loops with assignment, prefix/postfix, and compound updates; non-ANSI Verilog ports and repeat/while/forever/wait statements; single/multi-root include/macro/conditional preprocessing with default arguments, token paste/stringification, persistent timescale/default-net/cell/keyword/unconnected-drive context, and source ancestry; bounded delay scaling; and targeted context/package/directive/include/macro/duplicate/initializer/type/wait/implicit-process diagnostics |
| [frontend/frontend_sv_conformance_tests.cpp](../frontend/frontend_sv_conformance_tests.cpp) | Independently authored, source-ID-marked SystemVerilog conformance pairs cross-referenced to permissively licensed sv-tests, Surelog, and slang expectations: defaulted/token-pasted macros, `macromodule`, escaped keyword identifiers, typed package enums/aliases, value/type parameters, parameterized interfaces/modports, static generate HIR, exact macro-arity/reserved-name rejection, and checked module/interface closing labels |
| [frontend/frontend_file_tests.cpp](../frontend/frontend_file_tests.cpp) | SystemVerilog-2017 bounded text-file system function/task HIR plus language, arity, literal-format, conversion-count, and recovery diagnostics |
| [frontend/frontend_container_tests.cpp](../frontend/frontend_container_tests.cpp) | SystemVerilog one-dimensional static/dynamic/queue/associative declarations and ports, methods, read-memory tasks, direct query calls, and positional/keyed assignment-pattern HIR; plus multidimensional/type/lifetime/method/pattern syntax and language-version diagnostics |
| [frontend/frontend_class_tests.cpp](../frontend/frontend_class_tests.cpp) | SystemVerilog class declaration, resolution, specialization, inheritance, hidden-member, covariant-return, and stable virtual-slot HIR plus malformed syntax/type/visibility/override/resource diagnostics |
| [elaboration/elaborator_test.cpp](../elaboration/elaborator_test.cpp) | Independently selected VHDL/SystemVerilog counters, bounded recursive VHDL context/package-constant visibility/transitive provenance and missing/ambiguous/cyclic visibility diagnostics, language-specific nested conditional and assertion truth semantics, SystemVerilog `$clog2` edge values/parameter dependencies/arity and unsupported-negative diagnostics, static/runtime/nested loop-control execution with innermost `break`/`continue` and `exit`/`next` targeting, unconditional VHDL and post-test SystemVerilog loops with guaranteed-first-iteration and trailing-condition `continue` evidence, dependency-driven VHDL/SV condition waits with false/X rechecks, attached statements, and constant-false terminal suspension, specialization-selected VHDL/SV conditional, iterative, and selection generate with recursive labeled/indexed/alternative mixed-binding paths, external-genvar ascending mixed hierarchy and postfix-decrement descending behavior, VHDL ascending/descending selection ranges with bound-evaluation and interval-overlap diagnostics, always-selected VHDL block and SV direct/named static generate bodies, implicit SV generate behavior, declaration-ordered and loop-index-dependent generated constant/parameter folding with evaluation/subtype failures, executable scoped local signals/assignments/processes, per-iteration loop-variable behavior, loop-variable construction actuals, descending ranges, multi-choice/default/no-match behavior, and evaluation/stall/overlap diagnostics, executable SV-parent and VHDL-parent mixed hierarchies, bidirectional bounded VHDL/SV construction-actual specialization plus ambiguous case-folded-name rejection, typed HDL→SystemC→HDL and SystemC-top→HDL hierarchy with shared signal IDs, construction actuals in both SystemC boundary directions including provider-built parameter-dependent ports and missing/unknown/subtype diagnostics, and missing/invalid-child-binding rejection, persistent named VHDL/SV local-register lowering and reads, source-level timed/dynamic wait lowering and wakeups, deterministic static/dynamic wildcard sensitivity inference and latch retention, scalar implicit-net initialization, cell specialization metadata, omitted-input pulls, dense instance-specific specialization ownership with source/language/library metadata, cross-library VHDL selection, and targeted unsupported-semantics/driver diagnostics |
| [elaboration/elaborator_sv_conformance_test.cpp](../elaboration/elaborator_sv_conformance_test.cpp) | Independently authored public-expectation SystemVerilog elaboration pairs for parameterized `macromodule` instances, package constants/types, parameterized interface/modport binding, generated specialization paths and widths, plus exact nonstatic-generate, required-type-parameter, ambiguous-package-type, and wrong-interface-type diagnostics |
| [elaboration/elaborator_sv_file_test.cpp](../elaboration/elaborator_sv_file_test.cpp) | Stable SimIR lowering for open/close/literal/formatted/string write, line read, EOF, and error operations through module and suspending-task integer handles, with invalid handle/string-target diagnostics |
| [elaboration/elaborator_sv_container_test.cpp](../elaboration/elaborator_sv_container_test.cpp) | Typed static/dynamic/queue/associative object, callable, port-alias, query, read-memory, and assignment-pattern lowering with interpreter execution plus exact kind/range/index/capacity/context diagnostics |
| [elaboration/elaborator_vhdl_dynamic_slice_test.cpp](../elaboration/elaborator_vhdl_dynamic_slice_test.cpp) | Fixed-width ascending/descending VHDL runtime slices over packed values and record members, checked local/signal targets, projected waveform scheduling, and exact malformed/type/direction/width/chained-target/runtime-bound diagnostics |
| [elaboration/elaborator_vhdl_composite_test.cpp](../elaboration/elaborator_vhdl_composite_test.cpp) | Bounded VHDL access ownership/allocation/dereference/callable/escape behavior, protected declaration-body conformance/private layout/shared construction/wait-free method scheduling, and signed-32-bit physical ranges/units/folding/arithmetic/conversion with hierarchy, debug metadata, interpreter execution, overflow, and exact negative diagnostics |
| [runtime/runtime_file_tests.cpp](../runtime/runtime_file_tests.cpp) | Isolated manifest-root text-file round trips, exact contents, EOF/error state, 4,096-byte policy, unsupported binary mode, root escape, cross-process ownership, stale/closed handles, and deterministic flushing |
| [runtime/runtime_tests.cpp](../runtime/runtime_tests.cpp) | Source-ID-marked common scheduler, SimIR, failure-containment, and VCD expectations plus packed values including the checked allocation-free ≤64-bit `Logic4Word` path, exhaustive standard-logic resolution, wide arithmetic including 65-bit exponentiation, wide logical/arithmetic shifts and rotations with four-state end-element fill, oversized fill, and modulo-width wrap, wide exact X/Z case equality, selector-/choice-side `casez`/`casex` wildcard vectors, and one-sided `==?` vectors plus logical-equality unknown-dominance regression, phase ordering, delta limit, SimIR timed/static waits and operations including resumable non-design Pause and noninitializing static-process wakeup and exactly-once natural/design-stop final-process execution after ordinary pending work is discarded, source-point ordering and stable continuation requeueing, update coalescing, interpreter/external-executor scheduled writes, transition-aware continuous inertial writes with stable-RHS preservation, per-scalar VHDL projected transport/inertial/rejection queues for vectors and slices, an edge-filtered dynamic `WaitOn` comparison covering duplicate normalization, a rejected opposite edge, and tick-2/tick-3 delta-1 wakeups, force/release, design-stop lifecycle, and VCD core |
| [runtime/runtime_vpi_reference_plugin_tests.cpp](../runtime/runtime_vpi_reference_plugin_tests.cpp) | Independent C/C++ VPI v2 service transcripts, frozen v1-prefix compatibility, malformed/resource/post-unload negatives, repeated loading, and relocated images |
| [runtime/runtime_vhpi_reference_plugin_tests.cpp](../runtime/runtime_vhpi_reference_plugin_tests.cpp) | Independent C/C++ VHPI v2 service transcripts across all service families, frozen v1-prefix compatibility, malformed/post-unload negatives, interpreter/LLVM labels, repeated loading, and relocated images |
| [runtime/runtime_class_heap_tests.cpp](../runtime/runtime_class_heap_tests.cpp) | Opaque nullable/generation-safe class handles, transactional construction, aliases/casts/properties, instance/static/virtual methods, suspension, shared static initialization, every supported handle-container shape, and checked stale/null/type/resource failures |
| [runtime/runtime_uvm_object_policy_tests.cpp](../runtime/runtime_uvm_object_policy_tests.cpp) | Simulation-owned UVM line/tree/table printing, deep/shallow/reference comparison, and owner-branded transactional copying across scalar, string, object, sequential/keyed-array, alias, and cyclic graphs; physical/abstract/type knobs, field-recursion overrides, complete bounded mismatch accounting, stable escaped formatting, automation-hook rollback, stale/foreign/type rejection, cataloged policies, and output/work ceilings |
| [runtime/runtime_uvm_packer_tests.cpp](../runtime/runtime_uvm_packer_tests.cpp) | Typed big/little-endian UVM scalar, byte, integer, string, real, object-identity, and recursive-array packing; four-/nine-state bits, optional metadata, exact unpack round trips, malformed/truncated/trailing/policy rejection, and depth/item/bit/payload ceilings |
| [runtime/runtime_uvm_synchronization_policy_tests.cpp](../runtime/runtime_uvm_synchronization_policy_tests.cpp) | Simulation-owned UVM named event pools, trigger/persistent/on/off waits, payloads, callback snapshot dispatch and containment, reset/cancel lifecycle, threshold/auto-reset barriers, deterministic generic pools and queues, heartbeat observation windows, spell challenges, stale-state diagnostics, and aggregate resource ceilings |
| [runtime/runtime_uvm_factory_tests.cpp](../runtime/runtime_uvm_factory_tests.cpp) | Simulation-isolated UVM factory create/debug tracing with ordered instance/type override steps, deterministic inventories, callback containment, oldest-first eviction, stable escaping, and record/output ceilings |
| [runtime/runtime_uvm_resource_tests.cpp](../runtime/runtime_uvm_resource_tests.cpp) | Simulation-isolated UVM resource name/type lookup, read, and accepted/rejected write tracing with deterministic audit/report order, callback containment, stable escaping, eviction, and record/output ceilings |
| [runtime/runtime_uvm_config_db_tests.cpp](../runtime/runtime_uvm_config_db_tests.cpp) | Simulation-isolated UVM config set/get/exists tracing and audited precedence/read/write inventories with callback containment, stable escaping, deterministic eviction, and entry/waiter/report/trace ceilings |
| [runtime/runtime_uvm_command_line_tests.cpp](../runtime/runtime_uvm_command_line_tests.cpp) | Simulation-isolated ordered argv retention, plusarg/UVM subsets, exact and prefix matching, first/all value extraction with duplicates, tool/version identity, factory/config/default and phase/time verbosity/max-quit/objection-trace application, deterministic hierarchical matching, malformed transactional rejection, and bounded argument/query/result/report-control resources |
| [runtime/runtime_uvm_test_runner_tests.cpp](../runtime/runtime_uvm_test_runner_tests.cpp) | Simulation-owned `run_test` selection and precedence, deterministic seed and global-timeout handling, topology output, repeated-run cleanup, `$finish` success, fatal/exception containment, re-entry rejection, and bounded run/name/topology/message resources |
| [library/library_artifact_test.cpp](../library/library_artifact_test.cpp) | CM-087 canonical `.fsimlib` metadata, checksum validation, deterministic publication, read-only installation, schema-27 portable VHDL/SystemVerilog unit and embedded-PSL round trips, complete VHDL-2019 frontend-form preservation, relocation, and malformed/schema/path/depth/identity rejection |
| [api/api_mapped_library_test.cpp](../api/api_mapped_library_test.cpp) | CM-087 append-only C API inspection of ordered mapped-library identity, digest, unit count, and optional-native admission without disturbing legacy clients |
| [project/project_config_test.cpp](../project/project_config_test.cpp) | Source-ID-marked project acceptance/diagnostic expectations: schema-1 manifests including deterministic `min`/`typ`/`max` delay-mode configuration and default, glob ordering, schema/unknown/invalid-value diagnostics, and JSON escaping |
| [app/expression_application_test.cpp](../app/expression_application_test.cpp) | Fast source-to-runtime interpreter/compiled differential for expression checkpoints, currently covering VHDL/Verilog-2005/SystemVerilog fixed-width exponentiation; SystemVerilog final-procedure lifecycle, procedural compound/standalone updates, wildcard equality/inequality and logical-equality unknown dominance, derived `$clog2` widths and cache-distinct specializations, `$signed`/`$unsigned` comparison/shift semantics, `$isunknown`, `$onehot`/`$onehot0`, `$countones`/`$countbits`, packed `$bits`, one-dimensional packed bound/size/increment queries with optional dimension `1`, and packed dimension counts; plus VHDL conditional and selected assignments, concurrent assertions, direct timing/driver attributes, duration-qualified implicit signal attributes in expressions/process sensitivities/waits, redundant transaction toggles, signed `abs`, `sla`/`rol`/`ror`, negative-count reversal, oversized arithmetic fill, modulo-width rotation, and LLVM O0/O2 execution |
| [app/application_test.cpp](../app/application_test.cpp) | Check/build/cache/run, exact parsed-byte compilation-unit/transitive-include digests, included/shared-root and imported VHDL-context/package specialization provenance and invalidation, file/source-set/combined preprocessing policies with library ownership and shared default-net/cell/reset state, per-specialization provenance reuse/invalidation, VHDL-generic/package-constant and SystemVerilog-parameter specialized width/value interpreter-versus-LLVM execution, bidirectional bounded VHDL/SV construction-actual behavior, external-genvar/postfix-increment three-iteration and conditional/selection generated SV→VHDL hierarchies with stable selected/indexed/alternative bindings, descending VHDL case-generate range selection, executable conditional, implicit, direct/named-static SV and unguarded-block VHDL local signals/processes with declaration-ordered generated constants/parameters, scoped VCD names, and cold/warm native-cache identity, selective native-cache invalidation, and separated VHDL entity-interface provenance, bounded O0/O2 VHDL/SystemVerilog nested-conditional, static/runtime loop-control, unconditional VHDL-loop, and post-test SV-loop state/change/VCD differentials, persistent local-register behavior, O2 mixed SV/VHDL/SV interpreter-versus-hybrid differential execution with normalized serialized VCD equality, exact fully compiled O2 scheduled-write comparison at ticks 0 and 2, interpreter/compiled scheduling-overflow containment, static/dynamic/wildcard SV wakeups and periodic VHDL timed waits, supported-sibling compilation beside a 65-bit fallback process, eligible/fallback process and compiled-module counts, per-module native-cache cold/warm telemetry, forced-O0 hybrid debugger selection with source/conditional-signal breakpoints, all four step modes, packed local reads, live debug-VCD selection, real-SIGINT safe-point stop/resume/handler restoration, and exact interpreter transcript/lifecycle/callback/final-state equivalence, official Accellera 3.0.2 compile/runtime and typed-factory validation with HDL-selected and SystemC-top plug-in roots, typed direct factory construction values, native `SC_METHOD` execution driven from SV and VHDL, Accellera `SC_THREAD` timed/delta/named-event suspension and compiled-SV-clocked `SC_CTHREAD` static waits, native child hierarchy, root-scoped lifecycle phases, direct-parent port chains, standard signal-interface exports with explicit hierarchy metadata, SystemC-method top initialization, `dont_initialize()` wakeup, scalar-edge counting, update-phase propagation, callback exception containment, exact time conversion, `` `timescale``-driven `auto` resolution/runtime scaling, scaled VCD, and value parsing |
| [app/application_test_classes.cpp](../app/application_test_classes.cpp) | Source `new`/`null`/`$cast`, constructors, properties, hidden inheritance, ordinary/static/nonvirtual/explicit-base/virtual functions, suspending class/module tasks, typed handle containers, generated multiple roots, shared statics, callbacks/safe points/debugger/VCD, interpreter/LLVM O2/debug-O0 snapshots, cold/warm/edit caches, and source-hidden copied `.fsimdesign` plus relocated mapped `.fsimlib` execution |
| [app/vhdl_psl_application_test.cpp](../app/vhdl_psl_application_test.cpp) | VHDL PSL declaration specialization and temporal execution through interpreter/debug/LLVM O0/O2 and cold/warm cache; deterministic duplicated/reversed VHDL roots; colliding-name SystemVerilog and SystemC roots; attempt/callback/report/coverage identity, ordering, containment, teardown, and bounded resource evidence; occurrence-qualified bounded debug snapshots, `vhdl` debugger views, VCD activity, file/access/protected/physical and alias/external-name visibility, driver/postponed-region metadata, breakpoint resume identity, and stale/foreign VHPI rejection; plus source-hidden `.fsimobj`, relocated mapped `.fsimlib`, relocated `.fsimdesign`, cold/warm replay, and portable VHPI checkpoint handle remapping with exact attempt/coverage/debug/VCD equality |
| [app/typed_boundary_application_test.cpp](../app/typed_boundary_application_test.cpp) | One SV→VHDL→SystemC graph proving positive and negative boundary legality, width conversion, stable semantic/DesignIR identities, owning HDL/SystemC source and macro-expansion provenance, canonical serialization independent of traversal order, portable normalized paths, and a VHDL PSL assertion that samples the converted SystemC result after stable delta propagation through interpreter/LLVM O0/O2/debug, VCD, cold/warm cache, and macro-source edit invalidation |
| [app/application_test_mixed.cpp](../app/application_test_mixed.cpp) | Source-ID-marked bidirectional SystemVerilog/VHDL construction-actual transfer, stable selected/generated hierarchy paths, interpreter/LLVM execution, specialization identities, and cold/warm native-cache agreement |
| [app/mixed_conversion_application_test.cpp](../app/mixed_conversion_application_test.cpp) | Source-ID-marked SV→VHDL→SV width, signedness, Boolean/integer, two-/four-state conversion matrix with exact conversion/adapter counts, source spans, debugger locals, VCD, interpreter/LLVM O0/O2 parity, cold/warm reuse, and edit invalidation |
| [app/resolution_application_test.cpp](../app/resolution_application_test.cpp) | Source-ID-marked mixed VHDL/SystemVerilog driver ownership and standard-logic resolution plus strength conflicts, pulls/supplies, MOS/resistive MOS, direct/conditional/vector/cyclic/resistive transmission graphs, scalar/packed charge retention and decay, zero/inertial/transport/reject/transition/region/postponed timing across both boundary directions, callbacks/debugger/VCD, `.fsimobj`/`.fsimdesign`/relocated `.fsimlib`, corrupt-artifact rejection, and interpreter/LLVM O0/O2 cold/warm/edit parity |
| [app/vhdl_logic9_application_test.cpp](../app/vhdl_logic9_application_test.cpp) | Checksum-pinned compiler-supplied `ieee.std_logic_1164` declaration/body provenance, declaration inventory, redeclaration rejection, exact nine-state vector truth tables, standard two-driver resolution, rising/falling edges, bounded vector type conversions, projected transactions, locals, VCD, mixed boundaries, interpreter/LLVM O0/O2 parity, and cold/warm native-cache reuse |
| [app/vhdl_numeric_application_test.cpp](../app/vhdl_numeric_application_test.cpp) | Checksum-pinned `ieee.numeric_std`/`numeric_bit` provenance, two-/nine-state signed and unsigned profiles, arithmetic/comparison/absolute/shift/rotate, bounded resize and integer/signedness conversions, size/width diagnostics, interpreter/LLVM O0/O2 parity, and cold/warm native-cache reuse |
| [app/vhdl_fixed_application_test.cpp](../app/vhdl_fixed_application_test.cpp) | Checksum-pinned `ieee.fixed_generic_pkg`/`fixed_pkg` dependency provenance, constrained signed/unsigned fixed types, static conversion and saturation, arithmetic/comparison, scale-aware nearest-rounded resize, slicing and range diagnostics, interpreter/LLVM O0/O2 parity, and cold/warm native-cache reuse |
| [app/vhdl_float_application_test.cpp](../app/vhdl_float_application_test.cpp) | Checksum-pinned `ieee.float_generic_pkg`/`float_pkg` dependency provenance, constrained binary32 values, static conversion/rounding/arithmetic/comparison/classification, exceptional values, raw-vector/integer conversion diagnostics, interpreter/LLVM O0/O2 parity, and cold/warm native-cache reuse |
| [app/vhdl_ieee_integration_application_test.cpp](../app/vhdl_ieee_integration_application_test.cpp) | Reusable-context activation of every reviewed package; clean-room VITAL type/source provenance, composite constants/generics, delays/maps, all combinational primitive families, wide/null/direction/all-state semantics, static truth tables, negative profiles, typed debugger locals, exact VCD, O0/O2 parity, cold/warm reuse, and context-edit invalidation |
| [app/vhdl_vital_delay_application_test.cpp](../app/vhdl_vital_delay_application_test.cpp) | VITAL signal/wire/path and pulse scheduling plus configured vendor-style cell/memory models, static embedded memory contents, VITAL_LEVEL metadata, guarded timing, extended identifiers, component bindings, null paths, interpreter/LLVM O0/O2/debug, cold/warm cache, runtime-state, `.fsimobj`, relocated `.fsimdesign`, callback, and VCD parity |
| [app/vhdl_analysis_order_application_test.cpp](../app/vhdl_analysis_order_application_test.cpp) | Independently authored, source-ID-marked VHDL library/package/context/entity/architecture/type/subtype/generic/component/configuration/generate structural conformance, successful generated elaboration, deterministic source-unit order, and all eight exact package-body, context/use, architecture/configuration, entity/configuration-binding, and direct-configuration analysis-order failures |
| [app/tcl_application_test.cpp](../app/tcl_application_test.cpp) | Embedded Tcl repeatable-command and script batches, Tcl argument variables, fsim-owned standard streams, deterministic embedded exit codes, multiline interactive command completion, prompts/results, project/check/build dictionaries, signal hierarchy/value access, deposit/force/release semantics, time-limited and terminal runs, status dictionaries, and targeted evaluation/incomplete-input diagnostics |
| [app/display_application_test.cpp](../app/display_application_test.cpp) | Verilog/SystemVerilog literal, empty, constant numeric, and one-value `%b`/`%h`/`%o`/`%d`/`%c`/`%s` runtime `$display`/`$write` ordering, `%0` leading-zero suppression, formatted `$strobe` capture-before-mutation, literal-only `$monitor` initial publication, and all-severity source-aware VHDL reports including callback-before-failure termination; decoded language escapes, signed/unknown formatting, newline/no-newline and immediate/postponed policy, process/time/delta metadata, CLI routing, and interpreter versus LLVM O0/O2 equivalence |
| [app/sv_file_application_test.cpp](../app/sv_file_application_test.cpp) | Isolated same-language SystemVerilog text-file interpreter/LLVM O0/O2 exact-content and line-value parity, native execution without fallback, task stop/inspect/resume, root confinement, cold/warm cache reuse, input-content exclusion from compilation identity, selective HDL-literal invalidation, and exact 137-bit packed-signal/fixed-memory `$fread` plus read/write-memory, debugger, callback, VCD, and fully compiled O0/O2 cold/warm evidence |
| [app/sv_container_application_test.cpp](../app/sv_container_application_test.cpp) | SystemVerilog static/dynamic/queue/associative objects and nested port aliases through queries, read-memory, positional/keyed assignment patterns plus static default/index-key construction, automatic functions/tasks, suspension, debugger inspection, formatted output, exact X/Z state, interpreter/LLVM O0/O2, and cold/warm cache reuse |
| [app/sv_conformance_application_test.cpp](../app/sv_conformance_application_test.cpp) | Independently authored, source-ID-marked SystemVerilog runtime conformance across short-circuit and wildcard expressions, loop control, callable copy/suspension, event and nonblocking timing, immediate assertions, mutable strings and formatted files, read/write memories, dynamic/queue/associative containers, exact final values/time/file bytes, interpreter/LLVM O0/O2 parity, and cold/warm native-cache reuse |
| [app/vhdl_advanced_type_application_test.cpp](../app/vhdl_advanced_type_application_test.cpp) | Joint access/protected/physical interpreter and LLVM O0/O2 differential with private protected storage, debug locals and signal reads, normalized VCD, cold/warm native-cache reuse, and edited-source analysis/specialization/native invalidation |
| [app/random_application_test.cpp](../app/random_application_test.cpp) | Default, numeric, entropy-selected, and native-session seeds; stable independent per-process streams; bare/parenthesized `$random` and `$urandom`; one-/two-bound and reversed `$urandom_range`; X-bound behavior; signed formatting; and interpreter versus LLVM O0/O2 equivalence |
| [app/assertion_application_test.cpp](../app/assertion_application_test.cpp) | Standalone SystemVerilog severity tasks; implicit immediate-assertion errors; selected simple and lexical-block pass/failure actions with locals; nonfatal continuation; fatal single publication; source/severity metadata; CLI rendering; and interpreter versus LLVM O0/O2 equivalence |
| [app/line_directive_application_test.cpp](../app/line_directive_application_test.cpp) | SystemVerilog logical `` `line`` filename/line propagation into typed report/debug locations; distinct physical specialization/cache ownership; interpreter versus LLVM O0/O2 semantic and callback equivalence; native-object cold/warm reuse and logical-remap invalidation |
| [app/time_application_test.cpp](../app/time_application_test.cpp) | Compilation/module SystemVerilog `timeunit`/`timeprecision`, exact fractional/scientific HIR, explicit unit suffixes, precision-first half-up rounding, `auto` 1 ps resolution, timestamp callbacks, normalized VCD, coarse-resolution rejection, and interpreter versus LLVM O0/O2 plus cold/warm native-cache equivalence |
| [app/delay_mode_application_test.cpp](../app/delay_mode_application_test.cpp) | Manifest and CLI `min`/`typ`/`max` delay selection, typical default, selection-before-rounding timestamps, branch-specific `auto` resolution, interpreter versus LLVM O0/O2 semantic equivalence, normalized VCD, and cold/warm mode-distinct native-cache behavior |
| [app/transition_delay_application_test.cpp](../app/transition_delay_application_test.cpp) | SystemVerilog one/two/three-value rise/fall/turnoff delays for continuous whole/slice writes and supported gates, actual-transition selection, pulse rejection, interpreter versus LLVM O0/O2 equivalence, normalized VCD, and cold/warm cache reuse |
| [app/vhdl_projected_application_test.cpp](../app/vhdl_projected_application_test.cpp) | VHDL implicit/explicit inertial, transport, explicit rejection, sequential/conditional/selected concurrent whole and slice assignments, exact pulse-boundary timestamps, greater-than-delay rejection, interpreter versus LLVM O0/O2 equivalence, normalized VCD, and cold/warm cache reuse |
| [app/vhdl_procedure_wait_application_test.cpp](../app/vhdl_procedure_wait_application_test.cpp) | VHDL waits nested through static loops, conditionals, and two-level procedure calls, including event/condition rechecks, preserved absolute timeout, process repetition, and interpreter versus LLVM O0/O2 schedule equivalence |
| [app/vhdl_enumeration_application_test.cpp](../app/vhdl_enumeration_application_test.cpp) | VHDL package enumeration types and ascending/descending constrained subtypes, literal/constant/prior-generic bounds, identifier/character literals, typed constants/generics, subtype-left defaults, direction-aware scalar attributes through imported/subtype type marks, static package/generic folding, checked dynamic successor and constrained-store failures, directionally safe hierarchy aliases, delayed/multi-waveform stores, assignments, case choices, equality/ordinal comparison, debugger literal names, interpreter versus LLVM O0/O2 equivalence, normalized VCD, cold/warm cache reuse, and package-edit invalidation |
| [app/vhdl_array_application_test.cpp](../app/vhdl_array_application_test.cpp) | Nominal multidimensional, null, nested-array, and array-of-nested-record VHDL values; interleaved index/member reads and targets; direct/component/generic-dependent and callable copies; recursive specialization identity; disjoint/resolved/nested drivers and selected sensitivity; debug-visible locals and projected single/multi signal targets; hierarchy/debugger inspection, normalized VCD, interpreter/LLVM O0/O2 cold-warm parity, package-edit invalidation, and identical runtime-bound failures |
| [api/api_test.cpp](../api/api_test.cpp) | Source-ID-marked C API/callback/handle expectations: C ABI lifecycle, generation-checked hierarchy/value handles, force/deposit/release, synchronous process-bearing safe-point callbacks and re-entry guards, ordered VHDL/SystemVerilog note/warning/error/failure callbacks without fatal duplication, run, statement/process/delta/time stepping, callback-issued asynchronous stop/resume, and terminal-stop precedence |
| [api/api_header_c_test.c](../api/api_header_c_test.c) | Source-ID-marked strict-C public-header ABI prefix, flag separation, callback-layout, session creation, version, and destruction checks |
| [compiler/cache_test.cpp](../compiler/cache_test.cpp) | SHA-256 stability, process-aware locking/stale-lock recovery, atomic replacement, and cache store/load/erase |
| [compiler/jit_runtime_c_test.c](../compiler/jit_runtime_c_test.c) | C11 compilation, preserved resume-status values 0–5, appended wait/debug/permanent-wait statuses 6–9, fixed ABI offsets/size, and callback layout for the append-only v1 scheduled-write, inertial/projected-write, debug-control, signal-event, signal-last-value, elapsed-event-time, signal-transaction, language-output, report, runtime-formatting, mutable-string, and opaque text-file fields |
| [compiler/llvm_jit_test.cpp](../compiler/llvm_jit_test.cpp) | Source-ID-marked LLVM execution/cache/rejection expectations: LLVM O0/O2 CFG, logical/reduction/arithmetic-shift/rotate truth tables, exhaustive scalar four-state case-equality, `casez`, `casex`, and one-sided wildcard-equality kernels plus vector unknown-dominance cases, scheduled/inertial/projected writes, `WaitOn`, `WaitSensitivity`, `WaitForever`, and source `DebugPoint` operations; grouped specialization modules with atomic validation, execution, warm reuse, and member invalidation; layout-preserving resume statuses; O0-unconditional/O2-size-gated debug suspension; suspension- and debug-safe cyclic CFGs with raw zero-time-cycle rejection; sensitivity-only 128/257-bit signals; wait-list/signal/width/static-and-dynamic-edge validation; runtime-tail validation; and persistent-cache tests for referenced values, wildcard comparison policy, scheduled kind/delay, projected mode/delay/rejection, wait kind/operand/referenced-width/edge, and source-point identity |
| [systemc/systemc_compatibility_test.cpp](../systemc/systemc_compatibility_test.cpp) | Official Accellera SystemC version, module, process, signal, export-macro, ABI-identity, and compiler/standard-library compatibility expectations |
| [systemc/systemc_abi_c_test.c](../systemc/systemc_abi_c_test.c) | Source-ID-marked strict C11 compilation plus append-only host/registrar layout and opaque-handle checks |
| [systemc/plugin_loader_test.cpp](../systemc/plugin_loader_test.cpp) | Source-ID-marked native plug-in load/lifecycle and transactional factory registration, typed module/port/foreign-child construction, method/static-edge sensitivity and initialization metadata with opaque-handle connectivity, and throwing/rejected-initializer containment without partial registration |
| [systemc/plugin_compiler_test.cpp](../systemc/plugin_compiler_test.cpp) | Source-ID-marked direct-argv compiler plans, transitive dependency keys, tracked linked inputs, non-cacheable dependency gaps, cache locks, and cold/warm compilation |
| [app/application_systemc_matrix_test.cpp](../app/application_systemc_matrix_test.cpp) | Source-ID-marked method/thread/event/signal/channel/update and lifecycle scheduling matrix through SystemVerilog and VHDL hosts, interpreter/LLVM O0/O2 parity, debugger/VCD, cold/warm cache, and callback-order evidence |
| [frontend/frontend_strength_tests.cpp](../frontend/frontend_strength_tests.cpp) | Verilog-2005 drive/pull/charge strength and MOS/transmission primitive HIR, scalar/vector/static-array forms, source metadata, and targeted pair/polarity/context/terminal/resource diagnostics |
| [elaboration/elaborator_strength_test.cpp](../elaboration/elaborator_strength_test.cpp) | Strength and passive-switch lowering through generated and parameter-specialized hierarchy, aliased multiple roots, searched libraries, VHDL/SystemC wrappers, and disconnected/cyclic topology execution |

Several current executables cover many features at once. They are useful
architecture-gate tests, but they do not replace the atomic positive, negative,
elaboration, and runtime matrix required for v1.

The Batch 128 exact conformance corpus is machine-readable in
[`v1_conformance_corpus.txt`](v1_conformance_corpus.txt). Its 28 fixture rows
map all 100 adjacent `FSIM-CONFORMANCE` expectation markers to 28 registered
CTest owners and exact evidence modes. `fsim.v1-conformance-corpus` validates
the complete marker digest, allowed provenance, unique IDs, fixture/test
ownership, expectation-specific acceptance/execution/failure modes, and the
full frontend/elaboration/interpreter/LLVM/cache/debug/callback/VCD/source/
portable/API/plug-in/Tcl/CLI evidence union.

The Batch 119 frontend fixture
[`frontend_vhdl_time_file_tests.cpp`](../frontend/frontend_vhdl_time_file_tests.cpp)
retains nested wait trees, general report and severity expressions, nominal
file types and file objects across package/architecture/process/block regions,
file-interface subprogram profiles, TextIO-like calls, physical-time
expressions, and inertial/transport/reject waveform metadata. Its malformed
companion proves the new file/open/report/severity diagnostics. This is P+/P-
evidence only for the remaining areas. Batch 119 Task 2 adds exact
overload-resolved nested-procedure wait legality, interpreter execution, and
LLVM O0/O2 differential evidence. Task 3 adds general runtime report and
severity evaluation, skipped passing assertions, exact type diagnostics,
source metadata, standalone/assertion failure policy, and interpreter/LLVM
O0/O2 differential evidence. Task 4 adds bounded manifest-confined VHDL file
objects, declaration/status opens, modes, close/lifetime, file-formal aliasing,
lookahead `endfile`, integer-element direct I/O, exact diagnostics, and merged
interpreter/LLVM O0/O2 file-application evidence. Task 5 adds bounded TextIO
line/cursor reads, writes, success/failure, side/field formatting, line
clearing, profile diagnostics, and exact interpreter/LLVM O0/O2 bytes. Task 6
adds the signed-64-bit predefined `time` type, exact standard-unit and
resolution normalization, qualification, static arithmetic/comparison,
expression-valued wait/timeout scheduling, automatic-resolution selection,
three exact failure classes, and interpreter/LLVM O0/O2 parity. Task 7
completes scalarized projected-waveform replacement, transport truncation,
inertial pulse rejection, ordered whole/slice waveforms, exact delta behavior,
multi-driver resolution, VCD, cold/warm cache reuse, and interpreter/LLVM
O0/O2 parity. Task 8 adds same-language time-generic hierarchy,
nested-callable suspension,
source-scoped interpreter/O0/O2/debug execution points, normalized VCD,
stable specialization keys, and cold/warm report, file/TextIO, time, and
transaction cache evidence. Task 9 adds the accumulated positive/negative,
interpreter/O0/O2/debug, cold/warm/source-edit, scheduling, normalized-VCD,
I/O, time-resolution, and exact-failure differential gate. Task 10 owns the
batch release gates.

## Differential runtime rule

Runtime evidence must start from the same elaborated DesignIR and deterministic
seed, then execute once with the interpreter and once with the LLVM 22.1.8
engine. The harness must compare:

- exit/stop status, final time, and delta observations;
- every debug-visible final value;
- assertion severity, location, and message;
- committed value-change callbacks; and
- normalized VCD events when tracing is relevant.

An LLVM adapter unit test by itself is not differential evidence. An
interpreter-only language test by itself is not differential evidence.

The current application differential test compares status, time, delta,
committed-change callbacks, final values, and normalized serialized VCD for a
bounded SystemVerilog hierarchy at O0 and O2 and for the vertical SV/VHDL/SV
hierarchy at O2. It also
compares a fully compiled O2 scheduled-write process with an update commit at
tick 0 and delayed commit at tick 2. The overflow companion runs through both
engines and checks that a scheduler exception raised in a generated callback
does not cross the C ABI, is rethrown to the application, poisons the
simulation, and publishes no write. A separate O2 positive-edge fixture
compiles both processes and exactly matches the initial trigger at tick 0, the
rising trigger at tick 1/delta 0, the observer update at tick 1/delta 1, and
the falling trigger at tick 2/delta 0. Those two processes share one
specialization module and therefore require exactly one cold cache miss/store
and one warm hit. The suite also asserts per-module cold/warm telemetry at O0
and O2. A VHDL assertion fixture separately requires identical process,
instruction, severity, source location, and message from the interpreter and
compiled O0/O2 engines. This remains bounded architecture evidence: complete
HDL event controls, generic/parameter specialization identity, exhaustive
assertion fixtures, O0 mixed-language/application scheduled-write/application
sensitivity coverage, exhaustive semantic and trace fixtures, and Windows
execution evidence remain outstanding.

A separate two-process fixture places a supported scalar process beside a
65-bit value-bearing process in one specialization. The hybrid application
compiles the scalar sibling into one module and leaves only the wide sibling
on the interpreter, with exact final-state and scheduler equivalence.

The runtime suite separately compares an interpreted dynamic `WaitOn` process
with an alternate executor at the shared kernel boundary. It verifies that a
duplicate signal operand is normalized, wakeups occur at ticks 1 and 2 in
delta 1, and final state and resume PCs agree. This is SimIR/kernel evidence,
not frontend evidence for general HDL event-control syntax.

## Matrix maintenance

When adding or changing a construct:

1. update the row in `docs/feature-matrix.md`;
2. add all missing evidence without broadening an existing fixture
   accidentally;
3. link the registered test paths from the row; and
4. change a status to `execute` only when the row's exact supported boundary is
   truthful.

Unsupported syntax must receive a targeted diagnostic. A recovery parser may
continue after that diagnostic, but it must not silently discard the construct.

## Release gate

The v1 release is blocked if any required row remains `v1 target`, any required
P+/P-/E/R cell is empty, any interpreter/JIT comparison differs, or any required
test fails on Ubuntu x86-64/GCC or Windows x86-64/MSVC in either Debug or
Release. Deferred rows are excluded unless they are explicitly promoted into
the v1 scope.
