<!-- SPDX-License-Identifier: Apache-2.0 -->
# Producer-independent UVM tutorial

This tutorial runs one ordinary UVM test without depending on a vendor wrapper,
generated project, simulator-specific compatibility package, or patched UVM
source. The same project layout and commands apply to hand-written tests,
generated environments, and third-party verification IP that stays inside
fsim's [documented supported boundary](systemverilog-uvm.md).

Fsim currently governs two exact, unmodified source releases: UVM 1.2 and the
IEEE 1800.2-2020 kit version 2020.3.1. Choose one release for the complete
source set. Do not combine package sources, objects, designs, caches, or
checkpoints from different releases.

## 1. Prepare a governed UVM source tree

Set `UVM_ROOT` to a validated extracted release. The required package and macro
entry points are:

```text
$UVM_ROOT/src/uvm_pkg.sv
$UVM_ROOT/src/uvm_macros.svh
```

The repository's [source-provenance record](uvm-source-provenance.md) lists the
reviewed archives and full-tree identities. Fsim does not download sources
during a normal build or run, and it never rewrites the selected package.

Select the matching canonical release name:

```sh
UVM_RELEASE=1.2
# or: UVM_RELEASE=2020.3.1
```

## 2. Create a portable test

Save the following as `smoke_test.sv`. It uses standard object/component
registration, configuration, phases, objections, and reporting rather than a
producer-specific base class or command shim.

```systemverilog
`include "uvm_macros.svh"

