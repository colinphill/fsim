<!-- SPDX-License-Identifier: Apache-2.0 -->
# Standard Delay Format support

Fsim accepts SDF 4.0 directly and SDF 2.1 and 3.0 through explicit revision
adapters. The current v2 implementation preserves exact normalization,
hierarchy resolution and portable source identity, applies that immutable
representation to Verilog-1995/2001/2001-noconfig/2005 and
SystemVerilog-2005/2009/2012/2017 timing, and covers VHDL/VITAL targets plus
timing that crosses a VHDL boundary without widening the selected SDF or HDL
revision.

## Supported input and value policy

The clean-room frontend preserves ordered header values, exact signed decimal
and partial `min:typ:max` values, timescales, conditions, delay families, timing
checks, timing-environment constructs and empty, exact or wildcard instance
selectors. Revision adapters reject constructs, spellings, arities and ordering
that do not belong to the selected profile.

An application selects `min`, `typ` or `max`, converts the exact rational value
through the SDF and design timescales and rounds once at the simulator-precision
boundary. Missing selected slots, negative effective delays, arithmetic
expansion and tick overflow fail transactionally. No floating-point or host-
locale conversion participates in annotation identity.

## Verilog and SystemVerilog timing application

Before publication, each resolved mapping becomes an immutable target plan
keyed by stable elaborated timing-object identities. Validation covers cell and
object kind, delay/check arity, duplicate ownership, widths and source spans.
The supported application surface is:

- absolute and incremental `IOPATH`, including parallel/full paths, polarity,
  edges, conditions, `CONDELSE` and state dependence;
- `INTERCONNECT`, `PORT`, `MIPD` and `DEVICE` delays for nets, ports,
  primitives, UDPs, gates, switches and continuous assignments;
- governed one-, two-, three-, six- and twelve-value transition lists;
- `$setup`, `$hold`, `$setuphold`, `$recovery`, `$removal`, `$recrem`, `$skew`,
  `$timeskew`, `$fullskew`, `$width`, `$period` and `$nochange` limits;
- edge and conditional timing-check expressions, notifiers, negative checks,
  `PATHPULSE`, percentage pulse controls and `RETAIN`; and
- deterministic source/SDF precedence, multiple-file ordering and rollback-
  safe reannotation.

Interpreter and LLVM execution use the same precomputed effective timing
records. Inertial and transport cancellation, same-time scheduler regions,
strength/resolution state, switches, continuous drivers and force/release
interactions therefore do not perform hierarchy lookup or value conversion on
the event hot path. Disabling SDF preserves the original no-annotation timing
identity and does not allocate observation records.

## VHDL/VITAL and mixed-language timing application

The same exact target/value pipeline covers VHDL 87, 93, 2000, 2002 and 2008
VITAL cells and paths crossing VHDL-Verilog,
VHDL-SystemVerilog and VHDL-SystemC boundaries in either direction. Structural
call, port, generic, process and wrapper governance identifies standard cells,
primitives, state tables and memory paths; model names are not guessed.

VITAL delay and timing-check records retain source and effective values,
min/typ/max selection, pending-transaction policy, timing-history policy,
generation, root/library/path identity and provenance. Logic9 values, vector
width/direction, resolved drivers, SystemVerilog scheduler regions and SystemC
time/delta coordinates remain exact at mixed boundaries. Absolute/incremental
precedence and safe-point reannotation either publish the complete new
generation or retain the prior one.

VHPI and VPI enumerate stable read-only timing objects and bounded callbacks.
Debugger, callbacks, internal trace and VCD share deterministic effective,
pending-transaction and violation events. Disabled observation returns before
object lookup and cannot perturb scheduling.

## Command-line control

SDF options are accepted only by phases that elaborate or simulate. `--sdf` is
repeatable; its order establishes file precedence. `--sdf-root` selects the
bound design root, `--sdf-cell` applies a cell glob, `--delay-mode` selects
`min`, `typ` or `max`, and `--sdf-report-limit` bounds detailed report entries.
For example:

```sh
fsim run --project fsim.toml --sdf cells.sdf --sdf-root tb \
  --sdf-cell 'tb.dut.*' --delay-mode max --sdf-report-limit 256
```

The same controls are available to project `build`, `debug` and `tcl`, and to
manifest-free `elaborate` and `simulate`. Selector/report options without an
SDF input and SDF options on compile-only phases are rejected.

## Tcl control

