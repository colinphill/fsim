<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim

`fsim` is a greenfield mixed-language HDL simulator written in C++20. Its
intended compilation path is:

```text
VHDL / Verilog / SystemVerilog / SystemC
                    |
          typed, source-aware IR
                    |
       elaborated design + typed SimIR
              /                 \
 reference interpreter       LLVM ORC JIT
              \                 /
       deterministic runtime, debugger, VCD
```

## Project status

This repository is an **internal vertical slice**, not the fsim v1 release.
It establishes the semantic and platform spine on which the full language
implementations will be built.

The current tree contains:

- C++20 value kernels for packed 2-, 4-, and 9-state logic;
- a deterministic, single-thread, phased event scheduler;
- a typed SimIR and reference interpreter;
- hand-written VHDL-2008 and Verilog/SystemVerilog tokenizers and parsers for a
  deliberately small executable subset;
- bounded VHDL package declarations with declaration-ordered scalar
  integer/Boolean/bit constants, explicit `use library.package.all` or
  `use library.package.name` visibility, direct `package.constant` and
  `library.package.constant` expressions, acyclic package-to-package imports,
  and precise transitive package-source specialization provenance;
- bounded VHDL context declarations/references with recursive reusable
  library/use visibility, cycle diagnostics, and transitive context-source
  cache provenance;
- bounded SystemVerilog packages with immutable integral
  parameters/localparams, packed integral typedef aliases, and packed enum
  types/enumerators plus non-nested packed struct and equal-width packed union
  types with executable member, constant member-select, and constant
  `+:`/`-:` indexed-select reads/writes plus checked constant replication
  concatenations, signed/unsigned arithmetic shifts, complemented unary
  reductions, and both binary XNOR spellings; wildcard or selected imports;
  direct
  `package::constant`/`package::type` references; recursive dependency
  diagnostics; and precise specialization provenance;
- Verilog/SystemVerilog preprocessing with quoted/angle includes, manifest/CLI
  macros, object/function expansion with default arguments, multiline
  replacements, token concatenation/stringification, conditional compilation,
  source ancestry, `file`/`source-set`/`combined` state-sharing policies, and
  compilation-unit-wide cache provenance;
- bounded Verilog/SystemVerilog `` `timescale`` context, integer-delay scaling,
  and automatic selection of the finest declared directive precision;
- executable scalar `` `default_nettype`` implicit nets plus
  reset/cell/keyword-version/unconnected-drive compiler state, including
  cell specialization metadata and pull initialization for omitted inputs;
- bounded scalar `buf`/`not`/`and`/`nand`/`or`/`nor`/`xor`/`xnor` gate
  primitives with shared integer delays and comma-separated instances,
  lowered through the common continuous-process path;
- recursive VHDL/SV instance elaboration in both hierarchy directions with
  explicit cross-language bindings, whole-signal port aliasing, and boundary
  validation;
- specialization-time VHDL and SystemVerilog conditional/iterative/selection
  executable generate expansion, unguarded VHDL block statements, and
  explicit or implicit SystemVerilog generate forms with labeled and indexed
  generated scopes retained in mixed binding paths;
- lowering of scalar and common packed operations into SimIR;
- bounded source-level VHDL `wait for`/`wait on` and Verilog/SystemVerilog
  integer-delay, any-change, `posedge`, and `negedge` procedural event
  controls;
- deterministic simple-expression sensitivity inference for `always @*`,
  time-zero `always_comb`/`always_latch`, and dynamic `@*`;
- ordered Verilog/SystemVerilog exact `case`/`default` lowering with
  comma-separated choices and four-state `X`/`Z` matching;
- nested VHDL `if`/`elsif`/`else` with Boolean typing and nested
  Verilog/SystemVerilog `if`/`else` with packed four-state truth conversion;
- VHDL Boolean literals, equality/inequality, and `not`/`and`/`or`/`xor` plus
  `nand`/`nor`/`xnor` Boolean operations;
- ordered VHDL sequential packed `case` statements with `|` choices and
  `others`, lowered through common exact case-equality branches;
- bounded SystemVerilog conditional-expression lowering with exact
  four-state unknown-condition bit merging;
- vector-aware SystemVerilog logical negation and unsigned
  equality/relational comparisons with four-state unknown propagation, plus
  exact known-result `===`/`!==` comparison of `0`/`1`/`X`/`Z`;
