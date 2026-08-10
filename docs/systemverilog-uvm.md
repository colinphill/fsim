<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog UVM execution boundary

Fsim executes a bounded UVM environment from
the unmodified Accellera UVM 1.2 and IEEE 1800.2-2020 kit version 2020.3.1
sources. This document describes the implemented boundary through Batch 162
Change 18; it is not a claim of complete UVM conformance.

For a tool-neutral project walkthrough using ordinary UVM registration,
configuration, phases, objections, reports, engines, caches, traces, and
portable artifacts, start with the
[producer-independent UVM tutorial](uvm-tutorial.md).

## Supported releases and source policy

Fsim does not vendor or patch either UVM library. The governed harness accepts
only the immutable archives, complete extracted trees, and exact entry points
listed in [the source-provenance record](uvm-source-provenance.md). Normal
configuration uses `FSIM_UVM_SOURCE_MODE=OFF` and performs no UVM network
access. `FETCH` is explicit; `ARCHIVE` consumes predownloaded archives. All
materialized sources and manifests remain outside authored-source inventories.

The reviewed entry point for either release is `src/uvm_pkg.sv`; ordinary user
code may include/import the corresponding `uvm_macros.svh` and `uvm_pkg` names.
No simulator compatibility define or source rewrite is required for the
implemented example.

Every governed source set selects its release explicitly:

```toml
[[source_set]]
language = "systemverilog"
standard = "2017"
uvm_release = "1.2" # or "2020.3.1"
files = ["uvm/src/uvm_pkg.sv", "tb.sv"]
```

Manifest-free commands accept the equivalent `--uvm-release` option. Fsim
normalizes aliases to `1.2` or `2020.3.1`, verifies the selected value against
the parsed `uvm_pkg` API surface, and rejects mixed source/object releases
before publishing any checked, built, cached, or portable state.

### Governed release difference matrix

| Compatibility contract | UVM 1.2 | UVM 2020-3.1 |
|---|---:|---:|
| Canonical version tuple | 1.2 | 2020.3 |
| Package-level test-done/stop/timeout controls | retained | removed |
| Deprecated sequence/sequencer registration macros | retained | removed |
| Component stop/kill/status methods | retained | removed |
| Sequence-library lookup/start methods | retained | removed |
| Component configuration compatibility methods | retained | retained |
| `uvm_test_done_objection` compatibility class | retained | retained |
| `uvm_policy`, `uvm_field_op`, and `uvm_copier` | absent | present |
| IEEE object field-operation/policy dispatch | absent | present |
| IEEE report-server summary methods | absent | present |

The public `SystemVerilogUvmCompatibility` record is the normalized dispatch
for this table. Release plus exact source identity is retained in `.fsimobj`
metadata and class payloads, checked/built projects, native cache keys,
`.fsimdesign` metadata, and schema-2 checkpoint provenance. Restore/replay
compares both fields and fails transactionally on either mismatch.

## Implemented boundary

The current executable foundation includes:

- `uvm_object` construction, names, type identity, instance IDs,
  clone/copy/compare, print/record hooks, field automation, cycles, aliases,
  and rollback;
- `uvm_component` construction, multiple roots, parent/child hierarchy,
  lookup, duplicate rejection, and deterministic teardown;
- object, parameterized-object, and component utility registries with stable
  wrappers and create-by-type/name;
- untouched UVM 1.2 legacy field/object/component/registry, sequence, callback,
  analysis-implementation, and report macros with checked generated methods,
  call signatures, factory publication, and portable metadata;
- exact UVM 1.2 phase, objection, TLM-port, sequence, callback, register,
  object/printer/comparer/packer/recorder, and command-line method profiles,
  including the default deprecated root/test-done/configuration/stop/timeout
  and sequence/sequencer aliases plus `UVM_NO_DEPRECATED` rejection;
- exact UVM 2020-3.1 policy, field-operation, copier, object, printer, comparer,
  packer, recorder, report-server, and version profiles, including retained
  component-configuration/test-done compatibility shims and explicitly absent
  global stop/timeout, component stop/kill, and sequence-library APIs;
