<!-- SPDX-License-Identifier: Apache-2.0 -->
# Language support

## Reading this document

fsim targets VHDL-2008, Verilog-2005, SystemVerilog-2017, and a documented
IEEE 1666-2023-inspired SystemC subset. The repository is presently an
architecture vertical slice. “Parsed” below does not necessarily mean complete
legality checking or executable lowering.

Unsupported constructs must produce targeted diagnostics. They must never be
silently discarded.

## Current executable frontend slice

| Area | Parsed now | Executable now | Important limitations |
|---|---|---|---|
| VHDL units | `library`/`use`/context-reference clauses retained on their following unit; entities, architectures, ports, signals, bounded packed process variables, direct-entity and component-style instances | Recursively elaborated simple instances with positional/named whole-signal `port map` associations and persistent process-variable registers | Context declarations and visibility resolution are not implemented; no packages, generics or `generic map`, configurations, generates, expression/`open` actuals, or process declarative items beyond bounded variables |
| VHDL statements | Concurrent assignment, process sensitivity lists, `if`/`elsif`/`else`, signal/variable assignment, `null`, `after`, top-level `wait for`, top-level `wait on`, and `assert` with an optional literal `report` and standard severity | Whole and constant selected packed signal/local assignments, simple conditions, edge-guarded clock processes, repeating processes with bounded timed/any-change waits, scalar assertions, integer/logic/vector literals, selected operations | Every false assertion currently stops simulation regardless of severity; no bare/`until`/combined or conditionally nested waits, case/loop, aggregates, dynamic selected writes, nested selected targets or local scopes, general report expressions/statements, configurable assertion stop levels, inertial/transport/reject semantics, or complete LRM physical-time semantics; bounded integer time units are normalized exactly |
| VHDL expressions | Identifiers, decimal/logic/string literals, calls, index/slice syntax, common unary/binary syntax | Identifiers/literals, not, and/or/xor, unsigned add, equality, constant in-range indexed names/slices with declared-range mapping, and width-summing packed `&` concatenation | Dynamic/out-of-range indexed values, most calls/operators, aggregates, qualified expressions, and complete overload/self-determined sizing are not lowered |
| Verilog/SV units | Modules, ANSI and basic non-ANSI ports, nets/variables, packed constant ranges, module instances | Recursive simple hierarchy with named or positional whole-signal connections | No parameters/overrides, interfaces, packages, generates, expression actuals, or unpacked arrays |
| Verilog/SV statements | `assign`, event-controlled `always`/`always_ff`, inferred `always @*`/`always_comb`/`always_latch`, `initial`, blocks, leading packed procedural variables, `if`/`else`, exact `case` with comma-separated choices and `default`, blocking/NBA assignments, integer delays, any-change/`posedge`/`negedge`/wildcard procedural controls, `$finish`, and bounded immediate assertions with an optional literal `$error` message | Whole and constant selected packed signal/local assignments, deterministic simple-expression wildcard dependencies, time-zero `always_comb`/`always_latch`, ordered exact four-state case matching, dynamic any-change and scalar edge-filtered suspension, scalar immediate assertions; integer `#` delays inherit and scale by the active `` `timescale`` | Wildcard inference does not inspect function/task bodies; no fractional delays, loops, `casez`/`casex`/`case inside`, `unique`/`unique0`/`priority` case qualifiers, dynamic or indexed part-select assignment targets, nested selected targets/scopes, automatic variables, tasks/functions, fork, general expression controls, named events, files, dynamic data, assertion action blocks beyond the bounded `$error` form, or procedural force |
| Verilog/SV expressions | Identifiers, sized literals, strings, unary and common binary syntax, conditional (`?:`), index/part-select/concatenation syntax, call syntax | Identifiers/literals, unary plus/minus, bitwise complement (`~`), vector-aware logical negation (`!`), mixed-width logical and/or, unary and/or/xor reductions, bitwise and/or/xor, logical left/right shifts, fixed-width unsigned add/subtract/multiply/divide/remainder, equality/inequality, unsigned relational comparisons, equal-width conditional alternatives under a scalar condition with four-state bit merging, constant in-range bit/part selects with declared-range mapping, and packed concatenations of statically sized operands | Signed arithmetic/comparison, arithmetic shifts, exponentiation, dynamic/out-of-range selects, indexed part-selects, replication concatenation, and call forms are not lowered; conditional vector truth conversion, expression side effects/short-circuit observation, and full self-determined sizing remain pending |
| Directive context | Legal `` `timescale <unit>/<precision>`` forms with magnitudes `1`, `10`, or `100` are associated with subsequent modules; `` `default_nettype`` is recognized | Integer delays are scaled and `auto` selects the finest attached precision | Fractional delays, `timeunit`/`timeprecision` declarations, general preprocessing, and implicit-net/default-nettype semantics are not implemented; `` `default_nettype`` is a targeted error |
| SystemC | C++ compatibility header and versioned plug-in entry point | Header-local values/signals, dynamic loading, shell-free cached shared-library compilation, and build-time ABI/factory validation | Registered factories are not yet instantiated in the common hierarchy; no process kernel or Boost.Context suspension |