- mixed-width SystemVerilog logical conjunction/disjunction with controlling
  known-value and four-state indeterminate semantics;
- SystemVerilog unary reductions (including `~&`, `~|`, `~^`, and `^~`),
  binary `~^`/`^~` XNOR, and mixed-width logical shifts, including four-state
  reduction rules and deterministic unknown/oversized shift handling;
- fixed-width signed and unsigned packed arithmetic/comparison for VHDL and
  SystemVerilog, including distinct VHDL `rem`/`mod`, SystemVerilog
  mixed-signedness rules, deterministic `X/Z`/zero-divisor behavior, and
  fixed-width signed-overflow wrapping;
- VHDL packed logical `sll`/`srl` and left-element-filling arithmetic `sra`
  shifts for locally static nonnegative counts;
- declared-range-aware SystemVerilog constant bit/part selects and packed
  concatenations, including ascending and non-zero-based source ranges;
- declared-range-aware VHDL indexed names/slices and correct width-summing
  VHDL `&` concatenation for packed scalar/vector operands;
- constant bit/part assignment targets for SystemVerilog and VHDL packed
  signals and procedural locals, including blocking, common-update, and
  delayed partial writes with stable source-order merging;
- locally static VHDL sequential `for` loops in either `to` or `downto`
  direction, including null ranges and loop-indexed packed selections,
  elaborated into deterministic source-order SimIR;
- bounded SystemVerilog procedural `for` loops with inline `int`/`integer`
  indices, canonical unit-step conditions/updates, exclusive or inclusive
  bounds, and the same deterministic common-loop lowering;
- a narrow LLVM ORC adapter for processes whose value-bearing operations are
  at most 64 bits, including explicit jumps/branches and caller-owned
  resumable frames for timed, dynamic-signal, and static-sensitivity waits,
  yields, design stop, loops containing suspension points, update-phase and
  delayed writes, and a shared checked allocation-free single-word `Logic4`
  `aval`/`bval` path into the simulation kernel; eligible processes owned by
  one bounded elaborated specialization are lowered and optimized together in
  one LLVM module while capability misses retain per-process fallback;
- a checksummed persistent object-cache primitive with process-aware per-key
  locking, atomic replacement, stale-lock recovery, and LLVM native-object
  reuse plus cold/warm activity telemetry beneath the configured application
  cache;
- buffered VCD output;
- a schema-1 project-manifest loader and command-line driver;
- an executable versioned C session API for build, hierarchy/value access,
  simulation control, and synchronous callbacks;
- versioned SystemC plug-in ABI, dynamic-library loading, typed factory
  construction, and foreign-child registration; and
- an fsim SystemC compatibility header plus a shell-free, cached host compiler
  for plug-in shared libraries, with build-time entry-point and factory
  validation; and
- executable facade-defined `SC_METHOD` processes with time-zero
  initialization, `dont_initialize()`, static any-change/edge sensitivity,
  dynamic time/event `next_trigger`, immediate/delta/timed named events with
  pending replacement/cancellation, strict `notify_delayed`, dynamic OR/AND
  event expressions, registered primitive-channel update callbacks,
  kernel-backed module-local `sc_signal` objects, canonical packed port reads,
  and common update-phase writes.

The implemented SystemC hierarchy spine is bidirectional: HDL instances may
bind to typed SystemC factories, and those factories may declare
elaboration-time foreign children explicitly bound to VHDL or SystemVerilog
targets. Either HDL or SystemC may be the selected top. The resulting ports,
aliases, HDL descendants, and stable SystemC instance metadata enter the common
elaborated design. Static `SC_METHOD` callbacks and deduplicated
`sc_prim_channel::request_update()` callbacks execute in that hierarchy;
module-local `sc_signal` objects use the common signal store and scheduler.
Typed `sc_in`/`sc_out`/`sc_inout` bindings to those signals alias one common
DesignIR object, including when HDL instantiates the SystemC module. Native
SystemC child members now elaborate recursively with stable hierarchy handles
and direct-parent signal or port binding. The four standard module lifecycle
callbacks run at deterministic build/start/terminal boundaries. Standard
`sc_signal_in_if`/`sc_signal_inout_if` exports retain hierarchy metadata while
resolving to common signals. `SC_THREAD` and `SC_CTHREAD` use single-threaded
Boost.Context fibers for timed, event, and static-sensitivity suspension.
General custom interfaces remain planned.
Facade modules declare VHDL/SV children with the typed
`fsim::systemc::hdl_instance` extension; the full child path in `fsim.toml`
selects the implementation, so mixed hierarchy remains explicit in both
directions. Bounded scalar VHDL generic and integral SystemVerilog parameter
actuals already transfer across explicit VHDL/SV bindings in either direction
before port widths are checked. `hdl_instance::set_actual` also transfers
immutable named scalar values from SystemC into a selected VHDL/SV child;
typed factory schemas also carry the reverse HDL-to-SystemC direction for the
bounded scalar subset. The common hierarchy walk evaluates source-language
actuals, exposes validated canonical values to constructors, and only then
checks parameter-dependent ports.