- factory type and instance overrides, instance-before-type precedence,
  bounded `*`/`?` paths, loop rejection, stable print inventories, and bounded
  create/debug resolution traces with ordered override steps;
- typed resources, priority, auditing, callbacks, spell checking, and
  `uvm_config_db` set/get/exists/wait-modified scope behavior, including stable
  audited usage inventories and bounded lookup/read/write/set/get/exists
  traces;
- recognized factory/config/resource/verbosity/timeout/max-quit/objection-trace
  UVM plusargs, including `+UVM_RESOURCE_DB_TRACE`,
  `+UVM_CONFIG_DB_TRACE`, and source-ordered phase/time verbosity application;
- report objects, handler precedence, severity/ID/verbosity/action routing,
  message elements, catchers, server accounting, max-quit, stdout and MCD/file
  sinks, hierarchical command controls, and bounded packed formatting;
- exact common/runtime plus custom phase/domain graphs, component callbacks,
  synchronization, jumps, task-process suspension/cancellation, objections,
  drain time, ready-to-end quiescence, deterministic race/deadlock handling,
  and stable bounded objection tracing;
- typed TLM1 ports/exports/implementations, FIFO and transport operations,
  request/response and analysis fanout, plus typed TLM2 initiator/target/
  passthrough sockets, generic payloads, extensions, byte enables, DMI,
  blocking/debug/nonblocking transport, phases, timing, and cleanup;
- sequence items, sequences, sequencers, every standard arbitration mode,
  relevance, locks/grabs, responses, constraint-aware macros, driver pull/push
  handshakes, virtual sequences, coordinated reset, and process cancellation;
- active/passive agents, drivers, monitors, subscribers, and scoreboards with
  configuration, phase, objection, and analysis ownership;
- type-wide and instance callbacks plus engine-neutral transaction recording;
- register blocks, maps, registers, fields, multidimensional memories, every
  field access policy, desired/mirrored/reset state, byte enables, all endian
  modes, multiple maps, adapters, predictors, frontdoors, VPI/VHPI backdoors,
  standard sequences, callbacks, and coverage;
- immutable activity events, public debugger snapshots including
  `uvm configuration` factory/resource/config traces, DPI/VPI foreign
  snapshots and callbacks, and schema-2 portable UVM checkpoints; and
- simulation-owned isolation across roots, sequential simulations, engines,
  caches, portable artifacts, and relocation.

Batch 162 Change 13 adds one project-owned core smoke source compiled beside
each untouched governed package. Its derived `uvm_test` binds the standard
object printer/comparer/packer/recorder, factory, resource/configuration,
command-line, report-server, and callback types and executes eleven named smoke
methods for object policies, factory, resource/configuration, command-line,
reporting, callbacks, test selection, topology, timeout, and seed behavior.
The application requires every method body and standard base specialization
after direct build and after `.fsimobj`/`.fsimdesign` reload, then exercises the
corresponding simulation-owned behavior before emitting the exact
`core_smoke=governed/project/...` transcript marker.

Change 14 freezes the next project-owned flow layer after every direct or
portable load: all nine phase callback bodies, the component's TLM1 port/FIFO
members, sequence/sequencer/driver/monitor/agent/virtual-sequence/callback
inheritance, and fourteen common `uvm_tlm_generic_payload` profiles. The exact
matrix then executes phase/objection and cancellation behavior, sequence and
role services, TLM1/TLM2 transport, callbacks, transactions, multiple roots,
checkpoint replay, interpreter, LLVM O0/O2, cold/warm caches, and debug under
the `flow_smoke=phase/objection/.../cancellation` marker.

Change 15 freezes six project-owned register roles plus stable governed
`uvm_reg_field`, `uvm_reg_map`, and `uvm_mem` profiles after every direct,
object, or design load. The runtime smoke environment constructs register,
field, memory, little- and big-endian map state; applies sparse byte enables;
runs adapter/frontdoor/predictor and standard access-sequence paths; samples
callbacks and coverage; joins VPI and VHPI slices in one HDL backdoor; and
proves a DPI callback, wrong-width rollback, relocated checkpoint replay, and
one-record-short capture rejection. The exact serial runner carries the
`register_smoke=block/map/field/.../checkpoint/cap` marker through interpreter,
LLVM O0/O2, cold/warm cache, debug, multiple roots, and all fourteen traces.

