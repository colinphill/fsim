<!-- SPDX-License-Identifier: Apache-2.0 -->
# Batch 171 FST tracing release audit

This audit freezes deterministic FST output for the complete existing
format-neutral trace model while preserving VCD, internal observation,
debugger selection, and callbacks.

## Reviewed scope

- `B171-C19-LEDGER`: all seventeen Changes 2-18 rows in
  `tests/feature_matrix/fst_inventory.tsv` are `preserved`; its normalized
  SHA-256 is
  `fa40e80a69015276a850de6f4f84355d93ec541c774a76003f2b1718d1eac248`.
- `B171-C19-FORMATS`: VCD remains the default and byte-preserved consumer of
  the immutable model. Explicit/inferred FST supports distinct stored and
  deterministic profiles without weakening internal trace behavior.
- `B171-C19-VALUES`: arbitrary-width bit/logic/reg/time/chandle, real,
  shortreal, realtime, string, enum, VHDL physical/time/Logic9, resolved
  strength, and owner-qualified aggregate/class/container/coverage/assertion
  leaves retain exact width, type, shape, state, metadata, and payload.
- `B171-C19-BOUNDARIES`: multiple Verilog, SystemVerilog, VHDL, and SystemC
  roots retain canonical hierarchy, libraries, provenance, forward aliases,
  and deterministic time/delta/region/sequence/stable-id order.
- `B171-C19-CONTROLS`: project, CLI, Tcl, debugger, native C, C++, and
  manifest-free compile/elaborate/simulate phases use one transactional format,
  compression, selection, lifecycle, status, and report contract.
- `B171-C19-ARTIFACTS`: object, design, mapped-library, native-cache, and
  checkpoint forms retain trace profile, declaration identity, selection,
  semantic identity, and relocation-safe output intent.
- `B171-C19-NEGATIVE`: every prefix truncation plus malformed, corrupt,
  trailing, duplicate, stale, ordering, typed-value, checksum, I/O, allocation,
  and governed-resource failure rejects transactionally with one stable
  diagnostic and no partial output.
- `B171-C19-DEFERRED`: no waveform GUI/database, parallel dump engine,
  Accellera SystemC signal/port inventory, or bundled/lib-linked GTKWave/libfst
  implementation is claimed. Those remain outside Batch 171.

## Executable closure

The Apache-2.0-owned 28-obligation corpus covers VCD, both FST profiles, every
supported value family, aliases, four HDL root kinds, selective and late
observation, callbacks, compile/elaborate/simulate, every archive kind,
relocation, a negative, exact time advancement, PASS, and clean close.
`fsim.fst-closure` retains four nonempty logs and rejects a child that exits
zero without the exact transcript.

The Change 18 standalone closure passed in 1.44 seconds. Its final focused
Debug slice passed 21/21 in 22.76 seconds, including
`fsim.application.typed_boundaries`. The MSVC test-string audit found a maximum
token or adjacent group of 14,622 source bytes, below the conservative 16,000-
byte threshold.

## Resource, portability, and implementation review

- The reader uses fixed-width bounds and streaming binary `std::filesystem`
  input; it has no platform-only I/O or zlib/libfst dependency.
- Every container, block, hierarchy, scope, declaration, timestamp, event,
  decoded value, text, metadata, selection, report, archive, and output buffer
  has a checked bound.
- Disabled tracing returns before declaration-model construction or writer
  allocation. Failed traces keep the first terminal diagnostic and never
  publish or report a partial file as complete.
- Deterministic compression fixes its schema, algorithm, header, window,
  block, match, and threshold identities without a host compression library.
- The clean-room writer, reader, fixtures, and documentation are Apache-2.0.
  GTKWave/libfst is an optional external oracle only; GPL-2.0 code is not
  copied, translated, linked, or vendored.
- Merged application regression preserves the historical `fsim.application`
  name as a cheap dispatcher sentinel. Dedicated selectable cases retain all
  coverage without rerunning the same eight application phases.
- Changes 1-19 use focused exact-LLVM Debug validation. Release build testing is not required except during the final batch checks.
- Formatting is limited to changed implementation/test sources. Header formatting changes that would induce long rebuilds are avoided.

## Frozen local evidence

Change 19 freezes 17/17 preserved FST rows and zero active obligations at the
digest above, 2,477 cataloged production diagnostics, 1,056 bounded authored
sources, 1,252 Apache-2.0-owned files, and 510 test/control files enforced by
`fsim.fst-release-audit`. The release audit composes the
FST inventory, diagnostic catalog, source budget, FST portability, and
application regression de-duplication gates. It intentionally does not run the
workflow-reading resource contract before Change 20.

The Change 19 dedicated application/audit slice passes 12/12 in 29.78 seconds;
the complete FST/docs/runtime slice passes 24/24 in 21.75 seconds, including
`typed_boundaries`. The mixed-language FST example checks three sources/five
design units and runs to PASS at tick 3. The 223-test CTest inventory has no
exact duplicate command group, and the retained `fsim.application` name is a
0.01-second sentinel instead of a second execution of eight dedicated phases.
The refreshed graph contains 34,907 nodes and 161,999 edges.

## Final Change 20 evidence and handoff

Change 20 records fresh clean-first exact-LLVM Debug and Release eight-worker
builds completing 1,886 and 957 steps warning-free in 13:41.44 and 11:49.39.
They peak at 6,978,100 and 2,258,620 KiB with zero swaps. The final post-fix
regressions select all 162 direct product executables and pass 162/162 in Debug
and Release in 177.90 and 173.45 seconds, peak at 3,789,540 and 3,788,452 KiB,
and report zero swaps. `typed_boundaries` passes in 23.03 and 22.49 seconds;
the repaired non-project and SystemC matrix cases pass and no root `phase.fst`
is recreated.

Under the current no-CI instruction, the final qualification excludes all 61
`/usr/bin/cmake` policy, audit, recursive-closure and workflow-contract tests
and does not use an earlier workflow-reading attempt as final evidence. No
sanitizer or hosted-CI action, inspection, restart or monitoring runs. Release
testing remains confined to this final batch check. The accumulated Batch 171
implementation is committed and pushed once, and the exact Batch 172 planned
restart checkpoint remains in `docs/v2-resume.md`.
