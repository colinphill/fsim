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
| VHDL statements | Concurrent assignments, labeled/unlabeled `with`/`select` selected signal assignments and concurrent assertions, process sensitivity lists, `if`/`elsif`/`else`, ordered packed `case`/`when`/`others`, locally static sequential `for`, Boolean `while`, unconditional and labeled loops, conditional/targeted `exit` and `next`, simple and chained VHDL-2008 conditional signal/variable assignments, signal/variable assignment, `null`, `after`, top-level bare/`on`/`until`/`for` wait clauses in every legal combination, and sequential `assert` with an optional literal `report` and standard severity | Whole and constant selected packed signal/local assignments, source-ordered Boolean conditional-assignment alternatives, selected concurrent assignments with grouped exact choices, final `others`, one waveform value and optional delay per alternative, and inferred reactive sensitivity; nested Boolean-typed conditional branches, exact `|`-separated case choices with nested statement lists, ascending/descending/null `for` ranges with implicit-constant substitution and bounded unrolling, executable loop backedges with persistent locals, nested/named loop transfers, edge-guarded clock processes, first-suspending condition waits, event-or-timeout races with preserved absolute deadlines, Boolean assertions with nonfailure severities continuing, reactive concurrent-assertion sensitivity, integer/logic/vector/Boolean literals, and selected operations | Conditional assignment alternatives do not yet carry individual waveform delays or `unaffected`; bounded selected assignments require a final `others` and do not support matching-select `?`, multiple waveform elements, or `unaffected`; no conditionally nested waits, case ranges/aggregates, dynamic selected writes, nested selected targets or local scopes, general report expressions/statements, configurable assertion stop levels, inertial/transport/reject semantics, or complete LRM physical-time semantics; bounded integer time units are normalized exactly |
| VHDL expressions | Identifiers, decimal/logic/string/Boolean literals, calls, index/slice syntax, common unary/binary syntax | Identifiers/literals, unary plus/minus, bounded packed signed `abs`, Boolean and packed `not`/`and`/`or`/`xor`, Boolean `nand`/`nor`/`xnor`, equal-width signed/unsigned `+`, `-`, `*`, `/`, `rem`, `mod`, single-factor packed `**` with a locally static nonnegative exponent, equality/inequality/relational comparisons, packed `sll`/`srl`/`sla`/`sra` and `rol`/`ror` with locally static integer counts and negative-count reversal, constant in-range indexed names/slices with declared-range mapping, and width-summing packed `&` concatenation | Scalar integer `abs`, dynamic/negative packed exponents, dynamic shift counts, dynamic/out-of-range indexed values, most calls/operators, aggregates, qualified expressions, and complete overload/self-determined sizing are not lowered; explicitly mixed signed/unsigned numeric operands require conversion |
| Verilog/SV units | Modules; bounded packages containing integral parameters/localparams, packed integral typedef aliases, packed enums, non-nested packed structs, equal-width packed unions, and imports; compilation-unit or unit-local wildcard/selected imports; ANSI and basic non-ANSI ports, nets/variables, packed constant or parameterized ranges, integral value parameters/localparams, module instances with named or positional parameter overrides, explicit or implicit conditional/inline-or-module-`genvar` iterative/constant-choice generates, and direct or named static contents in explicit generate regions containing bounded parameters/localparams, local packed signals, continuous assignments, processes, instances, and nested regions | Recursive case-sensitive package imports, `package::constant` folding, imported/scoped/local typedef resolution, enum enumerator visibility, declaration-order packed-struct layout, and offset-zero packed-union overlay layout for parameters, ports, signals, generated signals, and procedural locals with precise source provenance; explicit/implicit enum values are checked for packed base fit/uniqueness; whole packed aggregates, constant member reads/writes, and one-level constant member bit/part-selects with specialization-folded parameter or package-constant bounds execute through common extract/insert/sliced-write SimIR operations; recursive hierarchy with per-instance integral constant specialization, declaration-ordered generated-parameter folding, specialization-selected or always-selected generated behavior with scope-qualified locals, loop-variable substitution, and named or positional whole-signal connections | Nested structs/aggregates, unequal-width or tagged unions, unpacked members, member initializers, anonymous structs/enums, interfaces, classes, subprograms, and export package items are not implemented; dynamic or recursively chained member selects and nominal struct/union/enum assignment/cast legality are not yet enforced; bounded aliases ultimately resolve to `bit`, `logic`, `reg`, `int`, or `integer`, while enum bases and aggregate members use packed `bit`/`logic`/`reg`; integer-domain signals remain non-executable; package names resolve within the owning manifest library; no type/string parameters, interfaces, noncanonical generate-loop updates, generated type parameters/functions/tasks, expression port actuals, or unpacked arrays |
| Verilog/SV statements | `assign`, bounded scalar built-in gate primitives, event-controlled `always`/`always_ff`, inferred `always @*`/`always_comb`/`always_latch`, `initial`, SystemVerilog `final`, blocks, leading packed procedural variables, `if`/`else`, `case`/`casez`/`casex` with comma-separated choices and `default`, canonical bounded procedural `for`, locally static `repeat`, runtime `while`/`do-while`, timing-controlled `forever`, nested `break`/`continue`, blocking/NBA assignments, SystemVerilog compound assignments and standalone prefix/postfix increment/decrement, integer delays, any-change/`posedge`/`negedge`/wildcard procedural controls, condition waits, `$stop`, `$finish`, standalone `$info`/`$warning`/`$error`/`$fatal`, and immediate assertions with simple or lexical-block pass/failure actions | Whole, constant bit/part-selected, and constant `+:`/`-:` indexed-selected packed signal/local assignments; SystemVerilog arithmetic, bitwise, logical-shift, and arithmetic-shift compound assignments plus standalone `++`/`--` normalize to blocking read-modify-write behavior; `$stop` pauses before the following statement and resumes after the application clears the stop; severity tasks retain an optional bounded literal message, with note/warning/error continuing and `$fatal` accepting an optional ignored numeric finish control before terminating; final procedures execute exactly once after ordinary quiescence or `$finish` and may contain the supported nonsuspending blocking statement subset; comma-separated optionally named `buf`/`not`/`and`/`nand`/`or`/`nor`/`xor`/`xnor` primitives with an optional shared integer delay lower to independent common four-state continuous processes; inline `int`/`integer` procedural loops with locally static `<`/`<=`/`>`/`>=` bounds, matching unit updates, null ranges, and bounded deterministic unrolling; nonnegative locally static repeat counts; executable pre/post-test loop backedges with nested control transfers; timing-controlled `forever`; nested `if`/`else` with packed four-state truth conversion; deterministic wildcard dependencies; time-zero `always_comb`/`always_latch`; ordered exact and symmetric selector-or-choice wildcard case matching; dynamic event suspension and immediate-test condition waits; packed-condition immediate assertions with implicit error and scoped pass/failure actions; integer `#` delays inherit and scale by the active `` `timescale`` | Gate strengths, arrays, multiple delay values, and MOS/switch primitives are not implemented; `$stop` accepts but ignores its optional verbosity argument; final procedures reject timing controls, waits, `$stop`, `$finish`, and NBAs; wildcard inference does not inspect function/task bodies; no fractional delays, nonsuspending `forever`, dynamic or negative repeat counts, externally declared or non-unit-step procedural `for` indices, `case inside`, `unique`/`unique0`/`priority` case qualifiers, dynamic bit/part-select assignment targets, nested selected targets/scopes, automatic variables beyond the substituted loop index, compound-assignment delay controls, increment/decrement within larger expressions, tasks/functions, fork, general expression controls, named events, files, dynamic data, formatted/dynamic severity-task messages, or procedural force |
| Verilog/SV expressions | Identifiers, sized literals, strings, unary and common binary syntax, conditional (`?:`), index/part-select/concatenation syntax, call syntax | Identifiers/literals, unary plus/minus, bitwise complement (`~`), vector-aware logical negation (`!`), mixed-width logical and/or, unary and/or/xor reductions and their `~&`/`~|`/`~^`/`^~` complements, bitwise and/or/xor plus binary `~^`/`^~` XNOR, logical and arithmetic left/right shifts with signedness-sensitive four-state sign fill, left-associative fixed-width `**`, equal-width signed/unsigned add/subtract/multiply/divide/remainder and relational comparisons, equality/inequality with unknown propagation, exact known-result case equality/inequality (`===`/`!==`), right-operand-masked SystemVerilog wildcard equality/inequality (`==?`/`!=?`), equal-width conditional alternatives under a scalar condition with four-state bit merging, constant in-range bit/part selects, constant `+:`/`-:` indexed part-selects with declared-range mapping, packed concatenations, checked constant replication concatenations of statically sized operands, bounded nonnegative integral `$clog2` constant calls, width/bit-preserving `$signed`/`$unsigned` casts, packed `$isunknown`, `$onehot`, and `$onehot0`, signed 32-bit `$countones` and constant-control `$countbits`, 32-bit `$bits` results for statically sized packed expressions, signed 32-bit `$left`/`$right`/`$low`/`$high`/`$size`/`$increment` results with an optional constant packed dimension `1`, and packed-only `$dimensions`/`$unpacked_dimensions` | Dynamic/out-of-range selects, streaming concatenations, runtime calls beyond the bounded system functions, dynamic `$countbits` controls, queries over types or unpacked/dynamic/multidimensional objects, dimension arguments other than `1`, and arbitrary vector-valued/negative `$clog2` arguments are not lowered; conditional vector truth conversion, expression side effects/short-circuit observation, and full self-determined sizing remain pending |
| Preprocessing/directives | Quoted and angle includes, manifest/CLI definitions, object/function macros with default arguments, multiline replacement, argument substitution, token concatenation/stringification, `__FILE__`/`__LINE__`, `undef`, nested conditional compilation, logical `` `line`` source remapping, legal `` `timescale``, `` `default_nettype``, reset/cell/keyword-version/unconnected-drive state, and ordered `file`/`source-set`/`combined` policies | Included units and macro-selected executable source enter the normal frontend; active `` `line`` mappings reach parser diagnostics, macro ancestry, DesignIR/SimIR debug points, report callbacks, and LLVM objects while physical ownership remains in analysis/native cache provenance; mappings reset for includes and compilation-unit roots; source-set/combined roots otherwise share macro, conditional, and parser directive state while retaining library ownership; scalar implicit nets and default port net types honor `` `default_nettype``; cell metadata and omitted-input pulls reach DesignIR/runtime; integer delays are scaled and `auto` selects the finest attached precision; ordered snapshots participate in cache identity | Standardized pragma behavior, fractional delays, `timeunit`/`timeprecision`, multi-driver wired-net resolution, and complete trireg charge semantics remain incomplete; unsupported directives receive targeted errors |
| SystemC | C++ compatibility header, versioned plug-in entry point, typed factories, and peer mixed-language hierarchy | Common signals/ports/exports/events/channels, native and foreign children, lifecycle callbacks, `SC_METHOD`, and Boost.Context-backed `SC_THREAD`/`SC_CTHREAD` timed/event/static waits execute on the deterministic common kernel | Arbitrary custom-interface metadata, dynamic processes, thread reset/kill, TLM/AMS/CCI, and Accellera ABI compatibility remain unsupported |