Change 16 makes the exact matrix resource and platform contract executable.
The test process applies a 6 GiB `RLIMIT_AS` ceiling on POSIX and the matching
Windows Job Object process-memory limit before parsing any governed source.
Every direct, compile, elaborate, or simulate stage has a 1,200-second CMake
timeout, and each serial release matrix has the approved 7,200-second CTest
timeout. A fast contract freezes filesystem-neutral source paths, both release
registrations, MSVC-compatible test setup, `_WIN32` `__cdecl`, the append-only
x64 C structure sizes/offsets, and compile-checked callback assignment. The
exact transcript exposes this as `platform_contract=source/abi/linux/windows/
cdecl/filesystem limits=as6g/stage1200/matrix7200`.

Change 17 makes the complete supported-boundary inventory executable. The
authoritative tab-separated inventory has 18 rows: 16 shared families and one
release-specific family for each governed source, so exactly 17 apply to each
run. It assigns positive, negative, and execution evidence to every family and
marks every row `supported`; `xfail`, expected-failure, waiver, allowlist, and
suppression entries are forbidden. After every direct build or portable design
load, the application requires 53 UVM 1.2 or 56 UVM 2020-3.1 governed class
identities plus all 27 project-owned classes. A missing identity, duplicate,
unknown release, missing evidence owner, or changed count fails immediately.
The exact transcript freezes this as
`conformance_inventory=standard/project families=17 governed_classes=`
`release-exact project_classes=27 supported_gaps=0 suppression=none`.

## Minimal object/factory/config/report example

The governed execution matrix uses the same source against both releases. Its
essential shape is:

```systemverilog
package example_pkg;
  import uvm_pkg::uvm_object;
  import uvm_pkg::uvm_object_registry;
  import uvm_pkg::uvm_object_wrapper;

  class example_item extends uvm_object;
    typedef uvm_object_registry #(example_item, "example_item") type_id;
    int payload;

    function new(string name = "example_item");
      super.new(name);
    endfunction

    static function type_id get_type();
      return type_id::get();
    endfunction

    virtual function uvm_object_wrapper get_object_type();
      return type_id::get();
    endfunction

    virtual function uvm_object create(string name = "");
      example_item created = new(name);
      return created;
    endfunction
  endclass
endpackage

module example;
  import uvm_pkg::*;
  import example_pkg::*;
  int observed_payload, observed_configured;
  logic observed_pass;

  initial begin
    example_item source = example_item::type_id::create("source");
    uvm_object copied;
    uvm_report_object reporter = new("reporter");
    int configured;
    source.payload = 7;
    copied = source.clone();
    uvm_config_db #(int)::set(null, "*", "payload", 11);
    if (!uvm_config_db #(int)::get(null, "", "payload", configured))
      $fatal(1, "configuration lookup failed");
    observed_payload = source.payload;
    observed_configured = configured;
    observed_pass = observed_payload == 7 && observed_configured == 11;
    reporter.uvm_report_info(
        "FSIM_UVM", $sformatf("payload=%0d configured=%0d",
                               source.payload, configured), 100);
  end
endmodule
```

## Exact phase, TLM, sequence, and register example

The registered Batch 162 fixture imports the real `uvm_object`,
`uvm_component`, `uvm_phase`, parameterized blocking-put port, and TLM FIFO
types from either governed release. One derived component implements real
build, connect, end-of-elaboration, start-of-simulation, run, extract, check,
report, and final callbacks. Two aliased roots execute the same source. During
run, the left root raises one objection, suspends both phase processes, drops
the objection, and completes after a three-tick drain. Typed TLM payload `37`
crosses each root's port/FIFO path and produces result `42`.

