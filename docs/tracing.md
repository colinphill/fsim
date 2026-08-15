<!-- SPDX-License-Identifier: Apache-2.0 -->
# VCD and FST tracing

fsim records the same immutable declaration and event model through VCD,
FST, internal observers, debugger selection, and callbacks. VCD remains the
default for existing projects. FST is selected explicitly or inferred from a
`.fst` output path; choosing one format does not disable or weaken the other.

## Configure a project trace

Schema-2 manifests accept the complete trace policy in `[run]`:

```toml
[run]
trace_file = "waves.fst"
trace_format = "fst"
trace_compression = "deterministic"
trace_filters = ["top.*"]
trace_report_limit = 4096
```

`trace_format` is `auto`, `vcd`, or `fst`. `auto` selects FST only for a
case-insensitive `.fst` extension and otherwise preserves VCD. An explicit
VCD/FST choice rejects the opposite known extension. FST compression is
`none` or `deterministic`; `auto` resolves to the deterministic profile for
FST and no compression for VCD.

The same controls are available to project and manifest-free phases:

```sh
fsim run -p fsim.toml --trace waves.fst --trace-format fst \
  --trace-compression deterministic --trace-filter 'top.*'

fsim simulate --design design.fsimdesign --trace waves.vcd \
  --trace-format vcd --trace-filter 'top.*'
```

`--trace-select` aliases `--trace-filter`, `--trace-output` aliases
`--trace`, and `--trace-lifecycle configured|disabled` controls whether the
request is active. Compile, elaborate, simulate, CLI, Tcl, debugger, native C,
and C++ requests all pass through one transactional control model.

## Supported values and hierarchy

The FST writer preserves arbitrary-width two- and four-state packed values,
SystemVerilog `bit`, `logic`, `reg`, `time`, `realtime`, `shortreal`, real,
chandle, strings including embedded NUL bytes, enumerations, and VHDL
physical/time values. VHDL Logic9 retains all nine states. Aggregate, class,
container, coverage, assertion, and resolved-strength observations use stable
owner-qualified leaf declarations with exact type and shape metadata.

Canonical hierarchy includes multiple Verilog, SystemVerilog, VHDL, and
SystemC roots, logical libraries, source provenance, and forward aliases.
Events retain `(time, delta, region, sequence, stable-id)` order, including
multiple changes at one source time and late snapshots. VCD consumes the same
declarations and events while retaining its established byte contract.

## Selection and lifecycle

Run-mode tracing starts with the configured glob selection. Debugger and Tcl
sessions can change the committed-value selection with `trace add`,
`trace remove`, `trace all`, `trace clear`, and `trace list`. Enabling an
object late emits one snapshot followed by later changes; disabling it does
not rewrite already published history.

Tcl can configure and inspect a trace before simulation:

```tcl
fsim::trace configure waves.fst -format fst \
  -compression deterministic -select top.*
set status [fsim::trace status]
set report [fsim::trace report]
```

The lifecycle is `disabled`, `configured`, `open`, `complete`, or `failed`.
Flush and close are explicit public operations. The first terminal failure is
retained, a partial file is never reported complete, and staged publication
preserves an earlier destination on error.

## Determinism, artifacts, and relocation

The deterministic FST profile fixes schema and algorithm versions, GZip/zlib
headers, a 15-bit window, 65,535-byte blocks, 258-byte matches, and the
small-input threshold. It uses no host compression library. Equal inputs
therefore produce equal bytes across the supported Linux and Windows boundary;
the uncompressed profile is a distinct portable identity with the same
decoded semantic digest.

Trace format, compression, declaration identity, selection, lifecycle, output
intent, report, and semantic identity survive object, design, mapped-library,
native-cache, and checkpoint archives. Output paths are stored relative to the
producer root and restored beneath the consumer root, so source-hidden and
relocated replay does not retain producer host paths.

## Reader and failure contract

`fsim::runtime::read_fst` accepts bytes and `read_fst_file` streams a binary
filesystem path. `FstReaderLimits` bounds container, block, hierarchy,
declaration, timestamp, value, decoded-value, text, and metadata storage.
Every prefix truncation, corrupt checksum, invalid identity/order/value, stale
profile, unsupported block, trailing data, I/O error, or exceeded bound returns
one transactional `FSIM-FST-READ-001` through `FSIM-FST-READ-005` diagnostic
without a partial trace.

The reader is a clean-room Apache-2.0 implementation. GTKWave/libfst may be
used only as an optional external conformance oracle; its GPL-2.0 source is not
copied, translated, linked, or vendored.

## Executable evidence

The mixed SystemVerilog/SystemC/VHDL tutorial records either
[VCD or FST](../examples/three_language_hierarchy/README.md#4-record-and-inspect-vcd-or-fst).
The 28-obligation owned corpus covers both formats, both FST compression
profiles, every supported value family, aliases, mixed roots, selection,
callbacks, phases, all five archive kinds, relocation, a negative, time
advancement, PASS, and clean close. `fsim.fst-closure` retains separate corpus,
lifecycle, phase/artifact, and negative/portability logs; the zero-active
[Batch 171 release audit](v2-fst-release-audit.md) freezes the reviewed scope.