SystemVerilog time status update: compilation-unit and leading module-local
`timeunit`/`timeprecision` declarations, including the combined
`timeunit value / value` form, now override inherited `` `timescale`` context.
Decimal/scientific delays retain exact bounded rational HIR, and explicit
`fs`/`ps`/`ns`/`us`/`ms`/`s` suffixes override the module unit. Delays round
to timeprecision before exact global-tick conversion, with half steps rounded
upward; `auto` considers declarations and explicit units. Delay triplets and
parameterized/nonconstant delay expressions remain incomplete. This update
supersedes the compact table's older fractional-delay and declaration-based
time limitations.

Named-event status update: Verilog-2005/SystemVerilog module-level `event`
declarations, comma groups, immediate `->` triggers, static `@event`, dynamic
`@(event)`, and repeated wakeups now execute. SystemVerilog `->>` publishes
through the common update phase and `->> #delay` publishes at a future
timestamp; event arguments and general event expressions remain deferred. This update
supersedes the older broad “named events” limitation in the compact table.

Display-task status update: Verilog-2005/SystemVerilog literal
`$display("text")`, `$display()`, and `$display` execute synchronously and
append a newline through the CLI or Tcl-owned output stream; literal/empty
`$write` uses the same path without appending a newline, while literal/empty
`$strobe` appends a newline in the current timestamp's postponed phase.
Interpreter, LLVM O0, and LLVM O2 preserve process/time/delta ordering. Format
substitutions, additional arguments, and value-sensitive `$monitor` behavior
remain deferred. This update supersedes the compact table's broader
display-task limitation.
Output string literals decode `\n`, `\t`, `\"`, `\\`, and one-to-three-digit
octal byte escapes. Unsupported or out-of-range escapes are diagnosed instead
of being silently rewritten.

