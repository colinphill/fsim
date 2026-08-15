<!-- SPDX-License-Identifier: Apache-2.0 -->
# Batch 170 SDF/VITAL and mixed-language release audit

This audit freezes the completed VHDL/VITAL and mixed-language SDF application
boundary layered on the Batch 168 parser/resolver and Batch 169
Verilog/SystemVerilog timing application.

## Reviewed scope

- `B170-C19-LEDGER`: all seventeen Changes 2-18 rows in
  `tests/feature_matrix/sdf_vital_inventory.tsv` are `preserved`; its SHA-256
  is `3e84f643e6df24090efb1161e0d6836847784268a139e3da3c2fda4568913c6f`.
- `B170-C19-PROFILES`: SDF 2.1/3.0/4.0 applies without widening to VHDL
  87/93/2000/2002/2008 beside every Batch 167 Verilog/SystemVerilog profile and
  the governed official Accellera SystemC 3.0.2 boundary.
- `B170-C19-MODELS`: standard-cell, primitive, state-table, memory and
  explicitly governed wrapper models use structural VITAL ownership rather
  than proprietary model-name heuristics.
- `B170-C19-BOUNDARIES`: both directions of VHDL-Verilog,
  VHDL-SystemVerilog and VHDL-SystemC paths preserve Logic9, width, hierarchy,
  direction, resolution, scheduler coordinates and effective timing identity.
- `B170-C19-PUBLIC`: VHPI/VPI, debugger, callback, internal trace and VCD
  expose immutable bounded effective timing. Project, CLI, Tcl, C, C++ and
  non-project phases retain equivalent controls, summaries and failures.
- `B170-C19-ARTIFACTS`: object, design, mapped-library, native-cache and
  checkpoint forms retain exact source/effective values, policy, provenance,
  multiple roots and physical relocation independence.
- `B170-C19-NEGATIVE`: ambiguity, mismatch, unsupported model, overflow and
  resource failures are cataloged, bounded and transactional.
- `B170-C19-DEFERRED`: proprietary library-name heuristics, ungoverned vendor
  extensions and VHDL-AMS remain outside the reviewed digital subset.

## Executable closure

`fsim.sdf-vital-closure` retains separate corpus, negative and portability
logs plus a three-row PASS `result.tsv`. It requires all owned SDF and HDL
fixtures, SDF/VHDL revisions, five model families, six mixed-language
directions, interpreter/LLVM, cold/warm/relocated execution, all five artifact
forms, multiple roots, Linux/Windows contracts, 2,700 time advances, PASS and
clean exit. Child exit zero without the exact transcript cannot satisfy the
closure.

The frozen transcript includes
`models=standard-cell,primitive,state-table,memory,wrapper` and binds that
model set to the owned fixtures rather than external vendor libraries.

The Change 18 Debug run passed the complete 52-test SDF gate in 63.76 seconds.
The dedicated closure passed in 0.13 seconds and retained nonempty 2,885-byte
corpus, 4,382-byte negative and 1,876-byte portability logs. The indexed graph
contained 33,438 nodes and 154,153 edges.

## Resource and implementation review

- Runtime timing consumes immutable precomputed ticks and stable targets; it
  performs no SDF parse, rational conversion or hierarchy lookup on the event
  hot path.
- Every hierarchy, identity, value, callback, event, archive, report and corpus
  collection has a checked bound and rejects before partial publication.
- Disabled observation returns before scheduler lookup or allocation.
- Immutable schemas carry explicit version 1, stable logical identities and
  checksummed portable framing.
- Changes 1-19 use focused exact-LLVM Debug validation. Release build testing is not required except during the final batch checks.
- Formatting is limited to changed implementation/test sources. Header formatting changes that would induce long rebuilds are avoided.

## Final Change 20 evidence and handoff

Change 20's LLVM-disabled GCC ASan/UBSan clean build completed 888/888 in
29:39.40. Every host affected by the test-only phase-isolation, LLVM-off and
governed-limit repairs was rebuilt, then the definitive sanitizer regression
passed 204/204 in 2,141.52 seconds (35:41.55), with 4,372,608 KiB peak RSS and
no sanitizer finding. Leak detection is disabled on the managed runner;
quarantine and allocator history are bounded. The SDF closure explicitly runs
the isolated artifact-phase witness, so a zero-status aggregate coordinator
cannot hide loss of the older-standard artifact transcript.

The final eight-worker exact-LLVM Debug clean-first build passed 1,810/1,810
steps in 14:13.67 and the complete regression passed 205/205 in 981.31 seconds.
The required final Release clean-first rebuild passed 919/919 retained-
dependency steps in 11:47.29 and the complete regression passed 205/205 in
858.07 seconds. Both clean logs contain no warning or error. Static evidence is
2,461 diagnostics, 1,020 bounded authored sources, 1,205 Apache-2.0-owned files
and 379 test/control files. The refreshed graph has 33,521 nodes and 154,412
edges. The ledger remains 17/17 preserved, zero active, at SHA-256
`3e84f643e6df24090efb1161e0d6836847784268a139e3da3c2fda4568913c6f`.

Release testing ran only for these final checks. No header formatting churn was
introduced; the sole header change is the functional ASan bypass for the
otherwise incompatible finite governed-process address-space ceiling. The one
closeout commit and push carry this record. The handoff is valid only after
every non-documentation hosted-CI job for that pushed commit is inspected and
green. Batch 171 starts from that clean synchronized closeout and preserves
this audit, the zero-active ledger and the exact restart handoff in
`docs/v2-resume.md`.
