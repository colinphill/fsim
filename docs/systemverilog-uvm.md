<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog UVM foundation

Fsim executes a bounded object/factory/configuration/reporting foundation from
the unmodified Accellera UVM 1.2 and IEEE 1800.2-2020 kit version 2020.3.1
sources. This document describes the implemented Batch 159 boundary. It is not
a claim of complete UVM conformance.

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

## Implemented boundary

The current executable foundation includes:

- `uvm_object` construction, names, type identity, instance IDs,
  clone/copy/compare, print/record hooks, field automation, cycles, aliases,
  and rollback;
- `uvm_component` construction, multiple roots, parent/child hierarchy,
  lookup, duplicate rejection, and deterministic teardown;
- object, parameterized-object, and component utility registries with stable
  wrappers and create-by-type/name;
- factory type and instance overrides, instance-before-type precedence,
  bounded `*`/`?` paths, loop rejection, and debug traces;
- typed resources, priority, auditing, callbacks, spell checking, and
  `uvm_config_db` set/get/exists/wait-modified scope behavior;
- recognized factory/config/resource/verbosity/timeout UVM plusargs;
- report objects, handler precedence, severity/ID/verbosity/action routing,
  message elements, catchers, server accounting, max-quit, stdout and MCD/file
  sinks, and bounded packed formatting; and
- simulation-owned isolation across roots, sequential simulations, engines,
  caches, portable artifacts, and relocation.

The following remain explicit future work: UVM phases, objections, TLM,
sequences, sequencers, drivers, monitors, agents, scoreboards, the register
model, remaining policy classes, and complete UVM 1.2/2020 compatibility.
Those boundaries are assigned to Batches 160-162 in the v2 plan.

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
analysis peaks near 1.69 GiB for UVM 1.2 and 1.94 GiB for UVM 2020-3.1. The
full executable evidence used 4-GiB and 5-GiB ceilings, respectively.

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
produce identical object/factory/config/report results through:

- interpreter, compiled O0, compiled O2, and debug engines;
- two aliased roots sharing one simulation UVM context;
- application callbacks and exact deterministic report/output order;
- VCD and FST, including transitions to payload 7, configured value 11, and
  pass 1 in both roots; and
- isolated cold and warm native caches with identical transcripts.

The compact standalone simulations complete at tick 0, delta 0 and emit one
`FSIM-UVM-EXAMPLE-PASS payload=7 configured=11` line per root.

## Ownership and resource behavior

UVM services are members of the application simulation, not static globals.
Roots inside one simulation intentionally share factory, resource, config,
command-line, and reporting state. A new simulation in the same process receives
a clean UVM context. Object and component handles are generation checked; other
service handles are opaque and monotonic within their owner.

Every service has checked storage/work limits. Malformed names/profiles/values,
nominal type mismatches, stale or cross-owner handles, override loops, invalid
patterns, callback failures or re-entry, sink failures, and resource ceilings
reject transactionally. Failed operations do not partially publish heap,
hierarchy, factory use, resources, callbacks, routes, or report accounting.

## Evidence inventory

The primary evidence owners are:

- `fsim.uvm-source-harness` for offline governed metadata;
- `fsim.frontend` and `fsim.elaboration` for the unmodified language surface;
- `fsim.runtime` for object/component/registry/factory/resource/config/
  command-line/report positive and negative matrices;
- `fsim.application` and `fsim.llvm` for source execution, multiple contexts,
  callbacks, restart isolation, and engine parity;
- `fsim.library.artifact`, `fsim.artifact.object`, and
  `fsim.artifact.design` for portable codecs, relocation, and invalidation;
  and
- `fsim.diagnostics-catalog` and `fsim.source-line-budget` for public error and
  maintainability contracts.

Exact upstream identities are in [the provenance record](uvm-source-provenance.md),
feature-to-test mappings are in [the feature matrix](feature-matrix.md), and
the active checkpoint is in [the v2 restart handoff](v2-resume.md).
