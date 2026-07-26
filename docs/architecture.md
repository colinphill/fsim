<!-- SPDX-License-Identifier: Apache-2.0 -->
# Architecture

## Status and invariants

This document records both the v1 architecture and the smaller implementation
present in this repository. “Current” means code exists in the vertical slice;
“v1 target” means the interface or semantic rule is intentional but its full
implementation is not complete.

The architectural invariants are:

- simulation is deterministic and single-threaded;
- parsing and native compilation may run concurrently, but simulation may not;
- language frontends never depend on a third-party HDL parser;
- LLVM is hidden behind a narrow adapter and no LLVM type crosses that target;
- generated code calls a versioned plain-C runtime table;
- the SimIR interpreter is the semantic reference for differential tests; and
- source spans and stable identifiers survive every lowering stage.

## Compilation pipeline

| Stage | Responsibility | Vertical-slice status |
|---|---|---|
| Source manager | Files, source locations, include and macro ancestry | Basic files and source spans are current; expansion ancestry is planned |
| Language frontend | Tokenization, preprocessing, parsing, name/type rules | Hand-written minimal VHDL and SV parsers are current; typed semantic HIR is partial |
| Design elaboration | Specialization, hierarchy, bindings, drivers, stable IDs | Recursive simple VHDL/SV hierarchy, explicit mixed bindings, port aliasing, and boundary checks are current; specialization and complete driver semantics are planned |
| SimIR lowering | Explicit reads, writes, waits, branches, assertions and yields | A typed executable subset is current |
| Reference engine | Execute any supported SimIR with deterministic scheduling | Current |
| LLVM engine | Compile each design-unit specialization and execute via ORC | The application compiles eligible at-most-64-bit processes as separate LLVM modules and uses typed per-process interpreter fallback; scheduled writes, sensitivity waits, and per-design-unit-specialization module grouping remain planned |
| Runtime | Time, deltas, resolution, callbacks, force/deposit and diagnostics | Scheduler, value changes, deposit, and a force/release mask are current; full driver/resolution model is planned |
| Visibility | C API, debugger safe points and VCD | Executable session API, VCD, and a scope/signal-oriented REPL with time/signal breakpoints are current; source/statement/process debugging and locals are planned |

The language-specific HIR will retain resolved symbols, types, overload choices,
constant values, and legality results. The common `DesignIR` will own dense
stable IDs for libraries, units, specializations, scopes, instances, processes,
signals, ports, drivers, source locations, and debug-visible objects. These
layers are still compacted together in parts of the current slice.

The current hierarchy builder recursively follows simple VHDL or SV instance
nodes. Same-language children resolve within the parsed units. A manifest
binding may override an instance with a language-qualified VHDL or SV target;
the builder then connects named or positional whole-signal actuals by aliasing
the child port ID to the parent signal ID. It diagnoses missing or duplicate
connections, width and signedness mismatches, implicit loss into a 2-state
destination, recursive hierarchy, unused bindings, and unresolved multiple
boundary drivers. Generic/parameter specialization, expression actuals,
unpacked/record boundaries, SystemC factories, and actual multi-driver
resolution remain outside this slice.

## Runtime values

The runtime distinguishes three logic domains:

- `Bit2`: `0` and `1`;
- `Logic4`: `0`, `1`, `X`, and `Z`; and
- `Logic9`: VHDL `U`, `X`, `0`, `1`, `Z`, `W`, `L`, `H`, and `-`.

Packed storage is used throughout. Scalar and common vectors up to 64 bits are
the fast path; wide values will use specialized runtime kernels. Conversion to
a lower-state domain must be explicit whenever information could be lost.
`PackedLogic4` stores common values as inline `aval`/`bval` planes and exposes a
checked `Logic4Word` representation for widths up to 64 bits. The simulation
kernel's external-executor boundary and generated-code callbacks share this
allocation-free word path, while preserving width validation at the boundary.