package smoke_pkg;
  import uvm_pkg::*;

  class smoke_item extends uvm_object;
    rand int unsigned payload;
    `uvm_object_utils_begin(smoke_item)
      `uvm_field_int(payload, UVM_DEFAULT)
    `uvm_object_utils_end

    function new(string name = "smoke_item");
      super.new(name);
    endfunction
  endclass

  class smoke_test extends uvm_test;
    `uvm_component_utils(smoke_test)

    function new(string name = "smoke_test", uvm_component parent = null);
      super.new(name, parent);
    endfunction

    function void build_phase(uvm_phase phase);
      super.build_phase(phase);
      uvm_config_db #(int)::set(this, "", "expected_payload", 42);
    endfunction

    task run_phase(uvm_phase phase);
      smoke_item item;
      int expected;
      phase.raise_objection(this, "run producer-independent smoke test");
      item = smoke_item::type_id::create("item");
      if (!uvm_config_db #(int)::get(this, "", "expected_payload", expected))
        `uvm_fatal("SMOKE/CONFIG", "expected_payload was not configured")
      item.payload = expected;
      if (item.payload != 42)
        `uvm_error("SMOKE/VALUE", "unexpected payload")
      else
        `uvm_info("SMOKE/PASS", "payload=42", UVM_LOW)
      phase.drop_objection(this, "producer-independent smoke test complete");
    endtask
  endclass
endpackage

module smoke_top;
  import uvm_pkg::*;
  import smoke_pkg::*;
  initial run_test("smoke_test");
endmodule
```

Keep the UVM package before user sources in one SystemVerilog-2017 source
set. Include `$UVM_ROOT/src` so the macro include resolves to the selected kit.

## 3. Run through the reference engine

The interpreter is the behavioral reference. Use at least twelve frontend
workers for the governed package:

```sh
fsim compile --library work \
  --lang systemverilog --standard 2017 \
  --compilation-unit source-set --uvm-release "$UVM_RELEASE" -j 12 \
  -I "$UVM_ROOT/src" "$UVM_ROOT/src/uvm_pkg.sv" smoke_test.sv
fsim elaborate work.smoke_top --snapshot smoke
fsim simulate --snapshot smoke --engine interpreter
```

A successful run selects `smoke_test`, executes its phase callbacks, balances
the run-phase objection, and reports `SMOKE/PASS` with `payload=42`. Treat a
fatal report, a nonzero process status, or a missing completion marker as a
failed run.

## 4. Compare compiled and debug execution

Run the same source with the compiled engine. Optimization changes native code,
not UVM ownership or ordering:

```sh
fsim simulate --snapshot smoke --engine compiled -O O2 --trace smoke.fst
```

Repeat once with the same cache to exercise warm-cache loading. The test
selection, report order, payload, terminal time, and trace transitions must
match the interpreter run. Use `--engine debug` when debugger state or a VCD is
required; it calls the same simulation-owned UVM services.

## 5. Keep repeatable workspace commands

For automation, retain the ordered compile, elaborate, and simulate commands in
a shell or build-system script. Set `UVM_RELEASE` to `1.2` or `2020.3.1` and
select the matching source tree with `UVM_ROOT`. fsim verifies the release
against the parsed package API and retains the release and exact source
identity in the library and snapshot. It does not load `fsim.toml`.

## 6. Reuse compiled libraries and snapshots

The published `smoke` snapshot can be simulated without reparsing the UVM
package or retaining its source tree:

```sh
fsim library objects work
fsim elaborate root=work.smoke_top --snapshot regression -O O2
fsim simulate --snapshot regression --engine compiled --trace smoke.fst
```

Recompiling the source set replaces its managed definitions. Re-elaborating
`regression` replaces only that snapshot; `smoke` retains its previous behavior.
For relocation, preserve the complete workspace state. A snapshot or checkpoint
from another UVM release is not interchangeable: source/release mismatches
reject before state publication.

## 7. Add standard command-line controls

Pass ordinary UVM plusargs after the fsim options used by your invocation. The
implemented command-line service recognizes factory overrides, configuration
and resource settings, verbosity, timeout, max-quit, objection tracing, and
factory/config/resource trace switches. Useful diagnosis settings include:

```text
+UVM_VERBOSITY=UVM_HIGH
+UVM_CONFIG_DB_TRACE
+UVM_RESOURCE_DB_TRACE
+UVM_OBJECTION_TRACE
```

Fsim keeps argument order, applies deterministic duplicate policy, and isolates
the resulting state per simulation. Unknown or malformed recognized settings
fail with cataloged `FSIM-UVM-CLI-*` or `FSIM-UVM-CMD-*` diagnostics.

## 8. Keep automation bounded

Governed package analysis is intentionally memory bounded but is not small.
The retained Batch 175 Clang measurements peak at 4,631,912 KiB for the UVM 1.2
matrix and 5,220,824 KiB for the UVM 2020-3.1 direct witness, with zero swaps.
The project matrix applies a 6 GiB child-process address-space ceiling, a
1,200-second limit per stage, and a 7,200-second limit per release matrix. Apply
equivalent or stricter limits in CI, preserve logs, and avoid parallelizing two
full release matrices on a host that cannot provide their combined resident
memory.

For a reliable pass, require all of the following:

- the process exits successfully;
- simulated time advances as expected for the test;
- the intended UVM pass/report marker is present;
- no fatal, assertion, timeout, or resource diagnostic appears; and
- requested VCD/FST files exist and contain the expected transitions.

## 9. Diagnose common failures

| Symptom | Check |
|---|---|
| Mixed-release or API-surface diagnostic | Make `--uvm-release`, `uvm_pkg.sv`, macro include, library, snapshot, cache, and checkpoint originate from one release |
| Macro is undefined | Put `$UVM_ROOT/src` on the include path and compile `uvm_pkg.sv` before the user source in the same source set |
| Test is not selected | Verify registration, the `run_test` name, and any `+UVM_TESTNAME` setting |
| Configuration lookup misses | Enable `+UVM_CONFIG_DB_TRACE` and inspect the component-relative scope and nominal value type |
| Run phase does not finish | Balance objections, inspect objection tracing, and check for suspended sequence/phase work without scheduler progress |
| Stale or cross-owner handle | Do not retain service handles across simulation teardown or use a handle created by another simulation |
| Memory or work ceiling | Reduce concurrent governed jobs, retain the exact diagnostic, and compare with the published resource baseline before changing a bound |

The [UVM guide](systemverilog-uvm.md) gives the precise implemented boundary,
the [closure audit](uvm-closure-audit.md) maps every supported family to
evidence, and the [diagnostic catalog](diagnostics.md) defines stable failure
codes. Unsupported behavior should fail explicitly; it must not be converted
to a waiver or assumed supported from parser acceptance alone.

This producer-independent flow is also one of the 14 witnesses in the
[SystemVerilog-2017 release audit](v1-systemverilog-release-audit.md). That
larger matrix composes UVM with direct/interpreter/LLVM/cache/debug/trace,
artifact, relocation, replay, checkpoint, multiple-root, mixed-language, and
public-API stages. Its process-memory, delta, trace, and timeout values govern
the retained evidence run; they are not SystemVerilog or UVM legality limits.