VHDL `if`/`else generate` and SystemVerilog `generate if` instance branches
are evaluated per specialization; selected block labels form stable hierarchy
components, including for explicit bindings to or from SystemC factories.
VHDL integer-range `for generate` and SystemVerilog loops using inline or
module-scope `genvar` declarations use deterministic `label[index]`
components and substitute the loop constant into child construction actuals
before specialization. Canonical assignment, prefix/postfix increment or
decrement, and compound-add/subtract updates normalize to the same HIR.
VHDL `case generate` supports scalar and inclusive locally static `to`/
`downto` range choices, while SystemVerilog generate-case supports scalar
constant choices. Both select default alternatives per specialization and
retain the alternative label in explicit mixed-language binding paths.
Selected generated bodies can execute local packed signals, concurrent
assignments, and processes. Their local names are scope-qualified for
debug/VCD visibility, including independent `label[index]` objects for each
realized loop iteration.
Bounded scalar/integral VHDL constants and SystemVerilog
parameters/localparams in generated bodies are evaluated in declaration order
after specialization and loop-index substitution, then folded before SimIR.
Unguarded labeled VHDL block statements always elaborate their body into the
declared block scope. SystemVerilog conditional/iterative/selection generates
may use their standard implicit module-item forms, while direct declarations and
behavior plus named static `begin : label` blocks inside explicit
`generate`/`endgenerate` regions elaborate through the same common hierarchy.

The full v1 language coverage described in
[Language support](docs/language-support.md) is not implemented yet. In
particular, complete semantic analysis, general mixed-boundary conversions and
multi-driver resolution, complete VHDL generic typing and SystemVerilog
parameter typing, complete scoped/local type coverage and call
safe points, broader interpreter/JIT differential coverage, remaining SystemC
kernel behavior, fractional-delay and declaration-based time semantics,
general VHDL package declarations/bodies and IEEE packages, complete HDL event
controls, and most testbench features remain work in progress. Unsupported
syntax is diagnosed rather than silently accepted.

## Requirements

- Windows or Linux on x86-64
- A C++20 compiler: GCC or Clang on Linux, or MSVC on Windows
- CMake 3.28 or newer
- LLVM **22.1.8** for the supported compiled-code configuration
- Boost.Context **1.91.0** for executable SystemC threads

LLVM is isolated behind one adapter. Frontend, interpreter, and most unit tests
can be developed without LLVM by configuring `FSIM_LLVM_MODE=OFF`. The
checked-in CMake configuration accepts only LLVM 22.1.8 when the backend is
enabled. The checked-in Linux and Windows LLVM CI jobs configure and run the
adapter tests against that exact version, including its C runtime-table header
test and O0/O2 ORC tests. Linux GCC and Windows MSVC are also exercised without
LLVM in Debug and Release configurations; separate Linux jobs run the suite
with ASan/UBSan and exercise the VHDL parser plus both Verilog/SV
preprocessor/parser entry points with Clang/libFuzzer. Adapter developers may
manually smoke-test an older LLVM while
bootstrapping, but that is not a supported project configuration and must not
be used to claim v1 compatibility.

The planned release dependency pins are CLI11 2.6.2, toml++ 3.4.0,
Boost.Context 1.91.0, and Catch2 3.15.2. CMake first uses an installed exact
Boost.Context 1.91.0 package and otherwise fetches Boost's official pinned
source archive with SHA-256 verification. Set
`FSIM_SYSTEMC_FIBER_MODE=OFF` only for a dependency-free build that
intentionally diagnoses `SC_THREAD`/`SC_CTHREAD` as non-executable.