Simulation time is an unsigned 64-bit tick count at one elaborated global
resolution. The v1 elaborator will select the finest declared VHDL, SV, or
SystemC precision when the manifest says `auto`. It will apply SV
`timeprecision` rounding before converting to ticks, require VHDL and SystemC
delays to be exactly representable, and diagnose overflow before an event is
scheduled. The current slice accepts Verilog/SystemVerilog `` `timescale``
directives whose unit and precision magnitudes are `1`, `10`, or `100` and
whose units are `fs`, `ps`, `ns`, `us`, `ms`, or `s`. The directive is attached
to subsequent modules, integer `#` delays are scaled by its time unit, and
`auto` considers the finest attached precision as well as explicit HDL delay
units. Bounded integer delays are then converted exactly to global ticks;
inexact, overflowing, malformed, or coarser-than-unit precisions are rejected.
Fractional SV delays and `timeunit`/`timeprecision` declarations, rounding at a
declared precision, and SystemC participation in automatic resolution
selection are not complete.

## Scheduler

Future events are grouped by timestamp. Work at one timestamp is processed in
four ordered queues:

1. active;
2. inactive;
3. update; and
4. postponed.

Tasks inside a phase have a stable semantic order and an insertion order.
Scheduling into an already completed phase defers the task to the next delta.
The runtime records recently changed signals and pending stable orders so a
`max_deltas` failure can identify the likely zero-time oscillation.

The four queues are the implementation spine for the more detailed
cross-language lattice in
[cross-language-semantics.md](cross-language-semantics.md). Language-specific
driver transactions, SystemC channel updates, net resolution, and all postponed
callbacks are not yet complete.

## SimIR

SimIR processes are explicit state machines. The current operation set includes:

- constant loads and signal reads;
- unary not and typed binary bitwise, addition, and equality operations;
- blocking writes, update-phase writes, and delayed writes;
- timed, dynamic-signal, and static-sensitivity waits;
- next-delta yields;
- jumps and branches;
- assertions; and
- process halt and simulation stop.

The deterministic simulation kernel owns process PCs and boundary scheduling.
Reference processes use interpreter-owned register frames; compiled processes
use caller-owned LLVM frames through the `ProcessExecutor` boundary. Both paths
report waits, yields, stop, and halt through the same kernel boundary handler.
Every simulation test added for a compiled operation should run through both
paths and compare output, final state, assertions, and trace events.

## LLVM boundary and native cache

Supported compiled builds use LLVM 22.1.8, ORC, and LLJIT. The adapter public
header exposes no LLVM class. Generated functions receive a versioned C table
containing opaque context plus signal-read, signal-write, and assertion
callbacks. CMake requires the exact supported LLVM package when
`FSIM_LLVM_MODE=ON`; the checked-in Linux LLVM job builds and runs the adapter
suite against 22.1.8. A separate C11 test verifies that the runtime table is a
genuine C ABI.

The current adapter compiles control-flow graphs containing loads, reads,
common operations, blocking writes, assertions, jumps, branches, timed waits,
next-delta yields, design stop, and halt. A versioned caller-owned plain-C frame
holds the process PC plus separate `aval`/`bval` register planes; a versioned
result reports completion, assertion failure, timed-wait delay, yield, or stop.
The caller schedules the reported boundary and later resumes the same frame.
Loops are accepted when every invocation reaches a `WaitFor` or `Yield` safe
point; reachable zero-time cycles are rejected. `WriteUpdate`, `WriteAfter`,
`WaitOn`, `WaitSensitivity`, and values wider than 64 bits remain outside the
compiled subset. `LlvmJitUnsupportedError` identifies capability misses that
the application hybrid engine handles with per-process interpreter fallback.
Malformed SimIR, ABI mismatches, LLVM/cache failures, and generated-runtime
failures remain fatal `LlvmJitError`s. LLVM-enabled `fsim build` and `fsim run`
install compiled executors for eligible processes; builds without LLVM remain
interpreter-only. LLVM-enabled application tests compare a bounded
SystemVerilog hierarchy through reference and hybrid execution at O0 and O2,
and the vertical SV-to-VHDL-to-SV hierarchy through reference and O2 hybrid
execution.

