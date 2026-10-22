<!-- SPDX-License-Identifier: Apache-2.0 -->
# Running UVM tests on the native UVM services

Batch 86 (2026-10-22). This work unblocks the parked UVM sv-tests: the
`*-uvm.sv` cases of chapter 16, `testbenches/uvm_*` and
`generated/uvm_classes_*`. The UVM library is compiled once into a separate
library (`--search-library uvm`). fsim does not interpret the library's
`uvm_root`. Instead it runs tests on the native services in
`src/runtime/uvm_*.cpp`, which include the factory, components, phases,
objections, reports, config_db and resources.

## Current state

- **Library classes are invisible to the frontend.** The frontend class
  resolver sees only the classes being compiled, so a call such as
  `uvm_config_db#(int)::set(...)` or
  `uvm_resource_db#(virtual mif)::read_by_name(...)` reaches elaboration
  unresolved:
  - a statement keeps the plain spelling, with no `@sv-static-task:`;
  - an expression has an empty `class_member_identity`.

  The same gap gives "no compatible method" for a method inherited from a
  library base (`port.connect`, FSIM-SV-CLASS-013).
- **The native intrinsics** (`@uvm-config-db-*`, `@uvm-registry-create:`)
  are produced only when UVM is in the same compilation.
- **Components register** when constructed (`construct_class`). Phase
  adapters exist (`execute_uvm_function_phase` and
  `execute_uvm_task_phase`), but:
  - nothing calls them;
  - they pass a null phase handle;
  - they run `run_phase` synchronously on the class interpreter, so it cannot
    wait on `@(posedge vif.clk)`.
- **Objections** are not bridged. The test runner needs a test name.

## Steps

1. **Library class calls.** At elaboration, and in the class interpreter,
   resolve a call spelled `C#(actuals)::m` or `C::m`:
   - split off the member;
   - resolve `C` through the caller scope's imports;
   - resolve each type actual (including `virtual I`);
   - specialize `C`, and select the method.

   This is a shared semantic helper, so the artifacts do not change. For
   `uvm_config_db` and `uvm_resource_db`, the call is mapped to the native
   intrinsics, typed by the specialization's `T`. Virtual-interface values
   are stored as 64-bit handles.
2. **Inherited library methods.** A method of a library base class that is
   called through a receiver (`port.connect`, `phase.raise_objection`) is
   deferred to elaboration instead of being rejected at compile time.
3. **`run_test`.** Lowered as a host call that does the following, in order:
   1. picks or creates the root;
   2. creates the named test through the factory, or uses the components
      that already exist when no name is given (common in sv-tests);
   3. runs the function phases build, connect, end_of_elaboration and
      start_of_simulation, passing each a real phase object;
   4. runs `run_phase` of every component as a forked simulator process, by
      virtual dispatch from a lowered loop;
   5. waits until all run-phase objections drop;
   6. runs extract, check, report and final;
   7. prints the report summary and calls `$finish`.
4. **Phase objects and objections.**
   - A phase class handle maps to a `SystemVerilogUvmPhaseHandle`.
   - `phase.raise_objection` and `phase.drop_objection` (and
     `set_drain_time`) call `uvm_objections`.
5. **Reports.** `uvm_report_{warning,error,fatal}` and the package-level
   `uvm_report_*` functions go to the native report server, with the
   end-of-test summary.
6. **TLM.** The analysis port and the sequencer/driver handshake used by the
   testbenches go to the native TLM and sequence services.

Each step is checked with the labelled ctest, the fast set and a targeted
rerun of the parked UVM cases (runner without `--parked`). The parked list is
rebuilt from each full run.

## Outcome

All steps landed in Batch 86:
- **SourceEnvironment** binds library class calls and
  `X::type_id::create` (`@uvm-registry-create:`). It maps the package
  report functions to `@uvm-report-*`.
- **`run_test`** expands to a SimIR template with these parts:
  - the function phases are host-driven loops, and each component's
    callback runs by lowered virtual dispatch;
  - `run_phase` is forked per component in a `fork automatic` loop;
  - a wait on the class heap lasts until the run phase ends;
  - the remaining function phases follow, then the summary and `$finish`.
- **Fork declarations.** Automatic declarations of a fork outside a
  callable live in a frame of their own, so each loop iteration's children
  keep their own values (IEEE 1800-2017 9.3.2).
- **Analysis ports.** `write` fans out to the `write` of every connected
  imp's owner, in the class the owner was specialized for.
- **Sequence items.** A natively created driver or sequencer gets the ports
  its library constructor would make.
  - `seq.start(sqr)` runs the user's `pre_start`..`post_start` and `body`.
  - `finish_item` queues the item, then waits until a driver takes it.
  - `get` and `get_next_item` wait for an item; `item_done` completes it.
- **Type parameters.** A class-typed variable whose type is a type parameter
  (`T rec; rec.data`) binds to the parameter's default class. Elaboration
  re-selects the member by name in each specialization.