## Build and test

On Linux:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

To point CMake at the supported LLVM installation:

```sh
cmake --preset dev -DFSIM_LLVM_MODE=ON \
  -DLLVM_DIR=/path/to/llvm-22.1.8/lib/cmake/llvm
cmake --build --preset dev
```

An equivalent configuration without a preset is:

```sh
cmake -S . -B build/llvm -DCMAKE_BUILD_TYPE=Debug \
  -DFSIM_LLVM_MODE=ON \
  -DLLVM_DIR=/path/to/llvm-22.1.8/lib/cmake/llvm
cmake --build build/llvm
ctest --test-dir build/llvm --output-on-failure
```

On Windows, install LLVM 22.1.8 for x86-64 and use the checked-in preset:

```powershell
cmake --preset windows-msvc -DLLVM_DIR=C:\llvm-22.1.8\lib\cmake\llvm
cmake --build --preset windows-msvc
ctest --test-dir build\windows-msvc -C Release --output-on-failure
```

The `ci-sanitizers` preset runs the non-LLVM suite with GCC ASan/UBSan. The
`ci-fuzz` preset additionally requires Clang and its libFuzzer/compiler-rt
development package; it recompiles an isolated instrumented copy of the
frontend, so fuzzers and ordinary tests may be enabled in the same build
without adding a libFuzzer entry point to normal executables.

## Command-line use

The primary interface is:

```text
fsim check
fsim build
fsim run
fsim debug
```

For example:

```sh
build/dev/fsim check -p examples/vertical_slice/fsim.toml
build/dev/fsim build -p examples/vertical_slice/fsim.toml
build/dev/fsim run   -p examples/vertical_slice/fsim.toml
build/dev/fsim debug -p examples/vertical_slice/fsim.toml
```

Direct source files are also accepted:

```sh
build/dev/fsim check --lang systemverilog examples/vertical_slice/tb.sv
```

For Verilog/SystemVerilog source sets, `compilation_unit = "file"` resets
macro and directive context for every listed file, `"source-set"` shares
ordered context across that source set, and `"combined"` shares context across
all combined source sets with the same language and standard. Combined sets
retain their declared libraries; their include directories and manifest
definitions are accumulated in source-set order. VHDL files remain independent
analysis units.

`--diagnostics=json` selects structured diagnostics. Manifest values can be
overridden with options such as `--top`, `--duration`, `--max-deltas`,
`--trace`, `--seed`, `-O`, and `-j`.

The current debugger supports relative or absolute time runs,
statement/process/delta/time stepping, source/time/signal-change breakpoints,
scope/signal navigation, value inspection, and deposit/force/release. With LLVM
enabled, `fsim debug` forces O0 compilation for eligible processes and retains
per-process interpreter fallback; builds without LLVM use the interpreter.
SimIR retains source-bearing statement, wait, assertion, process-entry, and
process-suspension points. O0 generated code always exposes those points, while
O2 tests one size-gated runtime flag so ordinary runs continue through them.
Signal breakpoints accept exact-state `==`/`!=` conditions. When a debug VCD is
configured, `trace add`, `trace remove`, `trace all`, `trace clear`, and
`trace list` change the live committed-value selection. Call instrumentation
and nested/scoped locals remain future work; bounded packed process variables
are shown by `locals` through interpreter or compiled frames. A design
`$finish` is terminal for that simulation; a debugger or Ctrl-C stop remains
resumable, while a fatal runtime error poisons the simulation and prevents
further execution. The CLI installs its SIGINT handler only for the active
run/debug command and restores the host's previous handler on exit.

The native C session API also exposes tested statement/process/delta/time
stepping and an asynchronous stop request that may be issued from a synchronous
safe-point callback. Executable safe-point callbacks carry the current process
handle. A terminal HDL stop takes precedence when it coincides with an external
step/stop request, so a finished design is never reported resumable.