The persistent cache primitive provides process-aware per-key locking,
stale-owner recovery, checksummed entries, temporary-file plus atomic
replacement, corrupt-entry rejection, and safe replacement of an existing
entry. When explicitly given a cache directory, the LLVM adapter installs this
primitive through LLVM's ObjectCache hook. Its native-object key covers the
complete supported SimIR process, symbol, and IDs and widths of only the
signals referenced by that process, plus cache/runtime ABI schemas, exact LLVM
version, O0/O2 mode, target triple and data layout, and the detected host
CPU/features. An unrelated elaborated signal-width change therefore reuses the
process object, while a referenced signal ID or width change invalidates it.
Cached objects are parsed and checked for the expected architecture before
reuse; a rejected entry is recompiled and replaced. The frame and resume-result
ABI versions and structure sizes participate in the key and in frame-layout
identity. Cold, warm, corruption-recovery, SimIR/referenced-width invalidation,
and optimization-mode invalidation are tested at O0 and O2.

LLVM-enabled `fsim build` and `fsim run` select this cache beneath the
configured project cache as `llvm-native`. The adapter and application expose
hit, miss, store, rejected-entry, and load/store-failure counters; `fsim build`
reports the principal counters. Application tests require a cold miss and
store for every compiled process followed by a warm hit with no misses or
cache failures at both O0 and O2. This remains a per-process adapter cache
rather than the required per-specialization cache with explicit source/include
identity, has no age/size eviction policy, and O0 does not yet provide the full
debug metadata or statement/call safe-point model. The application analysis
cache remains separate.

## Debug and public API

`include/fsim/api.h` defines opaque 64-bit session/object handles, versioned
structures, diagnostic/status returns, hierarchy and value operations,
run/step/stop calls, and synchronous callbacks. It deliberately exposes no C++
layout and no exception may cross it. Sessions can currently load, check, and
build projects; enumerate the bounded signal/process object view; look up
hierarchical signal paths; read, deposit, force, and release values; run; step
by delta or time; request stop; and receive lifecycle, safe-point, and
value-change callbacks. Object handles carry a build generation so a rebuild
invalidates stale hierarchy handles, and mutating/rebuilding re-entry from a
synchronous callback is rejected. Statement/process stepping, scope objects,
locals, and complete assertion/source metadata are not yet wired.

Optimized `run` and instrumented `debug` are required to have identical
simulation semantics. Debug code will use addressable process frames and safe
points at statements, waits, calls, process boundaries, assertion failures,
delta boundaries, and time boundaries. The default LLVM-enabled `run` path is
the O2 hybrid engine. The current `debug` path deliberately remains
interpreter-only, so instrumented O0 JIT debugging and run/debug equivalence
are still release-gate work. The current REPL implements
`continue`/relative `run`, `run-until`, delta/time stepping, time and
signal-change breakpoints with list/delete/clear operations, hierarchy/scope
navigation, signal examination, and deposit/force/release. A design `$finish`
marks the simulation finished, an external stop may be resumed, and a fatal
runtime exception poisons the simulation so later execution commands are
refused. Ctrl-C only sets an atomic stop request; the simulation thread observes
it at a safe point. Source breakpoints, statement/process stepping, locals, and
trace selection remain planned.

## Platform boundary

The supported release targets are Linux x86-64 with GCC and Windows x86-64 with
MSVC. Filesystem, dynamic-library loading, process invocation, Unicode path
handling, and signal/console interruption stay behind platform-specific
boundaries. SystemC source compilation passes argument arrays directly to the
selected GCC-like or MSVC toolchain and never invokes a shell. The current
compiler component produces checksummed, content-keyed shared libraries with
per-key locking, and project builds invoke it for SystemC source sets.
GCC-like builds use compiler-emitted dependency files and content-hash the
complete reported closure, including implicit system headers. MSVC uses a
conservative manifest-root scan in this slice. Source content and path-addressed
linked inputs also participate in the key; options or inputs whose dependency
closure cannot be proved make a build non-cacheable. GCC-like tracked inputs
using `__DATE__`, `__TIME__`, or `__TIMESTAMP__` are likewise non-cacheable.
The compiler recomputes the plan and key after compilation and discards an
output when a tracked input changed before publication. A project build loads
the resulting library, checks `fsim_plugin_init_v1`, contains initialization
exceptions, and requires at least one valid factory registration. Instantiating
those factories into a common SystemC elaboration/kernel remains planned.

This cache boundary does not yet fingerprint every helper behind the selected
compiler driver or every environment-injected code-generation setting. The
MSVC fallback also does not consume `/sourceDependencies`, so extensions such
as `#pragma include_alias` are outside its proven dependency model.
