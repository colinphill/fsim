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
| VHDL units | `library`/`use`/context-reference clauses retained on their following unit; reusable context declarations containing bounded context items; constant-only package declarations; entities, architectures, scalar integer/Boolean/bit generics, parameterized packed ranges, ports, signals, bounded packed process variables, direct-entity and component-style instances with positional-then-named `generic map` actuals, labeled `if`/`else`, integer-range `for`, scalar/inclusive-range-choice `case` generate regions, and unguarded labeled block statements containing bounded constants, local packed signals, concurrent assignments, processes, instances, and nested regions | Recursive acyclic `context library.name` expansion; explicit use visibility and direct `package.constant`/`library.package.constant` references for declaration-ordered scalar integer/Boolean/bit project-package constants, including package-to-package imports with precise transitive context/package source provenance; recursively elaborated per-occurrence generic specializations with named/positional whole-signal `port map` associations, declaration-ordered generated-constant folding, interval-based selection with null-range handling, specialization-selected or always-selected scoped behavior with scope-qualified locals, and persistent process-variable registers | Package bodies and package types/subprograms, standard-library context/package loading, record/other non-package selected names, and general visibility/overload resolution are not implemented; package/context visibility cycles are rejected; no configurations, general generic types, guarded blocks, nonintegral case-generate choices, generated subprograms/types or other declarative items, expression/`open` port actuals, or process declarative items beyond bounded variables |
| VHDL statements | Concurrent assignment, process sensitivity lists, `if`/`elsif`/`else`, signal/variable assignment, `null`, `after`, top-level `wait for`, top-level `wait on`, and `assert` with an optional literal `report` and standard severity | Whole and constant selected packed signal/local assignments, nested Boolean-typed conditional branches, edge-guarded clock processes, repeating processes with bounded timed/any-change waits, Boolean assertions, integer/logic/vector/Boolean literals, selected operations | Every false assertion currently stops simulation regardless of severity; no bare/`until`/combined or conditionally nested waits, case/loop, aggregates, dynamic selected writes, nested selected targets or local scopes, general report expressions/statements, configurable assertion stop levels, inertial/transport/reject semantics, or complete LRM physical-time semantics; bounded integer time units are normalized exactly |
| VHDL expressions | Identifiers, decimal/logic/string/Boolean literals, calls, index/slice syntax, common unary/binary syntax | Identifiers/literals, unary plus/minus, Boolean and packed `not`/`and`/`or`/`xor`, Boolean `nand`/`nor`/`xnor`, equal-width signed/unsigned `+`, `-`, `*`, `/`, `rem`, `mod`, equality/inequality/relational comparisons, constant in-range indexed names/slices with declared-range mapping, and width-summing packed `&` concatenation | Dynamic/out-of-range indexed values, most calls/operators, aggregates, qualified expressions, and complete overload/self-determined sizing are not lowered; explicitly mixed signed/unsigned numeric operands require conversion |
| Verilog/SV units | Modules; bounded packages containing integral parameters/localparams, packed integral typedef aliases, packed enums, non-nested packed structs, equal-width packed unions, and imports; compilation-unit or unit-local wildcard/selected imports; ANSI and basic non-ANSI ports, nets/variables, packed constant or parameterized ranges, integral value parameters/localparams, module instances with named or positional parameter overrides, explicit or implicit conditional/inline-or-module-`genvar` iterative/constant-choice generates, and direct or named static contents in explicit generate regions containing bounded parameters/localparams, local packed signals, continuous assignments, processes, instances, and nested regions | Recursive case-sensitive package imports, `package::constant` folding, imported/scoped/local typedef resolution, enum enumerator visibility, declaration-order packed-struct layout, and offset-zero packed-union overlay layout for parameters, ports, signals, generated signals, and procedural locals with precise source provenance; explicit/implicit enum values are checked for packed base fit/uniqueness; whole packed aggregates, constant member reads/writes, and one-level constant member bit/part-selects with specialization-folded parameter or package-constant bounds execute through common extract/insert/sliced-write SimIR operations; recursive hierarchy with per-instance integral constant specialization, declaration-ordered generated-parameter folding, specialization-selected or always-selected generated behavior with scope-qualified locals, loop-variable substitution, and named or positional whole-signal connections | Nested structs/aggregates, unequal-width or tagged unions, unpacked members, member initializers, anonymous structs/enums, interfaces, classes, subprograms, and export package items are not implemented; dynamic or recursively chained member selects and nominal struct/union/enum assignment/cast legality are not yet enforced; bounded aliases ultimately resolve to `bit`, `logic`, `reg`, `int`, or `integer`, while enum bases and aggregate members use packed `bit`/`logic`/`reg`; integer-domain signals remain non-executable; package names resolve within the owning manifest library; no type/string parameters, interfaces, noncanonical generate-loop updates, generated type parameters/functions/tasks, expression port actuals, or unpacked arrays |
| Verilog/SV statements | `assign`, event-controlled `always`/`always_ff`, inferred `always @*`/`always_comb`/`always_latch`, `initial`, blocks, leading packed procedural variables, `if`/`else`, exact `case` with comma-separated choices and `default`, blocking/NBA assignments, integer delays, any-change/`posedge`/`negedge`/wildcard procedural controls, `$finish`, and bounded immediate assertions with an optional literal `$error` message | Whole, constant bit/part-selected, and constant `+:`/`-:` indexed-selected packed signal/local assignments, nested `if`/`else` with packed four-state truth conversion, deterministic simple-expression wildcard dependencies, time-zero `always_comb`/`always_latch`, ordered exact four-state case matching, dynamic any-change and scalar edge-filtered suspension, packed-condition immediate assertions; integer `#` delays inherit and scale by the active `` `timescale`` | Wildcard inference does not inspect function/task bodies; no fractional delays, loops, `casez`/`casex`/`case inside`, `unique`/`unique0`/`priority` case qualifiers, dynamic bit/part-select assignment targets, nested selected targets/scopes, automatic variables, tasks/functions, fork, general expression controls, named events, files, dynamic data, assertion action blocks beyond the bounded `$error` form, or procedural force |
| Verilog/SV expressions | Identifiers, sized literals, strings, unary and common binary syntax, conditional (`?:`), index/part-select/concatenation syntax, call syntax | Identifiers/literals, unary plus/minus, bitwise complement (`~`), vector-aware logical negation (`!`), mixed-width logical and/or, unary and/or/xor reductions, bitwise and/or/xor, logical left/right shifts, equal-width signed/unsigned add/subtract/multiply/divide/remainder and relational comparisons, equality/inequality, equal-width conditional alternatives under a scalar condition with four-state bit merging, constant in-range bit/part selects, constant `+:`/`-:` indexed part-selects with declared-range mapping, and packed concatenations of statically sized operands | Arithmetic shifts, exponentiation, dynamic/out-of-range selects, replication concatenation, and call forms are not lowered; conditional vector truth conversion, expression side effects/short-circuit observation, and full self-determined sizing remain pending |
| Preprocessing/directives | Quoted and angle includes, manifest/CLI definitions, object/function macros with default arguments, multiline replacement, argument substitution, token concatenation/stringification, `__FILE__`/`__LINE__`, `undef`, nested conditional compilation, legal `` `timescale``, `` `default_nettype``, reset/cell/keyword-version/unconnected-drive state, and ordered `file`/`source-set`/`combined` policies | Included units and macro-selected executable source enter the normal frontend; source-set/combined roots share macro, conditional, and parser directive state while retaining library ownership; scalar implicit nets and default port net types honor `` `default_nettype``; cell metadata and omitted-input pulls reach DesignIR/runtime; integer delays are scaled and `auto` selects the finest attached precision; ordered snapshots participate in cache identity | `` `line`` remapping, standardized pragma behavior, fractional delays, `timeunit`/`timeprecision`, multi-driver wired-net resolution, and complete trireg charge semantics remain incomplete; unsupported directives receive targeted errors |
| SystemC | C++ compatibility header, versioned plug-in entry point, typed factories, and peer mixed-language hierarchy | Common signals/ports/exports/events/channels, native and foreign children, lifecycle callbacks, `SC_METHOD`, and Boost.Context-backed `SC_THREAD`/`SC_CTHREAD` timed/event/static waits execute on the deterministic common kernel | Arbitrary custom-interface metadata, dynamic processes, thread reset/kill, TLM/AMS/CCI, and Accellera ABI compatibility remain unsupported |

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
plus a VHDL top driving a bound SV combinational child. Bounded scalar
generic/parameter actuals cross explicit VHDL/SV bindings in either direction
before boundary-width checks; positional actuals map by ordinal, and
VHDL-associated names use case-insensitive target matching with ambiguity
diagnostics for case-distinct SV declarations. Selected conditional-generate
labels are retained in binding paths, so a generated child may cross into
VHDL, SystemVerilog, or SystemC under the same explicit path rules. This does
the same for loop-generated children using deterministic, language-neutral
`label[index]` path components. Case-generated children use their declared
alternative label. This does not yet establish
complete VHDL generic or SystemVerilog parameter typing and sizing, SystemC
construction schemas, general vector-direction conversion,
aggregates/interfaces, or resolved multi-driver behavior. SystemC factories
already elaborate as peer hierarchy nodes in either direction through
explicit bindings.

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
registered callbacks. Module-local typed `sc_signal` objects already use the
common kernel, and typed bindings from module ports to those signals share the
same DesignIR object across HDL/SystemC boundaries. Constructor-time native
SystemC child modules elaborate recursively, including direct bindings from
child ports to parent signals or ports. Module lifecycle callbacks execute at
the common build/start/terminal boundaries. Standard signal input/inout
interfaces and typed exports may chain before binding a child port, retaining
explicit hierarchy metadata and one common signal identity. General custom
interface metadata and broader channel behavior remain v1 targets. TLM, AMS,
CCI, dynamic processes, arbitrary custom primitive-channel
interfaces/binding beyond the bounded registered `sc_prim_channel` update
callback, and Accellera kernel/ABI compatibility are deferred.

## Release evidence

The promise above becomes v1 only when a checked-in feature matrix maps every
required construct to positive, negative, elaboration, and runtime tests.
Every semantic simulation test must run through both the SimIR interpreter and
LLVM JIT with identical final values, assertions, scheduling observations, and
trace changes on Ubuntu x86-64/GCC and Windows x86-64/MSVC.