`fsim::sdf configure SOURCE ROOT CELL_GLOB min|typ|max REPORT_LIMIT` appends one
input and atomically publishes the resulting control request. `fsim::sdf
summary` returns the effective input/file/path/check counts, generation,
truncation state and semantic identity. `fsim::sdf report` returns the bounded
input/path/timing-check detail list. Summary and report reads are safe inside a
simulation callback; mutation remains phase checked.

```tcl
set summary [fsim::sdf configure cells.sdf tb {tb.dut.*} max 256]
puts "SDF generation [dict get $summary generation]"
foreach entry [fsim::sdf report] {
  puts "[dict get $entry kind] [dict get $entry object]"
}
```

Failed duplicate, phase, limit or resource validation retains the last
published request and reports a cataloged `FSIM-SDF-CONTROL-*` diagnostic.

## Native C and C++ APIs

The installed C API in `fsim/api.h` adds append-only
`fsim_sdf_input_t`, `fsim_sdf_options_t`, `fsim_sdf_summary_t` and
`fsim_sdf_report_entry_t` structures. Configure with
`fsim_session_configure_sdf`, read the immutable summary with
`fsim_session_get_sdf_summary`, and enumerate bounded report entries with
`fsim_session_get_sdf_report_entry`. Callers advertise every structure prefix
with `struct_size` and `FSIM_API_VERSION`; invalid prefixes, enums, views,
indices and phase transitions fail without replacing prior state.

The source-tree C++ surface is split by ownership:

- `fsim/frontend/sdf.hpp` parses and normalizes exact SDF input;
- the `sdf_*resolution.hpp`, `sdf_mapping_validation.hpp` and
  `sdf_target_plan.hpp` application headers bind stable targets;
- the value, path, interconnect, delay-mode, timing-check, condition, pulse,
  precedence, scheduling, drive and reannotation headers own effective timing;
- `sdf_control.hpp` owns CLI/Tcl/C/C++ control summaries and reports;
- `sdf_effective_archive.hpp` owns schema-1 `FSDFEFF` object, design, library,
  native-cache and checkpoint payloads; and
- `sdf_observability.hpp` exposes stable debugger, callback, internal-trace,
  VPI and VCD timing/violation records;
- `sdf_vital_*` and `sdf_mixed_*` headers own structural VITAL models,
  scheduling/checks, mixed boundaries, foreign interfaces and observation;
- `sdf_vital_archive.hpp` retains effective VITAL/mixed state in every portable
  form; and
- `sdf_vital_phases.hpp` gives project, CLI, Tcl, C, C++ and non-project
  phases equivalent summaries, archive identity and failures.

Every public result reports success only when it has no error diagnostic.
Callers can tighten default resource and report limits but cannot widen a
target or accept a partial mutation.

## Persistence and observability

The existing `FSDFPORT` payload retains normalized and resolved annotation
identity. The `FSDFEFF` payload adds exact source and effective values, target
identities, selected policy and provenance. Objects, designs, mapped libraries,
cold/warm/relocated native caches and checkpoints reject bad magic, unsupported
schema, truncation, trailing bytes, corruption and expected-identity mismatch
before publication. A successful relocated reload does not consult the
original SDF path.

Effective paths, timing checks and violations have stable debugger IDs, VPI
handles, VCD names, values, source spans, time, delta, scheduler region and
source order. Debugger, callbacks, internal trace, VPI and VCD observe the same
preallocated records without changing scheduling.

## Governed evidence

The seventeen-row
[`sdf_application_inventory.tsv`](../tests/feature_matrix/sdf_application_inventory.tsv)
maps Changes 2-18 one-to-one and is checked by
`fsim.sdf-application-inventory`. The `fsim.sdf-application-closure` test runs
seven retained-log stages and requires the exact
`FSIM-SDF-APPLICATION-CORPUS-PASS` transcript tokens independently of child
exit status. Its owned standard-cell, primitive, interconnect, pulse and timing-
check corpus covers interpreter/LLVM, optimized/debug, project/non-project,
object/design/library/cache/checkpoint, relocation/replay, public observation,
Linux/Windows contracts and cataloged negative/resource families.

See the [diagnostic catalog](diagnostics.md#standard-delay-format-frontend),
[feature matrix](feature-matrix.md) and
[Batch 169 application audit](v2-sdf-application-release-audit.md),
[Batch 170 SDF/VITAL audit](v2-sdf-vital-release-audit.md) and
[mixed VITAL example](../examples/sdf_vital_mixed/README.md) for the frozen
boundaries and executable evidence.