VHDL report status update: literal `report "text";` statements at all four
standard severities execute through a severity/source-aware hook, including
VHDL doubled-quote decoding, native API callback delivery, and
interpreter/LLVM O0/O2 equivalence. Note, warning, and error continue;
failure publishes once and then terminates before any following statement.
General string expressions and a configurable stop threshold remain deferred.

Literal `$monitor("text")` and empty `$monitor` forms publish once in the
postponed phase. Value operands, formatting substitutions, monitor-list
replacement, `$monitoron`, and `$monitoroff` remain deferred.

A sole constant unsigned decimal, binary, octal, or hexadecimal output
argument is width-truncated and emitted in default decimal form. A based
literal marked signed is interpreted as two's-complement at its declared
width. Unknown-state literals, unformatted dynamic operands, and additional
operands remain deferred.

Dynamic output status update: `$display` and `$write` accept one `%b`, `%h`,
`%o`, `%d`, `%c`, or `%s`
conversion with one packed runtime expression, literal prefix/suffix text,
and `%%`. Binary output preserves full declared width and four-state bits;
hex output retains `ceil(width/4)` lowercase digits, preserving uniform X/Z
nibbles and mapping mixed known/unknown nibbles to `x`; octal uses the same
policy over `ceil(width/3)` digits. Decimal output handles
arbitrary packed widths, respects typed signedness through two's-complement,
and renders a value containing X/Z as `x`. Character output uses the
least-significant eight bits and renders an unknown byte as `x`.
Packed-string output emits bytes most-significant first, omits leading zero
padding, and renders an X/Z-containing byte as `x`. Additional arguments,
other conversions, general width/precision modifiers, and dynamic
`$monitor` remain targeted. `$strobe` accepts the same single
conversion/value form, captures
the formatted result when called, and publishes it in the postponed phase.
The `%0b`, `%0h`, and `%0o` forms suppress leading known-zero digits while
retaining at least one digit.

Literal `$info`, `$warning`, `$error`, and `$fatal` messages use the same
Verilog/SystemVerilog escape decoding as output tasks. They are valid as
standalone statements and immediate-assertion actions. Note, warning, and
error report once and continue; failure reports once and terminates.

The VHDL expression slice also executes one-dimensional packed-object
`'left`, `'right`, `'low`, `'high`, `'length`, and `'ascending` attributes.
An optional dimension must be the constant `1`; declared `to`/`downto`
direction is preserved. A visible signal may use the dynamic `'event`
attribute; its Boolean result is true only in the delta cycle containing that
signal's committed effective-value change. The same signal may use
`'last_value` to read its packed effective value immediately before the latest
value-changing event. `'last_event` returns elapsed global-resolution ticks
since that event, or `TIME'HIGH` if the signal has never changed. The
zero-duration form of `'stable` is false in the signal's event delta and true
otherwise; explicit duration arguments are not yet lowered. `'active` is true
for any committed signal transaction in the current delta, including a
same-value transaction for which `'event` remains false.

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
- deterministic `$random`, `$urandom`, and `$urandom_range` streams seeded
  per stable process ID, `$readmem*`, display/stop tasks; and
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