The aggregate fixture then creates a two-root custom domain. A ready-to-end
callback raises and drops a new objection against a two-tick drain, producing a
deterministic completion at tick 5. A following phase deliberately suspends
without scheduler work and must reject with `FSIM-UVM-PHASE-008`, cancel every
process, and leave the phase and objection services quiescent.

The same exact source derives sequence item, sequence, sequencer, driver,
monitor, agent, scoreboard, environment, and callback classes. Its dedicated
environment proves strict-FIFO priority, lock/grab ownership, typed responses,
monitor-to-scoreboard analysis, a real virtual-sequence process tree, type and
instance callback order, transaction recording, and retained checkpoint caps.
It also derives register, block, adapter, predictor, register-sequence, and
register-callback classes. A second environment proves little/big-endian
multiple maps, byte enables, resets and mirrors, frontdoor adapters, TLM
prediction, combined VPI/VHPI backdoors, callbacks, coverage, the access
sequence, relocated checkpoint replay, and atomic record limits. The exact
transcript is:

```text
FSIM-UVM-PHASE-TLM-PASS phases=build/connect/eoe/sos/run/extract/check/report/final roots=left,right objection=1/0 drain=3 payload=37 result=42 source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008 sequence=arb/lock/response/virtual roles=agent/driver/monitor/scoreboard callback=6 transaction=5 cap=records register=frontdoor/backdoor/predictor maps=little/big byte_enable=1010 callback_coverage=1 sequence=access replay=relocated cap=records
```

## Direct execution

Assume `UVM_ROOT` names one validated extracted release root and `example.sv`
contains the source above:

```sh
build/llvm22-ninja-debug/fsim run \
  --lang systemverilog --standard 2017 \
  --compilation-unit source-set -j 8 \
  -I "$UVM_ROOT/src" --top example --engine interpreter \
  "$UVM_ROOT/src/uvm_pkg.sv" example.sv

build/llvm22-ninja-debug/fsim run \
  --lang systemverilog --standard 2017 \
  --compilation-unit source-set -j 8 \
  -I "$UVM_ROOT/src" --top example --engine compiled -O O2 \
  "$UVM_ROOT/src/uvm_pkg.sv" example.sv
```

Use explicit address-space controls in automation. The final clean package-only
analysis peaks near 1.69 GiB for UVM 1.2 and 1.94 GiB for UVM 2020-3.1. Exact
Batch 162 Change 13 direct runs peak at 4,299,260 and 4,861,476 KiB
respectively under 6-GiB ceilings. Focused portable compile peaks at
3,365,800/3,735,852 KiB, object-only O2 elaboration at
3,239,364/3,746,852 KiB, and loaded interpreter execution at
936,660/1,041,144 KiB, all with zero swaps. Exact measurements are recorded in
[the source-provenance record](uvm-source-provenance.md).

The complete Change 16 serial matrix enforces those controls internally rather
than relying on an outer shell. Its observed maximum RSS is 4,299,868 KiB for
UVM 1.2 and 4,861,176 KiB for UVM 2020-3.1; focused capped direct probes peak
at 4,299,428/4,861,616 KiB. All four records report zero swaps.

The Change 17 inventory matrix passes ten stages per release in 8:03.14 and
9:51.85, with observed maximum RSS of 4,299,292 and 4,861,336 KiB and zero
swaps. Focused direct inventory probes pass in 1:52.41/2:09.16 at
4,299,032/4,860,876 KiB. Each retained matrix log contains ten exact inventory
markers, and all fourteen traces remain 28,345 bytes with the governed digest
recorded in the source-provenance document.

The Change 18 closure matrix passes ten stages per release in 8:28.42 and
11:14.87, with observed maximum RSS of 4,299,288 and 4,860,288 KiB and zero
swaps. Each retained log contains ten closure markers; all fourteen traces
remain 28,345 bytes with the governed digest. The executable
[release-closure matrix](../tests/feature_matrix/uvm_release_closure.tsv) and
[closure audit](uvm-closure-audit.md) freeze compatibility, diagnostic,
source, complexity, resource, artifact, cache, and zero-gap evidence.

