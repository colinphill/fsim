<!-- SPDX-License-Identifier: Apache-2.0 -->
# Lowering class functions and constructors to SimIR

Owner decision (2026-10-12): class functions and constructors are lowered
through the HIR lowerer, as class tasks already are, instead of extending the
runtime class-HIR interpreter.

## Current state

- Class tasks called from a process are lowered as deferred SimIR
  subroutines (`lower_hir_class_task_call`, `lower_hir_class_task_invocation`
  in `src/elaboration/lowerer_hir_callable.cpp`). The frame holds a
  `class_receiver` register; inside the body, properties are read and written
  with `ClassPropertyRead`/`ClassPropertyWrite` on that receiver.
- Class functions are lowered to a `ClassMethodCall` operation. The runtime
  then runs the method on the class-HIR interpreter
  (`src/app/application_class_hir_execution.cpp`), and constructors run the
  same way (`invoke_source_constructor`).
- The interpreter evaluates packed values, strings and class handles only. It
  has no value containers (`int v[]`, queues, arrays of structures as
  properties or locals) and skips several statement kinds. This blocks about
  30 to 50 corpus cases and UVM code.
- Class properties are stored in `SystemVerilogClassHeap`
  (`include/fsim/runtime/class_heap.hpp`) as packed values, strings, handles,
  or containers of handles. There is no storage for value containers.

## Steps

1. **Class functions.** Generalize the task invocation into a class callable
   invocation with an optional result (packed or string). Lower a non-static
   class function call through it. Virtual methods (8.20) are dispatched by
   testing the receiver against each overriding class, most derived first.
   Calls the lowering cannot express fall back to `ClassMethodCall`. If a
   process fails to lower after it lowered a class function body, it is
   lowered again with class-function lowering off, so the interpreter
   remains the fallback.
2. **Static class functions.** Lower them the same way without a receiver.
3. **Constructors.** Split allocation (the object with its property
   defaults) from the constructor body. Lower `new` as allocation followed by
   a lowered constructor invocation, including `super.new(...)` chains.
4. **Value-container properties.** Store a `ContainerValue` per value-container
   property, described by the `ContainerType` built in class inspection. Add
   operations that copy such a property into and out of a container register.
   Lowered bodies then use the existing container operations, for example
   `new[n]`, `size`, indexing, `push_back` and `foreach`. Module code can
   reach `obj.v` the same way.
5. **Randomization of container properties** (arrays of random elements and
   structure members), on top of step 4 and the interval solver.

The interpreter stays for callers that are not lowered processes: UVM
services, randomize callbacks, property initializers and the debugger.

Each step is checked with the labelled ctest, the fast set and a targeted
corpus rerun.

## Status (batch 50)

- Steps 1 to 3 are done for non-parameterized classes outside the UVM
  library. Class functions (instance, virtual, `super.`, static, void,
  string results) and constructors run as lowered subroutines. A process
  that cannot lower one of their bodies, or that reports errors while doing
  so, is lowered again with the runtime interpreter. Calls from
  `$display` arguments, conditions and the like lower the same way.
- Step 4 is done for containers of packed and real values:
  - A function or constructor body caches the object's container
    properties in container registers. It loads them at entry and stores
    them before nested calls (reloading after) and at exit.
  - Module code reads and writes `obj.v` (index, `size`, `push_back`,
    `pop_front`, `new[n]`, `new[n](init)`, whole assignment, `foreach`)
    through `ClassPropertyContainerRead`/`Write`.
  - Processes using these operations run on the interpreter under the
    LLVM engine (the JIT rejects the operations).
- Classes that extend the UVM library keep the runtime path for their
  methods and constructors, which use the runtime UVM services.
- Open:
  - tasks (which can suspend) reading container properties;
  - aggregate (structure) elements from module code (`obj.cfg[i].a`);
  - parameterized classes and UVM-derived constructors;
  - static container properties;
  - randomization of container properties (step 5);
  - native JIT support for the container property operations.