VHDL identifiers are canonicalized case-insensitively. Verilog and
SystemVerilog identifiers remain case-sensitive. VHDL nine-state scalar and
vector literals are accepted by executable lowering and collapse into the
current common four-state representation using the documented mixed-language
mapping. Preserving the full nine-state domain through signals, resolution, and
all VHDL operations is still in progress.

For the bounded hierarchy slice, child ports alias parent signal IDs after
width, signedness, and lossy-2-state checks. Same-language lookup and explicit
VHDL/SV manifest overrides are implemented. Automated runtime evidence covers
both hierarchy directions: an SV top driving a VHDL counter and an SV child,
plus a VHDL top driving a bound SV combinational child. This does not yet
establish parameters/generics, general vector-direction conversion,
aggregates/interfaces, runtime SystemC factory instantiation, or resolved
multi-driver behavior.

## v1 target

### VHDL-2008

Required for v1:

- entities, architectures, configurations, packages and bodies, contexts, and
  libraries;
- generics, ports, components/direct instantiation, blocks, and generates;
- the complete synthesizable sequential and concurrent statement set;
- arrays, records, access and protected types;
- overload and resolution rules, attributes, files and TextIO;
- waits, assertions and reports;
- inertial, transport, and reject delays; and
- reviewed Apache-2.0 IEEE logic, numeric, fixed, and floating-point packages.

Deferred beyond v1: PSL, VHPI, VHDL-AMS, VITAL/SDF timing, and proprietary
package or pragma semantics.

### Verilog-2005 and SystemVerilog-2017

Required for v1:

- the preprocessor, modules, interfaces/modports, packages, parameters, and
  generates;
- nets, variables, packed and unpacked types, structs/unions/enums, and
  memories;
- gate primitives, continuous/procedural assignments, and all `always` forms;
- functions/tasks, `initial`/`final`, delays/events, fork/join, and named
  events;
- strings, files, dynamic/associative arrays and queues;
- basic deterministic random functions, `$readmem*`, display/stop tasks; and
- immediate assertions.

Deferred beyond v1: classes, constraints and UVM; concurrent SVA; covergroups;
DPI/VPI; program and clocking blocks; UDPs; specify/timing checks; strengths;
and SDF.

### SystemC

The SystemC v1 target is the signal-level subset described in
[systemc-subset.md](systemc-subset.md). Arbitrary ordinary C++ may run inside
registered callbacks, but TLM, AMS, CCI, dynamic processes, arbitrary custom
primitive channels, and Accellera kernel/ABI compatibility are deferred.

## Release evidence

The promise above becomes v1 only when a checked-in feature matrix maps every
required construct to positive, negative, elaboration, and runtime tests.
Every semantic simulation test must run through both the SimIR interpreter and
LLVM JIT with identical final values, assertions, scheduling observations, and
trace changes on Ubuntu x86-64/GCC and Windows x86-64/MSVC.