## Portable artifact flow

The same source may be separated into portable phases:

```sh
fsim compile --lang systemverilog --standard 2017 \
  --compilation-unit source-set -j 8 -I "$UVM_ROOT/src" \
  --output uvm-example.fsimobj \
  "$UVM_ROOT/src/uvm_pkg.sv" example.sv

fsim elaborate --object uvm-example.fsimobj \
  --top root=example -O O2 --output uvm-example.fsimdesign

fsim simulate --design uvm-example.fsimdesign \
  --engine compiled --cache uvm-cache --trace uvm-example.fst
```

An explicit root alias is required when publishing a design artifact. Repeat
`--object` and `--top alias=target` for multiple independent roots. Moving the
complete `.fsimdesign` directory does not change its transcript or native-cache
identity. Future, truncated, trailing, malformed, or incompatible object,
design, class, and HIR schemas reject before simulation.

## Engines, callbacks, traces, and caches

The reference interpreter defines behavior. LLVM O0/O2 and the debug engine
invoke the same simulation-owned typed services. The two governed releases
produce identical object/factory/config/report and phase/objection/TLM results
through:

- interpreter, compiled O0, compiled O2, and debug engines;
- two aliased roots sharing one simulation UVM context;
- application callbacks and exact deterministic report/output order;
- VCD and FST, including transitions to payload 7, configured value 11, and
  pass 1 in both roots; and
- isolated cold and warm native caches with identical transcripts.

The compact object/factory/config/report simulations complete at tick 0, delta
0 and emit one `FSIM-UVM-EXAMPLE-PASS payload=7 configured=11` line per root.
The aggregate fixture completes at tick 5. All fourteen direct/O0/O2 cold/
warm/debug traces from both releases are 17,833 bytes with SHA-256
`95caf4dbb04fa7f1b1397df9b40e03a1fdbc19b90c6387221a8b59635ebd5eff`.

## Ownership and resource behavior

UVM services are members of the application simulation, not static globals.
Roots inside one simulation intentionally share factory, resource, config,
command-line, reporting, phase, objection, TLM, activity, and foreign state. A
new simulation in the same process receives a clean UVM context. Object,
component, phase, process, endpoint, socket, sequence, handshake, role,
transaction, callback, register-model, and snapshot handles are generation
checked or opaque and monotonic within their owner.

Every service has checked storage/work limits. Malformed names/profiles/values,
nominal type mismatches, stale or cross-owner handles, override loops, invalid
patterns, callback failures or re-entry, sink failures, register rights or
layout errors, and resource ceilings reject transactionally. Failed operations
do not partially publish heap, hierarchy, factory use, resources, callbacks,
sequence queues, register mirrors, routes, or report accounting.

## Evidence inventory

The primary evidence owners are:

- `fsim.uvm-source-harness` for offline governed metadata;
- `fsim.frontend` and `fsim.elaboration` for the unmodified language surface;
- `fsim.runtime` for object/component/registry/factory/resource/config/
  command-line/report, phase/objection/TLM, sequence/role/callback/transaction,
  register, activity/foreign/checkpoint positive and negative matrices;
- `fsim.application` and `fsim.llvm` for source execution, multiple contexts,
  callbacks, restart isolation, phase/TLM execution, and engine parity;
- `fsim.library.artifact`, `fsim.artifact.object`, and
  `fsim.artifact.design` for portable codecs, relocation, and invalidation;
  and
- `fsim.diagnostics-catalog` and `fsim.source-line-budget` for public error and
  maintainability contracts; and
- `fsim.uvm-phase-tlm-matrix` plus the two
  `fsim.application.uvm_phase_tlm.*` tests for all 79 exact UVM diagnostics and
  the unmodified-release direct/artifact/cache/debug/race/deadlock/sequence/
  register matrix.

Exact upstream identities are in [the provenance record](uvm-source-provenance.md),
feature-to-test mappings are in [the feature matrix](feature-matrix.md), and
the active checkpoint is in [the v2 restart handoff](v2-resume.md).