With LLVM enabled, `fsim build` compiles eligible processes and `fsim run`
uses a hybrid engine. Processes whose supported value-bearing operations are
at most 64 bits execute through LLVM at the selected O0/O2 setting—O2 by
default—while typed capability misses fall back per process to the reference
evaluator under the same deterministic kernel. Generated callbacks and the
reference kernel share a checked
allocation-free single-word `Logic4` representation for values up to 64 bits,
including blocking, update-phase, and delayed writes. The plain-C runtime-table
ABI retains its v1 prefix and appends `write_update` and `write_after` fields;
generated code size-gates those fields per process before use. The configured
cache stores one native object per compiled specialization module under
`llvm-native`; `fsim build` reports compiled process/module counts and native
cache hits, misses, stores, and rejected entries. LLVM O0/O2 object identity
includes the specialization-module identity and ordered process keys. Each
module identity includes a provenance key for the exact owning-root and
ordered Verilog/SystemVerilog transitive-include bytes supplied to the
preprocessor/parser, source path, language standard, library, compilation-unit
mode, macro/include settings, separated VHDL entity-interface source,
bundled-library version marker, and represented generic/parameter values. Each
process key includes
scheduled-write kind and the exact delayed-write delay, plus wait kind,
ordered operands, widths, dynamic edges, and static sensitivity signal/edge
data. An unrelated,
uninstantiated source edit therefore retains the specialization's native
object, while even a comment-only edit to its owning source invalidates it.
`WaitOn` and `WaitSensitivity` use appended resume-status values while keeping
the v1 result layout and its existing status values unchanged. The result
identifies the boundary instruction; immutable SimIR retains the dynamic
signal/edge list and static edge rules for the kernel. Consequently,
any-change sensitivity-only signals may exceed 64 bits because no signal value
crosses the generated ABI; edge-qualified sensitivities require scalar
signals. Builds without LLVM execute entirely through the reference evaluator.
The bounded O0 debug path is differentially tested against the interpreter for
source breakpoints and statement/process/scheduler stepping.
Value-bearing operations wider than 64 bits, call instrumentation and complete
local-variable scope/type semantics, complete parameter/generic type and sizing
rules, reusable code-specialization deduplication, and broader differential
coverage remain work in progress.

The application suite also compares a bounded scheduled-write design exactly
between the interpreter and O2 hybrid engine. It checks an update commit at
tick 0, a delayed commit at tick 2, and the final value and scheduler
observations. The bounded differential harness also requires byte-identical
normalized VCD output, including declarations, initial values, timestamps, and
committed changes, across interpreter and O0/O2 hybrid execution. A separate
interpreter/compiled case schedules past
`UINT64_MAX`, verifies that the original scheduler overflow exception is
contained across the generated callback boundary and rethrown, and confirms
that the simulation is poisoned without publishing the write.

An additional exact application comparison runs the bounded
`always @(posedge trigger)` path through the interpreter and O2 hybrid engine.
Both processes compile in the hybrid run. The expected changes are the initial
trigger value at tick 0, the trigger edge at tick 1/delta 0, the observed
update at tick 1/delta 1, and the falling trigger at tick 2/delta 0. This is
evidence for the represented positive-edge form, not complete Verilog or
SystemVerilog event-control semantics.

The [vertical-slice example](examples/vertical_slice/README.md) is intentionally
small. It runs an SV testbench containing an explicitly bound VHDL counter and
an SV combinational child. The final committed values are `counter_q = 1` and
`child_y = FE`; those committed top-level signals are recorded in its VCD. The
LLVM-enabled application test also runs this bounded mixed hierarchy through
the reference and O2 hybrid engines and compares status, time, delta,
committed-change callbacks, and final values. Assertion metadata, normalized
VCD comparison, and bidirectional bounded VHDL/SystemVerilog
construction-actual/cache tests are also automated. Exhaustive semantic
fixtures and Windows execution evidence remain open.

## Design documents

- [Architecture](docs/architecture.md)
- [Implementation plan and progress](docs/implementation-plan.md)
- [Deterministic cross-language semantics](docs/cross-language-semantics.md)
- [Diagnostic code catalog](docs/diagnostics.md)
- [Language support and feature status](docs/language-support.md)
- [Feature matrix and test evidence](docs/feature-matrix.md)
- [SystemC subset and plug-in model](docs/systemc-subset.md)

## Licensing

fsim source code is licensed under the
[Apache License 2.0](LICENSE). Files use SPDX identifier
`Apache-2.0`. Third-party sources and dependencies retain their own licenses
and must pass license review before they are bundled. The planned IEEE VHDL
package sources are not yet bundled in this vertical slice.
